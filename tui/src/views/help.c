/*
 * help.c - Lists global keys, then every view's own keys.
 */
#include <ncurses.h>

#include "view.h"
#include "../theme.h"

static const char *const aliases[] = { "help", "h", NULL };

static int draw_section(int y, int x, int max_y, const char *title,
			const struct key_hint *h, size_t n)
{
	if (y >= max_y)
		return y;
	attron(COLOR_PAIR(CP_INFO_LABEL) | A_BOLD);
	mvprintw(y++, x, "%s", title);
	attroff(COLOR_PAIR(CP_INFO_LABEL) | A_BOLD);
	for (size_t i = 0; i < n && y < max_y; i++, y++) {
		attron(COLOR_PAIR(CP_KEY));
		mvprintw(y, x + 2, "<%s>", h[i].key);
		attroff(COLOR_PAIR(CP_KEY));
		attron(COLOR_PAIR(CP_KEY_DESC));
		mvprintw(y, x + 14, "%s", h[i].desc);
		attroff(COLOR_PAIR(CP_KEY_DESC));
	}
	return y + 1;
}

static size_t draw(struct app *app, struct rect body)
{
	int col_w = 32, x = body.x + 3, y = body.y + 2, top = y;
	int max_y = body.y + body.h - 1;

	(void)app;
	ui_frame(body, "help", "");
	y = draw_section(y, x, max_y, "GENERAL", global_hints, global_nhints);
	for (int v = 0; v < VIEW_COUNT; v++) {
		const struct view *vw = view_get(v);

		if (vw == &view_help)
			continue;
		if (y + (int)vw->nhints + 1 >= max_y) {   /* next column */
			x += col_w;
			y = top;
			if (x + col_w > body.x + body.w)
				break;
		}
		y = draw_section(y, x, max_y, vw->name, vw->hints, vw->nhints);
	}
	return 0;
}

static bool on_key(struct app *app, int ch)
{
	(void)app;
	(void)ch;
	return false;
}

const struct view view_help = {
	.name = "help", .aliases = aliases,
	.hints = NULL, .nhints = 0,
	.is_list = false, .draw = draw, .on_key = on_key,
};
