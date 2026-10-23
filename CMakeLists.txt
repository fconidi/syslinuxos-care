cmake_minimum_required(VERSION 3.20)
project(linux_maintenance_suite VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

add_library(system_metrics src/metrics.cpp)
target_include_directories(system_metrics PUBLIC src)
target_link_libraries(system_metrics PUBLIC Qt6::Core)

add_executable(linux-maintenance-suite src/main.cpp)
target_link_libraries(linux-maintenance-suite PRIVATE system_metrics Qt6::Widgets)

enable_testing()
add_executable(metrics_test tests/metrics_test.cpp)
target_link_libraries(metrics_test PRIVATE system_metrics Qt6::Core)
add_test(NAME metrics_test COMMAND metrics_test)
