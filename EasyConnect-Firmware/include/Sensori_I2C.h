#pragma once

#include <stdint.h>

namespace SensoriI2C {

struct Reading {
    float temperature_c;
    float humidity_pct;
};

bool begin();
bool read(Reading& out_reading);

}  // namespace SensoriI2C
