#include "internal/dds_node_address.hpp"

#include <cctype>
#include <charconv>
#include <stdexcept>

namespace dds_abstract {
namespace {

bool IsDecimalInRange(std::string_view text, int minimum, int maximum) {
  if (text.empty()) return false;
  int value = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc() && result.ptr == text.data() + text.size() &&
         value >= minimum && value <= maximum;
}

bool IsIpv4Endpoint(std::string_view address) {
  const auto colon = address.rfind(':');
  if (colon == std::string_view::npos ||
      !IsDecimalInRange(address.substr(colon + 1), 1, 65535))
    return false;
  auto ipv4 = address.substr(0, colon);
  for (int index = 0; index < 4; ++index) {
    const auto dot = ipv4.find('.');
    if (!IsDecimalInRange(ipv4.substr(0, dot), 0, 255)) return false;
    if (index == 3) return dot == std::string_view::npos;
    if (dot == std::string_view::npos) return false;
    ipv4.remove_prefix(dot + 1);
  }
  return false;
}

bool IsInprocAddress(std::string_view address) {
  if (address.compare(0, 9, "inproc://") == 0) return address.size() > 9;
  if (address.compare(0, 7, "inproc/") == 0) return address.size() > 7;
  return false;
}

}  // namespace

DdsNodeAddress DdsNodeAddress::Parse(std::string_view value) {
  if (value.empty()) throw std::invalid_argument("node address is empty");
  if (IsInprocAddress(value) || IsIpv4Endpoint(value))
    return {DdsTransportKind::kZeroMq, std::string(value)};
  if (value.compare(0, 6, "inproc") == 0 ||
      (std::isdigit(static_cast<unsigned char>(value.front())) &&
       value.find(':') != std::string_view::npos))
    throw std::invalid_argument("invalid ZeroMQ node address: " +
                                std::string(value));
  return {DdsTransportKind::kZenoh, std::string(value)};
}

std::string DdsNodeAddress::ToString() const { return resource; }

}  // namespace dds_abstract
