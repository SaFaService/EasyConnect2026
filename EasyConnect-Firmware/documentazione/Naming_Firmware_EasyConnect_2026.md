# Naming Firmware EasyConnect 2026

## Obiettivo
Uniformare la nomenclatura del firmware con termini funzionali:
- `Controller` invece di `Master` (a livello software applicativo)
- `Peripheral` invece di `Slave` (a livello software applicativo)

Per il livello bus RS485 restano validi i nomi storici `RS485_Master` e `RS485_Slave` per compatibilita' tecnica.

## Mappa schede
| Scheda | Ruolo logico | Codice seriale `XX` | Env PlatformIO canonico | Entry point |
|---|---|---:|---|---|
| Standalone/Rewamping | Controller | `02` | `controller_standalone_rewamping` | `src/main_standalone_rewamping_controller.cpp` |
| EasyConnect Display | Controller | `01` | `controller_display` | `src/main_display_controller.cpp` (placeholder) |
| Pressione | Peripheral | `04` | `peripheral_pressure` | `src/main_pressure_peripheral.cpp` |
| Relay | Peripheral | `03` | `peripheral_relay` | `src/main_relay_peripheral.cpp` |
| Inverter 0-10V | Peripheral | `05` | `peripheral_0v10v` | `src/main_0v10v_peripheral.cpp` |

## Regola seriali
Formato unico per tutte le schede:
- `YYYYMMXXNNNN`

Dove:
- `YYYY` = anno
- `MM` = mese
- `XX` = tipo scheda
- `NNNN` = progressivo mensile

Riferimento completo: `documentazione/Logica_Seriali_EasyConnect.txt`

## Moduli condivisi (nomenclatura)
- `src/Serial_Controller.cpp` e `src/Serial_Peripheral.cpp`
- `include/Serial_Manager.h` espone API nuove e alias legacy:
  - `Serial_Controller_Menu()` / `Serial_Peripheral_Menu()`
  - `Serial_Master_Menu()` / `Serial_Slave_Menu()` (compatibilita')
- `include/RS485_Manager.h` espone API nuove e alias di ruolo:
  - `RS485_Controller_Loop()` / `RS485_Peripheral_Loop()`
  - `RS485_Master_Loop()` / `RS485_Slave_Loop()`

## Compatibilita' ambienti build
I nomi storici rimangono come alias:
- `master_rewamping`
- `pressione`
- `relay`
- `motore`
- `inverter`
- `easyconnect`

Quindi script e pipeline esistenti continuano a funzionare senza modifiche immediate.

## Versioning firmware
Ogni modifica firmware richiede incremento versione della scheda interessata.
Regola minima consigliata:
- incremento patch (`x.y.Z`) a ogni change locale
- incremento minor (`x.Y.0`) per release funzionale
- incremento major (`X.0.0`) per incompatibilita' protocollo/config
