// CUDA launch interface. This header is intentionally not installed.
#pragma once
#ifndef SHD_CUDA_SHD_CUH_
#define SHD_CUDA_SHD_CUH_

#include <cstddef>
#include <cstdint>
#include <cuda_runtime_api.h>
#include <shd_cuda.h>

namespace shd::cuda::detail {

struct Table;
struct TryFetchWorkspace;

struct Result {
	Status status = Status::OK;
	cudaError_t cuda_error = cudaSuccess;

	constexpr explicit operator bool() const noexcept {
		return status == Status::OK;
	}
};

struct TableInfo {
	PerfectHashtable::Type type = PerfectHashtable::ILLEGAL_TYPE;
	uint8_t key_length = 0;
	uint16_t value_length = 0;
	uint64_t item_count = 0;
};

// Attach inspects the device-resident pack prefix and therefore synchronizes
// stream before returning. Query functions below only enqueue work.
SHD_CUDA_API Result Attach(const uint8_t* device_pack, size_t pack_size,
						   cudaStream_t stream, Table** out) noexcept;
SHD_CUDA_API void Detach(Table* table) noexcept;
SHD_CUDA_API TableInfo GetTableInfo(const Table* table) noexcept;

// A workspace is reusable for batches up to capacity, but one workspace must
// not be used concurrently by multiple streams.
SHD_CUDA_API Result CreateTryFetchWorkspace(unsigned capacity,
										cudaStream_t stream,
										TryFetchWorkspace** out) noexcept;
SHD_CUDA_API void DestroyTryFetchWorkspace(TryFetchWorkspace* workspace) noexcept;

SHD_CUDA_API Result BatchLocateAsync(const Table* table, unsigned batch,
									 const uint8_t* device_keys, uint8_t key_length,
									 uint64_t* device_output,
									 cudaStream_t stream) noexcept;

// device_hit_count is optional. When supplied, it is overwritten in stream;
// ownership remains with the caller so one Table can be used concurrently.
SHD_CUDA_API Result BatchCheckAsync(const Table* table, unsigned batch,
									const uint8_t* device_keys, bool* device_hit_flags,
									unsigned* device_hit_count,
									cudaStream_t stream) noexcept;

SHD_CUDA_API Result BatchFetchAsync(const Table* table, unsigned batch,
									const uint8_t* device_keys, uint8_t* device_data,
									const uint8_t* device_default_value,
									bool* device_hit_flags,
									unsigned* device_hit_count,
									cudaStream_t stream) noexcept;

// Writes hit values at their input positions and stable-compacts miss indices.
SHD_CUDA_API Result BatchTryFetchAsync(const Table* table,
									   TryFetchWorkspace* workspace,
									   unsigned batch,
									   const uint8_t* device_keys,
									   uint8_t* device_data,
									   unsigned* device_misses,
									   unsigned* device_miss_count,
									   cudaStream_t stream) noexcept;

} // namespace shd::cuda::detail

#endif // SHD_CUDA_SHD_CUH_
