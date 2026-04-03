from __future__ import annotations

import getpass
import logging
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

try:
    from easyconnect_app.config import load_app_config  # noqa: E402
    from easyconnect_app.services.auth_service import AuthService  # noqa: E402
    from easyconnect_app.services.api_client import ApiClient, ApiError  # noqa: E402
except ModuleNotFoundError as exc:
    print(f"Dipendenza mancante: {exc}. Esegui prima 'pip install -r requirements.txt'.")
    raise SystemExit(3)


def setup_logging() -> None:
    logging.basicConfig(
        level=logging.DEBUG,
        format="%(asctime)s | %(levelname)s | %(name)s | %(message)s",
        handlers=[
            logging.StreamHandler(sys.stdout),
        ],
    )


def main() -> int:
    setup_logging()
    cfg = load_app_config()
    log = logging.getLogger("debug_auth_cli")
    log.info("Config loaded | base_url=%s | mock_mode=%s", cfg.base_url, cfg.mock_mode)

    email = input("Email: ").strip()
    password = getpass.getpass("Password: ")
    otp = input("OTP (invio se non presente): ").strip()

    api = ApiClient(cfg)
    auth = AuthService(api)
    try:
        session = auth.login(email, password, otp_code=otp)
        log.info("LOGIN OK | user=%s | role=%s | token=%s", session.username, session.role, bool(session.token))
        return 0
    except ApiError as exc:
        log.error("LOGIN FAIL | %s", exc)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
