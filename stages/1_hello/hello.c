// SPDX-License-Identifier: GPL-2.0
/*
 * hello.c - Stage 1: the smallest useful kernel module.
 *
 * What it shows:
 *   1. A module has an entry point (init) and an exit point (exit).
 *   2. Kernel code cannot use printf(); it uses printk / pr_info,
 *      and the messages appear in the kernel log (dmesg).
 *   3. A module can take parameters at load time:
 *        sudo insmod hello.ko who=student
 *   4. Kernel code runs on behalf of a process: `current` is the
 *      process that called into the kernel (here: insmod).
 */
#include <linux/init.h>         /* __init, __exit                 */
#include <linux/module.h>       /* module_init, MODULE_LICENSE... */
#include <linux/moduleparam.h>  /* module_param                   */
#include <linux/sched.h>        /* current, task_struct           */

/* Parameter: `insmod hello.ko who=NAME`. 0444 = readable in /sys. */
static char *who = "world";
module_param(who, charp, 0444);
MODULE_PARM_DESC(who, "Who to greet (default: world)");

/* Runs once, when the module is loaded (insmod). */
static int __init hello_init(void)
{
	pr_info("hello: Hello, %s! (loaded by '%s', pid %d)\n",
		who, current->comm, current->pid);
	return 0;               /* 0 = success; an error would abort loading */
}

/* Runs once, when the module is removed (rmmod). */
static void __exit hello_exit(void)
{
	pr_info("hello: Goodbye, %s!\n", who);
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("student");
MODULE_DESCRIPTION("Stage 1: hello world kernel module with a parameter");
MODULE_VERSION("1.0");
