/*
 * source.h - Data layer: reads /proc/procmon/ and parses it into
 *            plain C structs. Knows nothing about ncurses.
 *
 * The parsers take a FILE* so tests can feed them fixture text
 * (fmemopen) without the kernel module being loaded.
 */
#ifndef PMTOP_SOURCE_H
#define PMTOP_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include "procmon_shared.h"

#define COMM_MAX       (PM_COMM_LEN * 4 + 1)  /* unescaped, room for UTF-8 */
#define EVENT_LOG_MAX  5000                   /* history kept by the TUI   */

struct proc_row {
	pid_t    pid, ppid;
	uid_t    uid;
	char     state;
	int      nice, prio;
	unsigned threads;
	uint64_t utime_ns, stime_ns, start_ns;
	unsigned long vsz_kb, rss_kb;
	char     comm[COMM_MAX];

	/* derived by metrics.c, not read from the kernel */
	double   cpu_pct;   /* % of ONE cpu, like top (can exceed 100) */
	double   mem_pct;   /* rss / total RAM                          */
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
	char     comm[COMM_MAX];
};

/* Event history. Lives in struct app, NOT in a dataset, because it
 * accumulates across refreshes while datasets are swapped each time. */
struct event_log {
	struct proc_event *items;     /* oldest first */
	size_t             len;
	uint64_t           last_seq;  /* highest seq already stored */
	uint64_t           missed;    /* seq gaps: overwritten in the kernel
	                               * ring before we got to read them */
};

/* Everything one refresh produces. Two of these (cur + prev) are
 * swapped every refresh so buffers get reused. */
struct dataset {
	bool               module_loaded;
	char               error[160];     /* empty when OK */

	struct proc_row   *procs;          /* sorted by pid */
	size_t             nprocs, procs_cap;
	size_t             bad_lines;      /* lines that failed to parse */

	struct sys_summary sum;

	double             taken_at;       /* monotonic seconds */
	double             cpu_total_pct;  /* metrics.c: all procs / all cpus */
};

/* Re-read processes + summary into *d. Returns 0 or -errno. */
int  source_refresh(struct dataset *d);
/* Append new events (seq > log->last_seq). Returns 0 or -errno. */
int  source_read_events(struct event_log *log);

void source_free(struct dataset *d);
void event_log_free(struct event_log *log);

/* --- building blocks, exposed for tests --- */
int    source_parse_processes(FILE *fp, struct dataset *d);
int    source_parse_summary(FILE *fp, struct sys_summary *s);
int    source_parse_events(FILE *fp, struct event_log *log);
size_t source_unescape(char *dst, size_t len, const char *src);

#endif
