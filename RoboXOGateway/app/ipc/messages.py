"""Payload encode/decode for each IPC message type."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum


class MsgType(IntEnum):
    # Requests (0x01–0x7F)
    JOINT_MOVE = 0x01
    CARTESIAN_MOVE = 0x02
    JOG = 0x03
    JOG_STOP = 0x04
    IO_SET = 0x05
    STATUS_QUERY = 0x06
    ESTOP = 0x07
    ESTOP_RESET = 0x08

    # Responses (0x81–0xFF)
    STATUS_REPORT = 0x81
    ACK = 0x82
    ERROR = 0x83


# ─── Payload structs ────────────────────────────────────────────────────────

_JOINTS_STRUCT = struct.Struct("<6d")  # 48 bytes
_JOG_STRUCT = struct.Struct("<BBbBHxx")  # 8 bytes
_IO_SET_STRUCT = struct.Struct("<BBxx")  # 4 bytes
_STATUS_REPORT_STRUCT = struct.Struct("<6d6dIBBxx")  # 104 bytes
_ERROR_HEADER_STRUCT = struct.Struct("<HH")  # 4 bytes


# ─── Encode helpers (Python → C++) ──────────────────────────────────────────


def encode_joint_move(joints: list[float]) -> bytes:
    return _JOINTS_STRUCT.pack(*joints)


def encode_cartesian_move(pose: list[float]) -> bytes:
    return _JOINTS_STRUCT.pack(*pose)


def encode_jog(
    mode: int, axis: int, direction: int, speed_ratio: int, expires_ms: int
) -> bytes:
    return _JOG_STRUCT.pack(mode, axis, direction, speed_ratio, expires_ms)


def encode_io_set(pin: int, value: int) -> bytes:
    return _IO_SET_STRUCT.pack(pin, value)


# JogStop, StatusQuery, EStop, EStopReset have empty payloads — just use b""


# ─── Decode helpers (C++ → Python) ──────────────────────────────────────────


@dataclass(slots=True)
class StatusReport:
    joints_deg: list[float]
    tcp_mm_deg: list[float]
    io_state: int
    phase: int
    safety: int


def decode_status_report(payload: bytes) -> StatusReport:
    values = _STATUS_REPORT_STRUCT.unpack(payload)
    joints = list(values[0:6])
    tcp = list(values[6:12])
    io_state = values[12]
    phase = values[13]
    safety = values[14]
    return StatusReport(
        joints_deg=joints,
        tcp_mm_deg=tcp,
        io_state=io_state,
        phase=phase,
        safety=safety,
    )


@dataclass(slots=True)
class ErrorReport:
    code: int
    message: str


def decode_error(payload: bytes) -> ErrorReport:
    code, msg_len = _ERROR_HEADER_STRUCT.unpack_from(payload, 0)
    message = payload[4 : 4 + msg_len].decode("utf-8", errors="replace")
    return ErrorReport(code=code, message=message)
