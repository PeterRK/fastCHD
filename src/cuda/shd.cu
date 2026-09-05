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

template <unsigned LENGTH>
__device__ __forceinline__ Hash128 HashShortTo128(const uint8_t* message,
											 uint64_t seed) {
	static_assert(LENGTH == 4 || LENGTH == 8);
	constexpr uint64_t magic = 0xdeadbeefdeadbeefULL;
	uint64_t a = seed;
	uint64_t b = seed;
	uint64_t c = magic + (LENGTH == 4 ? Load32(message) : Load64(message));
	uint64_t d = magic + (uint64_t{LENGTH} << 56U);
	End(a, b, c, d);
	return {a, b};
}

template <unsigned FIXED_KEY_LENGTH>
__device__ __forceinline__ Id96 GenerateId(const DeviceTable& table,
										 const uint8_t* key, uint8_t key_length) {
	Hash128 hash;
	if constexpr (FIXED_KEY_LENGTH == 4 || FIXED_KEY_LENGTH == 8) {
		hash = HashShortTo128<FIXED_KEY_LENGTH>(key, table.seed);
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
	// At most six whole words precede the target word. Fixed bounds avoid
	// nvcc expanding a variable-trip loop into a much larger instruction sequence.
#pragma unroll
	for (unsigned i = 0; i < BITMAP_SECTION_SIZE / 32U - 1U; ++i) {
		rank += i < complete_words ? __popc(section->b32[i]) : 0U;
	}
	const uint32_t mask = (uint32_t{1} << (bit_offset & 31U)) - 1U;
	rank += __popc(section->b32[complete_words] & mask);
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
	if constexpr (FIXED_KEY_LENGTH == 4) {
		return Load32(a) == Load32(b);
	} else if constexpr (FIXED_KEY_LENGTH == 8) {
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
	// Copy a vector prefix when aligned; the scalar paths finish the tail.
	if (length >= sizeof(uint4) && (alignment & (alignof(uint4) - 1U)) == 0) {
		for (; offset + sizeof(uint4) <= length; offset += sizeof(uint4)) {
			*reinterpret_cast<uint4*>(destination + offset) =
				*reinterpret_cast<const uint4*>(source + offset);
		}
	}
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
	if (key_length == 4) {
		LocateKernel<4><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
			table->device_view, batch, keys, key_length, output);
	} else if (key_length == 8) {
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
		if (table->info.key_length == 4) {
			CheckKernel<4, true><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
				table->device_view, batch, keys, hit_flags, device_hit_count);
		} else if (table->info.key_length == 8) {
			CheckKernel<8, true><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
				table->device_view, batch, keys, hit_flags, device_hit_count);
		} else {
			CheckKernel<0, true><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
				table->device_view, batch, keys, hit_flags, device_hit_count);
		}
	} else if (table->info.key_length == 4) {
		CheckKernel<4, false><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
			table->device_view, batch, keys, hit_flags, nullptr);
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
		if (table->info.key_length == 4) {
			FetchKernel<4, true, false><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
				table->device_view, batch, keys, data, default_value, hit_flags,
				device_hit_count);
		} else if (table->info.key_length == 8) {
			FetchKernel<8, true, false><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
				table->device_view, batch, keys, data, default_value, hit_flags,
				device_hit_count);
		} else {
			FetchKernel<0, true, false><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
				table->device_view, batch, keys, data, default_value, hit_flags,
				device_hit_count);
		}
	} else if (table->info.key_length == 4) {
		FetchKernel<4, false, false><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
			table->device_view, batch, keys, data, default_value, hit_flags, nullptr);
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
	if (table->info.key_length == 4) {
		FetchKernel<4, false, true><<<GridSize(batch), BLOCK_SIZE, 0, stream>>>(
			table->device_view, batch, keys, data, nullptr,
			workspace->miss_flags, nullptr);
	} else if (table->info.key_length == 8) {
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
