# pmon: a process-monitor kernel module, built in 3 stages

This folder is the simple version of the project, and it follows the
assignment step by step:

| Assignment asks for | Where |
|---|---|
| Explain the functionality of a kernel module | [Part 0](#part-0--what-a-kernel-module-is) and [Stage 1](#stage-1--hello-the-smallest-module) |
| Choose what to do with it | [Part 0](#what-i-chose-to-build) (a process monitor) |
| **Create** the module | [Stage 2](#stage-2--pmon-v10-create-a-process-list) |
| **Modify / improve** it | [Stage 3](#stage-3--pmon-v20-improve-watch-fork-and-exit) |
| **Compile** and **use** it | the *Compile* / *Load* / *Use* steps of every stage |
| **Evidence** the modifications were applied | the ✔ checks and [Evidence summary](#evidence-summary) |

```
stages/
├── 1_hello/      hello.c   44 lines   init/exit, dmesg, parameter
├── 2_proclist/   pmon.c   108 lines   v1.0: /proc/pmon  (process list)
├── 3_events/     pmon.c   293 lines   v2.0: + /proc/pmon_events (fork/exit)
└── demo.sh                runs every step below and checks the result
```

Run everything (and take screenshots) with:

```bash
./stages/demo.sh --pause      # stops after every step: screenshot, then Enter
./stages/demo.sh 2            # just one stage
```

---

## Part 0 · What a kernel module is

A **kernel module** is a piece of code that is loaded into the *running* Linux
kernel without rebooting, and removed again when it's no longer needed.
Device drivers, file systems and firewalls are usually modules (`lsmod` lists
the ones loaded right now).

How it differs from a normal program:

| Normal program (user space) | Kernel module (kernel space) |
|---|---|
| starts at `main()` and runs until it ends | has an **init** function (on load) and an **exit** function (on unload); in between it waits until the kernel calls it |
| `printf()` → terminal | `pr_info()` → **kernel log**, read with `dmesg` |
| a crash kills only that program | a bug can crash the **whole machine** |
| can only see its own memory | can see **all** kernel data, e.g. every process (`task_struct`) |
| uses libc | no libc, only kernel functions; must declare `MODULE_LICENSE("GPL")` to use most of them |

User space talks to a module through files such as **/proc** (this project),
**/sys** (module parameters) or **/dev** (devices).

The tools used in this report:

| Command | What it does |
|---|---|
| `make` | builds `X.ko` using the kernel's own build system (needs `linux-headers-$(uname -r)`) |
| `modinfo X.ko` | shows the module's version, description, parameters |
| `sudo insmod X.ko param=value` | **loads** the module → its init function runs |
| `lsmod` | lists loaded modules |
| `sudo dmesg` | shows the kernel log (our `pr_info` messages) |
| `sudo rmmod X` | **unloads** it → its exit function runs |

### What I chose to build

**pmon, a process monitor that lives inside the kernel.** Tools like `ps` and
`top` read process information that the kernel prepares in `/proc/<pid>/`.
pmon gets the same information *directly* from the kernel's own process list.
In version 2 it also gets notified by the kernel every time a process is
created or exits, which `ps` can't do: `ps` only sees snapshots.

---

## Stage 1 · hello: the smallest module

File: [1_hello/hello.c](1_hello/hello.c)

```c
static char *who = "world";
module_param(who, charp, 0444);           // insmod hello.ko who=student

static int __init hello_init(void)        // runs on insmod
{
	pr_info("hello: Hello, %s! (loaded by '%s', pid %d)\n",
		who, current->comm, current->pid);
	return 0;
}

static void __exit hello_exit(void)       // runs on rmmod
{
	pr_info("hello: Goodbye, %s!\n", who);
}

module_init(hello_init);
module_exit(hello_exit);
MODULE_LICENSE("GPL");
```

`current` is the process the kernel is working for right now. During loading
that's `insmod`, which shows that a module's code runs inside the kernel *on
behalf of* a process.

**Compile:**

```bash
cd stages/1_hello
make
modinfo hello.ko
```
```
version:        1.0
description:    Stage 1: hello world kernel module with a parameter
license:        GPL
vermagic:       7.0.0-30-generic SMP preempt mod_unload modversions
parm:           who:Who to greet (default: world) (charp)
```
`vermagic` is the kernel version the module was built for. A module only
loads into that kernel.

**Load, use, unload:**

```bash
sudo insmod hello.ko who=student
lsmod | grep hello                       # hello   12288  0
cat /sys/module/hello/parameters/who     # student
sudo dmesg | tail -1                     # [3865.72] hello: Hello, student! (loaded by 'insmod', pid 82202)
sudo rmmod hello
sudo dmesg | tail -1                     # [3865.81] hello: Goodbye, student!
```

📸 **Screenshots:** `make` + `modinfo`, `insmod` + `dmesg`, `rmmod` + `dmesg`.

---

## Stage 2 · pmon v1.0: create a process list

File: [2_proclist/pmon.c](2_proclist/pmon.c)

When the module loads, it creates the file `/proc/pmon`. That's not a real file
on disk: every time someone reads it, the kernel calls our function `pmon_show()`,
which writes the text:

```c
static int pmon_show(struct seq_file *m, void *v)
{
	struct task_struct *p;

	rcu_read_lock();                     // the list can't change under us
	for_each_process(p) {                // every process in the system
		if (!show_kthreads && (p->flags & PF_KTHREAD))
			continue;
		seq_printf(m, "%-7d %-7d %-5c %-7d %-10lu %s\n",
			   task_pid_nr(p),                                 // PID
			   task_pid_nr(rcu_dereference(p->real_parent)),   // PPID
			   task_state_to_char(p),                          // R/S/D/Z...
			   get_nr_threads(p),                              // threads
			   rss_kb(p),                                      // memory
			   p->comm);                                       // name
	}
	rcu_read_unlock();
	...
}

static int __init pmon_init(void)
{
	proc_create("pmon", 0444, NULL, &pmon_ops);   // make /proc/pmon
	...
}
```

Key ideas:

- **`struct task_struct`** is the kernel's record of one process. `for_each_process`
  walks the kernel's list of them.
- **`rcu_read_lock()`**: processes can exit at any moment on another CPU. RCU
  guarantees that nothing we're looking at is freed until we unlock.
- **`task_lock()`** in `rss_kb()`: a process that's exiting gives up its memory
  map (`p->mm`). Taking its lock makes sure that can't happen while we read it.
- **`get_mm_rss_sum()`, not `get_mm_rss()`**: for speed, the kernel counts each
  process's memory separately on every CPU. `get_mm_rss()` returns a quick
  *approximate* total, while `get_mm_rss_sum()` adds up every CPU exactly, which
  is what `ps` shows. The first version used the approximate one and was
  about 100 KB off from `ps` (2316 vs 2424 KB). Switching made them equal.
- **`show_kthreads`** is a parameter with permission `0644`, so root can change
  it *while the module is running* through `/sys/module/pmon/parameters/`.

**Compile, load, use:**

```bash
cd stages/2_proclist
make
modinfo pmon.ko | grep -E 'version|parm'   # version: 1.0, parm: show_kthreads
sudo insmod pmon.ko
sudo dmesg | tail -1                       # pmon v1.0: created /proc/pmon
head -6 /proc/pmon
```
```
PID     PPID    STATE THREADS RSS(KB)    NAME
1       0       S     1       15004      systemd
2       0       S     1       0          kthreadd
3       2       S     1       0          pool_workqueue_
4       2       I     1       0          kworker/R-rcu_g
...
total: 341 processes
```

**Evidence that it's correct: the kernel's numbers match `ps`:**

```bash
sleep 300 &
grep "^$! " /proc/pmon
ps -o pid,ppid,stat,nlwp,rss,comm -p $!
```
```
85535   84045   S     1       2376       sleep          ← /proc/pmon (our module)
  PID    PPID STAT NLWP   RSS COMMAND
85535   84045 S+      1  2376 sleep                     ← ps
```
PID, parent, state, thread count and memory are identical. (`S+` is `S` plus
"running in the foreground of a terminal", an extra flag `ps` adds.)

**Change the parameter while it runs:**

```bash
tail -1 /proc/pmon                                         # total: 341 processes
echo N | sudo tee /sys/module/pmon/parameters/show_kthreads
tail -1 /proc/pmon                                         # total: 156 processes
```
The 185 entries that disappeared are kernel threads (`kworker`, `ksoftirqd`...).

```bash
sudo rmmod pmon
ls /proc/pmon          # No such file or directory: the module cleaned up
```

📸 **Screenshots:** `modinfo` (v1.0), `insmod` + `dmesg`, `head /proc/pmon`,
pmon vs `ps`, the parameter change, `rmmod` + file gone.

---

## Stage 3 · pmon v2.0: improve: watch fork and exit

File: [3_events/pmon.c](3_events/pmon.c)

**The improvement:** v1.0 can only show what's running *at the moment you look*.
A program that starts and ends between two `cat`s is never seen. v2.0 asks the
kernel to **call our code every time a process is created (fork) or exits**, and
keeps the last 64 events in a new file, `/proc/pmon_events`.

**What was modified:** everything from v1.0 is still there unchanged; the new
parts are marked `NEW` in the source.

```bash
diff -u 2_proclist/pmon.c 3_events/pmon.c     # 165 lines added, 6 changed
```

| Change | Why |
|---|---|
| `MODULE_VERSION("1.0")` → `"2.0"` | the new version is visible in `modinfo` |
| `pmon_on_fork()`, `pmon_on_exit()` | our code, called *by the kernel* at every fork / exit |
| `tracepoint_probe_register()` in init | attaches them to the kernel's `sched_process_fork` / `sched_process_exit` **tracepoints** (hooks built into the kernel for exactly this) |
| `struct pmon_event events[64]` + `spinlock` | a ring buffer: when full, the oldest event is overwritten. The lock is needed because several CPUs can fork at the same time. |
| `/proc/pmon_events` | the new file that shows the events |
| unregister + `tracepoint_synchronize_unregister()` in exit | wait until no CPU is still running our code before the module disappears, otherwise the kernel would jump into freed memory |

```c
static void pmon_on_exit(void *data, struct task_struct *p, bool group_dead)
{
	if (!group_dead)
		return;          // a thread ended, but the process is still alive
	...
	add_event('X', p, ppid, code);
}
```

**Compile and upgrade** (v1.0 must be unloaded first, since both are called `pmon`):

```bash
cd stages/3_events
make
modinfo -F version pmon.ko      # 2.0
sudo insmod pmon.ko
sudo dmesg | tail -1            # pmon v2.0: created /proc/pmon and /proc/pmon_events, watching fork/exit
```

**Use it:** make events we know the answer to, then read what the kernel saw:

```bash
sh -c 'exit 3'                          # a program that fails with code 3
sleep 300 & kill -9 $!                  # a program killed by signal 9
ls / > /dev/null                        # a normal program
tail -6 /proc/pmon_events
```
```
[ 3867.611836] EXIT  82651   81469   sleep            killed by signal 9
[ 3867.612390] FORK  82653   81469   bash
[ 3867.614529] EXIT  82653   81469   ls               exit code 0
...
total since load: 15 forks, 16 exits
```

Things worth pointing out in the report:

- A **FORK** line shows the *parent's* name (`bash`), and the **EXIT** of the
  same PID shows `ls`. A new process starts as a copy of its parent and only
  becomes `ls` when it calls `exec()`.
- "16 exits but 15 forks" is correct: some processes that exited were created
  *before* the module was loaded.
- Starting threads doesn't create FORK lines (`thread_group_leader` check). pmon
  counts processes, not threads.

```bash
sudo rmmod pmon
sudo dmesg | tail -1     # pmon v2.0: removed /proc/pmon and /proc/pmon_events (21 forks, 20 exits seen)
```

📸 **Screenshots:** the `diff`, `modinfo` (2.0), `insmod` + `dmesg`, the three
test commands + `tail /proc/pmon_events`, `rmmod` + `dmesg`.

---

## Evidence summary

Everything below is checked automatically by `./stages/demo.sh` (✔ = passed):

| Claim | Evidence |
|---|---|
| The module compiles | `hello.ko`, `pmon.ko` exist; `modinfo` reads them |
| init and exit run | `Hello, student!` / `Goodbye, student!` in `dmesg` |
| Parameters work | `/sys/module/hello/parameters/who` = `student`; `show_kthreads=N` changes the count 341 → 156 |
| The module creates /proc files | `/proc/pmon` appears on `insmod`, disappears on `rmmod` |
| The data is real | PPID, threads and RSS of a test process match `ps` exactly |
| **The modification was applied** | `modinfo` version 1.0 → 2.0; the new `/proc/pmon_events` exists; `dmesg` says `v2.0 … watching fork/exit` |
| **The improvement works** | `sh` → `exit code 3`, `sleep` → `killed by signal 9`, `ls` → `exit code 0` |

## If something goes wrong

| Message | Meaning / fix |
|---|---|
| `Key was rejected by service` | Secure Boot only allows signed modules; disable it in the VM settings |
| `insmod: File exists` | already loaded: `sudo rmmod pmon` first |
| `Unknown symbol ...` | missing `MODULE_LICENSE("GPL")` (GPL-only kernel functions) |
| `module verification failed ... tainting kernel` in dmesg | normal for self-built modules, not an error |

## Bonus: the full version

`../kmod/` is a larger version of the same idea (snapshot-based `/proc` files,
CPU time, exec events), with a k9s-style terminal UI in `../tui/` (`make && make
load && make run`). It isn't needed for the stages above.
