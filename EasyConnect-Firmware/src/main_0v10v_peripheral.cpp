// ═══════════════════════════════════════════════════════════════════════════
// EasyConnect — Periferica Inverter 0-10V
// Scheda: ESP32-C3 (Treedom "Periferica Sensori & 0-10V" Rev.1.0)
//
// Converte un setpoint di velocità (0-100%) ricevuto via RS485 in un segnale
// PWM su IO2. Il circuito op-amp (TSB572I U2B/U2C) lo filtra e amplifica
// portandolo a 0-10V verso il morsetto InverterSpeed dell'inverter motore.
// La tensione 10V di riferimento è prelevata dall'inverter stesso.
//
// Seriale formato: YYYYMM05XXXX  (05 = Periferica Inverter 0-10V)
//
// Gruppo:  1 = aspirazione   (motore lavora in estrazione)
//          2 = immissione     (motore lavora in mandata)
//
// Protocollo RS485 (frame terminato da '!'):
//   ?<IP>               → stato completo (risposta: OK,MOT5,...)
//   SPD<IP>:<0-100>     → imposta velocità %
//   ENA<IP>:<0|1>       → abilita / disabilita inverter
//   GRP<IP>:<1-2>       → imposta gruppo
//   IP<IP>:<newIP>      → cambia indirizzo RS485
//   SER<IP>:<serial>    → imposta seriale
//
// Comandi USB seriale: HELP / INFO / SETIP / SETSERIAL / SETGROUP /
//                      SETSPEED / ENABLE / SAVE / CLEARMEM / VIEW485
// ═══════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <Preferences.h>
#include "Pins.h"
#include "Led.h"

// ─── Versione firmware ─────────────────────────────────────────────────────
static const char* FW_VERSION = "0.1.2";

// ─── Pin (dichiarati nel profilo BOARD_PROFILE_0V10V di Pins.h) ────────────
//   PIN_RS485_DIR  = 7
//   PIN_RS485_TX   = 21
//   PIN_RS485_RX   = 20
//   PIN_LED_GREEN  = 9
//   PIN_LED_RED    = 8
//   PIN_0V10V_PWM        = 2   IO2 → op-amp → 0-10V
//   PIN_0V10V_ENABLE     = 3   IO3 → enable inverter
//   PIN_0V10V_FEEDBACK   = 10  IO10 ← feedback inverter

// ─── Feedback polarity ────────────────────────────────────────────────────
// Quando l'inverter è OK (no fault), il segnale di feedback attiva
// l'optocoupler U5 e porta IO10 a LOW (collettore a GND).
// Se il wiring è invertito, cambiare LOW → HIGH.
#define FEEDBACK_OK_LEVEL LOW

// ─── LEDC (PWM hardware ESP32-C3) ─────────────────────────────────────────
#define LEDC_CHANNEL    0
#define LEDC_FREQ_HZ    100     // 100 Hz: riduce la deformazione duty attraverso l'opto TLP182
#define LEDC_BITS       10
#define LEDC_MAX_DUTY   ((1u << LEDC_BITS) - 1u)   // = 1023

static constexpr unsigned long FEEDBACK_CHECK_DELAY_MS = 5000UL;

// ─── RS485 ────────────────────────────────────────────────────────────────
#define RS485_BAUD      115200

// ─── Struttura configurazione (salvata in Preferences, namespace "inv5") ──
struct Inv5Config {
    bool    configured;
    uint8_t rs485Address;   // 1..30
    uint8_t group;          // 1=aspirazione, 2=immissione
    char    serialId[16];
    uint8_t savedSpeed;     // velocità salvata (0..100); ripristinata al boot
    bool    enableOnBoot;   // abilita inverter automaticamente all'avvio
};

// ─── Variabili globali ────────────────────────────────────────────────────
static Inv5Config    g_cfg;
static Preferences   g_prefs;
static String        g_inputBuf;
static bool          g_debug485 = false;
static unsigned long g_lastAnyActivity    = 0;
static unsigned long g_lastDirectedAct    = 0;

// Stato runtime (non persistente)
static uint8_t       g_speed   = 0;
static bool          g_enabled = false;
static bool          g_fbOk    = false;
static bool          g_fbFaultLatched = false;
static unsigned long g_fbCheckStartMs = 0;

enum class InvRunState : uint8_t {
    Off = 0,
    WaitingFeedback,
    Running,
    Fault,
};

static InvRunState   g_runState = InvRunState::Off;

static Led g_ledGreen(PIN_LED_GREEN);
static Led g_ledRed(PIN_LED_RED);

// ─── Helpers ──────────────────────────────────────────────────────────────

static bool isValidSerial(const String &s) {
    if (s.length() != 12) return false;
    for (int i = 0; i < 12; i++) {
        if (!isDigit(s.charAt(i))) return false;
    }
    const int year  = s.substring(0, 4).toInt();
    const int month = s.substring(4, 6).toInt();
    const String tc = s.substring(6, 8);
    const int seq   = s.substring(8, 12).toInt();
    return (year >= 2020 && year <= 2099) &&
           (month >= 1 && month <= 12) &&
           (tc == "05") &&
           (seq >= 1 && seq <= 9999);
}

static bool hasMinConfig() {
    return g_cfg.rs485Address >= 1 &&
           g_cfg.rs485Address <= 30 &&
           isValidSerial(String(g_cfg.serialId));
}

static const char* runStateText() {
    switch (g_runState) {
        case InvRunState::Off:             return "OFF";
        case InvRunState::WaitingFeedback: return "WAIT_FB";
        case InvRunState::Running:         return "RUNNING";
        case InvRunState::Fault:           return "FAULT";
        default:                           return "UNKNOWN";
    }
}

static void writePwmForState() {
    const uint32_t duty = g_enabled ? (((uint32_t)g_speed * LEDC_MAX_DUTY) / 100u) : 0u;
    ledcWrite(LEDC_CHANNEL, duty);
}

static void applySpeed(uint8_t pct) {
    if (pct > 100) pct = 100;
    g_speed = pct;
    writePwmForState();
}

static void applyEnable(bool en) {
    g_enabled = en;
    digitalWrite(PIN_0V10V_ENABLE, en ? HIGH : LOW);
    writePwmForState();

    if (en) {
        g_fbFaultLatched = false;
        g_fbCheckStartMs = millis();
        g_runState = InvRunState::WaitingFeedback;
    } else {
        g_fbFaultLatched = false;
        g_fbCheckStartMs = 0;
        g_runState = InvRunState::Off;
    }
}

static void updateFeedbackState() {
    g_fbOk = (digitalRead(PIN_0V10V_FEEDBACK) == FEEDBACK_OK_LEVEL);

    if (!g_enabled) {
        g_fbFaultLatched = false;
        g_fbCheckStartMs = 0;
        g_runState = InvRunState::Off;
        return;
    }

    const unsigned long now = millis();
    if (g_fbOk) {
        g_fbFaultLatched = false;
        g_fbCheckStartMs = now;
        g_runState = InvRunState::Running;
        return;
    }

    if (g_runState != InvRunState::WaitingFeedback && g_runState != InvRunState::Fault) {
        g_runState = InvRunState::WaitingFeedback;
        g_fbCheckStartMs = now;
    }
    if (g_fbCheckStartMs == 0) {
        g_fbCheckStartMs = now;
    }

    if (g_runState == InvRunState::WaitingFeedback &&
        (now - g_fbCheckStartMs) >= FEEDBACK_CHECK_DELAY_MS) {
        g_fbFaultLatched = true;
        g_runState = InvRunState::Fault;
    }
}

// ─── Preferences ──────────────────────────────────────────────────────────

static void loadConfig() {
    g_cfg.configured    = g_prefs.getBool("set",  false);
    g_cfg.rs485Address  = (uint8_t)g_prefs.getInt("addr",  1);
    g_cfg.group         = (uint8_t)g_prefs.getInt("grp",   1);
    g_cfg.savedSpeed    = (uint8_t)g_prefs.getInt("spd",   0);
    g_cfg.enableOnBoot  = g_prefs.getBool("enboot", false);
    String s = g_prefs.getString("ser", "NON_SET");
    s.toCharArray(g_cfg.serialId, sizeof(g_cfg.serialId));
}

static void saveConfig() {
    g_cfg.configured = hasMinConfig();
    g_prefs.putBool("set",    g_cfg.configured);
    g_prefs.putInt("addr",    g_cfg.rs485Address);
    g_prefs.putInt("grp",     g_cfg.group);
    g_prefs.putString("ser",  g_cfg.serialId);
    g_prefs.putBool("enboot", g_cfg.enableOnBoot);
}

static void saveSpeed() {
    g_cfg.savedSpeed = g_speed;
    g_prefs.putInt("spd", g_cfg.savedSpeed);
}

// ─── RS485 ────────────────────────────────────────────────────────────────

static void rs485RxMode() {
    Serial1.flush();
    digitalWrite(PIN_RS485_DIR, LOW);
    delayMicroseconds(80);
}

static void rs485TxMode() {
    digitalWrite(PIN_RS485_DIR, HIGH);
    delayMicroseconds(80);
}

static void rs485Send(const String &payload) {
    rs485TxMode();
    Serial1.print(payload);
    Serial1.print("!");
    Serial1.flush();
    rs485RxMode();
    if (g_debug485) Serial.printf("[485-TX] %s!\n", payload.c_str());
}

static String buildStatus() {
    String s = "OK,MOT5,";
    s += g_speed;       s += ",";
    s += g_enabled ? 1 : 0; s += ",";
    s += g_fbOk    ? 1 : 0; s += ",";
    s += g_cfg.group;   s += ",";
    s += g_cfg.serialId; s += ",";
    s += FW_VERSION; s += ",";
    s += g_fbFaultLatched ? 1 : 0; s += ",";
    s += runStateText();
    return s;
}

static void processRs485Frame(const String &frame) {
    g_lastAnyActivity = millis();
    if (g_debug485) Serial.printf("[485-RX] %s\n", frame.c_str());

    // Status query: ?<IP>
    if (frame.startsWith("?")) {
        const int ip = frame.substring(1).toInt();
        if (ip == g_cfg.rs485Address) {
            g_lastDirectedAct = millis();
            rs485Send(buildStatus());
        }
        return;
    }

    // SPD<IP>:<0-100>
    if (frame.startsWith("SPD")) {
        const int colon = frame.indexOf(':');
        if (colon <= 3) return;
        if (frame.substring(3, colon).toInt() != g_cfg.rs485Address) return;
        g_lastDirectedAct = millis();
        const int pct = frame.substring(colon + 1).toInt();
        if (pct < 0 || pct > 100) { rs485Send("ERR,SPD,RANGE"); return; }
        applySpeed((uint8_t)pct);
        saveSpeed();
        rs485Send("OK,SPD," + String(g_cfg.rs485Address) + "," + String(g_speed));
        return;
    }

    // ENA<IP>:<0|1>
    if (frame.startsWith("ENA")) {
        const int colon = frame.indexOf(':');
        if (colon <= 3) return;
        if (frame.substring(3, colon).toInt() != g_cfg.rs485Address) return;
        g_lastDirectedAct = millis();
        const int v = frame.substring(colon + 1).toInt();
        applyEnable(v != 0);
        rs485Send("OK,ENA," + String(g_cfg.rs485Address) + "," + String(g_enabled ? 1 : 0));
        return;
    }

    // GRP<IP>:<1-2>
    if (frame.startsWith("GRP")) {
        const int colon = frame.indexOf(':');
        if (colon <= 3) return;
        if (frame.substring(3, colon).toInt() != g_cfg.rs485Address) return;
        g_lastDirectedAct = millis();
        const int grp = frame.substring(colon + 1).toInt();
        if (grp < 1 || grp > 2) { rs485Send("ERR,GRP,RANGE"); return; }
        g_cfg.group = (uint8_t)grp;
        saveConfig();
        rs485Send("OK,CFG,GRP," + String(g_cfg.rs485Address) + "," + String(g_cfg.group));
        return;
    }

    // IP<IP>:<newIP>
    if (frame.startsWith("IP")) {
        const int colon = frame.indexOf(':');
        if (colon <= 2) return;
        if (frame.substring(2, colon).toInt() != g_cfg.rs485Address) return;
        g_lastDirectedAct = millis();
        const int newIp = frame.substring(colon + 1).toInt();
        if (newIp < 1 || newIp > 30) { rs485Send("ERR,CFG,IP"); return; }
        const uint8_t oldIp = g_cfg.rs485Address;
        g_cfg.rs485Address = (uint8_t)newIp;
        saveConfig();
        rs485Send("OK,CFG,IP," + String(oldIp) + "," + String(g_cfg.rs485Address));
        return;
    }

    // SER<IP>:<serial>
    if (frame.startsWith("SER")) {
        const int colon = frame.indexOf(':');
        if (colon <= 3) return;
        if (frame.substring(3, colon).toInt() != g_cfg.rs485Address) return;
        g_lastDirectedAct = millis();
        String sn = frame.substring(colon + 1);
        sn.trim();
        if (!isValidSerial(sn)) { rs485Send("ERR,CFG,SERFMT"); return; }
        sn.toCharArray(g_cfg.serialId, sizeof(g_cfg.serialId));
        saveConfig();
        rs485Send("OK,CFG,SER," + String(g_cfg.rs485Address) + "," + String(g_cfg.serialId));
        return;
    }
}

static void rs485Update() {
    if (!Serial1.available()) return;
    const String frame = Serial1.readStringUntil('!');
    if (frame.length() == 0) return;
    processRs485Frame(frame);
}

// ─── Interfaccia seriale USB ───────────────────────────────────────────────

static void printHelp() {
    Serial.println("=== MENU INVERTER 0-10V ===");
    Serial.println("HELP              : Questo menu");
    Serial.println("INFO              : Stato completo");
    Serial.println("SETIP x           : Imposta indirizzo RS485 (1..30)");
    Serial.println("SETSERIAL x       : Seriale (formato YYYYMM05XXXX)");
    Serial.println("SETGROUP x        : Gruppo  1=aspirazione  2=immissione");
    Serial.println("SETSPEED x        : Velocità % (0..100) [test]");
    Serial.println("ENABLE ON|OFF     : Abilita/disabilita inverter [test]");
    Serial.println("SETENBOOT ON|OFF  : Abilita inverter all'avvio automatico");
    Serial.println("SAVE              : Salva configurazione e velocità corrente");
    Serial.println("VIEW485|STOP485   : Debug traffico RS485");
    Serial.println("CLEARMEM          : Factory reset e riavvio");
    Serial.println("===========================");
}

static void printInfo() {
    Serial.println("\n--- STATO INVERTER 0-10V ---");
    Serial.printf("FW             : %s\n", FW_VERSION);
    Serial.printf("Configurata    : %s\n", g_cfg.configured ? "SI" : "NO");
    Serial.printf("RS485 Addr     : %u\n", g_cfg.rs485Address);
    Serial.printf("Gruppo         : %u (%s)\n", g_cfg.group,
                  g_cfg.group == 1 ? "aspirazione" : g_cfg.group == 2 ? "immissione" : "?");
    Serial.printf("Seriale        : %s\n", g_cfg.serialId);
    Serial.printf("Velocita       : %u %%\n", g_speed);
    Serial.printf("Inverter EN    : %s\n", g_enabled ? "ON" : "OFF");
    Serial.printf("Feedback       : %s\n", g_fbOk ? "OK (no fault)" : "FAULT / NC");
    Serial.printf("Stato          : %s\n", runStateText());
    Serial.printf("Fault latched  : %s\n", g_fbFaultLatched ? "SI" : "NO");
    Serial.printf("Check delay    : %lu ms\n", (unsigned long)FEEDBACK_CHECK_DELAY_MS);
    Serial.printf("SavedSpeed     : %u %%\n", g_cfg.savedSpeed);
    Serial.printf("EnableOnBoot   : %s\n", g_cfg.enableOnBoot ? "SI" : "NO");
    Serial.printf("Debug 485      : %s\n", g_debug485 ? "ATTIVO" : "DISATTIVO");
    Serial.println("----------------------------\n");
}

static void processSerialCommand(const String &line) {
    String cmd = line;
    cmd.trim();
    if (cmd.length() == 0) return;
    String upper = cmd;
    upper.toUpperCase();

    if (upper == "HELP" || upper == "?") { printHelp(); return; }
    if (upper == "INFO" || upper == "STATUS") { printInfo(); return; }
    if (upper == "VIEW485") { g_debug485 = true;  Serial.println("OK: Debug 485 ATTIVO"); return; }
    if (upper == "STOP485") { g_debug485 = false; Serial.println("OK: Debug 485 DISATTIVO"); return; }
    if (upper == "SAVE") {
        saveConfig();
        saveSpeed();
        Serial.println("OK: Salvataggio completato.");
        return;
    }
    if (upper == "CLEARMEM") {
        Serial.println("ATTENZIONE: factory reset in corso...");
        g_prefs.clear();
        delay(500);
        ESP.restart();
        return;
    }

    if (upper.startsWith("SETIP ") || upper.startsWith("SETIP:")) {
        const int sep = cmd.indexOf(' ') >= 0 ? cmd.indexOf(' ') : cmd.indexOf(':');
        const int ip = cmd.substring(sep + 1).toInt();
        if (ip < 1 || ip > 30) { Serial.println("ERR: IP non valido (1..30)"); return; }
        g_cfg.rs485Address = (uint8_t)ip;
        saveConfig();
        Serial.printf("OK: IP=%u\n", g_cfg.rs485Address);
        return;
    }

    if (upper.startsWith("SETSERIAL ") || upper.startsWith("SETSERIAL:")) {
        const int sep = cmd.indexOf(' ') >= 0 ? cmd.indexOf(' ') : cmd.indexOf(':');
        if (sep < 0 || sep + 1 >= (int)cmd.length()) { Serial.println("ERR: Seriale mancante"); return; }
        String sn = cmd.substring(sep + 1);
        sn.trim();
        if (!isValidSerial(sn)) { Serial.println("ERR: seriale non valido. Formato: YYYYMM05XXXX"); return; }
        sn.toCharArray(g_cfg.serialId, sizeof(g_cfg.serialId));
        saveConfig();
        Serial.printf("OK: SERIALE=%s\n", g_cfg.serialId);
        return;
    }

    if (upper.startsWith("SETGROUP ") || upper.startsWith("SETGROUP:")) {
        const int sep = cmd.indexOf(' ') >= 0 ? cmd.indexOf(' ') : cmd.indexOf(':');
        const int grp = cmd.substring(sep + 1).toInt();
        if (grp < 1 || grp > 2) { Serial.println("ERR: Gruppo non valido (1=aspirazione, 2=immissione)"); return; }
        g_cfg.group = (uint8_t)grp;
        saveConfig();
        Serial.printf("OK: GROUP=%u (%s)\n", g_cfg.group, g_cfg.group == 1 ? "aspirazione" : "immissione");
        return;
    }

    if (upper.startsWith("SETSPEED ") || upper.startsWith("SETSPEED:")) {
        const int sep = cmd.indexOf(' ') >= 0 ? cmd.indexOf(' ') : cmd.indexOf(':');
        const int pct = cmd.substring(sep + 1).toInt();
        if (pct < 0 || pct > 100) { Serial.println("ERR: Velocita non valida (0..100)"); return; }
        applySpeed((uint8_t)pct);
        Serial.printf("OK: SPEED=%u%%\n", g_speed);
        return;
    }

    if (upper.startsWith("ENABLE ")) {
        const String arg = upper.substring(7);
        if (arg == "ON" || arg == "1") {
            applyEnable(true);
            Serial.println("OK: Inverter ABILITATO");
        } else if (arg == "OFF" || arg == "0") {
            applyEnable(false);
            Serial.println("OK: Inverter DISABILITATO");
        } else {
            Serial.println("ERR: usa ENABLE ON|OFF");
        }
        return;
    }

    if (upper.startsWith("SETENBOOT ")) {
        const String arg = upper.substring(10);
        if (arg == "ON" || arg == "1") {
            g_cfg.enableOnBoot = true;
        } else if (arg == "OFF" || arg == "0") {
            g_cfg.enableOnBoot = false;
        } else {
            Serial.println("ERR: usa SETENBOOT ON|OFF");
            return;
        }
        saveConfig();
        Serial.printf("OK: EnableOnBoot=%s\n", g_cfg.enableOnBoot ? "ON" : "OFF");
        return;
    }

    Serial.println("ERR: comando sconosciuto. Digita HELP.");
}

static void serialUpdate() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (g_inputBuf.length() > 0) {
                Serial.print("\r\n");
                processSerialCommand(g_inputBuf);
                g_inputBuf = "";
            }
        } else if (c == 8 || c == 127) {
            if (g_inputBuf.length() > 0) {
                g_inputBuf.remove(g_inputBuf.length() - 1);
                Serial.print("\b \b");
            }
        } else if (g_inputBuf.length() < 120) {
            g_inputBuf += c;
            Serial.write(c);
        }
    }
}

// ─── LED ──────────────────────────────────────────────────────────────────

static void updateLeds() {
    const unsigned long now = millis();

    // LED VERDE: attività RS485
    if (g_lastDirectedAct > 0 && (now - g_lastDirectedAct) < 5000UL) {
        g_ledGreen.setState(LED_BLINK_SLOW);
    } else if (g_lastAnyActivity > 0 && (now - g_lastAnyActivity) < 5000UL) {
        g_ledGreen.setState(LED_SOLID);
    } else {
        g_ledGreen.setState(LED_BLINK_FAST);
    }

    // LED ROSSO: stato dispositivo
    if (!g_cfg.configured) {
        g_ledRed.setState(LED_BLINK_FAST);
    } else if (g_fbFaultLatched) {
        g_ledRed.setState(LED_BLINK_SLOW);  // inverter in fault
    } else if (g_enabled) {
        g_ledRed.setState(LED_SOLID);       // in funzione OK
    } else {
        g_ledRed.setState(LED_OFF);         // disabilitato
    }

    g_ledGreen.update();
    g_ledRed.update();
}

// ─── Setup & Loop ─────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(1200);
    Serial.println("\n--- EASY CONNECT INVERTER 0-10V ---");
    Serial.printf("FW: %s\n", FW_VERSION);

    // Preferences
    g_prefs.begin("inv5", false);
    loadConfig();

    // LED
    pinMode(PIN_LED_GREEN, OUTPUT);
    pinMode(PIN_LED_RED,   OUTPUT);
    g_ledGreen.begin();
    g_ledRed.begin();

    // Enable pin
    pinMode(PIN_0V10V_ENABLE, OUTPUT);
    digitalWrite(PIN_0V10V_ENABLE, LOW);

    // Feedback pin
    pinMode(PIN_0V10V_FEEDBACK, INPUT_PULLUP);

    // PWM (LEDC)
    ledcSetup(LEDC_CHANNEL, LEDC_FREQ_HZ, LEDC_BITS);
    ledcAttachPin(PIN_0V10V_PWM, LEDC_CHANNEL);
    ledcWrite(LEDC_CHANNEL, 0);

    // RS485
    pinMode(PIN_RS485_DIR, OUTPUT);
    digitalWrite(PIN_RS485_DIR, LOW);
    Serial1.setRxBufferSize(512);
    Serial1.begin(RS485_BAUD, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
    Serial1.setTimeout(35);
    rs485RxMode();

    // Ripristina stato salvato
    if (g_cfg.configured) {
        applySpeed(g_cfg.savedSpeed);
        if (g_cfg.enableOnBoot) applyEnable(true);
        Serial.printf("[INFO] Config caricata: IP=%u GRP=%u SERIAL=%s SPD=%u%%\n",
                      g_cfg.rs485Address, g_cfg.group, g_cfg.serialId, g_speed);
    } else {
        Serial.println("[!] Scheda non configurata. Usare SETIP + SETSERIAL.");
        Serial.println("    Digitare HELP per la lista comandi.");
    }
}

void loop() {
    serialUpdate();
    rs485Update();

    updateFeedbackState();

    updateLeds();
    delay(2);
}
