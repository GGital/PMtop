/* SPDX-License-Identifier: GPL-2.0 */
/*
 * procmon_shared.h - The contract between the kernel module and user space.
 *
 * This header is included by BOTH:
 *   - kmod/ *.c    (kernel side, the producer of data)
 *   - tui/src/ *.c (user side, the consumer / pmtop TUI)
 *
 * It only contains constants, file paths and the text format description,
 * never kernel-only or libc-only types, so it compiles in both worlds.
 *
 * Interface overview (all files are read-only text, `cat`-able):
 *
 *   /proc/procmon/processes   one line per process (snapshot taken at open())
 *   /proc/procmon/summary     system-wide "key value" lines
 *   /proc/procmon/events      ring buffer of fork/exec/exit events
 *
 * If you change a format, bump PM_FORMAT_VERSION. The TUI checks the
 * version in each header line and refuses to parse a mismatching one.
 */
#ifndef PROCMON_SHARED_H
#define PROCMON_SHARED_H

#define PM_VERSION_STR    "0.1.0"
#define PM_FORMAT_VERSION 1

/* ------------------------------------------------------------------ */
/* Paths                                                              */
/* ------------------------------------------------------------------ */
#define PM_PROC_DIR        "procmon"
#define PM_FILE_PROCESSES  "processes"
#define PM_FILE_SUMMARY    "summary"
#define PM_FILE_EVENTS     "events"

#define PM_PROC_ROOT       "/proc/" PM_PROC_DIR
#define PM_PATH_PROCESSES  PM_PROC_ROOT "/" PM_FILE_PROCESSES
#define PM_PATH_SUMMARY    PM_PROC_ROOT "/" PM_FILE_SUMMARY
#define PM_PATH_EVENTS     PM_PROC_ROOT "/" PM_FILE_EVENTS

/* Same as the kernel's TASK_COMM_LEN (process name incl. NUL). */
#define PM_COMM_LEN 16

/* ------------------------------------------------------------------ */
/* /proc/procmon/processes                                            */
/* ------------------------------------------------------------------ */
/*
 * Line 1 is the header, every following line is one process (thread
 * group leader). Fields are separated by a single TAB, in the order of
 * enum pm_proc_field. `comm` is always last and has whitespace and
 * backslashes octal-escaped (e.g. "Web\040Content"), so splitting on
 * TAB is always safe.
 *
 * Times are cumulative nanoseconds. CPU% is NOT computed in the kernel:
 * the TUI diffs two snapshots (delta cpu time / delta wall time).
 */
#define PM_PROCESSES_MAGIC "# procmon-processes"
#define PM_PROCESSES_HEADER                                         \
	PM_PROCESSES_MAGIC " v1\tpid\tppid\tuid\tstate\tnice\tprio" \
	"\tthreads\tutime_ns\tstime_ns\tstart_ns\tvsz_kb\trss_kb\tcomm"

enum pm_proc_field {
	PMF_PID = 0,   /* tgid of the process                        */
	PMF_PPID,      /* parent's tgid                              */
	PMF_UID,       /* real uid (in the initial user namespace)   */
	PMF_STATE,     /* one char, see PM_STATE_* below             */
	PMF_NICE,      /* -20 .. 19                                  */
	PMF_PRIO,      /* kernel priority as shown by ps/top         */
	PMF_THREADS,   /* number of threads in the group             */
	PMF_UTIME_NS,  /* user CPU time, all threads                 */
	PMF_STIME_NS,  /* system CPU time, all threads               */
	PMF_START_NS,  /* boot-based start time                      */
	PMF_VSZ_KB,    /* virtual memory size                        */
	PMF_RSS_KB,    /* resident set size                          */
	PMF_COMM,      /* escaped process name, ALWAYS LAST          */
	PMF_COUNT
};

/* Process state letters (same letters as /proc/<pid>/stat). */
#define PM_STATE_RUNNING  'R'
#define PM_STATE_SLEEPING 'S'
#define PM_STATE_DISK     'D'  /* uninterruptible sleep */
#define PM_STATE_STOPPED  'T'
#define PM_STATE_TRACED   't'
#define PM_STATE_ZOMBIE   'Z'
#define PM_STATE_DEAD     'X'
#define PM_STATE_IDLE     'I'

/* ------------------------------------------------------------------ */
/* /proc/procmon/summary                                              */
/* ------------------------------------------------------------------ */
/*
 * Line 1 is the header, then "key<TAB>value" lines, one per key below.
 * Unknown keys must be ignored by the reader (forward compatible).
 */
#define PM_SUMMARY_MAGIC  "# procmon-summary"
#define PM_SUMMARY_HEADER PM_SUMMARY_MAGIC " v1"

#define PM_SUM_VERSION        "version"        /* module version string */
#define PM_SUM_UPTIME_NS      "uptime_ns"
#define PM_SUM_NR_CPUS        "nr_cpus"
#define PM_SUM_LOAD1          "load1_x100"     /* load avg * 100        */
#define PM_SUM_LOAD5          "load5_x100"
#define PM_SUM_LOAD15         "load15_x100"
#define PM_SUM_MEM_TOTAL_KB   "mem_total_kb"
#define PM_SUM_MEM_FREE_KB    "mem_free_kb"
#define PM_SUM_MEM_AVAIL_KB   "mem_available_kb"
#define PM_SUM_PROCS_TOTAL    "procs_total"
#define PM_SUM_PROCS_RUNNING  "procs_running"
#define PM_SUM_PROCS_SLEEPING "procs_sleeping"
#define PM_SUM_PROCS_STOPPED  "procs_stopped"
#define PM_SUM_PROCS_ZOMBIE   "procs_zombie"
#define PM_SUM_THREADS_TOTAL  "threads_total"
#define PM_SUM_EVENTS_SEQ     "events_seq"     /* last event seq number  */
#define PM_SUM_EVENTS_DROPPED "events_dropped" /* overwritten in ring    */

/* ------------------------------------------------------------------ */
/* /proc/procmon/events                                               */
/* ------------------------------------------------------------------ */
/*
 * Header line, then the ring buffer contents oldest -> newest.
 * Reading does NOT consume events; every event has a monotonically
 * increasing `seq`, the TUI remembers the last seq it has seen and
 * only appends newer ones.
 */
#define PM_EVENTS_MAGIC  "# procmon-events"
#define PM_EVENTS_HEADER                                           \
	PM_EVENTS_MAGIC " v1\tseq\tts_ns\ttype\tpid\tppid\texit_code\tcomm"

enum pm_event_field {
	PME_SEQ = 0,
	PME_TS_NS,      /* ktime_get_boottime_ns() at the event */
	PME_TYPE,       /* "fork" | "exec" | "exit"             */
	PME_PID,
	PME_PPID,
	PME_EXIT_CODE,  /* only meaningful for exit, else 0     */
	PME_COMM,       /* escaped, ALWAYS LAST                 */
	PME_COUNT
};

enum pm_event_type {
	PM_EV_FORK = 1,
	PM_EV_EXEC = 2,
	PM_EV_EXIT = 3,
};

static inline const char *pm_event_type_name(int type)
{
	switch (type) {
	case PM_EV_FORK: return "fork";
	case PM_EV_EXEC: return "exec";
	case PM_EV_EXIT: return "exit";
	default:         return "?";
	}
}

#endif /* PROCMON_SHARED_H */
