/*
 * users.h - uid -> user name, cached.
 *
 * getpwuid() can be slow (it may ask sssd/LDAP), and the table asks
 * for a name on every row of every frame, so each uid is looked up
 * once and remembered. Unknown uids are shown as the number.
 */
#ifndef PMTOP_USERS_H
#define PMTOP_USERS_H

#include <sys/types.h>

const char *user_name(uid_t uid);

#endif
