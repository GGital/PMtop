/*
 * actions.h - Things pmtop DOES to processes (as opposed to showing).
 *
 * Sending a signal by pid alone is racy: between the user pressing
 * <y> and kill(2), the process may exit and the pid be reused by an
 * unrelated process. We avoid that with a pidfd:
 *
 *   1. fd = pidfd_open(pid)          -> fd now refers to ONE process,
 *                                       even if the pid is later reused
 *   2. read /proc/<pid>/stat start   -> is it still the process the
 *                                       user saw (same start time)?
 *   3. pidfd_send_signal(fd, sig)    -> goes to the pinned process,
 *                                       or fails with ESRCH if it died
 */
#ifndef PMTOP_ACTIONS_H
#define PMTOP_ACTIONS_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct signal_choice {
	int         sig;
	const char *name;      /* "TERM" */
	const char *desc;      /* shown in the kill dialog */
};

extern const struct signal_choice signal_choices[];
extern const size_t               nsignal_choices;

/*
 * Send `sig` to the process (pid, start_ns) the user selected.
 * start_ns is procmon's start_ns (boot-based ns).
 * Returns 0, or -errno: -ESRCH (gone / pid reused), -EPERM (not yours).
 */
int action_signal(pid_t pid, uint64_t start_ns, int sig);

/* Human-readable result for the flash line. */
void action_describe_error(char *buf, size_t len, int err, pid_t pid);

#endif
