#include "Sensori_I2C.h"

#include <Arduino.h>
#include <driver/i2c.h>

namespace SensoriI2C {
namespace {

constexpr i2c_port_t kI2cPort = I2C_NUM_0;
constexpr uint8_t kShtc3Address = 0x70;
constexpr TickType_t kI2cTimeoutTicks = pdMS_TO_TICKS(60);
constexpr uint32_t kInitRetryDelayMs = 2000;

constexpr uint16_t kCmdWakeup = 0x3517;
constexpr uint16_t kCmdSleep = 0xB098;
constexpr uint16_t kCmdSoftReset = 0x805D;
constexpr uint16_t kCmdMeasureTFirstNpm = 0x7866;

bool s_ready = false;
uint32_t s_last_init_try_ms = 0;

uint8_t crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if (crc & 0x80U) {
                crc = static_cast<uint8_t>((crc << 1U) ^ 0x31U);
            } else {
                crc <<= 1U;
            }
        }
    }
    return crc;
}

bool write_command(uint16_t cmd) {
    const uint8_t payload[2] = {
        static_cast<uint8_t>((cmd >> 8) & 0xFFU),
        static_cast<uint8_t>(cmd & 0xFFU),
    };
    return i2c_master_write_to_device(kI2cPort, kShtc3Address, payload, sizeof(payload), kI2cTimeoutTicks) == ESP_OK;
}

bool read_raw(uint8_t* buffer, size_t length) {
    return i2c_master_read_from_device(kI2cPort, kShtc3Address, buffer, length, kI2cTimeoutTicks) == ESP_OK;
}

bool read_measurement(float& out_temp_c, float& out_humidity_pct) {
    if (!write_command(kCmdWakeup)) {
        return false;
    }
    delay(1);

    if (!write_command(kCmdMeasureTFirstNpm)) {
        write_command(kCmdSleep);
        return false;
    }
    delay(14);

    uint8_t payload[6] = {0};
    if (!read_raw(payload, sizeof(payload))) {
        write_command(kCmdSleep);
        return false;
    }

    const bool temp_crc_ok = crc8(payload, 2) == payload[2];
    const bool hum_crc_ok = crc8(payload + 3, 2) == payload[5];
    if (!temp_crc_ok || !hum_crc_ok) {
        write_command(kCmdSleep);
        return false;
    }

    const uint16_t raw_temp = static_cast<uint16_t>((payload[0] << 8) | payload[1]);
    const uint16_t raw_hum = static_cast<uint16_t>((payload[3] << 8) | payload[4]);

    out_temp_c = -45.0f + (175.0f * static_cast<float>(raw_temp) / 65535.0f);
    out_humidity_pct = 100.0f * static_cast<float>(raw_hum) / 65535.0f;
    if (out_humidity_pct < 0.0f) {
        out_humidity_pct = 0.0f;
    } else if (out_humidity_pct > 100.0f) {
        out_humidity_pct = 100.0f;
    }

    write_command(kCmdSleep);
    return true;
}

}  // namespace

bool begin() {
    if (s_ready) {
        return true;
    }

    const uint32_t now = millis();
    if (s_last_init_try_ms != 0U && (now - s_last_init_try_ms) < kInitRetryDelayMs) {
        return false;
    }
    s_last_init_try_ms = now;

    if (!write_command(kCmdWakeup)) {
        return false;
    }
    delay(1);
    write_command(kCmdSoftReset);
    delay(1);
    write_command(kCmdSleep);

    float temp_c = NAN;
    float hum_pct = NAN;
    s_ready = read_measurement(temp_c, hum_pct);
    return s_ready;
}

bool read(Reading& out_reading) {
    if (!s_ready && !begin()) {
        return false;
    }

    float temp_c = NAN;
    float hum_pct = NAN;
    if (!read_measurement(temp_c, hum_pct)) {
        s_ready = false;
        return false;
    }

    out_reading.temperature_c = temp_c;
    out_reading.humidity_pct = hum_pct;
    return true;
}

}  // namespace SensoriI2C
