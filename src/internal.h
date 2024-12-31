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
#ifndef SHD_INTERNAL_H_
#define SHD_INTERNAL_H_

#include <cstring>
#include <functional>
//#define SHD_PACK_SIZE 4
#include <shd.h>
#include "common.h"

namespace shd {

struct V96 {
	uint32_t u[3];
};
union V128X {
	V128 v;
	struct {
		V96 l96;
		uint32_t h32;
	} u;
};
union V96X {
	V96 v;
	struct {
		uint64_t l64;
		uint32_t h32;
	} u;
};

static FORCE_INLINE bool operator==(const V96& a, const V96& b) {
	V96X ax{.v = a};
	V96X bx{.v = b};
	return ax.u.l64 == bx.u.l64 && ax.u.h32 == bx.u.h32;
}

static FORCE_INLINE V96 GenID(uint32_t seed, const uint8_t* key, uint8_t len) {
	V128X tmp{.v = HashTo128(key, len, seed)};
	return tmp.u.l96;
}
static FORCE_INLINE uint16_t L0Hash(const V96& id) {
	return id.u[0];
}
static FORCE_INLINE uint32_t L1Hash(const V96& id) {
	return id.u[1];
}
static FORCE_INLINE uint64_t L2Hash(const V96& id, uint8_t sd8) {
	const uint32_t seed = (sd8+1U) * 0xff00ffU;	//{sd8, ~sd8, sd8, ~sd8}
	V128X tmp{ .u = {id, seed} };
	return tmp.v.l ^ tmp.v.h;
}

static FORCE_INLINE unsigned PopCount32(uint32_t x) {
	static_assert(sizeof(int)==sizeof(uint32_t));
	return __builtin_popcount(x);
}
static FORCE_INLINE unsigned PopCount64(uint64_t x) {
	static_assert(sizeof(long long)==sizeof(uint64_t));
	return __builtin_popcountll(x);
}

template <typename T>
T FORCE_INLINE AddRelaxed(T& tgt, T val) {
	return __atomic_fetch_add(&tgt, val, __ATOMIC_RELAXED);
}

template <typename T>
T FORCE_INLINE SubRelaxed(T& tgt, T val) {
	return __atomic_fetch_sub(&tgt, val, __ATOMIC_RELAXED);
}

static FORCE_INLINE bool TestAndSetBit(uint8_t bitmap[], size_t pos) {
	auto& b = bitmap[pos>>3U];
	const uint8_t m = 1U << (pos&7U);
	if (b & m) {
		return false;
	}
	b |= m;
	return true;
}
static FORCE_INLINE bool AtomicTestAndSetBit(uint8_t bitmap[], uint64_t pos) {
	auto& b = bitmap[pos>>3U];
	const uint8_t m = 1U << (pos&7U);
	while (true) {
		auto b0 = __atomic_load_n(&b, __ATOMIC_ACQUIRE);
		if (b0 & m) {
			return false;
		}
		auto b1 = b0 | m;
		if (__atomic_compare_exchange_n(&b, &b0, b1, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
			return true;
		}
	}
}

static FORCE_INLINE void PrefetchBit(const uint8_t bitmap[], size_t pos) {
	PrefetchForNext(&bitmap[pos>>3U]);
}

static FORCE_INLINE bool TestBit(const uint8_t bitmap[], size_t pos) {
	return (bitmap[pos>>3U] & (1U<<(pos&7U))) != 0;
}

static FORCE_INLINE void SetBit(uint8_t bitmap[], size_t pos) {
	bitmap[pos>>3U] |= (1U<<(pos&7U));
}

static FORCE_INLINE void ClearBit(uint8_t bitmap[], size_t pos) {
	bitmap[pos>>3U] &= ~(1U<<(pos&7U));
}

static constexpr uint64_t L1H_MAX = 0x7fffffff;
static constexpr uint32_t L1CELL = 5;
static constexpr uint64_t L1TIP = L1H_MAX / L1CELL;

static FORCE_INLINE constexpr uint32_t L1Size(uint32_t item) {
	return ((uint64_t)item+(L1CELL-1))/L1CELL;
}

static FORCE_INLINE constexpr uint64_t L1Band(uint32_t item) {
	auto l1sz = L1Size(item);
	return (L1H_MAX*(L1H_MAX+L1TIP) + (l1sz-1)) / l1sz;
}

static FORCE_INLINE uint32_t SkewMap(uint32_t code, const Divisor<uint64_t>& band) {
	uint64_t x = code & L1H_MAX;
	return x*(x+L1TIP) / band;
}

static FORCE_INLINE constexpr uint64_t L2Size(uint32_t item) {
	return ((uint64_t)item)*2U | 1U;	//up to odd
}

struct BitmapSection {
	uint32_t b32[7];
	uint32_t step;
};
static constexpr unsigned BITMAP_SECTION_SIZE = 28U * 8U;
static FORCE_INLINE constexpr uint32_t SectionSize(uint32_t item) {
	return (L2Size(item) + (BITMAP_SECTION_SIZE-1)) / BITMAP_SECTION_SIZE;
}
static FORCE_INLINE constexpr uint32_t BitmapSize(uint32_t item) {
	return SectionSize(item) * (BITMAP_SECTION_SIZE/8U);
}

//optimize for common short cases
static FORCE_INLINE bool Equal(const uint8_t* a, const uint8_t* b, uint8_t len) {
	if (len == sizeof(uint64_t)) {
		return *(const uint64_t*)a == *(const uint64_t*)b;
	} else if (len == sizeof(uint32_t)) {
		return *(const uint32_t*)a == *(const uint32_t*)b;
	} else {
		return memcmp(a, b, len) == 0;
	}
}
static FORCE_INLINE void Assign(uint8_t* dest, const uint8_t* src, uint8_t len) {
	if (len == sizeof(uint64_t)) {
		*(uint64_t*)dest = *(const uint64_t*)src;
	} else if (len == sizeof(uint32_t)) {
		*(uint32_t*)dest = *(const uint32_t*)src;
	} else {
		memcpy(dest, src, len);
	}
}

static constexpr uint32_t SHD_MAGIC = 0x4448537f;

static constexpr uint32_t OFFSET_FIELD_SIZE = 6;
static constexpr uint64_t MAX_OFFSET = (1ULL<<(OFFSET_FIELD_SIZE*8U))-1;

static FORCE_INLINE size_t ReadOffsetField(const uint8_t* field) {
	return (((uint64_t)*(uint16_t*)(field+4))<<32U) | *(uint32_t*)field;
}

static FORCE_INLINE void WriteOffsetField(uint8_t* field, size_t offset) {
	*(uint32_t*)field = offset;
	*(uint16_t*)(field+4) = offset>>32U;
}


using Type = PerfectHashtable::Type;

struct Header {
	uint32_t magic = SHD_MAGIC;
	uint8_t type = Type::INDEX_ONLY;
	uint8_t key_len = 0;
	uint16_t val_len = 0;
	uint32_t seed = 0;
	uint32_t item = 0;
	uint16_t item_high = 0;
	uint16_t seg_cnt = 0;
	//uint32_t parts[seg_cnt] = 0;

	// uint8_t cells[]
	// 32B align
	// BitmapSection sections[]

	// key_val[item] or key_off[item]		sizeof(key_off)-key_len is val_len
	// separated_value[], dynamic length, length mark is embedded
};

struct SegmentView {
	const uint8_t* cells = nullptr;
	const BitmapSection* sections = nullptr;
	Divisor<uint64_t> l1bd;
	Divisor<uint64_t> l2sz;
	uint64_t offset = 0; //item offset
};

struct PackView {
	Type type = Type::INDEX_ONLY;
	uint8_t key_len = 0;
	uint16_t val_len = 0;
	uint32_t line_size = 0; //key_len+val_len
	uint32_t seed = 0;
	Divisor<uint16_t> l0sz;
	uint64_t item = 0;
	const uint8_t* content = nullptr;
	const uint8_t* extend = nullptr;
	const uint8_t* space_end = nullptr;
	SegmentView segments[0];
};

extern std::unique_ptr<uint8_t[]> CreatePackView(const uint8_t* addr, size_t size);
extern Slice SeparatedValue(const uint8_t* pt, const uint8_t* end);

extern uint64_t CalcPos(const PackView& index, const uint8_t* key, uint8_t key_len);

static constexpr unsigned MINI_BATCH = 32;
static constexpr unsigned DOUBLE_COPY_LINE_SIZE_LIMIT = 160;

void BatchFindPos(const PackView& pack, size_t batch, const std::function<const uint8_t*(uint8_t*)>& reader,
				  const std::function<void(uint64_t)>& output, const uint8_t* bitmap);
void BatchDataMapping(const PackView& index, uint8_t* space, size_t batch,
					  const std::function<const uint8_t*(uint8_t*)>& reader);


extern unsigned BatchSearch(const PackView& pack, unsigned batch, const uint8_t* const keys[], const uint8_t* out[]);
extern unsigned BatchFetch(const PackView& pack, const uint8_t* __restrict__ dft_val, unsigned batch,
						   const uint8_t* __restrict__ keys, uint8_t* __restrict__ data, unsigned* __restrict__ miss);
extern unsigned BatchSearch(const PackView& base, const PackView& patch, unsigned batch,
							const uint8_t* const keys[], const uint8_t* out[]);
extern unsigned BatchFetch(const PackView& base, const PackView& patch, const uint8_t* __restrict__ dft_val,
						   unsigned batch, const uint8_t* __restrict__ keys, uint8_t* __restrict__ data,
						   unsigned* __restrict__ miss);

extern BuildStatus Rebuild(const PackView& base, const DataReaders& in, IDataWriter& out, Retry retry);

} //shd
#endif //SHD_INTERNAL_H_
