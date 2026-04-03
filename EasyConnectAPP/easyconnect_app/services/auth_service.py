from __future__ import annotations

import logging

from easyconnect_app.models import UserSession
from easyconnect_app.services.api_client import ApiClient, ApiError


class AuthService:
    ALLOWED_ROLES = {"admin", "builder", "maintainer"}

    def __init__(self, api_client: ApiClient) -> None:
        self.api_client = api_client
        self._log = logging.getLogger(__name__)

    def login(self, username: str, password: str, otp_code: str = "") -> UserSession:
        username = username.strip()
        if not username or not password:
            raise ApiError("Inserisci email e password.")
        self._log.info(
            "Login attempt | user=%s | mock_mode=%s | has_otp=%s",
            username,
            self.api_client.mock_mode,
            bool(otp_code.strip()),
        )

        if self.api_client.mock_mode:
            role = self._mock_role_for_user(username, password)
            token = f"mock-{username}"
            self.api_client.set_bearer_token(token)
            return UserSession(username=username, role=role, token=token)

        otp = otp_code.strip()
        payload = {
            "email": username,
            "username": username,
            "password": password,
            # Invio multiplo per compatibilita' con diversi deploy backend.
            "code": otp,
            "otp": otp,
            "otp_code": otp,
            "two_factor_code": otp,
            "2fa_code": otp,
            "source": "desktop_app",
        }

        try:
            response = self.api_client.call_endpoint(
                "auth",
                method="POST",
                payload=payload,
            )
        except ApiError as exc:
            # Fallback: alcuni deploy PHP accettano correttamente solo form-urlencoded.
            message = str(exc)
            self._log.warning("Login JSON failed | user=%s | message=%s", username, message)
            if "HTTP 401" not in message and "Credenziali non valide" not in message:
                raise
            try:
                response = self.api_client.call_endpoint(
                    "auth",
                    method="POST",
                    payload=payload,
                    as_form=True,
                )
                self._log.info("Login fallback form-urlencoded succeeded | user=%s", username)
            except ApiError as exc_form:
                self._log.warning("Login form fallback failed | user=%s | message=%s", username, str(exc_form))
                raise ApiError(
                    f"{exc_form} | endpoint auth verificato sia in JSON che form-data."
                ) from exc_form
        if not isinstance(response, dict):
            raise ApiError("Formato risposta login non valido.")

        if str(response.get("status", "")).lower() == "2fa_required":
            self._log.info("Login requires 2FA | user=%s", username)
            raise ApiError("2FA_REQUIRED")

        user_obj = response.get("user", {})
        raw_role = str(response.get("role", user_obj.get("role", ""))).strip()
        role = self._normalize_role(raw_role)
        token = str(response.get("token", response.get("session_token", ""))).strip()

        if role not in self.ALLOWED_ROLES:
            self._log.warning("Login denied by role | user=%s | role=%s", username, role)
            raise ApiError("Ruolo non autorizzato per l'app desktop.")

        if token:
            self.api_client.set_bearer_token(token)
        self._log.info("Login success | user=%s | role=%s | token=%s", username, role, bool(token))

        return UserSession(username=username, role=role, token=token)

    def _mock_role_for_user(self, username: str, password: str) -> str:
        users = {
            "admin": ("admin", "admin"),
            "builder": ("builder", "builder"),
            "maintainer": ("maintainer", "maintainer"),
        }
        entry = users.get(username.lower())
        if not entry:
            raise ApiError("Utente non trovato (mock mode).")
        expected_password, role = entry
        if password != expected_password and password != "easyconnect":
            raise ApiError("Password non valida (mock mode).")
        return role

    def _normalize_role(self, role: str) -> str:
        role_norm = role.strip().lower()
        role_map = {
            "admin": "admin",
            "administrator": "admin",
            "amministratore": "admin",
            "builder": "builder",
            "costruttore": "builder",
            "maintainer": "maintainer",
            "manutentore": "maintainer",
        }
        return role_map.get(role_norm, role_norm)
