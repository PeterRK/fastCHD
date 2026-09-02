//==============================================================================
// Skew Hash and Displace Algorithm - CUDA query implementation.
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
#include <vector>

#include <cub/device/device_select.cuh>
#include <cub/iterator/counting_input_iterator.cuh>

#include "../pack_format.h"

namespace shd::cuda::detail {
namespace {

constexpr unsigned BLOCK_SIZE = 256;
constexpr unsigned MAX_BLOCKS = 65535;
static_assert(sizeof(bool) == 1, "CUDA batch_check requires one-byte bool");

enum : uint8_t {
	DIVIDE_POWER_OF_TWO = 0,
	DIVIDE_ADD_FACTOR = 1,
	DIVIDE_ADD_NUMERATOR = 2
};

struct FastDiv32 {
	uint32_t divisor = 1;
	uint32_t factor = 0;
	uint8_t shift = 0;
	uint8_t mode = DIVIDE_POWER_OF_TWO;
	uint8_t padding[2] = {};

	__device__ __forceinline__ uint32_t div(uint32_t value) const {
		if (mode == DIVIDE_POWER_OF_TWO) {
			return value >> shift;
		}
		const uint32_t low = factor * value;
		uint32_t high = __umulhi(factor, value);
		const uint32_t addend = mode == DIVIDE_ADD_NUMERATOR ? value : factor;
		const uint32_t sum = low + addend;
		high += sum < low;
		return high >> shift;
	}

	__device__ __forceinline__ uint32_t mod(uint32_t value) const {
		return value - div(value) * divisor;
	}
};

struct FastDiv64 {
	uint64_t divisor = 1;
	uint64_t factor = 0;
	uint8_t shift = 0;
	uint8_t mode = DIVIDE_POWER_OF_TWO;
	uint8_t padding[6] = {};

	__device__ __forceinline__ uint64_t div(uint64_t value) const {
		if (mode == DIVIDE_POWER_OF_TWO) {
			return value >> shift;
		}
		const uint64_t low = factor * value;
		uint64_t high = __umul64hi(factor, value);
		const uint64_t addend = mode == DIVIDE_ADD_NUMERATOR ? value : factor;
		const uint64_t sum = low + addend;
		high += sum < low;
		return high >> shift;
	}

	__device__ __forceinline__ uint64_t mod(uint64_t value) const {
		return value - div(value) * divisor;
	}
};

FastDiv32 MakeFastDiv32(uint32_t divisor) noexcept {
	FastDiv32 out;
	out.divisor = divisor;
	unsigned shift = 0;
	for (uint32_t value = divisor; value > 1U; value >>= 1U) {
		++shift;
	}
	const uint32_t power = uint32_t{1} << shift;
	out.shift = static_cast<uint8_t>(shift);
	if (power == divisor) {
		return out;
	}
	out.factor = static_cast<uint32_t>((static_cast<uint64_t>(power) << 32U) / divisor);
	const uint32_t remainder = out.factor * divisor + divisor;
	out.mode = remainder <= power ? DIVIDE_ADD_NUMERATOR : DIVIDE_ADD_FACTOR;
	return out;
}

FastDiv64 MakeFastDiv64(uint64_t divisor) noexcept {
	FastDiv64 out;
	out.divisor = divisor;
	unsigned shift = 0;
	for (uint64_t value = divisor; value > 1U; value >>= 1U) {
		++shift;
	}
	const uint64_t power = uint64_t{1} << shift;
	out.shift = static_cast<uint8_t>(shift);
	if (power == divisor) {
		return out;
	}
	out.factor = static_cast<uint64_t>((static_cast<__uint128_t>(power) << 64U) / divisor);
	const uint64_t remainder = out.factor * divisor + divisor;
	out.mode = remainder <= power ? DIVIDE_ADD_NUMERATOR : DIVIDE_ADD_FACTOR;
	return out;
}

struct DeviceSegment {
	uint64_t cells_offset = 0;
	uint64_t sections_offset = 0;
	uint64_t item_offset = 0;
	FastDiv64 l1_band;
	FastDiv64 l2_size;
};

struct DeviceTable {
	const uint8_t* pack = nullptr;
	uint64_t pack_size = 0;
	uint64_t content_offset = 0;
	uint64_t item = 0;
	uint32_t line_size = 0;
	uint32_t seed = 0;
	uint16_t segment_count = 0;
	uint16_t value_length = 0;
	uint8_t type = PerfectHashtable::ILLEGAL_TYPE;
	uint8_t key_length = 0;
	uint8_t padding[4] = {};
	FastDiv32 l0_size;
	DeviceSegment segments[MAX_SEGMENT];
};

struct Hash128 {
	uint64_t low;
	uint64_t high;
};

struct Id96 {
	uint32_t word[3];
};

__device__ __forceinline__ uint32_t Load32(const uint8_t* p) {
	return static_cast<uint32_t>(p[0])
		| (static_cast<uint32_t>(p[1]) << 8U)
		| (static_cast<uint32_t>(p[2]) << 16U)
		| (static_cast<uint32_t>(p[3]) << 24U);
}

__device__ __forceinline__ uint64_t Load64(const uint8_t* p) {
	return static_cast<uint64_t>(Load32(p))
		| (static_cast<uint64_t>(Load32(p + 4)) << 32U);
}

__device__ __forceinline__ uint64_t RotateLeft64(uint64_t value, unsigned shift) {
	return (value << shift) | (value >> (64U - shift));
}

__device__ __forceinline__ void Mix(uint64_t& h0, uint64_t& h1,
									 uint64_t& h2, uint64_t& h3) {
	h2 = RotateLeft64(h2, 50); h2 += h3; h0 ^= h2;
	h3 = RotateLeft64(h3, 52); h3 += h0; h1 ^= h3;
	h0 = RotateLeft64(h0, 30); h0 += h1; h2 ^= h0;
	h1 = RotateLeft64(h1, 41); h1 += h2; h3 ^= h1;
	h2 = RotateLeft64(h2, 54); h2 += h3; h0 ^= h2;
	h3 = RotateLeft64(h3, 48); h3 += h0; h1 ^= h3;
	h0 = RotateLeft64(h0, 38); h0 += h1; h2 ^= h0;
	h1 = RotateLeft64(h1, 37); h1 += h2; h3 ^= h1;
	h2 = RotateLeft64(h2, 62); h2 += h3; h0 ^= h2;
	h3 = RotateLeft64(h3, 34); h3 += h0; h1 ^= h3;
	h0 = RotateLeft64(h0, 5); h0 += h1; h2 ^= h0;
	h1 = RotateLeft64(h1, 36); h1 += h2; h3 ^= h1;
}

__device__ __forceinline__ void End(uint64_t& h0, uint64_t& h1,
									 uint64_t& h2, uint64_t& h3) {
	h3 ^= h2; h2 = RotateLeft64(h2, 15); h3 += h2;
	h0 ^= h3; h3 = RotateLeft64(h3, 52); h0 += h3;
	h1 ^= h0; h0 = RotateLeft64(h0, 26); h1 += h0;
	h2 ^= h1; h1 = RotateLeft64(h1, 51); h2 += h1;
	h3 ^= h2; h2 = RotateLeft64(h2, 28); h3 += h2;
	h0 ^= h3; h3 = RotateLeft64(h3, 9); h0 += h3;
	h1 ^= h0; h0 = RotateLeft64(h0, 47); h1 += h0;
	h2 ^= h1; h1 = RotateLeft64(h1, 54); h2 += h1;
	h3 ^= h2; h2 = RotateLeft64(h2, 32); h3 += h2;
	h0 ^= h3; h3 = RotateLeft64(h3, 25); h0 += h3;
	h1 ^= h0; h0 = RotateLeft64(h0, 63); h1 += h0;
}

__device__ __forceinline__ Hash128 HashTo128(const uint8_t* message,
										   unsigned length, uint64_t seed) {
	constexpr uint64_t magic = 0xdeadbeefdeadbeefULL;
	uint64_t a = seed;
	uint64_t b = seed;
	uint64_t c = magic;
	uint64_t d = magic;

	unsigned offset = 0;
	const unsigned block_end = length & ~0x1fU;
	for (; offset < block_end; offset += 32U) {
		c += Load64(message + offset);
		d += Load64(message + offset + 8U);
		Mix(a, b, c, d);
		a += Load64(message + offset + 16U);
		b += Load64(message + offset + 24U);
	}

	if (length & 0x10U) {
		c += Load64(message + offset);
		d += Load64(message + offset + 8U);
		Mix(a, b, c, d);
		offset += 16U;
	}

	d += static_cast<uint64_t>(length) << 56U;
	const unsigned tail = length & 0xfU;
	if (tail == 0) {
		c += magic;
		d += magic;
	} else {
		const unsigned c_bytes = tail < 8U ? tail : 8U;
		for (unsigned i = 0; i < c_bytes; ++i) {
			c += static_cast<uint64_t>(message[offset + i]) << (i * 8U);
		}
		for (unsigned i = 8U; i < tail; ++i) {
			d += static_cast<uint64_t>(message[offset + i]) << ((i - 8U) * 8U);
		}
	}
	End(a, b, c, d);
	return {a, b};
}

__device__ __forceinline__ Hash128 Hash8To128(const uint8_t* message,
											 uint64_t seed) {
	constexpr uint64_t magic = 0xdeadbeefdeadbeefULL;
	uint64_t a = seed;
	uint64_t b = seed;
	uint64_t c = magic + Load64(message);
	uint64_t d = magic + (uint64_t{8} << 56U);
	End(a, b, c, d);
	return {a, b};
}

template <unsigned FIXED_KEY_LENGTH>
__device__ __forceinline__ Id96 GenerateId(const DeviceTable& table,
										 const uint8_t* key, uint8_t key_length) {
	Hash128 hash;
	if constexpr (FIXED_KEY_LENGTH == 8) {
		hash = Hash8To128(key, table.seed);
	} else {
		hash = HashTo128(key, key_length, table.seed);
	}
	return {{
		static_cast<uint32_t>(hash.low),
		static_cast<uint32_t>(hash.low >> 32U),
		static_cast<uint32_t>(hash.high)
	}};
}

__device__ __forceinline__ uint64_t L2Hash(const Id96& id, uint8_t displacement) {
	const uint32_t seed = (static_cast<uint32_t>(displacement) + 1U) * 0xff00ffU;
	const uint64_t low = static_cast<uint64_t>(id.word[0])
		| (static_cast<uint64_t>(id.word[1]) << 32U);
	const uint64_t high = static_cast<uint64_t>(id.word[2])
		| (static_cast<uint64_t>(seed) << 32U);
	return low ^ high;
}

template <unsigned FIXED_KEY_LENGTH>
__device__ __forceinline__ uint64_t LocateOne(const DeviceTable& table,
										const uint8_t* key, uint8_t key_length) {
	const Id96 id = GenerateId<FIXED_KEY_LENGTH>(table, key, key_length);
	const uint32_t segment_index =
		table.l0_size.mod(static_cast<uint16_t>(id.word[0]));
	const DeviceSegment& segment = table.segments[segment_index];

	const uint64_t x = id.word[1] & L1H_MAX;
	const uint64_t numerator = x * (x + L1TIP);
	const uint64_t cell_index = segment.l1_band.div(numerator);
	const uint8_t displacement = table.pack[segment.cells_offset + cell_index];
	const uint64_t bit_position = segment.l2_size.mod(L2Hash(id, displacement));
	const uint64_t section_index = bit_position / BITMAP_SECTION_SIZE;
	const unsigned bit_offset = static_cast<unsigned>(bit_position % BITMAP_SECTION_SIZE);
	const auto* section = reinterpret_cast<const BitmapSection*>(
		table.pack + segment.sections_offset) + section_index;

	uint32_t rank = section->step;
	const unsigned complete_words = bit_offset >> 5U;
	for (unsigned i = 0; i < complete_words; ++i) {
		rank += __popc(section->b32[i]);
	}
	const unsigned remaining = bit_offset & 31U;
	if (remaining != 0) {
		const uint32_t mask = (uint32_t{1} << remaining) - 1U;
		rank += __popc(section->b32[complete_words] & mask);
	}
	return segment.item_offset + rank;
}

__device__ __forceinline__ bool EqualBytes(const uint8_t* a, const uint8_t* b,
										  uint8_t length) {
	for (unsigned i = 0; i < length; ++i) {
		if (a[i] != b[i]) {
			return false;
		}
	}
	return true;
}

template <unsigned FIXED_KEY_LENGTH>
__device__ __forceinline__ bool EqualKey(const uint8_t* a, const uint8_t* b,
										 uint8_t key_length) {
	if constexpr (FIXED_KEY_LENGTH == 8) {
		return Load64(a) == Load64(b);
	} else {
		return EqualBytes(a, b, key_length);
	}
}

__device__ __forceinline__ void CopyBytes(uint8_t* __restrict destination,
										 const uint8_t* __restrict source,
										 uint16_t length) {
	unsigned offset = 0;
	const uintptr_t alignment =
		reinterpret_cast<uintptr_t>(destination)
		| reinterpret_cast<uintptr_t>(source);
	if ((alignment & (alignof(uint64_t) - 1U)) == 0) {
		for (; offset + sizeof(uint64_t) <= length; offset += sizeof(uint64_t)) {
			*reinterpret_cast<uint64_t*>(destination + offset) =
				*reinterpret_cast<const uint64_t*>(source + offset);
		}
	} else if ((alignment & (alignof(uint32_t) - 1U)) == 0) {
		for (; offset + sizeof(uint32_t) <= length; offset += sizeof(uint32_t)) {
			*reinterpret_cast<uint32_t*>(destination + offset) =
				*reinterpret_cast<const uint32_t*>(source + offset);
		}
	}
	for (; offset < length; ++offset) {
		destination[offset] = source[offset];
	}
}

__device__ __forceinline__ void AddBlockHits(unsigned local_hits,
										 unsigned* block_hits,
										 unsigned* global_hits) {
	block_hits[threadIdx.x] = local_hits;
	__syncthreads();
	for (unsigned stride = BLOCK_SIZE / 2U; stride != 0; stride >>= 1U) {
		if (threadIdx.x < stride) {
			block_hits[threadIdx.x] += block_hits[threadIdx.x + stride];
		}
		__syncthreads();
	}
	if (threadIdx.x == 0) {
		atomicAdd(global_hits, block_hits[0]);
	}
}

template <unsigned FIXED_KEY_LENGTH>
__global__ void LocateKernel(const DeviceTable* table, unsigned batch,
							 const uint8_t* keys, uint8_t key_length, uint64_t* output) {
	const unsigned length =
		FIXED_KEY_LENGTH == 0 ? key_length : FIXED_KEY_LENGTH;
	const uint64_t stride = static_cast<uint64_t>(blockDim.x) * gridDim.x;
	for (uint64_t i = static_cast<uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
		 i < batch; i += stride) {
		output[i] = LocateOne<FIXED_KEY_LENGTH>(
			*table, keys + i * length, static_cast<uint8_t>(length));
	}
}

template <unsigned FIXED_KEY_LENGTH, bool COUNT_HITS>
__global__ void CheckKernel(const DeviceTable* table, unsigned batch,
							const uint8_t* keys, bool* output, unsigned* hit_count) {
	unsigned local_hits = 0;
	const unsigned key_length =
		FIXED_KEY_LENGTH == 0 ? table->key_length : FIXED_KEY_LENGTH;
	const uint64_t stride = static_cast<uint64_t>(blockDim.x) * gridDim.x;
	for (uint64_t i = static_cast<uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
		 i < batch; i += stride) {
		const uint8_t* key = keys + i * key_length;
		const uint64_t position = LocateOne<FIXED_KEY_LENGTH>(
			*table, key, static_cast<uint8_t>(key_length));
		bool hit = false;
		if (position < table->item) {
			const uint8_t* line =
				table->pack + table->content_offset + position * table->line_size;
			hit = EqualKey<FIXED_KEY_LENGTH>(
				key, line, static_cast<uint8_t>(key_length));
		}
		output[i] = hit;
		if constexpr (COUNT_HITS) {
			local_hits += hit;
		}
	}
	if constexpr (COUNT_HITS) {
		__shared__ unsigned block_hits[BLOCK_SIZE];
		AddBlockHits(local_hits, block_hits, hit_count);
	}
}

template <unsigned FIXED_KEY_LENGTH, bool COUNT_HITS, bool FLAGS_ARE_MISSES>
__global__ void FetchKernel(const DeviceTable* table, unsigned batch,
							const uint8_t* keys, uint8_t* data,
							const uint8_t* default_value, bool* flags,
							unsigned* hit_count) {
	unsigned local_hits = 0;
	const unsigned key_length =
		FIXED_KEY_LENGTH == 0 ? table->key_length : FIXED_KEY_LENGTH;
	const uint64_t stride = static_cast<uint64_t>(blockDim.x) * gridDim.x;
	for (uint64_t i = static_cast<uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
		 i < batch; i += stride) {
		const uint8_t* key = keys + i * key_length;
		const uint64_t position = LocateOne<FIXED_KEY_LENGTH>(
			*table, key, static_cast<uint8_t>(key_length));
		const uint8_t* source = nullptr;
		bool hit = false;
		if (position < table->item) {
			const uint8_t* line =
				table->pack + table->content_offset + position * table->line_size;
			if (EqualKey<FIXED_KEY_LENGTH>(
					key, line, static_cast<uint8_t>(key_length))) {
				hit = true;
				source = line + key_length;
			}
		}
		if (flags != nullptr) {
			flags[i] = FLAGS_ARE_MISSES ? !hit : hit;
		}
		if (!hit) {
			source = default_value;
		}
		if (source != nullptr) {
			CopyBytes(data + i * table->value_length, source, table->value_length);
		}
		if constexpr (COUNT_HITS) {
			local_hits += hit;
		}
	}
	if constexpr (COUNT_HITS) {
		__shared__ unsigned block_hits[BLOCK_SIZE];
		AddBlockHits(local_hits, block_hits, hit_count);
	}
}

unsigned GridSize(unsigned batch) noexcept {
	const uint64_t required =
		(static_cast<uint64_t>(batch) + BLOCK_SIZE - 1U) / BLOCK_SIZE;
	return static_cast<unsigned>(std::min<uint64_t>(required, MAX_BLOCKS));
}

Result CudaResult(cudaError_t error) noexcept {
	if (error == cudaSuccess) {
		return {};
	}
	return {
		error == cudaErrorMemoryAllocation ? Status::OUT_OF_MEMORY : Status::CUDA_ERROR,
		error
	};
}

bool Advance(size_t& offset, size_t amount, size_t total) noexcept {
	if (offset > total || amount > total - offset) {
		return false;
	}
	offset += amount;
	return true;
}

Result LaunchResult() noexcept {
	return CudaResult(cudaPeekAtLastError());
}

} // namespace

struct Table {
	DeviceTable* device_view = nullptr;
	TableInfo info;
};

struct TryFetchWorkspace {
	bool* miss_flags = nullptr;
	void* select_temp = nullptr;
	size_t select_temp_bytes = 0;
	unsigned capacity = 0;
};

Result Attach(const uint8_t* device_pack, size_t pack_size, cudaStream_t stream,
			  Table** output) noexcept {
	if (output != nullptr) {
		*output = nullptr;
	}
	if (device_pack == nullptr || output == nullptr || pack_size < sizeof(Header)) {
		return {Status::INVALID_ARGUMENT};
	}

	std::vector<uint8_t> prefix;
	try {
		const size_t prefix_size = std::min(
			pack_size, sizeof(Header) + static_cast<size_t>(MAX_SEGMENT) * sizeof(uint32_t));
		prefix.resize(prefix_size);
	} catch (...) {
		return {Status::OUT_OF_MEMORY};
	}

	// Attach is synchronous. Complete any pack upload queued on the bound stream
	// before reading its prefix into ordinary pageable host memory.
	auto error = cudaStreamSynchronize(stream);
	if (error != cudaSuccess) {
		return CudaResult(error);
	}
	error = cudaMemcpy(prefix.data(), device_pack, prefix.size(),
					   cudaMemcpyDeviceToHost);
	if (error != cudaSuccess) {
		return CudaResult(error);
	}

	Header header;
	std::memcpy(&header, prefix.data(), sizeof(header));
	if (header.magic != SHD_MAGIC) {
		return {Status::INVALID_PACK};
	}
	if (header.type == PerfectHashtable::KV_SEPARATED) {
		return {Status::UNSUPPORTED_TYPE};
	}
	if (header.type > PerfectHashtable::KV_INLINE) {
		return {Status::INVALID_PACK};
	}
	if ((header.type == PerfectHashtable::KEY_SET && header.key_len == 0)
		|| (header.type == PerfectHashtable::KV_INLINE
			&& (header.key_len == 0 || header.val_len == 0))) {
		return {Status::INVALID_PACK};
	}
	if (header.seg_cnt == 0 || header.seg_cnt > MAX_SEGMENT) {
		return {Status::INVALID_PACK};
	}
	const size_t parts_bytes = static_cast<size_t>(header.seg_cnt) * sizeof(uint32_t);
	if (prefix.size() < sizeof(Header) + parts_bytes) {
		return {Status::INVALID_PACK};
	}

	DeviceTable host_view;
	host_view.pack = device_pack;
	host_view.pack_size = pack_size;
	host_view.type = header.type;
	host_view.key_length = header.key_len;
	host_view.value_length = header.val_len;
	host_view.line_size = static_cast<uint32_t>(header.key_len) + header.val_len;
	host_view.seed = header.seed;
	host_view.segment_count = header.seg_cnt;
	host_view.item = (static_cast<uint64_t>(header.item_high) << 32U) | header.item;
	host_view.l0_size = MakeFastDiv32(header.seg_cnt);

	size_t offset = sizeof(Header) + parts_bytes;
	uint64_t total_items = 0;
	uint32_t parts[MAX_SEGMENT] = {};
	for (unsigned i = 0; i < header.seg_cnt; ++i) {
		std::memcpy(&parts[i],
			prefix.data() + sizeof(Header) + i * sizeof(uint32_t), sizeof(uint32_t));
		if (parts[i] == 0) {
			return {Status::INVALID_PACK};
		}
		DeviceSegment& segment = host_view.segments[i];
		segment.item_offset = total_items;
		total_items += parts[i];
		segment.cells_offset = offset;
		segment.l1_band = MakeFastDiv64(L1Band(parts[i]));
		segment.l2_size = MakeFastDiv64(L2Size(parts[i]));
		if (!Advance(offset, L1Size(parts[i]), pack_size)) {
			return {Status::INVALID_PACK};
		}
	}
	if (total_items != host_view.item) {
		return {Status::INVALID_PACK};
	}
	if (offset > std::numeric_limits<size_t>::max() - 31U) {
		return {Status::INVALID_PACK};
	}
	offset = (offset + 31U) & ~size_t{31U};
	if (offset > pack_size) {
		return {Status::INVALID_PACK};
	}
	for (unsigned i = 0; i < header.seg_cnt; ++i) {
		host_view.segments[i].sections_offset = offset;
		const size_t section_count = SectionSize(parts[i]);
		if (section_count > std::numeric_limits<size_t>::max() / sizeof(BitmapSection)
			|| !Advance(offset, section_count * sizeof(BitmapSection), pack_size)) {
			return {Status::INVALID_PACK};
		}
	}
	if (header.type != PerfectHashtable::INDEX_ONLY) {
		host_view.content_offset = offset;
		if (host_view.line_size == 0
			|| host_view.item > (pack_size - offset) / host_view.line_size
			|| !Advance(offset,
				static_cast<size_t>(host_view.item * host_view.line_size), pack_size)) {
			return {Status::INVALID_PACK};
		}
	}

	Table* table = new (std::nothrow) Table;
	if (table == nullptr) {
		return {Status::OUT_OF_MEMORY};
	}
	error = cudaMalloc(reinterpret_cast<void**>(&table->device_view), sizeof(DeviceTable));
	if (error != cudaSuccess) {
		Detach(table);
		return CudaResult(error);
	}
	error = cudaMemcpyAsync(table->device_view, &host_view, sizeof(host_view),
								 cudaMemcpyHostToDevice, stream);
	if (error == cudaSuccess) {
		error = cudaStreamSynchronize(stream);
	}
	if (error != cudaSuccess) {
		Detach(table);
		return CudaResult(error);
	}
	table->info.type = static_cast<PerfectHashtable::Type>(header.type);
	table->info.key_length = header.key_len;
	table->info.value_length =
		header.type == PerfectHashtable::INDEX_ONLY ? 0 : header.val_len;
	table->info.item_count = host_view.item;
	*output = table;
	return {};
}

void Detach(Table* table) noexcept {
	if (table == nullptr) {
		return;
	}
	if (table->device_view != nullptr) {
		cudaFree(table->device_view);
	}
	delete table;
}

TableInfo GetTableInfo(const Table* table) noexcept {
	return table == nullptr ? TableInfo{} : table->info;
}

Result CreateTryFetchWorkspace(unsigned capacity, cudaStream_t stream,
							   TryFetchWorkspace** output) noexcept {
	if (output != nullptr) *output = nullptr;
	if (output == nullptr || capacity == 0
		|| capacity > static_cast<unsigned>(INT_MAX)) {
		return {Status::INVALID_ARGUMENT};
	}
	auto* workspace = new (std::nothrow) TryFetchWorkspace;
	if (workspace == nullptr) return {Status::OUT_OF_MEMORY};
	workspace->capacity = capacity;

	auto error = cudaMalloc(reinterpret_cast<void**>(&workspace->miss_flags),
		static_cast<size_t>(capacity) * sizeof(bool));
	if (error == cudaSuccess) {
		cub::CountingInputIterator<unsigned> indices(0);
		error = cub::DeviceSelect::Flagged(nullptr, workspace->select_temp_bytes,
			indices, workspace->miss_flags, static_cast<unsigned*>(nullptr),
			static_cast<unsigned*>(nullptr), static_cast<int>(capacity), stream);
	}
	if (error == cudaSuccess && workspace->select_temp_bytes != 0) {
		error = cudaMalloc(&workspace->select_temp, workspace->select_temp_bytes);
	}
	if (error != cudaSuccess) {
		DestroyTryFetchWorkspace(workspace);
		return CudaResult(error);
	}
	*output = workspace;
	return {};
}

void DestroyTryFetchWorkspace(TryFetchWorkspace* workspace) noexcept {
	if (workspace == nullptr) return;
	if (workspace->select_temp != nullptr) cudaFree(workspace->select_temp);
	if (workspace->miss_flags != nullptr) cudaFree(workspace->miss_flags);
	delete workspace;
}

Result BatchLocateAsync(const Table* table, unsigned batch, const uint8_t* keys,
						uint8_t key_length, uint64_t* output,
						cudaStream_t stream) noexcept {
	if (table == nullptr || table->device_view == nullptr || key_length == 0
		|| (batch != 0 && (keys == nullptr || output == nullptr))
		|| (table->info.type != PerfectHashtable::INDEX_ONLY
			&& key_length != table->info.key_length)) {
		return {Status::INVALID_ARGUMENT};
	}
	if (batch == 0) {
		return {};
	}
	if (key_length == 8) {
		LocateKernel<8><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
			table->device_view, batch, keys, key_length, output);
	} else {
		LocateKernel<0><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
			table->device_view, batch, keys, key_length, output);
	}
	return LaunchResult();
}

Result BatchCheckAsync(const Table* table, unsigned batch, const uint8_t* keys,
					   bool* hit_flags, unsigned* device_hit_count,
					   cudaStream_t stream) noexcept {
	if (table == nullptr || table->device_view == nullptr) {
		return {Status::INVALID_ARGUMENT};
	}
	if (table->info.type != PerfectHashtable::KEY_SET
		&& table->info.type != PerfectHashtable::KV_INLINE) {
		return {Status::UNSUPPORTED_TYPE};
	}
	if (batch != 0 && (keys == nullptr || hit_flags == nullptr)) {
		return {Status::INVALID_ARGUMENT};
	}
	if (device_hit_count != nullptr) {
		const auto result = CudaResult(cudaMemsetAsync(
			device_hit_count, 0, sizeof(unsigned), stream));
		if (!result) {
			return result;
		}
	}
	if (batch == 0) {
		return {};
	}
	if (device_hit_count != nullptr) {
		if (table->info.key_length == 8) {
			CheckKernel<8, true><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
				table->device_view, batch, keys, hit_flags, device_hit_count);
		} else {
			CheckKernel<0, true><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
				table->device_view, batch, keys, hit_flags, device_hit_count);
		}
	} else if (table->info.key_length == 8) {
		CheckKernel<8, false><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
			table->device_view, batch, keys, hit_flags, nullptr);
	} else {
		CheckKernel<0, false><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
			table->device_view, batch, keys, hit_flags, nullptr);
	}
	return LaunchResult();
}

Result BatchFetchAsync(const Table* table, unsigned batch, const uint8_t* keys,
					   uint8_t* data, const uint8_t* default_value,
					   bool* hit_flags, unsigned* device_hit_count,
					   cudaStream_t stream) noexcept {
	if (table == nullptr || table->device_view == nullptr) {
		return {Status::INVALID_ARGUMENT};
	}
	if (table->info.type != PerfectHashtable::KV_INLINE) {
		return {Status::UNSUPPORTED_TYPE};
	}
	if (batch != 0 && (keys == nullptr || data == nullptr)) {
		return {Status::INVALID_ARGUMENT};
	}
	if (device_hit_count != nullptr) {
		const auto result = CudaResult(cudaMemsetAsync(
			device_hit_count, 0, sizeof(unsigned), stream));
		if (!result) {
			return result;
		}
	}
	if (batch == 0) {
		return {};
	}
	if (device_hit_count != nullptr) {
		if (table->info.key_length == 8) {
			FetchKernel<8, true, false><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
				table->device_view, batch, keys, data, default_value, hit_flags,
				device_hit_count);
		} else {
			FetchKernel<0, true, false><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
				table->device_view, batch, keys, data, default_value, hit_flags,
				device_hit_count);
		}
	} else if (table->info.key_length == 8) {
		FetchKernel<8, false, false><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
			table->device_view, batch, keys, data, default_value, hit_flags, nullptr);
	} else {
		FetchKernel<0, false, false><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
			table->device_view, batch, keys, data, default_value, hit_flags, nullptr);
	}
	return LaunchResult();
}

Result BatchTryFetchAsync(const Table* table, TryFetchWorkspace* workspace,
						  unsigned batch, const uint8_t* keys, uint8_t* data,
						  unsigned* misses, unsigned* miss_count,
						  cudaStream_t stream) noexcept {
	if (table == nullptr || table->device_view == nullptr || workspace == nullptr) {
		return {Status::INVALID_ARGUMENT};
	}
	if (table->info.type != PerfectHashtable::KV_INLINE) {
		return {Status::UNSUPPORTED_TYPE};
	}
	if (batch > workspace->capacity || batch > static_cast<unsigned>(INT_MAX)
		|| miss_count == nullptr
		|| (batch != 0 && (keys == nullptr || data == nullptr || misses == nullptr))) {
		return {Status::INVALID_ARGUMENT};
	}
	if (batch == 0) return CudaResult(cudaMemsetAsync(
		miss_count, 0, sizeof(unsigned), stream));
	if (table->info.key_length == 8) {
		FetchKernel<8, false, true><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
			table->device_view, batch, keys, data, nullptr,
			workspace->miss_flags, nullptr);
	} else {
		FetchKernel<0, false, true><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
			table->device_view, batch, keys, data, nullptr,
			workspace->miss_flags, nullptr);
	}
	auto result = LaunchResult();
	if (!result) return result;
	cub::CountingInputIterator<unsigned> indices(0);
	return CudaResult(cub::DeviceSelect::Flagged(
		workspace->select_temp, workspace->select_temp_bytes, indices,
		workspace->miss_flags, misses, miss_count, static_cast<int>(batch), stream));
}

} // namespace shd::cuda::detail

namespace shd::cuda {

struct PerfectHashtable::Impl {
	detail::Table* table = nullptr;
	detail::TableInfo info;
	cudaStream_t stream = nullptr;
	Status last_status = Status::INVALID_ARGUMENT;
	cudaError_t last_cuda_error = cudaSuccess;
	uint8_t* device_staging = nullptr;
	uint8_t* host_staging = nullptr;
	size_t staging_capacity = 0;
	detail::TryFetchWorkspace* try_fetch_workspace = nullptr;
	unsigned try_fetch_capacity = 0;

	~Impl() noexcept {
		detail::DestroyTryFetchWorkspace(try_fetch_workspace);
		if (host_staging != nullptr) cudaFreeHost(host_staging);
		if (device_staging != nullptr) cudaFree(device_staging);
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

Status CudaStatus(cudaError_t error) noexcept {
	if (error == cudaSuccess) {
		return Status::OK;
	}
	return error == cudaErrorMemoryAllocation ? Status::OUT_OF_MEMORY : Status::CUDA_ERROR;
}

detail::Result ToResult(cudaError_t error) noexcept {
	return {CudaStatus(error), error};
}

template <typename ImplT>
detail::Result UploadStaging(ImplT* impl, size_t bytes) noexcept {
	return ToResult(cudaMemcpyAsync(impl->device_staging, impl->host_staging,
		bytes, cudaMemcpyHostToDevice, impl->stream));
}

template <typename ImplT>
detail::Result DownloadStaging(ImplT* impl, size_t offset,
							   size_t bytes) noexcept {
	auto result = ToResult(cudaMemcpyAsync(impl->host_staging + offset,
		impl->device_staging + offset, bytes, cudaMemcpyDeviceToHost,
		impl->stream));
	if (result) {
		result = ToResult(cudaStreamSynchronize(impl->stream));
	}
	return result;
}

constexpr size_t AlignUp(size_t value, size_t alignment) noexcept {
	return (value + alignment - 1U) & ~(alignment - 1U);
}

template <typename ImplT>
bool ReserveStaging(ImplT* impl, size_t bytes) noexcept {
	if (bytes <= impl->staging_capacity) {
		return true;
	}

	uint8_t* new_device = nullptr;
	uint8_t* new_host = nullptr;
	auto error = cudaMalloc(reinterpret_cast<void**>(&new_device), bytes);
	if (error == cudaSuccess) {
		error = cudaMemsetAsync(new_device, 0, bytes, impl->stream);
	}
	if (error == cudaSuccess) {
		error = cudaMallocHost(reinterpret_cast<void**>(&new_host), bytes);
	}
	if (error != cudaSuccess) {
		if (new_host != nullptr) cudaFreeHost(new_host);
		if (new_device != nullptr) cudaFree(new_device);
		StoreResult(impl, CudaStatus(error), error);
		return false;
	}

	if (impl->host_staging != nullptr) cudaFreeHost(impl->host_staging);
	if (impl->device_staging != nullptr) cudaFree(impl->device_staging);
	impl->host_staging = new_host;
	impl->device_staging = new_device;
	impl->staging_capacity = bytes;
	return true;
}

template <typename ImplT>
bool ReserveTryFetchWorkspace(ImplT* impl, unsigned capacity) noexcept {
	if (capacity <= impl->try_fetch_capacity) return true;
	detail::TryFetchWorkspace* workspace = nullptr;
	const auto result = detail::CreateTryFetchWorkspace(
		capacity, impl->stream, &workspace);
	if (!result) {
		StoreResult(impl, result.status, result.cuda_error);
		return false;
	}
	detail::DestroyTryFetchWorkspace(impl->try_fetch_workspace);
	impl->try_fetch_workspace = workspace;
	impl->try_fetch_capacity = capacity;
	return true;
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

	const size_t key_bytes = static_cast<size_t>(batch) * key_length;
	const size_t output_offset = AlignUp(key_bytes, alignof(uint64_t));
	const size_t output_bytes = static_cast<size_t>(batch) * sizeof(uint64_t);
	if (!ReserveStaging(m_impl, output_offset + output_bytes)) {
		return;
	}
	std::memcpy(m_impl->host_staging, keys, key_bytes);
	auto result = UploadStaging(m_impl, key_bytes);
	if (result) {
		result = detail::BatchLocateAsync(m_impl->table, batch,
			m_impl->device_staging, key_length,
			reinterpret_cast<uint64_t*>(m_impl->device_staging + output_offset),
			m_impl->stream);
	}
	if (result) {
		result = DownloadStaging(m_impl, output_offset, output_bytes);
	}
	if (result) {
		std::memcpy(output, m_impl->host_staging + output_offset, output_bytes);
	}
	StoreResult(m_impl, result.status, result.cuda_error);
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

	const size_t key_bytes = static_cast<size_t>(batch) * m_impl->info.key_length;
	const size_t flags_offset = key_bytes;
	const size_t flags_bytes = static_cast<size_t>(batch) * sizeof(bool);
	const size_t count_offset = AlignUp(flags_offset + flags_bytes, alignof(unsigned));
	if (!ReserveStaging(m_impl, count_offset + sizeof(unsigned))) {
		return 0;
	}
	std::memcpy(m_impl->host_staging, keys, key_bytes);
	auto result = UploadStaging(m_impl, key_bytes);
	if (result) {
		result = detail::BatchCheckAsync(m_impl->table, batch,
			m_impl->device_staging,
			reinterpret_cast<bool*>(m_impl->device_staging + flags_offset),
			reinterpret_cast<unsigned*>(m_impl->device_staging + count_offset),
			m_impl->stream);
	}
	if (result) {
		result = DownloadStaging(m_impl, flags_offset,
			count_offset + sizeof(unsigned) - flags_offset);
	}
	unsigned hits = 0;
	if (result) {
		std::memcpy(output, m_impl->host_staging + flags_offset, flags_bytes);
		std::memcpy(&hits, m_impl->host_staging + count_offset, sizeof(hits));
	}
	StoreResult(m_impl, result.status, result.cuda_error);
	return result ? hits : 0;
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

	const size_t key_bytes = static_cast<size_t>(batch) * m_impl->info.key_length;
	const size_t default_offset = key_bytes;
	const size_t input_end = default_offset + m_impl->info.value_length;
	const size_t data_offset = AlignUp(input_end, alignof(uint64_t));
	const size_t data_bytes = static_cast<size_t>(batch) * m_impl->info.value_length;
	const size_t count_offset = AlignUp(data_offset + data_bytes, alignof(unsigned));
	if (!ReserveStaging(m_impl, count_offset + sizeof(unsigned))) {
		return 0;
	}
	std::memcpy(m_impl->host_staging, keys, key_bytes);
	std::memcpy(m_impl->host_staging + default_offset,
		default_value, m_impl->info.value_length);
	auto result = UploadStaging(m_impl, input_end);
	if (result) {
		result = detail::BatchFetchAsync(m_impl->table, batch,
			m_impl->device_staging, m_impl->device_staging + data_offset,
			m_impl->device_staging + default_offset, nullptr,
			reinterpret_cast<unsigned*>(m_impl->device_staging + count_offset),
			m_impl->stream);
	}
	if (result) {
		result = DownloadStaging(m_impl, data_offset,
			count_offset + sizeof(unsigned) - data_offset);
	}
	unsigned hits = 0;
	if (result) {
		std::memcpy(data, m_impl->host_staging + data_offset, data_bytes);
		std::memcpy(&hits, m_impl->host_staging + count_offset, sizeof(hits));
	}
	StoreResult(m_impl, result.status, result.cuda_error);
	return result ? hits : 0;
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
	const size_t key_bytes = static_cast<size_t>(batch) * m_impl->info.key_length;
	const size_t data_offset = AlignUp(key_bytes, alignof(uint64_t));
	const size_t data_bytes = static_cast<size_t>(batch) * m_impl->info.value_length;
	const size_t miss_offset = AlignUp(data_offset + data_bytes, alignof(unsigned));
	const size_t miss_bytes = static_cast<size_t>(batch) * sizeof(unsigned);
	const size_t count_offset = miss_offset + miss_bytes;
	if (!ReserveStaging(m_impl, count_offset + sizeof(unsigned))
		|| !ReserveTryFetchWorkspace(m_impl, batch)) {
		return 0;
	}
	auto* device_misses =
		reinterpret_cast<unsigned*>(m_impl->device_staging + miss_offset);
	auto* device_count =
		reinterpret_cast<unsigned*>(m_impl->device_staging + count_offset);

	std::memcpy(m_impl->host_staging, keys, key_bytes);
	auto result = UploadStaging(m_impl, key_bytes);
	if (result) {
		result = detail::BatchTryFetchAsync(m_impl->table,
			m_impl->try_fetch_workspace, batch, m_impl->device_staging,
			m_impl->device_staging + data_offset, device_misses, device_count,
			m_impl->stream);
	}
	if (result) {
		result = DownloadStaging(m_impl, data_offset,
			count_offset + sizeof(unsigned) - data_offset);
	}
	unsigned misses = 0;
	if (result) {
		std::memcpy(&misses, m_impl->host_staging + count_offset, sizeof(misses));
		if (misses > batch) {
			result = {Status::CUDA_ERROR, cudaErrorUnknown};
		} else {
			std::memcpy(data, m_impl->host_staging + data_offset, data_bytes);
			std::memcpy(miss, m_impl->host_staging + miss_offset, miss_bytes);
		}
	}
	StoreResult(m_impl, result.status, result.cuda_error);
	return result ? batch - misses : 0;
}

} // namespace shd::cuda
