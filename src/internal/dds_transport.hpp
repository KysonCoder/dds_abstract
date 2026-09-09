#ifndef DDS_ABSTRACT_SRC_INTERNAL_DDS_TRANSPORT_HPP_
#define DDS_ABSTRACT_SRC_INTERNAL_DDS_TRANSPORT_HPP_

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "dds_abstract/dds_node.hpp"
#include "internal/dds_node_address.hpp"

namespace dds_abstract {

struct DdsIncomingMessage {
  enum class Kind { kBroadcast, kRequest } kind;
  DdsBytes payload;
  // Opaque backend correlation data. Empty for broadcasts.
  std::string correlation_id;
};

class DdsTransport {
 public:
  virtual ~DdsTransport() = default;
  // A null node address disables the corresponding channel.
  virtual void Open(const DdsNodeAddress* broadcast_address,
                    const DdsNodeAddress* request_address, bool is_server) = 0;
  virtual void Close() noexcept = 0;
  virtual void Publish(DdsByteView message) = 0;
  virtual DdsBytes Request(DdsByteView message,
                           std::chrono::milliseconds timeout) = 0;
  virtual bool TryReceive(const std::atomic_bool& stop_requested,
                          std::chrono::milliseconds timeout,
                          DdsIncomingMessage* message) = 0;
  virtual void Reply(const DdsIncomingMessage& request,
                     DdsByteView response) = 0;
};

using DdsTransportFactory =
    std::function<std::unique_ptr<DdsTransport>(DdsTransportKind)>;

// Process-local backend used by tests and useful for deterministic simulation.
[[nodiscard]] std::unique_ptr<DdsTransport> MakeDdsZeroMqTransport();
[[nodiscard]] std::unique_ptr<DdsTransport> MakeDdsZenohTransport();

}  // namespace dds_abstract

#endif  // DDS_ABSTRACT_SRC_INTERNAL_DDS_TRANSPORT_HPP_
