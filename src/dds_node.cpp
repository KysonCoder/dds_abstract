#include "dds_abstract/dds_node.hpp"

#include <atomic>
#include <future>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

#include "internal/dds_thread_config.hpp"
#include "internal/dds_transport.hpp"

namespace dds_abstract {
namespace {

enum class EndpointRole { kServer, kClient };

std::unique_ptr<DdsTransport> MakeTransport(DdsTransportKind kind) {
  if (kind == DdsTransportKind::kZeroMq) return MakeDdsZeroMqTransport();
  if (kind == DdsTransportKind::kZenoh) return MakeDdsZenohTransport();
  throw std::runtime_error("unsupported transport backend");
}

class DdsNodeCore {
 public:
  DdsNodeCore(EndpointRole role, std::string broadcast_address,
              std::string request_address, DdsThreadConfig worker_config)
      : role_(role), worker_config_(std::move(worker_config)) {
    if (broadcast_address.empty() && request_address.empty())
      throw std::invalid_argument(
          "broadcast and request node addresses cannot both be empty");
    if (!broadcast_address.empty()) {
      broadcast_address_ = DdsNodeAddress::Parse(broadcast_address);
      broadcast_transport_ = MakeTransport(broadcast_address_->transport);
    }
    if (!request_address.empty()) {
      request_address_ = DdsNodeAddress::Parse(request_address);
      request_transport_ = MakeTransport(request_address_->transport);
    }
  }

  ~DdsNodeCore() { Stop(); }

  void OnBroadcast(DdsBroadcastHandler handler) {
    std::lock_guard lock(callback_mutex_);
    broadcast_handler_ = std::move(handler);
  }

  void OnRequest(DdsRequestHandler handler) {
    std::lock_guard lock(callback_mutex_);
    request_handler_ = std::move(handler);
  }

  void OnError(DdsErrorHandler handler) {
    std::lock_guard lock(callback_mutex_);
    error_handler_ = std::move(handler);
  }

  void SetWorkerThreadName(std::string name) {
    EnsureNotRunning();
    worker_config_.name = std::move(name);
  }

  void SetWorkerThreadScheduling(DdsSchedulingPolicy policy, int priority,
                                 bool strict) {
    EnsureNotRunning();
    worker_config_.policy = policy;
    worker_config_.priority = priority;
    worker_config_.strict = strict;
  }

  void Start() {
    EnsureNotRunning();
    const bool is_server = role_ == EndpointRole::kServer;
    try {
      if (broadcast_transport_)
        broadcast_transport_->Open(&*broadcast_address_, nullptr, is_server);
      if (request_transport_)
        request_transport_->Open(nullptr, &*request_address_, is_server);
    } catch (...) {
      CloseTransports();
      throw;
    }

    stop_requested_ = false;
    std::promise<void> ready;
    auto ready_result = ready.get_future();
    worker_ = std::thread([this, ready = std::move(ready)]() mutable {
      try {
        ConfigureDdsCurrentThread(worker_config_);
        ready.set_value();
      } catch (...) {
        ready.set_exception(std::current_exception());
        return;
      }
      ReceiveLoop();
    });
    try {
      ready_result.get();
    } catch (...) {
      Stop();
      throw;
    }
  }

  void Stop() noexcept {
    if (!worker_.joinable()) return;
    stop_requested_ = true;
    CloseTransports();
    worker_.join();
  }

  [[nodiscard]] bool IsRunning() const noexcept { return worker_.joinable(); }

  void Publish(DdsByteView message) {
    if (!IsRunning()) throw std::logic_error("server is not running");
    if (!broadcast_transport_)
      throw std::logic_error("broadcast node address is not configured");
    broadcast_transport_->Publish(message);
  }

  DdsBytes Request(DdsByteView message, std::chrono::milliseconds timeout) {
    if (!IsRunning()) throw std::logic_error("client is not running");
    if (!request_transport_)
      throw std::logic_error("request node address is not configured");
    return request_transport_->Request(message, timeout);
  }

 private:
  void EnsureNotRunning() const {
    if (IsRunning())
      throw std::logic_error("operation is not allowed after Start");
  }

  void CloseTransports() noexcept {
    if (broadcast_transport_) broadcast_transport_->Close();
    if (request_transport_) request_transport_->Close();
  }

  void ReceiveLoop() {
    while (!stop_requested_.load()) {
      try {
        DdsTransport* transport = role_ == EndpointRole::kServer
                                      ? request_transport_.get()
                                      : broadcast_transport_.get();
        if (transport == nullptr) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }
        DdsIncomingMessage incoming;
        if (!transport->TryReceive(stop_requested_,
                                   std::chrono::milliseconds(10), &incoming))
          continue;
        Dispatch(*transport, incoming);
      } catch (...) {
        if (!stop_requested_.load()) Report(std::current_exception());
      }
    }
  }

  void Dispatch(DdsTransport& transport, const DdsIncomingMessage& incoming) {
    if (incoming.kind == DdsIncomingMessage::Kind::kBroadcast) {
      DdsBroadcastHandler handler;
      {
        std::lock_guard lock(callback_mutex_);
        handler = broadcast_handler_;
      }
      if (handler) handler(incoming.payload);
      return;
    }
    DdsRequestHandler handler;
    {
      std::lock_guard lock(callback_mutex_);
      handler = request_handler_;
    }
    DdsBytes response;
    if (handler) response = handler(incoming.payload);
    transport.Reply(incoming, response);
  }

  void Report(std::exception_ptr error) noexcept {
    DdsErrorHandler handler;
    {
      std::lock_guard lock(callback_mutex_);
      handler = error_handler_;
    }
    if (!handler) return;
    try {
      handler(error);
    } catch (...) {
    }
  }

  EndpointRole role_;
  DdsThreadConfig worker_config_;
  std::optional<DdsNodeAddress> broadcast_address_;
  std::optional<DdsNodeAddress> request_address_;
  std::unique_ptr<DdsTransport> broadcast_transport_;
  std::unique_ptr<DdsTransport> request_transport_;
  std::mutex callback_mutex_;
  DdsBroadcastHandler broadcast_handler_;
  DdsRequestHandler request_handler_;
  DdsErrorHandler error_handler_;
  std::thread worker_;
  std::atomic_bool stop_requested_{false};
};

}  // namespace

struct DdsServer::Impl {
  explicit Impl(DdsServerConfig config)
      : core(EndpointRole::kServer, std::move(config.broadcast_node_address),
             std::move(config.request_node_address), std::move(config.worker)) {
  }
  DdsNodeCore core;
};

struct DdsClient::Impl {
  explicit Impl(DdsClientConfig config)
      : core(EndpointRole::kClient, std::move(config.broadcast_node_address),
             std::move(config.request_node_address), std::move(config.worker)) {
  }
  DdsNodeCore core;
};

DdsServer::DdsServer(DdsServerConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
DdsServer::~DdsServer() = default;
void DdsServer::OnRequest(DdsRequestHandler handler) {
  impl_->core.OnRequest(std::move(handler));
}
void DdsServer::OnError(DdsErrorHandler handler) {
  impl_->core.OnError(std::move(handler));
}
void DdsServer::SetWorkerThreadName(std::string name) {
  impl_->core.SetWorkerThreadName(std::move(name));
}
void DdsServer::SetWorkerThreadScheduling(DdsSchedulingPolicy policy,
                                          int priority, bool strict) {
  impl_->core.SetWorkerThreadScheduling(policy, priority, strict);
}
void DdsServer::Start() { impl_->core.Start(); }
void DdsServer::Stop() noexcept { impl_->core.Stop(); }
bool DdsServer::IsRunning() const noexcept { return impl_->core.IsRunning(); }
void DdsServer::Publish(DdsByteView message) { impl_->core.Publish(message); }

DdsClient::DdsClient(DdsClientConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
DdsClient::~DdsClient() = default;
void DdsClient::OnBroadcast(DdsBroadcastHandler handler) {
  impl_->core.OnBroadcast(std::move(handler));
}
void DdsClient::OnError(DdsErrorHandler handler) {
  impl_->core.OnError(std::move(handler));
}
void DdsClient::SetWorkerThreadName(std::string name) {
  impl_->core.SetWorkerThreadName(std::move(name));
}
void DdsClient::SetWorkerThreadScheduling(DdsSchedulingPolicy policy,
                                          int priority, bool strict) {
  impl_->core.SetWorkerThreadScheduling(policy, priority, strict);
}
void DdsClient::Start() { impl_->core.Start(); }
void DdsClient::Stop() noexcept { impl_->core.Stop(); }
bool DdsClient::IsRunning() const noexcept { return impl_->core.IsRunning(); }
DdsBytes DdsClient::Request(DdsByteView message,
                            std::chrono::milliseconds timeout) {
  return impl_->core.Request(message, timeout);
}

}  // namespace dds_abstract
