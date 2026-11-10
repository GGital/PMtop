// SPDX-License-Identifier: GPL-2.0
/*
 * procmon_main.c - Module entry/exit points.
 *
 * Load order:   events (ring buffer + hooks)  ->  procfs files
 * Unload order: procfs files                  ->  events
 *
 * procfs goes last on load / first on unload so no reader can ever see
 * a half-initialised or already-freed event buffer.
 */
#include <linux/module.h>
#include <linux/init.h>

#include "pm_internal.h"

static unsigned int max_events = 1024;
module_param(max_events, uint, 0444);
MODULE_PARM_DESC(max_events, "Capacity of the fork/exec/exit event ring buffer (default 1024)");

static int __init procmon_init(void)
{
	int err;

	err = pm_events_init(max_events);
	if (err) {
		pm_err("event subsystem init failed: %d\n", err);
		return err;
	}

	err = pm_procfs_init();
	if (err) {
		pm_err("procfs init failed: %d\n", err);
		pm_events_exit();
		return err;
	}

	pm_info("loaded v%s, interface at %s\n", PM_VERSION_STR, PM_PROC_ROOT);
	return 0;
}

static void __exit procmon_exit(void)
{
	pm_procfs_exit();
	pm_events_exit();
	pm_info("unloaded\n");
}

module_init(procmon_init);
module_exit(procmon_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("student");
MODULE_DESCRIPTION("Process monitoring module exposing process data via /proc/procmon");
MODULE_VERSION(PM_VERSION_STR);
