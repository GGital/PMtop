#include <ctype.h>
#include <ncurses.h>
#include <string.h>

#include "actions.h"
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

void input_open_kill(struct app *app, const struct proc_row *p)
{
	app->kill_pid = p->pid;
	app->kill_start_ns = p->start_ns;     /* identifies THIS process */
	snprintf(app->kill_name, sizeof(app->kill_name), "%s", p->comm);
	app->kill_choice = 0;                 /* SIGTERM: the polite default */
	app->mode = MODE_CONFIRM;
}

static void handle_confirm(struct app *app, int ch)
{
	const struct signal_choice *c = &signal_choices[app->kill_choice];
	char msg[160];
	int err;

	switch (ch) {
	case KEY_LEFT: case 'h': case KEY_BTAB:
		app->kill_choice = (app->kill_choice + nsignal_choices - 1) % nsignal_choices;
		return;
	case KEY_RIGHT: case 'l': case '\t':
		app->kill_choice = (app->kill_choice + 1) % nsignal_choices;
		return;
	case 'y': case 'Y': case '\n': case KEY_ENTER:
		err = action_signal(app->kill_pid, app->kill_start_ns, c->sig);
		if (err) {
			action_describe_error(msg, sizeof(msg), err, app->kill_pid);
			app_flash(app, true, "%s", msg);
		} else {
			app_flash(app, false, "sent SIG%s to %d (%s)",
				  c->name, app->kill_pid, app->kill_name);
			app->force_refresh = true;    /* show the effect now */
		}
		break;
	case 'n': case 'N': case KEY_ESC: case 'q':
		break;
	default:
		return;                           /* ignore other keys, keep dialog */
	}
	app->mode = MODE_NORMAL;
}

static bool handle_nav(struct app *app, int ch, size_t len, int page)
{
	int *cur = &app->cursor[app->view];   /* every view has its own */

	switch (ch) {
	case KEY_UP:    case 'k': (*cur)--; break;
	case KEY_DOWN:  case 'j': (*cur)++; break;
	case KEY_PPAGE: case 2:   *cur -= page; break;   /* ctrl-b */
	case KEY_NPAGE: case 6:   *cur += page; break;   /* ctrl-f */
	case KEY_HOME:  case 'g': *cur = 0; break;
	case KEY_END:   case 'G': *cur = (int)len - 1; break;
	default: return false;
	}
	if (*cur >= (int)len)                   /* table_draw clamps too, but */
		*cur = (int)len - 1;            /* don't let it run off while */
	if (*cur < 0)                           /* the list is empty          */
		*cur = 0;
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
		if (app->view == VIEW_PROCESSES) {
			if (app->filter[0]) {             /* 1st esc: drop filter */
				app->filter[0] = '\0';
				model_rebuild(app);
			}
		} else {
			/* help returns to wherever it was opened from; other views
			 * return to their previous view, but never bounce back into
			 * help or themselves (describe -> ? -> esc -> esc) */
			view_id_t back = app->prev_view;

			if (app->view != VIEW_HELP &&
			    (back == VIEW_HELP || back == app->view))
				back = VIEW_PROCESSES;
			view_switch(app, back);
		}
		return;
	}

	if (v->is_list && handle_nav(app, ch, list_len, page))
		return;
	if (v->on_key)
		v->on_key(app, ch);
}
