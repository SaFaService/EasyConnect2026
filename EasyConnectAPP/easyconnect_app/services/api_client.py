from __future__ import annotations

import logging
from typing import Any
from urllib.parse import urljoin

import httpx

from easyconnect_app.config import AppConfig


class ApiError(Exception):
    pass


class ApiClient:
    _SENSITIVE_KEYS = {"password", "pass", "pwd", "code", "otp", "otp_code", "2fa_code", "two_factor_code", "token"}

    def __init__(self, config: AppConfig) -> None:
        self.config = config
        self._token = ""
        self._http = httpx.Client(
            timeout=float(config.timeout_seconds),
            verify=config.verify_ssl,
            follow_redirects=True,
        )
        self._log = logging.getLogger(__name__)

    @property
    def mock_mode(self) -> bool:
        return self.config.mock_mode

    def set_bearer_token(self, token: str) -> None:
        self._token = token.strip()

    def resolve_endpoint(self, endpoint_key: str) -> str:
        endpoint = self.config.endpoints.get(endpoint_key, "").strip()
        if not endpoint:
            raise ApiError(f"Endpoint '{endpoint_key}' non configurato.")
        if endpoint.startswith("http://") or endpoint.startswith("https://"):
            return endpoint
        if not self.config.base_url:
            raise ApiError("base_url non configurata.")
        return urljoin(self.config.base_url.rstrip("/") + "/", endpoint.lstrip("/"))

    def request_json(
        self,
        method: str,
        url: str,
        *,
        payload: dict[str, Any] | None = None,
        params: dict[str, Any] | None = None,
        as_form: bool = False,
    ) -> dict[str, Any] | list[Any]:
        headers: dict[str, str] = {"Accept": "application/json"}
        if self._token:
            headers["Authorization"] = f"Bearer {self._token}"

        payload_dbg = self._safe_payload(payload)
        self._log.debug(
            "HTTP request | method=%s | url=%s | as_form=%s | payload=%s | params=%s",
            method.upper(),
            url,
            as_form,
            payload_dbg,
            params,
        )

        try:
            request_kwargs: dict[str, Any] = {
                "method": method.upper(),
                "url": url,
                "params": params,
                "headers": headers,
            }
            if as_form:
                request_kwargs["data"] = payload
            else:
                request_kwargs["json"] = payload

            response = self._http.request(
                **request_kwargs,
            )
            self._log.debug("HTTP response | status=%s | url=%s", response.status_code, url)
            response.raise_for_status()
        except httpx.HTTPStatusError as exc:
            resp = exc.response
            message = ""
            try:
                body = resp.json()
                if isinstance(body, dict):
                    message = str(body.get("message", body.get("error", ""))).strip()
            except ValueError:
                message = resp.text.strip()

            if message.startswith("<"):
                message = ""
            if len(message) > 220:
                message = message[:220].rstrip() + "..."
            self._log.warning(
                "HTTP status error | status=%s | url=%s | message=%s",
                resp.status_code,
                url,
                message,
            )

            if resp.status_code == 401:
                message = message or "Credenziali non valide o accesso non autorizzato."
                raise ApiError(f"{message} (HTTP 401)") from exc
            if resp.status_code == 403:
                message = message or "Permessi insufficienti per questa operazione."
                raise ApiError(f"{message} (HTTP 403)") from exc
            if resp.status_code == 404:
                message = message or "Endpoint non trovato: verifica base_url/percorso."
                raise ApiError(f"{message} (HTTP 404)") from exc

            fallback = message or f"Errore server HTTP {resp.status_code}."
            raise ApiError(fallback) from exc
        except httpx.HTTPError as exc:
            self._log.exception("HTTP network error | url=%s", url)
            raise ApiError("Errore di rete: impossibile raggiungere il server.") from exc

        try:
            data = response.json()
            self._log.debug("HTTP json ok | url=%s | type=%s", url, type(data).__name__)
            return data
        except ValueError as exc:
            self._log.error("HTTP json parse error | url=%s | body_prefix=%s", url, response.text[:140])
            raise ApiError("Risposta non JSON dal backend.") from exc

    def call_endpoint(
        self,
        endpoint_key: str,
        *,
        method: str = "GET",
        payload: dict[str, Any] | None = None,
        params: dict[str, Any] | None = None,
        as_form: bool = False,
    ) -> dict[str, Any] | list[Any]:
        return self.request_json(
            method=method,
            url=self.resolve_endpoint(endpoint_key),
            payload=payload,
            params=params,
            as_form=as_form,
        )

    def _safe_payload(self, payload: dict[str, Any] | None) -> dict[str, Any] | None:
        if payload is None:
            return None
        out: dict[str, Any] = {}
        for key, value in payload.items():
            key_l = str(key).strip().lower()
            if key_l in self._SENSITIVE_KEYS:
                value_s = str(value) if value is not None else ""
                if value_s == "":
                    out[str(key)] = ""
                elif key_l == "password":
                    out[str(key)] = "<masked>"
                else:
                    out[str(key)] = f"<masked:{len(value_s)}>"
            else:
                out[str(key)] = value
        return out
