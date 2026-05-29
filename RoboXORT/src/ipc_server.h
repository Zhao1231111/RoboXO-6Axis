#ifndef IPC_SERVER_H
#define IPC_SERVER_H

#include "ipc_protocol.h"
#include <atomic>
#include <string>

// Forward declarations for shared global state (defined in main.cpp)
extern ipc::SharedRobotState   g_shared_state;
extern ipc::JogCommandPacked   g_jog_cmd;
extern std::atomic<bool>       g_estop;
extern ipc::SPSCQueue<ipc::IOCommand, 16> g_io_queue;

namespace ipc {

// Socket path
constexpr const char* SOCKET_PATH = "/tmp/roboxo_rt.sock";

// StatusReport push interval (100ms = 10Hz)
constexpr int STATUS_REPORT_INTERVAL_MS = 100;

// Maximum payload size we expect to receive
constexpr size_t MAX_PAYLOAD_SIZE = 256;

class IPCServer {
public:
    IPCServer() = default;
    ~IPCServer();

    // Main loop - runs in its own thread. Does not return unless stop() is called.
    void run();

    // Signal the server to stop (from another thread)
    void stop();

private:
    // Connection handling
    bool setup_socket();
    void cleanup_socket();
    int  accept_connection();
    void handle_connection(int client_fd);

    // Frame I/O
    bool recv_exact(int fd, void* buf, size_t len);
    bool send_frame(int fd, MsgType type, uint8_t flags, uint16_t seq,
                    const void* payload, uint16_t payload_len);

    // Response helpers
    bool send_ack(int fd, uint16_t seq);
    bool send_error(int fd, uint16_t seq, uint16_t code, const char* msg);
    bool send_status_report(int fd, uint16_t seq);

    // Command dispatch
    void dispatch_jog(const JogPayload& payload);
    void dispatch_jog_stop();
    void dispatch_io_set(const IOSetPayload& payload);
    void dispatch_estop();

    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    uint16_t send_seq_ = 0;
};

} // namespace ipc

#endif // IPC_SERVER_H
