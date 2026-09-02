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

cmake_minimum_required(VERSION 3.18)
project(fastSHD VERSION 2.1.1)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()
find_package(Threads REQUIRED)

option(MODERN_CPU_ONLY "build for modern CPU only" ON)
option(SHD_BUILD_TESTS "build tests" ON)
option(SHD_BUILD_BENCHMARKS "build benchmarks" OFF)
option(SHD_ENABLE_CUDA "build the CUDA query library" OFF)

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
target_include_directories(shd PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(shd PRIVATE Threads::Threads)
target_compile_definitions(shd PRIVATE SHD_BUILDING_SHARED PUBLIC SHD_USING_SHARED)
target_compile_definitions(shd PRIVATE $<$<CONFIG:Release>:NDEBUG>)

if (SHD_ENABLE_CUDA)
    if (NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES "60;70;75;80;86;89;90" CACHE STRING
            "NVIDIA GPU architectures for the CUDA query library")
    endif()
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)

    add_library(shd_cuda SHARED src/cuda/shd.cu)
    set_target_properties(shd_cuda PROPERTIES
        OUTPUT_NAME shd-cuda
        CUDA_STANDARD 17
        CUDA_STANDARD_REQUIRED ON
        CUDA_EXTENSIONS OFF
        CUDA_RUNTIME_LIBRARY Shared
        CUDA_SEPARABLE_COMPILATION OFF
        CXX_VISIBILITY_PRESET hidden
        CUDA_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
    target_include_directories(shd_cuda PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(shd_cuda PRIVATE CUDA::cudart)
    target_compile_definitions(shd_cuda
        PRIVATE SHD_CUDA_BUILDING_SHARED
        INTERFACE SHD_CUDA_USING_SHARED
    )
    target_compile_definitions(shd_cuda PRIVATE $<$<CONFIG:Release>:NDEBUG>)
endif()

if (SHD_BUILD_TESTS)
    find_package(GTest REQUIRED)
    file(GLOB test_src CONFIGURE_DEPENDS test/*.cc)
    add_executable(shd-test ${test_src})
    target_link_libraries(shd-test PRIVATE Threads::Threads GTest::gtest shd)
    add_test(NAME shd-test COMMAND shd-test)

    if (SHD_ENABLE_CUDA)
        add_executable(shd-cuda-test test/test_cuda.cu test/test.cc)
        set_target_properties(shd-cuda-test PROPERTIES
            CUDA_STANDARD 17
            CUDA_STANDARD_REQUIRED ON
            CUDA_EXTENSIONS OFF
            CUDA_RUNTIME_LIBRARY Shared
        )
        target_link_libraries(shd-cuda-test
            PRIVATE Threads::Threads GTest::gtest shd shd_cuda CUDA::cudart)
        add_test(NAME shd-cuda-test COMMAND shd-cuda-test)
    endif()
endif()

if (SHD_BUILD_BENCHMARKS)
    find_package(gflags CONFIG REQUIRED)
    add_executable(bench-billion benchmark/billion.cc)
    target_link_libraries(bench-billion PRIVATE Threads::Threads gflags::gflags shd)
endif()

install(TARGETS shd
    EXPORT fastSHDTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
if (SHD_ENABLE_CUDA)
    install(TARGETS shd_cuda
        EXPORT fastSHDTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN "shd_cuda.h" EXCLUDE)
if (SHD_ENABLE_CUDA)
    install(FILES include/shd_cuda.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

set(fastSHD_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/fastSHD")
configure_package_config_file(
    cmake/fastSHDConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/fastSHDConfig.cmake"
    INSTALL_DESTINATION "${fastSHD_INSTALL_CMAKEDIR}"
)
install(EXPORT fastSHDTargets
    FILE fastSHDTargets.cmake
    NAMESPACE fastSHD::
    DESTINATION "${fastSHD_INSTALL_CMAKEDIR}"
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/fastSHDConfig.cmake"
    DESTINATION "${fastSHD_INSTALL_CMAKEDIR}"
)
