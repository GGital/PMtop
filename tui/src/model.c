#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "model.h"
#include "users.h"

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
	if (strcasestr(user_name(p->uid), filter))
		return true;
	snprintf(pidbuf, sizeof(pidbuf), "%d", p->pid);
	return strstr(pidbuf, filter) != NULL;
}

pid_t model_selected_pid(const struct app *app)
{
	const struct proc_row *sel = model_selected(app);

	return sel ? sel->pid : -1;
}

/* Sort or filter changed: start again from the top, like k9s. */
void model_rebuild(struct app *app)
{
	app->cursor[VIEW_PROCESSES] = 0;
	app->scrolls[VIEW_PROCESSES] = 0;
	model_rebuild_keep(app, -1);
}

/*
 * `keep` must be captured while visible[] still indexes app->data.
 * After a refresh swaps data/prev, visible[] points into the OLD
 * buffer, so refresh_data() grabs the pid before the swap.
 */
void model_rebuild_keep(struct app *app, pid_t keep)
{
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

	/* keep the cursor on the same process if it still exists,
	 * otherwise stay at the same row index (table_draw clamps it) */
	for (size_t i = 0; keep >= 0 && i < app->nvisible; i++) {
		if (app->data.procs[app->visible[i]].pid == keep) {
			app->cursor[VIEW_PROCESSES] = (int)i;
			break;
		}
	}
}

const struct proc_row *model_selected(const struct app *app)
{
	if (!app->visible || app->cursor[VIEW_PROCESSES] < 0 ||
	    (size_t)app->cursor[VIEW_PROCESSES] >= app->nvisible)
		return NULL;
	return &app->data.procs[app->visible[app->cursor[VIEW_PROCESSES]]];
}

const struct proc_row *model_find(const struct app *app, pid_t pid, uint64_t start_ns)
{
	size_t lo = 0, hi = app->data.nprocs;

	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		const struct proc_row *p = &app->data.procs[mid];

		if (p->pid == pid)
			return (!start_ns || p->start_ns == start_ns) ? p : NULL;
		if (p->pid < pid)
			lo = mid + 1;
		else
			hi = mid;
	}
	return NULL;
}

const char *model_sort_name(sort_key_t k)
{
	static const char *names[] = { "PID", "NAME", "CPU", "MEM", "THR", "STATE" };
	return names[k];
}
