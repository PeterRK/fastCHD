//==============================================================================
// Skew Hash and Displace Algorithm - CUDA host query wrapper.
// Copyright (C) 2020  Ruan Kunliang
//
// This library is free software; you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the Free
// Software Foundation; either version 2.1 of the License, or (at your option)
// any later version.
//==============================================================================

#include "shd.cuh"

#include <algorithm>
#include <climits>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace shd::cuda {

namespace {

constexpr unsigned PIPELINE_CHUNK = 32768;
constexpr size_t PIPELINE_BYTES = 4U << 20U;

detail::Result ToResult(cudaError_t error) noexcept {
	if (error == cudaSuccess) return {};
	return {error == cudaErrorMemoryAllocation ? Status::OUT_OF_MEMORY : Status::CUDA_ERROR,
		error};
}

constexpr size_t AlignUp(size_t value, size_t alignment) noexcept {
	return (value + alignment - 1U) & ~(alignment - 1U);
}

enum class Operation { LOCATE, CHECK, FETCH, TRY_FETCH };

struct Layout {
	size_t input_bytes;
	size_t output_offset;
	size_t output_bytes;
	size_t miss_offset;
	size_t count_offset;
	size_t total_bytes;
};

template <Operation OP>
Layout BatchLayout(unsigned batch, unsigned key_length, unsigned value_length) noexcept {
	const size_t keys = static_cast<size_t>(batch) * key_length;
	const size_t input = keys + (OP == Operation::FETCH ? value_length : 0U);
	const size_t output = AlignUp(input, alignof(uint64_t));
	const size_t width = OP == Operation::LOCATE ? sizeof(uint64_t)
		: OP == Operation::CHECK ? sizeof(bool) : value_length;
	const size_t bytes = static_cast<size_t>(batch) * width;
	const size_t misses = AlignUp(output + bytes, alignof(unsigned));
	const size_t count = misses + (OP == Operation::TRY_FETCH
		? static_cast<size_t>(batch) * sizeof(unsigned) : 0U);
	return {input, output, bytes, misses, count,
		OP == Operation::LOCATE ? output + bytes : count + sizeof(unsigned)};
}

// Bound spare capacity to at most 50% while amortizing incremental batch growth.
template <typename T>
T GrowCapacity(T requested, T current) noexcept {
	const T extra = std::min(current / 2, std::numeric_limits<T>::max() - current);
	return std::max(requested, static_cast<T>(current + extra));
}

// Each in-flight chunk owns its transfers, count and compaction workspace.
struct Staging {
	uint8_t* device = nullptr;
	uint8_t* host = nullptr;
	size_t capacity = 0;
	detail::TryFetchWorkspace* workspace = nullptr;
	unsigned workspace_capacity = 0;
	Layout layout{};
	unsigned begin = 0;
	unsigned count = 0;
	bool pending = false;

	~Staging() noexcept {
		detail::DestroyTryFetchWorkspace(workspace);
		if (host != nullptr) cudaFreeHost(host);
		if (device != nullptr) cudaFree(device);
	}

	detail::Result Reserve(size_t bytes, unsigned workspace_size, cudaStream_t stream) noexcept {
		if (bytes > capacity) {
			const size_t new_capacity = GrowCapacity(bytes, capacity);
			uint8_t* new_device = nullptr;
			uint8_t* new_host = nullptr;
			auto error = cudaMalloc(reinterpret_cast<void**>(&new_device), new_capacity);
			if (error == cudaSuccess) {
				error = cudaMallocHost(reinterpret_cast<void**>(&new_host), new_capacity);
			}
			if (error == cudaSuccess) {
				// Padding and unspecified try-fetch output may be copied back as well.
				error = cudaMemsetAsync(new_device, 0, new_capacity, stream);
			}
			if (error != cudaSuccess) {
				if (new_host != nullptr) cudaFreeHost(new_host);
				if (new_device != nullptr) cudaFree(new_device);
				return ToResult(error);
			}
			if (host != nullptr) cudaFreeHost(host);
			if (device != nullptr) cudaFree(device);
			host = new_host;
			device = new_device;
			capacity = new_capacity;
		}
		if (workspace_size > workspace_capacity) {
			const unsigned new_capacity = GrowCapacity(workspace_size, workspace_capacity);
			detail::TryFetchWorkspace* replacement = nullptr;
			const auto result = detail::CreateTryFetchWorkspace(new_capacity, stream, &replacement);
			if (!result) return result;
			detail::DestroyTryFetchWorkspace(workspace);
			workspace = replacement;
			workspace_capacity = new_capacity;
		}
		return {};
	}
};

} // namespace

struct PerfectHashtable::Impl {
	detail::Table* table = nullptr;
	detail::TableInfo info;
	cudaStream_t stream = nullptr;
	Status last_status = Status::INVALID_ARGUMENT;
	cudaError_t last_cuda_error = cudaSuccess;
	Staging staging[2];
	cudaStream_t auxiliary_stream = nullptr;
	cudaEvent_t ready = nullptr;

	~Impl() noexcept {
		if (ready != nullptr) cudaEventDestroy(ready);
		if (auxiliary_stream != nullptr) cudaStreamDestroy(auxiliary_stream);
		detail::Detach(table);
	}
};

namespace {

template <typename ImplT>
void StoreResult(ImplT* impl, Status status,
				 cudaError_t cuda_error = cudaSuccess) noexcept {
	if (impl != nullptr) {
		impl->last_cuda_error = cuda_error;
		impl->last_status = status;
	}
}

template <Operation OP>
detail::Result Submit(const detail::Table* table, Staging& slot, cudaStream_t stream,
		unsigned begin, unsigned count, unsigned key_length, unsigned value_length,
		const uint8_t* keys, const uint8_t* default_value) noexcept {
	slot.begin = begin;
	slot.count = count;
	slot.layout = BatchLayout<OP>(count, key_length, value_length);
	const auto& layout = slot.layout;
	const size_t key_bytes = static_cast<size_t>(count) * key_length;
	std::memcpy(slot.host, keys + static_cast<size_t>(begin) * key_length, key_bytes);
	if constexpr (OP == Operation::FETCH) {
		std::memcpy(slot.host + key_bytes, default_value, value_length);
	}
	slot.pending = true;
	auto result = ToResult(cudaMemcpyAsync(slot.device, slot.host,
		layout.input_bytes, cudaMemcpyHostToDevice, stream));
	if (result) {
		auto* output = slot.device + layout.output_offset;
		auto* counter = reinterpret_cast<unsigned*>(slot.device + layout.count_offset);
		if constexpr (OP == Operation::LOCATE) {
			result = detail::BatchLocateAsync(table, count, slot.device,
				static_cast<uint8_t>(key_length), reinterpret_cast<uint64_t*>(output), stream);
		} else if constexpr (OP == Operation::CHECK) {
			result = detail::BatchCheckAsync(table, count, slot.device,
				reinterpret_cast<bool*>(output), counter, stream);
		} else if constexpr (OP == Operation::FETCH) {
			result = detail::BatchFetchAsync(table, count, slot.device, output,
				slot.device + key_bytes, nullptr, counter, stream);
		} else {
			result = detail::BatchTryFetchAsync(table, slot.workspace, count, slot.device,
				output, reinterpret_cast<unsigned*>(slot.device + layout.miss_offset), counter, stream);
		}
	}
	if (result) {
		result = ToResult(cudaMemcpyAsync(slot.host + layout.output_offset,
			slot.device + layout.output_offset, layout.total_bytes - layout.output_offset,
			cudaMemcpyDeviceToHost, stream));
	}
	return result;
}

template <Operation OP>
detail::Result Collect(Staging& slot, cudaStream_t stream, unsigned value_length,
		void* output, unsigned* misses, unsigned& hits) noexcept {
	if (!slot.pending) return {};
	auto result = ToResult(cudaStreamSynchronize(stream));
	if (!result) return result;
	slot.pending = false;
	const auto& layout = slot.layout;
	if constexpr (OP != Operation::LOCATE) {
		unsigned count;
		std::memcpy(&count, slot.host + layout.count_offset, sizeof(count));
		if (count > slot.count) return {Status::CUDA_ERROR, cudaErrorUnknown};
		if constexpr (OP == Operation::TRY_FETCH) {
			const auto* local_misses = reinterpret_cast<const unsigned*>(slot.host + layout.miss_offset);
			// Chunks are collected in input order; translate local indices before appending.
			for (unsigned i = 0; i < count; ++i) {
				misses[slot.begin - hits + i] = slot.begin + local_misses[i];
			}
			hits += slot.count - count;
		} else {
			hits += count;
		}
	}
	const size_t width = OP == Operation::LOCATE ? sizeof(uint64_t)
		: OP == Operation::CHECK ? sizeof(bool) : value_length;
	std::memcpy(static_cast<uint8_t*>(output) + static_cast<size_t>(slot.begin) * width,
		slot.host + layout.output_offset, layout.output_bytes);
	return {};
}

template <Operation OP, typename ImplT>
unsigned RunBatch(ImplT* impl, unsigned batch, const uint8_t* keys, unsigned key_length,
		void* output, const uint8_t* default_value = nullptr, unsigned* misses = nullptr) noexcept {
	const unsigned value_length = impl->info.value_length;
	const size_t width = key_length + (OP == Operation::LOCATE ? sizeof(uint64_t)
		: OP == Operation::CHECK ? sizeof(bool) : value_length + sizeof(unsigned));
	const unsigned chunk = static_cast<unsigned>(std::min<size_t>(PIPELINE_CHUNK,
		std::max<size_t>(256U, PIPELINE_BYTES / width)));
	const unsigned lanes = batch >= 2U * chunk ? 2U : 1U;
	const unsigned capacity = lanes == 1 ? batch : chunk;
	detail::Result result;
	if (lanes == 2) {
		if (impl->auxiliary_stream == nullptr) {
			result = ToResult(cudaStreamCreateWithFlags(&impl->auxiliary_stream, cudaStreamNonBlocking));
		}
		if (result && impl->ready == nullptr) {
			result = ToResult(cudaEventCreateWithFlags(&impl->ready, cudaEventDisableTiming));
		}
	}
	const cudaStream_t streams[] = {impl->stream, impl->auxiliary_stream};
	for (unsigned i = 0; result && i < lanes; ++i) {
		result = impl->staging[i].Reserve(BatchLayout<OP>(capacity, key_length, value_length).total_bytes,
			OP == Operation::TRY_FETCH ? capacity : 0U, streams[i]);
	}
	if (result && lanes == 2) {
		// The private stream must observe all work preceding this call on the bound stream.
		result = ToResult(cudaEventRecord(impl->ready, impl->stream));
		if (result) result = ToResult(cudaStreamWaitEvent(impl->auxiliary_stream, impl->ready, 0));
	}
	unsigned hits = 0;
	unsigned lane = 0;
	for (unsigned begin = 0; result && begin < batch; lane = (lane + 1U) % lanes) {
		auto& slot = impl->staging[lane];
		result = Collect<OP>(slot, streams[lane], value_length, output, misses, hits);
		if (!result) break;
		const unsigned count = std::min(capacity, batch - begin);
		result = Submit<OP>(impl->table, slot, streams[lane], begin, count,
			key_length, value_length, keys, default_value);
		begin += count;
	}
	for (unsigned i = 0; result && i < lanes; ++i) {
		const unsigned next = (lane + i) % lanes;
		result = Collect<OP>(impl->staging[next], streams[next], value_length, output, misses, hits);
	}
	if (!result) {
		// Even a failed submission may have queued a transfer. Drain before reuse/free.
		for (unsigned i = 0; i < lanes; ++i) {
			if (i == 0 || streams[i] != nullptr) cudaStreamSynchronize(streams[i]);
			impl->staging[i].pending = false;
		}
	}
	StoreResult(impl, result.status, result.cuda_error);
	return result ? hits : 0;
}

} // namespace

PerfectHashtable::PerfectHashtable(const uint8_t* device_pack, size_t pack_size,
								   cudaStream_t stream) noexcept {
	m_impl = new (std::nothrow) Impl;
	if (m_impl == nullptr) {
		return;
	}
	m_impl->stream = stream;
	auto result = detail::Attach(device_pack, pack_size, stream, &m_impl->table);
	if (result) {
		m_impl->info = detail::GetTableInfo(m_impl->table);
	}
	StoreResult(m_impl, result.status, result.cuda_error);
}

PerfectHashtable::~PerfectHashtable() noexcept {
	delete m_impl;
}

PerfectHashtable::PerfectHashtable(PerfectHashtable&& other) noexcept
	: m_impl(std::exchange(other.m_impl, nullptr)) {}

PerfectHashtable& PerfectHashtable::operator=(PerfectHashtable&& other) noexcept {
	if (this != &other) {
		this->~PerfectHashtable();
		new (this) PerfectHashtable(std::move(other));
	}
	return *this;
}

bool PerfectHashtable::operator!() const noexcept {
	return m_impl == nullptr || m_impl->table == nullptr;
}

PerfectHashtable::Type PerfectHashtable::type() const noexcept {
	return !*this ? ILLEGAL_TYPE : m_impl->info.type;
}

uint8_t PerfectHashtable::key_len() const noexcept {
	return !*this ? 0 : m_impl->info.key_length;
}

uint16_t PerfectHashtable::val_len() const noexcept {
	return !*this ? 0 : m_impl->info.value_length;
}

size_t PerfectHashtable::item() const noexcept {
	return !*this ? 0 : static_cast<size_t>(m_impl->info.item_count);
}

cudaStream_t PerfectHashtable::stream() const noexcept {
	return m_impl == nullptr ? nullptr : m_impl->stream;
}

void PerfectHashtable::set_stream(cudaStream_t stream) noexcept {
	if (m_impl != nullptr) {
		m_impl->stream = stream;
	}
}

Status PerfectHashtable::status() const noexcept {
	return m_impl == nullptr
		? Status::OUT_OF_MEMORY
		: m_impl->last_status;
}

int PerfectHashtable::cuda_error() const noexcept {
	return m_impl == nullptr
		? static_cast<int>(cudaErrorMemoryAllocation)
		: static_cast<int>(m_impl->last_cuda_error);
}

void PerfectHashtable::batch_locate(unsigned batch, const uint8_t* keys,
									uint8_t key_length, uint64_t* output) {
	if (m_impl == nullptr || m_impl->table == nullptr) {
		return;
	}
	if (key_length == 0
		|| (m_impl->info.type != INDEX_ONLY && key_length != m_impl->info.key_length)
		|| (batch != 0 && (keys == nullptr || output == nullptr))) {
		StoreResult(m_impl, Status::INVALID_ARGUMENT);
		return;
	}
	if (batch == 0) {
		StoreResult(m_impl, Status::OK);
		return;
	}

	RunBatch<Operation::LOCATE>(m_impl, batch, keys, key_length, output);
}

unsigned PerfectHashtable::batch_check(unsigned batch, const uint8_t* keys,
									   bool* output) const noexcept {
	if (m_impl == nullptr || m_impl->table == nullptr) {
		return 0;
	}
	if (m_impl->info.type != KEY_SET && m_impl->info.type != KV_INLINE) {
		StoreResult(m_impl, Status::UNSUPPORTED_TYPE);
		return 0;
	}
	if (batch == 0) {
		StoreResult(m_impl, Status::OK);
		return 0;
	}
	if (keys == nullptr || output == nullptr) {
		StoreResult(m_impl, Status::INVALID_ARGUMENT);
		return 0;
	}

	return RunBatch<Operation::CHECK>(m_impl, batch, keys, m_impl->info.key_length, output);
}

unsigned PerfectHashtable::batch_fetch(unsigned batch, const uint8_t* keys,
									   uint8_t* data,
									   const uint8_t* default_value) const noexcept {
	if (m_impl == nullptr || m_impl->table == nullptr) {
		return 0;
	}
	if (m_impl->info.type != KV_INLINE) {
		StoreResult(m_impl, Status::UNSUPPORTED_TYPE);
		return 0;
	}
	if (batch == 0) {
		StoreResult(m_impl, Status::OK);
		return 0;
	}
	if (keys == nullptr || data == nullptr || default_value == nullptr) {
		StoreResult(m_impl, Status::INVALID_ARGUMENT);
		return 0;
	}

	return RunBatch<Operation::FETCH>(m_impl, batch, keys, m_impl->info.key_length, data, default_value);
}

unsigned PerfectHashtable::batch_try_fetch(unsigned batch, const uint8_t* keys,
										   uint8_t* data,
										   unsigned* miss) const noexcept {
	if (m_impl == nullptr || m_impl->table == nullptr) {
		return 0;
	}
	if (m_impl->info.type != KV_INLINE) {
		StoreResult(m_impl, Status::UNSUPPORTED_TYPE);
		return 0;
	}
	if (batch == 0) {
		StoreResult(m_impl, Status::OK);
		return 0;
	}
	if (batch > static_cast<unsigned>(INT_MAX) || keys == nullptr
		|| data == nullptr || miss == nullptr) {
		StoreResult(m_impl, Status::INVALID_ARGUMENT);
		return 0;
	}
	return RunBatch<Operation::TRY_FETCH>(m_impl, batch, keys, m_impl->info.key_length, data, nullptr, miss);
}

} // namespace shd::cuda
