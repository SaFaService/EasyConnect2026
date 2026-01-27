#include "Calibration.h"
#include "GestioneMemoria.h"
#include <Preferences.h>
#include <WebServer.h>

extern Impostazioni config;
extern Preferences memoria;
extern float currentDeltaP; // Variabile globale definita in main_master.cpp
extern WebServer server; // Riferimento al server HTTP definito in WebHandler.cpp

// Variabili di stato per il Wizard
bool isSampling = false;
unsigned long sampleStartTime = 0;
unsigned long lastSampleTick = 0;
int sampleCount = 0;
float sampleAccumulator = 0.0;
float lastSampledValue = 0.0;

// --- HTML PAGINA CALIBRAZIONE ---
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

// --- API HANDLERS ---

void handleApiStartSample() {
    isSampling = true;
    sampleCount = 0;
    sampleAccumulator = 0.0;
    sampleStartTime = millis();
    lastSampleTick = 0;
    server.send(200, "text/plain", "OK");
}

void handleApiStatus() {
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
                memoria.begin("easy", false);
                memoria.putBytes("calibP", config.deltaP_Calib, sizeof(config.deltaP_Calib));
                memoria.putBytes("calibPerc", config.perc_Calib, sizeof(config.perc_Calib));
                memoria.putInt("numVel", config.numVelocitaSistema);
                memoria.end();
            }
        }
    }
    server.send(200, "text/plain", "OK");
}

void handleApiUpdateThresholds() {
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

        memoria.begin("easy", false);
        memoria.putBytes("calibPerc", config.perc_Calib, sizeof(config.perc_Calib));
        memoria.end();
    }
    server.send(200, "text/plain", "OK");
}

// --- LOOP DI CALIBRAZIONE (Chiamato nel loop principale) ---
void calibrationLoop() {
    if (isSampling) {
        unsigned long now = millis();
        // Eseguiamo un campionamento ogni secondo
        if (now - lastSampleTick >= 1000) {
            lastSampleTick = now;
            sampleAccumulator += currentDeltaP;
            sampleCount++;
            
            // Fine campionamento (20 campioni)
            if (sampleCount >= 20) {
                lastSampledValue = sampleAccumulator / 20.0;
                isSampling = false;
            }
        }
    }
}

// --- SETUP SERVER ---
void setupCalibration() {
    server.on("/calibrazione", []() { server.send(200, "text/html", getCalibrationPageHTML()); });
    server.on("/api/calib/start_sample", handleApiStartSample);
    server.on("/api/calib/status", handleApiStatus);
    server.on("/api/calib/save_step", HTTP_POST, handleApiSaveStep);
    server.on("/api/calib/update", HTTP_POST, handleApiUpdateThresholds);
    
    // Caricamento dati all'avvio
    memoria.begin("easy", true);
    config.numVelocitaSistema = memoria.getInt("numVel", 0);
    memoria.getBytes("calibP", config.deltaP_Calib, sizeof(config.deltaP_Calib));
    memoria.getBytes("calibPerc", config.perc_Calib, sizeof(config.perc_Calib));
    memoria.end();
}

// --- LOGICA SOGLIE ---
String checkThresholds(float currentP) {
    if (config.numVelocitaSistema == 0) return ""; // Nessuna calibrazione

    int maxExceeded = -1;

    // Controlla tutte le soglie
    for (int i = 1; i <= config.numVelocitaSistema; i++) {
        float limit = config.deltaP_Calib[i] * (1.0 + (config.perc_Calib[i] / 100.0));
        // Ignora soglie a 0 o molto basse se non rilevanti
        if (config.deltaP_Calib[i] > 5.0 && currentP > limit) {
            maxExceeded = i;
        }
    }

    if (maxExceeded != -1) {
        if (maxExceeded == config.numVelocitaSistema) {
            return "Cambiare Filtri";
        } else {
            return "Superata soglia " + String(maxExceeded);
        }
    }
    
    return "";
}