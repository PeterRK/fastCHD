//==============================================================================
// Skew Hash and Displace Algorithm - CUDA query tests.
// Copyright (C) 2020  Ruan Kunliang
//
// This library is free software; you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the Free
// Software Foundation; either version 2.1 of the License, or (at your option)
// any later version.
//==============================================================================

#include <cuda_runtime.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include <shd.h>
#include <shd_cuda.h>
#include "../src/cuda/shd.cuh"
#include "test.h"

namespace {

constexpr unsigned SEGMENTS = 3;
constexpr unsigned ITEMS_PER_SEGMENT = 1024;
constexpr unsigned ITEM_COUNT = SEGMENTS * ITEMS_PER_SEGMENT;

class VectorWriter final : public shd::IDataWriter {
public:
	bool operator!() const noexcept override {
		return false;
	}

	bool flush() noexcept override {
		return true;
	}

	bool write(const void* data, size_t size) noexcept override {
		try {
			const auto* first = static_cast<const uint8_t*>(data);
			bytes.insert(bytes.end(), first, first + size);
			return true;
		} catch (...) {
			return false;
		}
	}

	std::vector<uint8_t> bytes;
};

template <typename T>
class DeviceBuffer {
public:
	DeviceBuffer() = default;
	~DeviceBuffer() {
		if (m_data != nullptr) {
			cudaFree(m_data);
		}
	}
	DeviceBuffer(const DeviceBuffer&) = delete;
	DeviceBuffer& operator=(const DeviceBuffer&) = delete;

	cudaError_t allocate(size_t count) {
		return cudaMalloc(reinterpret_cast<void**>(&m_data), count * sizeof(T));
	}
	T* get() const {
		return m_data;
	}

private:
	T* m_data = nullptr;
};

class Stream {
public:
	Stream() {
		m_error = cudaStreamCreateWithFlags(&m_stream, cudaStreamNonBlocking);
	}
	~Stream() {
		if (m_stream != nullptr) {
			cudaStreamDestroy(m_stream);
		}
	}
	cudaError_t error() const {
		return m_error;
	}
	cudaStream_t get() const {
		return m_stream;
	}

private:
	cudaStream_t m_stream = nullptr;
	cudaError_t m_error = cudaSuccess;
};

bool HasCudaDevice() {
	int count = 0;
	return cudaGetDeviceCount(&count) == cudaSuccess && count > 0;
}

shd::DataReaders MakeReaders() {
	shd::DataReaders readers;
	for (unsigned i = 0; i < SEGMENTS; ++i) {
		readers.push_back(std::make_unique<EmbeddingGenerator>(
			static_cast<uint64_t>(i) * ITEMS_PER_SEGMENT, ITEMS_PER_SEGMENT,
			EmbeddingGenerator::MASK0));
	}
	return readers;
}

std::unique_ptr<shd::PerfectHashtable> MakeCpuTable(
		const std::vector<uint8_t>& pack) {
	return std::make_unique<shd::PerfectHashtable>(
		pack.size(), [&pack](uint8_t* output) {
			std::memcpy(output, pack.data(), pack.size());
			return true;
		});
}

void ExpectedValue(uint64_t key, uint8_t* output) {
	const uint64_t word = key ^ EmbeddingGenerator::MASK0;
	for (unsigned i = 0; i < EmbeddingGenerator::VALUE_SIZE / sizeof(word); ++i) {
		std::memcpy(output + i * sizeof(word), &word, sizeof(word));
	}
}

void FillFixedKey(unsigned index, uint8_t length, uint8_t* output) {
	output[0] = static_cast<uint8_t>(index);
	for (unsigned i = 1; i < length; ++i) {
		output[i] = static_cast<uint8_t>(index * 131U + i * 17U + (index >> (i % 5U)));
	}
}

class FixedKeyReader final : public shd::IDataReader {
public:
	FixedKeyReader(uint8_t key_length, unsigned count)
		: m_key_length(key_length), m_count(count),
		  m_keys(static_cast<size_t>(key_length) * count) {
		for (unsigned i = 0; i < count; ++i) {
			FillFixedKey(i, key_length, m_keys.data() + static_cast<size_t>(i) * key_length);
		}
	}

	void reset() override {
		m_position = 0;
	}

	size_t total() override {
		return m_count;
	}

	shd::Record read(bool) override {
		const uint8_t* key = m_keys.data() + static_cast<size_t>(m_position++) * m_key_length;
		return {{key, m_key_length}, {}};
	}

private:
	uint8_t m_key_length;
	unsigned m_count;
	unsigned m_position = 0;
	std::vector<uint8_t> m_keys;
};

class FixedValueReader final : public shd::IDataReader {
public:
	FixedValueReader(uint16_t value_length, unsigned count)
		: m_value_length(value_length), m_count(count) {}

	void reset() override {
		m_position = 0;
	}

	size_t total() override {
		return m_count;
	}

	shd::Record read(bool key_only) override {
		m_key = m_position++;
		for (unsigned i = 0; i < m_value_length; ++i) {
			m_value[i] = static_cast<uint8_t>(m_key * 29U + i * 17U);
		}
		return {
			{reinterpret_cast<const uint8_t*>(&m_key), sizeof(m_key)},
			key_only ? shd::Slice{} : shd::Slice{m_value, m_value_length}
		};
	}

private:
	uint16_t m_value_length;
	unsigned m_count;
	unsigned m_position = 0;
	uint64_t m_key = 0;
	uint8_t m_value[65]{};
};

TEST(SHDCuda, FetchArbitraryValueLengthsAndAlignment) {
	if (!HasCudaDevice()) {
		GTEST_SKIP() << "CUDA device unavailable";
	}
	static constexpr uint16_t lengths[] = {
		1, 3, 4, 7, 8, 9, 15, 16, 31, 32, 33, 63, 65
	};
	static constexpr unsigned count = 257;
	std::vector<uint64_t> keys(count);
	for (unsigned i = 0; i < count; ++i) {
		keys[i] = i;
	}

	for (uint16_t value_length : lengths) {
		SCOPED_TRACE(value_length);
		VectorWriter writer;
		shd::DataReaders readers;
		readers.push_back(std::make_unique<FixedValueReader>(value_length, count));
		ASSERT_EQ(shd::BuildDict(readers, writer), shd::BUILD_STATUS_OK);

		DeviceBuffer<uint8_t> device_pack;
		ASSERT_EQ(device_pack.allocate(writer.bytes.size()), cudaSuccess);
		ASSERT_EQ(cudaMemcpy(device_pack.get(), writer.bytes.data(), writer.bytes.size(),
			cudaMemcpyHostToDevice), cudaSuccess);

		shd::cuda::PerfectHashtable table(device_pack.get(), writer.bytes.size());
		ASSERT_FALSE(!table);
		std::vector<uint8_t> default_value(value_length, 0);
		for (unsigned output_offset : {0U, 1U}) {
			std::vector<uint8_t> values(
				static_cast<size_t>(count) * value_length + 1U, 0);
			EXPECT_EQ(table.batch_fetch(count,
				reinterpret_cast<const uint8_t*>(keys.data()),
				values.data() + output_offset, default_value.data()), count);
			ASSERT_EQ(table.status(), shd::cuda::Status::OK);

			for (unsigned i = 0; i < count; ++i) {
				for (unsigned j = 0; j < value_length; ++j) {
					EXPECT_EQ(values[output_offset
						+ static_cast<size_t>(i) * value_length + j],
						static_cast<uint8_t>(i * 29U + j * 17U));
				}
			}
		}
	}
}

TEST(SHDCuda, HashLengthBoundariesMatchCpu) {
	if (!HasCudaDevice()) {
		GTEST_SKIP() << "CUDA device unavailable";
	}
	static constexpr uint8_t lengths[] = {
		1, 2, 3, 4, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 127, 255
	};
	static constexpr unsigned count = 67;
	Stream stream;
	ASSERT_EQ(stream.error(), cudaSuccess);

	for (uint8_t key_length : lengths) {
		SCOPED_TRACE(static_cast<unsigned>(key_length));
		VectorWriter writer;
		shd::DataReaders readers;
		readers.push_back(std::make_unique<FixedKeyReader>(key_length, count));
		ASSERT_EQ(shd::BuildSet(readers, writer), shd::BUILD_STATUS_OK);
		auto cpu = MakeCpuTable(writer.bytes);
		ASSERT_FALSE(!*cpu);

		std::vector<uint8_t> keys(static_cast<size_t>(key_length) * count);
		for (unsigned i = 0; i < count; ++i) {
			FillFixedKey(i, key_length, keys.data() + static_cast<size_t>(i) * key_length);
		}
		std::vector<uint64_t> expected(count);
		cpu->batch_locate(count, keys.data(), key_length, expected.data());

		DeviceBuffer<uint8_t> device_pack;
		ASSERT_EQ(device_pack.allocate(writer.bytes.size()), cudaSuccess);
		ASSERT_EQ(cudaMemcpy(device_pack.get(), writer.bytes.data(), writer.bytes.size(),
			cudaMemcpyHostToDevice), cudaSuccess);

		shd::cuda::PerfectHashtable table(
			device_pack.get(), writer.bytes.size(), stream.get());
		ASSERT_FALSE(!table) << "status=" << static_cast<unsigned>(table.status())
			<< " cuda_error=" << table.cuda_error();
		std::vector<uint64_t> actual(count);
		table.batch_locate(count, keys.data(), key_length, actual.data());
		ASSERT_EQ(table.status(), shd::cuda::Status::OK);
		EXPECT_EQ(actual, expected);
		auto hits = std::make_unique<bool[]>(count);
		EXPECT_EQ(table.batch_check(count, keys.data(), hits.get()), count);
		EXPECT_EQ(table.status(), shd::cuda::Status::OK);
	}
}

TEST(SHDCuda, IndexLocateMatchesCpu) {
	if (!HasCudaDevice()) {
		GTEST_SKIP() << "CUDA device unavailable";
	}
	VectorWriter writer;
	auto readers = MakeReaders();
	ASSERT_EQ(shd::BuildIndex(readers, writer), shd::BUILD_STATUS_OK);
	auto cpu = MakeCpuTable(writer.bytes);
	ASSERT_FALSE(!*cpu);

	std::vector<uint64_t> keys(ITEM_COUNT * 2U);
	for (unsigned i = 0; i < ITEM_COUNT; ++i) {
		keys[i * 2U] = i;
		keys[i * 2U + 1U] = ITEM_COUNT + i;
	}
	std::vector<uint64_t> expected(keys.size());
	cpu->batch_locate(static_cast<unsigned>(keys.size()),
		reinterpret_cast<const uint8_t*>(keys.data()), sizeof(uint64_t), expected.data());

	Stream stream;
	ASSERT_EQ(stream.error(), cudaSuccess);
	DeviceBuffer<uint8_t> device_pack;
	ASSERT_EQ(device_pack.allocate(writer.bytes.size()), cudaSuccess);
	ASSERT_EQ(cudaMemcpy(device_pack.get(), writer.bytes.data(), writer.bytes.size(),
		cudaMemcpyHostToDevice), cudaSuccess);

	shd::cuda::PerfectHashtable table(device_pack.get(), writer.bytes.size(), stream.get());
	ASSERT_FALSE(!table);
	ASSERT_EQ(table.type(), shd::cuda::PerfectHashtable::INDEX_ONLY);
	ASSERT_EQ(table.item(), ITEM_COUNT);
	std::vector<uint64_t> actual(keys.size());
	table.batch_locate(static_cast<unsigned>(keys.size()),
		reinterpret_cast<const uint8_t*>(keys.data()), sizeof(uint64_t), actual.data());
	ASSERT_EQ(table.status(), shd::cuda::Status::OK);
	EXPECT_EQ(actual, expected);
}

TEST(SHDCuda, SetCheck) {
	if (!HasCudaDevice()) {
		GTEST_SKIP() << "CUDA device unavailable";
	}
	VectorWriter writer;
	auto readers = MakeReaders();
	ASSERT_EQ(shd::BuildSet(readers, writer), shd::BUILD_STATUS_OK);

	std::vector<uint64_t> keys(ITEM_COUNT * 2U);
	for (unsigned i = 0; i < ITEM_COUNT; ++i) {
		keys[i * 2U] = i;
		keys[i * 2U + 1U] = ITEM_COUNT + i;
	}
	Stream stream;
	ASSERT_EQ(stream.error(), cudaSuccess);
	DeviceBuffer<uint8_t> device_pack;
	ASSERT_EQ(device_pack.allocate(writer.bytes.size()), cudaSuccess);
	ASSERT_EQ(cudaMemcpy(device_pack.get(), writer.bytes.data(), writer.bytes.size(),
		cudaMemcpyHostToDevice), cudaSuccess);

	shd::cuda::PerfectHashtable table(device_pack.get(), writer.bytes.size(), stream.get());
	ASSERT_FALSE(!table);
	ASSERT_EQ(table.type(), shd::cuda::PerfectHashtable::KEY_SET);
	ASSERT_EQ(table.key_len(), sizeof(uint64_t));
	auto hits = std::make_unique<bool[]>(keys.size());
	EXPECT_EQ(table.batch_check(static_cast<unsigned>(keys.size()),
		reinterpret_cast<const uint8_t*>(keys.data()), hits.get()), ITEM_COUNT);
	ASSERT_EQ(table.status(), shd::cuda::Status::OK);

	for (unsigned i = 0; i < ITEM_COUNT; ++i) {
		EXPECT_TRUE(hits[i * 2U]);
		EXPECT_FALSE(hits[i * 2U + 1U]);
	}
}

TEST(SHDCuda, InlineCheckFetchAndTryFetch) {
	if (!HasCudaDevice()) {
		GTEST_SKIP() << "CUDA device unavailable";
	}
	VectorWriter writer;
	auto readers = MakeReaders();
	ASSERT_EQ(shd::BuildDict(readers, writer), shd::BUILD_STATUS_OK);

	std::vector<uint64_t> keys(ITEM_COUNT * 2U);
	for (unsigned i = 0; i < ITEM_COUNT; ++i) {
		keys[i * 2U] = i;
		keys[i * 2U + 1U] = ITEM_COUNT + i;
	}
	const size_t value_bytes =
		keys.size() * static_cast<size_t>(EmbeddingGenerator::VALUE_SIZE);
	std::vector<uint8_t> default_value(EmbeddingGenerator::VALUE_SIZE, 0x33);

	Stream stream;
	ASSERT_EQ(stream.error(), cudaSuccess);
	DeviceBuffer<uint8_t> device_pack;
	ASSERT_EQ(device_pack.allocate(writer.bytes.size()), cudaSuccess);
	ASSERT_EQ(cudaMemcpy(device_pack.get(), writer.bytes.data(), writer.bytes.size(),
		cudaMemcpyHostToDevice), cudaSuccess);

	shd::cuda::PerfectHashtable table(device_pack.get(), writer.bytes.size(), stream.get());
	ASSERT_FALSE(!table);
	ASSERT_EQ(table.type(), shd::cuda::PerfectHashtable::KV_INLINE);
	ASSERT_EQ(table.val_len(), EmbeddingGenerator::VALUE_SIZE);
	auto hits = std::make_unique<bool[]>(keys.size());
	EXPECT_EQ(table.batch_check(static_cast<unsigned>(keys.size()),
		reinterpret_cast<const uint8_t*>(keys.data()), hits.get()), ITEM_COUNT);

	std::vector<uint8_t> values(value_bytes, 0x5a);
	EXPECT_EQ(table.batch_fetch(static_cast<unsigned>(keys.size()),
		reinterpret_cast<const uint8_t*>(keys.data()), values.data(),
		default_value.data()), ITEM_COUNT);
	uint8_t expected[EmbeddingGenerator::VALUE_SIZE];
	for (unsigned i = 0; i < ITEM_COUNT; ++i) {
		ExpectedValue(i, expected);
		EXPECT_EQ(std::memcmp(values.data() + i * 2U * EmbeddingGenerator::VALUE_SIZE,
			expected, sizeof(expected)), 0);
		EXPECT_EQ(std::memcmp(values.data() + (i * 2U + 1U) * EmbeddingGenerator::VALUE_SIZE,
			default_value.data(), default_value.size()), 0);
	}

	std::fill(values.begin(), values.end(), 0x5a);
	EXPECT_EQ(table.batch_fetch(static_cast<unsigned>(keys.size()),
		reinterpret_cast<const uint8_t*>(keys.data()), values.data(), nullptr), 0U);
	EXPECT_EQ(table.status(), shd::cuda::Status::INVALID_ARGUMENT);
	EXPECT_TRUE(std::all_of(values.begin(), values.end(),
		[](uint8_t value) { return value == 0x5a; }));

	std::fill(values.begin(), values.end(), 0x7c);
	std::vector<unsigned> misses(keys.size());
	EXPECT_EQ(table.batch_try_fetch(static_cast<unsigned>(keys.size()),
		reinterpret_cast<const uint8_t*>(keys.data()), values.data(),
		misses.data()), ITEM_COUNT);
	for (unsigned i = 0; i < ITEM_COUNT; ++i) {
		EXPECT_EQ(misses[i], i * 2U + 1U);
	}
	for (unsigned i = 0; i < ITEM_COUNT; ++i) {
		ExpectedValue(i, expected);
		EXPECT_EQ(std::memcmp(values.data() + i * 2U * EmbeddingGenerator::VALUE_SIZE,
			expected, sizeof(expected)), 0);
	}
	EXPECT_EQ(table.status(), shd::cuda::Status::OK);
}

TEST(SHDCuda, ConcurrentStreamsSharePack) {
	if (!HasCudaDevice()) {
		GTEST_SKIP() << "CUDA device unavailable";
	}
	VectorWriter writer;
	auto readers = MakeReaders();
	ASSERT_EQ(shd::BuildSet(readers, writer), shd::BUILD_STATUS_OK);
	std::vector<uint64_t> keys(ITEM_COUNT);
	for (unsigned i = 0; i < ITEM_COUNT; ++i) {
		keys[i] = i;
	}

	Stream first_stream;
	Stream second_stream;
	ASSERT_EQ(first_stream.error(), cudaSuccess);
	ASSERT_EQ(second_stream.error(), cudaSuccess);
	DeviceBuffer<uint8_t> device_pack;
	ASSERT_EQ(device_pack.allocate(writer.bytes.size()), cudaSuccess);
	ASSERT_EQ(cudaMemcpy(device_pack.get(), writer.bytes.data(), writer.bytes.size(),
		cudaMemcpyHostToDevice), cudaSuccess);

	shd::cuda::PerfectHashtable first(
		device_pack.get(), writer.bytes.size(), first_stream.get());
	shd::cuda::PerfectHashtable second(
		device_pack.get(), writer.bytes.size(), second_stream.get());
	ASSERT_FALSE(!first);
	ASSERT_FALSE(!second);
	unsigned first_hits = 0;
	unsigned second_hits = 0;
	auto first_output = std::make_unique<bool[]>(keys.size());
	auto second_output = std::make_unique<bool[]>(keys.size());
	std::thread first_thread([&] {
		first_hits = first.batch_check(ITEM_COUNT,
			reinterpret_cast<const uint8_t*>(keys.data()), first_output.get());
	});
	std::thread second_thread([&] {
		second_hits = second.batch_check(ITEM_COUNT,
			reinterpret_cast<const uint8_t*>(keys.data()), second_output.get());
	});
	first_thread.join();
	second_thread.join();
	EXPECT_EQ(first_hits, ITEM_COUNT);
	EXPECT_EQ(second_hits, ITEM_COUNT);
	EXPECT_EQ(first.status(), shd::cuda::Status::OK);
	EXPECT_EQ(second.status(), shd::cuda::Status::OK);
}

TEST(SHDCuda, InternalTableSupportsConcurrentStreams) {
	if (!HasCudaDevice()) {
		GTEST_SKIP() << "CUDA device unavailable";
	}
	VectorWriter writer;
	auto readers = MakeReaders();
	ASSERT_EQ(shd::BuildSet(readers, writer), shd::BUILD_STATUS_OK);
	std::vector<uint64_t> keys(ITEM_COUNT);
	for (unsigned i = 0; i < ITEM_COUNT; ++i) {
		keys[i] = i;
	}

	Stream first_stream;
	Stream second_stream;
	ASSERT_EQ(first_stream.error(), cudaSuccess);
	ASSERT_EQ(second_stream.error(), cudaSuccess);
	DeviceBuffer<uint8_t> device_pack;
	DeviceBuffer<uint64_t> device_keys;
	DeviceBuffer<bool> first_output;
	DeviceBuffer<bool> second_output;
	DeviceBuffer<unsigned> first_count;
	DeviceBuffer<unsigned> second_count;
	ASSERT_EQ(device_pack.allocate(writer.bytes.size()), cudaSuccess);
	ASSERT_EQ(device_keys.allocate(keys.size()), cudaSuccess);
	ASSERT_EQ(first_output.allocate(keys.size()), cudaSuccess);
	ASSERT_EQ(second_output.allocate(keys.size()), cudaSuccess);
	ASSERT_EQ(first_count.allocate(1), cudaSuccess);
	ASSERT_EQ(second_count.allocate(1), cudaSuccess);
	ASSERT_EQ(cudaMemcpy(device_pack.get(), writer.bytes.data(), writer.bytes.size(),
		cudaMemcpyHostToDevice), cudaSuccess);
	ASSERT_EQ(cudaMemcpy(device_keys.get(), keys.data(), keys.size() * sizeof(uint64_t),
		cudaMemcpyHostToDevice), cudaSuccess);

	shd::cuda::detail::Table* raw_table = nullptr;
	const auto attach = shd::cuda::detail::Attach(
		device_pack.get(), writer.bytes.size(), first_stream.get(), &raw_table);
	ASSERT_EQ(attach.status, shd::cuda::Status::OK);
	ASSERT_EQ(attach.cuda_error, cudaSuccess);
	std::unique_ptr<shd::cuda::detail::Table,
		decltype(&shd::cuda::detail::Detach)> table(raw_table, &shd::cuda::detail::Detach);
	const auto info = shd::cuda::detail::GetTableInfo(table.get());
	EXPECT_EQ(info.type, shd::cuda::PerfectHashtable::KEY_SET);
	EXPECT_EQ(info.key_length, sizeof(uint64_t));
	EXPECT_EQ(info.item_count, ITEM_COUNT);

	const auto first = shd::cuda::detail::BatchCheckAsync(table.get(), ITEM_COUNT,
		reinterpret_cast<const uint8_t*>(device_keys.get()), first_output.get(),
		first_count.get(), first_stream.get());
	const auto second = shd::cuda::detail::BatchCheckAsync(table.get(), ITEM_COUNT,
		reinterpret_cast<const uint8_t*>(device_keys.get()), second_output.get(),
		second_count.get(), second_stream.get());
	ASSERT_EQ(first.status, shd::cuda::Status::OK);
	ASSERT_EQ(second.status, shd::cuda::Status::OK);
	ASSERT_EQ(cudaStreamSynchronize(first_stream.get()), cudaSuccess);
	ASSERT_EQ(cudaStreamSynchronize(second_stream.get()), cudaSuccess);
	unsigned first_hits = 0;
	unsigned second_hits = 0;
	ASSERT_EQ(cudaMemcpy(&first_hits, first_count.get(), sizeof(first_hits),
		cudaMemcpyDeviceToHost), cudaSuccess);
	ASSERT_EQ(cudaMemcpy(&second_hits, second_count.get(), sizeof(second_hits),
		cudaMemcpyDeviceToHost), cudaSuccess);
	EXPECT_EQ(first_hits, ITEM_COUNT);
	EXPECT_EQ(second_hits, ITEM_COUNT);
}

TEST(SHDCuda, InternalTryFetchCompactsMisses) {
	if (!HasCudaDevice()) {
		GTEST_SKIP() << "CUDA device unavailable";
	}
	VectorWriter writer;
	auto readers = MakeReaders();
	ASSERT_EQ(shd::BuildDict(readers, writer), shd::BUILD_STATUS_OK);

	std::vector<uint64_t> keys(ITEM_COUNT * 2U);
	for (unsigned i = 0; i < ITEM_COUNT; ++i) {
		keys[i * 2U] = i;
		keys[i * 2U + 1U] = ITEM_COUNT + i;
	}
	const unsigned batch = static_cast<unsigned>(keys.size());
	const size_t value_bytes =
		static_cast<size_t>(batch) * EmbeddingGenerator::VALUE_SIZE;

	Stream stream;
	ASSERT_EQ(stream.error(), cudaSuccess);
	DeviceBuffer<uint8_t> device_pack;
	DeviceBuffer<uint64_t> device_keys;
	DeviceBuffer<uint8_t> device_values;
	DeviceBuffer<unsigned> device_misses;
	DeviceBuffer<unsigned> device_miss_count;
	ASSERT_EQ(device_pack.allocate(writer.bytes.size()), cudaSuccess);
	ASSERT_EQ(device_keys.allocate(keys.size()), cudaSuccess);
	ASSERT_EQ(device_values.allocate(value_bytes), cudaSuccess);
	ASSERT_EQ(device_misses.allocate(batch), cudaSuccess);
	ASSERT_EQ(device_miss_count.allocate(1), cudaSuccess);
	ASSERT_EQ(cudaMemcpy(device_pack.get(), writer.bytes.data(), writer.bytes.size(),
		cudaMemcpyHostToDevice), cudaSuccess);
	ASSERT_EQ(cudaMemcpy(device_keys.get(), keys.data(), keys.size() * sizeof(uint64_t),
		cudaMemcpyHostToDevice), cudaSuccess);

	shd::cuda::detail::Table* raw_table = nullptr;
	const auto attach = shd::cuda::detail::Attach(
		device_pack.get(), writer.bytes.size(), stream.get(), &raw_table);
	ASSERT_EQ(attach.status, shd::cuda::Status::OK);
	std::unique_ptr<shd::cuda::detail::Table,
		decltype(&shd::cuda::detail::Detach)> table(
		raw_table, &shd::cuda::detail::Detach);
	shd::cuda::detail::TryFetchWorkspace* raw_workspace = nullptr;
	const auto create = shd::cuda::detail::CreateTryFetchWorkspace(
		batch, stream.get(), &raw_workspace);
	ASSERT_EQ(create.status, shd::cuda::Status::OK);
	std::unique_ptr<shd::cuda::detail::TryFetchWorkspace,
		decltype(&shd::cuda::detail::DestroyTryFetchWorkspace)> workspace(
		raw_workspace, &shd::cuda::detail::DestroyTryFetchWorkspace);

	// Miss value slots are intentionally unspecified; initialize them so
	// initcheck can validate the defined hit slots without expected noise.
	ASSERT_EQ(cudaMemsetAsync(device_values.get(), 0xa5, value_bytes, stream.get()),
		cudaSuccess);
	const auto fetch = shd::cuda::detail::BatchTryFetchAsync(table.get(),
		workspace.get(), batch,
		reinterpret_cast<const uint8_t*>(device_keys.get()), device_values.get(),
		device_misses.get(), device_miss_count.get(), stream.get());
	ASSERT_EQ(fetch.status, shd::cuda::Status::OK);
	ASSERT_EQ(cudaStreamSynchronize(stream.get()), cudaSuccess);

	unsigned miss_count = 0;
	ASSERT_EQ(cudaMemcpy(&miss_count, device_miss_count.get(), sizeof(miss_count),
		cudaMemcpyDeviceToHost), cudaSuccess);
	ASSERT_EQ(miss_count, ITEM_COUNT);
	std::vector<unsigned> misses(miss_count);
	ASSERT_EQ(cudaMemcpy(misses.data(), device_misses.get(),
		misses.size() * sizeof(unsigned), cudaMemcpyDeviceToHost), cudaSuccess);
	for (unsigned i = 0; i < miss_count; ++i) EXPECT_EQ(misses[i], i * 2U + 1U);

	std::vector<uint8_t> values(value_bytes);
	ASSERT_EQ(cudaMemcpy(values.data(), device_values.get(), value_bytes,
		cudaMemcpyDeviceToHost), cudaSuccess);
	uint8_t expected[EmbeddingGenerator::VALUE_SIZE];
	for (unsigned i = 0; i < ITEM_COUNT; ++i) {
		ExpectedValue(i, expected);
		EXPECT_EQ(std::memcmp(values.data()
			+ static_cast<size_t>(i * 2U) * EmbeddingGenerator::VALUE_SIZE,
			expected, sizeof(expected)), 0);
	}
}

TEST(SHDCuda, RejectsSeparatedValues) {
	if (!HasCudaDevice()) {
		GTEST_SKIP() << "CUDA device unavailable";
	}
	VectorWriter writer;
	shd::DataReaders readers;
	readers.push_back(std::make_unique<VariedValueGenerator>(0, 32, 5U));
	ASSERT_EQ(shd::BuildDictWithVariedValue(readers, writer), shd::BUILD_STATUS_OK);
	DeviceBuffer<uint8_t> device_pack;
	ASSERT_EQ(device_pack.allocate(writer.bytes.size()), cudaSuccess);
	ASSERT_EQ(cudaMemcpy(device_pack.get(), writer.bytes.data(), writer.bytes.size(),
		cudaMemcpyHostToDevice), cudaSuccess);

	shd::cuda::PerfectHashtable table(device_pack.get(), writer.bytes.size());
	EXPECT_TRUE(!table);
	EXPECT_EQ(table.status(), shd::cuda::Status::UNSUPPORTED_TYPE);
}

} // namespace
