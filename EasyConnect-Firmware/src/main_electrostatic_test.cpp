// main_electrostatic_test.cpp
// Target: diagnostic_electrostatic  (BOARD_PROFILE_CONTROLLER — ESP32-C3)
//
// Flusso operativo:
//   1. start       → scansione addr 1-20, memorizza i dispositivi trovati
//   2. view485     → avvia visualizzazione continua (ogni 10s)
//   3. ON [addr]   → attiva relay filtro (tutti se addr omesso)
//   4. OFF [addr]  → disattiva relay filtro (tutti se addr omesso)
//   5. stop485     → ferma visualizzazione, torna in attesa
//
// RS485 (BOARD_PROFILE_CONTROLLER):  TX=IO21  RX=IO20  DIR=IO7
// Filtro elettrostatico: Modbus RTU 9600 8N1
//   Reg 0x0001 (R)   — stato: BIT0=run, BIT1=wash, BIT2=fault, BIT3=air, BIT8-15=umidità%
//   Reg 0x0002 (R)   — ore cumulative (16-bit)
//   Reg 0x0006 (R/W) — 0x0001=avvia, 0x0000=ferma

#if defined(BOARD_PROFILE_CONTROLLER)

#include <Arduino.h>
#include <HardwareSerial.h>
#include "Pins.h"

static HardwareSerial RS485Serial(1);

#define MODBUS_BAUD         9600
#define MODBUS_TIMEOUT_MS   150
#define SCAN_TIMEOUT_MS     80
#define VIEW_INTERVAL_MS    10000
#define SCAN_MAX_ADDR       20
#define MAX_DEVICES         8

// ── Stato globale ────────────────────────────────────────────────────────────

static uint8_t  found_addrs[MAX_DEVICES];
static uint8_t  found_count = 0;
static bool     viewing     = false;
static uint32_t last_view   = 0;

// ── CRC16 Modbus ─────────────────────────────────────────────────────────────

static uint16_t crc16(const uint8_t* buf, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
    }
    return crc;
}

// ── RS485 transazione raw ────────────────────────────────────────────────────

static int rs485_transaction(const uint8_t* tx, uint8_t tx_len,
                              uint8_t* rx, uint8_t rx_max,
                              uint32_t timeout_ms)
{
    while (RS485Serial.available()) RS485Serial.read();

    digitalWrite(PIN_RS485_DIR, HIGH);
    RS485Serial.write(tx, tx_len);
    RS485Serial.flush();
    digitalWrite(PIN_RS485_DIR, LOW);

    uint32_t t0 = millis(), last_byte = millis();
    int n = 0;
    while (n < rx_max) {
        if (RS485Serial.available()) {
            rx[n++] = RS485Serial.read();
            last_byte = millis();
        }
        if (n > 0 && (millis() - last_byte) > 5) break;
        if ((millis() - t0) > timeout_ms) break;
    }
    return n;
}

// ── FC03 — lettura registri ──────────────────────────────────────────────────

static bool modbus_read_regs(uint8_t addr, uint16_t reg, uint8_t count,
                              uint16_t* out, uint32_t timeout_ms = MODBUS_TIMEOUT_MS)
{
    uint8_t tx[8] = {
        addr, 0x03,
        (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF),
        0x00, count
    };
    uint16_t c = crc16(tx, 6);
    tx[6] = c & 0xFF; tx[7] = c >> 8;

    uint8_t rx[64];
    int n = rs485_transaction(tx, 8, rx, sizeof(rx), timeout_ms);

    if (n < 5 + 2 * count)                                           return false;
    if (rx[0] != addr || rx[1] != 0x03 || rx[2] != 2 * count)       return false;
    if ((rx[n-2] | (uint16_t)(rx[n-1] << 8)) != crc16(rx, n - 2))   return false;

    for (int i = 0; i < count; i++)
        out[i] = ((uint16_t)rx[3 + 2*i] << 8) | rx[4 + 2*i];
    return true;
}

// ── FC06 — scrittura registro singolo ────────────────────────────────────────

static bool modbus_write_reg(uint8_t addr, uint16_t reg, uint16_t val) {
    uint8_t tx[8] = {
        addr, 0x06,
        (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF),
        (uint8_t)(val >> 8), (uint8_t)(val & 0xFF)
    };
    uint16_t c = crc16(tx, 6);
    tx[6] = c & 0xFF; tx[7] = c >> 8;

    uint8_t rx[8];
    int n = rs485_transaction(tx, 8, rx, sizeof(rx), MODBUS_TIMEOUT_MS);

    if (n < 8)                              return false;
    if (rx[0] != addr || rx[1] != 0x06)    return false;
    return (rx[6] | (uint16_t)(rx[7] << 8)) == crc16(rx, 6);
}

// ── Stampa stato di un indirizzo ─────────────────────────────────────────────

static void print_device_status(uint8_t addr) {
    uint16_t regs[2];
    if (!modbus_read_regs(addr, 0x0001, 2, regs)) {
        Serial.printf("  [addr %02d] nessuna risposta\n", addr);
        return;
    }
    uint16_t s = regs[0];
    Serial.printf("  [addr %02d]  Run:%-2s  Wash:%-2s  Fault:%-2s  Air:%-2s  Hum:%3d%%  Ore:%d\n",
        addr,
        (s & 0x01) ? "SI" : "NO",
        (s & 0x02) ? "SI" : "NO",
        (s & 0x04) ? "SI" : "NO",
        (s & 0x08) ? "SI" : "NO",
        (s >> 8) & 0xFF,
        regs[1]);
}

static void print_all_status() {
    if (found_count == 0) {
        Serial.println("  Nessun dispositivo in lista. Esegui 'start' prima.");
        return;
    }
    for (uint8_t i = 0; i < found_count; i++)
        print_device_status(found_addrs[i]);
}

// ── Comandi ──────────────────────────────────────────────────────────────────

static void cmd_start() {
    found_count = 0;
    Serial.printf("Scansione addr 1..%d...\n", SCAN_MAX_ADDR);

    for (int a = 1; a <= SCAN_MAX_ADDR; a++) {
        uint16_t reg;
        if (modbus_read_regs((uint8_t)a, 0x0001, 1, &reg, SCAN_TIMEOUT_MS)) {
            if (found_count < MAX_DEVICES)
                found_addrs[found_count++] = (uint8_t)a;
            Serial.printf("  TROVATO ");
            print_device_status((uint8_t)a);
        }
        delay(10);
    }

    if (found_count == 0)
        Serial.println("Nessun dispositivo trovato. Verifica cablaggio e indirizzo DIP.");
    else
        Serial.printf("Trovati %d dispositivo/i. Usa 'view485' per avviare il monitoraggio.\n", found_count);
}

static void cmd_fullscan() {
    Serial.println("Scansione completa addr 0..255 (~25s)...");
    Serial.println("(addr 0 = broadcast: non risponde, incluso per completezza)");
    int found = 0;
    for (int a = 0; a <= 255; a++) {
        if (a % 16 == 0)
            Serial.printf("  [%3d-%3d] ...\r", a, a + 15);
        uint16_t reg;
        if (modbus_read_regs((uint8_t)a, 0x0001, 1, &reg, SCAN_TIMEOUT_MS)) {
            Serial.printf("  TROVATO addr %3d (0x%02X)  →  ", a, a);
            print_device_status((uint8_t)a);
            if (found_count < MAX_DEVICES)
                found_addrs[found_count++] = (uint8_t)a;
            found++;
        }
        delay(10);
    }
    Serial.println();
    if (found == 0)
        Serial.println("Nessun dispositivo trovato su nessun indirizzo.");
    else
        Serial.printf("Scansione completa: %d dispositivo/i trovato/i e memorizzato/i.\n", found);
}

static void cmd_view485() {
    if (found_count == 0) {
        Serial.println("Nessun dispositivo in lista. Esegui 'start' prima.");
        return;
    }
    viewing = true;
    last_view = millis() - VIEW_INTERVAL_MS;  // prima lettura immediata
    Serial.printf("Monitoraggio avviato (ogni %ds). Digita 'stop485' per fermare.\n",
        VIEW_INTERVAL_MS / 1000);
}

static void cmd_stop485() {
    viewing = false;
    Serial.println("Monitoraggio fermato.");
}

static void cmd_on(int addr) {
    if (addr > 0) {
        bool ok = modbus_write_reg((uint8_t)addr, 0x0006, 0x0001);
        Serial.printf("  [addr %02d] ON → %s\n", addr, ok ? "OK" : "FAIL");
    } else {
        if (found_count == 0) { Serial.println("Nessun dispositivo. Esegui 'start' prima."); return; }
        for (uint8_t i = 0; i < found_count; i++) {
            bool ok = modbus_write_reg(found_addrs[i], 0x0006, 0x0001);
            Serial.printf("  [addr %02d] ON → %s\n", found_addrs[i], ok ? "OK" : "FAIL");
        }
    }
}

static void cmd_off(int addr) {
    if (addr > 0) {
        bool ok = modbus_write_reg((uint8_t)addr, 0x0006, 0x0000);
        Serial.printf("  [addr %02d] OFF → %s\n", addr, ok ? "OK" : "FAIL");
    } else {
        if (found_count == 0) { Serial.println("Nessun dispositivo. Esegui 'start' prima."); return; }
        for (uint8_t i = 0; i < found_count; i++) {
            bool ok = modbus_write_reg(found_addrs[i], 0x0006, 0x0000);
            Serial.printf("  [addr %02d] OFF → %s\n", found_addrs[i], ok ? "OK" : "FAIL");
        }
    }
}

// ── rawscan: debug raw bytes ──────────────────────────────────────────────────
// Invia una richiesta FC03 reg 0x0001 all'indirizzo specificato e stampa
// esattamente cosa arriva sul bus (hex + ASCII), senza validare nulla.
// Usa rawscan 0 per ascoltare il bus senza trasmettere (listen-only).

static void cmd_rawscan(int addr) {
    uint8_t rx[32];

    if (addr == 0) {
        // Listen-only: svuota il buffer e ascolta per 500ms
        while (RS485Serial.available()) RS485Serial.read();
        Serial.println("Listen-only 500ms (nessuna trasmissione)...");
        uint32_t t0 = millis();
        int n = 0;
        while ((millis() - t0) < 500 && n < (int)sizeof(rx)) {
            if (RS485Serial.available())
                rx[n++] = RS485Serial.read();
        }
        if (n == 0) {
            Serial.println("  Bus silenzioso — nessun byte ricevuto.");
        } else {
            Serial.printf("  Ricevuti %d byte sul bus:\n  HEX: ", n);
            for (int i = 0; i < n; i++) Serial.printf("%02X ", rx[i]);
            Serial.println();
        }
        return;
    }

    // Costruisce richiesta FC03: leggi 1 registro da 0x0001
    uint8_t tx[8] = { (uint8_t)addr, 0x03, 0x00, 0x01, 0x00, 0x01 };
    uint16_t c = crc16(tx, 6);
    tx[6] = c & 0xFF; tx[7] = c >> 8;

    Serial.printf("TX [addr %d] → ", addr);
    for (int i = 0; i < 8; i++) Serial.printf("%02X ", tx[i]);
    Serial.println();

    while (RS485Serial.available()) RS485Serial.read();

    digitalWrite(PIN_RS485_DIR, HIGH);
    RS485Serial.write(tx, 8);
    RS485Serial.flush();
    digitalWrite(PIN_RS485_DIR, LOW);

    // Attendi fino a 300ms raccogliendo tutto
    uint32_t t0 = millis(), last_byte = millis();
    int n = 0;
    while (n < (int)sizeof(rx)) {
        if (RS485Serial.available()) {
            rx[n++] = RS485Serial.read();
            last_byte = millis();
        }
        if (n > 0 && (millis() - last_byte) > 10) break;
        if ((millis() - t0) > 300) break;
    }

    if (n == 0) {
        Serial.println("RX → nessuna risposta (silenzio totale)");
        Serial.println("  Possibili cause: cavo A/B invertito, GND mancante, scheda spenta, addr errato");
    } else {
        Serial.printf("RX %d byte → ", n);
        for (int i = 0; i < n; i++) Serial.printf("%02X ", rx[i]);
        Serial.println();

        // Analisi veloce
        if (rx[0] != (uint8_t)addr)
            Serial.printf("  ATTENZIONE: addr risposta (%02X) != addr richiesto (%02X)\n", rx[0], addr);
        if (n >= 2 && (rx[1] & 0x80))
            Serial.printf("  ERRORE MODBUS: exception code %02X\n", rx[2]);
        if (n >= 2 && rx[1] == 0x03 && n >= 7) {
            uint16_t rx_crc   = rx[n-2] | ((uint16_t)rx[n-1] << 8);
            uint16_t calc_crc = crc16(rx, n - 2);
            Serial.printf("  CRC: ricevuto %04X, calcolato %04X → %s\n",
                rx_crc, calc_crc, rx_crc == calc_crc ? "OK" : "ERRORE CRC");
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────

static void print_help() {
    Serial.println("Comandi:");
    Serial.println("  start                  scansiona addr 1-20 e memorizza i dispositivi trovati");
    Serial.println("  fullscan               scansiona addr 0-255 (tutti gli indirizzi possibili)");
    Serial.println("  view485                avvia visualizzazione stato ogni 10s");
    Serial.println("  stop485                ferma la visualizzazione");
    Serial.println("  ON  [addr]             attiva relay (tutti se addr omesso)");
    Serial.println("  OFF [addr]             disattiva relay (tutti se addr omesso)");
    Serial.println("  rawscan <addr>         debug: mostra byte grezzi TX/RX per quell'addr");
    Serial.println("  rawscan 0              debug: ascolta il bus per 500ms senza trasmettere");
    Serial.println("  help / ?               questo messaggio");
}

// ── Parser comandi ────────────────────────────────────────────────────────────

static void process_cmd(char* s) {
    char* tok = strtok(s, " \t");
    if (!tok) { Serial.print("> "); return; }

    if      (strcasecmp(tok, "start")    == 0) { cmd_start(); }
    else if (strcasecmp(tok, "fullscan") == 0) { cmd_fullscan(); }
    else if (strcasecmp(tok, "view485")  == 0) { cmd_view485(); }
    else if (strcasecmp(tok, "stop485")  == 0) { cmd_stop485(); }
    else if (strcasecmp(tok, "on")       == 0) {
        char* a = strtok(nullptr, " \t");
        cmd_on(a ? atoi(a) : 0);
    }
    else if (strcasecmp(tok, "off")      == 0) {
        char* a = strtok(nullptr, " \t");
        cmd_off(a ? atoi(a) : 0);
    }
    else if (strcasecmp(tok, "rawscan")  == 0) {
        char* a = strtok(nullptr, " \t");
        if (!a) { Serial.println("Uso: rawscan <addr>  oppure  rawscan 0 (listen)"); }
        else    { cmd_rawscan(atoi(a)); }
    }
    else if (strcasecmp(tok, "help") == 0 || strcmp(tok, "?") == 0) {
        print_help();
    }
    else {
        Serial.printf("Comando sconosciuto: '%s'. Digita 'help'.\n", tok);
    }

    if (!viewing) Serial.print("> ");
}

// ── Setup / Loop ──────────────────────────────────────────────────────────────

static char    cmd_buf[64];
static uint8_t cmd_len = 0;

void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(PIN_RS485_DIR, OUTPUT);
    digitalWrite(PIN_RS485_DIR, LOW);
    RS485Serial.begin(MODBUS_BAUD, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);

    Serial.println();
    Serial.println("=========================================");
    Serial.println(" EasyConnect — Modbus RTU Electrostatic ");
    Serial.println("=========================================");
    Serial.printf("RS485: TX=IO%d  RX=IO%d  DIR=IO%d  |  %d 8N1\n",
        PIN_RS485_TX, PIN_RS485_RX, PIN_RS485_DIR, MODBUS_BAUD);
    Serial.println();
    print_help();
    Serial.println();
    Serial.print("> ");
}

void loop() {
    // Lettura seriale con echo
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (cmd_len > 0) {
                cmd_buf[cmd_len] = '\0';
                Serial.println();
                process_cmd(cmd_buf);
                cmd_len = 0;
            }
        } else if (cmd_len < (uint8_t)(sizeof(cmd_buf) - 1)) {
            Serial.print(c);
            cmd_buf[cmd_len++] = c;
        }
    }

    // Polling view485
    if (viewing && (millis() - last_view >= VIEW_INTERVAL_MS)) {
        last_view = millis();
        Serial.println();
        Serial.printf("── [%lus] ──────────────────────────────────\n", millis() / 1000);
        print_all_status();
    }
}

#endif // BOARD_PROFILE_CONTROLLER
