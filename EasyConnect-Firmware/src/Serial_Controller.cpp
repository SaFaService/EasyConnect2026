#include "Serial_Manager.h"
#include "GestioneMemoria.h"
#include <esp_task_wdt.h> // Per il reset del watchdog
#include "Pins.h"
#include <WiFi.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <MD5Builder.h>
#include <time.h>
#include "OTA_Manager.h" // Per la funzione di download

// La parola chiave 'extern' indica al compilatore che queste variabili sono definite
// in un altro file (in questo caso, in 'main_standalone_rewamping_controller.cpp').
extern Impostazioni config;
extern Preferences memoria;
extern const char* FW_VERSION;
extern bool debugViewData;
extern bool debugViewApi;
extern bool manualOtaActive; // Flag from main_standalone_rewamping_controller
extern bool listaPerifericheAttive[101];
extern DatiSlave databaseSlave[101];
extern void modoTrasmissione();
extern void modoRicezione();
extern void scansionaSlave();
extern void scansionaSlaveStandalone();
extern void forceWifiOffForLab();
extern void forceWifiOnForLab();
extern String checkThresholds(float currentP);
extern float getFilteredDeltaP();
extern bool isFilteredDeltaPValid();
extern float currentDeltaP;
extern bool currentDeltaPValid;

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
static String gLastThresholdMsg = "";
static unsigned long gLastThresholdCheckMs = 0;

static const unsigned long TEST_WIZARD_SAMPLE_INTERVAL_MS = 1000UL;
static const unsigned long TEST_WIZARD_DEFAULT_DURATION_MS = 900000UL; // 15 minuti
static const unsigned long TEST_WIZARD_MIN_STOP_DURATION_MS = 120000UL; // 2 minuti
static const int TEST_WIZARD_MAX_SPEEDS = 10;
static const int TEST_WIZARD_DIRTY_LEVELS = 3;
static const int TEST_WIZARD_MIN_SAMPLES = 120;
static const int TEST_WIZARD_MIN_VALID_RATIO_PCT = 70;
static const unsigned long TEST_WIZARD_SENSOR_TIMEOUT_MS = 30000UL;
static const uint32_t TEST_WIZARD_ESTIMATED_ROW_BYTES = 128U;
static const uint32_t TEST_WIZARD_FS_SAFETY_MARGIN_BYTES = 8192U;
static const char *TEST_WIZARD_CSV_HEADER =
    "session_id,uptime_ms,elapsed_s,total_speeds,dirt_level,speed_index,delta_raw_pa,raw_valid,delta_filtered_pa,filtered_valid,threshold";

enum TestWizardStage {
    TESTWIZ_STAGE_IDLE = 0,
    TESTWIZ_STAGE_WAIT_TOTAL_SPEEDS,
    TESTWIZ_STAGE_WAIT_DIRTY_LEVEL,
    TESTWIZ_STAGE_WAIT_SPEED_INDEX
};

struct DeltaPTestWizardState {
    bool spiffsReady = false;
    bool running = false;
    TestWizardStage stage = TESTWIZ_STAGE_IDLE;
    int totalSpeeds = 0;
    int dirtLevel = 0;
    int speedIndex = 0;
    unsigned long sessionId = 0;
    unsigned long startMs = 0;
    unsigned long lastSampleTick = 0;
    unsigned long targetDurationMs = TEST_WIZARD_DEFAULT_DURATION_MS;
    unsigned long totalSamples = 0;
    unsigned long rawValidSamples = 0;
    unsigned long filteredValidSamples = 0;
    float rawMin = 0.0f;
    float rawMax = 0.0f;
    float rawSum = 0.0f;
    bool rawStatsReady = false;
    float filteredMin = 0.0f;
    float filteredMax = 0.0f;
    float filteredSum = 0.0f;
    bool filteredStatsReady = false;
    String tempPath = "";
    File tempFile;
};

static DeltaPTestWizardState gTestWizard;
static bool writeCsvLineChecked(File &f, const String &line);

struct DeltaPTestWizardLastOutcome {
    bool available = false;
    bool saved = false;
    String status = "";
    String origin = "";
    String reason = "";
    unsigned long endMs = 0;
    unsigned long sessionId = 0;
    int totalSpeeds = 0;
    int dirtLevel = 0;
    int speedIndex = 0;
    unsigned long samples = 0;
    unsigned long rawValidSamples = 0;
    unsigned long filteredValidSamples = 0;
    int validPct = 0;
    bool rawStatsReady = false;
    float rawMin = 0.0f;
    float rawAvg = 0.0f;
    float rawMax = 0.0f;
    bool filteredStatsReady = false;
    float filteredMin = 0.0f;
    float filteredAvg = 0.0f;
    float filteredMax = 0.0f;
};

static DeltaPTestWizardLastOutcome gTestWizardLastOutcome;

static void storeLastWizardOutcome(const char *status, bool saved, const char *origin, const String &reason) {
    gTestWizardLastOutcome.available = true;
    gTestWizardLastOutcome.saved = saved;
    gTestWizardLastOutcome.status = status ? String(status) : "";
    gTestWizardLastOutcome.origin = origin ? String(origin) : "";
    gTestWizardLastOutcome.reason = reason;
    gTestWizardLastOutcome.endMs = millis();
    gTestWizardLastOutcome.sessionId = gTestWizard.sessionId;
    gTestWizardLastOutcome.totalSpeeds = gTestWizard.totalSpeeds;
    gTestWizardLastOutcome.dirtLevel = gTestWizard.dirtLevel;
    gTestWizardLastOutcome.speedIndex = gTestWizard.speedIndex;
    gTestWizardLastOutcome.samples = gTestWizard.totalSamples;
    gTestWizardLastOutcome.rawValidSamples = gTestWizard.rawValidSamples;
    gTestWizardLastOutcome.filteredValidSamples = gTestWizard.filteredValidSamples;
    gTestWizardLastOutcome.validPct =
        (gTestWizard.totalSamples > 0)
            ? (int)((gTestWizard.rawValidSamples * 100UL) / gTestWizard.totalSamples)
            : 0;
    gTestWizardLastOutcome.rawStatsReady = gTestWizard.rawStatsReady;
    gTestWizardLastOutcome.rawMin = gTestWizard.rawMin;
    gTestWizardLastOutcome.rawMax = gTestWizard.rawMax;
    gTestWizardLastOutcome.rawAvg =
        (gTestWizard.rawValidSamples > 0) ? (gTestWizard.rawSum / (float)gTestWizard.rawValidSamples) : 0.0f;
    gTestWizardLastOutcome.filteredStatsReady = gTestWizard.filteredStatsReady;
    gTestWizardLastOutcome.filteredMin = gTestWizard.filteredMin;
    gTestWizardLastOutcome.filteredMax = gTestWizard.filteredMax;
    gTestWizardLastOutcome.filteredAvg =
        (gTestWizard.filteredValidSamples > 0)
            ? (gTestWizard.filteredSum / (float)gTestWizard.filteredValidSamples)
            : 0.0f;
}

static String getTestWizardFinalPathForLevel(int level) {
    return "/deltap_test_sporco_" + String(level) + ".csv";
}

static String getTestWizardMetaPathForLevel(int level) {
    return "/deltap_test_sporco_" + String(level) + ".meta";
}

static bool writeTestWizardSaveMeta(int level) {
    const String metaPath = getTestWizardMetaPathForLevel(level);
    if (SPIFFS.exists(metaPath)) {
        SPIFFS.remove(metaPath);
    }

    File meta = SPIFFS.open(metaPath, "w");
    if (!meta) {
        Serial.println("[TESTWIZ] Warning: impossibile scrivere metadati ultimo salvataggio.");
        return false;
    }

    const time_t nowEpoch = time(nullptr);
    const long long savedEpoch = (nowEpoch > 1000000000) ? (long long)nowEpoch : 0LL;

    meta.printf("saved_epoch=%lld\n", savedEpoch);
    meta.printf("saved_uptime_ms=%lu\n", (unsigned long)millis());
    meta.printf("session_id=%lu\n", gTestWizard.sessionId);
    meta.printf("samples=%lu\n", gTestWizard.totalSamples);
    meta.printf("raw_valid_samples=%lu\n", gTestWizard.rawValidSamples);
    meta.printf("total_speeds=%d\n", gTestWizard.totalSpeeds);
    meta.printf("dirt_level=%d\n", gTestWizard.dirtLevel);
    meta.printf("speed_index=%d\n", gTestWizard.speedIndex);
    meta.close();
    return true;
}

static String csvEscapeField(const String &value) {
    String escaped = value;
    escaped.replace("\"", "\"\"");
    return "\"" + escaped + "\"";
}

static bool ensureSpiffsForTestWizard() {
    if (gTestWizard.spiffsReady) return true;
    if (!SPIFFS.begin(true)) {
        Serial.println("[TESTWIZ] Errore: impossibile montare SPIFFS.");
        return false;
    }
    gTestWizard.spiffsReady = true;
    return true;
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

static bool isWizardTempPath(const String &path) {
    const String p = normalizeSpiffsPath(path);
    return p.startsWith("/tmp_dptest_l") || p.startsWith("/tmp_merge_l");
}

static int cleanupWizardTempFilesFromSpiffs() {
    int removed = 0;
    File root = SPIFFS.open("/");
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return 0;
    }

    File f = root.openNextFile();
    while (f) {
        const String name = f.name();
        f.close();
        if (isWizardTempPath(name) && removeSpiffsFileFlexible(name)) {
            removed++;
        }
        f = root.openNextFile();
    }
    root.close();
    return removed;
}

static void printWizardSpiffsUsage(const char *tag) {
    if (!ensureSpiffsForTestWizard()) {
        Serial.printf("[TESTWIZ] %s SPIFFS: non disponibile.\n", tag);
        return;
    }
    const uint32_t total = SPIFFS.totalBytes();
    const uint32_t used = SPIFFS.usedBytes();
    const uint32_t freeBytes = (used <= total) ? (total - used) : 0;
    Serial.printf("[TESTWIZ] %s SPIFFS: usato=%lu, libero=%lu, totale=%lu bytes\n",
                  tag, (unsigned long)used, (unsigned long)freeBytes, (unsigned long)total);
}

static uint32_t estimateWizardTempCsvBytes(unsigned long durationMs) {
    unsigned long samples = durationMs / TEST_WIZARD_SAMPLE_INTERVAL_MS;
    if (samples == 0) samples = 1;
    const uint32_t headerBytes = (uint32_t)strlen(TEST_WIZARD_CSV_HEADER) + 2U;
    return headerBytes + ((uint32_t)samples * TEST_WIZARD_ESTIMATED_ROW_BYTES);
}

static bool hasEnoughSpiffsSpaceForWizard(unsigned long durationMs) {
    if (!ensureSpiffsForTestWizard()) return false;

    const uint32_t total = SPIFFS.totalBytes();
    const uint32_t used = SPIFFS.usedBytes();
    const uint32_t freeBytes = (used <= total) ? (total - used) : 0;
    const uint32_t estimatedBytes = estimateWizardTempCsvBytes(durationMs);
    const uint32_t requiredBytes = estimatedBytes + TEST_WIZARD_FS_SAFETY_MARGIN_BYTES;

    if (freeBytes >= requiredBytes) return true;

    Serial.printf("[TESTWIZ] Errore: spazio SPIFFS insufficiente per il test (libero=%lu, richiesto~%lu bytes).\n",
                  (unsigned long)freeBytes, (unsigned long)requiredBytes);
    return false;
}

static void resetTestWizardRunStats() {
    if (gTestWizard.tempFile) {
        gTestWizard.tempFile.close();
    }
    gTestWizard.running = false;
    gTestWizard.sessionId = 0;
    gTestWizard.startMs = 0;
    gTestWizard.lastSampleTick = 0;
    gTestWizard.totalSamples = 0;
    gTestWizard.rawValidSamples = 0;
    gTestWizard.filteredValidSamples = 0;
    gTestWizard.rawMin = 0.0f;
    gTestWizard.rawMax = 0.0f;
    gTestWizard.rawSum = 0.0f;
    gTestWizard.rawStatsReady = false;
    gTestWizard.filteredMin = 0.0f;
    gTestWizard.filteredMax = 0.0f;
    gTestWizard.filteredSum = 0.0f;
    gTestWizard.filteredStatsReady = false;
    gTestWizard.tempPath = "";
}

static void resetTestWizardWizardState() {
    gTestWizard.stage = TESTWIZ_STAGE_IDLE;
    gTestWizard.totalSpeeds = 0;
    gTestWizard.dirtLevel = 0;
    gTestWizard.speedIndex = 0;
}

static void resetAllTestWizardState() {
    resetTestWizardWizardState();
    resetTestWizardRunStats();
}

static bool parseStrictPositiveInt(const String &input, int &outValue) {
    String s = input;
    s.trim();
    if (s.length() == 0) return false;
    for (size_t i = 0; i < s.length(); i++) {
        if (!isDigit(s[i])) return false;
    }
    outValue = s.toInt();
    return true;
}

static void printTestWizardPromptForStage() {
    if (gTestWizard.stage == TESTWIZ_STAGE_WAIT_TOTAL_SPEEDS) {
        Serial.printf("[TESTWIZ] Inserisci il numero di velocita' (1-%d):\n", TEST_WIZARD_MAX_SPEEDS);
    } else if (gTestWizard.stage == TESTWIZ_STAGE_WAIT_DIRTY_LEVEL) {
        Serial.printf("[TESTWIZ] Inserisci livello sporco filtro (1-%d):\n", TEST_WIZARD_DIRTY_LEVELS);
    } else if (gTestWizard.stage == TESTWIZ_STAGE_WAIT_SPEED_INDEX) {
        Serial.printf("[TESTWIZ] Inserisci la velocita' in test (1-%d):\n", gTestWizard.totalSpeeds);
    }
}

static bool appendLineToTempTestWizardFile(const String &line) {
    if (gTestWizard.tempPath.length() == 0) {
        Serial.println("[TESTWIZ] Errore: percorso file temporaneo non valido.");
        return false;
    }
    if (!gTestWizard.tempFile) {
        gTestWizard.tempFile = SPIFFS.open(gTestWizard.tempPath, "a");
    }
    if (!gTestWizard.tempFile) {
        Serial.println("[TESTWIZ] Errore: apertura file temporaneo in append fallita.");
        return false;
    }

    const size_t written = gTestWizard.tempFile.print(line);
    gTestWizard.tempFile.flush();
    if (written != line.length() || gTestWizard.tempFile.getWriteError() != 0) {
        Serial.println("[TESTWIZ] Errore: scrittura file temporaneo incompleta.");
        printWizardSpiffsUsage("Errore scrittura temp");
        return false;
    }
    return true;
}

static bool extractTestWizardKeyFromCsvLine(const String &line, int &outTotalSpeeds, int &outDirtLevel, int &outSpeedIndex) {
    String cols[6];
    int found = 0;
    int start = 0;
    for (size_t i = 0; i <= line.length(); i++) {
        const bool sep = (i == line.length()) || (line[i] == ',');
        if (!sep) continue;
        if (found < 6) cols[found] = line.substring(start, i);
        found++;
        start = i + 1;
        if (found >= 6) break;
    }
    if (found < 6) return false;

    cols[3].trim();
    cols[4].trim();
    cols[5].trim();

    int total = 0, dirt = 0, speed = 0;
    if (!parseStrictPositiveInt(cols[3], total)) return false;
    if (!parseStrictPositiveInt(cols[4], dirt)) return false;
    if (!parseStrictPositiveInt(cols[5], speed)) return false;

    outTotalSpeeds = total;
    outDirtLevel = dirt;
    outSpeedIndex = speed;
    return true;
}

static bool writeCsvLineChecked(File &f, const String &line) {
    const size_t expected = line.length() + 1U;
    const size_t w1 = f.print(line);
    const size_t w2 = f.print('\n');
    f.flush();
    return ((w1 + w2) == expected) && (f.getWriteError() == 0);
}

static bool mergeTempTestWizardIntoFinalCsv(const String &tmpPath, int level) {
    if (!SPIFFS.exists(tmpPath)) {
        Serial.println("[TESTWIZ] Errore: file temporaneo non trovato.");
        return false;
    }

    const String finalPath = getTestWizardFinalPathForLevel(level);
    const String stagePath = "/tmp_merge_l" + String(level) + "_" + String(gTestWizard.sessionId) + ".csv";
    if (SPIFFS.exists(stagePath)) SPIFFS.remove(stagePath);

    File stage = SPIFFS.open(stagePath, "w");
    if (!stage) {
        Serial.println("[TESTWIZ] Errore: creazione file staging fallita.");
        return false;
    }
    if (!writeCsvLineChecked(stage, String(TEST_WIZARD_CSV_HEADER))) {
        stage.close();
        SPIFFS.remove(stagePath);
        Serial.println("[TESTWIZ] Errore: scrittura intestazione CSV staging fallita.");
        return false;
    }

    // Mantieni eventuali dati gia' presenti, escludendo la stessa combinazione
    // total_speeds + dirt_level + speed_index che verra' sovrascritta dal nuovo test.
    if (SPIFFS.exists(finalPath)) {
        File prev = SPIFFS.open(finalPath, "r");
        if (!prev) {
            stage.close();
            SPIFFS.remove(stagePath);
            Serial.println("[TESTWIZ] Errore: lettura CSV finale esistente fallita.");
            return false;
        }

        bool skipHeader = true;
        while (prev.available()) {
            String line = prev.readStringUntil('\n');
            line.replace("\r", "");
            if (skipHeader) {
                skipHeader = false;
                continue;
            }
            if (line.length() == 0) continue;

            int rowTotal = 0, rowDirt = 0, rowSpeed = 0;
            bool hasKey = extractTestWizardKeyFromCsvLine(line, rowTotal, rowDirt, rowSpeed);
            bool sameKey = hasKey &&
                           rowTotal == gTestWizard.totalSpeeds &&
                           rowDirt == gTestWizard.dirtLevel &&
                           rowSpeed == gTestWizard.speedIndex;
            if (!sameKey) {
                if (!writeCsvLineChecked(stage, line)) {
                    prev.close();
                    stage.close();
                    SPIFFS.remove(stagePath);
                    Serial.println("[TESTWIZ] Errore: scrittura righe CSV esistenti fallita (spazio insufficiente?).");
                    return false;
                }
            }
        }
        prev.close();
    }

    File src = SPIFFS.open(tmpPath, "r");
    if (!src) {
        stage.close();
        SPIFFS.remove(stagePath);
        Serial.println("[TESTWIZ] Errore: lettura file temporaneo fallita.");
        return false;
    }

    // Salta intestazione del file temporaneo.
    bool skipHeader = true;
    while (src.available()) {
        String line = src.readStringUntil('\n');
        line.replace("\r", "");
        if (skipHeader) {
            skipHeader = false;
            continue;
        }
        if (line.length() == 0) continue;
        if (!writeCsvLineChecked(stage, line)) {
            src.close();
            stage.close();
            SPIFFS.remove(stagePath);
            Serial.println("[TESTWIZ] Errore: scrittura righe test sul file staging fallita (spazio insufficiente?).");
            return false;
        }
    }

    src.close();
    stage.flush();
    stage.close();

    // Commit atomico: preserva sempre il CSV precedente se il rename fallisce.
    const String backupPath = finalPath + ".bak";
    if (SPIFFS.exists(backupPath)) {
        SPIFFS.remove(backupPath);
    }

    if (SPIFFS.exists(finalPath)) {
        if (!SPIFFS.rename(finalPath, backupPath)) {
            Serial.println("[TESTWIZ] Errore: backup CSV finale fallito prima del commit.");
            SPIFFS.remove(stagePath);
            return false;
        }
    }

    if (!SPIFFS.rename(stagePath, finalPath)) {
        Serial.println("[TESTWIZ] Errore: rename file staging -> finale fallita.");
        if (SPIFFS.exists(backupPath)) {
            SPIFFS.rename(backupPath, finalPath);
        }
        SPIFFS.remove(stagePath);
        return false;
    }

    if (SPIFFS.exists(backupPath)) {
        SPIFFS.remove(backupPath);
    }
    return true;
}

static bool computeDeltaPFromLiveSlaveSnapshot(float &outDeltaP);

static void printTestWizardStatus() {
    if (gTestWizard.running) {
        const unsigned long elapsedMs = millis() - gTestWizard.startMs;
        const unsigned long remainingMs =
            (elapsedMs >= gTestWizard.targetDurationMs) ? 0 : (gTestWizard.targetDurationMs - elapsedMs);
        const unsigned long elapsedS = elapsedMs / 1000UL;
        const unsigned long remainingS = remainingMs / 1000UL;
        const int validPct = (gTestWizard.totalSamples > 0)
                                 ? (int)((gTestWizard.rawValidSamples * 100UL) / gTestWizard.totalSamples)
                                 : 0;

        Serial.println("\n[TESTWIZ] Stato test in corso");
        Serial.printf("  Sessione     : %lu\n", gTestWizard.sessionId);
        Serial.printf("  Livello sporco: %d\n", gTestWizard.dirtLevel);
        Serial.printf("  Velocita'    : %d/%d\n", gTestWizard.speedIndex, gTestWizard.totalSpeeds);
        Serial.printf("  Tempo        : %lus / %lus\n", elapsedS, gTestWizard.targetDurationMs / 1000UL);
        Serial.printf("  Rimanente    : %lus\n", remainingS);
        Serial.printf("  Campioni     : %lu (validi raw=%lu, %d%%)\n",
                      gTestWizard.totalSamples, gTestWizard.rawValidSamples, validPct);
        Serial.printf("  File temp    : %s\n\n", gTestWizard.tempPath.c_str());
        return;
    }

    if (gTestWizard.stage != TESTWIZ_STAGE_IDLE) {
        Serial.println("[TESTWIZ] Wizard in attesa input.");
        printTestWizardPromptForStage();
        return;
    }

    Serial.println("[TESTWIZ] Nessun wizard/test attivo.");
}

static void printTestWizardDiagnostics() {
    const unsigned long now = millis();
    int activeCount = 0;
    int recentCount = 0;
    int grp1Recent = 0;
    int grp2Recent = 0;

    for (int i = 1; i <= 100; i++) {
        if (!listaPerifericheAttive[i]) continue;
        activeCount++;
        const unsigned long last = databaseSlave[i].lastResponseTime;
        const bool recent = (last > 0) && ((now - last) <= TEST_WIZARD_SENSOR_TIMEOUT_MS);
        if (!recent) continue;
        recentCount++;
        if (databaseSlave[i].grp == 1) grp1Recent++;
        if (databaseSlave[i].grp == 2) grp2Recent++;
    }

    float fallbackDelta = 0.0f;
    const bool fallbackOk = computeDeltaPFromLiveSlaveSnapshot(fallbackDelta);

    Serial.println("\n[TESTWIZ][DIAG] Stato diagnostico");
    Serial.printf("  Mode master           : %d (%s)\n",
                  config.modalitaMaster,
                  (config.modalitaMaster == 2) ? "REWAMPING" : "STANDALONE");
    Serial.printf("  currentDeltaPValid    : %s\n", currentDeltaPValid ? "true" : "false");
    Serial.printf("  currentDeltaP         : %.2f Pa\n", currentDeltaP);
    Serial.printf("  filteredDeltaPValid   : %s\n", isFilteredDeltaPValid() ? "true" : "false");
    Serial.printf("  filteredDeltaP        : %.2f Pa\n", getFilteredDeltaP());
    Serial.printf("  Slave attivi/recenti  : %d / %d (timeout %lus)\n",
                  activeCount, recentCount, TEST_WIZARD_SENSOR_TIMEOUT_MS / 1000UL);
    Serial.printf("  Gruppo1 recenti       : %d\n", grp1Recent);
    Serial.printf("  Gruppo2 recenti       : %d\n", grp2Recent);
    Serial.printf("  Fallback delta valido : %s", fallbackOk ? "true" : "false");
    if (fallbackOk) {
        Serial.printf(" (%.2f Pa)", fallbackDelta);
    }
    Serial.println();
    printWizardSpiffsUsage("Diagnostica");
    Serial.println("\n");
}

static void printTestWizardStatistics() {
    const float rawAvg =
        (gTestWizard.rawValidSamples > 0) ? (gTestWizard.rawSum / (float)gTestWizard.rawValidSamples) : 0.0f;
    const float filteredAvg = (gTestWizard.filteredValidSamples > 0)
                                  ? (gTestWizard.filteredSum / (float)gTestWizard.filteredValidSamples)
                                  : 0.0f;
    const int validPct = (gTestWizard.totalSamples > 0)
                             ? (int)((gTestWizard.rawValidSamples * 100UL) / gTestWizard.totalSamples)
                             : 0;

    Serial.printf("[TESTWIZ] Statistiche campionamento: campioni=%lu | raw validi=%lu (%d%%)\n",
                  gTestWizard.totalSamples, gTestWizard.rawValidSamples, validPct);
    if (gTestWizard.rawStatsReady) {
        Serial.printf("[TESTWIZ] DeltaP raw   min/avg/max = %.2f / %.2f / %.2f Pa\n",
                      gTestWizard.rawMin, rawAvg, gTestWizard.rawMax);
    } else {
        Serial.println("[TESTWIZ] DeltaP raw   non disponibile (campioni non validi).");
    }
    if (gTestWizard.filteredStatsReady) {
        Serial.printf("[TESTWIZ] DeltaP filt. min/avg/max = %.2f / %.2f / %.2f Pa\n",
                      gTestWizard.filteredMin, filteredAvg, gTestWizard.filteredMax);
    } else {
        Serial.println("[TESTWIZ] DeltaP filt. non disponibile.");
    }
}

static bool evaluateTestWizardSuccess(bool naturalEnd, String &failReason) {
    if (gTestWizard.totalSamples < TEST_WIZARD_MIN_SAMPLES) {
        failReason = "campioni insufficienti";
        return false;
    }

    if (!naturalEnd) {
        const unsigned long elapsed = millis() - gTestWizard.startMs;
        if (elapsed < TEST_WIZARD_MIN_STOP_DURATION_MS) {
            failReason = "stop manuale troppo anticipato";
            return false;
        }
    }

    if (gTestWizard.rawValidSamples == 0) {
        failReason = "nessun campione raw valido";
        return false;
    }

    const int validPct = (int)((gTestWizard.rawValidSamples * 100UL) / gTestWizard.totalSamples);
    if (validPct < TEST_WIZARD_MIN_VALID_RATIO_PCT) {
        failReason = "troppi campioni invalidi";
        return false;
    }

    return true;
}

static void finishDeltaPTestWizard(bool naturalEnd, const char *originTag) {
    if (!gTestWizard.running) return;

    if (gTestWizard.tempFile) {
        gTestWizard.tempFile.close();
    }

    String failReason = "";
    bool saveOk = evaluateTestWizardSuccess(naturalEnd, failReason);
    if (saveOk) {
        saveOk = mergeTempTestWizardIntoFinalCsv(gTestWizard.tempPath, gTestWizard.dirtLevel);
        if (!saveOk) {
            failReason = "errore merge CSV finale";
        }
    }

    if (SPIFFS.exists(gTestWizard.tempPath)) {
        SPIFFS.remove(gTestWizard.tempPath);
    }

    if (saveOk) {
        writeTestWizardSaveMeta(gTestWizard.dirtLevel);
        Serial.printf("[TESTWIZ] Test completato (%s) e salvato su %s\n",
                      originTag, getTestWizardFinalPathForLevel(gTestWizard.dirtLevel).c_str());
        printTestWizardStatistics();
        Serial.printf("[TESTWIZ] Download: /download_test_csv?level=%d\n", gTestWizard.dirtLevel);
        storeLastWizardOutcome("saved", true, originTag, "");
    } else {
        Serial.printf("[TESTWIZ] Test NON salvato (%s): %s\n", originTag, failReason.c_str());
        printTestWizardStatistics();
        storeLastWizardOutcome("not_saved", false, originTag, failReason);
    }

    resetAllTestWizardState();
}

static void failDeltaPTestWizardNoSave(const char *originTag, const String &reason) {
    if (!gTestWizard.running) return;

    if (gTestWizard.tempFile) {
        gTestWizard.tempFile.close();
    }

    if (SPIFFS.exists(gTestWizard.tempPath)) {
        SPIFFS.remove(gTestWizard.tempPath);
    }

    Serial.printf("[TESTWIZ] Test NON salvato (%s): %s\n", originTag, reason.c_str());
    printTestWizardStatistics();
    storeLastWizardOutcome("error", false, originTag, reason);
    resetAllTestWizardState();
}

static void abortDeltaPTestWizard(const char *reason) {
    const unsigned long hadSamples = gTestWizard.totalSamples;
    if (gTestWizard.tempFile) {
        gTestWizard.tempFile.close();
    }
    if (gTestWizard.running && gTestWizard.tempPath.length() > 0 && SPIFFS.exists(gTestWizard.tempPath)) {
        SPIFFS.remove(gTestWizard.tempPath);
    }
    storeLastWizardOutcome(
        (hadSamples > 0) ? "aborted_run" : "aborted_wizard",
        false,
        "abort",
        reason ? String(reason) : String("annullato"));
    resetAllTestWizardState();
    Serial.printf("[TESTWIZ] Wizard annullato: %s\n", reason);
}

static bool startDeltaPTestWizardRun() {
    if (!ensureSpiffsForTestWizard()) return false;
    if (gTestWizard.running) {
        Serial.println("[TESTWIZ] Errore: test gia' in corso.");
        return false;
    }

    const int removedTmpAtStart = cleanupWizardTempFilesFromSpiffs();
    if (removedTmpAtStart > 0) {
        Serial.printf("[TESTWIZ] Cleanup iniziale: rimossi %d file temporanei.\n", removedTmpAtStart);
    }
    if (!hasEnoughSpiffsSpaceForWizard(gTestWizard.targetDurationMs)) {
        Serial.println("[TESTWIZ] Suggerimento: usa il cestino su / (Registro Test DeltaP) e riprova.");
        storeLastWizardOutcome("start_failed", false, "start", "spazio SPIFFS insufficiente");
        resetAllTestWizardState();
        return false;
    }

    gTestWizard.sessionId = millis();
    gTestWizard.startMs = millis();
    gTestWizard.lastSampleTick = gTestWizard.startMs;
    gTestWizard.totalSamples = 0;
    gTestWizard.rawValidSamples = 0;
    gTestWizard.filteredValidSamples = 0;
    gTestWizard.rawMin = 0.0f;
    gTestWizard.rawMax = 0.0f;
    gTestWizard.rawSum = 0.0f;
    gTestWizard.rawStatsReady = false;
    gTestWizard.filteredMin = 0.0f;
    gTestWizard.filteredMax = 0.0f;
    gTestWizard.filteredSum = 0.0f;
    gTestWizard.filteredStatsReady = false;
    gTestWizard.tempPath = "/tmp_dptest_l" + String(gTestWizard.dirtLevel) + "_" + String(gTestWizard.sessionId) + ".csv";

    if (SPIFFS.exists(gTestWizard.tempPath)) {
        SPIFFS.remove(gTestWizard.tempPath);
    }

    gTestWizard.tempFile = SPIFFS.open(gTestWizard.tempPath, "w");
    if (!gTestWizard.tempFile) {
        const int removedTmpRetry = cleanupWizardTempFilesFromSpiffs();
        if (removedTmpRetry > 0) {
            Serial.printf("[TESTWIZ] Retry apertura: rimossi %d file temporanei.\n", removedTmpRetry);
            gTestWizard.tempFile = SPIFFS.open(gTestWizard.tempPath, "w");
        }
    }
    if (!gTestWizard.tempFile) {
        Serial.println("[TESTWIZ] Errore: creazione file temporaneo fallita.");
        printWizardSpiffsUsage("Errore apertura file temp");
        Serial.println("[TESTWIZ] Suggerimento: usa il cestino su / (Registro Test DeltaP) per liberare spazio.");
        storeLastWizardOutcome("start_failed", false, "start", "creazione file temporaneo fallita");
        resetAllTestWizardState();
        return false;
    }
    if (!writeCsvLineChecked(gTestWizard.tempFile, String(TEST_WIZARD_CSV_HEADER))) {
        Serial.println("[TESTWIZ] Errore: scrittura intestazione file temporaneo fallita.");
        printWizardSpiffsUsage("Errore intestazione temp");
        gTestWizard.tempFile.close();
        removeSpiffsFileFlexible(gTestWizard.tempPath);
        storeLastWizardOutcome("start_failed", false, "start", "scrittura intestazione file temporaneo fallita");
        resetAllTestWizardState();
        return false;
    }
    printWizardSpiffsUsage("Avvio test");

    gTestWizard.running = true;
    gTestWizard.stage = TESTWIZ_STAGE_IDLE;

    float initialFallbackDelta = 0.0f;
    const bool hasInitialDelta = currentDeltaPValid || computeDeltaPFromLiveSlaveSnapshot(initialFallbackDelta);

    Serial.println("\n[TESTWIZ] Test avviato.");
    Serial.printf("[TESTWIZ] Livello sporco: %d | Velocita' test: %d/%d\n",
                  gTestWizard.dirtLevel, gTestWizard.speedIndex, gTestWizard.totalSpeeds);
    Serial.printf("[TESTWIZ] Durata: %lu secondi | Campionamento: 1Hz\n", gTestWizard.targetDurationMs / 1000UL);
    Serial.println("[TESTWIZ] Salvataggio CSV solo a test valido.");
    Serial.println("[TESTWIZ] Regola CSV: stessa combinazione livello+velocita' => sovrascrittura.");
    Serial.println("[TESTWIZ] Comandi utili: TESTWIZ_STATUS, TESTWIZ_STOP, TESTWIZ_ABORT");
    if (!hasInitialDelta) {
        Serial.println("[TESTWIZ] Warning: nessun DeltaP valido al momento. Usa TESTWIZ_DIAG per verifica.");
    }
    return true;
}

static bool consumeTestWizardPromptInput(const String &input) {
    if (gTestWizard.stage == TESTWIZ_STAGE_IDLE) return false;

    int value = 0;
    if (!parseStrictPositiveInt(input, value)) {
        Serial.println("[TESTWIZ] Valore non valido.");
        printTestWizardPromptForStage();
        return true;
    }

    if (gTestWizard.stage == TESTWIZ_STAGE_WAIT_TOTAL_SPEEDS) {
        if (value < 1 || value > TEST_WIZARD_MAX_SPEEDS) {
            Serial.printf("[TESTWIZ] Numero velocita' fuori range (1-%d).\n", TEST_WIZARD_MAX_SPEEDS);
            printTestWizardPromptForStage();
            return true;
        }
        gTestWizard.totalSpeeds = value;
        gTestWizard.stage = TESTWIZ_STAGE_WAIT_DIRTY_LEVEL;
        printTestWizardPromptForStage();
        return true;
    }

    if (gTestWizard.stage == TESTWIZ_STAGE_WAIT_DIRTY_LEVEL) {
        if (value < 1 || value > TEST_WIZARD_DIRTY_LEVELS) {
            Serial.printf("[TESTWIZ] Livello sporco fuori range (1-%d).\n", TEST_WIZARD_DIRTY_LEVELS);
            printTestWizardPromptForStage();
            return true;
        }
        gTestWizard.dirtLevel = value;
        gTestWizard.stage = TESTWIZ_STAGE_WAIT_SPEED_INDEX;
        printTestWizardPromptForStage();
        return true;
    }

    if (gTestWizard.stage == TESTWIZ_STAGE_WAIT_SPEED_INDEX) {
        if (value < 1 || value > gTestWizard.totalSpeeds) {
            Serial.printf("[TESTWIZ] Velocita' fuori range (1-%d).\n", gTestWizard.totalSpeeds);
            printTestWizardPromptForStage();
            return true;
        }
        gTestWizard.speedIndex = value;
        if (!startDeltaPTestWizardRun()) {
            Serial.println("[TESTWIZ] Avvio test fallito.");
        }
        return true;
    }

    return false;
}

static bool computeDeltaPFromLiveSlaveSnapshot(float &outDeltaP) {
    const unsigned long now = millis();
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
        if (last == 0) continue;
        if ((now - last) > TEST_WIZARD_SENSOR_TIMEOUT_MS) continue;

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
    return false;
}

static void serviceDeltaPTestWizard() {
    if (!gTestWizard.running) return;

    const unsigned long now = millis();
    while (now - gTestWizard.lastSampleTick >= TEST_WIZARD_SAMPLE_INTERVAL_MS) {
        gTestWizard.lastSampleTick += TEST_WIZARD_SAMPLE_INTERVAL_MS;
        const unsigned long sampleTs = gTestWizard.lastSampleTick;
        const unsigned long elapsedSec = (sampleTs - gTestWizard.startMs) / 1000UL;

        bool rawValid = currentDeltaPValid;
        float rawDelta = currentDeltaP;
        if (!rawValid) {
            float fallbackDelta = 0.0f;
            if (computeDeltaPFromLiveSlaveSnapshot(fallbackDelta)) {
                rawValid = true;
                rawDelta = fallbackDelta;
            }
        }
        const bool filteredValid = isFilteredDeltaPValid();
        const float filteredDelta = getFilteredDeltaP();
        const String thresholdMsg = filteredValid ? checkThresholds(filteredDelta) : "";

        String row = "";
        row.reserve(180);
        row += String(gTestWizard.sessionId);
        row += ",";
        row += String(sampleTs);
        row += ",";
        row += String(elapsedSec);
        row += ",";
        row += String(gTestWizard.totalSpeeds);
        row += ",";
        row += String(gTestWizard.dirtLevel);
        row += ",";
        row += String(gTestWizard.speedIndex);
        row += ",";
        row += rawValid ? String(rawDelta, 2) : "";
        row += ",";
        row += rawValid ? "1" : "0";
        row += ",";
        row += filteredValid ? String(filteredDelta, 2) : "";
        row += ",";
        row += filteredValid ? "1" : "0";
        row += ",";
        row += csvEscapeField(thresholdMsg);
        row += "\n";

        if (!appendLineToTempTestWizardFile(row)) {
            failDeltaPTestWizardNoSave("errore scrittura", "file temporaneo test non disponibile");
            return;
        }

        gTestWizard.totalSamples++;
        if (rawValid) {
            gTestWizard.rawValidSamples++;
            gTestWizard.rawSum += rawDelta;
            if (!gTestWizard.rawStatsReady) {
                gTestWizard.rawMin = rawDelta;
                gTestWizard.rawMax = rawDelta;
                gTestWizard.rawStatsReady = true;
            } else {
                if (rawDelta < gTestWizard.rawMin) gTestWizard.rawMin = rawDelta;
                if (rawDelta > gTestWizard.rawMax) gTestWizard.rawMax = rawDelta;
            }
        }
        if (filteredValid) {
            gTestWizard.filteredValidSamples++;
            gTestWizard.filteredSum += filteredDelta;
            if (!gTestWizard.filteredStatsReady) {
                gTestWizard.filteredMin = filteredDelta;
                gTestWizard.filteredMax = filteredDelta;
                gTestWizard.filteredStatsReady = true;
            } else {
                if (filteredDelta < gTestWizard.filteredMin) gTestWizard.filteredMin = filteredDelta;
                if (filteredDelta > gTestWizard.filteredMax) gTestWizard.filteredMax = filteredDelta;
            }
        }
    }

    if (now - gTestWizard.startMs >= gTestWizard.targetDurationMs) {
        finishDeltaPTestWizard(true, "durata completata");
    }
}

static void printCalibrationSummaryToSerial() {
    Serial.println("\n--- CALIBRAZIONE SALVATA ---");
    Serial.printf("Velocita' configurate: %d\n", config.numVelocitaSistema);

    if (config.numVelocitaSistema <= 0) {
        Serial.println("Nessuna calibrazione presente.");
    } else {
        for (int i = 1; i <= config.numVelocitaSistema && i <= 10; i++) {
            const float base = config.deltaP_Calib[i];
            const int perc = config.perc_Calib[i];
            const float limit = base * (1.0f + (perc / 100.0f));
            Serial.printf("V%d -> DeltaP=%.1f Pa | Soglia=%d%% | Limite=%.1f Pa\n",
                          i, base, perc, limit);
        }
    }

    if (isFilteredDeltaPValid()) {
        float filtered = getFilteredDeltaP();
        String warn = checkThresholds(filtered);
        Serial.printf("DeltaP filtrato: %.1f Pa\n", filtered);
        Serial.printf("Stato soglie: %s\n", (warn.length() > 0) ? warn.c_str() : "OK");
    } else {
        Serial.println("DeltaP filtrato: non disponibile (misure non valide).");
    }
    Serial.println("----------------------------\n");
}

static void monitorThresholdAlertOnSerial() {
    const unsigned long now = millis();
    if (now - gLastThresholdCheckMs < 1000) return; // Controllo 1Hz
    gLastThresholdCheckMs = now;

    String currentMsg = "";
    if (isFilteredDeltaPValid()) {
        currentMsg = checkThresholds(getFilteredDeltaP());
    }

    if (currentMsg != gLastThresholdMsg) {
        if (currentMsg.length() > 0) {
            Serial.printf("[CALIB][ALERT] %s | DeltaP=%.1f Pa\n", currentMsg.c_str(), getFilteredDeltaP());
        } else if (gLastThresholdMsg.length() > 0) {
            if (isFilteredDeltaPValid()) {
                Serial.printf("[CALIB][OK] Rientrato sotto soglia | DeltaP=%.1f Pa\n", getFilteredDeltaP());
            } else {
                Serial.println("[CALIB][INFO] Misura DeltaP non valida: impossibile valutare soglie.");
            }
        }
        gLastThresholdMsg = currentMsg;
    }
}

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

static String testWizardStageToString(TestWizardStage stage) {
    if (stage == TESTWIZ_STAGE_WAIT_TOTAL_SPEEDS) return "wait_total_speeds";
    if (stage == TESTWIZ_STAGE_WAIT_DIRTY_LEVEL) return "wait_dirt_level";
    if (stage == TESTWIZ_STAGE_WAIT_SPEED_INDEX) return "wait_speed_index";
    return "idle";
}

// --- FUNZIONE DI ELABORAZIONE COMANDI (Estratta per sicurezza) ---
void processSerialCommand(String cmd) {
    cmd.trim();
    if (cmd.length() == 0) return;

    Serial.print("> Ricevuto: "); Serial.println(cmd);
    String cmdUpper = cmd; 
    cmdUpper.toUpperCase(); 

    // Comandi dedicati wizard test DeltaP
    if (cmdUpper == "TESTWIZ_ABORT") {
        abortDeltaPTestWizard("richiesta utente");
        return;
    }
    if (cmdUpper == "TESTWIZ_STATUS") {
        printTestWizardStatus();
        return;
    }
    if (cmdUpper == "TESTWIZ_DIAG") {
        printTestWizardDiagnostics();
        return;
    }
    if (cmdUpper == "TESTWIZ_STOP") {
        if (!gTestWizard.running) {
            Serial.println("[TESTWIZ] Nessun test in corso.");
        } else {
            finishDeltaPTestWizard(false, "stop manuale");
        }
        return;
    }
    if (cmdUpper.startsWith("TESTWIZ_DURATION ")) {
        if (gTestWizard.running) {
            Serial.println("[TESTWIZ] Impossibile cambiare durata durante un test in corso.");
            return;
        }
        String secsStr = cmd.substring(17);
        secsStr.trim();
        int secs = secsStr.toInt();
        if (secs < 60 || secs > 7200) {
            Serial.println("[TESTWIZ] Durata non valida. Usa un valore tra 60 e 7200 secondi.");
            return;
        }
        gTestWizard.targetDurationMs = (unsigned long)secs * 1000UL;
        Serial.printf("[TESTWIZ] Durata test impostata a %d secondi.\n", secs);
        return;
    }
    if (cmdUpper == "TESTWIZ") {
        if (manualOtaActive) {
            Serial.println("[TESTWIZ] Errore: non disponibile durante TEST/OTA manuale.");
            return;
        }
        if (config.modalitaMaster != 2) {
            Serial.println("[TESTWIZ] Errore: disponibile solo in modalita' REWAMPING (SETMODE 2).");
            return;
        }
        if (gTestWizard.running) {
            Serial.println("[TESTWIZ] Test gia' in corso. Usa TESTWIZ_STATUS/TESTWIZ_STOP.");
            return;
        }
        resetAllTestWizardState();
        gTestWizard.stage = TESTWIZ_STAGE_WAIT_TOTAL_SPEEDS;
        Serial.println("\n=== WIZARD TEST DELTAP ===");
        Serial.printf("[TESTWIZ] Livelli sporco disponibili: %d\n", TEST_WIZARD_DIRTY_LEVELS);
        Serial.printf("[TESTWIZ] Durata attuale test: %lu secondi\n", gTestWizard.targetDurationMs / 1000UL);
        Serial.println("[TESTWIZ] Comandi utili: TESTWIZ_STATUS, TESTWIZ_ABORT, TESTWIZ_DURATION <sec>");
        printTestWizardPromptForStage();
        return;
    }

    if (consumeTestWizardPromptInput(cmd)) {
        return;
    }

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
        Serial.println("\n=== ELENCO COMANDI CONTROLLER ===");
        Serial.println("INFO             : Visualizza configurazione");
        Serial.println("READSERIAL       : Leggi Seriale");
        Serial.println("READMODE         : Leggi Modo Controller");
        Serial.println("READSIC          : Leggi stato Sicurezza");
        Serial.println("READLIFEH        : Leggi soglia ore ExpLifetime Standalone");
        Serial.println("READVERSION      : Leggi Versione FW");
        Serial.println("READCALIB        : Leggi calibrazione e soglie salvate");
        Serial.println("SETSERIAL x      : Imposta SN (es. SETSERIAL AABB)");
        Serial.println("SETMODE x        : 1:Standalone, 2:Rewamping");
        Serial.println("SETSIC ON/OFF    : Sicurezza locale (IO2)");
        Serial.println("SETLIFEH x       : Soglia ore lampade Standalone (0=disabilita)");
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
        Serial.println("TESTWIZ          : Avvia wizard test DeltaP su seriale");
        Serial.println("TESTWIZ_STATUS   : Stato wizard/test DeltaP");
        Serial.println("TESTWIZ_DIAG     : Diagnostica feed DeltaP/sensori");
        Serial.println("TESTWIZ_STOP     : Chiude test e salva solo se valido");
        Serial.println("TESTWIZ_ABORT    : Annulla wizard/test senza salvare");
        Serial.println("TESTWIZ_DURATION s: Imposta durata test in secondi (60-7200)");
        Serial.println("HELPWIZ          : Guida rapida wizard test DeltaP");
        Serial.println("REBOOT           : Riavvia la scheda");
        Serial.println("CLEARMEM         : Reset Fabbrica");
        Serial.println("=============================\n");
    }
    else if (cmdUpper == "HELPOTA") {
        Serial.println("\n=== MENU OTA VIA SERIALE ===");
        Serial.println("1. OTA_MODE <ID>        : Entra in modalita' OTA (richiede WiFi connesso).");
        Serial.println("2. OTA_DOWNLOAD <URL>   : Scarica il file .bin sul Controller.");
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
    else if (cmdUpper == "HELPWIZ") {
        Serial.println("\n=== WIZARD TEST DELTAP ===");
        Serial.println("TESTWIZ                 : Avvio wizard guidato");
        Serial.println("  1) Numero velocita' impianto");
        Serial.println("  2) Livello sporco (1..3)");
        Serial.println("  3) Velocita' in test");
        Serial.println("Durante il test (1 Hz):");
        Serial.println("  TESTWIZ_STATUS        : Stato in tempo reale");
        Serial.println("  TESTWIZ_DIAG          : Diagnostica campioni/sensori");
        Serial.println("  TESTWIZ_STOP          : Fine manuale (salva solo se valido)");
        Serial.println("  TESTWIZ_ABORT         : Annulla e cancella file temporaneo");
        Serial.println("Configurazione opzionale:");
        Serial.println("  TESTWIZ_DURATION <s>  : Durata test in secondi (default 900)");
        Serial.println("Prerequisito:");
        Serial.println("  Modalita' REWAMPING (SETMODE 2)");
        Serial.println("Output CSV:");
        Serial.println("  /deltap_test_sporco_1.csv");
        Serial.println("  /deltap_test_sporco_2.csv");
        Serial.println("  /deltap_test_sporco_3.csv");
        Serial.println("Regola salvataggio:");
        Serial.println("  Se ripeti stesso livello+velocita' il test precedente viene sovrascritto.");
        Serial.println("===========================\n");
    }
    else if (cmdUpper == "INFO") {
        Serial.println("\n--- STATO ATTUALE CONTROLLER ---");
        Serial.printf("Configurato : %s\n", config.configurata ? "SI" : "NO");
        Serial.printf("Seriale     : %s\n", config.serialeID);
        Serial.printf("Modo        : %s\n", config.modalitaMaster == 2 ? "REWAMPING" : "STANDALONE");
        Serial.printf("Sicurezza   : %s\n", config.usaSicurezzaLocale ? "ATTIVA (IO2)" : "DISABILITATA");
        Serial.printf("Soglia Ore  : %d h (Standalone ExpLifetime)\n", config.sogliaManutenzione);
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
    else if (cmdUpper == "READLIFEH") {
        Serial.printf("Soglia Ore Standalone: %d h\n", config.sogliaManutenzione);
    }
    else if (cmdUpper == "READVERSION") {
        Serial.printf("Versione FW: %s\n", FW_VERSION);
    }
    else if (cmdUpper == "READCALIB") {
        printCalibrationSummaryToSerial();
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
    else if (cmdUpper.startsWith("SETLIFEH ") || cmdUpper.startsWith("SETLIFEH:")) {
        String val = cmd.substring(9); val.trim();
        int hours = val.toInt();
        if (hours < 0 || hours > 200000) {
            Serial.println("ERR: valore non valido (range 0..200000)");
            return;
        }
        config.sogliaManutenzione = hours;
        memoria.putInt("m_life_h", config.sogliaManutenzione);
        Serial.printf("OK: Soglia ore Standalone salvata: %d h\n", config.sogliaManutenzione);
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
        Serial.println("Comando non riconosciuto. Usa HELP / HELPTEST / HELPOTA / HELPWIZ.");
    }
}

bool webStartDeltaPTestWizard(int totalSpeeds, int dirtLevel, int speedIndex, String &message) {
    message = "";

    if (manualOtaActive) {
        message = "Wizard non disponibile durante TEST/OTA manuale.";
        return false;
    }
    if (config.modalitaMaster != 2) {
        message = "Wizard disponibile solo in modalita' REWAMPING (SETMODE 2).";
        return false;
    }
    if (gTestWizard.running) {
        message = "Test gia' in corso.";
        return false;
    }
    if (totalSpeeds < 1 || totalSpeeds > TEST_WIZARD_MAX_SPEEDS) {
        message = "Numero velocita' fuori range (1-" + String(TEST_WIZARD_MAX_SPEEDS) + ").";
        return false;
    }
    if (dirtLevel < 1 || dirtLevel > TEST_WIZARD_DIRTY_LEVELS) {
        message = "Livello sporco fuori range (1-" + String(TEST_WIZARD_DIRTY_LEVELS) + ").";
        return false;
    }
    if (speedIndex < 1 || speedIndex > totalSpeeds) {
        message = "Velocita' test fuori range (1-" + String(totalSpeeds) + ").";
        return false;
    }

    resetAllTestWizardState();
    gTestWizard.totalSpeeds = totalSpeeds;
    gTestWizard.dirtLevel = dirtLevel;
    gTestWizard.speedIndex = speedIndex;
    if (!startDeltaPTestWizardRun()) {
        message = "Avvio test fallito (controllare SPIFFS/dati sensori).";
        return false;
    }

    message = "Test avviato.";
    return true;
}

bool webStopDeltaPTestWizard(bool saveIfPossible, String &message) {
    message = "";

    if (gTestWizard.running) {
        if (!saveIfPossible) {
            abortDeltaPTestWizard("annullato da web");
            message = "Test annullato senza salvataggio.";
            return true;
        }

        String failReason = "";
        const bool willSave = evaluateTestWizardSuccess(false, failReason);
        finishDeltaPTestWizard(false, "stop web");
        if (willSave) {
            message = "Test chiuso e salvato.";
            return true;
        }

        message = "Test chiuso ma NON salvato: " + failReason;
        return false;
    }

    if (gTestWizard.stage != TESTWIZ_STAGE_IDLE) {
        resetAllTestWizardState();
        message = "Wizard seriale in attesa input annullato.";
        return true;
    }

    message = "Nessun test in corso.";
    return false;
}

String webGetDeltaPTestWizardStatusJson() {
    const bool running = gTestWizard.running;
    const bool waiting = (!running && gTestWizard.stage != TESTWIZ_STAGE_IDLE);
    const unsigned long nowMs = millis();
    const unsigned long elapsedMs = running ? (millis() - gTestWizard.startMs) : 0UL;
    const unsigned long durationMs = gTestWizard.targetDurationMs;
    const unsigned long remainingMs = running
                                          ? ((elapsedMs >= durationMs) ? 0UL : (durationMs - elapsedMs))
                                          : durationMs;

    const unsigned long elapsedS = elapsedMs / 1000UL;
    const unsigned long remainingS = remainingMs / 1000UL;
    const unsigned long durationS = durationMs / 1000UL;
    const int validPct = (gTestWizard.totalSamples > 0)
                             ? (int)((gTestWizard.rawValidSamples * 100UL) / gTestWizard.totalSamples)
                             : 0;
    const float rawAvg = (gTestWizard.rawValidSamples > 0)
                             ? (gTestWizard.rawSum / (float)gTestWizard.rawValidSamples)
                             : 0.0f;
    const float filteredAvg = (gTestWizard.filteredValidSamples > 0)
                                  ? (gTestWizard.filteredSum / (float)gTestWizard.filteredValidSamples)
                                  : 0.0f;
    const unsigned long expectedSamples = durationMs / TEST_WIZARD_SAMPLE_INTERVAL_MS;
    const long missingSamples = (long)expectedSamples - (long)gTestWizard.totalSamples;
    const unsigned long lastSampleMs = gTestWizard.lastSampleTick;
    const unsigned long sampleLagMs = (running && lastSampleMs > 0 && nowMs >= lastSampleMs)
                                          ? (nowMs - lastSampleMs)
                                          : 0UL;

    const bool rawLiveValid = currentDeltaPValid;
    const bool filteredLiveValid = isFilteredDeltaPValid();
    const float filteredLiveValue = getFilteredDeltaP();
    const String thresholdMsg = filteredLiveValid ? checkThresholds(filteredLiveValue) : "";

    int activeCount = 0;
    int recentCount = 0;
    int grp1Recent = 0;
    int grp2Recent = 0;
    for (int i = 1; i <= 100; i++) {
        if (!listaPerifericheAttive[i]) continue;
        activeCount++;
        const unsigned long last = databaseSlave[i].lastResponseTime;
        const bool recent = (last > 0) && ((nowMs - last) <= TEST_WIZARD_SENSOR_TIMEOUT_MS);
        if (!recent) continue;
        recentCount++;
        if (databaseSlave[i].grp == 1) grp1Recent++;
        if (databaseSlave[i].grp == 2) grp2Recent++;
    }

    float fallbackDelta = 0.0f;
    const bool fallbackValid = computeDeltaPFromLiveSlaveSnapshot(fallbackDelta);

    const bool spiffsOk = ensureSpiffsForTestWizard();
    uint32_t fsTotal = 0;
    uint32_t fsUsed = 0;
    uint32_t fsFree = 0;
    bool tempExists = false;
    uint32_t tempSize = 0;
    const bool tempOpen = (bool)gTestWizard.tempFile;
    int tempWriteError = tempOpen ? gTestWizard.tempFile.getWriteError() : 0;
    bool finalExists = false;
    uint32_t finalSize = 0;
    String finalPath = "";
    const uint32_t estimatedTempBytes = estimateWizardTempCsvBytes(durationMs);
    const uint32_t requiredBytes = estimatedTempBytes + TEST_WIZARD_FS_SAFETY_MARGIN_BYTES;

    if (spiffsOk) {
        fsTotal = SPIFFS.totalBytes();
        fsUsed = SPIFFS.usedBytes();
        fsFree = (fsUsed <= fsTotal) ? (fsTotal - fsUsed) : 0;

        if (gTestWizard.tempPath.length() > 0) {
            tempExists = SPIFFS.exists(gTestWizard.tempPath);
            if (tempExists) {
                File tf = SPIFFS.open(gTestWizard.tempPath, "r");
                if (tf) {
                    tempSize = (uint32_t)tf.size();
                    tf.close();
                }
            }
        }

        if (gTestWizard.dirtLevel >= 1 && gTestWizard.dirtLevel <= TEST_WIZARD_DIRTY_LEVELS) {
            finalPath = getTestWizardFinalPathForLevel(gTestWizard.dirtLevel);
            finalExists = SPIFFS.exists(finalPath);
            if (finalExists) {
                File ff = SPIFFS.open(finalPath, "r");
                if (ff) {
                    finalSize = (uint32_t)ff.size();
                    ff.close();
                }
            }
        }
    }

    String statusText = "Idle";
    if (manualOtaActive) {
        statusText = "Bloccato: TEST/OTA manuale attivo";
    } else if (config.modalitaMaster != 2) {
        statusText = "Bloccato: usare SETMODE 2 (REWAMPING)";
    } else if (running) {
        statusText = "Test in corso";
    } else if (waiting) {
        statusText = "Wizard seriale in attesa input";
    }

    const bool canStart = (!running && !waiting && !manualOtaActive && config.modalitaMaster == 2);

    String json = "{";
    json += "\"running\":" + String(running ? "true" : "false") + ",";
    json += "\"wizard_waiting\":" + String(waiting ? "true" : "false") + ",";
    json += "\"can_start\":" + String(canStart ? "true" : "false") + ",";
    json += "\"manual_ota\":" + String(manualOtaActive ? "true" : "false") + ",";
    json += "\"mode\":" + String(config.modalitaMaster) + ",";
    json += "\"now_uptime_ms\":" + String(nowMs) + ",";
    json += "\"stage\":\"" + testWizardStageToString(gTestWizard.stage) + "\",";
    json += "\"status_text\":\"" + escapeJsonString(statusText) + "\",";
    json += "\"session_id\":" + String(gTestWizard.sessionId) + ",";
    json += "\"total_speeds\":" + String(gTestWizard.totalSpeeds) + ",";
    json += "\"dirt_level\":" + String(gTestWizard.dirtLevel) + ",";
    json += "\"speed_index\":" + String(gTestWizard.speedIndex) + ",";
    json += "\"sample_interval_ms\":" + String(TEST_WIZARD_SAMPLE_INTERVAL_MS) + ",";
    json += "\"duration_s\":" + String(durationS) + ",";
    json += "\"elapsed_s\":" + String(elapsedS) + ",";
    json += "\"remaining_s\":" + String(remainingS) + ",";
    json += "\"expected_samples\":" + String(expectedSamples) + ",";
    json += "\"missing_samples\":" + String(missingSamples) + ",";
    json += "\"start_uptime_ms\":" + String(gTestWizard.startMs) + ",";
    json += "\"last_sample_uptime_ms\":" + String(lastSampleMs) + ",";
    json += "\"sample_lag_ms\":" + String(sampleLagMs) + ",";
    json += "\"samples\":" + String(gTestWizard.totalSamples) + ",";
    json += "\"raw_valid_samples\":" + String(gTestWizard.rawValidSamples) + ",";
    json += "\"filtered_valid_samples\":" + String(gTestWizard.filteredValidSamples) + ",";
    json += "\"valid_pct\":" + String(validPct) + ",";
    json += "\"raw_live_valid\":" + String(rawLiveValid ? "true" : "false") + ",";
    json += "\"raw_live_pa\":" + String(currentDeltaP, 2) + ",";
    json += "\"filtered_live_valid\":" + String(filteredLiveValid ? "true" : "false") + ",";
    json += "\"filtered_live_pa\":" + String(filteredLiveValue, 2) + ",";
    json += "\"threshold_msg\":\"" + escapeJsonString(thresholdMsg) + "\",";
    json += "\"fallback_valid\":" + String(fallbackValid ? "true" : "false") + ",";
    json += "\"fallback_pa\":" + String(fallbackDelta, 2) + ",";
    json += "\"active_slaves\":" + String(activeCount) + ",";
    json += "\"recent_slaves\":" + String(recentCount) + ",";
    json += "\"grp1_recent\":" + String(grp1Recent) + ",";
    json += "\"grp2_recent\":" + String(grp2Recent) + ",";
    json += "\"raw_stats_ready\":" + String(gTestWizard.rawStatsReady ? "true" : "false") + ",";
    json += "\"raw_min\":" + String(gTestWizard.rawMin, 2) + ",";
    json += "\"raw_avg\":" + String(rawAvg, 2) + ",";
    json += "\"raw_max\":" + String(gTestWizard.rawMax, 2) + ",";
    json += "\"filtered_stats_ready\":" + String(gTestWizard.filteredStatsReady ? "true" : "false") + ",";
    json += "\"filtered_min\":" + String(gTestWizard.filteredMin, 2) + ",";
    json += "\"filtered_avg\":" + String(filteredAvg, 2) + ",";
    json += "\"filtered_max\":" + String(gTestWizard.filteredMax, 2) + ",";
    json += "\"spiffs_ready\":" + String(spiffsOk ? "true" : "false") + ",";
    json += "\"spiffs_total_bytes\":" + String(fsTotal) + ",";
    json += "\"spiffs_used_bytes\":" + String(fsUsed) + ",";
    json += "\"spiffs_free_bytes\":" + String(fsFree) + ",";
    json += "\"estimated_temp_bytes\":" + String(estimatedTempBytes) + ",";
    json += "\"required_bytes\":" + String(requiredBytes) + ",";
    json += "\"temp_path\":\"" + escapeJsonString(gTestWizard.tempPath) + "\",";
    json += "\"temp_open\":" + String(tempOpen ? "true" : "false") + ",";
    json += "\"temp_exists\":" + String(tempExists ? "true" : "false") + ",";
    json += "\"temp_size_bytes\":" + String(tempSize) + ",";
    json += "\"temp_write_error\":" + String(tempWriteError) + ",";
    json += "\"final_path\":\"" + escapeJsonString(finalPath) + "\",";
    json += "\"final_exists\":" + String(finalExists ? "true" : "false") + ",";
    json += "\"final_size_bytes\":" + String(finalSize) + ",";
    json += "\"last_available\":" + String(gTestWizardLastOutcome.available ? "true" : "false") + ",";
    json += "\"last_saved\":" + String(gTestWizardLastOutcome.saved ? "true" : "false") + ",";
    json += "\"last_status\":\"" + escapeJsonString(gTestWizardLastOutcome.status) + "\",";
    json += "\"last_origin\":\"" + escapeJsonString(gTestWizardLastOutcome.origin) + "\",";
    json += "\"last_reason\":\"" + escapeJsonString(gTestWizardLastOutcome.reason) + "\",";
    json += "\"last_end_uptime_ms\":" + String(gTestWizardLastOutcome.endMs) + ",";
    json += "\"last_session_id\":" + String(gTestWizardLastOutcome.sessionId) + ",";
    json += "\"last_total_speeds\":" + String(gTestWizardLastOutcome.totalSpeeds) + ",";
    json += "\"last_dirt_level\":" + String(gTestWizardLastOutcome.dirtLevel) + ",";
    json += "\"last_speed_index\":" + String(gTestWizardLastOutcome.speedIndex) + ",";
    json += "\"last_samples\":" + String(gTestWizardLastOutcome.samples) + ",";
    json += "\"last_raw_valid_samples\":" + String(gTestWizardLastOutcome.rawValidSamples) + ",";
    json += "\"last_filtered_valid_samples\":" + String(gTestWizardLastOutcome.filteredValidSamples) + ",";
    json += "\"last_valid_pct\":" + String(gTestWizardLastOutcome.validPct) + ",";
    json += "\"last_raw_stats_ready\":" + String(gTestWizardLastOutcome.rawStatsReady ? "true" : "false") + ",";
    json += "\"last_raw_min\":" + String(gTestWizardLastOutcome.rawMin, 2) + ",";
    json += "\"last_raw_avg\":" + String(gTestWizardLastOutcome.rawAvg, 2) + ",";
    json += "\"last_raw_max\":" + String(gTestWizardLastOutcome.rawMax, 2) + ",";
    json += "\"last_filtered_stats_ready\":" + String(gTestWizardLastOutcome.filteredStatsReady ? "true" : "false") + ",";
    json += "\"last_filtered_min\":" + String(gTestWizardLastOutcome.filteredMin, 2) + ",";
    json += "\"last_filtered_avg\":" + String(gTestWizardLastOutcome.filteredAvg, 2) + ",";
    json += "\"last_filtered_max\":" + String(gTestWizardLastOutcome.filteredMax, 2);
    json += "}";
    return json;
}

bool webIsDeltaPTestWizardBusy() {
    return gTestWizard.running || (gTestWizard.stage != TESTWIZ_STAGE_IDLE);
}

// Funzione principale per la gestione del menu seriale del Controller.
void Serial_Controller_Menu() {
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

    // Monitor continuo soglie: logga solo quando cambia stato.
    monitorThresholdAlertOnSerial();
    serviceDeltaPTestWizard();
}

// Alias legacy mantenuto per compatibilita'.
void Serial_Master_Menu() {
    Serial_Controller_Menu();
}
