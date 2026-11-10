/*
 * source.c - Reads and parses the procmon text interface.
 *
 * Every file starts with "<magic> v<N>". We refuse to parse a version
 * we don't know (module and TUI built from different trees) instead of
 * silently showing garbage.
 *
 * Parsing strategy: getline() each line, split in place on TAB into a
 * fixed number of fields, convert with strtoll/strtoull and reject the
 * line if anything doesn't fit. A bad line is counted and skipped; one
 * corrupt row never blanks the whole table.
 */
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "source.h"

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static double now_monotonic(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* "<magic> v1..." -> 0, anything else -> -EPROTO */
static int check_header(const char *line, const char *magic)
{
	size_t n = strlen(magic);

	if (strncmp(line, magic, n) != 0 || line[n] != ' ' || line[n + 1] != 'v')
		return -EPROTO;
	if (atoi(line + n + 2) != PM_FORMAT_VERSION)
		return -EPROTO;
	return 0;
}

/* Undo the kernel's seq_escape(): "\ooo" (3 octal digits) -> one byte. */
size_t source_unescape(char *dst, size_t len, const char *src)
{
	size_t o = 0;

	if (!len)
		return 0;
	while (*src && o + 1 < len) {
		if (src[0] == '\\' &&
		    src[1] >= '0' && src[1] <= '3' &&
		    src[2] >= '0' && src[2] <= '7' &&
		    src[3] >= '0' && src[3] <= '7') {
			dst[o++] = (char)(((src[1] - '0') << 6) |
					  ((src[2] - '0') << 3) | (src[3] - '0'));
			src += 4;
		} else {
			dst[o++] = *src++;
		}
	}
	dst[o] = '\0';
	return o;
}

/*
 * Split `line` in place on TAB. Returns the number of fields found.
 * Returns -1 if there are MORE than `max` fields (format drift).
 */
static int split_tabs(char *line, char **f, int max)
{
	int n = 0;

	line[strcspn(line, "\n")] = '\0';
	for (;;) {
		char *tab = strchr(line, '\t');

		if (n == max)
			return -1;
		f[n++] = line;
		if (!tab)
			return n;
		*tab = '\0';
		line = tab + 1;
	}
}

static bool to_ll(const char *s, long long *out)
{
	char *end;

	if (!*s)
		return false;
	errno = 0;
	*out = strtoll(s, &end, 10);
	return errno == 0 && *end == '\0';
}

static bool to_ull(const char *s, unsigned long long *out)
{
	char *end;

	if (!*s || *s == '-')
		return false;
	errno = 0;
	*out = strtoull(s, &end, 10);
	return errno == 0 && *end == '\0';
}

/* ------------------------------------------------------------------ */
/* processes                                                          */
/* ------------------------------------------------------------------ */

static bool parse_proc_line(char *line, struct proc_row *r)
{
	char *f[PMF_COUNT];
	long long pid, ppid, nice, prio;
	unsigned long long uid, thr, ut, st, start, vsz, rss;

	if (split_tabs(line, f, PMF_COUNT) != PMF_COUNT)
		return false;
	if (!to_ll(f[PMF_PID], &pid) || !to_ll(f[PMF_PPID], &ppid) ||
	    !to_ull(f[PMF_UID], &uid) || strlen(f[PMF_STATE]) != 1 ||
	    !to_ll(f[PMF_NICE], &nice) || !to_ll(f[PMF_PRIO], &prio) ||
	    !to_ull(f[PMF_THREADS], &thr) || !to_ull(f[PMF_UTIME_NS], &ut) ||
	    !to_ull(f[PMF_STIME_NS], &st) || !to_ull(f[PMF_START_NS], &start) ||
	    !to_ull(f[PMF_VSZ_KB], &vsz) || !to_ull(f[PMF_RSS_KB], &rss))
		return false;

	memset(r, 0, sizeof(*r));
	r->pid      = (pid_t)pid;
	r->ppid     = (pid_t)ppid;
	r->uid      = (uid_t)uid;
	r->state    = f[PMF_STATE][0];
	r->nice     = (int)nice;
	r->prio     = (int)prio;
	r->threads  = (unsigned)thr;
	r->utime_ns = ut;
	r->stime_ns = st;
	r->start_ns = start;
	r->vsz_kb   = (unsigned long)vsz;
	r->rss_kb   = (unsigned long)rss;
	source_unescape(r->comm, sizeof(r->comm), f[PMF_COMM]);
	return true;
}

static int cmp_pid(const void *a, const void *b)
{
	pid_t x = ((const struct proc_row *)a)->pid;
	pid_t y = ((const struct proc_row *)b)->pid;

	return (x > y) - (x < y);
}

int source_parse_processes(FILE *fp, struct dataset *d)
{
	char *line = NULL;
	size_t cap = 0;
	int ret = 0;

	d->nprocs = 0;
	d->bad_lines = 0;

	if (getline(&line, &cap, fp) < 0 || check_header(line, PM_PROCESSES_MAGIC)) {
		ret = -EPROTO;
		goto out;
	}
	while (getline(&line, &cap, fp) >= 0) {
		if (d->nprocs == d->procs_cap) {
			size_t ncap = d->procs_cap ? d->procs_cap * 2 : 512;
			struct proc_row *n = realloc(d->procs, ncap * sizeof(*n));

			if (!n) {
				ret = -ENOMEM;
				goto out;
			}
			d->procs = n;
			d->procs_cap = ncap;
		}
		if (parse_proc_line(line, &d->procs[d->nprocs]))
			d->nprocs++;
		else
			d->bad_lines++;
	}
	/* The kernel emits task-list order; metrics.c needs pid order. */
	qsort(d->procs, d->nprocs, sizeof(*d->procs), cmp_pid);
out:
	free(line);
	return ret;
}

/* ------------------------------------------------------------------ */
/* summary                                                            */
/* ------------------------------------------------------------------ */

enum sum_type { T_U64, T_UINT, T_ULONG, T_STR };

static const struct {
	const char   *key;
	enum sum_type type;
	size_t        off;
} sum_keys[] = {
#define K(key, type, field) { key, type, offsetof(struct sys_summary, field) }
	K(PM_SUM_VERSION,        T_STR,   version),
	K(PM_SUM_UPTIME_NS,      T_U64,   uptime_ns),
	K(PM_SUM_NR_CPUS,        T_UINT,  nr_cpus),
	K(PM_SUM_LOAD1,          T_ULONG, load_x100[0]),
	K(PM_SUM_LOAD5,          T_ULONG, load_x100[1]),
	K(PM_SUM_LOAD15,         T_ULONG, load_x100[2]),
	K(PM_SUM_MEM_TOTAL_KB,   T_ULONG, mem_total_kb),
	K(PM_SUM_MEM_FREE_KB,    T_ULONG, mem_free_kb),
	K(PM_SUM_MEM_AVAIL_KB,   T_ULONG, mem_avail_kb),
	K(PM_SUM_PROCS_TOTAL,    T_UINT,  procs_total),
	K(PM_SUM_PROCS_RUNNING,  T_UINT,  procs_running),
	K(PM_SUM_PROCS_SLEEPING, T_UINT,  procs_sleeping),
	K(PM_SUM_PROCS_STOPPED,  T_UINT,  procs_stopped),
	K(PM_SUM_PROCS_ZOMBIE,   T_UINT,  procs_zombie),
	K(PM_SUM_THREADS_TOTAL,  T_UINT,  threads_total),
	K(PM_SUM_EVENTS_SEQ,     T_U64,   events_seq),
	K(PM_SUM_EVENTS_DROPPED, T_U64,   events_dropped),
#undef K
};

static void set_sum_value(struct sys_summary *s, const char *key, const char *val)
{
	unsigned long long v = 0;

	for (size_t i = 0; i < sizeof(sum_keys) / sizeof(sum_keys[0]); i++) {
		char *field = (char *)s + sum_keys[i].off;

		if (strcmp(sum_keys[i].key, key) != 0)
			continue;
		if (sum_keys[i].type == T_STR) {
			snprintf(field, sizeof(s->version), "%s", val);
			return;
		}
		if (!to_ull(val, &v))
			return;
		switch (sum_keys[i].type) {
		case T_U64:   *(uint64_t *)field = v; break;
		case T_UINT:  *(unsigned *)field = (unsigned)v; break;
		case T_ULONG: *(unsigned long *)field = (unsigned long)v; break;
		case T_STR:   break;
		}
		return;
	}
	/* unknown key: ignore (newer module, older TUI) */
}

int source_parse_summary(FILE *fp, struct sys_summary *s)
{
	char *line = NULL, *f[2];
	size_t cap = 0;
	int ret = 0;

	memset(s, 0, sizeof(*s));
	if (getline(&line, &cap, fp) < 0 || check_header(line, PM_SUMMARY_MAGIC)) {
		ret = -EPROTO;
		goto out;
	}
	while (getline(&line, &cap, fp) >= 0)
		if (split_tabs(line, f, 2) == 2)
			set_sum_value(s, f[0], f[1]);
out:
	free(line);
	return ret;
}

/* ------------------------------------------------------------------ */
/* events                                                             */
/* ------------------------------------------------------------------ */

static int event_type_from_name(const char *s)
{
	for (int t = PM_EV_FORK; t <= PM_EV_EXIT; t++)
		if (strcmp(s, pm_event_type_name(t)) == 0)
			return t;
	return 0;
}

static bool parse_event_line(char *line, struct proc_event *e)
{
	char *f[PME_COUNT];
	unsigned long long seq, ts;
	long long pid, ppid, code;

	if (split_tabs(line, f, PME_COUNT) != PME_COUNT)
		return false;
	if (!to_ull(f[PME_SEQ], &seq) || !to_ull(f[PME_TS_NS], &ts) ||
	    !to_ll(f[PME_PID], &pid) || !to_ll(f[PME_PPID], &ppid) ||
	    !to_ll(f[PME_EXIT_CODE], &code))
		return false;
	e->type = event_type_from_name(f[PME_TYPE]);
	if (!e->type)
		return false;
	e->seq       = seq;
	e->ts_ns     = ts;
	e->pid       = (pid_t)pid;
	e->ppid      = (pid_t)ppid;
	e->exit_code = (int)code;
	source_unescape(e->comm, sizeof(e->comm), f[PME_COMM]);
	return true;
}

int source_parse_events(FILE *fp, struct event_log *log)
{
	char *line = NULL;
	size_t cap = 0;
	struct proc_event e;
	int ret = 0;

	if (!log->items) {
		log->items = calloc(EVENT_LOG_MAX, sizeof(*log->items));
		if (!log->items)
			return -ENOMEM;
	}
	if (getline(&line, &cap, fp) < 0 || check_header(line, PM_EVENTS_MAGIC)) {
		ret = -EPROTO;
		goto out;
	}
	while (getline(&line, &cap, fp) >= 0) {
		if (!parse_event_line(line, &e) || e.seq <= log->last_seq)
			continue;
		if (log->last_seq && e.seq > log->last_seq + 1)
			log->missed += e.seq - log->last_seq - 1;
		if (log->len == EVENT_LOG_MAX) {      /* drop the oldest */
			memmove(log->items, log->items + 1,
				(EVENT_LOG_MAX - 1) * sizeof(*log->items));
			log->len--;
		}
		log->items[log->len++] = e;
		log->last_seq = e.seq;
	}
out:
	free(line);
	return ret;
}

/* ------------------------------------------------------------------ */
/* entry points                                                       */
/* ------------------------------------------------------------------ */

static int parse_file(const char *path, int (*fn)(FILE *, void *), void *arg)
{
	FILE *fp = fopen(path, "re");
	int ret;

	if (!fp)
		return -errno;
	ret = fn(fp, arg);
	fclose(fp);
	return ret;
}

static int parse_processes_cb(FILE *fp, void *d) { return source_parse_processes(fp, d); }
static int parse_summary_cb(FILE *fp, void *s)   { return source_parse_summary(fp, s); }
static int parse_events_cb(FILE *fp, void *l)    { return source_parse_events(fp, l); }

static void set_error(struct dataset *d, const char *what, int err)
{
	snprintf(d->error, sizeof(d->error), "%s: %s", what,
		 err == -EPROTO ? "unknown format version (rebuild module + TUI)"
				: strerror(-err));
}

int source_refresh(struct dataset *d)
{
	int ret;

	d->error[0] = '\0';
	if (access(PM_PROC_ROOT, F_OK) != 0) {
		int err = errno;

		d->module_loaded = false;
		d->nprocs = 0;
		memset(&d->sum, 0, sizeof(d->sum));
		snprintf(d->error, sizeof(d->error), "%s: %s", PM_PROC_ROOT, strerror(err));
		return -err;
	}
	d->module_loaded = true;

	/* timestamp right before the read: metrics.c divides by this */
	d->taken_at = now_monotonic();
	ret = parse_file(PM_PATH_PROCESSES, parse_processes_cb, d);
	if (ret) {
		d->nprocs = 0;
		set_error(d, PM_PATH_PROCESSES, ret);
		return ret;
	}
	ret = parse_file(PM_PATH_SUMMARY, parse_summary_cb, &d->sum);
	if (ret)
		set_error(d, PM_PATH_SUMMARY, ret);
	return ret;
}

int source_read_events(struct event_log *log)
{
	return parse_file(PM_PATH_EVENTS, parse_events_cb, log);
}

void source_free(struct dataset *d)
{
	free(d->procs);
	memset(d, 0, sizeof(*d));
}

void event_log_free(struct event_log *log)
{
	free(log->items);
	memset(log, 0, sizeof(*log));
}
