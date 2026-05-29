#include "motion_control.h"
#include <iostream>
#include <fstream>
#include <string>

using namespace std;

double single_joint_test[6] = {0,0,0,0,0,0};

void move_home_position()   
{
    VectorXd origin_point_joint_test(6);
    for (int i = 0; i < 6; i++) {
        origin_point_joint_test(i) = g_general_6s->getActPositionAngle(i);
    }

    VectorXd target_point_joint_test(6);         // 目标位置,角度制
    VectorXd velocity_current_joint_test(6);	 // 当前速度,角度制
    VectorXd acceleration_current_joint_test(6); // 当前加速度,角度制

    // 设置目标位置角度值，从当前位置开始运动固定角度
    target_point_joint_test(0) = 0;
    target_point_joint_test(1) = 0;
    target_point_joint_test(2) = 0;
    target_point_joint_test(3) = 0;
    target_point_joint_test(4) = 0;
    target_point_joint_test(5) = 0;
    velocity_current_joint_test << 0, 0, 0, 0, 0, 0;		  // 设置当前速度
    acceleration_current_joint_test << 0, 0, 0, 0, 0, 0;      // 设置当前加速度
    double Ts_joint_test = 0.001;					          // 设置运动周期
    double velocityPercent_joint_test = 50;					  // 设置速度百分比
    double accelerationPercent_joint_test = 10;				  // 设置加速度百分比
    double decelerationPercent_joint_test = 10;				  // 设置减速度百分比
    double jacobiPercent_joint_test = 10;					  // 设置雅可比速度百分比 

    //************************************ 计算关节角度插补 ********************************
    std::deque<double> trajectory_joint_test;       // 队列存储整个路径中对应六个关节角度的插值

    // 计算完整个运动过程的的六关节角度序列，存入trajectory_joint_test队列中
    g_general_6s->move_joint_interp(target_point_joint_test,
        origin_point_joint_test, velocity_current_joint_test, acceleration_current_joint_test, Ts_joint_test, velocityPercent_joint_test,
        accelerationPercent_joint_test, decelerationPercent_joint_test, jacobiPercent_joint_test, trajectory_joint_test);

    if (!PowerStatus) // 判断使能状态
        NeedPowerOn = true;		  // 开启使能

    // 插补轨迹写入运动队列
    g_general_6s->add_angle_deque(trajectory_joint_test); // 设置运动轨迹，整个轨迹的关节角度序列

    // --- 阻塞等待机制 ---
    // 1. 如果当前处于未上电状态，等待底层 EtherCAT 线程完成上电初始化
    while (!PowerStatus) {
        usleep(50000);
    }
    // 2. 上电完成后，等待轨迹被完全消耗完毕
    while (PowerStatus && !g_general_6s->get_angle_deque().empty()) {
        usleep(50000); 
    }
}

void multi_joint_move_test() {
    VectorXd origin_point_joint_test(6); // 初始位置 (角度制)
	VectorXd target_point_joint_test(6); // 目标位置 (角度制)
    VectorXd vel_current_joint_test(6);  // 当前速度
    VectorXd acc_current_joint_test(6);  // 当前加速度

    double pos_cur_ang[6] = {0};                 
    
    // 获取当前实际角度作为起点
    for (int i = 0; i < 6; i++) {
        pos_cur_ang[i] = g_general_6s->getActPositionAngle(i); 
        origin_point_joint_test(i) = pos_cur_ang[i];
		target_point_joint_test(i) = single_joint_test[i];
    }
    
    vel_current_joint_test << 0, 0, 0, 0, 0, 0;
    acc_current_joint_test << 0, 0, 0, 0, 0, 0;
    
    // 规划参数配置
    double Ts_joint_test = 0.001;               // 运动周期 1ms
    double velPerc_joint_test = 10;             // 速度百分比
    double accPerc_joint_test = 10;             // 加速度百分比
    double decPerc_joint_test = 10;             // 减速度百分比
    double jerkPerc_joint_test = 10;            // 加加速度百分比
    std::deque<double> trajectory_joint_test;
    
    // 调用底层库进行关节空间插补，生成轨迹点序列
    g_general_6s->move_joint_interp(target_point_joint_test, origin_point_joint_test, 
        vel_current_joint_test, acc_current_joint_test, Ts_joint_test, velPerc_joint_test,
        accPerc_joint_test, decPerc_joint_test, jerkPerc_joint_test, trajectory_joint_test);
    
    // 请求上电并开始执行轨迹
    if (!PowerStatus) NeedPowerOn = 1;
    g_general_6s->set_angle_deque(trajectory_joint_test); // 写入运动轨迹序列，交由 EtherCAT 线程下发
    
    sleep(5); // 等待运动初始化
    
    // 运动过程状态监控
    double cur_angle_double[6];
    while (PowerStatus && !g_general_6s->get_angle_deque().empty()) {
        for (int i = 0; i < 6; i++) cur_angle_double[i] = g_general_6s->getActPositionAngle(i);
        printf("运行中，当前角度: %lf %lf %lf %lf %lf %lf \n", cur_angle_double[0], cur_angle_double[1], cur_angle_double[2], cur_angle_double[3], cur_angle_double[4], cur_angle_double[5]);
        
        if (g_general_6s->get_angle_deque().empty() && PowerStatus) NeedPowerOff = 1;
        sleep(1); // 每秒监控一次
    }
        
    // --- 运动结束后的数据存储 ---
    double cur_angle_tor[6];
    
    FILE *outFile0 = fopen("/home/seuauto/Desktop/data/data_i1.txt", "w"); // 保存期望角度 (输入)
    FILE *outFile1 = fopen("/home/seuauto/Desktop/data/data_o1.txt", "w"); // 保存实际角度 (输出)
    FILE *outFile3 = fopen("/home/seuauto/Desktop/data/data_o3.txt", "w"); // 保存实际力矩 (输出)
    
    int len = trajectory_joint_test.size();
    int len1 = angle_deque_out.size();
    cout << "输出轨迹长度: " << len1 << endl;

    // 写入期望轨迹
    int k = 0;
    while(k < len) {
        for(int i = 0; i < 6; i++) cur_angle_double[i] = trajectory_joint_test[k + i];
        k += 6;
        fprintf(outFile0, "期望角度: %lf %lf %lf %lf %lf %lf \n", cur_angle_double[0], cur_angle_double[1], cur_angle_double[2], cur_angle_double[3], cur_angle_double[4], cur_angle_double[5]);
    }
    fclose(outFile0);

    // 写入实际读取轨迹与力矩
    k = 0;
    while(k < len1) {
        for(int i = 0; i < 6; i++) {
            cur_angle_double[i] = angle_deque_out[k + i];
            cur_angle_tor[i] = tor_deque_out[k + i];
        }
        k += 6;
        fprintf(outFile1, "实际角度: %lf %lf %lf %lf %lf %lf \n", cur_angle_double[0], cur_angle_double[1], cur_angle_double[2], cur_angle_double[3], cur_angle_double[4], cur_angle_double[5]);
        fprintf(outFile3, "实际力矩: %lf %lf %lf %lf %lf %lf \n", cur_angle_tor[0], cur_angle_tor[1], cur_angle_tor[2], cur_angle_tor[3], cur_angle_tor[4], cur_angle_tor[5]);
    }
    fclose(outFile1);
    fclose(outFile3);
    cout << "数据存储完成!" << endl;

    // --- 阻塞等待机制 ---
    // 1. 如果当前处于未上电状态，等待底层 EtherCAT 线程完成上电初始化
    while (!PowerStatus) {
        usleep(50000);
    }
    // 2. 上电完成后，等待轨迹被完全消耗完毕
    while (PowerStatus && !g_general_6s->get_angle_deque().empty()) {
        usleep(50000); 
    }

}

VectorXd lining_motion_test(double x,double y,double z,VectorXd origin_point_angle_degree,VectorXd origin_point_cartesian_coordinate,VectorXd &target_point_joint_test,VectorXd &target_point_cartesian_coordinate)
{
    double velocity_current_rectangular;	       // 当前速度
    double acceleration_current_rectangular;	   // 当前加速度
    MatrixXd trans_matrix;                         // 存储正解矩阵   

    target_point_cartesian_coordinate(0) = origin_point_cartesian_coordinate[0] + x;
    target_point_cartesian_coordinate(1) = origin_point_cartesian_coordinate[1] + y;
    target_point_cartesian_coordinate(2) = origin_point_cartesian_coordinate[2] + z;
    target_point_cartesian_coordinate(3) = origin_point_cartesian_coordinate[3] ;
    target_point_cartesian_coordinate(4) = origin_point_cartesian_coordinate[4] ;
    target_point_cartesian_coordinate(5) = origin_point_cartesian_coordinate[5] ;
    
    trans_matrix = g_general_6s->rpy_2_tr(target_point_cartesian_coordinate);
    g_general_6s->calc_inverse_kin(trans_matrix, origin_point_angle_degree, target_point_joint_test);
    
    velocity_current_rectangular = 0.0;		  // 设置当前速度
    acceleration_current_rectangular = 0.0;	  // 设置当前加速度
    double Ts_joint = 0.001;				  // 设置运动周期
    double maxVelocityLimit = 50;		  // 设置最大直线速度
    double maxAccelerationLimit = 200;	  // 设置最大直线加速度
    double maxDecelerationLimit = -200;	  // 设置最大直线减速度
    double maxJerkLimit = 50;		  // 设置最大雅可比速度

    std::deque<double> trajectory_line;       // 队列存储整个路径中对应六个关节角度的插值
    g_general_6s->move_line_interp(target_point_cartesian_coordinate,
        origin_point_cartesian_coordinate, origin_point_angle_degree, velocity_current_rectangular, 
        acceleration_current_rectangular, Ts_joint, maxVelocityLimit,
        maxAccelerationLimit, maxDecelerationLimit, maxJerkLimit, trajectory_line);
    if (!PowerStatus)    // 判断使能状态
        NeedPowerOn = 1; // 开启使能


    // 插补轨迹写入运动队列
    g_general_6s->add_angle_deque(trajectory_line); 

        // --- 阻塞等待机制 ---
    // 1. 如果当前处于未上电状态，等待底层 EtherCAT 线程完成上电初始化
    while (!PowerStatus) {
        usleep(50000);
    }
    // 2. 上电完成后，等待轨迹被完全消耗完毕
    while (PowerStatus && !g_general_6s->get_angle_deque().empty()) {
        usleep(50000); 
    }

    
    return target_point_cartesian_coordinate;
}

VectorXd circle_motion_test(double mid_x, double mid_y, double mid_z,
                            double target_x, double target_y, double target_z,
                            VectorXd origin_point_angle_degree,
                            VectorXd origin_point_cartesian_coordinate,
                            VectorXd &target_point_joint_test,
                            VectorXd &target_point_cartesian_coordinate)
{
    double velocity_current_rectangular = 0.0;
    double acceleration_current_rectangular = 0.0;

    VectorXd mid_point_cartesian_coordinate(6);
    mid_point_cartesian_coordinate(0) = origin_point_cartesian_coordinate[0] + mid_x;
    mid_point_cartesian_coordinate(1) = origin_point_cartesian_coordinate[1] + mid_y;
    mid_point_cartesian_coordinate(2) = origin_point_cartesian_coordinate[2] + mid_z;
    mid_point_cartesian_coordinate(3) = origin_point_cartesian_coordinate[3];
    mid_point_cartesian_coordinate(4) = origin_point_cartesian_coordinate[4];
    mid_point_cartesian_coordinate(5) = origin_point_cartesian_coordinate[5];

    target_point_cartesian_coordinate(0) = origin_point_cartesian_coordinate[0] + target_x;
    target_point_cartesian_coordinate(1) = origin_point_cartesian_coordinate[1] + target_y;
    target_point_cartesian_coordinate(2) = origin_point_cartesian_coordinate[2] + target_z;
    target_point_cartesian_coordinate(3) = origin_point_cartesian_coordinate[3];
    target_point_cartesian_coordinate(4) = origin_point_cartesian_coordinate[4];
    target_point_cartesian_coordinate(5) = origin_point_cartesian_coordinate[5];

    MatrixXd trans_matrix = g_general_6s->rpy_2_tr(target_point_cartesian_coordinate);
    g_general_6s->calc_inverse_kin(trans_matrix, origin_point_angle_degree, target_point_joint_test);

    double Ts_joint = 0.001;
    double maxVelocityLimit = 50;
    double maxAccelerationLimit = 200;
    double maxDecelerationLimit = -200;
    double maxJerkLimit = 50;

    std::deque<double> trajectory_circle;
    g_general_6s->move_circle_interp(target_point_cartesian_coordinate, mid_point_cartesian_coordinate,
        origin_point_cartesian_coordinate, origin_point_angle_degree, velocity_current_rectangular,
        acceleration_current_rectangular, Ts_joint, maxVelocityLimit, maxAccelerationLimit,
        maxDecelerationLimit, maxJerkLimit, trajectory_circle);

    if (!PowerStatus)
        NeedPowerOn = 1;

    g_general_6s->add_angle_deque(trajectory_circle);

    while (!PowerStatus) {
        usleep(50000);
    }
    while (PowerStatus && !g_general_6s->get_angle_deque().empty()) {
        usleep(50000);
    }

    return target_point_cartesian_coordinate;
}

// 专门用于极缓慢下探的直线插补函数
VectorXd downward_probe_motion(double z_offset, VectorXd origin_point_angle_degree, VectorXd origin_point_cartesian_coordinate)
{
    VectorXd target_point_joint_test(6);
    VectorXd target_point_cartesian_coordinate(6);
    double velocity_current_rectangular;
    double acceleration_current_rectangular;
    MatrixXd trans_matrix;   

    target_point_cartesian_coordinate(0) = origin_point_cartesian_coordinate[0];
    target_point_cartesian_coordinate(1) = origin_point_cartesian_coordinate[1];
    target_point_cartesian_coordinate(2) = origin_point_cartesian_coordinate[2] + z_offset;
    target_point_cartesian_coordinate(3) = origin_point_cartesian_coordinate[3] ;
    target_point_cartesian_coordinate(4) = origin_point_cartesian_coordinate[4] ;
    target_point_cartesian_coordinate(5) = origin_point_cartesian_coordinate[5] ;
    
    trans_matrix = g_general_6s->rpy_2_tr(target_point_cartesian_coordinate);
    g_general_6s->calc_inverse_kin(trans_matrix, origin_point_angle_degree, target_point_joint_test);
    
    velocity_current_rectangular = 0.0;		  
    acceleration_current_rectangular = 0.0;	  
    double Ts_joint = 0.001;				  
    double maxVelocityLimit = 2.0;		  // 极低速度 2mm/s
    double maxAccelerationLimit = 10;	  
    double maxDecelerationLimit = -10;	  
    double maxJerkLimit = 10;		  

    std::deque<double> trajectory_line;       
    g_general_6s->move_line_interp(target_point_cartesian_coordinate,
        origin_point_cartesian_coordinate, origin_point_angle_degree, velocity_current_rectangular, 
        acceleration_current_rectangular, Ts_joint, maxVelocityLimit,
        maxAccelerationLimit, maxDecelerationLimit, maxJerkLimit, trajectory_line);
        
    if (!PowerStatus) NeedPowerOn = 1; 

    // 插补轨迹写入运动队列
    g_general_6s->add_angle_deque(trajectory_line); 

    // --- 智能条件阻塞等待机制 ---
    // 1. 如果当前处于未上电状态，等待底层 EtherCAT 线程完成上电初始化
    while (!PowerStatus) {
        usleep(50000);
    }
    
    // 延时 500ms，避开起步加速阶段的动摩擦力和惯性力矩峰值
    usleep(500000);

    // 开启碰撞检测标志，通知实时线程开始检测力矩
    is_touch_probing = true;
    touch_detected = false;

    // 2. 上电完成后，等待轨迹被完全消耗完毕，或直到检测到力矩突变！
    while (PowerStatus && !g_general_6s->get_angle_deque().empty()) {
        if (touch_detected) {
            std::cout << "\n[底层运动控制] 触发力矩检测！紧急停止当前动作，截断剩余轨迹。" << std::endl;
            // 紧急停止的清理操作已移交到底层 EtherCAT 线程内部执行，以保证线程安全！
            break;
        }
        usleep(10000); // 10ms 循环检查
    }
    
    // 动作结束（无论是撞到停下还是走完停下），关闭检测标志
    is_touch_probing = false;

    
    return target_point_cartesian_coordinate;
}

void joint_motion_test(VectorXd joint_angles_degree_offset,VectorXd origin_point_joint_test,VectorXd &target_point_joint_test,VectorXd &target_point_cartesian_test)
{
    VectorXd velocity_current_joint_test(6);	 // 当前速度,角度制
    VectorXd acceleration_current_joint_test(6);	 // 当前加速度,角度制
    MatrixXd trans_matrix;   

    target_point_joint_test(0) = origin_point_joint_test[0] + joint_angles_degree_offset[0];
    target_point_joint_test(1) = origin_point_joint_test[1] + joint_angles_degree_offset[1];
    target_point_joint_test(2) = origin_point_joint_test[2] + joint_angles_degree_offset[2];
    target_point_joint_test(3) = origin_point_joint_test[3] + joint_angles_degree_offset[3];
    target_point_joint_test(4) = origin_point_joint_test[4] + joint_angles_degree_offset[4];
    target_point_joint_test(5) = origin_point_joint_test[5] + joint_angles_degree_offset[5];

    g_general_6s->calc_forward_kin(target_point_joint_test, trans_matrix); //计算正解矩阵
    target_point_cartesian_test = g_general_6s->tr_2_MCS(trans_matrix);

    velocity_current_joint_test << 0, 0, 0, 0, 0, 0;		  // 设置当前速度
    acceleration_current_joint_test << 0, 0, 0, 0, 0, 0;		  // 设置当前加速度
    
    double Ts_joint_test = 0.001;					  // 设置运动周期
    double velocityPercent_joint_test = 30;					  // 设置速度百分比
    double accelerationPercent_joint_test = 20;					  // 设置加速度百分比
    double decelerationPercent_joint_test = 20;					  // 设置减速度百分比
    double jacobiPercent_joint_test = 10;					  // 设置雅可比速度百分比 
    
    std::deque<double> trajectory_joint_test;       
    g_general_6s->move_joint_interp(target_point_joint_test,
        origin_point_joint_test, velocity_current_joint_test, acceleration_current_joint_test, Ts_joint_test, velocityPercent_joint_test,
        accelerationPercent_joint_test, decelerationPercent_joint_test, jacobiPercent_joint_test, trajectory_joint_test);

    if (!PowerStatus) // 判断使能状态
        NeedPowerOn = true;		  // 开启使能
    
    // 插补轨迹写入运动队列
    g_general_6s->add_angle_deque(trajectory_joint_test); 

    // --- 阻塞等待机制 ---
    while (!PowerStatus) {
        usleep(50000);
    }
    while (PowerStatus && !g_general_6s->get_angle_deque().empty()) {
        usleep(50000); 
    }

    return ;
}

// 利用 PTP (关节空间插补) 移动到指定的基座系笛卡尔坐标点
void ptp_motion_to_cartesian_base(VectorXd target_cartesian_base) {
    VectorXd current_joint(6);
    for (int i = 0; i < 6; i++) current_joint(i) = g_general_6s->getActPositionAngle(i);
    
    // 1. 笛卡尔坐标 -> 基座齐次变换矩阵 -> 目标关节角
    MatrixXd trans_matrix = g_general_6s->rpy_2_tr(target_cartesian_base);
    VectorXd target_joint(6);
    g_general_6s->calc_inverse_kin(trans_matrix, current_joint, target_joint);
    
    // 2. 关节空间插补 (PTP)
    VectorXd vel_current_joint = VectorXd::Zero(6);
    VectorXd acc_current_joint = VectorXd::Zero(6);
    double Ts = 0.001;
    double velPerc = 30, accPerc = 20, decPerc = 20, jerkPerc = 10;
    
    std::deque<double> trajectory;
    g_general_6s->move_joint_interp(target_joint, current_joint, vel_current_joint, acc_current_joint, 
                                    Ts, velPerc, accPerc, decPerc, jerkPerc, trajectory);
    
    if (!PowerStatus) NeedPowerOn = true;
    g_general_6s->add_angle_deque(trajectory);

    while (!PowerStatus) {
        usleep(50000);
    }
    while (PowerStatus && !g_general_6s->get_angle_deque().empty()) {
        usleep(50000); 
    }
}

extern int gripper_io_data;
extern bool gripper_action_req;

void set_gripper(bool open) {
    if (open) {
        // 打开夹爪 (对应 io15 = 1, io16 = 0)
        gripper_io_data = 16384; 
    } else {
        // 关闭夹爪 (对应 io15 = 0, io16 = 1)
        gripper_io_data = 32768; 
    }
    gripper_action_req = true;
    usleep(500000); // 延时等待气缸动作完成
}

void grasp_object(VectorXd target_point_cartesian, double drop_height) {
    cout << "\n[抓取流程] 步骤1：PTP移动到目标正上方安全点..." << endl;
    ptp_motion_to_cartesian_base(target_point_cartesian);

    cout << "[抓取流程] 步骤2：张开夹爪..." << endl;
    set_gripper(true);
    usleep(1000000); // 等待气动夹爪完全张开

    cout << "[抓取流程] 步骤3：垂直下降 " << drop_height << " mm..." << endl;
    VectorXd down_target = target_point_cartesian;
    down_target(2) -= drop_height;
    ptp_motion_to_cartesian_base(down_target);

    cout << "[抓取流程] 步骤4：闭合夹爪，抓取物体..." << endl;
    set_gripper(false);
    usleep(1000000); // 等待气动夹爪完全抓稳

    // cout << "[抓取流程] 步骤5：垂直上升返回原高度..." << endl;
    // ptp_motion_to_cartesian_base(target_point_cartesian);

    cout << "[抓取流程] 抓取动作执行完毕！" << endl;
}
