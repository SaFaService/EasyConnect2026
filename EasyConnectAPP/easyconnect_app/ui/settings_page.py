from __future__ import annotations

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QCheckBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)


class SettingsPage(QWidget):
    back_requested = Signal()
    save_requested = Signal(dict)

    def __init__(self) -> None:
        super().__init__()
        self._endpoint_fields: dict[str, QLineEdit] = {}
        self._is_admin = False

        root = QVBoxLayout(self)
        root.setContentsMargins(18, 18, 18, 18)
        root.setSpacing(12)

        top = QHBoxLayout()
        self.back_btn = QPushButton("Torna alla Home")
        self.back_btn.clicked.connect(self.back_requested.emit)
        title = QLabel("Impostazioni Software")
        title.setObjectName("PlantsTitle")
        self.status = QLabel("")
        self.status.setObjectName("PlantsStatus")
        top.addWidget(self.back_btn)
        top.addSpacing(10)
        top.addWidget(title)
        top.addStretch(1)
        top.addWidget(self.status)
        root.addLayout(top)

        general = QGroupBox("Generali")
        gform = QFormLayout(general)
        self.app_version = QLabel("-")
        self.base_url = QLineEdit()
        self.timeout_seconds = QSpinBox()
        self.timeout_seconds.setRange(3, 180)
        self.verify_ssl = QCheckBox("Verifica certificato SSL")
        self.mock_mode = QCheckBox("Modalita mock")
        gform.addRow("Versione app:", self.app_version)
        gform.addRow("Base URL:", self.base_url)
        gform.addRow("Timeout (s):", self.timeout_seconds)
        gform.addRow("", self.verify_ssl)
        gform.addRow("", self.mock_mode)
        root.addWidget(general)

        endpoints = QGroupBox("Endpoint API")
        eform = QFormLayout(endpoints)
        ordered_keys = [
            "auth",
            "plants",
            "plant_detail",
            "plant_update",
            "serial",
            "serial_overview",
            "serial_detail",
            "create_plant",
            "profile_api",
            "forgot_password",
            "version_check",
        ]
        for key in ordered_keys:
            field = QLineEdit()
            self._endpoint_fields[key] = field
            eform.addRow(f"{key}:", field)
        root.addWidget(endpoints, 1)

        actions = QHBoxLayout()
        actions.addStretch(1)
        self.save_btn = QPushButton("Salva Impostazioni")
        self.save_btn.clicked.connect(self._emit_save)
        actions.addWidget(self.save_btn)
        root.addLayout(actions)

    def load_values(self, *, app_version: str, config: dict, is_admin: bool) -> None:
        self._is_admin = is_admin
        self.app_version.setText(app_version)
        self.base_url.setText(str(config.get("base_url", "")))
        self.timeout_seconds.setValue(int(config.get("timeout_seconds", 20)))
        self.verify_ssl.setChecked(bool(config.get("verify_ssl", True)))
        self.mock_mode.setChecked(bool(config.get("mock_mode", False)))
        endpoints = config.get("endpoints", {})
        for key, field in self._endpoint_fields.items():
            field.setText(str(endpoints.get(key, "")))

        self._set_editable(is_admin)
        if is_admin:
            self.status.setText("Modifica consentita (admin)")
        else:
            self.status.setText("Solo lettura: serve ruolo admin")

    def _set_editable(self, editable: bool) -> None:
        self.base_url.setReadOnly(not editable)
        self.timeout_seconds.setEnabled(editable)
        self.verify_ssl.setEnabled(editable)
        self.mock_mode.setEnabled(editable)
        for field in self._endpoint_fields.values():
            field.setReadOnly(not editable)
        self.save_btn.setEnabled(editable)

    def set_busy(self, busy: bool) -> None:
        self.back_btn.setEnabled(not busy)
        self.save_btn.setEnabled((not busy) and self._is_admin)
        if busy:
            self.status.setText("Salvataggio impostazioni...")

    def show_message(self, message: str) -> None:
        self.status.setText(message)

    def _emit_save(self) -> None:
        payload = {
            "base_url": self.base_url.text().strip(),
            "timeout_seconds": int(self.timeout_seconds.value()),
            "verify_ssl": bool(self.verify_ssl.isChecked()),
            "mock_mode": bool(self.mock_mode.isChecked()),
            "endpoints": {k: v.text().strip() for k, v in self._endpoint_fields.items()},
        }
        self.save_requested.emit(payload)

