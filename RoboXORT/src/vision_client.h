#ifndef VISION_CLIENT_H
#define VISION_CLIENT_H

#include <Eigen/Eigen>
#include <string>
#include <vector>

struct VisionClientConfig {
    std::string server_ip = "127.0.0.1";
    int server_port = 50051;
    int timeout_ms = 60000;
    double whiteboard_width_mm = 655.0;
    double whiteboard_height_mm = 460.0;
    Eigen::VectorXd board_center_base;

    VisionClientConfig() : board_center_base(6) {
        board_center_base << 0.0, 0.0, 0.0, 0.0, 0.0, 0.0;
    }
};

struct VisionDetectionResult {
    bool ok = false;
    std::string target;
    std::string frame;
    std::string error;
    std::vector<double> center_px;
    std::vector<double> direction_px;
    double angle_px_rad = 0.0;
    std::vector<double> center_board_mm;
    std::vector<double> direction_board;
    double angle_board_rad = 0.0;
    std::vector<double> bbox;
    double confidence = 0.0;
    std::vector<double> warped_size_px;
    std::vector<double> whiteboard_size_mm;
    Eigen::Vector3d center_base_mm = Eigen::Vector3d::Zero();
};

VisionClientConfig load_vision_client_config(const std::string &config_path = "config.txt");
bool request_vision_detection(const std::string &target, VisionDetectionResult &result, std::string &error);
Eigen::Vector3d pixel_to_board(
    double u,
    double v,
    double image_width_px,
    double image_height_px,
    double whiteboard_width_mm,
    double whiteboard_height_mm
);
Eigen::Vector3d board_point_to_base(const VisionClientConfig &config, const Eigen::Vector3d &point_board_mm);
Eigen::VectorXd board_pose_to_base_pose(
    const VisionClientConfig &config,
    const Eigen::Vector3d &point_board_mm,
    const Eigen::Vector3d &rpy_board
);
int run_vision_test(const std::string &target);

#endif
