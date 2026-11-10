/*
 * format.h - Human-readable number formatting shared by all views.
 */
#ifndef PMTOP_FORMAT_H
#define PMTOP_FORMAT_H

#include <stddef.h>
#include <stdint.h>

void fmt_kb(char *buf, size_t len, unsigned long kb);        /* 120.6M  3.2G   */
void fmt_cputime(char *buf, size_t len, uint64_t ns);        /* 1:42.26 2:03:04 */
void fmt_duration(char *buf, size_t len, uint64_t ns);       /* 3d 4h  12m 5s   */
void fmt_uptime(char *buf, size_t len, uint64_t ns);         /* 0d 03:12:44     */
const char *fmt_state_name(char state);                      /* 'R' -> "running" */

#endif
