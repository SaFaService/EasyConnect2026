import logging
import sys
from pathlib import Path

from PySide6.QtCore import QTimer
from PySide6.QtGui import QIcon
from PySide6.QtWidgets import QApplication

from easyconnect_app.config import load_app_config
from easyconnect_app.ui.main_window import MainWindow
from easyconnect_app.ui.splash import SplashScreen


def setup_logging(app_root: Path) -> None:
    log_path = app_root / "easyconnect_debug.log"
    root = logging.getLogger()
    if root.handlers:
        return

    root.setLevel(logging.DEBUG)
    fmt = logging.Formatter("%(asctime)s | %(levelname)s | %(name)s | %(message)s")

    console = logging.StreamHandler(sys.stdout)
    console.setLevel(logging.DEBUG)
    console.setFormatter(fmt)
    root.addHandler(console)

    file_handler = logging.FileHandler(log_path, encoding="utf-8")
    file_handler.setLevel(logging.DEBUG)
    file_handler.setFormatter(fmt)
    root.addHandler(file_handler)


def main() -> int:
    config = load_app_config()
    app_root = Path(__file__).resolve().parent
    setup_logging(app_root)
    logging.getLogger(__name__).info(
        "Avvio EasyConnect Desktop | base_url=%s | mock_mode=%s",
        config.base_url,
        config.mock_mode,
    )
    brand_assets = app_root.parent / "WebSiteComunicazione" / "assets" / "img"

    app = QApplication(sys.argv)
    app.setApplicationName("EasyConnect Desktop")
    app_icon_path = app_root / "assets" / "immagini" / "Icona.png"
    if app_icon_path.exists():
        icon = QIcon(str(app_icon_path))
        app.setWindowIcon(icon)

    splash = SplashScreen(
        logo_path=brand_assets / "AntraluxCloud.png",
        startup_messages=[
            "Caricando utenti...",
            "Caricando impianti...",
            "Leggendo le periferiche...",
        ],
        duration_ms=3600,
    )
    window: MainWindow | None = None

    def ensure_window() -> MainWindow:
        nonlocal window
        if window is None:
            window = MainWindow(config)
            if app_icon_path.exists():
                window.setWindowIcon(QIcon(str(app_icon_path)))
        return window

    def show_main_window() -> None:
        w = ensure_window()
        w.showNormal()
        w.showMaximized()
        w.activateWindow()

    splash.play(show_main_window)
    QTimer.singleShot(60, ensure_window)
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
