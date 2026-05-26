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
    // **************************** 配置关节运动信息 ***************************
    double current_angle_double[6];
    VectorXd joint_angles_degree_offset(6);
    VectorXd joint_angles_degree_offset1(6);
    VectorXd joint_angles_degree_offset2(6);
    joint_angles_degree_offset1<<  30, 0, 0, 0, 0, 0;
    joint_angles_degree_offset<<    0, 0, 0, 0, 0, 0;
    joint_angles_degree_offset2<< -30, 0, 0, 0, 0, 0;

    //******************************** 运动测试 *******************************
    VectorXd position_joint_initial(6);     // 初始关节位置
    VectorXd target_point_joint_test(6);    // 目标关节位置
    VectorXd target_point_cartesian_test(6);      // 目标直角坐标

    //   1. 复位
    get_joint_Position_initial(position_joint_initial);     // 读取初始时刻关节真实位置
    move_home_position(position_joint_initial);
    //    2. 检查复位的关节序列是否输出完毕
    cout << "已回到原位" << endl;   

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
                downward_probe_motion(-100, origin_point_joint, origin_cartesian);
                
                // 持续等待触碰
                while (is_touch_probing && !g_general_6s->get_angle_deque().empty()) {
                    if (touch_detected) {
                        cout << "[任务状态] 接触白板！已停止下探。" << endl;
                        cout << "     -> [力矩详情] 阈值设定: " << TORQUE_THRESHOLD << endl;
                        cout << "     -> [力矩详情] 基准力矩 J2: " << baseline_tor[1] << " | J3: " << baseline_tor[2] << endl;
                        cout << "     -> [力矩详情] 触发力矩 J2: " << trigger_tor_1 << " | J3: " << trigger_tor_2 << endl;
                        cout << "     -> [力矩详情] 实际偏差 J2: " << abs(trigger_tor_1 - baseline_tor[1]) 
                             << " | J3: " << abs(trigger_tor_2 - baseline_tor[2]) << endl;
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
                lining_motion_test(0.0, 0.0, -3.0, origin_point_joint, origin_cartesian, press_joint_target, press_cartesian_target);
                
                while (PowerStatus && !g_general_6s->get_angle_deque().empty()) {
                    usleep(100000);
                }
                
                current_state = TaskState::TASK_FINISHED;
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
