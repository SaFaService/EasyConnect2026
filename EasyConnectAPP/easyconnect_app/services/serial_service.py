from __future__ import annotations

import time
from typing import Iterable

import serial
import serial.tools.list_ports

from easyconnect_app.models import SerialPortInfo


class SerialConsole:
    _SERIAL_TIMEOUT = 0.15
    _OPEN_SETTLE_SEC = 0.2
    _NO_RESPONSE_BOOT_WAIT_SEC = 1.8
    _POST_BOOT_SETTLE_SEC = 0.25

    def list_ports(self) -> list[SerialPortInfo]:
        out: list[SerialPortInfo] = []
        for port in serial.tools.list_ports.comports():
            description = (port.description or "").strip()
            if "bluetooth" in description.lower():
                continue
            if not self._is_port_available(port.device):
                continue
            out.append(SerialPortInfo(device=port.device, description=description or "-"))
        return out

    def _is_port_available(self, device: str) -> bool:
        try:
            with self._open_serial(device, timeout=0.05):
                return True
        except Exception:
            return False

    def read_info(self, port: str, *, baudrate: int = 115200) -> str:
        return self.send_command(port, "INFO", baudrate=baudrate, read_timeout=2.5)

    def _open_serial(self, port: str, *, baudrate: int = 115200, timeout: float = _SERIAL_TIMEOUT) -> serial.Serial:
        # Keep DTR/RTS low before opening: ESP32 boards can reset on line toggles.
        ser = serial.Serial()
        ser.port = port
        ser.baudrate = baudrate
        ser.timeout = timeout
        ser.write_timeout = timeout
        ser.dtr = False
        ser.rts = False
        ser.open()
        return ser

    @staticmethod
    def _read_lines(ser: serial.Serial, *, read_timeout: float) -> list[str]:
        lines: list[str] = []
        deadline = time.monotonic() + read_timeout
        last_data = time.monotonic()
        while time.monotonic() < deadline:
            if ser.in_waiting > 0:
                line = ser.readline().decode("utf-8", errors="replace").strip()
                if line:
                    lines.append(line)
                    last_data = time.monotonic()
            elif lines and (time.monotonic() - last_data) > 0.35:
                break
            else:
                time.sleep(0.03)
        return lines

    def _send_and_read(self, ser: serial.Serial, command: str, *, read_timeout: float) -> list[str]:
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        ser.write((command.strip() + "\n").encode("utf-8"))
        ser.flush()
        lines = self._read_lines(ser, read_timeout=read_timeout)
        if lines:
            return lines

        # First command can be lost if board just rebooted on serial open.
        time.sleep(self._NO_RESPONSE_BOOT_WAIT_SEC)
        ser.reset_input_buffer()
        ser.write((command.strip() + "\n").encode("utf-8"))
        ser.flush()
        return self._read_lines(ser, read_timeout=read_timeout)

    def send_command(
        self,
        port: str,
        command: str,
        *,
        baudrate: int = 115200,
        read_timeout: float = 2.0,
    ) -> str:
        with self._open_serial(port, baudrate=baudrate, timeout=self._SERIAL_TIMEOUT) as ser:
            time.sleep(self._OPEN_SETTLE_SEC)
            lines = self._send_and_read(ser, command, read_timeout=read_timeout)

        if not lines:
            return "(Nessuna risposta seriale)"
        return "\n".join(lines)

    def run_batch(
        self,
        port: str,
        commands: Iterable[str],
        *,
        baudrate: int = 115200,
        read_timeout: float = 1.6,
    ) -> str:
        logs: list[str] = []
        with self._open_serial(port, baudrate=baudrate, timeout=self._SERIAL_TIMEOUT) as ser:
            time.sleep(self._OPEN_SETTLE_SEC)
            for idx, command in enumerate(commands):
                if idx > 0:
                    time.sleep(0.05)
                lines = self._send_and_read(ser, command, read_timeout=read_timeout)
                response = "\n".join(lines) if lines else "(Nessuna risposta seriale)"
                logs.append(f"> {command}\n{response}")
                # If the board rebooted after an early command, let it finish setup once.
                if not lines and idx == 0:
                    time.sleep(self._POST_BOOT_SETTLE_SEC)
        return "\n\n".join(logs)

    def configure_master(
        self,
        *,
        port: str,
        serial_number: str,
        mode: int,
        api_url: str,
        api_key: str,
        customer_api_url: str,
        customer_api_key: str,
    ) -> str:
        commands = [
            f"SETSERIAL {serial_number}",
            f"SETMODE {mode}",
        ]
        if api_url.strip():
            commands.append(f"SETAPIURL {api_url.strip()}")
        if api_key.strip():
            commands.append(f"SETAPIKEY {api_key.strip()}")
        if customer_api_url.strip():
            commands.append(f"SETCUSTURL {customer_api_url.strip()}")
        if customer_api_key.strip():
            commands.append(f"SETCUSTKEY {customer_api_key.strip()}")
        commands.append("INFO")
        return self.run_batch(port=port, commands=commands)

    def configure_slave(
        self,
        *,
        port: str,
        serial_number: str,
        rs485_ip: int,
        group: int,
        mode: int,
    ) -> str:
        commands = [
            f"SETIP {rs485_ip}",
            f"SETSERIAL {serial_number}",
            f"SETGROUP {group}",
            f"SETMODE {mode}",
            "INFO",
        ]
        return self.run_batch(port=port, commands=commands)
