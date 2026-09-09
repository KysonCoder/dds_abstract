cmake_minimum_required(VERSION 3.16)
project(dds_abstract VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(DDS_ABSTRACT_BUILD_TESTS "Build unit tests" ON)
option(DDS_ABSTRACT_WITH_ZEROMQ "Enable ZeroMQ backend when found" ON)
option(DDS_ABSTRACT_WITH_ZENOH "Enable Zenoh backend when found" ON)
option(DDS_ABSTRACT_BUILD_PYTHON "Build Python bindings when pybind11 is found" ON)
option(DDS_ABSTRACT_REQUIRE_BACKENDS
       "Fail configuration when an enabled backend is unavailable" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

add_library(dds_abstract
    src/dds_node.cpp
    src/dds_node_address.cpp
    src/dds_thread_config.cpp
    src/dds_zeromq_transport.cpp
    src/dds_zenoh_transport.cpp)
add_library(dds::abstract ALIAS dds_abstract)

target_compile_features(dds_abstract PUBLIC cxx_std_17)
set_target_properties(dds_abstract PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_include_directories(dds_abstract PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
if (WIN32)
  target_compile_definitions(dds_abstract PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if (DDS_ABSTRACT_BUILD_PYTHON)
  find_package(Python COMPONENTS Interpreter Development REQUIRED)
  set(PYBIND11_FINDPYTHON ON)
  find_package(pybind11 CONFIG QUIET)
  if (pybind11_FOUND)
    pybind11_add_module(_dds_abstract python/dds_abstract_pybind.cpp)
    target_link_libraries(_dds_abstract PRIVATE dds::abstract)
    set_target_properties(_dds_abstract PROPERTIES
        INSTALL_RPATH "$ORIGIN/../..")
    target_compile_definitions(_dds_abstract
        PRIVATE DDS_ABSTRACT_VERSION="${PROJECT_VERSION}")
    install(TARGETS _dds_abstract
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/python/dds_abstract)
    install(FILES python/dds_abstract/__init__.py
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/python/dds_abstract)
  else()
    message(STATUS "dds_abstract: pybind11 not found; Python module disabled")
  endif()
endif()

install(TARGETS dds_abstract EXPORT dds_abstractTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/dds_abstract
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT dds_abstractTargets
    FILE dds_abstractTargets.cmake
    NAMESPACE dds::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dds_abstract)
configure_package_config_file(
    cmake/dds_abstractConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/dds_abstractConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dds_abstract)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/dds_abstractConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/dds_abstractConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/dds_abstractConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/dds_abstract)

if (DDS_ABSTRACT_WITH_ZEROMQ)
  find_package(cppzmq QUIET)
  if (cppzmq_FOUND)
    target_link_libraries(dds_abstract PRIVATE cppzmq)
    target_compile_definitions(dds_abstract PRIVATE DDS_ABSTRACT_HAS_ZEROMQ=1)
    message(STATUS "dds_abstract: ZeroMQ backend enabled")
  else()
    find_path(DDS_ABSTRACT_ZMQ_INCLUDE_DIR NAMES zmq.hpp)
    find_library(DDS_ABSTRACT_ZMQ_LIBRARY NAMES zmq)
    if (DDS_ABSTRACT_ZMQ_INCLUDE_DIR AND DDS_ABSTRACT_ZMQ_LIBRARY)
      add_library(dds_abstract_cppzmq INTERFACE)
      target_include_directories(dds_abstract_cppzmq SYSTEM INTERFACE
          ${DDS_ABSTRACT_ZMQ_INCLUDE_DIR})
      target_link_libraries(dds_abstract_cppzmq INTERFACE
          ${DDS_ABSTRACT_ZMQ_LIBRARY})
      target_link_libraries(dds_abstract PRIVATE dds_abstract_cppzmq)
      set(cppzmq_FOUND TRUE)
      target_compile_definitions(dds_abstract PRIVATE DDS_ABSTRACT_HAS_ZEROMQ=1)
      message(STATUS "dds_abstract: ZeroMQ backend enabled using system libzmq/cppzmq headers")
    elseif (DDS_ABSTRACT_REQUIRE_BACKENDS)
      message(FATAL_ERROR
          "dds_abstract: enabled ZeroMQ backend requires cppzmq or system zmq.hpp/libzmq")
    else()
      message(STATUS "dds_abstract: cppzmq and system ZeroMQ were not found; backend disabled")
    endif()
  endif()
endif()

if (DDS_ABSTRACT_WITH_ZENOH)
  find_package(zenohc QUIET)
  find_package(zenohcxx QUIET)
  if (zenohc_FOUND AND zenohcxx_FOUND)
    target_link_libraries(dds_abstract PRIVATE zenohcxx::zenohc)
    target_compile_definitions(dds_abstract PRIVATE DDS_ABSTRACT_HAS_ZENOH=1)
    message(STATUS "dds_abstract: Zenoh backend enabled")
  else()
    if (DDS_ABSTRACT_REQUIRE_BACKENDS)
      message(FATAL_ERROR
          "dds_abstract: enabled Zenoh backend requires zenoh-c and zenoh-cpp")
    endif()
    message(STATUS "dds_abstract: zenoh-c/zenoh-cpp not found; Zenoh backend disabled")
  endif()
endif()

if (DDS_ABSTRACT_BUILD_TESTS)
  enable_testing()
  add_executable(dds_abstract_tests tests/dds_node_test.cpp)
  target_link_libraries(dds_abstract_tests PRIVATE dds::abstract)
  add_test(NAME dds_abstract_tests COMMAND dds_abstract_tests)
  if (cppzmq_FOUND)
    add_executable(dds_abstract_zeromq_tests tests/dds_zeromq_test.cpp)
    target_link_libraries(dds_abstract_zeromq_tests PRIVATE dds::abstract)
    add_test(NAME dds_abstract_zeromq_tests COMMAND dds_abstract_zeromq_tests)
  endif()
  if (zenohc_FOUND AND zenohcxx_FOUND)
    add_executable(dds_abstract_zenoh_tests tests/dds_zenoh_test.cpp)
    target_link_libraries(dds_abstract_zenoh_tests PRIVATE dds::abstract)
    add_test(NAME dds_abstract_zenoh_tests COMMAND dds_abstract_zenoh_tests)
  endif()
  if (cppzmq_FOUND AND zenohc_FOUND AND zenohcxx_FOUND)
    add_executable(dds_abstract_mixed_protocol_tests
        tests/dds_mixed_protocol_test.cpp)
    target_link_libraries(dds_abstract_mixed_protocol_tests
        PRIVATE dds::abstract)
    add_test(NAME dds_abstract_mixed_protocol_tests
        COMMAND dds_abstract_mixed_protocol_tests)
  endif()
  if (TARGET _dds_abstract)
    add_test(NAME dds_abstract_python_tests
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/dds_python_test.py)
    set_tests_properties(dds_abstract_python_tests PROPERTIES
        ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:_dds_abstract>")
  endif()
endif()
