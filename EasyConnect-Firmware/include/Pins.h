#ifndef PINS_H
#define PINS_H

#include <Arduino.h>

// File unico di riferimento per il pinout del progetto.
// Le sezioni sono organizzate per scheda/ruolo; gli alias legacy
// restano disponibili per compatibilita' con il codice esistente.

// Valore segnaposto per pin non ancora definiti.
#define PIN_NOT_ASSIGNED 255

// ============================================================
// BUS COMUNI
// ============================================================
#define PIN_RS485_DIR               7
#define PIN_RS485_TX                21
#define PIN_RS485_RX                20

// ============================================================
// CONTROLLER / MASTER STANDALONE-REWAMPING
// ============================================================
#define PIN_CONTROLLER_LED_GREEN    9
#define PIN_CONTROLLER_LED_RED      8
#define PIN_CONTROLLER_SAFETY       2

// Tastiera / membrana collegata al controller
#define PIN_KEYBOARD_LED_WIFI       10
#define PIN_KEYBOARD_LED_SENS1      4
#define PIN_KEYBOARD_LED_SENS2      6
#define PIN_KEYBOARD_LED_AUX1       5
#define PIN_KEYBOARD_LED_SAFETY     3
#define PIN_KEYBOARD_LED_AUX2       1
#define PIN_KEYBOARD_BUTTON         0

// LED esterni master (ordine legacy BAL4..BAL1 + extra)
#define PIN_CONTROLLER_LED_EXT_1    4
#define PIN_CONTROLLER_LED_EXT_2    5
#define PIN_CONTROLLER_LED_EXT_3    6
#define PIN_CONTROLLER_LED_EXT_4    1
#define PIN_CONTROLLER_LED_EXT_5    10

// Alias legacy master/controller
#define PIN_LED_VERDE               PIN_CONTROLLER_LED_GREEN
#define PIN_LED_ROSSO               PIN_CONTROLLER_LED_RED
#define PIN_MASTER_SICUREZZA        PIN_CONTROLLER_SAFETY
#define PIN_LED_EXT_1               PIN_CONTROLLER_LED_EXT_1
#define PIN_LED_EXT_2               PIN_CONTROLLER_LED_EXT_2
#define PIN_LED_EXT_3               PIN_CONTROLLER_LED_EXT_3
#define PIN_LED_EXT_4               PIN_CONTROLLER_LED_EXT_4
#define PIN_LED_EXT_5               PIN_CONTROLLER_LED_EXT_5

// Alias legacy tastiera membrana
#define MK_PIN_WIFI                 PIN_KEYBOARD_LED_WIFI
#define MK_PIN_SENS1                PIN_KEYBOARD_LED_SENS1
#define MK_PIN_SENS2                PIN_KEYBOARD_LED_SENS2
#define MK_PIN_AUX1                 PIN_KEYBOARD_LED_AUX1
#define MK_PIN_SAFETY               PIN_KEYBOARD_LED_SAFETY
#define MK_PIN_AUX2                 PIN_KEYBOARD_LED_AUX2
#define MK_PIN_BUTTON               PIN_KEYBOARD_BUTTON

// ============================================================
// PERIFERICA PRESSIONE
// ============================================================
#define PIN_PRESSURE_LED_GREEN      9
#define PIN_PRESSURE_LED_RED        8
#define PIN_PRESSURE_RS485_DIR      PIN_RS485_DIR
#define PIN_PRESSURE_RS485_TX       PIN_RS485_TX
#define PIN_PRESSURE_RS485_RX       PIN_RS485_RX
#define PIN_PRESSURE_I2C_SDA        0
#define PIN_PRESSURE_I2C_SCL        1
#define PIN_PRESSURE_SAFETY         10

// Alias legacy pressione
#define PIN_I2C_SDA                 PIN_PRESSURE_I2C_SDA
#define PIN_I2C_SCL                 PIN_PRESSURE_I2C_SCL
#define PIN_SICUREZZA               PIN_PRESSURE_SAFETY

// ============================================================
// PERIFERICA RELAY
// ============================================================
#define PIN_RELAY_OUTPUT            3
#define PIN_RELAY_FEEDBACK          6
#define PIN_RELAY_SAFETY            2
#define PIN_RELAY_LED_RED           8
#define PIN_RELAY_LED_GREEN         9
#define PIN_RELAY_RS485_DIR         PIN_RS485_DIR
#define PIN_RELAY_RS485_TX          PIN_RS485_TX
#define PIN_RELAY_RS485_RX          PIN_RS485_RX

// ============================================================
// PERIFERICA MOTORE (placeholder da confermare)
// ============================================================
#define PIN_MOTOR_OUTPUT            PIN_NOT_ASSIGNED
#define PIN_MOTOR_FEEDBACK          PIN_NOT_ASSIGNED
#define PIN_MOTOR_SAFETY            PIN_NOT_ASSIGNED
#define PIN_MOTOR_LED_RED           PIN_NOT_ASSIGNED
#define PIN_MOTOR_LED_GREEN         PIN_NOT_ASSIGNED
#define PIN_MOTOR_RS485_DIR         PIN_RS485_DIR
#define PIN_MOTOR_RS485_TX          PIN_RS485_TX
#define PIN_MOTOR_RS485_RX          PIN_RS485_RX

// ============================================================
// CONTROLLER DISPLAY (placeholder da confermare)
// ============================================================
#define PIN_DISPLAY_LED_RED         PIN_NOT_ASSIGNED
#define PIN_DISPLAY_LED_GREEN       PIN_NOT_ASSIGNED
#define PIN_DISPLAY_RS485_DIR       PIN_RS485_DIR
#define PIN_DISPLAY_RS485_TX        PIN_RS485_TX
#define PIN_DISPLAY_RS485_RX        PIN_RS485_RX

// ============================================================
// FIRMWARE DIAGNOSTICO BOOT
// ============================================================
#define PIN_DIAG_BOOT_1             5
#define PIN_DIAG_BOOT_2             8
#define PIN_DIAG_BOOT_3             9

#endif
