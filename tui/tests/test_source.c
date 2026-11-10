/*
 * test_source.c - Unit tests for the parsers (source.c) and CPU%/MEM%
 *                 (metrics.c). No kernel module needed: fixtures are fed
 *                 through fmemopen().
 *
 *   make -C tui test
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/metrics.h"
#include "../src/source.h"

static int failures, checks;

#define CHECK(cond) do {                                               \
	checks++;                                                      \
	if (!(cond)) {                                                 \
		failures++;                                            \
		fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	}                                                              \
} while (0)

static FILE *mem(const char *s)
{
	return fmemopen((void *)s, strlen(s), "r");
}

#define PROC_HDR PM_PROCESSES_HEADER "\n"

static void test_unescape(void)
{
	char buf[64];

	source_unescape(buf, sizeof(buf), "Web\\040Content");
	CHECK(strcmp(buf, "Web Content") == 0);
	source_unescape(buf, sizeof(buf), "a\\011b\\134c");      /* tab, backslash */
	CHECK(strcmp(buf, "a\tb\\c") == 0);
	source_unescape(buf, sizeof(buf), "trail\\04");          /* incomplete: literal */
	CHECK(strcmp(buf, "trail\\04") == 0);
	source_unescape(buf, 4, "abcdef");                       /* truncation */
	CHECK(strcmp(buf, "abc") == 0);
}

static void test_processes(void)
{
	struct dataset d = { 0 };
	FILE *fp = mem(PROC_HDR
		"42\t1\t1000\tR\t-5\t15\t3\t100\t200\t5000\t2048\t1024\tWeb\\040Content\n"
		"7\t1\t0\tS\t0\t20\t1\t0\t0\t10\t0\t0\tkthreadd\n"
		"oops\tnot\ta\tline\n"                                    /* too few */
		"8\t1\t0\tS\t0\t20\t1\t0\t0\t10\t0\t0\tname\textra\n"   /* too many */
		"9\t1\t0\tS\t0\t20\t1\t-3\t0\t10\t0\t0\tneg\n");          /* negative u64 */

	CHECK(source_parse_processes(fp, &d) == 0);
	fclose(fp);
	CHECK(d.nprocs == 2);
	CHECK(d.bad_lines == 3);
	CHECK(d.procs[0].pid == 7);                  /* sorted by pid */
	CHECK(d.procs[1].pid == 42);
	CHECK(d.procs[1].ppid == 1);
	CHECK(d.procs[1].uid == 1000);
	CHECK(d.procs[1].state == 'R');
	CHECK(d.procs[1].nice == -5);
	CHECK(d.procs[1].prio == 15);
	CHECK(d.procs[1].threads == 3);
	CHECK(d.procs[1].utime_ns == 100 && d.procs[1].stime_ns == 200);
	CHECK(d.procs[1].start_ns == 5000);
	CHECK(d.procs[1].vsz_kb == 2048 && d.procs[1].rss_kb == 1024);
	CHECK(strcmp(d.procs[1].comm, "Web Content") == 0);

	/* wrong version must be rejected, not half-parsed */
	fp = mem(PM_PROCESSES_MAGIC " v99\tpid\n1\t0\t0\tS\t0\t20\t1\t0\t0\t0\t0\t0\tx\n");
	CHECK(source_parse_processes(fp, &d) != 0);
	CHECK(d.nprocs == 0);
	fclose(fp);

	/* many rows: forces realloc growth past the initial 512 */
	{
		size_t n = 2000, len = 0, cap = n * 64 + 256;
		char *big = malloc(cap);

		len += snprintf(big + len, cap - len, PROC_HDR);
		for (size_t i = n; i > 0; i--)
			len += snprintf(big + len, cap - len,
					"%zu\t1\t0\tS\t0\t20\t1\t0\t0\t0\t0\t0\tp%zu\n", i, i);
		fp = mem(big);
		CHECK(source_parse_processes(fp, &d) == 0);
		CHECK(d.nprocs == n);
		CHECK(d.procs[0].pid == 1 && d.procs[n - 1].pid == (pid_t)n);
		fclose(fp);
		free(big);
	}
	source_free(&d);
}

static void test_summary(void)
{
	struct sys_summary s;
	FILE *fp = mem(PM_SUMMARY_HEADER "\n"
		"version\t0.1.0\n"
		"uptime_ns\t123456789\n"
		"nr_cpus\t4\n"
		"load1_x100\t41\n"
		"mem_total_kb\t8000000\n"
		"mem_available_kb\t4000000\n"
		"procs_zombie\t2\n"
		"some_future_key\t999\n"         /* must be ignored */
		"events_seq\t77\n");

	CHECK(source_parse_summary(fp, &s) == 0);
	fclose(fp);
	CHECK(strcmp(s.version, "0.1.0") == 0);
	CHECK(s.uptime_ns == 123456789ULL);
	CHECK(s.nr_cpus == 4);
	CHECK(s.load_x100[0] == 41);
	CHECK(s.mem_total_kb == 8000000 && s.mem_avail_kb == 4000000);
	CHECK(s.procs_zombie == 2);
	CHECK(s.events_seq == 77);
}

static void test_events(void)
{
	struct event_log log = { 0 };
	FILE *fp = mem(PM_EVENTS_HEADER "\n"
		"1\t100\tfork\t10\t1\t0\tbash\n"
		"2\t200\texec\t10\t1\t0\tls\n"
		"3\t300\texit\t10\t1\t256\tls\n"
		"4\t400\tbogus\t10\t1\t0\tx\n");   /* unknown type: skipped */

	CHECK(source_parse_events(fp, &log) == 0);
	fclose(fp);
	CHECK(log.len == 3);
	CHECK(log.last_seq == 3);
	CHECK(log.items[2].type == PM_EV_EXIT && log.items[2].exit_code == 256);

	/* re-reading the same ring must not duplicate */
	fp = mem(PM_EVENTS_HEADER "\n"
		"2\t200\texec\t10\t1\t0\tls\n"
		"3\t300\texit\t10\t1\t256\tls\n"
		"5\t500\tfork\t11\t1\t0\tsh\n");
	CHECK(source_parse_events(fp, &log) == 0);
	fclose(fp);
	CHECK(log.len == 4);
	CHECK(log.items[3].seq == 5);
	CHECK(log.missed == 1);                  /* seq 4 was never seen */
	event_log_free(&log);
}

static struct proc_row row(pid_t pid, uint64_t cpu_ns, uint64_t start, unsigned long rss)
{
	struct proc_row r = { .pid = pid, .utime_ns = cpu_ns / 2,
			      .stime_ns = cpu_ns - cpu_ns / 2,
			      .start_ns = start, .rss_kb = rss };
	return r;
}

static void test_metrics(void)
{
	struct proc_row pr[] = {
		row(10, 1000000000ULL, 1, 0),        /* will use 0.5 s      */
		row(20, 5000000000ULL, 2, 0),        /* recycled pid below  */
	};
	struct proc_row cr[] = {
		row(10, 1500000000ULL, 1, 1000),     /* +0.5 s over 1 s = 50% */
		row(20, 100000000ULL, 9500000000ULL, 0),   /* new proc, same pid */
		row(30, 250000000ULL, 9750000000ULL, 0),   /* born 0.25 s ago, used 0.25 s */
	};
	struct dataset prev = { .procs = pr, .nprocs = 2, .taken_at = 100.0 };
	struct dataset cur  = { .procs = cr, .nprocs = 3, .taken_at = 101.0 };

	prev.sum.uptime_ns = 9000000000ULL;
	cur.sum.uptime_ns  = 10000000000ULL;
	cur.sum.nr_cpus = 2;
	cur.sum.mem_total_kb = 4000;

	metrics_compute(&cur, &prev);
	CHECK(fabs(cr[0].cpu_pct - 50.0) < 0.01);
	CHECK(fabs(cr[0].mem_pct - 25.0) < 0.01);
	/* pid 20 recycled: started 0.5 s ago, used 0.1 s -> 20% */
	CHECK(fabs(cr[1].cpu_pct - 20.0) < 0.01);
	/* pid 30 new: 0.25 s over 0.25 s -> 100% */
	CHECK(fabs(cr[2].cpu_pct - 100.0) < 0.01);
	/* total: (0.5 + 0.1 + 0.25) s / (1 s * 2 cpus) = 42.5% */
	CHECK(fabs(cur.cpu_total_pct - 42.5) < 0.01);

	/* first sample (no prev): everything 0, no crash */
	prev.taken_at = 0;
	metrics_compute(&cur, &prev);
	CHECK(cr[0].cpu_pct == 0 && cur.cpu_total_pct == 0);
}

int main(void)
{
	test_unescape();
	test_processes();
	test_summary();
	test_events();
	test_metrics();
	printf("%d/%d checks passed\n", checks - failures, checks);
	return failures ? 1 : 0;
}
