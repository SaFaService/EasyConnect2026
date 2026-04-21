/**
 * ITA: Modulo di calibrazione DeltaP e monitor soglie con filtro/isteresi.
 * ENG: DeltaP calibration module and threshold monitor with filter/hysteresis.
 *
 * ITA: Contiene wizard web, persistenza su NVS e logica di stabilizzazione
 *      allarmi per evitare falsi positivi.
 * ENG: Contains web wizard, NVS persistence, and alarm stabilization logic
 *      to reduce false positives.
 */
#include "Calibration.h"
#include "GestioneMemoria.h"
#include <Preferences.h>
#include <WebServer.h>

extern Impostazioni config;
extern Preferences memoria;
// ITA: DeltaP corrente pubblicato dal loop principale.
// ENG: Current DeltaP published by the main control loop.
extern float currentDeltaP;
// ITA: Validita' del DeltaP corrente.
// ENG: Validity flag for current DeltaP value.
extern bool currentDeltaPValid;
// ITA: Istanza server HTTP del firmware.
// ENG: Firmware HTTP server instance.
extern WebServer server;

// ITA: Variabili di stato del wizard di campionamento.
// ENG: Sampling wizard runtime state.
bool isSampling = false;
unsigned long sampleStartTime = 0;
unsigned long lastSampleTick = 0;
int sampleCount = 0;
float sampleAccumulator = 0.0;
float lastSampledValue = 0.0;

// --- ITA: STATO MONITOR DELTA P (Filtro + Isteresi) ---
// --- ENG: DELTA P MONITOR STATE (Filtering + Hysteresis) ---
static const int DELTA_WINDOW_SIZE = 5;
static const int DELTA_MIN_VALID_FOR_FILTER = 3;
static const float DELTA_EMA_ALPHA = 0.35f;
static const unsigned long ALERT_RISE_HOLD_MS = 12000;   // tempo minimo sopra soglia prima di alzare lo stato
static const unsigned long ALERT_FALL_HOLD_MS = 20000;   // tempo minimo sotto soglia prima di abbassare lo stato
static const float ALERT_CLEAR_HYST_PCT = 8.0f;          // isteresi in discesa per stabilizzare

static float gDeltaWindow[DELTA_WINDOW_SIZE] = {0.0f};
static int gDeltaWindowCount = 0;
static int gDeltaWindowIdx = 0;
static int gDeltaInvalidStreak = 0;
static bool gFilteredDeltaValid = false;
static float gFilteredDelta = 0.0f;

static int gStableExceeded = -1;
static int gPendingRiseTarget = -1;
static unsigned long gPendingRiseStart = 0;
static bool gPendingFallActive = false;
static int gPendingFallTarget = -1;
static unsigned long gPendingFallStart = 0;

static void resetDeltaMonitorState() {
    // ITA: Reset completo di buffer, filtri e stato allarmi.
    // ENG: Full reset of buffers, filters, and alarm state.
    for (int i = 0; i < DELTA_WINDOW_SIZE; i++) gDeltaWindow[i] = 0.0f;
    gDeltaWindowCount = 0;
    gDeltaWindowIdx = 0;
    gDeltaInvalidStreak = 0;
    gFilteredDeltaValid = false;
    gFilteredDelta = 0.0f;
    gStableExceeded = -1;
    gPendingRiseTarget = -1;
    gPendingRiseStart = 0;
    gPendingFallActive = false;
    gPendingFallTarget = -1;
    gPendingFallStart = 0;
}

static float medianWindowValue() {
    // ITA: Calcola la mediana della finestra per smorzare outlier.
    // ENG: Computes rolling-window median to suppress outliers.
    if (gDeltaWindowCount <= 0) return 0.0f;
    float tmp[DELTA_WINDOW_SIZE];
    for (int i = 0; i < gDeltaWindowCount; i++) tmp[i] = gDeltaWindow[i];

    for (int i = 0; i < gDeltaWindowCount - 1; i++) {
        for (int j = i + 1; j < gDeltaWindowCount; j++) {
            if (tmp[j] < tmp[i]) {
                float t = tmp[i];
                tmp[i] = tmp[j];
                tmp[j] = t;
            }
        }
    }

    const int mid = gDeltaWindowCount / 2;
    if ((gDeltaWindowCount % 2) == 1) return tmp[mid];
    return (tmp[mid - 1] + tmp[mid]) * 0.5f;
}

static int computeExceededLevelInstant(float currentP) {
    // ITA: Ritorna il massimo livello soglia superato istantaneamente.
    // ENG: Returns the highest threshold level currently exceeded.
    if (config.numVelocitaSistema <= 0) return -1;
    int maxExceeded = -1;
    for (int i = 1; i <= config.numVelocitaSistema; i++) {
        float base = config.deltaP_Calib[i];
        if (base <= 5.0f) continue;
        float limit = base * (1.0f + (config.perc_Calib[i] / 100.0f));
        if (currentP > limit) maxExceeded = i;
    }
    return maxExceeded;
}

static float clearLimitForLevel(int level) {
    // ITA: Limite di rientro con isteresi in discesa.
    // ENG: Hysteresis-based clear limit for downward transitions.
    if (level < 1 || level > config.numVelocitaSistema) return 0.0f;
    float base = config.deltaP_Calib[level];
    float limit = base * (1.0f + (config.perc_Calib[level] / 100.0f));
    float clearLimit = limit * (1.0f - (ALERT_CLEAR_HYST_PCT / 100.0f));
    float floorLimit = base * 1.02f; // non scendere sotto ~baseline
    return (clearLimit > floorLimit) ? clearLimit : floorLimit;
}

static void updateThresholdState(float filteredP, bool validSample) {
    // ITA: FSM con hold-time su salita/discesa per stabilita'.
    // ENG: FSM with rise/fall hold-time for stability.
    if (!validSample || config.numVelocitaSistema <= 0) return;

    const unsigned long now = millis();
    const int instantExceeded = computeExceededLevelInstant(filteredP);

    // Gestione salita stato (verso soglie piu' alte).
    if (instantExceeded > gStableExceeded) {
        if (gPendingRiseTarget != instantExceeded) {
            gPendingRiseTarget = instantExceeded;
            gPendingRiseStart = now;
        } else if ((now - gPendingRiseStart) >= ALERT_RISE_HOLD_MS) {
            gStableExceeded = instantExceeded;
            gPendingRiseTarget = -1;
        }
    } else {
        gPendingRiseTarget = -1;
    }

    // Gestione discesa stato con isteresi.
    if (gStableExceeded >= 0) {
        bool belowClear = (filteredP < clearLimitForLevel(gStableExceeded));
        int desiredFallTarget = belowClear ? instantExceeded : gStableExceeded;
        if (desiredFallTarget < gStableExceeded) {
            if (!gPendingFallActive || gPendingFallTarget != desiredFallTarget) {
                gPendingFallActive = true;
                gPendingFallTarget = desiredFallTarget;
                gPendingFallStart = now;
            } else if ((now - gPendingFallStart) >= ALERT_FALL_HOLD_MS) {
                gStableExceeded = desiredFallTarget;
                gPendingFallActive = false;
            }
        } else {
            gPendingFallActive = false;
        }
    } else {
        gPendingFallActive = false;
    }
}

void updateDeltaPMonitoring(float rawDeltaP, bool isValidSample) {
    // ITA: Pipeline filtro: validazione -> mediana -> EMA -> soglie.
    // ENG: Filter pipeline: validation -> median -> EMA -> thresholds.
    if (!isValidSample) {
        gDeltaInvalidStreak++;
        if (gDeltaInvalidStreak >= 3) {
            gFilteredDeltaValid = false;
        }
        return;
    }

    gDeltaInvalidStreak = 0;
    gDeltaWindow[gDeltaWindowIdx] = rawDeltaP;
    gDeltaWindowIdx = (gDeltaWindowIdx + 1) % DELTA_WINDOW_SIZE;
    if (gDeltaWindowCount < DELTA_WINDOW_SIZE) gDeltaWindowCount++;

    float median = medianWindowValue();
    if (gDeltaWindowCount >= DELTA_MIN_VALID_FOR_FILTER) {
        if (!gFilteredDeltaValid) {
            gFilteredDelta = median;
        } else {
            gFilteredDelta = (DELTA_EMA_ALPHA * median) + ((1.0f - DELTA_EMA_ALPHA) * gFilteredDelta);
        }
        gFilteredDeltaValid = true;
        updateThresholdState(gFilteredDelta, true);
    }
}

float getFilteredDeltaP() {
    return gFilteredDelta;
}

bool isFilteredDeltaPValid() {
    return gFilteredDeltaValid;
}

// --- ITA: HTML PAGINA CALIBRAZIONE ---
// --- ENG: CALIBRATION PAGE HTML ---
String getCalibrationPageHTML() {
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>Calibrazione</title>";
    html += "<style>";
    html += "body { font-family: 'Segoe UI', sans-serif; background: #f0f2f5; padding: 20px; text-align: center; }";
    html += ".container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
    html += "input, select, button { padding: 10px; margin: 10px 0; width: 100%; border-radius: 5px; border: 1px solid #ccc; }";
    html += "button { background: #007bff; color: white; font-weight: bold; cursor: pointer; border: none; }";
    html += "button:hover { opacity: 0.9; }";
    html += ".card { background: #f9f9f9; border: 1px solid #ddd; padding: 10px; margin-bottom: 10px; border-radius: 5px; }";
    html += ".hidden { display: none; }";
    html += "</style></head><body>";

    // --- LOGIN PIN ---
    html += "<div id='login-screen' class='container'>";
    html += "<h2>Accesso Calibrazione</h2>";
    html += "<p>Inserire PIN di sicurezza:</p>";
    html += "<input type='password' id='pin' placeholder='PIN (1234)'>";
    html += "<button onclick='checkPin()'>ENTRA</button>";
    html += "<br><a href='/'>Torna alla Home</a>";
    html += "</div>";

    // --- CONTENUTO CALIBRAZIONE ---
    html += "<div id='calib-content' class='container hidden'>";
    html += "<h2>Gestione Calibrazione</h2>";
    
    // Lista calibrazioni esistenti
    html += "<div id='existing-data'>";
    if (config.numVelocitaSistema > 0) {
        html += "<p>Dati salvati (Velocit&agrave;: " + String(config.numVelocitaSistema) + ")</p>";
        for (int i = 1; i <= config.numVelocitaSistema; i++) {
            html += "<div class='card'>";
            html += "<h3>Velocit&agrave; " + String(i) + "</h3>";
            html += "<p>DeltaP Rilevato: <b>" + String(config.deltaP_Calib[i], 1) + " Pa</b></p>";
            html += "<label>Soglia (%): </label>";
            html += "<select id='perc_" + String(i) + "'>";
            for (int p = 10; p <= 90; p += 10) {
                String sel = (config.perc_Calib[i] == p) ? "selected" : "";
                html += "<option value='" + String(p) + "' " + sel + ">" + String(p) + "%</option>";
            }
            html += "</select>";
            html += "</div>";
        }
        html += "<button onclick='updateThresholds()' style='background:#28a745;'>AGGIORNA SOGLIE</button>";
    } else {
        html += "<p>Nessuna calibrazione presente.</p>";
    }
    html += "</div>";

    html += "<hr>";
    html += "<h3>Nuova Calibrazione</h3>";
    html += "<button onclick='startWizard()' style='background:#17a2b8;'>AVVIA WIZARD GUIDATO</button>";
    html += "<br><br><a href='/'>Torna alla Home</a>";
    html += "</div>";

    // --- WIZARD MODAL ---
    html += "<div id='wizard-screen' class='container hidden'>";
    html += "<h2 id='wiz-title'>Wizard Calibrazione</h2>";
    html += "<div id='wiz-body'></div>";
    html += "<div id='wiz-controls'>";
    html += "<button id='btn-next' onclick='nextStep()'>AVANTI</button>";
    html += "<button onclick='location.reload()' style='background:#6c757d;'>ANNULLA</button>";
    html += "</div></div>";

    // --- SCRIPT JS ---
    html += "<script>";
    html += "let currentStep = 0; let totalSpeeds = 0; let currentSpeedIndex = 0;";
    
    // Login
    html += "function checkPin() {";
    html += "  if(document.getElementById('pin').value === '1234') {";
    html += "    document.getElementById('login-screen').classList.add('hidden');";
    html += "    document.getElementById('calib-content').classList.remove('hidden');";
    html += "  } else { alert('PIN Errato!'); }";
    html += "}";

    // Aggiorna Soglie
    html += "function updateThresholds() {";
    html += "  let data = {};";
    html += "  let count = " + String(config.numVelocitaSistema) + ";";
    html += "  for(let i=1; i<=count; i++) {";
    html += "    data['p'+i] = document.getElementById('perc_'+i).value;";
    html += "  }";
    html += "  fetch('/api/calib/update', {method:'POST', body:JSON.stringify(data)})";
    html += "    .then(r => { alert('Soglie Aggiornate!'); location.reload(); });";
    html += "}";

    // Wizard Logic
    html += "function startWizard() {";
    html += "  document.getElementById('calib-content').classList.add('hidden');";
    html += "  document.getElementById('wizard-screen').classList.remove('hidden');";
    html += "  currentStep = 1; renderStep();";
    html += "}";

    html += "function renderStep() {";
    html += "  let body = document.getElementById('wiz-body');";
    html += "  let btn = document.getElementById('btn-next');";
    html += "  btn.disabled = false; btn.innerText = 'AVANTI';";
    
    html += "  if(currentStep === 1) {";
    html += "    body.innerHTML = '<p>Inserisci il numero di velocit&agrave; del sistema:</p><input type=\"number\" id=\"numSpeeds\" min=\"1\" max=\"10\">';";
    html += "    btn.onclick = () => { let v = document.getElementById('numSpeeds').value; if(v){ totalSpeeds=parseInt(v); currentSpeedIndex=1; currentStep=2; renderStep(); } };";
    html += "  }";
    
    html += "  else if(currentStep === 2) {"; // Richiesta cambio velocità
    html += "    let msg = 'Imposta il motore alla velocit&agrave; ' + currentSpeedIndex + '.';";
    html += "    body.innerHTML = '<p>' + msg + '</p>';";
    html += "    btn.onclick = () => { currentStep = 3; renderStep(); };";
    html += "  }";

    html += "  else if(currentStep === 3) {"; // Campionamento
    html += "    body.innerHTML = '<p>Rilevamento in corso... (20 sec)</p><h3 id=\"sample-val\">--</h3>';";
    html += "    btn.disabled = true;";
    html += "    fetch('/api/calib/start_sample').then(() => checkSampleStatus());";
    html += "  }";

    html += "  else if(currentStep === 4) {"; // Impostazione Percentuale
    html += "    let content = '<p>Valore Rilevato: <b>' + lastVal + ' Pa</b></p><p>Scegli soglia percentuale:</p><select id=\"stepPerc\">';";
    html += "    for(let i=10; i<=90; i+=10) content += '<option value=\"'+i+'\">'+i+'%</option>';";
    html += "    content += '</select>';";
    html += "    body.innerHTML = content;";
    html += "    btn.onclick = () => { saveStepData(); };";
    html += "  }";
    
    html += "  else if(currentStep === 5) {"; // Finale
    html += "    body.innerHTML = '<h3>Calibrazione Completata!</h3><p>Salvataggio in corso...</p>';";
    html += "    btn.classList.add('hidden');";
    html += "    setTimeout(() => location.reload(), 3000);";
    html += "  }";
    html += "}";

    html += "let lastVal = 0;";
    html += "function checkSampleStatus() {";
    html += "  fetch('/api/calib/status').then(r=>r.json()).then(d => {";
    html += "    if(d.status === 'done') { lastVal = d.value; currentStep = 4; renderStep(); }";
    html += "    else { document.getElementById('sample-val').innerText = d.current + ' Pa'; setTimeout(checkSampleStatus, 1000); }";
    html += "  });";
    html += "}";

    html += "function saveStepData() {";
    html += "  let p = document.getElementById('stepPerc').value;";
    html += "  fetch('/api/calib/save_step?idx='+currentSpeedIndex+'&val='+lastVal+'&perc='+p+'&total='+totalSpeeds, {method:'POST'})";
    html += "    .then(() => {";
    html += "       if(currentSpeedIndex < totalSpeeds) { currentSpeedIndex++; currentStep = 2; renderStep(); }";
    html += "       else { currentStep = 5; renderStep(); }";
    html += "    });";
    html += "}";

    html += "</script></body></html>";
    server.send(200, "text/html", html);
    return "";
}

// --- ITA: API HANDLERS ---
// --- ENG: API HANDLERS ---

void handleApiStartSample() {
    // ITA: Avvia una nuova finestra di campionamento.
    // ENG: Starts a new sampling window.
    isSampling = true;
    sampleCount = 0;
    sampleAccumulator = 0.0;
    sampleStartTime = millis();
    lastSampleTick = sampleStartTime;
    lastSampledValue = 0.0f;
    server.send(200, "text/plain", "OK");
}

void handleApiStatus() {
    // ITA: Espone stato wizard e valore corrente/finale.
    // ENG: Exposes wizard status and current/final value.
    String json = "{";
    if (isSampling) {
        json += "\"status\":\"sampling\",";
        json += "\"current\":\"" + String(currentDeltaP, 1) + "\"";
    } else {
        json += "\"status\":\"done\",";
        json += "\"value\":\"" + String(lastSampledValue, 1) + "\"";
    }
    json += "}";
    server.send(200, "application/json", json);
}

void handleApiSaveStep() {
    // ITA: Salva un punto calibrazione (indice, valore, percentuale).
    // ENG: Saves one calibration point (index, value, percentage).
    if (server.hasArg("idx") && server.hasArg("val") && server.hasArg("perc")) {
        int idx = server.arg("idx").toInt();
        float val = server.arg("val").toFloat();
        int perc = server.arg("perc").toInt();
        int total = server.arg("total").toInt();

        if (idx >= 0 && idx <= 10) {
            config.deltaP_Calib[idx] = val;
            config.perc_Calib[idx] = perc;
            config.numVelocitaSistema = total;
            
            // Salvataggio in memoria se è l'ultimo step
            if (idx == total) {
                Preferences pref;
                pref.begin("easy", false);
                pref.putBytes("calibP", config.deltaP_Calib, sizeof(config.deltaP_Calib));
                pref.putBytes("calibPerc", config.perc_Calib, sizeof(config.perc_Calib));
                pref.putInt("numVel", config.numVelocitaSistema);
                pref.end();
            }
        }
    }
    server.send(200, "text/plain", "OK");
}

void handleApiUpdateThresholds() {
    // ITA: Aggiorna solo le percentuali soglia via API.
    // ENG: Updates only threshold percentages via API.
    if (server.hasArg("plain")) {
        String body = server.arg("plain");
        // Parsing JSON semplificato manuale
        // Per semplicità, iteriamo sugli argomenti se inviati come form o parsing grezzo
        // Qui usiamo un approccio semplice: il JS invia un JSON, ma ArduinoJson non è incluso nel contesto.
        // Usiamo un parsing manuale veloce basato sulla stringa ricevuta {"p0":"10","p1":"20"...}
        
        for(int i=1; i<=config.numVelocitaSistema; i++) {
            String key = "\"p" + String(i) + "\":\"";
            int start = body.indexOf(key);
            if (start != -1) {
                start += key.length();
                int end = body.indexOf("\"", start);
                String val = body.substring(start, end);
                config.perc_Calib[i] = val.toInt();
            }
        }

        Preferences pref;
        pref.begin("easy", false);
        pref.putBytes("calibPerc", config.perc_Calib, sizeof(config.perc_Calib));
        pref.end();
    }
    server.send(200, "text/plain", "OK");
}

// --- ITA: LOOP CALIBRAZIONE (chiamato nel loop principale) ---
// --- ENG: CALIBRATION LOOP (called from main loop) ---
void calibrationLoop() {
    if (isSampling) {
        unsigned long now = millis();
        const unsigned long SAMPLE_INTERVAL_MS = 1000;
        const unsigned long SAMPLE_WINDOW_MS = 20000;

        // Campionamento a 1 Hz: media solo su campioni validi.
        while (now - lastSampleTick >= SAMPLE_INTERVAL_MS) {
            lastSampleTick += SAMPLE_INTERVAL_MS;
            if (currentDeltaPValid) {
                sampleAccumulator += currentDeltaP;
                sampleCount++;
            }
        }

        // Finestra temporale fissa di 20 secondi.
        if (now - sampleStartTime >= SAMPLE_WINDOW_MS) {
            lastSampledValue = (sampleCount > 0) ? (sampleAccumulator / (float)sampleCount) : 0.0f;
            isSampling = false;
        }
    }
}

// --- ITA: SETUP SERVER ---
// --- ENG: SERVER SETUP ---
void setupCalibration() {
    server.on("/calibrazione", []() { server.send(200, "text/html", getCalibrationPageHTML()); });
    server.on("/api/calib/start_sample", handleApiStartSample);
    server.on("/api/calib/status", handleApiStatus);
    server.on("/api/calib/save_step", HTTP_POST, handleApiSaveStep);
    server.on("/api/calib/update", HTTP_POST, handleApiUpdateThresholds);
    
    // Caricamento dati all'avvio
    Preferences pref;
    pref.begin("easy", true);
    config.numVelocitaSistema = pref.getInt("numVel", 0);
    pref.getBytes("calibP", config.deltaP_Calib, sizeof(config.deltaP_Calib));
    pref.getBytes("calibPerc", config.perc_Calib, sizeof(config.perc_Calib));
    pref.end();
    resetDeltaMonitorState();
}

// --- ITA: LOGICA SOGLIE ---
// --- ENG: THRESHOLD LOGIC ---
String checkThresholds(float currentP) {
    (void)currentP; // La decisione usa stato stabile interno (filtro + isteresi).
    if (config.numVelocitaSistema == 0 || !gFilteredDeltaValid) return "";

    if (gStableExceeded != -1) {
        if (gStableExceeded == config.numVelocitaSistema) {
            return "Cambiare Filtri";
        } else {
            return "Superata soglia " + String(gStableExceeded);
        }
    }
    
    return "";
}
