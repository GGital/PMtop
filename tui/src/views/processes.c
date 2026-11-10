/*
 * processes.c - The main view: a sortable, filterable process table.
 *
 * STATUS: table rendering + sorting keys work. USER column shows the
 *         numeric uid for now (name lookup cache: step 4).
 */
#include <ncurses.h>
#include <stdio.h>

#include "view.h"
#include "../model.h"
#include "../table.h"
#include "../theme.h"

enum {
	COL_PID, COL_PPID, COL_USER, COL_STATE, COL_PRI, COL_NI, COL_THR,
	COL_CPU, COL_MEM, COL_VSZ, COL_RSS, COL_TIME, COL_NAME, NCOLS
};

static const struct table_col cols[NCOLS] = {
	[COL_PID]   = { "PID",   7, ALIGN_RIGHT },
	[COL_PPID]  = { "PPID",  7, ALIGN_RIGHT },
	[COL_USER]  = { "USER",  9, ALIGN_LEFT  },
	[COL_STATE] = { "S",     2, ALIGN_LEFT  },
	[COL_PRI]   = { "PRI",   4, ALIGN_RIGHT },
	[COL_NI]    = { "NI",    3, ALIGN_RIGHT },
	[COL_THR]   = { "THR",   4, ALIGN_RIGHT },
	[COL_CPU]   = { "CPU%",  6, ALIGN_RIGHT },
	[COL_MEM]   = { "MEM%",  6, ALIGN_RIGHT },
	[COL_VSZ]   = { "VSZ",   8, ALIGN_RIGHT },
	[COL_RSS]   = { "RSS",   8, ALIGN_RIGHT },
	[COL_TIME]  = { "TIME",  9, ALIGN_RIGHT },
	[COL_NAME]  = { "NAME",  0, ALIGN_LEFT  },
};

static const struct key_hint hints[] = {
	{ "enter",   "Describe"   },
	{ "ctrl-k",  "Kill"       },
	{ "shift-c", "Sort CPU"   },
	{ "shift-m", "Sort MEM"   },
	{ "shift-p", "Sort PID"   },
	{ "shift-n", "Sort Name"  },
	{ "shift-t", "Sort Thr"   },
	{ "shift-s", "Sort State" },
};

static const char *const aliases[] = { "proc", "processes", "ps", "p", NULL };

/* "123456" KiB -> "120.6M" */
static void fmt_kb(char *buf, size_t len, unsigned long kb)
{
	if (kb >= 1024UL * 1024)
		snprintf(buf, len, "%.1fG", kb / (1024.0 * 1024));
	else if (kb >= 1024)
		snprintf(buf, len, "%.1fM", kb / 1024.0);
	else
		snprintf(buf, len, "%luK", kb);
}

/* cumulative cpu ns -> "mm:ss.cc" or "hh:mm:ss" */
static void fmt_cputime(char *buf, size_t len, uint64_t ns)
{
	uint64_t cs = ns / 10000000ULL, s = cs / 100;

	if (s >= 3600)
		snprintf(buf, len, "%llu:%02llu:%02llu", (unsigned long long)(s / 3600),
			 (unsigned long long)(s / 60 % 60), (unsigned long long)(s % 60));
	else
		snprintf(buf, len, "%llu:%02llu.%02llu", (unsigned long long)(s / 60),
			 (unsigned long long)(s % 60), (unsigned long long)(cs % 100));
}

static void cell(void *ctx, size_t row, int col, char *buf, size_t len)
{
	struct app *app = ctx;
	const struct proc_row *p = &app->data.procs[app->visible[row]];

	switch (col) {
	case COL_PID:   snprintf(buf, len, "%d", p->pid); break;
	case COL_PPID:  snprintf(buf, len, "%d", p->ppid); break;
	case COL_USER:  snprintf(buf, len, "%u", p->uid); break; /* TODO(step 4): name */
	case COL_STATE: snprintf(buf, len, "%c", p->state); break;
	case COL_PRI:   snprintf(buf, len, "%d", p->prio); break;
	case COL_NI:    snprintf(buf, len, "%d", p->nice); break;
	case COL_THR:   snprintf(buf, len, "%u", p->threads); break;
	case COL_CPU:   snprintf(buf, len, "%.1f", p->cpu_pct); break;
	case COL_MEM:   snprintf(buf, len, "%.1f", p->mem_pct); break;
	case COL_VSZ:   fmt_kb(buf, len, p->vsz_kb); break;
	case COL_RSS:   fmt_kb(buf, len, p->rss_kb); break;
	case COL_TIME:  fmt_cputime(buf, len, p->utime_ns + p->stime_ns); break;
	case COL_NAME:  snprintf(buf, len, "%s", p->comm); break;
	}
}

static int style(void *ctx, size_t row)
{
	struct app *app = ctx;
	const struct proc_row *p = &app->data.procs[app->visible[row]];

	switch (p->state) {
	case PM_STATE_RUNNING: return CP_ROW_RUNNING;
	case PM_STATE_ZOMBIE:
	case PM_STATE_DEAD:    return CP_ROW_ZOMBIE;
	case PM_STATE_DISK:
	case PM_STATE_STOPPED:
	case PM_STATE_TRACED:  return CP_ROW_WARN;
	case PM_STATE_IDLE:    return CP_ROW_DIM;
	default:               return p->vsz_kb == 0 ? CP_ROW_DIM : CP_ROW;
	}
}

static int sort_to_col(sort_key_t k)
{
	switch (k) {
	case SORT_PID:     return COL_PID;
	case SORT_NAME:    return COL_NAME;
	case SORT_CPU:     return COL_CPU;
	case SORT_MEM:     return COL_MEM;
	case SORT_THREADS: return COL_THR;
	case SORT_STATE:   return COL_STATE;
	}
	return -1;
}

static size_t draw(struct app *app, struct rect body)
{
	char title[64], accent[192];
	struct table_spec spec = {
		.cols = cols, .ncols = NCOLS, .nrows = app->nvisible,
		.cell = cell, .style = style, .ctx = app,
		.sort_col = sort_to_col(app->sort_key), .sort_desc = app->sort_desc,
	};

	snprintf(title, sizeof(title), "processes(%s)", app->filter[0] ? "filtered" : "all");
	if (app->filter[0])
		snprintf(accent, sizeof(accent), "[%zu] </%s>", app->nvisible, app->filter);
	else
		snprintf(accent, sizeof(accent), "[%zu]", app->nvisible);
	ui_frame(body, title, accent);

	if (!app->data.module_loaded) {
		ui_center_text(body, -1, CP_FLASH_ERR, "kernel module not loaded");
		ui_center_text(body, 1, CP_KEY_DESC, "sudo insmod kmod/procmon.ko   (or: make load)");
		return 0;
	}
	if (app->data.nprocs == 0) {
		ui_center_text(body, 0, CP_KEY_DESC,
			       "module loaded, but no data yet (collector is a stub until step 2)");
		return 0;
	}

	table_draw((struct rect){ body.y + 1, body.x + 1, body.h - 2, body.w - 2 },
		   &spec, &app->selected, &app->scroll);
	return app->nvisible;
}

static void set_sort(struct app *app, sort_key_t k, bool default_desc)
{
	if (app->sort_key == k)
		app->sort_desc = !app->sort_desc;
	else {
		app->sort_key = k;
		app->sort_desc = default_desc;
	}
	model_rebuild(app);
}

static bool on_key(struct app *app, int ch)
{
	const struct proc_row *sel = model_selected(app);

	switch (ch) {
	case 'C': set_sort(app, SORT_CPU, true); return true;
	case 'M': set_sort(app, SORT_MEM, true); return true;
	case 'P': set_sort(app, SORT_PID, false); return true;
	case 'N': set_sort(app, SORT_NAME, false); return true;
	case 'T': set_sort(app, SORT_THREADS, true); return true;
	case 'S': set_sort(app, SORT_STATE, false); return true;
	case '\n':
	case KEY_ENTER:
	case 'd':
		if (!sel)
			return true;
		app->target_pid = sel->pid;
		view_switch(app, VIEW_DETAIL);
		return true;
	case 11: /* ctrl-k */
		if (!sel)
			return true;
		app->target_pid = sel->pid;
		app->kill_signal = 15; /* SIGTERM */
		app->mode = MODE_CONFIRM;
		return true;
	}
	return false;
}

const struct view view_processes = {
	.name = "processes", .aliases = aliases,
	.hints = hints, .nhints = sizeof(hints) / sizeof(hints[0]),
	.is_list = true, .draw = draw, .on_key = on_key,
};
