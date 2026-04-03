from __future__ import annotations

import json
from pathlib import Path

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QColor, QIcon, QPixmap
from PySide6.QtWidgets import (
    QAbstractItemView,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QPushButton,
    QSplitter,
    QStackedWidget,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from easyconnect_app.models import PartyInfo, PeripheralInfo, Plant, PlantDetail


class PlantsPage(QWidget):
    refresh_requested = Signal()
    back_requested = Signal()
    plant_selected = Signal(int)
    detail_back_requested = Signal()
    serial_requested = Signal(str)
    plant_update_requested = Signal(int, str, str)

    def __init__(self) -> None:
        super().__init__()
        self._plant_ids: list[int] = []
        self._icon_rules = self._load_icon_map()
        self._current_plant_id = 0
        self._can_edit_current = False
        self._suppress_next_selection = False

        root = QVBoxLayout(self)
        root.setContentsMargins(18, 18, 18, 18)
        root.setSpacing(12)

        top = QHBoxLayout()
        self.back_btn = QPushButton("Torna alla Home")
        self.back_btn.clicked.connect(self.back_requested.emit)
        self.title = QLabel("Impianti")
        self.title.setObjectName("PlantsTitle")
        self.status = QLabel("")
        self.status.setObjectName("PlantsStatus")
        self.refresh_btn = QPushButton("Aggiorna")
        self.refresh_btn.clicked.connect(self.refresh_requested.emit)
        top.addWidget(self.back_btn)
        top.addSpacing(10)
        top.addWidget(self.title)
        top.addStretch(1)
        top.addWidget(self.status)
        top.addWidget(self.refresh_btn)
        root.addLayout(top)

        self.content = QStackedWidget()
        self.content.addWidget(self._build_list_page())
        self.content.addWidget(self._build_detail_page())
        self.content.setCurrentIndex(0)
        root.addWidget(self.content, 1)

    def _build_list_page(self) -> QWidget:
        page = QWidget()
        v = QVBoxLayout(page)
        v.setContentsMargins(0, 0, 0, 0)

        self.table = QTableWidget(0, 8)
        self.table.setObjectName("PlantsTable")
        self.table.setHorizontalHeaderLabels(
            [
                "Stato",
                "Impianto",
                "Indirizzo",
                "Seriale",
                "FW",
                "Segnale",
                "Ultimo Aggiornamento",
                "ID",
            ]
        )
        self.table.horizontalHeader().setStretchLastSection(False)
        self.table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.table.setSelectionMode(QAbstractItemView.SingleSelection)
        self.table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.table.verticalHeader().setVisible(False)
        self.table.setColumnWidth(0, 70)
        self.table.setColumnWidth(1, 280)
        self.table.setColumnWidth(2, 360)
        self.table.setColumnWidth(3, 180)
        self.table.setColumnWidth(4, 95)
        self.table.setColumnWidth(5, 90)
        self.table.setColumnWidth(6, 190)
        self.table.setColumnWidth(7, 80)
        self.table.itemSelectionChanged.connect(self._on_selection_changed)
        self.table.cellClicked.connect(self._on_list_cell_clicked)

        v.addWidget(self.table, 1)
        return page

    def _build_detail_page(self) -> QWidget:
        page = QWidget()
        v = QVBoxLayout(page)
        v.setContentsMargins(0, 0, 0, 0)
        v.setSpacing(8)

        top = QHBoxLayout()
        self.back_to_list_btn = QPushButton("Indietro alla Lista")
        self.back_to_list_btn.clicked.connect(self._go_back_to_list)
        self.detail_header = QLabel("Dettaglio Impianto")
        self.detail_header.setObjectName("PlantsTitle")
        top.addWidget(self.back_to_list_btn)
        top.addSpacing(10)
        top.addWidget(self.detail_header)
        top.addStretch(1)
        v.addLayout(top)

        self.detail_splitter = QSplitter(Qt.Vertical)
        self.detail_splitter.setChildrenCollapsible(False)

        top_cards = QWidget()
        cards_layout = QVBoxLayout(top_cards)
        cards_layout.setContentsMargins(0, 0, 0, 0)
        cards_layout.setSpacing(10)

        self.master_box = QGroupBox("Scheda Master")
        mform = QFormLayout(self.master_box)
        self.master_name_edit = QLineEdit()
        self.master_address_edit = QLineEdit()
        self.master_serial_btn = QPushButton("-")
        self.master_serial_btn.setObjectName("SerialLinkBtn")
        self.master_serial_btn.setFlat(True)
        self.master_serial_btn.setCursor(Qt.PointingHandCursor)
        self.master_serial_btn.clicked.connect(self._on_master_serial_clicked)
        self.master_fw = self._value_label()
        self.master_rssi = self._value_label()
        self.master_seen = self._value_label()
        self.master_status = self._value_label()
        self.master_name_edit.setPlaceholderText("Nome impianto")
        self.master_address_edit.setPlaceholderText("Indirizzo impianto")
        mform.addRow("Nome:", self.master_name_edit)
        mform.addRow("Indirizzo:", self.master_address_edit)
        mform.addRow("Seriale:", self.master_serial_btn)
        mform.addRow("Firmware:", self.master_fw)
        mform.addRow("RSSI:", self.master_rssi)
        mform.addRow("Ultimo update:", self.master_seen)
        mform.addRow("Stato:", self.master_status)

        save_row = QHBoxLayout()
        save_row.addStretch(1)
        self.save_plant_btn = QPushButton("Salva Impianto")
        self.save_plant_btn.clicked.connect(self._emit_plant_update)
        save_row.addWidget(self.save_plant_btn)
        mform.addRow("", save_row)

        self.contacts_box = QGroupBox("Anagrafiche")
        cform = QFormLayout(self.contacts_box)
        self.owner_label = self._value_label()
        self.maintainer_label = self._value_label()
        self.builder_label = self._value_label()
        self.creator_label = self._value_label()
        cform.addRow("Cliente:", self.owner_label)
        cform.addRow("Manutentore:", self.maintainer_label)
        cform.addRow("Costruttore:", self.builder_label)
        cform.addRow("Creatore:", self.creator_label)

        cards_layout.addWidget(self.master_box)
        cards_layout.addWidget(self.contacts_box)
        cards_layout.addStretch(1)

        self.periph_box = QGroupBox("Periferiche collegate (clic sul seriale)")
        pv = QVBoxLayout(self.periph_box)
        self.peripherals_table = QTableWidget(0, 11)
        self.peripherals_table.setHorizontalHeaderLabels(
            [
                "",
                "Tipo",
                "Seriale",
                "Gruppo",
                "Modo",
                "FW",
                "P",
                "T",
                "UR",
                "Ultimo update",
                "Stato",
            ]
        )
        self.peripherals_table.verticalHeader().setVisible(False)
        self.peripherals_table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self.peripherals_table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.peripherals_table.setSelectionMode(QAbstractItemView.SingleSelection)
        self.peripherals_table.setMinimumHeight(320)
        header = self.peripherals_table.horizontalHeader()
        header.setSectionResizeMode(QHeaderView.Interactive)
        header.setStretchLastSection(True)
        self.peripherals_table.setColumnWidth(0, 44)
        self.peripherals_table.setColumnWidth(1, 190)
        self.peripherals_table.setColumnWidth(2, 180)
        self.peripherals_table.setColumnWidth(3, 70)
        self.peripherals_table.setColumnWidth(4, 120)
        self.peripherals_table.setColumnWidth(5, 90)
        self.peripherals_table.setColumnWidth(6, 80)
        self.peripherals_table.setColumnWidth(7, 80)
        self.peripherals_table.setColumnWidth(8, 80)
        self.peripherals_table.setColumnWidth(9, 170)
        self.peripherals_table.setColumnWidth(10, 100)
        self.peripherals_table.cellClicked.connect(self._on_peripheral_cell_clicked)
        pv.addWidget(self.peripherals_table, 1)

        self.detail_splitter.addWidget(top_cards)
        self.detail_splitter.addWidget(self.periph_box)
        self.detail_splitter.setSizes([320, 520])

        v.addWidget(self.detail_splitter, 1)
        return page

    def _value_label(self) -> QLabel:
        lbl = QLabel("-")
        lbl.setWordWrap(True)
        lbl.setTextInteractionFlags(Qt.TextSelectableByMouse)
        return lbl

    def set_busy(self, busy: bool) -> None:
        self.refresh_btn.setEnabled(not busy)
        self.table.setEnabled(not busy)
        self.back_to_list_btn.setEnabled(not busy)
        self.save_plant_btn.setEnabled(not busy and self._can_edit_current)
        self.master_name_edit.setEnabled((not busy) and self._can_edit_current)
        self.master_address_edit.setEnabled((not busy) and self._can_edit_current)
        if busy:
            self.status.setText("Caricamento...")

    def set_plants(self, plants: list[Plant]) -> None:
        self.status.setText(f"{len(plants)} impianti trovati")
        self._plant_ids = [int(p.plant_id) for p in plants]
        self.table.clearSelection()
        self.table.setRowCount(len(plants))
        for row, plant in enumerate(plants):
            status_text = "ONLINE" if plant.online else "OFFLINE"
            status_item = QTableWidgetItem(f"\u25cf {status_text}")
            status_item.setForeground(QColor("#1f8e42") if plant.online else QColor("#b23a3a"))
            status_item.setTextAlignment(Qt.AlignCenter)
            self.table.setItem(row, 0, status_item)
            self.table.setItem(row, 1, QTableWidgetItem(plant.name))
            self.table.setItem(row, 2, QTableWidgetItem(plant.address))
            serial_item = QTableWidgetItem(plant.serial_number)
            serial_item.setForeground(QColor("#14548a"))
            self.table.setItem(row, 3, serial_item)
            self.table.setItem(row, 4, QTableWidgetItem(plant.firmware_version))
            self.table.setItem(row, 5, QTableWidgetItem(plant.rssi))
            self.table.setItem(row, 6, QTableWidgetItem(plant.updated_at))
            self.table.setItem(row, 7, QTableWidgetItem(str(plant.plant_id)))

        self._current_plant_id = 0
        self.content.setCurrentIndex(0)
        self.clear_detail()

    def _on_selection_changed(self) -> None:
        if self._suppress_next_selection:
            self._suppress_next_selection = False
            return
        row = self.table.currentRow()
        if row < 0 or row >= len(self._plant_ids):
            return
        self.plant_selected.emit(self._plant_ids[row])

    def _on_list_cell_clicked(self, row: int, column: int) -> None:
        if column != 3:
            return
        self._suppress_next_selection = True
        item = self.table.item(row, 3)
        serial = item.text().strip() if item else ""
        if serial:
            self.serial_requested.emit(serial)

    def set_detail(self, detail: PlantDetail) -> None:
        self._current_plant_id = int(detail.plant.plant_id)
        self._can_edit_current = bool(detail.can_edit)
        plant_name = detail.plant.name.strip() or f"#{detail.plant.plant_id}"
        self.detail_header.setText(f"Dettaglio Impianto: {plant_name}")

        self.master_name_edit.setText(detail.plant.name or "")
        self.master_address_edit.setText(detail.plant.address or "")
        self.master_serial_btn.setText(detail.plant.serial_number or "-")
        self.master_fw.setText(detail.plant.firmware_version or "-")
        self.master_rssi.setText(detail.plant.rssi or "-")
        self.master_seen.setText(detail.plant.updated_at or "-")
        self.master_status.setText("ONLINE" if detail.plant.online else "OFFLINE")

        self.master_name_edit.setReadOnly(not self._can_edit_current)
        self.master_address_edit.setReadOnly(not self._can_edit_current)
        self.master_name_edit.setEnabled(self._can_edit_current)
        self.master_address_edit.setEnabled(self._can_edit_current)
        self.save_plant_btn.setVisible(self._can_edit_current)
        self.save_plant_btn.setEnabled(self._can_edit_current)

        self.owner_label.setText(self._party_text(detail.owner))
        self.maintainer_label.setText(self._party_text(detail.maintainer))
        self.builder_label.setText(self._party_text(detail.builder))
        self.creator_label.setText(self._party_text(detail.creator))
        self._set_peripherals(detail.peripherals)
        self.content.setCurrentIndex(1)

    def clear_detail(self) -> None:
        self._can_edit_current = False
        self.master_name_edit.clear()
        self.master_address_edit.clear()
        self.master_name_edit.setReadOnly(True)
        self.master_address_edit.setReadOnly(True)
        self.master_name_edit.setEnabled(False)
        self.master_address_edit.setEnabled(False)
        self.save_plant_btn.setVisible(False)
        self.master_serial_btn.setText("-")
        for label in (
            self.master_fw,
            self.master_rssi,
            self.master_seen,
            self.master_status,
            self.owner_label,
            self.maintainer_label,
            self.builder_label,
            self.creator_label,
        ):
            label.setText("-")
        self.peripherals_table.setRowCount(0)

    def _go_back_to_list(self) -> None:
        self.content.setCurrentIndex(0)
        self.detail_back_requested.emit()

    def _emit_plant_update(self) -> None:
        if self._current_plant_id <= 0:
            return
        self.plant_update_requested.emit(
            self._current_plant_id,
            self.master_name_edit.text().strip(),
            self.master_address_edit.text().strip(),
        )

    def _on_master_serial_clicked(self) -> None:
        serial = self.master_serial_btn.text().strip()
        if serial and serial != "-":
            self.serial_requested.emit(serial)

    def _party_text(self, party: PartyInfo) -> str:
        parts = []
        if party.company and party.company != "-":
            parts.append(party.company)
        if party.name and party.name != "-":
            parts.append(party.name)
        if party.email and party.email != "-":
            parts.append(party.email)
        if party.phone and party.phone != "-":
            parts.append(party.phone)
        return " | ".join(parts) if parts else "-"

    def _set_peripherals(self, peripherals: list[PeripheralInfo]) -> None:
        self.peripherals_table.setRowCount(len(peripherals))
        for row, p in enumerate(peripherals):
            icon_item = QTableWidgetItem("")
            icon = self._icon_for_peripheral(p)
            if not icon.isNull():
                icon_item.setIcon(icon)
            icon_item.setTextAlignment(Qt.AlignCenter)
            self.peripherals_table.setItem(row, 0, icon_item)
            self.peripherals_table.setItem(row, 1, QTableWidgetItem(p.board_label or p.board_type))
            serial_item = QTableWidgetItem(p.serial_number)
            serial_item.setForeground(QColor("#14548a"))
            self.peripherals_table.setItem(row, 2, serial_item)
            self.peripherals_table.setItem(row, 3, QTableWidgetItem(p.group))
            self.peripherals_table.setItem(row, 4, QTableWidgetItem(p.mode))
            self.peripherals_table.setItem(row, 5, QTableWidgetItem(p.firmware_version))
            self.peripherals_table.setItem(row, 6, QTableWidgetItem(p.pressure))
            self.peripherals_table.setItem(row, 7, QTableWidgetItem(p.temperature))
            self.peripherals_table.setItem(row, 8, QTableWidgetItem(p.humidity))
            self.peripherals_table.setItem(row, 9, QTableWidgetItem(p.last_seen))
            status = QTableWidgetItem("ONLINE" if p.online else "OFFLINE")
            status.setForeground(QColor("#1f8e42") if p.online else QColor("#b23a3a"))
            self.peripherals_table.setItem(row, 10, status)

    def _on_peripheral_cell_clicked(self, row: int, column: int) -> None:
        if column != 2:
            return
        serial_item = self.peripherals_table.item(row, 2)
        serial = serial_item.text().strip() if serial_item else ""
        if serial:
            self.serial_requested.emit(serial)

    def _load_icon_map(self) -> list[tuple[list[str], Path]]:
        app_root = Path(__file__).resolve().parents[2]
        mapping_path = app_root / "assets" / "immagini" / "peripheral_icon_map.json"
        if not mapping_path.exists():
            return []
        try:
            raw = json.loads(mapping_path.read_text(encoding="utf-8"))
        except Exception:
            return []

        out: list[tuple[list[str], Path]] = []
        if not isinstance(raw, list):
            return out
        for item in raw:
            if not isinstance(item, dict):
                continue
            icon_rel = str(item.get("icon", "")).strip()
            keywords = item.get("contains", [])
            if not icon_rel or not isinstance(keywords, list):
                continue
            icon_path = app_root / "assets" / "immagini" / icon_rel
            if not icon_path.exists():
                continue
            kw = [str(k).strip().lower() for k in keywords if str(k).strip()]
            if not kw:
                continue
            out.append((kw, icon_path))
        return out

    def _icon_for_peripheral(self, peripheral: PeripheralInfo) -> QIcon:
        text = f"{peripheral.board_type} {peripheral.board_label}".lower()
        for keywords, path in self._icon_rules:
            if any(k in text for k in keywords):
                pix = QPixmap(str(path))
                if pix.isNull():
                    continue
                return QIcon(pix.scaled(34, 34, Qt.KeepAspectRatio, Qt.SmoothTransformation))
        return QIcon()

    def show_error(self, message: str) -> None:
        self.status.setText(message)
