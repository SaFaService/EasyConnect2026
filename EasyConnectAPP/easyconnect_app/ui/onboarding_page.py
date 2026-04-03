from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QComboBox,
    QFileDialog,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPlainTextEdit,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from easyconnect_app.models import SerialPortInfo


class OnboardingPage(QWidget):
    action_requested = Signal(str, dict)

    def __init__(self) -> None:
        super().__init__()
        root = QVBoxLayout(self)
        root.setSpacing(10)

        root.addWidget(self._build_connection_box())
        root.addWidget(self._build_serial_db_box())
        root.addWidget(self._build_config_box())
        root.addWidget(self._build_firmware_box())
        root.addWidget(self._build_log_box())

        self._toggle_fields()

    def _build_connection_box(self) -> QGroupBox:
        box = QGroupBox("Connessione Scheda")
        form = QFormLayout(box)

        self.board_type = QComboBox()
        self.board_type.addItem("Master", "master")
        self.board_type.addItem("Slave Pressione", "slave")
        self.board_type.currentIndexChanged.connect(self._toggle_fields)

        port_row = QHBoxLayout()
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(250)
        self.refresh_ports_btn = QPushButton("Aggiorna Porte")
        self.refresh_ports_btn.clicked.connect(
            lambda: self.action_requested.emit("refresh_ports", {})
        )
        self.read_info_btn = QPushButton("Leggi INFO")
        self.read_info_btn.clicked.connect(
            lambda: self.action_requested.emit(
                "read_info",
                {"port": self.port_combo.currentData()},
            )
        )
        port_row.addWidget(self.port_combo)
        port_row.addWidget(self.refresh_ports_btn)
        port_row.addWidget(self.read_info_btn)

        form.addRow("Tipo Scheda", self.board_type)
        form.addRow("Porta USB", port_row)
        return box

    def _build_serial_db_box(self) -> QGroupBox:
        box = QGroupBox("Provisioning Seriale / Impianto")
        form = QFormLayout(box)

        self.product_type = QComboBox()
        self.product_type.addItem("01 - Centralina Display", "01")
        self.product_type.addItem("02 - Centralina Standalone/Rewamping", "02")
        self.product_type.addItem("03 - Scheda Relay", "03")
        self.product_type.addItem("04 - Scheda Pressione", "04")
        self.product_type.addItem("05 - Scheda Motore", "05")

        reserve_row = QHBoxLayout()
        self.reserve_btn = QPushButton("Riserva Prossimo Seriale")
        self.reserve_btn.clicked.connect(
            lambda: self.action_requested.emit(
                "reserve_serial",
                {"product_type_code": self.product_type.currentData()},
            )
        )
        self.reserved_serial = QLineEdit()
        self.reserved_serial.setPlaceholderText("seriale riservato")
        reserve_row.addWidget(self.reserve_btn)
        reserve_row.addWidget(self.reserved_serial)

        check_row = QHBoxLayout()
        self.check_serial_input = QLineEdit()
        self.check_serial_input.setPlaceholderText("YYYYMMTTNNNN")
        self.check_serial_btn = QPushButton("Verifica Seriale")
        self.check_serial_btn.clicked.connect(
            lambda: self.action_requested.emit(
                "check_serial",
                {
                    "serial_number": self.check_serial_input.text().strip(),
                    "product_type_code": self.product_type.currentData(),
                },
            )
        )
        check_row.addWidget(self.check_serial_input)
        check_row.addWidget(self.check_serial_btn)

        assign_row = QHBoxLayout()
        self.master_id_input = QLineEdit()
        self.master_id_input.setPlaceholderText("master_id portale")
        self.assign_btn = QPushButton("Assegna Seriale a Master")
        self.assign_btn.clicked.connect(
            lambda: self.action_requested.emit(
                "assign_master_serial",
                {
                    "serial_number": self.serial_input.text().strip(),
                    "master_id": self.master_id_input.text().strip(),
                },
            )
        )
        assign_row.addWidget(self.master_id_input)
        assign_row.addWidget(self.assign_btn)

        plant_row = QHBoxLayout()
        self.plant_name_input = QLineEdit()
        self.plant_name_input.setPlaceholderText("Nome nuovo impianto")
        self.plant_address_input = QLineEdit()
        self.plant_address_input.setPlaceholderText("Indirizzo")
        self.create_plant_btn = QPushButton("Crea Impianto")
        self.create_plant_btn.clicked.connect(
            lambda: self.action_requested.emit(
                "create_plant",
                {
                    "name": self.plant_name_input.text().strip(),
                    "address": self.plant_address_input.text().strip(),
                    "serial_number": self.serial_input.text().strip(),
                },
            )
        )
        plant_row.addWidget(self.plant_name_input)
        plant_row.addWidget(self.plant_address_input)
        plant_row.addWidget(self.create_plant_btn)

        form.addRow("Tipo Prodotto", self.product_type)
        form.addRow("Riserva Seriale", reserve_row)
        form.addRow("Check Seriale", check_row)
        form.addRow("Assegnazione Master", assign_row)
        form.addRow("Nuovo Impianto", plant_row)
        return box

    def _build_config_box(self) -> QGroupBox:
        box = QGroupBox("Configurazione Scheda (USB Seriale)")
        form = QFormLayout(box)

        self.serial_input = QLineEdit()
        self.serial_input.setPlaceholderText("YYYYMMTTNNNN")

        self.master_mode_combo = QComboBox()
        self.master_mode_combo.addItem("1 - Standalone", 1)
        self.master_mode_combo.addItem("2 - Rewamping", 2)

        self.api_url_input = QLineEdit()
        self.api_key_input = QLineEdit()
        self.cust_url_input = QLineEdit()
        self.cust_key_input = QLineEdit()

        self.slave_ip_spin = QSpinBox()
        self.slave_ip_spin.setRange(1, 100)
        self.slave_group_spin = QSpinBox()
        self.slave_group_spin.setRange(1, 100)
        self.slave_mode_combo = QComboBox()
        self.slave_mode_combo.addItem("1 - Temp/Hum", 1)
        self.slave_mode_combo.addItem("2 - Pressione", 2)
        self.slave_mode_combo.addItem("3 - Tutto", 3)

        self.apply_config_btn = QPushButton("Applica Configurazione")
        self.apply_config_btn.clicked.connect(self._emit_apply_config)

        self.master_fields: list[QWidget] = [
            self.master_mode_combo,
            self.api_url_input,
            self.api_key_input,
            self.cust_url_input,
            self.cust_key_input,
        ]
        self.slave_fields: list[QWidget] = [
            self.slave_ip_spin,
            self.slave_group_spin,
            self.slave_mode_combo,
        ]

        form.addRow("Seriale", self.serial_input)
        form.addRow("Modo Master", self.master_mode_combo)
        form.addRow("API URL", self.api_url_input)
        form.addRow("API KEY", self.api_key_input)
        form.addRow("Customer API URL", self.cust_url_input)
        form.addRow("Customer API KEY", self.cust_key_input)
        form.addRow("IP RS485 Slave", self.slave_ip_spin)
        form.addRow("Gruppo Slave", self.slave_group_spin)
        form.addRow("Modo Slave", self.slave_mode_combo)
        form.addRow(self.apply_config_btn)
        return box

    def _build_firmware_box(self) -> QGroupBox:
        box = QGroupBox("Flash Firmware")
        form = QFormLayout(box)

        fw_row = QHBoxLayout()
        self.fw_path_input = QLineEdit()
        self.fw_path_input.setPlaceholderText("Seleziona file .bin")
        browse_btn = QPushButton("Sfoglia")
        browse_btn.clicked.connect(self._browse_firmware)
        fw_row.addWidget(self.fw_path_input)
        fw_row.addWidget(browse_btn)

        flash_row = QHBoxLayout()
        self.chip_combo = QComboBox()
        self.chip_combo.addItems(["esp32c3", "esp32", "esp32s3"])
        self.baud_combo = QComboBox()
        self.baud_combo.addItems(["115200", "230400", "460800"])
        self.baud_combo.setCurrentText("460800")
        self.flash_btn = QPushButton("Flash Firmware")
        self.flash_btn.clicked.connect(self._emit_flash)

        flash_row.addWidget(QLabel("Chip"))
        flash_row.addWidget(self.chip_combo)
        flash_row.addWidget(QLabel("Baud"))
        flash_row.addWidget(self.baud_combo)
        flash_row.addStretch(1)
        flash_row.addWidget(self.flash_btn)

        form.addRow("Firmware BIN", fw_row)
        form.addRow(flash_row)
        return box

    def _build_log_box(self) -> QGroupBox:
        box = QGroupBox("Log Operazioni")
        layout = QVBoxLayout(box)
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        layout.addWidget(self.log)
        return box

    def set_ports(self, ports: list[SerialPortInfo]) -> None:
        self.port_combo.clear()
        for port in ports:
            self.port_combo.addItem(f"{port.device} - {port.description}", port.device)

    def append_log(self, message: str) -> None:
        self.log.appendPlainText(message.rstrip())
        self.log.ensureCursorVisible()

    def set_busy(self, busy: bool) -> None:
        for btn in (
            self.refresh_ports_btn,
            self.read_info_btn,
            self.reserve_btn,
            self.check_serial_btn,
            self.assign_btn,
            self.create_plant_btn,
            self.apply_config_btn,
            self.flash_btn,
        ):
            btn.setEnabled(not busy)

    def set_reserved_serial(self, serial_number: str) -> None:
        self.reserved_serial.setText(serial_number)
        self.serial_input.setText(serial_number)

    def _toggle_fields(self) -> None:
        is_master = self.board_type.currentData() == "master"
        for widget in self.master_fields:
            widget.setVisible(is_master)
        for widget in self.slave_fields:
            widget.setVisible(not is_master)

    def _browse_firmware(self) -> None:
        start_dir = str(Path.cwd())
        path, _ = QFileDialog.getOpenFileName(
            self,
            "Seleziona firmware .bin",
            start_dir,
            "Firmware (*.bin);;Tutti i file (*.*)",
        )
        if path:
            self.fw_path_input.setText(path)

    def _emit_apply_config(self) -> None:
        payload = {
            "board_type": self.board_type.currentData(),
            "port": self.port_combo.currentData(),
            "serial_number": self.serial_input.text().strip(),
            "master_mode": self.master_mode_combo.currentData(),
            "api_url": self.api_url_input.text().strip(),
            "api_key": self.api_key_input.text().strip(),
            "customer_api_url": self.cust_url_input.text().strip(),
            "customer_api_key": self.cust_key_input.text().strip(),
            "slave_ip": self.slave_ip_spin.value(),
            "slave_group": self.slave_group_spin.value(),
            "slave_mode": self.slave_mode_combo.currentData(),
        }
        self.action_requested.emit("apply_config", payload)

    def _emit_flash(self) -> None:
        payload = {
            "port": self.port_combo.currentData(),
            "firmware_path": self.fw_path_input.text().strip(),
            "chip": self.chip_combo.currentText(),
            "baudrate": int(self.baud_combo.currentText()),
        }
        self.action_requested.emit("flash_firmware", payload)
