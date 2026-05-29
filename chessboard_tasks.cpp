#include "chessboard_tasks.h"
#include "motion_control.h" // 包含所有的运动控制接口
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
    
    VectorXd dummy_j(6), dummy_c(6);
    lining_motion_test(dx, dy, dz, current_joint, current_cartesian, dummy_j, dummy_c);
    
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

// ======================= 主接口 =======================

void draw_tic_tac_toe_task() {
    cout << "\n========== 开始绘制井字棋任务 ==========" << endl; 

    // ------------------- 初始化与上电阶段 -------------------
    cout << "\n[任务开始] 正在检查使能状态..." << endl;
    while (!PowerStatus) {
        usleep(500000);
    }
    cout << "[任务状态] 伺服上电完成！" << endl;


    // 1. 获取棋盘中心配置（与探测任务共用配置文件 config.txt）
    VectorXd board_center_cartesian(6);
    board_center_cartesian << 0.5, 0.0, 0.3, 0.0, 0.0, 0.0;
    VectorXd dummy_target(6);
    int dummy_torque = 150;
    
    load_task_config(board_center_cartesian, dummy_target, dummy_torque);
    
    // 修正棋盘中心位姿，强制设为笔尖向下 (RX = 180度, RY = 0, RZ = 0)
    // 这样在 draw_chessboard 和 draw_x/o 时，所有的点都会自动继承这个向下姿态！
    board_center_cartesian(3) = 3.14159;
    board_center_cartesian(4) = 0.0;
    board_center_cartesian(5) = 0.0;
    
    // 3. 开始执行
    cout << "\n[动作 1] 复位..." << endl;
    move_home_position();
    
    cout << "\n[动作 2] 移动到棋盘中心上方..." << endl;
    VectorXd top_center = board_center_cartesian;
    top_center(2) += BOARD_PEN_LIFT_MM + 70;
    ptp_motion_to_cartesian_base(top_center);

    // 抓取物体
    grasp_object(top_center, BOARD_PEN_LIFT_MM + 75); // 垂直下降100mm
    
    // 画棋盘
    // draw_chessboard(board_center_cartesian);
    
    // cout << "\n[动作 3] 回归安全点..." << endl;
    // move_home_position();
    
    // draw_x(board_center_cartesian, 0);
    // move_home_position();
    
    draw_o(board_center_cartesian, 4);
    move_home_position();
    
    // draw_x(board_center_cartesian, 8);
    // move_home_position();

    cout << "\n========== 井字棋任务圆满结束 ==========" << endl;
}
