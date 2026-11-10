// SPDX-License-Identifier: GPL-2.0
/*
 * pmon.c v2.0 - Stage 3: the process monitor, improved.
 *
 * Creates the file /proc/pmon. Reading it (cat /proc/pmon) makes the
 * kernel walk its own list of processes and print one line each:
 *
 *   PID     PPID    STATE THREADS RSS(KB)    NAME
 *   1       0       S     1       14808      systemd
 *
 * NEW in v2.0: the module also WATCHES processes being created and
 * exiting, and keeps the last 64 events in /proc/pmon_events:
 *
 *   TIME           EVENT PID     PPID    NAME             RESULT
 *   [ 3301.204511] FORK  4211    4100    bash
 *   [ 3301.209877] EXIT  4211    4100    ls               exit code 0
 *
 * Kernel ideas used:
 *   - task_struct      : the kernel's record of one process
 *   - for_each_process : walks the list of all processes
 *   - RCU read lock    : keeps the list safe while we walk it
 *   - /proc + seq_file : how a module shows text to user space
 *   - module parameter : show_kthreads, can be changed while loaded
 *   - tracepoints      : NEW, hooks the kernel calls at fork and exit
 *   - spinlock         : NEW, protects the event buffer
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>        /* proc_create, remove_proc_entry */
#include <linux/seq_file.h>       /* seq_printf, single_open        */
#include <linux/sched.h>          /* task_struct                    */
#include <linux/sched/signal.h>   /* for_each_process               */
#include <linux/sched/task.h>     /* task_lock                      */
#include <linux/mm.h>             /* get_mm_rss_sum                 */
#include <linux/tracepoint.h>     /* NEW: tracepoint_probe_register */
#include <linux/spinlock.h>       /* NEW: spinlock_t                */
#include <linux/timekeeping.h>    /* NEW: ktime_get_boottime_ns     */
#include <linux/math64.h>         /* NEW: div_u64_rem               */

#define PROC_NAME "pmon"
#define EVENTS_PROC_NAME "pmon_events"   /* NEW */
#define MAX_EVENTS 64                    /* NEW: size of the ring buffer */

/* 0644: root can change it at runtime:
 *   echo N | sudo tee /sys/module/pmon/parameters/show_kthreads */
static bool show_kthreads = true;
module_param(show_kthreads, bool, 0644);
MODULE_PARM_DESC(show_kthreads, "Also list kernel threads (default: Y)");

/* ------------------------------------------------------------------ */
/* NEW in v2.0: event ring buffer                                     */
/* ------------------------------------------------------------------ */

struct pmon_event {
	u64   time_ns;                 /* nanoseconds since boot            */
	char  type;                    /* 'F' = fork, 'X' = exit            */
	pid_t pid, ppid;
	int   code;                    /* exit status, only for 'X'         */
	char  comm[TASK_COMM_LEN];     /* process name                      */
};

static struct pmon_event events[MAX_EVENTS];
static unsigned int ev_next;           /* slot the next event goes into     */
static unsigned int ev_count;          /* how many slots are filled         */
static unsigned long total_forks, total_exits;
static DEFINE_SPINLOCK(ev_lock);       /* many CPUs fork at the same time   */

/* Store one event; when the buffer is full the oldest is overwritten. */
static void add_event(char type, struct task_struct *p, pid_t ppid, int code)
{
	struct pmon_event *e;

	spin_lock(&ev_lock);
	e = &events[ev_next];
	e->time_ns = ktime_get_boottime_ns();
	e->type = type;
	e->pid = task_pid_nr(p);
	e->ppid = ppid;
	e->code = code;
	get_task_comm(e->comm, p);

	ev_next = (ev_next + 1) % MAX_EVENTS;
	if (ev_count < MAX_EVENTS)
		ev_count++;
	if (type == 'F')
		total_forks++;
	else
		total_exits++;
	spin_unlock(&ev_lock);
}

/*
 * Tracepoint probes. The kernel calls these itself, in the middle of
 * fork() and exit() of other processes, so they must be quick and
 * must never sleep.
 */
static void pmon_on_fork(void *data, struct task_struct *parent,
			 struct task_struct *child)
{
	if (!thread_group_leader(child))
		return;                /* a new thread, not a new process */
	add_event('F', child, task_tgid_nr(parent), 0);
}

static void pmon_on_exit(void *data, struct task_struct *p, bool group_dead)
{
	struct signal_struct *sig = p->signal;
	pid_t ppid;
	int code;

	if (!group_dead)
		return;                /* other threads of p are still running */

	/* exit_group() / fatal signal: the code of the whole process */
	code = (sig->flags & SIGNAL_GROUP_EXIT) ? sig->group_exit_code : p->exit_code;

	rcu_read_lock();
	ppid = task_tgid_nr(rcu_dereference(p->real_parent));
	rcu_read_unlock();
	add_event('X', p, ppid, code);
}

/* The tracepoints are not exported, so we look them up by name. */
static struct tracepoint *tp_fork, *tp_exit;

static void find_tracepoint(struct tracepoint *tp, void *priv)
{
	if (!strcmp(tp->name, "sched_process_fork"))
		tp_fork = tp;
	else if (!strcmp(tp->name, "sched_process_exit"))
		tp_exit = tp;
}

/* Called every time someone reads /proc/pmon_events. */
static int pmon_events_show(struct seq_file *m, void *v)
{
	unsigned int i, idx;
	u32 usec;
	u64 sec;

	seq_printf(m, "%-14s %-5s %-7s %-7s %-16s %s\n",
		   "TIME", "EVENT", "PID", "PPID", "NAME", "RESULT");

	spin_lock(&ev_lock);
	for (i = 0; i < ev_count; i++) {
		/* oldest first: start right after the newest event */
		const struct pmon_event *e;

		idx = (ev_next + MAX_EVENTS - ev_count + i) % MAX_EVENTS;
		e = &events[idx];
		sec = div_u64_rem(e->time_ns, NSEC_PER_SEC, &usec);
		usec /= NSEC_PER_USEC;

		seq_printf(m, "[%5llu.%06u] %-5s %-7d %-7d %-16s ", sec, usec,
			   e->type == 'F' ? "FORK" : "EXIT", e->pid, e->ppid, e->comm);
		if (e->type == 'X' && (e->code & 0x7f))
			seq_printf(m, "killed by signal %d\n", e->code & 0x7f);
		else if (e->type == 'X')
			seq_printf(m, "exit code %d\n", (e->code >> 8) & 0xff);
		else
			seq_putc(m, '\n');
	}
	seq_printf(m, "total since load: %lu forks, %lu exits\n",
		   total_forks, total_exits);
	spin_unlock(&ev_lock);
	return 0;
}

static int pmon_events_open(struct inode *inode, struct file *file)
{
	return single_open(file, pmon_events_show, NULL);
}

static const struct proc_ops pmon_events_ops = {
	.proc_open    = pmon_events_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/* ------------------------------------------------------------------ */
/* /proc/pmon: the process list (same as v1.0)                        */
/* ------------------------------------------------------------------ */

/* Resident memory of a process in KB (0 for kernel threads). */
static unsigned long rss_kb(struct task_struct *p)
{
	unsigned long kb = 0;

	task_lock(p);           /* stops p->mm from disappearing under us */
	if (p->mm)              /* _sum = exact, adds up every CPU's count */
		kb = get_mm_rss_sum(p->mm) << (PAGE_SHIFT - 10);   /* pages -> KB */
	task_unlock(p);
	return kb;
}

/* Called every time someone reads /proc/pmon. */
static int pmon_show(struct seq_file *m, void *v)
{
	struct task_struct *p;
	int count = 0;

	seq_printf(m, "%-7s %-7s %-5s %-7s %-10s %s\n",
		   "PID", "PPID", "STATE", "THREADS", "RSS(KB)", "NAME");

	rcu_read_lock();        /* processes may exit while we walk the list */
	for_each_process(p) {
		if (!show_kthreads && (p->flags & PF_KTHREAD))
			continue;
		seq_printf(m, "%-7d %-7d %-5c %-7d %-10lu %s\n",
			   task_pid_nr(p),
			   task_pid_nr(rcu_dereference(p->real_parent)),
			   task_state_to_char(p),
			   get_nr_threads(p),
			   rss_kb(p),
			   p->comm);
		count++;
	}
	rcu_read_unlock();

	seq_printf(m, "total: %d processes\n", count);
	return 0;
}

static int pmon_open(struct inode *inode, struct file *file)
{
	return single_open(file, pmon_show, NULL);
}

/* What happens on open/read/close of /proc/pmon. */
static const struct proc_ops pmon_ops = {
	.proc_open    = pmon_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

static int __init pmon_init(void)
{
	int err;

	/* NEW: attach to the fork and exit tracepoints */
	for_each_kernel_tracepoint(find_tracepoint, NULL);
	if (!tp_fork || !tp_exit)
		return -ENOENT;
	err = tracepoint_probe_register(tp_fork, pmon_on_fork, NULL);
	if (err)
		return err;
	err = tracepoint_probe_register(tp_exit, pmon_on_exit, NULL);
	if (err)
		goto undo_fork;

	err = -ENOMEM;
	if (!proc_create(PROC_NAME, 0444, NULL, &pmon_ops))
		goto undo_exit;
	if (!proc_create(EVENTS_PROC_NAME, 0444, NULL, &pmon_events_ops)) {
		remove_proc_entry(PROC_NAME, NULL);
		goto undo_exit;
	}
	pr_info("pmon v2.0: created /proc/%s and /proc/%s, watching fork/exit\n",
		PROC_NAME, EVENTS_PROC_NAME);
	return 0;

undo_exit:
	tracepoint_probe_unregister(tp_exit, pmon_on_exit, NULL);
undo_fork:
	tracepoint_probe_unregister(tp_fork, pmon_on_fork, NULL);
	tracepoint_synchronize_unregister();
	return err;
}

static void __exit pmon_exit(void)
{
	remove_proc_entry(EVENTS_PROC_NAME, NULL);
	remove_proc_entry(PROC_NAME, NULL);

	/* NEW: detach, then wait until no CPU is still inside our probes,
	 * otherwise the kernel could jump into freed module code. */
	tracepoint_probe_unregister(tp_exit, pmon_on_exit, NULL);
	tracepoint_probe_unregister(tp_fork, pmon_on_fork, NULL);
	tracepoint_synchronize_unregister();

	pr_info("pmon v2.0: removed /proc/%s and /proc/%s (%lu forks, %lu exits seen)\n",
		PROC_NAME, EVENTS_PROC_NAME, total_forks, total_exits);
}

module_init(pmon_init);
module_exit(pmon_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("student");
MODULE_DESCRIPTION("Stage 3: list processes in /proc/pmon and log fork/exit in /proc/pmon_events");
MODULE_VERSION("2.0");
