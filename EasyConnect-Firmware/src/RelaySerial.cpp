#include "RelaySerial.h"
#include <string.h>

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

RelaySerialInterface::RelaySerialInterface()
    : fwVersion_(nullptr),
      cfg_(nullptr),
      counters_(nullptr),
      controller_(nullptr),
      storage_(nullptr),
      debug485_(nullptr),
      inputBuffer_(""),
      testOreEnabled_(false),
      testOreTriggered_(false),
      testOreStartMs_(0) {}

void RelaySerialInterface::begin(const char *fwVersion,
                                 RelayConfig *cfg,
                                 RelayCounters *counters,
                                 RelayController *controller,
                                 RelayStorage *storage,
                                 bool *debug485) {
    fwVersion_ = fwVersion;
    cfg_ = cfg;
    counters_ = counters;
    controller_ = controller;
    storage_ = storage;
    debug485_ = debug485;
}

void RelaySerialInterface::printHelp() const {
    Serial.println("=== MENU RELAY ===");
    Serial.println("HELP                : Questo menu");
    Serial.println("HELPADVANCE         : Menu tecnico avanzato");
    Serial.println("INFO                : Stato completo");
    Serial.println("SETIP x             : Imposta indirizzo RS485 (1..30)");
    Serial.println("SETSERIAL x         : Imposta seriale (formato YYYYMM03XXXX)");
    Serial.println("SETGROUP x          : Imposta gruppo");
    Serial.println("SETMODE x           : LUCE|UVC|ELETTROSTATICO|GAS|COMANDO (o 1..5)");
    Serial.println("SETLIFEH x          : Soglia fine vita lampade in ore (0=disabilita)");
    Serial.println("SETFB ...           : Configura o disabilita controllo feedback");
    Serial.println("READIP|READSERIAL|READGROUP|READMODE|READLIFEH");
    Serial.println("READFB              : Lettura controllo feedback");
    Serial.println("READCOUNTERS        : Lettura contatori");
    Serial.println("SAVE                : Salva configurazione");
    Serial.println("===================");
}

void RelaySerialInterface::printAdvancedHelp() const {
    Serial.println("=== MENU AVANZATO RELAY ===");
    Serial.println("ADVRELAY ON|OFF|TOGGLE     : Comando diretto relay (no check feedback)");
    Serial.println("ADVRELAYFB ON|OFF|TOGGLE   : Comando relay con logica feedback");
    Serial.println("RELAY ON|OFF|TOGGLE|STATUS : Comando relay standard");
    Serial.println("SETFB OFF                  : Disabilita controllo feedback");
    Serial.println("SETFB ON l d t             : Abilita feedback con logic/delay/tentativi");
    Serial.println("SETFB l d t [en]           : Sintassi legacy");
    Serial.println("SETFBEN 0|1                : Abilita feedback");
    Serial.println("SETBOOT ON|OFF             : Stato relay al boot");
    Serial.println("SETMSG FB testo            : Messaggio fault feedback");
    Serial.println("SETMSG SAFETY testo        : Messaggio safety aperta");
    Serial.println("READFB|READMSG             : Lettura configurazioni avanzate");
    Serial.println("VIEW485|STOP485            : Debug traffico RS485");
    Serial.println("RESETCNT                   : Reset contatori");
    Serial.println("CLEARMEM                   : Factory reset e riavvio");
    Serial.println("===========================");
}

void RelaySerialInterface::printInfo() const {
    if (!cfg_ || !counters_ || !controller_) return;

    const float hours = static_cast<float>(counters_->onSeconds) / 3600.0f;
    const bool lifeExpired = (cfg_->lifeLimitHours > 0) && (hours >= static_cast<float>(cfg_->lifeLimitHours));
    Serial.println("\n--- STATO RELAY ---");
    Serial.printf("FW             : %s\n", fwVersion_ ? fwVersion_ : "N/A");
    Serial.printf("Configurata    : %s\n", cfg_->configured ? "SI" : "NO");
    Serial.printf("RS485 Addr     : %u\n", cfg_->rs485Address);
    Serial.printf("Gruppo         : %u\n", cfg_->group);
    Serial.printf("Seriale        : %s\n", cfg_->serialId);
    Serial.printf("Modalita       : %s (%u)\n", relayModeToString(cfg_->mode), static_cast<unsigned>(cfg_->mode));
    Serial.printf("Boot Relay     : %s\n", relayBoolToOnOff(cfg_->bootRelayOn));
    Serial.printf("Relay Stato    : %s\n", relayBoolToOnOff(controller_->isRelayOn()));
    Serial.printf("Ctrl Stato     : %s\n", relayControlStateToString(controller_->state()));
    Serial.printf("Safety         : %s\n", controller_->isSafetyClosed() ? "CHIUSA" : "APERTA");
    Serial.printf("Feedback Match : %s\n", controller_->isFeedbackMatched() ? "OK" : "NO");
    Serial.printf("Feedback Raw   : %s\n", controller_->isFeedbackSignalHigh() ? "HIGH" : "LOW");
    Serial.printf("Feedback Exp   : %s (logic=%u)\n",
                  controller_->isFeedbackExpectedHigh() ? "HIGH" : "LOW",
                  cfg_->feedback.logic);
    Serial.printf("FB cfg         : en=%u logic=%u delay=%us tentativi=%u\n",
                  cfg_->feedback.enabled ? 1U : 0U,
                  cfg_->feedback.logic,
                  cfg_->feedback.checkDelaySec,
                  cfg_->feedback.attempts);
    Serial.printf("Contatore ON   : %lu\n", static_cast<unsigned long>(counters_->relayStarts));
    Serial.printf("Ore ON         : %.2f h (%llu sec)\n", hours, counters_->onSeconds);
    Serial.printf("Soglia Vita    : %lu h\n", static_cast<unsigned long>(cfg_->lifeLimitHours));
    Serial.printf("Fine Vita      : %s\n", lifeExpired ? "SI" : "NO");
    if (testOreEnabled_) {
        const unsigned long elapsedSec = (millis() - testOreStartMs_) / 1000UL;
        Serial.printf("TestOre        : ATTIVO (%lus/%lus)\n", elapsedSec, 60UL);
    } else if (testOreTriggered_) {
        Serial.println("TestOre        : SCATTATO");
    } else {
        Serial.println("TestOre        : DISATTIVO");
    }
    Serial.printf("Msg FB Fault   : %s\n", cfg_->messages.feedbackFault);
    Serial.printf("Msg Safety     : %s\n", cfg_->messages.safetyFault);
    Serial.printf("Debug 485      : %s\n", (debug485_ && *debug485_) ? "ATTIVO" : "DISATTIVO");
    if (controller_->lastEvent().length() > 0) {
        Serial.printf("Ultimo Evento  : %s\n", controller_->lastEvent().c_str());
    }
    Serial.println("-------------------\n");
}

void RelaySerialInterface::saveConfig() {
    if (!cfg_ || !storage_) return;
    cfg_->configured = relayHasMinimumConfig(*cfg_);
    storage_->saveConfig(*cfg_);
}

void RelaySerialInterface::saveCounters() {
    if (!counters_ || !storage_) return;
    storage_->saveCounters(*counters_);
    if (controller_) controller_->markCountersSaved();
}

void RelaySerialInterface::processCommand(const String &line) {
    if (!cfg_ || !counters_ || !controller_ || !storage_) return;

    String cmd = line;
    cmd.trim();
    if (cmd.length() == 0) return;

    String upper = cmd;
    upper.toUpperCase();

    if (upper == "HELP" || upper == "?") {
        printHelp();
        return;
    }
    if (upper == "HELPADVANCE" || upper == "HELPADV" || upper == "ADVHELP") {
        printAdvancedHelp();
        return;
    }
    if (upper == "INFO" || upper == "STATUS") {
        printInfo();
        return;
    }
    if (upper == "READIP") {
        Serial.printf("IP: %u\n", cfg_->rs485Address);
        return;
    }
    if (upper == "READSERIAL") {
        Serial.printf("SERIALE: %s\n", cfg_->serialId);
        return;
    }
    if (upper == "READGROUP" || upper == "READGRP") {
        Serial.printf("GROUP: %u\n", cfg_->group);
        return;
    }
    if (upper == "READMODE") {
        Serial.printf("MODE: %s (%u)\n", relayModeToString(cfg_->mode), static_cast<unsigned>(cfg_->mode));
        return;
    }
    if (upper == "READLIFEH") {
        Serial.printf("LIFEH: %lu\n", static_cast<unsigned long>(cfg_->lifeLimitHours));
        return;
    }
    if (upper == "READFB") {
        if (cfg_->feedback.enabled) {
            Serial.printf("FB: en=1 logic=%u delay=%us tentativi=%u\n",
                          cfg_->feedback.logic,
                          cfg_->feedback.checkDelaySec,
                          cfg_->feedback.attempts);
        } else {
            Serial.printf("FB: en=0 logic=%u (delay/tentativi ignorati)\n", cfg_->feedback.logic);
        }
        return;
    }
    if (upper == "READMSG") {
        Serial.printf("MSG FB    : %s\n", cfg_->messages.feedbackFault);
        Serial.printf("MSG SAFETY: %s\n", cfg_->messages.safetyFault);
        return;
    }
    if (upper == "READCOUNTERS") {
        const float hours = static_cast<float>(counters_->onSeconds) / 3600.0f;
        Serial.printf("CNT ON: %lu\n", static_cast<unsigned long>(counters_->relayStarts));
        Serial.printf("ON SEC: %llu\n", counters_->onSeconds);
        Serial.printf("ON HRS: %.2f\n", hours);
        return;
    }
    if (upper == "VIEW485") {
        if (debug485_) *debug485_ = true;
        Serial.println("OK: Debug 485 ATTIVO");
        return;
    }
    if (upper == "STOP485") {
        if (debug485_) *debug485_ = false;
        Serial.println("OK: Debug 485 DISATTIVO");
        return;
    }
    if (upper == "SAVE") {
        saveConfig();
        saveCounters();
        Serial.println("OK: Salvataggio completato.");
        return;
    }
    if (upper == "RESETCNT") {
        counters_->relayStarts = 0;
        counters_->onSeconds = 0;
        saveCounters();
        Serial.println("OK: Contatori azzerati.");
        return;
    }
    if (upper == "CLEARMEM") {
        Serial.println("ATTENZIONE: reset memoria in corso...");
        storage_->factoryReset();
        delay(500);
        ESP.restart();
        return;
    }

    if (upper.startsWith("SETIP ") || upper.startsWith("SETIP:")) {
        int sep = cmd.indexOf(' ');
        if (sep < 0) sep = cmd.indexOf(':');
        int ip = (sep >= 0) ? cmd.substring(sep + 1).toInt() : 0;
        if (ip < 1 || ip > 30) {
            Serial.println("ERR: IP non valido (1..30)");
            return;
        }
        cfg_->rs485Address = static_cast<uint8_t>(ip);
        saveConfig();
        Serial.printf("OK: IP=%u\n", cfg_->rs485Address);
        return;
    }

    if (upper.startsWith("SETSERIAL ") || upper.startsWith("SETSERIAL:")) {
        int sep = cmd.indexOf(' ');
        if (sep < 0) sep = cmd.indexOf(':');
        if (sep < 0 || sep + 1 >= cmd.length()) {
            Serial.println("ERR: Seriale mancante");
            return;
        }
        String sn = cmd.substring(sep + 1);
        sn.trim();
        if (!relayIsValidSerialForRelay(sn)) {
            Serial.println("ERR: seriale non valido. Formato richiesto: YYYYMM03XXXX");
            return;
        }
        sn.toCharArray(cfg_->serialId, sizeof(cfg_->serialId));
        saveConfig();
        Serial.printf("OK: SERIALE=%s\n", cfg_->serialId);
        return;
    }

    if (upper.startsWith("SETGROUP ") || upper.startsWith("SETGROUP:")) {
        int sep = cmd.indexOf(' ');
        if (sep < 0) sep = cmd.indexOf(':');
        int grp = (sep >= 0) ? cmd.substring(sep + 1).toInt() : 0;
        if (grp < 0 || grp > 255) {
            Serial.println("ERR: Gruppo non valido (0..255)");
            return;
        }
        cfg_->group = static_cast<uint8_t>(grp);
        saveConfig();
        Serial.printf("OK: GROUP=%u\n", cfg_->group);
        return;
    }

    if (upper.startsWith("SETMODE ") || upper.startsWith("SETMODE:")) {
        int sep = cmd.indexOf(' ');
        if (sep < 0) sep = cmd.indexOf(':');
        if (sep < 0 || sep + 1 >= cmd.length()) {
            Serial.println("ERR: Mode mancante");
            return;
        }
        RelayMode m;
        String token = cmd.substring(sep + 1);
        if (!relayParseMode(token, m)) {
            Serial.println("ERR: Mode non valido");
            return;
        }
        cfg_->mode = m;
        saveConfig();
        controller_->onConfigChanged();
        Serial.printf("OK: MODE=%s\n", relayModeToString(cfg_->mode));
        return;
    }

    if (upper.startsWith("SETLIFEH ") || upper.startsWith("SETLIFEH:")) {
        int sep = cmd.indexOf(' ');
        if (sep < 0) sep = cmd.indexOf(':');
        if (sep < 0 || sep + 1 >= cmd.length()) {
            Serial.println("ERR: valore mancante");
            return;
        }
        const long hours = cmd.substring(sep + 1).toInt();
        if (hours < 0 || hours > 200000) {
            Serial.println("ERR: range valido 0..200000");
            return;
        }
        cfg_->lifeLimitHours = static_cast<uint32_t>(hours);
        saveConfig();
        Serial.printf("OK: LIFEH=%lu\n", static_cast<unsigned long>(cfg_->lifeLimitHours));
        return;
    }

    if (upper.startsWith("SETBOOT ")) {
        bool v;
        if (!parseOnOffValue(cmd.substring(8), v)) {
            Serial.println("ERR: usa ON/OFF o 1/0");
            return;
        }
        cfg_->bootRelayOn = v;
        saveConfig();
        Serial.printf("OK: BOOT=%s\n", relayBoolToOnOff(cfg_->bootRelayOn));
        return;
    }

    if (upper.startsWith("SETFBEN ")) {
        bool v;
        if (!parseOnOffValue(cmd.substring(8), v)) {
            Serial.println("ERR: usa 0/1 o ON/OFF");
            return;
        }
        cfg_->feedback.enabled = v;
        saveConfig();
        controller_->onConfigChanged();
        Serial.printf("OK: FB EN=%u\n", cfg_->feedback.enabled ? 1U : 0U);
        return;
    }

    if (upper.startsWith("SETFB ")) {
        String params = cmd.substring(6);
        params.replace(',', ' ');
        params.trim();

        bool enabledOnly = false;
        if (parseOnOffValue(params, enabledOnly)) {
            if (enabledOnly) {
                Serial.println("ERR: con SETFB ON servono anche logic delay tentativi");
            } else {
                cfg_->feedback.enabled = false;
                saveConfig();
                controller_->onConfigChanged();
                Serial.println("OK: FB DISABILITATO");
            }
            return;
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
        int enabled = -1;
        const int parsed = sscanf(numericParams.c_str(), "%d %d %d %d", &logic, &delaySec, &attempts, &enabled);
        if (parsed < 3) {
            Serial.println("ERR: usa SETFB OFF oppure SETFB ON l d t oppure SETFB l d t [en]");
            return;
        }
        if (logic < 0 || logic > 1 || delaySec < 1 || delaySec > 3600 || attempts < 1 || attempts > 20) {
            Serial.println("ERR: parametri fuori range (logic 0/1, delay 1..3600, tentativi 1..20)");
            return;
        }

        cfg_->feedback.logic = static_cast<uint8_t>(logic);
        cfg_->feedback.checkDelaySec = static_cast<uint16_t>(delaySec);
        cfg_->feedback.attempts = static_cast<uint8_t>(attempts);
        if (hasLeadingEnable) {
            cfg_->feedback.enabled = enabledFromFirstToken;
        } else if (parsed >= 4) {
            cfg_->feedback.enabled = (enabled != 0);
        }
        saveConfig();
        controller_->onConfigChanged();
        Serial.printf("OK: FB=%u,%u,%u,%u\n",
                      cfg_->feedback.logic,
                      cfg_->feedback.checkDelaySec,
                      cfg_->feedback.attempts,
                      cfg_->feedback.enabled ? 1U : 0U);
        return;
    }

    if (upper.startsWith("SETMSG FB ")) {
        String msg = cmd.substring(10);
        msg.trim();
        if (msg == "-") msg = "";
        msg.toCharArray(cfg_->messages.feedbackFault, sizeof(cfg_->messages.feedbackFault));
        saveConfig();
        Serial.println("OK: Messaggio feedback aggiornato.");
        return;
    }

    if (upper.startsWith("SETMSG SAFETY ")) {
        String msg = cmd.substring(14);
        msg.trim();
        if (msg == "-") msg = "";
        msg.toCharArray(cfg_->messages.safetyFault, sizeof(cfg_->messages.safetyFault));
        saveConfig();
        Serial.println("OK: Messaggio safety aggiornato.");
        return;
    }

    // Comando test nascosto: forza fine-vita dopo 60s (non mostrato nel menu HELP).
    if (upper.startsWith("TESTORE ")) {
        String arg = cmd.substring(8);
        arg.trim();
        arg.toUpperCase();
        if (arg == "ON") {
            testOreEnabled_ = true;
            testOreTriggered_ = false;
            testOreStartMs_ = millis();
            Serial.println("OK: TESTORE ON (fine-vita tra 60s).");
        } else if (arg == "OFF") {
            testOreEnabled_ = false;
            testOreTriggered_ = false;
            testOreStartMs_ = 0;
            Serial.println("OK: TESTORE OFF.");
        } else if (arg == "STATUS") {
            if (testOreEnabled_) {
                const unsigned long elapsedSec = (millis() - testOreStartMs_) / 1000UL;
                Serial.printf("TESTORE: ON (%lus/60s)\n", elapsedSec);
            } else {
                Serial.printf("TESTORE: %s\n", testOreTriggered_ ? "TRIGGERED" : "OFF");
            }
        } else {
            Serial.println("ERR: usa TESTORE ON|OFF|STATUS");
        }
        return;
    }

    if (upper.startsWith("ADVRELAYFB ") || upper.startsWith("ADVRELAY ") || upper.startsWith("RELAY ")) {
        bool directMode = false;
        bool forceFeedbackCheck = false;
        String action;

        if (upper.startsWith("ADVRELAYFB ")) {
            forceFeedbackCheck = true;
            action = cmd.substring(11);
        } else if (upper.startsWith("ADVRELAY ")) {
            directMode = true;
            action = cmd.substring(9);
        } else {
            // Alias legacy: relay command con logica standard (mode-driven)
            action = cmd.substring(6);
        }

        action.trim();
        String actUpper = action;
        actUpper.toUpperCase();

        if (actUpper == "STATUS") {
            Serial.printf("Relay: %s | State: %s\n",
                          relayBoolToOnOff(controller_->isRelayOn()),
                          relayControlStateToString(controller_->state()));
            return;
        }

        const bool requiresValidationMode = (cfg_->mode == RelayMode::UVC || cfg_->mode == RelayMode::Elettrostatico);
        if (forceFeedbackCheck && (actUpper == "ON" || actUpper == "TOGGLE") && (!cfg_->feedback.enabled || !requiresValidationMode)) {
            Serial.println("ERR: verifica feedback disponibile solo con feedback abilitato in UVC/Elettrostatico");
            return;
        }

        String out;
        bool ok = false;
        if (actUpper == "ON") {
            ok = directMode ? controller_->commandRelayDirect(true, "SERIAL_ADV", out)
                            : controller_->commandRelay(true, "SERIAL_ADV", out);
        } else if (actUpper == "OFF") {
            ok = directMode ? controller_->commandRelayDirect(false, "SERIAL_ADV", out)
                            : controller_->commandRelay(false, "SERIAL_ADV", out);
        } else if (actUpper == "TOGGLE") {
            if (directMode) {
                ok = controller_->commandRelayDirect(!controller_->isRelayOn(), "SERIAL_ADV", out);
            } else {
                ok = controller_->commandToggle("SERIAL_ADV", out);
            }
        } else {
            Serial.println("ERR: azione relay non valida");
            return;
        }

        Serial.println(out);
        if (ok) saveCounters();
        return;
    }

    Serial.println("ERR: comando sconosciuto. Digita HELP.");
}

void RelaySerialInterface::serviceTestOreSimulation() {
    if (!testOreEnabled_ || !cfg_ || !counters_ || !storage_) return;
    if (millis() - testOreStartMs_ < 60000UL) return;

    const uint64_t limitHours = (cfg_->lifeLimitHours > 0) ? cfg_->lifeLimitHours : 10000UL;
    const uint64_t targetSeconds = limitHours * 3600ULL;

    if (counters_->onSeconds < targetSeconds) {
        counters_->onSeconds = targetSeconds;
        storage_->saveCounters(*counters_);
        if (controller_) controller_->markCountersSaved();
    }

    testOreEnabled_ = false;
    testOreTriggered_ = true;
    testOreStartMs_ = 0;
    Serial.println("[TESTORE] Fine-vita simulata attivata.");
}

void RelaySerialInterface::update() {
    serviceTestOreSimulation();

    while (Serial.available()) {
        char c = static_cast<char>(Serial.read());
        if (c == '\n' || c == '\r') {
            if (inputBuffer_.length() > 0) {
                Serial.print("\r\n");
                processCommand(inputBuffer_);
                inputBuffer_ = "";
            }
        } else if (c == 8 || c == 127) {
            if (inputBuffer_.length() > 0) {
                inputBuffer_.remove(inputBuffer_.length() - 1);
                Serial.print("\b \b");
            }
        } else if (inputBuffer_.length() < 220) {
            inputBuffer_ += c;
            Serial.write(c);
        }
    }
}
