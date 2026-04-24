cmake_minimum_required(VERSION 3.20)
project(halloc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(HALLOC_ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(HALLOC_ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)
option(HALLOC_ENABLE_TSAN "Enable Thread Sanitizer" OFF)

if(HALLOC_ENABLE_TSAN AND (HALLOC_ENABLE_ASAN OR HALLOC_ENABLE_UBSAN))
    message(FATAL_ERROR "TSan cannot be enabled together with ASan or UBSan")
endif()

include(CTest)

add_library(halloc src/halloc.cpp src/os.cpp src/span.cpp src/page_heap.cpp src/thread_cache.cpp src/stats.cpp)
target_include_directories(halloc PUBLIC ${CMAKE_SOURCE_DIR}/include)

add_executable(halloc_unit_tests tests/main.cpp)
target_link_libraries(halloc_unit_tests PRIVATE halloc)
target_include_directories(halloc_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(halloc_stress_tests tests/stress_main.cpp)
target_include_directories(halloc_stress_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)

find_package(benchmark)

if(NOT benchmark_FOUND)
    set(benchmark_DIR /opt/homebrew/lib/cmake/benchmark)
    find_package(benchmark)
endif()

add_executable(halloc_mt_stress benchmarks/mt_stress.cpp)
target_link_libraries(halloc_mt_stress PRIVATE halloc)
target_include_directories(halloc_mt_stress PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(halloc_bench benchmarks/main.cpp)
target_include_directories(halloc_bench PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(benchmark_FOUND)
    target_link_libraries(halloc_bench PRIVATE benchmark::benchmark halloc)
else()
    target_link_libraries(halloc_bench PRIVATE halloc)
endif()

add_test(NAME smoke_halloc_unit_tests COMMAND halloc_unit_tests LABELS "unit;smoke")
add_test(NAME smoke_halloc_stress_tests COMMAND halloc_stress_tests LABELS "stress;smoke")