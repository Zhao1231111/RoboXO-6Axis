"""Game control endpoints — game session and FSM scaffold."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
import asyncio
import math
import random
from typing import Any

from fastapi import APIRouter, Request
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field

router = APIRouter(prefix="/api/game", tags=["game"])


class GameState(str, Enum):
    IDLE = "Idle"
    ALARM = "Alarm"
    GRABBING_PEN = "GrabbingPen"
    WAITING_HUMAN = "WaitingHuman"
    RECOGNIZING = "Recognizing"
    THINKING = "Thinking"
    ROBOT_EXECUTING = "RobotExecuting"
    CHECK_END = "CheckEnd"
    GAME_OVER = "GameOver"
    ERROR = "Error"

_NOT_IMPLEMENTED = JSONResponse(
    status_code=501,
    content={"error": "not_implemented", "message": "Game logic not yet implemented"},)

@dataclass
class GameSession:
    board: list[int] = field(default_factory=lambda: [0] * 9)
    turn: str = "human"
    difficulty: str = "hard"
    first_player: str = "human"
    state: GameState = GameState.IDLE
    robot_score: int = 0
    human_score: int = 0
    draw_count: int = 0
    waiting_vision: bool = False
    waiting_robot_action: bool = False
    robot_ready: bool = False
    robot_action_done: bool = False
    vision_ready: bool = False
    alarm_task: asyncio.Task[None] | None = None
    alarm_generation: int = 0
    pending_move: int = -1
    pending_reason: str = ""

    def reset_round(self) -> None:
        self.board = [0] * 9
        self.turn = self.first_player
        self.state = GameState.IDLE
        self.waiting_vision = False
        self.waiting_robot_action = False
        self.robot_ready = False
        self.robot_action_done = False
        self.vision_ready = False
        self.alarm_task = None
        self.alarm_generation += 1
        self.pending_move = -1
        self.pending_reason = ""

    def set_running(self, difficulty: str, first_player: str) -> None:
        self.difficulty = difficulty
        self.first_player = first_player
        self.turn = first_player
        self.state = GameState.IDLE
        self.waiting_vision = False
        self.waiting_robot_action = False
        self.robot_ready = False
        self.robot_action_done = False
        self.vision_ready = False
        self.alarm_task = None
        self.alarm_generation += 1
        self.pending_move = -1
        self.pending_reason = ""


WIN_LINES: tuple[tuple[int, int, int], ...] = (
    (0, 1, 2),
    (3, 4, 5),
    (6, 7, 8),
    (0, 3, 6),
    (1, 4, 7),
    (2, 5, 8),
    (0, 4, 8),
    (2, 4, 6),
)

ROBOT_PIECE = 2
HUMAN_PIECE = 1
EMPTY_PIECE = 0
ALARM_SECONDS = 3.0


def _get_session(request: Request) -> GameSession:
    app_state = request.app.state.app
    session = getattr(app_state, "game_session", None)
    if session is None:
        session = GameSession()
        app_state.game_session = session
    return session


def _normalize_board(board: list[int]) -> list[int]:
    if len(board) != 9:
        raise ValueError("board must contain exactly 9 cells")
    normalized = []
    for cell in board:
        if cell not in (0, 1, 2):
            raise ValueError("board cells must be 0, 1, or 2")
        normalized.append(int(cell))
    return normalized


def _winner(board: list[int]) -> int:
    for a, b, c in WIN_LINES:
        v = board[a]
        if v != EMPTY_PIECE and v == board[b] and v == board[c]:
            return v
    return EMPTY_PIECE


def _is_draw(board: list[int]) -> bool:
    return EMPTY_PIECE not in board and _winner(board) == EMPTY_PIECE


def _apply_state(session: GameSession, next_state: GameState) -> None:
    session.state = next_state
    session.waiting_vision = next_state == GameState.RECOGNIZING
    session.waiting_robot_action = next_state == GameState.ROBOT_EXECUTING


def _is_estop_active(request: Request) -> bool:
    app_state = request.app.state.app
    latest = getattr(app_state, "latest_status", None)
    return bool(latest is not None and getattr(latest, "safety", None) == 2)


def _cancel_alarm(session: GameSession) -> None:
    if session.alarm_task is not None and not session.alarm_task.done():
        session.alarm_task.cancel()
    session.alarm_task = None


async def _alarm_then_transition(request: Request, expected_state: GameState, next_state: GameState, note: str = "") -> None:
    session = _get_session(request)
    generation = session.alarm_generation
    await asyncio.sleep(ALARM_SECONDS)
    session_after_sleep = _get_session(request)
    if session_after_sleep.alarm_generation != generation:
        return
    if session_after_sleep.state != expected_state:
        return
    if _is_estop_active(request):
        return
    _apply_state(session_after_sleep, next_state)
    if note:
        print(f"[GameFSM] {next_state.value}: {note}")


def _schedule_alarm(request: Request, expected_state: GameState, next_state: GameState, note: str = "") -> None:
    session = _get_session(request)
    _cancel_alarm(session)
    session.alarm_generation += 1
    session.alarm_task = asyncio.create_task(_alarm_then_transition(request, expected_state, next_state, note))


def _terminal_state(board: list[int]) -> GameState | None:
    if _winner(board) != EMPTY_PIECE or _is_draw(board):
        return GameState.GAME_OVER
    return None


def _score_terminal(board: list[int], depth: int) -> int | None:
    winner = _winner(board)
    if winner == ROBOT_PIECE:
        return 10 - depth
    if winner == HUMAN_PIECE:
        return depth - 10
    if _is_draw(board):
        return 0
    return None


def _legal_moves(board: list[int]) -> list[int]:
    return [i for i, cell in enumerate(board) if cell == EMPTY_PIECE]


def _minimax_alpha_beta(
    board: list[int],
    depth: int,
    is_robot_turn: bool,
    alpha: int,
    beta: int,
    depth_limit: int | None,
) -> tuple[int, int | None]:
    terminal = _score_terminal(board, depth)
    if terminal is not None:
        return terminal, None
    if depth_limit is not None and depth >= depth_limit:
        preference = [4, 0, 2, 6, 8, 1, 3, 5, 7]
        for pos in preference:
            if board[pos] == EMPTY_PIECE:
                return 0, pos
        return 0, None

    moves = _legal_moves(board)
    if not moves:
        return 0, None

    best_move: int | None = None
    if is_robot_turn:
        best_score = -math.inf
        for move in moves:
            board[move] = ROBOT_PIECE
            score, _ = _minimax_alpha_beta(board, depth + 1, False, alpha, beta, depth_limit)
            board[move] = EMPTY_PIECE
            if score > best_score:
                best_score = score
                best_move = move
            alpha = max(alpha, score)
            if beta <= alpha:
                break
        return int(best_score), best_move

    best_score = math.inf
    for move in moves:
        board[move] = HUMAN_PIECE
        score, _ = _minimax_alpha_beta(board, depth + 1, True, alpha, beta, depth_limit)
        board[move] = EMPTY_PIECE
        if score < best_score:
            best_score = score
            best_move = move
        beta = min(beta, score)
        if beta <= alpha:
            break
    return int(best_score), best_move


def _choose_robot_move(board: list[int], difficulty: str) -> tuple[int, str]:
    legal = _legal_moves(board)
    if not legal:
        return -1, "no_legal_moves"
    if _winner(board) != EMPTY_PIECE or _is_draw(board):
        return -1, "game_over"
    difficulty = difficulty.lower()
    if difficulty == "easy":
        if random.random() < 0.6:
            return random.choice(legal), "easy_random"
        score, move = _minimax_alpha_beta(board[:], 0, True, -math.inf, math.inf, 2)
        return (move if move is not None else random.choice(legal)), f"easy_fallback_score_{score}"
    if difficulty == "medium":
        if random.random() < 0.3:
            return random.choice(legal), "medium_random"
        score, move = _minimax_alpha_beta(board[:], 0, True, -math.inf, math.inf, 4)
        return (move if move is not None else random.choice(legal)), f"medium_minimax_score_{score}"
    score, move = _minimax_alpha_beta(board[:], 0, True, -math.inf, math.inf, None)
    if move is not None:
        return move, f"hard_minimax_score_{score}"
    for pos in (4, 0, 2, 6, 8, 1, 3, 5, 7):
        if pos in legal:
            return pos, "hard_fallback"
    return random.choice(legal), "hard_random_fallback"


def _transition_to(request: Request, next_state: GameState, note: str = "") -> GameSession:
    session = _get_session(request)
    _apply_state(session, next_state)
    if note:
        print(f"[GameFSM] {session.state.value}: {note}")
    else:
        print(f"[GameFSM] {session.state.value}")
    return session


def _to_alarm(request: Request, next_after_alarm: GameState, note: str = "") -> GameSession:
    session = _get_session(request)
    _apply_state(session, GameState.ALARM)
    session.pending_reason = f"alarm->{next_after_alarm.value}"
    print(f"[GameFSM] Alarm: {note or session.pending_reason}")
    _schedule_alarm(request, GameState.ALARM, next_after_alarm, note)
    return session


def _sync_session_snapshot(session: GameSession) -> dict[str, Any]:
    return {
        "board": session.board,
        "turn": session.turn,
        "difficulty": session.difficulty,
        "first_player": session.first_player,
        "state": session.state.value,
        "robot_score": session.robot_score,
        "human_score": session.human_score,
        "draw_count": session.draw_count,
        "waiting_vision": session.waiting_vision,
        "waiting_robot_action": session.waiting_robot_action,
        "robot_ready": session.robot_ready,
        "robot_action_done": session.robot_action_done,
        "vision_ready": session.vision_ready,
        "pending_move": session.pending_move,
        "pending_reason": session.pending_reason,
    }


def _update_scores_on_terminal(session: GameSession) -> None:
    winner = _winner(session.board)
    if winner == HUMAN_PIECE:
        session.human_score += 1
    elif winner == ROBOT_PIECE:
        session.robot_score += 1
    else:
        session.draw_count += 1


class VisionBoardState(BaseModel):
    board: list[int] = Field(..., description="1D array of 9 ints. 0=empty, 1=X(player), 2=O(robot)", min_length=9, max_length=9)
    player_moved: bool = Field(False, description="Whether the player just made a move")
    timestamp: float | None = None
    difficulty: str = Field("hard", description="easy / medium / hard")
    first_player: str = Field("human", description="human / robot")


class StartGameRequest(BaseModel):
    difficulty: str = Field("hard", description="easy / medium / hard")
    first_player: str = Field("human", description="human / robot")


class FrontendSignalRequest(BaseModel):
    signal: str = Field(..., description="frontend signal, e.g. ready_for_recognizing")


@router.post("/start")
async def game_start(request: Request, payload: StartGameRequest) -> JSONResponse:
    session = _get_session(request)
    session.set_running(payload.difficulty, payload.first_player)
    _to_alarm(request, GameState.GRABBING_PEN, "game start -> grasp pen")
    # TaskID = 1 (GraspPen)
    request.app.state.app.ipc_client.send_task_command(1, 0, 0)
    return JSONResponse({"status": "success", "message": "Game session created and alarm scheduled for grabbing pen", "session": _sync_session_snapshot(session)})

@router.post("/drop_pen")
async def game_drop_pen(request: Request) -> JSONResponse:
    # TaskID = 5 (DropPen)
    request.app.state.app.ipc_client.send_task_command(5, 0, 0)
    return JSONResponse({"status": "success", "message": "Drop pen command sent"})


@router.get("/state")
def game_state() -> JSONResponse:
    return _NOT_IMPLEMENTED

@router.post("/reset")
async def game_reset(request: Request) -> JSONResponse:
    session = _get_session(request)
    session.reset_round()
    request.app.state.app.ipc_client.send_task_command(3, 0, 0)
    return JSONResponse({"status": "success", "message": "Game session reset and erase board command sent", "session": _sync_session_snapshot(session)})


@router.post("/signal")
async def frontend_signal(request: Request, payload: FrontendSignalRequest) -> JSONResponse:
    session = _get_session(request)
    if payload.signal == "ready_for_recognizing" and session.state == GameState.WAITING_HUMAN:
        _transition_to(request, GameState.RECOGNIZING, "frontend confirmed ready for vision")
    return JSONResponse({"status": "success", "session": _sync_session_snapshot(session)})


@router.post("/vision/board_state")
async def vision_board_state(request: Request, state: VisionBoardState) -> dict:
    session = _get_session(request)
    session.board = _normalize_board(state.board)
    session.difficulty = state.difficulty
    session.first_player = state.first_player
    session.vision_ready = True

    print(f"[Vision Debug] 收到视觉请求: player_moved={state.player_moved}, 当前网关状态={session.state.value}")

    if session.state == GameState.IDLE:
        _transition_to(request, GameState.WAITING_HUMAN, "idle -> waiting human")

    if state.player_moved and session.state in {GameState.WAITING_HUMAN, GameState.RECOGNIZING}:
        _transition_to(request, GameState.RECOGNIZING, "vision board received")
        if _terminal_state(session.board) == GameState.GAME_OVER:
            _transition_to(request, GameState.GAME_OVER, "board already terminal")
        else:
            _transition_to(request, GameState.THINKING, "human move accepted, AI thinking")
            print(f"[Vision Debug] 开始 AI 决策, 当前棋盘: {session.board}, 难度: {session.difficulty}")
            move, reason = _choose_robot_move(session.board, session.difficulty)
            print(f"[Vision Debug] AI 决策完成: 落子位置={move}, 原因={reason}")
            if move == -1:
                print("[Vision Debug] AI 找不到合法步，进入 ERROR 状态")
                _transition_to(request, GameState.ERROR, reason)
            else:
                session.pending_move = move
                session.pending_reason = reason
                session.turn = "robot"
                print(f"[Vision Debug] 即将下发 IPC 指令给 C++: send_task_command(2, {move}, 0)")
                _to_alarm(request, GameState.ROBOT_EXECUTING, f"pre-motion alarm before robot move={move}")
                request.app.state.app.ipc_client.send_task_command(2, move, 0)
                session.waiting_robot_action = True
                _transition_to(request, GameState.CHECK_END, "robot command sent")
    elif state.player_moved:
        print(f"[Vision Debug] 被拦截: 状态机处于 {session.state.value}，不在允许接收下棋的状态 ({GameState.WAITING_HUMAN.value} 或 {GameState.RECOGNIZING.value})")
        _transition_to(request, GameState.ERROR, f"unexpected player_moved in state={session.state.value}")
    else:
        print(f"[Vision Debug] 视觉端提示 player_moved=False，本次不触发机器人下棋动作")

    print(f"[Vision] state={session.state.value}, board={session.board}")
    return {"status": "success", "message": "Game session updated.", "session": _sync_session_snapshot(session), "winner": _winner(session.board), "draw": _is_draw(session.board)}


@router.post("/robot/action_done")
async def robot_action_done(request: Request) -> JSONResponse:
    session = _get_session(request)
    session.robot_action_done = True
    session.waiting_robot_action = False
    _transition_to(request, GameState.CHECK_END, "robot action completed")
    if _terminal_state(session.board) == GameState.GAME_OVER:
        _update_scores_on_terminal(session)
        _to_alarm(request, GameState.GAME_OVER, "game finished after robot action")
        _transition_to(request, GameState.IDLE, "game over -> idle")
    else:
        session.turn = "human"
        _transition_to(request, GameState.WAITING_HUMAN, "continue to waiting human")
    return JSONResponse({"status": "success", "session": _sync_session_snapshot(session)})


@router.post("/robot/pen_ready")
async def robot_pen_ready(request: Request) -> JSONResponse:
    session = _get_session(request)
    session.robot_ready = True
    _transition_to(request, GameState.WAITING_HUMAN, "robot pen grabbed")
    return JSONResponse({"status": "success", "session": _sync_session_snapshot(session)})