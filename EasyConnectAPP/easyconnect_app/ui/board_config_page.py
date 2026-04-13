from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QSpinBox,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from easyconnect_app.models import BoardSnapshot, Plant, PlantDetail


class BoardConfigPage(QWidget):
    back_requested = Signal()
    refresh_plants_requested = Signal()
    plant_selected = Signal(int)
    read_requested = Signal()
    save_requested = Signal(dict)

    def __init__(self) -> None:
        super().__init__()
        self._plants_by_index: list[int] = []
        self._current_snapshot: BoardSnapshot | None = None
        self._current_detail: PlantDetail | None = None

        root = QVBoxLayout(self)
        root.setContentsMargins(18, 18, 18, 18)
        root.setSpacing(12)

        top = QHBoxLayout()
        self.back_btn = QPushButton("Torna alla Home")
        self.back_btn.clicked.connect(self.back_requested.emit)
        self.title = QLabel("Configurazione Scheda")
        self.title.setObjectName("PlantsTitle")
        self.status = QLabel("")
        self.status.setObjectName("PlantsStatus")
        top.addWidget(self.back_btn)
        top.addSpacing(10)
        top.addWidget(self.title)
        top.addStretch(1)
        top.addWidget(self.status)
        root.addLayout(top)

        selector_box = QGroupBox("Contesto")
        selector_form = QFormLayout(selector_box)
        selector_row = QHBoxLayout()
        self.plant_combo = QComboBox()
        self.plant_combo.currentIndexChanged.connect(self._on_plant_changed)
        self.refresh_btn = QPushButton("Aggiorna Impianti")
        self.refresh_btn.clicked.connect(self.refresh_plants_requested.emit)
        selector_row.addWidget(self.plant_combo, 1)
        selector_row.addWidget(self.refresh_btn)
        self.master_hint = QLabel("-")
        self.master_hint.setWordWrap(True)
        self.warning_label = QLabel("")
        self.warning_label.setObjectName("PlantsStatus")
        self.warning_label.setWordWrap(True)
        selector_form.addRow("Impianto:", selector_row)
        selector_form.addRow("Centralina impianto:", self.master_hint)
        selector_form.addRow("Avvisi:", self.warning_label)
        root.addWidget(selector_box)

        action_row = QHBoxLayout()
        self.read_btn = QPushButton("Leggi Scheda da COM")
        self.read_btn.clicked.connect(self.read_requested.emit)
        self.save_btn = QPushButton("Applica Configurazione")
        self.save_btn.clicked.connect(self._emit_save)
        action_row.addWidget(self.read_btn)
        action_row.addWidget(self.save_btn)
        action_row.addStretch(1)
        root.addLayout(action_row)

        summary_box = QGroupBox("Scheda collegata")
        summary_form = QFormLayout(summary_box)
        self.board_label = QLabel("-")
        self.product_type = QLabel("-")
        self.firmware_version = QLabel("-")
        summary_form.addRow("Tipo scheda:", self.board_label)
        summary_form.addRow("Tipo prodotto:", self.product_type)
        summary_form.addRow("FW:", self.firmware_version)
        root.addWidget(summary_box)

        self.config_box = QGroupBox("Parametri configurabili")
        config_form = QFormLayout(self.config_box)
        self.serial_edit = QLineEdit()
        self.rs485_ip_spin = QSpinBox()
        self.rs485_ip_spin.setRange(0, 255)
        self.group_spin = QSpinBox()
        self.group_spin.setRange(0, 255)
        self.mode_combo = QComboBox()
        self.api_url_edit = QLineEdit()
        self.api_key_edit = QLineEdit()
        self.feedback_enabled_chk = QCheckBox("Feedback abilitato")
        self.feedback_logic_combo = QComboBox()
        self.feedback_logic_combo.addItem("0 - atteso HIGH", 0)
        self.feedback_logic_combo.addItem("1 - atteso LOW", 1)
        self.feedback_delay_spin = QSpinBox()
        self.feedback_delay_spin.setRange(0, 3600)
        self.feedback_attempts_spin = QSpinBox()
        self.feedback_attempts_spin.setRange(0, 20)
        self.feedback_message_edit = QLineEdit()
        self.safety_message_edit = QLineEdit()

        config_form.addRow("Seriale:", self.serial_edit)
        config_form.addRow("IP RS485:", self.rs485_ip_spin)
        config_form.addRow("Gruppo:", self.group_spin)
        config_form.addRow("Modalita:", self.mode_combo)
        config_form.addRow("API Url:", self.api_url_edit)
        config_form.addRow("API Key:", self.api_key_edit)
        config_form.addRow("", self.feedback_enabled_chk)
        config_form.addRow("FB Logic:", self.feedback_logic_combo)
        config_form.addRow("FB Delay (s):", self.feedback_delay_spin)
        config_form.addRow("FB Tentativi:", self.feedback_attempts_spin)
        config_form.addRow("Messaggio FB:", self.feedback_message_edit)
        config_form.addRow("Messaggio Safety:", self.safety_message_edit)
        root.addWidget(self.config_box)

        raw_box = QGroupBox("INFO seriale")
        raw_layout = QVBoxLayout(raw_box)
        self.raw_info = QTextEdit()
        self.raw_info.setReadOnly(True)
        self.raw_info.setMinimumHeight(220)
        raw_layout.addWidget(self.raw_info)
        root.addWidget(raw_box, 1)

        self._set_snapshot(None)

    def set_busy(self, busy: bool) -> None:
        self.back_btn.setEnabled(not busy)
        self.refresh_btn.setEnabled(not busy)
        self.plant_combo.setEnabled(not busy)
        self.read_btn.setEnabled(not busy)
        self.save_btn.setEnabled(not busy and self._current_snapshot is not None)
        if busy:
            self.status.setText("Operazione in corso...")

    def set_plants(self, plants: list[Plant], selected_plant_id: int = 0) -> None:
        self._plants_by_index = []
        self.plant_combo.blockSignals(True)
        self.plant_combo.clear()
        for plant in plants:
            label = f"{plant.name} | #{plant.plant_id} | {plant.serial_number or '-'}"
            self.plant_combo.addItem(label)
            self._plants_by_index.append(int(plant.plant_id))
        self.plant_combo.blockSignals(False)

        if not self._plants_by_index:
            self.master_hint.setText("Nessun impianto disponibile")
            self.warning_label.setText("")
            return

        target_index = 0
        if selected_plant_id > 0 and selected_plant_id in self._plants_by_index:
            target_index = self._plants_by_index.index(selected_plant_id)
        self.plant_combo.setCurrentIndex(target_index)
        self._on_plant_changed(target_index)

    def set_plant_detail(self, detail: PlantDetail) -> None:
        self._current_detail = detail
        master_serial = (detail.plant.serial_number or "").strip()
        if master_serial:
            self.master_hint.setText(f"{master_serial} | {detail.plant.name}")
            if not self.warning_label.text().strip():
                self.warning_label.setText("")
        else:
            self.master_hint.setText("Nessuna centralina associata")
            self.warning_label.setText(
                "Per questo impianto va configurata prima una centralina."
            )

    def set_read_result(self, snapshot: BoardSnapshot, warnings: list[str]) -> None:
        self._set_snapshot(snapshot)
        self.warning_label.setText("\n".join(warnings))
        self.status.setText("Scheda letta correttamente")

    def show_message(self, message: str) -> None:
        self.status.setText(message)

    def selected_plant_id(self) -> int:
        index = self.plant_combo.currentIndex()
        if index < 0 or index >= len(self._plants_by_index):
            return 0
        return self._plants_by_index[index]

    def _on_plant_changed(self, index: int) -> None:
        if index < 0 or index >= len(self._plants_by_index):
            return
        self.plant_selected.emit(self._plants_by_index[index])

    def _set_snapshot(self, snapshot: BoardSnapshot | None) -> None:
        self._current_snapshot = snapshot
        if snapshot is None:
            self.board_label.setText("-")
            self.product_type.setText("-")
            self.firmware_version.setText("-")
            self.serial_edit.clear()
            self.rs485_ip_spin.setValue(0)
            self.group_spin.setValue(0)
            self.mode_combo.clear()
            self.api_url_edit.clear()
            self.api_key_edit.clear()
            self.feedback_enabled_chk.setChecked(False)
            self.feedback_logic_combo.setCurrentIndex(0)
            self.feedback_delay_spin.setValue(0)
            self.feedback_attempts_spin.setValue(0)
            self.feedback_message_edit.clear()
            self.safety_message_edit.clear()
            self.raw_info.clear()
            self._apply_visibility("unknown")
            self.save_btn.setEnabled(False)
            return

        self.board_label.setText(snapshot.board_label or "-")
        self.product_type.setText(snapshot.product_type_code or "-")
        self.firmware_version.setText(snapshot.firmware_version or "-")
        self.serial_edit.setText(snapshot.serial_number or "")
        self.rs485_ip_spin.setValue(snapshot.rs485_ip or 0)
        self.group_spin.setValue(snapshot.group or 0)
        self.api_url_edit.setText(snapshot.api_url or "")
        self.api_key_edit.clear()
        self.feedback_enabled_chk.setChecked(bool(snapshot.feedback_enabled))
        self.feedback_delay_spin.setValue(snapshot.feedback_delay_sec or 0)
        self.feedback_attempts_spin.setValue(snapshot.feedback_attempts or 0)
        self.feedback_message_edit.setText(snapshot.feedback_message or "")
        self.safety_message_edit.setText(snapshot.safety_message or "")
        if snapshot.feedback_logic == 1:
            self.feedback_logic_combo.setCurrentIndex(1)
        else:
            self.feedback_logic_combo.setCurrentIndex(0)
        self.raw_info.setPlainText(snapshot.raw_info or "")
        self._set_mode_options(snapshot)
        self._apply_visibility(snapshot.board_kind)
        self.save_btn.setEnabled(True)

    def _set_mode_options(self, snapshot: BoardSnapshot) -> None:
        self.mode_combo.clear()
        options: list[tuple[int, str]] = []
        if snapshot.board_kind == "master":
            options = [(1, "Standalone"), (2, "Rewamping")]
        elif snapshot.board_kind == "relay":
            options = [
                (1, "LUCE"),
                (2, "UVC"),
                (3, "ELETTROSTATICO"),
                (4, "GAS"),
                (5, "COMANDO"),
            ]
        elif snapshot.board_kind == "pressure":
            options = [(1, "Temp/Hum"), (2, "Pressure"), (3, "Tutto")]

        for code, label in options:
            self.mode_combo.addItem(label, code)

        if snapshot.mode_code is not None:
            idx = self.mode_combo.findData(snapshot.mode_code)
            if idx >= 0:
                self.mode_combo.setCurrentIndex(idx)

    def _apply_visibility(self, board_kind: str) -> None:
        is_master = board_kind == "master"
        is_relay = board_kind == "relay"
        is_pressure = board_kind == "pressure"

        self.rs485_ip_spin.setVisible(is_relay or is_pressure)
        self.group_spin.setVisible(is_relay or is_pressure)
        self.mode_combo.setVisible(is_master or is_relay or is_pressure)
        self.api_url_edit.setVisible(is_master)
        self.api_key_edit.setVisible(is_master)
        self.feedback_enabled_chk.setVisible(is_relay)
        self.feedback_logic_combo.setVisible(is_relay)
        self.feedback_delay_spin.setVisible(is_relay)
        self.feedback_attempts_spin.setVisible(is_relay)
        self.feedback_message_edit.setVisible(is_relay)
        self.safety_message_edit.setVisible(is_relay)

        form = self.config_box.layout()
        if isinstance(form, QFormLayout):
            label_visibility = [
                (self.rs485_ip_spin, is_relay or is_pressure),
                (self.group_spin, is_relay or is_pressure),
                (self.mode_combo, is_master or is_relay or is_pressure),
                (self.api_url_edit, is_master),
                (self.api_key_edit, is_master),
                (self.feedback_logic_combo, is_relay),
                (self.feedback_delay_spin, is_relay),
                (self.feedback_attempts_spin, is_relay),
                (self.feedback_message_edit, is_relay),
                (self.safety_message_edit, is_relay),
            ]
            for field, visible in label_visibility:
                label = form.labelForField(field)
                if label is not None:
                    label.setVisible(visible)

    def _emit_save(self) -> None:
        payload = {
            "plant_id": self.selected_plant_id(),
            "serial_number": self.serial_edit.text().strip(),
            "rs485_ip": self.rs485_ip_spin.value(),
            "group": self.group_spin.value(),
            "mode_code": self.mode_combo.currentData(),
            "api_url": self.api_url_edit.text().strip(),
            "api_key": self.api_key_edit.text().strip(),
            "feedback_enabled": self.feedback_enabled_chk.isChecked(),
            "feedback_logic": self.feedback_logic_combo.currentData(),
            "feedback_delay_sec": self.feedback_delay_spin.value(),
            "feedback_attempts": self.feedback_attempts_spin.value(),
            "feedback_message": self.feedback_message_edit.text().strip(),
            "safety_message": self.safety_message_edit.text().strip(),
        }
        self.save_requested.emit(payload)
