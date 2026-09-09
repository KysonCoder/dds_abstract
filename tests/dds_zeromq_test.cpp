#include <array>
#include <cassert>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "dds_abstract/dds_node.hpp"
#include "dds_test_utils.hpp"

using namespace dds_abstract;
using namespace std::chrono_literals;

int main() {
  constexpr std::size_t kClientCount = 4;
  DdsServer server({"127.0.0.1:39271", "127.0.0.1:39272"});
  server.OnRequest([](DdsByteView value) {
    return BytesFromString("zmq:" + StringFromBytes(value));
  });

  std::array<std::promise<void>, kClientCount> events;
  std::vector<std::unique_ptr<DdsClient>> clients;
  clients.reserve(kClientCount);
  for (std::size_t i = 0; i < kClientCount; ++i) {
    auto client = std::make_unique<DdsClient>(
        DdsClientConfig{"127.0.0.1:39271", "127.0.0.1:39272"});
    client->OnBroadcast([&, i](DdsByteView value) {
      assert(StringFromBytes(value) == "event");
      events[i].set_value();
    });
    clients.push_back(std::move(client));
  }

  server.Start();
  for (const auto& client : clients) {
    client->Start();
  }
  std::this_thread::sleep_for(300ms);
  for (std::size_t i = 0; i < kClientCount; ++i) {
    const std::string request = "ping-" + std::to_string(i);
    assert(StringFromBytes(clients[i]->Request(BytesFromString(request), 1s)) ==
           "zmq:" + request);
  }
  server.Publish(BytesFromString("event"));
  for (auto& event : events) {
    assert(event.get_future().wait_for(1s) == std::future_status::ready);
  }
}
