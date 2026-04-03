from __future__ import annotations

from dataclasses import dataclass

from easyconnect_app.services.api_client import ApiClient, ApiError


@dataclass(slots=True)
class VersionInfo:
    latest_version: str
    download_url: str
    notes: str
    mandatory: bool


class VersionService:
    def __init__(self, api_client: ApiClient) -> None:
        self.api_client = api_client

    def check(self, current_version: str) -> VersionInfo | None:
        response = self.api_client.call_endpoint(
            "version_check",
            method="GET",
            params={"current_version": current_version},
        )
        if not isinstance(response, dict):
            return None
        latest = str(
            response.get("latest_version", response.get("version", ""))
        ).strip()
        if not latest:
            return None
        return VersionInfo(
            latest_version=latest,
            download_url=str(response.get("download_url", "")).strip(),
            notes=str(response.get("notes", "")).strip(),
            mandatory=bool(response.get("mandatory", False)),
        )

    @staticmethod
    def is_newer(latest: str, current: str) -> bool:
        def parse(v: str) -> list[int]:
            parts = []
            for token in v.strip().replace("-", ".").split("."):
                if token.isdigit():
                    parts.append(int(token))
                    continue
                num = ""
                for c in token:
                    if c.isdigit():
                        num += c
                    else:
                        break
                parts.append(int(num) if num else 0)
            return parts

        a = parse(latest)
        b = parse(current)
        max_len = max(len(a), len(b))
        a.extend([0] * (max_len - len(a)))
        b.extend([0] * (max_len - len(b)))
        return a > b

