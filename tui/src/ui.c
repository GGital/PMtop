/*
 * ui.c - Draws the fixed chrome around a view: header (info, key hints,
 *        logo), the ":" / "/" prompt, the footer crumbs + flash line and
 *        the confirm dialog. The middle of the screen belongs to the
 *        current view (views/ *.c).
 */
#include <ncurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>

#include "model.h"
#include "theme.h"
#include "ui.h"
#include "views/view.h"

/* Our own logo, box-drawing letters spelling "pmtop". */
static const char *const logo[] = {
	"┌─┐┌┬┐┌┬┐┌─┐┌─┐",
	"├─┘│││ │ │ │├─┘",
	"┴  ┴ ┴ ┴ └─┘┴  ",
};
#define LOGO_W 15

static struct utsname uts;

double ui_now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

void app_flash(struct app *app, bool err, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(app->flash, sizeof(app->flash), fmt, ap);
	va_end(ap);
	app->flash_err = err;
	app->flash_until = ui_now() + 4.0;
}

void ui_init(void)
{
	uname(&uts);
	initscr();
	raw();                 /* we handle ctrl-c ourselves */
	noecho();
	keypad(stdscr, TRUE);
	curs_set(0);
	set_escdelay(25);
	timeout(100);          /* getch() returns ERR after 100 ms */
	theme_init();
}

void ui_shutdown(void)
{
	endwin();
}

/* ------------------------------------------------------------------ */
/* primitives                                                         */
/* ------------------------------------------------------------------ */

void ui_fill(struct rect r, int pair)
{
	attron(COLOR_PAIR(pair));
	for (int y = 0; y < r.h; y++)
		mvhline(r.y + y, r.x, ' ', r.w);
	attroff(COLOR_PAIR(pair));
}

void ui_frame(struct rect r, const char *title, const char *accent)
{
	int tlen, alen, x;

	if (r.w < 4 || r.h < 2)
		return;
	attron(COLOR_PAIR(CP_BORDER));
	mvaddch(r.y, r.x, ACS_ULCORNER);
	mvhline(r.y, r.x + 1, ACS_HLINE, r.w - 2);
	mvaddch(r.y, r.x + r.w - 1, ACS_URCORNER);
	mvvline(r.y + 1, r.x, ACS_VLINE, r.h - 2);
	mvvline(r.y + 1, r.x + r.w - 1, ACS_VLINE, r.h - 2);
	mvaddch(r.y + r.h - 1, r.x, ACS_LLCORNER);
	mvhline(r.y + r.h - 1, r.x + 1, ACS_HLINE, r.w - 2);
	mvaddch(r.y + r.h - 1, r.x + r.w - 1, ACS_LRCORNER);
	attroff(COLOR_PAIR(CP_BORDER));

	if (!title[0] && !(accent && accent[0]))
		return;
	/* centered " title[accent] " on the top border */
	tlen = (int)strlen(title);
	alen = accent ? (int)strlen(accent) : 0;
	x = r.x + (r.w - (tlen + alen + 2)) / 2;
	if (x <= r.x)
		x = r.x + 1;
	attron(COLOR_PAIR(CP_TITLE) | A_BOLD);
	mvprintw(r.y, x, " %s", title);
	attroff(COLOR_PAIR(CP_TITLE) | A_BOLD);
	if (alen) {
		attron(COLOR_PAIR(CP_TITLE_ACCENT) | A_BOLD);
		printw("%s", accent);
		attroff(COLOR_PAIR(CP_TITLE_ACCENT) | A_BOLD);
	}
	addch(' ');
}

void ui_center_text(struct rect r, int dy, int pair, const char *text)
{
	int len = (int)strlen(text);
	int x = r.x + (r.w - len) / 2;

	attron(COLOR_PAIR(pair));
	mvprintw(r.y + r.h / 2 + dy, x > r.x ? x : r.x + 1, "%.*s", r.w - 2, text);
	attroff(COLOR_PAIR(pair));
}

/* ------------------------------------------------------------------ */
/* header                                                             */
/* ------------------------------------------------------------------ */

static void info_line(int y, const char *label, const char *value)
{
	attron(COLOR_PAIR(CP_INFO_LABEL) | A_BOLD);
	mvprintw(y, 1, "%-9s", label);
	attroff(COLOR_PAIR(CP_INFO_LABEL) | A_BOLD);
	attron(COLOR_PAIR(CP_INFO_VALUE));
	printw("%.28s", value);
	attroff(COLOR_PAIR(CP_INFO_VALUE));
}

/* "[█████░░░░░]  43%" */
static void gauge(int y, int x, int w, double pct)
{
	int fill = (int)(pct / 100.0 * w + 0.5);
	int pair = pct < 60 ? CP_GAUGE_LOW : pct < 85 ? CP_GAUGE_MID : CP_GAUGE_HIGH;

	if (fill > w)
		fill = w;
	mvaddch(y, x, '[');
	attron(COLOR_PAIR(pair));
	for (int i = 0; i < w; i++)
		addstr(i < fill ? "█" : "░");
	attroff(COLOR_PAIR(pair));
	printw("] %3.0f%%", pct);
}

static void fmt_uptime(char *buf, size_t len, uint64_t ns)
{
	uint64_t s = ns / 1000000000ULL;

	snprintf(buf, len, "%llud %02llu:%02llu:%02llu",
		 (unsigned long long)(s / 86400), (unsigned long long)(s / 3600 % 24),
		 (unsigned long long)(s / 60 % 60), (unsigned long long)(s % 60));
}

static void draw_info(const struct app *app)
{
	const struct sys_summary *s = &app->data.sum;
	char buf[64];

	info_line(0, "Host:", uts.nodename);
	info_line(1, "Kernel:", uts.release);
	if (app->data.module_loaded)
		snprintf(buf, sizeof(buf), "v%s", s->version[0] ? s->version : "?");
	else
		snprintf(buf, sizeof(buf), "not loaded");
	info_line(2, "procmon:", buf);
	fmt_uptime(buf, sizeof(buf), s->uptime_ns);
	info_line(3, "Uptime:", buf);
	snprintf(buf, sizeof(buf), "%.2f %.2f %.2f",
		 s->load_x100[0] / 100.0, s->load_x100[1] / 100.0, s->load_x100[2] / 100.0);
	info_line(4, "Load:", buf);

	info_line(5, "MEM:", "");
	gauge(5, 10, 14, s->mem_total_kb
		  ? 100.0 * (s->mem_total_kb - s->mem_avail_kb) / s->mem_total_kb : 0);

	snprintf(buf, sizeof(buf), "%u  R%u S%u Z%u", s->procs_total,
		 s->procs_running, s->procs_sleeping, s->procs_zombie);
	info_line(6, "Procs:", buf);
}

static void draw_hints(const struct app *app)
{
	const struct view *v = view_get(app->view);
	int x0 = 42, col_w = 22, rows = HEADER_H - 1;
	int max_x = COLS - LOGO_W - 3;
	size_t n = v->nhints + global_nhints;

	for (size_t i = 0; i < n; i++) {
		const struct key_hint *h = i < v->nhints ? &v->hints[i]
						: &global_hints[i - v->nhints];
		int x = x0 + (int)(i / rows) * col_w, y = (int)(i % rows);

		if (x + col_w > max_x)
			break;
		attron(COLOR_PAIR(CP_KEY) | A_BOLD);
		mvprintw(y, x, "%-10s", "");
		mvprintw(y, x, "<%s>", h->key);
		attroff(COLOR_PAIR(CP_KEY) | A_BOLD);
		attron(COLOR_PAIR(CP_KEY_DESC));
		mvprintw(y, x + 10, "%.11s", h->desc);
		attroff(COLOR_PAIR(CP_KEY_DESC));
	}
}

static void draw_logo(void)
{
	int x = COLS - LOGO_W - 1;

	if (x < 42)
		return;
	attron(COLOR_PAIR(CP_LOGO) | A_BOLD);
	for (size_t i = 0; i < sizeof(logo) / sizeof(logo[0]); i++)
		mvaddstr((int)i, x, logo[i]);
	attroff(COLOR_PAIR(CP_LOGO) | A_BOLD);
	attron(COLOR_PAIR(CP_KEY_DESC));
	mvprintw(4, x, "%*s", LOGO_W, "process monitor");
	attroff(COLOR_PAIR(CP_KEY_DESC));
}

/* ------------------------------------------------------------------ */
/* prompt, footer, dialog                                             */
/* ------------------------------------------------------------------ */

static void draw_prompt(const struct app *app, int y)
{
	struct rect r = { y, 0, PROMPT_H, COLS };
	const char *sigil = app->mode == MODE_COMMAND ? ":" : "/";

	ui_frame(r, "", NULL);
	attron(COLOR_PAIR(CP_TITLE) | A_BOLD);
	mvprintw(y + 1, 2, "%s>", sigil);
	attroff(COLOR_PAIR(CP_TITLE) | A_BOLD);
	attron(COLOR_PAIR(CP_INFO_VALUE));
	printw(" %s", app->prompt);
	attroff(COLOR_PAIR(CP_INFO_VALUE));
	attron(A_BLINK | COLOR_PAIR(CP_TITLE));
	addstr("▏");
	attroff(A_BLINK | COLOR_PAIR(CP_TITLE));
	if (app->mode == MODE_COMMAND) {
		attron(COLOR_PAIR(CP_KEY_DESC));
		mvprintw(y + 1, COLS - 40, "proc · tree · events · help · q");
		attroff(COLOR_PAIR(CP_KEY_DESC));
	}
}

static void crumb(const char *text, int pair)
{
	attron(COLOR_PAIR(pair) | A_BOLD);
	printw(" <%s> ", text);
	attroff(COLOR_PAIR(pair) | A_BOLD);
	addch(' ');
}

static void draw_footer(const struct app *app)
{
	int y = LINES - 1;

	move(y, 0);
	clrtoeol();
	move(y, 1);
	if (app->prev_view != app->view)
		crumb(view_get(app->prev_view)->name, CP_CRUMB);
	crumb(view_get(app->view)->name, CP_CRUMB_ACTIVE);

	if (app->flash[0] && ui_now() < app->flash_until) {
		int pair = app->flash_err ? CP_FLASH_ERR : CP_FLASH_INFO;
		int len = (int)strlen(app->flash);

		attron(COLOR_PAIR(pair));
		mvprintw(y, COLS - len - 2 > 0 ? COLS - len - 2 : 0, "%s", app->flash);
		attroff(COLOR_PAIR(pair));
	} else if (app->data.error[0]) {
		attron(COLOR_PAIR(CP_FLASH_ERR));
		mvprintw(y, COLS - (int)strlen(app->data.error) - 2 > 30
			 ? COLS - (int)strlen(app->data.error) - 2 : 30,
			 "%.*s", COLS - 32, app->data.error);
		attroff(COLOR_PAIR(CP_FLASH_ERR));
	}
}

static void draw_confirm(const struct app *app)
{
	struct rect r = { LINES / 2 - 3, COLS / 2 - 26, 7, 52 };
	const struct proc_row *p = model_selected(app);
	char msg[96];

	ui_fill(r, CP_DEFAULT);
	ui_frame(r, "kill", NULL);
	snprintf(msg, sizeof(msg), "Send SIGTERM to %d (%s)?", app->target_pid,
		 p ? p->comm : "?");
	ui_center_text(r, -1, CP_INFO_VALUE, msg);
	ui_center_text(r, 1, CP_KEY, "<y> OK      <n/esc> Cancel");
}

/* ------------------------------------------------------------------ */

size_t ui_draw(struct app *app, int *page)
{
	const struct view *v = view_get(app->view);
	int y = HEADER_H;
	struct rect body;
	size_t len;

	erase();
	draw_info(app);
	draw_hints(app);
	draw_logo();

	if (app->mode == MODE_COMMAND || app->mode == MODE_FILTER) {
		draw_prompt(app, y);
		y += PROMPT_H;
	}

	body = (struct rect){ y, 0, LINES - y - FOOTER_H, COLS };
	len = v->draw(app, body);
	*page = body.h - 3 > 1 ? body.h - 3 : 1;

	draw_footer(app);
	if (app->mode == MODE_CONFIRM)
		draw_confirm(app);
	refresh();
	return len;
}
