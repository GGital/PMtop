#include <pwd.h>
#include <stdio.h>

#include "users.h"

#define USER_CACHE_MAX 256

static struct {
	uid_t uid;
	char  name[32];
} cache[USER_CACHE_MAX];
static int ncache;

const char *user_name(uid_t uid)
{
	static char fallback[16];
	struct passwd *pw;

	for (int i = 0; i < ncache; i++)
		if (cache[i].uid == uid)
			return cache[i].name;

	pw = getpwuid(uid);
	if (ncache == USER_CACHE_MAX) {          /* absurdly many users */
		snprintf(fallback, sizeof(fallback), "%u", uid);
		return pw ? pw->pw_name : fallback;
	}
	cache[ncache].uid = uid;
	if (pw)
		snprintf(cache[ncache].name, sizeof(cache[ncache].name), "%s", pw->pw_name);
	else
		snprintf(cache[ncache].name, sizeof(cache[ncache].name), "%u", uid);
	return cache[ncache++].name;
}
