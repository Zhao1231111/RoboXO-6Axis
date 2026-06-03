"""System management endpoints."""

from __future__ import annotations

from fastapi import APIRouter, Request

from app.models import SystemStatusResponse

router = APIRouter(prefix="/api/system", tags=["system"])


@router.get("/status")
def system_status(request: Request) -> SystemStatusResponse:
    app_state = request.app.state.app
    return SystemStatusResponse(
        ipc=app_state.ipc_connected,
        uptime_s=app_state.uptime_s,
    )
