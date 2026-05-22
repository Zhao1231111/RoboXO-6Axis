#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <sys/mman.h>
#include <malloc.h>
#include <sched.h> /* sched_setscheduler() */
#include <thread>
#include <deque> 
#include "eigen/Eigen/Eigen"
#include "general_6s.h"
#include "ecrt.h"
#include "probe_detect_tasks.h" // 引入任务声明

using namespace Eigen;
using namespace std;

// --- 接触检测相关的全局变量定义 ---
bool is_touch_probing = false;       // 标志位：是否正在进行下探检测
bool touch_detected = false;         // 标志位：是否已检测到接触
signed int baseline_tor[6] = {0};    // 基准力矩
int TORQUE_THRESHOLD = 50;           // 力矩突变阈值 (需根据实际调整)
int trigger_tor_1 = 0;               // 记录触发时实际力矩
int trigger_tor_2 = 0;


// 外部引用的机械臂控制算法对象
extern General_6S* g_general_6s;

// --- 应用参数定义 ---
#define FREQUENCY 1000 // EtherCAT 循环频率 1000Hz (周期 1ms)
#define CLOCK_TO_USE CLOCK_MONOTONIC

// EtherCAT 字典对象
#define TARGET_POSITION 0 
#define CYCLIC_POSITION 8 // CSP (周期同步位置) 模式的控制字值

#define NSEC_PER_SEC (1000000000L)
#define PERIOD_NS (NSEC_PER_SEC / FREQUENCY) // 1ms = 1000000 ns
#define TIMESPEC2NS(T) ((uint64_t) (T).tv_sec * NSEC_PER_SEC + (T).tv_nsec)

// --- EtherCAT 状态与配置变量 ---
static ec_master_t* master = NULL;
static ec_master_state_t master_state = {};
static ec_domain_t* domain1 = NULL;
static ec_domain_state_t domain1_state = {};
static ec_slave_config_t* sc[6] = {};
static ec_slave_config_state_t sc_state[6] = {};

int flag[6] = { 0 }; // 伺服准备好标志
int flag2 = 0;       // 上电状态机流转标志

// 过程数据指针
static uint8_t* domain1_pd = NULL;

// 六个松下伺服驱动器的总线别名和位置
#define PANASONIC_0 0,0
#define PANASONIC_1 0,1
#define PANASONIC_2 0,2
#define PANASONIC_3 0,3
#define PANASONIC_4 0,4
#define PANASONIC_5 0,5
#define num_ 6

uint16_t a[6] = { 0 };
uint16_t p[6] = { 0,1,2,3,4,5 };
#define VID_PID 0x00000922,0x00000a01 // 松下伺服厂商ID和产品代码

// 运行状态标志
bool PowerStatus = 0; // 伺服上电状态
bool NeedPowerOn = 0; // 请求上电标志
bool NeedPowerOff = 0; // 请求下电标志

std::deque<double> angle_deque_out;
std::deque<int> tor_deque_out;
std::deque<double> trajectory; // 队列存储整个路径中对应六个关节角度的插值

// PDO 映射偏移量记录结构体
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
} offset;

// Domain1 注册的 PDO 对象表 (CiA 402)
const static ec_pdo_entry_reg_t domain1_regs[] = {
    // 轴 1 (PANASONIC_0)
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
    // 轴 2
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
    // 轴 3
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
    // 轴 4
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
    // 轴 5
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
    // 轴 6
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
    {}
};

// --- PDO 配置 ---
static ec_pdo_entry_info_t device_pdo_entries[] = {
    {0x6040, 0x00, 16}, {0x607a, 0x00, 32}, {0x60b8, 0x00, 16}, {0x6060, 0x00, 8}, // RxPDO
    {0x6041, 0x00, 16}, {0x6064, 0x00, 32}, {0x60b9, 0x00, 16}, {0x60ba, 0x00, 32}, // TxPDO
    {0x60bc, 0x00, 32}, {0x603f, 0x00, 16}, {0x60fd, 0x00, 32}, {0x6077, 0x00, 16}
};

static ec_pdo_info_t device_pdos[] = {
    {0x1600, 4, device_pdo_entries + 0}, // RxPdo: 接收主站数据 (目标位置、控制字等)
    {0x1A00, 8, device_pdo_entries + 4}  // TxPdo: 发送状态给主站 (当前位置、状态字、力矩等)
};

static ec_sync_info_t device_syncs[] = {
    { 0, EC_DIR_OUTPUT, 0, NULL, EC_WD_DISABLE },
    { 1, EC_DIR_INPUT, 0, NULL, EC_WD_DISABLE },
    { 2, EC_DIR_OUTPUT, 1, device_pdos + 0, EC_WD_ENABLE },
    { 3, EC_DIR_INPUT, 1, device_pdos + 1, EC_WD_DISABLE },
    { 0xFF }
};

static unsigned int counter = 0;
static unsigned int blink = 0;
static unsigned int sync_ref_counter = 0;
const struct timespec cycletime = { 0, PERIOD_NS };

// --- 辅助函数 ---

// 累加时间
struct timespec timespec_add(struct timespec time1, struct timespec time2) {
    struct timespec result;
    if ((time1.tv_nsec + time2.tv_nsec) >= NSEC_PER_SEC) {
        result.tv_sec = time1.tv_sec + time2.tv_sec + 1;
        result.tv_nsec = time1.tv_nsec + time2.tv_nsec - NSEC_PER_SEC;
    } else {
        result.tv_sec = time1.tv_sec + time2.tv_sec;
        result.tv_nsec = time1.tv_nsec + time2.tv_nsec;
    }
    return result;
}

// 打印当前关节位置
void print_current_pos(Encoder_Param encoder_param) {
    double pos_cur_ang[6]; 
    double pos_cur_ang_inc[6]; 
    double pos_cur_ang_inc_zero_angle[6]; 
    for (int i = 0; i < 6; i++) {
        pos_cur_ang[i] = g_general_6s->getActPositionAngle(i); // 获取当前位置角度值
        pos_cur_ang_inc[i] = (pos_cur_ang[i] * encoder_param.direction[i] * encoder_param.reducRatio[i] + encoder_param.singleTurnEncoder[i] + encoder_param.deviation[i]) * (1 << encoder_param.encoderResolution[i]) / 360.0;
        pos_cur_ang_inc_zero_angle[i] = pos_cur_ang_inc[i] * 360 / (1 << encoder_param.encoderResolution[i])- encoder_param.singleTurnEncoder[i];    
    }
    printf("current_pos (角度): %lf %lf %lf %lf %lf %lf \n", pos_cur_ang[0], pos_cur_ang[1], pos_cur_ang[2], pos_cur_ang[3], pos_cur_ang[4], pos_cur_ang[5]);
    printf("absolute_pos_inc (增量): %lf %lf %lf %lf %lf %lf \n", pos_cur_ang_inc[0], pos_cur_ang_inc[1], pos_cur_ang_inc[2], pos_cur_ang_inc[3], pos_cur_ang_inc[4], pos_cur_ang_inc[5]);
}

// 检查数据域状态
void check_domain1_state(void) {
    ec_domain_state_t ds;
    ecrt_domain_state(domain1, &ds);
    if (ds.working_counter != domain1_state.working_counter)
        printf("Domain1: WC %u.\n", ds.working_counter);
    if (ds.wc_state != domain1_state.wc_state)
        printf("Domain1: State %u.\n", ds.wc_state);
    domain1_state = ds;
}

// 检查主站状态
void check_master_state(void) {
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

// 检查从站状态
void check_slave_config_states(ec_slave_config_t* sc, int i) {
    ec_slave_config_state_t s;
    ecrt_slave_config_state(sc, &s);
    if (s.operational == 1) flag[i] = 1;
    sc_state[i] = s;
}

// --- 实时循环任务 ---
void cyclic_task() {
    struct timespec wakeupTime, time;
    clock_gettime(CLOCK_TO_USE, &wakeupTime);

    while (1) {
        // 定时唤醒 (周期 1ms)
        wakeupTime = timespec_add(wakeupTime, cycletime);
        clock_nanosleep(CLOCK_TO_USE, TIMER_ABSTIME, &wakeupTime, NULL);

        // 设置主站应用时间
        ecrt_master_application_time(master, TIMESPEC2NS(wakeupTime));

        // 接收 EtherCAT 过程数据
        ecrt_master_receive(master);
        ecrt_domain_process(domain1);

        check_domain1_state();

        // 1. 读取各关节实际位置并保存
        signed int actualInc[6];
        for (unsigned int i = 0; i < 6; i++) {
            actualInc[i] = EC_READ_S32(domain1_pd + offset.position_actual_value[i]);   
        }
        g_general_6s->set_act_inc(actualInc); // 将读取到的编码器数值写入运动学对象
        
        signed int actualtor[6];

        // 1Hz 频率检查状态并执行上电状态机
        if (counter) {
            counter--;
        } else {
            counter = FREQUENCY * 2;
            printf("当前编码器值(Inc): %d %d %d %d %d %d \n", actualInc[0], actualInc[1], actualInc[2], actualInc[3], actualInc[4], actualInc[5]);
            check_master_state();
            
            for (int i = 0; i < num_; i++) {
                check_slave_config_states(sc[i], i);
            }

            // --- 伺服上电状态机 ---
            // 控制字流程： 0x0080(复位) -> 0x0006(关闭使能) -> 0x0007(准备使能) -> 0x000F(开启使能)
            if (!PowerStatus && NeedPowerOn) {
                // 等待所有驱动器进入 operational 状态
                if (flag[0] == 1 && flag[1] == 1 && flag[2] == 1 && flag[3] == 1 && flag[4] == 1 && flag[5] == 1 && flag2 == 0) {
                    for (int i = 0; i < 6; i++) EC_WRITE_U16(domain1_pd + offset.ctrl_word[i], 0x0080);
                    flag2 = 2;
                } else if (flag2 == 2) {
                    for (int i = 0; i < 6; i++) EC_WRITE_U16(domain1_pd + offset.ctrl_word[i], 0x0006);
                    flag2 = 3;
                } else if (flag2 == 3) {
                    for (int i = 0; i < 6; i++) {
                        EC_WRITE_U16(domain1_pd + offset.ctrl_word[i], 0x0007);
                        EC_WRITE_S8(domain1_pd + offset.operation_mode[i], CYCLIC_POSITION); // 设置为 CSP 模式
                        EC_WRITE_S32(domain1_pd + offset.target_position[i], actualInc[i]);  // 保持当前位置
                    }
                    flag2 = 4;
                } else if (flag2 == 4) {
                    for (int i = 0; i < 6; i++) {
                        EC_WRITE_U16(domain1_pd + offset.ctrl_word[i], 0x000f);
                        EC_WRITE_S32(domain1_pd + offset.target_position[i], actualInc[i]);
                    }
                    flag2 = 5;
                    printf("伺服上电成功!\n");
                    PowerStatus = 1; // 上电完成标志
                    NeedPowerOn = 0;
                }
            }
            blink = !blink;
        }

        // 2. 轨迹下发与数据记录
        // 如果伺服已上电并且轨迹队列不为空，向驱动器下发目标位置
        if (PowerStatus && !g_general_6s->get_angle_deque().empty()) {
            for (int i = 0; i < 6; i++) {
                // 写入目标位置
                EC_WRITE_S32(domain1_pd + offset.target_position[i], g_general_6s->set_target_pos_to_servo(i));
                // 读取实时力矩并记录
                actualtor[i] = EC_READ_S16(domain1_pd + offset.torque_actual_value[i]); 
                tor_deque_out.push_back(actualtor[i]);
                angle_deque_out.push_back(g_general_6s->getActPositionAngle(i));
            }
            
            // --- 接触检测逻辑 ---
            if (is_touch_probing && !touch_detected) {
                // 判断 J2(i=1) 或 J3(i=2) 是否受力突变
                if (abs(actualtor[1] - baseline_tor[1]) > TORQUE_THRESHOLD || 
                    abs(actualtor[2] - baseline_tor[2]) > TORQUE_THRESHOLD) {
                    trigger_tor_1 = actualtor[1];
                    trigger_tor_2 = actualtor[2];
                    touch_detected = true;
                    // 在 EtherCAT 线程中发现接触，应用层将捕获这个标志位并清空队列
                }
            }
        }
        
        // 3. 时钟同步
        if (sync_ref_counter) {
            sync_ref_counter--;
        } else {
            sync_ref_counter = 1; 
            clock_gettime(CLOCK_TO_USE, &time);
            ecrt_master_sync_reference_clock(master);
        }
        ecrt_master_sync_slave_clocks(master);

        // 4. 发送过程数据
        ecrt_domain_queue(domain1);
        ecrt_master_send(master);
    }
}


// --- 初始化与功能测试 ---
void test_robot_func() {
    // 1. 设置机器人的 DH 参数 (Denavit-Hartenberg，用于运动学正逆解)
    DH_param dh_example;
    dh_example.a[0] = 0.0408; dh_example.a[1] = 450.342; dh_example.a[2] = 99.107; 
    dh_example.a[3] = 0.0; dh_example.a[4] = 0.0; dh_example.a[5] = 0.0;
    dh_example.alpha[0] = M_PI * 90 / 180; dh_example.alpha[1] = M_PI * 0 / 180; dh_example.alpha[2] = M_PI * 90 / 180;
    dh_example.alpha[3] = M_PI * 90 / 180; dh_example.alpha[4] = M_PI * (-90) / 180; dh_example.alpha[5] = M_PI * 0 / 180;
    dh_example.d[0] = 390; dh_example.d[1] = 0.4997; dh_example.d[2] = 0.0;
    dh_example.d[3] = 470.557; dh_example.d[4] = 0.0; dh_example.d[5] = 123; 
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
    
    print_current_pos(motor_pa.encoder);
    
    
    // 触发任务状态机
    NeedPowerOn = 1;
    sleep(5);
    // multi_joint_move_test(); // 原来的调用方式
    run_task_state_machine(); // 新的任务状态机调用
}

// --- 启动 EtherCAT 主站 ---
int StartEC() {
    // 锁住内存，防止因为缺页中断导致的实时性下降
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        perror("mlockall failed");
        return -1;
    }

    master = ecrt_request_master(0);
    if (!master) return -1;

    domain1 = ecrt_master_create_domain(master);
    if (!domain1) return -1;

    // 配置从站
    for (int i = 0; i < num_; i++) {
        if (!(sc[i] = ecrt_master_slave_config(master, a[i], p[i], VID_PID))) {
            fprintf(stderr, "获取从站 %d 配置失败.\n", i);
            return -1;
        }
        if (ecrt_slave_config_pdos(sc[i], EC_END, device_syncs)) {
            fprintf(stderr, "配置从站 %d PDO 失败.\n", i);
            return -1;
        }
    }
    printf("成功配置从站 PDO!\n");

    // 注册 Domain
    if (ecrt_domain_reg_pdo_entry_list(domain1, domain1_regs)) {
        fprintf(stderr, "PDO 条目注册失败!\n");
        exit(EXIT_FAILURE);
    }

    // 配置分布式时钟 (DC)
    for (int i = 0; i < num_; i++) {
        ecrt_slave_config_dc(sc[i], 0x0300, PERIOD_NS, PERIOD_NS / 2, 0, 0);
    }

    printf("激活 EtherCAT 主站...\n");
    if (ecrt_master_activate(master)) return -1;

    if (!(domain1_pd = ecrt_domain_data(domain1))) return -1;

    // 设置线程优先级为最高，采用 FIFO 实时调度
    struct sched_param param = {};
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    printf("使用的线程优先级为 %i.\n", param.sched_priority);
    if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
        perror("sched_setscheduler 配置失败");
    }

    printf("开始执行 EtherCAT 实时循环任务.\n");
    cyclic_task();
    return 0;
}

// --- 控制器入口 ---
int start_controller() {
    // 实例化机械臂算法对象
    g_general_6s = new General_6S();
    printf("算法对象初始化成功: %p\n", g_general_6s);
    
    // 启动 EtherCAT 通信线程
    std::thread a(StartEC);
    a.detach();
    
    // 动态等待主站激活及从站进入 OP 状态
    int wait_time = 0;
    printf("等待从站进入 OP 状态...\n");
    while (!(flag[0] == 1 && flag[1] == 1 && flag[2] == 1 && flag[3] == 1 && flag[4] == 1 && flag[5] == 1)) {
        sleep(1);
        wait_time++;
        if (wait_time >= 30) {
            printf("警告: 等待从站进入 OP 状态超时 (30秒)!\n");
            break;
        }
    }
    if (wait_time < 30) {
        printf("所有从站已就绪，耗时 %d 秒。\n", wait_time);
    }
    // 运行机器人测试流程
    test_robot_func();
    return 0;
}

int main(int argc, char* argv[]) {
    // 启动控制
    start_controller();
    return 0;
}
