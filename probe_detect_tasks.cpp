#include "probe_detect_tasks.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

using namespace std;

double single_joint_test[6] = {0,0,0,0,0,0};

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

    for (int i = 0; i < trajectory_line.size(); i++) 
    {
        std::cout<<trajectory_line[i]<<" ";
        if(i%6==5)
        printf("\n");
    }
    // 插补轨迹写入运动队列
    g_general_6s->add_angle_deque(trajectory_line); 
    
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
}

// 读取本地配置文件的函数
void load_task_config(VectorXd& board_center, VectorXd& target_point, int& torque_thresh) {
    std::ifstream file("config.txt");
    if (!file.is_open()) {
        std::cout << "[配置读取] 未找到 config.txt，将使用代码中的默认参数。" << std::endl;
        return; // 保留默认值
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue; // 跳过空行和注释

        std::istringstream iss(line);
        std::string key;
        iss >> key;

        if (key == "board_center_base") {
            for (int i = 0; i < 6; i++) iss >> board_center(i);
        } else if (key == "target_in_board") {
            for (int i = 0; i < 6; i++) iss >> target_point(i);
        } else if (key == "torque_threshold") {
            iss >> torque_thresh;
        }
    }
    std::cout << "[配置读取] 成功从 config.txt 加载参数！" << std::endl;
}

void run_task_state_machine() {
    TaskState current_state = TaskState::INIT;
    
    while (true) {
        switch (current_state) {
            case TaskState::INIT: {
                if (PowerStatus == 1) {
                    cout << "[任务状态] 伺服上电完成，准备移动到起点..." << endl;
                    current_state = TaskState::MOVE_TO_START;
                }
                break;
            }
            case TaskState::MOVE_TO_START: {
                cout << "[任务状态] 移动到白板上方(基于工件坐标系)..." << endl;
                
                VectorXd board_center_base(6);
                board_center_base << 0.5, 0.0, 0.3, 0.0, 0.0, 0.0; // 默认值
                
                VectorXd target_in_board(6);
                target_in_board << 0.0, 0.0, -0.05, 0.0, 0.0, 0.0; // 默认值
                
                // --- 读取外部配置文件，覆盖默认值 ---
                load_task_config(board_center_base, target_in_board, TORQUE_THRESHOLD);
                
                // 1. 定义白板中心相对于机器人基座的位姿矩阵
                MatrixXd T_base_board = g_general_6s->rpy_2_tr(board_center_base);
                
                // 2. 在白板坐标系下的目标起点
                MatrixXd T_board_target = g_general_6s->rpy_2_tr(target_in_board);
                
                // 3. 坐标系变换计算：T_base_target = T_base_board * T_board_target
                MatrixXd T_base_target = T_base_board * T_board_target;
                
                // 将齐次矩阵重新转回笛卡尔6D向量 (通过算法库提供的公有方法)
                VectorXd target_cartesian_base = g_general_6s->tr_2_MCS(T_base_target);
                
                // 4. 调用 PTP 关节运动，平滑移动到计算出的基座坐标系点位
                ptp_motion_to_cartesian_base(target_cartesian_base);
                
                // 等待移动完成
                while (PowerStatus && !g_general_6s->get_angle_deque().empty()) {
                    sleep(1);
                }
                
                cout << "[任务状态] 到达起点，记录基准力矩..." << endl;
                sleep(1); // 确保稳定
                
                int q_size = tor_deque_out.size();
                if (q_size >= 6) {
                    for (int i = 0; i < 6; i++) {
                        baseline_tor[i] = tor_deque_out[q_size - 6 + i];
                    }
                }
                
                current_state = TaskState::TOUCH_PROBING;
                break;
            }
            case TaskState::TOUCH_PROBING: {
                cout << "[任务状态] 开始缓慢下探并检测力矩..." << endl;
                is_touch_probing = true;
                touch_detected = false;
                
                VectorXd origin_point_joint(6);
                for (int i = 0; i < 6; i++) origin_point_joint(i) = g_general_6s->getActPositionAngle(i);
                
                MatrixXd trans_matrix;
                g_general_6s->calc_forward_kin(origin_point_joint, trans_matrix);
                VectorXd origin_cartesian = g_general_6s->tr_2_MCS(trans_matrix);
                
                // 下探最大距离 10cm
                downward_probe_motion(-0.1, origin_point_joint, origin_cartesian);
                
                // 持续等待触碰
                while (is_touch_probing && !g_general_6s->get_angle_deque().empty()) {
                    if (touch_detected) {
                        cout << "[任务状态] 接触白板！已停止下探。" << endl;
                        break;
                    }
                    usleep(10000); // 10ms 循环检查
                }
                
                if (!touch_detected) {
                    cout << "[任务状态] 未检测到白板（下探到极限）" << endl;
                    is_touch_probing = false;
                    current_state = TaskState::ERROR;
                    break;
                }
                
                // 确保清除剩余轨迹
                g_general_6s->get_angle_deque().clear();
                is_touch_probing = false;
                
                sleep(1); // 停顿1秒稳定
                
                // 触碰后额外下压一点产生摩擦力
                cout << "[任务状态] 触碰成功，额外下压 3mm..." << endl;
                for (int i = 0; i < 6; i++) origin_point_joint(i) = g_general_6s->getActPositionAngle(i);
                g_general_6s->calc_forward_kin(origin_point_joint, trans_matrix);
                origin_cartesian = g_general_6s->tr_2_MCS(trans_matrix);
                
                VectorXd press_joint_target(6);
                VectorXd press_cartesian_target(6);
                lining_motion_test(0.0, 0.0, -0.003, origin_point_joint, origin_cartesian, press_joint_target, press_cartesian_target);
                
                while (PowerStatus && !g_general_6s->get_angle_deque().empty()) {
                    usleep(100000);
                }
                
                current_state = TaskState::WIPING_BOARD;
                break;
            }
            case TaskState::WIPING_BOARD: {
                cout << "[任务状态] 开始执行平面擦除轨迹..." << endl;
                
                VectorXd origin_point_joint(6);
                for (int i = 0; i < 6; i++) origin_point_joint(i) = g_general_6s->getActPositionAngle(i);
                MatrixXd trans_matrix;
                g_general_6s->calc_forward_kin(origin_point_joint, trans_matrix);
                VectorXd origin_cartesian = g_general_6s->tr_2_MCS(trans_matrix);
                
                VectorXd wipe_joint_target(6);
                VectorXd wipe_cartesian_target(6);
                // 演示擦拭：在当前平面上横向平移 10cm
                lining_motion_test(0.1, 0.0, 0.0, origin_point_joint, origin_cartesian, wipe_joint_target, wipe_cartesian_target);
                
                while (PowerStatus && !g_general_6s->get_angle_deque().empty()) {
                    usleep(100000);
                }
                
                current_state = TaskState::TASK_FINISHED;
                break;
            }
            case TaskState::TASK_FINISHED: {
                cout << "[任务状态] 擦除任务完成！" << endl;
                return;
            }
            case TaskState::ERROR: {
                cout << "[任务状态] 发生错误！" << endl;
                return;
            }
        }
        usleep(500000); // 500ms
    }
}
