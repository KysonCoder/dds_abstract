#include <pybind11/functional.h>
#include <pybind11/pybind11.h>

#include <chrono>
#include <cstring>
#include <memory>
#include <string>

#include "dds_abstract/dds_node.hpp"

namespace py = pybind11;
using namespace dds_abstract;

namespace {

DdsBytes FromPythonBytes(const py::bytes& value) {
  std::string data = value;
  DdsBytes result(data.size());
  std::memcpy(result.data(), data.data(), data.size());
  return result;
}

py::bytes ToPythonBytes(DdsByteView value) {
  return py::bytes(reinterpret_cast<const char*>(value.data()), value.size());
}

std::string ExceptionMessage(std::exception_ptr error) {
  try {
    if (error) std::rethrow_exception(error);
  } catch (const std::exception& exception) {
    return exception.what();
  } catch (...) {
    return "unknown C++ exception";
  }
  return {};
}

}  // namespace

PYBIND11_MODULE(_dds_abstract, module) {
  module.attr("__version__") = DDS_ABSTRACT_VERSION;

  py::enum_<DdsSchedulingPolicy>(module, "SchedulingPolicy")
      .value("NORMAL", DdsSchedulingPolicy::kNormal)
      .value("FIFO", DdsSchedulingPolicy::kFifo)
      .value("ROUND_ROBIN", DdsSchedulingPolicy::kRoundRobin);

  py::class_<DdsServer>(module, "Server")
      .def(py::init([](std::string broadcast_node_address,
                       std::string request_node_address) {
             return std::make_unique<DdsServer>(
                 DdsServerConfig{std::move(broadcast_node_address),
                                 std::move(request_node_address)});
           }),
           py::arg("broadcast_node_address") = "",
           py::arg("request_node_address") = "")
      .def("on_request",
           [](DdsServer& self, py::function callback) {
             auto shared_callback =
                 std::make_shared<py::function>(std::move(callback));
             self.OnRequest([shared_callback](DdsByteView value) {
               py::gil_scoped_acquire acquire;
               return FromPythonBytes(
                   (*shared_callback)(ToPythonBytes(value)).cast<py::bytes>());
             });
           })
      .def("on_error",
           [](DdsServer& self, py::function callback) {
             auto shared_callback =
                 std::make_shared<py::function>(std::move(callback));
             self.OnError([shared_callback](std::exception_ptr error) {
               py::gil_scoped_acquire acquire;
               (*shared_callback)(ExceptionMessage(error));
             });
           })
      .def("set_worker_thread_name", &DdsServer::SetWorkerThreadName)
      .def("set_worker_thread_scheduling",
           &DdsServer::SetWorkerThreadScheduling, py::arg("policy"),
           py::arg("priority"), py::arg("strict") = false)
      .def("start", &DdsServer::Start)
      .def("stop", &DdsServer::Stop, py::call_guard<py::gil_scoped_release>())
      .def("is_running", &DdsServer::IsRunning)
      .def("publish", [](DdsServer& self, const py::bytes& value) {
        self.Publish(FromPythonBytes(value));
      });

  py::class_<DdsClient>(module, "Client")
      .def(py::init([](std::string broadcast_node_address,
                       std::string request_node_address) {
             return std::make_unique<DdsClient>(
                 DdsClientConfig{std::move(broadcast_node_address),
                                 std::move(request_node_address)});
           }),
           py::arg("broadcast_node_address") = "",
           py::arg("request_node_address") = "")
      .def("on_broadcast",
           [](DdsClient& self, py::function callback) {
             auto shared_callback =
                 std::make_shared<py::function>(std::move(callback));
             self.OnBroadcast([shared_callback](DdsByteView value) {
               py::gil_scoped_acquire acquire;
               (*shared_callback)(ToPythonBytes(value));
             });
           })
      .def("on_error",
           [](DdsClient& self, py::function callback) {
             auto shared_callback =
                 std::make_shared<py::function>(std::move(callback));
             self.OnError([shared_callback](std::exception_ptr error) {
               py::gil_scoped_acquire acquire;
               (*shared_callback)(ExceptionMessage(error));
             });
           })
      .def("set_worker_thread_name", &DdsClient::SetWorkerThreadName)
      .def("set_worker_thread_scheduling",
           &DdsClient::SetWorkerThreadScheduling, py::arg("policy"),
           py::arg("priority"), py::arg("strict") = false)
      .def("start", &DdsClient::Start)
      .def("stop", &DdsClient::Stop, py::call_guard<py::gil_scoped_release>())
      .def("is_running", &DdsClient::IsRunning)
      .def(
          "request",
          [](DdsClient& self, const py::bytes& value, int timeout_ms) {
            const auto request = FromPythonBytes(value);
            DdsBytes response;
            {
              py::gil_scoped_release release;
              response =
                  self.Request(request, std::chrono::milliseconds(timeout_ms));
            }
            return ToPythonBytes(response);
          },
          py::arg("message"), py::arg("timeout_ms") = 1000);
}
