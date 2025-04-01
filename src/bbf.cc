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

#include <cstring>
#include "common.h"
#include "bbf.h"
#include "pipeline.h"

namespace bbf {

struct Step {
	uint64_t blk;
	uint64_t mask;
};

static FORCE_INLINE Step Calc(const Divisor<uint64_t>& block, const uint8_t *key, unsigned len) noexcept {
	auto code = ::shd::HashTo128(key, len);
	auto a = 1ULL << ((code.l >> 24) & 63);
	auto b = 1ULL << ((code.l >> 18) & 63);
	auto c = 1ULL << ((code.l >> 12) & 63);
	auto d = 1ULL << ((code.l >> 6) & 63);
	auto e = 1ULL << (code.l & 63);
	return {code.h % block, (a | b) | (c | d) | e};
}

bool BloomFilter::test(const uint8_t *key, unsigned len) const noexcept {
	auto s = Calc(m_block, key, len);
	auto space = reinterpret_cast<const uint64_t*>(m_mem.addr()+sizeof(uint64_t));
	return (space[s.blk] & s.mask) == s.mask;
}

bool BloomFilter::set(const uint8_t *key, unsigned len) const noexcept {
	auto s = Calc(m_block, key, len);
	auto space = reinterpret_cast<uint64_t*>(m_mem.addr()+sizeof(uint64_t));
	auto& item = *reinterpret_cast<uint64_t*>(m_mem.addr());
	if ((space[s.blk] & s.mask) == s.mask) {
		return false;
	}
	space[s.blk] |= s.mask;
	item++;
	return true;
}

BloomFilter::BloomFilter(size_t capacity) {
	if (capacity == 0) {
		return;
	}
	m_block = (capacity+7) / sizeof(uint64_t);
	auto size = sizeof(uint64_t) + m_block.value() * sizeof(uint64_t);
	m_mem = MemBlock(size);
	if (!m_mem) {
		return;
	}
	memset(m_mem.addr(), 0, m_mem.size());
}

BloomFilter::BloomFilter(const std::string& path) {
	auto mem = MemBlock::LoadFile(path.c_str());
	if (!mem) {
		return;
	}
	if (mem.size() < sizeof(uint64_t)*2 || mem.size() % sizeof(uint64_t) != 0) {
		return;
	}
	m_block = (mem.size()-sizeof(uint64_t)) / sizeof(uint64_t);
	m_mem = std::move(mem);
}

BloomFilter::BloomFilter(size_t size, const std::function<bool(uint8_t*)>& load) {
	if (size < sizeof(uint64_t)*2 || size % sizeof(uint64_t) != 0) {
		return;
	}
	auto mem = MemBlock(size);
	if (!mem || !load(mem.addr())) {
		return;
	}
	m_block = (size-sizeof(uint64_t)) / sizeof(uint64_t);
	m_mem = std::move(mem);
}

unsigned BloomFilter::batch_test(unsigned batch, unsigned key_len,
								 const uint8_t* __restrict__ keys, bool* __restrict__ out) const noexcept {
	auto space = reinterpret_cast<const uint64_t*>(m_mem.addr()+sizeof(uint64_t));
	unsigned hit = 0;
	Pipeline<15>(batch,
			[this, space, &keys, key_len](unsigned i)->Step {
				auto s = Calc(m_block, keys+i*key_len, key_len);
				PrefetchForNext(&space[s.blk]);
				return s;
			},
			[space, &out, &hit](Step& s, unsigned i) {
				out[i] = (space[s.blk] & s.mask) == s.mask;
				hit += out[i];
			}
	);
	return hit;
}

void BloomFilter::batch_set(unsigned batch, unsigned key_len, const uint8_t* keys) const noexcept {
	auto space = reinterpret_cast<uint64_t*>(m_mem.addr()+sizeof(uint64_t));
	auto& item = *reinterpret_cast<uint64_t*>(m_mem.addr());
	Pipeline<15>(batch,
			[this, space, &keys, key_len](unsigned i)->Step {
				auto s = Calc(m_block, keys+i*key_len, key_len);
				PrefetchForNext(&space[s.blk]);
				return s;
			},
			[space, &item](Step& s, unsigned) {
				item += (space[s.blk] & s.mask) != s.mask;
				space[s.blk] |= s.mask;
			}
	);
}

} // bbf