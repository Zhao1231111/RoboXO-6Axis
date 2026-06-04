#include "chessboard_tasks.h"
#include "motion_control.h" // 包含所有的运动控制接口
#include "probe_detect_tasks.h"
#include "ipc_server.h"
#include <cmath>

extern void load_task_config(VectorXd& board_center, VectorXd& target_point, int& torque_thresh);

static void copy_pose_orientation(const VectorXd &source_pose, VectorXd &target_pose) {
    target_pose(3) = source_pose(3);
    target_pose(4) = source_pose(4);
    target_pose(5) = source_pose(5);
}

// ======================= 基础几何辅助函数 =======================
static VectorXd get_cell_center_cartesian(const VectorXd &board_center_cartesian, double board_size, int cell_index) {
    VectorXd cell_center = board_center_cartesian;
    if (cell_index < 0) cell_index = 0;
    if (cell_index > 8) cell_index = 8;

    const int row = cell_index / 3;
    const int col = cell_index % 3;
    const double cell_size = board_size / 3.0;

    cell_center(0) = board_center_cartesian(0) + (col - 1) * cell_size;
    cell_center(1) = board_center_cartesian(1) + (1 - row) * cell_size;
    return cell_center;
}

// 快速提取当前真实的六个关节角度
static VectorXd get_current_joint() {
    VectorXd j(6);
    for (int i = 0; i < 6; i++) {
        j(i) = g_general_6s->getActPositionAngle(i);
    }
    return j;
}

// 快速提取当前真实的笛卡尔空间位姿
static VectorXd get_current_cartesian() {
    VectorXd j = get_current_joint();
    MatrixXd T;
    g_general_6s->calc_forward_kin(j, T);
    return g_general_6s->tr_2_MCS(T);
}

// ======================= 运动封装 =======================

// 抬起画笔
static void lift_pen(double draw_z, double pen_lift, const VectorXd &draw_pose) {
    VectorXd current_cartesian = get_current_cartesian();

    VectorXd lifted = current_cartesian;
    lifted(2) = draw_z + pen_lift;
    copy_pose_orientation(draw_pose, lifted);
    
    // 使用阻塞式PTP移动
    ptp_motion_to_cartesian_base(lifted);
}

// 移动画笔到指定 XY 坐标的上方，然后再垂直下笔
static void move_pen_to_xy_without_drawing(double x, double y, double draw_z, double pen_lift, const VectorXd &draw_pose) {
    const double safe_z = draw_z + pen_lift;
    
    // 1. 抬起笔
    lift_pen(draw_z, pen_lift, draw_pose);
    
    // 2. 移动到目标坐标上方
    VectorXd above_target = get_current_cartesian();
    above_target(0) = x;
    above_target(1) = y;
    above_target(2) = safe_z;
    copy_pose_orientation(draw_pose, above_target);
    ptp_motion_to_cartesian_base(above_target);
    
    // 3. 垂直下放画笔
    VectorXd down = above_target;
    down(2) = draw_z;
    ptp_motion_to_cartesian_base(down);
}

// 在平面上绘制一条线段
static void draw_segment_on_plane(double x0, double y0, double x1, double y1,
                                  double draw_z, double pen_lift,
                                  const VectorXd &draw_pose) {
    // 移到起点并下笔
    move_pen_to_xy_without_drawing(x0, y0, draw_z, pen_lift, draw_pose);
    
    // 直线运动到终点
    VectorXd current_joint = get_current_joint();
    VectorXd current_cartesian = get_current_cartesian();
    double dx = x1 - current_cartesian(0);
    double dy = y1 - current_cartesian(1);
    double dz = draw_z - current_cartesian(2); 
    
    lining_motion_test(dx, dy, dz);
    
    // 画完后抬起画笔
    lift_pen(draw_z, pen_lift, draw_pose);
}


// ======================= 具体图元绘制 =======================

void draw_chessboard(const VectorXd &board_center_cartesian,
                            double board_size,
                            double pen_lift) {
    const double half = board_size / 2.0;
    const double cell = board_size / 3.0;
    const double draw_z = board_center_cartesian(2);
    const double cx = board_center_cartesian(0);
    const double cy = board_center_cartesian(1);

    cout << "[任务] 开始绘制 4 条棋盘线..." << endl;
    // 两条竖线
    draw_segment_on_plane(cx - cell / 2.0, cy - half, cx - cell / 2.0, cy + half, draw_z, pen_lift, board_center_cartesian);
    draw_segment_on_plane(cx + cell / 2.0, cy - half, cx + cell / 2.0, cy + half, draw_z, pen_lift, board_center_cartesian);

    // 两条横线
    draw_segment_on_plane(cx - half, cy - cell / 2.0, cx + half, cy - cell / 2.0, draw_z, pen_lift, board_center_cartesian);
    draw_segment_on_plane(cx - half, cy + cell / 2.0, cx + half, cy + cell / 2.0, draw_z, pen_lift, board_center_cartesian);
}

void draw_x(const VectorXd &board_center_cartesian,
                   int cell_index,
                   double board_size,
                   double pen_lift) {
    const VectorXd cell_center = get_cell_center_cartesian(board_center_cartesian, board_size, cell_index);
    const double cell = board_size / 3.0;
    const double half = cell * BOARD_MARK_RATIO; // 留边，避免压到棋盘线
    const double z = board_center_cartesian(2);
    const double cx = cell_center(0);
    const double cy = cell_center(1);

    cout << "[任务] 在格子 " << cell_index << " 画 X..." << endl;
    draw_segment_on_plane(cx - half, cy - half, cx + half, cy + half, z, pen_lift, board_center_cartesian);
    draw_segment_on_plane(cx - half, cy + half, cx + half, cy - half, z, pen_lift, board_center_cartesian);
}

void draw_o(const VectorXd &board_center_cartesian,
                   int cell_index,
                   double board_size,
                   double pen_lift,
                   int segments) {
    (void)segments;

    const VectorXd cell_center = get_cell_center_cartesian(board_center_cartesian, board_size, cell_index);
    const double cell = board_size / 3.0;
    const double radius = cell * BOARD_MARK_RATIO; // 留边，避免压到棋盘线
    const double z = board_center_cartesian(2);
    const double cx = cell_center(0);
    const double cy = cell_center(1);

    cout << "[任务] 在格子 " << cell_index << " 画 O..." << endl;
    const double start_x = cx + radius;
    const double start_y = cy;
    
    // 移到起点并下放
    move_pen_to_xy_without_drawing(start_x, start_y, z, pen_lift, board_center_cartesian);

    VectorXd current_joint = get_current_joint();
    VectorXd current_cartesian = get_current_cartesian();
    VectorXd dummy_j(6), dummy_c(6);

    circle_motion_test(-2.0 * radius, 0.0, 0.0,
                       0.0, 0.0, 0.0,
                       current_joint, current_cartesian, dummy_j, dummy_c);

    lift_pen(z, pen_lift, board_center_cartesian);
}

void erase_chessboard(const VectorXd &board_center_cartesian,
                      double board_size,
                      double pen_lift) {
    // 擦除时多扩展出20mm边缘，确保完全覆盖所有的X和O
    const double half = board_size / 2.0 + 20.0; 
    const double draw_z = board_center_cartesian(2);
    const double cx = board_center_cartesian(0);
    const double cy = board_center_cartesian(1);

    cout << "\n[动作 5] 开始执行 S 形全覆盖擦除动作..." << endl;
    
    // 假设橡皮有效擦除宽度为30mm，计算需要的往复次数
    double step_y = 30.0;
    int num_lines = ceil((half * 2) / step_y);
    if (num_lines < 2) num_lines = 2;
    double actual_step = (half * 2) / (num_lines - 1);

    // 首先移到左上角并下放（此处复用了力矩下探，安全可靠）
    double start_x = cx - half;
    double start_y = cy - half;
    move_pen_to_xy_without_drawing(start_x, start_y, draw_z, pen_lift, board_center_cartesian);

    int dir = 1; // 1代表向右(+X方向)，-1代表向左
    for (int i = 0; i < num_lines; i++) {
        // 横扫 (X轴)
        double dx = (dir == 1) ? (half * 2) : -(half * 2);
        lining_motion_test(dx, 0.0, 0.0);
        
        // 如果不是最后一条线，则向下移动一行 (Y轴)
        if (i < num_lines - 1) {
            lining_motion_test(0.0, actual_step, 0.0);
        }
        
        dir = -dir; // 换向
    }
    
    // 擦完后抬起橡皮
    lift_pen(draw_z, pen_lift, board_center_cartesian);
}

// ======================= 主接口 =======================

void draw_tic_tac_toe_task() {
    cout << "\n========== 开始绘制井字棋任务 ==========" << endl; 

    // ------------------- 初始化与上电阶段 -------------------
    cout << "\n[任务开始] 正在检查使能状态..." << endl;
    while (!PowerStatus) {
        usleep(500000);
    }
    cout << "[任务状态] 伺服上电完成！" << endl;


    // 1. 获取棋盘中心配置
    VectorXd board_center_cartesian(6), pen_target(6), eraser_target(6);
    board_center_cartesian << 527.299, -0.492295, 330.026, 3.14159, 0.0, 0.0;
    pen_target << 527.294, -257.495, 370.013, 3.14159, 0.0, 0.0;
    eraser_target << 527.294, 249.506, 330.013, 3.14159, 0.0, 0.0;

    VectorXd origin_cartesian(6);
    origin_cartesian = board_center_cartesian;

    double pen_z = board_center_cartesian(2);
    double eraser_z = origin_cartesian(2);
    
    
    // 3. 开始执行
    cout << "\n[动作 1] 复位..." << endl;
    move_home_position();

    // 抓取画笔

    grasp_pen(pen_target, pen_z);

    VectorXd temp(6);
    temp = board_center_cartesian;
    temp(2) += 50;
    ptp_motion_to_cartesian_base(temp);
    lining_motion_test(0.0, 0.0, -50.0);
    if (!probe_and_press(100, pen_z)) {
        return; // 如果探测失败，则直接退出任务
    }
    
    // 更新棋盘中心坐标（使后续所有绘图都基于刚刚检测到的实际接触高度）
    board_center_cartesian(2) = pen_z;
    
    // 画棋盘
    draw_chessboard(board_center_cartesian);
    
    cout << "\n[动作 3] 回归安全点..." << endl;
    move_home_position();
    
    // ================== 用户交互循环 ==================
    while (true) {
        cout << "\n===============================" << endl;
        cout << "请输入下一步指令: " << endl;
        cout << "  x [0-8] : 在指定格子画 X (例: x 4)" << endl;
        cout << "  o [0-8] : 在指定格子画 O (例: o 0)" << endl;
        cout << "  e       : 结束绘画，开始擦除" << endl;
        cout << "指令> ";
        
        string cmd;
        cin >> cmd;
        
        if (cmd == "e" || cmd == "E") {
            cout << "退出绘画模式..." << endl;
            break;
        } else if (cmd == "x" || cmd == "X") {
            int pos;
            cin >> pos;
            if (pos >= 0 && pos <= 8) {
                draw_x(board_center_cartesian, pos);
                move_home_position();
            } else {
                cout << "位置错误：请输入 0~8 的数字！" << endl;
            }
        } else if (cmd == "o" || cmd == "O") {
            int pos;
            cin >> pos;
            if (pos >= 0 && pos <= 8) {
                draw_o(board_center_cartesian, pos);
                move_home_position();
            } else {
                cout << "位置错误：请输入 0~8 的数字！" << endl;
            }
        } else {
            cout << "未知指令，请重新输入！" << endl;
            // 清理输入流，防止死循环
            cin.clear();
            cin.ignore(10000, '\n');
        }
    }
    // =================================================

    grasp_eraser(eraser_target, eraser_z);
    temp = board_center_cartesian;
    temp(2) += 50;
    ptp_motion_to_cartesian_base(temp);
    lining_motion_test(0.0, 0.0, -50.0);
    if (!probe_and_press(100, eraser_z)) {
        return; // 如果探测失败，则直接退出任务
    }
    
    // 更新棋盘中心坐标（使后续所有动作基于擦除高度）
    board_center_cartesian(2) = eraser_z;

    // 调用封装好的智能全覆盖擦除动作
    erase_chessboard(board_center_cartesian);
    
    
    move_home_position();

    cout << "\n========== 井字棋任务圆满结束 ==========" << endl;
}

// ======================= 异步任务执行线程 =======================
// 全局状态变量，用于保存每次动作后的高度信息
static VectorXd g_board_center_cartesian(6);
static VectorXd g_pen_target(6);
static VectorXd g_eraser_target(6);
static double g_pen_z = 0.0;
static double g_eraser_z = 0.0;

void task_executor_loop() {
    cout << "[Task Executor] 任务执行线程已启动..." << endl;
    
    // 初始化位置信息
    g_board_center_cartesian << 527.299, -0.492295, 330.026, 3.14159, 0.0, 0.0;
    g_pen_target << 527.294, -257.495, 370.013, 3.14159, 0.0, 0.0;
    g_eraser_target << 527.294, 249.506, 330.013, 3.14159, 0.0, 0.0;
    g_pen_z = g_board_center_cartesian(2);
    g_eraser_z = g_board_center_cartesian(2);

    while (!g_estop.load(std::memory_order_relaxed)) {
        ipc::TaskCommandPayload cmd;
        if (g_task_queue.try_pop(cmd)) {
            // 等待伺服上电
            while (!PowerStatus && !g_estop) {
                usleep(100000);
            }
            if (g_estop) break;

            cout << "\n[Task Executor] 收到任务指令: " << (int)cmd.task_id << " 参数: " << cmd.arg1 << endl;
            
            switch (cmd.task_id) {
                case TASK_GRASP_PEN:
                    move_home_position();
                    grasp_pen(g_pen_target, g_pen_z);
                    {
                        VectorXd temp = g_board_center_cartesian;
                        temp(2) += 50;
                        ptp_motion_to_cartesian_base(temp);
                        lining_motion_test(0.0, 0.0, -50.0);
                        if (probe_and_press(100, g_pen_z)) {
                            g_board_center_cartesian(2) = g_pen_z;
                            draw_chessboard(g_board_center_cartesian);
                        }
                    }
                    move_home_position();
                    break;

                case TASK_DRAW_O:
                    if (cmd.arg1 >= 0 && cmd.arg1 <= 8) {
                        draw_o(g_board_center_cartesian, cmd.arg1);
                        move_home_position();
                    }
                    break;
                    
                case TASK_DRAW_X:
                    if (cmd.arg1 >= 0 && cmd.arg1 <= 8) {
                        draw_x(g_board_center_cartesian, cmd.arg1);
                        move_home_position();
                    }
                    break;

                case TASK_ERASE_BOARD:
                    grasp_eraser(g_eraser_target, g_eraser_z);
                    {
                        VectorXd temp = g_board_center_cartesian;
                        temp(2) += 50;
                        ptp_motion_to_cartesian_base(temp);
                        lining_motion_test(0.0, 0.0, -50.0);
                        if (probe_and_press(100, g_eraser_z)) {
                            g_board_center_cartesian(2) = g_eraser_z;
                            erase_chessboard(g_board_center_cartesian);
                        }
                    }
                    move_home_position();
                    break;

                default:
                    cout << "[Task Executor] 未知任务 ID: " << (int)cmd.task_id << endl;
                    break;
            }
            cout << "[Task Executor] 任务执行完毕" << endl;
        } else {
            usleep(10000); // 10ms polling
        }
    }
    cout << "[Task Executor] 退出" << endl;
}
