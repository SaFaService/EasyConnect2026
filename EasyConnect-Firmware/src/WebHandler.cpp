#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <SPIFFS.h>
#include <time.h>
#include "GestioneMemoria.h"
#include "Pins.h"
#include "Calibration.h" // Inclusione modulo calibrazione
#include "Serial_Manager.h"

// Riferimenti esterni
extern Preferences memoria;
extern Impostazioni config;
extern bool listaPerifericheAttive[101];
extern int statoInternet;
extern bool scansioneInCorso;
extern float currentDeltaP; // Variabile globale per DeltaP
extern bool currentDeltaPValid; // True se il delta corrente e' valido
extern DatiSlave databaseSlave[101];
extern WebServer server;

// Riferimento alla versione FW definita nel main
extern const char* FW_VERSION;

// Funzione di setup calibrazione definita in Calibration.cpp
extern void setupCalibration();

// Variabili gestione WiFi/AP
unsigned long lastWifiCheck = 0;
bool apEnabled = true;
int wifiRetryCount = 0; // Contatore tentativi riconnessione WiFi
bool wifiForceOff = false; // Modalita' laboratorio: WiFi/AP forzati OFF
bool mdnsStarted = false;
unsigned long lastMdnsAttempt = 0;
bool mdnsBoundToSta = false;
IPAddress mdnsLastStaIp;
static bool webServerConfigured = false;
static bool standaloneWifiProfile = false;

static bool isStandaloneWifiProfileActive() {
    return standaloneWifiProfile || (config.modalitaMaster == 1);
}

static const char* wifiHostNameByProfile() {
    return isStandaloneWifiProfileActive() ? "antraluxstandalone" : "antraluxrewamping";
}

static const char* wifiApSsidByProfile() {
    return isStandaloneWifiProfileActive() ? "AntraluxStandalone" : "AntraluxRewamping";
}

static const char* wifiApPasswordByProfile() {
    return isStandaloneWifiProfileActive() ? "Standalone1234!" : NULL;
}

static String escapeHtmlAttr(String value) {
    value.replace("&", "&amp;");
    value.replace("\"", "&quot;");
    value.replace("<", "&lt;");
    value.replace(">", "&gt;");
    return value;
}

static void resetMdnsState(bool stopResponder) {
    if (stopResponder && mdnsStarted) {
        MDNS.end();
    }
    mdnsStarted = false;
    mdnsBoundToSta = false;
    mdnsLastStaIp = IPAddress((uint32_t)0);
    lastMdnsAttempt = 0;
}

static bool startMdnsResponder(bool staConnected) {
    const char* host = wifiHostNameByProfile();
    WiFi.setHostname(host);
    if (!MDNS.begin(host)) {
        Serial.println("[WIFI] Errore avvio mDNS.");
        return false;
    }
    MDNS.addService("http", "tcp", 80);
    mdnsBoundToSta = staConnected;
    mdnsLastStaIp = staConnected ? WiFi.localIP() : IPAddress((uint32_t)0);
    Serial.printf("[WIFI] mDNS attivo: http://%s.local\n", host);
    return true;
}

static void ensureMdnsService() {
    if (WiFi.getMode() == WIFI_OFF) {
        if (mdnsStarted) {
            resetMdnsState(true);
            Serial.println("[WIFI] mDNS disattivato (WiFi OFF).");
        }
        return;
    }

    const bool staConnected = (WiFi.status() == WL_CONNECTED);
    const IPAddress staIp = staConnected ? WiFi.localIP() : IPAddress((uint32_t)0);
    unsigned long now = millis();

    // Se mDNS era stato avviato prima che la STA ottenesse IP (o IP cambiato), riavvia.
    const bool needRebindToSta = mdnsStarted && staConnected &&
                                 (!mdnsBoundToSta || (staIp != mdnsLastStaIp));
    if (needRebindToSta) {
        resetMdnsState(true);
    }

    if (!mdnsStarted && (now - lastMdnsAttempt < 3000)) {
        return;
    }

    if (!mdnsStarted) {
        lastMdnsAttempt = now;
        mdnsStarted = startMdnsResponder(staConnected);
    } else {
        // Manteniamo traccia del binding STA in caso di disconnessione successiva.
        mdnsBoundToSta = staConnected;
        mdnsLastStaIp = staIp;
    }
}

// Oggetti Globali
WebServer server(80);
DNSServer dnsServer;

// --- LOGO (Base64 Placeholder) ---
const char* LOGO_IMG = "data:image/jpeg;base64,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b29mAAAACXByb29mQ01ZSwA4QklNBDsAAAAAAi0AAAAQAAAAAQAAAAAAEnByaW50T3V0cHV0T3B0aW9ucwAAABcAAAAAQ3B0bmJvb2wAAAAAAENsYnJib29sAAAAAABSZ3NNYm9vbAAAAAAAQ3JuQ2Jvb2wAAAAAAENudENib29sAAAAAABMYmxzYm9vbAAAAAAATmd0dmJvb2wAAAAAAEVtbERib29sAAAAAABJbnRyYm9vbAAAAAAAQmNrZ09iamMAAAABAAAAAAAAUkdCQwAAAAMAAAAAUmQgIGRvdWJAb+AAAAAAAAAAAABHcm4gZG91YkBv4AAAAAAAAAAAAEJsICBkb3ViQG/gAAAAAAAAAAAAQnJkVFVudEYjUmx0AAAAAAAAAAAAAAAAQmxkIFVudEYjUmx0AAAAAAAAAAAAAAAAUnNsdFVudEYjUHhsQFIGZkAAAAAAAAAKdmVjdG9yRGF0YWJvb2wBAAAAAFBnUHNlbnVtAAAAAFBnUHMAAAAAUGdQQwAAAABMZWZ0VW50RiNSbHQAAAAAAAAAAAAAAABUb3AgVW50RiNSbHQAAAAAAAAAAAAAAABTY2wgVW50RiNQcmNAWQAAAAAAAAAAABBjcm9wV2hlblByaW50aW5nYm9vbAAAAAAOY3JvcFJlY3RCb3R0b21sb25nAAAAAAAAAAxjcm9wUmVjdExlZnRsb25nAAAAAAAAAA1jcm9wUmVjdFJpZ2h0bG9uZwAAAAAAAAALY3JvcFJlY3RUb3Bsb25nAAAAAAA4QklNA+0AAAAAABAASBmZAAEAAgBIGZkAAQACOEJJTQQmAAAAAAAOAAAAAAAAAAAAAD+AAAA4QklNBA0AAAAAAAQAAABaOEJJTQQZAAAAAAAEAAAAHjhCSU0D8wAAAAAACQAAAAAAAAAAAQA4QklNJxAAAAAAAAoAAQAAAAAAAAACOEJJTQP1AAAAAABIAC9mZgABAGxmZgAGAAAAAAABAC9mZgABAKGZmgAGAAAAAAABADIAAAABAFoAAAAGAAAAAAABADUAAAABAC0AAAAGAAAAAAABOEJJTQP4AAAAAABwAAD/////////////////////////////A+gAAAAA/////////////////////////////wPoAAAAAP////////////////////////////8D6AAAAAD/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c3ludGF4LW5zIyI+IDxyZGY6RGVzY3JpcHRpb24gcmRmOmFib3V0PSIiIHhtbG5zOnhtcD0iaHR0cDovL25zLmFkb2JlLmNvbS94YXAvMS4wLyIgeG1sbnM6ZGM9Imh0dHA6Ly9wdXJsLm9yZy9kYy9lbGVtZW50cy8xLjEvIiB4bWxuczpwaG90b3Nob3A9Imh0dHA6Ly9ucy5hZG9iZS5jb20vcGhvdG9zaG9wLzEuMC8iIHhtbG5zOnhtcE1NPSJodHRwOi8vbnMuYWRvYmUuY29tL3hhcC8xLjAvbW0vIiB4bWxuczpzdEV2dD0iaHR0cDovL25zLmFkb2JlLmNvbS94YXAvMS4wL3NUeXBlL1Jlc291cmNlRXZlbnQjIiB4bXA6Q3JlYXRvclRvb2w9IkFkb2JlIFBob3Rvc2hvcCAyNi4xMCAoV2luZG93cykiIHhtcDpDcmVhdGVEYXRlPSIyMDI2LTAxLTIyVDEwOjMzOjA2KzAxOjAwIiB4bXA6TW9kaWZ5RGF0ZT0iMjAyNi0wMS0yMlQxMDozNTowOSswMTowMCIgeG1wOk1ldGFkYXRhRGF0ZT0iMjAyNi0wMS0yMlQxMDozNTowOSswMTowMCIgZGM6Zm9ybWF0PSJpbWFnZS9qcGVnIiBwaG90b3Nob3A6Q29sb3JNb2RlPSIzIiB4bXBNTTpJbnN0YW5jZUlEPSJ4bXAuaWlkOjRlODE2NjU3LWNlODktNjc0Ni05NTA2LTM5N2QwOTg2YjA1NCIgeG1wTU06RG9jdW1lbnRJRD0iYWRvYmU6ZG9jaWQ6cGhvdG9zaG9wOjEwNDZmZWEwLWZiN2QtODE0Yi1hMWI4LTE2OWJmZGY3MWMwYiIgeG1wTU06T3JpZ2luYWxEb2N1bWVudElEPSJ4bXAuZGlkOmY2OGY0ZmU0LTkxZjQtY2Q0MS04ZjQ2LTk3ZDA5MTU1ZDZjZCI+IDx4bXBNTTpIaXN0b3J5PiA8cmRmOlNlcT4gPHJkZjpsaSBzdEV2dDphY3Rpb249ImNyZWF0ZWQiIHN0RXZ0Omluc3RhbmNlSUQ9InhtcC5paWQ6ZjY4ZjRmZTQtOTFmNC1jZDQxLThmNDYtOTdkMDkxNTVkNmNkIiBzdEV2dDp3aGVuPSIyMDI2LTAxLTIyVDEwOjMzOjA2KzAxOjAwIiBzdEV2dDpzb2Z0d2FyZUFnZW50PSJBZG9iZSBQaG90b3Nob3AgMjYuMTAgKFdpbmRvd3MpIi8+IDxyZGY6bGkgc3RFdnQ6YWN0aW9uPSJjb252ZXJ0ZWQiIHN0RXZ0OnBhcmFtZXRlcnM9ImZyb20gYXBwbGljYXRpb24vdm5kLmFkb2JlLnBob3Rvc2hvcCB0byBpbWFnZS9qcGVnIi8+IDxyZGY6bGkgc3RFdnQ6YWN0aW9uPSJzYXZlZCIgc3RFdnQ6aW5zdGFuY2VJRD0ieG1wLmlpZDo0ZTgxNjY1Ny1jZTg5LTY3NDYtOTUwNi0zOTdkMDk4NmIwNTQiIHN0RXZ0OndoZW49IjIwMjYtMDEtMjJUMTA6MzU6MDkrMDE6MDAiIHN0RXZ0OnNvZnR3YXJlQWdlbnQ9IkFkb2JlIFBob3Rvc2hvcCAyNi4xMCAoV2luZG93cykiIHN0RXZ0OmNoYW5nZWQ9Ii8iLz4gPC9yZGY6U2VxPiA8L3htcE1NOkhpc3Rvcnk+IDwvcmRmOkRlc2NyaXB0aW9uPiA8L3JkZjpSREY+IDwveDp4bXBtZXRhPiAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgIDw/eHBhY2tldCBlbmQ9InciPz7/7gAOQWRvYmUAZEAAAAAB/9sAhAABAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAgICAgICAgICAgIDAwMDAwMDAwMDAQEBAQEBAQEBAQECAgECAgMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwMDAwP/wAARCAAyAJYDAREAAhEBAxEB/90ABAAT/8QBogAAAAYCAwEAAAAAAAAAAAAABwgGBQQJ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pBP2Dq17FJJc7UyISqTEtTyHhSCp/Mgfaekz8sOvt6xSdefJLpvC12f7j+PeTr69tn4ZacZTuLpjcxoIO3emofuJYKeoy2axmOp8vt8TOI4tyYeiuUSWUl3Zbm3Iu9pv5AthdADUeEUq18OX1oCSr0z4bt5gdNbnBN/i+4WqlruA/COMkZprj+0gBl9GUep6RGB6KxPyP777k7c+RvTOIzex9pU+C6W+POy+2tpYnNRf3YxtPR7v7I7Vi25m4MlS0NVv/e2TgxtLM6R1Yx224jpRZ2DKJNyfa9ssbHar9luHJlneNiO41WOOopUIgLEcNUh9OmUslv767ur+0DQrSOJXUHA7nehr8TEAedEHr099z/CvqWo6f33jvj71R1f1F21T/wB29/db7p2JsbbO0K2HtHqjNpvjrI5OtwdBjZqnEnctH9pVRyOUahrqlLWka7e37/erfW77nezT2J1JIruzjw5BokoCTnTkfNV9Or3e0WptZlsbaOK6FGRlUL3odSVoBiuD8ifXoAuru7+xOue4fkH2JvL4i/KyeHvtegN/4ih2X1vi9zJt2px/QGxdubq2pnaubdWJEOd2xurH1VDOqI8btBrR2VgfZne2FrdWO12tvvdnW28ZCWcrqrM7Kw7ThlII+3pFbXc9vdX1xNtdzSfwmAVQaUjUEHuGQ1Qfs6dN9dkdn9tdudQdp9K/Gj5EbV7V6dx/YMM2I7w2Ti+tOuO1urt5UWDXe/V1Vv8Ap9ybiXam9azJ4DF5fbVXU0klAcnivtqpoaaplnibtrazsrG+s9w3W2eznKZicu8cik6JNGldSUZlkANdLVFSADeee4urq1ubTb51uYQ2JFCq6NTUmqpoxIUoSKVFDQEkCjuH5FfIztHC1+wOk/ip3R1d2RnKSbD1XZXyApNibX6x6lauhejqt3mo27vndmT7SyG35JDNQYvC08kGSmRFmq6SBnmRJFtW1Wbrc7hvME1opr4cOtpJKZ09yKIw3As5BUVorGgKh7+/uUMFpts0c7CheTSESuK4Zi5HEBRnzIGekjN8csvuVuovhdR4HceN+H3x+2TsTIds7m3LTQo3yZ3Bi4hLtDqqik1lq/aUGaxZ3DvuoEaw5CeSkxKa4J8iqvjdY4vrt/aRTvtzI4jVf9AU/FIfRqHRCOKjU/EJ019A0n0u0qjDaoEUuT/opHwp81qNUnkcLwLdCb3lsHenVfaeE+V3R+2cxurJy0m3+v8A5HdQbVhpf4j2/wBVU9bLTbf3ntzHzSU9LWdsdIVGVmq8eC8cuXwElbiy5k+w8KPbrm3vLKTZtxlVFqzwStwjkp3IxyfDloA3krhXpTVVReQTW1ym52cZZqBZY1/GnkwH8cdaj+Jar/DQ7QceMyWfT49dtD+TTw1vFbya7f2bar8Wv7D/AMujjr//1N/E/U/65/3v37r3XXv3Xuve/de697917r3v3Xuve/de697917r3v3Xuve/de697917r3v3Xuve/de697917r3v3Xuve/de6979Tr3Xvfuvde9+6912Pof8AW/4ke/de6//V38T9T/rn/e/fuvdde/de697917r3v3Xuve/de697917r3v3Xuve/de697917r3v3Xuve/de697917r3v3Xuve/de697917r3v3Xuve/de697917rsfQ/63/Ej37r3X//2Q==";

// --- FAVICON (Base64 - Pallino Blu) ---
const char* FAVICON_IMG = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAFklEQVR42mNk+M9AAzDCo/6j/qP+QwwA+bg/wX44w18AAAAASUVORK5CYII=";

// Dichiarazione forward per checkThresholds se usata qui ma definita in Calibration.cpp
extern String checkThresholds(float currentP);

static const int TEST_CSV_LEVELS = 3;
static bool webSpiffsReady = false;

static bool ensureWebSpiffsMounted() {
    if (webSpiffsReady) return true;
    if (!SPIFFS.begin(true)) {
        Serial.println("[WEB] Errore mount SPIFFS per download CSV.");
        return false;
    }
    webSpiffsReady = true;
    return true;
}

static String getTestCsvPathForLevel(int level) {
    return "/deltap_test_sporco_" + String(level) + ".csv";
}

static String getTestCsvDownloadName(int level) {
    return "deltap_test_sporco_" + String(level) + ".csv";
}

static String getTestCsvMetaPathForLevel(int level) {
    return "/deltap_test_sporco_" + String(level) + ".meta";
}

static long long readLongLongFromMeta(const String &metaPath, const String &key) {
    File f = SPIFFS.open(metaPath, "r");
    if (!f) return 0;

    const String prefix = key + "=";
    long long out = 0;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (!line.startsWith(prefix)) continue;
        const String value = line.substring(prefix.length());
        out = atoll(value.c_str());
        break;
    }

    f.close();
    return out;
}

static String formatTimestampForDashboard(time_t ts) {
    if (ts <= 0) return "";
    struct tm tmInfo;
    if (!localtime_r(&ts, &tmInfo)) return "";
    char buf[32];
    if (strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M:%S", &tmInfo) == 0) return "";
    return String(buf);
}

static String getTestCsvLastSaveInfo(int level, bool csvExists) {
    if (!csvExists) return "";

    const String metaPath = getTestCsvMetaPathForLevel(level);
    if (!SPIFFS.exists(metaPath)) {
        return "Ultimo salvataggio: metadati non disponibili";
    }

    const long long savedEpoch = readLongLongFromMeta(metaPath, "saved_epoch");
    if (savedEpoch > 1000000000LL) {
        const String stamp = formatTimestampForDashboard((time_t)savedEpoch);
        if (stamp.length() > 0) {
            return "Ultimo salvataggio: " + stamp;
        }
    }

    const long long savedUptimeMs = readLongLongFromMeta(metaPath, "saved_uptime_ms");
    if (savedUptimeMs > 0) {
        return "Ultimo salvataggio: uptime " + String((unsigned long)(savedUptimeMs / 1000LL)) + " s";
    }

    return "Ultimo salvataggio: non disponibile";
}

static String normalizeSpiffsPath(const String &path) {
    if (path.length() == 0) return path;
    if (path[0] == '/') return path;
    return "/" + path;
}

static bool removeSpiffsFileFlexible(const String &path) {
    const String normalized = normalizeSpiffsPath(path);
    if (SPIFFS.remove(normalized)) return true;
    if (normalized.startsWith("/")) {
        const String alt = normalized.substring(1);
        if (alt.length() > 0 && SPIFFS.remove(alt)) return true;
    }
    return false;
}

static bool isTmpWizardPathForLevel(const String &path, int levelFilter) {
    const String p = normalizeSpiffsPath(path);
    if (levelFilter < 1) {
        return p.startsWith("/tmp_dptest_l") || p.startsWith("/tmp_merge_l");
    }
    const String tmpPrefix = "/tmp_dptest_l" + String(levelFilter) + "_";
    const String mergePrefix = "/tmp_merge_l" + String(levelFilter) + "_";
    return p.startsWith(tmpPrefix) || p.startsWith(mergePrefix);
}

static int removeWizardTmpFilesFromSpiffs(int levelFilter) {
    int removed = 0;
    File root = SPIFFS.open("/");
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return 0;
    }

    File f = root.openNextFile();
    while (f) {
        const String rawName = f.name();
        f.close();
        if (isTmpWizardPathForLevel(rawName, levelFilter)) {
            if (removeSpiffsFileFlexible(rawName)) {
                removed++;
            }
        }
        f = root.openNextFile();
    }
    root.close();
    return removed;
}

static String escapeJsonString(const String &input) {
    String out;
    out.reserve(input.length() + 8);
    for (size_t i = 0; i < input.length(); i++) {
        const char c = input[i];
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

static bool computeLiveDeltaPForDashboard(float &outDeltaP) {
    const unsigned long now = millis();
    const unsigned long OFFLINE_TIMEOUT_MS = 30000UL;
    bool grp1Found = false;
    bool grp2Found = false;
    float grp1Pressure = 0.0f;
    float grp2Pressure = 0.0f;
    bool firstOnlineFound = false;
    bool secondOnlineFound = false;
    float firstOnlinePressure = 0.0f;
    float secondOnlinePressure = 0.0f;

    for (int i = 1; i <= 100; i++) {
        if (!listaPerifericheAttive[i]) continue;
        const unsigned long last = databaseSlave[i].lastResponseTime;
        const bool online = (last > 0) && ((now - last) <= OFFLINE_TIMEOUT_MS);
        if (!online) continue;

        if (!firstOnlineFound) {
            firstOnlinePressure = databaseSlave[i].p;
            firstOnlineFound = true;
        } else if (!secondOnlineFound) {
            secondOnlinePressure = databaseSlave[i].p;
            secondOnlineFound = true;
        }

        if (!grp1Found && databaseSlave[i].grp == 1) {
            grp1Pressure = databaseSlave[i].p;
            grp1Found = true;
        } else if (!grp2Found && databaseSlave[i].grp == 2) {
            grp2Pressure = databaseSlave[i].p;
            grp2Found = true;
        }
    }

    if (grp1Found && grp2Found) {
        outDeltaP = grp1Pressure - grp2Pressure;
        return true;
    }
    if (firstOnlineFound && secondOnlineFound) {
        outDeltaP = firstOnlinePressure - secondOnlinePressure;
        return true;
    }

    outDeltaP = 0.0f;
    return false;
}

void handleDownloadTestCsv() {
    if (!server.hasArg("level")) {
        server.send(400, "text/plain", "Parametro 'level' mancante.");
        return;
    }

    const int level = server.arg("level").toInt();
    if (level < 1 || level > TEST_CSV_LEVELS) {
        server.send(400, "text/plain", "Parametro 'level' non valido (1..3).");
        return;
    }

    if (!ensureWebSpiffsMounted()) {
        server.send(500, "text/plain", "SPIFFS non disponibile.");
        return;
    }

    const String path = getTestCsvPathForLevel(level);
    if (!SPIFFS.exists(path)) {
        server.send(404, "text/plain", "CSV non ancora disponibile per questo livello.");
        return;
    }

    File f = SPIFFS.open(path, "r");
    if (!f) {
        server.send(500, "text/plain", "Errore apertura file CSV.");
        return;
    }

    const String metaPath = getTestCsvMetaPathForLevel(level);
    const long long savedEpoch = SPIFFS.exists(metaPath) ? readLongLongFromMeta(metaPath, "saved_epoch") : 0;
    String downloadName = getTestCsvDownloadName(level);
    if (savedEpoch > 1000000000LL) {
        downloadName = "deltap_test_sporco_" + String(level) + "_" + String((unsigned long)savedEpoch) + ".csv";
    }

    String disposition = String("attachment; filename=\"") + downloadName + "\"";
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    server.sendHeader("Content-Disposition", disposition);
    server.sendHeader("Connection", "close");
    server.streamFile(f, "text/csv");
    f.close();
}

void handleDeleteTestCsv() {
    if (webIsDeltaPTestWizardBusy()) {
        server.send(409, "text/plain", "Impossibile cancellare durante wizard/test DeltaP attivo.");
        return;
    }

    if (!ensureWebSpiffsMounted()) {
        server.send(500, "text/plain", "SPIFFS non disponibile.");
        return;
    }

    const bool deleteAll = server.hasArg("all") && server.arg("all") == "1";
    int removedCsv = 0;
    int removedTmp = 0;

    if (deleteAll) {
        for (int level = 1; level <= TEST_CSV_LEVELS; level++) {
            const String path = getTestCsvPathForLevel(level);
            const String metaPath = getTestCsvMetaPathForLevel(level);
            if (SPIFFS.exists(path) && removeSpiffsFileFlexible(path)) {
                removedCsv++;
            }
            if (SPIFFS.exists(metaPath)) {
                removeSpiffsFileFlexible(metaPath);
            }
        }
        removedTmp = removeWizardTmpFilesFromSpiffs(0);
    } else {
        if (!server.hasArg("level")) {
            server.send(400, "text/plain", "Parametro 'level' mancante.");
            return;
        }
        const int level = server.arg("level").toInt();
        if (level < 1 || level > TEST_CSV_LEVELS) {
            server.send(400, "text/plain", "Parametro 'level' non valido (1..3).");
            return;
        }

        const String path = getTestCsvPathForLevel(level);
        const String metaPath = getTestCsvMetaPathForLevel(level);
        if (SPIFFS.exists(path) && removeSpiffsFileFlexible(path)) {
            removedCsv = 1;
        }
        if (SPIFFS.exists(metaPath)) {
            removeSpiffsFileFlexible(metaPath);
        }
        removedTmp = removeWizardTmpFilesFromSpiffs(level);
    }

    const uint32_t total = SPIFFS.totalBytes();
    const uint32_t used = SPIFFS.usedBytes();
    const uint32_t freeBytes = (used <= total) ? (total - used) : 0;

    String msg = "OK: CSV rimossi=" + String(removedCsv) +
                 ", tmp rimossi=" + String(removedTmp) +
                 ", spazio libero=" + String(freeBytes) + " bytes.";
    server.send(200, "text/plain", msg);
}

void handleTestWizardStatus() {
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.send(200, "application/json", webGetDeltaPTestWizardStatusJson());
}

void handleTestWizardStart() {
    const int totalSpeeds = server.hasArg("total_speeds") ? server.arg("total_speeds").toInt() : 0;
    const int dirtLevel = server.hasArg("dirt_level") ? server.arg("dirt_level").toInt() : 0;
    const int speedIndex = server.hasArg("speed_index") ? server.arg("speed_index").toInt() : 0;

    String message = "";
    const bool ok = webStartDeltaPTestWizard(totalSpeeds, dirtLevel, speedIndex, message);
    String json = "{\"ok\":" + String(ok ? "true" : "false") +
                  ",\"message\":\"" + escapeJsonString(message) + "\"}";
    server.send(ok ? 200 : 400, "application/json", json);
}

void handleTestWizardStop() {
    const String action = server.hasArg("action") ? server.arg("action") : "save";
    const bool saveIfPossible = (action != "abort");

    String message = "";
    const bool ok = webStopDeltaPTestWizard(saveIfPossible, message);
    String json = "{\"ok\":" + String(ok ? "true" : "false") +
                  ",\"message\":\"" + escapeJsonString(message) + "\"}";
    server.send(ok ? 200 : 409, "application/json", json);
}

// --- GENERAZIONE HTML DASHBOARD ---
String getDashboardHTML() {
    // Inizio costruzione pagina HTML (Start building HTML page)
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>Antralux Rewamping</title>";
    html += "<style>";
    
    // Definizione stili CSS (CSS Style definitions)
    html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 0; background-color: #f0f2f5; color: #333; }";
    html += ".header { background-color: #fff; padding: 15px; text-align: center; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
    html += ".logo { max-height: 60px; margin-bottom: 10px; }";
    html += ".container { padding: 20px; max-width: 800px; margin: 0 auto; }";
    
    // Stile delle Card / Riquadri (Card Style)
    html += ".card { background: #fff; border-radius: 10px; padding: 20px; margin-bottom: 20px; box-shadow: 0 2px 5px rgba(0,0,0,0.05); }";
    html += ".card h3 { margin-top: 0; color: #0056b3; border-bottom: 2px solid #f0f2f5; padding-bottom: 10px; text-align: center; }";
    
    // Griglia per i dati (Data Grid)
    html += ".info-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 15px; }";
    html += ".info-item { background: #f9f9f9; padding: 10px; border-radius: 5px; text-align: center; }";
    html += ".info-label { font-size: 0.85em; color: #666; display: block; }";
    html += ".info-value { font-size: 1.2em; font-weight: bold; color: #333; }";
    
    // Pulsanti (Buttons)
    html += ".menu-bar { display: flex; gap: 10px; margin-top: 20px; }";
    html += ".btn { flex: 1; padding: 15px; text-align: center; text-decoration: none; color: white; border-radius: 8px; font-weight: bold; transition: opacity 0.3s; }";
    html += ".btn-calib { background-color: #17a2b8; }";
    html += ".btn-wifi { background-color: #6c757d; }";
    html += ".btn:hover { opacity: 0.9; }";
    html += ".icon-btn { border: 1px solid #d8d8d8; border-radius: 6px; background: #fff; cursor: pointer; padding: 4px 8px; font-size: 1em; }";
    html += ".icon-btn:hover { background: #f3f3f3; }";
    html += ".trash-btn { color: #b22222; }";
    html += ".wiz-grid { display:grid; grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap:10px; }";
    html += ".wiz-input { width:100%; padding:8px; border:1px solid #ccc; border-radius:6px; box-sizing:border-box; margin-top:4px; }";
    html += ".wiz-actions { display:flex; gap:8px; margin-top:12px; flex-wrap:wrap; }";
    html += ".wiz-btn { border:none; border-radius:6px; color:#fff; font-weight:bold; padding:10px 12px; cursor:pointer; }";
    html += ".wiz-btn:disabled { opacity:0.5; cursor:not-allowed; }";
    html += ".wiz-start { background:#17a2b8; }";
    html += ".wiz-stop { background:#f0ad4e; }";
    html += ".wiz-abort { background:#dc3545; }";
    html += ".wiz-msg { margin-top:8px; font-size:0.9em; }";
    html += ".wiz-status { margin-top:10px; font-size:0.95em; line-height:1.5; }";
    html += ".dot { height: 15px; width: 15px; background-color: #bbb; border-radius: 50%; display: inline-block; vertical-align: middle; }";
    html += ".dot-green { background-color: #28a745; }";
    html += ".dot-red { background-color: #dc3545; }";
    html += "</style></head><body>";

    // Intestazione con Logo (Header with Logo)
    html += "<div class='header'>";
    html += "<img src='" + String(LOGO_IMG) + "' class='logo' alt='Antralux Logo'>";
    html += "</div>";

    html += "<div class='container'>";

    // --- CALCOLI DATI SISTEMA (SYSTEM DATA CALCULATIONS) ---
    int countPeriferiche = 0; // Contatore periferiche (Device counter)
    // Scansioniamo l'array per contare e trovare le pressioni
    // (Scan array to count and find pressures)
    for(int i=1; i<=100; i++) {
        if(listaPerifericheAttive[i]) {
            countPeriferiche++;
        }
    }
    
    // Delta P visualizzato: valore filtrato/stabilizzato.
    bool filteredValid = isFilteredDeltaPValid();
    float filteredDelta = getFilteredDeltaP();
    float dashboardDelta = 0.0f;
    bool dashboardDeltaValid = false;

    if (filteredValid) {
        dashboardDelta = filteredDelta;
        dashboardDeltaValid = true;
    } else if (currentDeltaPValid) {
        dashboardDelta = currentDeltaP;
        dashboardDeltaValid = true;
    } else {
        dashboardDeltaValid = computeLiveDeltaPForDashboard(dashboardDelta);
    }

    String deltaPStr = dashboardDeltaValid ? (String(dashboardDelta, 1) + " Pa") : "In attesa dati (Waiting data)";
    
    // --- CONTROLLO SOGLIE CALIBRAZIONE ---
    String warningMsg = checkThresholds(filteredDelta);
    String warningHtml = "";
    warningHtml += "<div id='deltaPWarning' style='color:red; font-weight:bold; margin-top:5px;";
    warningHtml += (warningMsg != "") ? "'" : " display:none;'";
    warningHtml += ">" + warningMsg + "</div>";

    // --- DATI CONNESSIONE (CONNECTION DATA) ---
    String apClass = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) ? "dot-green" : "dot-red";
    String internetClass = (statoInternet == 2) ? "dot-green" : "dot-red";
    String ssidName = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "Non connesso";
    String ipAddr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "--";
    const bool csvFsReady = ensureWebSpiffsMounted();

    // --- RIQUADRO MASTER (MASTER CARD) ---
    html += "<div class='card'>";
    // Titolo: Seriale Master (Title: Master Serial)
    html += "<h3>Centralina: " + String(config.serialeID) + " (v" + String(FW_VERSION) + ")</h3>";
    html += "<div class='info-grid'>";
    
    // Info Connessione (Connection Info)
    html += "<div class='info-item'><span class='info-label'>MODALIT&Agrave; AP</span><span class='info-value'><span class='dot " + apClass + "'></span></span></div>";
    html += "<div class='info-item'><span class='info-label'>INTERNET</span><span class='info-value'><span class='dot " + internetClass + "'></span></span></div>";
    html += "<div class='info-item'><span class='info-label'>RETE WIFI</span><span class='info-value'>" + ssidName + "</span></div>";
    html += "<div class='info-item'><span class='info-label'>IP RETE</span><span class='info-value'>" + ipAddr + "</span></div>";

    // Numero Periferiche Rilevate (Number of Detected Peripherals)
    html += "<div class='info-item'><span class='info-label'>PERIFERICHE (DEVICES)</span>";
    html += "<span class='info-value'>" + String(countPeriferiche) + "</span></div>";
    
    // Valore Delta Pressione (Delta Pressure Value)
    html += "<div class='info-item'><span class='info-label'>DELTA PRESSIONE</span>"; // Aggiunto ID per aggiornamento JS (Added ID for JS update)
    html += "<span class='info-value' id='deltaP'>" + deltaPStr + "</span>" + warningHtml + "</div>";
    
    html += "</div></div>";

    // --- RIQUADRI SLAVE (SLAVE CARDS) ---
    if (countPeriferiche > 0) {
        for (int i = 1; i <= 100; i++) {
            if (listaPerifericheAttive[i]) {
                html += "<div class='card'>";
                // Titolo: Seriale Slave (Title: Slave Serial)
                html += "<h3>Sensore: " + String(databaseSlave[i].sn) + " (v" + String(databaseSlave[i].version) + ")</h3>";
                html += "<div class='info-grid'>";
                
                // Indirizzo IP / ID 485 (IP Address)
                html += "<div class='info-item'><span class='info-label'>IP (ADDR)</span><span class='info-value'>" + String(i) + "</span></div>";
                // Gruppo (Group)
                html += "<div class='info-item'><span class='info-label'>GRUPPO (GROUP)</span><span class='info-value'>" + String(databaseSlave[i].grp) + "</span></div>";
                // Modalità (Mode) - Placeholder, dato non disponibile dallo slave (Data not available from slave)
                html += "<div class='info-item'><span class='info-label'>MODALIT&Agrave;</span><span class='info-value'>--</span></div>";
                // Pressione (Pressure)
                html += "<div class='info-item'><span class='info-label'>PRESSIONE</span><span class='info-value' id='press-" + String(i) + "'>" + String(databaseSlave[i].p, 0) + " Pa</span></div>";
                // Temperatura (Temperature)
                html += "<div class='info-item'><span class='info-label'>TEMP</span><span class='info-value' id='temp-" + String(i) + "'>" + String(databaseSlave[i].t, 1) + " &deg;C</span></div>";
                
                html += "</div></div>";
            }
        }
    } else {
        // Nessuna periferica (No devices)
        html += "<div class='card'><p style='text-align:center; color:#666;'>Nessuna periferica rilevata.</p></div>";
    }

    // --- TASTI MENU (MENU BUTTONS) ---
    html += "<div class='menu-bar'>";
    html += "<a href='/calibrazione' class='btn btn-calib'>CALIBRAZIONE</a>";
    html += "<a href='/wifi' class='btn btn-wifi'>WIFI</a>";
    html += "</div>";

    // --- DOWNLOAD CSV TEST DELTAP ---
    html += "<div class='card'>";
    html += "<h3>Registro Test DeltaP</h3>";
    html += "<p style='margin-top:0;'>Download CSV per livello di sporco ";
    html += "<button class='icon-btn trash-btn' onclick='deleteAllTestCsv()' title='Cancella tutti i CSV e file temporanei'>&#128465;</button>";
    html += "</p>";
    for (int level = 1; level <= TEST_CSV_LEVELS; level++) {
        html += "<p>Livello sporco " + String(level) + ": ";
        const String path = getTestCsvPathForLevel(level);
        const bool exists = csvFsReady && SPIFFS.exists(path);
        if (exists) {
            html += "<a href='#' onclick='downloadTestCsv(" + String(level) + "); return false;'>Scarica CSV</a>";
            html += " <button class='icon-btn trash-btn' onclick='deleteTestCsv(" + String(level) + ")' title='Cancella CSV livello " + String(level) + "'>&#128465;</button>";
        } else {
            html += "<span style='color:#888;'>non disponibile</span>";
            html += " <button class='icon-btn trash-btn' onclick='deleteTestCsv(" + String(level) + ")' title='Cancella eventuali file temporanei livello " + String(level) + "'>&#128465;</button>";
        }
        html += "</p>";
        if (csvFsReady) {
            const String lastSaveInfo = getTestCsvLastSaveInfo(level, exists);
            if (lastSaveInfo.length() > 0) {
                html += "<p style='margin-top:-6px; color:#666; font-size:0.9em;'>" + lastSaveInfo + "</p>";
            }
        }
    }
    html += "</div>";

    // --- WIZARD TEST DELTAP DA WEB ---
    html += "<div class='card'>";
    html += "<h3>Wizard Campionamento DeltaP</h3>";
    html += "<p style='margin-top:0;'>Avvio test da pagina locale (equivalente a TESTWIZ) con stato live.</p>";
    html += "<div class='wiz-grid'>";
    html += "<div><label>Velocita' totali<input id='wizTotalSpeeds' class='wiz-input' type='number' min='1' max='10' value='5'></label></div>";
    html += "<div><label>Livello sporco<input id='wizDirtLevel' class='wiz-input' type='number' min='1' max='3' value='1'></label></div>";
    html += "<div><label>Velocita' test<input id='wizSpeedIndex' class='wiz-input' type='number' min='1' max='10' value='1'></label></div>";
    html += "<div><label>Aggiornamento stato (s)<input id='wizPollSec' class='wiz-input' type='number' min='2' max='600' value='10'></label></div>";
    html += "</div>";
    html += "<div class='wiz-actions'>";
    html += "<button id='wizStartBtn' class='wiz-btn wiz-start' onclick='startTestWizardFromWeb()'>Avvia Test</button>";
    html += "<button id='wizStopBtn' class='wiz-btn wiz-stop' onclick=\"stopTestWizardFromWeb('save')\">Stop + Salva</button>";
    html += "<button id='wizAbortBtn' class='wiz-btn wiz-abort' onclick=\"stopTestWizardFromWeb('abort')\">Annulla</button>";
    html += "</div>";
    html += "<div id='wizMessage' class='wiz-msg'></div>";
    html += "<div class='wiz-status'>";
    html += "<div><b>Stato:</b> <span id='wizState'>--</span></div>";
    html += "<div><b>Stage:</b> <span id='wizStage'>--</span></div>";
    html += "<div><b>Sessione:</b> <span id='wizSession'>--</span></div>";
    html += "<div><b>Progresso:</b> <span id='wizProgress'>--</span></div>";
    html += "<div><b>Campioni:</b> <span id='wizSamples'>--</span></div>";
    html += "<div><b>Campionamento:</b> <span id='wizSampling'>--</span></div>";
    html += "<div><b>DeltaP raw:</b> <span id='wizRaw'>--</span></div>";
    html += "<div><b>DeltaP filtrato:</b> <span id='wizFiltered'>--</span></div>";
    html += "<div><b>Feed live:</b> <span id='wizLive'>--</span></div>";
    html += "<div><b>Soglia:</b> <span id='wizThreshold'>--</span></div>";
    html += "<div><b>Sensori:</b> <span id='wizSensors'>--</span></div>";
    html += "<div><b>SPIFFS:</b> <span id='wizSpiffs'>--</span></div>";
    html += "<div><b>File temp:</b> <span id='wizTemp'>--</span></div>";
    html += "<div><b>CSV finale:</b> <span id='wizFinal'>--</span></div>";
    html += "<div><b>Ultimo esito:</b> <span id='wizLast'>--</span></div>";
    html += "</div>";
    html += "</div>";

    // --- SCRIPT PER AGGIORNAMENTO AUTOMATICO (AUTO-UPDATE SCRIPT) ---
    html += "<script>";
    html += "function downloadTestCsv(level) {";
    html += "  const url = '/download_test_csv?level=' + level + '&_ts=' + Date.now();";
    html += "  window.location.href = url;";
    html += "}";
    html += "function deleteTestCsv(level) {";
    html += "  if (!confirm('Confermi cancellazione CSV livello ' + level + '?')) return;";
    html += "  fetch('/delete_test_csv?level=' + level, { method: 'POST' })";
    html += "    .then(r => r.text().then(t => ({ ok: r.ok, text: t })))";
    html += "    .then(res => { alert(res.text); if (res.ok) window.location.reload(); })";
    html += "    .catch(() => alert('Errore durante cancellazione CSV.'));";
    html += "}";
    html += "function deleteAllTestCsv() {";
    html += "  if (!confirm('Confermi cancellazione di tutti i CSV test e file temporanei?')) return;";
    html += "  fetch('/delete_test_csv?all=1', { method: 'POST' })";
    html += "    .then(r => r.text().then(t => ({ ok: r.ok, text: t })))";
    html += "    .then(res => { alert(res.text); if (res.ok) window.location.reload(); })";
    html += "    .catch(() => alert('Errore durante cancellazione CSV.'));";
    html += "}";
    html += "let wizTimer = null;";
    html += "function wizPollMs(){";
    html += "  const el = document.getElementById('wizPollSec');";
    html += "  const v = el ? parseInt(el.value, 10) : 10;";
    html += "  if (isNaN(v)) return 10000;";
    html += "  return Math.max(2000, Math.min(600000, v * 1000));";
    html += "}";
    html += "function wizSetMessage(msg, isError){";
    html += "  const el = document.getElementById('wizMessage');";
    html += "  if (!el) return;";
    html += "  el.style.color = isError ? '#b22222' : '#2f7a32';";
    html += "  el.innerText = msg || ''; ";
    html += "}";
    html += "function wizScheduleNext(){";
    html += "  if (wizTimer) clearTimeout(wizTimer);";
    html += "  wizTimer = setTimeout(updateTestWizardStatus, wizPollMs());";
    html += "}";
    html += "function wizRenderStatus(data){";
    html += "  const state = document.getElementById('wizState');";
    html += "  const stage = document.getElementById('wizStage');";
    html += "  const session = document.getElementById('wizSession');";
    html += "  const progress = document.getElementById('wizProgress');";
    html += "  const samples = document.getElementById('wizSamples');";
    html += "  const sampling = document.getElementById('wizSampling');";
    html += "  const raw = document.getElementById('wizRaw');";
    html += "  const filtered = document.getElementById('wizFiltered');";
    html += "  const live = document.getElementById('wizLive');";
    html += "  const threshold = document.getElementById('wizThreshold');";
    html += "  const sensors = document.getElementById('wizSensors');";
    html += "  const spiffs = document.getElementById('wizSpiffs');";
    html += "  const temp = document.getElementById('wizTemp');";
    html += "  const final = document.getElementById('wizFinal');";
    html += "  const last = document.getElementById('wizLast');";
    html += "  if (state) state.innerText = data.status_text || '--';";
    html += "  if (stage) stage.innerText = data.stage || '--';";
    html += "  if (session) session.innerText = (data.session_id || 0) + ' | livello ' + (data.dirt_level || 0) + ' | velocita ' + (data.speed_index || 0) + '/' + (data.total_speeds || 0);";
    html += "  const dur = Number(data.duration_s || 0);";
    html += "  const ela = Number(data.elapsed_s || 0);";
    html += "  const rem = Number(data.remaining_s || 0);";
    html += "  const exp = Number(data.expected_samples || 0);";
    html += "  const miss = Number(data.missing_samples || 0);";
    html += "  const pct = (dur > 0) ? Math.min(100, Math.round((ela * 100) / dur)) : 0;";
    html += "  if (progress) progress.innerText = ela + 's / ' + dur + 's (restanti ' + rem + 's, ' + pct + '%, attesi ' + exp + ' campioni)';";
    html += "  const s = Number(data.samples || 0);";
    html += "  const rv = Number(data.raw_valid_samples || 0);";
    html += "  const fv = Number(data.filtered_valid_samples || 0);";
    html += "  const vp = Number(data.valid_pct || 0);";
    html += "  if (samples) samples.innerText = s + ' totali, raw validi ' + rv + ' (' + vp + '%), filtrati validi ' + fv;";
    html += "  const lastSample = Number(data.last_sample_uptime_ms || 0);";
    html += "  const lag = Number(data.sample_lag_ms || 0);";
    html += "  if (sampling) sampling.innerText = 'mancanti ' + miss + ', ultimo sample uptime ' + lastSample + ' ms, lag ' + lag + ' ms';";
    html += "  if (raw) {";
    html += "    if (data.raw_stats_ready) raw.innerText = data.raw_min + ' / ' + data.raw_avg + ' / ' + data.raw_max + ' Pa';";
    html += "    else raw.innerText = 'non disponibile';";
    html += "  }";
    html += "  if (filtered) {";
    html += "    if (data.filtered_stats_ready) filtered.innerText = data.filtered_min + ' / ' + data.filtered_avg + ' / ' + data.filtered_max + ' Pa';";
    html += "    else filtered.innerText = 'non disponibile';";
    html += "  }";
    html += "  const rawLive = data.raw_live_valid ? (data.raw_live_pa + ' Pa') : 'N/D';";
    html += "  const filtLive = data.filtered_live_valid ? (data.filtered_live_pa + ' Pa') : 'N/D';";
    html += "  const fb = data.fallback_valid ? (data.fallback_pa + ' Pa') : 'N/D';";
    html += "  if (live) live.innerText = 'raw ' + rawLive + ' | filtrato ' + filtLive + ' | fallback ' + fb;";
    html += "  if (threshold) threshold.innerText = (data.threshold_msg && data.threshold_msg.length > 0) ? data.threshold_msg : 'OK';";
    html += "  if (sensors) sensors.innerText = 'attivi ' + (data.active_slaves || 0) + ', recenti ' + (data.recent_slaves || 0) + ' (grp1=' + (data.grp1_recent || 0) + ', grp2=' + (data.grp2_recent || 0) + ')';";
    html += "  if (spiffs) spiffs.innerText = 'libero ' + (data.spiffs_free_bytes || 0) + ' / ' + (data.spiffs_total_bytes || 0) + ' B, usato ' + (data.spiffs_used_bytes || 0) + ' B, stimato test ' + (data.estimated_temp_bytes || 0) + ' B, richiesto ' + (data.required_bytes || 0) + ' B';";
    html += "  if (temp) temp.innerText = (data.temp_path || '--') + ' | open=' + (!!data.temp_open) + ', exists=' + (!!data.temp_exists) + ', size=' + (data.temp_size_bytes || 0) + ' B, writeErr=' + (data.temp_write_error || 0);";
    html += "  if (final) final.innerText = (data.final_path || '--') + ' | exists=' + (!!data.final_exists) + ', size=' + (data.final_size_bytes || 0) + ' B';";
    html += "  if (last) {";
    html += "    if (data.last_available) {";
    html += "      const ls = Number(data.last_samples || 0);";
    html += "      const lrv = Number(data.last_raw_valid_samples || 0);";
    html += "      const lfv = Number(data.last_filtered_valid_samples || 0);";
    html += "      const lvp = Number(data.last_valid_pct || 0);";
    html += "      let txt = (data.last_status || '--') + ' | origin=' + (data.last_origin || '--');";
    html += "      txt += ' | sessione=' + (data.last_session_id || 0) + ' liv=' + (data.last_dirt_level || 0) + ' vel=' + (data.last_speed_index || 0) + '/' + (data.last_total_speeds || 0);";
    html += "      txt += ' | campioni=' + ls + ' raw=' + lrv + ' (' + lvp + '%) filt=' + lfv;";
    html += "      if (data.last_reason && data.last_reason.length > 0) txt += ' | motivo=' + data.last_reason;";
    html += "      txt += ' | uptime=' + (data.last_end_uptime_ms || 0) + ' ms';";
    html += "      last.innerText = txt;";
    html += "    } else {";
    html += "      last.innerText = 'nessun esito registrato';";
    html += "    }";
    html += "  }";
    html += "  const startBtn = document.getElementById('wizStartBtn');";
    html += "  const stopBtn = document.getElementById('wizStopBtn');";
    html += "  const abortBtn = document.getElementById('wizAbortBtn');";
    html += "  if (startBtn) startBtn.disabled = !data.can_start;";
    html += "  if (stopBtn) stopBtn.disabled = !data.running;";
    html += "  if (abortBtn) abortBtn.disabled = !(data.running || data.wizard_waiting);";
    html += "}";
    html += "function updateTestWizardStatus(){";
    html += "  fetch('/testwiz_status?_ts=' + Date.now())";
    html += "    .then(r => r.json())";
    html += "    .then(data => wizRenderStatus(data))";
    html += "    .catch(err => console.error('Errore stato wizard:', err))";
    html += "    .finally(() => wizScheduleNext());";
    html += "}";
    html += "function startTestWizardFromWeb(){";
    html += "  const total = parseInt(document.getElementById('wizTotalSpeeds').value, 10) || 0;";
    html += "  const dirt = parseInt(document.getElementById('wizDirtLevel').value, 10) || 0;";
    html += "  const speed = parseInt(document.getElementById('wizSpeedIndex').value, 10) || 0;";
    html += "  const body = new URLSearchParams({ total_speeds: total, dirt_level: dirt, speed_index: speed }).toString();";
    html += "  fetch('/testwiz_start', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body })";
    html += "    .then(r => r.json())";
    html += "    .then(res => { wizSetMessage(res.message || '', !res.ok); updateTestWizardStatus(); })";
    html += "    .catch(() => wizSetMessage('Errore avvio wizard da web.', true));";
    html += "}";
    html += "function stopTestWizardFromWeb(action){";
    html += "  const body = new URLSearchParams({ action: action }).toString();";
    html += "  fetch('/testwiz_stop', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body })";
    html += "    .then(r => r.json())";
    html += "    .then(res => { wizSetMessage(res.message || '', !res.ok); updateTestWizardStatus(); })";
    html += "    .catch(() => wizSetMessage('Errore stop wizard da web.', true));";
    html += "}";
    html += "function updateData() {";
    html += "  fetch('/dashboard_data')";
    html += "    .then(response => response.json())";
    html += "    .then(data => {";
    html += "      document.getElementById('deltaP').innerHTML = data.deltaP;";
    html += "      const w = document.getElementById('deltaPWarning');";
    html += "      if (w) {";
    html += "        if (data.warning && data.warning.length > 0) {";
    html += "          w.innerText = data.warning;";
    html += "          w.style.display = 'block';";
    html += "        } else {";
    html += "          w.innerText = '';";
    html += "          w.style.display = 'none';";
    html += "        }";
    html += "      }";
    html += "      if (data.slaves) {";
    html += "        data.slaves.forEach(slave => {";
    html += "          const pressEl = document.getElementById('press-' + slave.id);";
    html += "          if (pressEl) pressEl.innerHTML = slave.press;";
    html += "          const tempEl = document.getElementById('temp-' + slave.id);";
    html += "          if (tempEl) tempEl.innerHTML = slave.temp;";
    html += "        });";
    html += "      }";
    // Pianifica la prossima richiesta SOLO dopo aver finito questa (evita sovrapposizioni)
    html += "      setTimeout(updateData, 2000);"; 
    html += "    })";
    html += "    .catch(error => {";
    html += "      console.error('Errore aggiornamento dati:', error);";
    html += "      setTimeout(updateData, 2000);"; // Riprova anche in caso di errore
    html += "    });";
    html += "}";
    // Avvia al caricamento della pagina
    html += "document.addEventListener('DOMContentLoaded', function(){";
    html += "  updateData();";
    html += "  updateTestWizardStatus();";
    html += "  const pollEl = document.getElementById('wizPollSec');";
    html += "  if (pollEl) pollEl.addEventListener('change', function(){ wizScheduleNext(); });";
    html += "});";
    html += "</script>";

    html += "</div></body></html>";
    return html;
}

// --- GESTIONE WIFI (SCANSIONE E CONNESSIONE) ---
void handleWifiPage() {    
    const bool standaloneProfile = isStandaloneWifiProfileActive();
    const String localHost = standaloneProfile ? "antraluxstandalone.local" : "antraluxrewamping.local";
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<link rel='icon' href='" + String(FAVICON_IMG) + "'>";
    html += "<style>body{font-family:'Segoe UI', sans-serif; background-color:#f0f2f5; padding:20px; color:#333;} ";
    html += ".container{max-width:600px; margin:0 auto;}";
    html += ".card{background:white; padding:20px; border-radius:10px; box-shadow:0 2px 5px rgba(0,0,0,0.1); margin-bottom:20px;}";
    html += "h2, h3{color:#0056b3; margin-top:0;}";
    html += ".net{padding:10px; border-bottom:1px solid #eee; cursor:pointer; display:flex; justify-content:space-between;}";
    html += ".net:hover{background-color:#f9f9f9;}";
    html += "input[type='text'], input[type='password'] {padding:10px; width:100%; margin:5px 0 15px 0; border-radius:5px; border:1px solid #ccc; box-sizing:border-box;}";
    html += "label{font-weight:bold; font-size:0.9em;}";
    html += ".btn{background:#28a745; color:white; border:none; padding:12px; width:100%; border-radius:5px; font-weight:bold; cursor:pointer;}";
    html += ".btn-back{background:#6c757d; margin-top:10px;}";
    html += ".hidden{display:none;}";
    html += "</style>";
    html += "<script>";
    html += "function selNet(ssid) { document.getElementById('ssid').value = ssid; document.getElementById('pass').focus(); }";
    html += "function toggleStatic() { var d = document.getElementById('static_div'); d.classList.toggle('hidden'); }";
    html += "function toggleKey(id) { var x = document.getElementById(id); if(x){ x.type = (x.type === 'password') ? 'text' : 'password'; } }";
    html += "</script>";
    html += "</head><body><div class='container'>";

    // --- 1. STATO CONNESSIONE ---
    if (WiFi.status() == WL_CONNECTED) {
        html += "<div class='card'>";
        html += "<h3>Stato Connessione</h3>";
        html += "<p><b>SSID:</b> " + WiFi.SSID() + "</p>";
        html += "<p><b>IP:</b> " + WiFi.localIP().toString() + "</p>";
        html += "<p><b>Gateway:</b> " + WiFi.gatewayIP().toString() + "</p>";
        html += "<p><b>Signal:</b> " + String(WiFi.RSSI()) + " dBm</p>";
        html += "</div>";
    }

    // --- 2. LISTA RETI WIFI ---
    html += "<div class='card'>";
    html += "<div style='position:relative;'>";
    html += "<h3>Reti WiFi Rilevate</h3>";
    html += "<a href='/wifi' style='position:absolute; right:0; top:0; font-size:1.5em; text-decoration:none; color:#28a745;' title='Aggiorna'>&#x21bb;</a>";
    html += "</div>";
    html += "<div style='max-height:200px; overflow-y:auto; border:1px solid #ddd; border-radius:5px;'>";
    int n = WiFi.scanNetworks();
    if (n == 0) {
        html += "<div style='padding:10px;'>Nessuna rete trovata.</div>";
    } else {
        for (int i = 0; i < n; ++i) {
            html += "<div class='net' onclick='selNet(\"" + WiFi.SSID(i) + "\")'>";
            html += "<span>" + WiFi.SSID(i) + "</span><span style='color:#666;'>" + String(WiFi.RSSI(i)) + " dBm</span></div>";
        }
    }
    html += "</div></div>";

    // Recupero valori salvati
    Preferences pref;
    pref.begin("easy", true);
    bool st = pref.getBool("static_ip", false);
    String ip = pref.isKey("ip") ? pref.getString("ip") : "";
    String sub = pref.isKey("sub") ? pref.getString("sub") : "255.255.255.0";
    String gw = pref.isKey("gw") ? pref.getString("gw") : "";
    bool apAlways = pref.getBool("ap_always", false);
    bool standaloneWifiBoot = pref.getBool("st_wifi_boot", false);
    String ssidSaved = pref.getString("ssid", "");
    String passSaved = pref.getString("pass", "");
    String api = pref.isKey("api_url") ? pref.getString("api_url") : "";
    String key = pref.isKey("apiKey") ? pref.getString("apiKey") : "";
    String custApi = pref.isKey("custApiUrl") ? pref.getString("custApiUrl") : "";
    String custKey = pref.isKey("custApiKey") ? pref.getString("custApiKey") : "";
    pref.end();

    // --- 3. FORM RETE (separato dal salvataggio API) ---
    html += "<form action='/save_wifi' method='POST'>";
    html += "<div class='card'>";
    html += "<h3>Configurazione Rete</h3>";
    html += "<label>SSID (Nome Rete)</label>";
    html += "<input type='text' name='ssid' id='ssid' value='" + ssidSaved + "' placeholder='Seleziona o scrivi...'>";
    html += "<label>Password</label>";
    html += "<input type='password' name='pass' id='pass' value='" + passSaved + "' placeholder='Password WiFi'>";
    html += "<div style='margin-bottom:15px;'>";
    html += "<input type='checkbox' name='static_ip' onchange='toggleStatic()' " + String(st ? "checked" : "") + "> Usa Indirizzo IP Statico (Manuale)";
    html += "</div>";
    html += "<div id='static_div' class='" + String(st ? "" : "hidden") + "'>";
    html += "<label>Indirizzo IP</label><input type='text' name='ip' value='" + ip + "' placeholder='es. 192.168.1.100'>";
    html += "<label>Subnet Mask</label><input type='text' name='sub' value='" + sub + "' placeholder='es. 255.255.255.0'>";
    html += "<label>Gateway</label><input type='text' name='gw' value='" + gw + "' placeholder='es. 192.168.1.1'>";
    html += "</div>";
    html += "</div>";
    html += "<div class='card'>";
    html += "<h3>Access Point Interno</h3>";
    html += "<p style='font-size:0.9em; color:#666;'>mDNS " + localHost + " disponibile quando il WiFi e' attivo.</p>";
    if (standaloneProfile) {
        html += "<input type='checkbox' name='st_wifi_boot' " + String(standaloneWifiBoot ? "checked" : "") + "> <b>Mantieni WiFi attivo anche dopo riavvio</b>";
    } else {
        html += "<input type='checkbox' name='ap_always' " + String(apAlways ? "checked" : "") + "> <b>Mantieni AP Sempre Attivo</b>";
    }
    html += "</div>";
    html += "<input type='submit' value='SALVA SOLO WIFI/RETE' class='btn'>";
    html += "</form>";

    // --- 4. FORM API (separato dal WiFi) ---
    if (standaloneProfile) {
        String apiView = escapeHtmlAttr(api.length() > 0 ? api : "NON IMPOSTATO");
        String keyView = escapeHtmlAttr(key.length() > 0 ? key : "NON IMPOSTATA");
        String custApiView = escapeHtmlAttr(custApi.length() > 0 ? custApi : "NON IMPOSTATO");
        String custKeyView = escapeHtmlAttr(custKey.length() > 0 ? custKey : "NON IMPOSTATA");
        html += "<div class='card'>";
        html += "<h3>Configurazione API Server</h3>";
        html += "<p style='font-size:0.9em; color:#666;'>In modalita' Standalone le API sono impostate in produzione e non modificabili da questa pagina.</p>";
        html += "<label>URL API Factory (sola lettura)</label>";
        html += "<input type='text' value='" + apiView + "' readonly>";
        html += "<label>API Key Factory (sola lettura)</label>";
        html += "<input type='text' value='" + keyView + "' readonly>";
        html += "<hr>";
        html += "<label>URL API Cliente (sola lettura)</label>";
        html += "<input type='text' value='" + custApiView + "' readonly>";
        html += "<label>API Key Cliente (sola lettura)</label>";
        html += "<input type='text' value='" + custKeyView + "' readonly>";
        html += "</div>";
    } else {
        html += "<form action='/save_api' method='POST'>";
        html += "<div class='card'>";
        html += "<h3>Configurazione API Server</h3>";
        html += "<label>URL API Factory (Antralux)</label>";
        html += "<input type='text' name='api_url' value='" + api + "' placeholder='https://.../api.php'>";
        html += "<label>API Key Factory</label>";
        html += "<div style='position:relative'>";
        html += "<input type='password' name='api_key' id='api_key_factory' value='" + key + "' placeholder='Chiave API Factory' style='padding-right:40px;'>";
        html += "<span onclick=\"toggleKey('api_key_factory')\" style='position:absolute; right:10px; top:15px; cursor:pointer; font-size:1.2em;'>&#128065;</span>";
        html += "</div>";
        html += "<hr>";
        html += "<label>URL API Cliente (opzionale)</label>";
        html += "<input type='text' name='cust_api_url' value='" + custApi + "' placeholder='https://.../api.php'>";
        html += "<label>API Key Cliente (opzionale)</label>";
        html += "<div style='position:relative'>";
        html += "<input type='password' name='cust_api_key' id='api_key_customer' value='" + custKey + "' placeholder='Chiave API Cliente' style='padding-right:40px;'>";
        html += "<span onclick=\"toggleKey('api_key_customer')\" style='position:absolute; right:10px; top:15px; cursor:pointer; font-size:1.2em;'>&#128065;</span>";
        html += "</div>";
        html += "</div>";
        html += "<input type='submit' value='SALVA SOLO API' class='btn'>";
        html += "</form>";
    }

    html += "<a href='/'><button class='btn btn-back'>TORNA ALLA DASHBOARD</button></a>";
    html += "</div></body></html>";
    
    server.send(200, "text/html", html);
}

void handleSaveWifi() {
    const bool standaloneProfile = isStandaloneWifiProfileActive();
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    
    bool staticIp = server.hasArg("static_ip");
    String ip = server.arg("ip");
    String sub = server.arg("sub");
    String gw = server.arg("gw");
    
    bool apAlways = server.hasArg("ap_always");
    bool standaloneWifiBoot = server.hasArg("st_wifi_boot");

    // Salvataggio in NVS
    Preferences pref;
    pref.begin("easy", false);
    pref.putString("ssid", ssid);
    pref.putString("pass", pass);
    pref.putBool("static_ip", staticIp);
    if (staticIp) {
        pref.putString("ip", ip);
        pref.putString("sub", sub);
        pref.putString("gw", gw);
    } else {
        pref.remove("ip");
        pref.remove("sub");
        pref.remove("gw");
    }
    if (standaloneProfile) {
        pref.putBool("st_wifi_boot", standaloneWifiBoot);
    } else {
        pref.putBool("ap_always", apAlways);
    }
    pref.end();

    server.send(200, "text/html", "<html><body><h2>WiFi salvato!</h2><p>Riavvio in corso per applicare la rete: " + ssid + ".</p></body></html>");
    delay(1500);
    ESP.restart();
}

void handleSaveApi() {
    if (isStandaloneWifiProfileActive()) {
        server.send(403, "text/plain", "Configurazione API bloccata in modalita' Standalone.");
        return;
    }

    String apiUrl = server.arg("api_url");
    String apiKey = server.arg("api_key");
    String custApiUrl = server.arg("cust_api_url");
    String custApiKey = server.arg("cust_api_key");

    Preferences pref;
    pref.begin("easy", false);
    pref.putString("api_url", apiUrl);
    pref.putString("apiKey", apiKey);
    pref.putString("custApiUrl", custApiUrl);
    pref.putString("custApiKey", custApiKey);
    pref.end();

    apiUrl.toCharArray(config.apiUrl, sizeof(config.apiUrl));
    apiKey.toCharArray(config.apiKey, sizeof(config.apiKey));
    custApiUrl.toCharArray(config.customerApiUrl, sizeof(config.customerApiUrl));
    custApiKey.toCharArray(config.customerApiKey, sizeof(config.customerApiKey));

    server.send(200, "text/html", "<html><body><h2>API salvate!</h2><p>Configurazione aggiornata senza modificare la rete WiFi.</p><p><a href='/wifi'>Torna alla pagina WiFi</a></p></body></html>");
}

void handleConnect() {
    // Compatibilita' con form legacy.
    handleSaveWifi();
}

// --- INVIO DATI JSON ---
void handleData() {
    String json = "{";
    json += "\"internet\":" + String(statoInternet) + ",";
    json += "\"scansione\":" + String(scansioneInCorso ? 1 : 0);
    json += "}";
    server.send(200, "application/json", json);
}

// --- INVIO DATI JSON PER DASHBOARD ---
void handleDashboardData() {
    bool filteredValid = isFilteredDeltaPValid();
    float filteredDelta = getFilteredDeltaP();
    float dashboardDelta = 0.0f;
    bool dashboardDeltaValid = false;
    if (filteredValid) {
        dashboardDelta = filteredDelta;
        dashboardDeltaValid = true;
    } else if (currentDeltaPValid) {
        dashboardDelta = currentDeltaP;
        dashboardDeltaValid = true;
    } else {
        dashboardDeltaValid = computeLiveDeltaPForDashboard(dashboardDelta);
    }

    String deltaPStr = dashboardDeltaValid ? (String(dashboardDelta, 1) + " Pa") : "In attesa dati";
    String warningMsg = checkThresholds(filteredDelta);
    String warningEscaped = warningMsg;
    warningEscaped.replace("\\", "\\\\");
    warningEscaped.replace("\"", "\\\"");

    // Costruzione JSON (JSON Construction)
    String json = "{";
    json += "\"deltaP\":\"" + deltaPStr + "\",";
    json += "\"warning\":\"" + warningEscaped + "\",";
    json += "\"slaves\":[";

    bool firstSlave = true;
    for (int i = 1; i <= 100; i++) {
        if (listaPerifericheAttive[i]) {
            if (!firstSlave) {
                json += ",";
            }
            json += "{";
            json += "\"id\":" + String(i) + ",";
            json += "\"press\":\"" + String(databaseSlave[i].p, 0) + " Pa\",";
            json += "\"ver\":\"" + String(databaseSlave[i].version) + "\",";
            json += "\"temp\":\"" + String(databaseSlave[i].t, 1) + " &deg;C\"";
            json += "}";
            firstSlave = false;
        }
    }

    json += "]}";
    // Chiude la connessione subito dopo l'invio per evitare "Connection reset by peer"
    server.sendHeader("Connection", "close");
    server.send(200, "application/json", json);
}

// --- SETUP SERVER ---
void setupMasterWiFi() {
    const bool standaloneProfile = isStandaloneWifiProfileActive();
    const char* hostName = wifiHostNameByProfile();
    const char* apSsid = wifiApSsidByProfile();
    const char* apPass = wifiApPasswordByProfile();
    resetMdnsState(true);

    // Prova a connettersi come Client se e presente un SSID salvato
    Preferences pref;
    pref.begin("easy", true);
    String s = pref.getString("ssid", "");
    String p = pref.getString("pass", "");
    
    bool staticIp = pref.getBool("static_ip", false);
    String ipStr = pref.isKey("ip") ? pref.getString("ip") : "";
    String subStr = pref.isKey("sub") ? pref.getString("sub") : "";
    String gwStr = pref.isKey("gw") ? pref.getString("gw") : "";
    String k = pref.isKey("apiKey") ? pref.getString("apiKey") : "";
    k.toCharArray(config.apiKey, 65);
    
    pref.end();

    // Imposta hostname prima della connessione STA (DHCP option 12).
    WiFi.setHostname(hostName);

    if (s != "") {
        // Configurazione IP Statico se richiesto
        if (staticIp && ipStr != "") {
            IPAddress ip, sub, gw;
            if(ip.fromString(ipStr) && sub.fromString(subStr) && gw.fromString(gwStr)) {
                WiFi.config(ip, gw, sub);
                Serial.println("[WIFI] Configurazione IP Statico applicata.");
            }
        }

        WiFi.begin(s.c_str(), p.c_str());
        Serial.print("[WIFI] Connessione a " + s);
    }

    // Avvio Iniziale AP (Sempre attivo all'avvio per sicurezza, poi gestito dal loop)
    // Se non c'è rete configurata, resta attivo.
    WiFi.softAP(apSsid, apPass);
    dnsServer.start(53, "*", WiFi.softAPIP());
    apEnabled = true;
    Serial.printf("[WIFI] Access Point Aperto: %s\n", apSsid);

    // mDNS disponibile in AP e STA (se WiFi non e' OFF).
    ensureMdnsService();

    if (!webServerConfigured) {
        // Gestione Favicon per evitare errori nel log
        server.on("/favicon.ico", []() { server.send(204); });

        // Gestione Root (Home Page)
        server.on("/", []() {
            if (isStandaloneWifiProfileActive()) {
                handleWifiPage();
            } else {
                server.send(200, "text/html", getDashboardHTML());
            }
        });

        // Setup Calibrazione solo in Rewamping
        if (!standaloneProfile) {
            setupCalibration();
        }

        server.on("/dashboard_data", handleDashboardData); // Nuovo endpoint per i dati (New endpoint for data)
        server.on("/data", handleData);
        server.on("/download_test_csv", handleDownloadTestCsv);
        server.on("/delete_test_csv", HTTP_POST, handleDeleteTestCsv);
        server.on("/testwiz_status", handleTestWizardStatus);
        server.on("/testwiz_start", HTTP_POST, handleTestWizardStart);
        server.on("/testwiz_stop", HTTP_POST, handleTestWizardStop);
        server.on("/wifi", handleWifiPage);
        server.on("/connect", HTTP_POST, handleConnect);
        server.on("/save_wifi", HTTP_POST, handleSaveWifi);
        server.on("/save_api", HTTP_POST, handleSaveApi);

        // Captive Portal (Android/Windows/iOS)
        server.on("/generate_204", []() {
            if (isStandaloneWifiProfileActive()) {
                handleWifiPage();
            } else {
                server.send(200, "text/html", getDashboardHTML());
            }
        });
        server.on("/fwlink", []() {
            if (isStandaloneWifiProfileActive()) {
                handleWifiPage();
            } else {
                server.send(200, "text/html", getDashboardHTML());
            }
        });

        server.onNotFound([]() {
            server.sendHeader("Location", "/", true);
            server.send(302, "text/plain", "");
        });

        server.begin();
        webServerConfigured = true;
    }
}

void setupStandaloneWifiPortal() {
    standaloneWifiProfile = true;
    setupMasterWiFi();
}

void forceWifiOffForLab() {
    wifiForceOff = true;
    wifiRetryCount = 0;
    statoInternet = 0;

    WiFi.disconnect(true, true);
    WiFi.softAPdisconnect(true);
    dnsServer.stop();
    WiFi.mode(WIFI_OFF);
    resetMdnsState(true);

    apEnabled = false;
    Serial.println("[WIFI] Modalita' LAB: WiFi e AP forzati OFF.");
}

void forceWifiOnForLab() {
    const char* hostName = wifiHostNameByProfile();
    const char* apSsid = wifiApSsidByProfile();
    const char* apPass = wifiApPasswordByProfile();

    wifiForceOff = false;
    wifiRetryCount = 0;
    statoInternet = 0;
    lastWifiCheck = 0; // forza ciclo gestione immediato

    Preferences pref;
    pref.begin("easy", true);
    String ssid = pref.getString("ssid", "");
    String pass = pref.getString("pass", "");
    bool staticIp = pref.getBool("static_ip", false);
    String ipStr = pref.isKey("ip") ? pref.getString("ip") : "";
    String subStr = pref.isKey("sub") ? pref.getString("sub") : "";
    String gwStr = pref.isKey("gw") ? pref.getString("gw") : "";
    pref.end();

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(hostName);
    if (staticIp && ipStr.length() > 0) {
        IPAddress ip, sub, gw;
        if (ip.fromString(ipStr) && sub.fromString(subStr) && gw.fromString(gwStr)) {
            WiFi.config(ip, gw, sub);
            Serial.println("[WIFI] Configurazione IP statico applicata.");
        }
    }

    if (ssid.length() > 0) {
        WiFi.begin(ssid.c_str(), pass.c_str());
        Serial.printf("[WIFI] Riattivazione WiFi verso SSID: %s\n", ssid.c_str());
    } else {
        Serial.println("[WIFI] Nessun SSID salvato: avvio solo AP locale.");
    }

    WiFi.softAP(apSsid, apPass);
    dnsServer.start(53, "*", WiFi.softAPIP());
    apEnabled = true;
    resetMdnsState(true);
    ensureMdnsService();
    Serial.printf("[WIFI] AP locale attivo: %s\n", apSsid);
}

void gestisciWebEWiFi() {
    if (wifiForceOff) {
        statoInternet = 0;
        return;
    }

    dnsServer.processNextRequest();
    server.handleClient();
    ensureMdnsService();

    // --- LOGICA AUTOMATICA AP E RICONNESSIONE ---
    unsigned long now = millis();
    if (now - lastWifiCheck > 5000) { // Controllo ogni 5 secondi
        lastWifiCheck = now;
        const char* apSsid = wifiApSsidByProfile();
        const char* apPass = wifiApPasswordByProfile();

        Preferences pref;
        pref.begin("easy", true);
        bool apAlwaysOn = isStandaloneWifiProfileActive()
                              ? pref.getBool("st_wifi_boot", false)
                              : pref.getBool("ap_always", false);
        pref.end();

        if (WiFi.status() == WL_CONNECTED) {
            statoInternet = 2; // Connesso
            wifiRetryCount = 0; // Resetta il contatore se la connessione ha successo
            ensureMdnsService();

            // Se connesso, AP è attivo e NON deve essere sempre attivo -> Spegni AP
            if (!apAlwaysOn && apEnabled) {
                 WiFi.softAPdisconnect(true);
                 dnsServer.stop();
                 WiFi.mode(WIFI_STA);
                 apEnabled = false;
                 resetMdnsState(true);
                 ensureMdnsService();
                 Serial.println("[WIFI] Connesso a Internet. AP Disattivato (Auto).");
            }
            // Se connesso, l'utente vuole l'AP sempre attivo ma è spento -> Accendi AP
            else if (apAlwaysOn && !apEnabled) {
                 WiFi.mode(WIFI_AP_STA);
                 WiFi.softAP(apSsid, apPass);
                 dnsServer.start(53, "*", WiFi.softAPIP());
                 apEnabled = true;
                 resetMdnsState(true);
                 ensureMdnsService();
                 Serial.println("[WIFI] AP Riattivato (Impostazione Sempre Attivo).");
            }
        } else {
            statoInternet = 0; // Disconnesso
            ensureMdnsService();

            // Se la modalità "Sempre Attivo" è disabilitata, gestiamo i tentativi
            if (!apAlwaysOn) {
                // Se non sta già tentando di riconnettersi, avvia il processo
                if (WiFi.getMode() != WIFI_STA) {
                    Preferences reconnectPref;
                    reconnectPref.begin("easy", true);
                    String ssid = reconnectPref.getString("ssid", "");
                    String pass = reconnectPref.getString("pass", "");
                    reconnectPref.end();
                    if (ssid.length() > 0) {
                        WiFi.begin(ssid.c_str(), pass.c_str());
                    }
                }
                
                wifiRetryCount++; // Incrementa il contatore dei tentativi
                Serial.printf("[WIFI] Connessione persa. Tentativo %d/10...\n", wifiRetryCount);

                // Se superiamo i 10 tentativi e l'AP è spento, lo attiviamo come fallback
                if (wifiRetryCount > 10 && !apEnabled) {
                    if (isStandaloneWifiProfileActive()) {
                        WiFi.mode(WIFI_AP_STA);
                        WiFi.softAP(apSsid, apPass);
                    } else {
                        String apName = "EasyConnect-" + String(config.serialeID);
                        if (String(config.serialeID) == "NON_SET") apName = "EasyConnect-Recovery";
                        WiFi.mode(WIFI_AP_STA);
                        WiFi.softAP(apName.c_str(), "12345678");
                    }
                    dnsServer.start(53, "*", WiFi.softAPIP());
                    apEnabled = true;
                    resetMdnsState(true);
                    ensureMdnsService();
                    Serial.println("[WIFI] Riconnessione fallita. AP Riattivato (Recovery).");
                }
            }
        }
    }
}

