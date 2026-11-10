#include <string.h>

#include "view.h"

static const struct view *const views[VIEW_COUNT] = {
	[VIEW_PROCESSES] = &view_processes,
	[VIEW_TREE]      = &view_tree,
	[VIEW_DETAIL]    = &view_detail,
	[VIEW_EVENTS]    = &view_events,
	[VIEW_HELP]      = &view_help,
};

const struct key_hint global_hints[] = {
	{ ":",      "Command" },
	{ "/",      "Filter"  },
	{ "?",      "Help"    },
	{ "esc",    "Back"    },
	{ "ctrl-l", "Redraw"  },
	{ "ctrl-c", "Quit"    },
};
const size_t global_nhints = sizeof(global_hints) / sizeof(global_hints[0]);

const struct view *view_get(view_id_t id)
{
	return views[id];
}

int view_lookup(const char *command)
{
	for (int i = 0; i < VIEW_COUNT; i++) {
		const char *const *a = views[i]->aliases;

		for (; a && *a; a++)
			if (strcmp(*a, command) == 0)
				return i;
	}
	return -1;
}

void view_switch(struct app *app, view_id_t id)
{
	if (id == app->view)
		return;
	/* cursor/scroll are per view (app->cursor[]), so going back to a
	 * view returns you to the row you left */
	app->prev_view = app->view;
	app->view = id;
}
