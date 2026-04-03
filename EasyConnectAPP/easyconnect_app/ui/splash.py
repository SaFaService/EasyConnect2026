from __future__ import annotations

import math
from pathlib import Path
from typing import Callable

from PySide6.QtCore import QElapsedTimer, QRectF, Qt, QTimer
from PySide6.QtGui import QColor, QGuiApplication, QLinearGradient, QPainter, QPixmap
from PySide6.QtWidgets import QWidget


def _clamp(value: float, min_value: float = 0.0, max_value: float = 1.0) -> float:
    return max(min_value, min(max_value, value))


def _ease(value: float) -> float:
    x = _clamp(value)
    return x * x * (3.0 - 2.0 * x)


def _phase(progress: float, start: float, end: float) -> float:
    if end <= start:
        return 1.0
    return _clamp((progress - start) / (end - start))


class SplashScreen(QWidget):
    def __init__(
        self,
        *,
        logo_path: str | Path | None = None,
        startup_messages: list[str] | None = None,
        duration_ms: int = 2800,
    ) -> None:
        super().__init__()
        self.setWindowFlags(Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.SplashScreen)
        self.setAttribute(Qt.WA_OpaquePaintEvent, True)
        self.setFixedSize(860, 460)

        self._duration_ms = max(1800, int(duration_ms))
        self._on_finished: Callable[[], None] | None = None
        self._logo = QPixmap(str(logo_path)) if logo_path else QPixmap()
        self._messages = startup_messages or [
            "Caricando utenti...",
            "Caricando impianti...",
            "Leggendo le periferiche...",
            "Verificando Impianti...",
            "Lettura versioni software..."
        ]

        self._timer = QTimer(self)
        self._timer.setInterval(16)
        self._timer.timeout.connect(self._on_tick)
        self._elapsed = QElapsedTimer()

    def play(self, on_finished: Callable[[], None]) -> None:
        self._on_finished = on_finished
        self._center_on_screen()
        self.show()
        self._elapsed.restart()
        self._timer.start()
        self.update()

    def _center_on_screen(self) -> None:
        screen = QGuiApplication.primaryScreen()
        if not screen:
            return
        geo = screen.availableGeometry()
        self.move(geo.center().x() - self.width() // 2, geo.center().y() - self.height() // 2)

    def _on_tick(self) -> None:
        if self._elapsed.elapsed() >= self._duration_ms:
            self._timer.stop()
            self.close()
            if self._on_finished:
                self._on_finished()
            return
        self.update()

    def paintEvent(self, _event) -> None:  # type: ignore[override]
        elapsed = min(self._elapsed.elapsed(), self._duration_ms)
        p = elapsed / float(self._duration_ms)

        fade_in = _ease(_phase(p, 0.0, 0.24))
        fade_out = _ease(_phase(p, 0.92, 1.0))
        scene_alpha = fade_in * (1.0 - fade_out)

        pulse_strength = _ease(_phase(p, 0.24, 0.42)) * (1.0 - _ease(_phase(p, 0.85, 1.0)))
        pulse = 1.0 + (0.14 * pulse_strength * math.sin((elapsed / 1000.0) * 2.0 * math.pi * 0.38))

        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing, True)
        painter.setRenderHint(QPainter.SmoothPixmapTransform, True)

        self._draw_background(painter)
        card = QRectF(28, 24, self.width() - 56, self.height() - 48)
        self._draw_card(painter, card, scene_alpha)
        self._draw_logo(painter, card, pulse, scene_alpha)
        self._draw_startup_message(painter, card, p, scene_alpha)

    def _draw_background(self, painter: QPainter) -> None:
        grad = QLinearGradient(0.0, 0.0, self.width(), self.height())
        grad.setColorAt(0.0, QColor("#eef4fb"))
        grad.setColorAt(1.0, QColor("#dde9f6"))
        painter.fillRect(self.rect(), grad)

    def _draw_card(self, painter: QPainter, rect: QRectF, alpha: float) -> None:
        painter.save()
        painter.setOpacity(max(0.0, min(1.0, alpha)))
        painter.setPen(Qt.NoPen)
        painter.setBrush(QColor(255, 255, 255, 238))
        painter.drawRoundedRect(rect, 20, 20)
        painter.restore()

    def _draw_logo(self, painter: QPainter, card: QRectF, pulse: float, alpha: float) -> None:
        if alpha <= 0.0:
            return
        painter.save()
        painter.setOpacity(max(0.0, min(1.0, alpha)))
        if self._logo.isNull():
            painter.setPen(QColor("#23445f"))
            painter.drawText(card, Qt.AlignCenter, "AntraluxCloud")
            painter.restore()
            return

        slot = QRectF(card.left() + 70, card.top() + 120, card.width() - 140, card.height() - 190)
        draw_rect = self._fit_rect(slot, self._logo)

        cx = draw_rect.center().x()
        cy = draw_rect.center().y()
        w = draw_rect.width() * pulse
        h = draw_rect.height() * pulse
        scaled = QRectF(cx - w / 2.0, cy - h / 2.0, w, h)

        painter.drawPixmap(scaled.toRect(), self._logo)
        painter.restore()

    def _draw_startup_message(self, painter: QPainter, card: QRectF, progress: float, base_alpha: float) -> None:
        msg_alpha = _ease(_phase(progress, 0.12, 0.30)) * (1.0 - _ease(_phase(progress, 0.94, 1.0))) * base_alpha
        if msg_alpha <= 0.0:
            return

        idx = min(len(self._messages) - 1, int(progress * len(self._messages)))
        message = self._messages[idx]

        painter.save()
        painter.setOpacity(msg_alpha)
        painter.setPen(QColor("#2c5f82"))
        painter.drawText(
            QRectF(card.left() + 20, card.bottom() - 72, card.width() - 40, 30),
            Qt.AlignHCenter | Qt.AlignVCenter,
            message,
        )
        painter.restore()

    @staticmethod
    def _fit_rect(slot: QRectF, pixmap: QPixmap) -> QRectF:
        pw = float(pixmap.width())
        ph = float(pixmap.height())
        if pw <= 0.0 or ph <= 0.0:
            return slot

        scale = min(slot.width() / pw, slot.height() / ph)
        w = pw * scale
        h = ph * scale
        x = slot.left() + (slot.width() - w) / 2.0
        y = slot.top() + (slot.height() - h) / 2.0
        return QRectF(x, y, w, h)
