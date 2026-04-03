from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


@dataclass(slots=True)
class AppConfig:
    app_version: str = "0.2.1"
    base_url: str = ""
    verify_ssl: bool = True
    timeout_seconds: int = 20
    mock_mode: bool = False
    endpoints: dict[str, str] = field(
        default_factory=lambda: {
            "auth": "/api_desktop_auth.php",
            "plants": "/api_desktop_plants.php",
            "serial": "/api_serial.php",
            "create_plant": "/api_desktop_create_plant.php",
            "forgot_password": "/forgot_password.php",
            "profile": "/profile.php",
            "profile_api": "/api_desktop_profile.php",
            "plant_detail": "/api_desktop_plant_detail.php",
            "plant_update": "/api_desktop_plant_update.php",
            "version_check": "/api_desktop_version.php",
            "serial_overview": "/api_serial_overview.php",
            "serial_detail": "/serial_detail.php",
        }
    )


def _merge_defaults(data: dict[str, Any]) -> AppConfig:
    cfg = AppConfig()
    cfg.app_version = str(data.get("app_version", cfg.app_version)).strip() or cfg.app_version
    cfg.base_url = str(data.get("base_url", cfg.base_url)).strip()
    if "antralux.com" in cfg.base_url.lower():
        cfg.base_url = cfg.base_url.replace("/Impianti", "/impianti")
    cfg.verify_ssl = bool(data.get("verify_ssl", cfg.verify_ssl))
    cfg.timeout_seconds = int(data.get("timeout_seconds", cfg.timeout_seconds))
    cfg.mock_mode = bool(data.get("mock_mode", cfg.mock_mode))

    endpoints = data.get("endpoints", {})
    if isinstance(endpoints, dict):
        for key, val in endpoints.items():
            if isinstance(key, str) and isinstance(val, str):
                cfg.endpoints[key] = val

    return cfg


def load_app_config() -> AppConfig:
    root_dir = Path(__file__).resolve().parents[1]
    config_path = root_dir / "appsettings.json"
    example_path = root_dir / "appsettings.example.json"

    for path in (config_path, example_path):
        if path.exists():
            raw = path.read_text(encoding="utf-8")
            data = json.loads(raw)
            if not isinstance(data, dict):
                raise ValueError(f"Configurazione non valida in {path}")
            return _merge_defaults(data)

    return AppConfig()
