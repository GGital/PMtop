#include <ncurses.h>

#include "theme.h"

/* 256-color indexes */
enum {
	C_BLACK = 16, C_WHITE = 255, C_GRAY = 245, C_DARKGRAY = 238,
	C_ORANGE = 208, C_AQUA = 51, C_BLUE = 33, C_FUCHSIA = 201,
	C_GREEN = 46, C_YELLOW = 226, C_RED = 196, C_PAPAYA = 223,
};

void theme_init(void)
{
	if (!has_colors())
		return;
	start_color();
	use_default_colors();   /* -1 = terminal's own background */

	if (COLORS >= 256) {
		init_pair(CP_DEFAULT,      C_WHITE,   -1);
		init_pair(CP_LOGO,         C_ORANGE,  -1);
		init_pair(CP_INFO_LABEL,   C_ORANGE,  -1);
		init_pair(CP_INFO_VALUE,   C_WHITE,   -1);
		init_pair(CP_KEY,          C_BLUE,    -1);
		init_pair(CP_KEY_DESC,     C_GRAY,    -1);
		init_pair(CP_BORDER,       C_BLUE,    -1);
		init_pair(CP_TITLE,        C_AQUA,    -1);
		init_pair(CP_TITLE_ACCENT, C_FUCHSIA, -1);
		init_pair(CP_TABLE_HEADER, C_WHITE,   -1);
		init_pair(CP_ROW,          C_AQUA,    -1);
		init_pair(CP_ROW_SELECTED, C_BLACK,   C_AQUA);
		init_pair(CP_ROW_RUNNING,  C_GREEN,   -1);
		init_pair(CP_ROW_WARN,     C_YELLOW,  -1);
		init_pair(CP_ROW_ZOMBIE,   C_RED,     -1);
		init_pair(CP_ROW_DIM,      C_GRAY,    -1);
		init_pair(CP_CRUMB,        C_BLACK,   C_BLUE);
		init_pair(CP_CRUMB_ACTIVE, C_BLACK,   C_ORANGE);
		init_pair(CP_FLASH_INFO,   C_PAPAYA,  -1);
		init_pair(CP_FLASH_ERR,    C_RED,     -1);
		init_pair(CP_PROMPT,       C_WHITE,   C_DARKGRAY);
		init_pair(CP_GAUGE_LOW,    C_GREEN,   -1);
		init_pair(CP_GAUGE_MID,    C_YELLOW,  -1);
		init_pair(CP_GAUGE_HIGH,   C_RED,     -1);
	} else {
		init_pair(CP_DEFAULT,      COLOR_WHITE,   -1);
		init_pair(CP_LOGO,         COLOR_YELLOW,  -1);
		init_pair(CP_INFO_LABEL,   COLOR_YELLOW,  -1);
		init_pair(CP_INFO_VALUE,   COLOR_WHITE,   -1);
		init_pair(CP_KEY,          COLOR_BLUE,    -1);
		init_pair(CP_KEY_DESC,     COLOR_WHITE,   -1);
		init_pair(CP_BORDER,       COLOR_BLUE,    -1);
		init_pair(CP_TITLE,        COLOR_CYAN,    -1);
		init_pair(CP_TITLE_ACCENT, COLOR_MAGENTA, -1);
		init_pair(CP_TABLE_HEADER, COLOR_WHITE,   -1);
		init_pair(CP_ROW,          COLOR_CYAN,    -1);
		init_pair(CP_ROW_SELECTED, COLOR_BLACK,   COLOR_CYAN);
		init_pair(CP_ROW_RUNNING,  COLOR_GREEN,   -1);
		init_pair(CP_ROW_WARN,     COLOR_YELLOW,  -1);
		init_pair(CP_ROW_ZOMBIE,   COLOR_RED,     -1);
		init_pair(CP_ROW_DIM,      COLOR_WHITE,   -1);
		init_pair(CP_CRUMB,        COLOR_BLACK,   COLOR_BLUE);
		init_pair(CP_CRUMB_ACTIVE, COLOR_BLACK,   COLOR_YELLOW);
		init_pair(CP_FLASH_INFO,   COLOR_YELLOW,  -1);
		init_pair(CP_FLASH_ERR,    COLOR_RED,     -1);
		init_pair(CP_PROMPT,       COLOR_WHITE,   COLOR_BLACK);
		init_pair(CP_GAUGE_LOW,    COLOR_GREEN,   -1);
		init_pair(CP_GAUGE_MID,    COLOR_YELLOW,  -1);
		init_pair(CP_GAUGE_HIGH,   COLOR_RED,     -1);
	}
}
