#pragma once

/**
 * @file ui_dc_splash.h
 * @brief Splash screen Display Controller
 *
 * Sequenza (~5.5 s):
 *   t=200ms   Logo Antralux fade-in + zoom 40%→100% (1.8 s, ease-out)
 *   t=2000ms  Shimmer sweep sinistra→destra (1.2 s, ease-in-out)
 *   t=1600ms  Tagline fade-in (0.6 s, ease-out)
 *   t=400ms   Progress bar 0→100% (5 s, ease-in-out) → al termine carica Home
 */

void ui_dc_splash_create(void);
