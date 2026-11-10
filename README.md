# procmon — Process Monitoring Kernel Module + `pmtop` TUI

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
│       ├── input.[ch]        key dispatch, : and / modes
│       └── views/
│           ├── view.h        `struct view` interface
│           ├── registry.c    list of views + global key hints
│           ├── processes.c   main process table
│           ├── tree.c        parent/child tree
│           ├── detail.c      one process in depth
│           ├── events.c      live fork/exec/exit stream
│           └── help.c        key bindings
└── scripts/
    ├── load.sh               insmod (reloads if already loaded)
    ├── unload.sh             rmmod
    └── smoke_test.sh         checks /proc/procmon format
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
 Host:    studentVM          <enter>   Describe   <shift-t> Sort Thr    ┌─┐┌┬┐┌┬┐┌─┐┌─┐
 Kernel:  7.0.0-30-generic   <ctrl-k>  Kill       <shift-s> Sort State  ├─┘│││ │ │ │├─┘
 procmon: v0.1.0             <shift-c> Sort CPU   <:>       Command     ┴  ┴ ┴ ┴ └─┘┴
 Uptime:  0d 03:12:44        <shift-m> Sort MEM   </>       Filter
 Load:    0.52 0.40 0.31     <shift-p> Sort PID   <?>       Help        process monitor
 MEM:     [█████░░░░░░░░░]  38%  <shift-n> Sort Name  <esc>     Back
 Procs:   312  R2 S305 Z0
┌──────────────────────────── processes(all)[312] ─────────────────────────────┐
│    PID    PPID  USER   S   PRI  NI  THR  CPU%↓  MEM%    VSZ     RSS  NAME    │
│   1423    1200  1000   R    20   0   34   12.5   4.1   3.2G  310.4M  firefox │  ← cursor bar
│    812       1  0      S    20   0    1    0.3   0.2  22.1M    9.8M  sshd    │
└──────────────────────────────────────────────────────────────────────────────┘
  <processes>                                                    flash messages
```

| Key | Action |
|---|---|
| `↑ ↓` / `j k`, `PgUp PgDn`, `g G` | move |
| `:` | command: `proc`, `tree`, `events`, `help`, `q` |
| `/` | live filter by name or pid (`esc` clears) |
| `enter` / `d` | describe the selected process |
| `ctrl-k` | kill (with confirmation) |
| `shift-c/m/p/n/t/s` | sort by CPU / MEM / PID / Name / Threads / State (again = reverse) |
| `?` | help · `esc` back · `ctrl-l` redraw · `ctrl-c` / `q` quit |

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
make run        # start pmtop
make unload
```

> If `insmod` fails with *Key was rejected by service*, Secure Boot is
> enforcing module signatures. Either disable Secure Boot in the VM or
> sign the module with a MOK key.

## Roadmap

| Step | Scope | Status |
|---|---|---|
| 1 | Structure, build system, shared ABI, procfs plumbing, TUI chrome | **done** |
| 2 | `pm_collect.c`: real process data + summary | todo |
| 3 | `source.c` parsers, `metrics.c` CPU%/MEM% → live process table | todo |
| 4 | detail view, kill dialog, uid → username, header CPU gauge | todo |
| 5 | `pm_events.c` tracepoints + ring buffer, events view | todo |
| 6 | tree view, polish, smoke tests, stress test with many processes | todo |

Look for `TODO(step N)` in the code to see exactly what each step fills in.
