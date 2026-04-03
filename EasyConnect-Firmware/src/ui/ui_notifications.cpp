/**
 * @file ui_notifications.cpp
 * @brief Pannello notifiche pull-down con swipe-to-dismiss
 *
 * ┌─────────────────── 1024px ──────────────────────┐  y=0
 * │  NOTIFICHE                        [Cancella tutto]│  header panel 44px
 * ├─────────────────────────────────────────────────┤
 * │ [⚠]  Allarme DeltaP                    08:32    │  item 64px
 * │       Pressione differenz. fuori soglia          │
 * ├─────────────────────────────────────────────────┤
 * │ [⚡]  OTA Completato                   08:15    │  item 64px
 * │       Firmware v2.1.4 installato                 │
 * ├─────────────────────────────────────────────────┤
 * │  …                                              │
 * ├─────────────────────────────────────────────────┤
 * │                   ━━━━━                         │  handle 14px
 * └─────────────────────────────────────────────────┘  y=NOTIF_PANEL_H
 *
 * Gesti
 *   • Drag header verso il basso  → apre il pannello
 *   • Tap backdrop (area sotto)   → chiude il pannello
 *   • Drag handle verso l'alto    → chiude il pannello
 *   • Swipe orizzontale su item   → elimina la notifica con animazione
 */

#include "ui_notifications.h"
#include "ui_styles.h"

// ─────────────────────────────────────────────────────────────────────────────
// COSTANTI
// ─────────────────────────────────────────────────────────────────────────────

#define NOTIF_PANEL_H       310   // altezza pannello (px)
#define NOTIF_PANEL_HDR_H    44   // header interno del pannello
#define NOTIF_HANDLE_H       16   // drag handle in fondo
#define NOTIF_ITEM_H         62   // altezza singola notifica
#define NOTIF_ITEM_GAP        4   // spazio tra notifiche

#define NOTIF_OPEN_Y          0   // Y pannello aperto
#define NOTIF_CLOSED_Y     (-NOTIF_PANEL_H)

#define NOTIF_SWIPE_THR     130   // px minimo per dismissione
#define NOTIF_ANIM_MS       230   // animazione slide pannello
#define NOTIF_ITEM_SLIDE_MS 180   // animazione item slide-out
#define NOTIF_ITEM_CLOSE_MS 140   // animazione item height→0

// ─────────────────────────────────────────────────────────────────────────────
// DATI NOTIFICHE
// ─────────────────────────────────────────────────────────────────────────────

struct NotifDef {
    const char* icon;
    lv_color_t  color;   // colore accent (bordino sinistro + icona)
    const char* title;
    const char* body;
    const char* ts;      // timestamp
};

static const NotifDef k_notifs[] = {
    {
        LV_SYMBOL_WARNING,
        lv_color_hex(0xE74C3C),
        "Allarme DeltaP",
        "Pressione differenziale fuori soglia: 187 Pa",
        "08:32"
    },
    {
        LV_SYMBOL_CHARGE,
        lv_color_hex(0x2ECC71),
        "OTA Completato",
        "Firmware v2.1.4 installato con successo",
        "08:15"
    },
    {
        LV_SYMBOL_WIFI,
        lv_color_hex(0x3498DB),
        "Connessione persa",
        "Master EasyConnect non raggiungibile",
        "07:58"
    },
    {
        LV_SYMBOL_BELL,
        lv_color_hex(0xF1C40F),
        "Manutenzione",
        "Filtro G4 richiede sostituzione (> 2000 h)",
        "Ieri"
    },
};
static constexpr int k_notif_count = (int)(sizeof(k_notifs) / sizeof(k_notifs[0]));

// ─────────────────────────────────────────────────────────────────────────────
// STATO GLOBALE PANNELLO
// ─────────────────────────────────────────────────────────────────────────────

static lv_obj_t* g_panel     = nullptr;
static lv_obj_t* g_backdrop  = nullptr;
static lv_obj_t* g_list      = nullptr;   // contenitore scrollabile delle notifiche

static bool     g_panel_open  = false;
static bool     g_dragging    = false;
static int32_t  g_drag_scr_y  = 0;       // screen-Y dove il dito ha iniziato il drag
static int32_t  g_panel_y0    = 0;       // Y pannello a inizio drag

// ─────────────────────────────────────────────────────────────────────────────
// STATO SWIPE PER SINGOLO ITEM (struttura passata come user_data)
// ─────────────────────────────────────────────────────────────────────────────

struct SwipeState {
    int32_t start_x;   // screen-X a inizio swipe
    bool    active;    // swipe in corso
};
static SwipeState g_sw[k_notif_count];   // un slot per item

// ─────────────────────────────────────────────────────────────────────────────
// HELPER: exec-callback per lv_anim_t
// ─────────────────────────────────────────────────────────────────────────────

static void _set_panel_y(void* obj, int32_t y) {
    lv_obj_set_y((lv_obj_t*)obj, y);
}
static void _set_translate_x(void* obj, int32_t x) {
    lv_obj_set_style_translate_x((lv_obj_t*)obj, x, 0);
}
static void _set_opa(void* obj, int32_t opa) {
    lv_obj_set_style_opa((lv_obj_t*)obj, (lv_opa_t)opa, 0);
}
static void _set_height(void* obj, int32_t h) {
    lv_obj_set_height((lv_obj_t*)obj, h);
}

// ─────────────────────────────────────────────────────────────────────────────
// PANNELLO: APRI / CHIUDI
// ─────────────────────────────────────────────────────────────────────────────

static void _panel_animate(int32_t to_y) {
    lv_anim_del(g_panel, _set_panel_y);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_panel);
    lv_anim_set_exec_cb(&a, _set_panel_y);
    lv_anim_set_values(&a, lv_obj_get_y(g_panel), to_y);
    lv_anim_set_time(&a, NOTIF_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void panel_open() {
    if (g_panel_open) return;
    g_panel_open = true;
    _panel_animate(NOTIF_OPEN_Y);
    lv_obj_clear_flag(g_backdrop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_panel);
}

static void panel_close() {
    if (!g_panel_open && lv_obj_get_y(g_panel) <= NOTIF_CLOSED_Y) return;
    g_panel_open = false;
    _panel_animate(NOTIF_CLOSED_Y);
    lv_obj_add_flag(g_backdrop, LV_OBJ_FLAG_HIDDEN);
}

// ─────────────────────────────────────────────────────────────────────────────
// GESTO PULL-DOWN SULL'HEADER
// ─────────────────────────────────────────────────────────────────────────────

static void _header_pressed_cb(lv_event_t* e) {
    lv_point_t pt;
    lv_indev_get_point(lv_indev_get_act(), &pt);
    g_drag_scr_y = pt.y;
    g_panel_y0   = lv_obj_get_y(g_panel);
    g_dragging   = true;
    lv_anim_del(g_panel, _set_panel_y);   // interrompe eventuali animazioni
}

static void _header_pressing_cb(lv_event_t* e) {
    if (!g_dragging) return;
    lv_point_t pt;
    lv_indev_get_point(lv_indev_get_act(), &pt);
    int32_t delta  = pt.y - g_drag_scr_y;
    int32_t new_y  = g_panel_y0 + delta;
    new_y = LV_CLAMP(NOTIF_CLOSED_Y, new_y, NOTIF_OPEN_Y);
    lv_obj_set_y(g_panel, new_y);

    // Mostra il backdrop proporzionalmente all'apertura
    int32_t progress = (new_y - NOTIF_CLOSED_Y) * LV_OPA_40 / (-NOTIF_CLOSED_Y);
    lv_obj_set_style_opa(g_backdrop, (lv_opa_t)LV_CLAMP(0, progress, LV_OPA_40), 0);
    lv_obj_clear_flag(g_backdrop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_panel);
}

static void _header_released_cb(lv_event_t* e) {
    if (!g_dragging) return;
    g_dragging = false;

    lv_point_t vect;
    lv_indev_get_vect(lv_indev_get_act(), &vect);
    int32_t cur_y = lv_obj_get_y(g_panel);

    // Apri se: più di 1/3 visibile OPPURE gesto veloce verso il basso
    bool should_open = (cur_y > NOTIF_CLOSED_Y * 2 / 3) || (vect.y > 4);
    if (should_open) {
        // Ripristina opacità backdrop piena prima di aprire
        lv_obj_set_style_opa(g_backdrop, LV_OPA_40, 0);
        panel_open();
    } else {
        panel_close();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GESTO PULL-UP SULL'HANDLE IN FONDO AL PANNELLO
// ─────────────────────────────────────────────────────────────────────────────

static void _handle_pressed_cb(lv_event_t* e) {
    lv_point_t pt;
    lv_indev_get_point(lv_indev_get_act(), &pt);
    g_drag_scr_y = pt.y;
    g_panel_y0   = lv_obj_get_y(g_panel);
    g_dragging   = true;
    lv_anim_del(g_panel, _set_panel_y);
}

static void _handle_released_cb(lv_event_t* e) {
    if (!g_dragging) return;
    g_dragging = false;
    lv_point_t vect;
    lv_indev_get_vect(lv_indev_get_act(), &vect);
    int32_t cur_y = lv_obj_get_y(g_panel);
    // Chiudi se: meno di 2/3 visibile OPPURE gesto veloce verso l'alto
    bool should_close = (cur_y < NOTIF_CLOSED_Y / 3) || (vect.y < -4);
    if (should_close) panel_close();
    else              panel_open();
}

// ─────────────────────────────────────────────────────────────────────────────
// DISMISS SINGOLA NOTIFICA (animazione a due fasi)
// ─────────────────────────────────────────────────────────────────────────────

// Fase 2: anima l'altezza a 0 poi elimina il widget
static void _phase2_delete_ready(lv_anim_t* a) {
    lv_obj_del((lv_obj_t*)a->var);
}
static void _phase1_ready(lv_anim_t* a) {
    lv_obj_t* item = (lv_obj_t*)a->var;
    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, item);
    lv_anim_set_exec_cb(&b, _set_height);
    lv_anim_set_values(&b, lv_obj_get_height(item), 0);
    lv_anim_set_time(&b, NOTIF_ITEM_CLOSE_MS);
    lv_anim_set_path_cb(&b, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&b, _phase2_delete_ready);
    lv_anim_start(&b);
}

// Fase 1: slide orizzontale + fade → poi fase 2
static void _item_dismiss(lv_obj_t* item, int32_t dir_sign) {
    int32_t cur_tx = lv_obj_get_style_translate_x(item, 0);

    // Slide orizzontale verso fuori
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, item);
    lv_anim_set_exec_cb(&a, _set_translate_x);
    lv_anim_set_values(&a, cur_tx, dir_sign * (UI_SCREEN_W + 60));
    lv_anim_set_time(&a, NOTIF_ITEM_SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&a, _phase1_ready);
    lv_anim_start(&a);

    // Fade parallelo
    int32_t cur_opa = LV_OPA_COVER
                      - (LV_OPA_COVER * LV_ABS(cur_tx)) / (UI_SCREEN_W / 2);
    cur_opa = LV_CLAMP(0, cur_opa, LV_OPA_COVER);
    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, item);
    lv_anim_set_exec_cb(&b, _set_opa);
    lv_anim_set_values(&b, cur_opa, LV_OPA_TRANSP);
    lv_anim_set_time(&b, NOTIF_ITEM_SLIDE_MS);
    lv_anim_start(&b);
}

// Snap-back se swipe annullato
static void _item_snap_back(lv_obj_t* item) {
    int32_t cur_tx = lv_obj_get_style_translate_x(item, 0);
    int32_t cur_opa = LV_OPA_COVER
                      - (LV_OPA_COVER * LV_ABS(cur_tx)) / (UI_SCREEN_W / 2);
    cur_opa = LV_CLAMP(0, cur_opa, LV_OPA_COVER);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, item);
    lv_anim_set_exec_cb(&a, _set_translate_x);
    lv_anim_set_values(&a, cur_tx, 0);
    lv_anim_set_time(&a, NOTIF_ITEM_CLOSE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, item);
    lv_anim_set_exec_cb(&b, _set_opa);
    lv_anim_set_values(&b, cur_opa, LV_OPA_COVER);
    lv_anim_set_time(&b, NOTIF_ITEM_CLOSE_MS);
    lv_anim_start(&b);
}

// ─────────────────────────────────────────────────────────────────────────────
// CALLBACK SWIPE SU ITEM
// ─────────────────────────────────────────────────────────────────────────────

static void _item_pressed_cb(lv_event_t* e) {
    auto* sw = (SwipeState*)lv_event_get_user_data(e);
    lv_point_t pt;
    lv_indev_get_point(lv_indev_get_act(), &pt);
    sw->start_x = pt.x;
    sw->active  = true;
}

static void _item_pressing_cb(lv_event_t* e) {
    auto* sw = (SwipeState*)lv_event_get_user_data(e);
    if (!sw->active) return;
    lv_obj_t* item = lv_event_get_target(e);

    lv_point_t pt;
    lv_indev_get_point(lv_indev_get_act(), &pt);
    int32_t dx = pt.x - sw->start_x;

    // Sposta visivamente (translate non altera il layout flex)
    lv_obj_set_style_translate_x(item, dx, 0);

    // Fade proporzionale allo spostamento
    int32_t opa = LV_OPA_COVER - (LV_OPA_COVER * LV_ABS(dx)) / (UI_SCREEN_W / 2);
    lv_obj_set_style_opa(item, (lv_opa_t)LV_CLAMP(0, opa, LV_OPA_COVER), 0);
}

static void _item_released_cb(lv_event_t* e) {
    auto* sw = (SwipeState*)lv_event_get_user_data(e);
    if (!sw->active) return;
    sw->active = false;

    lv_obj_t* item = lv_event_get_target(e);
    int32_t   cur_tx = lv_obj_get_style_translate_x(item, 0);

    if (LV_ABS(cur_tx) >= NOTIF_SWIPE_THR) {
        _item_dismiss(item, cur_tx > 0 ? 1 : -1);
    } else {
        _item_snap_back(item);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CREA UN SINGOLO ITEM NOTIFICA
// ─────────────────────────────────────────────────────────────────────────────

static void _make_notif_item(lv_obj_t* parent, const NotifDef& def, SwipeState* sw) {
    lv_obj_t* item = lv_obj_create(parent);
    lv_obj_set_size(item, UI_SCREEN_W, NOTIF_ITEM_H);
    lv_obj_set_style_bg_color(item, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(item, 0, 0);
    lv_obj_set_style_radius(item, 0, 0);
    lv_obj_set_style_pad_all(item, 0, 0);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    // Bordino colorato a sinistra (4px)
    lv_obj_t* bar = lv_obj_create(item);
    lv_obj_set_size(bar, 4, NOTIF_ITEM_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, def.color, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    // Icona
    lv_obj_t* ico = lv_label_create(item);
    lv_label_set_text(ico, def.icon);
    lv_obj_set_style_text_color(ico, def.color, 0);
    lv_obj_set_style_text_font(ico, UI_FONT_SUBTITLE, 0);
    lv_obj_set_pos(ico, 14, (NOTIF_ITEM_H - 24) / 2);
    lv_obj_clear_flag(ico, LV_OBJ_FLAG_CLICKABLE);

    // Titolo
    lv_obj_t* title = lv_label_create(item);
    lv_label_set_text(title, def.title);
    lv_obj_set_style_text_font(title, UI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_pos(title, 50, 10);
    lv_obj_clear_flag(title, LV_OBJ_FLAG_CLICKABLE);

    // Body
    lv_obj_t* body = lv_label_create(item);
    lv_label_set_text(body, def.body);
    lv_obj_set_style_text_font(body, UI_FONT_TINY, 0);
    lv_obj_set_style_text_color(body, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_pos(body, 50, 30);
    lv_obj_set_width(body, UI_SCREEN_W - 140);
    lv_label_set_long_mode(body, LV_LABEL_LONG_CLIP);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_CLICKABLE);

    // Timestamp (allineato a destra)
    lv_obj_t* ts = lv_label_create(item);
    lv_label_set_text(ts, def.ts);
    lv_obj_set_style_text_font(ts, UI_FONT_TINY, 0);
    lv_obj_set_style_text_color(ts, UI_COLOR_TEXT_DIM, 0);
    lv_obj_align(ts, LV_ALIGN_TOP_RIGHT, -12, 12);
    lv_obj_clear_flag(ts, LV_OBJ_FLAG_CLICKABLE);

    // Separatore in fondo
    lv_obj_t* sep = lv_obj_create(item);
    lv_obj_set_size(sep, UI_SCREEN_W, 1);
    lv_obj_set_pos(sep, 0, NOTIF_ITEM_H - 1);
    lv_obj_set_style_bg_color(sep, UI_COLOR_BORDER, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_CLICKABLE);

    // Swipe callbacks
    sw->active  = false;
    sw->start_x = 0;
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(item, _item_pressed_cb,  LV_EVENT_PRESSED,  sw);
    lv_obj_add_event_cb(item, _item_pressing_cb, LV_EVENT_PRESSING, sw);
    lv_obj_add_event_cb(item, _item_released_cb, LV_EVENT_RELEASED, sw);
}

// ─────────────────────────────────────────────────────────────────────────────
// CALLBACK "CANCELLA TUTTO"
// ─────────────────────────────────────────────────────────────────────────────

static void _clear_all_cb(lv_event_t* e) {
    lv_obj_clean(g_list);   // rimuove tutti i figli del contenitore
}

// ─────────────────────────────────────────────────────────────────────────────
// CALLBACK BACKDROP
// ─────────────────────────────────────────────────────────────────────────────

static void _backdrop_clicked_cb(lv_event_t* e) {
    panel_close();
}

// ─────────────────────────────────────────────────────────────────────────────
// INIT PUBBLICO
// ─────────────────────────────────────────────────────────────────────────────

void ui_notif_panel_init(lv_obj_t* scr, lv_obj_t* header) {

    // ── 1. BACKDROP ─────────────────────────────────────────────────────────
    // Area semi-trasparente sotto il pannello; intercetta il tap per chiuderlo
    g_backdrop = lv_obj_create(scr);
    lv_obj_set_size(g_backdrop, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(g_backdrop, 0, 0);
    lv_obj_set_style_bg_color(g_backdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_backdrop, LV_OPA_40, 0);
    lv_obj_set_style_border_width(g_backdrop, 0, 0);
    lv_obj_set_style_radius(g_backdrop, 0, 0);
    lv_obj_clear_flag(g_backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_backdrop, LV_OBJ_FLAG_HIDDEN);   // nascosto inizialmente
    lv_obj_add_event_cb(g_backdrop, _backdrop_clicked_cb, LV_EVENT_CLICKED, nullptr);

    // ── 2. PANNELLO ──────────────────────────────────────────────────────────
    g_panel = lv_obj_create(scr);
    lv_obj_set_size(g_panel, UI_SCREEN_W, NOTIF_PANEL_H);
    lv_obj_set_pos(g_panel, 0, NOTIF_CLOSED_Y);   // inizia fuori schermo (sopra)
    lv_obj_set_style_bg_color(g_panel, lv_color_hex(0x0A1628), 0);
    lv_obj_set_style_bg_opa(g_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_panel, 0, 0);
    lv_obj_set_style_radius(g_panel, 0, 0);
    lv_obj_set_style_pad_all(g_panel, 0, 0);
    lv_obj_clear_flag(g_panel, LV_OBJ_FLAG_SCROLLABLE);
    // Ombra in basso per evidenziare profondità
    lv_obj_set_style_shadow_color(g_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(g_panel, LV_OPA_60, 0);
    lv_obj_set_style_shadow_width(g_panel, 20, 0);
    lv_obj_set_style_shadow_ofs_y(g_panel, 8, 0);

    // ── 3. HEADER INTERNO DEL PANNELLO ───────────────────────────────────────
    lv_obj_t* ph = lv_obj_create(g_panel);
    lv_obj_set_size(ph, UI_SCREEN_W, NOTIF_PANEL_HDR_H);
    lv_obj_set_pos(ph, 0, 0);
    lv_obj_set_style_bg_color(ph, lv_color_hex(0x0D1C32), 0);
    lv_obj_set_style_bg_opa(ph, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ph, 0, 0);
    lv_obj_set_style_border_side(ph, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(ph, lv_color_hex(0x1E3860), 0);
    lv_obj_set_style_border_width(ph, 1, 0);
    lv_obj_set_style_pad_hor(ph, UI_PADDING, 0);
    lv_obj_set_style_pad_ver(ph, 0, 0);
    lv_obj_clear_flag(ph, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* ph_title = lv_label_create(ph);
    lv_label_set_text(ph_title, LV_SYMBOL_BELL "  Notifiche");
    lv_obj_set_style_text_font(ph_title, UI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(ph_title, lv_color_hex(0x17E5E5), 0);
    lv_obj_align(ph_title, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* ph_clear = lv_btn_create(ph);
    lv_obj_set_size(ph_clear, 140, 30);
    lv_obj_set_style_bg_color(ph_clear, lv_color_hex(0x1E3860), 0);
    lv_obj_set_style_bg_color(ph_clear, lv_color_hex(0x2D4F80), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(ph_clear, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ph_clear, 6, 0);
    lv_obj_set_style_border_width(ph_clear, 0, 0);
    lv_obj_set_style_shadow_width(ph_clear, 0, 0);
    lv_obj_align(ph_clear, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(ph_clear, _clear_all_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* ph_clear_lbl = lv_label_create(ph_clear);
    lv_label_set_text(ph_clear_lbl, LV_SYMBOL_TRASH "  Cancella tutto");
    lv_obj_set_style_text_font(ph_clear_lbl, UI_FONT_TINY, 0);
    lv_obj_set_style_text_color(ph_clear_lbl, lv_color_hex(0x8FA8C8), 0);
    lv_obj_align(ph_clear_lbl, LV_ALIGN_CENTER, 0, 0);

    // ── 4. LISTA SCROLLABILE ─────────────────────────────────────────────────
    g_list = lv_obj_create(g_panel);
    lv_obj_set_pos(g_list, 0, NOTIF_PANEL_HDR_H);
    lv_obj_set_size(g_list, UI_SCREEN_W, NOTIF_PANEL_H - NOTIF_PANEL_HDR_H - NOTIF_HANDLE_H);
    lv_obj_set_style_bg_color(g_list, lv_color_hex(0x0A1628), 0);
    lv_obj_set_style_bg_opa(g_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_list, 0, 0);
    lv_obj_set_style_pad_all(g_list, 0, 0);
    lv_obj_set_style_pad_row(g_list, NOTIF_ITEM_GAP, 0);
    lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(g_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);

    // Popola le notifiche
    for (int i = 0; i < k_notif_count; i++) {
        _make_notif_item(g_list, k_notifs[i], &g_sw[i]);
    }

    // ── 5. HANDLE DI CHIUSURA IN FONDO ──────────────────────────────────────
    lv_obj_t* handle_zone = lv_obj_create(g_panel);
    lv_obj_set_size(handle_zone, UI_SCREEN_W, NOTIF_HANDLE_H);
    lv_obj_set_pos(handle_zone, 0, NOTIF_PANEL_H - NOTIF_HANDLE_H);
    lv_obj_set_style_bg_color(handle_zone, lv_color_hex(0x0D1C32), 0);
    lv_obj_set_style_bg_opa(handle_zone, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(handle_zone, 0, 0);
    lv_obj_set_style_radius(handle_zone, 0, 0);
    lv_obj_clear_flag(handle_zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(handle_zone, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(handle_zone, _handle_pressed_cb,  LV_EVENT_PRESSED,  nullptr);
    lv_obj_add_event_cb(handle_zone, _header_pressing_cb, LV_EVENT_PRESSING, nullptr); // riusa stesso cb
    lv_obj_add_event_cb(handle_zone, _handle_released_cb, LV_EVENT_RELEASED, nullptr);

    // Barretta grafica centrata nell'handle
    lv_obj_t* grip = lv_obj_create(handle_zone);
    lv_obj_set_size(grip, 48, 4);
    lv_obj_set_style_bg_color(grip, lv_color_hex(0x4A6380), 0);
    lv_obj_set_style_bg_opa(grip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(grip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(grip, 0, 0);
    lv_obj_clear_flag(grip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(grip, LV_ALIGN_CENTER, 0, 0);

    // ── 6. GESTO PULL-DOWN SULL'HEADER PRINCIPALE ────────────────────────────
    lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(header, _header_pressed_cb,  LV_EVENT_PRESSED,  nullptr);
    lv_obj_add_event_cb(header, _header_pressing_cb, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(header, _header_released_cb, LV_EVENT_RELEASED, nullptr);
}
