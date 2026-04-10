/**
 * ITA: Questo file aggrega i font vendor in un'unica unita di compilazione.
 * ENG: This file aggregates vendor fonts into a single compilation unit.
 *
 * ITA: Ogni include .cpp qui sotto registra i glifi in modo che il linker
 *      li renda disponibili alla UI senza doverli referenziare singolarmente.
 * ENG: Each .cpp include below registers glyph tables so the linker keeps
 *      them available to the UI without per-font manual references.
 */
#include "DisplayVendorFonts.h"

// ITA: Font piccolo per testi secondari, label compatte, note.
// ENG: Small font for secondary text, compact labels, notes.
#include "vendor_fonts/font8.cpp"
// ITA: Font base per testi informativi.
// ENG: Base font for informational text.
#include "vendor_fonts/font12.cpp"
// ITA: Font medio per contenuti principali.
// ENG: Medium font for primary content.
#include "vendor_fonts/font16.cpp"
// ITA: Font medio-grande per titoli di sezione.
// ENG: Medium-large font for section titles.
#include "vendor_fonts/font20.cpp"
// ITA: Font grande per metriche in evidenza.
// ENG: Large font for highlighted metrics.
#include "vendor_fonts/font24.cpp"
// ITA: Font extra-large per numeri/schermate hero.
// ENG: Extra-large font for numbers/hero screens.
#include "vendor_fonts/font48.cpp"
