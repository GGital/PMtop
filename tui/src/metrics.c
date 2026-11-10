/*
 * metrics.c - CPU% and MEM% from two consecutive samples.
 *
 *            Δ(utime + stime) [ns]
 *   cpu% = ─────────────────────── × 100      (per process, % of ONE cpu,
 *              Δwall [s] × 1e9                 like top: 2 busy threads = 200%)
 *
 *   mem% = rss_kb / mem_total_kb × 100
 *
 *   header CPU = Σ Δcpu of all processes / (Δwall × nr_cpus)
 *
 * Both arrays are sorted by pid, so matching is one linear merge walk.
 * A row only matches if pid AND start time are equal; a pid recycled
 * between samples is treated as a new process.
 */
#include "metrics.h"

void metrics_compute(struct dataset *cur, const struct dataset *prev)
{
	double dt = (prev->taken_at > 0) ? cur->taken_at - prev->taken_at : 0;
	unsigned long total_kb = cur->sum.mem_total_kb;
	double busy_ns = 0;
	size_t j = 0;

	cur->cpu_total_pct = 0;

	for (size_t i = 0; i < cur->nprocs; i++) {
		struct proc_row *p = &cur->procs[i];
		uint64_t now = p->utime_ns + p->stime_ns, before;
		double window;

		p->mem_pct = total_kb ? 100.0 * p->rss_kb / total_kb : 0;
		p->cpu_pct = 0;
		if (dt <= 0 || prev->nprocs == 0)
			continue;          /* first sample: nothing to diff against */

		while (j < prev->nprocs && prev->procs[j].pid < p->pid)
			j++;

		if (j < prev->nprocs && prev->procs[j].pid == p->pid &&
		    prev->procs[j].start_ns == p->start_ns) {
			/* same process as last time */
			before = prev->procs[j].utime_ns + prev->procs[j].stime_ns;
			window = dt;
		} else if (p->start_ns > prev->sum.uptime_ns &&
			   cur->sum.uptime_ns > p->start_ns) {
			/* born since the last sample: its whole life is the window */
			before = 0;
			window = (cur->sum.uptime_ns - p->start_ns) / 1e9;
			if (window > dt)
				window = dt;
		} else {
			continue;
		}
		if (now < before || window <= 0)
			continue;

		p->cpu_pct = (double)(now - before) / (window * 1e9) * 100.0;
		busy_ns += (double)(now - before);
	}

	if (dt > 0 && cur->sum.nr_cpus) {
		cur->cpu_total_pct = busy_ns / (dt * 1e9 * cur->sum.nr_cpus) * 100.0;
		if (cur->cpu_total_pct > 100)
			cur->cpu_total_pct = 100;
	}
}
