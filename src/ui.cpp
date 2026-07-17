#include "ui.h"

#include <lvgl.h>
#include <stdio.h>
#include <time.h>

// Design tokens — dark theme, warm terracotta accent. See plan for rationale.
#define COLOR_BG      lv_color_hex(0x12100E)
#define COLOR_SURFACE lv_color_hex(0x1C1917)
#define COLOR_ACCENT  lv_color_hex(0xD97757)
#define COLOR_ACCENT2 lv_color_hex(0xE8DCC8)
#define COLOR_TEXT    lv_color_hex(0xF4F1EA)
#define COLOR_TEXT2   lv_color_hex(0x8A8580)

static const char *DAY_LABELS[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

static lv_obj_t *tileview;
static lv_obj_t *tile_today, *tile_week, *tile_models;

static lv_obj_t *today_arc;
static lv_obj_t *today_pct_label;
static lv_obj_t *today_sub_label;

static lv_obj_t *week_chart;
static lv_chart_series_t *week_series;
static lv_obj_t *week_header_label;

static lv_obj_t *model_rows[3];
static lv_obj_t *model_labels[3];
static lv_obj_t *model_bars[3];

static lv_obj_t *led;
static lv_obj_t *nav_dots[3];
static lv_obj_t *clock_label;
static lv_obj_t *wifi_icon;

static void style_tile_bg(lv_obj_t *tile) {
  lv_obj_set_style_bg_color(tile, COLOR_BG, 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tile, 0, 0);
  lv_obj_set_style_pad_all(tile, 16, 0);
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  return l;
}

static void build_today_tile(lv_obj_t *tile) {
  style_tile_bg(tile);

  lv_obj_t *title = make_label(tile, &lv_font_montserrat_20, COLOR_TEXT);
  lv_label_set_text(title, "Today");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

  today_arc = lv_arc_create(tile);
  lv_obj_set_size(today_arc, 220, 220);
  lv_arc_set_rotation(today_arc, 270);
  lv_arc_set_bg_angles(today_arc, 0, 360);
  lv_arc_set_range(today_arc, 0, 100);
  lv_arc_set_value(today_arc, 0);
  lv_obj_remove_style(today_arc, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(today_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_color(today_arc, COLOR_SURFACE, LV_PART_MAIN);
  lv_obj_set_style_arc_width(today_arc, 18, LV_PART_MAIN);
  lv_obj_set_style_arc_color(today_arc, COLOR_ACCENT, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(today_arc, 18, LV_PART_INDICATOR);
  lv_obj_center(today_arc);

  today_pct_label = make_label(tile, &lv_font_montserrat_48, COLOR_TEXT);
  lv_label_set_text(today_pct_label, "--%");
  lv_obj_center(today_pct_label);

  today_sub_label = make_label(tile, &lv_font_montserrat_14, COLOR_TEXT2);
  lv_label_set_text(today_sub_label, "");
  lv_obj_align(today_sub_label, LV_ALIGN_BOTTOM_MID, 0, -8);
}

static void build_week_tile(lv_obj_t *tile) {
  style_tile_bg(tile);

  lv_obj_t *title = make_label(tile, &lv_font_montserrat_20, COLOR_TEXT);
  lv_label_set_text(title, "This Week");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

  week_header_label = make_label(tile, &lv_font_montserrat_14, COLOR_TEXT2);
  lv_label_set_text(week_header_label, "");
  lv_obj_align(week_header_label, LV_ALIGN_TOP_MID, 0, 32);

  week_chart = lv_chart_create(tile);
  lv_obj_set_size(week_chart, 260, 180);
  lv_obj_align(week_chart, LV_ALIGN_CENTER, 0, 10);
  lv_chart_set_type(week_chart, LV_CHART_TYPE_BAR);
  lv_chart_set_range(week_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
  lv_chart_set_div_line_count(week_chart, 0, 0);
  lv_obj_set_style_bg_color(week_chart, COLOR_SURFACE, 0);
  lv_obj_set_style_border_width(week_chart, 0, 0);
  lv_obj_set_style_bg_color(week_chart, COLOR_ACCENT2, LV_PART_ITEMS);
  week_series = lv_chart_add_series(week_chart, COLOR_ACCENT2, LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_point_count(week_chart, 7);
  for (int i = 0; i < 7; i++) lv_chart_set_next_value(week_chart, week_series, 0);

  lv_obj_t *row = lv_obj_create(tile);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, 260, 20);
  lv_obj_align_to(row, week_chart, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  for (int i = 0; i < 7; i++) {
    make_label(row, &lv_font_montserrat_14, COLOR_TEXT2);
    lv_label_set_text(lv_obj_get_child(row, i), DAY_LABELS[i]);
  }
}

static void build_models_tile(lv_obj_t *tile) {
  style_tile_bg(tile);

  lv_obj_t *title = make_label(tile, &lv_font_montserrat_20, COLOR_TEXT);
  lv_label_set_text(title, "Models");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

  lv_obj_t *col = lv_obj_create(tile);
  lv_obj_remove_style_all(col);
  lv_obj_set_size(col, 300, 260);
  lv_obj_align(col, LV_ALIGN_CENTER, 0, 10);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  for (int i = 0; i < 3; i++) {
    lv_obj_t *row = lv_obj_create(col);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 60);
    model_rows[i] = row;

    lv_obj_t *label = make_label(row, &lv_font_montserrat_14, COLOR_TEXT);
    lv_label_set_text(label, "");
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    model_labels[i] = label;

    lv_obj_t *bar = lv_bar_create(row);
    lv_obj_set_size(bar, lv_pct(100), 14);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, COLOR_ACCENT, LV_PART_INDICATOR);
    model_bars[i] = bar;
  }
}

static void build_led(lv_obj_t *parent) {
  led = lv_obj_create(parent);
  lv_obj_remove_style_all(led);
  lv_obj_set_size(led, 14, 14);
  lv_obj_set_style_radius(led, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(led, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(led, COLOR_SURFACE, 0);
  lv_obj_align(led, LV_ALIGN_TOP_RIGHT, -14, 14);
  lv_obj_clear_flag(led, LV_OBJ_FLAG_CLICKABLE);
}

static void build_nav_dots(lv_obj_t *parent) {
  for (int i = 0; i < 3; i++) {
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, i == 0 ? COLOR_ACCENT : COLOR_TEXT2, 0);
    lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, (i - 1) * 20, -10);
    nav_dots[i] = dot;
  }
}

// Updates the top-bar clock (24h local time) and WiFi indicator once/second.
static void status_timer_cb(lv_timer_t *timer) {
  (void)timer;

  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  if (t.tm_year + 1900 >= 2020) { // NTP synced
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text(clock_label, buf);
  } else {
    lv_label_set_text(clock_label, "--:--");
  }

  lv_obj_set_style_text_color(
      wifi_icon, wifi_is_connected() ? COLOR_TEXT2 : lv_color_hex(0x5A2B2B), 0);
}

static void build_status_bar(lv_obj_t *parent) {
  clock_label = make_label(parent, &lv_font_montserrat_20, COLOR_TEXT2);
  lv_label_set_text(clock_label, "--:--");
  lv_obj_align(clock_label, LV_ALIGN_TOP_LEFT, 14, 10);

  // Sits just left of the thinking LED (which is at x offset -14).
  wifi_icon = make_label(parent, &lv_font_montserrat_14, COLOR_TEXT2);
  lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
  lv_obj_align(wifi_icon, LV_ALIGN_TOP_RIGHT, -36, 12);

  lv_timer_create(status_timer_cb, 1000, NULL);
}

static void tileview_scroll_event_cb(lv_event_t *e) {
  lv_obj_t *tv = lv_event_get_target_obj(e);
  lv_obj_t *tile = lv_tileview_get_tile_act(tv);
  int idx = 0;
  if (tile == tile_week) idx = 1;
  else if (tile == tile_models) idx = 2;
  for (int i = 0; i < 3; i++) {
    lv_obj_set_style_bg_color(nav_dots[i], i == idx ? COLOR_ACCENT : COLOR_TEXT2, 0);
  }
}

void ui_init() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, COLOR_BG, 0);

  tileview = lv_tileview_create(scr);
  lv_obj_set_size(tileview, lv_pct(100), lv_pct(100));

  tile_today = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_RIGHT);
  tile_week = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_HOR);
  tile_models = lv_tileview_add_tile(tileview, 2, 0, LV_DIR_LEFT);

  build_today_tile(tile_today);
  build_week_tile(tile_week);
  build_models_tile(tile_models);

  build_led(lv_layer_top());
  build_nav_dots(lv_layer_top());
  build_status_bar(lv_layer_top());

  lv_obj_add_event_cb(tileview, tileview_scroll_event_cb, LV_EVENT_SCROLL_END, NULL);
}

void ui_update(const UsageData &data) {
  lv_arc_set_value(today_arc, data.session_used_pct);

  char buf[48];
  snprintf(buf, sizeof(buf), "%d%%", data.session_used_pct);
  lv_label_set_text(today_pct_label, buf);

  time_t now = time(nullptr);
  long remaining_min = (data.session_resets_at - now) / 60;
  if (remaining_min < 0) remaining_min = 0;
  snprintf(buf, sizeof(buf), "%ldk / %ldk tokens - resets in %ldm",
           data.tokens_used / 1000, data.tokens_limit / 1000, remaining_min);
  lv_label_set_text(today_sub_label, buf);

  snprintf(buf, sizeof(buf), "%d%% used this week", data.week_used_pct);
  lv_label_set_text(week_header_label, buf);
  for (int i = 0; i < 7; i++) {
    lv_chart_set_value_by_id(week_chart, week_series, i, data.daily_pct[i]);
  }
  lv_chart_refresh(week_chart);

  for (int i = 0; i < 3; i++) {
    if (i < data.model_count) {
      lv_obj_remove_flag(model_rows[i], LV_OBJ_FLAG_HIDDEN);
      snprintf(buf, sizeof(buf), "%s  %d%%", data.models[i].name, data.models[i].pct);
      lv_label_set_text(model_labels[i], buf);
      lv_bar_set_value(model_bars[i], data.models[i].pct, LV_ANIM_ON);
    } else {
      lv_obj_add_flag(model_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
  }

  lv_obj_set_style_bg_color(led, data.thinking ? COLOR_ACCENT : COLOR_SURFACE, 0);
}

void ui_set_thinking(bool thinking) {
  lv_obj_set_style_bg_color(led, thinking ? COLOR_ACCENT : COLOR_SURFACE, 0);
}
