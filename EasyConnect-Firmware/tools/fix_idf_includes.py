"""
fix_idf_includes.py — extra_scripts PlatformIO

Risolve la compilazione della Waveshare vendor lib che usa il nuovo
ESP-IDF 5.x I2C master API (driver/i2c_master.h) con framework-arduinoespressif32.

Catena del problema:
  vendor/i2c.h → driver/i2c_master.h  (da esp_driver_i2c/include)
    → driver/i2c_types.h  (da esp_driver_i2c/include)
    → hal/i2c_types.h     → vecchia versione nel flat SDK manca i2c_clock_source_t

Soluzione:
  1. Aggiunge esp_driver_i2c/include (per driver/i2c_master.h)
  2. Inietta include/ del progetto in CPPPATH (per lo shim include/hal/i2c_types.h)
     Lo shim usa #include_next per inglobare la vecchia hal/i2c_types.h e aggiunge
     i2c_clock_source_t e i2c_addr_bit_len_t richiesti dal nuovo API.
"""

Import("env")
import os

# ── 1. esp_driver_i2c headers (driver/i2c_master.h, driver/i2c_types.h) ──────
packages_dir = os.path.join(os.path.expanduser("~"), ".platformio", "packages")
libs_inc = os.path.join(packages_dir, "framework-arduinoespressif32-libs",
                        "esp32s3", "include")

paths_to_prepend = []

# Componenti SDK da aggiungere al CPPPATH
sdk_components = [
    ("esp_driver_i2c",    "driver/i2c_master.h + driver/i2c_types.h"),
    # NOTE: NON aggiungere esp_lcd dal -libs: il framework Arduino ha già
    # esp_lcd headers (versione più vecchia). Aggiungere la versione nuova
    # causa conflitti di #define/enum con la vecchia esp_lcd_types.h.
]

for comp, desc in sdk_components:
    p = os.path.join(libs_inc, comp, "include")
    if os.path.isdir(p):
        paths_to_prepend.append(p)
        print(f"[fix_idf_includes] + {comp}: {p}")
    else:
        print(f"[fix_idf_includes] WARNING: {comp} not found: {p}")

if paths_to_prepend:
    env.Append(CPPPATH=paths_to_prepend)

# ── 2. Project include/ — assicura che lo shim include/hal/i2c_types.h sia
#       trovato PRIMA del hal/i2c_types.h del flat SDK ─────────────────────────
project_dir = env.subst("$PROJECT_DIR")
project_include = os.path.join(project_dir, "include")
if os.path.isdir(project_include):
    # Prepend: massima priorità — cercato PRIMA dei path framework
    env.Prepend(CPPPATH=[project_include])
    print(f"[fix_idf_includes] Prepended project include: {project_include}")
else:
    print(f"[fix_idf_includes] WARNING: project include not found: {project_include}")
