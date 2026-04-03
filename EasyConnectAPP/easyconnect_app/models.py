from __future__ import annotations

from dataclasses import dataclass


@dataclass(slots=True)
class UserSession:
    username: str
    role: str
    token: str = ""


@dataclass(slots=True)
class Plant:
    plant_id: int
    name: str
    address: str
    serial_number: str
    online: bool
    firmware_version: str
    rssi: str
    updated_at: str
    owner_email: str = ""
    owner_company: str = ""
    maintainer_email: str = ""
    maintainer_company: str = ""
    builder_email: str = ""
    builder_company: str = ""

    @classmethod
    def from_dict(cls, data: dict) -> "Plant":
        return cls(
            plant_id=int(data.get("id", data.get("plant_id", 0))),
            name=str(data.get("name", data.get("nickname", "-"))),
            address=str(data.get("address", "-")),
            serial_number=str(data.get("serial_number", data.get("master_sn", "-"))),
            online=bool(data.get("online", False)),
            firmware_version=str(data.get("firmware_version", data.get("fw_ver", "-"))),
            rssi=str(data.get("rssi", "-")),
            updated_at=str(data.get("updated_at", "-")),
            owner_email=str(data.get("owner_email", "")),
            owner_company=str(data.get("owner_company", "")),
            maintainer_email=str(data.get("maintainer_email", "")),
            maintainer_company=str(data.get("maintainer_company", "")),
            builder_email=str(data.get("builder_email", "")),
            builder_company=str(data.get("builder_company", "")),
        )


@dataclass(slots=True)
class SerialPortInfo:
    device: str
    description: str


@dataclass(slots=True)
class PartyInfo:
    name: str
    email: str
    phone: str
    company: str

    @classmethod
    def from_dict(cls, data: dict | None) -> "PartyInfo":
        data = data or {}
        return cls(
            name=str(data.get("name", "-")),
            email=str(data.get("email", "-")),
            phone=str(data.get("phone", "-")),
            company=str(data.get("company", "-")),
        )


@dataclass(slots=True)
class PeripheralInfo:
    serial_number: str
    board_type: str
    board_label: str
    group: str
    mode: str
    firmware_version: str
    pressure: str
    temperature: str
    humidity: str
    last_seen: str
    online: bool

    @classmethod
    def from_dict(cls, data: dict) -> "PeripheralInfo":
        return cls(
            serial_number=str(data.get("serial_number", data.get("slave_sn", "-"))),
            board_type=str(data.get("board_type", data.get("product_type_code", "-"))),
            board_label=str(data.get("board_label", data.get("type_label", "-"))),
            group=str(data.get("group", data.get("slave_grp", "-"))),
            mode=str(data.get("mode", "-")),
            firmware_version=str(data.get("firmware_version", data.get("fw_version", "-"))),
            pressure=str(data.get("pressure", "-")),
            temperature=str(data.get("temperature", "-")),
            humidity=str(data.get("humidity", "-")),
            last_seen=str(data.get("last_seen", data.get("recorded_at", "-"))),
            online=bool(data.get("online", False)),
        )


@dataclass(slots=True)
class PlantDetail:
    plant: Plant
    owner: PartyInfo
    maintainer: PartyInfo
    builder: PartyInfo
    creator: PartyInfo
    peripherals: list[PeripheralInfo]
    can_edit: bool = False

    @classmethod
    def from_dict(cls, data: dict) -> "PlantDetail":
        plant = Plant.from_dict(data.get("plant", data))
        peripherals_raw = data.get("peripherals", [])
        peripherals = [
            PeripheralInfo.from_dict(row)
            for row in peripherals_raw
            if isinstance(row, dict)
        ]
        return cls(
            plant=plant,
            owner=PartyInfo.from_dict(data.get("owner")),
            maintainer=PartyInfo.from_dict(data.get("maintainer")),
            builder=PartyInfo.from_dict(data.get("builder")),
            creator=PartyInfo.from_dict(data.get("creator")),
            peripherals=peripherals,
            can_edit=bool(data.get("can_edit", False)),
        )


@dataclass(slots=True)
class UserProfile:
    email: str
    role: str
    phone: str
    whatsapp: str
    telegram: str
    company: str
    name: str
    has_2fa: bool

    @classmethod
    def from_dict(cls, data: dict) -> "UserProfile":
        return cls(
            email=str(data.get("email", "")),
            role=str(data.get("role", "")),
            phone=str(data.get("phone", "")),
            whatsapp=str(data.get("whatsapp", "")),
            telegram=str(data.get("telegram", "")),
            company=str(data.get("company", "")),
            name=str(data.get("name", "")),
            has_2fa=bool(data.get("has_2fa", False)),
        )
