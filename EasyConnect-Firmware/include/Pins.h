#ifndef PINS_H
#define PINS_H
#include <Arduino.h>

// LED di stato (Sulla scheda)
#define PIN_LED_VERDE 9   
#define PIN_LED_ROSSO 8   

// RS485
#define PIN_RS485_DIR 7   
#define PIN_RS485_TX 21   
#define PIN_RS485_RX 20   

// I2C (Sensori Slave)
#define PIN_I2C_SDA 0     
#define PIN_I2C_SCL 1     

// Sicurezza
#define PIN_SICUREZZA 10         // Slave (Input)
#define PIN_MASTER_SICUREZZA 2   // Master (Input)

// LED Esterni Master (Tastiera/Membrana)
#define PIN_LED_EXT_1 4
#define PIN_LED_EXT_2 5
#define PIN_LED_EXT_3 6
#define PIN_LED_EXT_4 1
#define PIN_LED_EXT_5 10

#endif