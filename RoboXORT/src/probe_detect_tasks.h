#ifndef PROBE_DETECT_TASKS_H
#define PROBE_DETECT_TASKS_H

#include <deque>
#include <unistd.h>
#include <iostream>
#include <Eigen/Eigen>
#include "general_6s.h"

using namespace Eigen;
using namespace std;

// --- 全局依赖变量 (在 main.cpp 中定义) ---
extern General_6S* g_general_6s;
extern bool PowerStatus;
extern bool NeedPowerOn;
extern bool NeedPowerOff;
extern std::deque<double> angle_deque_out;
extern std::deque<int> tor_deque_out;

// --- 接触检测相关的全局变量 ---
extern bool is_touch_probing;
extern bool touch_detected;
extern signed int baseline_tor[6];
extern int TORQUE_THRESHOLD; // 力矩突变阈值
extern int trigger_tor_1;    // 触发瞬间 J2 的实际力矩
extern int trigger_tor_2;    // 触发瞬间 J3 的实际力矩

// --- 任务状态枚举 ---
enum class TaskState {
    INIT,               // 初始状态，等待伺服上电与通信建立
    MOVE_TO_START,      // 移动到白板上方初始点
    TOUCH_PROBING,      // 缓慢下探，执行力矩接触检测
    WIPING_BOARD,       // 接触后执行平面擦除轨迹
    TASK_FINISHED,      // 任务完成，数据保存
    ERROR               // 异常处理状态
};

// --- 导入运动控制模块 ---
#include "motion_control.h"

// --- 状态机任务主循环 ---
void run_task_state_machine();

// --- 封装好的下探与按压动作 ---
bool probe_and_press(int torque_threshold, double &out_z_height);

#endif // ROBOT_TASKS_H
