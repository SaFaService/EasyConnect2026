#include "DisplayApi_Manager.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>

#include "rs485_network.h"
#include "ui/ui_dc_home.h"
#include "dc_data_model.h"

extern const char* FW_VERSION;

static constexpr unsigned long kDisplayApiIntervalMs = 60000UL;

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

bool displayApiIsBusy() { return s_send_busy; }

static String _u64_to_string(uint64_t value) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    return String(buf);
}

static String _json_escape(const String& value) {
    String out;
    out.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); i++) {
        const char c = value.charAt(i);
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((uint8_t)c < 0x20) out += ' ';
                else out += c;
                break;
        }
    }
    return out;
}

static bool _wifi_api_enabled() {
    return g_dc_model.settings.wifi_enabled;
}

static String _pref_get_string_if_key(Preferences& pref, const char* key, const char* fallback) {
    if (!pref.isKey(key)) return String(fallback ? fallback : "");
    return pref.getString(key, fallback ? fallback : "");
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

static bool _compute_delta_p(float& out_delta_p) {
    bool g1_found = false;
    bool g2_found = false;
    float g1 = 0.0f;
    float g2 = 0.0f;

    const int count = rs485_network_device_count();
    for (int i = 0; i < count; i++) {
        Rs485Device dev;
        if (!rs485_network_get_device(i, dev)) continue;
        if (!dev.online || !dev.data_valid) continue;
        if (dev.type != Rs485DevType::SENSOR) continue;
        if (dev.sensor_profile == Rs485SensorProfile::AIR_010) continue;

        if (!g1_found && dev.group == 1) {
            g1 = dev.p;
            g1_found = true;
        } else if (!g2_found && dev.group == 2) {
            g2 = dev.p;
            g2_found = true;
        }
    }

    if (g1_found && g2_found) {
        out_delta_p = g1 - g2;
        return true;
    }
    out_delta_p = 0.0f;
    return false;
}

static String _device_type_text(const Rs485Device& dev) {
    if (dev.type == Rs485DevType::RELAY) return "relay";
    if (dev.type == Rs485DevType::SENSOR && dev.sensor_profile == Rs485SensorProfile::AIR_010) return "air_010";
    if (dev.type == Rs485DevType::SENSOR) return "sensor";
    return "unknown";
}

static String _build_payload(const DisplayApiConfig& cfg) {
    char plant_name[48] = {};
    ui_plant_name_get(plant_name, sizeof(plant_name));

    float delta_p = 0.0f;
    const bool delta_valid = _compute_delta_p(delta_p);

    const uint32_t uptime_s = millis() / 1000UL;
    const uint32_t heap_free = ESP.getFreeHeap();
    const uint32_t heap_min = ESP.getMinFreeHeap();
    const uint32_t heap_total = ESP.getHeapSize();
    const uint32_t psram_free = ESP.getFreePsram();
    const uint32_t psram_total = ESP.getPsramSize();
    const uint32_t sketch_used = ESP.getSketchSize();
    const uint32_t sketch_free = ESP.getFreeSketchSpace();
    const uint32_t cpu_mhz = getCpuFrequencyMhz();
    const long rssi = WiFi.RSSI();
    const int runtime_count = rs485_network_device_count();
    const int plant_count = rs485_network_plant_device_count();

    String json = "{";
    json.reserve(2300 + (runtime_count * 360));
    json += "\"master_sn\":\"" + _json_escape(cfg.serialNumber) + "\",";
    json += "\"fw_ver\":\"" + _json_escape(String(FW_VERSION)) + "\",";
    json += "\"master_mode\":3,";
    json += "\"plant_kind\":\"display\",";
    json += "\"plant_name\":\"" + _json_escape(String(plant_name)) + "\",";
    json += "\"delta_p\":";
    json += delta_valid ? String(delta_p, 2) : "null";
    json += ",";
    json += "\"rssi\":" + String(rssi) + ",";
    json += "\"traffic\":{";
    json += "\"api_tx_session\":" + _u64_to_string(s_tx_bytes_session) + ",";
    json += "\"api_rx_session\":" + _u64_to_string(s_rx_bytes_session) + ",";
    json += "\"api_posts_session\":" + String(s_posts_session);
    json += "},";
    json += "\"resources\":{";
    json += "\"uptime_s\":" + String(uptime_s) + ",";
    json += "\"cpu_mhz\":" + String(cpu_mhz) + ",";
    json += "\"heap_free\":" + String(heap_free) + ",";
    json += "\"heap_min\":" + String(heap_min) + ",";
    json += "\"heap_total\":" + String(heap_total) + ",";
    json += "\"psram_free\":" + String(psram_free) + ",";
    json += "\"psram_total\":" + String(psram_total) + ",";
    json += "\"sketch_used\":" + String(sketch_used) + ",";
    json += "\"sketch_free\":" + String(sketch_free);
    json += "},";
    json += "\"display\":{";
    json += "\"wifi_enabled\":" + String(_wifi_api_enabled() ? 1 : 0) + ",";
    json += "\"wifi_ssid\":\"" + _json_escape(WiFi.SSID()) + "\",";
    json += "\"ip\":\"" + _json_escape(WiFi.localIP().toString()) + "\",";
    json += "\"runtime_devices\":" + String(runtime_count) + ",";
    json += "\"plant_devices\":" + String(plant_count);
    json += "},";
    json += "\"slaves\":[";

    bool first = true;
    for (int i = 0; i < runtime_count; i++) {
        Rs485Device dev;
        if (!rs485_network_get_device(i, dev)) continue;

        if (!first) json += ",";
        json += "{";
        json += "\"id\":" + String(dev.address) + ",";
        json += "\"sn\":\"" + _json_escape(String(dev.sn)) + "\",";
        json += "\"ver\":\"" + _json_escape(String(dev.version)) + "\",";
        json += "\"grp\":" + String(dev.group) + ",";
        json += "\"online485\":" + String(dev.online ? 1 : 0) + ",";
        json += "\"in_plant\":" + String(dev.in_plant ? 1 : 0) + ",";
        json += "\"data_valid\":" + String(dev.data_valid ? 1 : 0) + ",";
        json += "\"comm_failures\":" + String(dev.comm_failures) + ",";
        json += "\"device_type\":\"" + _device_type_text(dev) + "\"";

        if (dev.type == Rs485DevType::SENSOR) {
            json += ",\"p\":";
            json += (dev.online && dev.data_valid) ? String(dev.p, 2) : "null";
            json += ",\"t\":";
            json += (dev.online && dev.data_valid) ? String(dev.t, 2) : "null";
            json += ",\"h\":";
            json += (dev.online && dev.data_valid) ? String(dev.h, 2) : "null";
            json += ",\"sic\":" + String(dev.sensor_active ? 1 : 0);
            json += ",\"sensor_profile\":" + String((int)dev.sensor_profile);
            json += ",\"sensor_mode\":" + String(dev.sensor_mode);
            if (dev.sensor_profile == Rs485SensorProfile::AIR_010) {
                json += ",\"motor_speed\":" + String(dev.h, 0);
                json += ",\"motor_enabled\":" + String(dev.sensor_active ? 1 : 0);
                json += ",\"motor_feedback_ok\":" + String(dev.sensor_feedback_ok ? 1 : 0);
                json += ",\"motor_feedback_fault\":" + String(dev.sensor_feedback_fault_latched ? 1 : 0);
                json += ",\"motor_state\":\"" + _json_escape(String(dev.sensor_state)) + "\"";
            }
        } else if (dev.type == Rs485DevType::RELAY) {
            json += ",\"relay_mode\":" + String(dev.relay_mode);
            json += ",\"relay_online\":" + String(dev.online ? 1 : 0);
            json += ",\"relay_on\":" + String(dev.relay_on ? 1 : 0);
            json += ",\"relay_safety_closed\":" + String(dev.relay_safety_closed ? 1 : 0);
            json += ",\"relay_feedback_ok\":" + String(dev.relay_feedback_ok ? 1 : 0);
            json += ",\"relay_feedback_fault\":" + String(dev.relay_feedback_fault_latched ? 1 : 0);
            json += ",\"relay_lifetime_alarm\":" + String(dev.relay_life_expired ? 1 : 0);
            json += ",\"relay_state\":\"" + _json_escape(String(dev.relay_state)) + "\"";
        }

        if (!dev.online) {
            json += ",\"err485\":1";
        }
        json += "}";
        first = false;
    }

    json += "]}";
    return json;
}

static void* _mbedtls_calloc_psram(size_t n, size_t size) {
    void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_calloc(n, size, MALLOC_CAP_DEFAULT);
    return p;
}

static int _post_payload(const DisplayApiEndpoint& endpoint, const String& payload, String& response) {
    mbedtls_platform_set_calloc_free(_mbedtls_calloc_psram, heap_caps_free);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(6000);

    Serial.printf("[DISPLAY-API] Heap prima TLS: internal=%u largest_internal=%u psram=%u payload=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)ESP.getFreePsram(),
                  (unsigned)payload.length());

    if (!http.begin(client, endpoint.url)) {
        response = "";
        return -9999;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-API-KEY", endpoint.key);
    const int code = http.POST(payload);
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
        s_send_busy = false;
        return;
    }

    const String payload = _build_payload(cfg);
    if (plan.sameKey) {
        Serial.println("[DISPLAY-API] API key Antralux e utente identiche: invio singolo.");
    }

    for (int i = 0; i < plan.count; i++) {
        const DisplayApiEndpoint& endpoint = plan.endpoints[i];
        String response;
        const int code = _post_payload(endpoint, payload, response);
        s_last_http_code = code;
        s_last_target = endpoint.name;
        if (code != -9999) {
            s_tx_bytes_session += (uint64_t)payload.length();
            s_posts_session++;
        }
        if (response.length() > 0) {
            s_rx_bytes_session += (uint64_t)response.length();
        }

        if (code >= 200 && code < 300) {
            Serial.printf("[DISPLAY-API] Invio OK (%s, HTTP %d)\n", endpoint.name, code);
        } else if (code == 401 || code == 403) {
            Serial.printf("[DISPLAY-API] Accesso negato (%s, HTTP %d): verificare API key.\n",
                          endpoint.name, code);
        } else {
            Serial.printf("[DISPLAY-API] Invio fallito (%s, HTTP %d)\n", endpoint.name, code);
        }
        delay(40);
    }
    s_send_busy = false;
}

void displayApiService() {
    if (!_wifi_api_enabled() || WiFi.status() != WL_CONNECTED) {
        s_first_connected_seen = false;
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
    Serial.printf("Ultimo HTTP    : %d (%s)\n", s_last_http_code, s_last_target.c_str());
    Serial.printf("POST sessione  : %lu\n", (unsigned long)s_posts_session);
    Serial.println("=================================");
}
