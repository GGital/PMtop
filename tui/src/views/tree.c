/*
 * tree.c - Parent/child hierarchy (like pstree), collapsible with space.
 *
 * STATUS: placeholder. Implemented in step 6.
 */
#include "view.h"
#include "../theme.h"

static const char *const aliases[] = { "tree", "t", NULL };

static const struct key_hint hints[] = {
	{ "space", "Collapse" },
	{ "enter", "Describe" },
};

static size_t draw(struct app *app, struct rect body)
{
	(void)app;
	ui_frame(body, "tree", "");
	ui_center_text(body, 0, CP_KEY_DESC, "tree view: coming in step 6");
	return 0;
}

static bool on_key(struct app *app, int ch)
{
	(void)app;
	(void)ch;
	return false;
}

const struct view view_tree = {
	.name = "tree", .aliases = aliases,
	.hints = hints, .nhints = sizeof(hints) / sizeof(hints[0]),
	.is_list = true, .draw = draw, .on_key = on_key,
};
