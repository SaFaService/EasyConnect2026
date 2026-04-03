from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re

from PySide6.QtCore import QSize, Qt, Signal
from PySide6.QtGui import QColor, QIcon, QPixmap
from PySide6.QtWidgets import (
    QGraphicsDropShadowEffect,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSizePolicy,
    QToolButton,
    QVBoxLayout,
    QWidget,
)

from easyconnect_app.ui import icons


@dataclass(slots=True)
class HomeAction:
    key: str
    title: str
    icon: QIcon


class HomeTileButton(QToolButton):
    def __init__(self, action: HomeAction) -> None:
        super().__init__()
        self.action_key = action.key
        self.setObjectName("HomeTile")
        self.setIcon(action.icon)
        self.setIconSize(QSize(110, 110))
        self.setText(action.title)
        self.setToolButtonStyle(Qt.ToolButtonTextUnderIcon)
        self.setCheckable(False)
        self.setMinimumSize(230, 230)
        self.setMaximumSize(230, 230)
        self.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)

        shadow = QGraphicsDropShadowEffect(self)
        shadow.setBlurRadius(24)
        shadow.setOffset(0, 7)
        shadow.setColor(QColor(19, 59, 88, 62))
        self.setGraphicsEffect(shadow)


class HomePage(QWidget):
    action_clicked = Signal(str)
    profile_requested = Signal()

    def __init__(self) -> None:
        super().__init__()
        app_root = Path(__file__).resolve().parents[2]
        self._icon_dirs = [
            app_root / "assets" / "icons",
            app_root / "assets" / "immagini",
        ]

        root = QVBoxLayout(self)
        root.setContentsMargins(18, 18, 18, 18)
        root.setSpacing(14)

        top = QHBoxLayout()
        title = QLabel("")
        title.setObjectName("HomeLogo")
        logo = self._load_home_logo()
        if logo and not logo.isNull():
            title.setPixmap(logo)
        else:
            title.setText("AntraluxCloud")
            title.setObjectName("HomeTitle")
        self.session_label = QPushButton("")
        self.session_label.setObjectName("HomeSessionLink")
        self.session_label.setFlat(True)
        self.session_label.setCursor(Qt.PointingHandCursor)
        self.session_label.clicked.connect(self.profile_requested.emit)
        top.addWidget(title)
        top.addStretch(1)
        top.addWidget(self.session_label)
        root.addLayout(top)

        self._grid_holder = QWidget()
        self._grid = QGridLayout(self._grid_holder)
        self._grid.setContentsMargins(8, 8, 8, 8)
        self._grid.setHorizontalSpacing(14)
        self._grid.setVerticalSpacing(14)
        root.addWidget(self._grid_holder, 1)

        actions = [
            HomeAction(
                "plants",
                "Impianti",
                self._load_custom_icon(
                    ["plants", "impianti", "iconaImpianti", "IconaImpianti"],
                    icons.icon_plants(),
                ),
            ),
            HomeAction(
                "settings",
                "Impostazioni",
                self._load_custom_icon(
                    ["settings", "impostazioni", "iconaImpostazioni", "iconaSetting"],
                    icons.icon_settings(),
                ),
            ),
            HomeAction(
                "board_config",
                "Configurazione\nScheda",
                self._load_custom_icon(
                    ["board_config", "iconaScheda", "configurazioneScheda", "schedaRelay"],
                    icons.icon_board(),
                ),
            ),
            HomeAction(
                "board_read",
                "Leggi\nScheda",
                self._load_custom_icon(
                    ["board_read", "leggiScheda", "iconaleggischeda", "iconaLeggiScheda"],
                    icons.icon_board(),
                ),
            ),
            HomeAction(
                "firmware_upload",
                "Caricamento\nFirmware",
                self._load_custom_icon(
                    ["firmware_upload", "firmware", "caricamentoFirmware", "iconaFirmware"],
                    icons.icon_firmware(),
                ),
            ),
            HomeAction(
                "new_plant",
                "Nuovo\nImpianto",
                self._load_custom_icon(
                    [
                        "new_plant",
                        "nuovoImpianto",
                        "iconaNuovoImpianto",
                        "iconaConfigurazione",
                        "icona Configurazione",
                    ],
                    icons.icon_new_plant(),
                ),
            ),
        ]

        self._tiles: list[HomeTileButton] = []
        for action in actions:
            btn = HomeTileButton(action)
            btn.clicked.connect(lambda _checked=False, k=action.key: self.action_clicked.emit(k))
            self._tiles.append(btn)

        self._relayout_tiles()

    def _load_home_logo(self) -> QPixmap:
        app_root = Path(__file__).resolve().parents[2]
        candidates = [
            app_root.parent / "WebSiteComunicazione" / "assets" / "img" / "AntraluxCloud.png",
            app_root / "assets" / "immagini" / "Icona.png",
        ]
        for p in candidates:
            if not p.exists():
                continue
            pix = QPixmap(str(p))
            if pix.isNull():
                continue
            return pix.scaled(260, 76, Qt.KeepAspectRatio, Qt.SmoothTransformation)
        return QPixmap()

    def set_session_text(self, value: str) -> None:
        self.session_label.setText(value)

    def resizeEvent(self, event) -> None:  # type: ignore[override]
        super().resizeEvent(event)
        self._relayout_tiles()

    def _relayout_tiles(self) -> None:
        for idx, tile in enumerate(self._tiles):
            self._grid.removeWidget(tile)
            tile.show()

        if not self._tiles:
            return

        margins = self._grid.contentsMargins()
        available_width = max(
            1,
            self._grid_holder.width() - margins.left() - margins.right(),
        )
        tile_width = self._tiles[0].minimumWidth()
        spacing = self._grid.horizontalSpacing()
        cell_width = tile_width + max(0, spacing)
        columns = max(1, (available_width + max(0, spacing)) // max(1, cell_width))

        for i, tile in enumerate(self._tiles):
            row = i // columns
            col = i % columns
            self._grid.addWidget(tile, row, col, alignment=Qt.AlignLeft | Qt.AlignTop)

    def _load_custom_icon(self, base_names: list[str], fallback: QIcon) -> QIcon:
        valid_ext = (".png", ".jpg", ".jpeg", ".bmp", ".webp", ".svg")

        def normalize(name: str) -> str:
            return re.sub(r"[^a-z0-9]+", "", name.lower())

        normalized_requested = [normalize(name) for name in base_names if name.strip()]
        if not normalized_requested:
            return fallback

        # 1) Ricerca diretta nome + estensione
        for directory in self._icon_dirs:
            if not directory.exists():
                continue
            for base_name in base_names:
                for ext in valid_ext:
                    path = directory / f"{base_name}{ext}"
                    if not path.exists():
                        continue
                    pix = QPixmap(str(path))
                    if not pix.isNull():
                        return QIcon(pix)

        # 2) Ricerca robusta ignorando maiuscole/spazi/simboli nel nome file
        for directory in self._icon_dirs:
            if not directory.exists():
                continue
            for file_path in directory.iterdir():
                if not file_path.is_file() or file_path.suffix.lower() not in valid_ext:
                    continue
                stem_norm = normalize(file_path.stem)
                if stem_norm in normalized_requested:
                    pix = QPixmap(str(file_path))
                    if not pix.isNull():
                        return QIcon(pix)

        # 3) Match parziale (es. "iconaimpostazioni" vs "iconasetting")
        for directory in self._icon_dirs:
            if not directory.exists():
                continue
            for file_path in directory.iterdir():
                if not file_path.is_file() or file_path.suffix.lower() not in valid_ext:
                    continue
                stem_norm = normalize(file_path.stem)
                for wanted in normalized_requested:
                    if wanted and (wanted in stem_norm or stem_norm in wanted):
                        pix = QPixmap(str(file_path))
                        if not pix.isNull():
                            return QIcon(pix)

        return fallback
