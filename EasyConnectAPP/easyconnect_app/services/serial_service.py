from __future__ import annotations

import time
from typing import Iterable

import serial
import serial.tools.list_ports

from easyconnect_app.models import SerialPortInfo


class SerialConsole:
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
            with serial.Serial(port=device, timeout=0.05):
                return True
        except Exception:
            return False

    def read_info(self, port: str, *, baudrate: int = 115200) -> str:
        return self.send_command(port, "INFO", baudrate=baudrate, read_timeout=2.5)

    def send_command(
        self,
        port: str,
        command: str,
        *,
        baudrate: int = 115200,
        read_timeout: float = 2.0,
    ) -> str:
        lines: list[str] = []
        with serial.Serial(port=port, baudrate=baudrate, timeout=0.15) as ser:
            time.sleep(0.15)
            ser.reset_input_buffer()
            ser.reset_output_buffer()
            ser.write((command.strip() + "\n").encode("utf-8"))
            ser.flush()

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
        for command in commands:
            response = self.send_command(
                port,
                command,
                baudrate=baudrate,
                read_timeout=read_timeout,
            )
            logs.append(f"> {command}\n{response}")
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
