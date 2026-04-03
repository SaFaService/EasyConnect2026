# Manuale Scheda Relay EasyConnect 2026

## Scopo
La scheda relay EasyConnect 2026 e' una periferica RS485 basata su ESP32-C3 che pilota un'uscita relay, legge un ingresso di feedback e monitora una linea di sicurezza. Il firmware di riferimento e' quello compilato dall'ambiente PlatformIO `peripheral_relay`.

## Stato firmware verificato
- Firmware testato: `0.1.3`
- Ambiente PlatformIO: `peripheral_relay`
- Build locale riuscita
- Flash su scheda riuscito via USB seriale
- Avvio firmware verificato via seriale USB

Nota operativa: durante il test la scheda risultava con `Safety = APERTA`, quindi l'ingresso sicurezza non era chiuso. In tutte le modalita' tranne `COMANDO`, in questa condizione il relay resta in fault e non puo' attivarsi.

## Pin usati dal firmware
- Relay output: `GPIO3`
- Feedback relay: `GPIO6`
- Sicurezza: `GPIO2`
- RS485 DIR: `GPIO7`
- RS485 TX: `GPIO21`
- RS485 RX: `GPIO20`
- LED verde: `GPIO9`
- LED rosso: `GPIO8`

Definizione attuale del pinout relay:
- file unico pinout progetto: `include/Pins.h`
- sezione relay: macro `PIN_RELAY_*`
- sezione controller/master: macro `PIN_CONTROLLER_*`, `PIN_MASTER_*`, `PIN_LED_EXT_*`
- sezione periferica pressione: macro `PIN_PRESSURE_*` e alias legacy esistenti

Logica elettrica configurata nel firmware:
- Relay attivo alto
- Ingresso sicurezza con `INPUT_PULLUP`
- Sicurezza considerata chiusa quando il pin va a `LOW`

Conseguenza pratica: se il contatto sicurezza non chiude verso GND, la scheda entra in `SAFETY_OPEN`.

## Funzionamento
La scheda mantiene tre blocchi logici principali:
- Controllo relay
- Interfaccia seriale USB per setup e diagnostica
- Interfaccia RS485 per interrogazione, comando e OTA

### Modalita' disponibili
- `LUCE`: comando diretto relay, conteggio ore attivo
- `UVC`: attivazione con verifica feedback, conteggio ore attivo
- `ELETTROSTATICO`: attivazione con verifica feedback, conteggio ore attivo
- `GAS`: comando diretto relay, conteggio ore disattivato
- `COMANDO`: il relay segue automaticamente lo stato della safety; safety chiusa = relay ON, safety aperta = relay OFF

### Regole di sicurezza
- La safety e' sempre prioritaria
- Se la safety si apre, il relay viene spento subito
- In `LUCE`, `UVC`, `ELETTROSTATICO` e `GAS` la safety aperta porta il controller in `FAULT`
- In `COMANDO` la safety viene usata come contatto pulito di comando e il relay segue direttamente il suo stato
- Con safety aperta i comandi ON vengono rifiutati

### Gestione feedback
Il feedback e' configurabile con:
- `enabled`: abilita/disabilita la verifica
- `logic`: `0` attende livello alto, `1` attende livello basso
- `checkDelaySec`: ritardo prima della verifica
- `attempts`: numero totale tentativi

La verifica feedback viene usata solo in `UVC` e `ELETTROSTATICO` quando il feedback e' abilitato.
Se il feedback e' disabilitato, i campi `checkDelaySec` e `attempts` restano memorizzati ma vengono ignorati.

Sequenza tipica in UVC/Elettrostatico:
1. Comando ON
2. Relay acceso
3. Attesa `checkDelaySec`
4. Verifica feedback
5. Se il feedback non e' coerente, spegnimento e retry dopo 2 secondi
6. Se i tentativi finiscono, stato `FAULT`

## LED di stato
### LED verde
- Lampeggio lento: traffico RS485 diretto alla scheda negli ultimi 5 secondi
- Acceso fisso: traffico RS485 generico negli ultimi 5 secondi
- Lampeggio veloce: nessuna attivita' RS485 recente

### LED rosso
- Lampeggio veloce: scheda non configurata
- Lampeggio lento: safety aperta
- Lampeggio veloce: fault relay
- Acceso fisso: scheda configurata, safety chiusa e nessun fault

Nota: se la scheda non e' configurata prevale l'indicazione "non configurata".

## Dati persistenti
La memoria `Preferences` salva:
- indirizzo RS485
- gruppo
- seriale scheda
- modalita'
- boot relay on/off
- configurazione feedback
- messaggi di fault
- contatori ore e avviamenti

Contatori salvati automaticamente:
- ogni 15 secondi mentre cambiano
- quando il relay torna OFF

## Configurazione iniziale via USB
Collegare la scheda via USB e aprire il monitor seriale a `115200`.
Il firmware ora esegue anche l'eco dei caratteri digitati, quindi i comandi sono visibili mentre si scrivono.

Comandi minimi per rendere la scheda configurata:
```text
SETIP 5
SETSERIAL 202603030001
SAVE
INFO
```

Requisiti minimi:
- `SETIP`: valore `1..30`
- `SETSERIAL`: formato `YYYYMM03XXXX`
- `03` identifica la scheda relay

Esempio completo:
```text
SETIP 5
SETSERIAL 202603030001
SETGROUP 1
SETMODE UVC
SETFB ON 0 3 3
SETBOOT OFF
SAVE
INFO
```

## Comandi USB principali
### Base
```text
HELP
INFO
SETIP x
SETSERIAL x
SETGROUP x
SETMODE LUCE|UVC|ELETTROSTATICO|GAS|COMANDO
READIP
READSERIAL
READGROUP
READMODE
SETFB ...
READFB
READCOUNTERS
SAVE
```

### Avanzati
```text
HELPADVANCE
SETFB OFF
SETFB ON l d t
SETFB l d t [en]
SETFBEN 0|1
SETBOOT ON|OFF
SETMSG FB testo
SETMSG SAFETY testo
READFB
READMSG
VIEW485
STOP485
RESETCNT
CLEARMEM
```

### Comandi relay da USB
```text
ADVRELAY ON|OFF|TOGGLE
ADVRELAYFB ON|OFF|TOGGLE
RELAY ON|OFF|TOGGLE|STATUS
```

Differenze:
- `ADVRELAY`: bypass verifica feedback, safety comunque attiva
- `ADVRELAYFB`: usa la logica normale relay
- `RELAY`: comando relay standard, con comportamento dipendente dalla modalita'
- In `COMANDO` i comandi manuali relay vengono rifiutati per evitare conflitti con la logica automatica safety-driven

## Comandi RS485
### Stato
```text
?5
```
Interroga il nodo con indirizzo `5`.

### Comando relay
```text
RLY,5,ON!
RLY,5,OFF!
RLY,5,TOGGLE!
RLY,5,STATUS!
```

Compatibilita' controller standalone:
```text
CMD,5,ON!
CMD,5,OFF!
CMD,5,STATUS!
```
Nota: i frame `CMD,...` vengono accettati solo se la scheda e' in modalita' `UVC`.

### Configurazione classica via RS485
```text
IP5:6!
SER5:202603030001!
GRP5:1!
MOD5:COMANDO!
```

### Configurazione estesa via RS485
```text
BOOT5:ON!
FBC5:OFF!
FBC5:ON,0,3,3!
FBC5:0,3,3,1!
MSG5:FB:FEEDBACK_FAULT!
MSG5:SAFE:SAFETY_OPEN!
CNT5:RESET!
```

## Procedura di messa in servizio consigliata
1. Verificare cablaggio alimentazione e USB.
2. Verificare che il contatto safety chiuda verso GND.
3. Verificare collegamento RS485 su `TX/RX/DIR`.
4. Caricare il firmware `peripheral_relay`.
5. Aprire la seriale USB a `115200`.
6. Impostare `SETIP` e `SETSERIAL`.
7. Impostare la modalita' operativa.
8. Configurare feedback e messaggi fault se necessari.
9. Lanciare `INFO` e verificare:
   - `Configurata : SI`
   - `Safety : CHIUSA` oppure stato coerente con la modalita' `COMANDO`
   - `Ctrl Stato` coerente
10. Provare il relay con `RELAY ON` o via RS485.

## Upload firmware
Comando usato con esito positivo:
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e peripheral_relay -t upload --upload-port COM12
```

Comando generico:
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e peripheral_relay -t upload --upload-port COMx
```

Per sola compilazione:
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e peripheral_relay
```

## Diagnostica rapida
### Caso: relay non parte
Controllare:
- safety chiusa
- modalita' impostata correttamente
- feedback coerente con la logica configurata
- seriale e IP configurati

### Caso: stato `FAULT`
Cause piu' probabili:
- ingresso sicurezza aperto
- feedback non rilevato entro il tempo previsto
- numero tentativi esaurito

Nota: in modalita' `COMANDO`, la safety aperta spegne il relay ma non rappresenta un fault operativo.

### Caso: scheda non configurata
La scheda resta in `Configurata : NO` finche' non sono validi entrambi:
- indirizzo RS485 `1..30`
- seriale nel formato `YYYYMM03XXXX`

## Note finali
- Il firmware supporta anche OTA via RS485.
- La scheda salva i contatori in memoria non volatile.
- Dopo un `CLEARMEM` la scheda riparte con configurazione di fabbrica.
