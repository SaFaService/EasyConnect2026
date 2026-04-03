from __future__ import annotations

from typing import Any

from easyconnect_app.services.api_client import ApiClient


class SerialProvisioningService:
    def __init__(self, api_client: ApiClient) -> None:
        self.api_client = api_client

    def reserve_next_serial(self, product_type_code: str) -> dict[str, Any]:
        if self.api_client.mock_mode:
            return {
                "status": "ok",
                "command": "reserve_next_serial",
                "serial_number": "202602" + product_type_code + "0099",
            }
        return self._serial_command(
            "reserve_next_serial",
            {"product_type_code": product_type_code},
        )

    def check_serial(self, serial_number: str, product_type_code: str) -> dict[str, Any]:
        if self.api_client.mock_mode:
            return {
                "status": "available",
                "assignable": True,
                "serial_number": serial_number,
                "product_type_code": product_type_code,
            }
        return self._serial_command(
            "check_serial",
            {"serial_number": serial_number, "product_type_code": product_type_code},
        )

    def assign_serial_to_master(self, serial_number: str, master_id: int) -> dict[str, Any]:
        if self.api_client.mock_mode:
            return {
                "status": "ok",
                "command": "assign_serial_to_master",
                "serial_number": serial_number,
                "master_id": master_id,
            }
        return self._serial_command(
            "assign_serial_to_master",
            {"serial_number": serial_number, "master_id": master_id},
        )

    def create_plant(self, name: str, address: str, serial_number: str) -> dict[str, Any]:
        if self.api_client.mock_mode:
            return {
                "status": "ok",
                "plant_id": 999,
                "name": name,
                "address": address,
                "serial_number": serial_number,
            }
        return self.api_client.call_endpoint(
            "create_plant",
            method="POST",
            payload={"name": name, "address": address, "serial_number": serial_number},
        )

    def _serial_command(self, command: str, extra: dict[str, Any]) -> dict[str, Any]:
        payload: dict[str, Any] = {"action": command}
        payload.update(extra)
        response = self.api_client.call_endpoint("serial", method="POST", payload=payload)
        return response if isinstance(response, dict) else {"raw": response}
