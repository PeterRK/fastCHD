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

cmake_minimum_required(VERSION 3.10)
project(fastSHD)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_SKIP_BUILD_RPATH TRUE)
set(CMAKE_EXE_LINKER_FLAGS -Wl,--rpath=.)

option(MODERN_CPU_ONLY "build for modern CPU only" ON)

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-unroll-loops -fno-stack-protector")
if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
	if (MODERN_CPU_ONLY)
		message("NOTICE: build for modern CPU not older than skylake")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=skylake")
		#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mpopcnt")
    endif()
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --param case-values-threshold=3")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --param max-inline-insns-size=64")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --param large-function-insns=5000")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --param large-function-growth=200")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --param large-unit-insns=30000")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --param inline-unit-growth=100")
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

file(GLOB source
    src/*.cc
)

add_library(shd SHARED ${source})
target_link_libraries(shd pthread)

file(GLOB test_src
    test/*.cc
)

add_executable(shd-test ${test_src})
target_link_libraries(shd-test pthread gtest shd)

add_executable(bench-billion benchmark/billion.cc)
target_link_libraries(bench-billion pthread gflags shd)
