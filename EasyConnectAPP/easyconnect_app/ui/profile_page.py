from __future__ import annotations

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from easyconnect_app.models import UserProfile


class ProfilePage(QWidget):
    back_requested = Signal()
    save_requested = Signal(dict)
    refresh_requested = Signal()

    def __init__(self) -> None:
        super().__init__()
        root = QVBoxLayout(self)
        root.setContentsMargins(18, 18, 18, 18)
        root.setSpacing(12)

        top = QHBoxLayout()
        self.back_btn = QPushButton("Torna alla Home")
        self.back_btn.clicked.connect(self.back_requested.emit)
        self.title = QLabel("Profilo Operatore")
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

        account = QGroupBox("Account")
        account_form = QFormLayout(account)
        self.email = QLineEdit()
        self.email.setReadOnly(True)
        self.role = QLineEdit()
        self.role.setReadOnly(True)
        self.has_2fa = QLabel("-")
        account_form.addRow("Email:", self.email)
        account_form.addRow("Ruolo:", self.role)
        account_form.addRow("2FA:", self.has_2fa)
        root.addWidget(account)

        contacts = QGroupBox("Contatti")
        cform = QFormLayout(contacts)
        self.name = QLineEdit()
        self.company = QLineEdit()
        self.phone = QLineEdit()
        self.whatsapp = QLineEdit()
        self.telegram = QLineEdit()
        cform.addRow("Nome:", self.name)
        cform.addRow("Azienda:", self.company)
        cform.addRow("Telefono:", self.phone)
        cform.addRow("WhatsApp:", self.whatsapp)
        cform.addRow("Telegram:", self.telegram)
        root.addWidget(contacts)

        actions = QHBoxLayout()
        actions.addStretch(1)
        self.save_btn = QPushButton("Salva profilo")
        self.save_btn.clicked.connect(self._emit_save)
        actions.addWidget(self.save_btn)
        root.addLayout(actions)
        root.addStretch(1)

    def set_busy(self, busy: bool) -> None:
        self.refresh_btn.setEnabled(not busy)
        self.save_btn.setEnabled(not busy)
        self.name.setEnabled(not busy)
        self.company.setEnabled(not busy)
        self.phone.setEnabled(not busy)
        self.whatsapp.setEnabled(not busy)
        self.telegram.setEnabled(not busy)
        if busy:
            self.status.setText("Caricamento profilo...")

    def set_profile(self, profile: UserProfile) -> None:
        self.email.setText(profile.email)
        self.role.setText(profile.role)
        self.name.setText(profile.name)
        self.company.setText(profile.company)
        self.phone.setText(profile.phone)
        self.whatsapp.setText(profile.whatsapp)
        self.telegram.setText(profile.telegram)
        self.has_2fa.setText("Attiva" if profile.has_2fa else "Disattiva")
        self.status.setText("Profilo caricato")

    def show_error(self, message: str) -> None:
        self.status.setText(message)

    def _emit_save(self) -> None:
        self.save_requested.emit(
            {
                "name": self.name.text().strip(),
                "company": self.company.text().strip(),
                "phone": self.phone.text().strip(),
                "whatsapp": self.whatsapp.text().strip(),
                "telegram": self.telegram.text().strip(),
            }
        )

