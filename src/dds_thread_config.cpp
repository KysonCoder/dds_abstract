#include "internal/dds_thread_config.hpp"

#include <stdexcept>
#include <system_error>

#include "dds_abstract/dds_node.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

namespace dds_abstract {

void ConfigureDdsCurrentThread(const DdsThreadConfig& config) {
  auto fail = [&](const char* what, int ec) {
    if (config.strict)
      throw std::system_error(ec, std::system_category(), what);
  };
#ifdef _WIN32
  if (!config.name.empty()) {
    std::wstring name(config.name.begin(), config.name.end());
    const HRESULT hr = SetThreadDescription(GetCurrentThread(), name.c_str());
    if (FAILED(hr)) fail("SetThreadDescription", static_cast<int>(hr));
  }
  if (config.policy != DdsSchedulingPolicy::kNormal || config.priority != 0) {
    int priority = THREAD_PRIORITY_NORMAL;
    if (config.priority >= 2)
      priority = THREAD_PRIORITY_TIME_CRITICAL;
    else if (config.priority == 1)
      priority = THREAD_PRIORITY_HIGHEST;
    else if (config.priority == -1)
      priority = THREAD_PRIORITY_BELOW_NORMAL;
    else if (config.priority <= -2)
      priority = THREAD_PRIORITY_LOWEST;
    if (!SetThreadPriority(GetCurrentThread(), priority))
      fail("SetThreadPriority", static_cast<int>(GetLastError()));
  }
#else
  if (!config.name.empty()) {
    std::string name = config.name.substr(0, 15);
    const int ec = pthread_setname_np(pthread_self(), name.c_str());
    if (ec) fail("pthread_setname_np", ec);
  }
  if (config.policy != DdsSchedulingPolicy::kNormal) {
    const int policy =
        config.policy == DdsSchedulingPolicy::kFifo ? SCHED_FIFO : SCHED_RR;
    sched_param parameter{};
    parameter.sched_priority = config.priority;
    const int ec = pthread_setschedparam(pthread_self(), policy, &parameter);
    if (ec) fail("pthread_setschedparam", ec);
  }
#endif
}

}  // namespace dds_abstract
