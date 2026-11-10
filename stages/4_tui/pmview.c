/*
 * pmview.c - Stage 4: a k9s-style terminal UI for the pmon v2.0 module.
 *
 * This is a NORMAL user-space program (not part of the kernel). It shows
 * how user space uses a kernel module: it simply reads the two files the
 * module creates, once per second, and draws them with ncurses.
 *
 *   /proc/pmon          -> "processes" view (table + memory bars)
 *   /proc/pmon_events   -> "events" view    (fork / exit stream)
 *   /sys/module/pmon/version -> module version shown in the header
 *
 * Build:  make            Run:  ./pmview     (load stages/3_events/pmon.ko first)
 *
 * Keys:   Tab / 1 / 2   switch view          /       filter (Esc clears; in events
 *                                                    also "killed", "exit code 3")
 *         j k  ↑ ↓      move                 P N R T sort by PID/Name/RSS/Threads
 *         g G           top / bottom         q       quit
 */
#define _GNU_SOURCE
#include <locale.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/utsname.h>
#include <time.h>

#define PROC_LIST    "/proc/pmon"
#define PROC_EVENTS  "/proc/pmon_events"
#define VERSION_FILE "/sys/module/pmon/version"
#define MAX_PROCS    8192
#define MAX_EVENTS   256
#define HEADER_H     6

/* ================================================================== */
/* 1. DATA: what we read from the kernel module                       */
/* ================================================================== */

struct proc {
	int pid, ppid, threads;
	char state;
	unsigned long rss_kb;
	char name[64];
};

struct event {
	double boot_sec;        /* seconds since boot, as the kernel prints it */
	char type[8];           /* "FORK" or "EXIT" */
	int pid, ppid;
	char name[20];
	char result[40];        /* "exit code 0", "killed by signal 9", or "" */
};

static struct proc procs[MAX_PROCS];
static int nprocs;
static struct event events[MAX_EVENTS];
static int nevents;
static unsigned long total_forks, total_exits;
static char version[32];
static bool have_list, have_events;

/*
 * /proc/pmon looks like:
 *   PID     PPID    STATE THREADS RSS(KB)    NAME
 *   1       0       S     1       15004      systemd
 *   total: 341 processes
 * sscanf() matches the data lines; the header and "total:" line don't
 * start with numbers, so they are skipped automatically.
 */
static void read_processes(void)
{
	char line[256];
	FILE *f = fopen(PROC_LIST, "r");

	nprocs = 0;
	have_list = (f != NULL);
	if (!f)
		return;
	while (fgets(line, sizeof(line), f) && nprocs < MAX_PROCS) {
		struct proc *p = &procs[nprocs];

		if (sscanf(line, "%d %d %c %d %lu %63[^\n]", &p->pid, &p->ppid,
			   &p->state, &p->threads, &p->rss_kb, p->name) == 6)
			nprocs++;
	}
	fclose(f);
}

/*
 * /proc/pmon_events looks like:
 *   TIME           EVENT PID     PPID    NAME             RESULT
 *   [ 3867.611836] EXIT  82651   81469   sleep            killed by signal 9
 *   total since load: 15 forks, 16 exits
 * NAME is always padded to 16 characters, so RESULT starts 17 chars later.
 */
static void read_events(void)
{
	char line[256];
	FILE *f = fopen(PROC_EVENTS, "r");

	nevents = 0;
	have_events = (f != NULL);
	if (!f)
		return;
	while (fgets(line, sizeof(line), f) && nevents < MAX_EVENTS) {
		struct event *e = &events[nevents];
		char *rest;
		int n = 0;

		line[strcspn(line, "\n")] = '\0';
		if (sscanf(line, "total since load: %lu forks, %lu exits",
			   &total_forks, &total_exits) == 2)
			continue;
		if (sscanf(line, "[%lf] %7s %d %d %n", &e->boot_sec, e->type,
			   &e->pid, &e->ppid, &n) != 4 || n == 0)
			continue;                               /* header line */
		rest = line + n;
		snprintf(e->name, sizeof(e->name), "%.16s", rest);
		e->name[strcspn(e->name, " ")] = '\0';     /* drop the padding */
		snprintf(e->result, sizeof(e->result), "%s",
			 strlen(rest) > 17 ? rest + 17 : "");
		nevents++;
	}
	fclose(f);
}

static void read_version(void)
{
	FILE *f = fopen(VERSION_FILE, "r");

	strcpy(version, "?");
	if (f) {
		if (fgets(version, sizeof(version), f))
			version[strcspn(version, "\n")] = '\0';
		fclose(f);
	}
}

static void refresh_data(void)
{
	read_processes();
	read_events();
	read_version();
}

/* ================================================================== */
/* 2. VIEW STATE: what the user is looking at                         */
/* ================================================================== */

enum { VIEW_PROCS, VIEW_EVENTS };
static int view = VIEW_PROCS;
static int cursor[2], top[2];        /* selected row, first row on screen */
static char search[32];
static bool editing_filter;
static char sort_key = 'R';          /* P=pid N=name R=rss T=threads */

static int rows[MAX_PROCS];          /* indexes of the rows to show, in order */
static int nrows;

/* Does a row match the "/" filter? `extra` = more text to search (or NULL). */
static bool match(const char *name, int pid, const char *extra)
{
	char pidstr[16];

	if (!search[0])
		return true;
	snprintf(pidstr, sizeof(pidstr), "%d", pid);
	return strcasestr(name, search) || strstr(pidstr, search) ||
	       (extra && strcasestr(extra, search));
}

static int cmp_procs(const void *a, const void *b)
{
	const struct proc *x = &procs[*(const int *)a], *y = &procs[*(const int *)b];

	switch (sort_key) {
	case 'N': return strcasecmp(x->name, y->name);
	case 'R': return (y->rss_kb > x->rss_kb) - (y->rss_kb < x->rss_kb);  /* big first */
	case 'T': return y->threads - x->threads;                           /* many first */
	default:  return x->pid - y->pid;
	}
}

/* Build rows[]: search, then sort (processes) or newest-first (events). */
static void build_rows(void)
{
	nrows = 0;
	if (view == VIEW_PROCS) {
		for (int i = 0; i < nprocs; i++)
			if (match(procs[i].name, procs[i].pid, NULL))
				rows[nrows++] = i;
		qsort(rows, nrows, sizeof(rows[0]), cmp_procs);
	} else {
		for (int i = nevents - 1; i >= 0; i--) {
			char text[64];      /* events: also match "FORK", "killed", "exit code 3" */

			snprintf(text, sizeof(text), "%s %s", events[i].type, events[i].result);
			if (match(events[i].name, events[i].pid, text))
				rows[nrows++] = i;
		}
	}
	if (cursor[view] >= nrows)
		cursor[view] = nrows > 0 ? nrows - 1 : 0;
}

/* ================================================================== */
/* 3. DRAWING                                                         */
/* ================================================================== */

enum {
	C_LABEL = 1, C_VALUE, C_KEY, C_HINT, C_BORDER, C_TITLE, C_COUNT,
	C_HEAD, C_ROW, C_SELECT, C_RUN, C_WARN, C_BAD, C_DIM, C_CRUMB, C_BAR
};

static void init_colors(void)
{
	bool c256 = COLORS >= 256;

	start_color();
	use_default_colors();
	init_pair(C_LABEL,  c256 ? 208 : COLOR_YELLOW,  -1);   /* orange  */
	init_pair(C_VALUE,  c256 ? 255 : COLOR_WHITE,   -1);
	init_pair(C_KEY,    c256 ? 33  : COLOR_BLUE,    -1);   /* blue    */
	init_pair(C_HINT,   c256 ? 245 : COLOR_WHITE,   -1);   /* gray    */
	init_pair(C_BORDER, c256 ? 33  : COLOR_BLUE,    -1);
	init_pair(C_TITLE,  c256 ? 51  : COLOR_CYAN,    -1);   /* aqua    */
	init_pair(C_COUNT,  c256 ? 201 : COLOR_MAGENTA, -1);   /* fuchsia */
	init_pair(C_HEAD,   c256 ? 255 : COLOR_WHITE,   -1);
	init_pair(C_ROW,    c256 ? 51  : COLOR_CYAN,    -1);
	init_pair(C_SELECT, c256 ? 16  : COLOR_BLACK, c256 ? 51 : COLOR_CYAN);
	init_pair(C_RUN,    c256 ? 46  : COLOR_GREEN,   -1);
	init_pair(C_WARN,   c256 ? 226 : COLOR_YELLOW,  -1);
	init_pair(C_BAD,    c256 ? 196 : COLOR_RED,     -1);
	init_pair(C_DIM,    c256 ? 245 : COLOR_WHITE,   -1);
	init_pair(C_CRUMB,  c256 ? 16  : COLOR_BLACK, c256 ? 208 : COLOR_YELLOW);
	init_pair(C_BAR,    c256 ? 208 : COLOR_YELLOW,  -1);
}

/* print with a color, at the current cursor position */
#define COLOR_PRINT(pair, ...) do { attron(COLOR_PAIR(pair)); printw(__VA_ARGS__); \
				    attroff(COLOR_PAIR(pair)); } while (0)

static void draw_header(void)
{
	static const char *logo[] = { "┌─┐┌┬┐┬  ┬", "├─┘│││└┐┌┘", "┴  ┴ ┴ └┘ " };
	static const char *keys[][2] = {
		{ "tab", "Switch view" }, { "/", "Filter" }, { "j/k", "Move" },
		{ "P", "Sort PID" }, { "N", "Sort Name" }, { "R", "Sort RSS" },
		{ "T", "Sort Threads" }, { "q", "Quit" },
	};
	struct utsname u;
	int running = 0, zombie = 0;

	uname(&u);
	for (int i = 0; i < nprocs; i++) {
		running += procs[i].state == 'R';
		zombie += procs[i].state == 'Z';
	}

	/* left: info */
	move(0, 1); COLOR_PRINT(C_LABEL, "Host:    "); COLOR_PRINT(C_VALUE, "%s", u.nodename);
	move(1, 1); COLOR_PRINT(C_LABEL, "Kernel:  "); COLOR_PRINT(C_VALUE, "%s", u.release);
	move(2, 1); COLOR_PRINT(C_LABEL, "Module:  ");
	if (have_list)
		COLOR_PRINT(C_VALUE, "pmon v%s", version);
	else
		COLOR_PRINT(C_BAD, "pmon not loaded");
	move(3, 1); COLOR_PRINT(C_LABEL, "Procs:   ");
	COLOR_PRINT(C_VALUE, "%d  (running %d, zombie %d)", nprocs, running, zombie);
	move(4, 1); COLOR_PRINT(C_LABEL, "Events:  ");
	if (have_events)
		COLOR_PRINT(C_VALUE, "%lu forks, %lu exits since load", total_forks, total_exits);
	else
		COLOR_PRINT(C_DIM, "needs pmon v2.0 (stage 3)");

	/* middle: key hints in two columns */
	for (int i = 0; i < 8; i++) {
		int x = 52 + (i / 4) * 20, y = i % 4;

		if (x + 20 > COLS - 14)
			break;
		move(y, x); COLOR_PRINT(C_KEY, "<%s>", keys[i][0]);
		move(y, x + 6); COLOR_PRINT(C_HINT, "%s", keys[i][1]);
	}

	/* right: logo */
	if (COLS > 70)
		for (int i = 0; i < 3; i++) {
			attron(COLOR_PAIR(C_LABEL) | A_BOLD);
			mvaddstr(i, COLS - 12, logo[i]);
			attroff(COLOR_PAIR(C_LABEL) | A_BOLD);
		}
}

/* a box with a centered " title[count] " in the top border */
static void draw_box(int y, int x, int h, int w, const char *title, const char *count)
{
	int tx = x + (w - (int)strlen(title) - (int)strlen(count) - 2) / 2;

	attron(COLOR_PAIR(C_BORDER));
	mvaddch(y, x, ACS_ULCORNER);         mvhline(y, x + 1, ACS_HLINE, w - 2);
	mvaddch(y, x + w - 1, ACS_URCORNER); mvvline(y + 1, x, ACS_VLINE, h - 2);
	mvvline(y + 1, x + w - 1, ACS_VLINE, h - 2);
	mvaddch(y + h - 1, x, ACS_LLCORNER); mvhline(y + h - 1, x + 1, ACS_HLINE, w - 2);
	mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
	attroff(COLOR_PAIR(C_BORDER));
	attron(A_BOLD);
	move(y, tx); COLOR_PRINT(C_TITLE, " %s", title); COLOR_PRINT(C_COUNT, "%s ", count);
	attroff(A_BOLD);
}

static int state_color(const struct proc *p)
{
	switch (p->state) {
	case 'R':           return C_RUN;
	case 'D': case 'T': return C_WARN;
	case 'Z':           return C_BAD;
	default:            return p->rss_kb == 0 ? C_DIM : C_ROW;   /* kernel thread */
	}
}

static int event_color(const struct event *e)
{
	if (!strcmp(e->type, "FORK"))
		return C_RUN;
	if (strstr(e->result, "killed"))
		return C_BAD;
	return strcmp(e->result, "exit code 0") ? C_WARN : C_DIM;
}

/* kernel time (seconds since boot) -> wall clock "21:46:07.611" */
static void boot_to_clock(double boot_sec, char *buf, size_t len)
{
	struct timespec now_boot, now_real;
	double t;
	time_t sec;

	clock_gettime(CLOCK_BOOTTIME, &now_boot);
	clock_gettime(CLOCK_REALTIME, &now_real);
	t = now_real.tv_sec + now_real.tv_nsec / 1e9
	    - (now_boot.tv_sec + now_boot.tv_nsec / 1e9 - boot_sec);
	sec = (time_t)t;
	strftime(buf, len, "%H:%M:%S", localtime(&sec));
	snprintf(buf + strlen(buf), len - strlen(buf), ".%03d", (int)((t - sec) * 1000));
}

/* one table row; `sel` = draw as the cursor bar */
static void draw_proc_row(int y, int w, const struct proc *p, unsigned long max_rss, bool sel)
{
	int bar = max_rss ? (int)(10.0 * p->rss_kb / max_rss + 0.5) : 0;
	int pair = sel ? C_SELECT : state_color(p);

	attron(COLOR_PAIR(pair));
	mvhline(y, 1, ' ', w - 2);
	mvprintw(y, 2, "%7d %7d  %c %7d %9lu ", p->pid, p->ppid, p->state, p->threads, p->rss_kb);
	attroff(COLOR_PAIR(pair));
	/* memory bar, relative to the biggest process */
	attron(COLOR_PAIR(sel ? C_SELECT : C_BAR));
	for (int i = 0; i < 10; i++)
		addstr(i < bar ? "█" : (sel ? " " : "·"));
	attroff(COLOR_PAIR(sel ? C_SELECT : C_BAR));
	attron(COLOR_PAIR(pair));
	printw("  %.*s", w - 62 > 4 ? w - 62 : 4, p->name);
	attroff(COLOR_PAIR(pair));
}

static void draw_event_row(int y, int w, const struct event *e, bool sel)
{
	char clock[32];
	int pair = sel ? C_SELECT : event_color(e);

	boot_to_clock(e->boot_sec, clock, sizeof(clock));
	attron(COLOR_PAIR(pair));
	mvhline(y, 1, ' ', w - 2);
	mvprintw(y, 2, "%-12s  %-5s %7d %7d  %-16s %.*s", clock, e->type, e->pid,
		 e->ppid, e->name, w - 64 > 4 ? w - 64 : 4, e->result);
	attroff(COLOR_PAIR(pair));
}

/*
 * One column title padded to `width` SCREEN columns. printf's "%7s" pads by
 * bytes, but an arrow like "↓" is 3 bytes and only 1 column wide, so we pad
 * by hand. `gap` = spaces after the cell.
 */
static void head_cell(int width, char align, const char *title, const char *arrow, int gap)
{
	int cols = (int)strlen(title) + (arrow[0] ? 1 : 0);
	int pad = width > cols ? width - cols : 0;

	if (align == 'R')
		printw("%*s", pad, "");
	printw("%s%s", title, arrow);
	if (align == 'L')
		printw("%*s", pad, "");
	printw("%*s", gap, "");
}

static void draw_body(void)
{
	int y = HEADER_H, h = LINES - HEADER_H - 1, w = COLS;
	int body_rows = h - 3;                 /* minus borders and column header */
	char title[32], count[64];
	unsigned long max_rss = 1;

	snprintf(title, sizeof(title), view == VIEW_PROCS ? "processes" : "events");
	snprintf(count, sizeof(count), "[%d]%s%s%s", nrows,
		 search[0] ? " </" : "", search, search[0] ? ">" : "");
	draw_box(y, 0, h, w, title, count);

	if (!have_list || (view == VIEW_EVENTS && !have_events)) {
		const char *msg = !have_list
			? "pmon is not loaded:  cd stages/3_events && sudo insmod pmon.ko"
			: "/proc/pmon_events missing: load pmon v2.0 (stage 3), not v1.0";
		attron(COLOR_PAIR(C_BAD));
		mvprintw(y + h / 2, (w - (int)strlen(msg)) / 2, "%s", msg);
		attroff(COLOR_PAIR(C_BAD));
		return;
	}

	/* column header; the arrow marks the current sort. Widths and gaps
	 * match the printf formats of draw_proc_row() / draw_event_row(). */
	attron(COLOR_PAIR(C_HEAD) | A_BOLD);
	move(y + 1, 2);
	if (view == VIEW_PROCS) {
		head_cell(7, 'R', "PID", sort_key == 'P' ? "↑" : "", 1);
		head_cell(7, 'R', "PPID", "", 2);
		head_cell(1, 'L', "S", "", 1);
		head_cell(7, 'R', "THR", sort_key == 'T' ? "↓" : "", 1);
		head_cell(9, 'R', "RSS(KB)", sort_key == 'R' ? "↓" : "", 1);
		head_cell(10, 'L', "MEM", "", 2);
		head_cell(4, 'L', "NAME", sort_key == 'N' ? "↑" : "", 0);
	} else {
		head_cell(12, 'L', "TIME", "↓", 2);
		head_cell(5, 'L', "EVENT", "", 1);
		head_cell(7, 'R', "PID", "", 1);
		head_cell(7, 'R', "PPID", "", 2);
		head_cell(16, 'L', "NAME", "", 1);
		head_cell(6, 'L', "RESULT", "", 0);
	}
	attroff(COLOR_PAIR(C_HEAD) | A_BOLD);

	/* keep the cursor on screen */
	if (cursor[view] < top[view])
		top[view] = cursor[view];
	if (cursor[view] >= top[view] + body_rows)
		top[view] = cursor[view] - body_rows + 1;

	for (int i = 0; i < nprocs; i++)
		if (procs[i].rss_kb > max_rss)
			max_rss = procs[i].rss_kb;

	for (int r = 0; r < body_rows && top[view] + r < nrows; r++) {
		int idx = rows[top[view] + r];
		bool sel = (top[view] + r == cursor[view]);

		if (view == VIEW_PROCS)
			draw_proc_row(y + 2 + r, w, &procs[idx], max_rss, sel);
		else
			draw_event_row(y + 2 + r, w, &events[idx], sel);
	}
}

/* bottom line: k9s-style breadcrumbs, or the search being typed */
static void draw_footer(void)
{
	move(LINES - 1, 0);
	clrtoeol();
	if (editing_filter) {
		COLOR_PRINT(C_TITLE, " /%s", search);
		COLOR_PRINT(C_HINT, "▏  (Enter keep, Esc clear)");
		return;
	}
	attron(COLOR_PAIR(C_CRUMB) | A_BOLD);
	printw(" <%s> ", view == VIEW_PROCS ? "processes" : "events");
	attroff(COLOR_PAIR(C_CRUMB) | A_BOLD);
	COLOR_PRINT(C_HINT, "   data from %s, refreshed every second",
		    view == VIEW_PROCS ? PROC_LIST : PROC_EVENTS);
}

static void draw(void)
{
	erase();
	build_rows();
	draw_header();
	draw_body();
	draw_footer();
	refresh();
}

/* ================================================================== */
/* 4. KEYBOARD + MAIN LOOP                                            */
/* ================================================================== */

/* returns false when the user wants to quit */
static bool handle_key(int ch)
{
	int page = LINES - HEADER_H - 4;

	if (editing_filter) {                       /* typing after "/" */
		size_t len = strlen(search);

		if (ch == 27) {                         /* Esc: clear */
			search[0] = '\0';
			editing_filter = false;
		} else if (ch == '\n' || ch == KEY_ENTER) {
			editing_filter = false;
		} else if ((ch == KEY_BACKSPACE || ch == 127 || ch == 8) && len) {
			search[len - 1] = '\0';
		} else if (ch >= 32 && ch < 127 && len + 1 < sizeof(search)) {
			search[len] = (char)ch;
			search[len + 1] = '\0';
		}
		cursor[view] = 0;
		return true;
	}

	switch (ch) {
	case 'q': case 3:              return false;          /* q or Ctrl-C */
	case 12:                       clearok(curscr, TRUE); break;  /* Ctrl-L: full redraw */
	case '\t':                     view = !view; break;
	case '1':                      view = VIEW_PROCS; break;
	case '2':                      view = VIEW_EVENTS; break;
	case '/':                      editing_filter = true; break;
	case 27:                       search[0] = '\0'; break;  /* Esc */
	case 'j': case KEY_DOWN:       cursor[view]++; break;
	case 'k': case KEY_UP:         cursor[view]--; break;
	case KEY_NPAGE:                cursor[view] += page; break;
	case KEY_PPAGE:                cursor[view] -= page; break;
	case 'g': case KEY_HOME:       cursor[view] = 0; break;
	case 'G': case KEY_END:        cursor[view] = nrows - 1; break;
	case 'P': case 'N': case 'R': case 'T':
		sort_key = (char)ch;
		cursor[VIEW_PROCS] = 0;
		break;
	}
	if (cursor[view] >= nrows)
		cursor[view] = nrows - 1;
	if (cursor[view] < 0)
		cursor[view] = 0;
	return true;
}

static double now_sec(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

int main(void)
{
	double next_refresh = 0;
	bool running = true;

	setlocale(LC_ALL, "");        /* needed for the █ ┌ ─ characters */
	initscr();
	raw();                        /* get Ctrl-C as a key, exit cleanly */
	noecho();
	keypad(stdscr, TRUE);         /* arrow keys */
	curs_set(0);
	set_escdelay(25);
	timeout(100);                 /* getch() waits at most 100 ms */
	if (has_colors())
		init_colors();

	while (running) {
		int ch;

		if (now_sec() >= next_refresh) {     /* re-read the module's files */
			refresh_data();
			next_refresh = now_sec() + 1.0;
		}
		draw();
		ch = getch();
		if (ch != ERR)
			running = handle_key(ch);
	}
	endwin();
	return 0;
}
