/*
 * events.c - Live stream of fork/exec/exit events from the kernel ring buffer.
 *
 * STATUS: placeholder. Implemented in step 5.
 */
#include "view.h"
#include "../theme.h"

static const char *const aliases[] = { "events", "ev", "e", NULL };

static const struct key_hint hints[] = {
	{ "c",     "Clear"   },
	{ "space", "Pause"   },
};

static size_t draw(struct app *app, struct rect body)
{
	(void)app;
	ui_frame(body, "events", "");
	ui_center_text(body, 0, CP_KEY_DESC, "events view: coming in step 5");
	return 0;
}

static bool on_key(struct app *app, int ch)
{
	(void)app;
	(void)ch;
	return false;
}

const struct view view_events = {
	.name = "events", .aliases = aliases,
	.hints = hints, .nhints = sizeof(hints) / sizeof(hints[0]),
	.is_list = true, .draw = draw, .on_key = on_key,
};
