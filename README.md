# dds_abstract

完整文档：

- [架构设计文档](docs/architecture_design.md)
- [使用说明（C++ 与 Python）](docs/user_guide.md)
- [UML 架构图、框图、时序图与部署图](docs/uml_diagrams.md)
- [UML Word 文档](docs/dds_abstract_UML架构与交互设计.docx)
- [UML PDF 文档](docs/dds_abstract_UML架构与交互设计.pdf)
- [UML HTML 文档](docs/dds_abstract_UML架构与交互设计.html)

`dds_abstract` 是 C++17 通信抽象库，当前支持 ZeroMQ 和 Zenoh。公开 API 不暴露具体协议类型，并分别提供 `DdsServer` 与 `DdsClient`。

## Node address

协议根据 node address 内容自动识别，不使用协议前缀：

| Node address | 后端 |
|---|---|
| `192.168.0.1:8080` | ZeroMQ TCP |
| `inproc://events` 或 `inproc/events` | ZeroMQ inproc |
| `dpm/param/events` | Zenoh key expression |

广播地址和请求地址相互独立，可以使用不同协议，也可以有一个为空，但不能同时为空。

## C++ API

```cpp
#include <dds_abstract/dds_node.hpp>

using namespace dds_abstract;

DdsServer server({"127.0.0.1:8080", "dpm/param/query"});
server.OnRequest([](DdsByteView request) -> DdsBytes {
  return {};
});
server.SetWorkerThreadName("dds-server");
server.SetWorkerThreadScheduling(DdsSchedulingPolicy::kFifo, 20, true);
server.Start();
server.Publish(message);

DdsClient client({"127.0.0.1:8080", "dpm/param/query"});
client.OnBroadcast([](DdsByteView event) {});
client.Start();
DdsBytes answer = client.Request(question, std::chrono::milliseconds(10));
```

`DdsByteView` 只在回调期间有效。需要保存消息时复制为 `DdsBytes`。

## Python API

构建时找到 pybind11 后自动生成 `_dds_abstract` 模块：

```python
from dds_abstract import Client, Server

server = Server("inproc://events", "inproc://rpc")
server.on_request(lambda request: b"reply:" + request)
server.start()

client = Client("inproc://events", "inproc://rpc")
client.on_broadcast(lambda event: print(event))
client.start()
answer = client.request(b"hello", timeout_ms=1000)
```

## 构建、测试与安装

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /desired/prefix
```

### Conan 2

工程提供 `conanfile.py`，库包引用为 `dds_abstract/1.0.0`。默认生成共享库，
启用 ZeroMQ 和 Zenoh，关闭 Python 绑定：

```sh
conan profile detect --force
conan create . --build=missing
```

常用选项：

```sh
conan create . --build=missing \
  -o dds_abstract/*:shared=True \
  -o dds_abstract/*:with_zeromq=True \
  -o dds_abstract/*:with_zenoh=True \
  -o dds_abstract/*:python_bindings=False
```

ZeroMQ 依赖由 ConanCenter 的 `cppzmq/4.11.0`（及其 `zeromq/4.3.5`
依赖）管理。Zenoh 当前没有采用 ConanCenter 配方，因此启用 Zenoh 时，构建环境仍需
预先提供 `zenoh-c` 和 `zenoh-cpp` 的 CMake package；缺少任何已启用后端时，Conan
构建会直接报错，不会静默生成缺少协议能力的库。

共享库版本为 `1.0.0`，ABI 主版本为 `1`：

```text
libdds_abstract.so.1.0.0
libdds_abstract.so.1
libdds_abstract.so
```

安装后可通过 CMake 使用：

```cmake
find_package(dds_abstract 1 CONFIG REQUIRED)
target_link_libraries(application PRIVATE dds::dds_abstract)
```

## 头文件与扩展边界

- 唯一公开头文件：`include/dds_abstract/dds_node.hpp`。
- `src/internal/` 中的 node address 解析、传输接口和线程辅助代码均为内部实现。
- 新后端通过内部 `DdsTransport` 接口和工厂加入，不改变 `DdsServer`、`DdsClient` API。
- 架构可扩展到 ROS DDS、CAN/CANopen 或其他后端；当前版本不实现这些协议。

## 实时线程

- Linux 使用 `pthread_setname_np`、`SCHED_FIFO` 或 `SCHED_RR`；实时调度通常需要 `CAP_SYS_NICE`。
- Windows 使用 `SetThreadDescription` 和 `SetThreadPriority`。
- `strict=true` 时线程配置失败会使 `Start()` 抛异常。

## 编码规范

代码采用 Google C++ 风格：

```sh
clang-format -i include/dds_abstract/*.hpp src/internal/*.hpp src/*.cpp \
  tests/*.cpp tests/*.hpp python/*.cpp
```
