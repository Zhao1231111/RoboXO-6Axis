#ifndef MOTION_CONTROL_H
#define MOTION_CONTROL_H

#include <deque>
#include <unistd.h>
#include <iostream>
#include "eigen/Eigen/Eigen"
#include "general_6s.h"

using namespace Eigen;
using namespace std;

// --- 全局依赖变量 (通常在 main.cpp 中定义) ---
extern General_6S* g_general_6s;
extern bool PowerStatus;
extern bool NeedPowerOn;
extern bool NeedPowerOff;
extern std::deque<double> angle_deque_out;
extern std::deque<int> tor_deque_out;

extern double single_joint_test[6];
extern bool is_touch_probing;
extern bool touch_detected;

/**
 * @brief 将机器人的各个关节移动到初始（零点）位置，并阻塞等待直至完成
 */
void move_home_position();

/**
 * @brief 多关节联合移动测试，包含简单的控制和运行状态监控，运动数据会被记录到文件中
 */
void multi_joint_move_test();

/**
 * @brief 在笛卡尔坐标系下的直线插补运动，基于当前位姿移动指定的增量 (x, y, z)
 * @param x, y, z 直线移动的增量距离
 * @param origin_point_angle_degree 起点关节角度
 * @param origin_point_cartesian_coordinate 起点笛卡尔位姿
 * @param target_point_joint_test 返回：终点的关节角度
 * @param target_point_cartesian_coordinate 返回：终点的笛卡尔位姿
 * @return 计算得到的终点笛卡尔位姿
 */
VectorXd lining_motion_test(double x, double y, double z, VectorXd origin_point_angle_degree, VectorXd origin_point_cartesian_coordinate, VectorXd &target_point_joint_test, VectorXd &target_point_cartesian_coordinate);

/**
 * @brief 在笛卡尔坐标系下的圆弧插补运动，基于当前位姿和两个相对点三点定圆弧
 * @param mid_x, mid_y, mid_z 圆弧中间点相对起点的偏移量
 * @param target_x, target_y, target_z 圆弧终点相对起点的偏移量
 * @param origin_point_angle_degree 起点关节角度
 * @param origin_point_cartesian_coordinate 起点笛卡尔位姿
 * @param target_point_joint_test 返回：终点的关节角度
 * @param target_point_cartesian_coordinate 返回：终点的笛卡尔位姿
 * @return 计算得到的终点笛卡尔位姿
 */
VectorXd circle_motion_test(double mid_x, double mid_y, double mid_z,
                            double target_x, double target_y, double target_z,
                            VectorXd origin_point_angle_degree,
                            VectorXd origin_point_cartesian_coordinate,
                            VectorXd &target_point_joint_test,
                            VectorXd &target_point_cartesian_coordinate);

/**
 * @brief 专门用于慢速下探的直线运动函数，保证检测精度
 * @param z_offset Z轴下探深度偏移量
 * @param origin_point_angle_degree 起点关节角度
 * @param origin_point_cartesian_coordinate 起点笛卡尔位姿
 * @return 计算得到的终点笛卡尔位姿
 */
VectorXd downward_probe_motion(double z_offset, VectorXd origin_point_angle_degree, VectorXd origin_point_cartesian_coordinate);

/**
 * @brief 关节空间增量移动测试，根据给定各关节的相对偏移量移动
 * @param joint_angles_degree_offset 各关节的相对角度增量
 * @param origin_point_joint_test 起点关节角度
 * @param target_point_joint_test 返回：计算得到的终点关节角度
 * @param target_point_cartesian_test 返回：计算得到的终点笛卡尔位姿
 */
void joint_motion_test(VectorXd joint_angles_degree_offset, VectorXd origin_point_joint_test, VectorXd &target_point_joint_test, VectorXd &target_point_cartesian_test);

/**
 * @brief PTP (Point-to-Point) 插补运动，通过给定的基座系笛卡尔坐标直接控制移动
 * @param target_cartesian_base 目标笛卡尔坐标系位姿
 */
void ptp_motion_to_cartesian_base(VectorXd target_cartesian_base);

/**
 * @brief 设置夹爪的开合状态
 * @param open true为打开，false为关闭
 */
void set_gripper(bool open);

/**
 * @brief 执行抓取物体的全套动作：PTP到目标点 -> 张开夹爪 -> 下降 -> 闭合夹爪 -> 上升
 * @param target_point_cartesian 安全位置（物体正上方）的笛卡尔坐标
 * @param drop_height 抓取时需要下降的高度偏移量（mm）
 */
void grasp_object(VectorXd target_point_cartesian, double drop_height);

#endif // MOTION_CONTROL_H
