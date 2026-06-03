from __future__ import annotations

import os
from dataclasses import dataclass


@dataclass(frozen=True)
class Settings:
    ipc_socket_path: str = os.environ.get("ROBOXO_IPC_PATH", "/tmp/roboxo_rt.sock")
    ws_heartbeat_timeout: float = float(
        os.environ.get("ROBOXO_WS_HEARTBEAT_TIMEOUT", "10")
    )
    host: str = os.environ.get("ROBOXO_HOST", "0.0.0.0")
    port: int = int(os.environ.get("ROBOXO_PORT", "8000"))


settings = Settings()
