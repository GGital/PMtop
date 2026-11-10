// SPDX-License-Identifier: GPL-2.0
/*
 * pm_collect.c - Turn kernel task_structs into plain pm_proc_info records.
 *
 * This is the only file that walks the task list.
 *
 * Locking rules used here
 * -----------------------
 *  - The task list is walked under rcu_read_lock(). RCU guarantees that
 *    no task_struct we can reach is freed until we unlock, but tasks may
 *    still exit (state changes, mm detached) while we look at them.
 *    Inside an RCU read section we must NOT sleep: no GFP_KERNEL
 *    allocation, no mutexes, no mmput().
 *  - p->mm is read under task_lock(p). exit_mm() clears p->mm under the
 *    same lock, and the task's own reference keeps the mm alive while
 *    p->mm is non-NULL. This is exactly how get_task_mm() works, but we
 *    avoid taking a reference (mmput() may sleep).
 *  - Counters such as utime/stime/total_vm are read without their
 *    writers' locks. They are single 64-bit words on 64-bit kernels, so
 *    the worst case is a value that is a few microseconds stale.
 */
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/overflow.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/math64.h>
#include <linux/rcupdate.h>
#include <linux/cred.h>
#include <linux/uidgid.h>
#include <linux/user_namespace.h>
#include <linux/timekeeping.h>
#include <linux/cpumask.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/sched/loadavg.h>
#include <linux/sched/prio.h>

#include "pm_internal.h"

/* Extra slots in case processes are created between counting and filling. */
#define PM_SNAPSHOT_SLACK 64

/* pages -> KiB */
#define PM_PAGES_TO_KB(x) ((unsigned long)(x) << (PAGE_SHIFT - 10))

/*
 * CPU time of the whole thread group, in ns.
 *
 * The kernel keeps two kinds of CPU time per thread:
 *   - utime/stime         : sampled at every timer tick. Cheap, but a
 *                           task that runs between ticks is missed, so
 *                           bursty tasks are heavily under-counted.
 *   - se.sum_exec_runtime : exact ns, updated by the scheduler.
 *
 * Like /proc/<pid>/stat (cputime_adjust(), which modules cannot call)
 * we use the exact total and only use the sampled values to split it
 * into user vs system:
 *
 *   utime = rtime * u / (u + s)      stime = rtime - utime
 *
 * signal->{utime,stime,sum_sched_runtime} hold what threads that
 * already exited consumed; live threads are added on top.
 * Caller holds rcu_read_lock().
 */
static void pm_group_cputime(struct task_struct *p, u64 *utime, u64 *stime)
{
	struct signal_struct *sig = p->signal;
	struct task_struct *t;
	u64 u = READ_ONCE(sig->utime);
	u64 s = READ_ONCE(sig->stime);
	u64 rtime = READ_ONCE(sig->sum_sched_runtime);

	for_each_thread(p, t) {
		u += READ_ONCE(t->utime);
		s += READ_ONCE(t->stime);
		rtime += READ_ONCE(t->se.sum_exec_runtime);
	}

	/* same edge cases as the kernel's cputime_adjust() */
	if (s == 0) {
		*utime = rtime;
		*stime = 0;
	} else if (u == 0) {
		*utime = 0;
		*stime = rtime;
	} else {
		/* u/(u+s) <= 1, so the 128-bit result always fits in 64 bits */
		*utime = mul_u64_u64_div_u64(rtime, u, u + s);
		*stime = rtime - *utime;
	}
}

/* Caller holds rcu_read_lock(). */
static void pm_fill_one(struct pm_proc_info *out, struct task_struct *p)
{
	struct mm_struct *mm;

	memset(out, 0, sizeof(*out));

	out->pid   = task_tgid_nr(p);
	out->ppid  = pid_alive(p) ? task_tgid_nr(rcu_dereference(p->real_parent)) : 0;
	out->uid   = from_kuid_munged(&init_user_ns, task_uid(p));
	out->state = task_state_to_char(p);
	out->nice  = task_nice(p);
	out->prio  = p->prio - MAX_RT_PRIO;        /* same as ps/top "PRI" */
	out->nr_threads = get_nr_threads(p);
	out->start_ns   = p->start_boottime;
	pm_group_cputime(p, &out->utime_ns, &out->stime_ns);
	get_task_comm(out->comm, p);

	/* Kernel threads have no mm; they keep vsz/rss = 0. */
	task_lock(p);
	mm = p->mm;
	if (mm && !(p->flags & PF_KTHREAD)) {
		out->vsz_kb = PM_PAGES_TO_KB(READ_ONCE(mm->total_vm));
		out->rss_kb = PM_PAGES_TO_KB(get_mm_rss_sum(mm));  /* exact, like /proc */
	}
	task_unlock(p);
}

static size_t pm_count_processes(void)
{
	struct task_struct *p;
	size_t n = 0;

	rcu_read_lock();
	for_each_process(p)
		n++;
	rcu_read_unlock();
	return n;
}

struct pm_snapshot *pm_snapshot_take(void)
{
	struct pm_snapshot *snap;
	struct task_struct *p;
	size_t cap;

	/*
	 * Pass 1: count, so we can allocate with GFP_KERNEL (may sleep)
	 * OUTSIDE the RCU section. Processes created between the two
	 * passes fill the slack; if even that runs out we stop early
	 * rather than allocate atomically.
	 */
	cap = pm_count_processes();
	cap += cap / 8 + PM_SNAPSHOT_SLACK;

	snap = kvzalloc(struct_size(snap, procs, cap), GFP_KERNEL);
	if (!snap)
		return ERR_PTR(-ENOMEM);

	/* Pass 2: copy. */
	rcu_read_lock();
	for_each_process(p) {
		if (snap->count == cap)
			break;
		pm_fill_one(&snap->procs[snap->count++], p);
	}
	rcu_read_unlock();

	return snap;
}

void pm_snapshot_free(struct pm_snapshot *snap)
{
	kvfree(snap);
}

/* avenrun[] fixed point -> load * 100, rounded like /proc/loadavg. */
static unsigned long pm_load_x100(unsigned long avg)
{
	avg += FIXED_1 / 200;
	return LOAD_INT(avg) * 100 + LOAD_FRAC(avg);
}

void pm_summary_collect(struct pm_summary *s)
{
	struct task_struct *p;
	struct sysinfo si;

	memset(s, 0, sizeof(*s));

	s->uptime_ns = ktime_get_boottime_ns();
	s->nr_cpus   = num_online_cpus();

	s->load_x100[0] = pm_load_x100(avenrun[0]);
	s->load_x100[1] = pm_load_x100(avenrun[1]);
	s->load_x100[2] = pm_load_x100(avenrun[2]);

	si_meminfo(&si);   /* fills totalram/freeram in units of mem_unit */
	s->mem_total_kb = (unsigned long)(((u64)si.totalram * si.mem_unit) >> 10);
	s->mem_free_kb  = (unsigned long)(((u64)si.freeram * si.mem_unit) >> 10);
	s->mem_avail_kb = PM_PAGES_TO_KB(si_mem_available());

	/*
	 * Per-process state is the state of the thread-group leader,
	 * same as `ps` and `top` show it.
	 */
	rcu_read_lock();
	for_each_process(p) {
		s->procs_total++;
		s->threads_total += get_nr_threads(p);

		switch (task_state_to_char(p)) {
		case PM_STATE_RUNNING:
			s->procs_running++;
			break;
		case PM_STATE_SLEEPING:
		case PM_STATE_DISK:
		case PM_STATE_IDLE:
			s->procs_sleeping++;
			break;
		case PM_STATE_STOPPED:
		case PM_STATE_TRACED:
			s->procs_stopped++;
			break;
		case PM_STATE_ZOMBIE:
		case PM_STATE_DEAD:
			s->procs_zombie++;
			break;
		}
	}
	rcu_read_unlock();
}
