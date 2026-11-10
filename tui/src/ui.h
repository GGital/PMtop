/*
 * ui.h - Screen layout and shared drawing primitives.
 *
 *  ┌──────────────────────────── HEADER (8 rows) ────────────────────────────┐
 *  │ Host/Kernel/Uptime/CPU/MEM info │ <key> hints (view + global) │  LOGO   │
 *  ├──────────────────────────── PROMPT (3 rows, only in : or / mode) ───────┤
 *  ├──────────────────────────── BODY (rest) ────────────────────────────────┤
 *  │ ┌───────────────── processes(all)[312] ─────────────────┐                │
 *  │ │ PID  PPID USER  S  CPU%  MEM%  ...  NAME              │  view->draw()  │
 *  │ └───────────────────────────────────────────────────────┘                │
 *  ├──────────────────────────── FOOTER (1 row) ─────────────────────────────┤
 *  │ <processes> <filter:ssh>                         flash message / error  │
 *  └─────────────────────────────────────────────────────────────────────────┘
 */
#ifndef PMTOP_UI_H
#define PMTOP_UI_H

#include <stdbool.h>
#include <stddef.h>
#include "app.h"

struct rect {
	int y, x, h, w;
};

struct key_hint {
	const char *key;    /* "ctrl-k", "shift-c", "enter" ... */
	const char *desc;   /* "Kill", "Sort CPU" ...           */
};

#define HEADER_H 8
#define PROMPT_H 3
#define FOOTER_H 1

void   ui_init(void);
void   ui_shutdown(void);
/* Draws a full frame. Returns the current list length and the page
 * height (rows per screen) for input.c's list navigation. */
size_t ui_draw(struct app *app, int *page);

/* primitives used by views */
void   ui_frame(struct rect r, const char *title, const char *accent);
void   ui_center_text(struct rect r, int dy, int pair, const char *text);
void   ui_fill(struct rect r, int pair);
void   ui_gauge(int y, int x, int w, double pct);
void   ui_sparkline(int y, int x, int w, const double *v, int n, double max);

/* helpers */
double ui_now(void);
void   app_flash(struct app *app, bool err, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));

#endif
