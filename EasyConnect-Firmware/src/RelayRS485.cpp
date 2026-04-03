#include "RelayRS485.h"
#include <string.h>
#include <Update.h>
#include <SPIFFS.h>
#include <ESP.h>

namespace {
static bool parseOnOffValue(const String &raw, bool &outValue) {
    String t = raw;
    t.trim();
    t.toUpperCase();
    if (t == "1" || t == "ON" || t == "TRUE" || t == "SI" || t == "YES") {
        outValue = true;
        return true;
    }
    if (t == "0" || t == "OFF" || t == "FALSE" || t == "NO") {
        outValue = false;
        return true;
    }
    return false;
}

static void splitFirstToken(const String &raw, String &firstToken, String &remainder) {
    String text = raw;
    text.trim();
    const int sep = text.indexOf(' ');
    if (sep < 0) {
        firstToken = text;
        remainder = "";
        return;
    }
    firstToken = text.substring(0, sep);
    remainder = text.substring(sep + 1);
    remainder.trim();
}
}

RelayRs485Interface::RelayRs485Interface()
    : fwVersion_(nullptr),
      cfg_(nullptr),
      counters_(nullptr),
      controller_(nullptr),
      storage_(nullptr),
      debug485_(nullptr),
      lastAnyActivityMs_(0),
      lastDirectedActivityMs_(0),
      uvcRemoteActivation_(false),
      otaInProgress_(false),
      otaTotalSize_(0),
      otaExpectedMD5_(""),
      otaWrittenSize_(0),
      otaExpectedOffset_(0),
      otaRunningMd5Active_(false) {
    memset(&pins_, 0, sizeof(pins_));
}

void RelayRs485Interface::begin(const char *fwVersion,
                                const RelayPins &pins,
                                RelayConfig *cfg,
                                RelayCounters *counters,
                                RelayController *controller,
                                RelayStorage *storage,
                                bool *debug485) {
    fwVersion_ = fwVersion;
    pins_ = pins;
    cfg_ = cfg;
    counters_ = counters;
    controller_ = controller;
    storage_ = storage;
    debug485_ = debug485;

    pinMode(pins_.rs485DirPin, OUTPUT);
    Serial1.setRxBufferSize(1024);
    Serial1.begin(115200, SERIAL_8N1, pins_.rs485RxPin, pins_.rs485TxPin);
    Serial1.setTimeout(35);
    setRxMode();
}

void RelayRs485Interface::setRxMode() {
    Serial1.flush();
    digitalWrite(pins_.rs485DirPin, LOW);
    delayMicroseconds(80);
}

void RelayRs485Interface::setTxMode() {
    digitalWrite(pins_.rs485DirPin, HIGH);
    delayMicroseconds(80);
}

void RelayRs485Interface::sendFrame(const String &payload) {
    setTxMode();
    Serial1.print(payload);
    Serial1.print("!");
    Serial1.flush();
    setRxMode();

    if (debug485_ && *debug485_) {
        Serial.printf("[RS485-TX] %s!\n", payload.c_str());
    }
}

String RelayRs485Interface::buildStatusPayload() const {
    if (!cfg_ || !counters_ || !controller_) return "ERR,RELAY,NOCTX";

    const float hours = static_cast<float>(counters_->onSeconds) / 3600.0f;
    const bool lifeExpired = (cfg_->lifeLimitHours > 0) && (hours >= static_cast<float>(cfg_->lifeLimitHours));
    String payload = "OK,RELAY,";
    payload += static_cast<int>(cfg_->mode);
    payload += ",";
    payload += controller_->isRelayOn() ? "1" : "0";
    payload += ",";
    payload += controller_->isSafetyClosed() ? "1" : "0";
    payload += ",";
    payload += controller_->isFeedbackMatched() ? "1" : "0";
    payload += ",";
    payload += String(counters_->relayStarts);
    payload += ",";
    payload += String(hours, 2);
    payload += ",";
    payload += String(cfg_->group);
    payload += ",";
    payload += String(cfg_->serialId);
    payload += ",";
    payload += relayControlStateToString(controller_->state());
    payload += ",";
    payload += fwVersion_ ? fwVersion_ : "N/A";
    payload += ",";
    payload += String(cfg_->lifeLimitHours);
    payload += ",";
    payload += lifeExpired ? "1" : "0";
    payload += ",";
    payload += controller_->isFeedbackFaultLatched() ? "1" : "0";
    return payload;
}

bool RelayRs485Interface::handleClassicCfgCommand(const String &frame, const String &upper) {
    if (!cfg_ || !storage_ || !controller_) return false;

    if (upper.startsWith("MOD")) {
        const int sep = frame.indexOf(':');
        if (sep <= 3) return true;
        const int target = frame.substring(3, sep).toInt();
        if (target != cfg_->rs485Address) return true;
        lastDirectedActivityMs_ = millis();
        RelayMode m;
        if (!relayParseMode(frame.substring(sep + 1), m)) {
            sendFrame("ERR,CFG,MODE");
            return true;
        }
        cfg_->mode = m;
        cfg_->configured = relayHasMinimumConfig(*cfg_);
        storage_->saveConfig(*cfg_);
        controller_->onConfigChanged();
        sendFrame("OK,CFG,MODE," + String(target) + "," + String(static_cast<int>(cfg_->mode)));
        return true;
    }

    if (upper.startsWith("GRP")) {
        const int sep = frame.indexOf(':');
        if (sep <= 3) return true;
        const int target = frame.substring(3, sep).toInt();
        if (target != cfg_->rs485Address) return true;
        lastDirectedActivityMs_ = millis();
        const int group = frame.substring(sep + 1).toInt();
        if (group < 0 || group > 255) {
            sendFrame("ERR,CFG,GRP");
            return true;
        }
        cfg_->group = static_cast<uint8_t>(group);
        cfg_->configured = relayHasMinimumConfig(*cfg_);
        storage_->saveConfig(*cfg_);
        sendFrame("OK,CFG,GRP," + String(target) + "," + String(cfg_->group));
        return true;
    }

    if (upper.startsWith("IP")) {
        const int sep = frame.indexOf(':');
        if (sep <= 2) return true;
        const int target = frame.substring(2, sep).toInt();
        if (target != cfg_->rs485Address) return true;
        lastDirectedActivityMs_ = millis();
        const int newIp = frame.substring(sep + 1).toInt();
        if (newIp < 1 || newIp > 30) {
            sendFrame("ERR,CFG,IP");
            return true;
        }
        const uint8_t oldIp = cfg_->rs485Address;
        cfg_->rs485Address = static_cast<uint8_t>(newIp);
        cfg_->configured = relayHasMinimumConfig(*cfg_);
        storage_->saveConfig(*cfg_);
        sendFrame("OK,CFG,IP," + String(oldIp) + "," + String(cfg_->rs485Address));
        return true;
    }

    if (upper.startsWith("SER")) {
        const int sep = frame.indexOf(':');
        if (sep <= 3) return true;
        const int target = frame.substring(3, sep).toInt();
        if (target != cfg_->rs485Address) return true;
        lastDirectedActivityMs_ = millis();
        String serial = frame.substring(sep + 1);
        serial.trim();
        if (!relayIsValidSerialForRelay(serial)) {
            sendFrame("ERR,CFG,SERFMT");
            return true;
        }
        serial.toCharArray(cfg_->serialId, sizeof(cfg_->serialId));
        cfg_->configured = relayHasMinimumConfig(*cfg_);
        storage_->saveConfig(*cfg_);
        sendFrame("OK,CFG,SER," + String(target) + "," + String(cfg_->serialId));
        return true;
    }

    return false;
}

bool RelayRs485Interface::handleExtendedCfgCommand(const String &frame, const String &upper) {
    if (!cfg_ || !storage_ || !controller_ || !counters_) return false;

    if (upper.startsWith("BOOT")) {
        const int sep = frame.indexOf(':');
        if (sep <= 4) return true;
        const int target = frame.substring(4, sep).toInt();
        if (target != cfg_->rs485Address) return true;
        lastDirectedActivityMs_ = millis();
        bool value;
        if (!parseOnOffValue(frame.substring(sep + 1), value)) {
            sendFrame("ERR,CFG,BOOT");
            return true;
        }
        cfg_->bootRelayOn = value;
        storage_->saveConfig(*cfg_);
        sendFrame("OK,CFG,BOOT," + String(target) + "," + String(cfg_->bootRelayOn ? 1 : 0));
        return true;
    }

    if (upper.startsWith("LIF")) {
        const int sep = frame.indexOf(':');
        if (sep <= 3) return true;
        const int target = frame.substring(3, sep).toInt();
        if (target != cfg_->rs485Address) return true;
        lastDirectedActivityMs_ = millis();

        const long hours = frame.substring(sep + 1).toInt();
        if (hours < 0 || hours > 200000) {
            sendFrame("ERR,CFG,LIF");
            return true;
        }
        cfg_->lifeLimitHours = static_cast<uint32_t>(hours);
        storage_->saveConfig(*cfg_);
        sendFrame("OK,CFG,LIF," + String(target) + "," + String(cfg_->lifeLimitHours));
        return true;
    }

    if (upper.startsWith("FBC")) {
        const int sep = frame.indexOf(':');
        if (sep <= 3) return true;
        const int target = frame.substring(3, sep).toInt();
        if (target != cfg_->rs485Address) return true;
        lastDirectedActivityMs_ = millis();

        String params = frame.substring(sep + 1);
        params.replace(',', ' ');
        params.trim();

        bool enabledOnly = false;
        if (parseOnOffValue(params, enabledOnly)) {
            if (enabledOnly) {
                sendFrame("ERR,CFG,FBC");
            } else {
                cfg_->feedback.enabled = false;
                storage_->saveConfig(*cfg_);
                controller_->onConfigChanged();
                sendFrame("OK,CFG,FBC," + String(target) + "," +
                          String(cfg_->feedback.logic) + "," +
                          String(cfg_->feedback.checkDelaySec) + "," +
                          String(cfg_->feedback.attempts) + ",0");
            }
            return true;
        }

        String firstToken;
        String remainder;
        splitFirstToken(params, firstToken, remainder);

        bool enabledFromFirstToken = false;
        const bool hasLeadingEnable = parseOnOffValue(firstToken, enabledFromFirstToken);
        String numericParams = hasLeadingEnable ? remainder : params;
        int logic = -1;
        int delaySec = -1;
        int attempts = -1;
        int enable = -1;
        const int parsed = sscanf(numericParams.c_str(), "%d %d %d %d", &logic, &delaySec, &attempts, &enable);
        if (parsed < 3 || logic < 0 || logic > 1 || delaySec < 1 || delaySec > 3600 || attempts < 1 || attempts > 20) {
            sendFrame("ERR,CFG,FBC");
            return true;
        }

        cfg_->feedback.logic = static_cast<uint8_t>(logic);
        cfg_->feedback.checkDelaySec = static_cast<uint16_t>(delaySec);
        cfg_->feedback.attempts = static_cast<uint8_t>(attempts);
        if (hasLeadingEnable) {
            cfg_->feedback.enabled = enabledFromFirstToken;
        } else if (parsed >= 4) {
            cfg_->feedback.enabled = (enable != 0);
        }

        storage_->saveConfig(*cfg_);
        controller_->onConfigChanged();
        sendFrame("OK,CFG,FBC," + String(target) + "," +
                  String(cfg_->feedback.logic) + "," +
                  String(cfg_->feedback.checkDelaySec) + "," +
                  String(cfg_->feedback.attempts) + "," +
                  String(cfg_->feedback.enabled ? 1 : 0));
        return true;
    }

    if (upper.startsWith("MSG")) {
        // Formato: MSG<ID>:FB:testo oppure MSG<ID>:SAFE:testo
        const int sep1 = frame.indexOf(':');
        const int sep2 = frame.indexOf(':', sep1 + 1);
        if (sep1 <= 3 || sep2 < 0) return true;
        const int target = frame.substring(3, sep1).toInt();
        if (target != cfg_->rs485Address) return true;
        lastDirectedActivityMs_ = millis();

        String targetField = frame.substring(sep1 + 1, sep2);
        targetField.trim();
        targetField.toUpperCase();
        String value = frame.substring(sep2 + 1);
        value.trim();

        if (targetField == "FB") {
            value.toCharArray(cfg_->messages.feedbackFault, sizeof(cfg_->messages.feedbackFault));
            storage_->saveConfig(*cfg_);
            sendFrame("OK,CFG,MSG," + String(target) + ",FB");
            return true;
        }
        if (targetField == "SAFE" || targetField == "SAFETY") {
            value.toCharArray(cfg_->messages.safetyFault, sizeof(cfg_->messages.safetyFault));
            storage_->saveConfig(*cfg_);
            sendFrame("OK,CFG,MSG," + String(target) + ",SAFETY");
            return true;
        }

        sendFrame("ERR,CFG,MSG");
        return true;
    }

    if (upper.startsWith("CNT")) {
        const int sep = frame.indexOf(':');
        if (sep <= 3) return true;
        const int target = frame.substring(3, sep).toInt();
        if (target != cfg_->rs485Address) return true;
        lastDirectedActivityMs_ = millis();
        String action = frame.substring(sep + 1);
        action.trim();
        action.toUpperCase();
        if (action == "RESET") {
            counters_->relayStarts = 0;
            counters_->onSeconds = 0;
            storage_->saveCounters(*counters_);
            controller_->markCountersSaved();
            sendFrame("OK,CFG,CNT," + String(target) + ",RESET");
            return true;
        }
        sendFrame("ERR,CFG,CNT");
        return true;
    }

    return false;
}

uint8_t RelayRs485Interface::calculateChecksum(const String &data) {
    uint8_t crc = 0;
    for (int i = 0; i < data.length(); i++) {
        crc ^= data.charAt(i);
    }
    return crc;
}

void RelayRs485Interface::hexToBytes(const String &hex, uint8_t *bytes, int len) {
    for (int i = 0; i < len; i++) {
        char c1 = hex.charAt(i * 2);
        char c2 = hex.charAt(i * 2 + 1);

        uint8_t b1 = 0;
        if (c1 >= '0' && c1 <= '9') b1 = c1 - '0';
        else if (c1 >= 'A' && c1 <= 'F') b1 = c1 - 'A' + 10;
        else if (c1 >= 'a' && c1 <= 'f') b1 = c1 - 'a' + 10;

        uint8_t b2 = 0;
        if (c2 >= '0' && c2 <= '9') b2 = c2 - '0';
        else if (c2 >= 'A' && c2 <= 'F') b2 = c2 - 'A' + 10;
        else if (c2 >= 'a' && c2 <= 'f') b2 = c2 - 'a' + 10;

        bytes[i] = (b1 << 4) | b2;
    }
}

bool RelayRs485Interface::handleOtaCommand(const String &frame, const String &upper) {
    if (!(upper.startsWith("OTA,") || upper.startsWith("TEST,"))) {
        return false;
    }
    if (debug485_ && *debug485_) {
        Serial.println("[RS485-OTA] RX: " + frame);
    }
    processOtaCommand(frame);
    return true;
}

void RelayRs485Interface::processOtaCommand(const String &cmd) {
    if (!cfg_) return;
    const int myId = cfg_->rs485Address;

    if (cmd.startsWith("TEST,SPACE?")) {
        if (!SPIFFS.begin(true)) SPIFFS.begin(true);
        uint32_t total = SPIFFS.totalBytes();
        uint32_t used = SPIFFS.usedBytes();
        sendFrame("OK,TEST,SPACE," + String(myId) + "," + String(total) + "," + String(used));
        return;
    }

    if (cmd.startsWith("TEST,SPACE,")) {
        int c1 = cmd.indexOf(',');
        int c2 = cmd.indexOf(',', c1 + 1);
        int c3 = cmd.indexOf(',', c2 + 1);
        if (c3 == -1) return;

        int targetId = cmd.substring(c2 + 1, c3).toInt();
        if (targetId != myId) return;

        size_t requestedSize = (size_t)cmd.substring(c3 + 1).toInt();
        uint32_t maxSpace = (uint32_t)ESP.getFreeSketchSpace();
        bool enough = (requestedSize <= maxSpace);
        bool ok = false;
        String err = "";
        if (enough) {
            ok = Update.begin(requestedSize);
            err = ok ? "" : String(Update.errorString());
            if (ok) Update.abort();
        }

        if (ok) sendFrame("OK,TEST,SPACE," + String(myId) + ",OK," + String((uint32_t)requestedSize) + "," + String(maxSpace));
        else if (!enough) sendFrame("OK,TEST,SPACE," + String(myId) + ",NO," + String((uint32_t)requestedSize) + "," + String(maxSpace));
        else {
            err.replace(",", ";");
            sendFrame("OK,TEST,SPACE," + String(myId) + ",FAIL," + err);
        }
        return;
    }

    if (cmd.startsWith("TEST,ERASE,")) {
        int c1 = cmd.indexOf(',');
        int c2 = cmd.indexOf(',', c1 + 1);
        int c3 = cmd.indexOf(',', c2 + 1);
        if (c3 == -1) return;

        int targetId = cmd.substring(c2 + 1, c3).toInt();
        if (targetId != myId) return;

        size_t requestedSize = (size_t)cmd.substring(c3 + 1).toInt();
        uint32_t maxSpace = (uint32_t)ESP.getFreeSketchSpace();
        bool enough = (requestedSize <= maxSpace);
        bool ok = false;
        String err = "";
        if (enough) {
            ok = Update.begin(requestedSize);
            err = ok ? "" : String(Update.errorString());
            if (ok) Update.abort();
        }

        if (ok) sendFrame("OK,TEST,ERASE," + String(myId) + ",OK," + String(maxSpace));
        else if (!enough) sendFrame("OK,TEST,ERASE," + String(myId) + ",NO," + String(maxSpace));
        else {
            err.replace(",", ";");
            sendFrame("OK,TEST,ERASE," + String(myId) + ",FAIL," + err);
        }
        return;
    }

    if (cmd.startsWith("TEST,START,")) {
        int c1 = cmd.indexOf(',');
        int c2 = cmd.indexOf(',', c1 + 1);
        int c3 = cmd.indexOf(',', c2 + 1);
        int c4 = cmd.indexOf(',', c3 + 1);
        if (c3 == -1) return;
        if (c4 != -1) {
            int targetId = cmd.substring(c2 + 1, c3).toInt();
            if (targetId != myId) return;
        }

        if (!SPIFFS.begin(true)) SPIFFS.begin(true);
        if (SPIFFS.exists("/test_recv.bin")) SPIFFS.remove("/test_recv.bin");
        testFile_ = SPIFFS.open("/test_recv.bin", "w");
        if (testFile_) {
            sendFrame("OK,TEST,READY," + String(myId));
        }
        return;
    }

    if (cmd.startsWith("TEST,DATA,")) {
        if (!testFile_) return;

        int firstComma = cmd.indexOf(',');
        int secondComma = cmd.indexOf(',', firstComma + 1);
        int thirdComma = cmd.indexOf(',', secondComma + 1);
        int fourthComma = cmd.indexOf(',', thirdComma + 1);
        int fifthComma = cmd.indexOf(',', fourthComma + 1);
        if (thirdComma == -1 || fourthComma == -1) return;

        String offsetStr;
        String hexData;
        String checksumStr;
        if (fifthComma != -1) {
            int targetId = cmd.substring(secondComma + 1, thirdComma).toInt();
            if (targetId != myId) return;
            offsetStr = cmd.substring(thirdComma + 1, fourthComma);
            hexData = cmd.substring(fourthComma + 1, fifthComma);
            checksumStr = cmd.substring(fifthComma + 1);
        } else {
            offsetStr = cmd.substring(secondComma + 1, thirdComma);
            hexData = cmd.substring(thirdComma + 1, fourthComma);
            checksumStr = cmd.substring(fourthComma + 1);
        }

        uint8_t receivedCrc = (uint8_t)strtol(checksumStr.c_str(), NULL, 16);
        uint8_t calcCrc = calculateChecksum(hexData);
        if (receivedCrc == calcCrc) {
            int len = hexData.length() / 2;
            uint8_t buff[128];
            if (len > 0 && len <= (int)sizeof(buff)) {
                hexToBytes(hexData, buff, len);
                testFile_.write(buff, len);
                sendFrame("OK,TEST,ACK," + String(myId) + "," + offsetStr);
            }
        }
        return;
    }

    if (cmd.startsWith("TEST,END")) {
        int c1 = cmd.indexOf(',');
        int c2 = cmd.indexOf(',', c1 + 1);
        if (c2 != -1) {
            int targetId = cmd.substring(c2 + 1).toInt();
            if (targetId != myId) return;
        }
        if (testFile_) testFile_.close();
        sendFrame("OK,TEST,END," + String(myId));
        return;
    }

    if (cmd.startsWith("TEST,VERIFY,")) {
        int c1 = cmd.indexOf(',');
        int c2 = cmd.indexOf(',', c1 + 1);
        int c3 = cmd.indexOf(',', c2 + 1);
        if (c2 == -1) return;

        String expectedMD5;
        if (c3 != -1) {
            int targetId = cmd.substring(c2 + 1, c3).toInt();
            if (targetId != myId) return;
            expectedMD5 = cmd.substring(c3 + 1);
        } else {
            expectedMD5 = cmd.substring(c2 + 1);
        }
        expectedMD5.trim();
        expectedMD5.toUpperCase();

        File f = SPIFFS.open("/test_recv.bin", "r");
        if (!f) return;
        MD5Builder md5;
        md5.begin();
        md5.addStream(f, f.size());
        md5.calculate();
        String calcMD5 = md5.toString();
        calcMD5.toUpperCase();
        f.close();

        if (calcMD5 == expectedMD5) sendFrame("OK,TEST,PASS," + String(myId));
        else sendFrame("OK,TEST,FAIL," + String(myId) + "," + calcMD5);
        return;
    }

    if (cmd.startsWith("TEST,DELETE,")) {
        if (!SPIFFS.begin(true)) SPIFFS.begin(true);
        int c1 = cmd.indexOf(',');
        int c2 = cmd.indexOf(',', c1 + 1);
        if (c2 == -1) return;
        int targetId = cmd.substring(c2 + 1).toInt();
        if (targetId != myId) return;

        bool removed = false;
        bool hadFile = false;
        if (SPIFFS.exists("/test_recv.bin")) {
            hadFile = true;
            removed = SPIFFS.remove("/test_recv.bin");
        }
        if (removed) sendFrame("OK,TEST,DELETE," + String(myId) + ",OK");
        else if (!hadFile) sendFrame("OK,TEST,DELETE," + String(myId) + ",NOFILE");
        else sendFrame("OK,TEST,DELETE," + String(myId) + ",FAIL");
        return;
    }

    // OTA reale: START
    if (cmd.startsWith("OTA,START,")) {
        int firstComma = cmd.indexOf(',');
        int secondComma = cmd.indexOf(',', firstComma + 1);
        int thirdComma = cmd.indexOf(',', secondComma + 1);
        int fourthComma = cmd.indexOf(',', thirdComma + 1);
        if (thirdComma == -1) return;

        String sizeStr;
        if (fourthComma != -1) {
            int targetId = cmd.substring(secondComma + 1, thirdComma).toInt();
            if (targetId != myId) return;
            sizeStr = cmd.substring(thirdComma + 1, fourthComma);
            otaExpectedMD5_ = cmd.substring(fourthComma + 1);
        } else {
            sizeStr = cmd.substring(secondComma + 1, thirdComma);
            otaExpectedMD5_ = cmd.substring(thirdComma + 1);
        }
        otaExpectedMD5_.trim();
        otaExpectedMD5_.toUpperCase();
        otaTotalSize_ = (size_t)sizeStr.toInt();

        if (!Update.begin(otaTotalSize_)) {
            sendFrame("OK,OTA,FAIL," + String(myId));
            return;
        }

        otaInProgress_ = true;
        otaWrittenSize_ = 0;
        otaExpectedOffset_ = 0;
        otaRunningMd5_.begin();
        otaRunningMd5Active_ = true;

        delay(1000);
        Serial1.end();
        Serial1.begin(115200, SERIAL_8N1, pins_.rs485RxPin, pins_.rs485TxPin);
        setRxMode();

        sendFrame("OK,OTA,READY," + String(myId));
        return;
    }

    if (cmd.startsWith("OTA,DATA,") && otaInProgress_) {
        int firstComma = cmd.indexOf(',');
        int secondComma = cmd.indexOf(',', firstComma + 1);
        int thirdComma = cmd.indexOf(',', secondComma + 1);
        int fourthComma = cmd.indexOf(',', thirdComma + 1);
        int fifthComma = cmd.indexOf(',', fourthComma + 1);
        if (thirdComma == -1 || fourthComma == -1) return;

        String offsetStr;
        String hexData;
        String checksumStr;
        if (fifthComma != -1) {
            int targetId = cmd.substring(secondComma + 1, thirdComma).toInt();
            if (targetId != myId) return;
            offsetStr = cmd.substring(thirdComma + 1, fourthComma);
            hexData = cmd.substring(fourthComma + 1, fifthComma);
            checksumStr = cmd.substring(fifthComma + 1);
        } else {
            offsetStr = cmd.substring(secondComma + 1, thirdComma);
            hexData = cmd.substring(thirdComma + 1, fourthComma);
            checksumStr = cmd.substring(fourthComma + 1);
        }

        uint8_t receivedCrc = (uint8_t)strtol(checksumStr.c_str(), NULL, 16);
        uint8_t calcCrc = calculateChecksum(hexData);
        if (receivedCrc != calcCrc) return;
        if (hexData.length() == 0 || (hexData.length() % 2) != 0) return;

        int offset = offsetStr.toInt();
        int len = hexData.length() / 2;
        uint8_t buff[128];
        if (len <= 0 || len > (int)sizeof(buff)) return;

        if ((size_t)offset < otaWrittenSize_) {
            sendFrame("OK,OTA,ACK," + String(myId) + "," + String(offset));
            return;
        }
        if ((size_t)offset != otaWrittenSize_) return;

        hexToBytes(hexData, buff, len);
        if (Update.write(buff, len) == (size_t)len) {
            otaWrittenSize_ += (size_t)len;
            otaExpectedOffset_ = otaWrittenSize_;
            if (otaRunningMd5Active_) otaRunningMd5_.add(buff, len);
            sendFrame("OK,OTA,ACK," + String(myId) + "," + String(offset));
        }
        return;
    }

    if (cmd.startsWith("OTA,VERIFY,") && otaInProgress_) {
        int c1 = cmd.indexOf(',');
        int c2 = cmd.indexOf(',', c1 + 1);
        int c3 = cmd.indexOf(',', c2 + 1);
        if (c3 == -1) return;
        int targetId = cmd.substring(c2 + 1, c3).toInt();
        if (targetId != myId) return;

        String expectedMd5 = cmd.substring(c3 + 1);
        expectedMd5.trim();
        expectedMd5.toUpperCase();

        if (otaWrittenSize_ != otaTotalSize_) {
            sendFrame("OK,OTA,VERIFY," + String(myId) + ",FAIL,SIZE," + String((uint32_t)otaWrittenSize_) + "," + String((uint32_t)otaTotalSize_));
            return;
        }

        if (otaRunningMd5Active_) {
            otaRunningMd5_.calculate();
            otaRunningMd5Active_ = false;
        }
        String calcMd5 = otaRunningMd5_.toString();
        calcMd5.toUpperCase();

        if (calcMd5 == expectedMd5) sendFrame("OK,OTA,VERIFY," + String(myId) + ",PASS," + calcMd5);
        else sendFrame("OK,OTA,VERIFY," + String(myId) + ",FAIL," + calcMd5);
        return;
    }

    if (cmd.startsWith("OTA,END") && otaInProgress_) {
        int firstComma = cmd.indexOf(',');
        int secondComma = cmd.indexOf(',', firstComma + 1);
        if (secondComma != -1) {
            int targetId = cmd.substring(secondComma + 1).toInt();
            if (targetId != myId) return;
        }

        if (Update.end(true)) {
            String calculatedMD5 = Update.md5String();
            calculatedMD5.toUpperCase();
            if (calculatedMD5 == otaExpectedMD5_) {
                sendFrame("OK,OTA,SUCCESS," + String(myId));
                otaInProgress_ = false;
                otaRunningMd5Active_ = false;
                delay(1000);
                ESP.restart();
            } else {
                sendFrame("OK,OTA,FAIL," + String(myId));
                otaInProgress_ = false;
                otaRunningMd5Active_ = false;
            }
        } else {
            sendFrame("OK,OTA,FAIL," + String(myId));
            otaInProgress_ = false;
            otaRunningMd5Active_ = false;
        }
        return;
    }
}

void RelayRs485Interface::handleFrame(const String &frame) {
    if (!cfg_ || !controller_) return;

    String upper = frame;
    upper.toUpperCase();

    if (handleOtaCommand(frame, upper)) {
        return;
    }

    if (upper.startsWith("?")) {
        const int id = frame.substring(1).toInt();
        if (id == cfg_->rs485Address) {
            lastDirectedActivityMs_ = millis();
            sendFrame(buildStatusPayload());
        }
        return;
    }

    if (upper.startsWith("CMD,") || upper.startsWith("RLY,")) {
        const bool standaloneCmd = upper.startsWith("CMD,");
        const int c1 = frame.indexOf(',');
        const int c2 = frame.indexOf(',', c1 + 1);
        if (c2 < 0) return;

        const int target = frame.substring(c1 + 1, c2).toInt();
        if (target != cfg_->rs485Address) return;
        lastDirectedActivityMs_ = millis();

        String action = frame.substring(c2 + 1);
        action.trim();
        action.toUpperCase();

        // Compatibilita' Standalone/Rewamping:
        // - CMD,* e' usato dal master standalone e viene accettato solo in UVC.
        // - RLY,* e' comando relay generico e resta disponibile per gestione estesa.
        if (standaloneCmd && cfg_->mode != RelayMode::UVC) {
            sendFrame("ERR,RELAY,NOT_UVC," + String(target) + "," + String(static_cast<int>(cfg_->mode)));
            return;
        }

        if (action == "STATUS") {
            sendFrame(buildStatusPayload());
            return;
        }

        String result;
        bool ok = false;
        if (action == "ON") ok = controller_->commandRelay(true, "RS485", result);
        else if (action == "OFF") ok = controller_->commandRelay(false, "RS485", result);
        else if (action == "TOGGLE") ok = controller_->commandToggle("RS485", result);
        else {
            sendFrame("ERR,RELAY,CMD");
            return;
        }

        String payload = ok ? "OK,RELAY,CMD," : "ERR,RELAY,CMD,";
        payload += String(target);
        payload += ",";
        payload += action;
        payload += ",";
        payload += controller_->isRelayOn() ? "1" : "0";
        payload += ",";
        payload += relayControlStateToString(controller_->state());
        payload += ",";
        payload += result;
        sendFrame(payload);

        if (ok && controller_->isRelayOn()) {
            uvcRemoteActivation_ = true;
        } else if (action == "OFF" || !controller_->isRelayOn()) {
            uvcRemoteActivation_ = false;
        }
        return;
    }

    if (handleClassicCfgCommand(frame, upper)) return;
    if (handleExtendedCfgCommand(frame, upper)) return;
}

void RelayRs485Interface::update() {
    if (!cfg_) return;

    if (!Serial1.available()) return;

    String frame = Serial1.readStringUntil('!');
    frame.trim();
    if (frame.length() == 0) return;
    lastAnyActivityMs_ = millis();

    if (debug485_ && *debug485_) {
        Serial.printf("[RS485-RX] %s!\n", frame.c_str());
    }

    handleFrame(frame);
}

unsigned long RelayRs485Interface::lastAnyActivityMs() const {
    return lastAnyActivityMs_;
}

unsigned long RelayRs485Interface::lastDirectedActivityMs() const {
    return lastDirectedActivityMs_;
}

bool RelayRs485Interface::hasUvcRemoteActivation() const {
    return uvcRemoteActivation_;
}

void RelayRs485Interface::clearUvcRemoteActivation() {
    uvcRemoteActivation_ = false;
}
