#include <stdio.h>

#include "format.h"
#include "procmon_shared.h"

void fmt_kb(char *buf, size_t len, unsigned long kb)
{
	if (kb >= 1024UL * 1024)
		snprintf(buf, len, "%.1fG", kb / (1024.0 * 1024));
	else if (kb >= 1024)
		snprintf(buf, len, "%.1fM", kb / 1024.0);
	else
		snprintf(buf, len, "%luK", kb);
}

/* cumulative cpu ns -> "m:ss.cc" below an hour, "h:mm:ss" above */
void fmt_cputime(char *buf, size_t len, uint64_t ns)
{
	unsigned long long cs = ns / 10000000ULL, s = cs / 100;

	if (s >= 3600)
		snprintf(buf, len, "%llu:%02llu:%02llu", s / 3600, s / 60 % 60, s % 60);
	else
		snprintf(buf, len, "%llu:%02llu.%02llu", s / 60, s % 60, cs % 100);
}

/* the two most significant units: "3d 4h", "12m 5s", "42s" */
void fmt_duration(char *buf, size_t len, uint64_t ns)
{
	unsigned long long s = ns / 1000000000ULL;

	if (s >= 86400)
		snprintf(buf, len, "%llud %lluh", s / 86400, s / 3600 % 24);
	else if (s >= 3600)
		snprintf(buf, len, "%lluh %llum", s / 3600, s / 60 % 60);
	else if (s >= 60)
		snprintf(buf, len, "%llum %llus", s / 60, s % 60);
	else
		snprintf(buf, len, "%llus", s);
}

void fmt_uptime(char *buf, size_t len, uint64_t ns)
{
	unsigned long long s = ns / 1000000000ULL;

	snprintf(buf, len, "%llud %02llu:%02llu:%02llu",
		 s / 86400, s / 3600 % 24, s / 60 % 60, s % 60);
}

const char *fmt_state_name(char state)
{
	switch (state) {
	case PM_STATE_RUNNING:  return "running";
	case PM_STATE_SLEEPING: return "sleeping";
	case PM_STATE_DISK:     return "disk sleep (uninterruptible)";
	case PM_STATE_STOPPED:  return "stopped";
	case PM_STATE_TRACED:   return "traced";
	case PM_STATE_ZOMBIE:   return "zombie (waiting for parent)";
	case PM_STATE_DEAD:     return "dead";
	case PM_STATE_IDLE:     return "idle (kernel thread)";
	default:                return "unknown";
	}
}
