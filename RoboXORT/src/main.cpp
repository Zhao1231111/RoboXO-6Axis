#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <sys/mman.h>
#include <malloc.h>
#include <sched.h>
#include <signal.h>
#include <thread>
#include <deque>
#include <cmath>
#include <Eigen/Eigen>
#include "general_6s.h"
#include "ecrt.h"
#include "ipc_protocol.h"
#include "ipc_server.h"

using namespace Eigen;
using namespace std;

// ============================================================================
// Global Shared State (IPC <-> RT)
// ============================================================================

ipc::SharedRobotState                g_shared_state;
ipc::JogCommandPacked                g_jog_cmd;
std::atomic<bool>                    g_estop{false};
ipc::SPSCQueue<ipc::IOCommand, 16>  g_io_queue;

// ============================================================================
// Robot Algorithm Object
// ============================================================================

extern General_6S* g_general_6s;

// ============================================================================
// Gripper Length (mm)
// ============================================================================

int L = 160;

// ============================================================================
// Legacy globals required by motion_control.h / probe_detect_tasks.h / etc.
// ============================================================================

bool PowerStatus = false;
bool NeedPowerOn = false;
bool NeedPowerOff = false;

std::deque<double> angle_deque_out;
std::deque<int>    tor_deque_out;
std::deque<double> trajectory;

bool is_touch_probing = false;
bool touch_detected = false;
signed int baseline_tor[6] = {0};
int TORQUE_THRESHOLD = 50;
int trigger_tor_1 = 0;
int trigger_tor_2 = 0;

int gripper_io_data = 0;
bool gripper_action_req = false;

// ============================================================================
// EtherCAT Configuration
// ============================================================================

#define FREQUENCY 1000
#define CLOCK_TO_USE CLOCK_MONOTONIC
#define CYCLIC_POSITION 8
#define NSEC_PER_SEC (1000000000L)
#define PERIOD_NS (NSEC_PER_SEC / FREQUENCY)
#define TIMESPEC2NS(T) ((uint64_t)(T).tv_sec * NSEC_PER_SEC + (T).tv_nsec)

static ec_master_t*             master = NULL;
static ec_master_state_t        master_state = {};
static ec_domain_t*             domain1 = NULL;
static ec_domain_state_t        domain1_state = {};
static ec_slave_config_t*       sc[7] = {};
static ec_slave_config_state_t  sc_state[7] = {};

int flag[7] = {0};
int flag2 = 0;

static uint8_t* domain1_pd = NULL;

#define PANASONIC_0 0,0
#define PANASONIC_1 0,1
#define PANASONIC_2 0,2
#define PANASONIC_3 0,3
#define PANASONIC_4 0,4
#define PANASONIC_5 0,5
#define IO_ban 0,6
#define NUM_SLAVES 7

uint16_t a[7] = {0};
uint16_t p[7] = {0, 1, 2, 3, 4, 5, 6};
#define VID_PID  0x00000922, 0x00000a01
#define VID_PID2 0x00000c6d, 0x00000001

// PDO offset structure
static struct {
    unsigned int ctrl_word[6];
    unsigned int operation_mode[6];
    unsigned int target_position[6];
    unsigned int touch_probe_function[6];
    unsigned int status_word[6];
    unsigned int position_actual_value[6];
    unsigned int touch_probe_status[6];
    unsigned int touch_probe_pos1_pos_value[6];
    unsigned int digital_inputs[6];
    unsigned int torque_actual_value[6];
    unsigned int BC[6];
    unsigned int F[6];
    unsigned int io_out;
    unsigned int io_in;
} offset;

const static ec_pdo_entry_reg_t domain1_regs[] = {
    {PANASONIC_0, VID_PID, 0x6040, 0, &offset.ctrl_word[0]},
    {PANASONIC_0, VID_PID, 0x607A, 0, &offset.target_position[0]},
    {PANASONIC_0, VID_PID, 0x60B8, 0, &offset.touch_probe_function[0]},
    {PANASONIC_0, VID_PID, 0x6060, 0, &offset.operation_mode[0]},
    {PANASONIC_0, VID_PID, 0x6041, 0, &offset.status_word[0]},
    {PANASONIC_0, VID_PID, 0x6064, 0, &offset.position_actual_value[0]},
    {PANASONIC_0, VID_PID, 0x60B9, 0, &offset.touch_probe_status[0]},
    {PANASONIC_0, VID_PID, 0x60BA, 0, &offset.touch_probe_pos1_pos_value[0]},
    {PANASONIC_0, VID_PID, 0x60BC, 0, &offset.BC[0]},
    {PANASONIC_0, VID_PID, 0x603F, 0, &offset.F[0]},
    {PANASONIC_0, VID_PID, 0x60FD, 0, &offset.digital_inputs[0]},
    {PANASONIC_0, VID_PID, 0x6077, 0, &offset.torque_actual_value[0]},

    {PANASONIC_1, VID_PID, 0x6040, 0, &offset.ctrl_word[1]},
    {PANASONIC_1, VID_PID, 0x607A, 0, &offset.target_position[1]},
    {PANASONIC_1, VID_PID, 0x60B8, 0, &offset.touch_probe_function[1]},
    {PANASONIC_1, VID_PID, 0x6060, 0, &offset.operation_mode[1]},
    {PANASONIC_1, VID_PID, 0x6041, 0, &offset.status_word[1]},
    {PANASONIC_1, VID_PID, 0x6064, 0, &offset.position_actual_value[1]},
    {PANASONIC_1, VID_PID, 0x60B9, 0, &offset.touch_probe_status[1]},
    {PANASONIC_1, VID_PID, 0x60BA, 0, &offset.touch_probe_pos1_pos_value[1]},
    {PANASONIC_1, VID_PID, 0x60BC, 0, &offset.BC[1]},
    {PANASONIC_1, VID_PID, 0x603F, 0, &offset.F[1]},
    {PANASONIC_1, VID_PID, 0x60FD, 0, &offset.digital_inputs[1]},
    {PANASONIC_1, VID_PID, 0x6077, 0, &offset.torque_actual_value[1]},

    {PANASONIC_2, VID_PID, 0x6040, 0, &offset.ctrl_word[2]},
    {PANASONIC_2, VID_PID, 0x607A, 0, &offset.target_position[2]},
    {PANASONIC_2, VID_PID, 0x60B8, 0, &offset.touch_probe_function[2]},
    {PANASONIC_2, VID_PID, 0x6060, 0, &offset.operation_mode[2]},
    {PANASONIC_2, VID_PID, 0x6041, 0, &offset.status_word[2]},
    {PANASONIC_2, VID_PID, 0x6064, 0, &offset.position_actual_value[2]},
    {PANASONIC_2, VID_PID, 0x60B9, 0, &offset.touch_probe_status[2]},
    {PANASONIC_2, VID_PID, 0x60BA, 0, &offset.touch_probe_pos1_pos_value[2]},
    {PANASONIC_2, VID_PID, 0x60BC, 0, &offset.BC[2]},
    {PANASONIC_2, VID_PID, 0x603F, 0, &offset.F[2]},
    {PANASONIC_2, VID_PID, 0x60FD, 0, &offset.digital_inputs[2]},
    {PANASONIC_2, VID_PID, 0x6077, 0, &offset.torque_actual_value[2]},

    {PANASONIC_3, VID_PID, 0x6040, 0, &offset.ctrl_word[3]},
    {PANASONIC_3, VID_PID, 0x607A, 0, &offset.target_position[3]},
    {PANASONIC_3, VID_PID, 0x60B8, 0, &offset.touch_probe_function[3]},
    {PANASONIC_3, VID_PID, 0x6060, 0, &offset.operation_mode[3]},
    {PANASONIC_3, VID_PID, 0x6041, 0, &offset.status_word[3]},
    {PANASONIC_3, VID_PID, 0x6064, 0, &offset.position_actual_value[3]},
    {PANASONIC_3, VID_PID, 0x60B9, 0, &offset.touch_probe_status[3]},
    {PANASONIC_3, VID_PID, 0x60BA, 0, &offset.touch_probe_pos1_pos_value[3]},
    {PANASONIC_3, VID_PID, 0x60BC, 0, &offset.BC[3]},
    {PANASONIC_3, VID_PID, 0x603F, 0, &offset.F[3]},
    {PANASONIC_3, VID_PID, 0x60FD, 0, &offset.digital_inputs[3]},
    {PANASONIC_3, VID_PID, 0x6077, 0, &offset.torque_actual_value[3]},

    {PANASONIC_4, VID_PID, 0x6040, 0, &offset.ctrl_word[4]},
    {PANASONIC_4, VID_PID, 0x607A, 0, &offset.target_position[4]},
    {PANASONIC_4, VID_PID, 0x60B8, 0, &offset.touch_probe_function[4]},
    {PANASONIC_4, VID_PID, 0x6060, 0, &offset.operation_mode[4]},
    {PANASONIC_4, VID_PID, 0x6041, 0, &offset.status_word[4]},
    {PANASONIC_4, VID_PID, 0x6064, 0, &offset.position_actual_value[4]},
    {PANASONIC_4, VID_PID, 0x60B9, 0, &offset.touch_probe_status[4]},
    {PANASONIC_4, VID_PID, 0x60BA, 0, &offset.touch_probe_pos1_pos_value[4]},
    {PANASONIC_4, VID_PID, 0x60BC, 0, &offset.BC[4]},
    {PANASONIC_4, VID_PID, 0x603F, 0, &offset.F[4]},
    {PANASONIC_4, VID_PID, 0x60FD, 0, &offset.digital_inputs[4]},
    {PANASONIC_4, VID_PID, 0x6077, 0, &offset.torque_actual_value[4]},

    {PANASONIC_5, VID_PID, 0x6040, 0, &offset.ctrl_word[5]},
    {PANASONIC_5, VID_PID, 0x607A, 0, &offset.target_position[5]},
    {PANASONIC_5, VID_PID, 0x60B8, 0, &offset.touch_probe_function[5]},
    {PANASONIC_5, VID_PID, 0x6060, 0, &offset.operation_mode[5]},
    {PANASONIC_5, VID_PID, 0x6041, 0, &offset.status_word[5]},
    {PANASONIC_5, VID_PID, 0x6064, 0, &offset.position_actual_value[5]},
    {PANASONIC_5, VID_PID, 0x60B9, 0, &offset.touch_probe_status[5]},
    {PANASONIC_5, VID_PID, 0x60BA, 0, &offset.touch_probe_pos1_pos_value[5]},
    {PANASONIC_5, VID_PID, 0x60BC, 0, &offset.BC[5]},
    {PANASONIC_5, VID_PID, 0x603F, 0, &offset.F[5]},
    {PANASONIC_5, VID_PID, 0x60FD, 0, &offset.digital_inputs[5]},
    {PANASONIC_5, VID_PID, 0x6077, 0, &offset.torque_actual_value[5]},

    {IO_ban, VID_PID2, 0x7000, 0, &offset.io_out},
    {IO_ban, VID_PID2, 0x6000, 0, &offset.io_in},
    {}
};

static ec_pdo_entry_info_t device_pdo_entries[] = {
    {0x6040, 0x00, 16}, {0x607a, 0x00, 32}, {0x60b8, 0x00, 16}, {0x6060, 0x00, 8},
    {0x6041, 0x00, 16}, {0x6064, 0x00, 32}, {0x60b9, 0x00, 16}, {0x60ba, 0x00, 32},
    {0x60bc, 0x00, 32}, {0x603f, 0x00, 16}, {0x60fd, 0x00, 32}, {0x6077, 0x00, 16}
};

static ec_pdo_info_t device_pdos[] = {
    {0x1600, 4, device_pdo_entries + 0},
    {0x1A00, 8, device_pdo_entries + 4}
};

static ec_sync_info_t device_syncs[] = {
    {0, EC_DIR_OUTPUT, 0, NULL, EC_WD_DISABLE},
    {1, EC_DIR_INPUT,  0, NULL, EC_WD_DISABLE},
    {2, EC_DIR_OUTPUT, 1, device_pdos + 0, EC_WD_ENABLE},
    {3, EC_DIR_INPUT,  1, device_pdos + 1, EC_WD_DISABLE},
    {0xFF}
};

static ec_pdo_entry_info_t device2_pdo_entries[] = {
    {0x7000, 0x00, 16},
    {0x6000, 0x00, 16},
};

static ec_pdo_info_t device2_pdos[] = {
    {0x1600, 1, device2_pdo_entries + 0},
    {0x1A00, 1, device2_pdo_entries + 1}
};

static ec_sync_info_t device2_syncs[] = {
    {0, EC_DIR_OUTPUT, 0, NULL, EC_WD_DISABLE},
    {1, EC_DIR_INPUT,  0, NULL, EC_WD_DISABLE},
    {2, EC_DIR_OUTPUT, 1, device2_pdos + 0, EC_WD_ENABLE},
    {3, EC_DIR_INPUT,  1, device2_pdos + 1, EC_WD_DISABLE},
    {0xFF}
};

static const struct timespec cycletime = {0, PERIOD_NS};

// ============================================================================
// Jog Motion Generator State (local to RT thread)
// ============================================================================

struct JogState {
    bool     active = false;
    uint8_t  mode = 0;         // 0=joint, 1=cartesian
    uint8_t  axis = 0;         // 0~5
    int8_t   direction = 0;    // +1 or -1
    double   target_velocity = 0.0;   // degrees/s (joint) or mm/s (cart pos) or deg/s (cart rot)
    double   current_velocity = 0.0;  // current ramped velocity
    int      expire_counter = 0;      // ms countdown

    // Position tracking for jog
    double   jog_target_deg[6] = {};    // joint-space target
    double   jog_target_cart[6] = {};   // cartesian target (X,Y,Z,Rx,Ry,Rz)
    bool     cart_initialized = false;

    // Acceleration parameters (set once during init)
    double   joint_accel_limit[6] = {};  // deg/s^2 per axis
    double   cart_accel_linear = 0.0;    // mm/s^2
    double   cart_accel_angular = 0.0;   // deg/s^2

    // Joint limits (safety)
    double   joint_limit_lower[6] = {-170, -120, -70, -170, -120, -360};
    double   joint_limit_upper[6] = { 170,  120, 210,  170,  120,  360};

    void reset() {
        active = false;
        current_velocity = 0.0;
        expire_counter = 0;
    }
};

// Maximum jog speeds
static constexpr double JOG_MAX_JOINT_SPEED_DPS = 30.0;   // deg/s at 100% ratio
static constexpr double JOG_MAX_CART_LINEAR_MPS = 50.0;   // mm/s at 100% ratio
static constexpr double JOG_MAX_CART_ANGULAR_DPS = 15.0;  // deg/s at 100% ratio

// Acceleration ramp: reach full speed in ~200ms
static constexpr double JOG_ACCEL_TIME_S = 0.2;
static constexpr double JOG_JOINT_ACCEL = JOG_MAX_JOINT_SPEED_DPS / JOG_ACCEL_TIME_S;
static constexpr double JOG_CART_LINEAR_ACCEL = JOG_MAX_CART_LINEAR_MPS / JOG_ACCEL_TIME_S;
static constexpr double JOG_CART_ANGULAR_ACCEL = JOG_MAX_CART_ANGULAR_DPS / JOG_ACCEL_TIME_S;

// ============================================================================
// Helper: timespec addition
// ============================================================================

static struct timespec timespec_add(struct timespec t1, struct timespec t2) {
    struct timespec result;
    result.tv_nsec = t1.tv_nsec + t2.tv_nsec;
    result.tv_sec  = t1.tv_sec + t2.tv_sec;
    if (result.tv_nsec >= NSEC_PER_SEC) {
        result.tv_sec++;
        result.tv_nsec -= NSEC_PER_SEC;
    }
    return result;
}

// ============================================================================
// EtherCAT State Checking
// ============================================================================

static void check_domain1_state() {
    ec_domain_state_t ds;
    ecrt_domain_state(domain1, &ds);
    if (ds.working_counter != domain1_state.working_counter)
        printf("Domain1: WC %u.\n", ds.working_counter);
    if (ds.wc_state != domain1_state.wc_state)
        printf("Domain1: State %u.\n", ds.wc_state);
    domain1_state = ds;
}

static void check_master_state() {
    ec_master_state_t ms;
    ecrt_master_state(master, &ms);
    if (ms.slaves_responding != master_state.slaves_responding)
        printf("%u slave(s).\n", ms.slaves_responding);
    if (ms.al_states != master_state.al_states)
        printf("AL states: 0x%02X.\n", ms.al_states);
    if (ms.link_up != master_state.link_up)
        printf("Link is %s.\n", ms.link_up ? "up" : "down");
    master_state = ms;
}

static void check_slave_config_states(ec_slave_config_t* slave_cfg, int i) {
    ec_slave_config_state_t s;
    ecrt_slave_config_state(slave_cfg, &s);
    if (s.operational == 1) flag[i] = 1;
    sc_state[i] = s;
}

// ============================================================================
// Real-Time Cyclic Task
// ============================================================================

static void cyclic_task() {
    struct timespec wakeupTime;
    clock_gettime(CLOCK_TO_USE, &wakeupTime);

    unsigned int status_check_counter = 0;
    unsigned int sync_ref_counter = 0;

    JogState jog;
    for (int i = 0; i < 6; i++) {
        jog.joint_accel_limit[i] = JOG_JOINT_ACCEL;
    }
    jog.cart_accel_linear = JOG_CART_LINEAR_ACCEL;
    jog.cart_accel_angular = JOG_CART_ANGULAR_ACCEL;

    // Hold position target (encoder increments)
    signed int hold_position[6] = {};
    bool position_initialized = false;

    while (1) {
        wakeupTime = timespec_add(wakeupTime, cycletime);
        clock_nanosleep(CLOCK_TO_USE, TIMER_ABSTIME, &wakeupTime, NULL);

        ecrt_master_application_time(master, TIMESPEC2NS(wakeupTime));
        ecrt_master_receive(master);
        ecrt_domain_process(domain1);

        check_domain1_state();

        // --- Read actual positions and torques ---
        signed int actualInc[6];
        signed int actualTor[6];
        for (int i = 0; i < 6; i++) {
            actualInc[i] = EC_READ_S32(domain1_pd + offset.position_actual_value[i]);
            actualTor[i] = EC_READ_S16(domain1_pd + offset.torque_actual_value[i]);
        }
        g_general_6s->set_act_inc(actualInc);

        // Read IO input
        uint16_t io_in_val = EC_READ_U16(domain1_pd + offset.io_in);

        // Initialize hold position on first cycle
        if (!position_initialized && PowerStatus) {
            for (int i = 0; i < 6; i++) hold_position[i] = actualInc[i];
            position_initialized = true;
        }

        // === EStop Check (highest priority) ===
        if (g_estop.load(std::memory_order_relaxed)) {
            if (PowerStatus) {
                for (int i = 0; i < 6; i++) {
                    hold_position[i] = actualInc[i];
                    EC_WRITE_S32(domain1_pd + offset.target_position[i], actualInc[i]);
                }
            }
            jog.reset();
            g_general_6s->get_angle_deque().clear();

            // Update shared state as estop
            double joints[6], tcp[6];
            for (int i = 0; i < 6; i++) joints[i] = g_general_6s->getActPositionAngle(i);
            MatrixXd fk_matrix;
            VectorXd joint_vec(6);
            for (int i = 0; i < 6; i++) joint_vec(i) = joints[i];
            g_general_6s->calc_forward_kin(joint_vec, fk_matrix);
            VectorXd cart = g_general_6s->tr_2_MCS(fk_matrix);
            for (int i = 0; i < 6; i++) tcp[i] = cart(i);
            uint32_t io_combined = (uint32_t(io_in_val) << 16) | uint32_t(EC_READ_U16(domain1_pd + offset.io_out));
            g_shared_state.write(joints, tcp, io_combined, ipc::SAFETY_ESTOP);

            goto cycle_end;
        }

        // === Servo Power State Machine (periodic check) ===
        if (status_check_counter > 0) {
            status_check_counter--;
        } else {
            status_check_counter = FREQUENCY * 2;
            check_master_state();
            for (int i = 0; i < NUM_SLAVES; i++) {
                check_slave_config_states(sc[i], i);
            }

            if (!PowerStatus && NeedPowerOn) {
                if (flag[0] && flag[1] && flag[2] && flag[3] && flag[4] && flag[5] && flag2 == 0) {
                    for (int i = 0; i < 6; i++) EC_WRITE_U16(domain1_pd + offset.ctrl_word[i], 0x0080);
                    flag2 = 2;
                } else if (flag2 == 2) {
                    for (int i = 0; i < 6; i++) EC_WRITE_U16(domain1_pd + offset.ctrl_word[i], 0x0006);
                    flag2 = 3;
                } else if (flag2 == 3) {
                    for (int i = 0; i < 6; i++) {
                        EC_WRITE_U16(domain1_pd + offset.ctrl_word[i], 0x0007);
                        EC_WRITE_S8(domain1_pd + offset.operation_mode[i], CYCLIC_POSITION);
                        EC_WRITE_S32(domain1_pd + offset.target_position[i], actualInc[i]);
                    }
                    flag2 = 4;
                } else if (flag2 == 4) {
                    for (int i = 0; i < 6; i++) {
                        EC_WRITE_U16(domain1_pd + offset.ctrl_word[i], 0x000f);
                        EC_WRITE_S32(domain1_pd + offset.target_position[i], actualInc[i]);
                    }
                    flag2 = 5;
                    PowerStatus = true;
                    NeedPowerOn = false;
                    position_initialized = true;
                    for (int i = 0; i < 6; i++) hold_position[i] = actualInc[i];

                    // Initialize jog targets from current position
                    for (int i = 0; i < 6; i++)
                        jog.jog_target_deg[i] = g_general_6s->getActPositionAngle(i);
                    jog.cart_initialized = false;

                    printf("Servo Powered On!\n");
                }
            }
        }

        // === Motion Generation ===
        if (PowerStatus) {
            // Read latest jog command from IPC
            auto cmd = g_jog_cmd.load();

            if (cmd.active) {
                // New or continuing jog
                if (!jog.active) {
                    // Jog just started: initialize from current position
                    jog.active = true;
                    jog.current_velocity = 0.0;
                    for (int i = 0; i < 6; i++)
                        jog.jog_target_deg[i] = g_general_6s->getActPositionAngle(i);
                    jog.cart_initialized = false;
                }

                jog.mode = cmd.mode;
                jog.axis = cmd.axis;
                jog.direction = cmd.direction;
                jog.expire_counter = cmd.expires_ms;

                // Compute target velocity
                if (cmd.mode == 0) {
                    // Joint space
                    jog.target_velocity = jog.direction * JOG_MAX_JOINT_SPEED_DPS * cmd.speed_ratio / 100.0;
                } else {
                    // Cartesian space
                    double max_speed = (cmd.axis < 3) ? JOG_MAX_CART_LINEAR_MPS : JOG_MAX_CART_ANGULAR_DPS;
                    jog.target_velocity = jog.direction * max_speed * cmd.speed_ratio / 100.0;
                }
            }

            if (jog.active) {
                // Decrement expire counter
                jog.expire_counter--;
                if (jog.expire_counter <= 0) {
                    // Expired: ramp down to stop
                    jog.target_velocity = 0.0;
                    // Clear the atomic command so we don't re-read stale data
                    g_jog_cmd.clear();
                }

                // Velocity ramp (trapezoidal acceleration)
                double accel_limit;
                if (jog.mode == 0) {
                    accel_limit = jog.joint_accel_limit[jog.axis];
                } else {
                    accel_limit = (jog.axis < 3) ? jog.cart_accel_linear : jog.cart_accel_angular;
                }
                double dt = 0.001; // 1ms
                double vel_diff = jog.target_velocity - jog.current_velocity;
                double max_delta = accel_limit * dt;
                if (fabs(vel_diff) <= max_delta) {
                    jog.current_velocity = jog.target_velocity;
                } else {
                    jog.current_velocity += (vel_diff > 0 ? max_delta : -max_delta);
                }

                // Check if fully stopped
                if (jog.target_velocity == 0.0 && fabs(jog.current_velocity) < 0.001) {
                    jog.current_velocity = 0.0;
                    jog.active = false;
                }

                if (jog.mode == 0) {
                    // --- Joint space jog ---
                    double increment = jog.current_velocity * dt; // degrees
                    jog.jog_target_deg[jog.axis] += increment;

                    // Joint limit check
                    if (jog.jog_target_deg[jog.axis] < jog.joint_limit_lower[jog.axis]) {
                        jog.jog_target_deg[jog.axis] = jog.joint_limit_lower[jog.axis];
                        jog.current_velocity = 0.0;
                        jog.target_velocity = 0.0;
                    }
                    if (jog.jog_target_deg[jog.axis] > jog.joint_limit_upper[jog.axis]) {
                        jog.jog_target_deg[jog.axis] = jog.joint_limit_upper[jog.axis];
                        jog.current_velocity = 0.0;
                        jog.target_velocity = 0.0;
                    }

                    // Convert to encoder increments and send
                    for (int i = 0; i < 6; i++) {
                        signed int inc;
                        g_general_6s->angleToInc(jog.jog_target_deg[i], inc, i);
                        hold_position[i] = inc;
                        EC_WRITE_S32(domain1_pd + offset.target_position[i], inc);
                    }
                } else {
                    // --- Cartesian space jog ---
                    if (!jog.cart_initialized) {
                        // Get current cartesian position via FK
                        VectorXd cur_joint(6);
                        for (int i = 0; i < 6; i++) cur_joint(i) = jog.jog_target_deg[i];
                        MatrixXd T;
                        g_general_6s->calc_forward_kin(cur_joint, T);
                        VectorXd cart_pos = g_general_6s->tr_2_MCS(T);
                        for (int i = 0; i < 6; i++) jog.jog_target_cart[i] = cart_pos(i);
                        jog.cart_initialized = true;
                    }

                    // Increment cartesian target
                    double increment = jog.current_velocity * dt;
                    jog.jog_target_cart[jog.axis] += increment;

                    // Compute IK
                    VectorXd cart_target(6);
                    for (int i = 0; i < 6; i++) cart_target(i) = jog.jog_target_cart[i];
                    MatrixXd T_target = g_general_6s->rpy_2_tr(cart_target);

                    VectorXd cur_joint_seed(6);
                    for (int i = 0; i < 6; i++) cur_joint_seed(i) = jog.jog_target_deg[i];

                    VectorXd new_joints(6);
                    g_general_6s->calc_inverse_kin(T_target, cur_joint_seed, new_joints);

                    // Validate IK result: check for NaN or excessive joint change
                    bool ik_valid = true;
                    for (int i = 0; i < 6; i++) {
                        if (std::isnan(new_joints(i)) || std::isinf(new_joints(i))) {
                            ik_valid = false;
                            break;
                        }
                        // Sanity: single cycle shouldn't move more than 1 degree
                        if (fabs(new_joints(i) - jog.jog_target_deg[i]) > 1.0) {
                            ik_valid = false;
                            break;
                        }
                        // Joint limit check
                        if (new_joints(i) < jog.joint_limit_lower[i] ||
                            new_joints(i) > jog.joint_limit_upper[i]) {
                            ik_valid = false;
                            break;
                        }
                    }

                    if (ik_valid) {
                        for (int i = 0; i < 6; i++) jog.jog_target_deg[i] = new_joints(i);
                        for (int i = 0; i < 6; i++) {
                            signed int inc;
                            g_general_6s->angleToInc(jog.jog_target_deg[i], inc, i);
                            hold_position[i] = inc;
                            EC_WRITE_S32(domain1_pd + offset.target_position[i], inc);
                        }
                    } else {
                        // IK failed: revert cartesian increment, hold position
                        jog.jog_target_cart[jog.axis] -= increment;
                        jog.current_velocity = 0.0;
                        jog.target_velocity = 0.0;
                        for (int i = 0; i < 6; i++) {
                            EC_WRITE_S32(domain1_pd + offset.target_position[i], hold_position[i]);
                        }
                    }
                }
            } else if (!g_general_6s->get_angle_deque().empty()) {
                // Legacy trajectory playback (for other subsystems)
                for (int i = 0; i < 6; i++) {
                    signed int target_inc = g_general_6s->set_target_pos_to_servo(i);
                    hold_position[i] = target_inc;
                    EC_WRITE_S32(domain1_pd + offset.target_position[i], target_inc);
                    actualTor[i] = EC_READ_S16(domain1_pd + offset.torque_actual_value[i]);
                    tor_deque_out.push_back(actualTor[i]);
                    angle_deque_out.push_back(g_general_6s->getActPositionAngle(i));
                }

                // Touch probe detection (legacy support)
                if (is_touch_probing && !touch_detected) {
                    if (abs(actualTor[1] - baseline_tor[1]) > 1.5 * TORQUE_THRESHOLD ||
                        abs(actualTor[2] - baseline_tor[2]) > TORQUE_THRESHOLD) {
                        trigger_tor_1 = actualTor[1];
                        trigger_tor_2 = actualTor[2];
                        touch_detected = true;
                        g_general_6s->get_angle_deque().clear();
                    }
                }
            } else {
                // No motion: hold position
                for (int i = 0; i < 6; i++) {
                    EC_WRITE_S32(domain1_pd + offset.target_position[i], hold_position[i]);
                }
            }
        }

        // === IO Command Processing ===
        ipc::IOCommand io_cmd;
        while (g_io_queue.try_pop(io_cmd)) {
            // Map pin/value to IO output register
            // Pin 0~15 maps to bits of the 16-bit IO output
            uint16_t current_io_out = EC_READ_U16(domain1_pd + offset.io_out);
            if (io_cmd.value) {
                current_io_out |= (1u << io_cmd.pin);
            } else {
                current_io_out &= ~(1u << io_cmd.pin);
            }
            EC_WRITE_U16(domain1_pd + offset.io_out, current_io_out);
        }

        // Legacy gripper control
        if (gripper_action_req) {
            EC_WRITE_S32(domain1_pd + offset.io_out, gripper_io_data);
            gripper_action_req = false;
        }

        // === Update Shared State for IPC ===
        {
            double joints[6], tcp[6];
            for (int i = 0; i < 6; i++) joints[i] = g_general_6s->getActPositionAngle(i);

            VectorXd joint_vec(6);
            for (int i = 0; i < 6; i++) joint_vec(i) = joints[i];
            MatrixXd fk_matrix;
            g_general_6s->calc_forward_kin(joint_vec, fk_matrix);
            VectorXd cart = g_general_6s->tr_2_MCS(fk_matrix);
            for (int i = 0; i < 6; i++) tcp[i] = cart(i);

            uint32_t io_combined = (uint32_t(io_in_val) << 16)
                                 | uint32_t(EC_READ_U16(domain1_pd + offset.io_out));
            uint8_t safety = jog.active || !g_general_6s->get_angle_deque().empty()
                           ? ipc::SAFETY_MOVING : ipc::SAFETY_BRAKED;
            g_shared_state.write(joints, tcp, io_combined, safety);
        }

    cycle_end:
        // === Clock Synchronization ===
        if (sync_ref_counter > 0) {
            sync_ref_counter--;
        } else {
            sync_ref_counter = 1;
            ecrt_master_sync_reference_clock(master);
        }
        ecrt_master_sync_slave_clocks(master);

        ecrt_domain_queue(domain1);
        ecrt_master_send(master);
    }
}

// ============================================================================
// EtherCAT Master Initialization
// ============================================================================

static int init_ethercat() {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        perror("mlockall failed");
        return -1;
    }

    master = ecrt_request_master(0);
    if (!master) return -1;

    domain1 = ecrt_master_create_domain(master);
    if (!domain1) return -1;

    for (int i = 0; i < NUM_SLAVES; i++) {
        if (i < 6) {
            if (!(sc[i] = ecrt_master_slave_config(master, a[i], p[i], VID_PID))) {
                fprintf(stderr, "获取伺服从站 %d 配置失败.\n", i);
                return -1;
            }
            if (ecrt_slave_config_pdos(sc[i], EC_END, device_syncs)) {
                fprintf(stderr, "配置伺服从站 %d PDO 失败.\n", i);
                return -1;
            }
        } else {
            if (!(sc[i] = ecrt_master_slave_config(master, a[i], p[i], VID_PID2))) {
                fprintf(stderr, "获取IO从站 %d 配置失败.\n", i);
                return -1;
            }
            if (ecrt_slave_config_pdos(sc[i], EC_END, device2_syncs)) {
                fprintf(stderr, "配置IO从站 %d PDO 失败.\n", i);
                return -1;
            }
        }
    }
    printf("成功配置从站 PDO!\n");

    if (ecrt_domain_reg_pdo_entry_list(domain1, domain1_regs)) {
        fprintf(stderr, "PDO 条目注册失败!\n");
        return -1;
    }

    for (int i = 0; i < 6; i++) {
        ecrt_slave_config_dc(sc[i], 0x0300, PERIOD_NS, PERIOD_NS / 2, 0, 0);
    }

    printf("激活 EtherCAT 主站...\n");
    if (ecrt_master_activate(master)) return -1;

    if (!(domain1_pd = ecrt_domain_data(domain1))) return -1;

    return 0;
}

// ============================================================================
// Robot Parameters Initialization
// ============================================================================

static void init_robot_params() {
    DH_param dh;
    dh.a[0] = 0.0408; dh.a[1] = 450.342; dh.a[2] = 99.107;
    dh.a[3] = 0.0;    dh.a[4] = 0.0;     dh.a[5] = 0.0;
    dh.alpha[0] = M_PI * 90 / 180;  dh.alpha[1] = 0.0;               dh.alpha[2] = M_PI * 90 / 180;
    dh.alpha[3] = M_PI * 90 / 180;  dh.alpha[4] = M_PI * (-90) / 180; dh.alpha[5] = 0.0;
    dh.d[0] = 390;    dh.d[1] = 0.4997; dh.d[2] = 0.0;
    dh.d[3] = 470.557; dh.d[4] = 0.0;   dh.d[5] = 123 + L;
    dh.theta[0] = 0.0;              dh.theta[1] = M_PI * 90 / 180;  dh.theta[2] = 0.0;
    dh.theta[3] = 0.0;              dh.theta[4] = M_PI * 90 / 180;  dh.theta[5] = 0.0;

    Decare_Para decare;
    decare.maxacc = 5; decare.maxdec = -5; decare.maxjerk = 10000; decare.maxvel = 3000;

    Motor_Param motor_pa;
    motor_pa.encoder.reducRatio[0] = 80.007;  motor_pa.encoder.reducRatio[1] = 109.837;
    motor_pa.encoder.reducRatio[2] = 100.024; motor_pa.encoder.reducRatio[3] = 118.996;
    motor_pa.encoder.reducRatio[4] = 80.007;  motor_pa.encoder.reducRatio[5] = 79.977;

    motor_pa.encoder.singleTurnEncoder[0] = 237.172852; motor_pa.encoder.singleTurnEncoder[1] = 207.078552;
    motor_pa.encoder.singleTurnEncoder[2] = 131.119080; motor_pa.encoder.singleTurnEncoder[3] = 238.971863;
    motor_pa.encoder.singleTurnEncoder[4] = 31.110535;  motor_pa.encoder.singleTurnEncoder[5] = 100.274963;

    motor_pa.encoder.direction[0] = -1; motor_pa.encoder.direction[1] =  1;
    motor_pa.encoder.direction[2] =  1; motor_pa.encoder.direction[3] = -1;
    motor_pa.encoder.direction[4] =  1; motor_pa.encoder.direction[5] = -1;

    motor_pa.RatedVel_rpm[0] = 450; motor_pa.RatedVel_rpm[1] = 350;
    motor_pa.RatedVel_rpm[2] = 450; motor_pa.RatedVel_rpm[3] = 350;
    motor_pa.RatedVel_rpm[4] = 450; motor_pa.RatedVel_rpm[5] = 450;

    for (int i = 0; i < 6; i++) {
        motor_pa.encoder.deviation[i] = 0;
        motor_pa.encoder.encoderResolution[i] = 23;
        motor_pa.maxAcc[i] = 5.0;
        motor_pa.maxDecel[i] = -5.0;
        motor_pa.maxRotSpeed[i] = 5000;
        motor_pa.RatedVel[i] = motor_pa.RatedVel_rpm[i] * 6.0 / motor_pa.encoder.reducRatio[i];
        motor_pa.DeRatedVel[i] = -motor_pa.RatedVel[i];
    }

    g_general_6s->set_param(motor_pa.encoder, motor_pa, dh, decare);
}

// ============================================================================
// Entry Point
// ============================================================================

int main(int argc, char* argv[]) {
    printf("=== RoboXORT Controller ===\n");

    // 1. Initialize robot algorithm object
    g_general_6s = new General_6S();
    printf("算法对象初始化成功.\n");

    init_robot_params();
    printf("机器人参数配置完成.\n");

    // 2. Initialize EtherCAT
    if (init_ethercat() != 0) {
        fprintf(stderr, "EtherCAT 初始化失败!\n");
        return 1;
    }

    // 3. Start RT cyclic task in a dedicated thread
    std::thread rt_thread([]() {
        struct sched_param param = {};
        param.sched_priority = sched_get_priority_max(SCHED_FIFO);
        printf("RT 线程优先级: %d\n", param.sched_priority);
        if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
            perror("sched_setscheduler failed");
        }
        cyclic_task();
    });

    // 4. Wait for slaves to reach OP state
    printf("等待从站进入 OP 状态...\n");
    for (int wait = 0; wait < 30; wait++) {
        if (flag[0] && flag[1] && flag[2] && flag[3] && flag[4] && flag[5]) {
            printf("所有从站已就绪，耗时 %d 秒。\n", wait);
            break;
        }
        sleep(1);
        if (wait == 29) {
            printf("警告: 等待从站进入 OP 状态超时 (30秒)!\n");
        }
    }

    // 5. Request servo power on
    NeedPowerOn = true;
    printf("请求伺服上电...\n");
    for (int wait = 0; wait < 10; wait++) {
        if (PowerStatus) {
            printf("伺服上电完成.\n");
            break;
        }
        sleep(1);
    }

    // 6. Start IPC server thread
    ipc::IPCServer ipc_server;
    std::thread ipc_thread([&ipc_server]() {
        ipc_server.run();
    });

    printf("系统就绪，等待 IPC 连接...\n");

    // 7. Main thread waits (signal handling for graceful shutdown)
    sigset_t waitset;
    sigemptyset(&waitset);
    sigaddset(&waitset, SIGINT);
    sigaddset(&waitset, SIGTERM);
    sigprocmask(SIG_BLOCK, &waitset, NULL);

    int sig;
    sigwait(&waitset, &sig);
    printf("\n收到信号 %d，正在关闭...\n", sig);

    // Graceful shutdown
    g_estop.store(true, std::memory_order_release);
    ipc_server.stop();

    if (ipc_thread.joinable()) ipc_thread.join();
    // RT thread runs forever (detach or let process exit)
    rt_thread.detach();

    printf("控制器已关闭.\n");
    return 0;
}
