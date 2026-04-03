#ifndef RELAY_SERIAL_H
#define RELAY_SERIAL_H

#include <Arduino.h>
#include "RelayController.h"
#include "RelayStorage.h"

class RelaySerialInterface {
public:
    RelaySerialInterface();
    void begin(const char *fwVersion,
               RelayConfig *cfg,
               RelayCounters *counters,
               RelayController *controller,
               RelayStorage *storage,
               bool *debug485);
    void update();

private:
    const char *fwVersion_;
    RelayConfig *cfg_;
    RelayCounters *counters_;
    RelayController *controller_;
    RelayStorage *storage_;
    bool *debug485_;
    String inputBuffer_;

    void printHelp() const;
    void printAdvancedHelp() const;
    void printInfo() const;
    void processCommand(const String &line);
    void saveConfig();
    void saveCounters();
    void serviceTestOreSimulation();

    bool testOreEnabled_;
    bool testOreTriggered_;
    unsigned long testOreStartMs_;
};

#endif
