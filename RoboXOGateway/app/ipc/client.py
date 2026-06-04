"""IPC client managing the Unix Domain Socket connection to the C++ RT process."""

from __future__ import annotations

import asyncio
import logging
import socket
import threading
import time

from .protocol import Frame, FrameParser, encode_frame

logger = logging.getLogger(__name__)

_MAX_RECONNECT_DELAY = 10.0


class IPCClient:
    """Manages a SOCK_STREAM Unix Domain Socket connection to the C++ RT component.

    - A background reader thread receives data, parses frames, and pushes them
      into an asyncio.Queue for the main event loop to consume.
    - Sending is done directly from async coroutines (short writes are atomic
      on Unix Domain Sockets).
    - Automatic reconnection with exponential backoff on disconnect.
    """

    def __init__(
        self,
        path: str,
        frame_queue: asyncio.Queue[Frame],
        loop: asyncio.AbstractEventLoop,
    ) -> None:
        self._path = path
        self._queue = frame_queue
        self._loop = loop

        self._sock: socket.socket | None = None
        self._connected = False
        self._seq: int = 0
        self._seq_lock = threading.Lock()

        self._reader_thread: threading.Thread | None = None
        self._closing = False

    @property
    def connected(self) -> bool:
        return self._connected

    def connect(self) -> None:
        """Start the client: attempt connection and launch reader thread."""
        self._closing = False
        logger.info("IPC client starting, target socket: %s", self._path)
        self._reader_thread = threading.Thread(
            target=self._reader_loop, name="ipc-reader", daemon=True
        )
        self._reader_thread.start()

    def close(self) -> None:
        """Shut down the client gracefully."""
        logger.info("IPC client shutting down")
        self._closing = True
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass
        self._connected = False

    def send_frame(self, msg_type: int, payload: bytes = b"", flags: int = 0) -> None:
        """Encode and send a frame. Raises RuntimeError if not connected."""
        if not self._connected or self._sock is None:
            logger.warning("IPC send attempted while disconnected (type=0x%02X)", msg_type)
            raise RuntimeError("IPC not connected")
        with self._seq_lock:
            seq = self._seq
            self._seq = (self._seq + 1) & 0xFFFF
        data = encode_frame(msg_type, payload, seq, flags)
        logger.debug("IPC TX: type=0x%02X seq=%d len=%d", msg_type, seq, len(data))
        try:
            self._sock.sendall(data)
        except OSError as e:
            logger.error("IPC send failed: %s", e)
            self._connected = False
            raise RuntimeError("IPC send failed") from e

    # ─── Internal ───────────────────────────────────────────────────────────

    def _try_connect(self) -> bool:
        logger.debug("IPC attempting connection to %s ...", self._path)
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.connect(self._path)
            sock.settimeout(1.0)
            self._sock = sock
            self._connected = True
            logger.info("IPC connected to %s", self._path)
            return True
        except OSError as e:
            logger.warning("IPC connect failed (%s): %s", self._path, e)
            return False

    def _reader_loop(self) -> None:
        """Background thread: connect, read frames, reconnect on failure."""
        logger.info("IPC reader thread started")
        delay = 1.0
        parser = FrameParser()

        while not self._closing:
            if not self._connected:
                if not self._try_connect():
                    logger.debug("IPC reconnect in %.1fs ...", delay)
                    time.sleep(delay)
                    delay = min(delay * 2, _MAX_RECONNECT_DELAY)
                    continue
                delay = 1.0
                parser = FrameParser()

            assert self._sock is not None
            try:
                data = self._sock.recv(4096)
                if not data:
                    raise ConnectionError("peer closed connection")
            except socket.timeout:
                continue
            except OSError as e:
                if self._closing:
                    break
                logger.warning("IPC connection lost: %s — will reconnect", e)
                self._connected = False
                try:
                    self._sock.close()
                except OSError:
                    pass
                self._sock = None
                continue

            # logger.debug("IPC RX: %d bytes", len(data))
            frames = parser.feed(data)
            for frame in frames:    
                if frame.msg_type == 0x81:
                    # Status Report, Ignore to reduce noise
                    pass
                else:
                    logger.debug(
                        "IPC frame parsed: type=0x%02X flags=0x%02X seq=%d payload=%dB",
                        frame.msg_type, frame.flags, frame.seq, len(frame.payload),
                    )
                self._loop.call_soon_threadsafe(self._queue.put_nowait, frame)

        logger.info("IPC reader thread exiting")
