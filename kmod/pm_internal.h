/* SPDX-License-Identifier: GPL-2.0 */
/*
 * pm_internal.h - Kernel-internal types and the API between the
 *                 module's own compilation units.
 *
 *   procmon_main.c  -> module_init/exit, parameters, wires everything up
 *   pm_collect.c    -> reads task_structs, produces snapshots/summary
 *   pm_procfs.c     -> /proc/procmon/ files (formatting only)
 *   pm_events.c     -> fork/exec/exit hooks + ring buffer
 *
 * Rule of thumb: pm_collect.c and pm_events.c know about kernel
 * internals; pm_procfs.c only knows about the structs in this file.
 */
#ifndef PM_INTERNAL_H
#define PM_INTERNAL_H

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/seq_file.h>

#include "procmon_shared.h"

#define PM_NAME "procmon"

#define pm_info(fmt, ...) pr_info(PM_NAME ": " fmt, ##__VA_ARGS__)
#define pm_warn(fmt, ...) pr_warn(PM_NAME ": " fmt, ##__VA_ARGS__)
#define pm_err(fmt, ...)  pr_err(PM_NAME ": " fmt, ##__VA_ARGS__)

/* ------------------------------------------------------------------ */
/* Process snapshot (pm_collect.c)                                    */
/* ------------------------------------------------------------------ */

/* One process, copied out of its task_struct. Plain data, no pointers. */
struct pm_proc_info {
	pid_t         pid;
	pid_t         ppid;
	uid_t         uid;
	char          state;
	int           nice;
	int           prio;
	u32           nr_threads;
	u64           utime_ns;
	u64           stime_ns;
	u64           start_ns;
	unsigned long vsz_kb;
	unsigned long rss_kb;
	char          comm[TASK_COMM_LEN];
};

/*
 * A consistent copy of all processes, taken once when a reader opens
 * /proc/procmon/processes. Taking the copy up front means we hold the
 * RCU read lock only briefly and never across copy_to_user().
 */
struct pm_snapshot {
	size_t              count;
	struct pm_proc_info procs[];  /* flexible array, kvmalloc'd */
};

struct pm_snapshot *pm_snapshot_take(void);       /* ERR_PTR on failure */
void pm_snapshot_free(struct pm_snapshot *snap);

/* System-wide numbers for /proc/procmon/summary. */
struct pm_summary {
	u64           uptime_ns;
	unsigned int  nr_cpus;
	unsigned long load_x100[3];   /* 1, 5, 15 minutes */
	unsigned long mem_total_kb;
	unsigned long mem_free_kb;
	unsigned long mem_avail_kb;
	unsigned int  procs_total;
	unsigned int  procs_running;
	unsigned int  procs_sleeping;
	unsigned int  procs_stopped;
	unsigned int  procs_zombie;
	unsigned int  threads_total;
};

void pm_summary_collect(struct pm_summary *s);

/* ------------------------------------------------------------------ */
/* Event ring buffer (pm_events.c)                                    */
/* ------------------------------------------------------------------ */

struct pm_event {
	u64   seq;
	u64   ts_ns;
	int   type;        /* enum pm_event_type */
	pid_t pid;
	pid_t ppid;
	int   exit_code;
	char  comm[TASK_COMM_LEN];
};

int  pm_events_init(unsigned int capacity);
void pm_events_exit(void);
void pm_events_seq_show(struct seq_file *m);     /* dumps whole ring */
void pm_events_stats(u64 *last_seq, u64 *dropped);

/* ------------------------------------------------------------------ */
/* procfs front-end (pm_procfs.c)                                     */
/* ------------------------------------------------------------------ */

int  pm_procfs_init(void);
void pm_procfs_exit(void);

#endif /* PM_INTERNAL_H */
