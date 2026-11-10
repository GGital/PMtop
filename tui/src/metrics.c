#include "metrics.h"

void metrics_compute(struct dataset *cur, const struct dataset *prev)
{
	(void)prev;
	/*
	 * TODO(step 3): both arrays are sorted by pid -> walk them like a
	 * merge, or bsearch prev for each cur row.
	 */
	for (size_t i = 0; i < cur->nprocs; i++) {
		cur->procs[i].cpu_pct = 0.0;
		cur->procs[i].mem_pct = 0.0;
	}
}
