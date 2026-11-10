#include <ctype.h>
#include <ncurses.h>
#include <string.h>

#include "input.h"
#include "model.h"
#include "ui.h"
#include "views/view.h"

#define KEY_ESC    27
#define KEY_CTRL_C 3
#define KEY_DEL    127
#define KEY_CTRL_L 12

static void prompt_open(struct app *app, input_mode_t mode)
{
	app->mode = mode;
	if (mode == MODE_FILTER) {          /* edit the existing filter */
		snprintf(app->prompt, sizeof(app->prompt), "%s", app->filter);
		app->prompt_len = strlen(app->prompt);
	} else {
		app->prompt[0] = '\0';
		app->prompt_len = 0;
	}
}

static void run_command(struct app *app, const char *cmd)
{
	int id;

	if (!cmd[0])
		return;
	if (!strcmp(cmd, "q") || !strcmp(cmd, "q!") || !strcmp(cmd, "quit")) {
		app->running = false;
		return;
	}
	id = view_lookup(cmd);
	if (id < 0) {
		app_flash(app, true, "unknown command \"%s\"", cmd);
		return;
	}
	view_switch(app, (view_id_t)id);
}

static void handle_prompt(struct app *app, int ch)
{
	switch (ch) {
	case KEY_ESC:
		if (app->mode == MODE_FILTER) {
			app->filter[0] = '\0';
			model_rebuild(app);
		}
		app->mode = MODE_NORMAL;
		return;
	case '\n':
	case KEY_ENTER:
		if (app->mode == MODE_COMMAND)
			run_command(app, app->prompt);
		app->mode = MODE_NORMAL;
		return;
	case KEY_BACKSPACE:
	case KEY_DEL:
	case 8:
		if (app->prompt_len)
			app->prompt[--app->prompt_len] = '\0';
		break;
	default:
		if (isprint(ch) && app->prompt_len + 1 < sizeof(app->prompt)) {
			app->prompt[app->prompt_len++] = (char)ch;
			app->prompt[app->prompt_len] = '\0';
		}
		break;
	}
	if (app->mode == MODE_FILTER) {     /* live filtering */
		snprintf(app->filter, sizeof(app->filter), "%s", app->prompt);
		model_rebuild(app);
	}
}

static void handle_confirm(struct app *app, int ch)
{
	if (ch == 'y' || ch == 'Y') {
		/* TODO(step 4): kill(app->target_pid, app->kill_signal), report errno */
		app_flash(app, true, "kill %d: not implemented yet (step 4)", app->target_pid);
	}
	app->mode = MODE_NORMAL;
}

static bool handle_nav(struct app *app, int ch, size_t len, int page)
{
	switch (ch) {
	case KEY_UP:    case 'k': app->selected--; break;
	case KEY_DOWN:  case 'j': app->selected++; break;
	case KEY_PPAGE: case 2:   app->selected -= page; break;   /* ctrl-b */
	case KEY_NPAGE: case 6:   app->selected += page; break;   /* ctrl-f */
	case KEY_HOME:  case 'g': app->selected = 0; break;
	case KEY_END:   case 'G': app->selected = (int)len - 1; break;
	default: return false;
	}
	/* table_draw() clamps to the valid range */
	return true;
}

void input_handle(struct app *app, int ch, size_t list_len, int page)
{
	const struct view *v = view_get(app->view);

	if (ch == KEY_CTRL_C) {
		app->running = false;
		return;
	}
	if (ch == KEY_CTRL_L) {             /* force full repaint */
		clearok(curscr, TRUE);
		return;
	}
	switch (app->mode) {
	case MODE_COMMAND:
	case MODE_FILTER:
		handle_prompt(app, ch);
		return;
	case MODE_CONFIRM:
		handle_confirm(app, ch);
		return;
	case MODE_NORMAL:
		break;
	}

	switch (ch) {
	case ':': prompt_open(app, MODE_COMMAND); return;
	case '/': prompt_open(app, MODE_FILTER); return;
	case '?': view_switch(app, VIEW_HELP); return;
	case 'q': app->running = false; return;
	case KEY_ESC:
		if (app->filter[0]) {
			app->filter[0] = '\0';
			model_rebuild(app);
		} else if (app->view != VIEW_PROCESSES) {
			view_switch(app, app->prev_view == app->view
					 ? VIEW_PROCESSES : app->prev_view);
		}
		return;
	}

	if (v->is_list && handle_nav(app, ch, list_len, page))
		return;
	if (v->on_key)
		v->on_key(app, ch);
}
