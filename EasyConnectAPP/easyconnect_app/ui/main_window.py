from __future__ import annotations

import json
import logging
from pathlib import Path
from typing import Any, Callable

from PySide6.QtCore import QSettings, QThreadPool, QUrl, Qt
from PySide6.QtGui import QDesktopServices
from PySide6.QtWidgets import (
    QComboBox,
    QInputDialog,
    QLabel,
    QMainWindow,
    QMessageBox,
    QStackedWidget,
    QStatusBar,
)

from easyconnect_app.config import AppConfig
from easyconnect_app.models import PlantDetail, SerialPortInfo, UserProfile, UserSession
from easyconnect_app.services.api_client import ApiClient
from easyconnect_app.services.auth_service import AuthService
from easyconnect_app.services.plants_service import PlantsService
from easyconnect_app.services.profile_service import ProfileService
from easyconnect_app.services.serial_service import SerialConsole
from easyconnect_app.services.version_service import VersionInfo, VersionService
from easyconnect_app.ui.home_page import HomePage
from easyconnect_app.ui.login_page import LoginPage
from easyconnect_app.ui.plants_page import PlantsPage
from easyconnect_app.ui.profile_page import ProfilePage
from easyconnect_app.ui.settings_page import SettingsPage
from easyconnect_app.ui.style import APP_STYLE
from easyconnect_app.ui.worker import Worker


class MainWindow(QMainWindow):
    def __init__(self, config: AppConfig) -> None:
        super().__init__()
        self._log = logging.getLogger(__name__)
        self.config = config
        self.setWindowTitle("EasyConnect Desktop")
        self.resize(1320, 840)
        self.setStyleSheet(APP_STYLE)

        self._settings = QSettings("Antralux", "EasyConnectDesktop")
        self.threadpool = QThreadPool.globalInstance()
        self._workers: set[Worker] = set()
        self.session: UserSession | None = None
        self._otp_prompt_count = 0
        self._current_plant_id = 0
        self._available_ports: list[SerialPortInfo] = []
        self._selected_port = ""
        self._appsettings_path = Path(__file__).resolve().parents[2] / "appsettings.json"

        self.api_client = ApiClient(config)
        self.auth_service = AuthService(self.api_client)
        self.plants_service = PlantsService(self.api_client)
        self.profile_service = ProfileService(self.api_client)
        self.version_service = VersionService(self.api_client)
        self.serial_console = SerialConsole()

        self.stack = QStackedWidget()
        self.setCentralWidget(self.stack)

        app_root = Path(__file__).resolve().parents[2]
        logo_path = app_root.parent / "WebSiteComunicazione" / "assets" / "img" / "AntraluxCloud.png"

        self.login_page = LoginPage(logo_path=logo_path if logo_path.exists() else None)
        self.login_page.login_requested.connect(self._handle_login_requested)
        self.login_page.forgot_password_requested.connect(self._open_forgot_password_page)
        self.stack.addWidget(self.login_page)
        self._apply_saved_login_preferences()

        self.home_page = HomePage()
        self.home_page.action_clicked.connect(self._handle_home_action)
        self.home_page.profile_requested.connect(self._open_profile_page)
        self.stack.addWidget(self.home_page)

        self.plants_page = PlantsPage()
        self.plants_page.refresh_requested.connect(self._load_plants)
        self.plants_page.back_requested.connect(self._go_home)
        self.plants_page.plant_selected.connect(self._load_plant_detail)
        self.plants_page.detail_back_requested.connect(lambda: self._set_status_state("lista impianti"))
        self.plants_page.serial_requested.connect(self._open_serial_detail)
        self.plants_page.plant_update_requested.connect(self._update_plant)
        self.stack.addWidget(self.plants_page)

        self.profile_page = ProfilePage()
        self.profile_page.back_requested.connect(self._go_home)
        self.profile_page.refresh_requested.connect(self._load_profile)
        self.profile_page.save_requested.connect(self._save_profile)
        self.stack.addWidget(self.profile_page)

        self.settings_page = SettingsPage()
        self.settings_page.back_requested.connect(self._go_home)
        self.settings_page.save_requested.connect(self._save_settings)
        self.stack.addWidget(self.settings_page)

        self._init_status_bar()
        self._probe_serial_ports()
        self._check_app_version()

    def _init_status_bar(self) -> None:
        status = QStatusBar(self)
        status.setObjectName("AppStatusBar")
        self.setStatusBar(status)

        self._status_state = QLabel("Stato: pronto")
        self._status_info = QLabel("Info: -")
        self._status_user = QLabel("Utente: -")
        self._status_fw = QLabel("FW: -")
        self._status_app = QLabel(f"App: {self.config.app_version}")

        self._com_label = QLabel("COM:")
        self._com_combo = QComboBox()
        self._com_combo.setObjectName("ComSelector")
        self._com_combo.setMinimumWidth(220)
        self._com_combo.currentIndexChanged.connect(self._on_com_changed)

        status.addWidget(self._status_state)
        status.addWidget(self._com_label)
        status.addWidget(self._com_combo)
        status.addWidget(self._status_info, 1)
        status.addPermanentWidget(self._status_user)
        status.addPermanentWidget(self._status_fw)
        status.addPermanentWidget(self._status_app)

    def _set_status_state(self, value: str) -> None:
        self._status_state.setText(f"Stato: {value}")

    def _set_status_info(self, value: str) -> None:
        self._status_info.setText(f"Info: {value}")

    def _set_status_fw(self, value: str) -> None:
        self._status_fw.setText(f"FW: {value}")

    def _set_status_user(self, value: str) -> None:
        self._status_user.setText(f"Utente: {value}")

    def _probe_serial_ports(self) -> None:
        self._run_async(
            self.serial_console.list_ports,
            on_success=self._serial_ports_loaded,
            on_error=lambda _msg: self._set_status_info("Errore rilevamento porte COM"),
        )

    def _serial_ports_loaded(self, ports: list[SerialPortInfo]) -> None:
        self._available_ports = ports
        self._com_combo.blockSignals(True)
        self._com_combo.clear()
        if not ports:
            self._selected_port = ""
            self._com_combo.addItem("Nessuna porta disponibile", "")
            self._set_status_info("Nessuna porta COM rilevata")
            self._com_combo.blockSignals(False)
            return

        for port in ports:
            text = f"{port.device} - {port.description}"
            self._com_combo.addItem(text, port.device)

        last_index = len(ports) - 1
        self._com_combo.setCurrentIndex(last_index)
        self._com_combo.blockSignals(False)
        self._on_com_changed(last_index)
        self._set_status_info(f"Porte COM disponibili: {len(ports)}")

    def _on_com_changed(self, index: int) -> None:
        if index < 0:
            self._selected_port = ""
            return
        self._selected_port = str(self._com_combo.itemData(index) or "").strip()
        if self._selected_port:
            self._set_status_state(f"porta selezionata {self._selected_port}")

    def _run_async(
        self,
        fn: Callable[[], Any],
        *,
        on_success: Callable[[Any], None] | None = None,
        on_error: Callable[[str], None] | None = None,
        on_finished: Callable[[], None] | None = None,
    ) -> None:
        worker = Worker(fn)
        self._workers.add(worker)
        if on_success:
            worker.signals.success.connect(on_success)
        if on_error:
            worker.signals.error.connect(on_error)

        def _cleanup() -> None:
            self._workers.discard(worker)
            if on_finished:
                on_finished()

        worker.signals.finished.connect(_cleanup)
        self.threadpool.start(worker)

    def _handle_login_requested(self, username: str, password: str) -> None:
        self._otp_prompt_count = 0
        self._log.info("UI login requested | user=%s", username.strip())
        self._attempt_login(username, password, "")

    def _attempt_login(self, username: str, password: str, otp_code: str) -> None:
        had_otp = bool(otp_code.strip())
        self._log.info(
            "UI login dispatch | user=%s | had_otp=%s | otp_len=%s",
            username.strip(),
            had_otp,
            len(otp_code.strip()),
        )
        self.login_page.set_busy(True)
        self._set_status_state("verifica credenziali")
        self._run_async(
            lambda: self.auth_service.login(username, password, otp_code),
            on_success=self._login_success,
            on_error=lambda msg: self._login_error(msg, username, password, had_otp),
            on_finished=lambda: self.login_page.set_busy(False),
        )

    def _login_success(self, session: UserSession) -> None:
        self._otp_prompt_count = 0
        self._log.info("UI login success | user=%s | role=%s", session.username, session.role)
        self.session = session
        self._persist_login_preferences(session.username)
        self.home_page.set_session_text(session.username)
        self._set_status_user(session.username)
        self._set_status_state("login effettuato")
        self.showNormal()
        self.setWindowState(self.windowState() | Qt.WindowMaximized)
        self.stack.setCurrentWidget(self.home_page)
        self._probe_serial_ports()

    def _login_error(self, message: str, username: str, password: str, had_otp: bool) -> None:
        self._log.warning(
            "UI login error | user=%s | had_otp=%s | message=%s",
            username.strip(),
            had_otp,
            message,
        )
        self._set_status_state("errore login")
        if message == "2FA_REQUIRED":
            self._prompt_otp_and_retry(
                title="Verifica 2FA",
                text="Inserisci il codice 2FA (Google Authenticator):",
                username=username,
                password=password,
                cancel_message="Login annullato: codice 2FA richiesto.",
            )
            return

        if (not had_otp) and self._message_suggests_2fa(message):
            self._prompt_otp_and_retry(
                title="Verifica 2FA",
                text="Inserisci il codice OTP:",
                username=username,
                password=password,
                cancel_message="Login annullato: codice OTP non inserito.",
            )
            return

        if had_otp and self._message_suggests_2fa(message):
            self._prompt_otp_and_retry(
                title="Codice 2FA non valido",
                text="Codice non valido. Inserisci un nuovo codice OTP:",
                username=username,
                password=password,
                cancel_message=message,
            )
            return

        self.login_page.show_error(message)

    def _prompt_otp_and_retry(
        self,
        *,
        title: str,
        text: str,
        username: str,
        password: str,
        cancel_message: str,
    ) -> None:
        if self._otp_prompt_count >= 4:
            self._log.warning("OTP prompt limit reached | user=%s", username.strip())
            self.login_page.show_error(
                "Troppi tentativi OTP. Verifica ora dispositivo/server e riprova."
            )
            self._set_status_state("errore OTP")
            return

        self._otp_prompt_count += 1
        self._log.info(
            "UI OTP prompt | user=%s | prompt_count=%s",
            username.strip(),
            self._otp_prompt_count,
        )
        code, ok = QInputDialog.getText(self, title, text)
        if ok and code.strip():
            self._attempt_login(username, password, code.strip())
            return
        self.login_page.show_error(cancel_message)
        self._set_status_state("OTP annullato")

    def _message_suggests_2fa(self, message: str) -> bool:
        m = message.lower()
        triggers = ["2fa", "otp", "authenticator", "two factor", "two-factor"]
        return any(t in m for t in triggers)

    def _open_forgot_password_page(self) -> None:
        try:
            url = self.api_client.resolve_endpoint("forgot_password")
        except Exception:
            base_url = self.api_client.config.base_url.strip().rstrip("/")
            if not base_url:
                self.login_page.show_error("Configura base_url per il recupero password.")
                return
            url = base_url + "/forgot_password.php"
        QDesktopServices.openUrl(QUrl(url))

    def _open_profile_page(self) -> None:
        self.stack.setCurrentWidget(self.profile_page)
        self._load_profile()

    def _handle_home_action(self, action_key: str) -> None:
        if action_key == "plants":
            self._open_plants_page()
            return
        if action_key == "settings":
            self._open_settings_page()
            return

        labels = {
            "board_config": "Configurazione scheda",
            "board_read": "Leggi scheda",
            "firmware_upload": "Caricamento firmware",
            "new_plant": "Configurazione nuovo impianto",
        }
        label = labels.get(action_key, action_key)
        QMessageBox.information(
            self,
            "Funzione in preparazione",
            f"'{label}' sara' collegata nella prossima iterazione.",
        )

    def _open_plants_page(self) -> None:
        self.stack.setCurrentWidget(self.plants_page)
        self._load_plants()

    def _go_home(self) -> None:
        self.stack.setCurrentWidget(self.home_page)
        self._set_status_state("home")

    def _open_settings_page(self) -> None:
        self.stack.setCurrentWidget(self.settings_page)
        is_admin = bool(self.session and self.session.role == "admin")
        self.settings_page.load_values(
            app_version=self.config.app_version,
            config=self._read_settings_payload(),
            is_admin=is_admin,
        )
        self._set_status_state("impostazioni software")

    def _load_profile(self) -> None:
        self.profile_page.set_busy(True)
        self._set_status_state("caricamento profilo")
        self._run_async(
            self.profile_service.get_profile,
            on_success=self._profile_loaded,
            on_error=self._profile_error,
            on_finished=lambda: self.profile_page.set_busy(False),
        )

    def _profile_loaded(self, profile: UserProfile) -> None:
        profile = self._merge_profile_with_cache(profile)
        self.profile_page.set_profile(profile)
        self._save_profile_cache(profile)
        self._set_status_state("profilo caricato")

    def _profile_error(self, message: str) -> None:
        cached = self._load_profile_cache()
        if cached:
            self.profile_page.set_profile(cached)
            self.profile_page.show_error(
                f"{message} | visualizzati dati profilo salvati localmente."
            )
            self._set_status_state("errore caricamento profilo")
            return
        if self.session:
            self.profile_page.set_profile(
                UserProfile(
                    email=self.session.username,
                    role=self.session.role,
                    phone="",
                    whatsapp="",
                    telegram="",
                    company="",
                    name="",
                    has_2fa=False,
                )
            )
            self.profile_page.show_error(
                f"{message} | profilo in modalita' base (API non disponibile)."
            )
        else:
            self.profile_page.show_error(message)
        self._set_status_state("errore caricamento profilo")

    def _save_profile(self, payload: dict[str, str]) -> None:
        self.profile_page.set_busy(True)
        self._set_status_state("salvataggio profilo")
        self._run_async(
            lambda: (
                self.profile_service.update_profile(
                    name=payload.get("name", ""),
                    company=payload.get("company", ""),
                    phone=payload.get("phone", ""),
                    whatsapp=payload.get("whatsapp", ""),
                    telegram=payload.get("telegram", ""),
                ),
                payload,
            ),
            on_success=self._profile_saved,
            on_error=self._profile_error,
            on_finished=lambda: self.profile_page.set_busy(False),
        )

    def _profile_saved(self, result: tuple[UserProfile, dict[str, str]]) -> None:
        profile, payload = result
        if not profile.name.strip():
            profile.name = payload.get("name", "")
        if not profile.company.strip():
            profile.company = payload.get("company", "")
        if not profile.phone.strip():
            profile.phone = payload.get("phone", "")
        if not profile.whatsapp.strip():
            profile.whatsapp = payload.get("whatsapp", "")
        if not profile.telegram.strip():
            profile.telegram = payload.get("telegram", "")
        self._save_profile_cache(profile)
        self.profile_page.set_profile(profile)
        self.profile_page.show_error("Profilo aggiornato")
        self._set_status_state("profilo aggiornato")

    def _load_plants(self) -> None:
        if not self.session:
            self.plants_page.show_error("Sessione non disponibile.")
            return

        self._set_status_state("caricamento impianti")
        self.plants_page.set_busy(True)
        self._run_async(
            self.plants_service.list_assigned_plants,
            on_success=self._plants_loaded,
            on_error=self._plants_error,
            on_finished=lambda: self.plants_page.set_busy(False),
        )

    def _plants_loaded(self, plants: list[Any]) -> None:
        self.plants_page.set_plants(plants)
        self._set_status_state(f"impianti caricati ({len(plants)})")
        if not plants:
            self._set_status_fw("-")
            self.plants_page.clear_detail()

    def _plants_error(self, message: str) -> None:
        self.plants_page.show_error(message)
        self.plants_page.clear_detail()
        self._set_status_state("errore caricamento impianti")

    def _load_plant_detail(self, plant_id: int) -> None:
        if plant_id <= 0:
            return
        self._current_plant_id = plant_id
        self._set_status_state(f"caricamento dettaglio impianto #{plant_id}")
        self._run_async(
            lambda: self.plants_service.get_plant_detail(plant_id),
            on_success=self._plant_detail_loaded,
            on_error=self._plant_detail_error,
        )

    def _plant_detail_loaded(self, detail: PlantDetail) -> None:
        self.plants_page.set_detail(detail)
        self._set_status_state(f"dettaglio impianto #{detail.plant.plant_id} caricato")
        self._set_status_fw(detail.plant.firmware_version or "-")

    def _plant_detail_error(self, message: str) -> None:
        self.plants_page.show_error(message)
        self._set_status_state("errore dettaglio impianto")

    def _update_plant(self, plant_id: int, name: str, address: str) -> None:
        self.plants_page.set_busy(True)
        self._set_status_state(f"salvataggio impianto #{plant_id}")
        self._run_async(
            lambda: self.plants_service.update_plant(plant_id, name=name, address=address),
            on_success=lambda _plant: self._plant_updated(plant_id),
            on_error=self._plant_update_error,
            on_finished=lambda: self.plants_page.set_busy(False),
        )

    def _plant_updated(self, plant_id: int) -> None:
        self._set_status_state(f"impianto #{plant_id} aggiornato")
        self._load_plants()
        self._load_plant_detail(plant_id)

    def _plant_update_error(self, message: str) -> None:
        self.plants_page.show_error(message)
        self._set_status_state("errore aggiornamento impianto")

    def _open_serial_detail(self, serial: str) -> None:
        serial = serial.strip()
        if not serial:
            return
        try:
            base = self.api_client.resolve_endpoint("serial_detail")
        except Exception:
            base_url = self.api_client.config.base_url.strip().rstrip("/")
            if not base_url:
                self._set_status_state("serial detail non configurato")
                return
            base = f"{base_url}/serial_detail.php"
        sep = "&" if "?" in base else "?"
        QDesktopServices.openUrl(QUrl(f"{base}{sep}serial={serial}"))

    def _apply_saved_login_preferences(self) -> None:
        remember = bool(self._settings.value("remember_username", False, type=bool))
        username = str(self._settings.value("saved_username", "", type=str))
        self.login_page.set_remember_username(remember)
        if remember and username:
            self.login_page.set_username(username)

    def _persist_login_preferences(self, username: str) -> None:
        remember = self.login_page.remember_username()
        self._settings.setValue("remember_username", remember)
        self._settings.setValue("saved_username", username if remember else "")

    def _check_app_version(self) -> None:
        if self.api_client.mock_mode:
            return

        self._run_async(
            lambda: self.version_service.check(self.config.app_version),
            on_success=self._handle_version_check_result,
            on_error=self._handle_version_check_error,
        )

    def _handle_version_check_result(self, info: VersionInfo | None) -> None:
        if info is None:
            return
        if not self.version_service.is_newer(info.latest_version, self.config.app_version):
            return

        self._set_status_info(
            f"Disponibile nuova versione app: {info.latest_version}"
        )

        msg = QMessageBox(self)
        msg.setIcon(QMessageBox.Information)
        msg.setWindowTitle("Aggiornamento disponibile")
        notes = f"\n\nNote:\n{info.notes}" if info.notes else ""
        msg.setText(
            f"Versione attuale: {self.config.app_version}\n"
            f"Versione disponibile: {info.latest_version}{notes}"
        )
        update_button = msg.addButton("Apri aggiornamento", QMessageBox.AcceptRole)
        msg.addButton("Dopo", QMessageBox.RejectRole)
        msg.exec()
        if msg.clickedButton() is update_button and info.download_url:
            QDesktopServices.openUrl(QUrl(info.download_url))

    def _handle_version_check_error(self, message: str) -> None:
        self._log.info("Version check non disponibile: %s", message)

    def _save_settings(self, payload: dict[str, Any]) -> None:
        if not self.session or self.session.role != "admin":
            self.settings_page.show_message("Salvataggio consentito solo ad admin.")
            return
        self.settings_page.set_busy(True)
        self._run_async(
            lambda: self._write_settings_payload(payload),
            on_success=lambda _ok: self._settings_saved(payload),
            on_error=lambda message: self.settings_page.show_message(message),
            on_finished=lambda: self.settings_page.set_busy(False),
        )

    def _settings_saved(self, payload: dict[str, Any]) -> None:
        self.config.base_url = str(payload.get("base_url", self.config.base_url)).strip()
        self.config.timeout_seconds = int(payload.get("timeout_seconds", self.config.timeout_seconds))
        self.config.verify_ssl = bool(payload.get("verify_ssl", self.config.verify_ssl))
        self.config.mock_mode = bool(payload.get("mock_mode", self.config.mock_mode))
        endpoints = payload.get("endpoints", {})
        if isinstance(endpoints, dict):
            for key, value in endpoints.items():
                self.config.endpoints[str(key)] = str(value)
        self.settings_page.show_message("Impostazioni salvate. Riavvia l'app per applicare tutto.")
        self._set_status_state("impostazioni salvate")

    def _save_profile_cache(self, profile: UserProfile) -> None:
        data = {
            "email": profile.email,
            "role": profile.role,
            "name": profile.name,
            "company": profile.company,
            "phone": profile.phone,
            "whatsapp": profile.whatsapp,
            "telegram": profile.telegram,
            "has_2fa": profile.has_2fa,
        }
        self._settings.setValue("profile_cache_json", json.dumps(data, ensure_ascii=True))

    def _load_profile_cache(self) -> UserProfile | None:
        raw = str(self._settings.value("profile_cache_json", "", type=str) or "").strip()
        if not raw:
            return None
        try:
            data = json.loads(raw)
        except Exception:
            return None
        if not isinstance(data, dict):
            return None
        return UserProfile.from_dict(data)

    def _merge_profile_with_cache(self, profile: UserProfile) -> UserProfile:
        cached = self._load_profile_cache()
        if cached is None:
            return profile
        if not profile.name.strip():
            profile.name = cached.name
        if not profile.company.strip():
            profile.company = cached.company
        if not profile.phone.strip():
            profile.phone = cached.phone
        if not profile.whatsapp.strip():
            profile.whatsapp = cached.whatsapp
        if not profile.telegram.strip():
            profile.telegram = cached.telegram
        return profile

    def _read_settings_payload(self) -> dict[str, Any]:
        if self._appsettings_path.exists():
            try:
                raw = self._appsettings_path.read_text(encoding="utf-8")
                data = json.loads(raw)
                if isinstance(data, dict):
                    return data
            except Exception:
                pass
        return {
            "app_version": self.config.app_version,
            "base_url": self.config.base_url,
            "verify_ssl": self.config.verify_ssl,
            "timeout_seconds": self.config.timeout_seconds,
            "mock_mode": self.config.mock_mode,
            "endpoints": dict(self.config.endpoints),
        }

    def _write_settings_payload(self, payload: dict[str, Any]) -> bool:
        data = self._read_settings_payload()
        data["app_version"] = self.config.app_version
        data["base_url"] = str(payload.get("base_url", data.get("base_url", ""))).strip()
        data["timeout_seconds"] = int(payload.get("timeout_seconds", data.get("timeout_seconds", 20)))
        data["verify_ssl"] = bool(payload.get("verify_ssl", data.get("verify_ssl", True)))
        data["mock_mode"] = bool(payload.get("mock_mode", data.get("mock_mode", False)))
        endpoints_payload = payload.get("endpoints", {})
        endpoints_data = data.get("endpoints", {})
        if not isinstance(endpoints_data, dict):
            endpoints_data = {}
        if isinstance(endpoints_payload, dict):
            for key, value in endpoints_payload.items():
                endpoints_data[str(key)] = str(value)
        data["endpoints"] = endpoints_data
        self._appsettings_path.write_text(
            json.dumps(data, indent=2, ensure_ascii=True) + "\n",
            encoding="utf-8",
        )
        return True
