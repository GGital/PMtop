# procmon — Process Monitoring Kernel Module

> **Start here: [`stages/`](stages/README.md).** Three small modules (hello →
> process list → fork/exit events), a small k9s-style viewer for them
> (`stages/4_tui/pmview`), a step-by-step guide, screenshots list and evidence
> checks. That is the assignment version.
>
> Everything below is the **full / bonus version**: a bigger module
> (`procmon.ko`) plus `pmtop`, a k9s-style terminal UI.

A Linux kernel module (`procmon.ko`) that collects process information
straight from the kernel's `task_struct`s and exposes it through
`/proc/procmon/`. A user-space terminal UI (`pmtop`) draws it in a
k9s-style interface: an info header, key hints, a colored table,
`:` commands, `/` filtering and breadcrumbs.

```
┌──────────────────────────── KERNEL SPACE ─────────────────────────────┐
│                                                                       │
│   task list (for_each_process, RCU)      sched tracepoints            │
│            │                             fork / exec / exit           │
│            ▼                                     │                    │
│   ┌──────────────────┐                  ┌────────▼─────────┐          │
│   │  pm_collect.c    │                  │   pm_events.c    │          │
│   │  snapshot +      │                  │   ring buffer    │          │
│   │  summary         │                  │   (spinlock)     │          │
│   └────────┬─────────┘                  └────────┬─────────┘          │
│            └──────────────┬──────────────────────┘                    │
│                  ┌────────▼─────────┐                                 │
│                  │   pm_procfs.c    │  formats text with seq_file     │
│                  └────────┬─────────┘                                 │
│                           │   procmon_main.c: init/exit, params       │
└───────────────────────────┼───────────────────────────────────────────┘
          /proc/procmon/{processes, summary, events}   ← include/procmon_shared.h
┌───────────────────────────┼───────────── USER SPACE ──────────────────┐
│                  ┌────────▼─────────┐                                 │
│                  │    source.c      │  parse text → structs           │
│                  └────────┬─────────┘                                 │
│                  ┌────────▼─────────┐                                 │
│                  │    metrics.c     │  CPU% from 2 samples, MEM%      │
│                  └────────┬─────────┘                                 │
│                  ┌────────▼─────────┐                                 │
│                  │    model.c       │  filter + sort → visible[]      │
│                  └────────┬─────────┘                                 │
│   input.c ──────► ┌───────▼──────────┐ ◄── theme.c (colors)           │
│   (keys, modes)   │  ui.c + views/*  │ ◄── table.c (table widget)     │
│                   └──────────────────┘     ncurses                    │
└───────────────────────────────────────────────────────────────────────┘
```

## Directory layout

```
kernel_module_OS/
├── stages/                   ★ assignment version: 3 small modules + guide
├── Makefile                  top level: make / make load / make run
├── include/
│   └── procmon_shared.h      THE CONTRACT: paths, field order, format version
├── kmod/                     ── kernel module (procmon.ko) ──
│   ├── Makefile              Kbuild, out-of-tree
│   ├── pm_internal.h         kernel structs + API between the .c files
│   ├── procmon_main.c        module_init / module_exit, `max_events` param
│   ├── pm_collect.c          walks task list → pm_snapshot, pm_summary
│   ├── pm_procfs.c           /proc/procmon/* via seq_file
│   └── pm_events.c           tracepoint hooks → ring buffer
├── tui/                      ── user-space UI (pmtop) ──
│   ├── Makefile              links -lncursesw
│   └── src/
│       ├── main.c            arg parsing, main loop (refresh + draw + input)
│       ├── app.h             struct app: the whole UI state
│       ├── source.[ch]       reads + parses /proc/procmon/*
│       ├── metrics.[ch]      CPU% / MEM% derivation
│       ├── model.[ch]        filtering, sorting, selection tracking
│       ├── theme.[ch]        k9s-like color palette (256 / 8 colors)
│       ├── ui.[ch]           layout: header, prompt, footer, dialogs
│       ├── table.[ch]        reusable table widget
│       ├── input.[ch]        key dispatch, : and / modes, kill dialog
│       ├── actions.[ch]      race-free kill (pidfd + start-time check)
│       ├── users.[ch]        uid → user name cache
│       ├── format.[ch]       human-readable sizes, times, durations
│       └── ../tests/         unit tests for parsers + metrics (make test)
│       └── views/
│           ├── view.h        `struct view` interface
│           ├── registry.c    list of views + global key hints
│           ├── processes.c   main process table
│           ├── tree.c        parent/child tree
│           ├── detail.c      describe: fields, gauges, CPU history, children
│           ├── events.c      live fork/exec/exit stream
│           └── help.c        key bindings
└── scripts/
    ├── load.sh               insmod (reloads if already loaded)
    ├── unload.sh             rmmod
    ├── events_test.py        checks fork/exec/exit events with a known workload
    ├── smoke_test.sh         checks /proc/procmon format
    └── xcheck_proc.py        compares every field with /proc/<pid>/stat (+ statm for RSS)
```

## Kernel ↔ user interface

Everything is plain text, so you can always debug with `cat`.
The exact format lives in [`include/procmon_shared.h`](include/procmon_shared.h).

| File | Content | How it's produced |
|---|---|---|
| `/proc/procmon/processes` | header + one TAB-separated line per process | snapshot taken at `open()`, streamed with `seq_file` |
| `/proc/procmon/summary` | `key<TAB>value` lines (uptime, load, memory, counts) | `single_open()` |
| `/proc/procmon/events` | ring buffer of fork/exec/exit, each with a `seq` number | copy of ring under spinlock |

```
$ head -3 /proc/procmon/processes
# procmon-processes v1	pid	ppid	uid	state	nice	prio	threads	utime_ns	stime_ns	start_ns	vsz_kb	rss_kb	comm
1	0	0	S	0	20	1	812000000	2310000000	51000000	23412	14080	systemd
2	0	0	S	0	20	1	0	4000000	51000000	0	0	kthreadd
```

Design decisions:

- **Snapshot at open().** The task list is copied under `rcu_read_lock()`
  into a `kvmalloc`'d array. We never hold the lock across `copy_to_user()`,
  and the reader sees one consistent moment in time.
- **No CPU% in the kernel.** The kernel exports cumulative ns; `pmtop`
  diffs two samples. That keeps the module stateless (no timers).
- **Events are non-destructive.** Reading `events` doesn't consume
  anything. The TUI remembers the last `seq` it saw, so several readers
  can run at once.
- **`comm` is last and escaped.** Process names can contain spaces, so
  splitting on TAB always works.

## pmtop UI

```
 Host:    studentVM                <enter>   Describe   <shift-s> Sort State   ┌─┐┌┬┐┌┬┐┌─┐┌─┐
 Kernel:  7.0.0-30-generic         <ctrl-k>  Kill       <:>       Command      ├─┘│││ │ │ │├─┘
 procmon: v0.1.0                   <shift-c> Sort CPU   </>       Filter       ┴  ┴ ┴ ┴ └─┘┴
 Uptime:  0d 00:37:24              <shift-m> Sort MEM   <?>       Help
 Load:    1.04 0.76 0.60  (2 cpu)  <shift-p> Sort PID   <esc>     Back         process monitor
 CPU:     [███████░░░░░░░]  52%    <shift-n> Sort Name  <ctrl-l>  Redraw
 MEM:     [██████░░░░░░░░]  42% 3.2/7.7G   <shift-t> Sort Thr   <ctrl-c>  Quit
 Procs:   341  R3 S338 T0 Z0
┌──────────────────────────────── processes(all)[341] ────────────────────────────────┐
│    PID  NAME              PPID  USER   S  PRI  NI  THR  CPU%↓  MEM%     VSZ     RSS │
│  28522  yes              28509  1000   R   20   0    1   95.3   0.0    8.1M    1.9M │  ← cursor bar
│   1317  java                 1  1002   S   20   0  110    1.0   5.4    4.6G  427.2M │
└─────────────────────────────────────────────────────────────────────────────────────┘
  <processes>                                                         flash messages
```

How the numbers are made:

- **CPU%** per process = Δ(utime+stime) between two refreshes ÷ Δwall time.
  Percent of *one* CPU, like `top` (a process with two busy threads shows 200%).
  A process is matched across samples by pid **and** start time, so a recycled
  pid starts fresh. A process born between samples is measured over its own lifetime.
- **Header CPU** = all processes' CPU time ÷ (Δwall × number of CPUs).
- **MEM%** = RSS ÷ total RAM. Header MEM = (total − available) ÷ total, like `free`.
- The cursor follows the selected **pid** across refreshes, while sorting or filtering
  jumps back to the top.

| Key | Action |
|---|---|
| `↑ ↓` / `j k`, `PgUp PgDn`, `g G` | move |
| `:` | command: `proc`, `tree`, `events`, `help`, `q` |
| `/` | live filter by name, pid or user (`esc` clears) |
| `enter` / `d` | describe the selected process |
| `ctrl-k` | kill dialog: `←/→` (or `h/l`, `tab`) picks TERM/KILL/INT/HUP/STOP/CONT, `y`/`enter` sends, `n`/`esc` cancels |
| `shift-c/m/p/n/t/s` | sort by CPU / MEM / PID / Name / Threads / State (again = reverse) |
| `?` | help · `esc` back · `ctrl-l` redraw · `ctrl-c` / `q` quit |

In **events** (`:events`): newest first. `space` pauses, `c` clears, `f` / `x` / `z`
show or hide fork / exec / exit, `/` filters, `enter` describes the process. The
footer shows events per second and whether any were lost ("kernel dropped" =
overwritten in the module's ring buffer, "pmtop missed" = gaps in `seq`).

In **describe**: `j/k` moves through the children, `enter` describes a child,
`p` jumps to the parent, `ctrl-k` kills, `esc` goes back. If the process exits
while you look at it, the view keeps its last values and marks it *exited*.

**Kill is race-free.** pmtop never does `kill(pid)`. It opens a pidfd (a handle
to that one process), checks the process still has the start time you saw
(otherwise the pid was reused by something else), and signals through the pidfd.
Without root you can only signal your own processes; pmtop explains `EPERM` instead
of failing silently.

Row colors: **green** running, **yellow** D/T (blocked or stopped),
**red** zombie, **gray** kernel threads and idle, **aqua** everything else.

## Build & run

Requirements: kernel headers for the running kernel
(`linux-headers-$(uname -r)`), `gcc`, `make`, `libncurses-dev`.

```bash
make            # builds kmod/procmon.ko and tui/pmtop
make load       # sudo insmod (use ./scripts/load.sh max_events=4096 for params)
cat /proc/procmon/summary
./scripts/smoke_test.sh
make run        # start pmtop   (pmtop -d 500 for a 500 ms refresh)
make test       # unit + kill tests; + procfs/xcheck tests when the module is loaded
make unload
```

Known limitations:

- Kernel threads show their 15-char name (`pool_workqueue_`), as `ps` does.
  `/proc` shows longer names using kernel-internal data that modules can't reach.
- RSS is the exact value (like `ps` and `/proc/<pid>/statm`). `/proc/<pid>/stat`
  shows a slightly lower *approximate* value, so don't compare against that.

> If `insmod` fails with *Key was rejected by service*, Secure Boot is
> enforcing module signatures. Either disable Secure Boot in the VM or
> sign the module with a MOK key.

## Roadmap

| Step | Scope | Status |
|---|---|---|
| 1 | Structure, build system, shared ABI, procfs plumbing, TUI chrome | **done** |
| 2 | `pm_collect.c`: real process data + summary | **done** |
| 3 | `source.c` parsers, `metrics.c` CPU%/MEM% → live process table | **done** |
| 4 | detail view, kill dialog, uid → username, header CPU gauge | **done** |
| 5 | `pm_events.c` tracepoints + ring buffer, events view | **done** |
| 6 | tree view (`:tree` is still a placeholder), more polish | optional, not started |

Every step is verified: `make test` runs the unit and kill tests, plus the
procfs, event and `/proc` cross-checks when the module is loaded.
