#pragma once

#include <stddef.h>
#include <stdint.h>

enum DcApiCommandAccess : uint8_t {
    DC_API_ACCESS_CUSTOMER = 0,
    DC_API_ACCESS_FACTORY  = 1,
};

// Serializza g_dc_model nel contratto JSON API v1.0.
// Restituisce false se il buffer non e' sufficiente.
bool dc_api_build_payload(char* buf, size_t len);

// Parsing legacy/default: accesso factory.
bool dc_api_parse_command(const char* json);

// Parsing esplicito con livello di accesso dell'endpoint sorgente.
bool dc_api_parse_command_with_access(const char* json, DcApiCommandAccess access);
