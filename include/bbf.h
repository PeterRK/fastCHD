//==============================================================================
// Block Bloom Filter with 3.5% false positive rate
// Copyright (C) 2025  Ruan Kunliang
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
#ifndef BLOCK_BLOOM_FILTER_H_
#define BLOCK_BLOOM_FILTER_H_

#include <cstdint>
#include <string>
#include <functional>
#include "utils.h"

namespace bbf {

using ::shd::IDataWriter;
using ::shd::MemBlock;
using ::shd::Divisor;

class BloomFilter {
public:
	explicit BloomFilter(size_t capacity);
	explicit BloomFilter(const std::string& path);
	BloomFilter(size_t size, const std::function<bool(uint8_t*)>& load);
	bool operator!() const noexcept { return m_mem.size() < sizeof(uint64_t)*2; }

	size_t item() const noexcept {
		return *reinterpret_cast<const uint64_t*>(m_mem.addr());
	}
	size_t capacity() const noexcept {
		return m_mem.size() - sizeof(uint64_t);
	}

	bool dump(IDataWriter& out) {
		if (!*this) {
			return false;
		}
		return out.write(m_mem.addr(), m_mem.size());
	}

	bool test(const uint8_t* key, unsigned len) const noexcept;
	bool set(const uint8_t* key, unsigned len) const noexcept;

	unsigned batch_test(unsigned batch, unsigned key_len,
						const uint8_t* __restrict__ keys, bool* __restrict__ out) const noexcept;

	void batch_set(unsigned batch, unsigned key_len, const uint8_t* keys) const noexcept;

private:
	MemBlock m_mem;
	Divisor<uint64_t> m_block;
};

} // bbf
#endif // BLOCK_BLOOM_FILTER_H_