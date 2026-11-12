# pmon: a process-monitor kernel module

**pmon** is a Linux kernel module that reads process information straight
from the kernel's own process list and watches every process being created
(fork) or exiting. It shows both through two `/proc` files. **pmview** is a
small k9s-style terminal program that reads those files and draws them live.

```
PMtop/
├── Makefile        make / make load / make unload / make run / make demo
├── demo.sh         builds, loads, tests and unloads the module, with ✔ checks
├── module/
│   ├── Makefile    Kbuild, out-of-tree
│   └── pmon.c      the kernel module (v2.0): /proc/pmon + /proc/pmon_events
└── viewer/
    ├── Makefile    links -lncursesw
    └── pmview.c    user-space viewer for the two files
```

Quick start (needs `linux-headers-$(uname -r)` and `libncurses-dev`):

```bash
make            # build module/pmon.ko and viewer/pmview
make load       # sudo insmod module/pmon.ko
make run        # start the viewer
make unload     # sudo rmmod pmon
./demo.sh --pause   # step-by-step demo with checks; stops for screenshots
```

---

## What a kernel module is

A **kernel module** is a piece of code that is loaded into the *running* Linux
kernel without rebooting, and removed again when it's no longer needed.
Device drivers, file systems and firewalls are usually modules (`lsmod` lists
the ones loaded right now).

| Normal program (user space) | Kernel module (kernel space) |
|---|---|
| starts at `main()` and runs until it ends | has an **init** function (on load) and an **exit** function (on unload); in between it waits until the kernel calls it |
| `printf()` → terminal | `pr_info()` → **kernel log**, read with `dmesg` |
| a crash kills only that program | a bug can crash the **whole machine** |
| can only see its own memory | can see **all** kernel data, e.g. every process (`task_struct`) |
| uses libc | no libc, only kernel functions; must declare `MODULE_LICENSE("GPL")` to use most of them |

User space talks to a module through files such as **/proc** (this project),
**/sys** (module parameters) or **/dev** (devices).

| Command | What it does |
|---|---|
| `make` | builds `pmon.ko` using the kernel's own build system |
| `modinfo pmon.ko` | shows the module's version, description, parameters |
| `sudo insmod pmon.ko` | **loads** the module, its init function runs |
| `lsmod` | lists loaded modules |
| `sudo dmesg` | shows the kernel log (our `pr_info` messages) |
| `sudo rmmod pmon` | **unloads** it, its exit function runs |

Tools like `ps` and `top` read process information that the kernel prepares in
`/proc/<pid>/`. pmon gets the same information *directly* from the kernel's
process list, and it is notified by the kernel every time a process is
created or exits, which `ps` can't do: `ps` only sees snapshots.

---

## The module: [module/pmon.c](module/pmon.c)

### 1. The process list: `/proc/pmon`

When the module loads it creates `/proc/pmon`. That's not a real file on disk:
every time someone reads it, the kernel calls `pmon_show()`, which writes the
text:

```c
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
```

- **`struct task_struct`** is the kernel's record of one process.
  `for_each_process` walks the kernel's list of them.
- **`rcu_read_lock()`**: processes can exit at any moment on another CPU. RCU
  guarantees that nothing we're looking at is freed until we unlock.
- **`task_lock()`** in `rss_kb()`: an exiting process gives up its memory map
  (`p->mm`). Taking its lock makes sure that can't happen while we read it.
- **`get_mm_rss_sum()`** adds up the per-CPU memory counters exactly, so the
  RSS matches what `ps` shows (`get_mm_rss()` is only approximate).
- **`show_kthreads`** is a parameter with permission `0644`, so root can change
  it *while the module is running* through `/sys/module/pmon/parameters/`.

```
PID     PPID    STATE THREADS RSS(KB)    NAME
1       0       S     1       15004      systemd
2       0       S     1       0          kthreadd
...
total: 341 processes
```

### 2. Fork and exit events: `/proc/pmon_events`

A process list only shows what's running *at the moment you look*: a program
that starts and ends between two `cat`s is never seen. So pmon also asks the
kernel to **call our code every time a process is created or exits**, and keeps
the last 64 events in `/proc/pmon_events`.

| Part | Why |
|---|---|
| `pmon_on_fork()`, `pmon_on_exit()` | our code, called *by the kernel* at every fork / exit |
| `tracepoint_probe_register()` in init | attaches them to the kernel's `sched_process_fork` / `sched_process_exit` **tracepoints** (hooks built into the kernel for exactly this) |
| `struct pmon_event events[64]` + `spinlock` | a ring buffer: when full, the oldest event is overwritten. The lock is needed because several CPUs can fork at the same time. |
| `/proc/pmon_events` | the file that shows the events |
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

### Compile, load, use

```bash
make
modinfo -F version module/pmon.ko      # 2.0
make load
sudo dmesg | tail -1    # pmon v2.0: created /proc/pmon and /proc/pmon_events, watching fork/exit
```

Make events we know the answer to, then read what the kernel saw:

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

- A **FORK** line shows the *parent's* name (`bash`), and the **EXIT** of the
  same PID shows `ls`. A new process starts as a copy of its parent and only
  becomes `ls` when it calls `exec()`.
- "16 exits but 15 forks" is correct: some processes that exited were created
  *before* the module was loaded.
- Starting threads doesn't create FORK lines (`thread_group_leader` check).
  pmon counts processes, not threads.

```bash
make unload
sudo dmesg | tail -1     # pmon v2.0: removed /proc/pmon and /proc/pmon_events (21 forks, 20 exits seen)
```

📸 **Screenshots:** `modinfo` (2.0), `make load` + `dmesg`, the three test
commands + `tail /proc/pmon_events`, `make unload` + `dmesg`.

---

## The viewer: [viewer/pmview.c](viewer/pmview.c)

This one is **not** a kernel module. It's a normal program, and it shows how
user space consumes what a module provides. It reads the module's files once
per second and draws them like the Kubernetes tool *k9s*:

```
 Host:    studentVM                                 <tab> Switch view                   ┌─┐┌┬┐┬  ┬
 Kernel:  7.0.0-30-generic                          </>   Filter                        ├─┘│││└┐┌┘
 Module:  pmon v2.0                                 <j/k> Move                          ┴  ┴ ┴ └┘
 Procs:   347  (running 1, zombie 0)                <P>   Sort PID
 Events:  601 forks, 603 exits since load

┌───────────────────────────────────────── processes[347] ─────────────────────────────────────────┐
│     PID    PPID  S     THR  RSS(KB)↓ MEM         NAME                                            │
│   10791    7720  S      14    483508 ██████████  MainThread                                      │
│    1317       1  S     110    446916 █████████·  java                                            │
│ ...                                                                                              │
└──────────────────────────────────────────────────────────────────────────────────────────────────┘
 <processes>    data from /proc/pmon, refreshed every second
```

| It reads | Shown as |
|---|---|
| `/proc/pmon` | **processes** view: sortable table, colored by state, memory bars |
| `/proc/pmon_events` | **events** view: newest first, FORK green, killed red, error exit yellow |
| `/sys/module/pmon/version` | header: `Module: pmon v2.0` |

The code is one file in four parts: **1. data** (read and `sscanf` the `/proc`
files), **2. view state** (filter and sort), **3. drawing** (ncurses),
**4. keyboard + main loop**. It needs no special permission, because the module
made its files readable by everyone (`0444`).

```bash
make load
make run
```

Keys: `tab` (or `1` / `2`) switches view, `j/k` or arrows move, `/` filters by
name or PID (in the events view also by event or result, e.g. `/killed` or
`/exit code 3`), `P N R T` sort by PID / name / memory / threads, `ctrl-l`
redraws, `q` quits.

Good things to show:

- In another terminal run `sh -c 'exit 3'` and `sleep 100 & sleep 0.3; kill -9 $!`,
  then type `/killed` or `/exit code 3` in the **events** view. The ring buffer
  holds only the last 64 events and a desktop creates many processes in the
  background, so filter instead of scrolling. Wait a moment before `kill`:
  killed *before* it calls `exec()`, the child is still a copy of `bash` and is
  logged under that name.
- Unload the module (`make unload`) while pmview is running: it shows
  "pmon is not loaded".

📸 **Screenshots:** processes view, events view after the test commands.

---

## Evidence summary

Checked automatically by `./demo.sh` (✔ = passed):

| Claim | Evidence |
|---|---|
| The module compiles | `pmon.ko` exists; `modinfo` reports version 2.0 |
| The module creates /proc files | `/proc/pmon` and `/proc/pmon_events` appear on `insmod`, disappear on `rmmod` |
| init and exit run | `pmon v2.0: created ...` / `removed ...` in `dmesg` |
| Fork/exit watching works | `sh` → `exit code 3`, `sleep` → `killed by signal 9`, `ls` → `exit code 0` |
| User programs can use it | `pmview` shows both files live |

## If something goes wrong

| Message | Meaning / fix |
|---|---|
| `Key was rejected by service` | Secure Boot only allows signed modules; disable it in the VM settings |
| `insmod: File exists` | already loaded: `make unload` first |
| `Unknown symbol ...` | missing `MODULE_LICENSE("GPL")` (GPL-only kernel functions) |
| `module verification failed ... tainting kernel` in dmesg | normal for self-built modules, not an error |
