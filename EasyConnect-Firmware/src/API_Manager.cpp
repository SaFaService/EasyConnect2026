/**
 * ITA: Gestione invio dati del master verso API cloud.
 * ENG: Master data upload manager for cloud APIs.
 *
 * ITA: Questo file decide a quali endpoint inviare (factory/customer),
 *      prepara il payload JSON e gestisce gli esiti HTTP.
 * ENG: This file decides which endpoints to use (factory/customer),
 *      builds the JSON payload, and handles HTTP outcomes.
 */
#include "API_Manager.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "certificates.h"
#include "GestioneMemoria.h"
#include "RS485_Manager.h"    // Per accedere alla struttura DatiSlave
#include "Calibration.h"
#include <WiFi.h>

// ITA: Variabili globali definite in altri moduli.
// ENG: Global variables defined in other modules.
extern const char* FW_VERSION;
extern Impostazioni config;
extern bool listaPerifericheAttive[101];
extern DatiSlave databaseSlave[101];
extern float currentDeltaP;
extern bool currentDeltaPValid;
extern bool debugViewApi;
extern bool apiAuthRejected;
extern bool apiCustomerIssue;
extern bool slaveExcludedByPortal[101];

// ITA: Descrive un endpoint API (nome logico + URL + API key).
// ENG: Describes one API endpoint (logical name + URL + API key).
struct ApiEndpoint {
    String name;
    String url;
    String key;
    bool isCustomer;
};

// ITA: Piano di dispatch calcolato a runtime (1 o 2 endpoint).
// ENG: Runtime dispatch plan (1 or 2 endpoints).
struct ApiDispatchPlan {
    ApiEndpoint endpoints[2];
    int count;
    bool customerConfigured;
    bool sameAsFactory;
};

// ITA: Contatori traffico API della sessione corrente (dal boot).
// ENG: API traffic counters for current boot session.
static uint64_t gApiTxBytesSession = 0;
static uint64_t gApiRxBytesSession = 0;
static uint32_t gApiPostsSession = 0;

static String u64ToString(uint64_t value) {
    // ITA: Conversione robusta uint64 -> String anche su toolchain embedded.
    // ENG: Robust uint64 -> String conversion on embedded toolchains.
    char buf[24];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    return String(buf);
}

static bool isSlaveOnline485(int slaveId, unsigned long nowMs) {
    // ITA: Valuta online se ultimo pacchetto ricevuto <= timeout.
    // ENG: Marks device online if last packet was received within timeout.
    if (slaveId < 1 || slaveId > 100) return false;
    const unsigned long OFFLINE_TIMEOUT_MS = 10000;
    if (databaseSlave[slaveId].lastResponseTime == 0) return false;
    return (nowMs - databaseSlave[slaveId].lastResponseTime) <= OFFLINE_TIMEOUT_MS;
}

static void resetExcludedSlaves() {
    // ITA: Reimposta filtro esclusioni ricevuto dal portale.
    // ENG: Resets exclusion filter received from backend portal.
    for (int i = 0; i <= 100; i++) {
        slaveExcludedByPortal[i] = false;
    }
}

// ITA: Ritorna true solo se il campo ignore_serials è presente e processato.
// ENG: Returns true only if ignore_serials exists and is processed.
static bool applyIgnoreSerialsFromResponse(const String& response) {
    int keyPos = response.indexOf("\"ignore_serials\"");
    if (keyPos < 0) return false;

    int arrStart = response.indexOf('[', keyPos);
    int arrEnd = response.indexOf(']', arrStart >= 0 ? arrStart : keyPos);
    if (arrStart < 0 || arrEnd < 0 || arrEnd <= arrStart) return false;

    String body = response.substring(arrStart + 1, arrEnd);
    body.trim();

    resetExcludedSlaves();
    if (body.length() == 0) return true;

    int from = 0;
    while (from < body.length()) {
        int q1 = body.indexOf('"', from);
        if (q1 < 0) break;
        int q2 = body.indexOf('"', q1 + 1);
        if (q2 < 0) break;

        String sn = body.substring(q1 + 1, q2);
        sn.trim();

        if (sn.length() > 0) {
            for (int i = 1; i <= 100; i++) {
                if (!listaPerifericheAttive[i]) continue;
                if (sn == String(databaseSlave[i].sn)) {
                    slaveExcludedByPortal[i] = true;
                }
            }
        }
        from = q2 + 1;
    }
    return true;
}

static bool computeDeltaPForApi(unsigned long nowMs, float& outDeltaP) {
    // ITA: Priorità 1: gruppo 1 - gruppo 2.
    // ENG: Priority 1: group 1 - group 2.
    // ITA: Fallback: prime due periferiche online (utile per test/lab).
    // ENG: Fallback: first two online devices (useful for testing/lab).
    bool grp1Found = false;
    bool grp2Found = false;
    float grp1Pressure = 0.0f;
    float grp2Pressure = 0.0f;
    bool firstOnlineFound = false;
    bool secondOnlineFound = false;
    float firstOnlinePressure = 0.0f;
    float secondOnlinePressure = 0.0f;

    for (int i = 1; i <= 100; i++) {
        if (!listaPerifericheAttive[i] || slaveExcludedByPortal[i]) continue;
        if (!isSlaveOnline485(i, nowMs)) continue;

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

static String buildPayload(float deltaP, unsigned long nowMs) {
    // ITA: Include telemetria master + stato di tutte le periferiche.
    // ENG: Includes master telemetry + state of all peripherals.
    long rssi = WiFi.RSSI();
    const uint32_t uptimeSeconds = millis() / 1000UL;
    const uint32_t heapFree = ESP.getFreeHeap();
    const uint32_t heapMin = ESP.getMinFreeHeap();
    const uint32_t heapTotal = ESP.getHeapSize();
    const uint32_t sketchUsed = ESP.getSketchSize();
    const uint32_t sketchFree = ESP.getFreeSketchSpace();
    const uint32_t cpuMhz = getCpuFrequencyMhz();
    String json = "{";
    json += "\"master_sn\":\"" + String(config.serialeID) + "\",";
    json += "\"fw_ver\":\"" + String(FW_VERSION) + "\",";
    json += "\"master_mode\":" + String(config.modalitaMaster) + ",";
    json += "\"delta_p\":" + String(deltaP, 2) + ",";
    json += "\"rssi\":" + String(rssi) + ",";
    json += "\"traffic\":{";
    json += "\"api_tx_session\":" + u64ToString(gApiTxBytesSession) + ",";
    json += "\"api_rx_session\":" + u64ToString(gApiRxBytesSession) + ",";
    json += "\"api_posts_session\":" + String(gApiPostsSession);
    json += "},";
    json += "\"resources\":{";
    json += "\"uptime_s\":" + String(uptimeSeconds) + ",";
    json += "\"cpu_mhz\":" + String(cpuMhz) + ",";
    json += "\"heap_free\":" + String(heapFree) + ",";
    json += "\"heap_min\":" + String(heapMin) + ",";
    json += "\"heap_total\":" + String(heapTotal) + ",";
    json += "\"sketch_used\":" + String(sketchUsed) + ",";
    json += "\"sketch_free\":" + String(sketchFree);
    json += "},";
    json += "\"slaves\":[";

    bool first = true;
    for (int i = 1; i <= 100; i++) {
        if (!listaPerifericheAttive[i] || slaveExcludedByPortal[i]) continue;

        const bool online485 = isSlaveOnline485(i, nowMs);
        StandaloneRelaySnapshot relaySnapshot = {};
        const bool hasRelaySnapshot = getStandaloneRelaySnapshot(i, relaySnapshot);
        const bool relayDetected = hasRelaySnapshot && relaySnapshot.detectedAtBoot;
        if (!first) json += ",";
        json += "{";
        json += "\"id\":" + String(i) + ",";
        json += "\"sn\":\"" + String(databaseSlave[i].sn) + "\",";
        json += "\"ver\":\"" + String(databaseSlave[i].version) + "\",";
        json += "\"grp\":" + String(databaseSlave[i].grp) + ",";
        json += "\"online485\":" + String(online485 ? 1 : 0) + ",";
        if (online485) {
            json += "\"p\":" + String(databaseSlave[i].p) + ",";
            json += "\"t\":" + String(databaseSlave[i].t) + ",";
            json += "\"sic\":" + String(databaseSlave[i].sic);
        } else {
            json += "\"p\":null,";
            json += "\"t\":null,";
            json += "\"sic\":null,";
            json += "\"err485\":1";
        }

        // ITA: Se lo slave è una relay board, aggiunge campi dedicati.
        // ENG: If the slave is a relay board, append relay-specific fields.
        if (relayDetected) {
            float hoursRemaining = -1.0f;
            if (relaySnapshot.lifeLimitHours > 0) {
                hoursRemaining = static_cast<float>(relaySnapshot.lifeLimitHours) - relaySnapshot.hours;
                if (hoursRemaining < 0.0f) hoursRemaining = 0.0f;
            }
            json += ",\"device_type\":\"relay\"";
            json += ",\"relay_mode\":" + String(relaySnapshot.mode);
            json += ",\"relay_online\":" + String(relaySnapshot.online ? 1 : 0);
            json += ",\"relay_on\":" + String(relaySnapshot.relayOn ? 1 : 0);
            json += ",\"relay_safety_closed\":" + String(relaySnapshot.safetyClosed ? 1 : 0);
            json += ",\"relay_feedback_ok\":" + String(relaySnapshot.feedbackMatched ? 1 : 0);
            json += ",\"relay_feedback_fault\":" + String(relaySnapshot.feedbackFaultLatched ? 1 : 0);
            json += ",\"relay_safety_alarm\":" + String(relaySnapshot.safetyAlarm ? 1 : 0);
            json += ",\"relay_lifetime_alarm\":" + String(relaySnapshot.lifetimeAlarm ? 1 : 0);
            json += ",\"relay_lamp_fault\":" + String(relaySnapshot.lampFault ? 1 : 0);
            json += ",\"relay_life_limit_hours\":" + String(relaySnapshot.lifeLimitHours);
            json += ",\"relay_hours_on\":" + String(relaySnapshot.hours, 2);
            if (hoursRemaining >= 0.0f) {
                json += ",\"relay_hours_remaining\":" + String(hoursRemaining, 2);
            } else {
                json += ",\"relay_hours_remaining\":null";
            }
            json += ",\"relay_starts\":" + String(relaySnapshot.starts);
            json += ",\"relay_state\":\"" + String(relaySnapshot.stateText) + "\"";
        }
        json += "}";
        first = false;
    }
    json += "]}";
    return json;
}

static ApiDispatchPlan resolveDispatchPlan() {
    // ITA: Determina routing invio in base a config factory/customer.
    // ENG: Determines upload routing based on factory/customer config.
    ApiDispatchPlan plan;
    plan.count = 0;
    plan.customerConfigured = false;
    plan.sameAsFactory = false;

    String factoryUrl = String(config.apiUrl);
    String factoryKey = String(config.apiKey);
    String customerUrl = String(config.customerApiUrl);
    String customerKey = String(config.customerApiKey);
    factoryUrl.trim();
    factoryKey.trim();
    customerUrl.trim();
    customerKey.trim();

    const bool hasFactory = factoryUrl.length() >= 5;
    const bool hasCustomer = (customerUrl.length() >= 5) && (customerKey.length() > 0);
    plan.customerConfigured = hasCustomer;
    plan.sameAsFactory = hasFactory && hasCustomer &&
                          ((factoryUrl == customerUrl && factoryKey == customerKey) ||
                           (factoryKey.length() > 0 && factoryKey == customerKey));

    // ITA: Regola:
    //      - se endpoint cliente == endpoint factory o la API key e' identica, invia una sola volta.
    //      - altrimenti invia in sequenza (mai in parallelo).
    // ENG: Rule:
    //      - if customer endpoint equals factory endpoint or the API key is identical, send once only.
    //      - otherwise send sequentially (never in parallel).
    if (plan.sameAsFactory) {
        plan.endpoints[plan.count++] = {"customer", customerUrl, customerKey, true};
        return plan;
    }

    if (hasFactory) {
        plan.endpoints[plan.count++] = {"factory", factoryUrl, factoryKey, false};
    }
    if (hasCustomer) {
        plan.endpoints[plan.count++] = {"customer", customerUrl, customerKey, true};
    }
    return plan;
}

static int postPayloadToEndpoint(const ApiEndpoint& endpoint, const String& json, String& responseOut) {
    // ITA: Connessione TLS con CA root e POST JSON.
    // ENG: TLS connection with root CA and JSON POST.
    WiFiClientSecure client;
    client.setCACert(rootCACertificate);
    HTTPClient http;

    if (!http.begin(client, endpoint.url)) {
        responseOut = "";
        return -9999;
    }

    http.addHeader("Content-Type", "application/json");
    if (endpoint.key.length() > 0) {
        http.addHeader("X-API-KEY", endpoint.key);
    }

    int httpCode = http.POST(json);
    responseOut = (httpCode > 0) ? http.getString() : "";
    http.end();
    return httpCode;
}

void sendDataToRemoteServer() {
    // ITA: Calcolo endpoint effettivi di invio.
    // ENG: Compute effective endpoint dispatch plan.
    ApiDispatchPlan plan = resolveDispatchPlan();

    // ITA: Flag di stato usati anche da UI/LED diagnostici.
    // ENG: Status flags also used by diagnostic UI/LED behavior.
    bool customerAuthRejected = false;
    bool anyAuthRejected = false;
    bool anySuccess = false;
    apiCustomerIssue = !plan.customerConfigured;

    if (WiFi.status() != WL_CONNECTED) {
        // ITA: Nessuna connessione: skip pulito.
        // ENG: No network connection: clean skip.
        apiAuthRejected = false;
        if (debugViewApi) Serial.println("[API-DATA] Invio saltato: WiFi non connesso.");
        return;
    }

    if (plan.count == 0) {
        // ITA: Nessun endpoint valido in configurazione.
        // ENG: No valid endpoint configured.
        apiAuthRejected = false;
        if (debugViewApi) Serial.println("[API-DATA] Invio saltato: nessun endpoint API configurato.");
        return;
    }

    const unsigned long nowMs = millis();
    // ITA: DeltaP preferito: filtro stabile; fallback a calcolo istantaneo.
    // ENG: Preferred DeltaP: stable filtered value; fallback to instant calc.
    float deltaNow = 0.0f;
    const bool deltaNowValid = computeDeltaPForApi(nowMs, deltaNow);
    float deltaForPayload = deltaNow;
    if (isFilteredDeltaPValid()) {
        deltaForPayload = getFilteredDeltaP();
    } else if (!deltaNowValid) {
        deltaForPayload = 0.0f;
    }
    String payload = buildPayload(deltaForPayload, nowMs);

    if (debugViewApi) {
        Serial.println("[API-DATA] JSON: " + payload);
        if (plan.sameAsFactory) {
            Serial.println("[API-DATA] API cliente e factory duplicate: invio singolo.");
        }
    }

    for (int i = 0; i < plan.count; i++) {
        const ApiEndpoint& endpoint = plan.endpoints[i];
        const uint64_t requestBytes = (uint64_t)payload.length();
        if (debugViewApi) {
            Serial.println("[API-DATA] Avvio invio dati a (" + endpoint.name + "): " + endpoint.url);
        }

        String response;
        int code = postPayloadToEndpoint(endpoint, payload, response);
        // ITA: Aggiorna contatori traffico solo su tentativi reali.
        // ENG: Update traffic counters only for actual send attempts.
        if (code != -9999) {
            gApiTxBytesSession += requestBytes;
            gApiPostsSession++;
        }
        if (response.length() > 0) {
            gApiRxBytesSession += (uint64_t)response.length();
        }
        if (debugViewApi) {
            Serial.printf("[API-DATA] HTTP Code (%s): %d\n", endpoint.name.c_str(), code);
            if (response.length() > 0) {
                Serial.println("[API-DATA] Risposta (" + endpoint.name + "): " + response);
            }
        }

        if (code >= 200 && code < 300) {
            anySuccess = true;
            // ITA: Se il server espone ignore_serials, aggiorna la lista locale.
            // ENG: If server exposes ignore_serials, refresh local exclusion list.
            applyIgnoreSerialsFromResponse(response);
        } else if (code == 401 || code == 403) {
            anyAuthRejected = true;
            if (endpoint.isCustomer) customerAuthRejected = true;
            Serial.printf("[API-DATA] Accesso API negato (%s, HTTP %d): verificare API key / impianto.\n",
                          endpoint.name.c_str(), code);
        } else if (code < 0) {
            Serial.printf("[API-DATA] Errore invio (%s): codice %d\n", endpoint.name.c_str(), code);
        }

        // ITA: Piccola pausa per non saturare lo stack di rete embedded.
        // ENG: Small pause to avoid overloading embedded networking stack.
        delay(40);
    }

    // ITA: Propaga stato finale verso altre parti firmware.
    // ENG: Propagate final status to other firmware modules.
    apiCustomerIssue = (!plan.customerConfigured) || customerAuthRejected;
    apiAuthRejected = (!anySuccess && anyAuthRejected);
}
