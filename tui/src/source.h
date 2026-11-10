/*
 * source.h - Data layer: reads /proc/procmon/ and parses it into
 *            plain C structs. Knows nothing about ncurses.
 *
 * STATUS: skeleton. Only detects whether the module is loaded.
 *         Parsers are implemented in step 3.
 */
#ifndef PMTOP_SOURCE_H
#define PMTOP_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "procmon_shared.h"

struct proc_row {
	pid_t    pid, ppid;
	uid_t    uid;
	char     state;
	int      nice, prio;
	unsigned threads;
	uint64_t utime_ns, stime_ns, start_ns;
	unsigned long vsz_kb, rss_kb;
	char     comm[PM_COMM_LEN * 4 + 1];  /* unescaped, room for UTF-8 */

	/* derived by metrics.c, not read from the kernel */
	double   cpu_pct;
	double   mem_pct;
};

struct sys_summary {
	char     version[32];
	uint64_t uptime_ns;
	unsigned nr_cpus;
	unsigned long load_x100[3];
	unsigned long mem_total_kb, mem_free_kb, mem_avail_kb;
	unsigned procs_total, procs_running, procs_sleeping;
	unsigned procs_stopped, procs_zombie, threads_total;
	uint64_t events_seq, events_dropped;
};

struct proc_event {
	uint64_t seq, ts_ns;
	int      type;           /* enum pm_event_type */
	pid_t    pid, ppid;
	int      exit_code;
	char     comm[PM_COMM_LEN * 4 + 1];
};

/* Everything one refresh produces. */
struct dataset {
	bool               module_loaded;
	char               error[160];     /* empty when OK */

	struct proc_row   *procs;          /* sorted by pid */
	size_t             nprocs, procs_cap;

	struct sys_summary sum;

	struct proc_event *events;         /* history, oldest first */
	size_t             nevents, events_cap;
	uint64_t           last_event_seq;

	double             taken_at;       /* monotonic seconds */
};

/* Re-read all three files into *d. Returns 0 or -errno. */
int  source_refresh(struct dataset *d);
void source_free(struct dataset *d);

#endif
