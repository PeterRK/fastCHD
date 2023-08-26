//==============================================================================
// Skew Hash and Displace Algorithm.
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

#pragma once
#ifndef SHD_H_
#define SHD_H_

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <type_traits>
#include "utils.h"

namespace shd {

static constexpr size_t MAX_KEY_LEN = UINT8_MAX;
static constexpr size_t MAX_INLINE_VALUE_LEN = UINT16_MAX;
static constexpr unsigned MAX_VALUE_LEN_BIT = 35U;	//7x
static constexpr size_t MAX_VALUE_LEN = (1ULL<<MAX_VALUE_LEN_BIT)-1U;
static constexpr uint16_t MAX_SEGMENT = 256U;

enum BuildStatus {
	BUILD_STATUS_OK, BUILD_STATUS_BAD_INPUT, BUILD_STATUS_FAIL_TO_OUTPUT,
	BUILD_STATUS_OUT_OF_CHANCE, BUILD_STATUS_CONFLICT
};

struct Retry {
	uint8_t conflict = 0;
	uint8_t total = 0;
};
static constexpr Retry DEFAULT_RETRY = {1, 4};

using DataReaders = std::vector<std::unique_ptr<IDataReader>>;

extern BuildStatus BuildIndex(const DataReaders& in, IDataWriter& out, Retry retry=DEFAULT_RETRY);

//key should have fixed length
//dynamic length key is not useful, just pad or use checksum instead
extern BuildStatus BuildSet(const DataReaders& in, IDataWriter& out, Retry retry=DEFAULT_RETRY);

//key & value should have fixed length
//inline large value may consume a lot of memory
extern BuildStatus BuildDict(const DataReaders& in, IDataWriter& out, Retry retry=DEFAULT_RETRY);

//key should have fixed length
extern BuildStatus BuildDictWithVariedValue(const DataReaders& in, IDataWriter& out, Retry retry=DEFAULT_RETRY);

extern bool g_trace_build_time;


class PerfectHashtable {
public:
	enum LoadPolicy {MAP_ONLY, MAP_FETCH, MAP_OCCUPY, COPY_DATA};
	explicit PerfectHashtable(const std::string& path, LoadPolicy load_policy=MAP_ONLY);
	PerfectHashtable(size_t size, const std::function<bool(uint8_t*)>& load);
	bool operator!() const noexcept { return m_view == nullptr; }

	enum Type : uint8_t {
		INDEX_ONLY = 0,
		KEY_SET = 1,
		KV_INLINE = 2,
		KV_SEPARATED = 3,
		ILLEGAL_TYPE = 0xff
	};
	Type type() const noexcept { return m_type; }
	uint8_t key_len() const noexcept { return m_key_len; }
	uint16_t val_len() const noexcept { return m_val_len; }
	size_t item() const noexcept { return m_item; }

	size_t locate(const uint8_t* key, uint8_t key_len) const noexcept;

	//KEY_SET, KV_INLINE or KV_SEPARATED
	//key is found when output slice is valid
	Slice search(const uint8_t* key) const noexcept;

	//KEY_SET or KV_INLINE
	//keys == out is OK
	unsigned batch_search(unsigned batch, const uint8_t* const keys[], const uint8_t* out[],
					   const PerfectHashtable* patch=nullptr) const noexcept;

	//only KV_INLINE, if dft_val == nullptr, do nothing when miss
	unsigned batch_fetch(unsigned batch, const uint8_t* __restrict__ keys, uint8_t* __restrict__ data,
						 const uint8_t* __restrict__ dft_val=nullptr,
						 const PerfectHashtable* patch=nullptr) const noexcept;

	unsigned batch_try_fetch(unsigned batch, const uint8_t* __restrict__ keys, uint8_t* __restrict__ data,
							 unsigned* __restrict__ miss, const PerfectHashtable* patch=nullptr) const noexcept;

	BuildStatus derive(const DataReaders& in, IDataWriter& out, Retry retry=DEFAULT_RETRY) const;

private:
	MemMap m_res;
	MemBlock m_mem;
	std::unique_ptr<uint8_t[]> m_view;
	Type m_type = ILLEGAL_TYPE;
	uint8_t m_key_len = 0;
	uint16_t m_val_len = 0;
	size_t m_item = 0;

	void _post_init() noexcept;
};

} //shd
#endif //SHD_H_
