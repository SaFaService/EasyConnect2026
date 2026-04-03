from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QAction, QColor, QIcon, QPainter, QPainterPath, QPen, QPixmap
from PySide6.QtWidgets import (
    QCheckBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)


class LoginPage(QWidget):
    login_requested = Signal(str, str)
    forgot_password_requested = Signal()

    def __init__(self, logo_path: str | Path | None = None) -> None:
        super().__init__()
        self._card = QGroupBox("")
        self._card.setObjectName("LoginCard")
        self._card.setMinimumSize(360, 470)
        self._card.setMaximumSize(1000, 1000)

        form = QFormLayout(self._card)
        form.setVerticalSpacing(12)

        self._logo_label = QLabel("")
        self._logo_label.setObjectName("LoginLogo")
        self._logo_label.setAlignment(Qt.AlignCenter)
        self._logo_source = QPixmap()
        if logo_path:
            pix = QPixmap(str(logo_path))
            if not pix.isNull():
                self._logo_source = pix
                self._logo_label.setPixmap(pix)

        header = QLabel("Accesso Operatore")
        header.setObjectName("LoginTitle")

        self.username = QLineEdit()
        self.username.setPlaceholderText("email")
        self.password = QLineEdit()
        self.password.setEchoMode(QLineEdit.Password)
        self.password.setPlaceholderText("password")
        self.password.returnPressed.connect(self._emit_login)
        self._password_toggle_action = QAction(self)
        self._password_toggle_action.setCheckable(True)
        self._password_toggle_action.setToolTip("Mostra password")
        self._password_toggle_action.toggled.connect(self._toggle_password_visibility)
        self.password.addAction(self._password_toggle_action, QLineEdit.TrailingPosition)
        self._update_password_toggle_icon(False)

        self.status = QLabel("")
        self.status.setObjectName("LoginStatus")
        self.status.setWordWrap(True)

        self.login_button = QPushButton("Accedi")
        self.login_button.clicked.connect(self._emit_login)
        self.forgot_password_button = QPushButton("Recupera password")
        self.forgot_password_button.setObjectName("ForgotBtn")
        self.forgot_password_button.setFlat(True)
        self.forgot_password_button.setCursor(Qt.PointingHandCursor)
        self.forgot_password_button.clicked.connect(self.forgot_password_requested.emit)

        email_label = QLabel("Login")
        email_label.setObjectName("LoginFieldLabel")
        pwd_label = QLabel("Password")
        pwd_label.setObjectName("LoginFieldLabel")
        self.remember_username_chk = QCheckBox("Ricorda Username")
        self.remember_username_chk.setObjectName("RememberUsernameChk")

        form.addRow(self._logo_label)
        form.addRow(header)
        form.addRow(email_label, self.username)
        form.addRow(pwd_label, self.password)
        form.addRow("", self.remember_username_chk)

        btn_row = QHBoxLayout()
        btn_row.addStretch(1)
        btn_row.addWidget(self.login_button)
        form.addRow(btn_row)

        link_row = QHBoxLayout()
        link_row.addStretch(1)
        link_row.addWidget(self.forgot_password_button)
        link_row.addStretch(1)
        form.addRow(link_row)
        form.addRow(self.status)

        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.addStretch(1)
        center_row = QHBoxLayout()
        center_row.addStretch(1)
        center_row.addWidget(self._card, alignment=Qt.AlignCenter)
        center_row.addStretch(1)
        root.addLayout(center_row)
        root.addStretch(1)

    def resizeEvent(self, event) -> None:  # type: ignore[override]
        super().resizeEvent(event)
        w = max(360, int(self.width() * 0.40))
        h = max(470, int(self.height() * 0.70))
        self._card.setFixedSize(w, h)
        pix = self._logo_source
        if pix and not pix.isNull():
            max_w = int(w * 0.52)
            max_h = int(h * 0.16)
            self._logo_label.setPixmap(
                pix.scaled(max_w, max_h, Qt.KeepAspectRatio, Qt.SmoothTransformation)
            )

    def set_busy(self, busy: bool) -> None:
        self.login_button.setEnabled(not busy)
        self.username.setEnabled(not busy)
        self.password.setEnabled(not busy)
        self._password_toggle_action.setEnabled(not busy)
        self.remember_username_chk.setEnabled(not busy)
        self.forgot_password_button.setEnabled(not busy)
        self.status.setText("Verifica in corso..." if busy else self.status.text())

    def show_error(self, message: str) -> None:
        self.status.setText(message)

    def _emit_login(self) -> None:
        self.status.setText("")
        self.login_requested.emit(self.username.text(), self.password.text())

    def _toggle_password_visibility(self, checked: bool) -> None:
        self.password.setEchoMode(QLineEdit.Normal if checked else QLineEdit.Password)
        self._update_password_toggle_icon(checked)
        self._password_toggle_action.setToolTip("Nascondi password" if checked else "Mostra password")

    def _update_password_toggle_icon(self, visible: bool) -> None:
        self._password_toggle_action.setIcon(self._build_eye_icon(crossed=visible))

    def set_username(self, username: str) -> None:
        self.username.setText(username)

    def set_remember_username(self, enabled: bool) -> None:
        self.remember_username_chk.setChecked(enabled)

    def remember_username(self) -> bool:
        return self.remember_username_chk.isChecked()

    def _build_eye_icon(self, crossed: bool) -> QIcon:
        size = 18
        pix = QPixmap(size, size)
        pix.fill(Qt.transparent)

        painter = QPainter(pix)
        painter.setRenderHint(QPainter.Antialiasing, True)
        pen = QPen(QColor(35, 42, 52), 1.7, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin)
        painter.setPen(pen)
        painter.setBrush(Qt.NoBrush)

        eye = QPainterPath()
        eye.moveTo(2.0, 9.0)
        eye.cubicTo(5.0, 3.4, 13.0, 3.4, 16.0, 9.0)
        eye.cubicTo(13.0, 14.6, 5.0, 14.6, 2.0, 9.0)
        painter.drawPath(eye)
        painter.setBrush(QColor(35, 42, 52))
        painter.drawEllipse(7.2, 7.2, 3.6, 3.6)

        if crossed:
            painter.setPen(QPen(QColor(35, 42, 52), 2.0, Qt.SolidLine, Qt.RoundCap))
            painter.drawLine(3.0, 15.0, 15.0, 3.0)

        painter.end()
        return QIcon(pix)
