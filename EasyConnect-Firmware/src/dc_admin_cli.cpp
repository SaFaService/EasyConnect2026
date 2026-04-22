#include "dc_admin_cli.h"

#include "dc_controller.h"
#include "dc_data_model.h"
#include "dc_settings.h"
#include "DisplayApi_Manager.h"
#include "rs485_network.h"
#include "wifi_boot.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <mbedtls/sha256.h>
#include <string.h>

static constexpr unsigned long kAdminSessionTimeoutMs = 5UL * 60UL * 1000UL;
static constexpr size_t kCliLineMaxLen = 160;
static constexpr size_t kSha256HexLen = 64;

static String s_cli_line;
static bool s_admin_session = false;
static unsigned long s_last_activity_ms = 0;
static char s_admin_hash[kSha256HexLen + 1] = "";
static bool s_wifi_scan_pending = false;

static bool cli_sha256_hex(const char* input, char* out, size_t out_size) {
    if (!input || !out || out_size < (kSha256HexLen + 1)) return false;

    uint8_t digest[32] = {};
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);

    const int start_rc = mbedtls_sha256_starts(&ctx, 0);
    const int update_rc = (start_rc == 0)
                        ? mbedtls_sha256_update(&ctx,
                                                reinterpret_cast<const unsigned char*>(input),
                                                strlen(input))
                        : -1;
    const int finish_rc = (update_rc == 0) ? mbedtls_sha256_finish(&ctx, digest) : -1;
    mbedtls_sha256_free(&ctx);

    if (start_rc != 0 || update_rc != 0 || finish_rc != 0) {
        out[0] = '\0';
        return false;
    }

    for (size_t i = 0; i < sizeof(digest); i++) {
        snprintf(out + (i * 2), out_size - (i * 2), "%02x", digest[i]);
    }
    out[kSha256HexLen] = '\0';
    return true;
}

static void cli_touch_activity() {
    s_last_activity_ms = millis();
}

static void cli_sync_runtime_serial(const String& serial) {
    String value = serial;
    value.trim();
    if (value.length() == 0 || value == "NON_SET") return;

    value.toCharArray(g_dc_model.device_serial, sizeof(g_dc_model.device_serial));
}

static String cli_default_admin_password() {
    const DisplayApiConfig cfg = displayApiLoadConfig();
    if (cfg.serialNumber.length() > 0 && cfg.serialNumber != "NON_SET") {
        return cfg.serialNumber;
    }

    if (g_dc_model.device_serial[0] != '\0') {
        return String(g_dc_model.device_serial);
    }

    const uint64_t mac = ESP.getEfuseMac();
    char fallback[24];
    snprintf(fallback, sizeof(fallback), "%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
    return String(fallback);
}

static void cli_load_admin_hash() {
    s_admin_hash[0] = '\0';

    Preferences pref;
    if (!pref.begin("easy_sys", true)) return;

    const String hash = pref.getString("adm_pw_hash", "");
    pref.end();
    if (hash.length() != kSha256HexLen) return;

    hash.toCharArray(s_admin_hash, sizeof(s_admin_hash));
}

static bool cli_store_admin_hash(const char* hash) {
    if (!hash || strlen(hash) != kSha256HexLen) return false;

    Preferences pref;
    if (!pref.begin("easy_sys", false)) return false;

    const bool ok = pref.putString("adm_pw_hash", hash) > 0;
    pref.end();
    if (!ok) return false;

    strncpy(s_admin_hash, hash, sizeof(s_admin_hash) - 1);
    s_admin_hash[sizeof(s_admin_hash) - 1] = '\0';
    return true;
}

static bool cli_verify_admin_password(const char* password) {
    if (!password || password[0] == '\0') return false;

    char candidate_hash[kSha256HexLen + 1];
    if (!cli_sha256_hex(password, candidate_hash, sizeof(candidate_hash))) return false;

    if (s_admin_hash[0] != '\0') {
        return strcmp(candidate_hash, s_admin_hash) == 0;
    }

    const String fallback = cli_default_admin_password();
    char fallback_hash[kSha256HexLen + 1];
    if (!cli_sha256_hex(fallback.c_str(), fallback_hash, sizeof(fallback_hash))) return false;
    return strcmp(candidate_hash, fallback_hash) == 0;
}

static const char* cli_access_level_text() {
    return s_admin_session ? "ADMIN" : "USER";
}

static unsigned long cli_admin_ms_left() {
    if (!s_admin_session) return 0;

    const unsigned long now = millis();
    const unsigned long elapsed = now - s_last_activity_ms;
    if (elapsed >= kAdminSessionTimeoutMs) return 0;
    return kAdminSessionTimeoutMs - elapsed;
}

static void cli_logout(const char* reason) {
    const bool was_admin = s_admin_session;
    s_admin_session = false;
    if (was_admin && reason && reason[0] != '\0') {
        Serial.println(reason);
    }
}

static void cli_service_timeout() {
    if (!s_admin_session) return;
    if ((millis() - s_last_activity_ms) < kAdminSessionTimeoutMs) return;
    cli_logout("[ADMIN-CLI] Sessione admin scaduta dopo 5 minuti di inattivita'.");
}

static bool cli_require_admin() {
    if (s_admin_session) return true;
    Serial.println("[ADMIN-CLI] Comando riservato admin. Usa AUTH <password>.");
    return false;
}

static const char* rs485_type_to_text(Rs485DevType type) {
    switch (type) {
        case Rs485DevType::SENSOR: return "SENSOR";
        case Rs485DevType::RELAY:  return "RELAY";
        default:                   return "UNKNOWN";
    }
}

static bool rs485_parse_id_arg(const String& raw, uint8_t& out_id) {
    String value = raw;
    value.trim();
    if (value.length() == 0) return false;

    for (int i = 0; i < (int)value.length(); i++) {
        const char c = value.charAt(i);
        if (c < '0' || c > '9') return false;
    }

    const int id = value.toInt();
    if (id < 1 || id > 200) return false;
    out_id = (uint8_t)id;
    return true;
}

static int rs485_split_csv(const String& src, String* out, int max_items) {
    if (!out || max_items <= 0) return 0;

    int count = 0;
    int start = 0;
    while (count < max_items) {
        const int comma = src.indexOf(',', start);
        if (comma < 0) {
            out[count++] = src.substring(start);
            break;
        }
        out[count++] = src.substring(start, comma);
        start = comma + 1;
    }
    return count;
}

static bool cli_get_value_after_command(const String& line, const String& upper,
                                        const char* command, String& value) {
    const String cmd = String(command);
    if (!upper.startsWith(cmd)) return false;
    if (line.length() <= cmd.length()) return false;

    const char sep = line.charAt(cmd.length());
    if (sep != ' ' && sep != ':' && sep != '=') return false;

    value = line.substring(cmd.length() + 1);
    value.trim();
    return true;
}

static bool cli_split_first_token(const String& src, String& head, String& tail) {
    String value = src;
    value.trim();
    if (value.length() == 0) return false;

    const int space_idx = value.indexOf(' ');
    if (space_idx < 0) {
        head = value;
        tail = "";
        return true;
    }

    head = value.substring(0, space_idx);
    tail = value.substring(space_idx + 1);
    tail.trim();
    return true;
}

static void cli_print_serial() {
    const DisplayApiConfig cfg = displayApiLoadConfig();
    Serial.printf("[DISPLAY] Seriale configurato: %s\n", cfg.serialNumber.c_str());
}

static void cli_print_rs485_scan_status() {
    const Rs485ScanState state = rs485_network_scan_state();
    if (state == Rs485ScanState::RUNNING) {
        Serial.printf("[485-CLI] Scansione in corso: %d/200\n", rs485_network_scan_progress());
        return;
    }
    if (state == Rs485ScanState::DONE) {
        Serial.printf("[485-CLI] Scansione completata. Trovati: %d\n", rs485_network_device_count());
        return;
    }
    Serial.println("[485-CLI] Nessuna scansione attiva.");
}

static void cli_print_status() {
    Serial.println("========== DISPLAY STATUS ==========");
    Serial.printf("Accesso         : %s\n", cli_access_level_text());
    if (s_admin_session) {
        Serial.printf("Admin timeout   : %lus\n", cli_admin_ms_left() / 1000UL);
    }

    Serial.printf("WiFi stato      : %d\n", (int)g_dc_model.wifi.state);
    Serial.printf("WiFi connesso   : %s\n", WiFi.status() == WL_CONNECTED ? "SI" : "NO");
    Serial.printf("WiFi SSID       : %s\n", g_dc_model.wifi.ssid[0] ? g_dc_model.wifi.ssid : "-");
    Serial.printf("WiFi RSSI       : %d\n", g_dc_model.wifi.rssi);
    Serial.printf("RS485 device    : %d\n", rs485_network_device_count());
    Serial.printf("RS485 scan      : %s\n",
                  rs485_network_scan_state() == Rs485ScanState::RUNNING ? "RUNNING" :
                  rs485_network_scan_state() == Rs485ScanState::DONE ? "DONE" : "IDLE");
    if (rs485_network_scan_state() == Rs485ScanState::RUNNING) {
        Serial.printf("Scan progress   : %d/200\n", rs485_network_scan_progress());
    }
    Serial.printf("API busy        : %s\n", displayApiIsBusy() ? "SI" : "NO");
    Serial.printf("Safeguard       : %s\n", g_dc_model.safeguard.active ? "ATTIVO" : "SPENTO");
    Serial.println("====================================");
}

static void cli_print_help() {
    Serial.println();
    Serial.println("========== MENU CLI DISPLAY ==========");
    Serial.printf("Livello attuale : %s\n", cli_access_level_text());
    if (s_admin_session) {
        Serial.printf("Timeout admin   : %lus inattivita'\n", cli_admin_ms_left() / 1000UL);
    } else {
        Serial.println("Sblocco admin   : AUTH <password>");
    }

    Serial.println("Comandi User:");
    Serial.println("  HELP / ?");
    Serial.println("  INFO");
    Serial.println("  STATUS");
    Serial.println("  READSERIAL");
    Serial.println("  APISTATUS");
    Serial.println("  485scan");
    Serial.println("  485list");
    Serial.println("  485status");

    if (s_admin_session) {
        Serial.println("Comandi Admin:");
        Serial.println("  LOGOUT");
        Serial.println("  AUTH <password>");
        Serial.println("  SETSERIAL <sn>");
        Serial.println("  SETAPIURL <url>");
        Serial.println("  SETAPIKEY <key>");
        Serial.println("  SETCUSTURL <url>");
        Serial.println("  SETCUSTKEY <key>");
        Serial.println("  SETADMINPW <old> <new>");
        Serial.println("  485ping <id> / 485info <id>");
        Serial.println("  485view / 485stopview");
        Serial.println("  WIFISCAN");
        Serial.println("  WIFICONNECT <ssid> <pass>");
        Serial.println("  WIFIDISABLE");
        Serial.println("  CALPRESS <addr> <offset>      [pending]");
        Serial.println("  CALTEMP <addr> <offset>       [pending]");
        Serial.println("  SETGROUP <addr> <group>       [pending]");
        Serial.println("  SETRELAYTYPE <addr> <type>    [pending]");
        Serial.println("  OTACHECK / OTASTART           [Task 3.3]");
        Serial.println("  OTACHANNEL <stable|beta>      [Task 3.3]");
        Serial.println("  FACTORYRESET CONFIRM");
    }

    Serial.println("======================================");
    Serial.println();
}

static void cli_print_sensor_details(const String* tok, int count, const String& raw) {
    if (count < 7) {
        Serial.println("[485-CLI] Risposta SENSOR inattesa.");
        Serial.println("[485-CLI] RAW: " + raw + "!");
        return;
    }

    const float p_pa = tok[3].toFloat();
    Serial.printf("  Temperatura: %s C\n", tok[1].c_str());
    Serial.printf("  Umidita': %s %%\n", tok[2].c_str());
    Serial.printf("  Pressione: %.2f Pa (%.2f mbar)\n", p_pa, p_pa / 100.0f);
    Serial.printf("  Sicurezza: %s\n", tok[4].c_str());
    Serial.printf("  Gruppo: %s\n", tok[5].c_str());
    Serial.printf("  Seriale: %s\n", tok[6].c_str());
    if (count > 7) Serial.printf("  FW: %s\n", tok[7].c_str());
}

static void cli_print_relay_details(const String* tok, int count, const String& raw) {
    if (count < 12) {
        Serial.println("[485-CLI] Risposta RELAY inattesa.");
        Serial.println("[485-CLI] RAW: " + raw + "!");
        return;
    }

    Serial.printf("  Modalita': %s\n", tok[2].c_str());
    Serial.printf("  Relay ON: %s\n", tok[3].c_str());
    Serial.printf("  Safety closed: %s\n", tok[4].c_str());
    Serial.printf("  Feedback ok: %s\n", tok[5].c_str());
    Serial.printf("  Avvii relay: %s\n", tok[6].c_str());
    Serial.printf("  Ore ON: %s\n", tok[7].c_str());
    Serial.printf("  Gruppo: %s\n", tok[8].c_str());
    Serial.printf("  Seriale: %s\n", tok[9].c_str());
    Serial.printf("  Stato: %s\n", tok[10].c_str());
    Serial.printf("  FW: %s\n", tok[11].c_str());
}

static void cli_handle_485_ping(const String& arg) {
    uint8_t id = 0;
    if (!rs485_parse_id_arg(arg, id)) {
        Serial.println("[485-CLI] Uso: 485ping <id>  (id 1..200)");
        return;
    }

    String raw;
    const bool ok = rs485_network_ping(id, raw);
    if (!ok) {
        if (raw == "BUSY_SCAN") {
            Serial.println("[485-CLI] Bus occupato da scansione in corso. Riprova a scansione finita.");
        } else if (raw == "BUSY") {
            Serial.println("[485-CLI] Bus RS485 occupato. Riprova.");
        } else if (raw == "ERR,INIT") {
            Serial.println("[485-CLI] RS485 non inizializzato.");
        } else if (raw == "ERR,ADDR") {
            Serial.println("[485-CLI] ID non valido. Range: 1..200.");
        } else {
            Serial.printf("[485-CLI] PING %u -> OFFLINE/NO_RESPONSE\n", id);
            if (raw.length() > 0) {
                Serial.println("[485-CLI] RAW RX: " + raw + "!");
            }
        }
        return;
    }

    Serial.printf("[485-CLI] PING %u -> ONLINE\n", id);
    Serial.println("[485-CLI] RAW: " + raw + "!");
}

static void cli_handle_485_info(const String& arg) {
    uint8_t id = 0;
    if (!rs485_parse_id_arg(arg, id)) {
        Serial.println("[485-CLI] Uso: 485info <id>  (id 1..200)");
        return;
    }

    String raw;
    Rs485Device dev;
    const bool ok = rs485_network_query_device(id, dev, raw);
    if (!ok) {
        if (raw == "BUSY_SCAN") {
            Serial.println("[485-CLI] Bus occupato da scansione in corso. Riprova a scansione finita.");
        } else if (raw == "BUSY") {
            Serial.println("[485-CLI] Bus RS485 occupato. Riprova.");
        } else if (raw == "ERR,INIT") {
            Serial.println("[485-CLI] RS485 non inizializzato.");
        } else if (raw == "ERR,ADDR") {
            Serial.println("[485-CLI] ID non valido. Range: 1..200.");
        } else {
            Serial.printf("[485-CLI] INFO %u -> OFFLINE/NO_RESPONSE\n", id);
            if (raw.length() > 0) {
                Serial.println("[485-CLI] RAW RX: " + raw + "!");
            }
        }
        return;
    }

    Serial.printf("[485-CLI] INFO periferica ID %u\n", id);
    Serial.println("[485-CLI] RAW: " + raw + "!");
    Serial.printf("  Tipo: %s\n", rs485_type_to_text(dev.type));
    Serial.printf("  Seriale: %s\n", dev.sn);
    Serial.printf("  FW: %s\n", dev.version);

    String tok[20];
    const int count = rs485_split_csv(raw, tok, 20);
    if (raw.startsWith("OK,RELAY,")) {
        cli_print_relay_details(tok, count, raw);
    } else if (raw.startsWith("OK,")) {
        cli_print_sensor_details(tok, count, raw);
    }
}

static void cli_handle_485_list() {
    const int count = rs485_network_device_count();
    if (count <= 0) {
        Serial.println("[485-CLI] Nessun device in cache. Eseguire prima: 485scan");
        return;
    }

    Serial.printf("[485-CLI] Dispositivi in cache: %d\n", count);
    for (int i = 0; i < count; i++) {
        Rs485Device dev;
        if (!rs485_network_get_device(i, dev)) continue;
        Serial.printf("  ID=%u TYPE=%s SN=%s FW=%s\n",
                      dev.address, rs485_type_to_text(dev.type), dev.sn, dev.version);
    }
}

static void cli_handle_485_view(bool enable) {
    rs485_network_set_monitor_enabled(enable);
    Serial.printf("[485-CLI] Monitor RS485: %s\n", enable ? "ATTIVO" : "DISATTIVO");
}

static void cli_handle_auth(const String& raw_password) {
    String password = raw_password;
    password.trim();
    if (password.length() == 0) {
        Serial.println("[ADMIN-CLI] Uso: AUTH <password>");
        return;
    }

    if (!cli_verify_admin_password(password.c_str())) {
        Serial.println("[ADMIN-CLI] AUTH fallita.");
        return;
    }

    s_admin_session = true;
    cli_touch_activity();
    Serial.println("[ADMIN-CLI] Sessione admin attiva per 5 minuti di inattivita'.");
}

static void cli_handle_set_admin_password(const String& raw_args) {
    String old_password;
    String new_password;
    if (!cli_split_first_token(raw_args, old_password, new_password) ||
        old_password.length() == 0 || new_password.length() == 0) {
        Serial.println("[ADMIN-CLI] Uso: SETADMINPW <old> <new>");
        return;
    }

    if (new_password.length() < 4) {
        Serial.println("[ADMIN-CLI] Nuova password troppo corta (min 4 caratteri).");
        return;
    }

    if (!cli_verify_admin_password(old_password.c_str())) {
        Serial.println("[ADMIN-CLI] Password corrente non valida.");
        return;
    }

    char hash[kSha256HexLen + 1];
    if (!cli_sha256_hex(new_password.c_str(), hash, sizeof(hash)) || !cli_store_admin_hash(hash)) {
        Serial.println("[ADMIN-CLI] Errore salvataggio password admin in NVS.");
        return;
    }

    Serial.println("[ADMIN-CLI] Password admin aggiornata.");
}

static void cli_handle_wifi_scan() {
    if (s_wifi_scan_pending) {
        Serial.println("[ADMIN-CLI] Scansione WiFi gia' in corso.");
        return;
    }

    if (wifi_boot_is_active()) {
        wifi_boot_abort();
    }

    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    if ((WiFi.getMode() & WIFI_STA) == 0 && !WiFi.mode(WIFI_STA)) {
        Serial.println("[ADMIN-CLI] Impossibile attivare STA per la scansione.");
        return;
    }

    // Bug 5: scansione asincrona — non blocca il main loop
    const int16_t rc = WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/true);
    if (rc == WIFI_SCAN_FAILED) {
        Serial.println("[ADMIN-CLI] Avvio scansione WiFi fallito.");
        return;
    }
    s_wifi_scan_pending = true;
    Serial.println("[ADMIN-CLI] Scansione WiFi avviata. Risultati disponibili a breve...");
}

static void cli_handle_wifi_connect(const String& raw_args) {
    String ssid;
    String pass;
    // Bug 7: pass può essere vuota per reti aperte; split su primo token
    if (!cli_split_first_token(raw_args, ssid, pass) || ssid.length() == 0) {
        Serial.println("[ADMIN-CLI] Uso: WIFICONNECT <ssid> [pass]");
        return;
    }

    if (wifi_boot_is_active()) {
        wifi_boot_abort();
    }

    dc_settings_wifi_set(true, ssid.c_str(), pass.length() > 0 ? pass.c_str() : "");
    if ((WiFi.getMode() & WIFI_STA) == 0) {
        WiFi.mode(WIFI_STA);
    }
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.disconnect(false, false);
    delay(100);
    WiFi.begin(ssid.c_str(), pass.length() > 0 ? pass.c_str() : nullptr);
    // Bug 3: g_dc_model.wifi aggiornato da _update_wifi() al prossimo tick del controller
    Serial.printf("[ADMIN-CLI] Connessione WiFi avviata verso '%s'.\n", ssid.c_str());
}

static void cli_handle_wifi_disable() {
    if (wifi_boot_is_active()) {
        wifi_boot_abort();
    }

    dc_settings_wifi_set(false, "", "");
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    // Bug 3: g_dc_model.wifi.state aggiornato da _update_wifi() al prossimo tick del controller
    Serial.println("[ADMIN-CLI] WiFi disabilitato e credenziali rimosse.");
}

static void cli_handle_factory_reset(const String& upper_line) {
    if (upper_line != "FACTORYRESET CONFIRM") {
        Serial.println("[ADMIN-CLI] Comando distruttivo. Conferma con: FACTORYRESET CONFIRM");
        return;
    }
    Serial.println("[ADMIN-CLI] Factory reset in corso...");
    dc_factory_reset();
}

static void cli_handle_pending_command(const char* command, const char* note) {
    Serial.printf("[ADMIN-CLI] %s non disponibile: %s\n",
                  command ? command : "Comando",
                  note ? note : "funzione non implementata");
}

static void cli_handle_485_command(const String& line, const String& upper) {
    String tail = line.substring(3);
    tail.trim();
    if (tail.startsWith("-")) {
        tail = tail.substring(1);
        tail.trim();
    }
    if (tail.length() == 0) {
        cli_print_help();
        return;
    }

    String cmd;
    String arg;
    if (!cli_split_first_token(tail, cmd, arg)) {
        cli_print_help();
        return;
    }
    cmd.toUpperCase();

    if (cmd == "SCAN") {
        if (rs485_network_scan_state() == Rs485ScanState::RUNNING) {
            cli_print_rs485_scan_status();
        } else {
            dc_scan_rs485();
            Serial.println("[485-CLI] Scansione RS485 avviata (1..200).");
        }
        return;
    }
    if (cmd == "LIST") {
        cli_handle_485_list();
        return;
    }
    if (cmd == "STATUS") {
        cli_print_rs485_scan_status();
        return;
    }

    if (cmd == "PING" || cmd == "PIN") {
        if (!cli_require_admin()) return;
        cli_handle_485_ping(arg);
        return;
    }
    if (cmd == "INFO") {
        if (!cli_require_admin()) return;
        cli_handle_485_info(arg);
        return;
    }
    if (cmd == "VIEW") {
        if (!cli_require_admin()) return;
        cli_handle_485_view(true);
        return;
    }
    if (cmd == "STOPVIEW") {
        if (!cli_require_admin()) return;
        cli_handle_485_view(false);
        return;
    }

    Serial.println("[485-CLI] Comando sconosciuto. Usa HELP.");
}

static void cli_handle_command(String line) {
    line.trim();
    if (line.length() == 0) return;

    cli_touch_activity();

    String upper = line;
    upper.toUpperCase();

    if (upper == "HELP" || upper == "?" || upper == "485" ||
        upper == "485HELP" || upper == "485 HELP" || upper == "485 - HELP") {
        cli_print_help();
        return;
    }

    String value;
    if (cli_get_value_after_command(line, upper, "AUTH", value)) {
        cli_handle_auth(value);
        return;
    }

    if (upper == "LOGOUT") {
        if (!s_admin_session) {
            Serial.println("[ADMIN-CLI] Nessuna sessione admin attiva.");
            return;
        }
        cli_logout("[ADMIN-CLI] Sessione admin chiusa.");
        return;
    }

    if (upper == "STATUS") {
        cli_print_status();
        return;
    }

    if (upper == "INFO" || upper == "APISTATUS") {
        cli_print_status();
        displayApiPrintStatus();
        return;
    }

    if (upper == "READSERIAL") {
        cli_print_serial();
        return;
    }

    if (cli_get_value_after_command(line, upper, "SETSERIAL", value)) {
        if (!cli_require_admin()) return;
        displayApiSetSerialNumber(value);
        cli_sync_runtime_serial(value);
        Serial.println("[DISPLAY-API] Seriale centralina salvato.");
        return;
    }

    if (cli_get_value_after_command(line, upper, "SETAPIURL", value)) {
        if (!cli_require_admin()) return;
        displayApiSetFactoryUrl(value);
        Serial.println("[DISPLAY-API] URL API Antralux salvato.");
        return;
    }

    if (cli_get_value_after_command(line, upper, "SETAPIKEY", value)) {
        if (!cli_require_admin()) return;
        displayApiSetFactoryKey(value);
        Serial.printf("[DISPLAY-API] API Key Antralux salvata (%u caratteri).\n",
                      (unsigned)value.length());
        return;
    }

    if (cli_get_value_after_command(line, upper, "SETCUSTURL", value)) {
        if (!cli_require_admin()) return;
        displayApiSetCustomerUrl(value);
        Serial.println("[DISPLAY-API] URL API utente salvato.");
        return;
    }

    if (cli_get_value_after_command(line, upper, "SETCUSTKEY", value)) {
        if (!cli_require_admin()) return;
        displayApiSetCustomerKey(value);
        Serial.printf("[DISPLAY-API] API Key utente salvata (%u caratteri).\n",
                      (unsigned)value.length());
        return;
    }

    if (cli_get_value_after_command(line, upper, "SETADMINPW", value)) {
        if (!cli_require_admin()) return;
        cli_handle_set_admin_password(value);
        return;
    }

    if (upper == "WIFISCAN") {
        if (!cli_require_admin()) return;
        cli_handle_wifi_scan();
        return;
    }

    if (cli_get_value_after_command(line, upper, "WIFICONNECT", value)) {
        if (!cli_require_admin()) return;
        cli_handle_wifi_connect(value);
        return;
    }

    if (upper == "WIFIDISABLE") {
        if (!cli_require_admin()) return;
        cli_handle_wifi_disable();
        return;
    }

    if (upper == "OTACHECK") {
        if (!cli_require_admin()) return;
        dc_ota_check();
        cli_handle_pending_command("OTACHECK", "completo nel Task 3.3");
        return;
    }

    if (upper == "OTASTART") {
        if (!cli_require_admin()) return;
        dc_ota_start();
        cli_handle_pending_command("OTASTART", "completo nel Task 3.3");
        return;
    }

    if (cli_get_value_after_command(line, upper, "OTACHANNEL", value)) {
        if (!cli_require_admin()) return;
        cli_handle_pending_command("OTACHANNEL", "persistenza canale prevista nel Task 3.3");
        return;
    }

    if (upper == "FACTORYRESET" || upper.startsWith("FACTORYRESET ")) {
        if (!cli_require_admin()) return;
        cli_handle_factory_reset(upper);
        return;
    }

    if (cli_get_value_after_command(line, upper, "CALPRESS", value)) {
        if (!cli_require_admin()) return;
        cli_handle_pending_command("CALPRESS", "persistence/calibration model non ancora introdotto");
        return;
    }
    if (cli_get_value_after_command(line, upper, "CALTEMP", value)) {
        if (!cli_require_admin()) return;
        cli_handle_pending_command("CALTEMP", "persistence/calibration model non ancora introdotto");
        return;
    }
    if (cli_get_value_after_command(line, upper, "SETGROUP", value)) {
        if (!cli_require_admin()) return;
        cli_handle_pending_command("SETGROUP", "assegnazione gruppi da completare in API/config model");
        return;
    }
    if (cli_get_value_after_command(line, upper, "SETRELAYTYPE", value)) {
        if (!cli_require_admin()) return;
        cli_handle_pending_command("SETRELAYTYPE", "tipi relay non persistiti nel display controller");
        return;
    }

    if (upper.startsWith("485")) {
        cli_handle_485_command(line, upper);
        return;
    }

    Serial.println("[ADMIN-CLI] Comando non riconosciuto. Usa HELP.");
}

static bool cli_should_mask_echo(const String& current_line) {
    String upper = current_line;
    upper.toUpperCase();
    return upper.startsWith("AUTH ") ||
           upper.startsWith("AUTH:") ||
           upper.startsWith("AUTH=") ||
           upper.startsWith("SETADMINPW ") ||
           upper.startsWith("SETADMINPW:") ||
           upper.startsWith("SETADMINPW=");
}

void dc_admin_cli_init(void) {
    cli_touch_activity();
    cli_load_admin_hash();
    cli_sync_runtime_serial(displayApiLoadConfig().serialNumber);

    Serial.println("[ADMIN-CLI] CLI seriale pronta. Livello default: USER.");
    if (s_admin_hash[0] != '\0') {
        Serial.println("[ADMIN-CLI] Password admin custom caricata da NVS.");
    } else {
        Serial.println("[ADMIN-CLI] Password admin di default derivata dal seriale centralina.");
    }
    cli_print_help();
}

static void cli_service_wifi_scan(void) {
    if (!s_wifi_scan_pending) return;

    const int16_t result = WiFi.scanComplete();
    if (result == WIFI_SCAN_RUNNING) return;

    s_wifi_scan_pending = false;

    if (result == WIFI_SCAN_FAILED || result < 0) {
        Serial.printf("[ADMIN-CLI] Scansione WiFi fallita (rc=%d).\n", (int)result);
    } else {
        Serial.printf("[ADMIN-CLI] Reti trovate: %d\n", (int)result);
        for (int i = 0; i < result; i++) {
            const bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
            Serial.printf("  %2d. %s  RSSI=%d  %s\n",
                          i + 1,
                          WiFi.SSID(i).c_str(),
                          WiFi.RSSI(i),
                          open ? "OPEN" : "SECURE");
        }
        WiFi.scanDelete();
    }

    // Bug 8: se il WiFi era abilitato, tenta di riconnettersi dopo la scansione
    if (dc_settings_wifi_enabled_get()) {
        dc_wifi_reconnect();
        Serial.println("[ADMIN-CLI] Riconnessione WiFi avviata dopo la scansione.");
    }
}

void dc_admin_cli_service(void) {
    cli_service_timeout();
    cli_service_wifi_scan();

    while (Serial.available()) {
        const char c = (char)Serial.read();
        cli_touch_activity();

        if (c == '\r' || c == '\n') {
            if (s_cli_line.length() > 0) {
                Serial.println();
                const String line = s_cli_line;
                s_cli_line = "";
                cli_handle_command(line);
            }
            continue;
        }

        if (c == '\b' || c == 127) {
            if (s_cli_line.length() > 0) {
                s_cli_line.remove(s_cli_line.length() - 1);
                Serial.print("\b \b");
            }
            continue;
        }

        if (c < 32 || c > 126) {
            continue;
        }

        if (s_cli_line.length() >= kCliLineMaxLen) {
            continue;
        }

        const bool mask_echo = cli_should_mask_echo(s_cli_line);
        s_cli_line += c;
        if (mask_echo) Serial.write('*');
        else Serial.write(c);
    }

    cli_service_timeout();
}
