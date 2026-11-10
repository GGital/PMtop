/*
 * main.c - pmtop: a k9s-style terminal UI for the procmon kernel module.
 *
 *   usage: pmtop [-d refresh_ms] [-h]
 *
 * Main loop:
 *   every refresh_ms : swap(prev, data); source_refresh(data);
 *                      source_read_events(events);
 *                      metrics_compute(data, prev); model_rebuild_keep()
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
#include "views/view.h"

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s [-d refresh_ms] [-h]\n"
		"  -d ms   refresh interval in milliseconds (default 1000, min 100)\n"
		"  -h      this help\n", argv0);
}

static void refresh_data(struct app *app)
{
	pid_t keep = model_selected_pid(app);   /* before visible[] goes stale */
	struct dataset tmp = app->prev;
	bool was_loaded = app->data.module_loaded;

	/* Reuse buffers: the old `prev` becomes the new `data`. */
	app->prev = app->data;
	app->data = tmp;

	source_refresh(&app->data);
	if (app->data.module_loaded) {
		/* module reloaded -> its seq counter restarted */
		if (app->data.sum.events_seq < app->events.last_seq)
			app->events.last_seq = 0;
		source_read_events(&app->events);
		if (!was_loaded && app->prev.taken_at > 0)
			app_flash(app, false, "procmon module detected");
	} else if (was_loaded) {
		app_flash(app, true, "procmon module went away");
	}

	metrics_compute(&app->data, &app->prev);
	model_rebuild_keep(app, keep);
	detail_on_refresh(app);
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

		if (now >= next_refresh || app.force_refresh) {
			app.force_refresh = false;
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
	event_log_free(&app.events);
	free(app.visible);
	return 0;
}
