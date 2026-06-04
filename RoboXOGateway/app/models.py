"""Pydantic models for REST API request/response schemas."""

from __future__ import annotations

from typing import Literal

from pydantic import BaseModel, Field


# ─── Request models ─────────────────────────────────────────────────────────


class JogRequest(BaseModel):
    mode: Literal["joint", "cartesian"]
    axis: int = Field(ge=0, le=5)
    direction: Literal[1, -1]
    speed_ratio: int = Field(ge=1, le=100)
    expires_ms: int = Field(default=300, ge=100, le=5000)


class IOSetRequest(BaseModel):
    pin: int = Field(ge=0, le=255)
    value: int = Field(ge=0, le=255)


class GameStartRequest(BaseModel):
    difficulty: Literal["easy", "medium", "hard"]
    first_player: Literal["human", "robot"]


# ─── Response models ────────────────────────────────────────────────────────


class RobotStateResponse(BaseModel):
    joints_deg: list[float]
    tcp_mm_deg: list[float]
    gripper: str
    io_state: int


class SystemStatusResponse(BaseModel):
    ipc: bool
    uptime_s: int


class ErrorResponse(BaseModel):
    error: str
    message: str
