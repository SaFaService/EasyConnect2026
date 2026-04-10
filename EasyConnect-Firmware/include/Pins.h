#ifndef PINS_H
#define PINS_H

#include <Arduino.h>

// Valore segnaposto per pin non assegnati / non presenti sulla scheda.
#define PIN_NOT_ASSIGNED 255

// ═══════════════════════════════════════════════════════════════════════════════
// SISTEMA A PROFILI SCHEDA
//
// Ogni target in platformio.ini dichiara il proprio profilo tramite un define
// di build:   -D BOARD_PROFILE_DISPLAY        (controller display S3)
//             -D BOARD_PROFILE_CONTROLLER     (controller standalone/rewamping C3)
//             -D BOARD_PROFILE_PRESSURE       (periferica pressione C3)
//             -D BOARD_PROFILE_RELAY          (periferica relay)
//             -D BOARD_PROFILE_0V10V          (periferica inverter 0-10V)
//             -D BOARD_PROFILE_DIAGNOSTIC     (firmware diagnostico)
//
// Se nessun profilo e' definito, la compilazione fallisce con un messaggio chiaro.
// ═══════════════════════════════════════════════════════════════════════════════


// ─── Profilo: Display Controller ─────────────────────────────────────────────
// MCU: ESP32-S3 (Waveshare ESP32-S3-Touch-LCD-7B)
// GPIO occupati dal display RGB: 0,1,2,3,5,7,8,9,10,14,17,18,21,38-42,45-48
// GPIO occupati da I2C: 8 (SDA), 9 (SCL)
// RS485: TX=IO16 (DI), RX=IO15 (RO), DIR automatico hardware (nessun GPIO)
// ─────────────────────────────────────────────────────────────────────────────
#if defined(BOARD_PROFILE_DISPLAY)

#define PIN_RS485_DIR               PIN_NOT_ASSIGNED  // automatico via Q1 su scheda (nessun GPIO)
#define PIN_RS485_TX                16   // IO16 -> RS485_DI (TX lato ESP)
#define PIN_RS485_RX                15   // IO15 <- RS485_RO (RX lato ESP)

// LED non presenti su questa scheda
#define PIN_LED_GREEN               PIN_NOT_ASSIGNED
#define PIN_LED_RED                 PIN_NOT_ASSIGNED
#define PIN_SAFETY                  PIN_NOT_ASSIGNED


// ─── Profilo: Controller Standalone / Rewamping ───────────────────────────────
// MCU: ESP32-C3
// ─────────────────────────────────────────────────────────────────────────────
#elif defined(BOARD_PROFILE_CONTROLLER)

// Bus RS485
#define PIN_RS485_DIR               7
#define PIN_RS485_TX                21
#define PIN_RS485_RX                20

// LED controller
#define PIN_LED_GREEN               9
#define PIN_LED_RED                 8
#define PIN_SAFETY                  2

// Alias storici controller (usati nel codice esistente)
#define PIN_CONTROLLER_LED_GREEN    PIN_LED_GREEN
#define PIN_CONTROLLER_LED_RED      PIN_LED_RED
#define PIN_CONTROLLER_SAFETY       PIN_SAFETY
#define PIN_LED_VERDE               PIN_LED_GREEN
#define PIN_LED_ROSSO               PIN_LED_RED
#define PIN_MASTER_SICUREZZA        PIN_SAFETY

// Tastiera / membrana
#define PIN_KEYBOARD_LED_WIFI       10
#define PIN_KEYBOARD_LED_SENS1      4
#define PIN_KEYBOARD_LED_SENS2      6
#define PIN_KEYBOARD_LED_AUX1       5
#define PIN_KEYBOARD_LED_SAFETY     3
#define PIN_KEYBOARD_LED_AUX2       1
#define PIN_KEYBOARD_BUTTON         0

// Alias legacy tastiera
#define MK_PIN_WIFI                 PIN_KEYBOARD_LED_WIFI
#define MK_PIN_SENS1                PIN_KEYBOARD_LED_SENS1
#define MK_PIN_SENS2                PIN_KEYBOARD_LED_SENS2
#define MK_PIN_AUX1                 PIN_KEYBOARD_LED_AUX1
#define MK_PIN_SAFETY               PIN_KEYBOARD_LED_SAFETY
#define MK_PIN_AUX2                 PIN_KEYBOARD_LED_AUX2
#define MK_PIN_BUTTON               PIN_KEYBOARD_BUTTON

// LED esterni (ordine legacy BAL4..BAL1 + extra)
#define PIN_CONTROLLER_LED_EXT_1    4
#define PIN_CONTROLLER_LED_EXT_2    5
#define PIN_CONTROLLER_LED_EXT_3    6
#define PIN_CONTROLLER_LED_EXT_4    1
#define PIN_CONTROLLER_LED_EXT_5    10
#define PIN_LED_EXT_1               PIN_CONTROLLER_LED_EXT_1
#define PIN_LED_EXT_2               PIN_CONTROLLER_LED_EXT_2
#define PIN_LED_EXT_3               PIN_CONTROLLER_LED_EXT_3
#define PIN_LED_EXT_4               PIN_CONTROLLER_LED_EXT_4
#define PIN_LED_EXT_5               PIN_CONTROLLER_LED_EXT_5


// ─── Profilo: Periferica Pressione ────────────────────────────────────────────
// MCU: ESP32-C3
// ─────────────────────────────────────────────────────────────────────────────
#elif defined(BOARD_PROFILE_PRESSURE)

// Bus RS485
#define PIN_RS485_DIR               7
#define PIN_RS485_TX                21
#define PIN_RS485_RX                20

// LED
#define PIN_LED_GREEN               9
#define PIN_LED_RED                 8
#define PIN_SAFETY                  10

// I2C sensore
#define PIN_I2C_SDA                 0
#define PIN_I2C_SCL                 1

// Alias legacy pressione
#define PIN_PRESSURE_LED_GREEN      PIN_LED_GREEN
#define PIN_PRESSURE_LED_RED        PIN_LED_RED
#define PIN_PRESSURE_SAFETY         PIN_SAFETY
#define PIN_PRESSURE_RS485_DIR      PIN_RS485_DIR
#define PIN_PRESSURE_RS485_TX       PIN_RS485_TX
#define PIN_PRESSURE_RS485_RX       PIN_RS485_RX
#define PIN_PRESSURE_I2C_SDA        PIN_I2C_SDA
#define PIN_PRESSURE_I2C_SCL        PIN_I2C_SCL
#define PIN_SICUREZZA               PIN_SAFETY


// ─── Profilo: Periferica Relay ────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
#elif defined(BOARD_PROFILE_RELAY)

// Bus RS485
#define PIN_RS485_DIR               7
#define PIN_RS485_TX                21
#define PIN_RS485_RX                20

// I/O relay
#define PIN_RELAY_OUTPUT            3
#define PIN_RELAY_FEEDBACK          6
#define PIN_RELAY_SAFETY            2
#define PIN_LED_RED                 8
#define PIN_LED_GREEN               9
#define PIN_SAFETY                  PIN_RELAY_SAFETY

// Alias legacy relay
#define PIN_RELAY_RS485_DIR         PIN_RS485_DIR
#define PIN_RELAY_RS485_TX          PIN_RS485_TX
#define PIN_RELAY_RS485_RX          PIN_RS485_RX
#define PIN_RELAY_LED_RED           PIN_LED_RED
#define PIN_RELAY_LED_GREEN         PIN_LED_GREEN


// ─── Profilo: Periferica Motore ───────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// ─── Profilo: Periferica Inverter 0-10V ──────────────────────────────────────
// MCU: ESP32-C3 (scheda Treedom "Periferica Sensori & 0-10V" Rev.1.0)
// ─────────────────────────────────────────────────────────────────────────────
#elif defined(BOARD_PROFILE_0V10V)

// Bus RS485
#define PIN_RS485_DIR               7
#define PIN_RS485_TX                21
#define PIN_RS485_RX                20

// LED (active-high: HIGH = acceso)
#define PIN_LED_GREEN               9   // D1 verde (450mcd)
#define PIN_LED_RED                 8   // D2 rosso (150mcd)
#define PIN_SAFETY                  PIN_NOT_ASSIGNED

// I/O specifici scheda
#define PIN_0V10V_PWM               2   // IO2 → op-amp (U2B/U2C) → 0-10V
#define PIN_0V10V_ENABLE            3   // IO3 → TLP182 (U6) → Q1 → InverterENABLE
#define PIN_0V10V_FEEDBACK         10   // IO10 ← TLP182 (U5) ← InverterFeedBack


// ─── Profilo: Firmware Diagnostico ───────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
#elif defined(BOARD_PROFILE_DIAGNOSTIC)

#define PIN_RS485_DIR               PIN_NOT_ASSIGNED
#define PIN_RS485_TX                PIN_NOT_ASSIGNED
#define PIN_RS485_RX                PIN_NOT_ASSIGNED
#define PIN_LED_GREEN               PIN_NOT_ASSIGNED
#define PIN_LED_RED                 PIN_NOT_ASSIGNED
#define PIN_SAFETY                  PIN_NOT_ASSIGNED
#define PIN_DIAG_BOOT_1             5
#define PIN_DIAG_BOOT_2             8
#define PIN_DIAG_BOOT_3             9


// ─── Nessun profilo definito → errore chiaro in compilazione ─────────────────
#else
#error "Profilo scheda non definito. Aggiungere -D BOARD_PROFILE_XXX ai build_flags in platformio.ini. " \
       "Valori validi: BOARD_PROFILE_DISPLAY | BOARD_PROFILE_CONTROLLER | BOARD_PROFILE_PRESSURE | " \
       "BOARD_PROFILE_RELAY | BOARD_PROFILE_0V10V | BOARD_PROFILE_DIAGNOSTIC"
#endif

#ifndef PIN_LED_VERDE
#define PIN_LED_VERDE               PIN_LED_GREEN
#endif

#ifndef PIN_LED_ROSSO
#define PIN_LED_ROSSO               PIN_LED_RED
#endif

#ifndef PIN_SICUREZZA
#define PIN_SICUREZZA               PIN_SAFETY
#endif

#ifndef PIN_LED_EXT_1
#define PIN_LED_EXT_1               PIN_NOT_ASSIGNED
#endif

#ifndef PIN_LED_EXT_2
#define PIN_LED_EXT_2               PIN_NOT_ASSIGNED
#endif

#ifndef PIN_LED_EXT_3
#define PIN_LED_EXT_3               PIN_NOT_ASSIGNED
#endif

#ifndef PIN_LED_EXT_4
#define PIN_LED_EXT_4               PIN_NOT_ASSIGNED
#endif

#ifndef PIN_LED_EXT_5
#define PIN_LED_EXT_5               PIN_NOT_ASSIGNED
#endif

#endif // PINS_H
