from __future__ import annotations

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QFont, QIcon, QPainter, QPainterPath, QPen, QPixmap


def _icon_canvas(size: int = 128) -> tuple[QPixmap, QPainter]:
    pm = QPixmap(size, size)
    pm.fill(Qt.transparent)
    painter = QPainter(pm)
    painter.setRenderHint(QPainter.Antialiasing, True)
    return pm, painter


def _finalize(pm: QPixmap, painter: QPainter) -> QIcon:
    painter.end()
    return QIcon(pm)


def icon_plants() -> QIcon:
    pm, p = _icon_canvas()
    p.setPen(QPen(QColor("#1f6a97"), 6))
    p.setBrush(Qt.NoBrush)
    p.drawArc(QRectF(24, 46, 80, 60), 20 * 16, 140 * 16)
    p.setPen(QPen(QColor("#f59d3d"), 6))
    p.drawLine(64, 76, 96, 50)
    p.setPen(QPen(QColor("#2f89b0"), 5, Qt.SolidLine, Qt.RoundCap))
    for i in range(3):
        y = 28 + i * 20
        path = QPainterPath(QPointF(20, y))
        path.cubicTo(QPointF(38, y - 8), QPointF(58, y + 8), QPointF(86, y))
        p.drawPath(path)
    return _finalize(pm, p)


def icon_settings() -> QIcon:
    pm, p = _icon_canvas()
    center = QPointF(64, 64)
    p.setPen(Qt.NoPen)
    p.setBrush(QColor("#2b7ea1"))
    for i in range(8):
        p.save()
        p.translate(center)
        p.rotate(i * 45)
        p.drawRoundedRect(QRectF(32, -7, 18, 14), 3, 3)
        p.restore()
    p.setBrush(QColor("#4da7c7"))
    p.drawEllipse(QRectF(28, 28, 72, 72))
    p.setBrush(QColor("#ffffff"))
    p.drawEllipse(QRectF(49, 49, 30, 30))
    return _finalize(pm, p)


def icon_board() -> QIcon:
    pm, p = _icon_canvas()
    p.setPen(Qt.NoPen)
    p.setBrush(QColor("#1f6a3f"))
    p.drawRoundedRect(QRectF(12, 20, 104, 88), 14, 14)
    p.setBrush(QColor("#dce5ef"))
    p.drawRoundedRect(QRectF(38, 44, 52, 32), 4, 4)
    p.setBrush(QColor("#2a2f3b"))
    p.drawRoundedRect(QRectF(18, 28, 20, 24), 3, 3)
    p.drawRoundedRect(QRectF(90, 28, 20, 24), 3, 3)
    p.drawRoundedRect(QRectF(18, 76, 20, 24), 3, 3)
    p.drawRoundedRect(QRectF(90, 76, 20, 24), 3, 3)
    return _finalize(pm, p)


def icon_firmware() -> QIcon:
    pm, p = _icon_canvas()
    p.setPen(QPen(QColor("#0b7f4a"), 4))
    p.setBrush(Qt.NoBrush)
    p.drawRoundedRect(QRectF(16, 18, 96, 92), 10, 10)
    p.setPen(QPen(QColor("#2fc675"), 2))
    font = QFont("Consolas", 12, QFont.Bold)
    p.setFont(font)
    matrix_cols = ["101", "011", "110", "001", "100"]
    for i, col in enumerate(matrix_cols):
        p.drawText(QRectF(26 + i * 18, 30, 18, 70), Qt.AlignTop | Qt.AlignHCenter, col)
    p.setPen(QPen(QColor("#f59d3d"), 5, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin))
    p.drawLine(64, 20, 64, 44)
    p.drawLine(64, 20, 54, 30)
    p.drawLine(64, 20, 74, 30)
    return _finalize(pm, p)


def icon_new_plant() -> QIcon:
    pm, p = _icon_canvas()
    # Mix stilizzato delle icone richieste (lampada, flussi, filtro, uvc)
    p.setPen(Qt.NoPen)
    p.setBrush(QColor("#f4b12f"))
    p.drawEllipse(QRectF(10, 16, 46, 46))  # lampada
    p.setBrush(QColor("#111111"))
    p.drawRoundedRect(QRectF(26, 56, 14, 20), 4, 4)

    p.setBrush(QColor("#f59d3d"))
    p.drawRoundedRect(QRectF(56, 16, 16, 52), 5, 5)  # filtro
    p.setBrush(QColor("#5ac1ff"))
    for i in range(4):
        p.drawRoundedRect(QRectF(76, 16 + i * 13, 30, 8), 3, 3)

    p.setPen(QPen(QColor("#2f89b0"), 6, Qt.SolidLine, Qt.RoundCap))
    p.drawArc(QRectF(14, 74, 42, 28), 20 * 16, 140 * 16)  # flusso aria
    p.setPen(QPen(QColor("#f59d3d"), 6, Qt.SolidLine, Qt.RoundCap))
    p.drawArc(QRectF(52, 74, 42, 28), 20 * 16, 140 * 16)

    p.setPen(QPen(QColor("#9f59c8"), 4, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin))
    lightning = QPainterPath()
    lightning.moveTo(92, 74)
    lightning.lineTo(78, 100)
    lightning.lineTo(90, 100)
    lightning.lineTo(82, 114)
    p.drawPath(lightning)  # richiamo UVC
    return _finalize(pm, p)
