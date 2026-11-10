/*
 * app.h - The single application state struct, shared by every module
 *         of the TUI. Flow of one frame:
 *
 *   main loop ──► source_refresh() ──► metrics_compute() ──► model_rebuild()
 *        │                                                        │
 *        └──► input_handle(key) ◄── getch()      ui_draw() ◄──────┘
 */
#ifndef PMTOP_APP_H
#define PMTOP_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "source.h"

#define CPU_HIST_MAX 240   /* detail view sparkline samples */

typedef enum {
	MODE_NORMAL,    /* keys go to the current view               */
	MODE_COMMAND,   /* ":" prompt  e.g. :proc  :tree  :ev  :q     */
	MODE_FILTER,    /* "/" prompt, live-filters the table         */
	MODE_CONFIRM,   /* yes/no dialog (kill)                      */
} input_mode_t;

typedef enum {
	VIEW_PROCESSES,
	VIEW_TREE,
	VIEW_DETAIL,
	VIEW_EVENTS,
	VIEW_HELP,
	VIEW_COUNT
} view_id_t;

typedef enum {
	SORT_PID,
	SORT_NAME,
	SORT_CPU,
	SORT_MEM,
	SORT_THREADS,
	SORT_STATE,
} sort_key_t;

struct app {
	bool          running;
	int           refresh_ms;

	/* navigation */
	view_id_t     view;
	view_id_t     prev_view;
	input_mode_t  mode;

	/* prompt (":" or "/") */
	char          prompt[128];
	size_t        prompt_len;

	/* table state (processes view) */
	char          filter[128];
	sort_key_t    sort_key;
	bool          sort_desc;
	size_t       *visible;      /* indexes into data.procs        */
	size_t        nvisible;

	/* per-view list cursor + first visible row */
	int           cursor[VIEW_COUNT];
	int           scrolls[VIEW_COUNT];

	/* detail view: the described process. A copy is kept so the view
	 * still makes sense after the process exits. */
	pid_t           target_pid;     /* 0 = none */
	uint64_t        target_start_ns;
	bool            target_alive;
	struct proc_row target;
	double          cpu_hist[CPU_HIST_MAX];  /* oldest first */
	int             hist_len;

	/* kill dialog (MODE_CONFIRM) */
	pid_t         kill_pid;
	uint64_t      kill_start_ns;
	char          kill_name[COMM_MAX];
	size_t        kill_choice;  /* index into signal_choices[] */

	bool          force_refresh; /* refresh on next loop iteration */

	/* events view */
	bool          ev_paused;
	uint64_t      ev_pause_seq;  /* while paused: show only seq <= this */
	uint64_t      ev_clear_seq;  /* 'c': hide seq <= this               */
	unsigned      ev_hide;       /* bitmask of hidden enum pm_event_type */

	/* data: current and previous sample (for CPU%) */
	struct dataset data;
	struct dataset prev;
	struct event_log events;    /* accumulates across refreshes */

	/* status line ("flash" message, bottom right) */
	char          flash[160];
	bool          flash_err;
	double        flash_until;
};

#endif
