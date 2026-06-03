"""Global application state shared across routers and WebSocket handlers."""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from typing import TYPE_CHECKING

from starlette.websockets import WebSocket

if TYPE_CHECKING:
    from app.ipc.client import IPCClient


_GRIPPER_OPEN_BIT = 14
_GRIPPER_CLOSE_BIT = 15

SAFETY_MAP = {0: "braked", 1: "moving", 2: "estop"}


@dataclass(slots=True)
class RobotStatus:
    joints_deg: list[float]
    tcp_mm_deg: list[float]
    io_state: int
    safety: int
    timestamp: float = field(default_factory=time.time)

    @property
    def gripper(self) -> str:
        open_set = bool(self.io_state & (1 << _GRIPPER_OPEN_BIT))
        close_set = bool(self.io_state & (1 << _GRIPPER_CLOSE_BIT))
        if close_set and not open_set:
            return "closed"
        return "open"

    @property
    def safety_str(self) -> str:
        return SAFETY_MAP.get(self.safety, "unknown")


class AppState:
    """Singleton holding runtime state for the entire application."""

    def __init__(self, ipc_client: IPCClient) -> None:
        self.ipc_client = ipc_client
        self.latest_status: RobotStatus | None = None
        self.ws_clients: set[WebSocket] = set()
        self.start_time: float = time.time()

    @property
    def ipc_connected(self) -> bool:
        return self.ipc_client.connected

    @property
    def uptime_s(self) -> int:
        return int(time.time() - self.start_time)
