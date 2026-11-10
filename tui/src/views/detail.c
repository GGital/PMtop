/*
 * detail.c - One process in depth: all fields, CPU/MEM gauges, children.
 *
 * STATUS: placeholder. Implemented in step 4.
 */
#include "view.h"
#include "../theme.h"

static const char *const aliases[] = { NULL };

static const struct key_hint hints[] = {
	{ "ctrl-k", "Kill" },
	{ "esc",    "Back" },
};

static size_t draw(struct app *app, struct rect body)
{
	(void)app;
	ui_frame(body, "detail", "");
	ui_center_text(body, 0, CP_KEY_DESC, "detail view: coming in step 4");
	return 0;
}

static bool on_key(struct app *app, int ch)
{
	(void)app;
	(void)ch;
	return false;
}

const struct view view_detail = {
	.name = "detail", .aliases = aliases,
	.hints = hints, .nhints = sizeof(hints) / sizeof(hints[0]),
	.is_list = false, .draw = draw, .on_key = on_key,
};
