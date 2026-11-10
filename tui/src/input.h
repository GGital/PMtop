/*
 * input.h - Keyboard dispatch. Order of handling for one key:
 *
 *   1. mode-specific (prompt editing / confirm dialog)
 *   2. global keys        ( :  /  ?  esc  ctrl-c )
 *   3. list navigation    ( j k ↑ ↓ pgup pgdn g G ) if view->is_list
 *   4. view->on_key()     ( sort, describe, kill ... )
 */
#ifndef PMTOP_INPUT_H
#define PMTOP_INPUT_H

#include "app.h"

void input_handle(struct app *app, int ch, size_t list_len, int page);

/* Open the kill dialog for `p` (MODE_CONFIRM). Pre-selects SIGTERM. */
void input_open_kill(struct app *app, const struct proc_row *p);

#endif
