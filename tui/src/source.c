/*
 * source.c - Reads and parses the procmon text interface.
 *
 * STATUS: skeleton. Implemented in step 3:
 *   - parse_processes(): check PM_PROCESSES_MAGIC + version, split each
 *     line on TAB into PMF_COUNT fields, unescape comm (\ooo octal).
 *   - parse_summary():   "key\tvalue" -> struct sys_summary, ignore
 *                        unknown keys.
 *   - parse_events():    append only events with seq > last_event_seq,
 *                        cap history to a fixed size.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "source.h"

static double now_monotonic(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

int source_refresh(struct dataset *d)
{
	d->taken_at = now_monotonic();
	d->error[0] = '\0';

	if (access(PM_PATH_PROCESSES, R_OK) != 0) {
		int err = errno;

		d->module_loaded = false;
		snprintf(d->error, sizeof(d->error),
			 "%s: %s", PM_PROC_ROOT, strerror(err));
		d->nprocs = 0;
		return -err;
	}
	d->module_loaded = true;

	/* TODO(step 3): parse_processes(d); parse_summary(d); parse_events(d); */
	return 0;
}

void source_free(struct dataset *d)
{
	free(d->procs);
	free(d->events);
	memset(d, 0, sizeof(*d));
}
