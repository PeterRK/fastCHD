//==============================================================================
// A modern implement of CHD algorithm.
// Copyright (C) 2020  Ruan Kunliang
//
// This library is free software; you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the Free
// Software Foundation; either version 2.1 of the License, or (at your option)
// any later version.
//
// This library is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
// details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with the This Library; if not, see <https://www.gnu.org/licenses/>.
//==============================================================================

#include <cstring>
#include <iostream>
#include <algorithm>
#include <thread>
#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <sys/sysinfo.h>
#include <chd.h>
#include <gflags/gflags.h>
#include "benchmark.h"

DEFINE_string(file, "bench.chd", "dict filename");
DEFINE_uint32(thread, 4, "number of worker threads");
DEFINE_bool(build, false, "build instead of fetching");
DEFINE_bool(copy, false, "load by copy");

static constexpr size_t BILLION = 1UL << 30U;

static int BenchBuild() {
	chd::FileWriter output(FLAGS_file.c_str());
	if (!output) {
		std::cout << "fail to create output file" << std::endl;
		return -1;
	}
	const size_t piece = BILLION/FLAGS_thread;
	const size_t remain = BILLION%FLAGS_thread;
	std::vector<std::unique_ptr<chd::IDataReader>> input;
	input.reserve(FLAGS_thread);
	size_t off = 0;
	for (unsigned i = 0; i < FLAGS_thread; i++) {
		auto sz = i<remain? piece+1 : piece;
		input.push_back(std::make_unique<EmbeddingGenerator>(off, sz));
		off += sz;
	}

	chd::g_trace_build_time = true;

	auto start = std::chrono::steady_clock::now();
	auto ret = BuildDict(input, output);
	if (ret != chd::BUILD_STATUS_OK) {
		std::cout << "fail to build: " << ret << std::endl;
		return 2;
	}
	auto end = std::chrono::steady_clock::now();

	std::cout << std::chrono::duration_cast<std::chrono::seconds>(end - start).count() << "s" << std::endl;
	return 0;
}

static int BenchFetch() {
	chd::PerfectHashtable dict(FLAGS_file, FLAGS_copy ? chd::PerfectHashtable::COPY_DATA : chd::PerfectHashtable::MAP_FETCH);
	if (!dict) {
		std::cout << "fail to load: " << FLAGS_file << std::endl;
		return -1;
	}
	if (dict.item() != BILLION) {
		std::cout << "need billion dict" << std::endl;
		return 1;
	}

	const unsigned n = FLAGS_thread;
	constexpr unsigned batch = 5000;
	constexpr unsigned loop = 1000;

	std::vector<std::thread> workers;
	workers.reserve(n);
	std::vector<uint64_t> results(n);

	for (unsigned i = 0; i < n; i++) {
		workers.emplace_back([&dict](uint64_t* res){
			std::vector<uint64_t> key_vec(batch);
			auto out = std::make_unique<uint8_t[]>(EmbeddingGenerator::VALUE_SIZE*batch);

			XorShift128Plus rnd;
			uint64_t sum_ns = 0;
			for (unsigned i = 0; i < loop; i++) {
				for (unsigned j = 0; j < batch; j++) {
					key_vec[j] = rnd()%BILLION;
				}
				auto start = std::chrono::steady_clock::now();
				dict.batch_fetch(batch, (const uint8_t*)key_vec.data(), out.get());
				auto end = std::chrono::steady_clock::now();
				sum_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
			}
			*res = sum_ns;
		}, &results[i]);
	}
	for (auto& t : workers) {
		t.join();
	}

	uint64_t qps = 0;
	uint64_t ns = 0;
	for (auto x : results) {
		qps += (loop*batch)*1000000000ULL/x;
		ns += x;
	}
	ns /= n*(uint64_t)loop*(uint64_t)batch;

	std::cout << (qps/1000000U) << " mqps with " << n << " threads" << std::endl;
	std::cout << ns << " ns/op" << std::endl;
	return 0;
}


int main(int argc, char* argv[]) {
	google::ParseCommandLineFlags(&argc, &argv, true);

	auto cpus = get_nprocs();
	if (cpus <= 0) cpus = 1;
	if (FLAGS_thread == 0 || FLAGS_thread > cpus) {
		FLAGS_thread = cpus;
	}

	if (FLAGS_build) {
		return BenchBuild();
	} else {
		return BenchFetch();
	}
}
