#include "ui_notifications.h"
#include "ui_styles.h"
#include "ui_dc_clock.h"

#include <string.h>

static constexpr int k_notif_max = 24;
static constexpr int k_panel_h = 320;
static constexpr int k_header_h = 52;
static constexpr int k_item_h = 74;

struct UiNotifEntry {
    bool active;
    UiNotifSeverity severity;
    uint32_t seq;
    char key[40];
    char title[64];
    char body[160];
    char ts[16];
};

static UiNotifEntry s_entries[k_notif_max];
static uint32_t s_seq_counter = 0;

static lv_obj_t* s_scr = nullptr;
static lv_obj_t* s_backdrop = nullptr;
static lv_obj_t* s_panel = nullptr;
static lv_obj_t* s_list = nullptr;
static bool s_open = false;

static lv_color_t _notif_color(UiNotifSeverity severity) {
    switch (severity) {
        case UI_NOTIF_ALERT: return UI_COLOR_ERROR;
        case UI_NOTIF_INFO: return UI_COLOR_WARNING;
        default: return UI_COLOR_SUCCESS;
    }
}

static const char* _notif_icon(UiNotifSeverity severity) {
    switch (severity) {
        case UI_NOTIF_ALERT: return LV_SYMBOL_WARNING;
        case UI_NOTIF_INFO: return LV_SYMBOL_BELL;
        default: return LV_SYMBOL_OK;
    }
}

static void _notif_make_timestamp(char* out, size_t out_size) {
    if (!out || out_size == 0) return;

    struct tm tm_local = {};
    if (ui_dc_clock_get_local_tm(&tm_local)) {
        lv_snprintf(out, (uint32_t)out_size, "%02d:%02d", tm_local.tm_hour, tm_local.tm_min);
    } else {
        lv_snprintf(out, (uint32_t)out_size, "--:--");
    }
}

static int _notif_find_slot(const char* key) {
    if (!key || !key[0]) return -1;
    for (int i = 0; i < k_notif_max; i++) {
        if (!s_entries[i].active) continue;
        if (strncmp(s_entries[i].key, key, sizeof(s_entries[i].key)) == 0) return i;
    }
    return -1;
}

static int _notif_allocate_slot() {
    for (int i = 0; i < k_notif_max; i++) {
        if (!s_entries[i].active) return i;
    }

    int oldest = 0;
    for (int i = 1; i < k_notif_max; i++) {
        if (s_entries[i].seq < s_entries[oldest].seq) oldest = i;
    }
    return oldest;
}

static bool _notif_sort_before(const UiNotifEntry& a, const UiNotifEntry& b) {
    if (a.severity != b.severity) return a.severity > b.severity;
    return a.seq > b.seq;
}

static void _notif_rebuild_list();

static void _notif_panel_set_open(bool open) {
    if (!s_panel || !s_backdrop) return;
    s_open = open;
    if (open) {
        lv_obj_clear_flag(s_backdrop, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_panel);
    } else {
        lv_obj_add_flag(s_backdrop, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void _notif_backdrop_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_notif_panel_close();
}

static void _notif_close_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_notif_panel_close();
}

static void _notif_clear_all_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_notif_clear_all();
}

static void _notif_obj_delete_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    lv_obj_t* obj = lv_event_get_target(e);
    if (obj == s_backdrop) s_backdrop = nullptr;
    if (obj == s_panel) s_panel = nullptr;
    if (obj == s_list) s_list = nullptr;
}

static void _notif_scr_delete_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    lv_obj_t* obj = lv_event_get_target(e);
    if (obj != s_scr) return;
    s_scr = nullptr;
    s_backdrop = nullptr;
    s_panel = nullptr;
    s_list = nullptr;
    s_open = false;
}

static void _notif_item_delete_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    int* slot = static_cast<int*>(lv_event_get_user_data(e));
    delete slot;
}

static void _notif_item_click_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int* slot = static_cast<int*>(lv_event_get_user_data(e));
    if (!slot) return;
    if (*slot < 0 || *slot >= k_notif_max) return;
    s_entries[*slot].active = false;
    _notif_rebuild_list();
}

static void _notif_make_item(lv_obj_t* parent, int slot) {
    const UiNotifEntry& entry = s_entries[slot];

    lv_obj_t* item = lv_btn_create(parent);
    lv_obj_set_width(item, LV_PCT(100));
    lv_obj_set_height(item, k_item_h);
    lv_obj_set_style_bg_color(item, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_color(item, lv_color_hex(0x1A2C48), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(item, 0, 0);
    lv_obj_set_style_radius(item, 10, 0);
    lv_obj_set_style_shadow_width(item, 0, 0);
    lv_obj_set_style_pad_all(item, 0, 0);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    int* user_slot = new int(slot);
    if (user_slot) {
        lv_obj_add_event_cb(item, _notif_item_delete_cb, LV_EVENT_DELETE, user_slot);
        lv_obj_add_event_cb(item, _notif_item_click_cb, LV_EVENT_CLICKED, user_slot);
    }

    lv_obj_t* bar = lv_obj_create(item);
    lv_obj_set_size(bar, 5, LV_PCT(100));
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, _notif_color(entry.severity), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* ico = lv_label_create(item);
    lv_label_set_text(ico, _notif_icon(entry.severity));
    lv_obj_set_style_text_font(ico, UI_FONT_SUBTITLE, 0);
    lv_obj_set_style_text_color(ico, _notif_color(entry.severity), 0);
    lv_obj_align(ico, LV_ALIGN_LEFT_MID, 18, -10);

    lv_obj_t* title = lv_label_create(item);
    lv_label_set_text(title, entry.title);
    lv_obj_set_style_text_font(title, UI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_width(title, 760);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 56, 10);

    lv_obj_t* body = lv_label_create(item);
    lv_label_set_text(body, entry.body);
    lv_obj_set_style_text_font(body, UI_FONT_TINY, 0);
    lv_obj_set_style_text_color(body, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_width(body, 820);
    lv_label_set_long_mode(body, LV_LABEL_LONG_DOT);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 56, 34);

    lv_obj_t* ts = lv_label_create(item);
    lv_label_set_text(ts, entry.ts);
    lv_obj_set_style_text_font(ts, UI_FONT_TINY, 0);
    lv_obj_set_style_text_color(ts, UI_COLOR_TEXT_DIM, 0);
    lv_obj_align(ts, LV_ALIGN_TOP_RIGHT, -16, 12);
}

static void _notif_rebuild_list() {
    if (!s_list) return;
    lv_obj_clean(s_list);

    int ordered[k_notif_max];
    int count = 0;
    for (int i = 0; i < k_notif_max; i++) {
        if (!s_entries[i].active) continue;
        int insert_at = count;
        for (int j = 0; j < count; j++) {
            if (_notif_sort_before(s_entries[i], s_entries[ordered[j]])) {
                insert_at = j;
                break;
            }
        }
        for (int j = count; j > insert_at; j--) {
            ordered[j] = ordered[j - 1];
        }
        ordered[insert_at] = i;
        count++;
    }

    if (count == 0) {
        lv_obj_t* empty = lv_label_create(s_list);
        lv_label_set_text(empty, "Nessuna notifica presente.");
        lv_obj_set_style_text_font(empty, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(empty, UI_COLOR_TEXT_DIM, 0);
        lv_obj_center(empty);
        return;
    }

    for (int i = 0; i < count; i++) {
        _notif_make_item(s_list, ordered[i]);
    }
}

void ui_notif_panel_init(lv_obj_t* scr, lv_obj_t* /*header*/) {
    s_scr = scr;
    lv_obj_add_event_cb(scr, _notif_scr_delete_cb, LV_EVENT_DELETE, nullptr);

    if (s_backdrop) lv_obj_del(s_backdrop);
    if (s_panel) lv_obj_del(s_panel);
    s_backdrop = nullptr;
    s_panel = nullptr;
    s_list = nullptr;
    s_open = false;

    s_backdrop = lv_obj_create(scr);
    lv_obj_set_size(s_backdrop, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(s_backdrop, 0, 0);
    lv_obj_set_style_bg_color(s_backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_backdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(s_backdrop, 0, 0);
    lv_obj_set_style_radius(s_backdrop, 0, 0);
    lv_obj_clear_flag(s_backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_backdrop, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_backdrop, _notif_backdrop_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_backdrop, _notif_obj_delete_cb, LV_EVENT_DELETE, nullptr);

    s_panel = lv_obj_create(scr);
    lv_obj_set_size(s_panel, UI_SCREEN_W, k_panel_h);
    lv_obj_set_pos(s_panel, 0, UI_HEADER_H);
    lv_obj_set_style_bg_color(s_panel, UI_COLOR_BG_MAIN, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_panel, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_radius(s_panel, 0, 0);
    lv_obj_set_style_pad_all(s_panel, 0, 0);
    lv_obj_set_style_shadow_color(s_panel, lv_color_black(), 0);
    lv_obj_set_style_shadow_width(s_panel, 18, 0);
    lv_obj_set_style_shadow_opa(s_panel, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_y(s_panel, 6, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_panel, _notif_obj_delete_cb, LV_EVENT_DELETE, nullptr);

    lv_obj_t* top = lv_obj_create(s_panel);
    lv_obj_set_size(top, UI_SCREEN_W, k_header_h);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_set_style_bg_color(top, UI_COLOR_HEADER, 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_left(top, 16, 0);
    lv_obj_set_style_pad_right(top, 16, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(top);
    lv_label_set_text(title, LV_SYMBOL_BELL "  Notifiche");
    lv_obj_set_style_text_font(title, UI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* clear_btn = lv_btn_create(top);
    lv_obj_set_size(clear_btn, 150, 34);
    lv_obj_align(clear_btn, LV_ALIGN_RIGHT_MID, -72, 0);
    lv_obj_set_style_bg_color(clear_btn, UI_COLOR_BG_CARD2, 0);
    lv_obj_set_style_bg_color(clear_btn, lv_color_hex(0x243A5E), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(clear_btn, 0, 0);
    lv_obj_set_style_radius(clear_btn, 8, 0);
    lv_obj_set_style_shadow_width(clear_btn, 0, 0);
    lv_obj_add_event_cb(clear_btn, _notif_clear_all_btn_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* clear_lbl = lv_label_create(clear_btn);
    lv_label_set_text(clear_lbl, LV_SYMBOL_TRASH " Cancella");
    lv_obj_set_style_text_font(clear_lbl, UI_FONT_TINY, 0);
    lv_obj_set_style_text_color(clear_lbl, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_center(clear_lbl);

    lv_obj_t* close_btn = lv_btn_create(top);
    lv_obj_set_size(close_btn, 56, 34);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(close_btn, UI_COLOR_BG_CARD2, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x243A5E), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_set_style_shadow_width(close_btn, 0, 0);
    lv_obj_add_event_cb(close_btn, _notif_close_btn_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_lbl, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_center(close_lbl);

    s_list = lv_obj_create(s_panel);
    lv_obj_set_size(s_list, UI_SCREEN_W, k_panel_h - k_header_h);
    lv_obj_set_pos(s_list, 0, k_header_h);
    lv_obj_set_style_bg_color(s_list, UI_COLOR_BG_MAIN, 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 12, 0);
    lv_obj_set_style_pad_row(s_list, 8, 0);
    lv_obj_set_layout(s_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_list, _notif_obj_delete_cb, LV_EVENT_DELETE, nullptr);

    _notif_rebuild_list();
}

void ui_notif_panel_open(void) {
    _notif_panel_set_open(true);
}

void ui_notif_panel_close(void) {
    _notif_panel_set_open(false);
}

void ui_notif_panel_toggle(void) {
    _notif_panel_set_open(!s_open);
}

void ui_notif_push_or_update(const char* key, UiNotifSeverity severity,
                             const char* title, const char* body) {
    if (!key || !key[0]) return;

    int slot = _notif_find_slot(key);
    if (slot >= 0 && s_entries[slot].active) {
        UiNotifEntry& existing = s_entries[slot];
        const char* safe_title = (title && title[0]) ? title : "Notifica";
        const char* safe_body = (body && body[0]) ? body : "-";
        if (existing.severity == severity &&
            strncmp(existing.title, safe_title, sizeof(existing.title)) == 0 &&
            strncmp(existing.body, safe_body, sizeof(existing.body)) == 0) {
            return;
        }
    }
    if (slot < 0) slot = _notif_allocate_slot();

    UiNotifEntry& entry = s_entries[slot];
    memset(&entry, 0, sizeof(entry));
    entry.active = true;
    entry.severity = severity;
    entry.seq = ++s_seq_counter;
    strncpy(entry.key, key, sizeof(entry.key) - 1);
    strncpy(entry.title, (title && title[0]) ? title : "Notifica", sizeof(entry.title) - 1);
    strncpy(entry.body, (body && body[0]) ? body : "-", sizeof(entry.body) - 1);
    _notif_make_timestamp(entry.ts, sizeof(entry.ts));

    _notif_rebuild_list();
}

void ui_notif_clear(const char* key) {
    const int slot = _notif_find_slot(key);
    if (slot < 0) return;
    s_entries[slot].active = false;
    _notif_rebuild_list();
}

void ui_notif_clear_prefix(const char* prefix) {
    if (!prefix || !prefix[0]) return;
    const size_t prefix_len = strlen(prefix);
    bool changed = false;
    for (int i = 0; i < k_notif_max; i++) {
        if (!s_entries[i].active) continue;
        if (strncmp(s_entries[i].key, prefix, prefix_len) != 0) continue;
        s_entries[i].active = false;
        changed = true;
    }
    if (changed) _notif_rebuild_list();
}

void ui_notif_clear_all(void) {
    memset(s_entries, 0, sizeof(s_entries));
    _notif_rebuild_list();
}

UiNotifSeverity ui_notif_highest_severity(void) {
    UiNotifSeverity highest = UI_NOTIF_NONE;
    for (int i = 0; i < k_notif_max; i++) {
        if (!s_entries[i].active) continue;
        if (s_entries[i].severity > highest) highest = s_entries[i].severity;
    }
    return highest;
}

int ui_notif_count(void) {
    int count = 0;
    for (int i = 0; i < k_notif_max; i++) {
        if (s_entries[i].active) count++;
    }
    return count;
}
