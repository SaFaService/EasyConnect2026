from __future__ import annotations

from dataclasses import dataclass


@dataclass(slots=True)
class UserPermissions:
    firmware_update: bool = False
    plant_create: bool = False
    serial_lifecycle: bool = False
    serial_reserve: bool = False
    manual_peripheral: bool = False

    @classmethod
    def from_dict(cls, data: dict | None) -> "UserPermissions":
        data = data or {}
        return cls(
            firmware_update=bool(data.get("firmware_update", data.get("can_firmware_update", False))),
            plant_create=bool(data.get("plant_create", data.get("can_create_plants", False))),
            serial_lifecycle=bool(data.get("serial_lifecycle", data.get("can_manage_serial_lifecycle", False))),
            serial_reserve=bool(data.get("serial_reserve", data.get("can_reserve_serials", False))),
            manual_peripheral=bool(data.get("manual_peripheral", data.get("can_assign_manual_peripherals", False))),
        )


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
    rs485_ip: str
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
            rs485_ip=str(data.get("rs485_ip", data.get("slave_id", "-"))),
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
    permissions: UserPermissions

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
            permissions=UserPermissions.from_dict(data.get("permissions")),
        )


@dataclass(slots=True)
class BoardSnapshot:
    board_kind: str
    product_type_code: str
    board_label: str
    serial_number: str
    firmware_version: str
    rs485_ip: int | None
    group: int | None
    mode_code: int | None
    mode_label: str
    api_url: str
    api_key_state: str
    customer_api_url: str
    customer_api_key_state: str
    feedback_enabled: bool | None
    feedback_logic: int | None
    feedback_delay_sec: int | None
    feedback_attempts: int | None
    feedback_message: str
    safety_message: str
    raw_info: str

    @property
    def is_master(self) -> bool:
        return self.product_type_code in {"01", "02"} or self.board_kind == "master"

    @property
    def is_peripheral(self) -> bool:
        return self.product_type_code in {"03", "04", "05"} or self.board_kind in {"relay", "pressure"}
