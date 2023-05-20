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

#include <cassert>
#include <cstring>
#include <memory>
#include "internal.h"
#include "pipeline.h"

namespace shd {

struct Step1 {
	const SegmentView* seg;
	V96 id;
	uint32_t l1pos;
};

struct Step2 {
	const SegmentView* seg;
	uint32_t section;
	uint8_t bit_off;
};

struct Step3 {
	const uint8_t* line;
};

static FORCE_INLINE Step1 Calc1(const PackView& index, const uint8_t* key, uint8_t key_len) {
	Step1 out;
	out.id = GenID(index.seed, key, key_len);
	out.seg = &index.segments[L0Hash(out.id) % index.l0sz];
	out.l1pos = SkewMap(L1Hash(out.id), out.seg->l1bd);
	return out;
}

static FORCE_INLINE Step1 Process1(const PackView& index, const uint8_t* key, uint8_t key_len) {
	Step1 out = Calc1(index, key, key_len);
	PrefetchForNext(&out.seg->cells[out.l1pos]);
	return out;
}

static FORCE_INLINE Step1 Process1(const PackView& pack, const uint8_t* key) {
	return Process1(pack, key, pack.key_len);
}

static FORCE_INLINE Step2 Calc2(const Step1& in) {
	Step2 out;
	out.seg = in.seg;
	const auto bit_pos = L2Hash(in.id, in.seg->cells[in.l1pos]) % in.seg->l2sz;
	out.section = bit_pos / BITMAP_SECTION_SIZE;
	out.bit_off = bit_pos % BITMAP_SECTION_SIZE;
	return out;
}

static FORCE_INLINE Step2 Process2(const Step1& in) {
	Step2 out = Calc2(in);
	PrefetchForNext(&out.seg->sections[out.section]);
	return out;
}

static FORCE_INLINE uint64_t CalcPos(const Step2& in) {
	auto& section = in.seg->sections[in.section];
	uint32_t cnt = section.step;	//step is the last field of section
	auto v = (const uint64_t*)section.b32;
	const uint64_t mask = (1LL << (in.bit_off & 63U)) - 1U;
	switch (in.bit_off >> 6U) {
		case 3: cnt += PopCount64(*v++);
		case 2: cnt += PopCount64(*v++);
		case 1: cnt += PopCount64(*v++);
		case 0: cnt += PopCount64(*v & mask);
	}
	return in.seg->offset + cnt;
}

uint64_t CalcPos(const PackView& index, const uint8_t* key, uint8_t key_len) {
	return CalcPos(Calc2(Calc1(index, key, key_len)));
}


#ifndef CACHE_BLOCK_SIZE
#define CACHE_BLOCK_SIZE 64U
#endif
static_assert(CACHE_BLOCK_SIZE >= 64U && (CACHE_BLOCK_SIZE&(CACHE_BLOCK_SIZE-1)) == 0);

static FORCE_INLINE Step3 Process3(const PackView& pack, const Step2& in, bool fetch_val=false) {
	const auto pos = CalcPos(in);
	Step3 out;
	if (LIKELY(pos < pack.item)) {
		out.line = pack.content + pos*pack.line_size;
		PrefetchForNext(out.line);
		auto off = (uintptr_t)out.line & (CACHE_BLOCK_SIZE-1);
		auto blk = (const void*)(((uintptr_t)out.line & ~(uintptr_t)(CACHE_BLOCK_SIZE-1)) + CACHE_BLOCK_SIZE);
		if (off + pack.key_len > CACHE_BLOCK_SIZE) {
			PrefetchForNext(blk);
		} else if (fetch_val && off + pack.line_size > CACHE_BLOCK_SIZE) {
			PrefetchForFuture(blk);
		}
	} else {
		out.line = nullptr;
	}
	return out;
}


#ifndef PIPELINE_LEVEL
#define PIPELINE_LEVEL 4
#endif

#define BUBBLE(type) [](const type& in, size_t) -> type { return in; }

unsigned BatchSearch(const PackView& pack, unsigned batch, const uint8_t* const keys[], const uint8_t* out[]) {
	if (pack.type != Type::KV_INLINE && pack.type != Type::KEY_SET) {
		return 0;
	}

#if PIPELINE_LEVEL >= 3
#define BUBBLE_GROUP(type) BUBBLE(type), BUBBLE(type), BUBBLE(type),
#elif PIPELINE_LEVEL >= 2
#define BUBBLE_GROUP(type) BUBBLE(type), BUBBLE(type),
#elif PIPELINE_LEVEL >= 1
#define BUBBLE_GROUP(type) BUBBLE(type),
#else
#define BUBBLE_GROUP(type)
#endif

	unsigned hit = 0;
	Pipeline(batch,
			[&pack, keys](unsigned i) -> Step1 {
				return Process1(pack, keys[i]);
			},
			BUBBLE_GROUP(Step1)
			[](const Step1& in, unsigned) -> Step2 {
				return Process2(in);
			},
			BUBBLE_GROUP(Step2)
			[&pack](const Step2& in, unsigned) -> Step3 {
				return Process3(pack, in);
			},
			BUBBLE_GROUP(Step3)
			[&pack, &hit, keys, out](const Step3& in, unsigned i) {
				if (LIKELY(in.line != nullptr) && Equal(keys[i], in.line, pack.key_len)) {
					hit++;
					out[i] = in.line + pack.key_len;
				} else {
					out[i] = nullptr;
				}
			}
	);
	return hit;
#undef BUBBLE_GROUP
}

unsigned BatchFetch(const PackView& pack, const uint8_t* __restrict__ dft_val, unsigned batch,
				  const uint8_t* __restrict__ keys, uint8_t* __restrict__ data, unsigned* __restrict__ miss) {
	if (pack.type != Type::KV_INLINE) {
		return 0;
	}

#if PIPELINE_LEVEL >= 4
#define BUBBLE_GROUP(type) BUBBLE(type), BUBBLE(type), BUBBLE(type),
#elif PIPELINE_LEVEL >= 3
#define BUBBLE_GROUP(type) BUBBLE(type), BUBBLE(type),
#elif PIPELINE_LEVEL >= 2
#define BUBBLE_GROUP(type) BUBBLE(type),
#else
#define BUBBLE_GROUP(type)
#endif

	unsigned hit = 0;
	Pipeline(batch,
			[&pack, keys](unsigned i) -> Step1 {
				auto key = keys+i*pack.key_len;
				return Process1(pack, key);
			},
			BUBBLE_GROUP(Step1)
			[](const Step1& in, unsigned) -> Step2 {
				return Process2(in);
			},
			BUBBLE_GROUP(Step2)
			[&pack](const Step2& in, unsigned) -> Step3 {
				return Process3(pack, in, true);
			},
			BUBBLE_GROUP(Step3)
			[&pack, &hit, keys, data, dft_val, &miss](const Step3& in, unsigned i) {
				auto key = keys + i*pack.key_len;
				auto out = data + i*pack.val_len;
				auto src = in.line + pack.key_len;
				if (LIKELY(in.line != nullptr) && Equal(key, in.line, pack.key_len)) {
					hit++;
				} else if (dft_val != nullptr) {
					src = dft_val;
				} else if (miss != nullptr) {
					*miss++ = i;
				} else {
					return;
				}
				memcpy(out, src, pack.val_len);
			}
	);
	return hit;
#undef BUBBLE_GROUP
}

template <typename T>
struct Relay {
	const uint8_t* v;
	T s;
};

using Step4 = Relay<Step1>;
using Step5 = Relay<Step2>;
using Step6 = Relay<Step3>;

unsigned BatchSearch(const PackView& base, const PackView& patch,
					 unsigned batch, const uint8_t* const keys[], const uint8_t* out[]) {
	if ((base.type != Type::KV_INLINE && base.type != Type::KEY_SET)
		|| base.type != patch.type || base.key_len != patch.key_len) {
		return 0;
	}

#if PIPELINE_LEVEL >= 2
#define BUBBLE_GROUP(type) BUBBLE(type),
#else
#define BUBBLE_GROUP(type)
#endif

	unsigned hit = 0;
	Pipeline(batch,
			[&patch, keys](unsigned i) -> Step1 {
				return Process1(patch, keys[i]);
			},
			BUBBLE_GROUP(Step1)
			[](const Step1& in, unsigned) -> Step2 {
				return Process2(in);
			},
			BUBBLE_GROUP(Step2)
			[&patch](const Step2& in, unsigned) -> Step3 {
				return Process3(patch, in);
			},
			BUBBLE_GROUP(Step3)
			[&base, &patch, keys](const Step3& in, unsigned i) -> Step4 {
				if (LIKELY(in.line != nullptr) && Equal(keys[i], in.line, patch.key_len)) {
					return {in.line + patch.key_len, Step1{}};
				} else {
					return {nullptr, Process1(base, keys[i])};
				}
			},
			BUBBLE_GROUP(Step4)
			[](const Step4& in, unsigned) -> Step5 {
				if (in.v != nullptr) {
					return {in.v, Step2{}};
				} else {
					return {nullptr, Process2(in.s)};
				}
			},
			BUBBLE_GROUP(Step5)
			[&base](const Step5& in, unsigned) -> Step6 {
				if (in.v != nullptr) {
					return {in.v, Step3{}};
				} else {
					return {nullptr, Process3(base, in.s)};
				}
			},
			BUBBLE_GROUP(Step6)
			[&base, &hit, keys, out](const Step6& in, unsigned i) {
				if (in.v != nullptr) {
					hit++;
					out[i] = in.v;
				} else if (LIKELY(in.s.line != nullptr) && Equal(keys[i], in.s.line, base.key_len)) {
					hit++;
					out[i] = in.s.line + base.key_len;
				} else {
					out[i] = nullptr;
				}
			}
	);
	return hit;
#undef BUBBLE_GROUP
}

unsigned BatchFetch(const PackView& base, const PackView& patch, const uint8_t* __restrict__ dft_val, unsigned batch,
				  const uint8_t* __restrict__ keys, uint8_t* __restrict__ data, unsigned* __restrict__ miss) {
	if (base.type != Type::KV_INLINE || base.type != patch.type
		|| base.key_len != patch.key_len || base.val_len != patch.val_len) {
		return 0;
	}

#if PIPELINE_LEVEL >= 3
#define BUBBLE_GROUP(type) BUBBLE(type),
#else
#define BUBBLE_GROUP(type)
#endif

	unsigned hit = 0;
	Pipeline(batch,
			[&patch, keys](unsigned i) -> Step1 {
				auto key = keys+i*patch.key_len;
				return Process1(patch, key);
			},
			BUBBLE_GROUP(Step1)
			[](const Step1& in, unsigned) -> Step2 {
				return Process2(in);
			},
			BUBBLE_GROUP(Step2)
			[&patch](const Step2& in, unsigned) -> Step3 {
				return Process3(patch, in, true);
			},
			BUBBLE_GROUP(Step3)
			[&base, &patch, keys](const Step3& in, unsigned i) -> Step4 {
				auto key = keys + i*base.key_len;
				if (LIKELY(in.line != nullptr) && Equal(key, in.line, patch.key_len)) {
					return {in.line + patch.key_len, Step1{}};
				} else {
					return {nullptr, Process1(base, key)};
				}
			},
			BUBBLE_GROUP(Step4)
			[](const Step4& in, unsigned) -> Step5 {
				if (in.v != nullptr) {
					return {in.v, Step2{}};
				} else {
					return {nullptr, Process2(in.s)};
				}
			},
			BUBBLE_GROUP(Step5)
			[&base](const Step5& in, unsigned) -> Step6 {
				if (in.v != nullptr) {
					return {in.v, Step3{}};
				} else {
					return {nullptr, Process3(base, in.s, true)};
				}
			},
			BUBBLE_GROUP(Step6)
			[&base, &hit, keys, data, dft_val, &miss](const Step6& in, unsigned i) {
				auto key = keys + i*base.key_len;
				auto out = data + i*base.val_len;
				auto src = in.v;
				if (src != nullptr) {
					hit++;
				} else if (LIKELY(in.s.line != nullptr) && Equal(key, in.s.line, base.key_len)) {
					hit++;
					src = in.s.line + base.key_len;
				} else if (dft_val != nullptr) {
					src = dft_val;
				} else if (miss != nullptr) {
					*miss++ = i;
				} else {
					return;
				}
				memcpy(out, src, base.val_len);
			}
	);
	return hit;
#undef BUBBLE_GROUP
}

static constexpr unsigned PIPELINE_BUFFER_SIZE = 16;	//bigger than depth of pipeline
static_assert((PIPELINE_BUFFER_SIZE & (PIPELINE_BUFFER_SIZE-1)) == 0, "");

void BatchFindPos(const PackView& pack, size_t batch, const std::function<const uint8_t*(uint8_t*)>& reader,
				const std::function<void(uint64_t)>& output, const uint8_t* bitmap) {
	if (pack.type == Type::INDEX_ONLY) return;
	auto buf = std::make_unique<uint8_t[]>(PIPELINE_BUFFER_SIZE*pack.key_len);
	typedef const uint8_t* Pointer;
	auto cache = std::make_unique<Pointer[]>(PIPELINE_BUFFER_SIZE);
	struct Step3X {
		uint64_t pos;
		const uint8_t* line;
	};

#if PIPELINE_LEVEL >= 4
#define BUBBLE_GROUP(type) BUBBLE(type), BUBBLE(type),
#elif PIPELINE_LEVEL >= 3
#define BUBBLE_GROUP(type) BUBBLE(type),
#else
#define BUBBLE_GROUP(type)
#endif

	Pipeline(batch,
			[&pack, &cache, &buf, &reader](size_t i) -> Step1 {
				auto j = i & (PIPELINE_BUFFER_SIZE-1);
				cache[j] = reader(buf.get() + j*pack.key_len);
				return Process1(pack, cache[j], pack.key_len);
			},
			BUBBLE_GROUP(Step1)
			[](const Step1& in, size_t) -> Step2 {
				return Process2(in);
			},
			BUBBLE_GROUP(Step2)
			[&pack, bitmap](const Step2& in, size_t) -> Step3X {
				const auto pos = CalcPos(in);
				assert(pos < pack.item);
				auto line = pack.content + pos*pack.line_size;
				PrefetchForNext(line);
				if (bitmap != nullptr) {
					PrefetchBit(bitmap,pos);
				}
				return {pos, line};
			},
			BUBBLE_GROUP(Step3X)
			[&pack, &cache, &output](const Step3X& in, size_t i) {
				if (Equal(cache[i&(PIPELINE_BUFFER_SIZE-1)], in.line, pack.key_len)) {
					output(in.pos);
				} else {
					output(UINT64_MAX);
				}
			}
	);
#undef BUBBLE_GROUP
}

void BatchDataMapping(const PackView& index, uint8_t* space, size_t batch,
				const std::function<const uint8_t*(uint8_t*)>& reader) {
	auto buf = std::make_unique<uint8_t[]>(PIPELINE_BUFFER_SIZE*index.line_size);
	typedef const uint8_t* Pointer;
	auto cache = std::make_unique<Pointer[]>(PIPELINE_BUFFER_SIZE);
	struct Step3X {
		uint8_t* line;
	};

#if PIPELINE_LEVEL >= 5
#define BUBBLE_GROUP(type) BUBBLE(type), BUBBLE(type),
#elif PIPELINE_LEVEL >= 3
#define BUBBLE_GROUP(type) BUBBLE(type),
#else
#define BUBBLE_GROUP(type)
#endif

	Pipeline(batch,
			[&index, &cache, &buf, &reader](size_t i) -> Step1 {
				auto j = i & (PIPELINE_BUFFER_SIZE-1);
				cache[j] = reader(buf.get() + j*index.line_size);
				return Process1(index, cache[j], index.key_len);
			},
			BUBBLE_GROUP(Step1)
			[](const Step1& in, size_t) -> Step2 {
				return Process2(in);
			},
			BUBBLE_GROUP(Step2)
			[space, &index](const Step2& in, size_t) -> Step3X {
				Step3X out;
				out.line = space + CalcPos(in)*index.line_size;
				PrefetchForWrite(out.line);
				auto off = (uintptr_t)out.line & (CACHE_BLOCK_SIZE-1);
				auto blk = (const void*)(((uintptr_t)out.line & ~(uintptr_t)(CACHE_BLOCK_SIZE-1)) + CACHE_BLOCK_SIZE);
				if (off + index.line_size > CACHE_BLOCK_SIZE) {
					PrefetchForWrite(blk);
				}
				return out;
			},
			BUBBLE_GROUP(Step3X)
			[&cache, &index](const Step3X& in, size_t i) {
				memcpy(in.line, cache[i&(PIPELINE_BUFFER_SIZE-1)], index.line_size);
			}
	);
#undef BUBBLE_GROUP
}

} //shd
