#ifndef SERIAL_MANAGER_H
#define SERIAL_MANAGER_H

#include <Arduino.h>

// Gestione comandi seriali per schede Controller (Display, Standalone/Rewamping).
void Serial_Controller_Menu();

// Gestione comandi seriali per schede Peripheral (Pressione, Relay, Motore).
void Serial_Peripheral_Menu();

// Alias legacy mantenuti per compatibilita' con codice esistente.
void Serial_Master_Menu();
void Serial_Slave_Menu();

// API interne per controllo wizard DeltaP da pagina web locale.
bool webStartDeltaPTestWizard(int totalSpeeds, int dirtLevel, int speedIndex, String &message);
bool webStopDeltaPTestWizard(bool saveIfPossible, String &message);
String webGetDeltaPTestWizardStatusJson();
bool webIsDeltaPTestWizardBusy();

#endif
