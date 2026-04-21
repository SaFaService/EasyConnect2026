# Display - 0/10V in modalita immissione

Questo documento descrive la gestione delle periferiche 0/10V quando una o piu
schede sono configurate in immissione.

## Gruppi 0/10V

Le periferiche 0/10V usano il campo `group`:

- `group = 1`: Aspirazione
- `group = 2`: Immissione

La Home del Display costruisce le barre di velocita partendo dalle periferiche
salvate nell'impianto e valide a runtime.

## Caso 1: Aspirazione + Immissione

Quando nell'impianto e presente almeno una periferica in aspirazione e almeno
una periferica in immissione, nel menu:

`Impostazioni > Ventilazione Impianto`

sono disponibili:

- `Barra Immissione`: ON/OFF
- `Percentuale Immissione`: 25% - 90%, visibile solo con `Barra Immissione` OFF

### Barra Immissione ON

La Home visualizza due barre:

- `Aspirazione`: comanda tutte le 0/10V con `group = 1`
- `Immissione`: comanda tutte le 0/10V con `group = 2`

Le due barre sono indipendenti.

### Barra Immissione OFF

La Home visualizza solo la barra `Aspirazione`.

Quando l'utente modifica la barra Aspirazione, il Display invia:

- alla periferica di aspirazione: il setpoint calcolato dalla barra Home
- alla periferica di immissione: un setpoint proporzionale calcolato dal valore
  logico della stessa barra

La formula usata per l'immissione e:

```text
immissione = min_aspirazione + barra * ((100 - percentuale_immissione) - min_aspirazione) / 100
```

Dove:

- `barra` e il valore logico della barra Home, da 0 a 100, dopo eventuale snap
  sugli step configurati
- `min_aspirazione` e il valore ottenuto dalla barra Aspirazione al punto 0,
  cioe la velocita minima motore configurata
- `percentuale_immissione` e il valore scelto nel menu `Percentuale Immissione`

Con questa logica:

- barra Home a 0%: immissione uguale al punto minimo dell'aspirazione
- barra Home a 100%: immissione uguale a `100% - Percentuale Immissione`
- valori intermedi: interpolazione lineare tra i due punti

Esempio con velocita minima 20% e `Percentuale Immissione = 30%`:

| Barra Home | Aspirazione | Immissione |
| --- | --- | --- |
| 0% | 20% | 20% |
| 50% | secondo min/max configurati | 45% |
| 100% | secondo min/max configurati | 70% |

## Caso 2: ImmissionAlone

Quando sono presenti una o piu periferiche in immissione ma nessuna periferica
in aspirazione, il Display lavora in modalita `ImmissionAlone`.

In questa modalita:

- la Home visualizza una sola barra `Immissione`
- la barra comanda direttamente tutte le periferiche 0/10V con `group = 2`
- il menu non mostra `Barra Immissione` e `Percentuale Immissione`, perche non
  esiste una barra Aspirazione a cui agganciare il calcolo proporzionale

## Persistenza

Le nuove impostazioni sono salvate in NVS nel namespace `easy_disp`:

- `imm_bar`: abilita/disabilita la barra separata Immissione
- `imm_pct`: percentuale differenziale immissione, limitata tra 25 e 90

