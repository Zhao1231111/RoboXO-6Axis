#include "calibration_task.h"
#include <sstream>
#include <vector>

extern void get_joint_Position_initial(VectorXd &position_current_angle);

void run_calibration_task() {
    // ------------------- 初始化与上电阶段 -------------------
    cout << "\n[标定任务] 正在检查使能状态..." << endl;
    while (!PowerStatus) {
        usleep(500000);
    }
    cout << "[标定任务] 伺服上电完成！" << endl;

    // cout << "打开夹爪" << endl;
    // set_gripper(true);
    // usleep(1000000);

    // cout << "闭合夹爪" << endl;
    // set_gripper(false);

    // // ------------------- 0.回到原位 -------------------
    VectorXd position_joint_initial(6);
    get_joint_Position_initial(position_joint_initial);
    cout << "\n[动作 0] 正在复位机器人到初始零点..." << endl;
    move_home_position();
    cout << " -> 已安全回到原位！" << endl;

    // // ------------------- 1. 移动到初始点 -------------------
    // cout << "\n[动作 1] 正在移动到默认基准点..." << endl;
    
    // // 设定默认初始坐标 (为了保持一致，采用与原任务相似的坐标)
    // VectorXd target_cartesian_base(6);
    // target_cartesian_base << 500, 0.0, 400, 3.14159, 0.0, 0.0;
    
    // // 阻塞式移动到起始点
    // ptp_motion_to_cartesian_base(target_cartesian_base);
    // cout << " -> 已到达起始基准点！" << endl;

    // // ------------------- 2. 交互式微调循环 -------------------
    // cout << "\n[动作 2] 开始交互式位置微调" << endl;
    // cout << "说明：" << endl;
    // cout << " - 输入轴方向 (x, y, 或 z) 即可按默认距离 (1.0mm) 移动，例如: x" << endl;
    // cout << " - 输入轴方向和具体距离，用空格隔开，例如: z -5.0" << endl;
    // cout << " - 输入 s 结束标定并输出当前坐标" << endl;
    
    // while (true) {
    //     cout << "\n请输入指令: ";
    //     string input_line;
    //     if (!getline(cin, input_line)) {
    //         break; // EOF
    //     }

    //     // 去除前后空格
    //     if (input_line.empty()) continue;

    //     stringstream ss(input_line);
    //     string cmd;
    //     ss >> cmd;

    //     if (cmd == "s" || cmd == "S") {
    //         break;
    //     }

    //     if (cmd != "x" && cmd != "X" && 
    //         cmd != "y" && cmd != "Y" && 
    //         cmd != "z" && cmd != "Z") {
    //         cout << "指令错误！请输入 x, y, z 或 s。" << endl;
    //         continue;
    //     }

    //     double distance = 50.0; // 默认距离 50 mm
    //     if (!ss.eof()) {
    //         double parsed_dist;
    //         if (ss >> parsed_dist) {
    //             distance = parsed_dist;
    //         }
    //     }

    //     double dx = 0, dy = 0, dz = 0;
    //     if (cmd == "x" || cmd == "X") dx = distance;
    //     else if (cmd == "y" || cmd == "Y") dy = distance;
    //     else if (cmd == "z" || cmd == "Z") dz = distance;

    //     cout << "正在沿 " << cmd << " 轴移动 " << distance << " ..." << endl;

    //     // 获取当前关节角和位姿
    //     VectorXd origin_point_joint(6);
    //     for (int i = 0; i < 6; i++) {
    //         origin_point_joint(i) = g_general_6s->getActPositionAngle(i);
    //     }
    //     MatrixXd trans_matrix;
    //     g_general_6s->calc_forward_kin(origin_point_joint, trans_matrix);
    //     VectorXd origin_cartesian = g_general_6s->tr_2_MCS(trans_matrix);

    //     VectorXd target_joint(6);
    //     VectorXd target_cartesian(6);

    //     // 调用直线插补运动
    //     lining_motion_test(dx, dy, dz, origin_point_joint, origin_cartesian, target_joint, target_cartesian);
    //     cout << "移动完成！" << endl;
    // }

    // // ------------------- 3. 输出标定结果 -------------------
    // cout << "\n[标定结束] 正在计算并输出当前末端坐标..." << endl;
    // VectorXd final_joint(6);
    // for (int i = 0; i < 6; i++) {
    //     final_joint(i) = g_general_6s->getActPositionAngle(i);
    // }
    // MatrixXd final_trans_matrix;
    // g_general_6s->calc_forward_kin(final_joint, final_trans_matrix);
    // VectorXd final_cartesian = g_general_6s->tr_2_MCS(final_trans_matrix);

    // cout << "============== 标定结果 ==============" << endl;
    // cout << "当前关节角 (度):" << endl;
    // cout << final_joint.transpose() << endl;
    // cout << "\n当前笛卡尔坐标 (x, y, z, rx, ry, rz):" << endl;
    // cout << final_cartesian.transpose() << endl;
    // cout << "======================================" << endl;
}
