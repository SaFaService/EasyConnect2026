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
from easyconnect_app.models import BoardSnapshot, Plant, PlantDetail, SerialPortInfo, UserPermissions, UserProfile, UserSession
from easyconnect_app.services.api_client import ApiClient
from easyconnect_app.services.auth_service import AuthService
from easyconnect_app.services.board_config_service import BoardConfigService
from easyconnect_app.services.plants_service import PlantsService
from easyconnect_app.services.profile_service import ProfileService
from easyconnect_app.services.serial_provisioning_service import SerialProvisioningService
from easyconnect_app.services.serial_service import SerialConsole
from easyconnect_app.services.version_service import VersionInfo, VersionService
from easyconnect_app.ui.board_config_page import BoardConfigPage
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
        self.user_profile: UserProfile | None = None
        self._otp_prompt_count = 0
        self._current_plant_id = 0
        self._available_ports: list[SerialPortInfo] = []
        self._selected_port = ""
        self._plants_cache: list[Plant] = []
        self._board_config_detail: PlantDetail | None = None
        self._current_board_snapshot: BoardSnapshot | None = None
        self._appsettings_path = Path(__file__).resolve().parents[2] / "appsettings.json"

        self.api_client = ApiClient(config)
        self.auth_service = AuthService(self.api_client)
        self.plants_service = PlantsService(self.api_client)
        self.profile_service = ProfileService(self.api_client)
        self.version_service = VersionService(self.api_client)
        self.serial_console = SerialConsole()
        self.serial_provisioning_service = SerialProvisioningService(self.api_client)
        self.board_config_service = BoardConfigService(self.serial_console)

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
        self._apply_home_permissions(None)

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

        self.board_config_page = BoardConfigPage()
        self.board_config_page.back_requested.connect(self._go_home)
        self.board_config_page.refresh_plants_requested.connect(self._load_plants)
        self.board_config_page.plant_selected.connect(self._load_board_config_detail)
        self.board_config_page.read_requested.connect(self._read_connected_board)
        self.board_config_page.save_requested.connect(self._save_board_configuration)
        self.stack.addWidget(self.board_config_page)

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
        self._apply_home_permissions(None)
        cached_profile = self._load_profile_cache()
        if cached_profile is not None:
            self._apply_home_permissions(cached_profile)
        self.showNormal()
        self.setWindowState(self.windowState() | Qt.WindowMaximized)
        self.stack.setCurrentWidget(self.home_page)
        self._probe_serial_ports()
        self._load_profile_for_session()

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
        if action_key in {"board_config", "board_read"}:
            self._open_board_config_page()
            return
        if action_key == "firmware_upload" and not self._can_access_action(action_key):
            QMessageBox.warning(
                self,
                "Permesso richiesto",
                "Il caricamento firmware non e' abilitato per questo utente.",
            )
            return
        if action_key == "new_plant" and not self._can_access_action(action_key):
            QMessageBox.warning(
                self,
                "Permesso richiesto",
                "La creazione di un nuovo impianto non e' abilitata per questo utente.",
            )
            return

        labels = {
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

    def _open_board_config_page(self) -> None:
        self.stack.setCurrentWidget(self.board_config_page)
        self._set_status_state("configurazione scheda")
        self.board_config_page.show_message("Seleziona un impianto e leggi la scheda collegata.")
        if self._plants_cache:
            self.board_config_page.set_plants(
                self._plants_cache,
                selected_plant_id=self.board_config_page.selected_plant_id(),
            )
        else:
            self._load_plants()

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

    def _load_profile_for_session(self) -> None:
        self._run_async(
            self.profile_service.get_profile,
            on_success=self._profile_loaded,
            on_error=self._profile_error,
        )

    def _profile_loaded(self, profile: UserProfile) -> None:
        profile = self._merge_profile_with_cache(profile)
        self.user_profile = profile
        self.profile_page.set_profile(profile)
        self._save_profile_cache(profile)
        self._apply_home_permissions(profile)
        self._set_status_state("profilo caricato")

    def _profile_error(self, message: str) -> None:
        cached = self._load_profile_cache()
        if cached:
            self.user_profile = cached
            self.profile_page.set_profile(cached)
            self._apply_home_permissions(cached)
            self.profile_page.show_error(
                f"{message} | visualizzati dati profilo salvati localmente."
            )
            self._set_status_state("errore caricamento profilo")
            return
        if self.session:
            fallback_profile = UserProfile(
                email=self.session.username,
                role=self.session.role,
                phone="",
                whatsapp="",
                telegram="",
                company="",
                name="",
                has_2fa=False,
                permissions=UserPermissions(),
            )
            self.user_profile = fallback_profile
            self.profile_page.set_profile(fallback_profile)
            self._apply_home_permissions(fallback_profile)
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
        profile = self._merge_profile_with_cache(profile)
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
        self.user_profile = profile
        self._save_profile_cache(profile)
        self.profile_page.set_profile(profile)
        self._apply_home_permissions(profile)
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
        typed_plants = [p for p in plants if isinstance(p, Plant)]
        self._plants_cache = typed_plants
        self.plants_page.set_plants(plants)
        self.board_config_page.set_plants(typed_plants, selected_plant_id=self.board_config_page.selected_plant_id())
        self._set_status_state(f"impianti caricati ({len(plants)})")
        if not plants:
            self._board_config_detail = None
            self._set_status_fw("-")
            self.plants_page.clear_detail()
            self.board_config_page.show_message("Nessun impianto disponibile.")

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

    def _load_board_config_detail(self, plant_id: int) -> None:
        if plant_id <= 0:
            return
        self._board_config_detail = None
        self.board_config_page.show_message("Caricamento dettaglio impianto...")
        self._run_async(
            lambda: self.plants_service.get_plant_detail(plant_id),
            on_success=self._board_config_detail_loaded,
            on_error=lambda message: self.board_config_page.show_message(message),
        )

    def _board_config_detail_loaded(self, detail: PlantDetail) -> None:
        self._board_config_detail = detail
        self.board_config_page.set_plant_detail(detail)
        self._set_status_info(f"Impianto selezionato: #{detail.plant.plant_id}")

    def _read_connected_board(self) -> None:
        if not self._selected_port:
            self.board_config_page.show_message("Seleziona prima una porta COM.")
            return
        plant_id = self.board_config_page.selected_plant_id()
        if plant_id <= 0:
            self.board_config_page.show_message("Seleziona un impianto.")
            return
        detail = self._selected_board_detail()
        if detail is None:
            self._load_board_config_detail(plant_id)
            self.board_config_page.show_message("Attendi il caricamento del dettaglio impianto selezionato.")
            return

        self.board_config_page.set_busy(True)
        self._set_status_state(f"lettura scheda su {self._selected_port}")
        self._run_async(
            lambda: self.board_config_service.read_board(self._selected_port),
            on_success=self._board_snapshot_loaded,
            on_error=self._board_read_error,
            on_finished=lambda: self.board_config_page.set_busy(False),
        )

    def _board_snapshot_loaded(self, snapshot: BoardSnapshot) -> None:
        self._current_board_snapshot = snapshot
        warnings: list[str] = []
        detail = self._selected_board_detail()
        if detail is not None:
            try:
                warnings = self.board_config_service.validate_selection(detail, snapshot)
            except Exception as exc:
                warnings = [str(exc)]
        self.board_config_page.set_read_result(snapshot, warnings)
        self._set_status_fw(snapshot.firmware_version or "-")
        self._set_status_info(f"Scheda letta: {snapshot.board_label}")

    def _board_read_error(self, message: str) -> None:
        self.board_config_page.show_message(message)
        self._set_status_state("errore lettura scheda")

    def _save_board_configuration(self, payload: dict[str, Any]) -> None:
        snapshot = self._current_board_snapshot
        detail = self._selected_board_detail()
        if snapshot is None:
            self.board_config_page.show_message("Leggi prima la scheda collegata.")
            return
        if detail is None:
            self.board_config_page.show_message("Dettaglio impianto non disponibile o non ancora caricato.")
            return

        try:
            self.board_config_service.validate_selection(detail, snapshot, payload)
        except Exception as exc:
            self.board_config_page.show_message(str(exc))
            return

        self.board_config_page.set_busy(True)
        self._set_status_state("salvataggio configurazione scheda")
        self._run_async(
            lambda: self._apply_board_configuration(detail, snapshot, payload),
            on_success=lambda result: self._board_configuration_saved(detail.plant.plant_id, result),
            on_error=self._board_configuration_error,
            on_finished=lambda: self.board_config_page.set_busy(False),
        )

    def _apply_board_configuration(
        self,
        detail: PlantDetail,
        snapshot: BoardSnapshot,
        payload: dict[str, Any],
    ) -> dict[str, Any]:
        serial_number = str(payload.get("serial_number", snapshot.serial_number)).strip()
        sync_message = ""

        if snapshot.is_master:
            check = self.serial_provisioning_service.check_serial(
                serial_number,
                snapshot.product_type_code or "02",
            )
            if not bool(check.get("exists", False)):
                raise ValueError("Il seriale della centralina non risulta presente nel database portale.")

        operation_log = self.board_config_service.apply_configuration(self._selected_port, snapshot, payload)

        if snapshot.is_master:
            assign = self.serial_provisioning_service.assign_serial_to_master(serial_number, detail.plant.plant_id)
            sync_message = str(assign.get("message", "Centralina assegnata al master selezionato.")).strip()

        return {
            "operation_log": operation_log,
            "sync_message": sync_message,
            "serial_number": serial_number,
        }

    def _board_configuration_saved(self, plant_id: int, result: dict[str, Any]) -> None:
        sync_message = str(result.get("sync_message", "")).strip()
        serial_number = str(result.get("serial_number", "")).strip()
        message = "Configurazione scheda completata."
        if serial_number:
            message += f" Seriale: {serial_number}."
        if sync_message:
            message += f" {sync_message}"
        self.board_config_page.show_message(message)
        self._set_status_state("configurazione scheda salvata")
        self._load_plants()
        self._load_board_config_detail(plant_id)

    def _board_configuration_error(self, message: str) -> None:
        self.board_config_page.show_message(message)
        self._set_status_state("errore configurazione scheda")

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

    def _apply_home_permissions(self, profile: UserProfile | None) -> None:
        can_firmware = False
        can_create_plant = False
        if profile is not None:
            can_firmware = bool(profile.permissions.firmware_update)
            can_create_plant = bool(profile.permissions.plant_create)
        self.home_page.set_action_visible("firmware_upload", can_firmware)
        self.home_page.set_action_visible("new_plant", can_create_plant)

    def _can_access_action(self, action_key: str) -> bool:
        profile = self.user_profile
        if profile is None:
            return False
        if action_key == "firmware_upload":
            return bool(profile.permissions.firmware_update)
        if action_key == "new_plant":
            return bool(profile.permissions.plant_create)
        return True

    def _selected_board_detail(self) -> PlantDetail | None:
        detail = self._board_config_detail
        selected_plant_id = self.board_config_page.selected_plant_id()
        if detail is None or selected_plant_id <= 0:
            return None
        if int(detail.plant.plant_id) != int(selected_plant_id):
            return None
        return detail

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
            "permissions": {
                "firmware_update": profile.permissions.firmware_update,
                "plant_create": profile.permissions.plant_create,
                "serial_lifecycle": profile.permissions.serial_lifecycle,
                "serial_reserve": profile.permissions.serial_reserve,
                "manual_peripheral": profile.permissions.manual_peripheral,
            },
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
        if (
            not profile.permissions.firmware_update
            and not profile.permissions.plant_create
            and not profile.permissions.serial_lifecycle
            and not profile.permissions.serial_reserve
            and not profile.permissions.manual_peripheral
        ):
            profile.permissions = cached.permissions
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
