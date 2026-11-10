/*
 * events.c - Live stream of process lifecycle events from the kernel.
 *
 *  ┌──────────────────────────── events(all)[812] ─────────────────────────────┐
 *  │ TIME          TYPE    PID   PPID  NAME          DETAIL                    │
 *  │ 21:40:12.123  exit   4211   4100  sh            exit 3                    │ newest
 *  │ 21:40:12.120  exec   4211   4100  sh                                      │   on
 *  │ 21:40:12.118  fork   4211   4100  bash          child of bash             │  top
 *  │ ...                                                                       │
 *  │ fork 12.0/s   exec 11.5/s   exit 12.0/s          dropped 0  missed 0      │
 *  └───────────────────────────────────────────────────────────────────────────┘
 *
 * Data comes from app->events (accumulated by source_read_events()).
 * Nothing here talks to the kernel.
 */
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "view.h"
#include "../model.h"
#include "../table.h"
#include "../theme.h"

static const char *const aliases[] = { "events", "event", "ev", "e", NULL };

static const struct key_hint hints[] = {
	{ "space", "Pause"      },
	{ "c",     "Clear"      },
	{ "f",     "Fork on/off" },
	{ "x",     "Exec on/off" },
	{ "z",     "Exit on/off" },
	{ "enter", "Describe"   },
};

#define RATE_WINDOW_NS (10ULL * 1000000000ULL)

/* visible events, newest first, as indexes into app->events.items */
static size_t *rows;
static size_t  nrows, rows_cap;

enum { EC_TIME, EC_TYPE, EC_PID, EC_PPID, EC_NAME, EC_DETAIL, NEC };
static const struct table_col cols[NEC] = {
	[EC_TIME]   = { "TIME",  12, ALIGN_LEFT  },
	[EC_TYPE]   = { "TYPE",   4, ALIGN_LEFT  },
	[EC_PID]    = { "PID",    7, ALIGN_RIGHT },
	[EC_PPID]   = { "PPID",   7, ALIGN_RIGHT },
	[EC_NAME]   = { "NAME",  16, ALIGN_LEFT  },
	[EC_DETAIL] = { "DETAIL", 0, ALIGN_LEFT  },
};

/* ------------------------------------------------------------------ */

/* wait(2)-style status -> "exit 3", "killed SIGKILL", "killed SIGSEGV (core)" */
static void fmt_exit(char *buf, size_t len, int status)
{
	int sig = status & 0x7f;

	if (sig == 0) {
		snprintf(buf, len, "exit %d", (status >> 8) & 0xff);
	} else {
		const char *name = sigabbrev_np(sig);

		snprintf(buf, len, "killed SIG%s%s", name ? name : "?",
			 status & 0x80 ? " (core dumped)" : "");
	}
}

/* boottime ns -> local wall clock "HH:MM:SS.mmm" */
static void fmt_ts(char *buf, size_t len, uint64_t ts_ns)
{
	struct timespec boot, real;
	uint64_t boot_ns, real_ns, ev_ns;
	time_t sec;
	struct tm tm;

	clock_gettime(CLOCK_BOOTTIME, &boot);
	clock_gettime(CLOCK_REALTIME, &real);
	boot_ns = (uint64_t)boot.tv_sec * 1000000000ULL + (uint64_t)boot.tv_nsec;
	real_ns = (uint64_t)real.tv_sec * 1000000000ULL + (uint64_t)real.tv_nsec;
	ev_ns = real_ns - (boot_ns > ts_ns ? boot_ns - ts_ns : 0);
	sec = (time_t)(ev_ns / 1000000000ULL);
	localtime_r(&sec, &tm);
	snprintf(buf, len, "%02d:%02d:%02d.%03llu", tm.tm_hour, tm.tm_min, tm.tm_sec,
		 (unsigned long long)(ev_ns / 1000000ULL % 1000));
}

static bool ev_matches(const struct app *app, const struct proc_event *e)
{
	char pidbuf[16];

	if (app->ev_hide & (1u << e->type))
		return false;
	if (e->seq <= app->ev_clear_seq)
		return false;
	if (app->ev_paused && e->seq > app->ev_pause_seq)
		return false;
	if (!app->filter[0])
		return true;
	if (strcasestr(e->comm, app->filter) ||
	    strcasestr(pm_event_type_name(e->type), app->filter))
		return true;
	snprintf(pidbuf, sizeof(pidbuf), "%d", e->pid);
	return strstr(pidbuf, app->filter) != NULL;
}

static void collect(struct app *app)
{
	const struct event_log *log = &app->events;

	nrows = 0;
	if (rows_cap < log->len) {
		size_t *n = realloc(rows, log->len * sizeof(*n));

		if (!n)
			return;
		rows = n;
		rows_cap = log->len;
	}
	for (size_t i = log->len; i-- > 0;)          /* newest first */
		if (ev_matches(app, &log->items[i]))
			rows[nrows++] = i;
}

static void cell(void *ctx, size_t row, int col, char *buf, size_t len)
{
	struct app *app = ctx;
	const struct proc_event *e = &app->events.items[rows[row]];
	const struct proc_row *parent;

	switch (col) {
	case EC_TIME: fmt_ts(buf, len, e->ts_ns); break;
	case EC_TYPE: snprintf(buf, len, "%s", pm_event_type_name(e->type)); break;
	case EC_PID:  snprintf(buf, len, "%d", e->pid); break;
	case EC_PPID: snprintf(buf, len, "%d", e->ppid); break;
	case EC_NAME: snprintf(buf, len, "%s", e->comm); break;
	case EC_DETAIL:
		if (e->type == PM_EV_EXIT) {
			fmt_exit(buf, len, e->exit_code);
		} else if (e->type == PM_EV_FORK) {
			parent = model_find(app, e->ppid, 0);
			snprintf(buf, len, "child of %s", parent ? parent->comm : "?");
		} else {
			snprintf(buf, len, "now running %s", e->comm);
		}
		break;
	}
}

static int style(void *ctx, size_t row)
{
	const struct proc_event *e = &((struct app *)ctx)->events.items[rows[row]];

	switch (e->type) {
	case PM_EV_FORK: return CP_ROW_RUNNING;
	case PM_EV_EXEC: return CP_ROW;
	case PM_EV_EXIT:
		if (e->exit_code & 0x7f)              /* killed by a signal */
			return CP_ROW_ZOMBIE;
		return e->exit_code ? CP_ROW_WARN : CP_ROW_DIM;
	}
	return CP_ROW;
}

/* fork/exec/exit per second over the last 10 s, plus loss counters */
static void draw_stats(const struct app *app, int y, int x, int w)
{
	const struct event_log *log = &app->events;
	uint64_t newest = log->len ? log->items[log->len - 1].ts_ns : 0;
	unsigned count[4] = { 0 };
	static const int type_pair[4] = { 0, CP_ROW_RUNNING, CP_ROW, CP_ROW_DIM };

	for (size_t i = log->len; i-- > 0;) {
		const struct proc_event *e = &log->items[i];

		if (newest - e->ts_ns > RATE_WINDOW_NS)
			break;
		if (e->type >= PM_EV_FORK && e->type <= PM_EV_EXIT)
			count[e->type]++;
	}
	move(y, x);
	for (int t = PM_EV_FORK; t <= PM_EV_EXIT; t++) {
		bool hidden = app->ev_hide & (1u << t);

		attron(COLOR_PAIR(type_pair[t]) | (hidden ? A_DIM : A_BOLD));
		printw("%s", pm_event_type_name(t));
		attroff(COLOR_PAIR(type_pair[t]) | (hidden ? A_DIM : A_BOLD));
		attron(COLOR_PAIR(CP_KEY_DESC));
		printw(hidden ? " (hidden)   " : " %5.1f/s   ", count[t] / 10.0);
		attroff(COLOR_PAIR(CP_KEY_DESC));
	}
	attron(COLOR_PAIR(app->data.sum.events_dropped || log->missed
			  ? CP_ROW_WARN : CP_KEY_DESC));
	mvprintw(y, x + w - 42 > x + 60 ? x + w - 42 : x + 60,
		 "kernel dropped %llu   pmtop missed %llu",
		 (unsigned long long)app->data.sum.events_dropped,
		 (unsigned long long)log->missed);
	attroff(COLOR_PAIR(app->data.sum.events_dropped || log->missed
			   ? CP_ROW_WARN : CP_KEY_DESC));
}

static size_t draw(struct app *app, struct rect body)
{
	char title[48], accent[192];
	struct table_spec spec = {
		.cols = cols, .ncols = NEC, .cell = cell, .style = style,
		.ctx = app, .sort_col = -1,
	};

	collect(app);
	snprintf(title, sizeof(title), "events(%s)",
		 app->filter[0] || app->ev_hide ? "filtered" : "all");
	snprintf(accent, sizeof(accent), "[%zu]%s%s%s%s", nrows,
		 app->filter[0] ? " </" : "", app->filter, app->filter[0] ? ">" : "",
		 app->ev_paused ? " ⏸ paused" : "");
	ui_frame(body, title, accent);

	if (!app->data.module_loaded) {
		ui_center_text(body, 0, CP_FLASH_ERR, "kernel module not loaded");
		return 0;
	}
	draw_stats(app, body.y + body.h - 2, body.x + 2, body.w - 4);
	if (nrows == 0) {
		ui_center_text(body, 0, CP_KEY_DESC,
			       app->events.len ? "no events match (esc / c / f x z)"
					       : "waiting for events... (start any program)");
		return 0;
	}
	table_draw((struct rect){ body.y + 1, body.x + 1, body.h - 3, body.w - 2 },
		   &spec, &app->cursor[VIEW_EVENTS], &app->scrolls[VIEW_EVENTS]);
	return nrows;
}

static void toggle(struct app *app, int type)
{
	app->ev_hide ^= 1u << type;
	app->cursor[VIEW_EVENTS] = app->scrolls[VIEW_EVENTS] = 0;
}

static bool on_key(struct app *app, int ch)
{
	int cur = app->cursor[VIEW_EVENTS];
	const struct proc_event *e;
	const struct proc_row *p;

	switch (ch) {
	case ' ':
		app->ev_paused = !app->ev_paused;
		app->ev_pause_seq = app->events.last_seq;
		app_flash(app, false, app->ev_paused ? "paused (space resumes)" : "resumed");
		return true;
	case 'c':
		app->ev_clear_seq = app->events.last_seq;
		app->cursor[VIEW_EVENTS] = app->scrolls[VIEW_EVENTS] = 0;
		return true;
	case 'f': toggle(app, PM_EV_FORK); return true;
	case 'x': toggle(app, PM_EV_EXEC); return true;
	case 'z': toggle(app, PM_EV_EXIT); return true;
	case '\n':
	case KEY_ENTER:
		if (cur < 0 || (size_t)cur >= nrows)
			return true;
		e = &app->events.items[rows[cur]];
		p = model_find(app, e->pid, 0);
		if (p && p->start_ns > e->ts_ns)
			p = NULL;     /* pid reused by a process born after this event */
		if (p)
			detail_open(app, p);
		else
			app_flash(app, true, "pid %d is no longer running", e->pid);
		return true;
	}
	return false;
}

const struct view view_events = {
	.name = "events", .aliases = aliases,
	.hints = hints, .nhints = sizeof(hints) / sizeof(hints[0]),
	.is_list = true, .draw = draw, .on_key = on_key,
};
