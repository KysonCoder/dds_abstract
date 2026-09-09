dds_abstract/
├── CMakeLists.txt                  # 主构建、依赖检测、测试和安装
├── Dockerfile                      # Ubuntu + ZeroMQ + Zenoh 构建环境
├── docker-compose.yml              # 开发容器编排
├── conanfile.py                    # Conan 2 包配方
├── include/dds_abstract/
│   └── dds_node.hpp                # 唯一公开 C++ API
├── src/
│   ├── dds_node.cpp                # 核心节点生命周期和消息分发
│   ├── dds_node_address.cpp        # 地址解析和后端选择
│   ├── dds_thread_config.cpp       # 工作线程名称和调度配置
│   ├── dds_zeromq_transport.cpp    # ZeroMQ 后端
│   ├── dds_zenoh_transport.cpp     # Zenoh 后端
│   └── internal/
│       ├── dds_node_address.hpp     # 内部地址类型
│       ├── dds_thread_config.hpp    # 内部线程接口
│       └── dds_transport.hpp        # 内部传输抽象接口
├── python/
│   ├── dds_abstract_pybind.cpp     # pybind11 绑定
│   └── dds_abstract/
│       └── __init__.py              # Python 包导出
├── tests/
│   ├── dds_node_test.cpp            # 基础功能和生命周期
│   ├── dds_zeromq_test.cpp          # ZeroMQ 测试
│   ├── dds_zenoh_test.cpp           # Zenoh 测试
│   ├── dds_mixed_protocol_test.cpp  # 混合协议测试
│   ├── dds_python_test.py           # Python 测试
│   └── dds_test_utils.hpp           # 测试辅助函数
├── test_package/                    # Conan 包消费测试
├── docs/                            # 架构、用户手册和 UML 文档
└── tools/
    └── generate_uml_documents.py    # UML 文档生成脚本