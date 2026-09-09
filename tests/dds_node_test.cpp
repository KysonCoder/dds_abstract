#include "dds_abstract/dds_node.hpp"

#include <cassert>
#include <chrono>
#include <future>
#include <stdexcept>

#include "dds_test_utils.hpp"

using namespace dds_abstract;
using namespace std::chrono_literals;

int main() {
  DdsServer server({"inproc://multi/events", "inproc://multi/rpc"});
  DdsClient client1({"inproc://multi/events", "inproc://multi/rpc"});
  DdsClient client2({"inproc://multi/events", "inproc://multi/rpc"});
  server.OnRequest([](DdsByteView value) {
    return BytesFromString("reply:" + StringFromBytes(value));
  });
  std::promise<void> event1;
  std::promise<void> event2;
  client1.OnBroadcast([&](DdsByteView) { event1.set_value(); });
  client2.OnBroadcast([&](DdsByteView) { event2.set_value(); });
  server.SetWorkerThreadName("dds-server");
  server.SetWorkerThreadScheduling(DdsSchedulingPolicy::kNormal, 0, true);
  server.Start();
  client1.Start();
  client2.Start();
  assert(StringFromBytes(client1.Request(BytesFromString("one"), 500ms)) ==
         "reply:one");
  assert(StringFromBytes(client2.Request(BytesFromString("two"), 500ms)) ==
         "reply:two");
  server.Publish(BytesFromString("event"));
  assert(event1.get_future().wait_for(500ms) == std::future_status::ready);
  assert(event2.get_future().wait_for(500ms) == std::future_status::ready);

  bool rejected = false;
  try {
    DdsClient invalid({"", ""});
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  assert(rejected);
}
