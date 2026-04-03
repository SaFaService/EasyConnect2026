#ifndef RELAY_RS485_H
#define RELAY_RS485_H

#include <Arduino.h>
#include "RelayController.h"
#include "RelayStorage.h"
#include <MD5Builder.h>
#include <FS.h>

class RelayRs485Interface {
public:
    RelayRs485Interface();
    void begin(const char *fwVersion,
               const RelayPins &pins,
               RelayConfig *cfg,
               RelayCounters *counters,
               RelayController *controller,
               RelayStorage *storage,
               bool *debug485);
    void update();

    unsigned long lastAnyActivityMs() const;
    unsigned long lastDirectedActivityMs() const;
    bool hasUvcRemoteActivation() const;
    void clearUvcRemoteActivation();

private:
    const char *fwVersion_;
    RelayPins pins_;
    RelayConfig *cfg_;
    RelayCounters *counters_;
    RelayController *controller_;
    RelayStorage *storage_;
    bool *debug485_;
    unsigned long lastAnyActivityMs_;
    unsigned long lastDirectedActivityMs_;
    bool uvcRemoteActivation_;

    void setRxMode();
    void setTxMode();
    void sendFrame(const String &payload);
    String buildStatusPayload() const;
    void handleFrame(const String &frame);

    bool handleClassicCfgCommand(const String &frame, const String &upper);
    bool handleExtendedCfgCommand(const String &frame, const String &upper);
    bool handleOtaCommand(const String &frame, const String &upper);
    void processOtaCommand(const String &cmd);
    void hexToBytes(const String &hex, uint8_t *bytes, int len);
    uint8_t calculateChecksum(const String &data);

    // Stato OTA/TEST (allineato al protocollo usato dalle periferiche pressione).
    bool otaInProgress_;
    size_t otaTotalSize_;
    String otaExpectedMD5_;
    size_t otaWrittenSize_;
    size_t otaExpectedOffset_;
    MD5Builder otaRunningMd5_;
    bool otaRunningMd5Active_;
    File testFile_;
};

#endif
