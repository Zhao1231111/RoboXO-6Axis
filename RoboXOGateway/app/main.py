"""FastAPI application entry point."""

from __future__ import annotations

import asyncio
import logging
import time
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from typing import Any, AsyncGenerator, Awaitable, Callable

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from app.config import settings
from app.game_fsm import GameFSM
from app.ipc.client import IPCClient
from app.ipc.protocol import Frame
from app.routers import game, robot, system
from app.state import AppState
from app.ws import broadcast_loop, router as ws_router

logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
# Suppress noisy third-party debug logs
logging.getLogger("uvicorn").setLevel(logging.INFO)
logging.getLogger("asyncio").setLevel(logging.INFO)


logger = logging.getLogger(__name__)


@dataclass(slots=True)
class AlarmJob:
    key: str
    due_at: float
    callback: Callable[[], Awaitable[None]]
    cancelled: bool = False


class AlarmScheduler:
    """Small async timer service for delayed FSM transitions.

    Best-practice notes:
    - Keep one long-lived background task instead of spawning many sleep tasks.
    - Serialize schedule/cancel operations with an asyncio.Lock.
    - Use monotonic time (loop.time()) for delay calculations.
    - Make callbacks awaitable so state transitions can safely call async IPC.
    """

    def __init__(self, loop: asyncio.AbstractEventLoop) -> None:
        self._loop = loop
        self._jobs: dict[str, AlarmJob] = {}
        self._lock = asyncio.Lock()
        self._wake = asyncio.Event()
        self._task: asyncio.Task[None] | None = None
        self._running = False

    async def start(self) -> None:
        if self._task is not None:
            return
        self._running = True
        self._task = asyncio.create_task(self._run(), name="alarm-scheduler")

    async def stop(self) -> None:
        self._running = False
        self._wake.set()
        if self._task is not None:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
            self._task = None

    async def schedule(self, key: str, delay_s: float, callback: Callable[[], Awaitable[None]]) -> None:
        due_at = self._loop.time() + max(0.0, delay_s)
        async with self._lock:
            self._jobs[key] = AlarmJob(key=key, due_at=due_at, callback=callback)
            self._wake.set()
        logger.info("Alarm scheduled: key=%s delay=%.3fs", key, delay_s)

    async def cancel(self, key: str) -> None:
        async with self._lock:
            job = self._jobs.get(key)
            if job is not None:
                job.cancelled = True
                self._jobs.pop(key, None)
                self._wake.set()
        logger.info("Alarm cancelled: key=%s", key)

    async def _run(self) -> None:
        while self._running:
            await self._wake.wait()
            self._wake.clear()

            while self._running:
                job: AlarmJob | None = None
                sleep_for: float | None = None

                async with self._lock:
                    if not self._jobs:
                        break
                    now = self._loop.time()
                    key, job = min(self._jobs.items(), key=lambda item: item[1].due_at)
                    if job.cancelled:
                        self._jobs.pop(key, None)
                        continue
                    sleep_for = job.due_at - now
                    if sleep_for > 0:
                        job = None

                if job is None:
                    if sleep_for is None:
                        break
                    try:
                        await asyncio.wait_for(self._wake.wait(), timeout=sleep_for)
                        self._wake.clear()
                        continue
                    except asyncio.TimeoutError:
                        pass
                    continue

                async with self._lock:
                    current = self._jobs.pop(job.key, None)
                if current is None or current.cancelled:
                    continue

                try:
                    await job.callback()
                except Exception:
                    logger.exception("Alarm job failed: key=%s", job.key)


@asynccontextmanager
async def lifespan(app: FastAPI) -> AsyncGenerator[None]:
    logger.info("RoboXO Gateway starting up")
    logger.info("IPC socket path: %s", settings.ipc_socket_path)
    logger.info("WS heartbeat timeout: %.1fs", settings.ws_heartbeat_timeout)

    queue: asyncio.Queue[Frame] = asyncio.Queue()
    loop = asyncio.get_running_loop()

    ipc_client = IPCClient(settings.ipc_socket_path, queue, loop)
    alarm_scheduler = AlarmScheduler(loop)
    game_fsm = GameFSM(ipc_client=ipc_client, alarm_scheduler=alarm_scheduler)
    app_state = AppState(ipc_client=ipc_client, game_fsm=game_fsm)
    app_state.alarm_scheduler = alarm_scheduler  # type: ignore[attr-defined]
    app.state.app = app_state

    ipc_client.connect()
    await app_state.alarm_scheduler.start()  # type: ignore[attr-defined]
    broadcast_task = asyncio.create_task(broadcast_loop(app_state, queue), name="ipc-broadcast-loop")
    logger.info("All background tasks launched")

    yield

    logger.info("RoboXO Gateway shutting down")
    broadcast_task.cancel()
    await app_state.alarm_scheduler.stop()  # type: ignore[attr-defined]
    ipc_client.close()
    try:
        await broadcast_task
    except asyncio.CancelledError:
        pass


app = FastAPI(
    title="RoboXO Gateway",
    version="0.1.0",
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(robot.router)
app.include_router(game.router)
app.include_router(system.router)
app.include_router(ws_router)

app.mount("/", StaticFiles(directory="../RoboXOFrontend/out", html=True, follow_symlink=True), name="frontend")