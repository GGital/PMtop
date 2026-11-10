// SPDX-License-Identifier: GPL-2.0
/*
 * pm_events.c - Process lifecycle events (fork / exec / exit).
 *
 * Design:
 *   - Hook the scheduler tracepoints  sched_process_fork,
 *     sched_process_exec, sched_process_exit. They are not exported to
 *     modules by symbol, so we find them by name with
 *     for_each_kernel_tracepoint() and attach with
 *     tracepoint_probe_register().
 *   - Each hook writes one struct pm_event into a fixed-size ring
 *     buffer protected by a spinlock (hooks run in atomic context:
 *     no sleeping, no allocation).
 *   - When the ring is full the oldest event is overwritten and
 *     `dropped` is incremented.
 *   - Readers get a copy of the ring (see pm_events_seq_show) so the
 *     spinlock is never held while formatting text.
 *
 * STATUS: skeleton. No hooks yet, ring is empty. Implemented in step 5.
 */
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "pm_internal.h"

static struct {
	struct pm_event *buf;      /* TODO(step 5): kcalloc(capacity)     */
	unsigned int     capacity;
	unsigned int     head;     /* next slot to write                  */
	unsigned int     len;      /* number of valid events              */
	u64              seq;      /* last assigned sequence number       */
	u64              dropped;
	spinlock_t       lock;
} pm_ring;

/*
 * TODO(step 5): tracepoint probes, e.g.
 *
 * static void pm_on_fork(void *data, struct task_struct *parent,
 *                        struct task_struct *child);
 * static void pm_on_exec(void *data, struct task_struct *p,
 *                        pid_t old_pid, struct linux_binprm *bprm);
 * static void pm_on_exit(void *data, struct task_struct *p);
 *
 * each calling pm_ring_push(type, task, exit_code).
 */

int pm_events_init(unsigned int capacity)
{
	spin_lock_init(&pm_ring.lock);
	pm_ring.capacity = capacity;
	pm_ring.head = 0;
	pm_ring.len = 0;
	pm_ring.seq = 0;
	pm_ring.dropped = 0;
	pm_ring.buf = NULL;
	/* TODO(step 5): allocate buffer, find + register tracepoints. */
	return 0;
}

void pm_events_exit(void)
{
	/*
	 * TODO(step 5): unregister probes, then tracepoint_synchronize_unregister()
	 * so no probe is still running when we free the buffer.
	 */
	kfree(pm_ring.buf);
	pm_ring.buf = NULL;
}

void pm_events_stats(u64 *last_seq, u64 *dropped)
{
	unsigned long flags;

	spin_lock_irqsave(&pm_ring.lock, flags);
	*last_seq = pm_ring.seq;
	*dropped = pm_ring.dropped;
	spin_unlock_irqrestore(&pm_ring.lock, flags);
}

void pm_events_seq_show(struct seq_file *m)
{
	seq_puts(m, PM_EVENTS_HEADER "\n");
	/*
	 * TODO(step 5): copy the ring under the lock into a temporary
	 * buffer, unlock, then print each event:
	 *   seq \t ts_ns \t type \t pid \t ppid \t exit_code \t comm(escaped)
	 */
}
