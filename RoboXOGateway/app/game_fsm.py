"""Game state machine for the tic-tac-toe robot."""

from __future__ import annotations

import asyncio
import logging
import math
import random
import time
from enum import Enum
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from app.ipc.client import IPCClient

logger = logging.getLogger(__name__)

ALARM_SECONDS = 3.0

# Board constants
EMPTY = 0
HUMAN_PIECE = 1
ROBOT_PIECE = 2

WIN_LINES: tuple[tuple[int, int, int], ...] = (
    (0, 1, 2), (3, 4, 5), (6, 7, 8),
    (0, 3, 6), (1, 4, 7), (2, 5, 8),
    (0, 4, 8), (2, 4, 6),
)


class Phase(str, Enum):
    IDLE = "idle"
    ALARMING = "alarming"
    GRABBING_PEN = "grabbing_pen"
    WAITING_HUMAN = "waiting_human"
    RECOGNIZING = "recognizing"
    THINKING = "thinking"
    DRAWING_CHESS = "drawing_chess"
    CHECK_END = "check_end"
    GAME_OVER = "game_over"
    DROPPING_PEN = "dropping_pen"
    ERASING = "erasing"


# Task IDs matching RT
TASK_GRASP_PEN = 1
TASK_DRAW_O = 2
TASK_ERASE_BOARD = 3
TASK_DRAW_X = 4
TASK_DROP_PEN = 5


def _winner(board: list[int]) -> int:
    for a, b, c in WIN_LINES:
        v = board[a]
        if v != EMPTY and v == board[b] == board[c]:
            return v
    return EMPTY


def _is_draw(board: list[int]) -> bool:
    return EMPTY not in board and _winner(board) == EMPTY


def _legal_moves(board: list[int]) -> list[int]:
    return [i for i, v in enumerate(board) if v == EMPTY]


def _minimax(
    board: list[int], depth: int, is_robot: bool,
    alpha: int, beta: int, depth_limit: int | None,
) -> tuple[int, int | None]:
    w = _winner(board)
    if w == ROBOT_PIECE:
        return 10 - depth, None
    if w == HUMAN_PIECE:
        return depth - 10, None
    if _is_draw(board):
        return 0, None
    if depth_limit is not None and depth >= depth_limit:
        for pos in (4, 0, 2, 6, 8, 1, 3, 5, 7):
            if board[pos] == EMPTY:
                return 0, pos
        return 0, None

    moves = _legal_moves(board)
    if not moves:
        return 0, None

    best_move: int | None = None
    if is_robot:
        best = -100
        for m in moves:
            board[m] = ROBOT_PIECE
            s, _ = _minimax(board, depth + 1, False, alpha, beta, depth_limit)
            board[m] = EMPTY
            if s > best:
                best, best_move = s, m
            alpha = max(alpha, s)
            if beta <= alpha:
                break
        return best, best_move
    else:
        best = 100
        for m in moves:
            board[m] = HUMAN_PIECE
            s, _ = _minimax(board, depth + 1, True, alpha, beta, depth_limit)
            board[m] = EMPTY
            if s < best:
                best, best_move = s, m
            beta = min(beta, s)
            if beta <= alpha:
                break
        return best, best_move


def _choose_robot_move(board: list[int], difficulty: str) -> int | None:
    legal = _legal_moves(board)
    if not legal:
        return None
    difficulty = difficulty.lower()
    if difficulty == "easy":
        if random.random() < 0.6:
            return random.choice(legal)
        _, move = _minimax(board[:], 0, True, -100, 100, 2)
        return move if move is not None else random.choice(legal)
    if difficulty == "medium":
        if random.random() < 0.3:
            return random.choice(legal)
        _, move = _minimax(board[:], 0, True, -100, 100, 4)
        return move if move is not None else random.choice(legal)
    _, move = _minimax(board[:], 0, True, -100, 100, None)
    if move is not None:
        return move
    for pos in (4, 0, 2, 6, 8, 1, 3, 5, 7):
        if pos in legal:
            return pos
    return random.choice(legal)


class GameFSM:
    """Event-driven game state machine.

    External code drives transitions by calling:
      - on_start(difficulty, first_player)
      - on_proceed()
      - on_safety_changed(value)
      - on_alarm_expired()  (called by AlarmScheduler callback)
    """

    def __init__(self, ipc_client: IPCClient, alarm_scheduler: Any) -> None:
        self._ipc = ipc_client
        self._alarm = alarm_scheduler

        self.phase: Phase = Phase.IDLE
        self.board: list[int] = [EMPTY] * 9
        self.difficulty: str = "hard"
        self.first_player: str = "human"
        self.turn: str = "human"

        self.human_score: int = 0
        self.robot_score: int = 0
        self.draw_count: int = 0

        self.game_result: str | None = None  # "human_win" | "robot_win" | "draw"
        self.pending_move: int = -1

        self._alarm_target: Phase | None = None
        self._alarm_end_time: float = 0.0
        self._seen_safety_1: bool = False
        self._input_task: asyncio.Task[None] | None = None

    @property
    def alarm_countdown(self) -> float:
        if self.phase != Phase.ALARMING:
            return 0.0
        remaining = self._alarm_end_time - time.time()
        return max(0.0, remaining)

    def snapshot(self) -> dict[str, Any]:
        board_2d = [self.board[i:i+3] for i in range(0, 9, 3)]
        return {
            "phase": self.phase.value,
            "board": board_2d,
            "difficulty": self.difficulty,
            "first_player": self.first_player,
            "turn": self.turn,
            "score": {
                "human": self.human_score,
                "robot": self.robot_score,
                "draw": self.draw_count,
            },
            "game_result": self.game_result,
            "alarm_countdown": self.alarm_countdown,
            "pending_move": self.pending_move,
        }

    # ──── Public events ─────────────────────────────────────────────

    async def on_start(self, difficulty: str, first_player: str) -> None:
        if self.phase != Phase.IDLE:
            logger.warning("on_start ignored: phase=%s", self.phase)
            return
        self.board = [EMPTY] * 9
        self.difficulty = difficulty
        self.first_player = first_player
        self.turn = first_player
        self.game_result = None
        self.pending_move = -1
        logger.info("Game started: difficulty=%s first_player=%s", difficulty, first_player)
        await self._enter_alarming(Phase.GRABBING_PEN)

    async def on_proceed(self) -> None:
        if self.phase == Phase.WAITING_HUMAN:
            self._set_phase(Phase.RECOGNIZING)
            self._start_stdin_input()
        elif self.phase == Phase.GAME_OVER:
            await self._enter_alarming(Phase.DROPPING_PEN)
        else:
            logger.warning("on_proceed ignored: phase=%s", self.phase)

    async def on_safety_changed(self, value: int) -> None:
        if value == 2:
            logger.error("ESTOP detected! Halting FSM.")
            self._cancel_input_task()
            self._set_phase(Phase.IDLE)
            return

        if value == 1:
            self._seen_safety_1 = True
            return

        # value == 0 and we previously saw 1
        if value == 0 and self._seen_safety_1:
            self._seen_safety_1 = False
            await self._on_task_completed()

    async def on_alarm_expired(self) -> None:
        if self.phase != Phase.ALARMING or self._alarm_target is None:
            return
        target = self._alarm_target
        self._alarm_target = None
        logger.info("Alarm expired -> %s", target.value)

        if target == Phase.GRABBING_PEN:
            self._set_phase(Phase.GRABBING_PEN)
            self._seen_safety_1 = False
            self._send_task(TASK_GRASP_PEN, 0)
        elif target == Phase.DRAWING_CHESS:
            self._set_phase(Phase.DRAWING_CHESS)
            self._seen_safety_1 = False
            task_id = TASK_DRAW_O if ROBOT_PIECE == 2 else TASK_DRAW_X
            self._send_task(task_id, self.pending_move)
        elif target == Phase.DROPPING_PEN:
            self._set_phase(Phase.DROPPING_PEN)
            self._seen_safety_1 = False
            self._send_task(TASK_DROP_PEN, 0)
        elif target == Phase.ERASING:
            self._set_phase(Phase.ERASING)
            self._seen_safety_1 = False
            self._send_task(TASK_ERASE_BOARD, 0)
        else:
            self._set_phase(target)

    async def on_reset(self) -> None:
        self._cancel_input_task()
        self.board = [EMPTY] * 9
        self.game_result = None
        self.pending_move = -1
        self._seen_safety_1 = False
        self._alarm_target = None
        self._set_phase(Phase.IDLE)
        logger.info("Game reset")

    # ──── Internal transitions ──────────────────────────────────────

    async def _on_task_completed(self) -> None:
        if self.phase == Phase.GRABBING_PEN:
            if self.first_player == "robot":
                self._do_robot_think()
            else:
                self._set_phase(Phase.WAITING_HUMAN)
        elif self.phase == Phase.DRAWING_CHESS:
            self.board[self.pending_move] = ROBOT_PIECE
            logger.info("Robot piece committed to board at cell %d", self.pending_move)
            self._set_phase(Phase.CHECK_END)
            await self._do_check_end()
        elif self.phase == Phase.DROPPING_PEN:
            await self._enter_alarming(Phase.ERASING)
        elif self.phase == Phase.ERASING:
            self.game_result = None
            self._set_phase(Phase.IDLE)
            logger.info("Round complete, back to idle.")
        else:
            logger.warning("Unexpected task completion in phase=%s", self.phase)

    async def _do_check_end(self) -> None:
        w = _winner(self.board)
        if w == HUMAN_PIECE:
            self.human_score += 1
            self.game_result = "human_win"
            self._set_phase(Phase.GAME_OVER)
        elif w == ROBOT_PIECE:
            self.robot_score += 1
            self.game_result = "robot_win"
            self._set_phase(Phase.GAME_OVER)
        elif _is_draw(self.board):
            self.draw_count += 1
            self.game_result = "draw"
            self._set_phase(Phase.GAME_OVER)
        else:
            self.turn = "human"
            self._set_phase(Phase.WAITING_HUMAN)

    def _do_robot_think(self) -> None:
        self._set_phase(Phase.THINKING)
        move = _choose_robot_move(self.board, self.difficulty)
        if move is None:
            logger.error("No legal move for robot!")
            self._set_phase(Phase.IDLE)
            return
        self.pending_move = move
        self.turn = "robot"
        logger.info("Robot chose cell %d", move)
        asyncio.ensure_future(self._enter_alarming(Phase.DRAWING_CHESS))

    async def _enter_alarming(self, target: Phase) -> None:
        self._set_phase(Phase.ALARMING)
        self._alarm_target = target
        self._alarm_end_time = time.time() + ALARM_SECONDS
        await self._alarm.schedule(
            "game_alarm",
            ALARM_SECONDS,
            self.on_alarm_expired,
        )

    # ──── Stdin input stub ──────────────────────────────────────────

    def _start_stdin_input(self) -> None:
        self._cancel_input_task()
        self._input_task = asyncio.ensure_future(self._read_stdin())

    def _cancel_input_task(self) -> None:
        if self._input_task is not None and not self._input_task.done():
            self._input_task.cancel()
        self._input_task = None

    async def _read_stdin(self) -> None:
        while True:
            try:
                raw = await asyncio.to_thread(
                    input, "[Game] 输入玩家落子位置 (0-8): "
                )
                pos = int(raw.strip())
                if pos < 0 or pos > 8:
                    print(f"  无效位置 {pos}，请输入 0-8")
                    continue
                if self.board[pos] != EMPTY:
                    print(f"  位置 {pos} 已被占用")
                    continue
                break
            except (ValueError, EOFError):
                print("  请输入有效数字 0-8")
                continue

        if self.phase != Phase.RECOGNIZING:
            return

        self.board[pos] = HUMAN_PIECE
        logger.info("Human placed at cell %d", pos)

        w = _winner(self.board)
        if w == HUMAN_PIECE or _is_draw(self.board):
            self._set_phase(Phase.CHECK_END)
            await self._do_check_end()
        else:
            self._do_robot_think()

    # ──── Helpers ───────────────────────────────────────────────────

    def _set_phase(self, phase: Phase) -> None:
        old = self.phase
        self.phase = phase
        if old != phase:
            logger.info("[FSM] %s -> %s", old.value, phase.value)

    def _send_task(self, task_id: int, arg1: int) -> None:
        try:
            self._ipc.send_task_command(task_id, arg1, 0)
            logger.info("Sent TaskCommand: id=%d arg1=%d", task_id, arg1)
        except RuntimeError as e:
            logger.error("Failed to send task command: %s", e)
