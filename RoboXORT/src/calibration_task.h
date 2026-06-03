#ifndef CALIBRATION_TASK_H
#define CALIBRATION_TASK_H

#include "motion_control.h"
#include "general_6s.h"
#include <Eigen/Eigen>
#include <iostream>
#include <string>

using namespace Eigen;
using namespace std;

/**
 * @brief 执行标定任务，先PTP移动到设定点，然后允许用户通过键盘微调机器人位置
 */
void run_calibration_task();

#endif // CALIBRATION_TASK_H
