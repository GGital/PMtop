#include <ncurses.h>
#include <string.h>

#include "table.h"
#include "theme.h"

#define COL_GAP 2
#define MAX_COLS 24

static void layout(const struct table_spec *s, int total_w, int *widths)
{
	int used = 0, flex = -1;

	for (int c = 0; c < s->ncols; c++) {
		widths[c] = s->cols[c].width;
		if (widths[c] == 0)
			flex = c;
		used += widths[c] + (c ? COL_GAP : 0);
	}
	if (flex >= 0) {
		widths[flex] = total_w - used;
		if (widths[flex] < 4)
			widths[flex] = 4;
	}
}

/* Print `text` into a cell of width w, padded/truncated, aligned. */
static void put_cell(int y, int x, int w, enum table_align a, const char *text)
{
	int len = (int)strlen(text);

	if (len > w)
		len = w;
	if (a == ALIGN_RIGHT)
		mvprintw(y, x, "%*.*s", w, len, text);
	else
		mvprintw(y, x, "%-*.*s", w, len, text);
}

void table_draw(struct rect area, const struct table_spec *s,
		int *selected, int *scroll)
{
	int widths[MAX_COLS];
	int body_h = area.h - 1;          /* first row is the header */
	char buf[256];

	if (s->ncols > MAX_COLS || area.h < 2 || area.w < 10)
		return;
	layout(s, area.w - 2, widths);

	/* --- header --- */
	attron(COLOR_PAIR(CP_TABLE_HEADER) | A_BOLD);
	for (int c = 0, x = area.x + 1; c < s->ncols; c++) {
		const char *t = s->cols[c].title;

		if (c == s->sort_col) {
			snprintf(buf, sizeof(buf), "%s%s", t, s->sort_desc ? "↓" : "↑");
			t = buf;
		}
		put_cell(area.y, x, widths[c], s->cols[c].align, t);
		x += widths[c] + COL_GAP;
	}
	attroff(COLOR_PAIR(CP_TABLE_HEADER) | A_BOLD);

	/* --- clamp cursor + scroll --- */
	if (s->nrows == 0) {
		*selected = 0;
		*scroll = 0;
		return;
	}
	if (*selected < 0)
		*selected = 0;
	if ((size_t)*selected >= s->nrows)
		*selected = (int)s->nrows - 1;
	if (*selected < *scroll)
		*scroll = *selected;
	if (*selected >= *scroll + body_h)
		*scroll = *selected - body_h + 1;
	if (*scroll < 0)
		*scroll = 0;

	/* --- rows --- */
	for (int i = 0; i < body_h; i++) {
		size_t row = (size_t)(*scroll + i);
		int y = area.y + 1 + i;
		int pair;

		if (row >= s->nrows)
			break;
		pair = (int)row == *selected ? CP_ROW_SELECTED
		       : (s->style ? s->style(s->ctx, row) : CP_ROW);

		attron(COLOR_PAIR(pair));
		mvhline(y, area.x, ' ', area.w);
		for (int c = 0, x = area.x + 1; c < s->ncols; c++) {
			buf[0] = '\0';
			s->cell(s->ctx, row, c, buf, sizeof(buf));
			put_cell(y, x, widths[c], s->cols[c].align, buf);
			x += widths[c] + COL_GAP;
		}
		attroff(COLOR_PAIR(pair));
	}
}
