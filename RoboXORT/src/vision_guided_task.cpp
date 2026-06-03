#include "vision_guided_task.h"

#include <Eigen/Eigen>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

#include "motion_control.h"
#include "vision_client.h"

using namespace Eigen;
using namespace std;

namespace {

struct VisionGuidedTaskConfig {
    string target = "pen";
    bool use_object_angle = true;
    Vector3d grasp_rpy_board = Vector3d(3.14159, 0.0, 0.0);
    double pick_lift_mm = 300.0;
};

static string trim(const string &text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == string::npos) return "";
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

static bool parse_bool_token(const string &text) {
    return text == "1" || text == "true" || text == "TRUE" || text == "on" || text == "ON";
}

static double normalize_symmetric_yaw_near(double yaw, double reference_yaw) {
    const double symmetric_period = 3.14159265358979323846;
    while (yaw - reference_yaw > 0.5 * symmetric_period) yaw -= symmetric_period;
    while (yaw - reference_yaw < -0.5 * symmetric_period) yaw += symmetric_period;
    return yaw;
}

static VisionGuidedTaskConfig load_vision_guided_task_config(const string &config_path = "config.txt") {
    VisionGuidedTaskConfig config;
    ifstream file(config_path);
    if (!file.is_open()) {
        cout << "[视觉任务配置] 未找到 " << config_path << "，使用默认视觉抓取参数。" << endl;
        return config;
    }

    string line;
    while (getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        istringstream iss(line);
        string key;
        iss >> key;
        if (key == "vision_target") {
            iss >> config.target;
        } else if (key == "vision_use_object_angle") {
            string raw;
            iss >> raw;
            config.use_object_angle = parse_bool_token(raw);
        } else if (key == "vision_grasp_rpy_board") {
            iss >> config.grasp_rpy_board(0) >> config.grasp_rpy_board(1) >> config.grasp_rpy_board(2);
        } else if (key == "vision_pick_lift_mm") {
            iss >> config.pick_lift_mm;
        }
    }
    return config;
}

} // namespace

void run_vision_object_task() {
    cout << "\n========== 开始视觉引导物体任务 ==========" << endl;

    const VisionGuidedTaskConfig task_config = load_vision_guided_task_config();
    const VisionClientConfig vision_config = load_vision_client_config();

    cout << "[视觉任务] 等待伺服上电..." << endl;
    while (!PowerStatus) {
        usleep(500000);
    }

    cout << "[视觉任务] 开始前回零..." << endl;
    move_home_position();
    set_gripper(true);

    VisionDetectionResult result;
    string error;
    if (!request_vision_detection(task_config.target, result, error)) {
        cerr << "[视觉任务] 请求失败: " << error << endl;
        return;
    }
    if (!result.ok) {
        cerr << "[视觉任务] 识别失败: " << (result.error.empty() ? error : result.error) << endl;
        return;
    }

    Vector3d object_board(
        result.center_board_mm[0],
        result.center_board_mm[1],
        result.center_board_mm[2]
    );
    Vector3d grasp_rpy_board = task_config.grasp_rpy_board;
    if (task_config.use_object_angle) {
        const double raw_grasp_yaw = grasp_rpy_board(2) + result.angle_board_rad;
        grasp_rpy_board(2) = normalize_symmetric_yaw_near(raw_grasp_yaw, task_config.grasp_rpy_board(2));
    }
    VectorXd object_pose_base = board_pose_to_base_pose(vision_config, object_board, grasp_rpy_board);

    cout << "[视觉任务] target: " << result.target << endl;
    cout << "[视觉任务] center_px: [" << result.center_px[0] << ", " << result.center_px[1] << "]" << endl;
    cout << "[视觉任务] center_board_mm: [" << object_board(0) << ", " << object_board(1) << ", " << object_board(2) << "]" << endl;
    cout << "[视觉任务] angle_board_rad: " << result.angle_board_rad << endl;
    cout << "[视觉任务] cleaned_grasp_yaw_board_rad: " << grasp_rpy_board(2) << endl;
    cout << "[视觉任务] object_pose_base(x y z rx ry rz): " << object_pose_base.transpose() << endl;
    cout << "[视觉任务] pick_lift_mm: " << task_config.pick_lift_mm << endl;

    VectorXd above_object_base = object_pose_base;
    above_object_base(2) += task_config.pick_lift_mm;

    cout << "[视觉任务] PTP 到物体正上方 30cm..." << endl;
    ptp_motion_to_cartesian_base(above_object_base);

    cout << "[视觉任务] MoveLine 向下 30cm..." << endl;
    lining_motion_test(0.0, 0.0, -task_config.pick_lift_mm - 13);

    cout << "[视觉任务] 关闭夹爪..." << endl;
    set_gripper(false);
    usleep(500000);

    cout << "[视觉任务] MoveLine 向上 30cm..." << endl;
    lining_motion_test(0.0, 0.0, task_config.pick_lift_mm);

    cout << "[视觉任务] 回零..." << endl;
    move_home_position();

    cout << "========== 视觉引导物体任务结束 ==========" << endl;
}
