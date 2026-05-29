#ifndef CHESSBOARD_TASKS_H
#define CHESSBOARD_TASKS_H

#include <deque>
#include <unistd.h>
#include <iostream>
#include <Eigen/Eigen>
#include "general_6s.h"

using namespace Eigen;
using namespace std;

extern General_6S* g_general_6s;

#define BOARD_SIZE_MM              500.0   // 棋盘总边长：50 cm × 50 cm
#define BOARD_PEN_LIFT_MM          20.0    // 抬笔高度
#define BOARD_MARK_RATIO           0.32    // X/O 占单格半宽比例，建议 0.25~0.40
#define BOARD_O_SEGMENTS           72      // O 的折线段数，越大越圆

// 主入口函数
void draw_tic_tac_toe_task();

void draw_chessboard(const VectorXd &board_center_cartesian,
                            double board_size = BOARD_SIZE_MM,
                            double pen_lift = BOARD_PEN_LIFT_MM);

void draw_x(const VectorXd &board_center_cartesian,
                   int cell_index,
                   double board_size = BOARD_SIZE_MM,
                   double pen_lift = BOARD_PEN_LIFT_MM);
void draw_o(const VectorXd &board_center_cartesian,
                   int cell_index,
                   double board_size = BOARD_SIZE_MM,
                   double pen_lift = BOARD_PEN_LIFT_MM,
                   int segments = BOARD_O_SEGMENTS);

#endif // CHESSBOARD_TASKS_H
