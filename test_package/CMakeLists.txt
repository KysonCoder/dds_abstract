cmake_minimum_required(VERSION 3.16)
project(dds_abstract_package_test LANGUAGES CXX)

find_package(dds_abstract CONFIG REQUIRED)

add_executable(dds_abstract_package_test src/example.cpp)
target_compile_features(dds_abstract_package_test PRIVATE cxx_std_17)
target_link_libraries(dds_abstract_package_test PRIVATE dds::dds_abstract)
