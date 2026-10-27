cmake_minimum_required(VERSION 3.14)
project(kvstore VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler optimization flags
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -O3 -pthread)
endif()

include_directories(include)

# Common & Engine & Network core library
set(KVSTORE_LIB_SOURCES
    src/common/checksum.cpp
    src/common/types.cpp
    src/common/protocol.cpp
    src/engine/hash_index.cpp
    src/engine/wal.cpp
    src/engine/snapshot.cpp
    src/engine/engine.cpp
    src/network/socket.cpp
    src/network/server.cpp
)

add_library(kvstore_core STATIC ${KVSTORE_LIB_SOURCES})
target_link_libraries(kvstore_core PUBLIC pthread)

# Executable: Server
add_executable(kvstore-server src/server_main.cpp)
target_link_libraries(kvstore-server PRIVATE kvstore_core pthread)

# Executable: CLI Client
add_executable(kvstore-cli src/cli_main.cpp)
target_link_libraries(kvstore-cli PRIVATE kvstore_core pthread)

# Executable: Benchmark
add_executable(kvstore-benchmark benchmarks/benchmark.cpp)
target_link_libraries(kvstore-benchmark PRIVATE kvstore_core pthread)

# Unit Tests
enable_testing()

set(TESTS
    test_checksum
    test_types
    test_hash_index
    test_wal_recovery
    test_checkpoint
    test_concurrency
)

foreach(t ${TESTS})
    add_executable(${t} tests/${t}.cpp)
    target_compile_options(${t} PRIVATE -UNDEBUG)
    target_link_libraries(${t} PRIVATE kvstore_core pthread)
    add_test(NAME ${t} COMMAND ${t})
endforeach()
