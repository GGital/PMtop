// SPDX-License-Identifier: GPL-2.0
/*
 * pm_collect.c - Turn kernel task_structs into plain pm_proc_info records.
 *
 * This is the only file that walks the task list. Everything is copied
 * under rcu_read_lock() so no task can be freed while we look at it;
 * we never sleep inside that section (GFP_ATOMIC or pre-allocation).
 *
 * STATUS: skeleton. Returns empty data. Implemented in step 2.
 */
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/overflow.h>
#include <linux/err.h>
#include <linux/string.h>

#include "pm_internal.h"

/*
 * TODO(step 2): fill one record from a thread-group leader.
 *   - pid/ppid      : task_tgid_nr(p), task_tgid_nr(rcu_dereference(p->real_parent))
 *   - uid           : from_kuid(&init_user_ns, task_uid(p))
 *   - state         : task_state_to_char(p)
 *   - nice/prio     : task_nice(p), p->prio - MAX_RT_PRIO
 *   - threads       : get_nr_threads(p)
 *   - utime/stime   : sum over threads (for_each_thread) or
 *                     thread_group_cputime_adjusted-equivalent
 *   - start_ns      : p->start_boottime
 *   - vsz/rss       : p->mm may be NULL (kernel threads) -> 0;
 *                     mm->total_vm, get_mm_rss(mm), PAGE_SHIFT - 10
 *   - comm          : get_task_comm()
 */
static void __maybe_unused pm_fill_one(struct pm_proc_info *out,
				       struct task_struct *p)
{
	memset(out, 0, sizeof(*out));
}

struct pm_snapshot *pm_snapshot_take(void)
{
	struct pm_snapshot *snap;

	/*
	 * TODO(step 2):
	 *   1. estimate count (nr_processes() + slack)
	 *   2. kvzalloc(struct_size(snap, procs, cap))   -- may sleep, so
	 *      do it BEFORE rcu_read_lock()
	 *   3. rcu_read_lock(); for_each_process(p) { if full break;
	 *      pm_fill_one(&snap->procs[snap->count++], p); }
	 *      rcu_read_unlock();
	 */
	snap = kvzalloc(struct_size(snap, procs, 0), GFP_KERNEL);
	if (!snap)
		return ERR_PTR(-ENOMEM);

	snap->count = 0;
	return snap;
}

void pm_snapshot_free(struct pm_snapshot *snap)
{
	kvfree(snap);
}

void pm_summary_collect(struct pm_summary *s)
{
	memset(s, 0, sizeof(*s));

	/*
	 * TODO(step 2):
	 *   uptime  : ktime_get_boottime_ns()
	 *   cpus    : num_online_cpus()
	 *   load    : avenrun[] (fixed point FSHIFT) -> *100
	 *   memory  : si_meminfo(&info), si_mem_available()
	 *   counts  : one for_each_process() pass, bucket by state
	 */
}
