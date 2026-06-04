"""Binary frame protocol for IPC communication with the C++ RT component.

Frame layout (little-endian, 4-byte aligned):

    Offset  Size  Field
    0       2     Sync word (0xAA55)
    2       1     Message type
    3       1     Flags (bit7=ACK, bit6=ERR)
    4       2     Sequence number (u16)
    6       2     Payload length (u16, excludes padding and CRC)
    8       N     Payload
    8+N     0-3   Zero-padding to 4-byte boundary
    ...     4     CRC-32 (over header + payload, excludes padding)
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

SYNC_WORD = 0xAA55
HEADER_SIZE = 8
CRC_SIZE = 4
HEADER_STRUCT = struct.Struct("<HBBHH")  # sync, type, flags, seq, payload_len

FLAG_ACK = 0x80
FLAG_ERR = 0x40


@dataclass(slots=True)
class Frame:
    msg_type: int
    flags: int
    seq: int
    payload: bytes


def _compute_crc(header: bytes, payload: bytes) -> int:
    crc = zlib.crc32(header)
    crc = zlib.crc32(payload, crc)
    return crc & 0xFFFFFFFF


def encode_frame(msg_type: int, payload: bytes, seq: int, flags: int = 0) -> bytes:
    """Encode a complete IPC frame ready to send over the wire."""
    payload_len = len(payload)
    header = HEADER_STRUCT.pack(SYNC_WORD, msg_type, flags, seq, payload_len)
    crc = _compute_crc(header, payload)

    pad_len = (4 - (HEADER_SIZE + payload_len) % 4) % 4
    padding = b"\x00" * pad_len

    return header + payload + padding + struct.pack("<I", crc)


class FrameParser:
    """Streaming parser that buffers incoming bytes and yields complete frames."""

    def __init__(self) -> None:
        self._buf = bytearray()

    def feed(self, data: bytes) -> list[Frame]:
        """Feed raw bytes received from the socket; returns parsed frames."""
        self._buf.extend(data)
        frames: list[Frame] = []

        while True:
            # Scan for sync word
            sync_pos = self._find_sync()
            if sync_pos is None:
                break

            # Discard bytes before sync
            if sync_pos > 0:
                del self._buf[:sync_pos]

            # Need at least a full header
            if len(self._buf) < HEADER_SIZE:
                break

            _, msg_type, flags, seq, payload_len = HEADER_STRUCT.unpack_from(
                self._buf, 0
            )

            pad_len = (4 - (HEADER_SIZE + payload_len) % 4) % 4
            total_len = HEADER_SIZE + payload_len + pad_len + CRC_SIZE

            if len(self._buf) < total_len:
                break

            header = bytes(self._buf[:HEADER_SIZE])
            payload = bytes(self._buf[HEADER_SIZE : HEADER_SIZE + payload_len])
            crc_offset = HEADER_SIZE + payload_len + pad_len
            (received_crc,) = struct.unpack_from("<I", self._buf, crc_offset)

            expected_crc = _compute_crc(header, payload)
            if received_crc != expected_crc:
                # Bad CRC — skip this sync word and try next
                del self._buf[:2]
                continue

            frames.append(Frame(msg_type=msg_type, flags=flags, seq=seq, payload=payload))
            del self._buf[:total_len]

        return frames

    def _find_sync(self) -> int | None:
        """Find position of 0xAA55 (little-endian: byte 0x55 then 0xAA)."""
        buf = self._buf
        for i in range(len(buf) - 1):
            if buf[i] == 0x55 and buf[i + 1] == 0xAA:
                return i
        return None
