from __future__ import annotations

import re
from typing import Any

from easyconnect_app.models import BoardSnapshot, PlantDetail
from easyconnect_app.services.api_client import ApiError
from easyconnect_app.services.serial_service import SerialConsole


class BoardConfigService:
    MASTER_MODE_OPTIONS = [
        (1, "Standalone"),
        (2, "Rewamping"),
    ]
    PERIPHERAL_MODE_OPTIONS = [
        (1, "Temp/Hum"),
        (2, "Pressure"),
        (3, "Tutto"),
    ]
    RELAY_MODE_OPTIONS = [
        (1, "LUCE"),
        (2, "UVC"),
        (3, "ELETTROSTATICO"),
        (4, "GAS"),
        (5, "COMANDO"),
    ]

    def __init__(self, serial_console: SerialConsole) -> None:
        self.serial_console = serial_console

    def read_board(self, port: str) -> BoardSnapshot:
        port = port.strip()
        if not port:
            raise ApiError("Seleziona una porta COM prima di leggere la scheda.")

        raw_info = self.serial_console.read_info(port)
        return self._parse_snapshot(raw_info)

    def validate_selection(
        self,
        detail: PlantDetail,
        snapshot: BoardSnapshot,
        values: dict[str, Any] | None = None,
    ) -> list[str]:
        warnings: list[str] = []
        plant_serial = (detail.plant.serial_number or "").strip().upper()
        has_master = self._is_master_serial(plant_serial)

        if not has_master:
            if not snapshot.is_master:
                raise ApiError("Per questo impianto deve essere configurata prima una centralina.")
            warnings.append("Questo impianto non ha ancora una centralina associata: la scheda letta verra' configurata come centralina.")

        duplicate = self.find_duplicate_ip(detail, snapshot, values)
        if duplicate:
            warnings.append(duplicate)

        return warnings

    def find_duplicate_ip(
        self,
        detail: PlantDetail,
        snapshot: BoardSnapshot,
        values: dict[str, Any] | None = None,
    ) -> str:
        effective_ip = snapshot.rs485_ip
        effective_serial = snapshot.serial_number
        if values:
            override_ip = self._safe_int(values.get("rs485_ip"))
            if override_ip is not None:
                effective_ip = override_ip
            override_serial = str(values.get("serial_number", "")).strip()
            if override_serial:
                effective_serial = override_serial

        if effective_ip is None:
            return ""

        for peripheral in detail.peripherals:
            existing_ip = self._safe_int(peripheral.rs485_ip)
            if existing_ip is None or existing_ip != effective_ip:
                continue
            if peripheral.serial_number.strip() == effective_serial.strip():
                continue
            return (
                f"Attenzione: l'indirizzo IP RS485 {effective_ip} "
                f"risulta gia' usato dalla scheda {peripheral.serial_number}."
            )
        return ""

    def mode_options_for(self, snapshot: BoardSnapshot) -> list[tuple[int, str]]:
        if snapshot.board_kind == "master":
            return list(self.MASTER_MODE_OPTIONS)
        if snapshot.board_kind == "relay":
            return list(self.RELAY_MODE_OPTIONS)
        if snapshot.board_kind == "pressure":
            return list(self.PERIPHERAL_MODE_OPTIONS)
        return []

    def apply_configuration(self, port: str, snapshot: BoardSnapshot, values: dict[str, Any]) -> str:
        port = port.strip()
        if not port:
            raise ApiError("Seleziona una porta COM valida.")

        serial_number = str(values.get("serial_number", snapshot.serial_number)).strip()
        if not serial_number or serial_number.upper() == "NON_SET":
            raise ApiError("Seriale scheda non valido.")

        if snapshot.board_kind == "master":
            mode = self._required_int(values.get("mode_code"), "Modalita centralina mancante.")
            return self.serial_console.configure_master(
                port=port,
                serial_number=serial_number,
                mode=mode,
                api_url=str(values.get("api_url", "")).strip(),
                api_key=str(values.get("api_key", "")).strip(),
                customer_api_url="",
                customer_api_key="",
            )

        if snapshot.board_kind == "pressure":
            return self.serial_console.configure_slave(
                port=port,
                serial_number=serial_number,
                rs485_ip=self._required_int(values.get("rs485_ip"), "Indirizzo IP RS485 mancante."),
                group=self._required_int(values.get("group"), "Gruppo mancante."),
                mode=self._required_int(values.get("mode_code"), "Modalita periferica mancante."),
            )

        if snapshot.board_kind == "relay":
            commands = [
                f"SETIP {self._required_int(values.get('rs485_ip'), 'Indirizzo IP RS485 mancante.')}",
                f"SETSERIAL {serial_number}",
                f"SETGROUP {self._required_int(values.get('group'), 'Gruppo mancante.')}",
                f"SETMODE {self._required_int(values.get('mode_code'), 'Modalita relay mancante.')}",
            ]

            feedback_enabled = bool(values.get("feedback_enabled", False))
            feedback_message = str(values.get("feedback_message", "")).strip()
            safety_message = str(values.get("safety_message", "")).strip()

            if feedback_enabled:
                logic = self._required_int(values.get("feedback_logic"), "Logica feedback mancante.")
                delay_sec = self._required_int(values.get("feedback_delay_sec"), "Delay feedback mancante.")
                attempts = self._required_int(values.get("feedback_attempts"), "Tentativi feedback mancanti.")
                commands.append(f"SETFB ON {logic} {delay_sec} {attempts}")
            else:
                commands.append("SETFB OFF")

            commands.append(f"SETMSG FB {feedback_message if feedback_message else '-'}")
            commands.append(f"SETMSG SAFETY {safety_message if safety_message else '-'}")
            commands.append("INFO")
            return self.serial_console.run_batch(port=port, commands=commands)

        raise ApiError("Tipo scheda non supportato dalla configurazione guidata.")

    def _parse_snapshot(self, raw_info: str) -> BoardSnapshot:
        raw = raw_info.strip()
        if not raw or "NESSUNA RISPOSTA SERIALE" in raw.upper():
            raise ApiError("Nessuna risposta seriale dalla scheda collegata.")
        key_map = self._parse_key_values(raw)
        raw_upper = raw.upper()

        serial_number = (
            key_map.get("seriale")
            or key_map.get("serial")
            or key_map.get("readserial")
            or ""
        ).strip()
        product_type_code = self._serial_type_code(serial_number)
        firmware_version = (
            key_map.get("versionefw")
            or key_map.get("fw")
            or key_map.get("fwversion")
            or ""
        ).strip()

        if "STATO RELAY" in raw_upper or "FB CFG" in raw_upper:
            board_kind = "relay"
            if not product_type_code:
                product_type_code = "03"
            mode_code, mode_label = self._parse_relay_mode(key_map.get("modalita", ""))
            fb_enabled, fb_logic, fb_delay, fb_attempts = self._parse_feedback_cfg(key_map.get("fbcfg", ""))
            return BoardSnapshot(
                board_kind=board_kind,
                product_type_code=product_type_code,
                board_label=self._board_label_for_kind(board_kind, product_type_code),
                serial_number=serial_number,
                firmware_version=firmware_version,
                rs485_ip=self._safe_int(key_map.get("rs485addr") or key_map.get("ip")),
                group=self._safe_int(key_map.get("gruppo") or key_map.get("group")),
                mode_code=mode_code,
                mode_label=mode_label,
                api_url="",
                api_key_state="",
                customer_api_url="",
                customer_api_key_state="",
                feedback_enabled=fb_enabled,
                feedback_logic=fb_logic,
                feedback_delay_sec=fb_delay,
                feedback_attempts=fb_attempts,
                feedback_message=(key_map.get("msgfbfault") or "").strip(),
                safety_message=(key_map.get("msgsafety") or "").strip(),
                raw_info=raw,
            )

        if "STATO ATTUALE CONTROLLER" in raw_upper or "URL API ANTRALUX" in raw_upper:
            board_kind = "master"
            if not product_type_code:
                product_type_code = "02"
            mode_code, mode_label = self._parse_master_mode(key_map.get("modo", ""))
            return BoardSnapshot(
                board_kind=board_kind,
                product_type_code=product_type_code,
                board_label=self._board_label_for_kind(board_kind, product_type_code),
                serial_number=serial_number,
                firmware_version=firmware_version,
                rs485_ip=None,
                group=None,
                mode_code=mode_code,
                mode_label=mode_label,
                api_url=(key_map.get("urlapiantralux") or "").strip(),
                api_key_state=(key_map.get("apikeyantralux") or "").strip(),
                customer_api_url=(key_map.get("urlapicliente") or "").strip(),
                customer_api_key_state=(key_map.get("apikeycliente") or "").strip(),
                feedback_enabled=None,
                feedback_logic=None,
                feedback_delay_sec=None,
                feedback_attempts=None,
                feedback_message="",
                safety_message="",
                raw_info=raw,
            )

        if "STATO ATTUALE PERIFERICA" in raw_upper or "IP (485)" in raw_upper:
            board_kind = "pressure"
            if not product_type_code:
                product_type_code = "04"
            mode_code = self._safe_int(key_map.get("modalita"))
            return BoardSnapshot(
                board_kind=board_kind,
                product_type_code=product_type_code,
                board_label=self._board_label_for_kind(board_kind, product_type_code),
                serial_number=serial_number,
                firmware_version=firmware_version,
                rs485_ip=self._safe_int(key_map.get("ip485") or key_map.get("ip")),
                group=self._safe_int(key_map.get("gruppo") or key_map.get("group")),
                mode_code=mode_code,
                mode_label=self._peripheral_mode_label(mode_code),
                api_url="",
                api_key_state="",
                customer_api_url="",
                customer_api_key_state="",
                feedback_enabled=None,
                feedback_logic=None,
                feedback_delay_sec=None,
                feedback_attempts=None,
                feedback_message="",
                safety_message="",
                raw_info=raw,
            )

        return BoardSnapshot(
            board_kind="unknown",
            product_type_code=product_type_code,
            board_label=self._board_label_for_kind("unknown", product_type_code),
            serial_number=serial_number,
            firmware_version=firmware_version,
            rs485_ip=None,
            group=None,
            mode_code=None,
            mode_label="",
            api_url="",
            api_key_state="",
            customer_api_url="",
            customer_api_key_state="",
            feedback_enabled=None,
            feedback_logic=None,
            feedback_delay_sec=None,
            feedback_attempts=None,
            feedback_message="",
            safety_message="",
            raw_info=raw,
        )

    @staticmethod
    def _parse_key_values(raw: str) -> dict[str, str]:
        out: dict[str, str] = {}
        for line in raw.splitlines():
            if ":" not in line:
                continue
            key, value = line.split(":", 1)
            normalized_key = re.sub(r"[^a-z0-9]+", "", key.strip().lower())
            if normalized_key:
                out[normalized_key] = value.strip()
        return out

    @staticmethod
    def _serial_type_code(serial_number: str) -> str:
        serial = serial_number.strip()
        if re.fullmatch(r"\d{12}", serial):
            return serial[6:8]
        return ""

    @staticmethod
    def _safe_int(value: Any) -> int | None:
        if value is None:
            return None
        match = re.search(r"-?\d+", str(value))
        if not match:
            return None
        try:
            return int(match.group(0))
        except ValueError:
            return None

    @staticmethod
    def _required_int(value: Any, message: str) -> int:
        parsed = BoardConfigService._safe_int(value)
        if parsed is None:
            raise ApiError(message)
        return parsed

    @staticmethod
    def _parse_feedback_cfg(value: str) -> tuple[bool | None, int | None, int | None, int | None]:
        match = re.search(r"en=(\d+)\s+logic=(\d+)\s+delay=(\d+)s\s+tentativi=(\d+)", value, re.IGNORECASE)
        if not match:
            return None, None, None, None
        return (
            match.group(1) == "1",
            int(match.group(2)),
            int(match.group(3)),
            int(match.group(4)),
        )

    @staticmethod
    def _parse_master_mode(value: str) -> tuple[int | None, str]:
        upper = value.upper()
        if "REWAMPING" in upper:
            return 2, "Rewamping"
        if "STANDALONE" in upper:
            return 1, "Standalone"
        return None, value.strip()

    @staticmethod
    def _parse_relay_mode(value: str) -> tuple[int | None, str]:
        upper = value.upper()
        mapping = {
            "LUCE": 1,
            "UVC": 2,
            "ELETTROSTATICO": 3,
            "GAS": 4,
            "COMANDO": 5,
        }
        for key, mode_code in mapping.items():
            if key in upper:
                return mode_code, key
        numeric = BoardConfigService._safe_int(value)
        if numeric is not None:
            for code, label in BoardConfigService.RELAY_MODE_OPTIONS:
                if code == numeric:
                    return code, label
        return None, value.strip()

    @staticmethod
    def _peripheral_mode_label(mode_code: int | None) -> str:
        for code, label in BoardConfigService.PERIPHERAL_MODE_OPTIONS:
            if code == mode_code:
                return label
        return ""

    @staticmethod
    def _board_label_for_kind(board_kind: str, product_type_code: str) -> str:
        if product_type_code == "01":
            return "Centralina Display"
        if product_type_code == "02":
            return "Centralina Standalone/Rewamping"
        if product_type_code == "03":
            return "Scheda Relay"
        if product_type_code == "04":
            return "Periferica Pressione/Temperatura"
        if board_kind == "master":
            return "Centralina"
        if board_kind == "relay":
            return "Scheda Relay"
        if board_kind == "pressure":
            return "Periferica"
        return "Scheda sconosciuta"

    @staticmethod
    def _is_master_serial(serial_number: str) -> bool:
        if not re.fullmatch(r"\d{12}", serial_number.strip()):
            return False
        return serial_number.strip()[6:8] in {"01", "02"}
