cmake_minimum_required(VERSION 3.16)
project(oram_lib VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++20 required for std::span
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Find OpenSSL
find_package(OpenSSL REQUIRED)

# ORAM library source files
set(ORAM_SOURCES
    src/crypto.cpp
    src/bucket_codec.cpp
    src/server.cpp
    src/oram.cpp
    src/path_oram.cpp
    src/ring_oram.cpp
)

# Create static library
add_library(oram STATIC ${ORAM_SOURCES})

# Include directories
target_include_directories(oram PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}  # For third_party includes
)

# Link OpenSSL
target_link_libraries(oram PUBLIC OpenSSL::SSL OpenSSL::Crypto)

# Install rules
install(TARGETS oram
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY include/oram
    DESTINATION include
)

# Option to build tests (will be implemented in Step 4)
option(BUILD_TESTS "Build unit tests" OFF)

if(BUILD_TESTS)
    enable_testing()
    # Tests will be added in Step 4
    # add_subdirectory(tests)
endif()

# Option to build benchmarks (will be implemented in Step 4)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)

if(BUILD_BENCHMARKS)
    # Benchmarks will be added in Step 4
    # add_subdirectory(benchmarks)
endif()

# Simple test executable for manual verification
add_executable(oram_simple_test tests/simple_test.cpp)
target_link_libraries(oram_simple_test oram)

# Path ORAM integration test
add_executable(path_oram_test tests/path_oram_test.cpp)
target_link_libraries(path_oram_test oram)

# Path ORAM network test (client-server)
add_executable(path_oram_network_test tests/path_oram_network_test.cpp)
target_link_libraries(path_oram_network_test oram)

# =============================================================================
# Clean targets
# =============================================================================

# Mark additional files for cleaning
set_directory_properties(PROPERTIES
    ADDITIONAL_CLEAN_FILES "${CMAKE_BINARY_DIR}/CMakeCache.txt;${CMAKE_BINARY_DIR}/CMakeFiles"
)

# Custom target to clean everything including CMake cache
add_custom_target(distclean
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/CMakeFiles
    COMMAND ${CMAKE_COMMAND} -E remove -f ${CMAKE_BINARY_DIR}/CMakeCache.txt
    COMMAND ${CMAKE_COMMAND} -E remove -f ${CMAKE_BINARY_DIR}/cmake_install.cmake
    COMMAND ${CMAKE_COMMAND} -E remove -f ${CMAKE_BINARY_DIR}/Makefile
    COMMAND ${CMAKE_COMMAND} -E remove -f ${CMAKE_BINARY_DIR}/*.a
    COMMAND ${CMAKE_COMMAND} -E remove -f ${CMAKE_BINARY_DIR}/oram_simple_test
    COMMAND ${CMAKE_COMMAND} -E remove -f ${CMAKE_BINARY_DIR}/path_oram_test
    COMMAND ${CMAKE_COMMAND} -E remove -f ${CMAKE_BINARY_DIR}/path_oram_network_test
    COMMENT "Removing all build files and CMake cache"
)

# Custom target to rebuild from scratch
add_custom_target(rebuild
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
    COMMENT "Clean and rebuild all targets"
)
