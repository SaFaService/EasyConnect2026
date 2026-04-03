# EasyConnect Desktop App (Python)

Applicazione desktop per EasyConnect.

## Stato attuale UI
- Splash: logo `AntraluxCloud` con fade-in + pulse lento e ampio.
- Sotto il logo: messaggi di avvio (senza progress bar):
  - `Caricando utenti...`
  - `Caricando impianti...`
  - `Leggendo le periferiche...`
- Login: card centrata e responsive (40% larghezza, 70% altezza), con link `Recupera password`.
- Home post-login: pulsanti quadrati con icona, ombra e stato pressed:
  - Impianti
  - Impostazioni
  - Configurazione scheda
  - Caricamento firmware
  - Configurazione nuovo impianto
- Le icone custom dei pulsanti si leggono da: `assets/icons/` e `assets/immagini/`
  - `plants.png`
  - `settings.png`
  - `board_config.png`
  - `firmware_upload.png`
  - `new_plant.png`
  - alias supportati: `iconaScheda.png`, `iconaFirmware.png`, `iconaSetting.png`, `Icona Configurazione.png`, ecc.

## Configurazione
File: `appsettings.json`

Default:
- `base_url`: `https://www.antralux.com/impianti`
- `mock_mode`: `false`

Endpoint:
- `/api_desktop_auth.php`
- `/api_desktop_plants.php`
- `/api_serial.php`
- `/api_desktop_create_plant.php`
- `/forgot_password.php`

## Avvio
```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
python main.py
```

## Debug login (terminale)
- Log runtime app: `easyconnect_debug.log` (nella root di `EasyConnectAPP`)
- Test login da terminale:
```powershell
python tools\debug_auth_cli.py
```

## Logging accessi
- Tabella DB: `user_access_logs`
- SQL: `sql_step_2_13_user_access_logs.sql`
- Canale registrato: `web` o `desktop_app`
