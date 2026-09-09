#ifndef DDS_ABSTRACT_INCLUDE_DDS_ABSTRACT_DDS_NODE_HPP_
#define DDS_ABSTRACT_INCLUDE_DDS_ABSTRACT_DDS_NODE_HPP_

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace dds_abstract {

using DdsBytes = std::vector<std::byte>;

class DdsByteView {
 public:
  DdsByteView() = default;
  DdsByteView(const DdsBytes& bytes)  // NOLINT(runtime/explicit)
      : data_(bytes.empty() ? &kEmptyByte : bytes.data()),
        size_(bytes.size()) {}
  DdsByteView(const std::byte* data, std::size_t size)
      : data_(data == nullptr ? &kEmptyByte : data), size_(size) {}

  [[nodiscard]] const std::byte* data() const { return data_; }
  [[nodiscard]] std::size_t size() const { return size_; }
  [[nodiscard]] const std::byte* begin() const { return data_; }
  [[nodiscard]] const std::byte* end() const { return data_ + size_; }
  [[nodiscard]] const std::byte& operator[](std::size_t index) const {
    return data_[index];
  }

 private:
  inline static constexpr std::byte kEmptyByte{0};
  const std::byte* data_ = &kEmptyByte;
  std::size_t size_ = 0;
};

enum class DdsSchedulingPolicy { kNormal, kFifo, kRoundRobin };

struct DdsThreadConfig {
  std::string name{"dds-worker"};
  DdsSchedulingPolicy policy{DdsSchedulingPolicy::kNormal};
  int priority{0};
  bool strict{false};
};

struct DdsServerConfig {
  std::string broadcast_node_address;
  std::string request_node_address;
  DdsThreadConfig worker{};
};

struct DdsClientConfig {
  std::string broadcast_node_address;
  std::string request_node_address;
  DdsThreadConfig worker{};
};

using DdsBroadcastHandler = std::function<void(DdsByteView)>;
using DdsRequestHandler = std::function<DdsBytes(DdsByteView)>;
using DdsErrorHandler = std::function<void(std::exception_ptr)>;

class DdsServer final {
 public:
  explicit DdsServer(DdsServerConfig config);
  ~DdsServer();
  DdsServer(const DdsServer&) = delete;
  DdsServer& operator=(const DdsServer&) = delete;

  void OnRequest(DdsRequestHandler handler);
  void OnError(DdsErrorHandler handler);
  void SetWorkerThreadName(std::string name);
  void SetWorkerThreadScheduling(DdsSchedulingPolicy policy, int priority,
                                 bool strict = false);
  void Start();
  void Stop() noexcept;
  [[nodiscard]] bool IsRunning() const noexcept;
  void Publish(DdsByteView message);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class DdsClient final {
 public:
  explicit DdsClient(DdsClientConfig config);
  ~DdsClient();
  DdsClient(const DdsClient&) = delete;
  DdsClient& operator=(const DdsClient&) = delete;

  void OnBroadcast(DdsBroadcastHandler handler);
  void OnError(DdsErrorHandler handler);
  void SetWorkerThreadName(std::string name);
  void SetWorkerThreadScheduling(DdsSchedulingPolicy policy, int priority,
                                 bool strict = false);
  void Start();
  void Stop() noexcept;
  [[nodiscard]] bool IsRunning() const noexcept;
  [[nodiscard]] DdsBytes Request(
      DdsByteView message,
      std::chrono::milliseconds timeout = std::chrono::seconds(1));

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace dds_abstract

#endif  // DDS_ABSTRACT_INCLUDE_DDS_ABSTRACT_DDS_NODE_HPP_
