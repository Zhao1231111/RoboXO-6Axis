#ifndef IPC_PROTOCOL_H
#define IPC_PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <atomic>
#include <array>

// ============================================================================
// IPC Binary Frame Protocol
// ============================================================================
//
// Frame layout (little-endian, 4-byte aligned):
//   Header (8B) + Payload (N B) + Padding (0~3 B) + CRC32 (4B)
//
// Padding: (4 - payload_length % 4) % 4 zero bytes
// CRC32 scope: Header + Payload (excludes padding)
// ============================================================================

namespace ipc {

// --- Frame sync word ---
constexpr uint16_t FRAME_SYNC = 0xAA55;

// --- Message type codes ---
enum class MsgType : uint8_t {
    JointMove     = 0x01,  // reserved
    CartesianMove = 0x02,  // reserved
    Jog           = 0x03,
    JogStop       = 0x04,
    IOSet         = 0x05,
    StatusQuery   = 0x06,
    EStop         = 0x07,
    EStopReset    = 0x08,
    TaskCommand   = 0x10,
    StatusReport  = 0x81,
    Ack           = 0x82,
    Error         = 0x83,
};

// --- Flag bits ---
constexpr uint8_t FLAG_ACK = 0x80;
constexpr uint8_t FLAG_ERR = 0x40;

// --- Frame header (8 bytes, packed) ---
#pragma pack(push, 1)
struct FrameHeader {
    uint16_t sync;
    uint8_t  type;
    uint8_t  flags;
    uint16_t sequence;
    uint16_t payload_length;
};
#pragma pack(pop)
static_assert(sizeof(FrameHeader) == 8, "FrameHeader must be 8 bytes");

// --- Payload structures ---
#pragma pack(push, 1)

struct JogPayload {
    uint8_t  mode;        // 0 = joint, 1 = cartesian
    uint8_t  axis;        // 0~5
    int8_t   direction;   // +1 or -1
    uint8_t  speed_ratio; // 1~100
    uint16_t expires_ms;  // timeout in ms
    uint8_t  _pad[2];
};
static_assert(sizeof(JogPayload) == 8, "JogPayload must be 8 bytes");

struct IOSetPayload {
    uint8_t pin;
    uint8_t value;
    uint8_t _pad[2];
};
static_assert(sizeof(IOSetPayload) == 4, "IOSetPayload must be 4 bytes");

struct TaskCommandPayload {
    uint8_t  task_id;
    int32_t  arg1;
    int32_t  arg2;
};
static_assert(sizeof(TaskCommandPayload) == 9, "TaskCommandPayload must be 9 bytes");

struct StatusReportPayload {
    double   joints_deg[6];   // 48 bytes
    double   tcp_mm_deg[6];   // 48 bytes
    uint32_t io_state;        // 4 bytes
    uint8_t  phase;           // 1 byte (reserved, always 0)
    uint8_t  safety;          // 1 byte: 0=no_task, 1=task_active, 2=estop
    uint8_t  _pad[2];         // 2 bytes
};
static_assert(sizeof(StatusReportPayload) == 104, "StatusReportPayload must be 104 bytes");

struct ErrorPayload {
    uint16_t code;
    uint16_t msg_len;
    // followed by msg_len bytes of UTF-8 text
};

#pragma pack(pop)

// --- Safety states ---
enum Safety : uint8_t {
    SAFETY_IDLE   = 0,  // 无自动任务
    SAFETY_ACTIVE = 1,  // 自动任务进行中
    SAFETY_ESTOP  = 2,
};

// ============================================================================
// CRC-32 (ISO-HDLC / zlib polynomial 0xEDB88320)
// ============================================================================

namespace detail {

constexpr std::array<uint32_t, 256> make_crc_table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
        table[i] = crc;
    }
    return table;
}

inline const std::array<uint32_t, 256>& crc_table() {
    static const auto table = make_crc_table();
    return table;
}

} // namespace detail

inline uint32_t crc32(const void* data, size_t len) {
    const auto& table = detail::crc_table();
    auto* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

inline uint32_t crc32_update(uint32_t crc, const void* data, size_t len) {
    const auto& table = detail::crc_table();
    auto* p = static_cast<const uint8_t*>(data);
    crc ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

// Compute CRC over header + payload
inline uint32_t frame_crc(const FrameHeader& hdr, const void* payload, uint16_t payload_len) {
    uint32_t crc = crc32(&hdr, sizeof(hdr));
    if (payload_len > 0) {
        crc = crc32_update(crc, payload, payload_len);
    }
    return crc;
}

// Compute padding needed after payload
inline uint16_t payload_padding(uint16_t payload_length) {
    return (4 - (payload_length % 4)) % 4;
}

// Total frame size including header, payload, padding, and CRC
inline size_t frame_total_size(uint16_t payload_length) {
    return sizeof(FrameHeader) + payload_length + payload_padding(payload_length) + 4;
}

// ============================================================================
// Shared State (Seqlock pattern: RT writes, IPC reads)
// ============================================================================

// NOTE: This seqlock relies on x86 TSO (Total Store Order) for correctness.
// On x86, stores are never reordered w.r.t. other stores, so the sequence
// counter visibility is guaranteed without explicit fences between writes.
// If porting to ARM/other weak architectures, add std::atomic_thread_fence.
struct SharedRobotState {
    alignas(64) std::atomic<uint32_t> seq{0};
    double   joints_deg[6]{};
    double   tcp_mm_deg[6]{};
    uint32_t io_state{0};
    uint8_t  safety{SAFETY_IDLE};
    uint8_t  phase{0};

    void write(const double* joints, const double* tcp, uint32_t io, uint8_t safe) {
        uint32_t s = seq.load(std::memory_order_relaxed);
        seq.store(s + 1, std::memory_order_relaxed); // odd = writing
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(joints_deg, joints, sizeof(joints_deg));
        std::memcpy(tcp_mm_deg, tcp, sizeof(tcp_mm_deg));
        io_state = io;
        safety = safe;
        std::atomic_thread_fence(std::memory_order_release);
        seq.store(s + 2, std::memory_order_release); // even = stable
    }

    bool read(StatusReportPayload& out) const {
        uint32_t s1 = seq.load(std::memory_order_acquire);
        if (s1 & 1) return false;
        std::atomic_thread_fence(std::memory_order_acquire);
        std::memcpy(out.joints_deg, joints_deg, sizeof(joints_deg));
        std::memcpy(out.tcp_mm_deg, tcp_mm_deg, sizeof(tcp_mm_deg));
        out.io_state = io_state;
        out.phase = phase;
        out.safety = safety;
        std::memset(out._pad, 0, sizeof(out._pad));
        std::atomic_thread_fence(std::memory_order_acquire);
        uint32_t s2 = seq.load(std::memory_order_relaxed);
        return s1 == s2;
    }
};

// ============================================================================
// Jog Command (atomic packed, IPC writes, RT reads)
// ============================================================================

// Packed layout (8 bytes):
//   [0] active  (u8)
//   [1] mode    (u8)
//   [2] axis    (u8)
//   [3] direction (i8)
//   [4] speed_ratio (u8)
//   [5] pad     (u8)
//   [6..7] expires_ms (u16)

struct JogCommandPacked {
    std::atomic<uint64_t> data{0};

    void set(uint8_t mode, uint8_t axis, int8_t direction, uint8_t speed_ratio, uint16_t expires_ms) {
        uint64_t v = 0;
        v |= uint64_t(1);                              // active
        v |= uint64_t(mode)        << 8;
        v |= uint64_t(axis)        << 16;
        v |= uint64_t(uint8_t(direction)) << 24;
        v |= uint64_t(speed_ratio) << 32;
        v |= uint64_t(0)           << 40;              // pad
        v |= uint64_t(expires_ms)  << 48;
        data.store(v, std::memory_order_release);
    }

    void clear() {
        data.store(0, std::memory_order_release);
    }

    struct Unpacked {
        bool     active;
        uint8_t  mode;
        uint8_t  axis;
        int8_t   direction;
        uint8_t  speed_ratio;
        uint16_t expires_ms;
    };

    Unpacked load() const {
        uint64_t v = data.load(std::memory_order_acquire);
        Unpacked u;
        u.active      = (v & 0xFF) != 0;
        u.mode        = (v >> 8)  & 0xFF;
        u.axis        = (v >> 16) & 0xFF;
        u.direction   = static_cast<int8_t>((v >> 24) & 0xFF);
        u.speed_ratio = (v >> 32) & 0xFF;
        u.expires_ms  = (v >> 48) & 0xFFFF;
        return u;
    }
};

// ============================================================================
// IO Command
// ============================================================================

struct IOCommand {
    uint8_t pin;
    uint8_t value;
};

// ============================================================================
// Lock-free SPSC Ring Buffer
// ============================================================================

template <typename T, size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
public:
    bool try_push(const T& item) {
        size_t w = write_idx_.load(std::memory_order_relaxed);
        size_t next_w = (w + 1) & (Capacity - 1);
        if (next_w == read_idx_.load(std::memory_order_acquire))
            return false; // full
        buf_[w] = item;
        write_idx_.store(next_w, std::memory_order_release);
        return true;
    }

    bool try_pop(T& item) {
        size_t r = read_idx_.load(std::memory_order_relaxed);
        if (r == write_idx_.load(std::memory_order_acquire))
            return false; // empty
        item = buf_[r];
        read_idx_.store((r + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

private:
    alignas(64) std::atomic<size_t> write_idx_{0};
    alignas(64) std::atomic<size_t> read_idx_{0};
    T buf_[Capacity]{};
};

} // namespace ipc

#endif // IPC_PROTOCOL_H
