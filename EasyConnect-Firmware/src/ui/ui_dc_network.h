#pragma once
#include "lvgl.h"

/**
 * @file ui_dc_network.h
 * @brief Schermata lista dispositivi RS485 rilevati.
 *
 * Layout (1024x600):
 *   +------------------------------------------------------------------+
 *   |  [←]   Dispositivi RS485                       [Scansiona]      | <- header 60px
 *   +------------------------------------------------------------------+
 *   |  IP  |  Seriale             |  Tipo    |  Dati                  |
 *   |   1  |  20240101ABCD1       |  Sensore |  25.3°C  60.2%RH      |
 *   |   3  |  20240101ABCD2       |  Relay   |                        |
 *   |  ...                                                             |
 *   +------------------------------------------------------------------+
 *
 * Tap su una riga → popup con dettaglio completo del dispositivo.
 * Pulsante "Scansiona" → avvia una nuova scansione RS485 1-200.
 */

lv_obj_t* ui_dc_network_create(void);
