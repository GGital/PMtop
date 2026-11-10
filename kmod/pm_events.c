// SPDX-License-Identifier: GPL-2.0
/*
 * pm_events.c - Process lifecycle events (fork / exec / exit).
 *
 * How we hook in
 * --------------
 * The scheduler has static tracepoints at exactly the moments we care
 * about. They are NOT exported to modules by symbol, but every kernel
 * tracepoint can be found by name with for_each_kernel_tracepoint()
 * and attached to with tracepoint_probe_register():
 *
 *   sched_process_fork (parent, child)        kernel_clone(), new task
 *   sched_process_exec (p, old_pid, bprm)     a successful execve()
 *   sched_process_exit (p, group_dead)        do_exit(), every thread
 *
 * Threads vs processes: fork also fires for pthread_create() and exit
 * fires for every thread. We only record whole PROCESSES:
 *   - fork: only if the child leads its own thread group
 *   - exit: only when group_dead is true (the last thread is leaving)
 *
 * Context rules inside a probe
 * ----------------------------
 * Probes run with preemption disabled, in the middle of fork/exec/exit
 * of SOME OTHER process, on every CPU at once. So: no sleeping, no
 * allocation, keep it short. We copy ~60 bytes into a preallocated
 * ring under a spinlock and return.
 *
 * Ring buffer
 * -----------
 * Fixed capacity (module param max_events). When full, the oldest
 * event is overwritten and `dropped` counts it. Every event gets a
 * monotonically increasing seq so readers can tell what is new.
 */
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/tracepoint.h>
#include <linux/timekeeping.h>
#include <linux/sched/signal.h>
#include <linux/binfmts.h>
#include <linux/mm.h>

#include "pm_internal.h"

#define PM_EVENTS_MIN 16
#define PM_EVENTS_MAX 65536

static struct {
	struct pm_event *buf;
	unsigned int     capacity;
	unsigned int     head;     /* next slot to write                  */
	unsigned int     len;      /* number of valid events              */
	u64              seq;      /* last assigned sequence number       */
	u64              dropped;  /* overwritten before anyone read them */
	spinlock_t       lock;
} pm_ring = {
	.lock = __SPIN_LOCK_UNLOCKED(pm_ring.lock),
};

/* ------------------------------------------------------------------ */
/* ring buffer                                                        */
/* ------------------------------------------------------------------ */

static void pm_ring_push(int type, struct task_struct *p, pid_t ppid, int exit_code)
{
	struct pm_event *e;
	unsigned long flags;
	u64 now = ktime_get_boottime_ns();

	spin_lock_irqsave(&pm_ring.lock, flags);
	e = &pm_ring.buf[pm_ring.head];
	e->seq       = ++pm_ring.seq;
	e->ts_ns     = now;
	e->type      = type;
	e->pid       = task_tgid_nr(p);
	e->ppid      = ppid;
	e->exit_code = exit_code;
	get_task_comm(e->comm, p);

	pm_ring.head = (pm_ring.head + 1) % pm_ring.capacity;
	if (pm_ring.len < pm_ring.capacity)
		pm_ring.len++;
	else
		pm_ring.dropped++;     /* we just overwrote the oldest */
	spin_unlock_irqrestore(&pm_ring.lock, flags);
}

/* ------------------------------------------------------------------ */
/* tracepoint probes (first arg is the `data` we registered: unused)  */
/* ------------------------------------------------------------------ */

static void pm_on_fork(void *data, struct task_struct *parent,
		       struct task_struct *child)
{
	if (!thread_group_leader(child))
		return;                         /* pthread_create(): not a process */
	/* comm is still the parent's here; exec (if any) comes next */
	pm_ring_push(PM_EV_FORK, child, task_tgid_nr(parent), 0);
}

static void pm_on_exec(void *data, struct task_struct *p, pid_t old_pid,
		       struct linux_binprm *bprm)
{
	pid_t ppid;

	rcu_read_lock();
	ppid = task_tgid_nr(rcu_dereference(p->real_parent));
	rcu_read_unlock();
	pm_ring_push(PM_EV_EXEC, p, ppid, 0);  /* comm is already the new one */
}

static void pm_on_exit(void *data, struct task_struct *p, bool group_dead)
{
	struct signal_struct *sig = p->signal;
	pid_t ppid;
	int code;

	if (!group_dead)
		return;                         /* other threads remain */

	/*
	 * Raw wait(2)-style status, decoded by the TUI:
	 *   exit(3)       -> 3 << 8 = 768
	 *   killed by 9   -> 9
	 * For a group exit (exit_group(), fatal signal) the code that
	 * matters is the group's, not whatever this thread was doing.
	 */
	code = (READ_ONCE(sig->flags) & SIGNAL_GROUP_EXIT)
	       ? READ_ONCE(sig->group_exit_code) : READ_ONCE(p->exit_code);

	rcu_read_lock();
	ppid = task_tgid_nr(rcu_dereference(p->real_parent));
	rcu_read_unlock();
	pm_ring_push(PM_EV_EXIT, p, ppid, code);
}

/* ------------------------------------------------------------------ */
/* finding + attaching the tracepoints                                */
/* ------------------------------------------------------------------ */

static struct pm_hook {
	const char        *name;
	void              *probe;
	struct tracepoint *tp;
	bool               registered;
} pm_hooks[] = {
	{ "sched_process_fork", pm_on_fork },
	{ "sched_process_exec", pm_on_exec },
	{ "sched_process_exit", pm_on_exit },
};

static void pm_find_tp(struct tracepoint *tp, void *priv)
{
	for (size_t i = 0; i < ARRAY_SIZE(pm_hooks); i++)
		if (!strcmp(tp->name, pm_hooks[i].name))
			pm_hooks[i].tp = tp;
}

static void pm_unregister_all(void)
{
	bool any = false;

	for (size_t i = 0; i < ARRAY_SIZE(pm_hooks); i++) {
		if (!pm_hooks[i].registered)
			continue;
		tracepoint_probe_unregister(pm_hooks[i].tp, pm_hooks[i].probe, NULL);
		pm_hooks[i].registered = false;
		any = true;
	}
	/*
	 * A probe may still be running on another CPU right now. Wait
	 * until every CPU is out of it before the caller frees the ring
	 * (and before our module code disappears on rmmod).
	 */
	if (any)
		tracepoint_synchronize_unregister();
}

int pm_events_init(unsigned int capacity)
{
	int err;

	if (capacity < PM_EVENTS_MIN || capacity > PM_EVENTS_MAX) {
		pm_err("max_events=%u out of range [%d, %d]\n",
		       capacity, PM_EVENTS_MIN, PM_EVENTS_MAX);
		return -EINVAL;
	}

	pm_ring.buf = kvcalloc(capacity, sizeof(*pm_ring.buf), GFP_KERNEL);
	if (!pm_ring.buf)
		return -ENOMEM;
	pm_ring.capacity = capacity;
	pm_ring.head = pm_ring.len = 0;
	pm_ring.seq = pm_ring.dropped = 0;

	for_each_kernel_tracepoint(pm_find_tp, NULL);

	for (size_t i = 0; i < ARRAY_SIZE(pm_hooks); i++) {
		if (!pm_hooks[i].tp) {
			pm_err("tracepoint %s not found\n", pm_hooks[i].name);
			err = -ENOENT;
			goto fail;
		}
		err = tracepoint_probe_register(pm_hooks[i].tp, pm_hooks[i].probe, NULL);
		if (err) {
			pm_err("cannot attach to %s: %d\n", pm_hooks[i].name, err);
			goto fail;
		}
		pm_hooks[i].registered = true;
	}
	pm_info("tracing fork/exec/exit, ring of %u events\n", capacity);
	return 0;

fail:
	pm_unregister_all();
	kvfree(pm_ring.buf);
	pm_ring.buf = NULL;
	return err;
}

void pm_events_exit(void)
{
	pm_unregister_all();
	kvfree(pm_ring.buf);
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

/* ------------------------------------------------------------------ */
/* reader                                                             */
/* ------------------------------------------------------------------ */

void pm_events_seq_show(struct seq_file *m)
{
	struct pm_event *copy;
	unsigned long flags;
	unsigned int n, start;

	seq_puts(m, PM_EVENTS_HEADER "\n");

	/*
	 * Copy the ring out under the lock, print without it: formatting
	 * thousands of lines while holding a lock that every fork() in
	 * the system wants would stall the whole machine.
	 * The allocation may sleep, so it happens before we lock.
	 */
	copy = kvmalloc_array(pm_ring.capacity, sizeof(*copy), GFP_KERNEL);
	if (!copy) {
		seq_puts(m, "# out of memory\n");
		return;
	}

	spin_lock_irqsave(&pm_ring.lock, flags);
	n = pm_ring.len;
	start = (pm_ring.head + pm_ring.capacity - n) % pm_ring.capacity;  /* oldest */
	for (unsigned int i = 0; i < n; i++)
		copy[i] = pm_ring.buf[(start + i) % pm_ring.capacity];
	spin_unlock_irqrestore(&pm_ring.lock, flags);

	/* Field order MUST match enum pm_event_field. */
	for (unsigned int i = 0; i < n; i++) {
		const struct pm_event *e = &copy[i];

		seq_printf(m, "%llu\t%llu\t%s\t%d\t%d\t%d\t",
			   e->seq, e->ts_ns, pm_event_type_name(e->type),
			   e->pid, e->ppid, e->exit_code);
		seq_escape(m, e->comm, " \t\n\\");
		seq_putc(m, '\n');
	}
	kvfree(copy);
}
