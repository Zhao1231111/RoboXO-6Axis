"""Game control endpoints — stub (501 Not Implemented) until state machine is added."""

from __future__ import annotations

from fastapi import APIRouter
from fastapi.responses import JSONResponse

router = APIRouter(prefix="/api/game", tags=["game"])

_NOT_IMPLEMENTED = JSONResponse(
    status_code=501,
    content={"error": "not_implemented", "message": "Game logic not yet implemented"},
)


@router.post("/start")
def game_start() -> JSONResponse:
    return _NOT_IMPLEMENTED


@router.get("/state")
def game_state() -> JSONResponse:
    return _NOT_IMPLEMENTED


@router.post("/reset")
def game_reset() -> JSONResponse:
    return _NOT_IMPLEMENTED
