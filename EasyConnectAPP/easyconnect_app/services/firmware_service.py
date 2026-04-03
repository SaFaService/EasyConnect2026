from __future__ import annotations

import subprocess
import sys
from pathlib import Path


class FirmwareService:
    def flash(
        self,
        *,
        port: str,
        firmware_path: str,
        chip: str = "esp32c3",
        baudrate: int = 460800,
    ) -> dict[str, str | bool]:
        firmware = Path(firmware_path)
        if not firmware.exists():
            raise FileNotFoundError(f"File firmware non trovato: {firmware}")

        cmd = [
            sys.executable,
            "-m",
            "esptool",
            "--chip",
            chip,
            "--port",
            port,
            "--baud",
            str(baudrate),
            "write_flash",
            "0x0",
            str(firmware),
        ]
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
        out = (proc.stdout or "") + ("\n" + proc.stderr if proc.stderr else "")
        return {"success": proc.returncode == 0, "output": out, "command": " ".join(cmd)}
