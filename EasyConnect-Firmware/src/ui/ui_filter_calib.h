#pragma once
#include <stdint.h>

#define UI_FILTER_MAX_POINTS 10

struct UiFilterCalibPoint {
    uint8_t speed_pct;   // velocità motore 0-100%
    float   delta_p;     // Pa rilevati a filtro pulito
};

struct UiFilterCalibData {
    int                n;                              // punti calibrati (0 = non calibrato)
    UiFilterCalibPoint pts[UI_FILTER_MAX_POINTS];
    int                threshold_pct;                  // soglia allarme 10-90%, default 30
    bool               monitoring_en;
};

// Persistenza NVS — namespace "easy_filt"
void                     ui_filter_calib_load(UiFilterCalibData& out);
void                     ui_filter_calib_save(const UiFilterCalibData& data);

// Singleton in RAM (caricato lazy al primo accesso)
const UiFilterCalibData& ui_filter_calib_get();
void                     ui_filter_calib_apply(const UiFilterCalibData& data);  // save + aggiorna RAM

// Verifica se il deltaP corrente supera la soglia per la velocità data.
// Usa interpolazione lineare tra i punti di calibrazione (modalità continua)
// oppure match diretto all'indice (modalità step).
// Ritorna: -1 = monitoraggio disabilitato / dati non disponibili
//           0 = OK (filtro pulito)
//           1 = soglia superata (filtro sporco)
int ui_filter_monitoring_check(float speed_pct, float delta_p, bool valid);

// Legge il deltaP dai sensori di pressione RS485 presenti nell'impianto.
// Se 2 sensori: |p1 - p2|.  Se 1 sensore: p assoluto.
// Ritorna true se almeno un sensore è disponibile.
bool ui_filter_rs485_read_deltap(float& out_dp, bool& out_valid);
