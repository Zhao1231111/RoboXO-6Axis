#include "teleop_keyboard.h"
#include "ipc_server.h"  // for g_jog_cmd, g_io_queue, g_estop, g_shared_state

#include <cstdio>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <cstring>

// Speed ratio presets
static const uint8_t SPEED_PRESETS[] = {1, 5, 10, 50, 100};
static const int NUM_PRESETS = 5;

static struct termios orig_termios;

static void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1; // 100ms timeout
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static int read_key() {
    char c;
    if (read(STDIN_FILENO, &c, 1) == 1)
        return c;
    return -1;
}

static const char* mode_name(uint8_t mode, uint8_t axis) {
    if (mode == 0) {
        static char buf[8];
        snprintf(buf, sizeof(buf), "J%d", axis + 1);
        return buf;
    } else {
        static const char* names[] = {"X", "Y", "Z", "Rx", "Ry", "Rz"};
        return names[axis];
    }
}

void run_teleop() {
    enable_raw_mode();

    uint8_t mode = 0;         // 0=joint, 1=cartesian
    uint8_t axis = 0;         // 0~5
    int speed_idx = 2;        // default 10%
    bool jogging = false;
    int8_t jog_direction = 0;

    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║          Keyboard Teleop Controller             ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  1-6: Joint J1-J6   x/y/z: Cart XYZ            ║\n");
    printf("║  r/p/w: Cart Rx/Ry/Rz                          ║\n");
    printf("║  +/-: Jog +/-       Space: Stop                 ║\n");
    printf("║  [/]: Speed -/+     g/h: Gripper open/close     ║\n");
    printf("║  e: EStop           q: Quit                     ║\n");
    printf("╚══════════════════════════════════════════════════╝\n");
    printf("\n");

    auto print_status = [&]() {
        ipc::StatusReportPayload state;
        bool got = false;
        for (int i = 0; i < 5; i++) {
            if (g_shared_state.read(state)) { got = true; break; }
        }

        printf("\r\033[K");  // clear line
        printf("[%s %s] Speed:%3d%%  ",
               mode == 0 ? "Joint" : "Cart",
               mode_name(mode, axis),
               SPEED_PRESETS[speed_idx]);
        if (jogging)
            printf("JOG %s ", jog_direction > 0 ? "+" : "-");
        else
            printf("IDLE  ");

        if (got) {
            printf("| J: %.1f %.1f %.1f %.1f %.1f %.1f",
                   state.joints_deg[0], state.joints_deg[1], state.joints_deg[2],
                   state.joints_deg[3], state.joints_deg[4], state.joints_deg[5]);
        }
        fflush(stdout);
    };

    print_status();

    while (true) {
        int key = read_key();

        if (key == -1) {
            // No key pressed - if jogging, refresh the command to prevent timeout
            if (jogging) {
                g_jog_cmd.set(mode, axis, jog_direction, SPEED_PRESETS[speed_idx], 300);
            }
            print_status();
            continue;
        }

        bool need_refresh = true;

        switch (key) {
        // Axis selection - joint
        case '1': mode = 0; axis = 0; break;
        case '2': mode = 0; axis = 1; break;
        case '3': mode = 0; axis = 2; break;
        case '4': mode = 0; axis = 3; break;
        case '5': mode = 0; axis = 4; break;
        case '6': mode = 0; axis = 5; break;

        // Axis selection - cartesian
        case 'x': case 'X': mode = 1; axis = 0; break;
        case 'y': case 'Y': mode = 1; axis = 1; break;
        case 'z': case 'Z': mode = 1; axis = 2; break;
        case 'r': case 'R': mode = 1; axis = 3; break;
        case 'p': case 'P': mode = 1; axis = 4; break;
        case 'w': case 'W': mode = 1; axis = 5; break;

        // Jog +/-
        case '+': case '=':
            jogging = true;
            jog_direction = 1;
            g_jog_cmd.set(mode, axis, jog_direction, SPEED_PRESETS[speed_idx], 300);
            break;
        case '-': case '_':
            jogging = true;
            jog_direction = -1;
            g_jog_cmd.set(mode, axis, jog_direction, SPEED_PRESETS[speed_idx], 300);
            break;

        // Stop
        case ' ':
            jogging = false;
            g_jog_cmd.clear();
            break;

        // Speed adjustment
        case '[':
            if (speed_idx > 0) speed_idx--;
            break;
        case ']':
            if (speed_idx < NUM_PRESETS - 1) speed_idx++;
            break;

        // Gripper
        case 'g': case 'G': {
            ipc::IOCommand cmd;
            cmd.pin = 14; cmd.value = 1;  // pin 14 = open
            g_io_queue.try_push(cmd);
            cmd.pin = 15; cmd.value = 0;
            g_io_queue.try_push(cmd);
            break;
        }
        case 'h': case 'H': {
            ipc::IOCommand cmd;
            cmd.pin = 14; cmd.value = 0;
            g_io_queue.try_push(cmd);
            cmd.pin = 15; cmd.value = 1;  // pin 15 = close
            g_io_queue.try_push(cmd);
            break;
        }

        // EStop
        case 'e': case 'E':
            g_estop.store(true, std::memory_order_release);
            g_jog_cmd.clear();
            jogging = false;
            printf("\n\n*** ESTOP ACTIVATED ***\n");
            printf("Press 'q' to exit.\n");
            break;

        // Quit
        case 'q': case 'Q':
            jogging = false;
            g_jog_cmd.clear();
            printf("\n\nExiting teleop.\n");
            disable_raw_mode();
            return;

        default:
            need_refresh = false;
            break;
        }

        // If axis/mode changed while jogging, update the command
        if (jogging && ((key >= '1' && key <= '6') || key == 'x' || key == 'y' || key == 'z'
                        || key == 'r' || key == 'p' || key == 'w')) {
            g_jog_cmd.set(mode, axis, jog_direction, SPEED_PRESETS[speed_idx], 300);
        }

        if (need_refresh) print_status();
    }

    disable_raw_mode();
}
