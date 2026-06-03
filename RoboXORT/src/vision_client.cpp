#include "vision_client.h"

#include <arpa/inet.h>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

using namespace Eigen;
using namespace std;

namespace {

static string trim(const string &text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == string::npos) return "";
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

static string json_escape(const string &text) {
    string out;
    for (char c : text) {
        if (c == '\\' || c == '"') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

static bool find_json_value(const string &json, const string &key, size_t &value_pos) {
    const string pattern = "\"" + key + "\"";
    size_t key_pos = json.find(pattern);
    if (key_pos == string::npos) return false;
    size_t colon = json.find(':', key_pos + pattern.size());
    if (colon == string::npos) return false;
    value_pos = json.find_first_not_of(" \t\r\n", colon + 1);
    return value_pos != string::npos;
}

static bool parse_json_bool(const string &json, const string &key, bool &value) {
    size_t pos = 0;
    if (!find_json_value(json, key, pos)) return false;
    if (json.compare(pos, 4, "true") == 0) {
        value = true;
        return true;
    }
    if (json.compare(pos, 5, "false") == 0) {
        value = false;
        return true;
    }
    return false;
}

static bool parse_json_string(const string &json, const string &key, string &value) {
    size_t pos = 0;
    if (!find_json_value(json, key, pos) || json[pos] != '"') return false;
    ++pos;
    string out;
    bool escape = false;
    for (; pos < json.size(); ++pos) {
        const char c = json[pos];
        if (escape) {
            out.push_back(c);
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else if (c == '"') {
            value = out;
            return true;
        } else {
            out.push_back(c);
        }
    }
    return false;
}

static bool parse_json_number(const string &json, const string &key, double &value) {
    size_t pos = 0;
    if (!find_json_value(json, key, pos)) return false;
    size_t end = pos;
    while (end < json.size()) {
        const char c = json[end];
        if (!(isdigit(c) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')) break;
        ++end;
    }
    if (end == pos) return false;
    value = stod(json.substr(pos, end - pos));
    return true;
}

static bool parse_json_number_array(const string &json, const string &key, vector<double> &values) {
    size_t pos = 0;
    if (!find_json_value(json, key, pos) || json[pos] != '[') return false;
    size_t end = json.find(']', pos + 1);
    if (end == string::npos) return false;

    values.clear();
    string body = json.substr(pos + 1, end - pos - 1);
    string token;
    stringstream ss(body);
    while (getline(ss, token, ',')) {
        token = trim(token);
        if (!token.empty()) values.push_back(stod(token));
    }
    return true;
}

static bool parse_detection_response(const string &json, VisionDetectionResult &result, string &error) {
    bool ok = false;
    if (!parse_json_bool(json, "ok", ok)) {
        error = "response has no ok field";
        return false;
    }
    result.ok = ok;
    parse_json_string(json, "target", result.target);
    parse_json_string(json, "frame", result.frame);
    parse_json_string(json, "error", result.error);

    if (!ok) {
        error = result.error.empty() ? "vision server returned ok=false" : result.error;
        return true;
    }

    parse_json_number_array(json, "center_px", result.center_px);
    parse_json_number_array(json, "direction_px", result.direction_px);
    parse_json_number(json, "angle_px_rad", result.angle_px_rad);
    parse_json_number_array(json, "center_board_mm", result.center_board_mm);
    parse_json_number_array(json, "direction_board", result.direction_board);
    parse_json_number(json, "angle_board_rad", result.angle_board_rad);
    parse_json_number_array(json, "bbox", result.bbox);
    parse_json_number(json, "confidence", result.confidence);
    parse_json_number_array(json, "warped_size_px", result.warped_size_px);
    parse_json_number_array(json, "whiteboard_size_mm", result.whiteboard_size_mm);

    if (result.center_px.size() < 2 || result.center_board_mm.size() < 3) {
        error = "response missing center fields";
        return false;
    }
    return true;
}

static bool recv_json_line(int sock, string &line, string &error) {
    line.clear();
    char buffer[1024];
    while (true) {
        const ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
        if (n < 0) {
            error = string("recv failed: ") + strerror(errno);
            return false;
        }
        if (n == 0) {
            error = "connection closed before newline";
            return false;
        }
        for (ssize_t i = 0; i < n; ++i) {
            if (buffer[i] == '\n') return true;
            line.push_back(buffer[i]);
        }
        if (line.size() > 65536) {
            error = "response line too long";
            return false;
        }
    }
}

static Matrix3d rpy_to_rotation(double rx, double ry, double rz) {
    return AngleAxisd(rx, Vector3d::UnitX()).toRotationMatrix()
         * AngleAxisd(ry, Vector3d::UnitY()).toRotationMatrix()
         * AngleAxisd(rz, Vector3d::UnitZ()).toRotationMatrix();
}

static Vector3d rotation_to_rpy(const Matrix3d &m) {
    Vector3d rpy;
    rpy(0) = atan2(-m(1, 2), m(2, 2));
    const double sr = sin(rpy(0));
    const double cr = cos(rpy(0));
    rpy(1) = atan2(m(0, 2), cr * m(2, 2) - sr * m(1, 2));
    rpy(2) = atan2(-m(0, 1), m(0, 0));
    return rpy;
}

} // namespace

VisionClientConfig load_vision_client_config(const string &config_path) {
    VisionClientConfig config;
    ifstream file(config_path);
    if (!file.is_open()) {
        cout << "[视觉配置] 未找到 " << config_path << "，使用默认视觉连接参数。" << endl;
        return config;
    }

    string line;
    while (getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        istringstream iss(line);
        string key;
        iss >> key;
        if (key == "vision_server") {
            iss >> config.server_ip >> config.server_port;
        } else if (key == "vision_timeout_ms") {
            iss >> config.timeout_ms;
        } else if (key == "whiteboard_size_mm") {
            iss >> config.whiteboard_width_mm >> config.whiteboard_height_mm;
        } else if (key == "board_center_base") {
            for (int i = 0; i < 6; ++i) iss >> config.board_center_base(i);
        }
    }
    return config;
}

bool request_vision_detection(const string &target, VisionDetectionResult &result, string &error) {
    const VisionClientConfig config = load_vision_client_config();
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        error = string("socket failed: ") + strerror(errno);
        return false;
    }

    timeval timeout;
    timeout.tv_sec = config.timeout_ms / 1000;
    timeout.tv_usec = (config.timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(config.server_port));
    if (inet_pton(AF_INET, config.server_ip.c_str(), &addr.sin_addr) != 1) {
        error = "invalid vision_server ip: " + config.server_ip;
        close(sock);
        return false;
    }

    if (connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        error = string("connect failed: ") + strerror(errno);
        close(sock);
        return false;
    }

    const string request = string("{\"target\":\"") + json_escape(target) + "\"}\n";
    if (send(sock, request.c_str(), request.size(), 0) != static_cast<ssize_t>(request.size())) {
        error = string("send failed: ") + strerror(errno);
        close(sock);
        return false;
    }

    string response;
    if (!recv_json_line(sock, response, error)) {
        close(sock);
        return false;
    }
    close(sock);

    if (!parse_detection_response(response, result, error)) return false;
    if (result.ok && result.center_board_mm.size() >= 3) {
        Vector3d board_point(result.center_board_mm[0], result.center_board_mm[1], result.center_board_mm[2]);
        result.center_base_mm = board_point_to_base(config, board_point);
    }
    return true;
}

Vector3d pixel_to_board(
    double u,
    double v,
    double image_width_px,
    double image_height_px,
    double whiteboard_width_mm,
    double whiteboard_height_mm
) {
    const double u0 = (image_width_px - 1.0) * 0.5;
    const double v0 = (image_height_px - 1.0) * 0.5;
    const double x_board = (v - v0) * whiteboard_height_mm / (image_height_px - 1.0);
    const double y_board = (u - u0) * whiteboard_width_mm / (image_width_px - 1.0);
    return Vector3d(x_board, y_board, 0.0);
}

Vector3d board_point_to_base(const VisionClientConfig &config, const Vector3d &point_board_mm) {
    const Vector3d translation(
        config.board_center_base(0),
        config.board_center_base(1),
        config.board_center_base(2)
    );
    const Matrix3d rotation = rpy_to_rotation(
        config.board_center_base(3),
        config.board_center_base(4),
        config.board_center_base(5)
    );
    return translation + rotation * point_board_mm;
}

VectorXd board_pose_to_base_pose(
    const VisionClientConfig &config,
    const Vector3d &point_board_mm,
    const Vector3d &rpy_board
) {
    VectorXd pose_base(6);
    const Vector3d point_base = board_point_to_base(config, point_board_mm);
    const Matrix3d rotation_base_board = rpy_to_rotation(
        config.board_center_base(3),
        config.board_center_base(4),
        config.board_center_base(5)
    );
    const Matrix3d rotation_base_target = rotation_base_board * rpy_to_rotation(
        rpy_board(0),
        rpy_board(1),
        rpy_board(2)
    );
    const Vector3d rpy_base = rotation_to_rpy(rotation_base_target);
    pose_base << point_base(0), point_base(1), point_base(2), rpy_base(0), rpy_base(1), rpy_base(2);
    return pose_base;
}

int run_vision_test(const string &target) {
    VisionDetectionResult result;
    string error;
    if (!request_vision_detection(target, result, error)) {
        cerr << "[视觉测试] 请求失败: " << error << endl;
        return 2;
    }
    if (!result.ok) {
        cerr << "[视觉测试] 识别失败: " << (result.error.empty() ? error : result.error) << endl;
        return 2;
    }

    cout << "\n========== 视觉识别测试结果 ==========" << endl;
    cout << "target: " << result.target << endl;
    cout << "frame: " << result.frame << endl;
    cout << "center_px: [" << result.center_px[0] << ", " << result.center_px[1] << "]" << endl;
    cout << "center_board_mm: [" << result.center_board_mm[0] << ", " << result.center_board_mm[1] << ", "
         << result.center_board_mm[2] << "]" << endl;
    cout << "center_base_mm(verify only): [" << result.center_base_mm(0) << ", " << result.center_base_mm(1)
         << ", " << result.center_base_mm(2) << "]" << endl;
    if (result.direction_px.size() >= 2) {
        cout << "direction_px: [" << result.direction_px[0] << ", " << result.direction_px[1] << "]" << endl;
    }
    if (result.direction_board.size() >= 2) {
        cout << "direction_board: [" << result.direction_board[0] << ", " << result.direction_board[1] << ", 0]" << endl;
    }
    cout << "angle_px_rad: " << result.angle_px_rad << endl;
    cout << "angle_board_rad: " << result.angle_board_rad << endl;
    cout << "confidence: " << result.confidence << endl;
    if (result.bbox.size() >= 4) {
        cout << "bbox: [" << result.bbox[0] << ", " << result.bbox[1] << ", " << result.bbox[2] << ", "
             << result.bbox[3] << "]" << endl;
    }
    cout << "=====================================" << endl;
    return 0;
}
