/*
 * model.h - Turns app->data.procs into app->visible[]: the filtered,
 *           sorted list of row indexes the table actually shows.
 *           Also keeps the selection on the same pid across refreshes.
 */
#ifndef PMTOP_MODEL_H
#define PMTOP_MODEL_H

#include "app.h"

void model_rebuild(struct app *app);
const struct proc_row *model_selected(const struct app *app);
const char *model_sort_name(sort_key_t k);

#endif
