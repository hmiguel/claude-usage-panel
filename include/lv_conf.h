// Placeholder LVGL v9 config — enough to compile the UI/net/main logic now.
// Per README §2.2: replace with the Waveshare demo's lv_conf.h once it's
// available, keeping LV_FONT_MONTSERRAT_14/_20/_48 enabled below.

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16

#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_STDINT_INCLUDE   <stdint.h>
#define LV_STDDEF_INCLUDE   <stddef.h>
#define LV_STDBOOL_INCLUDE  <stdbool.h>
#define LV_INTTYPES_INCLUDE <inttypes.h>
#define LV_LIMITS_INCLUDE   <limits.h>
#define LV_STDARG_INCLUDE   <stdarg.h>

#define LV_USE_OS LV_OS_NONE

// Tick source is registered at runtime via lv_tick_set_cb() in board_init.cpp.
#define LV_TICK_CUSTOM 0

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_LOG 1
#if LV_USE_LOG
  #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
  #define LV_LOG_PRINTF 1
#endif

#endif // LV_CONF_H
