//==============================================================================
// A modern implement of CHD algorithm.
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

namespace chd {

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
	out.l1pos = L1Hash(out.id) % out.seg->l1sz;
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

void BatchLocate(const PackView& index, unsigned batch, const uint8_t* __restrict__ keys,
				 uint8_t key_len, uint64_t* __restrict__ out) {
	Pipeline<8>(batch,
				[&index, keys, key_len](unsigned i) -> Step1 {
					return Process1(index, keys+i*key_len, key_len);
				},
				[](const Step1& in, unsigned) -> Step2 {
					return Process2(in);
				},
				[&out](const Step2& in, unsigned i) {
					out[i] = CalcPos(in);
				}
	);
}

unsigned BatchSearch(const PackView& pack, unsigned batch, const uint8_t* const keys[], const uint8_t* out[]) {
	if (pack.type != Type::KV_INLINE && pack.type != Type::KEY_SET) {
		return 0;
	}
	unsigned hit = 0;
	Pipeline<7>(batch,
			 [&pack, keys](unsigned i) -> Step1 {
				 return Process1(pack, keys[i]);
			 },
			 [](const Step1& in, unsigned) -> Step2 {
				 return Process2(in);
			 },
			 [&pack](const Step2& in, unsigned) -> Step3 {
				 return Process3(pack, in);
			 },
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
}

unsigned BatchFetch(const PackView& pack, const uint8_t* __restrict__ dft_val, unsigned batch,
				  const uint8_t* __restrict__ keys, uint8_t* __restrict__ data, unsigned* __restrict__ miss) {
	if (pack.type != Type::KV_INLINE) {
		return 0;
	}
	unsigned hit = 0;
	Pipeline<6>(batch,
				[&pack, keys](unsigned i) -> Step1 {
					auto key = keys+i*pack.key_len;
					return Process1(pack, key);
				},
				[](const Step1& in, unsigned) -> Step2 {
					return Process2(in);
				},
				[&pack](const Step2& in, unsigned) -> Step3 {
					return Process3(pack, in, true);
				},
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

	unsigned hit = 0;
	Pipeline<4>(batch,
			[&patch, keys](unsigned i) -> Step1 {
				return Process1(patch, keys[i]);
			},
			[](const Step1& in, unsigned) -> Step2 {
				return Process2(in);
			},
			[&patch](const Step2& in, unsigned) -> Step3 {
				return Process3(patch, in);
			},
			[&base, &patch, keys](const Step3& in, unsigned i) -> Step4 {
				if (LIKELY(in.line != nullptr) && Equal(keys[i], in.line, patch.key_len)) {
					return {in.line + patch.key_len, Step1{}};
				} else {
					return {nullptr, Process1(base, keys[i])};
				}
			},
			[](const Step4& in, unsigned) -> Step5 {
				if (in.v != nullptr) {
					return {in.v, Step2{}};
				} else {
					return {nullptr, Process2(in.s)};
				}
			},
			[&base](const Step5& in, unsigned) -> Step6 {
				if (in.v != nullptr) {
					return {in.v, Step3{}};
				} else {
					return {nullptr, Process3(base, in.s)};
				}
			},
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
}

unsigned BatchFetch(const PackView& base, const PackView& patch, const uint8_t* __restrict__ dft_val, unsigned batch,
				  const uint8_t* __restrict__ keys, uint8_t* __restrict__ data, unsigned* __restrict__ miss) {
	if (base.type != Type::KV_INLINE || base.type != patch.type
		|| base.key_len != patch.key_len || base.val_len != patch.val_len) {
		return 0;
	}

	unsigned hit = 0;
	Pipeline<3>(batch,
			[&patch, keys](unsigned i) -> Step1 {
				auto key = keys+i*patch.key_len;
				return Process1(patch, key);
			},
			[](const Step1& in, unsigned) -> Step2 {
				return Process2(in);
			},
			[&patch](const Step2& in, unsigned) -> Step3 {
				return Process3(patch, in, true);
			},
			[&base, &patch, keys](const Step3& in, unsigned i) -> Step4 {
				auto key = keys + i*base.key_len;
				if (LIKELY(in.line != nullptr) && Equal(key, in.line, patch.key_len)) {
					return {in.line + patch.key_len, Step1{}};
				} else {
					return {nullptr, Process1(base, key)};
				}
			},
			[](const Step4& in, unsigned) -> Step5 {
				if (in.v != nullptr) {
					return {in.v, Step2{}};
				} else {
					return {nullptr, Process2(in.s)};
				}
			},
			[&base](const Step5& in, unsigned) -> Step6 {
				if (in.v != nullptr) {
					return {in.v, Step3{}};
				} else {
					return {nullptr, Process3(base, in.s, true)};
				}
			},
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
}

static constexpr unsigned WINDOW_SIZE = 32;

void BatchFindPos(const PackView& pack, size_t batch, const std::function<void(uint8_t*)>& reader,
				const std::function<void(uint64_t)>& output, const uint8_t* bitmap) {
	if (pack.type == Type::INDEX_ONLY) return;
	auto buf = std::make_unique<uint8_t[]>(WINDOW_SIZE*pack.key_len);

	union {
		Step1 s1;
		Step2 s2;
		struct {
			uint64_t pos;
			const uint8_t* line;
		} s3;
	} state[WINDOW_SIZE];

	for (size_t i = 0; i < batch; i += WINDOW_SIZE) {
		auto m = std::min(static_cast<size_t>(WINDOW_SIZE), batch-i);
		for (unsigned j = 0; j < m; j++) {
			auto key = buf.get() + j * pack.key_len;
			reader(key);
			state[j].s1 = Process1(pack, key);
		}
		for (unsigned j = 0; j < m; j++) {
			state[j].s2 = Process2(state[j].s1);
		}
		for (unsigned j = 0; j < m; j++) {
			auto pos = CalcPos(state[j].s2);
			assert(pos < pack.item);
			auto line = pack.content + pos*pack.line_size;
			PrefetchForNext(line);
			if (bitmap != nullptr) {
				PrefetchBit(bitmap,pos);
			}
			state[j].s3 = {pos, line};
		}
		for (unsigned j = 0; j < m; j++) {
			auto key = buf.get() + j * pack.key_len;
			auto& s = state[j].s3;
			if (Equal(key, s.line, pack.key_len)) {
				output(s.pos);
			} else {
				output(UINT64_MAX);
			}
		}
	}
}

void BatchDataMapping(const PackView& index, uint8_t* space, size_t batch, const std::function<void(uint8_t*)>& reader) {
	auto buf = std::make_unique<uint8_t[]>(WINDOW_SIZE*index.line_size);

	union {
		Step1 s1;
		Step2 s2;
		struct {
			uint8_t* line;
		} s3;
	} state[WINDOW_SIZE];

	for (size_t i = 0; i < batch; i += WINDOW_SIZE) {
		auto m = std::min(static_cast<size_t>(WINDOW_SIZE), batch - i);
		for (unsigned j = 0; j < m; j++) {
			auto line = buf.get() + j * index.line_size;
			reader(line);
			state[j].s1 = Process1(index, line);
		}
		for (unsigned j = 0; j < m; j++) {
			state[j].s2 = Process2(state[j].s1);
		}
		for (unsigned j = 0; j < m; j++) {
			auto line = space + CalcPos(state[j].s2)*index.line_size;
			PrefetchForWrite(line);
			auto off = (uintptr_t)line & (CACHE_BLOCK_SIZE-1);
			auto blk = (const void*)(((uintptr_t)line & ~(uintptr_t)(CACHE_BLOCK_SIZE-1)) + CACHE_BLOCK_SIZE);
			if (off + index.line_size > CACHE_BLOCK_SIZE) {
				PrefetchForWrite(blk);
			}
			state[j].s3.line = line;
		}
		for (unsigned j = 0; j < m; j++) {
			auto line = buf.get() + j * index.line_size;
			auto& s = state[j].s3;
			memcpy(s.line, line, index.line_size);
		}
	}
}

} //chd
