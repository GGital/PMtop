// SPDX-License-Identifier: GPL-2.0
/*
 * pm_procfs.c - The /proc/procmon/ directory and its three files.
 *
 * This file only FORMATS data; it never touches task_structs itself.
 *
 *   processes : seq_file iterator over a pm_snapshot taken at open().
 *               Large output (thousands of lines) is fine because
 *               seq_file pages through it across multiple read() calls
 *               while the snapshot stays alive until release().
 *   summary   : single_open(), small fixed output.
 *   events    : single_open(), delegates to pm_events_seq_show().
 */
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/err.h>

#include "pm_internal.h"

static struct proc_dir_entry *pm_dir;

/* ------------------------------------------------------------------ */
/* /proc/procmon/processes                                            */
/* ------------------------------------------------------------------ */

/* Position 0 is the header line, position N (N>=1) is procs[N-1]. */
static void *pm_snap_at(struct pm_snapshot *snap, loff_t pos)
{
	if (pos == 0)
		return SEQ_START_TOKEN;
	if (pos - 1 < (loff_t)snap->count)
		return &snap->procs[pos - 1];
	return NULL;
}

static void *pm_proc_start(struct seq_file *m, loff_t *pos)
{
	return pm_snap_at(m->private, *pos);
}

static void *pm_proc_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return pm_snap_at(m->private, *pos);
}

static void pm_proc_stop(struct seq_file *m, void *v)
{
}

static int pm_proc_show(struct seq_file *m, void *v)
{
	const struct pm_proc_info *p = v;

	if (v == SEQ_START_TOKEN) {
		seq_puts(m, PM_PROCESSES_HEADER "\n");
		return 0;
	}

	/* Field order MUST match enum pm_proc_field. */
	seq_printf(m, "%d\t%d\t%u\t%c\t%d\t%d\t%u\t%llu\t%llu\t%llu\t%lu\t%lu\t",
		   p->pid, p->ppid, p->uid, p->state, p->nice, p->prio,
		   p->nr_threads, p->utime_ns, p->stime_ns, p->start_ns,
		   p->vsz_kb, p->rss_kb);
	seq_escape(m, p->comm, " \t\n\\");
	seq_putc(m, '\n');
	return 0;
}

static const struct seq_operations pm_proc_seq_ops = {
	.start = pm_proc_start,
	.next  = pm_proc_next,
	.stop  = pm_proc_stop,
	.show  = pm_proc_show,
};

static int pm_processes_open(struct inode *inode, struct file *file)
{
	struct pm_snapshot *snap;
	int err;

	snap = pm_snapshot_take();
	if (IS_ERR(snap))
		return PTR_ERR(snap);

	err = seq_open(file, &pm_proc_seq_ops);
	if (err) {
		pm_snapshot_free(snap);
		return err;
	}
	((struct seq_file *)file->private_data)->private = snap;
	return 0;
}

static int pm_processes_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;

	pm_snapshot_free(m->private);
	return seq_release(inode, file);
}

static const struct proc_ops pm_processes_ops = {
	.proc_open    = pm_processes_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = pm_processes_release,
};

/* ------------------------------------------------------------------ */
/* /proc/procmon/summary                                              */
/* ------------------------------------------------------------------ */

static int pm_summary_show(struct seq_file *m, void *v)
{
	struct pm_summary s;
	u64 ev_seq, ev_dropped;

	pm_summary_collect(&s);
	pm_events_stats(&ev_seq, &ev_dropped);

	seq_puts(m, PM_SUMMARY_HEADER "\n");
	seq_printf(m, PM_SUM_VERSION "\t%s\n", PM_VERSION_STR);
	seq_printf(m, PM_SUM_UPTIME_NS "\t%llu\n", s.uptime_ns);
	seq_printf(m, PM_SUM_NR_CPUS "\t%u\n", s.nr_cpus);
	seq_printf(m, PM_SUM_LOAD1 "\t%lu\n", s.load_x100[0]);
	seq_printf(m, PM_SUM_LOAD5 "\t%lu\n", s.load_x100[1]);
	seq_printf(m, PM_SUM_LOAD15 "\t%lu\n", s.load_x100[2]);
	seq_printf(m, PM_SUM_MEM_TOTAL_KB "\t%lu\n", s.mem_total_kb);
	seq_printf(m, PM_SUM_MEM_FREE_KB "\t%lu\n", s.mem_free_kb);
	seq_printf(m, PM_SUM_MEM_AVAIL_KB "\t%lu\n", s.mem_avail_kb);
	seq_printf(m, PM_SUM_PROCS_TOTAL "\t%u\n", s.procs_total);
	seq_printf(m, PM_SUM_PROCS_RUNNING "\t%u\n", s.procs_running);
	seq_printf(m, PM_SUM_PROCS_SLEEPING "\t%u\n", s.procs_sleeping);
	seq_printf(m, PM_SUM_PROCS_STOPPED "\t%u\n", s.procs_stopped);
	seq_printf(m, PM_SUM_PROCS_ZOMBIE "\t%u\n", s.procs_zombie);
	seq_printf(m, PM_SUM_THREADS_TOTAL "\t%u\n", s.threads_total);
	seq_printf(m, PM_SUM_EVENTS_SEQ "\t%llu\n", ev_seq);
	seq_printf(m, PM_SUM_EVENTS_DROPPED "\t%llu\n", ev_dropped);
	return 0;
}

static int pm_summary_open(struct inode *inode, struct file *file)
{
	return single_open(file, pm_summary_show, NULL);
}

static const struct proc_ops pm_summary_ops = {
	.proc_open    = pm_summary_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/* ------------------------------------------------------------------ */
/* /proc/procmon/events                                               */
/* ------------------------------------------------------------------ */

static int pm_events_show(struct seq_file *m, void *v)
{
	pm_events_seq_show(m);
	return 0;
}

static int pm_events_open(struct inode *inode, struct file *file)
{
	return single_open(file, pm_events_show, NULL);
}

static const struct proc_ops pm_events_ops = {
	.proc_open    = pm_events_open,
	.proc_read    = seq_read,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

/* ------------------------------------------------------------------ */
/* setup / teardown                                                   */
/* ------------------------------------------------------------------ */

int pm_procfs_init(void)
{
	pm_dir = proc_mkdir(PM_PROC_DIR, NULL);
	if (!pm_dir)
		return -ENOMEM;

	if (!proc_create(PM_FILE_PROCESSES, 0444, pm_dir, &pm_processes_ops) ||
	    !proc_create(PM_FILE_SUMMARY, 0444, pm_dir, &pm_summary_ops) ||
	    !proc_create(PM_FILE_EVENTS, 0444, pm_dir, &pm_events_ops)) {
		proc_remove(pm_dir);   /* removes any children created so far */
		pm_dir = NULL;
		return -ENOMEM;
	}
	return 0;
}

void pm_procfs_exit(void)
{
	proc_remove(pm_dir);
	pm_dir = NULL;
}
