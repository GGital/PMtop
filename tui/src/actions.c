#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/pidfd.h>
#include <unistd.h>

#include "actions.h"

const struct signal_choice signal_choices[] = {
	{ SIGTERM, "TERM", "ask it to exit (can be caught)" },
	{ SIGKILL, "KILL", "force exit now (cannot be caught)" },
	{ SIGINT,  "INT",  "like pressing ctrl-c" },
	{ SIGHUP,  "HUP",  "hang up / often: reload config" },
	{ SIGSTOP, "STOP", "pause (cannot be caught)" },
	{ SIGCONT, "CONT", "resume a stopped process" },
};
const size_t nsignal_choices = sizeof(signal_choices) / sizeof(signal_choices[0]);

/* Start time from /proc/<pid>/stat (field 22), in clock ticks. */
static int stat_start_ticks(pid_t pid, unsigned long long *ticks)
{
	char path[64], buf[1024], *p;
	FILE *fp;
	size_t n;

	snprintf(path, sizeof(path), "/proc/%d/stat", pid);
	fp = fopen(path, "re");
	if (!fp)
		return -ESRCH;
	n = fread(buf, 1, sizeof(buf) - 1, fp);
	fclose(fp);
	buf[n] = '\0';

	/* comm may contain spaces and ')' -> start after the LAST ')' */
	p = strrchr(buf, ')');
	if (!p)
		return -EPROTO;
	/* after ") " come fields 3.. ; start time is field 22 -> skip 19 */
	p += 2;
	for (int field = 3; field < 22; field++) {
		p = strchr(p, ' ');
		if (!p)
			return -EPROTO;
		p++;
	}
	return sscanf(p, "%llu", ticks) == 1 ? 0 : -EPROTO;
}

int action_signal(pid_t pid, uint64_t start_ns, int sig)
{
	unsigned long long ticks, expect;
	long hz = sysconf(_SC_CLK_TCK);
	int fd, ret = 0;

	if (pid <= 0)
		return -EINVAL;
	if (pid == getpid())
		return -EDEADLK;              /* refuse to kill ourselves */

	fd = pidfd_open(pid, 0);
	if (fd < 0)
		return -errno;

	/* The kernel reports start time as floor(ns / (1e9 / HZ)). */
	expect = start_ns / (1000000000ULL / (unsigned long long)hz);
	ret = stat_start_ticks(pid, &ticks);
	if (ret == 0 && (ticks + 1 < expect || ticks > expect + 1))
		ret = -ESRCH;                 /* same pid, different process */

	if (ret == 0 && pidfd_send_signal(fd, sig, NULL, 0) < 0)
		ret = -errno;
	close(fd);
	return ret;
}

void action_describe_error(char *buf, size_t len, int err, pid_t pid)
{
	switch (err) {
	case -ESRCH:
		snprintf(buf, len, "pid %d already exited (or its pid was reused) - nothing sent", pid);
		break;
	case -EPERM:
		snprintf(buf, len, "pid %d: permission denied - not your process (run pmtop with sudo)", pid);
		break;
	case -EDEADLK:
		snprintf(buf, len, "refusing to signal pmtop itself");
		break;
	default:
		snprintf(buf, len, "pid %d: %s", pid, strerror(-err));
		break;
	}
}
