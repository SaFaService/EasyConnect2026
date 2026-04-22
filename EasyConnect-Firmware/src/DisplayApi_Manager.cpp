#include "DisplayApi_Manager.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>

#include "dc_api_json.h"
#include "dc_data_model.h"

static constexpr unsigned long kDisplayApiIntervalMs = 60000UL;
static constexpr size_t kDisplayApiPayloadMaxLen = 12288U;

struct DisplayApiEndpoint {
    const char* name;
    String url;
    String key;
    bool customer;
};

struct DisplayApiDispatchPlan {
    DisplayApiEndpoint endpoints[2];
    int count = 0;
    bool customerConfigured = false;
    bool sameKey = false;
};

static unsigned long s_last_send_ms = 0;
static bool s_first_connected_seen = false;
static uint64_t s_tx_bytes_session = 0;
static uint64_t s_rx_bytes_session = 0;
static uint32_t s_posts_session = 0;
static int s_last_http_code = 0;
static String s_last_target;
static volatile bool s_send_busy = false;
static char s_payload_buffer[kDisplayApiPayloadMaxLen] = {};

bool displayApiIsBusy() { return s_send_busy; }

static String _u64_to_string(uint64_t value) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    return String(buf);
}

static bool _wifi_api_enabled() {
    return g_dc_model.settings.wifi_enabled;
}

static String _pref_get_string_if_key(Preferences& pref, const char* key, const char* fallback) {
    if (!pref.isKey(key)) return String(fallback ? fallback : "");
    return pref.getString(key, fallback ? fallback : "");
}

static String _status_error_text(const char* prefix, int code) {
    String out = prefix ? String(prefix) : String("HTTP");
    out += " ";
    out += String(code);
    return out;
}

static bool _response_get_string(const String& src, const char* key, String& out) {
    const String tag = "\"" + String(key) + "\":\"";
    int p = src.indexOf(tag);
    if (p < 0) return false;
    p += tag.length();

    out = "";
    bool escape = false;
    for (int i = p; i < src.length(); i++) {
        const char c = src[i];
        if (escape) {
            out += c;
            escape = false;
            continue;
        }
        if (c == '\\') {
            escape = true;
            continue;
        }
        if (c == '"') return true;
        out += c;
    }
    return false;
}

static void _set_api_error_text(const String& text) {
    text.toCharArray(g_dc_model.api.last_error, sizeof(g_dc_model.api.last_error));
}

static int _extract_pending_commands(const String& response, String* out_commands, int max_commands) {
    if (!out_commands || max_commands <= 0) return 0;

    const int label_pos = response.indexOf("\"pending_commands\"");
    if (label_pos < 0) return 0;

    const int array_pos = response.indexOf('[', label_pos);
    if (array_pos < 0) return 0;

    bool in_string = false;
    bool escape = false;
    int object_depth = 0;
    int object_start = -1;
    int count = 0;

    for (int i = array_pos + 1; i < response.length(); i++) {
        const char c = response[i];
        if (in_string) {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') in_string = false;
            continue;
        }

        if (c == '"') {
            in_string = true;
            continue;
        }
        if (c == '{') {
            if (object_depth == 0) object_start = i;
            object_depth++;
            continue;
        }
        if (c == '}') {
            if (object_depth > 0) {
                object_depth--;
                if (object_depth == 0 && object_start >= 0) {
                    out_commands[count++] = response.substring(object_start, i + 1);
                    object_start = -1;
                    if (count >= max_commands) break;
                }
            }
            continue;
        }
        if (c == ']' && object_depth == 0) {
            break;
        }
    }

    return count;
}

static void _process_response_commands(const DisplayApiEndpoint& endpoint, const String& response) {
    if (response.length() == 0) return;

    String status;
    if (_response_get_string(response, "status", status) && status.length() > 0 && status != "ok") {
        _set_api_error_text(status);
    }

    String commands[8];
    const int command_count = _extract_pending_commands(response, commands, 8);
    for (int i = 0; i < command_count; i++) {
        const bool ok = dc_api_parse_command_with_access(
            commands[i].c_str(),
            endpoint.customer ? DC_API_ACCESS_CUSTOMER : DC_API_ACCESS_FACTORY);
        Serial.printf("[DISPLAY-API] pending_command[%d] %s (%s)\n",
                      i,
                      ok ? "OK" : "KO",
                      endpoint.name);
    }
}

DisplayApiConfig displayApiLoadConfig() {
    DisplayApiConfig cfg;
    Preferences pref;
    if (!pref.begin("easy", true)) {
        cfg.serialNumber = "NON_SET";
        return cfg;
    }

    cfg.serialNumber = _pref_get_string_if_key(pref, "serialeID", "NON_SET");
    cfg.factoryUrl = _pref_get_string_if_key(pref, "api_url", "");
    cfg.factoryKey = _pref_get_string_if_key(pref, "apiKey", "");
    cfg.customerUrl = pref.isKey("cust_url") ? _pref_get_string_if_key(pref, "cust_url", "")
                                             : _pref_get_string_if_key(pref, "custApiUrl", "");
    cfg.customerKey = pref.isKey("cust_key") ? _pref_get_string_if_key(pref, "cust_key", "")
                                             : _pref_get_string_if_key(pref, "custApiKey", "");
    pref.end();

    cfg.serialNumber.trim();
    if (cfg.serialNumber.length() == 0) cfg.serialNumber = "NON_SET";
    cfg.factoryUrl.trim();
    cfg.factoryKey.trim();
    cfg.customerUrl.trim();
    cfg.customerKey.trim();
    return cfg;
}

static void _put_easy_string(const char* key, const String& value) {
    Preferences pref;
    if (!pref.begin("easy", false)) return;
    pref.putString(key, value);
    pref.end();
}

void displayApiSetSerialNumber(const String& serialNumber) {
    String v = serialNumber;
    v.trim();
    if (v.length() == 0) v = "NON_SET";
    _put_easy_string("serialeID", v);
}

void displayApiSetFactoryUrl(const String& url) {
    String v = url;
    v.trim();
    _put_easy_string("api_url", v);
}

void displayApiSetFactoryKey(const String& key) {
    String v = key;
    v.trim();
    _put_easy_string("apiKey", v);
}

void displayApiSetCustomerUrl(const String& url) {
    String v = url;
    v.trim();
    _put_easy_string("cust_url", v);
}

void displayApiSetCustomerKey(const String& key) {
    String v = key;
    v.trim();
    _put_easy_string("cust_key", v);
}

static DisplayApiDispatchPlan _resolve_plan(const DisplayApiConfig& cfg) {
    DisplayApiDispatchPlan plan;
    const bool has_factory = (cfg.factoryUrl.length() >= 5) && (cfg.factoryKey.length() > 0);
    const bool has_customer = (cfg.customerUrl.length() >= 5) && (cfg.customerKey.length() > 0);
    plan.customerConfigured = has_customer;

    if (has_factory && has_customer && cfg.factoryKey == cfg.customerKey) {
        plan.sameKey = true;
        plan.endpoints[plan.count++] = {"factory", cfg.factoryUrl, cfg.factoryKey, false};
        return plan;
    }

    if (has_factory) {
        plan.endpoints[plan.count++] = {"factory", cfg.factoryUrl, cfg.factoryKey, false};
    }
    if (has_customer) {
        plan.endpoints[plan.count++] = {"customer", cfg.customerUrl, cfg.customerKey, true};
    }
    return plan;
}

static void* _mbedtls_calloc_psram(size_t n, size_t size) {
    void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_calloc(n, size, MALLOC_CAP_DEFAULT);
    return p;
}

static int _post_payload(const DisplayApiEndpoint& endpoint,
                         const char* payload,
                         size_t payload_len,
                         String& response) {
    mbedtls_platform_set_calloc_free(_mbedtls_calloc_psram, heap_caps_free);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(6000);

    Serial.printf("[DISPLAY-API] Heap prima TLS: internal=%u largest_internal=%u psram=%u payload=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)ESP.getFreePsram(),
                  (unsigned)payload_len);

    if (!http.begin(client, endpoint.url)) {
        response = "";
        return -9999;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-API-KEY", endpoint.key);
    const int code = http.POST((uint8_t*)payload, payload_len);
    response = (code > 0) ? http.getString() : "";
    http.end();
    return code;
}

static void _send_now() {
    if (!_wifi_api_enabled()) return;
    if (WiFi.status() != WL_CONNECTED) return;
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;

    s_send_busy = true;
    const DisplayApiConfig cfg = displayApiLoadConfig();
    const DisplayApiDispatchPlan plan = _resolve_plan(cfg);
    if (plan.count == 0) {
        g_dc_model.api.state = DcApiState::IDLE;
        s_send_busy = false;
        return;
    }

    const unsigned long now = millis();
    g_dc_model.api.state = DcApiState::SENDING;
    g_dc_model.api.last_attempt_ms = now;
    g_dc_model.api.last_http_code = 0;
    g_dc_model.api.last_error[0] = '\0';

    if (!dc_api_build_payload(s_payload_buffer, sizeof(s_payload_buffer))) {
        g_dc_model.api.state = DcApiState::ERROR;
        g_dc_model.api.error_count++;
        _set_api_error_text("payload_too_large");
        s_send_busy = false;
        Serial.println("[DISPLAY-API] Payload JSON troppo grande per il buffer.");
        return;
    }

    const size_t payload_len = strlen(s_payload_buffer);
    if (plan.sameKey) {
        Serial.println("[DISPLAY-API] API key Antralux e utente identiche: invio singolo.");
    }

    bool any_success = false;
    for (int i = 0; i < plan.count; i++) {
        const DisplayApiEndpoint& endpoint = plan.endpoints[i];
        String response;
        const int code = _post_payload(endpoint, s_payload_buffer, payload_len, response);
        s_last_http_code = code;
        s_last_target = endpoint.name;
        g_dc_model.api.last_http_code = code;

        if (code != -9999) {
            s_tx_bytes_session += (uint64_t)payload_len;
            s_posts_session++;
            g_dc_model.api.send_count++;
        }
        if (response.length() > 0) {
            s_rx_bytes_session += (uint64_t)response.length();
        }

        if (code >= 200 && code < 300) {
            any_success = true;
            _process_response_commands(endpoint, response);
            Serial.printf("[DISPLAY-API] Invio OK (%s, HTTP %d)\n", endpoint.name, code);
        } else if (code == 401 || code == 403) {
            g_dc_model.api.error_count++;
            _set_api_error_text(_status_error_text("auth", code));
            Serial.printf("[DISPLAY-API] Accesso negato (%s, HTTP %d): verificare API key.\n",
                          endpoint.name, code);
        } else {
            g_dc_model.api.error_count++;
            _set_api_error_text(_status_error_text("http", code));
            Serial.printf("[DISPLAY-API] Invio fallito (%s, HTTP %d)\n", endpoint.name, code);
        }
        delay(40);
    }

    if (any_success) {
        g_dc_model.api.state = DcApiState::OK;
        g_dc_model.api.last_ok_ms = now;
    } else {
        g_dc_model.api.state = DcApiState::ERROR;
    }
    s_send_busy = false;
}

void displayApiService() {
    if (!_wifi_api_enabled() || WiFi.status() != WL_CONNECTED) {
        s_first_connected_seen = false;
        if (!s_send_busy) g_dc_model.api.state = DcApiState::IDLE;
        return;
    }
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;

    const unsigned long now = millis();
    if (!s_first_connected_seen) {
        s_first_connected_seen = true;
        s_last_send_ms = now;
        return;
    }
    if ((now - s_last_send_ms) >= kDisplayApiIntervalMs) {
        s_last_send_ms = now;
        _send_now();
    }
}

void displayApiPrintStatus() {
    const DisplayApiConfig cfg = displayApiLoadConfig();
    const DisplayApiDispatchPlan plan = _resolve_plan(cfg);
    Serial.println("========== DISPLAY API ==========");
    Serial.printf("WiFi abilitato : %s\n", _wifi_api_enabled() ? "SI" : "NO");
    Serial.printf("WiFi connesso  : %s\n", WiFi.status() == WL_CONNECTED ? "SI" : "NO");
    Serial.printf("Seriale        : %s\n", cfg.serialNumber.c_str());
    Serial.printf("URL Antralux   : %s\n", cfg.factoryUrl.c_str());
    if (cfg.factoryKey.length() > 0) {
        Serial.printf("Key Antralux   : Impostata (%u caratteri)\n", (unsigned)cfg.factoryKey.length());
    } else {
        Serial.println("Key Antralux   : NON IMPOSTATA");
    }
    Serial.printf("URL Utente     : %s\n", cfg.customerUrl.c_str());
    if (cfg.customerKey.length() > 0) {
        Serial.printf("Key Utente     : Impostata (%u caratteri)\n", (unsigned)cfg.customerKey.length());
    } else {
        Serial.println("Key Utente     : NON IMPOSTATA");
    }
    Serial.printf("Endpoint attivi: %d%s\n", plan.count, plan.sameKey ? " (dedup key)" : "");
    Serial.printf("Stato runtime  : %d\n", (int)g_dc_model.api.state);
    Serial.printf("Ultimo HTTP    : %d (%s)\n", s_last_http_code, s_last_target.c_str());
    Serial.printf("POST sessione  : %lu\n", (unsigned long)s_posts_session);
    Serial.printf("TX sessione    : %s\n", _u64_to_string(s_tx_bytes_session).c_str());
    Serial.printf("RX sessione    : %s\n", _u64_to_string(s_rx_bytes_session).c_str());
    if (g_dc_model.api.last_error[0] != '\0') {
        Serial.printf("Ultimo errore  : %s\n", g_dc_model.api.last_error);
    }
    Serial.println("=================================");
}
