#!/usr/bin/env python3
# Verifies /proc/procmon/events against processes we start ourselves,
# so we know exactly which events MUST (and must NOT) appear.
# Run after: make load
import os, signal, subprocess, sys, time

EV = "/proc/procmon/events"
fails = 0

def check(cond, msg):
    global fails
    print(("  ok    " if cond else "  FAIL  ") + msg)
    fails += not cond

def read_events():
    with open(EV) as f:
        assert f.readline().startswith("# procmon-events v1"), "bad header"
        out = []
        for line in f:
            seq, ts, typ, pid, ppid, code, comm = line.rstrip("\n").split("\t")
            out.append(dict(seq=int(seq), ts=int(ts), type=typ, pid=int(pid),
                            ppid=int(ppid), code=int(code), comm=comm))
        return out

def summary():
    with open("/proc/procmon/summary") as f:
        f.readline()
        return {k: v for k, v in (l.rstrip("\n").split("\t", 1) for l in f)}

def new_events(after):
    time.sleep(0.05)
    return [e for e in read_events() if e["seq"] > after]

def last_seq():
    return int(summary()["events_seq"])

print("1. fork + exec + exit(3) of a shell")
s = last_seq()
p = subprocess.Popen(["sh", "-c", "exit 3"]); p.wait()
ev = [e for e in new_events(s) if e["pid"] == p.pid]
types = [e["type"] for e in ev]
check(types == ["fork", "exec", "exit"], f"sequence fork,exec,exit (got {types})")
if len(ev) == 3:
    check(ev[0]["ppid"] == os.getpid(), f"fork ppid is us ({ev[0]['ppid']} == {os.getpid()})")
    me = open("/proc/self/comm").read().strip()
    check(ev[0]["comm"] == me, f"fork comm is still the parent's ({ev[0]['comm']} == {me})")
    check(ev[1]["comm"] == "sh", f"exec comm is the new program ({ev[1]['comm']})")
    check(ev[2]["code"] == 3 << 8, f"exit code is wait-status 3<<8=768 (got {ev[2]['code']})")
    check(ev[0]["ts"] <= ev[1]["ts"] <= ev[2]["ts"], "timestamps in order")

print("2. killed by SIGKILL")
s = last_seq()
p = subprocess.Popen(["sleep", "30"]); time.sleep(0.1); p.kill(); p.wait()
ex = [e for e in new_events(s) if e["pid"] == p.pid and e["type"] == "exit"]
check(len(ex) == 1 and ex[0]["code"] == signal.SIGKILL, f"exit code is signal 9 (got {[e['code'] for e in ex]})")

print("3. threads are NOT processes")
s = last_seq()
p = subprocess.Popen([sys.executable, "-c",
    "import threading,time\n"
    "ts=[threading.Thread(target=time.sleep,args=(0.2,)) for _ in range(8)]\n"
    "[t.start() for t in ts]; [t.join() for t in ts]"])
p.wait()
ev = new_events(s)
mine = [e["type"] for e in ev if e["pid"] == p.pid]
thread_forks = [e for e in ev if e["type"] == "fork" and e["ppid"] == p.pid]
check(mine == ["fork", "exec", "exit"], f"one fork/exec/exit for the process (got {mine})")
check(not thread_forks, f"8 pthread_create()s produced no fork events (got {len(thread_forks)})")

print("4. exit_group() with a thread still running: one exit, group's code")
s = last_seq()
p = subprocess.Popen([sys.executable, "-c",
    "import threading,time,sys\n"
    "threading.Thread(target=time.sleep,args=(5,),daemon=True).start()\n"
    "time.sleep(0.1); sys.exit(5)"])     # worker still sleeping at exit
p.wait()
ex = [e for e in new_events(s) if e["pid"] == p.pid and e["type"] == "exit"]
check(len(ex) == 1, f"exactly one exit event (got {len(ex)})")
check(len(ex) == 1 and ex[0]["code"] == 5 << 8, f"exit code 5<<8 (got {[e['code'] for e in ex]})")

print("5. ring overflow: oldest dropped, seq stays gapless")
cap_before = len(read_events())
d0 = int(summary()["events_dropped"])
subprocess.run(["sh", "-c", "i=0; while [ $i -lt 1500 ]; do /bin/true; i=$((i+1)); done"])
ev = read_events()
seqs = [e["seq"] for e in ev]
d1 = int(summary()["events_dropped"])
check(len(ev) >= cap_before and len(ev) == len(set(seqs)), f"ring full at {len(ev)} events, no duplicates")
check(all(b == a + 1 for a, b in zip(seqs, seqs[1:])), "seq numbers consecutive oldest -> newest")
check(d1 > d0, f"events_dropped grew ({d0} -> {d1})")
check(seqs[-1] <= last_seq(), "summary events_seq >= newest event read")

print("\nALL EVENT CHECKS PASSED" if not fails else f"\n{fails} CHECK(S) FAILED")
sys.exit(1 if fails else 0)
