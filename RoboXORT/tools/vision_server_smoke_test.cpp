//usr/bin/env g++ -std=c++14 -O2 "$0" -o /tmp/vision_server_smoke_test && exec /tmp/vision_server_smoke_test "$@"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

struct VisionServerConfig {
    string host = "127.0.0.1";
    int port = 50051;
    int timeout_ms = 60000;
    string target = "eraser";
};

static string trim(const string &text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == string::npos) return "";
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

static VisionServerConfig load_config(const string &path) {
    VisionServerConfig config;
    ifstream file(path);
    if (!file.is_open()) {
        cerr << "[warn] cannot open " << path << ", using default "
             << config.host << ":" << config.port << endl;
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
            iss >> config.host >> config.port;
        } else if (key == "vision_timeout_ms") {
            iss >> config.timeout_ms;
        } else if (key == "vision_target") {
            iss >> config.target;
        }
    }
    return config;
}

static bool wait_socket(int fd, bool write_ready, int timeout_ms) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    const int ret = select(fd + 1, write_ready ? nullptr : &fds, write_ready ? &fds : nullptr, nullptr, &tv);
    return ret > 0 && FD_ISSET(fd, &fds);
}

static bool connect_with_timeout(int fd, const sockaddr_in &addr, int timeout_ms, string &error) {
    const int old_flags = fcntl(fd, F_GETFL, 0);
    if (old_flags < 0) {
        error = string("fcntl(F_GETFL) failed: ") + strerror(errno);
        return false;
    }
    if (fcntl(fd, F_SETFL, old_flags | O_NONBLOCK) < 0) {
        error = string("fcntl(F_SETFL) failed: ") + strerror(errno);
        return false;
    }

    const int ret = connect(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        error = string("connect failed: ") + strerror(errno);
        return false;
    }

    if (ret < 0) {
        if (!wait_socket(fd, true, timeout_ms)) {
            error = "connect timeout";
            return false;
        }
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0) {
            error = string("connect failed: ") + strerror(so_error == 0 ? errno : so_error);
            return false;
        }
    }

    fcntl(fd, F_SETFL, old_flags);
    return true;
}

static bool send_all(int fd, const string &text, int timeout_ms, string &error) {
    size_t sent = 0;
    while (sent < text.size()) {
        if (!wait_socket(fd, true, timeout_ms)) {
            error = "send timeout";
            return false;
        }
        const ssize_t n = send(fd, text.data() + sent, text.size() - sent, 0);
        if (n <= 0) {
            error = string("send failed: ") + strerror(errno);
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

static bool recv_line(int fd, string &line, int timeout_ms, string &error) {
    line.clear();
    char ch = '\0';
    while (true) {
        if (!wait_socket(fd, false, timeout_ms)) {
            error = "receive timeout";
            return false;
        }
        const ssize_t n = recv(fd, &ch, 1, 0);
        if (n < 0) {
            error = string("recv failed: ") + strerror(errno);
            return false;
        }
        if (n == 0) break;
        if (ch == '\n') break;
        line.push_back(ch);
    }
    return !line.empty();
}

int main(int argc, char **argv) {
    if (argc >= 2 && (string(argv[1]) == "-h" || string(argv[1]) == "--help")) {
        cout << "Usage:\n"
             << "  ./tools/vision_server_smoke_test.cpp [target] [host] [port]\n\n"
             << "Defaults are read from config/config.txt.\n"
             << "Example:\n"
             << "  ./tools/vision_server_smoke_test.cpp eraser\n";
        return 0;
    }

    VisionServerConfig config = load_config("config/config.txt");
    if (argc >= 2) config.target = argv[1];
    if (argc >= 3) config.host = argv[2];
    if (argc >= 4) config.port = atoi(argv[3]);

    cout << "[vision test] server: " << config.host << ":" << config.port << endl;
    cout << "[vision test] target: " << config.target << endl;

    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        cerr << "[vision test] socket failed: " << strerror(errno) << endl;
        return 1;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(config.port));
    if (inet_pton(AF_INET, config.host.c_str(), &addr.sin_addr) != 1) {
        cerr << "[vision test] invalid IPv4 address: " << config.host << endl;
        close(fd);
        return 1;
    }

    string error;
    if (!connect_with_timeout(fd, addr, config.timeout_ms, error)) {
        cerr << "[vision test] " << error << endl;
        close(fd);
        return 1;
    }

    const string request = string("{\"target\":\"") + config.target + "\"}\n";
    cout << "[vision test] request: " << request;
    if (!send_all(fd, request, config.timeout_ms, error)) {
        cerr << "[vision test] " << error << endl;
        close(fd);
        return 1;
    }

    string response;
    if (!recv_line(fd, response, config.timeout_ms, error)) {
        cerr << "[vision test] " << error << endl;
        close(fd);
        return 1;
    }

    cout << "[vision test] raw response:\n" << response << endl;
    close(fd);
    return 0;
}
