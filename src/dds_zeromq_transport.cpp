#include <atomic>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include "internal/dds_transport.hpp"

#ifdef DDS_ABSTRACT_HAS_ZEROMQ
#include <zmq.hpp>
#endif

namespace dds_abstract {

#ifdef DDS_ABSTRACT_HAS_ZEROMQ
namespace {

zmq::context_t& GetZeroMqContext() {
  static zmq::context_t context(1);
  return context;
}

std::string ZeroMqEndpoint(const DdsNodeAddress& address) {
  constexpr std::string_view kNativeInprocPrefix = "inproc://";
  if (address.resource.compare(0, kNativeInprocPrefix.size(),
                               kNativeInprocPrefix) == 0)
    return address.resource;
  constexpr std::string_view kInprocPrefix = "inproc/";
  if (address.resource.compare(0, kInprocPrefix.size(), kInprocPrefix) == 0)
    return "inproc://" + address.resource.substr(kInprocPrefix.size());
  return "tcp://" + address.resource;
}

DdsBytes CopyMessage(const zmq::message_t& message) {
  const auto* first = static_cast<const std::byte*>(message.data());
  return DdsBytes(first, first + message.size());
}

class DdsZeroMqTransport final : public DdsTransport {
 public:
  DdsZeroMqTransport() : context_(GetZeroMqContext()) {}

  void Open(const DdsNodeAddress* broadcast,
            const DdsNodeAddress* request_address, bool is_server) override {
    if (is_open_.exchange(true))
      throw std::logic_error("ZeroMQ transport is already open");
    broadcast_socket_.reset();
    request_socket_.reset();
    is_server_ = is_server;
    try {
      if (is_server) {
        if (broadcast != nullptr) {
          broadcast_socket_ =
              std::make_unique<zmq::socket_t>(context_, zmq::socket_type::pub);
          broadcast_socket_->set(zmq::sockopt::linger, 0);
          broadcast_socket_->bind(ZeroMqEndpoint(*broadcast));
        }
        if (request_address != nullptr) {
          request_socket_ = std::make_unique<zmq::socket_t>(
              context_, zmq::socket_type::router);
          request_socket_->set(zmq::sockopt::linger, 0);
          request_socket_->bind(ZeroMqEndpoint(*request_address));
        }
      } else {
        if (broadcast != nullptr) {
          broadcast_socket_ =
              std::make_unique<zmq::socket_t>(context_, zmq::socket_type::sub);
          broadcast_socket_->set(zmq::sockopt::linger, 0);
          broadcast_socket_->set(zmq::sockopt::subscribe, "");
          broadcast_socket_->connect(ZeroMqEndpoint(*broadcast));
        }
        if (request_address != nullptr) {
          request_socket_ = std::make_unique<zmq::socket_t>(
              context_, zmq::socket_type::dealer);
          request_socket_->set(zmq::sockopt::linger, 0);
          request_socket_->connect(ZeroMqEndpoint(*request_address));
        }
      }
    } catch (...) {
      is_open_ = false;
      broadcast_socket_.reset();
      request_socket_.reset();
      throw;
    }
  }

  void Close() noexcept override { is_open_ = false; }

  void Publish(DdsByteView data) override {
    if (!is_server_ || !is_open_ || !broadcast_socket_)
      throw std::logic_error("ZeroMQ publisher is not open");
    std::lock_guard lock(publish_mutex_);
    broadcast_socket_->send(zmq::buffer(data.data(), data.size()),
                            zmq::send_flags::none);
  }

  DdsBytes Request(DdsByteView data,
                   std::chrono::milliseconds timeout) override {
    if (is_server_ || !is_open_ || !request_socket_)
      throw std::logic_error("ZeroMQ requester is not open");
    std::lock_guard lock(request_mutex_);
    request_socket_->send(zmq::buffer(data.data(), data.size()),
                          zmq::send_flags::none);
    zmq::pollitem_t item{static_cast<void*>(*request_socket_), 0, ZMQ_POLLIN,
                         0};
    if (zmq::poll(&item, 1, timeout) == 0)
      throw std::runtime_error("ZeroMQ request timed out");
    zmq::message_t response;
    if (!request_socket_->recv(response, zmq::recv_flags::none))
      throw std::runtime_error("ZeroMQ response receive failed");
    return CopyMessage(response);
  }

  bool TryReceive(const std::atomic_bool& stop_requested,
                  std::chrono::milliseconds timeout,
                  DdsIncomingMessage* message) override {
    if (message == nullptr) throw std::invalid_argument("message is null");
    while (is_open_ && !stop_requested.load()) {
      auto* receive_socket =
          is_server_ ? request_socket_.get() : broadcast_socket_.get();
      if (receive_socket == nullptr) {
        return false;
      }
      zmq::pollitem_t item{static_cast<void*>(*receive_socket), 0, ZMQ_POLLIN,
                           0};
      if (zmq::poll(&item, 1, timeout) == 0) return false;
      if (is_server_) {
        zmq::message_t identity, payload;
        if (!request_socket_->recv(identity) || !request_socket_->recv(payload))
          throw std::runtime_error("ZeroMQ request receive failed");
        *message = {
            DdsIncomingMessage::Kind::kRequest,
            CopyMessage(payload),
            {static_cast<const char*>(identity.data()), identity.size()}};
        return true;
      }
      zmq::message_t payload;
      if (!broadcast_socket_->recv(payload))
        throw std::runtime_error("ZeroMQ broadcast receive failed");
      *message = {
          DdsIncomingMessage::Kind::kBroadcast, CopyMessage(payload), {}};
      return true;
    }
    return false;
  }

  void Reply(const DdsIncomingMessage& incoming, DdsByteView data) override {
    if (!is_server_ || !is_open_ || !request_socket_) return;
    request_socket_->send(zmq::buffer(incoming.correlation_id),
                          zmq::send_flags::sndmore);
    request_socket_->send(zmq::buffer(data.data(), data.size()),
                          zmq::send_flags::none);
  }

 private:
  zmq::context_t& context_;
  std::unique_ptr<zmq::socket_t> broadcast_socket_, request_socket_;
  std::atomic_bool is_open_{};
  bool is_server_{};
  std::mutex publish_mutex_, request_mutex_;
};

}  // namespace
#endif

std::unique_ptr<DdsTransport> MakeDdsZeroMqTransport() {
#ifdef DDS_ABSTRACT_HAS_ZEROMQ
  return std::make_unique<DdsZeroMqTransport>();
#else
  throw std::runtime_error("ZeroMQ backend was not enabled at build time");
#endif
}

}  // namespace dds_abstract
