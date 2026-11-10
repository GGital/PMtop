/*
 * test_actions.c - The kill path against REAL processes we spawn:
 * pid-reuse protection, permission errors, self-protection.
 *
 *   make -C tui test
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../src/actions.h"

static int failures, checks;

#define CHECK(cond) do {                                               \
	checks++;                                                      \
	if (!(cond)) {                                                 \
		failures++;                                            \
		fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	}                                                              \
} while (0)

/* start time as procmon would report it (ns), derived from /proc ticks */
static unsigned long long start_ns_of(pid_t pid)
{
	char path[64], buf[1024], *p;
	unsigned long long ticks = 0;
	long hz = sysconf(_SC_CLK_TCK);
	FILE *fp;
	size_t n;

	snprintf(path, sizeof(path), "/proc/%d/stat", pid);
	fp = fopen(path, "r");
	if (!fp)
		return 0;
	n = fread(buf, 1, sizeof(buf) - 1, fp);
	fclose(fp);
	buf[n] = '\0';
	p = strrchr(buf, ')') + 2;
	for (int f = 3; f < 22; f++)
		p = strchr(p, ' ') + 1;
	sscanf(p, "%llu", &ticks);
	return ticks * (1000000000ULL / (unsigned long long)hz);
}

static pid_t spawn_sleeper(void)
{
	pid_t pid = fork();

	if (pid == 0) {
		pause();
		_exit(0);
	}
	usleep(50 * 1000);  /* let it get scheduled */
	return pid;
}

int main(void)
{
	pid_t child = spawn_sleeper();
	unsigned long long start = start_ns_of(child);
	int status;

	CHECK(start > 0);

	/* wrong start time = "pid was reused": must NOT signal */
	CHECK(action_signal(child, start + 5ULL * 1000000000ULL, SIGTERM) == -ESRCH);
	CHECK(kill(child, 0) == 0);                       /* still alive */

	/* right identity: delivered */
	CHECK(action_signal(child, start, SIGTERM) == 0);
	CHECK(waitpid(child, &status, 0) == child);
	CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);

	/* gone (and reaped): ESRCH, not a crash */
	CHECK(action_signal(child, start, SIGTERM) == -ESRCH);

	/* STOP / CONT round trip */
	child = spawn_sleeper();
	start = start_ns_of(child);
	CHECK(action_signal(child, start, SIGSTOP) == 0);
	CHECK(waitpid(child, &status, WUNTRACED) == child && WIFSTOPPED(status));
	CHECK(action_signal(child, start, SIGCONT) == 0);
	CHECK(action_signal(child, start, SIGKILL) == 0);
	CHECK(waitpid(child, &status, 0) == child && WTERMSIG(status) == SIGKILL);

	/* not ours (pid 1 as a normal user): EPERM */
	if (geteuid() != 0)
		CHECK(action_signal(1, start_ns_of(1), SIGTERM) == -EPERM);

	/* never ourselves */
	CHECK(action_signal(getpid(), start_ns_of(getpid()), SIGTERM) == -EDEADLK);
	CHECK(action_signal(0, 0, SIGTERM) == -EINVAL);

	printf("%d/%d action checks passed\n", checks - failures, checks);
	return failures ? 1 : 0;
}
