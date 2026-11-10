/*
 * theme.h - Color palette in the spirit of k9s: dark background,
 *           orange accents, aqua table rows, blue key hints.
 *           Uses 256 colors when available, falls back to 8.
 */
#ifndef PMTOP_THEME_H
#define PMTOP_THEME_H

enum color_pair {
	CP_DEFAULT = 1,
	CP_LOGO,          /* ascii logo, top right             */
	CP_INFO_LABEL,    /* "Kernel:" etc. in header          */
	CP_INFO_VALUE,
	CP_KEY,           /* "<ctrl-k>" key hints              */
	CP_KEY_DESC,      /* "Kill" next to the key            */
	CP_BORDER,        /* table frame                       */
	CP_TITLE,         /* " processes(all) "                */
	CP_TITLE_ACCENT,  /* "[312]" count / filter in title   */
	CP_TABLE_HEADER,  /* column names                      */
	CP_ROW,           /* normal row (sleeping)             */
	CP_ROW_SELECTED,  /* cursor bar                        */
	CP_ROW_RUNNING,   /* state R                           */
	CP_ROW_WARN,      /* state D / T                       */
	CP_ROW_ZOMBIE,    /* state Z                           */
	CP_ROW_DIM,       /* kernel threads / idle             */
	CP_CRUMB,         /* breadcrumb "<processes>"          */
	CP_CRUMB_ACTIVE,
	CP_FLASH_INFO,
	CP_FLASH_ERR,
	CP_PROMPT,        /* ":" / "/" prompt box              */
	CP_GAUGE_LOW,     /* CPU/MEM bars in header            */
	CP_GAUGE_MID,
	CP_GAUGE_HIGH,
	CP_COUNT
};

void theme_init(void);

#endif
