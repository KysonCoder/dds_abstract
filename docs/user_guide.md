# dds_abstract 使用说明

## 1. 功能概览

`dds_abstract` 提供两种通信能力：

- 广播：`DdsServer` 发布，多个 `DdsClient` 订阅。
- 请求/应答：多个 `DdsClient` 发起请求，`DdsServer` 通过回调生成应答。

广播地址和请求地址可以分别使用 ZeroMQ 或 Zenoh，也可以只配置其中一个，但不能
同时为空。

## 2. Node address 规则

不要添加 `zeromq://` 或 `zenoh://` 前缀，协议由字符串自动判断。

| 配置示例 | 识别结果 | 适用范围 |
|---|---|---|
| `127.0.0.1:8080` | ZeroMQ TCP | 同机或跨主机 |
| `192.168.1.20:9000` | ZeroMQ TCP | 网络通信 |
| `inproc://events` | ZeroMQ inproc | 仅同一进程 |
| `inproc/events` | ZeroMQ inproc | 仅同一进程 |
| `dpm/param/events` | Zenoh key expression | Zenoh 网络 |

注意：

- ZeroMQ TCP 当前只接受 `IPv4:端口`。
- server 和 client 的对应通道必须配置相同地址。
- `inproc` 不能用于两个独立进程。
- 广播通道建立需要时间，启动后立即发布的首条 ZeroMQ 消息可能发生 slow joiner
  丢失；需要启动确认时使用请求通道。

## 3. 构建依赖

基础要求：

- 支持 C++17 的编译器
- CMake 3.16 或更高版本
- ZeroMQ 后端：libzmq、cppzmq
- Zenoh 后端：zenoh-c、zenoh-cpp
- Python API：Python 开发文件、pybind11

## 4. 使用 CMake 构建

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DDDS_ABSTRACT_WITH_ZEROMQ=ON \
  -DDDS_ABSTRACT_WITH_ZENOH=ON \
  -DDDS_ABSTRACT_BUILD_PYTHON=ON
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /opt/dds_abstract
```

主要选项：

| CMake 选项 | 默认值 | 说明 |
|---|---:|---|
| `BUILD_SHARED_LIBS` | `ON` | 生成共享库；设为 `OFF` 生成静态库 |
| `DDS_ABSTRACT_BUILD_TESTS` | `ON` | 构建自动测试 |
| `DDS_ABSTRACT_WITH_ZEROMQ` | `ON` | 启用 ZeroMQ 后端 |
| `DDS_ABSTRACT_WITH_ZENOH` | `ON` | 启用 Zenoh 后端 |
| `DDS_ABSTRACT_BUILD_PYTHON` | `ON` | 找到 pybind11 时构建 Python 模块 |
| `DDS_ABSTRACT_REQUIRE_BACKENDS` | `OFF` | 已启用后端缺失时令配置失败 |

## 5. 使用 Conan 2 构建和打包

首次使用时创建 profile：

```bash
conan profile detect --force
```

创建默认共享库包：

```bash
conan create . --build=missing
```

生成带 Python 绑定的包：

```bash
conan create . --build=missing \
  -o dds_abstract/*:python_bindings=True
```

生成静态库或裁剪后端：

```bash
conan create . --build=missing \
  -o dds_abstract/*:shared=False \
  -o dds_abstract/*:with_zeromq=True \
  -o dds_abstract/*:with_zenoh=False
```

Conan 默认包引用为：

```text
dds_abstract/1.0.0
```

ZeroMQ 及 cppzmq 由 ConanCenter 下载。启用 Zenoh 时，系统仍需预装 `zenoh-c` 和
`zenoh-cpp`，且二者必须提供 CMake package。

## 6. 在其他 CMake 工程中使用

安装库或通过 Conan 生成依赖文件后：

```cmake
cmake_minimum_required(VERSION 3.16)
project(example LANGUAGES CXX)

find_package(dds_abstract 1 CONFIG REQUIRED)

add_executable(example main.cpp)
target_compile_features(example PRIVATE cxx_std_17)
target_link_libraries(example PRIVATE dds::dds_abstract)
```

如果消费工程也由 Conan 管理，其 `conanfile.py` 可以声明：

```python
from conan import ConanFile
from conan.tools.cmake import cmake_layout


class ApplicationConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("dds_abstract/1.0.0")

    def layout(self):
        cmake_layout(self)
```

## 7. C++ 快速开始

### 7.1 二进制数据辅助函数

API 使用 `std::byte`。文本示例可以使用以下转换函数：

```cpp
#include <cstddef>
#include <string>

#include <dds_abstract/dds_node.hpp>

dds_abstract::DdsBytes ToBytes(const std::string& text) {
  const auto* begin = reinterpret_cast<const std::byte*>(text.data());
  return {begin, begin + text.size()};
}

std::string ToString(dds_abstract::DdsByteView bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
```

### 7.2 ZeroMQ server

```cpp
#include <chrono>
#include <iostream>
#include <thread>

#include <dds_abstract/dds_node.hpp>

int main() {
  using namespace dds_abstract;

  DdsServerConfig config;
  config.broadcast_node_address = "127.0.0.1:8080";
  config.request_node_address = "127.0.0.1:8081";
  config.worker.name = "dds-server";

  DdsServer server(std::move(config));
  server.OnRequest([](DdsByteView request) {
    return ToBytes("reply:" + ToString(request));
  });
  server.OnError([](std::exception_ptr error) {
    try {
      if (error) std::rethrow_exception(error);
    } catch (const std::exception& exception) {
      std::cerr << exception.what() << '\n';
    }
  });

  server.Start();
  server.Publish(ToBytes("server-ready"));
  std::this_thread::sleep_for(std::chrono::seconds(60));
  server.Stop();
}
```

`ToBytes()` 和 `ToString()` 使用上一节的辅助函数。

### 7.3 多个 ZeroMQ client

```cpp
#include <chrono>
#include <iostream>
#include <memory>
#include <vector>

#include <dds_abstract/dds_node.hpp>

int main() {
  using namespace dds_abstract;
  using namespace std::chrono_literals;

  std::vector<std::unique_ptr<DdsClient>> clients;
  for (int index = 0; index < 4; ++index) {
    auto client = std::make_unique<DdsClient>(
        DdsClientConfig{"127.0.0.1:8080", "127.0.0.1:8081"});
    client->OnBroadcast([index](DdsByteView message) {
      std::cout << "client " << index << ": " << ToString(message) << '\n';
    });
    client->Start();
    clients.push_back(std::move(client));
  }

  for (int index = 0; index < 4; ++index) {
    const auto reply = clients[index]->Request(ToBytes("hello"), 1s);
    std::cout << ToString(reply) << '\n';
  }
}
```

### 7.4 Zenoh

C++ API 不变，只需把地址换成 Zenoh key：

```cpp
DdsServer server({"plant/events", "plant/rpc"});
DdsClient client({"plant/events", "plant/rpc"});
```

### 7.5 混合协议

两个通道会独立选择后端：

```cpp
// 广播使用 ZeroMQ，请求/应答使用 Zenoh。
DdsServer server({"127.0.0.1:8080", "plant/rpc"});
DdsClient client({"127.0.0.1:8080", "plant/rpc"});
```

### 7.6 只启用一个通道

```cpp
// 仅广播。
DdsServer publisher({"127.0.0.1:8080", ""});
DdsClient subscriber({"127.0.0.1:8080", ""});

// 仅请求/应答。
DdsServer responder({"", "plant/rpc"});
DdsClient requester({"", "plant/rpc"});
```

两个地址同时为空会在构造时抛出 `std::invalid_argument`。对未配置的通道调用
`Publish()` 或 `Request()` 会抛出 `std::logic_error`。

## 8. C++ API 参考

### 8.1 DdsServer

| 方法 | 说明 |
|---|---|
| `OnRequest(handler)` | 注册请求处理器，返回值作为应答 |
| `OnError(handler)` | 注册工作线程错误处理器 |
| `SetWorkerThreadName(name)` | 启动前设置线程名 |
| `SetWorkerThreadScheduling(policy, priority, strict)` | 启动前设置调度策略 |
| `Start()` | 打开通道并启动工作线程 |
| `Stop()` | 停止并等待工作线程退出 |
| `IsRunning()` | 查询运行状态 |
| `Publish(message)` | 在广播通道发布二进制消息 |

### 8.2 DdsClient

| 方法 | 说明 |
|---|---|
| `OnBroadcast(handler)` | 注册广播处理器 |
| `OnError(handler)` | 注册工作线程错误处理器 |
| `SetWorkerThreadName(name)` | 启动前设置线程名 |
| `SetWorkerThreadScheduling(policy, priority, strict)` | 启动前设置调度策略 |
| `Start()` | 打开通道并启动工作线程 |
| `Stop()` | 停止并等待工作线程退出 |
| `IsRunning()` | 查询运行状态 |
| `Request(message, timeout)` | 同步发送请求并等待应答 |

`DdsByteView` 不持有消息内存。若要在回调返回后保存数据，应复制：

```cpp
client.OnBroadcast([](DdsByteView view) {
  DdsBytes owned(view.begin(), view.end());
  // 保存或转移 owned。
});
```

## 9. 实时线程配置

配置结构体方式：

```cpp
DdsServerConfig config;
config.broadcast_node_address = "127.0.0.1:8080";
config.worker.name = "dds-publisher";
config.worker.policy = DdsSchedulingPolicy::kFifo;
config.worker.priority = 20;
config.worker.strict = true;
DdsServer server(std::move(config));
```

方法方式：

```cpp
server.SetWorkerThreadName("dds-publisher");
server.SetWorkerThreadScheduling(DdsSchedulingPolicy::kFifo, 20, true);
server.Start();
```

Linux 使用 `SCHED_FIFO` 或 `SCHED_RR` 时通常需要实时调度权限。例如可按部署安全
策略给最终可执行文件配置 `CAP_SYS_NICE`。`strict=true` 时配置失败会令 `Start()`
抛出异常；`strict=false` 时节点继续运行。

线程名称和调度配置必须在 `Start()` 前完成。

## 10. Python 安装与导入

### 10.1 CMake 安装

启用 Python 绑定并安装：

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DDDS_ABSTRACT_BUILD_PYTHON=ON
cmake --build build
cmake --install build --prefix /opt/dds_abstract
```

模块默认安装在：

```text
/opt/dds_abstract/lib/python/dds_abstract
```

将父目录加入 `PYTHONPATH`：

```bash
export PYTHONPATH=/opt/dds_abstract/lib/python:$PYTHONPATH
python3 -c "import dds_abstract; print(dds_abstract.__version__)"
```

在未安装的构建目录中，扩展模块名是 `_dds_abstract`；安装后推荐始终使用包名
`dds_abstract`。

### 10.2 Python server

```python
import time

from dds_abstract import SchedulingPolicy, Server


server = Server("127.0.0.1:8080", "plant/rpc")
server.on_request(lambda request: b"reply:" + request)
server.on_error(lambda message: print(f"server error: {message}"))
server.set_worker_thread_name("dds-py-server")

# Python 也支持实时策略，但 Python 回调受 GIL 和解释器影响，不属于硬实时执行。
server.set_worker_thread_scheduling(
    SchedulingPolicy.FIFO, priority=20, strict=False
)

server.start()
try:
    server.publish(b"server-ready")
    time.sleep(60)
finally:
    server.stop()
```

### 10.3 Python 多 client

```python
import threading

from dds_abstract import Client


events = [threading.Event() for _ in range(4)]
clients = []

for index in range(4):
    client = Client("127.0.0.1:8080", "plant/rpc")

    def on_broadcast(message: bytes, client_index=index) -> None:
        print(client_index, message)
        events[client_index].set()

    client.on_broadcast(on_broadcast)
    client.on_error(lambda message: print(f"client error: {message}"))
    client.start()
    clients.append(client)

try:
    for index, client in enumerate(clients):
        reply = client.request(f"hello-{index}".encode(), timeout_ms=2000)
        print(reply)
finally:
    for client in reversed(clients):
        client.stop()
```

### 10.4 Python API

| 类型/方法 | Python 形式 |
|---|---|
| server 构造 | `Server(broadcast_node_address="", request_node_address="")` |
| client 构造 | `Client(broadcast_node_address="", request_node_address="")` |
| 请求回调 | `server.on_request(callback)`，回调接收并返回 `bytes` |
| 广播回调 | `client.on_broadcast(callback)`，回调接收 `bytes` |
| 错误回调 | `node.on_error(callback)`，回调接收错误字符串 |
| 发布 | `server.publish(message: bytes)` |
| 请求 | `client.request(message: bytes, timeout_ms=1000) -> bytes` |
| 调度策略 | `SchedulingPolicy.NORMAL/FIFO/ROUND_ROBIN` |
| 版本 | `dds_abstract.__version__` |

Python 请求处理回调必须返回 `bytes`。阻塞的 `request()` 和 `stop()` 在 C++ 层会释放
GIL，广播和请求回调进入 Python 前会重新获取 GIL。

## 11. 错误处理

同步 API 的配置错误、状态错误和请求超时通过异常报告：

```cpp
try {
  auto response = client.Request(request, std::chrono::milliseconds(100));
} catch (const std::exception& error) {
  // 记录超时或传输错误。
}
```

Python：

```python
try:
    response = client.request(b"status", timeout_ms=100)
except RuntimeError as error:
    print(error)
```

接收工作线程中的异常通过 `OnError()` / `on_error()` 报告。建议在启动节点前注册错误
回调。

## 12. 测试

运行全部 C++ 和 Python 测试：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

专项测试包含：

- ZeroMQ：1 个 server、4 个 client。
- Zenoh：1 个 server、4 个 client。
- 广播和请求地址采用不同协议。
- Python 广播和请求/应答。
- Conan `test_package` 独立消费安装后的库。

## 13. 使用建议

- 构造、注册回调、配置线程，然后调用 `Start()`。
- 停止时显式调用 `Stop()`，并保证节点对象比回调使用的外部对象活得更久。
- 回调应短小、非阻塞；复杂处理转交给业务队列。
- 为每个请求设置符合业务上限的超时时间。
- 不要依赖广播完成可靠握手；重要命令和确认使用请求/应答。
- 跨进程使用 IPv4 或 Zenoh key，不使用 ZeroMQ inproc。
- 硬实时场景优先使用 C++ API，并对整个系统进行延迟和抖动测量。
