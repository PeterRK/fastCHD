#===============================================================================
# A modern implement of CHD algorithm.
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
project(fastCHD VERSION 1.1.1)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()
find_package(Threads REQUIRED)

option(MODERN_CPU_ONLY "build for modern CPU only" ON)
option(CHD_BUILD_TESTS "build tests" ON)
option(CHD_BUILD_BENCHMARKS "build benchmarks" OFF)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-fno-unroll-loops)
endif()
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options($<$<CONFIG:Release>:-O2>)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    add_compile_options($<$<CONFIG:Release>:-O3>)
endif()
if (MODERN_CPU_ONLY AND NOT WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    message(STATUS "fastCHD: tuning x86-64 build for Skylake or newer")
    add_compile_options(-march=skylake)
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Keep only the switch heuristic used by the hot lookup path.
    add_compile_options(--param=case-values-threshold=3)
endif()

# Include paths are attached to targets below.

file(GLOB source CONFIGURE_DEPENDS src/*.cc)

add_library(chd SHARED ${source})
target_compile_features(chd PUBLIC cxx_std_17)
target_include_directories(chd PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(chd PRIVATE Threads::Threads)
target_compile_definitions(chd PRIVATE $<$<CONFIG:Release>:NDEBUG>)

if (CHD_BUILD_TESTS)
    find_package(GTest REQUIRED)
    file(GLOB test_src CONFIGURE_DEPENDS test/*.cc)
    add_executable(chd-test ${test_src})
    target_link_libraries(chd-test PRIVATE Threads::Threads GTest::gtest chd)
    add_test(NAME chd-test COMMAND chd-test)
endif()

if (CHD_BUILD_BENCHMARKS)
    find_package(gflags CONFIG REQUIRED)
    add_executable(bench-billion benchmark/billion.cc)
    target_link_libraries(bench-billion PRIVATE Threads::Threads gflags::gflags chd)
endif()

install(TARGETS chd
    EXPORT fastCHDTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

set(fastCHD_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/fastCHD")
configure_package_config_file(
    cmake/fastCHDConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/fastCHDConfig.cmake"
    INSTALL_DESTINATION "${fastCHD_INSTALL_CMAKEDIR}"
)
install(EXPORT fastCHDTargets
    FILE fastCHDTargets.cmake
    NAMESPACE fastCHD::
    DESTINATION "${fastCHD_INSTALL_CMAKEDIR}"
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/fastCHDConfig.cmake"
    DESTINATION "${fastCHD_INSTALL_CMAKEDIR}"
)
