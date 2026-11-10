// SPDX-License-Identifier: GPL-2.0
/*
 * pmon.c v1.0 - Stage 2: a process monitor in the kernel.
 *
 * Creates the file /proc/pmon. Reading it (cat /proc/pmon) makes the
 * kernel walk its own list of processes and print one line each:
 *
 *   PID     PPID    STATE THREADS RSS(KB)    NAME
 *   1       0       S     1       14808      systemd
 *
 * Kernel ideas used:
 *   - task_struct      : the kernel's record of one process
 *   - for_each_process : walks the list of all processes
 *   - RCU read lock    : keeps the list safe while we walk it
 *   - /proc + seq_file : how a module shows text to user space
 *   - module parameter : show_kthreads, can be changed while loaded
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>        /* proc_create, remove_proc_entry */
#include <linux/seq_file.h>       /* seq_printf, single_open        */
#include <linux/sched.h>          /* task_struct                    */
#include <linux/sched/signal.h>   /* for_each_process               */
#include <linux/sched/task.h>     /* task_lock                      */
#include <linux/mm.h>             /* get_mm_rss_sum                 */

#define PROC_NAME "pmon"

/* 0644: root can change it at runtime:
 *   echo N | sudo tee /sys/module/pmon/parameters/show_kthreads */
static bool show_kthreads = true;
module_param(show_kthreads, bool, 0644);
MODULE_PARM_DESC(show_kthreads, "Also list kernel threads (default: Y)");

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
	if (!proc_create(PROC_NAME, 0444, NULL, &pmon_ops))
		return -ENOMEM;
	pr_info("pmon v1.0: created /proc/%s\n", PROC_NAME);
	return 0;
}

static void __exit pmon_exit(void)
{
	remove_proc_entry(PROC_NAME, NULL);
	pr_info("pmon v1.0: removed /proc/%s\n", PROC_NAME);
}

module_init(pmon_init);
module_exit(pmon_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("student");
MODULE_DESCRIPTION("Stage 2: list processes in /proc/pmon");
MODULE_VERSION("1.0");
