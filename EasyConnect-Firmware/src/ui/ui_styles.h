#pragma once
/**
 * @file ui_styles.h
 * @brief Palette colori e costanti di stile EasyConnect / Antralux UI
 *        Color palette and style constants for EasyConnect / Antralux UI
 *
 * Tutti i valori qui definiti sono il riferimento unico per l'intera UI Sandbox.
 * All values defined here are the single source of truth for the entire Sandbox UI.
 *
 * NOTA / NOTE:
 *   Questi stili appartengono alla UI "sandbox" (ui_splash, ui_home, ui_notifications).
 *   These styles belong to the "sandbox" UI layer (ui_splash, ui_home, ui_notifications).
 *   Il target di produzione (easyconnect) usa la propria palette inline in ogni file ui_dc_*.
 *   The production target (easyconnect) uses its own inline palette in each ui_dc_* file.
 *
 *   Modificare qui per aggiornare l'aspetto globale della sandbox.
 *   Modify here to update the overall look of the sandbox globally.
 */

#include "lvgl.h"

// ─────────────────────────────────────────────────────────────────────────────
// PALETTE COLORI ANTRALUX  (RGB hex → lv_color_hex)
// ANTRALUX COLOR PALETTE   (RGB hex → lv_color_hex)
// ─────────────────────────────────────────────────────────────────────────────

// ── Sfondi / Backgrounds ─────────────────────────────────────────────────────

/** Sfondo molto scuro usato per la splash screen e lo schermo base.
 *  Very dark background used for the splash screen and base screen. */
#define UI_COLOR_BG_DEEP        lv_color_hex(0x080E1A)

/** Sfondo principale della home screen.
 *  Main background of the home screen. */
#define UI_COLOR_BG_MAIN        lv_color_hex(0x0A1628)

/** Sfondo delle card / pannelli contenuto.
 *  Background for cards / content panels. */
#define UI_COLOR_BG_CARD        lv_color_hex(0x122038)

/** Sfondo alternativo per card secondarie o righe alternate.
 *  Alternative background for secondary cards or alternating rows. */
#define UI_COLOR_BG_CARD2       lv_color_hex(0x172840)

/** Sfondo dell'header (barra superiore).
 *  Background of the header (top bar). */
#define UI_COLOR_HEADER         lv_color_hex(0x0D1C32)

// ── Accento principale / Main accent ─────────────────────────────────────────

/** Ciano brillante Antralux — colore primario, archi, bordi attivi, highlight.
 *  Bright Antralux cyan — primary color, arcs, active borders, highlights. */
#define UI_COLOR_ACCENT         lv_color_hex(0x17E5E5)

/** Ciano scuro — usato come sfondo per gli archi e gli elementi di sfondo.
 *  Dark cyan — used as background for arcs and background elements. */
#define UI_COLOR_ACCENT_DIM     lv_color_hex(0x0A7878)

/** Blu elettrico — secondo colore di accento (es. arco interno splash).
 *  Electric blue — secondary accent color (e.g. inner arc in splash). */
#define UI_COLOR_ACCENT2        lv_color_hex(0x2D82FF)

// ── Stato / Status colors ─────────────────────────────────────────────────────

/** Verde: operazione OK, relay on, valori nella norma.
 *  Green: operation OK, relay on, values in range. */
#define UI_COLOR_SUCCESS        lv_color_hex(0x2ECC71)

/** Giallo: attenzione, valori nella zona di warning.
 *  Yellow: attention, values in the warning zone. */
#define UI_COLOR_WARNING        lv_color_hex(0xF1C40F)

/** Rosso: errore, guasto, valori critici.
 *  Red: error, fault, critical values. */
#define UI_COLOR_ERROR          lv_color_hex(0xE74C3C)

/** Blu informativo — usato per messaggi info generici.
 *  Informational blue — used for generic info messages. */
#define UI_COLOR_INFO           lv_color_hex(0x3498DB)

// ── Testo / Text colors ───────────────────────────────────────────────────────

/** Bianco puro — testo principale, massima leggibilità su sfondi scuri.
 *  Pure white — primary text, maximum readability on dark backgrounds. */
#define UI_COLOR_TEXT_PRIMARY   lv_color_hex(0xFFFFFF)

/** Grigio-blu chiaro — testo secondario, sottotitoli, descrizioni.
 *  Light grey-blue — secondary text, subtitles, descriptions. */
#define UI_COLOR_TEXT_SECONDARY lv_color_hex(0x8FA8C8)

/** Grigio scuro — testo attenuato, hint, timestamp, valori non prioritari.
 *  Dark grey — dimmed text, hints, timestamps, non-priority values. */
#define UI_COLOR_TEXT_DIM       lv_color_hex(0x4A6380)

// ── Bordi / Borders ───────────────────────────────────────────────────────────

/** Bordo standard delle card — linea sottile blu scuro.
 *  Standard card border — thin dark blue line. */
#define UI_COLOR_BORDER         lv_color_hex(0x1E3860)

/** Bordo attivo/selezionato — usa il ciano principale.
 *  Active/selected border — uses the main cyan accent. */
#define UI_COLOR_BORDER_ACTIVE  lv_color_hex(0x17E5E5)

// ── Colori touch / Touch point colors ────────────────────────────────────────
// Usati nel tab "Touch" di ui_home.cpp per colorare ogni dito.
// Used in the "Touch" tab of ui_home.cpp to color each finger.

/** Dito 1 — ciano / Finger 1 — cyan */
#define UI_COLOR_TOUCH_0        lv_color_hex(0x17E5E5)

/** Dito 2 — arancione / Finger 2 — orange */
#define UI_COLOR_TOUCH_1        lv_color_hex(0xFF6B35)

/** Dito 3 — verde / Finger 3 — green */
#define UI_COLOR_TOUCH_2        lv_color_hex(0x2ECC71)

/** Dito 4 — giallo / Finger 4 — yellow */
#define UI_COLOR_TOUCH_3        lv_color_hex(0xF1C40F)

/** Dito 5 — viola / Finger 5 — purple */
#define UI_COLOR_TOUCH_4        lv_color_hex(0xE056DB)

// ─────────────────────────────────────────────────────────────────────────────
// FONT
// (richiede che i corrispondenti LV_FONT_MONTSERRAT_xx siano abilitati = 1
//  nelle build flags in platformio.ini)
// (requires the corresponding LV_FONT_MONTSERRAT_xx to be enabled = 1
//  in the build flags in platformio.ini)
// ─────────────────────────────────────────────────────────────────────────────

/** Font 48pt — testo hero, logo, elementi centrali grandi.
 *  48pt font — hero text, logo, large central elements. */
#define UI_FONT_LARGE      (&lv_font_montserrat_48)

/** Font 32pt — titoli di sezione.
 *  32pt font — section titles. */
#define UI_FONT_TITLE      (&lv_font_montserrat_32)

/** Font 24pt — sottotitoli, testo enfatizzato.
 *  24pt font — subtitles, emphasized text. */
#define UI_FONT_SUBTITLE   (&lv_font_montserrat_24)

/** Font 20pt — testo corpo principale.
 *  20pt font — main body text. */
#define UI_FONT_BODY       (&lv_font_montserrat_20)

/** Font 16pt — etichette, pulsanti, voci di menu.
 *  16pt font — labels, buttons, menu items. */
#define UI_FONT_LABEL      (&lv_font_montserrat_16)

/** Font 14pt — testo piccolo, note, descrizioni secondarie.
 *  14pt font — small text, notes, secondary descriptions. */
#define UI_FONT_SMALL      (&lv_font_montserrat_14)

/** Font 12pt — testo minuscolo, timestamp, hint discreti.
 *  12pt font — tiny text, timestamps, subtle hints. */
#define UI_FONT_TINY       (&lv_font_montserrat_12)

// ─────────────────────────────────────────────────────────────────────────────
// DIMENSIONI E SPAZIATURE / DIMENSIONS AND SPACING
// ─────────────────────────────────────────────────────────────────────────────

/** Larghezza display in pixel / Display width in pixels */
#define UI_SCREEN_W         1024

/** Altezza display in pixel / Display height in pixels */
#define UI_SCREEN_H         600

/** Altezza header (barra superiore con titolo e icone).
 *  Header height (top bar with title and icons). */
#define UI_HEADER_H         52

/** Altezza barra tab (LVGL tabview).
 *  Tab bar height (LVGL tabview). */
#define UI_TAB_BAR_H        50

/** Altezza area contenuto = schermo - header - tab bar ≈ 498px.
 *  Content area height = screen - header - tab bar ≈ 498px. */
#define UI_CONTENT_H        (UI_SCREEN_H - UI_HEADER_H - UI_TAB_BAR_H)

/** Padding standard tra elementi / Standard padding between elements */
#define UI_PADDING          16

/** Padding ridotto / Small padding */
#define UI_PADDING_SM       8

/** Raggio bordi arrotondati card / Card rounded corner radius */
#define UI_RADIUS_CARD      14

/** Raggio bordi arrotondati pulsanti / Button rounded corner radius */
#define UI_RADIUS_BTN       10

/** Raggio cerchio perfetto (costante LVGL) / Perfect circle radius (LVGL constant) */
#define UI_RADIUS_CIRCLE    LV_RADIUS_CIRCLE

/** Spessore bordo standard / Standard border width */
#define UI_BORDER_W         1

// ─────────────────────────────────────────────────────────────────────────────
// VERSIONE FIRMWARE SANDBOX
// SANDBOX FIRMWARE VERSION
// ─────────────────────────────────────────────────────────────────────────────

/** Stringa versione mostrata nella splash e nell'info tab.
 *  Version string shown in splash and info tab. */
#define UI_SANDBOX_VERSION  "1.0.0"

/** Stringa build (data e ora di compilazione, espansa dal preprocessore).
 *  Build string (compilation date and time, expanded by preprocessor). */
#define UI_SANDBOX_BUILD    __DATE__ " " __TIME__
