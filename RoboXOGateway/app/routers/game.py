"""Game control endpoints — stub (501 Not Implemented) until state machine is added."""

from __future__ import annotations

from fastapi import APIRouter, Request
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field

router = APIRouter(prefix="/api/game", tags=["game"])

_NOT_IMPLEMENTED = JSONResponse(
    status_code=501,
    content={"error": "not_implemented", "message": "Game logic not yet implemented"},
)


@router.post("/start")
async def game_start(request: Request) -> JSONResponse:
    # TaskID = 1 (GraspPen)
    request.app.state.app.ipc_client.send_task_command(1, 0, 0)
    return JSONResponse({"status": "success", "message": "Grasp pen command sent"})


@router.get("/state")
def game_state() -> JSONResponse:
    return _NOT_IMPLEMENTED


@router.post("/reset")
async def game_reset(request: Request) -> JSONResponse:
    # TaskID = 3 (EraseBoard)
    request.app.state.app.ipc_client.send_task_command(3, 0, 0)
    return JSONResponse({"status": "success", "message": "Erase board command sent"})


class VisionBoardState(BaseModel):
    board: list[int] = Field(
        ..., 
        description="1D array of 9 ints. 0=empty, 1=X(player), 2=O(robot)",
        min_length=9, 
        max_length=9
    )
    player_moved: bool = Field(False, description="Whether the player just made a move")
    timestamp: float | None = None


@router.post("/vision/board_state")
async def vision_board_state(request: Request, state: VisionBoardState) -> dict:
    """
    Endpoint for the Vision PC to send the current board state.
    """
    # 占位：目前暂不实现井字棋 AI 决策逻辑，仅打印接收到的状态
    print(f"[Vision] 接收到棋盘状态: {state.board}")
    
    if state.player_moved:
        print("[Vision] 检测到玩家落子，准备开始决策...")
        
        # 占位决策逻辑：寻找第一个空位下子
        target_cell = -1
        for i, val in enumerate(state.board):
            if val == 0:
                target_cell = i
                break
        
        if target_cell != -1:
            print(f"[AI Decision] 决定在格子 {target_cell} 画 O")
            # 真实调用：触发底层 IPC，假设 task_id = 2 代表 DrawO
            request.app.state.app.ipc_client.send_task_command(2, target_cell, 0)
        else:
            print("[AI Decision] 棋盘已满或游戏结束")
            
    return {
        "status": "success",
        "message": "Board state received, decision pending.",
        "is_my_turn": state.player_moved
    }
