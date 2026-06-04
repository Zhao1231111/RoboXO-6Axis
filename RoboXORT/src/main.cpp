#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sched.h>
#include <time.h>
#include <thread>
#include <deque>
#include <cmath>
#include <cstdlib>
#include <Eigen/Eigen>
#include "general_6s.h"
#include "ethercat_config.h"
#include "robot_params.h"
#include "ipc_protocol.h"
#include "ipc_server.h"
#include "teleop_keyboard.h"
#include "chessboard_tasks.h"
#include "calibration_task.h"

using namespace Eigen;
using namespace std;

// ============================================================================
// Global Shared State (IPC <-> RT)
// ============================================================================

ipc::SharedRobotState                g_shared_state;
ipc::JogCommandPacked                g_jog_cmd;
std::atomic<bool>                    g_estop{false};
ipc::SPSCQueue<ipc::IOCommand, 16>  g_io_queue;
ipc::SPSCQueue<ipc::TaskCommandPayload, 16> g_task_queue;
std::atomic<bool>                    g_abort_trajectory{false};

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
// Jog Motion Generator State (local to RT thread)
// ============================================================================

struct JogState {
    bool     active = false;
    uint8_t  mode = 0;
    uint8_t  axis = 0;
    int8_t   direction = 0;
    double   target_velocity = 0.0;
    double   current_velocity = 0.0;
    int      expire_counter = 0;

    double   jog_target_deg[6] = {};
    double   jog_target_cart[6] = {};
    bool     cart_initialized = false;

    double   joint_accel_limit[6] = {};
    double   cart_accel_linear = 0.0;
    double   cart_accel_angular = 0.0;

    double   joint_limit_lower[6] = {-170, -120, -70, -170, -120, -360};
    double   joint_limit_upper[6] = { 170,  120, 210,  170,  120,  360};

    void reset() {
        active = false;
        current_velocity = 0.0;
        expire_counter = 0;
    }
};

static constexpr double JOG_MAX_JOINT_SPEED_DPS = 10.0;
static constexpr double JOG_MAX_CART_LINEAR_MPS = 10.0;
static constexpr double JOG_MAX_CART_ANGULAR_DPS = 5.0;
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
    if (result.tv_nsec >= EC_NSEC_PER_SEC) {
        result.tv_sec++;
        result.tv_nsec -= EC_NSEC_PER_SEC;
    }
    return result;
}

// ============================================================================
// Real-Time Cyclic Task
// ============================================================================

static void cyclic_task() {
    struct timespec wakeupTime;
    clock_gettime(EC_CLOCK_TO_USE, &wakeupTime);

    unsigned int status_check_counter = 0;
    unsigned int sync_ref_counter = 0;

    JogState jog;
    for (int i = 0; i < 6; i++) jog.joint_accel_limit[i] = JOG_JOINT_ACCEL;
    jog.cart_accel_linear = JOG_CART_LINEAR_ACCEL;
    jog.cart_accel_angular = JOG_CART_ANGULAR_ACCEL;

    signed int hold_position[6] = {};
    bool position_initialized = false;

    extern General_6S* g_general_6s;

    while (1) {
        wakeupTime = timespec_add(wakeupTime, ec_cycletime);
        clock_nanosleep(EC_CLOCK_TO_USE, TIMER_ABSTIME, &wakeupTime, NULL);

        if (!g_sim_mode) {
            ecrt_master_application_time(ec_master, EC_TIMESPEC2NS(wakeupTime));
            ecrt_master_receive(ec_master);
            ecrt_domain_process(ec_domain);
            ec_check_domain_state();
        }

        // --- Read actual positions and torques ---
        signed int actualInc[6];
        signed int actualTor[6];
        for (int i = 0; i < 6; i++) {
            actualInc[i] = EC_READ_S32(ec_domain_pd + ec_offsets.position_actual_value[i]);
            actualTor[i] = EC_READ_S16(ec_domain_pd + ec_offsets.torque_actual_value[i]);
        }
        g_general_6s->set_act_inc(actualInc);

        uint16_t io_in_val = EC_READ_U16(ec_domain_pd + ec_offsets.io_in);

        if (!position_initialized && PowerStatus) {
            for (int i = 0; i < 6; i++) hold_position[i] = actualInc[i];
            position_initialized = true;
        }

        // === EStop Check (highest priority) ===
        if (g_estop.load(std::memory_order_relaxed)) {
            if (PowerStatus) {
                for (int i = 0; i < 6; i++) {
                    hold_position[i] = actualInc[i];
                    EC_WRITE_S32(ec_domain_pd + ec_offsets.target_position[i], actualInc[i]);
                }
            }
            jog.reset();
            g_abort_trajectory.store(true, std::memory_order_release);

            double joints[6], tcp[6];
            for (int i = 0; i < 6; i++) joints[i] = g_general_6s->getActPositionAngle(i);
            VectorXd jv(6);
            for (int i = 0; i < 6; i++) jv(i) = joints[i];
            MatrixXd T;
            g_general_6s->calc_forward_kin(jv, T);
            VectorXd c = g_general_6s->tr_2_MCS(T);
            for (int i = 0; i < 6; i++) tcp[i] = c(i);
            for (int i = 3; i < 6; i++) tcp[i] = rad2deg(tcp[i]);
            uint32_t io = (uint32_t(io_in_val) << 16) | uint32_t(EC_READ_U16(ec_domain_pd + ec_offsets.io_out));
            g_shared_state.write(joints, tcp, io, ipc::SAFETY_ESTOP);

            goto cycle_end;
        }

        // === Servo Power State Machine ===
        if (!g_sim_mode) {
            if (status_check_counter > 0) {
                status_check_counter--;
            } else {
                status_check_counter = EC_FREQUENCY * 2;
                ec_check_master_state();
                for (int i = 0; i < NUM_SLAVES; i++) ec_check_slave_state(i);

                if (!PowerStatus && NeedPowerOn) {
                    bool all_op = true;
                    for (int i = 0; i < NUM_SERVO_AXES; i++) {
                        if (!ec_slave_op_flag[i]) { all_op = false; break; }
                    }

                    if (all_op && ec_power_state_machine == 0) {
                        for (int i = 0; i < 6; i++)
                            EC_WRITE_U16(ec_domain_pd + ec_offsets.ctrl_word[i], 0x0080);
                        ec_power_state_machine = 2;
                    } else if (ec_power_state_machine == 2) {
                        for (int i = 0; i < 6; i++)
                            EC_WRITE_U16(ec_domain_pd + ec_offsets.ctrl_word[i], 0x0006);
                        ec_power_state_machine = 3;
                    } else if (ec_power_state_machine == 3) {
                        for (int i = 0; i < 6; i++) {
                            EC_WRITE_U16(ec_domain_pd + ec_offsets.ctrl_word[i], 0x0007);
                            EC_WRITE_S8(ec_domain_pd + ec_offsets.operation_mode[i], CYCLIC_POSITION);
                            EC_WRITE_S32(ec_domain_pd + ec_offsets.target_position[i], actualInc[i]);
                        }
                        ec_power_state_machine = 4;
                    } else if (ec_power_state_machine == 4) {
                        for (int i = 0; i < 6; i++) {
                            EC_WRITE_U16(ec_domain_pd + ec_offsets.ctrl_word[i], 0x000f);
                            EC_WRITE_S32(ec_domain_pd + ec_offsets.target_position[i], actualInc[i]);
                        }
                        ec_power_state_machine = 5;
                        PowerStatus = true;
                        NeedPowerOn = false;
                        position_initialized = true;
                        for (int i = 0; i < 6; i++) hold_position[i] = actualInc[i];
                        for (int i = 0; i < 6; i++)
                            jog.jog_target_deg[i] = g_general_6s->getActPositionAngle(i);
                        jog.cart_initialized = false;
                        printf("伺服上电成功.\n");
                    }
                }
            }
        }

        // === Motion Generation ===
        if (PowerStatus) {
            auto cmd = g_jog_cmd.load();

            if (cmd.active) {
                if (!jog.active) {
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

                if (cmd.mode == 0) {
                    jog.target_velocity = jog.direction * JOG_MAX_JOINT_SPEED_DPS * cmd.speed_ratio / 100.0;
                } else {
                    double max_spd = (cmd.axis < 3) ? JOG_MAX_CART_LINEAR_MPS : JOG_MAX_CART_ANGULAR_DPS;
                    jog.target_velocity = jog.direction * max_spd * cmd.speed_ratio / 100.0;
                }
            }

            if (jog.active) {
                jog.expire_counter--;
                if (jog.expire_counter <= 0) {
                    jog.target_velocity = 0.0;
                    g_jog_cmd.clear();
                }

                double accel = (jog.mode == 0) ? jog.joint_accel_limit[jog.axis]
                             : (jog.axis < 3) ? jog.cart_accel_linear : jog.cart_accel_angular;
                double dt = 0.001;
                double vel_diff = jog.target_velocity - jog.current_velocity;
                double max_delta = accel * dt;
                if (fabs(vel_diff) <= max_delta)
                    jog.current_velocity = jog.target_velocity;
                else
                    jog.current_velocity += (vel_diff > 0 ? max_delta : -max_delta);

                if (jog.target_velocity == 0.0 && fabs(jog.current_velocity) < 0.001) {
                    jog.current_velocity = 0.0;
                    jog.active = false;
                }

                if (jog.mode == 0) {
                    // Joint space jog
                    jog.jog_target_deg[jog.axis] += jog.current_velocity * dt;
                    if (jog.jog_target_deg[jog.axis] < jog.joint_limit_lower[jog.axis]) {
                        jog.jog_target_deg[jog.axis] = jog.joint_limit_lower[jog.axis];
                        jog.current_velocity = 0.0; jog.target_velocity = 0.0;
                    }
                    if (jog.jog_target_deg[jog.axis] > jog.joint_limit_upper[jog.axis]) {
                        jog.jog_target_deg[jog.axis] = jog.joint_limit_upper[jog.axis];
                        jog.current_velocity = 0.0; jog.target_velocity = 0.0;
                    }
                    for (int i = 0; i < 6; i++) {
                        signed int inc;
                        g_general_6s->angleToInc(jog.jog_target_deg[i], inc, i);
                        hold_position[i] = inc;
                        EC_WRITE_S32(ec_domain_pd + ec_offsets.target_position[i], inc);
                    }
                } else {
                    // Cartesian space jog
                    if (!jog.cart_initialized) {
                        VectorXd cj(6);
                        for (int i = 0; i < 6; i++) cj(i) = jog.jog_target_deg[i];
                        MatrixXd T;
                        g_general_6s->calc_forward_kin(cj, T);
                        VectorXd cp = g_general_6s->tr_2_MCS(T);
                        for (int i = 0; i < 6; i++) jog.jog_target_cart[i] = cp(i);
                        jog.cart_initialized = true;
                    }

                    double increment = jog.current_velocity * dt;
                    double cart_increment = (jog.axis >= 3) ? deg2rad(increment) : increment;
                    jog.jog_target_cart[jog.axis] += cart_increment;

                    VectorXd ct(6);
                    for (int i = 0; i < 6; i++) ct(i) = jog.jog_target_cart[i];
                    MatrixXd Tt = g_general_6s->rpy_2_tr(ct);
                    VectorXd seed(6);
                    for (int i = 0; i < 6; i++) seed(i) = jog.jog_target_deg[i];
                    VectorXd nj(6);
                    g_general_6s->calc_inverse_kin(Tt, seed, nj);

                    bool ik_ok = true;
                    for (int i = 0; i < 6; i++) {
                        if (std::isnan(nj(i)) || std::isinf(nj(i)) ||
                            fabs(nj(i) - jog.jog_target_deg[i]) > 1.0 ||
                            nj(i) < jog.joint_limit_lower[i] ||
                            nj(i) > jog.joint_limit_upper[i]) {
                            ik_ok = false; break;
                        }
                    }

                    if (ik_ok) {
                        for (int i = 0; i < 6; i++) jog.jog_target_deg[i] = nj(i);
                        for (int i = 0; i < 6; i++) {
                            signed int inc;
                            g_general_6s->angleToInc(jog.jog_target_deg[i], inc, i);
                            hold_position[i] = inc;
                            EC_WRITE_S32(ec_domain_pd + ec_offsets.target_position[i], inc);
                        }
                    } else {
                        jog.jog_target_cart[jog.axis] -= cart_increment;
                        jog.current_velocity = 0.0;
                        jog.target_velocity = 0.0;
                        for (int i = 0; i < 6; i++)
                            EC_WRITE_S32(ec_domain_pd + ec_offsets.target_position[i], hold_position[i]);
                    }
                }
            } else if (!g_general_6s->get_angle_deque().empty() && !g_abort_trajectory.load(std::memory_order_relaxed)) {
                // Angle Deque 轨迹处理
                
                for (int i = 0; i < 6; i++) {
                    signed int target_inc = g_general_6s->set_target_pos_to_servo(i);
                    hold_position[i] = target_inc;
                    // 写入目标位置
                    EC_WRITE_S32(ec_domain_pd + ec_offsets.target_position[i], target_inc);
                    // 读取实时力矩并记录
                    actualTor[i] = EC_READ_S16(ec_domain_pd + ec_offsets.torque_actual_value[i]);
                    tor_deque_out.push_back(actualTor[i]);
                    angle_deque_out.push_back(g_general_6s->getActPositionAngle(i));
                }

                // --- 接触检测逻辑 ---
                if (is_touch_probing && !touch_detected) {
                    // 判断 J2(i=1) 或 J3(i=2) 是否受力突变
                    if (abs(actualTor[1] - baseline_tor[1]) > 1.5 * TORQUE_THRESHOLD ||
                        abs(actualTor[2] - baseline_tor[2]) > TORQUE_THRESHOLD) {
                        trigger_tor_1 = actualTor[1];
                        trigger_tor_2 = actualTor[2];
                        touch_detected = true;
                        // 在 EtherCAT 线程内部设置中止标志，通知非实时线程安全清空队列
                        g_abort_trajectory.store(true, std::memory_order_release);
                    }
                }
            } else {
                // Hold position
                for (int i = 0; i < 6; i++)
                    EC_WRITE_S32(ec_domain_pd + ec_offsets.target_position[i], hold_position[i]);
            }
        }

        // === IO Command Processing ===
        if (gripper_action_req) {
            EC_WRITE_U16(ec_domain_pd + ec_offsets.io_out, static_cast<uint16_t>(gripper_io_data));
            gripper_action_req = false;
        } else {
            ipc::IOCommand io_cmd;
            bool io_dirty = false;
            uint16_t current_io_out = EC_READ_U16(ec_domain_pd + ec_offsets.io_out);
            while (g_io_queue.try_pop(io_cmd)) {
                if (io_cmd.pin > 15) continue;
                if (io_cmd.value)
                    current_io_out |= (1u << io_cmd.pin);
                else
                    current_io_out &= ~(1u << io_cmd.pin);
                io_dirty = true;
            }
            if (io_dirty)
                EC_WRITE_U16(ec_domain_pd + ec_offsets.io_out, current_io_out);
        }

        // === CSP Velocity Clamp ===
        {
            static signed int prev_target[6] = {};
            static bool clamp_initialized = false;
            if (PowerStatus && !clamp_initialized) {
                for (int i = 0; i < 6; i++)
                    prev_target[i] = EC_READ_S32(ec_domain_pd + ec_offsets.target_position[i]);
                clamp_initialized = true;
            }
            if (clamp_initialized) {
                for (int i = 0; i < 6; i++) {
                    signed int desired = EC_READ_S32(ec_domain_pd + ec_offsets.target_position[i]);
                    signed int delta = desired - prev_target[i];
                    if (abs(delta) > csp_max_inc_per_cycle[i]) {
                        desired = prev_target[i] + (delta > 0 ? csp_max_inc_per_cycle[i] : -csp_max_inc_per_cycle[i]);
                        EC_WRITE_S32(ec_domain_pd + ec_offsets.target_position[i], desired);
                        hold_position[i] = desired;
                        printf("[WARN] CSP clamp axis %d: delta=%d > limit=%d\n", i, delta, csp_max_inc_per_cycle[i]);
                    }
                    prev_target[i] = desired;
                }
            }
        }

        // === Update Shared State ===
        {
            double joints[6], tcp[6];
            for (int i = 0; i < 6; i++) joints[i] = g_general_6s->getActPositionAngle(i);
            VectorXd jv(6);
            for (int i = 0; i < 6; i++) jv(i) = joints[i];
            MatrixXd T;
            g_general_6s->calc_forward_kin(jv, T);
            VectorXd c = g_general_6s->tr_2_MCS(T);
            for (int i = 0; i < 6; i++) tcp[i] = c(i);
            for (int i = 3; i < 6; i++) tcp[i] = rad2deg(tcp[i]);

            uint32_t io = (uint32_t(io_in_val) << 16) | uint32_t(EC_READ_U16(ec_domain_pd + ec_offsets.io_out));
            uint8_t safety = (jog.active || !g_general_6s->get_angle_deque().empty())
                           ? ipc::SAFETY_MOVING : ipc::SAFETY_BRAKED;
            g_shared_state.write(joints, tcp, io, safety);
        }

    cycle_end:
        if (g_sim_mode) {
            for (int i = 0; i < 6; i++) {
                signed int t = EC_READ_S32(ec_domain_pd + ec_offsets.target_position[i]);
                EC_WRITE_S32(ec_domain_pd + ec_offsets.position_actual_value[i], t);
            }
            static int sim_print_ctr = 0;
            // if (++sim_print_ctr >= 100) {
            //     sim_print_ctr = 0;
            //     double ja[6];
            //     for (int i = 0; i < 6; i++) ja[i] = g_general_6s->getActPositionAngle(i);
            //     printf("[SIM] Joints: [%7.2f, %7.2f, %7.2f, %7.2f, %7.2f, %7.2f]\n",
            //            ja[0], ja[1], ja[2], ja[3], ja[4], ja[5]);
            // }
        } else {
            if (sync_ref_counter > 0) {
                sync_ref_counter--;
            } else {
                sync_ref_counter = 1;
                ecrt_master_sync_reference_clock(ec_master);
            }
            ecrt_master_sync_slave_clocks(ec_master);
            ecrt_domain_queue(ec_domain);
            ecrt_master_send(ec_master);
        }
    }
}


// ============================================================================
// 功能测试
// ============================================================================
// --- 初始化与功能测试 ---
void test_robot_func() {
    // 1. 设置机器人的 DH 参数 (Denavit-Hartenberg，用于运动学正逆解)
    DH_param dh_example;
    dh_example.a[0] = 0.0408; dh_example.a[1] = 450.342; dh_example.a[2] = 99.107; 
    dh_example.a[3] = 0.0; dh_example.a[4] = 0.0; dh_example.a[5] = 0.0;
    dh_example.alpha[0] = M_PI * 90 / 180; dh_example.alpha[1] = M_PI * 0 / 180; dh_example.alpha[2] = M_PI * 90 / 180;
    dh_example.alpha[3] = M_PI * 90 / 180; dh_example.alpha[4] = M_PI * (-90) / 180; dh_example.alpha[5] = M_PI * 0 / 180;
    dh_example.d[0] = 390; dh_example.d[1] = 0.4997; dh_example.d[2] = 0.0;
    dh_example.d[3] = 470.557; dh_example.d[4] = 0.0; dh_example.d[5] = 123 + L; 
    dh_example.theta[0] = M_PI * 0 / 180; dh_example.theta[1] = M_PI * 90 / 180; dh_example.theta[2] = M_PI * 0 / 180;
    dh_example.theta[3] = M_PI * 0 / 180; dh_example.theta[4] = M_PI * 90 / 180; dh_example.theta[5] = M_PI * 0 / 180;

    // 2. 设置笛卡尔空间运动参数
    Decare_Para decare;
    decare.maxacc = 5; decare.maxdec = -5; decare.maxjerk = 10000; decare.maxvel = 3000;

    // 3. 设置电机与编码器参数
    Motor_Param motor_pa;
    motor_pa.encoder.reducRatio[0] = 80.007; motor_pa.encoder.reducRatio[1] = 109.837; motor_pa.encoder.reducRatio[2] = 100.024;
    motor_pa.encoder.reducRatio[3] = 118.996; motor_pa.encoder.reducRatio[4] = 80.007; motor_pa.encoder.reducRatio[5] = 79.977;
    
    motor_pa.encoder.singleTurnEncoder[0] = 237.172852; motor_pa.encoder.singleTurnEncoder[1] = 207.078552; motor_pa.encoder.singleTurnEncoder[2] = 131.119080;
    motor_pa.encoder.singleTurnEncoder[3] = 238.971863; motor_pa.encoder.singleTurnEncoder[4] = 31.110535; motor_pa.encoder.singleTurnEncoder[5] = 100.274963;
    
    motor_pa.encoder.direction[0] = -1; motor_pa.encoder.direction[1] = 1; motor_pa.encoder.direction[2] = 1;
    motor_pa.encoder.direction[3] = -1; motor_pa.encoder.direction[4] = 1; motor_pa.encoder.direction[5] = -1;

    motor_pa.RatedVel_rpm[0] = 450; motor_pa.RatedVel_rpm[1] = 350; motor_pa.RatedVel_rpm[2] = 450;
    motor_pa.RatedVel_rpm[3] = 350; motor_pa.RatedVel_rpm[4] = 450; motor_pa.RatedVel_rpm[5] = 450;

    for (int i = 0; i < 6; i++) {
        motor_pa.encoder.deviation[i] = 0;
        motor_pa.encoder.encoderResolution[i] = 23;
        motor_pa.maxAcc[i] = 5.0;
        motor_pa.maxDecel[i] = -5.0;
        motor_pa.maxRotSpeed[i] = 5000;
        motor_pa.RatedVel[i] = motor_pa.RatedVel_rpm[i] * 6 / motor_pa.encoder.reducRatio[i];
        motor_pa.DeRatedVel[i] = -motor_pa.RatedVel[i];
    }

    extern General_6S* g_general_6s;
    
    // 初始化算法层对象
    g_general_6s->set_param(motor_pa.encoder, motor_pa, dh_example, decare);
    
    // --- 运动学正解测试 ---
    VectorXd pos_acs(6);
    pos_acs << 0.5, 0, 0, 0, 0, 0; // 输入测试角度
    MatrixXd trans_matrix;
    g_general_6s->calc_forward_kin(pos_acs, trans_matrix);
    
    printf("正运动学变换矩阵:\n");
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) printf("%lf ", trans_matrix(i, j));
        printf("\n");
    }
    printf("\n");
    
    // print_current_pos(motor_pa.encoder);
    
    
    // 触发任务状态机
    NeedPowerOn = 1;
    sleep(5);
    // multi_joint_move_test(); // 原来的调用方式
    // run_task_state_machine(); // 新的任务状态机调用
    run_calibration_task();
    // draw_tic_tac_toe_task();
}

// ============================================================================
// Usage
// ============================================================================

static void print_usage(const char* prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("Options:\n");
    printf("  --sim       Run in simulation mode (no EtherCAT hardware required)\n");
    printf("  --ipc       Start the IPC server (If none, does test_robot_func)\n");
    printf("  --teleop    Start keyboard teleop interface instead of IPC\n");
    printf("  --help      Show this help message\n");
}

// ============================================================================
// Entry Point
// ============================================================================

int main(int argc, char* argv[]) {
    bool enable_ipc = false;
    bool enable_teleop = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sim") == 0) {
            g_sim_mode = true;
        } else if (strcmp(argv[i], "--ipc") == 0) {
            enable_ipc = true;
        } else if (strcmp(argv[i], "--teleop") == 0) {
            enable_teleop = true;
            enable_ipc = false;  // teleop replaces IPC
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    printf("=== RoboXORT Controller ===\n");

    // 1. Initialize robot algorithm object
    extern General_6S* g_general_6s;
    g_general_6s = new General_6S();
    printf("算法对象初始化成功.\n");

    init_robot_params();
    compute_csp_limits();
    printf("机器人参数配置完成.\n");

    // 2. Initialize EtherCAT (or simulation)
    if (g_sim_mode) {
        if (ec_init_sim() != 0) {
            fprintf(stderr, "仿真模式初始化失败!\n");
            return 1;
        }
        for (int i = 0; i < 6; i++) {
            signed int inc;
            g_general_6s->angleToInc(0.0, inc, i);
            EC_WRITE_S32(ec_domain_pd + ec_offsets.position_actual_value[i], inc);
            EC_WRITE_S32(ec_domain_pd + ec_offsets.target_position[i], inc);
        }
        PowerStatus = true;
        printf("[SIM] 现在是仿真模式.\n");
    } else {
        if (ec_init() != 0) {
            fprintf(stderr, "EtherCAT 初始化失败!\n");
            return 1;
        }
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

    // 4. Wait for slaves to reach OP state (skip in sim mode)
    if (!g_sim_mode) {
        printf("等待从站进入 OP 状态...\n");
        for (int wait = 0; wait < 30; wait++) {
            bool all_op = true;
            for (int i = 0; i < NUM_SERVO_AXES; i++) {
                if (!ec_slave_op_flag[i]) { all_op = false; break; }
            }
            if (all_op) {
                printf("所有从站已就绪，耗时 %d 秒.\n", wait);
                break;
            }
            sleep(1);
            if (wait == 29) printf("警告: 等待从站 OP 超时 (30秒)!\n");
        }

        // 5. Request servo power on
        NeedPowerOn = true;
        printf("请求伺服上电...\n");
        for (int wait = 0; wait < 10; wait++) {
            if (PowerStatus) { printf("伺服上电完成.\n"); break; }
            sleep(1);
        }
    }

    // 6. Start interface (IPC server or teleop)
    ipc::IPCServer ipc_server;
    std::thread ipc_thread;
    std::thread task_thread;

    if (enable_ipc) {
        task_thread = std::thread([]() { task_executor_loop(); });
        ipc_thread = std::thread([&ipc_server]() { ipc_server.run(); });
        printf("IPC 服务已启动，等待连接...\n");
    } else if (enable_teleop) {
        printf("进入键盘遥操作模式...\n");
        run_teleop();
        // After teleop exits, trigger graceful shutdown
        g_estop.store(true, std::memory_order_release);
        printf("遥操作结束.\n");
        rt_thread.detach();
        return 0;
    } else {
        printf("系统就绪 (IPC 服务未启动).\n");
        test_robot_func();
    }

    // 7. Main thread: wait for signal
    sigset_t waitset;
    sigemptyset(&waitset);
    sigaddset(&waitset, SIGINT);
    sigaddset(&waitset, SIGTERM);
    sigprocmask(SIG_BLOCK, &waitset, NULL);

    int sig;
    sigwait(&waitset, &sig);
    printf("\n收到信号 %d，正在关闭...\n", sig);

    g_estop.store(true, std::memory_order_release);
    if (enable_ipc) {
        ipc_server.stop();
        if (ipc_thread.joinable()) ipc_thread.join();
        if (task_thread.joinable()) task_thread.join();
    }
    rt_thread.detach();
    sleep(1);
    return 0;
}
