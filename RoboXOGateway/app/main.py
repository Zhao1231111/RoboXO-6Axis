"""FastAPI application entry point."""

from __future__ import annotations

import asyncio
import logging
from contextlib import asynccontextmanager
from typing import AsyncGenerator

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from app.config import settings
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


@asynccontextmanager
async def lifespan(app: FastAPI) -> AsyncGenerator[None]:
    logger.info("RoboXO Gateway starting up")
    logger.info("IPC socket path: %s", settings.ipc_socket_path)
    logger.info("WS heartbeat timeout: %.1fs", settings.ws_heartbeat_timeout)

    queue: asyncio.Queue[Frame] = asyncio.Queue()
    loop = asyncio.get_running_loop()

    ipc_client = IPCClient(settings.ipc_socket_path, queue, loop)
    app_state = AppState(ipc_client=ipc_client)
    app.state.app = app_state

    ipc_client.connect()
    broadcast_task = asyncio.create_task(broadcast_loop(app_state, queue))
    logger.info("All background tasks launched")

    yield

    logger.info("RoboXO Gateway shutting down")
    broadcast_task.cancel()
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

app.mount("/", StaticFiles(directory="public", html=True, follow_symlink=True), name="frontend")
