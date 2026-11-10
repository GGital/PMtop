/*
 * metrics.h - Derived numbers that need two samples (CPU%) or a
 *             reference (MEM%).
 *
 *   cpu% = (Δutime + Δstime) / (Δwall * 1e9) * 100       (per core, like top)
 *   mem% = rss_kb / mem_total_kb * 100
 *
 * A process is matched across samples by (pid, start_ns) so a recycled
 * pid is never mistaken for the old process.
 *
 * STATUS: skeleton. Implemented in step 3.
 */
#ifndef PMTOP_METRICS_H
#define PMTOP_METRICS_H

#include "source.h"

void metrics_compute(struct dataset *cur, const struct dataset *prev);

#endif
