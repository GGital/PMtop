/*
 * view.h - Every screen in pmtop is a `struct view`. Adding a screen
 *          means: write views/<name>.c exporting one struct view,
 *          and add it to the registry in views/registry.c.
 *
 *   view        command aliases      what it shows
 *   ----------  -------------------  ---------------------------------
 *   processes   :proc :ps :p          table of all processes (default)
 *   tree        :tree :t              parent/child hierarchy
 *   detail      (enter / d)           one process: all fields + gauges
 *   events      :events :ev :e        live fork/exec/exit stream
 *   help        ?  :help              all key bindings
 */
#ifndef PMTOP_VIEW_H
#define PMTOP_VIEW_H

#include <stdbool.h>
#include <stddef.h>

#include "../app.h"
#include "../ui.h"

struct view {
	const char             *name;      /* crumb + frame title        */
	const char *const      *aliases;   /* NULL-terminated, for ":"   */
	const struct key_hint  *hints;     /* shown in the header        */
	size_t                  nhints;
	bool                    is_list;   /* up/down/pgup handled by input.c */

	/* Draw into `body`. List views must set app->nvisible-like length
	 * via the return value: number of rows (for generic navigation). */
	size_t (*draw)(struct app *app, struct rect body);

	/* Return true if the key was consumed. */
	bool   (*on_key)(struct app *app, int ch);
};

extern const struct view view_processes;
extern const struct view view_tree;
extern const struct view view_detail;
extern const struct view view_events;
extern const struct view view_help;

const struct view *view_get(view_id_t id);
int                view_lookup(const char *command);   /* -1 if unknown */
void               view_switch(struct app *app, view_id_t id);

/* global hints, shown on every view */
extern const struct key_hint global_hints[];
extern const size_t          global_nhints;

#endif
