from __future__ import annotations

from easyconnect_app.models import UserPermissions, UserProfile
from easyconnect_app.services.api_client import ApiClient, ApiError


class ProfileService:
    def __init__(self, api_client: ApiClient) -> None:
        self.api_client = api_client

    def get_profile(self) -> UserProfile:
        if self.api_client.mock_mode:
            return UserProfile(
                email="admin@example.com",
                role="admin",
                phone="",
                whatsapp="",
                telegram="",
                company="Antralux",
                name="Utente Demo",
                has_2fa=True,
                permissions=UserPermissions(
                    firmware_update=True,
                    plant_create=True,
                    serial_lifecycle=True,
                    serial_reserve=True,
                    manual_peripheral=True,
                ),
            )

        response = self.api_client.call_endpoint("profile_api", method="GET")
        if not isinstance(response, dict):
            raise ApiError("Formato profilo non valido.")
        profile = response.get("profile", response)
        if not isinstance(profile, dict):
            raise ApiError("Formato profilo non valido.")
        return UserProfile.from_dict(profile)

    def update_profile(
        self,
        *,
        phone: str,
        whatsapp: str,
        telegram: str,
        company: str,
        name: str,
    ) -> UserProfile:
        payload = {
            "action": "update_profile",
            "phone": phone,
            "whatsapp": whatsapp,
            "telegram": telegram,
            "company": company,
            "name": name,
        }
        response = self.api_client.call_endpoint(
            "profile_api",
            method="POST",
            payload=payload,
        )
        if not isinstance(response, dict):
            raise ApiError("Formato aggiornamento profilo non valido.")
        profile = response.get("profile", response)
        if not isinstance(profile, dict):
            raise ApiError("Formato aggiornamento profilo non valido.")
        return UserProfile.from_dict(profile)
