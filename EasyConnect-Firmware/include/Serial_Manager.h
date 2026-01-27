#ifndef SERIAL_MANAGER_H
#define SERIAL_MANAGER_H

#include <Arduino.h>

// Funzione per gestire i comandi seriali specifici della scheda Master.
// Da chiamare nel loop() del Master.
void Serial_Master_Menu();

// Funzione per gestire i comandi seriali specifici della scheda Slave.
// Da chiamare nel loop() dello Slave.
void Serial_Slave_Menu();

#endif