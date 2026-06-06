"""WebSocket endpoint for real-time state push and heartbeat monitoring."""

from __future__ import annotations

import asyncio
import json
import logging
import time

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from app.config import settings
from app.ipc.messages import MsgType, decode_status_report
from app.ipc.protocol import Frame
from app.state import AppState, RobotStatus

logger = logging.getLogger(__name__)

router = APIRouter()


def _build_ws_message(app_state: AppState) -> str:
    status = app_state.latest_status
    fsm = app_state.game_fsm

    joints = status.joints_deg if status else [0.0] * 6
    tcp = status.tcp_mm_deg if status else [0.0] * 6
    gripper = status.gripper if status else "open"
    io_state = status.io_state if status else 0
    safety_str = status.safety_str if status else "idle"

    snap = fsm.snapshot()

    return json.dumps({
        "joints_deg": joints,
        "tcp_mm_deg": tcp,
        "gripper": gripper,
        "io_state": io_state,
        "board": snap["board"],
        "phase": snap["phase"],
        "safety": safety_str,
        "alarm_countdown": snap["alarm_countdown"],
        "game_result": snap["game_result"],
        "score": snap["score"],
    })


@router.websocket("/ws/state")
async def ws_state(websocket: WebSocket) -> None:
    app_state: AppState = websocket.app.state.app
    await websocket.accept()
    app_state.ws_clients.add(websocket)
    client_host = websocket.client.host if websocket.client else "unknown"
    logger.info("WebSocket client connected: %s (total=%d)", client_host, len(app_state.ws_clients))
    last_heartbeat = time.time()

    try:
        while True:
            try:
                raw = await asyncio.wait_for(websocket.receive_text(), timeout=1.0)
                msg = json.loads(raw)
                if msg.get("type") == "heartbeat":
                    last_heartbeat = time.time()
                    logger.debug("WebSocket heartbeat from %s", client_host)
            except asyncio.TimeoutError:
                pass

            if time.time() - last_heartbeat > settings.ws_heartbeat_timeout:
                logger.warning(
                    "WebSocket heartbeat timeout from %s (%.1fs), sending EStop",
                    client_host, time.time() - last_heartbeat,
                )
                if app_state.ipc_connected:
                    try:
                        app_state.ipc_client.send_frame(MsgType.ESTOP)
                    except RuntimeError:
                        pass
                break
    except WebSocketDisconnect:
        logger.info("WebSocket client disconnected: %s", client_host)
    finally:
        app_state.ws_clients.discard(websocket)
        logger.info("WebSocket clients remaining: %d", len(app_state.ws_clients))


async def broadcast_loop(app_state: AppState, queue: asyncio.Queue[Frame]) -> None:
    """Consume frames from the IPC reader and broadcast StatusReports via WebSocket."""
    logger.info("Broadcast loop started, waiting for IPC frames")
    frame_count = 0
    prev_safety: int | None = None

    while True:
        frame = await queue.get()
        frame_count += 1

        if frame.msg_type == MsgType.STATUS_REPORT:
            report = decode_status_report(frame.payload)
            app_state.latest_status = RobotStatus(
                joints_deg=report.joints_deg,
                tcp_mm_deg=report.tcp_mm_deg,
                io_state=report.io_state,
                safety=report.safety,
            )

            # Detect safety transitions and notify FSM
            if prev_safety is not None and report.safety != prev_safety:
                try:
                    await app_state.game_fsm.on_safety_changed(report.safety)
                except Exception:
                    logger.exception("FSM on_safety_changed failed")
            prev_safety = report.safety

            if frame_count == 1:
                logger.info("First StatusReport received from C++ RT")
            if not app_state.ws_clients:
                continue

            msg = _build_ws_message(app_state)
            stale: list[WebSocket] = []
            for ws in app_state.ws_clients.copy():
                try:
                    await ws.send_text(msg)
                except Exception:
                    stale.append(ws)
            for ws in stale:
                app_state.ws_clients.discard(ws)
                logger.warning("Removed stale WebSocket client")
        elif frame.msg_type == MsgType.ACK:
            logger.debug("IPC Ack received (seq=%d)", frame.seq)
        elif frame.msg_type == MsgType.ERROR:
            from app.ipc.messages import decode_error

            err = decode_error(frame.payload)
            logger.error("IPC Error (code=%d): %s", err.code, err.message)
        else:
            logger.warning("Unexpected IPC frame type: 0x%02X", frame.msg_type)
