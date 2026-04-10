#include "ui_dc_maintenance.h"

#include <string.h>

static constexpr const char* k_maintenance_pin = "0805";

#define MT_WHITE   lv_color_hex(0xFFFFFF)
#define MT_BG      lv_color_hex(0xEEF3F8)
#define MT_TEXT    lv_color_hex(0x243447)
#define MT_DIM     lv_color_hex(0x7A92B0)
#define MT_BORDER  lv_color_hex(0xDDE5EE)
#define MT_ORANGE  lv_color_hex(0xE84820)
#define MT_ORANGE2 lv_color_hex(0xB02810)
#define MT_ERROR   lv_color_hex(0xC0392B)

struct MaintenancePinCtx {
    lv_obj_t* mask;
    lv_obj_t* ta;
    lv_obj_t* error_lbl;
    UiDcMaintenanceSuccessCb on_success;
    void* user_data;
};

static MaintenancePinCtx* s_ctx = nullptr;

static void _popup_delete_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    MaintenancePinCtx* ctx = static_cast<MaintenancePinCtx*>(lv_event_get_user_data(e));
    if (ctx == s_ctx) s_ctx = nullptr;
    delete ctx;
}

static void _popup_close(MaintenancePinCtx* ctx) {
    if (!ctx || !ctx->mask) return;
    lv_obj_del(ctx->mask);
}

static void _popup_fail(MaintenancePinCtx* ctx) {
    if (!ctx) return;
    if (ctx->error_lbl) {
        lv_label_set_text(ctx->error_lbl, "PIN non valido");
    }
    if (ctx->ta) {
        lv_textarea_set_text(ctx->ta, "");
    }
}

static void _popup_submit(MaintenancePinCtx* ctx) {
    if (!ctx || !ctx->ta) return;
    if (strcmp(lv_textarea_get_text(ctx->ta), k_maintenance_pin) != 0) {
        _popup_fail(ctx);
        return;
    }

    UiDcMaintenanceSuccessCb cb = ctx->on_success;
    void* user_data = ctx->user_data;
    _popup_close(ctx);
    if (cb) cb(user_data);
}

static void _popup_cancel_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _popup_close(static_cast<MaintenancePinCtx*>(lv_event_get_user_data(e)));
}

static void _popup_confirm_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _popup_submit(static_cast<MaintenancePinCtx*>(lv_event_get_user_data(e)));
}

static void _popup_keyboard_cb(lv_event_t* e) {
    const lv_event_code_t code = lv_event_get_code(e);
    MaintenancePinCtx* ctx = static_cast<MaintenancePinCtx*>(lv_event_get_user_data(e));
    if (code == LV_EVENT_READY) {
        _popup_submit(ctx);
    } else if (code == LV_EVENT_CANCEL) {
        _popup_close(ctx);
    }
}

void ui_dc_maintenance_request_pin(const char* action_title,
                                   const char* action_label,
                                   UiDcMaintenanceSuccessCb on_success,
                                   void* user_data) {
    lv_obj_t* parent = lv_scr_act();
    if (!parent) return;

    if (s_ctx && s_ctx->mask) {
        lv_obj_del(s_ctx->mask);
    }

    MaintenancePinCtx* ctx = new MaintenancePinCtx();
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->on_success = on_success;
    ctx->user_data = user_data;

    lv_obj_t* mask = lv_obj_create(parent);
    ctx->mask = mask;
    s_ctx = ctx;
    lv_obj_add_event_cb(mask, _popup_delete_cb, LV_EVENT_DELETE, ctx);
    lv_obj_set_size(mask, 1024, 600);
    lv_obj_set_style_bg_color(mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(mask, LV_OPA_70, 0);
    lv_obj_set_style_border_width(mask, 0, 0);
    lv_obj_set_style_radius(mask, 0, 0);
    lv_obj_set_style_pad_all(mask, 0, 0);
    lv_obj_clear_flag(mask, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* top = lv_obj_create(mask);
    lv_obj_set_size(top, 1024, 168);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_set_style_bg_color(top, MT_WHITE, 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_left(top, 20, 0);
    lv_obj_set_style_pad_right(top, 20, 0);
    lv_obj_set_style_pad_top(top, 14, 0);
    lv_obj_set_style_pad_bottom(top, 14, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(top);
    lv_label_set_text(title, (action_title && action_title[0]) ? action_title : "Accesso manutenzione");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, MT_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* hint = lv_label_create(top);
    lv_label_set_text(hint,
        "Operazione riservata alla manutenzione.\nInserire il PIN numerico per continuare.");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, MT_DIM, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 0, 34);

    lv_obj_t* btn_cancel = lv_btn_create(top);
    lv_obj_set_size(btn_cancel, 150, 44);
    lv_obj_align(btn_cancel, LV_ALIGN_TOP_RIGHT, -190, 0);
    lv_obj_set_style_bg_color(btn_cancel, MT_BG, 0);
    lv_obj_set_style_border_color(btn_cancel, MT_BORDER, 0);
    lv_obj_set_style_border_width(btn_cancel, 1, 0);
    lv_obj_set_style_shadow_width(btn_cancel, 0, 0);
    lv_obj_add_event_cb(btn_cancel, _popup_cancel_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t* lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Annulla");
    lv_obj_set_style_text_color(lbl_cancel, MT_TEXT, 0);
    lv_obj_center(lbl_cancel);

    lv_obj_t* btn_confirm = lv_btn_create(top);
    lv_obj_set_size(btn_confirm, 170, 44);
    lv_obj_align(btn_confirm, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn_confirm, MT_ORANGE, 0);
    lv_obj_set_style_bg_color(btn_confirm, MT_ORANGE2, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_confirm, 0, 0);
    lv_obj_set_style_shadow_width(btn_confirm, 0, 0);
    lv_obj_add_event_cb(btn_confirm, _popup_confirm_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t* lbl_confirm = lv_label_create(btn_confirm);
    lv_label_set_text(lbl_confirm, (action_label && action_label[0]) ? action_label : "Conferma");
    lv_obj_set_style_text_color(lbl_confirm, lv_color_white(), 0);
    lv_obj_center(lbl_confirm);

    lv_obj_t* ta = lv_textarea_create(top);
    ctx->ta = ta;
    lv_obj_set_size(ta, 984, 56);
    lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(ta, LV_TEXT_ALIGN_CENTER, 0);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 4);
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_password_show_time(ta, 0);
    lv_textarea_set_accepted_chars(ta, "0123456789");
    lv_textarea_set_placeholder_text(ta, "PIN manutenzione");

    ctx->error_lbl = lv_label_create(top);
    lv_label_set_text(ctx->error_lbl, "");
    lv_obj_set_style_text_font(ctx->error_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ctx->error_lbl, MT_ERROR, 0);
    lv_obj_align(ctx->error_lbl, LV_ALIGN_BOTTOM_LEFT, 0, -4);

    lv_obj_t* kb = lv_keyboard_create(mask);
    lv_obj_set_size(kb, 1024, 432);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(kb, MT_BG, 0);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_add_event_cb(kb, _popup_keyboard_cb, LV_EVENT_READY, ctx);
    lv_obj_add_event_cb(kb, _popup_keyboard_cb, LV_EVENT_CANCEL, ctx);

    lv_obj_move_foreground(mask);
}
