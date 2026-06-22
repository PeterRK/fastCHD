#===============================================================================
# Skew Hash and Displace Algorithm.
# Copyright (C) 2020  Ruan Kunliang
# 
# This library is free software; you can redistribute it and/or modify it under 
# the terms of the GNU Lesser General Public License as published by the Free 
# Software Foundation; either version 2.1 of the License, or (at your option) 
# any later version.
#
# This library is distributed in the hope that it will be useful, but WITHOUT 
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
# details.
#
# You should have received a copy of the GNU Lesser General Public License 
# along with the This Library; if not, see <https:#www.gnu.org/licenses/>.
#===============================================================================

cmake_minimum_required(VERSION 3.15)
project(fastSHD)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()
find_package(Threads REQUIRED)

option(MODERN_CPU_ONLY "build for modern CPU only" ON)
option(SHD_BUILD_TESTS "build tests" ON)
option(SHD_BUILD_BENCHMARKS "build benchmarks" OFF)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-fno-unroll-loops)
endif()
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options($<$<CONFIG:Release>:-O2>)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    add_compile_options($<$<CONFIG:Release>:-O3>)
endif()
if (MODERN_CPU_ONLY AND NOT WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    message(STATUS "fastSHD: tuning x86-64 build for Skylake or newer")
    add_compile_options(-march=skylake)
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Keep only the switch heuristic used by the hot lookup path.
    add_compile_options(--param=case-values-threshold=3)
endif()

# Include paths are attached to targets below.

file(GLOB source CONFIGURE_DEPENDS src/*.cc)

add_library(shd SHARED ${source})
target_compile_features(shd PUBLIC cxx_std_17)
target_include_directories(shd PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(shd PRIVATE Threads::Threads)
target_compile_definitions(shd PRIVATE SHD_BUILDING_SHARED PUBLIC SHD_USING_SHARED)
target_compile_definitions(shd PRIVATE $<$<CONFIG:Release>:NDEBUG>)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(shd PRIVATE -fvisibility=hidden)
endif()

if (SHD_BUILD_TESTS)
    find_package(GTest REQUIRED)
    file(GLOB test_src CONFIGURE_DEPENDS test/*.cc)
    add_executable(shd-test ${test_src})
    target_link_libraries(shd-test PRIVATE Threads::Threads GTest::gtest shd)
    add_test(NAME shd-test COMMAND shd-test)
endif()

if (SHD_BUILD_BENCHMARKS)
    find_package(gflags CONFIG REQUIRED)
    add_executable(bench-billion benchmark/billion.cc)
    target_link_libraries(bench-billion PRIVATE Threads::Threads gflags::gflags shd)
endif()
