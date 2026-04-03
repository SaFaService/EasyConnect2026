#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <Arduino.h>
#include <WebServer.h>

// Riferimento al server web globale
extern WebServer server;

// Funzioni di gestione
void setupCalibration();
void calibrationLoop();
String checkThresholds(float currentDeltaP);
void updateDeltaPMonitoring(float rawDeltaP, bool isValidSample);
float getFilteredDeltaP();
bool isFilteredDeltaPValid();

#endif
