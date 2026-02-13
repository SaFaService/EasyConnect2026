#include "Serial_Manager.h"
#include "GestioneMemoria.h"
#include <esp_task_wdt.h> // Per il reset del watchdog
#include "Pins.h"
#include <WiFi.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <MD5Builder.h>
#include "OTA_Manager.h" // Per la funzione di download

// La parola chiave 'extern' indica al compilatore che queste variabili sono definite
// in un altro file (in questo caso, in 'main_master.cpp').
extern Impostazioni config;
extern Preferences memoria;
extern const char* FW_VERSION;
extern bool debugViewData;
extern bool debugViewApi;
extern bool manualOtaActive; // Flag from main_master
extern void modoTrasmissione();
extern void modoRicezione();
extern void scansionaSlave();
extern void scansionaSlaveStandalone();
extern void forceWifiOffForLab();
extern void forceWifiOnForLab();

// Helper functions from RS485_Master.cpp
extern String bufferToHex(uint8_t* buff, size_t len);
extern uint8_t calculateChecksum(String &data);

// Static variables for manual OTA state
static int manualOtaTargetId = -1;
static String manualOtaFilePath = "/test_file.bin"; // File per il test
static bool otaMenuActive = false;
static bool otaFileReady = false;
static bool otaSpaceOk = false;
static bool otaEraseOk = false;
static bool otaSendOk = false;
static bool otaVerifyOk = false;
static bool otaNoSpaceLock = false;
static bool otaVerifyFailLock = false;
static bool otaCommitRequested = false;
static String otaExpectedMd5 = "";
static String otaVersionBefore = "";

static String querySlaveResponseById(int id, unsigned long timeoutMs = 400) {
    while (Serial1.available()) Serial1.read();
    modoTrasmissione();
    Serial1.printf("?%d!", id);
    modoRicezione();

    unsigned long t0 = millis();
    while (millis() - t0 < timeoutMs) {
        if (Serial1.available()) {
            return Serial1.readStringUntil('!');
        }
    }
    return "";
}

static String extractSlaveVersion(String payload) {
    int lastComma = payload.lastIndexOf(',');
    if (lastComma == -1 || lastComma >= payload.length() - 1) return "";
    String ver = payload.substring(lastComma + 1);
    ver.trim();
    return ver;
}

// --- FUNZIONE DI ELABORAZIONE COMANDI (Estratta per sicurezza) ---
void processSerialCommand(String cmd) {
    cmd.trim();
    if (cmd.length() == 0) return;

    Serial.print("> Ricevuto: "); Serial.println(cmd);
    String cmdUpper = cmd; 
    cmdUpper.toUpperCase(); 

    // Vincoli operativi in modalita' OTA guidata
    if (manualOtaActive && otaMenuActive && otaNoSpaceLock) {
        bool allowed = (cmdUpper == "HELPOTA" || cmdUpper == "OTA_EXIT" ||
                        cmdUpper.startsWith("OTA_DOWNLOAD ") || cmdUpper == "WIFIOFF" || cmdUpper == "WIFION");
        if (!allowed) {
            Serial.println("[OTA] Blocco: spazio insufficiente.");
            Serial.println("[OTA] Comandi consentiti: OTA_DOWNLOAD <url>, OTA_EXIT, WIFIOFF, WIFION.");
            return;
        }
    }

    if (manualOtaActive && otaMenuActive && otaVerifyFailLock) {
        bool allowed = (cmdUpper == "HELPOTA" || cmdUpper == "OTA_EXIT" ||
                        cmdUpper == "OTA_ERASE" || cmdUpper == "OTA_PREPARE" ||
                        cmdUpper == "WIFIOFF" || cmdUpper == "WIFION");
        if (!allowed) {
            Serial.println("[OTA] Blocco: verifica MD5 fallita.");
            Serial.println("[OTA] Comandi consentiti: OTA_ERASE (o OTA_PREPARE), OTA_EXIT, WIFIOFF, WIFION.");
            return;
        }
    }

    // --- INIZIO BLOCCO COMANDI ---

    if (cmdUpper == "HELP" || cmdUpper == "?") {
        Serial.println("\n=== ELENCO COMANDI MASTER ===");
        Serial.println("INFO             : Visualizza configurazione");
        Serial.println("READSERIAL       : Leggi Seriale");
        Serial.println("READMODE         : Leggi Modo Master");
        Serial.println("READSIC          : Leggi stato Sicurezza");
        Serial.println("READVERSION      : Leggi Versione FW");
        Serial.println("SETSERIAL x      : Imposta SN (es. SETSERIAL AABB)");
        Serial.println("SETMODE x        : 1:Standalone, 2:Rewamping");
        Serial.println("SETSIC ON/OFF    : Sicurezza locale (IO2)");
        Serial.println("SETAPIURL url    : Imposta URL API Antralux");
        Serial.println("SETAPIKEY key    : Imposta API Key Antralux");
        Serial.println("SETCUSTURL url   : Imposta URL API Cliente");
        Serial.println("SETCUSTKEY key   : Imposta API Key Cliente");
        Serial.println("SETSLAVEGRP id g : Cambia gruppo a uno slave (es. SETSLAVEGRP 5 2)");
        Serial.println("SCAN485          : Forza scansione manuale periferiche RS485");
        Serial.println("PING485 id       : Ping RS485 su singolo slave (es. PING485 2)");
        Serial.println("PING485RAW id    : Ping RS485 con dump byte grezzo/HEX della risposta");
        Serial.println("WIFIOFF          : Disabilita il WiFi (fase test laboratorio)");
        Serial.println("WIFION           : Riabilita WiFi/AP da configurazione salvata");
        Serial.println("VIEWDATA         : Abilita visualizzazione dati RS485");
        Serial.println("STOPDATA         : Disabilita visualizzazione dati RS485");
        Serial.println("VIEWAPI          : Abilita log invio dati al server");
        Serial.println("STOPAPI          : Disabilita log invio dati al server");
        Serial.println("REBOOT           : Riavvia la scheda");
        Serial.println("CLEARMEM         : Reset Fabbrica");
        Serial.println("=============================\n");
    }
    else if (cmdUpper == "HELPOTA") {
        Serial.println("\n=== MENU OTA VIA SERIALE ===");
        Serial.println("1. OTA_MODE <ID>        : Entra in modalita' OTA (richiede WiFi connesso).");
        Serial.println("2. OTA_DOWNLOAD <URL>   : Scarica il file .bin sulla Master.");
        Serial.println("   Opzionale: WIFIOFF / WIFION dopo il download.");
        Serial.println("3. OTA_CHECK_SPACE      : Verifica spazio disponibile sulla Slave.");
        Serial.println("4. OTA_ERASE            : Cancella partizione OTA della Slave.");
        Serial.println("5. OTA_SEND             : Trasferisce il file .bin alla Slave.");
        Serial.println("6. OTA_VERIFY           : Verifica MD5 del file trasferito.");
        Serial.println("7. OTA_COMMIT           : Avvia aggiornamento e riavvio Slave.");
        Serial.println("8. OTA_RESULT           : Controlla esito (risposta/versione Slave).");
        Serial.println("9. OTA_EXIT             : Esce dalla modalita' OTA.");
        Serial.println("============================\n");
    }
    else if (cmdUpper == "HELPTEST") {
        Serial.println("\n=== MENU TEST TRASFERIMENTO FILE ===");
        Serial.println("Menu test progressivo trasferimento file via RS485.");
        Serial.println("ATTENZIONE: sospende polling RS485 e invio API.");
        Serial.println("---");
        Serial.println("1. TEST_MODE <ID>       : Entra in modalita' test (richiede WiFi connesso).");
        Serial.println("2. TEST_DOWNLOAD <URL>  : Scarica file (es. link Google Drive diretto).");
        Serial.println("   Opzionale dopo download: WIFIOFF (oppure WIFION).");
        Serial.println("3. TEST_CHECK_SPACE     : Chiede allo slave se c'e' spazio per il file.");
        Serial.println("4. TEST_ERASE           : Erase partizione OTA slave (opzionale).");
        Serial.println("5. TEST_SEND            : Invia file test allo slave (/test_recv.bin).");
        Serial.println("6. TEST_VERIFY          : Verifica MD5 del file ricevuto sullo slave.");
        Serial.println("7. TEST_DELETE          : Cancella file test sullo slave (opzionale).");
        Serial.println("8. TEST_EXIT            : Esce dalla modalita' test.");
        Serial.println("--- TEST SU PARTIZIONE OTA (Aggiornamento Reale) ---");
        Serial.println("OTA_PREPARE             : Prepara lo slave per l'aggiornamento (erase partizione).");
        Serial.println("OTA_SEND                : Invia il file scaricato alla partizione OTA dello slave.");
        Serial.println("OTA_COMMIT              : Finalizza l'aggiornamento e riavvia lo slave.");
        Serial.println("==============================\n");
    }
    else if (cmdUpper == "INFO") {
        Serial.println("\n--- STATO ATTUALE MASTER ---");
        Serial.printf("Configurato : %s\n", config.configurata ? "SI" : "NO");
        Serial.printf("Seriale     : %s\n", config.serialeID);
        Serial.printf("Modo        : %s\n", config.modalitaMaster == 2 ? "REWAMPING" : "STANDALONE");
        Serial.printf("Sicurezza   : %s\n", config.usaSicurezzaLocale ? "ATTIVA (IO2)" : "DISABILITATA");
        Serial.printf("Versione FW : %s\n", FW_VERSION);
        Serial.printf("URL API Antralux : %s\n", config.apiUrl);
        Serial.printf("URL API Cliente  : %s\n", config.customerApiUrl);
        Serial.printf("API Key Antralux : %s\n", String(config.apiKey).length() > 0 ? "Impostata" : "NON IMPOSTATA");
        Serial.printf("API Key Cliente  : %s\n", String(config.customerApiKey).length() > 0 ? "Impostata" : "NON IMPOSTATA");
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("Rete WiFi   : %s\n", WiFi.SSID().c_str());
            Serial.printf("Indirizzo IP: %s\n", WiFi.localIP().toString().c_str());
        } else {
            Serial.println("Rete WiFi   : DISCONNESSO");
        }
        Serial.println("----------------------------\n");
    }
    // Blocco di comandi 'READ' per leggere la configurazione attuale.
    else if (cmdUpper == "READSERIAL") {
        Serial.printf("Seriale: %s\n", config.serialeID);
    }
    else if (cmdUpper == "READMODE") {
        Serial.printf("Modo: %s\n", config.modalitaMaster == 2 ? "REWAMPING" : "STANDALONE");
    }
    else if (cmdUpper == "READSIC") {
        Serial.printf("Sicurezza: %s\n", config.usaSicurezzaLocale ? "ATTIVA (IO2)" : "DISABILITATA");
    }
    else if (cmdUpper == "READVERSION") {
        Serial.printf("Versione FW: %s\n", FW_VERSION);
    }
    // Blocco di comandi 'SET' per configurare la scheda.
    else if (cmdUpper.startsWith("SETSERIAL ") || cmdUpper.startsWith("SETSERIAL:")) {
        String s = cmd.substring(10); s.trim();
        s.toCharArray(config.serialeID, 32);
        memoria.putString("serialeID", config.serialeID);
        Serial.println("OK: Seriale Salvato");
        
        if (String(config.serialeID) != "NON_SET") {
                config.configurata = true;
                memoria.putBool("set", true);
                Serial.println("Configurazione Completa! (Configuration Complete!)");
        }
    }
    else if (cmdUpper.startsWith("SETMODE ") || cmdUpper.startsWith("SETMODE:")) {
        String val = cmd.substring(8); val.trim();
        config.modalitaMaster = val.toInt();
        memoria.putInt("m_mode", config.modalitaMaster);
        Serial.println("OK: Modo Salvato");
    }
    else if (cmdUpper.startsWith("SETSIC ") || cmdUpper.startsWith("SETSIC:")) {
        String val = cmdUpper.substring(7); val.trim();
        config.usaSicurezzaLocale = (val == "ON");
        memoria.putBool("m_sic", config.usaSicurezzaLocale);
        Serial.println("OK: Sicurezza Salvata");
    }
    else if (cmdUpper.startsWith("SETAPIURL ")) {
        String val = cmd.substring(10); val.trim();
        val.toCharArray(config.apiUrl, 128);
        memoria.putString("api_url", val);
        Serial.println("OK: URL API Antralux salvato.");
    }
    else if (cmdUpper.startsWith("SETAPIKEY ")) {
        String val = cmd.substring(10); val.trim();
        val.toCharArray(config.apiKey, 65);
        memoria.putString("apiKey", val);
        Serial.println("OK: API Key Antralux salvata.");
    }
    else if (cmdUpper.startsWith("SETCUSTURL ")) {
        String val = cmd.substring(11); val.trim();
        val.toCharArray(config.customerApiUrl, 128);
        memoria.putString("custApiUrl", val);
        Serial.println("OK: URL API Cliente salvato.");
    }
    else if (cmdUpper.startsWith("SETCUSTKEY ")) {
        String val = cmd.substring(11); val.trim();
        val.toCharArray(config.customerApiKey, 65);
        memoria.putString("custApiKey", val);
        Serial.println("OK: API Key Cliente salvata.");
    }
    // Comando speciale per configurare uno slave da remoto.
    else if (cmdUpper.startsWith("SETSLAVEGRP ")) {
        int primoSpazio = cmdUpper.indexOf(' ', 12);
        if (primoSpazio > 0) {
            String idStr = cmdUpper.substring(12, primoSpazio);
            String grpStr = cmdUpper.substring(primoSpazio + 1);
            int id = idStr.toInt();
            int grp = grpStr.toInt();
            
            if (id > 0 && grp > 0) {
                modoTrasmissione(); // Attiva la modalità di trasmissione RS485.
                Serial1.printf("GRP%d:%d!", id, grp);
                modoRicezione();
                Serial.printf("Inviato comando cambio gruppo a Slave %d -> Gruppo %d\n", id, grp);
            } else {
                Serial.println("Errore parametri. Uso: SETSLAVEGRP <ID> <GRP>");
            }
        }
    }
    else if (cmdUpper == "SCAN485") {
        if (manualOtaActive) {
            Serial.println("[SCAN] Errore: disabilitato durante modalita TEST/OTA manuale.");
            return;
        }

        Serial.println("[SCAN] Avvio scansione manuale richiesta da seriale...");
        bool prevDebug = debugViewData;
        debugViewData = true; // forza log TX/RX solo durante la scansione manuale
        if (config.modalitaMaster == 1) {
            scansionaSlaveStandalone();
        } else {
            scansionaSlave();
        }
        debugViewData = prevDebug;
        Serial.println("[SCAN] Scansione manuale completata.");
    }
    else if (cmdUpper.startsWith("PING485 ")) {
        String idStr = cmd.substring(8);
        idStr.trim();
        int id = idStr.toInt();
        if (id <= 0 || id > 100) {
            Serial.println("[PING485] Errore: ID non valido. Uso: PING485 <id>");
            return;
        }

        while (Serial1.available()) Serial1.read();

        unsigned long t0 = millis();
        Serial.printf("[PING485] TX -> ?%d!\n", id);
        modoTrasmissione();
        Serial1.printf("?%d!", id);
        modoRicezione();

        bool gotResponse = false;
        while (millis() - t0 < 200) {
            if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                unsigned long dt = millis() - t0;
                Serial.printf("[PING485] RX <- %s! (%lums)\n", resp.c_str(), dt);
                if (resp.startsWith("OK")) {
                    Serial.println("[PING485] ESITO: OK");
                } else {
                    Serial.println("[PING485] ESITO: Risposta non valida");
                }
                gotResponse = true;
                break;
            }
        }

        if (!gotResponse) {
            Serial.println("[PING485] Timeout: nessuna risposta.");
        }
    }
    else if (cmdUpper.startsWith("PING485RAW ")) {
        String idStr = cmd.substring(11);
        idStr.trim();
        int id = idStr.toInt();
        if (id <= 0 || id > 100) {
            Serial.println("[PING485RAW] Errore: ID non valido. Uso: PING485RAW <id>");
            return;
        }

        while (Serial1.available()) Serial1.read();

        String tx = "?" + String(id) + "!";
        Serial.printf("[PING485RAW] TX ASCII: %s\n", tx.c_str());
        Serial.print("[PING485RAW] TX HEX  : ");
        for (int i = 0; i < tx.length(); i++) {
            Serial.printf("%02X ", (uint8_t)tx[i]);
        }
        Serial.println();

        modoTrasmissione();
        Serial1.print(tx);
        modoRicezione();

        uint8_t rxBuf[256];
        size_t rxLen = 0;
        bool frameDone = false;
        unsigned long t0 = millis();

        while ((millis() - t0) < 250 && rxLen < sizeof(rxBuf)) {
            while (Serial1.available() && rxLen < sizeof(rxBuf)) {
                uint8_t b = (uint8_t)Serial1.read();
                rxBuf[rxLen++] = b;
                if (b == '!') {
                    frameDone = true;
                    break;
                }
            }
            if (frameDone) break;
        }

        if (rxLen == 0) {
            Serial.println("[PING485RAW] Timeout: nessun byte ricevuto.");
            return;
        }

        Serial.printf("[PING485RAW] RX LEN  : %u byte\n", (unsigned)rxLen);
        Serial.print("[PING485RAW] RX HEX  : ");
        for (size_t i = 0; i < rxLen; i++) {
            Serial.printf("%02X ", rxBuf[i]);
        }
        Serial.println();

        Serial.print("[PING485RAW] RX ASCII: ");
        for (size_t i = 0; i < rxLen; i++) {
            char c = (char)rxBuf[i];
            if (c >= 32 && c <= 126) Serial.print(c);
            else Serial.print('.');
        }
        Serial.println();
    }
    else if (cmdUpper == "WIFIOFF") {
        Serial.println("[WIFI] Disabilitazione modulo WiFi...");
        forceWifiOffForLab();
        delay(100);
        Serial.println("[WIFI] WiFi OFF.");
    }
    else if (cmdUpper == "WIFION") {
        Serial.println("[WIFI] Riattivazione WiFi/AP...");
        forceWifiOnForLab();
        Serial.println("[WIFI] Richiesta completata. Attendere connessione...");
    }
    else if (cmdUpper.startsWith("OTA_MODE ")) {
        if (manualOtaActive) {
            Serial.println("[OTA] Errore: procedura TEST/OTA gia' attiva. Usa TEST_EXIT/OTA_EXIT.");
            return;
        }
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[OTA] Errore: WiFi non connesso.");
            Serial.println("[OTA] Usa WIFION, attendi connessione, poi riprova.");
            return;
        }
        String idStr = cmd.substring(9);
        idStr.trim();
        manualOtaTargetId = idStr.toInt();
        if (manualOtaTargetId <= 0 || manualOtaTargetId > 100) {
            Serial.println("[OTA] Errore: ID slave non valido.");
            manualOtaTargetId = -1;
            return;
        }

        manualOtaActive = true;
        otaMenuActive = true;
        otaFileReady = false;
        otaSpaceOk = false;
        otaEraseOk = false;
        otaSendOk = false;
        otaVerifyOk = false;
        otaNoSpaceLock = false;
        otaVerifyFailLock = false;
        otaCommitRequested = false;
        otaExpectedMd5 = "";
        otaVersionBefore = "";

        String resp = querySlaveResponseById(manualOtaTargetId, 500);
        if (resp.startsWith("OK,")) {
            otaVersionBefore = extractSlaveVersion(resp);
            Serial.printf("[OTA] Versione attuale Slave %d: %s\n", manualOtaTargetId, otaVersionBefore.c_str());
        } else {
            Serial.printf("[OTA] Warning: slave %d non risponde al ping iniziale.\n", manualOtaTargetId);
        }
        Serial.printf("[OTA] Modalita' OTA attiva. Target Slave ID: %d\n", manualOtaTargetId);
        Serial.println("[OTA] Polling RS485 e API sospesi.");
    }
    else if (cmdUpper.startsWith("OTA_DOWNLOAD ")) {
        if (!manualOtaActive || !otaMenuActive || manualOtaTargetId == -1) {
            Serial.println("[OTA] Errore: eseguire prima OTA_MODE <id>.");
            return;
        }
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[OTA] Errore: WiFi non connesso, download non possibile.");
            Serial.println("[OTA] Usa WIFION e riprova.");
            return;
        }

        String url = cmd.substring(13);
        url.trim();
        if (url.length() < 10) {
            Serial.println("[OTA] Errore: URL non valido.");
            return;
        }

        Serial.println("[OTA] Download file OTA in corso...");
        if (downloadSlaveFirmware(url, manualOtaFilePath)) {
            File f = SPIFFS.open(manualOtaFilePath, "r");
            if (!f) {
                Serial.println("[OTA] Errore: file scaricato non leggibile.");
                otaFileReady = false;
                return;
            }
            MD5Builder md5;
            md5.begin();
            md5.addStream(f, f.size());
            md5.calculate();
            otaExpectedMd5 = md5.toString();
            otaExpectedMd5.toUpperCase();
            size_t fileSize = f.size();
            f.close();

            otaFileReady = true;
            otaSpaceOk = false;
            otaEraseOk = false;
            otaSendOk = false;
            otaVerifyOk = false;
            otaNoSpaceLock = false;
            otaVerifyFailLock = false;
            otaCommitRequested = false;

            Serial.printf("[OTA] Download OK. Size=%u bytes (%.1f KB)\n", fileSize, fileSize / 1024.0f);
            Serial.println("[OTA] MD5 Locale: " + otaExpectedMd5);
        } else {
            Serial.println("[OTA] ERRORE: Download fallito.");
            otaFileReady = false;
        }
    }
    else if (cmdUpper == "OTA_CHECK_SPACE") {
        if (!manualOtaActive || !otaMenuActive || manualOtaTargetId == -1) {
            Serial.println("[OTA] Errore: eseguire prima OTA_MODE <id>.");
            return;
        }
        if (!otaFileReady || !SPIFFS.exists(manualOtaFilePath)) {
            Serial.println("[OTA] Errore: file locale mancante. Usa OTA_DOWNLOAD <url>.");
            return;
        }

        File f = SPIFFS.open(manualOtaFilePath, "r");
        size_t fileSize = f.size();
        f.close();

        while (Serial1.available()) Serial1.read();
        Serial.printf("[OTA] Verifica spazio su Slave %d (richiesti %u bytes)...\n", manualOtaTargetId, fileSize);
        modoTrasmissione();
        Serial1.printf("TEST,SPACE,%d,%u!", manualOtaTargetId, fileSize);
        modoRicezione();

        unsigned long startWait = millis();
        bool received = false;
        while (millis() - startWait < 2500) {
            if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                Serial.println("[OTA] Risposta Slave: " + resp);
                String prefixOk = "OK,TEST,SPACE," + String(manualOtaTargetId) + ",OK,";
                String prefixNo = "OK,TEST,SPACE," + String(manualOtaTargetId) + ",NO,";
                String prefixFail = "OK,TEST,SPACE," + String(manualOtaTargetId) + ",FAIL,";
                if (resp.startsWith(prefixOk)) {
                    otaSpaceOk = true;
                    otaNoSpaceLock = false;
                    Serial.println("[OTA] Spazio conforme. Puoi procedere con OTA_ERASE.");
                } else if (resp.startsWith(prefixNo) || resp.startsWith(prefixFail)) {
                    otaSpaceOk = false;
                    otaNoSpaceLock = true;
                    otaEraseOk = false;
                    otaSendOk = false;
                    otaVerifyOk = false;
                    if (SPIFFS.exists(manualOtaFilePath)) {
                        SPIFFS.remove(manualOtaFilePath);
                    }
                    otaFileReady = false;
                    Serial.println("[OTA] Spazio NON conforme. File locale cancellato automaticamente.");
                    Serial.println("[OTA] Comandi disponibili: OTA_DOWNLOAD <url> oppure OTA_EXIT.");
                } else {
                    Serial.println("[OTA] Risposta inattesa.");
                }
                received = true;
                break;
            }
        }
        if (!received) Serial.println("[OTA] Timeout verifica spazio.");
    }
    // Comandi per il debug.
    else if (cmdUpper == "VIEWDATA") {
        debugViewData = true;
        Serial.println("Visualizzazione Dati RS485: ATTIVA");
    }
    else if (cmdUpper == "STOPDATA") {
        debugViewData = false;
        Serial.println("Visualizzazione Dati RS485: DISATTIVA");
    }
    else if (cmdUpper == "VIEWAPI") {
        debugViewApi = true;
        Serial.println("Visualizzazione Log API: ATTIVA");
    }
    else if (cmdUpper == "STOPAPI") {
        debugViewApi = false;
        Serial.println("Visualizzazione Log API: DISATTIVA");
    }
    // --- COMANDI TEST/OTA MANUALE ---
    else if (cmdUpper.startsWith("TEST_MODE ")) {
        if (manualOtaActive) {
            Serial.println("[TEST] Errore: Procedura già attiva. Usare TEST_EXIT prima.");
            return;
        }
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[TEST] Errore: WiFi non connesso.");
            Serial.println("[TEST] Senza WiFi non puoi scaricare il file.");
            Serial.println("[TEST] Usa WIFION, attendi la connessione e riprova.");
            return;
        }
        String idStr = cmd.substring(10);
        manualOtaTargetId = idStr.toInt();
        if (manualOtaTargetId > 0 && manualOtaTargetId <= 100) {
            manualOtaActive = true;
            otaMenuActive = false;
            Serial.printf("[TEST] Sistema in modalità TEST.\n");
            Serial.printf("[TEST] Target impostato su Slave ID: %d\n", manualOtaTargetId);
            Serial.println("[TEST] Il polling RS485 e le chiamate API sono sospese.");
        } else {
            Serial.println("[TEST] Errore: ID Slave non valido.");
            manualOtaTargetId = -1;
        }
    }
    else if (cmdUpper.startsWith("TEST_DOWNLOAD ")) {
        if (!manualOtaActive) {
            Serial.println("[TEST] Errore: Eseguire prima TEST_MODE <id>.");
            return;
        }
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[TEST] Errore: WiFi non connesso, download non possibile.");
            Serial.println("[TEST] Usa WIFION e riprova.");
            return;
        }
        String url = cmd.substring(14);
        url.trim();
        if (url.length() < 10) {
            Serial.println("[TEST] Errore: URL non valido.");
            return;
        }

        Serial.println("[TEST] Avvio download del file di test...");
        // Scarica su /test_file.bin
        if (downloadSlaveFirmware(url, manualOtaFilePath)) {
            Serial.println("[TEST] Download completato con successo.");
        } else {
            Serial.println("[TEST] ERRORE: Download fallito.");
        }
    }
    else if (cmdUpper == "TEST_WIFI_OFF") {
        if (!manualOtaActive) {
            Serial.println("[TEST] Errore: Eseguire prima TEST_MODE <id>.");
            return;
        }
        Serial.println("[TEST] Comando legacy: usa WIFIOFF/WIFION dal menu principale.");
        Serial.println("[TEST] Disabilitazione del modulo WiFi...");
        forceWifiOffForLab();
        delay(100); // Piccola pausa per assicurarsi che sia spento
        Serial.println("[TEST] WiFi disabilitato. Usa WIFION per riattivarlo.");
    }
    else if (cmdUpper == "TEST_CHECK_SPACE") {
        if (!manualOtaActive || manualOtaTargetId == -1) {
            Serial.println("[TEST] Errore: Eseguire prima TEST_MODE.");
            return;
        }
        if (!SPIFFS.exists(manualOtaFilePath)) {
            Serial.println("[TEST] Errore: File non trovato. Eseguire prima TEST_DOWNLOAD.");
            return;
        }

        File f = SPIFFS.open(manualOtaFilePath, "r");
        size_t fileSize = f.size();
        f.close();

        Serial.printf("[TEST] Verifica spazio OTA su Slave %d (richiesti %u bytes)...\n", manualOtaTargetId, fileSize);
        
        // Svuota buffer RX prima di trasmettere per evitare letture sporche
        while(Serial1.available()) Serial1.read();
        
        modoTrasmissione();
        Serial1.printf("TEST,SPACE,%d,%u!", manualOtaTargetId, fileSize);
        modoRicezione();

        unsigned long startWait = millis();
        bool received = false;
        while(millis() - startWait < 2000) {
            if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                Serial.println("[TEST] Risposta Slave: " + resp);

                String prefixOk = "OK,TEST,SPACE," + String(manualOtaTargetId) + ",OK,";
                String prefixNo = "OK,TEST,SPACE," + String(manualOtaTargetId) + ",NO,";
                String prefixFail = "OK,TEST,SPACE," + String(manualOtaTargetId) + ",FAIL,";
                if (resp.startsWith(prefixOk)) {
                    int cLast = resp.lastIndexOf(',');
                    int cPrev = resp.lastIndexOf(',', cLast - 1);
                    if (cPrev > 0 && cLast > cPrev) {
                        uint32_t req = (uint32_t)resp.substring(cPrev + 1, cLast).toInt();
                        uint32_t max = (uint32_t)resp.substring(cLast + 1).toInt();
                        Serial.printf("[TEST] Spazio OTA: SI. Richiesti=%u B (%.1f KB), Max=%u B (%.1f KB)\n",
                                      req, req / 1024.0f, max, max / 1024.0f);
                    } else {
                        Serial.println("[TEST] Spazio OTA: SI.");
                    }
                    received = true;
                } else if (resp.startsWith(prefixNo)) {
                    int cLast = resp.lastIndexOf(',');
                    int cPrev = resp.lastIndexOf(',', cLast - 1);
                    if (cPrev > 0 && cLast > cPrev) {
                        uint32_t req = (uint32_t)resp.substring(cPrev + 1, cLast).toInt();
                        uint32_t max = (uint32_t)resp.substring(cLast + 1).toInt();
                        Serial.printf("[TEST] Spazio OTA: NO. Richiesti=%u B (%.1f KB), Max=%u B (%.1f KB)\n",
                                      req, req / 1024.0f, max, max / 1024.0f);
                    } else {
                        Serial.println("[TEST] Spazio OTA: NO.");
                    }
                    received = true;
                } else if (resp.startsWith(prefixFail)) {
                    Serial.println("[TEST] Spazio OTA: ERRORE interno slave.");
                    received = true;
                }
                break;
            }
        }
        if (!received) Serial.println("[TEST] Timeout: Nessuna risposta dallo slave.");
    }
    else if (cmdUpper == "TEST_ERASE") {
        if (!manualOtaActive || manualOtaTargetId == -1) {
            Serial.println("[TEST] Errore: Eseguire prima TEST_MODE.");
            return;
        }
        if (!SPIFFS.exists(manualOtaFilePath)) {
            Serial.println("[TEST] Errore: File non trovato. Eseguire prima TEST_DOWNLOAD.");
            return;
        }

        File f = SPIFFS.open(manualOtaFilePath, "r");
        size_t fileSize = f.size();
        f.close();

        while (Serial1.available()) Serial1.read();
        Serial.printf("[TEST] Richiesta erase partizione OTA su Slave %d (size=%u)...\n", manualOtaTargetId, fileSize);
        modoTrasmissione();
        Serial1.printf("TEST,ERASE,%d,%u!", manualOtaTargetId, fileSize);
        modoRicezione();

        unsigned long startWait = millis();
        bool received = false;
        while (millis() - startWait < 5000) {
            if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                Serial.println("[TEST] Risposta Slave: " + resp);
                if (resp.indexOf("OK,TEST,ERASE," + String(manualOtaTargetId) + ",OK") != -1) {
                    Serial.println("[TEST] Erase completato.");
                } else if (resp.indexOf("OK,TEST,ERASE," + String(manualOtaTargetId) + ",NO") != -1) {
                    Serial.println("[TEST] Erase non eseguito: spazio insufficiente.");
                } else {
                    Serial.println("[TEST] Erase fallito o risposta inattesa.");
                }
                received = true;
                break;
            }
        }
        if (!received) Serial.println("[TEST] Timeout: nessuna risposta a TEST_ERASE.");
    }
    else if (cmdUpper == "OTA_PREPARE" || cmdUpper == "OTA_ERASE") {
        if (!manualOtaActive || !otaMenuActive || manualOtaTargetId == -1) {
            Serial.println("[OTA] Errore: eseguire prima OTA_MODE <id>.");
            return;
        }
        if (!otaFileReady || !SPIFFS.exists(manualOtaFilePath)) {
            Serial.println("[OTA] Errore: file non trovato. Eseguire prima OTA_DOWNLOAD.");
            return;
        }
        if (!otaSpaceOk) {
            Serial.println("[OTA] Errore: eseguire prima OTA_CHECK_SPACE con esito positivo.");
            return;
        }
        File f = SPIFFS.open(manualOtaFilePath, "r");
        size_t fileSize = f.size();
        f.close();

        while (Serial1.available()) Serial1.read();
        Serial.printf("[OTA] Erase partizione OTA su Slave %d (size=%u)...\n", manualOtaTargetId, fileSize);
        modoTrasmissione();
        Serial1.printf("TEST,ERASE,%d,%u!", manualOtaTargetId, fileSize);
        modoRicezione();

        unsigned long startWait = millis();
        bool received = false;
        while (millis() - startWait < 8000) {
            if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                Serial.println("[OTA] Risposta slave: " + resp);
                if (resp.indexOf("OK,TEST,ERASE," + String(manualOtaTargetId) + ",OK") != -1) {
                    otaEraseOk = true;
                    otaSendOk = false;
                    otaVerifyOk = false;
                    otaVerifyFailLock = false;
                    Serial.println("[OTA] Erase completato. Puoi procedere con OTA_SEND.");
                } else {
                    otaEraseOk = false;
                    Serial.println("[OTA] Erase fallito/non confermato.");
                }
                received = true;
                break;
            }
        }
        if (!received) Serial.println("[OTA] Timeout su OTA_ERASE.");
    }
    else if (cmdUpper == "TEST_SEND") {
        if (!manualOtaActive || manualOtaTargetId == -1) {
            Serial.println("[TEST] Errore: Eseguire prima TEST_MODE.");
            return;
        }
        if (!SPIFFS.exists(manualOtaFilePath)) {
            Serial.println("[TEST] Errore: File non trovato. Eseguire prima TEST_DOWNLOAD.");
            return;
        }
        File f = SPIFFS.open(manualOtaFilePath, "r");
        size_t fileSize = f.size();

        Serial.println("[TEST] Calcolo MD5 del file locale...");
        MD5Builder md5;
        md5.begin();
        md5.addStream(f, fileSize);
        md5.calculate();
        String md5Str = md5.toString();
        md5Str.toUpperCase();
        Serial.println("[TEST] MD5 Locale: " + md5Str);
        
        // 1. START
        // Svuota buffer RX
        while(Serial1.available()) Serial1.read();

        Serial.printf("[TEST] Invio START a Slave %d (Size: %u)...\n", manualOtaTargetId, fileSize);
        modoTrasmissione();
        Serial1.printf("TEST,START,%d,%u,%s!", manualOtaTargetId, fileSize, md5Str.c_str());
        modoRicezione();
        
        // Attesa READY
        unsigned long startWait = millis();
        bool ready = false;
        while(millis() - startWait < 5000) {
            if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                if (resp.indexOf("OK,TEST,READY," + String(manualOtaTargetId)) != -1) {
                    ready = true;
                    Serial.println("[TEST] Slave PRONTO.");
                } else {
                    Serial.println("[TEST] Risposta inattesa: " + resp);
                }
                break;
            }
        }
        
        if (!ready) {
            Serial.println("[TEST] Errore: Slave non pronto.");
            f.close();
            return;
        }

        // 2. TRANSFER LOOP
        size_t offset = 0;
        const int CHUNK_SIZE = 128;
        uint8_t buff[CHUNK_SIZE];

        // NUOVA LOGICA TIMEOUT: Timeout totale di 5 minuti, resettato ad ogni progresso.
        const unsigned long TOTAL_TRANSFER_TIMEOUT = 300000; // 5 minuti
        unsigned long lastProgressTime = millis();

        Serial.printf("[TEST] Avvio trasferimento...\n");

        while(offset < fileSize) {
            esp_task_wdt_reset(); // Previene il watchdog su trasferimenti lunghi

            if (millis() - lastProgressTime > TOTAL_TRANSFER_TIMEOUT) {
                Serial.println("\n[TEST] ERRORE: Timeout totale superato.");
                f.close();
                return;
            }

            f.seek(offset);
            size_t bytesRead = f.read(buff, CHUNK_SIZE);
            if (bytesRead == 0) {
                Serial.println("[TEST] ERRORE: Lettura file locale fallita.");
                f.close();
                return;
            }

            bool acked = false;
            // Svuota buffer RX
            while(Serial1.available()) Serial1.read();

            for (int retry = 0; retry < 5; retry++) {
                String hexData = bufferToHex(buff, bytesRead);
                uint8_t checksum = calculateChecksum(hexData);
                
                Serial.printf("[TEST] Invio chunk offset %u, tentativo %d/5...\n", offset, retry + 1);
                modoTrasmissione();
                Serial1.printf("TEST,DATA,%d,%u,%s,%02X!", manualOtaTargetId, offset, hexData.c_str(), checksum);
                modoRicezione();

                unsigned long startWait = millis();
                while(millis() - startWait < 2500) { // 2.5s timeout per ACK
                    esp_task_wdt_reset(); // Previene il watchdog durante l'attesa dell'ACK
                    if (Serial1.available()) {
                        String resp = Serial1.readStringUntil('!');
                        // Tolleranza al rumore iniziale
                        if (resp.indexOf("OK,TEST,ACK," + String(manualOtaTargetId) + "," + String(offset)) != -1) {
                            Serial.println("[TEST] ...ACK ricevuto.");
                            acked = true;
                            break;
                        }
                    }
                }
                if (acked) break;
                Serial.println("[TEST] ...Timeout ACK.");
            }

            if (acked) {
                // Se il chunk è andato a buon fine, aggiorna l'offset e resetta il timer di progresso
                offset += bytesRead;
                lastProgressTime = millis();
            } else {
                // Se il chunk fallisce tutti i tentativi, non fare nulla. Il loop continuerà a provare
                // lo stesso offset finché non scatta il timeout totale.
                Serial.printf("[TEST] Blocco a offset %u non riuscito. Riprovo...\n", offset);
                delay(1000); // Pausa per non sovraccaricare la linea
            }
        }
        f.close();
        
        // 3. END
        Serial.println("[TEST] Invio END...");
        // Svuota buffer RX
        while(Serial1.available()) Serial1.read();

        modoTrasmissione();
        Serial1.printf("TEST,END,%d!", manualOtaTargetId);
        modoRicezione();
        Serial.println("[TEST] Trasferimento completato.");
    }
    else if (cmdUpper == "OTA_SEND") {
        if (!manualOtaActive || !otaMenuActive || manualOtaTargetId == -1) {
            Serial.println("[OTA] Errore: eseguire prima OTA_MODE.");
            return;
        }
        if (!otaFileReady || !SPIFFS.exists(manualOtaFilePath)) {
            Serial.println("[OTA] Errore: file non trovato. Eseguire prima OTA_DOWNLOAD.");
            return;
        }
        if (!otaSpaceOk) {
            Serial.println("[OTA] Errore: eseguire prima OTA_CHECK_SPACE.");
            return;
        }
        if (!otaEraseOk) {
            Serial.println("[OTA] Errore: eseguire prima OTA_ERASE.");
            return;
        }
        File f = SPIFFS.open(manualOtaFilePath, "r");
        size_t fileSize = f.size();

        String md5Str = otaExpectedMd5;
        if (md5Str.length() == 0) {
            MD5Builder md5;
            md5.begin();
            md5.addStream(f, fileSize);
            md5.calculate();
            md5Str = md5.toString();
            md5Str.toUpperCase();
            f.seek(0);
        }

        // START OTA session on slave
        while (Serial1.available()) Serial1.read();
        Serial.printf("[OTA] START su Slave %d (size=%u, md5=%s)\n", manualOtaTargetId, fileSize, md5Str.c_str());
        modoTrasmissione();
        Serial1.printf("OTA,START,%d,%u,%s!", manualOtaTargetId, fileSize, md5Str.c_str());
        modoRicezione();

        bool ready = false;
        unsigned long waitReady = millis();
        while (millis() - waitReady < 30000) {
            if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                Serial.println("[OTA] START RX: " + resp);
                if (resp.indexOf("OK,OTA,READY," + String(manualOtaTargetId)) != -1) {
                    ready = true;
                }
                break;
            }
        }
        if (!ready) {
            Serial.println("[OTA] START non confermato dallo slave.");
            f.close();
            otaSendOk = false;
            return;
        }

        size_t offset = 0;
        const int CHUNK_SIZE = 128;
        uint8_t buff[CHUNK_SIZE];

        const unsigned long TOTAL_TRANSFER_TIMEOUT = 300000; // 5 minuti
        unsigned long lastProgressTime = millis();

        Serial.printf("[OTA] Avvio trasferimento di %u bytes alla partizione OTA...\n", fileSize);

        while(offset < fileSize) {
            esp_task_wdt_reset();

            if (millis() - lastProgressTime > TOTAL_TRANSFER_TIMEOUT) {
                Serial.println("\n[OTA] ERRORE: Timeout totale superato.");
                f.close();
                return;
            }

            f.seek(offset);
            size_t bytesRead = f.read(buff, CHUNK_SIZE);
            if (bytesRead == 0) {
                Serial.println("[OTA] ERRORE: Lettura file locale fallita.");
                f.close();
                return;
            }

            // Svuota buffer RX prima di inviare chunk
            while(Serial1.available()) Serial1.read();

            bool acked = false;
            for (int retry = 0; retry < 5; retry++) {
                String hexData = bufferToHex(buff, bytesRead);
                uint8_t checksum = calculateChecksum(hexData);
                
                Serial.printf("[OTA] Invio chunk offset %u, tentativo %d/5...\n", offset, retry + 1);
                modoTrasmissione();
                Serial1.printf("OTA,DATA,%d,%u,%s,%02X!", manualOtaTargetId, offset, hexData.c_str(), checksum);
                modoRicezione();

                unsigned long startWait = millis();
                while(millis() - startWait < 2500) {
                    esp_task_wdt_reset();
                    if (Serial1.available()) {
                        String resp = Serial1.readStringUntil('!');
                        if (resp.indexOf("OK,OTA,ACK," + String(manualOtaTargetId) + "," + String(offset)) != -1) {
                            Serial.println("[OTA] ...ACK ricevuto.");
                            acked = true;
                            break;
                        }
                    }
                }
                if (acked) break;
                Serial.println("[OTA] ...Timeout ACK.");
            }

            if (acked) {
                offset += bytesRead;
                lastProgressTime = millis();
            } else {
                Serial.printf("[OTA] Blocco a offset %u non riuscito. Riprovo...\n", offset);
                delay(1000);
            }
        }
        f.close();
        Serial.println("[OTA] Trasferimento completato.");
        otaSendOk = true;
        otaVerifyOk = false;
        otaVerifyFailLock = false;
    }
    else if (cmdUpper == "OTA_VERIFY") {
        if (!manualOtaActive || !otaMenuActive || manualOtaTargetId == -1) {
            Serial.println("[OTA] Errore: eseguire prima OTA_MODE.");
            return;
        }
        if (!otaSendOk) {
            Serial.println("[OTA] Errore: eseguire prima OTA_SEND.");
            return;
        }

        String md5Str = otaExpectedMd5;
        if (md5Str.length() == 0 && SPIFFS.exists(manualOtaFilePath)) {
            File f = SPIFFS.open(manualOtaFilePath, "r");
            MD5Builder md5;
            md5.begin();
            md5.addStream(f, f.size());
            md5.calculate();
            md5Str = md5.toString();
            md5Str.toUpperCase();
            f.close();
            otaExpectedMd5 = md5Str;
        }

        while (Serial1.available()) Serial1.read();
        Serial.printf("[OTA] Verifica MD5 su Slave %d (atteso=%s)\n", manualOtaTargetId, md5Str.c_str());
        modoTrasmissione();
        Serial1.printf("OTA,VERIFY,%d,%s!", manualOtaTargetId, md5Str.c_str());
        modoRicezione();

        bool finished = false;
        unsigned long startWait = millis();
        while (millis() - startWait < 10000) {
            if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                Serial.println("[OTA] VERIFY RX: " + resp);
                if (resp.indexOf("OK,OTA,VERIFY," + String(manualOtaTargetId) + ",PASS") != -1) {
                    otaVerifyOk = true;
                    otaVerifyFailLock = false;
                    Serial.println("[OTA] MD5 verificato: PASS.");
                } else {
                    otaVerifyOk = false;
                    otaVerifyFailLock = true;
                    Serial.println("[OTA] MD5 verificato: FAIL.");
                    Serial.println("[OTA] Comandi consentiti: OTA_ERASE/OTA_PREPARE oppure OTA_EXIT.");
                }
                finished = true;
                break;
            }
        }
        if (!finished) Serial.println("[OTA] Timeout su OTA_VERIFY.");
    }
    else if (cmdUpper == "OTA_COMMIT") {
        if (!manualOtaActive || !otaMenuActive || manualOtaTargetId == -1) {
            Serial.println("[OTA] Errore: eseguire prima OTA_MODE.");
            return;
        }
        if (!otaVerifyOk) {
            Serial.println("[OTA] Errore: eseguire prima OTA_VERIFY con esito PASS.");
            return;
        }
        Serial.println("[OTA] Invio comando COMMIT per finalizzare e riavviare...");
        // Svuota buffer RX
        while(Serial1.available()) Serial1.read();

        modoTrasmissione();
        Serial1.printf("OTA,END,%d!", manualOtaTargetId);
        modoRicezione();
        Serial.println("[OTA] Comando inviato. Lo slave dovrebbe riavviarsi se l'MD5 è corretto.");
        otaCommitRequested = true;
    }
    else if (cmdUpper == "OTA_RESULT") {
        if (!manualOtaActive || !otaMenuActive || manualOtaTargetId == -1) {
            Serial.println("[OTA] Errore: eseguire prima OTA_MODE.");
            return;
        }
        Serial.printf("[OTA] Controllo esito aggiornamento su Slave %d...\n", manualOtaTargetId);
        String resp = "";
        unsigned long startWait = millis();
        while (millis() - startWait < 30000) {
            resp = querySlaveResponseById(manualOtaTargetId, 600);
            if (resp.startsWith("OK,")) break;
            delay(250);
        }

        if (!resp.startsWith("OK,")) {
            Serial.println("[OTA] Nessuna risposta slave: aggiornamento non verificabile.");
            return;
        }

        String verNow = extractSlaveVersion(resp);
        Serial.println("[OTA] Risposta slave: " + resp);
        if (otaVersionBefore.length() > 0) {
            Serial.printf("[OTA] Versione prima: %s | adesso: %s\n", otaVersionBefore.c_str(), verNow.c_str());
            if (verNow != otaVersionBefore) {
                Serial.println("[OTA] Esito: aggiornamento riuscito (versione cambiata).");
            } else if (otaCommitRequested) {
                Serial.println("[OTA] Warning: versione invariata (potrebbe essere stesso firmware).");
            }
        } else {
            Serial.printf("[OTA] Versione attuale: %s\n", verNow.c_str());
        }
    }
    else if (cmdUpper == "TEST_VERIFY") {
            if (!manualOtaActive || manualOtaTargetId == -1) {
            Serial.println("[TEST] Errore: Eseguire prima TEST_MODE.");
            return;
        }
        
        // Ricalcola MD5 locale per sicurezza (o usa quello salvato)
        if (!SPIFFS.exists(manualOtaFilePath)) {
                Serial.println("[TEST] Errore: File locale mancante.");
                return;
        }
        File f = SPIFFS.open(manualOtaFilePath, "r");
        MD5Builder md5;
        md5.begin();
        md5.addStream(f, f.size());
        md5.calculate();
        String md5Str = md5.toString();
        md5Str.toUpperCase();
        f.close();

        Serial.printf("[TEST] Richiesta verifica MD5 a Slave %d (Atteso: %s)...\n", manualOtaTargetId, md5Str.c_str());
        // Svuota buffer RX
        while(Serial1.available()) Serial1.read();

        modoTrasmissione();
        Serial1.printf("TEST,VERIFY,%d,%s!", manualOtaTargetId, md5Str.c_str());
        modoRicezione();

        Serial.println("[TEST] Attesa risultato (timeout 15s)...");
        unsigned long startWait = millis();
        bool finished = false;
        while(millis() - startWait < 15000) {
            esp_task_wdt_reset(); // Previene il watchdog durante l'attesa della finalizzazione
            if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                Serial.println("[TEST] Risposta slave: " + resp);
                if (resp.indexOf("OK,TEST,PASS," + String(manualOtaTargetId)) != -1) {
                    Serial.println("[TEST] RISULTATO: SUCCESSO! MD5 Corrisponde.");
                } else if (resp.indexOf("OK,TEST,FAIL," + String(manualOtaTargetId)) != -1) {
                    Serial.println("[TEST] RISULTATO: FALLIMENTO! MD5 Diverso.");
                } else {
                    Serial.println("[TEST] RISULTATO: Risposta inattesa.");
                }
                finished = true;
                break;
            }
        }
        if (!finished) {
            Serial.println("[TEST] RISULTATO: Timeout.");
        }
    }
    else if (cmdUpper == "TEST_DELETE") {
        if (!manualOtaActive || manualOtaTargetId == -1) {
            Serial.println("[TEST] Errore: Eseguire prima TEST_MODE.");
            return;
        }
        while (Serial1.available()) Serial1.read();
        Serial.printf("[TEST] Richiesta cancellazione file test su Slave %d...\n", manualOtaTargetId);
        modoTrasmissione();
        Serial1.printf("TEST,DELETE,%d!", manualOtaTargetId);
        modoRicezione();

        unsigned long startWait = millis();
        bool finished = false;
        while (millis() - startWait < 3000) {
            if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                Serial.println("[TEST] Risposta slave: " + resp);
                if (resp.indexOf("OK,TEST,DELETE," + String(manualOtaTargetId) + ",OK") != -1) {
                    Serial.println("[TEST] File test cancellato sullo slave.");
                } else if (resp.indexOf("OK,TEST,DELETE," + String(manualOtaTargetId) + ",NOFILE") != -1) {
                    Serial.println("[TEST] Nessun file test presente sullo slave.");
                } else {
                    Serial.println("[TEST] Cancellazione non confermata.");
                }
                finished = true;
                break;
            }
        }
        if (!finished) Serial.println("[TEST] Timeout cancellazione file slave.");
    }
    else if (cmdUpper == "TEST_EXIT" || cmdUpper == "OTA_EXIT") {
        if (manualOtaActive) {
            manualOtaActive = false;
            manualOtaTargetId = -1;
            otaMenuActive = false;
            otaFileReady = false;
            otaSpaceOk = false;
            otaEraseOk = false;
            otaSendOk = false;
            otaVerifyOk = false;
            otaNoSpaceLock = false;
            otaVerifyFailLock = false;
            otaCommitRequested = false;
            otaExpectedMd5 = "";
            otaVersionBefore = "";
            Serial.println("[TEST] Procedura terminata. Il normale funzionamento è stato ripristinato.");
        } else {
            Serial.println("[TEST] Nessuna procedura attiva.");
        }
    }
    // Comando per il riavvio manuale
    else if (cmdUpper == "REBOOT") {
        Serial.println("Riavvio in corso...");
        delay(1000);
        ESP.restart();
    }
    // Comando per il reset di fabbrica.
    else if (cmdUpper == "CLEARMEM") {
        memoria.begin("easy", false); memoria.clear(); memoria.end();
        WiFi.disconnect(true, true);
        Serial.println("MEMORIA RESETTATA (FACTORY RESET). Riavvio...");
        delay(1000); ESP.restart();
    }
    // Blocco finale per comandi non riconosciuti
    else {
        Serial.println("Comando non riconosciuto. Usa HELP / HELPTEST / HELPOTA.");
    }
}

// Funzione principale per la gestione del menu seriale del Master.
void Serial_Master_Menu() {
    // 'static' significa che la variabile mantiene il suo valore tra le chiamate alla funzione.
    // Usata per accumulare i caratteri in arrivo.
    static String inputBuffer = ""; 
    
    // SICUREZZA: Limita il numero di caratteri processati per ciclo per evitare blocchi
    int charsProcessed = 0;
    const int MAX_CHARS_PER_LOOP = 64;

    // Finché ci sono dati disponibili sulla porta seriale...
    while (Serial.available() && charsProcessed < MAX_CHARS_PER_LOOP) {
        char c = Serial.read(); // ...leggi un carattere alla volta.
        charsProcessed++;
        
        // Se il carattere è un terminatore di riga (\n o \r), processa il comando se il buffer non è vuoto.
        if (c == '\n' || c == '\r') {
            Serial.println();
            if (inputBuffer.length() > 0) {
                // Chiama la funzione helper per processare il comando
                processSerialCommand(inputBuffer);
            }
            inputBuffer = ""; // Svuota il buffer per il prossimo comando SEMPRE, anche se processSerialCommand fa return.
        } else if (c == 8 || c == 127) {
            // Backspace
            if (inputBuffer.length() > 0) {
                inputBuffer.remove(inputBuffer.length() - 1);
                Serial.print("\b \b");
            }
        } else {
            // Protezione buffer overflow: evita che la stringa cresca all'infinito se non arriva mai un "a capo"
            if (inputBuffer.length() < 200) {
                inputBuffer += c; // Se non è un terminatore, aggiungi il carattere al buffer.
                Serial.write(c);  // Echo realtime sulla seriale
            }
        }
    }
}
