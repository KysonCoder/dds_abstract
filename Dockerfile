FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive
ARG TZ=Etc/UTC

# Zenoh 仓库版本，固定 tag / commit 以保证构建可复现
ARG ZENOH_CPP_REF=1.10.1

ENV TZ=${TZ}
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8

# 替换为清华大学 (TUNA) 镜像源
RUN sed -i 's|http://archive.ubuntu.com/ubuntu/|http://mirrors.tuna.tsinghua.edu.cn/ubuntu/|g' /etc/apt/sources.list && \
    sed -i 's|http://security.ubuntu.com/ubuntu/|http://mirrors.tuna.tsinghua.edu.cn/ubuntu/|g' /etc/apt/sources.list

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    pkg-config \
    curl \
    ca-certificates \
    libzmq3-dev \
    clang \
    libclang-dev \
    python3 \
    python3-dev \
    python3-pip \
    gdb \
    vim \
    nano \
    sudo \
    && rm -rf /var/lib/apt/lists/*

# Conan 2 用于锁定和复现 C++ 依赖版本
RUN python3 -m pip install --no-cache-dir "conan==2.21.0"

# 安装 Rust / Cargo
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
    | sh -s -- -y --profile minimal --default-toolchain stable

ENV PATH="/root/.cargo/bin:${PATH}"

# 为 Cargo 配置国内镜像源 (rsproxy) 解决编译拉取依赖超时问题
RUN mkdir -p /root/.cargo && \
    echo "[source.crates-io]" > /root/.cargo/config.toml && \
    echo "replace-with = 'rsproxy-sparse'" >> /root/.cargo/config.toml && \
    echo "[source.rsproxy-sparse]" >> /root/.cargo/config.toml && \
    echo "registry = 'sparse+https://rsproxy.cn/index/'" >> /root/.cargo/config.toml && \
    echo "[net]" >> /root/.cargo/config.toml && \
    echo "git-fetch-with-cli = true" >> /root/.cargo/config.toml

# 拉取并构建 zenoh-cpp（包含 zenoh-c 子模块）
WORKDIR /opt/third_party

RUN git clone https://github.com/eclipse-zenoh/zenoh-cpp.git zenoh-cpp \
    && cd zenoh-cpp \
    && git checkout ${ZENOH_CPP_REF} \
    && git submodule update --init --recursive

# 先构建 zenoh-c
RUN cmake -S /opt/third_party/zenoh-cpp/zenoh-c \
    -B /opt/third_party/zenoh-c-build \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DBUILD_SHARED_LIBS=ON \
    -DZENOHC_BUILD_TESTS_WITH_CXX=OFF \
    -DZENOHC_BUILD_WITH_SHARED_MEMORY=OFF \
    -DZENOHC_BUILD_WITH_UNSTABLE_API=OFF \
    && cmake --build /opt/third_party/zenoh-c-build --parallel \
    && cmake --install /opt/third_party/zenoh-c-build

# 再构建 zenoh-cpp
RUN cmake -S /opt/third_party/zenoh-cpp \
    -B /opt/third_party/zenoh-cpp-build \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DZENOHCXX_ZENOHC=ON \
    -DZENOHCXX_ZENOHPICO=OFF \
    -DZENOHCXX_ENABLE_TESTS=OFF \
    -DZENOHCXX_ENABLE_EXAMPLES=OFF \
    && cmake --build /opt/third_party/zenoh-cpp-build --parallel \
    && cmake --install /opt/third_party/zenoh-cpp-build

# 让后续容器直接进入工作区
WORKDIR /workspace/dds_abstract

CMD ["sleep", "infinity"]