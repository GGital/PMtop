/*
 * detail.c - "describe": one process in depth.
 *
 *  ┌──────────────────────── describe(1423)[firefox] ─────────────────────────┐
 *  │ Name      firefox                       ┌──────── children[3] ─────────┐ │
 *  │ PID       1423     PPID 1200 (gnome-sh) │  PID  NAME            CPU%   │ │
 *  │ User      student (1000)                │ 1450  Web Content     12.0   │ │
 *  │ State     R  running                    │ ...                          │ │
 *  │ ...                                     │                              │ │
 *  │ CPU   [████░░░░░░░░░░]  25%             │                              │ │
 *  │ MEM   [█░░░░░░░░░░░░░]   4%             │                              │ │
 *  │ CPU history (last 60s)                  │                              │ │
 *  │ ▁▁▂▃▅▇█▇▅▃▂▁▁▁▂▃                        └──────────────────────────────┘ │
 *  └──────────────────────────────────────────────────────────────────────────┘
 *
 * The described process is copied into app->target on every refresh,
 * so if it exits the view keeps its last values and says so.
 */
#include <ncurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "view.h"
#include "../format.h"
#include "../input.h"
#include "../model.h"
#include "../table.h"
#include "../theme.h"
#include "../users.h"

static const char *const aliases[] = { "describe", "d", NULL };

static const struct key_hint hints[] = {
	{ "enter",  "Desc child" },
	{ "p",      "Parent"     },
	{ "ctrl-k", "Kill"       },
	{ "esc",    "Back"       },
};

/* children of the target, as indexes into app->data.procs */
static size_t *children;
static size_t  nchildren, children_cap;

/* ------------------------------------------------------------------ */
/* public: open / refresh                                             */
/* ------------------------------------------------------------------ */

void detail_open(struct app *app, const struct proc_row *p)
{
	if (app->target_pid != p->pid || app->target_start_ns != p->start_ns) {
		app->hist_len = 0;               /* new process: new history */
		app->cursor[VIEW_DETAIL] = 0;
		app->scrolls[VIEW_DETAIL] = 0;
	}
	app->target_pid = p->pid;
	app->target_start_ns = p->start_ns;
	app->target = *p;
	app->target_alive = true;
	view_switch(app, VIEW_DETAIL);
}

void detail_on_refresh(struct app *app)
{
	const struct proc_row *p;

	if (app->target_pid <= 0)
		return;
	p = model_find(app, app->target_pid, app->target_start_ns);
	if (!p) {
		app->target_alive = false;       /* keep last copy, stop history */
		return;
	}
	app->target = *p;
	app->target_alive = true;

	if (app->hist_len == CPU_HIST_MAX) {
		memmove(app->cpu_hist, app->cpu_hist + 1,
			(CPU_HIST_MAX - 1) * sizeof(app->cpu_hist[0]));
		app->hist_len--;
	}
	app->cpu_hist[app->hist_len++] = p->cpu_pct;
}

/* ------------------------------------------------------------------ */
/* drawing                                                            */
/* ------------------------------------------------------------------ */

static void label(int y, int x, const char *text)
{
	attron(COLOR_PAIR(CP_INFO_LABEL) | A_BOLD);
	mvprintw(y, x, "%-10s", text);
	attroff(COLOR_PAIR(CP_INFO_LABEL) | A_BOLD);
}

static void value(int pair, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void value(int pair, const char *fmt, ...)
{
	va_list ap;

	attron(COLOR_PAIR(pair));
	va_start(ap, fmt);
	vw_printw(stdscr, fmt, ap);
	va_end(ap);
	attroff(COLOR_PAIR(pair));
}

static int state_pair(char s)
{
	switch (s) {
	case PM_STATE_RUNNING: return CP_ROW_RUNNING;
	case PM_STATE_ZOMBIE:
	case PM_STATE_DEAD:    return CP_ROW_ZOMBIE;
	case PM_STATE_DISK:
	case PM_STATE_STOPPED:
	case PM_STATE_TRACED:  return CP_ROW_WARN;
	default:               return CP_INFO_VALUE;
	}
}

static void collect_children(struct app *app)
{
	nchildren = 0;
	for (size_t i = 0; i < app->data.nprocs; i++) {
		if (app->data.procs[i].ppid != app->target_pid || !app->target_alive)
			continue;
		if (nchildren == children_cap) {
			size_t ncap = children_cap ? children_cap * 2 : 32;
			size_t *n = realloc(children, ncap * sizeof(*n));

			if (!n)
				return;
			children = n;
			children_cap = ncap;
		}
		children[nchildren++] = i;
	}
}

/* --- children table --- */
enum { CC_PID, CC_NAME, CC_CPU, CC_MEM, NCC };
static const struct table_col child_cols[NCC] = {
	[CC_PID]  = { "PID",  7, ALIGN_RIGHT },
	[CC_NAME] = { "NAME", 0, ALIGN_LEFT  },
	[CC_CPU]  = { "CPU%", 5, ALIGN_RIGHT },
	[CC_MEM]  = { "MEM%", 5, ALIGN_RIGHT },
};

static void child_cell(void *ctx, size_t row, int col, char *buf, size_t len)
{
	const struct proc_row *c = &((struct app *)ctx)->data.procs[children[row]];

	switch (col) {
	case CC_PID:  snprintf(buf, len, "%d", c->pid); break;
	case CC_NAME: snprintf(buf, len, "%s", c->comm); break;
	case CC_CPU:  snprintf(buf, len, "%.1f", c->cpu_pct); break;
	case CC_MEM:  snprintf(buf, len, "%.1f", c->mem_pct); break;
	}
}

static void draw_fields(struct app *app, int y, int x, int w)
{
	const struct proc_row *p = &app->target;
	const struct proc_row *parent = model_find(app, p->ppid, 0);
	char a[32], b[32], c[32];
	struct timespec boot, real;
	uint64_t boot_ns, age_ns;
	time_t started;
	double hist_max = 100;
	int gw = w - 22 > 10 ? (w - 22 < 30 ? w - 22 : 30) : 10;

	/* start_ns is boot-based: read both clocks NOW, so the wall-clock
	 * start time is stable from frame to frame */
	clock_gettime(CLOCK_BOOTTIME, &boot);
	clock_gettime(CLOCK_REALTIME, &real);
	boot_ns = (uint64_t)boot.tv_sec * 1000000000ULL + (uint64_t)boot.tv_nsec;
	age_ns = boot_ns > p->start_ns ? boot_ns - p->start_ns : 0;
	started = (time_t)(((uint64_t)real.tv_sec * 1000000000ULL +
			    (uint64_t)real.tv_nsec - age_ns) / 1000000000ULL);

	label(y, x, "Name");     value(CP_INFO_VALUE, "%s", p->comm);
	y++;
	label(y, x, "PID");      value(CP_INFO_VALUE, "%-8d", p->pid);
	attron(COLOR_PAIR(CP_INFO_LABEL) | A_BOLD); printw("PPID "); attroff(COLOR_PAIR(CP_INFO_LABEL) | A_BOLD);
	value(CP_INFO_VALUE, "%d", p->ppid);
	value(CP_KEY_DESC, " (%s)", parent ? parent->comm : p->ppid == 0 ? "kernel" : "?");
	y++;
	label(y, x, "User");     value(CP_INFO_VALUE, "%s", user_name(p->uid));
	value(CP_KEY_DESC, " (uid %u)", p->uid);
	y++;
	label(y, x, "State");
	if (app->target_alive) {
		value(state_pair(p->state), "%c", p->state);
		value(CP_KEY_DESC, "  %s", fmt_state_name(p->state));
	} else {
		value(CP_ROW_ZOMBIE, "-  exited");
	}
	y++;
	label(y, x, "Priority"); value(CP_INFO_VALUE, "PRI %d   NICE %d", p->prio, p->nice);
	y++;
	label(y, x, "Threads");  value(CP_INFO_VALUE, "%u", p->threads);
	y++;
	strftime(a, sizeof(a), "%Y-%m-%d %H:%M:%S", localtime(&started));
	fmt_duration(b, sizeof(b), age_ns);
	label(y, x, "Started");  value(CP_INFO_VALUE, "%s", a);
	value(CP_KEY_DESC, "  (%s ago)", b);
	y++;
	fmt_cputime(a, sizeof(a), p->utime_ns + p->stime_ns);
	fmt_cputime(b, sizeof(b), p->utime_ns);
	fmt_cputime(c, sizeof(c), p->stime_ns);
	label(y, x, "CPU time"); value(CP_INFO_VALUE, "%s", a);
	value(CP_KEY_DESC, "  (user %s, sys %s)", b, c);
	y++;
	fmt_kb(a, sizeof(a), p->rss_kb);
	fmt_kb(b, sizeof(b), p->vsz_kb);
	label(y, x, "Memory");   value(CP_INFO_VALUE, "RSS %s", a);
	value(CP_KEY_DESC, "   VSZ %s", b);
	y += 2;

	label(y, x, "CPU");
	ui_gauge(y, x + 10, gw, p->cpu_pct);
	if (p->cpu_pct > 100)
		value(CP_KEY_DESC, "  (%.1f cpus)", p->cpu_pct / 100);
	y++;
	label(y, x, "MEM");
	ui_gauge(y, x + 10, gw, p->mem_pct);
	y += 2;

	for (int i = 0; i < app->hist_len; i++)
		if (app->cpu_hist[i] > hist_max)
			hist_max = app->cpu_hist[i];
	attron(COLOR_PAIR(CP_INFO_LABEL) | A_BOLD);
	mvprintw(y, x, "CPU history");
	attroff(COLOR_PAIR(CP_INFO_LABEL) | A_BOLD);
	value(CP_KEY_DESC, "  last %ds, scale 0-%.0f%%",
	      (app->hist_len * app->refresh_ms) / 1000, hist_max);
	y++;
	ui_sparkline(y, x, w - 2, app->cpu_hist, app->hist_len, hist_max);
}

static size_t draw(struct app *app, struct rect body)
{
	char title[48], accent[COMM_MAX + 16];
	int split = body.w * 58 / 100;
	struct rect right = { body.y + 1, body.x + split, body.h - 2, body.w - split - 1 };
	struct table_spec spec = {
		.cols = child_cols, .ncols = NCC, .cell = child_cell,
		.ctx = app, .sort_col = -1,
	};

	if (app->target_pid <= 0) {
		ui_frame(body, "describe", "");
		ui_center_text(body, 0, CP_KEY_DESC, "select a process and press <enter>");
		return 0;
	}

	snprintf(title, sizeof(title), "describe(%d)", app->target_pid);
	snprintf(accent, sizeof(accent), "[%s]%s", app->target.comm,
		 app->target_alive ? "" : " exited");
	ui_frame(body, title, accent);

	if (!app->target_alive) {
		attron(COLOR_PAIR(CP_FLASH_ERR) | A_BOLD);
		mvprintw(body.y + body.h - 2, body.x + 2,
			 "process has exited - showing its last known values");
		attroff(COLOR_PAIR(CP_FLASH_ERR) | A_BOLD);
	}
	draw_fields(app, body.y + 2, body.x + 2, split - 3);

	collect_children(app);
	snprintf(accent, sizeof(accent), "[%zu]", nchildren);
	ui_frame(right, "children", accent);
	if (nchildren == 0) {
		ui_center_text(right, 0, CP_KEY_DESC, "no children");
		return 0;
	}
	spec.nrows = nchildren;
	table_draw((struct rect){ right.y + 1, right.x + 1, right.h - 2, right.w - 2 },
		   &spec, &app->cursor[VIEW_DETAIL], &app->scrolls[VIEW_DETAIL]);
	return nchildren;
}

static bool on_key(struct app *app, int ch)
{
	const struct proc_row *p;
	int cur = app->cursor[VIEW_DETAIL];

	switch (ch) {
	case '\n':
	case KEY_ENTER:
		if (cur >= 0 && (size_t)cur < nchildren)
			detail_open(app, &app->data.procs[children[cur]]);
		return true;
	case 'p':
		p = model_find(app, app->target.ppid, 0);
		if (p)
			detail_open(app, p);
		else
			app_flash(app, true, "parent %d is not visible", app->target.ppid);
		return true;
	case 11: /* ctrl-k */
		if (app->target_alive)
			input_open_kill(app, &app->target);
		else
			app_flash(app, true, "process already exited");
		return true;
	}
	return false;
}

const struct view view_detail = {
	.name = "describe", .aliases = aliases,
	.hints = hints, .nhints = sizeof(hints) / sizeof(hints[0]),
	.is_list = true, .draw = draw, .on_key = on_key,
};
