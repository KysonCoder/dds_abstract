#ifndef DDS_ABSTRACT_SRC_INTERNAL_DDS_THREAD_CONFIG_HPP_
#define DDS_ABSTRACT_SRC_INTERNAL_DDS_THREAD_CONFIG_HPP_

#include "dds_abstract/dds_node.hpp"

namespace dds_abstract {

void ConfigureDdsCurrentThread(const DdsThreadConfig& config);

}  // namespace dds_abstract

#endif  // DDS_ABSTRACT_SRC_INTERNAL_DDS_THREAD_CONFIG_HPP_
