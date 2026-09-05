//==============================================================================
// Skew Hash and Displace Algorithm - CUDA query interface.
// Copyright (C) 2020  Ruan Kunliang
//
// This library is free software; you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the Free
// Software Foundation; either version 2.1 of the License, or (at your option)
// any later version.
//==============================================================================

#pragma once
#ifndef SHD_CUDA_H_
#define SHD_CUDA_H_

#include <cstddef>
#include <cstdint>
#include <shd.h>

struct CUstream_st;
using cudaStream_t = CUstream_st*;

namespace shd::cuda {

enum class Status : uint8_t {
	OK = 0,
	INVALID_ARGUMENT,
	INVALID_PACK,
	UNSUPPORTED_TYPE,
	OUT_OF_MEMORY,
	CUDA_ERROR
};

// The serialized pack is a device pointer. Batch keys, outputs, and default
// values are host pointers staged by this wrapper. Construction inspects only
// the pack prefix and uploads a small descriptor; it never copies or owns the
// pack body. Calls on one wrapper are not concurrent; use separate wrappers
// over the same device pack for separate host threads/streams.
class SHD_CUDA_API PerfectHashtable final {
public:
	using Type = shd::PerfectHashtable::Type;
	static constexpr Type INDEX_ONLY = shd::PerfectHashtable::INDEX_ONLY;
	static constexpr Type KEY_SET = shd::PerfectHashtable::KEY_SET;
	static constexpr Type KV_INLINE = shd::PerfectHashtable::KV_INLINE;
	static constexpr Type KV_SEPARATED = shd::PerfectHashtable::KV_SEPARATED;
	static constexpr Type ILLEGAL_TYPE = shd::PerfectHashtable::ILLEGAL_TYPE;

	explicit PerfectHashtable(const uint8_t* device_pack, size_t pack_size,
						  cudaStream_t stream = nullptr) noexcept;
	~PerfectHashtable() noexcept;

	PerfectHashtable(PerfectHashtable&& other) noexcept;
	PerfectHashtable& operator=(PerfectHashtable&& other) noexcept;
	PerfectHashtable(const PerfectHashtable&) = delete;
	PerfectHashtable& operator=(const PerfectHashtable&) = delete;

	bool operator!() const noexcept;
	Type type() const noexcept;
	uint8_t key_len() const noexcept;
	uint16_t val_len() const noexcept;
	size_t item() const noexcept;

	cudaStream_t stream() const noexcept;
	void set_stream(cudaStream_t stream) noexcept;
	Status status() const noexcept;
	int cuda_error() const noexcept;

	// Host-memory batch API. Large batches may use a private stream and two
	// staging buffers to overlap chunks. All work observes prior operations on
	// the bound stream; both streams finish before return.
	void batch_locate(unsigned batch, const uint8_t* __restrict keys,
					  uint8_t key_len, uint64_t* __restrict out);

	// KEY_SET or KV_INLINE. Returns the number of hits.
	unsigned batch_check(unsigned batch, const uint8_t* __restrict keys,
						 bool* __restrict out) const noexcept;

	// KV_INLINE only. dft_val is required and every output slot is written.
	// Returns the number of hits.
	unsigned batch_fetch(unsigned batch, const uint8_t* __restrict keys,
						 uint8_t* __restrict data,
						 const uint8_t* __restrict dft_val) const noexcept;

	// KV_INLINE only. Hit values are written at their original batch positions;
	// miss indices are compacted in input order. Value slots for misses and the
	// unused tail of miss[] are unspecified. Returns the number of hits. batch
	// must not exceed INT_MAX in this implementation.
	unsigned batch_try_fetch(unsigned batch, const uint8_t* __restrict keys,
							 uint8_t* __restrict data,
							 unsigned* __restrict miss) const noexcept;

private:
	struct Impl;
	Impl* m_impl = nullptr;
};

} // namespace shd::cuda

#endif // SHD_CUDA_H_
