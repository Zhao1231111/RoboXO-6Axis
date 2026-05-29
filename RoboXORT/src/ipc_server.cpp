#include "ipc_server.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <time.h>

namespace ipc {

IPCServer::~IPCServer() {
    cleanup_socket();
}

void IPCServer::stop() {
    running_.store(false, std::memory_order_relaxed);
    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
    }
}

void IPCServer::run() {
    running_.store(true, std::memory_order_relaxed);

    if (!setup_socket()) {
        fprintf(stderr, "[IPC] Failed to setup socket, IPC server exiting.\n");
        return;
    }

    printf("[IPC] Server listening on %s\n", SOCKET_PATH);

    while (running_.load(std::memory_order_relaxed)) {
        int client_fd = accept_connection();
        if (client_fd < 0) {
            if (running_.load(std::memory_order_relaxed))
                fprintf(stderr, "[IPC] accept failed: %s\n", strerror(errno));
            continue;
        }

        printf("[IPC] Client connected.\n");
        handle_connection(client_fd);
        close(client_fd);
        printf("[IPC] Client disconnected.\n");

        // Safety: connection lost -> EStop
        dispatch_estop();
        printf("[IPC] EStop triggered due to client disconnect.\n");
    }

    cleanup_socket();
}

bool IPCServer::setup_socket() {
    unlink(SOCKET_PATH);

    listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        perror("[IPC] socket");
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[IPC] bind");
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, 1) < 0) {
        perror("[IPC] listen");
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    return true;
}

void IPCServer::cleanup_socket() {
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    unlink(SOCKET_PATH);
}

int IPCServer::accept_connection() {
    return accept(listen_fd_, nullptr, nullptr);
}

void IPCServer::handle_connection(int client_fd) {
    struct timespec last_report_time;
    clock_gettime(CLOCK_MONOTONIC, &last_report_time);

    while (running_.load(std::memory_order_relaxed)) {
        // Poll for incoming data with timeout for periodic StatusReport
        struct pollfd pfd;
        pfd.fd = client_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int poll_timeout_ms = 10; // 10ms poll granularity
        int ret = poll(&pfd, 1, poll_timeout_ms);

        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("[IPC] poll");
            break;
        }

        // Check if it's time to send periodic StatusReport
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        int64_t elapsed_ms = (now.tv_sec - last_report_time.tv_sec) * 1000
                           + (now.tv_nsec - last_report_time.tv_nsec) / 1000000;
        if (elapsed_ms >= STATUS_REPORT_INTERVAL_MS) {
            if (!send_status_report(client_fd, send_seq_++)) {
                break; // send failed, connection lost
            }
            last_report_time = now;
        }

        if (ret == 0) continue; // poll timeout, no data

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            break; // connection error
        }

        if (!(pfd.revents & POLLIN)) continue;

        // --- Read frame header ---
        FrameHeader hdr;
        if (!recv_exact(client_fd, &hdr, sizeof(hdr))) break;

        // Validate sync
        if (hdr.sync != FRAME_SYNC) {
            fprintf(stderr, "[IPC] Bad sync word 0x%04X, dropping connection.\n", hdr.sync);
            break;
        }

        // Read payload + padding + CRC
        uint16_t pad_len = payload_padding(hdr.payload_length);
        uint16_t total_after_hdr = hdr.payload_length + pad_len + 4; // +4 for CRC

        uint8_t buf[MAX_PAYLOAD_SIZE + 8]; // payload + padding + CRC
        if (total_after_hdr > sizeof(buf)) {
            fprintf(stderr, "[IPC] Payload too large (%u bytes), dropping.\n", hdr.payload_length);
            break;
        }

        if (!recv_exact(client_fd, buf, total_after_hdr)) break;

        // Verify CRC
        uint32_t received_crc;
        memcpy(&received_crc, buf + hdr.payload_length + pad_len, 4);
        uint32_t computed_crc = frame_crc(hdr, buf, hdr.payload_length);
        if (received_crc != computed_crc) {
            fprintf(stderr, "[IPC] CRC mismatch (got 0x%08X, expected 0x%08X).\n",
                    received_crc, computed_crc);
            send_error(client_fd, hdr.sequence, 0x0002, "CRC mismatch");
            continue; // try to recover
        }

        // --- Dispatch by type ---
        MsgType type = static_cast<MsgType>(hdr.type);

        switch (type) {
        case MsgType::Jog:
            if (hdr.payload_length != sizeof(JogPayload)) {
                send_error(client_fd, hdr.sequence, 0x0003, "invalid payload size");
                break;
            }
            {
                JogPayload payload;
                memcpy(&payload, buf, sizeof(payload));
                dispatch_jog(payload);
                send_ack(client_fd, hdr.sequence);
            }
            break;

        case MsgType::JogStop:
            dispatch_jog_stop();
            send_ack(client_fd, hdr.sequence);
            break;

        case MsgType::IOSet:
            if (hdr.payload_length != sizeof(IOSetPayload)) {
                send_error(client_fd, hdr.sequence, 0x0003, "invalid payload size");
                break;
            }
            {
                IOSetPayload payload;
                memcpy(&payload, buf, sizeof(payload));
                dispatch_io_set(payload);
                send_ack(client_fd, hdr.sequence);
            }
            break;

        case MsgType::StatusQuery:
            send_status_report(client_fd, hdr.sequence);
            break;

        case MsgType::EStop:
            dispatch_estop();
            send_ack(client_fd, hdr.sequence);
            break;

        case MsgType::JointMove:
        case MsgType::CartesianMove:
            send_error(client_fd, hdr.sequence, 0x0001, "not implemented");
            break;

        default:
            send_error(client_fd, hdr.sequence, 0x0004, "unknown message type");
            break;
        }
    }
}

// --- Low-level I/O ---

bool IPCServer::recv_exact(int fd, void* buf, size_t len) {
    auto* p = static_cast<uint8_t*>(buf);
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t n = recv(fd, p, remaining, 0);
        if (n <= 0) return false;
        p += n;
        remaining -= n;
    }
    return true;
}

bool IPCServer::send_frame(int fd, MsgType type, uint8_t flags, uint16_t seq,
                           const void* payload, uint16_t payload_len) {
    FrameHeader hdr;
    hdr.sync = FRAME_SYNC;
    hdr.type = static_cast<uint8_t>(type);
    hdr.flags = flags;
    hdr.sequence = seq;
    hdr.payload_length = payload_len;

    uint16_t pad_len = payload_padding(payload_len);
    uint32_t crc = frame_crc(hdr, payload, payload_len);

    // Build complete frame in buffer
    size_t total = frame_total_size(payload_len);
    uint8_t frame_buf[512];
    if (total > sizeof(frame_buf)) return false;

    size_t offset = 0;
    memcpy(frame_buf + offset, &hdr, sizeof(hdr));
    offset += sizeof(hdr);

    if (payload_len > 0) {
        memcpy(frame_buf + offset, payload, payload_len);
        offset += payload_len;
    }

    if (pad_len > 0) {
        memset(frame_buf + offset, 0, pad_len);
        offset += pad_len;
    }

    memcpy(frame_buf + offset, &crc, 4);
    offset += 4;

    // Single write for atomicity on Unix Domain Socket
    ssize_t n = send(fd, frame_buf, offset, MSG_NOSIGNAL);
    return n == static_cast<ssize_t>(offset);
}

// --- Response helpers ---

bool IPCServer::send_ack(int fd, uint16_t seq) {
    return send_frame(fd, MsgType::Ack, FLAG_ACK, seq, nullptr, 0);
}

bool IPCServer::send_error(int fd, uint16_t seq, uint16_t code, const char* msg) {
    uint16_t msg_len = static_cast<uint16_t>(strlen(msg));
    uint8_t payload[256];
    if (4 + msg_len > sizeof(payload)) return false;

    memcpy(payload, &code, 2);
    memcpy(payload + 2, &msg_len, 2);
    memcpy(payload + 4, msg, msg_len);

    return send_frame(fd, MsgType::Error, FLAG_ACK | FLAG_ERR, seq, payload, 4 + msg_len);
}

bool IPCServer::send_status_report(int fd, uint16_t seq) {
    StatusReportPayload payload;
    // Retry seqlock read up to 3 times
    for (int attempt = 0; attempt < 3; attempt++) {
        if (g_shared_state.read(payload)) {
            return send_frame(fd, MsgType::StatusReport, FLAG_ACK, seq, &payload, sizeof(payload));
        }
    }
    // If all retries failed, send stale data (better than nothing)
    memset(&payload, 0, sizeof(payload));
    return send_frame(fd, MsgType::StatusReport, FLAG_ACK, seq, &payload, sizeof(payload));
}

// --- Command dispatch ---

void IPCServer::dispatch_jog(const JogPayload& payload) {
    g_jog_cmd.set(payload.mode, payload.axis, payload.direction,
                  payload.speed_ratio, payload.expires_ms);
}

void IPCServer::dispatch_jog_stop() {
    g_jog_cmd.clear();
}

void IPCServer::dispatch_io_set(const IOSetPayload& payload) {
    IOCommand cmd;
    cmd.pin = payload.pin;
    cmd.value = payload.value;
    g_io_queue.try_push(cmd);
}

void IPCServer::dispatch_estop() {
    g_estop.store(true, std::memory_order_release);
    g_jog_cmd.clear();
}

} // namespace ipc
