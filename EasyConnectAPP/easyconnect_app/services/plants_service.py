from __future__ import annotations

from typing import Any

from easyconnect_app.models import PartyInfo, PeripheralInfo, Plant, PlantDetail
from easyconnect_app.services.api_client import ApiClient, ApiError


class PlantsService:
    def __init__(self, api_client: ApiClient) -> None:
        self.api_client = api_client
        self._plants_by_id: dict[int, Plant] = {}

    def list_assigned_plants(self) -> list[Plant]:
        if self.api_client.mock_mode:
            return [
                Plant(
                    plant_id=101,
                    name="Clinica Nord",
                    address="Via Roma 10, Milano",
                    serial_number="202602020001",
                    online=True,
                    firmware_version="2.4.1",
                    rssi="-54",
                    updated_at="2026-02-20 09:41",
                ),
                Plant(
                    plant_id=102,
                    name="Laboratorio Sud",
                    address="Via Napoli 33, Bari",
                    serial_number="202602020004",
                    online=False,
                    firmware_version="2.3.9",
                    rssi="-",
                    updated_at="2026-02-20 08:13",
                ),
            ]

        response = self.api_client.call_endpoint("plants", method="GET")
        rows = self._extract_list(response, possible_keys=("plants", "data", "items"))
        plants = [Plant.from_dict(row) for row in rows]
        self._plants_by_id = {p.plant_id: p for p in plants}
        return plants

    def get_plant_detail(self, plant_id: int) -> PlantDetail:
        if plant_id <= 0:
            raise ApiError("ID impianto non valido.")

        if self.api_client.mock_mode:
            sample = Plant(
                plant_id=plant_id,
                name="Clinica Nord",
                address="Via Roma 10, Milano",
                serial_number="202602020001",
                online=True,
                firmware_version="2.4.1",
                rssi="-54",
                updated_at="2026-02-20 09:41",
            )
            return PlantDetail(
                plant=sample,
                owner=PartyInfo(name="Cliente Demo", email="cliente@example.com", phone="+39 333 0000000", company="Cliente Srl"),
                maintainer=PartyInfo(name="Manutentore Demo", email="maintainer@example.com", phone="+39 333 1111111", company="Service Srl"),
                builder=PartyInfo(name="Costruttore Demo", email="builder@example.com", phone="+39 333 2222222", company="Builder Srl"),
                creator=PartyInfo(name="Admin Demo", email="admin@example.com", phone="-", company="Antralux"),
                peripherals=[
                    PeripheralInfo(
                        serial_number="202602040145",
                        board_type="pressure_temperature",
                        board_label="Pressione/Temperatura",
                        rs485_ip="12",
                        group="1",
                        mode="Aspirazione",
                        firmware_version="1.4.2",
                        pressure="142",
                        temperature="23.1",
                        humidity="-",
                        last_seen="2026-02-20 09:41",
                        online=True,
                    ),
                    PeripheralInfo(
                        serial_number="202602040146",
                        board_type="uvc",
                        board_label="Filtro UVC",
                        rs485_ip="13",
                        group="2",
                        mode="UVC",
                        firmware_version="1.1.0",
                        pressure="-",
                        temperature="-",
                        humidity="-",
                        last_seen="2026-02-20 09:34",
                        online=False,
                    ),
                ],
            )

        try:
            response = self.api_client.call_endpoint(
                "plant_detail",
                method="GET",
                params={"plant_id": plant_id},
            )
            if not isinstance(response, dict):
                raise ApiError("Formato dettaglio impianto non valido.")
            return PlantDetail.from_dict(response)
        except ApiError:
            fallback = self._build_detail_fallback(plant_id)
            if fallback is not None:
                return fallback
            raise

    @staticmethod
    def _extract_list(
        payload: dict[str, Any] | list[Any],
        *,
        possible_keys: tuple[str, ...],
    ) -> list[dict[str, Any]]:
        if isinstance(payload, list):
            return [row for row in payload if isinstance(row, dict)]
        if isinstance(payload, dict):
            for key in possible_keys:
                value = payload.get(key)
                if isinstance(value, list):
                    return [row for row in value if isinstance(row, dict)]
        raise ApiError("Formato lista impianti non valido.")

    def _build_detail_fallback(self, plant_id: int) -> PlantDetail | None:
        plant = self._plants_by_id.get(plant_id)
        if not plant:
            return None

        owner = PartyInfo(
            name="-",
            email=plant.owner_email or "-",
            phone="-",
            company=plant.owner_company or "-",
        )
        maintainer = PartyInfo(
            name="-",
            email=plant.maintainer_email or "-",
            phone="-",
            company=plant.maintainer_company or "-",
        )
        builder = PartyInfo(
            name="-",
            email=plant.builder_email or "-",
            phone="-",
            company=plant.builder_company or "-",
        )
        creator = PartyInfo(name="-", email="-", phone="-", company="-")

        peripherals: list[PeripheralInfo] = []
        try:
            response = self.api_client.call_endpoint(
                "serial_overview",
                method="POST",
                payload={"action": "overview"},
            )
            rows = self._extract_list(response, possible_keys=("serials", "data", "items"))
            for row in rows:
                assigned = int(row.get("assigned_master_id") or 0)
                if assigned != plant_id:
                    continue
                sn = str(row.get("serial_number", "")).strip()
                if not sn or sn == plant.serial_number:
                    continue
                pcode = str(row.get("product_type_code", "")).strip()
                status = str(row.get("status", "")).strip().lower()
                peripherals.append(
                    PeripheralInfo(
                        serial_number=sn,
                        board_type=pcode or "unknown",
                        board_label=self._label_for_product_type(pcode),
                        rs485_ip="-",
                        group="-",
                        mode="-",
                        firmware_version="-",
                        pressure="-",
                        temperature="-",
                        humidity="-",
                        last_seen=str(row.get("created_at", "-")),
                        online=status == "active",
                    )
                )
        except ApiError:
            pass

        return PlantDetail(
            plant=plant,
            owner=owner,
            maintainer=maintainer,
            builder=builder,
            creator=creator,
            peripherals=peripherals,
            can_edit=False,
        )

    def update_plant(self, plant_id: int, *, name: str, address: str) -> Plant:
        if plant_id <= 0:
            raise ApiError("ID impianto non valido.")
        payload = {
            "plant_id": plant_id,
            "name": name.strip(),
            "address": address.strip(),
        }
        if self.api_client.mock_mode:
            current = self._plants_by_id.get(plant_id)
            if current is None:
                raise ApiError("Impianto non trovato.")
            updated = Plant(
                plant_id=current.plant_id,
                name=payload["name"] or current.name,
                address=payload["address"] or current.address,
                serial_number=current.serial_number,
                online=current.online,
                firmware_version=current.firmware_version,
                rssi=current.rssi,
                updated_at=current.updated_at,
                owner_email=current.owner_email,
                owner_company=current.owner_company,
                maintainer_email=current.maintainer_email,
                maintainer_company=current.maintainer_company,
                builder_email=current.builder_email,
                builder_company=current.builder_company,
            )
            self._plants_by_id[plant_id] = updated
            return updated

        response = self.api_client.call_endpoint(
            "plant_update",
            method="POST",
            payload=payload,
        )
        if not isinstance(response, dict):
            raise ApiError("Formato risposta aggiornamento impianto non valido.")
        plant_obj = response.get("plant", response)
        if not isinstance(plant_obj, dict):
            raise ApiError("Formato dati impianto aggiornato non valido.")
        updated = Plant.from_dict(plant_obj)
        self._plants_by_id[updated.plant_id] = updated
        return updated

    @staticmethod
    def _label_for_product_type(code: str) -> str:
        code_n = code.strip().lower()
        if code_n in {"04", "slave_pressure"}:
            return "Pressione/Temperatura"
        if code_n in {"03", "slave_relay"}:
            return "Relay"
        if code_n in {"02", "master"}:
            return "Master"
        if code_n in {"01"}:
            return "Elettrovalvola"
        return "Periferica"
