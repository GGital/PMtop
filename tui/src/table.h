/*
 * table.h - Reusable k9s-style table widget: header row, scrolling,
 *           highlighted cursor bar, per-row color, sort indicator.
 *           Views provide rows through callbacks, so the widget never
 *           knows what a "process" or an "event" is.
 */
#ifndef PMTOP_TABLE_H
#define PMTOP_TABLE_H

#include <stddef.h>
#include "ui.h"

enum table_align { ALIGN_LEFT, ALIGN_RIGHT };

struct table_col {
	const char      *title;
	int              width;   /* 0 = take remaining space (one column) */
	enum table_align align;
};

/* Write the text for (row, col) into buf. */
typedef void (*table_cell_fn)(void *ctx, size_t row, int col,
			      char *buf, size_t len);
/* Color pair for a row (ignored for the selected row). */
typedef int (*table_style_fn)(void *ctx, size_t row);

struct table_spec {
	const struct table_col *cols;
	int             ncols;
	size_t          nrows;
	table_cell_fn   cell;
	table_style_fn  style;
	void           *ctx;
	int             sort_col;   /* -1 = none */
	bool            sort_desc;
};

/* Draws inside `area` (no frame). Clamps *selected and *scroll. */
void table_draw(struct rect area, const struct table_spec *spec,
		int *selected, int *scroll);

#endif
