#ifndef RELAY_STORAGE_H
#define RELAY_STORAGE_H

#include <Preferences.h>
#include "RelayTypes.h"

class RelayStorage {
public:
    RelayStorage();
    bool begin();
    void end();
    void load(RelayConfig &cfg, RelayCounters &counters);
    void saveConfig(const RelayConfig &cfg);
    void saveCounters(const RelayCounters &counters);
    void factoryReset();

private:
    Preferences prefs_;
    bool started_;
};

#endif
