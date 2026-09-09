#ifndef DDS_ABSTRACT_TESTS_DDS_TEST_UTILS_HPP_
#define DDS_ABSTRACT_TESTS_DDS_TEST_UTILS_HPP_

#include <cstring>
#include <string>

#include "dds_abstract/dds_node.hpp"

inline dds_abstract::DdsBytes BytesFromString(std::string_view text) {
  dds_abstract::DdsBytes value(text.size());
  std::memcpy(value.data(), text.data(), text.size());
  return value;
}

inline std::string StringFromBytes(dds_abstract::DdsByteView value) {
  return {reinterpret_cast<const char*>(value.data()), value.size()};
}

#endif  // DDS_ABSTRACT_TESTS_DDS_TEST_UTILS_HPP_
