/*
 * model.h - Turns app->data.procs into app->visible[]: the filtered,
 *           sorted list of row indexes the table actually shows.
 *           Also keeps the selection on the same pid across refreshes.
 */
#ifndef PMTOP_MODEL_H
#define PMTOP_MODEL_H

#include "app.h"

void model_rebuild(struct app *app);              /* sort/filter changed: cursor to top */
void model_rebuild_keep(struct app *app, pid_t keep); /* refresh: cursor follows `keep` */
pid_t model_selected_pid(const struct app *app);   /* -1 if none */
const struct proc_row *model_selected(const struct app *app);
/* bsearch data.procs (sorted by pid). start_ns 0 = any start time. */
const struct proc_row *model_find(const struct app *app, pid_t pid, uint64_t start_ns);
const char *model_sort_name(sort_key_t k);

#endif
