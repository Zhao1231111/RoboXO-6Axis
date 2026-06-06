"""Game control REST endpoints — thin layer delegating to GameFSM."""

from __future__ import annotations

from fastapi import APIRouter, Request
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field

from app.game_fsm import GameFSM

router = APIRouter(prefix="/api/game", tags=["game"])


def _get_fsm(request: Request) -> GameFSM:
    return request.app.state.app.game_fsm


class StartGameRequest(BaseModel):
    difficulty: str = Field("hard", description="easy / medium / hard")
    first_player: str = Field("human", description="human / robot")


class ManualDrawRequest(BaseModel):
    shape: str = Field(..., description="x or o")
    position: int = Field(..., ge=0, le=8, description="Cell index 0-8")


@router.post("/start")
async def game_start(request: Request, payload: StartGameRequest) -> JSONResponse:
    fsm = _get_fsm(request)
    await fsm.on_start(payload.difficulty, payload.first_player)
    return JSONResponse({"status": "ok", "session": fsm.snapshot()})


@router.post("/proceed")
async def game_proceed(request: Request) -> JSONResponse:
    fsm = _get_fsm(request)
    await fsm.on_proceed()
    return JSONResponse({"status": "ok", "session": fsm.snapshot()})


@router.get("/state")
def game_state(request: Request) -> JSONResponse:
    fsm = _get_fsm(request)
    return JSONResponse({"status": "ok", "session": fsm.snapshot()})


@router.post("/reset")
async def game_reset(request: Request) -> JSONResponse:
    fsm = _get_fsm(request)
    await fsm.on_reset()
    return JSONResponse({"status": "ok", "session": fsm.snapshot()})


@router.post("/manual_draw")
async def manual_draw(request: Request, payload: ManualDrawRequest) -> JSONResponse:
    app_state = request.app.state.app
    task_id = 4 if payload.shape.lower() == "x" else 2
    app_state.ipc_client.send_task_command(task_id, payload.position, 0)
    return JSONResponse({
        "status": "ok",
        "message": f"Sent command to draw {payload.shape.upper()} at position {payload.position}",
    })


@router.post("/drop_pen")
async def game_drop_pen(request: Request) -> JSONResponse:
    app_state = request.app.state.app
    app_state.ipc_client.send_task_command(5, 0, 0)
    return JSONResponse({"status": "ok", "message": "Drop pen command sent"})
