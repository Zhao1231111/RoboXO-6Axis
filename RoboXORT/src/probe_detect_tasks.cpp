#include "probe_detect_tasks.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

using namespace std;


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

void get_joint_Position_initial(VectorXd &position_current_angle)
{
     for (int i = 0; i < 6; i++)
    {
        position_current_angle[i] = g_general_6s->getActPositionAngle(i); // 获取当前位置角度值
    }
}


void run_task_state_machine() {
    // ------------------- 初始化与上电阶段 -------------------
    cout << "\n[任务开始] 正在检查使能状态..." << endl;
    while (!PowerStatus) {
        usleep(500000);
    }
    cout << "[任务状态] 伺服上电完成！" << endl;

    // ------------------- 1. 移动到机器人零点 -------------------
    VectorXd position_joint_initial(6);
    get_joint_Position_initial(position_joint_initial);
    cout << "\n[动作 1] 正在复位机器人到初始零点..." << endl;
    move_home_position();
    cout << " -> 已安全回到原位！" << endl;

    cout << "打开夹爪" << endl;
    set_gripper(true);
    usleep(1000000);

    cout << "闭合夹爪" << endl;
    set_gripper(false);


    // ------------------- 2. 移动到白板上方基准点 -------------------
    cout << "\n[动作 2] 正在移动到白板上方(基于工件坐标系)..." << endl;
    
    // 初始化默认坐标（可被 config.txt 覆盖）
    VectorXd board_center_base(6);
    board_center_base << 0.5, 0.0, 0.3, 0.0, 0.0, 0.0;
    VectorXd target_in_board(6);
    target_in_board << 0.0, 0.0, -0.05, 0.0, 0.0, 0.0;
    load_task_config(board_center_base, target_in_board, TORQUE_THRESHOLD);
    
    // 坐标系齐次变换矩阵计算
    MatrixXd T_base_board = g_general_6s->rpy_2_tr(board_center_base);
    MatrixXd T_board_target = g_general_6s->rpy_2_tr(target_in_board);
    MatrixXd T_base_target = T_base_board * T_board_target;
    VectorXd target_cartesian_base = g_general_6s->tr_2_MCS(T_base_target);
    
    // 阻塞式移动到起始点
    ptp_motion_to_cartesian_base(target_cartesian_base);
    cout << " -> 已到达白板上方起始点！" << endl;

    // ------------------- 3. 记录力矩基准 -------------------
    cout << "\n[内部标定] 记录当前姿态的基准力矩..." << endl;
    sleep(1); // 停留1秒，确保机器人完全静止且受力稳定
    int q_size = tor_deque_out.size();
    if (q_size >= 6) {
        for (int i = 0; i < 6; i++) {
            baseline_tor[i] = tor_deque_out[q_size - 6 + i];
        }
    }

    // ------------------- 4. 智能缓慢下探与接触检测 -------------------
    cout << "\n[动作 3] 开始缓慢下探并检测白板碰撞..." << endl;
    
    // 获取当前点位
    VectorXd origin_point_joint(6);
    for (int i = 0; i < 6; i++) origin_point_joint(i) = g_general_6s->getActPositionAngle(i);
    MatrixXd trans_matrix;
    g_general_6s->calc_forward_kin(origin_point_joint, trans_matrix);
    VectorXd origin_cartesian = g_general_6s->tr_2_MCS(trans_matrix);
    
    // 阻塞式下探（内部遇到碰撞会立即清空轨迹并退出）
    downward_probe_motion(-100, origin_point_joint, origin_cartesian);
    
    // 判断下探动作结束的原因
    if (touch_detected) {
        cout << "[任务状态] 成功接触白板！机器人已安全停止下探。" << endl;
        cout << "   - [力矩详情] 阈值设定: " << TORQUE_THRESHOLD << endl;
        cout << "   - [力矩详情] 基准力矩 J2: " << baseline_tor[1] << " | J3: " << baseline_tor[2] << endl;
        cout << "   - [力矩详情] 触发力矩 J2: " << trigger_tor_1 << " | J3: " << trigger_tor_2 << endl;
        cout << "   - [力矩详情] 实际偏差 J2: " << abs(trigger_tor_1 - baseline_tor[1]) 
             << " | J3: " << abs(trigger_tor_2 - baseline_tor[2]) << endl;
    } else {
        cout << "\n[严重错误] 机器人向下探测了 100mm 仍未接触到白板，已到达行程极限！" << endl;
        cout << " -> 任务终止，将返回原点..." << endl;
        move_home_position();
        return;
    }
    sleep(1); // 缓冲等待

    // ------------------- 5. 额外施加下压力 -------------------
    cout << "\n[动作 4] 施加额外下压 (3mm) 以保证擦拭摩擦力..." << endl;
    for (int i = 0; i < 6; i++) origin_point_joint(i) = g_general_6s->getActPositionAngle(i);
    g_general_6s->calc_forward_kin(origin_point_joint, trans_matrix);
    origin_cartesian = g_general_6s->tr_2_MCS(trans_matrix);
    
    VectorXd press_joint_target(6);
    VectorXd press_cartesian_target(6);
    lining_motion_test(0.0, 0.0, -3.0, origin_point_joint, origin_cartesian, press_joint_target, press_cartesian_target);

    // ------------------- 6. 执行平面擦拭轨迹 -------------------
    cout << "\n[动作 5] 开始执行擦除动作..." << endl;
    for (int i = 0; i < 6; i++) origin_point_joint(i) = g_general_6s->getActPositionAngle(i);
    g_general_6s->calc_forward_kin(origin_point_joint, trans_matrix);
    origin_cartesian = g_general_6s->tr_2_MCS(trans_matrix);
    
    VectorXd wipe_joint_target(6);
    VectorXd wipe_cartesian_target(6);
    // 在当前 X 轴方向平移 100mm (0.1m)
    lining_motion_test(100.0, 0.0, 0.0, origin_point_joint, origin_cartesian, wipe_joint_target, wipe_cartesian_target);
    
    // ------------------- 7. 任务完成与复位 -------------------
    cout << "\n[任务完成] 擦除动作结束！正在抬起画笔并返回原点..." << endl;
    move_home_position();
    cout << " -> 机器人已安全归位。所有任务圆满结束！" << endl;
}
