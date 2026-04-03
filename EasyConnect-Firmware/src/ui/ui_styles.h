#pragma once
/**
 * @file ui_styles.h
 * @brief Palette colori e costanti di stile EasyConnect / Antralux UI
 *
 * Tutti i valori qui definiti sono il riferimento unico per l'intera UI.
 * Modificare qui per aggiornare l'aspetto globale.
 */

#include "lvgl.h"

// ─────────────────────────────────────────────────────────────────────────────
// PALETTE COLORI ANTRALUX  (RGB565 → lv_color_hex)
// ─────────────────────────────────────────────────────────────────────────────

// Sfondi
#define UI_COLOR_BG_DEEP        lv_color_hex(0x080E1A)   // sfondo splash / schermo
#define UI_COLOR_BG_MAIN        lv_color_hex(0x0A1628)   // sfondo home
#define UI_COLOR_BG_CARD        lv_color_hex(0x122038)   // sfondo card/pannello
#define UI_COLOR_BG_CARD2       lv_color_hex(0x172840)   // sfondo card alternativo
#define UI_COLOR_HEADER         lv_color_hex(0x0D1C32)   // sfondo header

// Accento principale (teal/ciano Antralux)
#define UI_COLOR_ACCENT         lv_color_hex(0x17E5E5)   // ciano brillante
#define UI_COLOR_ACCENT_DIM     lv_color_hex(0x0A7878)   // ciano scuro (per bg archi)
#define UI_COLOR_ACCENT2        lv_color_hex(0x2D82FF)   // blu elettrico

// Stato
#define UI_COLOR_SUCCESS        lv_color_hex(0x2ECC71)   // verde OK
#define UI_COLOR_WARNING        lv_color_hex(0xF1C40F)   // giallo attenzione
#define UI_COLOR_ERROR          lv_color_hex(0xE74C3C)   // rosso errore
#define UI_COLOR_INFO           lv_color_hex(0x3498DB)   // blu info

// Testo
#define UI_COLOR_TEXT_PRIMARY   lv_color_hex(0xFFFFFF)   // bianco
#define UI_COLOR_TEXT_SECONDARY lv_color_hex(0x8FA8C8)   // grigio-blu chiaro
#define UI_COLOR_TEXT_DIM       lv_color_hex(0x4A6380)   // grigio scuro

// Bordi
#define UI_COLOR_BORDER         lv_color_hex(0x1E3860)   // bordo card
#define UI_COLOR_BORDER_ACTIVE  lv_color_hex(0x17E5E5)   // bordo selezionato

// Colori touch (un colore per ogni dito)
#define UI_COLOR_TOUCH_0        lv_color_hex(0x17E5E5)   // dito 1 - ciano
#define UI_COLOR_TOUCH_1        lv_color_hex(0xFF6B35)   // dito 2 - arancione
#define UI_COLOR_TOUCH_2        lv_color_hex(0x2ECC71)   // dito 3 - verde
#define UI_COLOR_TOUCH_3        lv_color_hex(0xF1C40F)   // dito 4 - giallo
#define UI_COLOR_TOUCH_4        lv_color_hex(0xE056DB)   // dito 5 - viola

// ─────────────────────────────────────────────────────────────────────────────
// FONT (richiede che i corrispondenti LV_FONT_MONTSERRAT_xx siano = 1)
// ─────────────────────────────────────────────────────────────────────────────

#define UI_FONT_LARGE      (&lv_font_montserrat_48)   // logo / hero text
#define UI_FONT_TITLE      (&lv_font_montserrat_32)   // titoli sezione
#define UI_FONT_SUBTITLE   (&lv_font_montserrat_24)   // sottotitoli
#define UI_FONT_BODY       (&lv_font_montserrat_20)   // testo corpo
#define UI_FONT_LABEL      (&lv_font_montserrat_16)   // etichette
#define UI_FONT_SMALL      (&lv_font_montserrat_14)   // testo piccolo
#define UI_FONT_TINY       (&lv_font_montserrat_12)   // testo minuscolo

// ─────────────────────────────────────────────────────────────────────────────
// DIMENSIONI E SPAZIATURE
// ─────────────────────────────────────────────────────────────────────────────

#define UI_SCREEN_W         1024
#define UI_SCREEN_H         600
#define UI_HEADER_H         52
#define UI_TAB_BAR_H        50
#define UI_CONTENT_H        (UI_SCREEN_H - UI_HEADER_H - UI_TAB_BAR_H)  // ~498

#define UI_PADDING          16
#define UI_PADDING_SM       8
#define UI_RADIUS_CARD      14
#define UI_RADIUS_BTN       10
#define UI_RADIUS_CIRCLE    LV_RADIUS_CIRCLE

#define UI_BORDER_W         1

// ─────────────────────────────────────────────────────────────────────────────
// VERSIONE FIRMWARE SANDBOX
// ─────────────────────────────────────────────────────────────────────────────

#define UI_SANDBOX_VERSION  "1.0.0"
#define UI_SANDBOX_BUILD    __DATE__ " " __TIME__
