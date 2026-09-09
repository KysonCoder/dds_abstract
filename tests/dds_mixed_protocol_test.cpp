#include <cassert>
#include <chrono>
#include <future>
#include <thread>

#include "dds_abstract/dds_node.hpp"
#include "dds_test_utils.hpp"

using namespace dds_abstract;
using namespace std::chrono_literals;

void TestZeroMqBroadcastAndZenohRequest() {
  DdsServer server({"127.0.0.1:39275", "dds/abstract/mixed/rpc"});
  DdsClient client({"127.0.0.1:39275", "dds/abstract/mixed/rpc"});
  server.OnRequest([](DdsByteView value) {
    return BytesFromString("mixed:" + StringFromBytes(value));
  });
  std::promise<void> event;
  client.OnBroadcast([&](DdsByteView) { event.set_value(); });
  server.Start();
  client.Start();
  std::this_thread::sleep_for(500ms);
  assert(StringFromBytes(client.Request(BytesFromString("ping"), 2s)) ==
         "mixed:ping");
  server.Publish(BytesFromString("event"));
  assert(event.get_future().wait_for(1s) == std::future_status::ready);
}

void TestZenohBroadcastAndZeroMqRequest() {
  DdsServer server({"dds/abstract/mixed/events", "127.0.0.1:39276"});
  DdsClient client({"dds/abstract/mixed/events", "127.0.0.1:39276"});
  server.OnRequest([](DdsByteView value) {
    return BytesFromString("reverse:" + StringFromBytes(value));
  });
  std::promise<void> event;
  client.OnBroadcast([&](DdsByteView) { event.set_value(); });
  server.Start();
  client.Start();
  std::this_thread::sleep_for(500ms);
  assert(StringFromBytes(client.Request(BytesFromString("ping"), 1s)) ==
         "reverse:ping");
  server.Publish(BytesFromString("event"));
  assert(event.get_future().wait_for(2s) == std::future_status::ready);
}

int main() {
  TestZeroMqBroadcastAndZenohRequest();
  TestZenohBroadcastAndZeroMqRequest();
}
