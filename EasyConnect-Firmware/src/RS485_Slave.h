#ifndef RS485_SLAVE_H
#define RS485_SLAVE_H

#include <Arduino.h>

void RS485_Slave_Loop();
void processOTACommand(String cmd);

#endif