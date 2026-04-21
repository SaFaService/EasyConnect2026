#include "ui_filter_calib.h"
#include "rs485_network.h"
#include <Preferences.h>
#include <math.h>
#include <string.h>

static UiFilterCalibData s_data = {};
static bool              s_loaded = false;

// ─── persistenza ─────────────────────────────────────────────────────────────

static void _sort_pts(UiFilterCalibData& d) {
    for (int i = 0; i < d.n - 1; i++) {
        for (int j = i + 1; j < d.n; j++) {
            if (d.pts[j].speed_pct < d.pts[i].speed_pct) {
                UiFilterCalibPoint t = d.pts[i];
                d.pts[i] = d.pts[j];
                d.pts[j] = t;
            }
        }
    }
}

void ui_filter_calib_load(UiFilterCalibData& out) {
    Preferences pref;
    pref.begin("easy_filt", true);
    out.n             = pref.getInt("fn", 0);
    out.threshold_pct = pref.getInt("fthr", 30);
    out.monitoring_en = (bool)pref.getUChar("fen", 0);
    if (out.n > 0 && out.n <= UI_FILTER_MAX_POINTS) {
        pref.getBytes("fpts", out.pts, (size_t)out.n * sizeof(UiFilterCalibPoint));
    }
    pref.end();
    if (out.n < 0 || out.n > UI_FILTER_MAX_POINTS) out.n = 0;
    if (out.threshold_pct < 10 || out.threshold_pct > 90) out.threshold_pct = 30;
}

void ui_filter_calib_save(const UiFilterCalibData& data) {
    UiFilterCalibData sorted = data;
    _sort_pts(sorted);
    Preferences pref;
    pref.begin("easy_filt", false);
    pref.putInt("fn", sorted.n);
    pref.putInt("fthr", sorted.threshold_pct);
    pref.putUChar("fen", (uint8_t)sorted.monitoring_en);
    if (sorted.n > 0) {
        pref.putBytes("fpts", sorted.pts, (size_t)sorted.n * sizeof(UiFilterCalibPoint));
    }
    pref.end();
}

const UiFilterCalibData& ui_filter_calib_get() {
    if (!s_loaded) {
        ui_filter_calib_load(s_data);
        _sort_pts(s_data);
        s_loaded = true;
    }
    return s_data;
}

void ui_filter_calib_apply(const UiFilterCalibData& data) {
    s_data   = data;
    s_loaded = true;
    _sort_pts(s_data);
    ui_filter_calib_save(s_data);
}

// ─── monitoring ──────────────────────────────────────────────────────────────

int ui_filter_monitoring_check(float speed_pct, float delta_p, bool valid) {
    const UiFilterCalibData& d = ui_filter_calib_get();
    if (!d.monitoring_en || d.n == 0 || !valid) return -1;

    float expected = 0.0f;
    if (d.n == 1) {
        expected = d.pts[0].delta_p;
    } else if (speed_pct <= (float)d.pts[0].speed_pct) {
        expected = d.pts[0].delta_p;
    } else if (speed_pct >= (float)d.pts[d.n - 1].speed_pct) {
        expected = d.pts[d.n - 1].delta_p;
    } else {
        for (int i = 0; i < d.n - 1; i++) {
            float s0 = (float)d.pts[i].speed_pct;
            float s1 = (float)d.pts[i + 1].speed_pct;
            if (speed_pct >= s0 && speed_pct < s1) {
                float t = (speed_pct - s0) / (s1 - s0);
                expected = d.pts[i].delta_p + t * (d.pts[i + 1].delta_p - d.pts[i].delta_p);
                break;
            }
        }
    }

    if (expected <= 0.0f) return -1;
    return (delta_p > expected * (1.0f + (float)d.threshold_pct / 100.0f)) ? 1 : 0;
}

// ─── lettura RS485 ────────────────────────────────────────────────────────────

bool ui_filter_rs485_read_deltap(float& out_dp, bool& out_valid) {
    float  p[2] = {};
    int    count = 0;
    const int n = rs485_network_device_count();
    for (int i = 0; i < n && count < 2; i++) {
        Rs485Device dev;
        if (!rs485_network_get_device(i, dev)) continue;
        if (!dev.in_plant || !dev.data_valid || !dev.online) continue;
        if (dev.type != Rs485DevType::SENSOR)                continue;
        if (dev.sensor_profile != Rs485SensorProfile::PRESSURE) continue;
        p[count++] = dev.p;
    }
    if (count == 0) { out_valid = false; out_dp = 0.0f; return false; }
    out_dp    = (count >= 2) ? fabsf(p[0] - p[1]) : p[0];
    out_valid = true;
    return true;
}
