# 工控机与个人服务器 Socket 通信技术说明

本文只说明当前项目中工控机和个人服务器之间使用的通信技术栈、协议形式和实现要点，不讨论通信内容代表的具体业务含义。其他任务如果只需要参考“如何让工控机和个人服务器通信”，可以直接复用本文方案。

## 1. 使用的技术栈

当前通信方案采用：

| 层级 | 技术 | 说明 |
| --- | --- | --- |
| 网络传输 | TCP/IP | 面向连接、可靠字节流，适合工控机向服务器发起低频请求并等待结果。 |
| Socket 类型 | IPv4 TCP Socket | C++ 端使用 `AF_INET + SOCK_STREAM`，Python 端监听 IPv4 地址和端口。 |
| 服务端语言 | Python 3 | 使用标准库 `socketserver.ThreadingTCPServer` 实现 TCP 服务。 |
| 客户端语言 | C++ | 使用 POSIX socket API：`socket`、`connect`、`send`、`recv`、`close`。 |
| 消息格式 | JSON | 请求和响应都是 JSON 对象，便于跨语言解析和调试。 |
| 消息边界 | 换行符 `\n` | 每条 JSON 消息以一行表示，即 JSON Lines 风格。 |
| 编码 | UTF-8 | Python 端用 UTF-8 编解码；C++ 端按普通字符串收发。 |
| 连接模式 | 短连接 | 每次请求新建 TCP 连接，收发完成后关闭。 |

一句话概括：这是一个“C++ 工控机客户端通过 TCP 连接 Python 服务器，发送一行 JSON，接收一行 JSON”的轻量级跨机器通信方案。

## 2. 通信拓扑

```text
工控机 C++ 程序
  |
  | 1. socket(AF_INET, SOCK_STREAM)
  | 2. connect(server_ip, server_port)
  | 3. send(JSON + "\n")
  | 4. recv() until "\n"
  | 5. close()
  v
个人服务器 Python TCP Server
  |
  | 1. bind(host, port)
  | 2. listen()
  | 3. accept()
  | 4. read until "\n"
  | 5. json.loads()
  | 6. json.dumps() + "\n"
  v
返回工控机
```

当前代码中的服务端监听地址和端口通过命令行参数指定，默认是：

```text
0.0.0.0:50051
```

工控机侧通过配置文件指定服务器地址，例如：

```text
remote_server 192.168.20.2 50051
remote_timeout_ms 60000
```

其中：

- `192.168.20.2` 是个人服务器在工控机可访问网络中的 IPv4 地址。
- `50051` 是 TCP 端口。
- `60000` 是 socket 收发超时时间，单位 ms。

## 3. 协议形式

### 3.1 JSON Lines

当前协议不是 HTTP，也不是 gRPC，而是直接跑在 TCP 上的 JSON Lines。

每条消息格式：

```text
<一个完整 JSON 对象>\n
```

示例：

```text
{"command":"run","payload":{}}\n
```

服务端返回也一样：

```text
{"ok":true,"data":{}}\n
```

换行符 `\n` 是消息结束标记。C++ 客户端会持续 `recv()`，直到读到 `\n` 才认为一条响应完整。

### 3.2 为什么必须有消息边界

TCP 是字节流协议，不保留应用层消息边界。也就是说：

- 一次 `send()` 的内容，可能被对方多次 `recv()` 才收完。
- 多次 `send()` 的内容，也可能被一次 `recv()` 一起读到。
- `recv()` 返回多少字节由内核缓冲区和网络状态决定，不等于一条完整消息。

所以应用层必须自己定义“消息什么时候结束”。当前项目选择用换行符 `\n` 作为分隔符。

这种做法的优点：

- 实现简单。
- 文本可读。
- Python 和 C++ 都容易处理。
- 可以直接用 `nc`、`printf` 等工具手工测试。
- 对低频请求足够可靠。

限制：

- JSON 内容本身不能直接包含未转义的换行作为消息边界。
- 不适合传输大图片、大数组、连续视频流。
- 高吞吐或强实时场景应考虑长度包头、二进制协议、HTTP、WebSocket 或 gRPC。

## 4. 服务端实现方式

服务端核心使用 Python 标准库 `socketserver`。

### 4.1 TCP Server 类型

当前服务端继承：

```python
socketserver.ThreadingTCPServer
```

关键设置：

```python
allow_reuse_address = True
```

含义：

- `ThreadingTCPServer`：每个连接由独立 handler 处理。
- `allow_reuse_address`：服务重启后可以更快重新绑定同一端口，减少 `Address already in use` 问题。

服务端绑定方式：

```python
server = TaskTCPServer((args.host, int(args.port)), TaskRequestHandler, args)
server.serve_forever()
```

如果 `host` 是 `0.0.0.0`，表示监听所有网卡，工控机可以通过服务器实际网卡 IP 访问。

如果 `host` 是 `127.0.0.1`，则只允许服务器本机访问，工控机无法连接。

### 4.2 请求读取

服务端不是一次性 `recv()` 后直接解析，而是循环读取，直到遇到 `\n`：

```python
chunks = []
while True:
    data = self.request.recv(4096)
    if not data:
        break
    if b"\n" in data:
        before, _after = data.split(b"\n", 1)
        chunks.append(before)
        break
    chunks.append(data)
```

同时限制请求行最大长度：

```python
if sum(len(chunk) for chunk in chunks) > 8192:
    raise ValueError("request line is too long")
```

这是一个很重要的防护：如果客户端异常发送超长内容或一直不发换行，服务端不会无限缓存数据。

### 4.3 JSON 解析与响应

服务端收到一行后：

```python
request = json.loads(raw.decode("utf-8"))
```

返回时：

```python
self.request.sendall(
    (json.dumps(response, ensure_ascii=False) + "\n").encode("utf-8")
)
```

这里用 `sendall()` 而不是 `send()`，因为：

- `send()` 可能只发送部分字节。
- `sendall()` 会尽量发送完整 buffer，失败才抛异常。

### 4.4 统一错误响应

服务端用 `try/except` 包住请求处理逻辑：

```python
try:
    ...
except Exception as exc:
    response = {"ok": False, "error": str(exc)}
```

这样即使请求格式错误、参数错误或服务端内部异常，客户端也能收到一条合法 JSON，而不是只能看到连接断开。

建议保留这个设计。工控机侧收到 `ok=false` 后，可以打印 `error` 并停止后续动作。

### 4.5 并发与锁

当前服务端虽然是多线程 TCP Server，但内部对核心处理逻辑加了：

```python
self.lock = threading.Lock()
```

并在处理请求时：

```python
with self.lock:
    ...
```

这种设计适合共享硬件、共享模型、共享文件输出目录等场景。多个客户端可以连接，但真正执行核心逻辑时串行化，避免资源竞争。

如果后续任务完全无共享资源，可以去掉锁；如果任务需要排队处理，也可以把锁替换成任务队列。

## 5. 客户端实现方式

工控机侧使用 C++ POSIX socket API。

### 5.1 创建 Socket

```cpp
const int sock = socket(AF_INET, SOCK_STREAM, 0);
```

含义：

- `AF_INET`：IPv4。
- `SOCK_STREAM`：TCP。
- 第三个参数 `0`：使用默认 TCP 协议。

### 5.2 设置超时

客户端设置发送和接收超时：

```cpp
timeval timeout;
timeout.tv_sec = config.timeout_ms / 1000;
timeout.tv_usec = (config.timeout_ms % 1000) * 1000;
setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
```

作用：

- 防止服务端无响应时 C++ 程序永久阻塞在 `recv()`。
- 防止网络异常时永久阻塞在 `send()`。

注意：当前主客户端的 `connect()` 本身主要依赖系统默认连接超时。项目里的 smoke test 工具提供了更完整的非阻塞连接超时实现，后续可以复用。

### 5.3 设置服务器地址

```cpp
sockaddr_in addr;
memset(&addr, 0, sizeof(addr));
addr.sin_family = AF_INET;
addr.sin_port = htons(static_cast<uint16_t>(config.server_port));
inet_pton(AF_INET, config.server_ip.c_str(), &addr.sin_addr);
```

关键点：

- `htons()` 把端口转换成网络字节序。
- `inet_pton()` 把字符串 IPv4 转换成二进制地址。
- 当前客户端只支持 IPv4，不支持域名和 IPv6。

### 5.4 建立连接

```cpp
connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
```

连接失败时关闭 socket：

```cpp
close(sock);
```

常见失败原因：

- 服务端没有启动。
- IP 或端口配置错误。
- 服务端只监听 `127.0.0.1`。
- 防火墙拦截。
- 两台机器网络不通。

### 5.5 发送请求

当前主客户端构造 JSON 字符串，并追加换行：

```cpp
const string request = string("{\"command\":\"run\",\"payload\":{}}\n");
send(sock, request.c_str(), request.size(), 0);
```

注意：严格来说，`send()` 可能只发送部分内容。当前请求很短，实际问题不大；但更推荐后续使用循环发送：

```cpp
bool send_all(int fd, const string &text) {
    size_t sent = 0;
    while (sent < text.size()) {
        ssize_t n = send(fd, text.data() + sent, text.size() - sent, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}
```

项目中的 smoke test 工具已经实现了带 `select()` 超时控制的 `send_all()`，后续推荐把它迁移进正式客户端。

### 5.6 接收响应

C++ 客户端循环 `recv()`，直到读到 `\n`：

```cpp
while (true) {
    const ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
    if (n <= 0) {
        ...
    }
    for (ssize_t i = 0; i < n; ++i) {
        if (buffer[i] == '\n') return true;
        line.push_back(buffer[i]);
    }
}
```

同时限制响应最大长度：

```cpp
if (line.size() > 65536) {
    error = "response line too long";
    return false;
}
```

这是为了避免服务端异常返回超大内容导致客户端内存不断增长。

### 5.7 关闭连接

收发完成后：

```cpp
close(sock);
```

当前协议是短连接，所以每次请求完成后都关闭 TCP 连接。这样做的优点是状态简单，服务端不需要维护会话；缺点是每次请求都有 TCP 建连开销。

对当前低频任务，短连接更合适。

## 6. 配置方式

当前客户端从文本配置文件读取服务器信息：

```text
remote_server 192.168.20.2 50051
remote_timeout_ms 60000
```

如果沿用当前代码中的配置项名称，则对应的是：

```text
vision_server <ip> <port>
vision_timeout_ms <milliseconds>
```

后续其他小题可以沿用同样格式，也可以改成更通用的命名：

```text
remote_server 192.168.20.2 50051
remote_timeout_ms 60000
```

建议：

- IP、端口、超时时间都放配置文件，不要写死在代码里。
- 端口按任务区分，避免多个服务抢同一端口。
- 服务端启动参数和客户端配置必须一致。

## 7. 测试方式

### 7.1 启动服务端

示例：

```bash
python3 tcp_server.py --host 0.0.0.0 --port 50051
```

技术上只要是一个能按本文协议收发 JSON Lines 的 Python TCP Server 即可。

### 7.2 检查端口监听

在个人服务器上：

```bash
ss -ltn | grep 50051
```

正常应看到类似：

```text
LISTEN ... 0.0.0.0:50051
```

### 7.3 检查网络连通

在工控机上：

```bash
ping 192.168.20.2
```

如果 ping 不通，先处理网络问题；socket 程序本身无法绕过网络不可达。

### 7.4 使用 nc 手工测试

```bash
printf '{"command":"run","payload":{}}\n' | nc 192.168.20.2 50051
```

只要服务端按协议工作，应返回一行 JSON。

### 7.5 使用项目 smoke test

项目里有 C++ 烟雾测试工具：

```bash
cd /home/duo/GPSR_ws_zyd/RoboControl/src/RoboXO-6Axis/RoboXORT
./tools/vision_server_smoke_test.cpp run 192.168.20.2 50051
```

这个工具重点测试：

- TCP socket 是否能连接。
- 请求是否能发送完整。
- 服务端是否返回以 `\n` 结束的一行响应。
- 超时逻辑是否生效。

它比运行完整工控任务更安全，适合先验证通信链路。

## 8. 常见错误

### 8.1 `Connection refused`

通常表示目标 IP 可达，但端口没有服务在监听。

检查：

- 服务端是否启动。
- 端口是否一致。
- 服务端是否监听 `0.0.0.0` 或正确网卡 IP。

### 8.2 `No route to host`

通常是网络不可达。

检查：

- 工控机和个人服务器是否在同一网络或路由可达。
- IP 是否写错。
- 防火墙是否拦截。

### 8.3 `recv failed` 或 `connection closed before newline`

通常是服务端提前断开，或者响应没有加 `\n`。

检查：

- 服务端是否抛异常退出。
- `sendall(json + "\n")` 是否保留。
- 客户端是否按行读取。

### 8.4 `response line too long`

说明服务端返回超过客户端限制的单行内容。

处理：

- JSON 只传关键结果。
- 不要把图片、大数组、日志全文放进 JSON。
- 大数据传输改用长度包头或文件传输方案。

### 8.5 客户端卡住

常见原因：

- 服务端没有返回换行。
- 超时没有正确设置。
- `connect()` 阶段等待系统默认超时。

建议：

- 保留 `SO_RCVTIMEO`、`SO_SNDTIMEO`。
- 对 `connect()` 使用非阻塞模式加 `select()`，参考项目中的 smoke test 工具。

## 9. 后续复用建议

如果其他小题也要让工控机和个人服务器通信，推荐固定以下规范：

1. 使用 TCP IPv4：`AF_INET + SOCK_STREAM`。
2. Python 服务端使用 `socketserver.ThreadingTCPServer`。
3. C++ 客户端使用 POSIX socket API。
4. 消息格式使用 JSON。
5. 每条消息以 `\n` 结束。
6. 每次请求新建连接，完成后关闭。
7. 服务端使用 `sendall()`。
8. 客户端使用 `send_all()` 和 `recv_line()`。
9. 两端都设置最大消息长度。
10. 响应统一包含 `ok` 字段，失败统一包含 `error` 字段。
11. IP、端口、超时时间写入配置文件。
12. 先用 `nc` 或 smoke test 验证通信，再接入正式任务。

## 10. 适用场景

适合：

- 工控机向个人服务器发起低频请求。
- 请求和响应数据量较小。
- 希望跨语言通信简单可读。
- 希望不引入 HTTP、gRPC 等额外框架。
- 任务可以接受一次请求一次连接的开销。

不适合：

- 高频实时控制闭环。
- 连续视频流传输。
- 大文件或大图片传输。
- 多客户端高并发业务服务。
- 需要复杂鉴权、加密、负载均衡的生产网络服务。

如果后续通信需求升级，可以考虑：

- 长连接 TCP：减少频繁建连开销。
- 长度包头协议：更适合二进制数据。
- HTTP REST：更适合通用服务接口。
- WebSocket：更适合双向持续通信。
- gRPC：更适合强类型、多接口、跨语言服务。

当前项目采用的方案胜在简单、直接、可调试：工控机发一行 JSON，个人服务器回一行 JSON，双方通过 TCP socket 完成可靠通信。
