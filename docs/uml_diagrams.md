# dds_abstract UML 图册

本文档对应 `dds_abstract` 1.0.0，图中只展示当前已实现的 ZeroMQ 和 Zenoh；
ROS DDS、CAN/CANopen 作为后续扩展点标识。

## 1. 总体架构图

```mermaid
flowchart TB
  subgraph Application[应用层]
    CppApp[C++ Application]
    PythonApp[Python Application]
  end

  subgraph PublicApi[公开接口层]
    PublicHeader[dds_node.hpp]
    Server[DdsServer]
    Client[DdsClient]
    PyBinding[pybind11 Python Binding]
  end

  subgraph Core[核心协调层]
    ServerImpl[DdsServer::Impl]
    ClientImpl[DdsClient::Impl]
    NodeCore[DdsNodeCore]
    AddressParser[DdsNodeAddress Parser]
    ThreadConfig[Thread Configuration]
    CallbackDispatcher[Callback Dispatcher]
  end

  subgraph Transport[传输抽象层]
    TransportInterface[DdsTransport]
    Factory[Transport Factory]
  end

  subgraph Backend[协议后端层]
    ZeroMq[ZeroMQ Transport]
    Zenoh[Zenoh Transport]
    Future[Future Transport<br/>ROS DDS / CAN]
  end

  CppApp --> PublicHeader
  PythonApp --> PyBinding
  PyBinding --> PublicHeader
  PublicHeader --> Server
  PublicHeader --> Client
  Server --> ServerImpl
  Client --> ClientImpl
  ServerImpl --> NodeCore
  ClientImpl --> NodeCore
  NodeCore --> AddressParser
  NodeCore --> ThreadConfig
  NodeCore --> CallbackDispatcher
  NodeCore --> Factory
  Factory --> TransportInterface
  TransportInterface --> ZeroMq
  TransportInterface --> Zenoh
  TransportInterface -. extension .-> Future
```

## 2. 模块框图

广播和请求通道分别解析地址、选择协议并持有独立传输实例，因此两个通道可以使用
不同协议。

```mermaid
flowchart LR
  subgraph Node[DdsServer 或 DdsClient]
    Config[Node Config]
    Core[DdsNodeCore]
    Worker[Worker Thread]
    BroadcastCallback[Broadcast Callback]
    RequestCallback[Request Callback]
    ErrorCallback[Error Callback]
  end

  subgraph BroadcastPath[广播通道]
    BroadcastAddress[broadcast_node_address]
    BroadcastParser[Address Parser]
    BroadcastFactory[Transport Factory]
    BroadcastTransport[独立 DdsTransport]
  end

  subgraph RequestPath[请求通道]
    RequestAddress[request_node_address]
    RequestParser[Address Parser]
    RequestFactory[Transport Factory]
    RequestTransport[独立 DdsTransport]
  end

  Config --> Core
  Core --> BroadcastAddress
  Core --> RequestAddress
  BroadcastAddress --> BroadcastParser
  BroadcastParser --> BroadcastFactory
  BroadcastFactory --> BroadcastTransport
  RequestAddress --> RequestParser
  RequestParser --> RequestFactory
  RequestFactory --> RequestTransport
  Worker --> Core
  Core --> BroadcastCallback
  Core --> RequestCallback
  Core --> ErrorCallback

  BroadcastTransport --> ZmqPubSub[ZeroMQ PUB/SUB]
  BroadcastTransport --> ZenohPubSub[Zenoh Publisher/Subscriber]
  RequestTransport --> ZmqRpc[ZeroMQ ROUTER/DEALER]
  RequestTransport --> ZenohRpc[Zenoh Queryable/Query]
```

## 3. 核心类图

```mermaid
classDiagram
  direction TB

  class DdsByteView {
    -const byte* data_
    -size_t size_
    +data() const byte*
    +size() size_t
    +begin() const byte*
    +end() const byte*
  }

  class DdsThreadConfig {
    +string name
    +DdsSchedulingPolicy policy
    +int priority
    +bool strict
  }

  class DdsServerConfig {
    +string broadcast_node_address
    +string request_node_address
    +DdsThreadConfig worker
  }

  class DdsClientConfig {
    +string broadcast_node_address
    +string request_node_address
    +DdsThreadConfig worker
  }

  class DdsServer {
    -unique_ptr~Impl~ impl_
    +OnRequest(handler)
    +OnError(handler)
    +SetWorkerThreadName(name)
    +SetWorkerThreadScheduling(policy, priority, strict)
    +Start()
    +Stop()
    +IsRunning() bool
    +Publish(message)
  }

  class DdsClient {
    -unique_ptr~Impl~ impl_
    +OnBroadcast(handler)
    +OnError(handler)
    +SetWorkerThreadName(name)
    +SetWorkerThreadScheduling(policy, priority, strict)
    +Start()
    +Stop()
    +IsRunning() bool
    +Request(message, timeout) DdsBytes
  }

  class DdsNodeCore {
    -EndpointRole role_
    -DdsThreadConfig worker_config_
    -optional~DdsNodeAddress~ broadcast_address_
    -optional~DdsNodeAddress~ request_address_
    -unique_ptr~DdsTransport~ broadcast_transport_
    -unique_ptr~DdsTransport~ request_transport_
    -thread worker_
    -atomic_bool stop_requested_
    +Start()
    +Stop()
    +Publish(message)
    +Request(message, timeout) DdsBytes
    -ReceiveLoop()
    -Dispatch(transport, message)
  }

  class DdsNodeAddress {
    +DdsTransportKind transport
    +string resource
    +Parse(value)$ DdsNodeAddress
    +ToString() string
  }

  class DdsIncomingMessage {
    +Kind kind
    +DdsBytes payload
    +string correlation_id
  }

  class DdsTransport {
    <<interface>>
    +Open(broadcast, request, is_server)*
    +Close()*
    +Publish(message)*
    +Request(message, timeout)* DdsBytes
    +TryReceive(stop, timeout, message)* bool
    +Reply(request, response)*
  }

  class DdsZeroMqTransport {
    -PUB_OR_SUB broadcast_socket_
    -ROUTER_OR_DEALER request_socket_
  }

  class DdsZenohTransport {
    -Session session_
    -Publisher_OR_Subscriber broadcast_entity_
    -Queryable request_entity_
  }

  DdsServerConfig *-- DdsThreadConfig
  DdsClientConfig *-- DdsThreadConfig
  DdsServer --> DdsServerConfig : constructs with
  DdsClient --> DdsClientConfig : constructs with
  DdsServer *-- DdsNodeCore : PImpl
  DdsClient *-- DdsNodeCore : PImpl
  DdsNodeCore *-- DdsNodeAddress : 0..2
  DdsNodeCore *-- DdsTransport : 1..2
  DdsTransport ..> DdsIncomingMessage
  DdsTransport ..> DdsByteView
  DdsZeroMqTransport ..|> DdsTransport
  DdsZenohTransport ..|> DdsTransport
```

## 4. 地址选择活动图

```mermaid
flowchart TD
  Start([输入 node address]) --> Empty{是否为空?}
  Empty -- 是 --> Optional{另一个通道是否已配置?}
  Optional -- 是 --> Disabled[禁用当前通道]
  Optional -- 否 --> InvalidBoth[抛出 invalid_argument]

  Empty -- 否 --> NativeInproc{以 inproc:// 开始?}
  NativeInproc -- 是 --> ZeroMq[选择 ZeroMQ]
  NativeInproc -- 否 --> ShortInproc{以 inproc/ 开始?}
  ShortInproc -- 是 --> Normalize[规范化为 inproc://]
  Normalize --> ZeroMq
  ShortInproc -- 否 --> Ipv4{是否为合法 IPv4:port?}
  Ipv4 -- 是 --> Tcp[补充 tcp://]
  Tcp --> ZeroMq
  Ipv4 -- 否 --> LooksInvalid{疑似错误的 ZeroMQ 地址?}
  LooksInvalid -- 是 --> InvalidAddress[抛出 invalid_argument]
  LooksInvalid -- 否 --> Zenoh[选择 Zenoh key expression]
```

## 5. 节点启动时序图

```mermaid
sequenceDiagram
  autonumber
  actor App
  participant Node as DdsServer / DdsClient
  participant Core as DdsNodeCore
  participant Broadcast as Broadcast Transport
  participant Request as Request Transport
  participant Worker as Worker Thread

  App->>Node: 注册消息和错误回调
  App->>Node: 配置线程名称、策略和优先级
  App->>Node: Start()
  Node->>Core: Start()
  opt 已配置广播地址
    Core->>Broadcast: Open(broadcast, null, role)
  end
  opt 已配置请求地址
    Core->>Request: Open(null, request, role)
  end
  Core->>Worker: 创建线程
  Worker->>Worker: ConfigureDdsCurrentThread()
  alt strict=true 且线程配置失败
    Worker-->>Core: ready future 异常
    Core->>Broadcast: Close()
    Core->>Request: Close()
    Core-->>App: Start() 抛出异常
  else 配置成功或非严格模式
    Worker-->>Core: ready
    Core-->>App: Start() 返回
    loop 节点运行期间
      Worker->>Core: ReceiveLoop()
    end
  end
```

## 6. 广播时序图

```mermaid
sequenceDiagram
  autonumber
  actor ServerApp
  participant Server as DdsServer
  participant Backend as ZeroMQ PUB / Zenoh Publisher
  participant Client1Backend as Client 1 Backend
  participant Client1Worker as Client 1 Worker
  actor Client1App
  participant ClientNBackend as Client N Backend
  participant ClientNWorker as Client N Worker
  actor ClientNApp

  ServerApp->>Server: Publish(message)
  Server->>Backend: Publish(DdsByteView)
  par client 1
    Backend-->>Client1Backend: broadcast payload
    Client1Backend-->>Client1Worker: TryReceive()
    Client1Worker->>Client1App: OnBroadcast(payload)
  and client N
    Backend-->>ClientNBackend: broadcast payload
    ClientNBackend-->>ClientNWorker: TryReceive()
    ClientNWorker->>ClientNApp: OnBroadcast(payload)
  end
```

## 7. 请求/应答时序图

```mermaid
sequenceDiagram
  autonumber
  actor ClientApp
  participant Client as DdsClient
  participant ClientTransport as Client Request Transport
  participant Protocol as ZeroMQ / Zenoh
  participant ServerTransport as Server Request Transport
  participant Worker as Server Worker Thread
  actor ServerApp

  ClientApp->>Client: Request(payload, timeout)
  Client->>ClientTransport: Request(payload, timeout)
  ClientTransport->>Protocol: 发送请求
  Protocol->>ServerTransport: 请求 + 关联信息
  ServerTransport-->>Worker: TryReceive(request)
  Worker->>ServerApp: OnRequest(payload)
  ServerApp-->>Worker: response
  Worker->>ServerTransport: Reply(correlation_id, response)
  ServerTransport->>Protocol: 发送关联应答
  Protocol-->>ClientTransport: response
  ClientTransport-->>Client: DdsBytes
  Client-->>ClientApp: response

  alt 超时前没有应答
    ClientTransport-->>Client: 抛出超时异常
    Client-->>ClientApp: 异常
  end
```

## 8. 错误处理时序图

```mermaid
sequenceDiagram
  participant Transport
  participant Worker as Worker Thread
  participant Core as DdsNodeCore
  actor App

  Worker->>Transport: TryReceive() / Dispatch()
  Transport--xWorker: 抛出异常
  Worker->>Core: Report(exception_ptr)
  alt 已注册 OnError
    Core->>App: OnError(exception_ptr)
    opt 错误回调再次抛出异常
      Core->>Core: 捕获并忽略，保护工作线程
    end
  else 未注册 OnError
    Core->>Core: 忽略异步错误
  end
```

## 9. 单进程部署图

ZeroMQ `inproc` 适用于同一进程内的组件通信。所有节点共享库内的 ZeroMQ context。

```mermaid
flowchart TB
  subgraph Host[Linux / Windows Host]
    subgraph Process[Application Process]
      Server[DdsServer]
      Client1[DdsClient 1]
      Client2[DdsClient 2]
      Client4[DdsClient 4]
      Context[(Shared ZeroMQ Context)]
      Broadcast[[inproc://events]]
      Rpc[[inproc://rpc]]

      Server --> Context
      Client1 --> Context
      Client2 --> Context
      Client4 --> Context
      Context --> Broadcast
      Context --> Rpc
    end
  end
```

## 10. 跨主机混合协议部署图

```mermaid
flowchart LR
  subgraph ServerHost[Server Host]
    ServerApp[Server Application]
    Server[DdsServer]
    ServerZmq[ZeroMQ PUB<br/>192.168.1.10:8080]
    ServerZenoh[Zenoh Queryable<br/>plant/rpc]
    ServerApp --> Server
    Server --> ServerZmq
    Server --> ServerZenoh
  end

  subgraph Infrastructure[Network Infrastructure]
    IpNetwork[(TCP/IP Network)]
    ZenohNetwork[(Zenoh Router / Peer Network)]
  end

  subgraph ClientHostA[Client Host A]
    ClientA[DdsClient]
    ClientAZmq[ZeroMQ SUB]
    ClientAZenoh[Zenoh Query]
    ClientA --> ClientAZmq
    ClientA --> ClientAZenoh
  end

  subgraph ClientHostB[Client Host B]
    PythonApp[Python Application]
    ClientB[DdsClient via pybind11]
    ClientBZmq[ZeroMQ SUB]
    ClientBZenoh[Zenoh Query]
    PythonApp --> ClientB
    ClientB --> ClientBZmq
    ClientB --> ClientBZenoh
  end

  ServerZmq --> IpNetwork
  IpNetwork --> ClientAZmq
  IpNetwork --> ClientBZmq
  ServerZenoh --> ZenohNetwork
  ClientAZenoh --> ZenohNetwork
  ClientBZenoh --> ZenohNetwork
```

该部署示例中：

- 广播地址为 `192.168.1.10:8080`，自动选择 ZeroMQ。
- 请求地址为 `plant/rpc`，自动选择 Zenoh。
- server 的 ZeroMQ 端执行 `bind`，client 执行 `connect`。
- Zenoh 节点通过默认 session 发现彼此；是否部署 router 取决于实际网络拓扑。
- C++ 和 Python 节点在协议层完全互通，消息均表现为二进制数据。

## 11. 构建与交付部署图

```mermaid
flowchart LR
  Source[dds_abstract Source]
  Conan[Conan 2 Recipe]
  CMake[CMake 3.16+]
  CppZmq[cppzmq / zeromq<br/>Conan dependency]
  Zenoh[zenoh-c / zenoh-cpp<br/>System dependency]
  Pybind[pybind11<br/>Optional Conan dependency]
  SharedLib[libdds_abstract.so.1.0.0<br/>or platform equivalent]
  Headers[dds_node.hpp]
  PythonModule[dds_abstract Python Package]
  Consumer[C++ / Python Consumer]

  Source --> Conan
  Conan --> CMake
  CppZmq --> CMake
  Zenoh --> CMake
  Pybind --> CMake
  CMake --> SharedLib
  CMake --> Headers
  CMake --> PythonModule
  SharedLib --> Consumer
  Headers --> Consumer
  PythonModule --> Consumer
```

## 12. 图例

| 表示 | 含义 |
|---|---|
| 实线箭头 | 当前实现中的直接依赖或调用 |
| 虚线箭头 | 规划中的扩展关系 |
| `broadcast_transport_` | 广播通道专用传输实例 |
| `request_transport_` | 请求通道专用传输实例 |
| `correlation_id` | 后端内部请求关联信息，不属于公开 API |
