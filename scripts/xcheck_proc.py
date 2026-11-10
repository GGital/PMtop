#!/usr/bin/env python3
# Cross-checks /proc/procmon/processes against the kernel's own /proc/<pid>/stat.
# Run after: make load.  Kernel-thread names are skipped: /proc shows their
# extended names, which are not reachable from a module (we show 15 chars, like ps).
import os
HZ = os.sysconf('SC_CLK_TCK'); PG = os.sysconf('SC_PAGE_SIZE') // 1024
rows = {}
with open('/proc/procmon/processes') as f:
    assert f.readline().startswith('# procmon-processes v1')
    for line in f:
        v = line.rstrip('\n').split('\t'); rows[int(v[0])] = v
mism = {}; checked = 0
def bad(k, pid, a, b):
    mism.setdefault(k, []).append((pid, a, b))
for pid, v in rows.items():
    if pid == os.getpid():      # we grow while running: our own RSS is a moving target
        continue
    try:
        st = open(f'/proc/{pid}/stat').read()
        statm = open(f'/proc/{pid}/statm').read().split()
    except FileNotFoundError:
        continue
    comm = st[st.index('(')+1:st.rindex(')')]
    f = st[st.rindex(')')+2:].split()
    checked += 1
    # f[0]=state f[1]=ppid f[11]=utime f[12]=stime f[15]=prio f[16]=nice f[17]=threads f[19]=start f[20]=vsize f[21]=rss
    if v[1] != f[1]: bad('ppid', pid, v[1], f[1])
    if v[4] != f[16]: bad('nice', pid, v[4], f[16])
    if v[5] != f[15]: bad('prio', pid, v[5], f[15])
    if v[6] != f[17]: bad('threads', pid, v[6], f[17])
    if abs(int(v[9])//(10**9//HZ) - int(f[19])) > 1: bad('start', pid, v[9], f[19])
    if int(v[10]) != int(f[20])//1024: bad('vsz', pid, v[10], int(f[20])//1024)
    # RSS: /proc/<pid>/stat uses the kernel's fast *approximate* per-CPU
    # counter; statm (like ps) uses the exact sum, which procmon reports.
    if int(v[11]) != int(statm[1])*PG: bad('rss', pid, v[11], int(statm[1])*PG)
    cpu_k = (int(v[7]) + int(v[8])) / 1e9; cpu_p = (int(f[11]) + int(f[12])) / HZ
    if abs(cpu_k - cpu_p) > max(0.05, 0.02*cpu_p): bad('cpu(>0.05s/2%)', pid, round(cpu_k,2), round(cpu_p,2))
    if v[10] != '0' and comm.replace(' ', '\\040') != v[12]: bad('comm', pid, v[12], comm)
    if v[3] != f[0] and f[0] not in 'RS' : bad('state', pid, v[3], f[0])
ps = {int(d) for d in os.listdir('/proc') if d.isdigit()}
print(f"procmon rows: {len(rows)}   /proc pids now: {len(ps)}   compared: {checked}")
print("missing from procmon:", sorted(ps - set(rows))[:10], " extra in procmon (exited since):", len(set(rows) - ps))
for k, l in mism.items(): print(f"MISMATCH {k}: {len(l)}  e.g. {l[:3]}")
if not mism: print("ALL FIELDS MATCH")
