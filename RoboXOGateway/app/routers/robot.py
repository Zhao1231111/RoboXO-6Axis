"""Robot control endpoints: jog, jog/stop, estop, state."""

from __future__ import annotations

from fastapi import APIRouter, HTTPException, Request, Response, status

from app.ipc.messages import MsgType, encode_io_set, encode_jog
from app.models import IOSetRequest, JogRequest, RobotStateResponse

router = APIRouter(prefix="/api/robot", tags=["robot"])


def _get_state(request: Request):
    return request.app.state.app


def _require_ipc(request: Request) -> None:
    app_state = _get_state(request)
    if not app_state.ipc_connected:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail={"error": "ipc_disconnected", "message": "IPC not connected to RT"},
        )


@router.post("/jog", status_code=status.HTTP_204_NO_CONTENT)
def jog(request: Request, body: JogRequest) -> None:
    _require_ipc(request)
    app_state = _get_state(request)
    mode = 0 if body.mode == "joint" else 1
    payload = encode_jog(mode, body.axis, body.direction, body.speed_ratio, body.expires_ms)
    app_state.ipc_client.send_frame(MsgType.JOG, payload)


@router.post("/jog/stop", status_code=status.HTTP_204_NO_CONTENT)
def jog_stop(request: Request) -> None:
    _require_ipc(request)
    app_state = _get_state(request)
    app_state.ipc_client.send_frame(MsgType.JOG_STOP)


@router.post("/io", status_code=status.HTTP_204_NO_CONTENT)
def io_set(request: Request, body: IOSetRequest) -> None:
    _require_ipc(request)
    app_state = _get_state(request)
    payload = encode_io_set(body.pin, body.value)
    app_state.ipc_client.send_frame(MsgType.IO_SET, payload)


@router.post("/estop", status_code=status.HTTP_204_NO_CONTENT)
def estop(request: Request) -> None:
    _require_ipc(request)
    app_state = _get_state(request)
    app_state.ipc_client.send_frame(MsgType.ESTOP)


@router.post("/estop/reset", status_code=status.HTTP_204_NO_CONTENT)
def estop_reset(request: Request) -> None:
    _require_ipc(request)
    app_state = _get_state(request)
    app_state.ipc_client.send_frame(MsgType.ESTOP_RESET)


@router.get("/state")
def get_state(request: Request) -> RobotStateResponse:
    _require_ipc(request)
    app_state = _get_state(request)
    s = app_state.latest_status
    if s is None:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail={"error": "no_data", "message": "No status report received yet"},
        )
    return RobotStateResponse(
        joints_deg=s.joints_deg,
        tcp_mm_deg=s.tcp_mm_deg,
        gripper=s.gripper,
        io_state=s.io_state,
    )
