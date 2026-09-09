#ifndef DDS_ABSTRACT_SRC_INTERNAL_DDS_NODE_ADDRESS_HPP_
#define DDS_ABSTRACT_SRC_INTERNAL_DDS_NODE_ADDRESS_HPP_

#include <string>
#include <string_view>

namespace dds_abstract {

enum class DdsTransportKind { kZeroMq, kZenoh };

struct DdsNodeAddress {
  DdsTransportKind transport{};
  std::string resource;

  [[nodiscard]] static DdsNodeAddress Parse(std::string_view value);
  [[nodiscard]] std::string ToString() const;
};

}  // namespace dds_abstract

#endif  // DDS_ABSTRACT_SRC_INTERNAL_DDS_NODE_ADDRESS_HPP_
