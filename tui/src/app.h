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
#include <sys/types.h>

#include "source.h"

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

	/* table state */
	char          filter[128];
	sort_key_t    sort_key;
	bool          sort_desc;
	int           selected;     /* index into visible[]           */
	int           scroll;       /* first visible row              */
	size_t       *visible;      /* indexes into data.procs        */
	size_t        nvisible;

	/* detail / kill */
	pid_t         target_pid;
	int           kill_signal;

	/* data: current and previous sample (for CPU%) */
	struct dataset data;
	struct dataset prev;

	/* status line ("flash" message, bottom right) */
	char          flash[160];
	bool          flash_err;
	double        flash_until;
};

#endif
