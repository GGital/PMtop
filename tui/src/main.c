/*
 * main.c - pmtop: a k9s-style terminal UI for the procmon kernel module.
 *
 *   usage: pmtop [-d refresh_ms] [-h]
 *
 * Main loop:
 *   every refresh_ms : swap(prev, data); source_refresh(data);
 *                      metrics_compute(data, prev); model_rebuild()
 *   every iteration  : ui_draw(); getch() with 100 ms timeout; input_handle()
 */
#include <getopt.h>
#include <locale.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "input.h"
#include "metrics.h"
#include "model.h"
#include "source.h"
#include "ui.h"

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s [-d refresh_ms] [-h]\n"
		"  -d ms   refresh interval in milliseconds (default 1000, min 100)\n"
		"  -h      this help\n", argv0);
}

static void refresh_data(struct app *app)
{
	/* Reuse buffers: the old `prev` becomes the new `data`. */
	struct dataset tmp = app->prev;

	app->prev = app->data;
	app->data = tmp;
	/* TODO(step 3): event history should live outside the swap. */
	source_refresh(&app->data);
	metrics_compute(&app->data, &app->prev);
	model_rebuild(app);
}

int main(int argc, char **argv)
{
	struct app app;
	double next_refresh = 0;
	int opt, page = 10;
	size_t list_len = 0;

	memset(&app, 0, sizeof(app));
	app.running = true;
	app.refresh_ms = 1000;
	app.view = app.prev_view = VIEW_PROCESSES;
	app.sort_key = SORT_CPU;
	app.sort_desc = true;

	while ((opt = getopt(argc, argv, "d:h")) != -1) {
		switch (opt) {
		case 'd':
			app.refresh_ms = atoi(optarg);
			if (app.refresh_ms < 100)
				app.refresh_ms = 100;
			break;
		default:
			usage(argv[0]);
			return opt == 'h' ? 0 : 1;
		}
	}

	setlocale(LC_ALL, "");     /* UTF-8 box drawing / gauges */
	ui_init();

	while (app.running) {
		double now = ui_now();
		int ch;

		if (now >= next_refresh) {
			refresh_data(&app);
			next_refresh = now + app.refresh_ms / 1000.0;
		}
		list_len = ui_draw(&app, &page);

		ch = getch();
		if (ch == ERR || ch == KEY_RESIZE)
			continue;
		input_handle(&app, ch, list_len, page);
	}

	ui_shutdown();
	source_free(&app.data);
	source_free(&app.prev);
	free(app.visible);
	return 0;
}
