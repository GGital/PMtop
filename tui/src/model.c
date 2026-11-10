#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "model.h"

static const struct app *sort_ctx;   /* qsort has no user pointer */

static int cmp_rows(const void *a, const void *b)
{
	const struct proc_row *x = &sort_ctx->data.procs[*(const size_t *)a];
	const struct proc_row *y = &sort_ctx->data.procs[*(const size_t *)b];
	int r = 0;

	switch (sort_ctx->sort_key) {
	case SORT_PID:     r = (x->pid > y->pid) - (x->pid < y->pid); break;
	case SORT_NAME:    r = strcasecmp(x->comm, y->comm); break;
	case SORT_CPU:     r = (x->cpu_pct > y->cpu_pct) - (x->cpu_pct < y->cpu_pct); break;
	case SORT_MEM:     r = (x->rss_kb > y->rss_kb) - (x->rss_kb < y->rss_kb); break;
	case SORT_THREADS: r = (x->threads > y->threads) - (x->threads < y->threads); break;
	case SORT_STATE:   r = x->state - y->state; break;
	}
	if (r == 0)
		r = (x->pid > y->pid) - (x->pid < y->pid);
	return sort_ctx->sort_desc ? -r : r;
}

static bool matches(const struct proc_row *p, const char *filter)
{
	char pidbuf[16];

	if (!filter[0])
		return true;
	if (strcasestr(p->comm, filter))
		return true;
	snprintf(pidbuf, sizeof(pidbuf), "%d", p->pid);
	return strstr(pidbuf, filter) != NULL;
}

void model_rebuild(struct app *app)
{
	pid_t keep = -1;
	const struct proc_row *sel = model_selected(app);

	if (sel)
		keep = sel->pid;

	free(app->visible);
	app->visible = calloc(app->data.nprocs ? app->data.nprocs : 1,
			      sizeof(*app->visible));
	app->nvisible = 0;
	if (!app->visible)
		return;

	for (size_t i = 0; i < app->data.nprocs; i++)
		if (matches(&app->data.procs[i], app->filter))
			app->visible[app->nvisible++] = i;

	sort_ctx = app;
	qsort(app->visible, app->nvisible, sizeof(*app->visible), cmp_rows);

	/* keep the cursor on the same process if it still exists */
	app->selected = 0;
	for (size_t i = 0; keep >= 0 && i < app->nvisible; i++) {
		if (app->data.procs[app->visible[i]].pid == keep) {
			app->selected = (int)i;
			break;
		}
	}
}

const struct proc_row *model_selected(const struct app *app)
{
	if (!app->visible || app->selected < 0 ||
	    (size_t)app->selected >= app->nvisible)
		return NULL;
	return &app->data.procs[app->visible[app->selected]];
}

const char *model_sort_name(sort_key_t k)
{
	static const char *names[] = { "PID", "NAME", "CPU", "MEM", "THR", "STATE" };
	return names[k];
}
