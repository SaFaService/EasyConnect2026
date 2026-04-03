#include "RelayStorage.h"
#include <string.h>

RelayStorage::RelayStorage() : started_(false) {}

bool RelayStorage::begin() {
    if (started_) return true;
    started_ = prefs_.begin("relay", false);
    return started_;
}

void RelayStorage::end() {
    if (!started_) return;
    prefs_.end();
    started_ = false;
}

void RelayStorage::load(RelayConfig &cfg, RelayCounters &counters) {
    relaySetDefaultConfig(cfg);
    relaySetDefaultCounters(counters);

    if (!started_) return;

    cfg.configured = prefs_.getBool("set", false);
    cfg.rs485Address = prefs_.getUChar("addr", cfg.rs485Address);
    cfg.group = prefs_.getUChar("grp", cfg.group);

    const int modeValue = prefs_.getUChar("mode", static_cast<uint8_t>(cfg.mode));
    if (modeValue >= 1 && modeValue <= 5) {
        cfg.mode = static_cast<RelayMode>(modeValue);
    }

    String serial = prefs_.getString("sn", cfg.serialId);
    serial.toCharArray(cfg.serialId, sizeof(cfg.serialId));
    if (!relayIsValidSerialForRelay(cfg.serialId)) {
        strncpy(cfg.serialId, "NON_SET", sizeof(cfg.serialId));
        cfg.serialId[sizeof(cfg.serialId) - 1] = '\0';
    }

    cfg.bootRelayOn = prefs_.getBool("boot_on", cfg.bootRelayOn);
    cfg.lifeLimitHours = prefs_.getUInt("life_h", cfg.lifeLimitHours);
    cfg.feedback.enabled = prefs_.getBool("fb_en", cfg.feedback.enabled);
    cfg.feedback.logic = prefs_.getUChar("fb_log", cfg.feedback.logic);
    cfg.feedback.checkDelaySec = prefs_.getUShort("fb_sec", cfg.feedback.checkDelaySec);
    cfg.feedback.attempts = prefs_.getUChar("fb_try", cfg.feedback.attempts);
    if (cfg.feedback.attempts == 0) cfg.feedback.attempts = 1;

    String msgFb = prefs_.getString("msg_fb", cfg.messages.feedbackFault);
    msgFb.toCharArray(cfg.messages.feedbackFault, sizeof(cfg.messages.feedbackFault));

    String msgSafety = prefs_.getString("msg_saf", cfg.messages.safetyFault);
    msgSafety.toCharArray(cfg.messages.safetyFault, sizeof(cfg.messages.safetyFault));

    counters.relayStarts = prefs_.getUInt("cnt_on", counters.relayStarts);
    counters.onSeconds = prefs_.getULong64("cnt_sec", counters.onSeconds);

    cfg.configured = relayHasMinimumConfig(cfg);
}

void RelayStorage::saveConfig(const RelayConfig &cfg) {
    if (!started_) return;

    prefs_.putBool("set", cfg.configured);
    prefs_.putUChar("addr", cfg.rs485Address);
    prefs_.putUChar("grp", cfg.group);
    prefs_.putUChar("mode", static_cast<uint8_t>(cfg.mode));
    prefs_.putString("sn", cfg.serialId);
    prefs_.putBool("boot_on", cfg.bootRelayOn);
    prefs_.putUInt("life_h", cfg.lifeLimitHours);

    prefs_.putBool("fb_en", cfg.feedback.enabled);
    prefs_.putUChar("fb_log", cfg.feedback.logic);
    prefs_.putUShort("fb_sec", cfg.feedback.checkDelaySec);
    prefs_.putUChar("fb_try", cfg.feedback.attempts == 0 ? 1 : cfg.feedback.attempts);

    prefs_.putString("msg_fb", cfg.messages.feedbackFault);
    prefs_.putString("msg_saf", cfg.messages.safetyFault);
}

void RelayStorage::saveCounters(const RelayCounters &counters) {
    if (!started_) return;
    prefs_.putUInt("cnt_on", counters.relayStarts);
    prefs_.putULong64("cnt_sec", counters.onSeconds);
}

void RelayStorage::factoryReset() {
    if (!started_) return;
    prefs_.clear();
}
