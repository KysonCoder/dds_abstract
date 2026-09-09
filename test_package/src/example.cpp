#include <dds_abstract/dds_node.hpp>

int main() {
  dds_abstract::DdsClient client({"dds/abstract/package/events", ""});
  return client.IsRunning() ? 1 : 0;
}
