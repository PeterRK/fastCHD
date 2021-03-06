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
#include <tuple>
#include <variant>
#include <type_traits>
#include "internal.h"

namespace chd {

struct Step1 {
	const SegmentView* seg;
	V96 id;
	uint32_t l1pos;
};

struct Step2 {
	const SegmentView* seg;
	uint32_t section;
	unsigned bit_off;
};

struct Step3 {
	const uint8_t* line;
};

static FORCE_INLINE Step1 Process1(const PackView& index, const uint8_t* key, uint8_t key_len) {
	Step1 out;
	out.id = GenID(index.seed, key, key_len);
	out.seg = &index.segments[L0Hash(out.id) % index.l0sz];
	out.l1pos = L1Hash(out.id) % out.seg->l1sz;
	PrefetchForNext(&out.seg->cells[out.l1pos]);
	return out;
}

static FORCE_INLINE Step1 Process1(const PackView& pack, const uint8_t* key) {
	return Process1(pack, key, pack.key_len);
}

static FORCE_INLINE Step2 Process2(const Step1& in) {
	Step2 out;
	out.seg = in.seg;
	const auto bit_pos = L2Hash(in.id, in.seg->cells[in.l1pos]) % in.seg->l2sz;
	out.section = bit_pos / BITMAP_SECTION_SIZE;
	out.bit_off = bit_pos % BITMAP_SECTION_SIZE;
	PrefetchForNext(&out.seg->sections[out.section]);
	return out;
}

static FORCE_INLINE uint64_t CalcPos(const Step2& in) {
	auto& section = in.seg->sections[in.section];
	uint32_t cnt = section.step;
	if (in.bit_off < 32U) {
		int32_t mask = 0x80000000;
		mask >>= 31U - (in.bit_off&31U);
		cnt += PopCount32(section.b32[0] & ~mask);
	} else {
		cnt += PopCount32(section.b32[0]);
		auto v = (const uint64_t*)(section.b32 + 1);
		uint8_t off = in.bit_off - 32U;
		switch (off/64U) {
			case 3:
			case 2: cnt += PopCount64(*v++);
			case 1: cnt += PopCount64(*v++);
			case 0:
				int64_t mask = 0x8000000000000000LL;
				mask >>= 63U - (off&63U);
				cnt += PopCount64(*v & ~mask);
		}
	}
	return in.seg->offset + cnt;
}

uint64_t CalcPos(const PackView& index, const uint8_t* key, uint8_t key_len) {
	return CalcPos(Process2(Process1(index, key, key_len)));
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

template <typename P1, typename P2, typename P3, typename P4>
static FORCE_INLINE void Pipeline(size_t n, const P1& p1, const P2& p2, const P3& p3, const P4& p4) {
	using S1 = std::result_of_t<P1(size_t)>;
	using S2 = std::result_of_t<P2(S1,size_t)>;
	using S3 = std::result_of_t<P3(S2,size_t)>;

	if (n < 3) {
		std::variant<int,S1,S2,S3> t[2];
		for (size_t i = 0; i < n; i++) std::get<1>(t[i]) = p1(i);
		for (size_t i = 0; i < n; i++) std::get<2>(t[i]) = p2(std::get<1>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<3>(t[i]) = p3(std::get<2>(t[i]), i);
		for (size_t i = 0; i < n; i++) p4(std::get<3>(t[i]), i);
		return;
	}

	S1 s1; S2 s2; S3 s3;

	s1 = p1(0);

	s2 = p2(s1, 0);
	s1 = p1(1);

	s3 = p3(s2, 0);
	s2 = p2(s1, 1);
	s1 = p1(2);

	for (size_t i = 3; i < n; i++) {
		p4(s3, i-3);
		s3 = p3(s2, i-2);
		s2 = p2(s1, i-1);
		s1 = p1(i);
	}

	p4(s3, n-3);
	s3 = p3(s2, n-2);
	s2 = p2(s1, n-1);

	p4(s3, n-2);
	s3 = p3(s2, n-1);

	p4(s3, n-1);
}

template <typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7>
static FORCE_INLINE void Pipeline(size_t n, const P1& p1, const P2& p2, const P3& p3,
								  const P4& p4, const P5& p5, const P6& p6, const P7& p7) {
	using S1 = std::result_of_t<P1(size_t)>;
	using S2 = std::result_of_t<P2(S1,size_t)>;
	using S3 = std::result_of_t<P3(S2,size_t)>;
	using S4 = std::result_of_t<P4(S3,size_t)>;
	using S5 = std::result_of_t<P5(S4,size_t)>;
	using S6 = std::result_of_t<P6(S5,size_t)>;

	if (n < 6) {
		std::variant<int,S1,S2,S3,S4,S5,S6> t[5];
		for (size_t i = 0; i < n; i++) std::get<1>(t[i]) = p1(i);
		for (size_t i = 0; i < n; i++) std::get<2>(t[i]) = p2(std::get<1>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<3>(t[i]) = p3(std::get<2>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<4>(t[i]) = p4(std::get<3>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<5>(t[i]) = p5(std::get<4>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<6>(t[i]) = p6(std::get<5>(t[i]), i);
		for (size_t i = 0; i < n; i++) p7(std::get<6>(t[i]), i);
		return;
	}

	S1 s1; S2 s2; S3 s3;
	S4 s4; S5 s5; S6 s6;

	s1 = p1(0);

	s2 = p2(s1, 0);
	s1 = p1(1);

	s3 = p3(s2, 0);
	s2 = p2(s1, 1);
	s1 = p1(2);

	s4 = p4(s3, 0);
	s3 = p3(s2, 1);
	s2 = p2(s1, 2);
	s1 = p1(3);

	s5 = p5(s4, 0);
	s4 = p4(s3, 1);
	s3 = p3(s2, 2);
	s2 = p2(s1, 3);
	s1 = p1(4);

	s6 = p6(s5, 0);
	s5 = p5(s4, 1);
	s4 = p4(s3, 2);
	s3 = p3(s2, 3);
	s2 = p2(s1, 4);
	s1 = p1(5);

	for (size_t i = 6; i < n; i++) {
		p7(s6, i-6);
		s6 = p6(s5, i-5);
		s5 = p5(s4, i-4);
		s4 = p4(s3, i-3);
		s3 = p3(s2, i-2);
		s2 = p2(s1, i-1);
		s1 = p1(i);
	}

	p7(s6, n-6);
	s6 = p6(s5, n-5);
	s5 = p5(s4, n-4);
	s4 = p4(s3, n-3);
	s3 = p3(s2, n-2);
	s2 = p2(s1, n-1);

	p7(s6, n-5);
	s6 = p6(s5, n-4);
	s5 = p5(s4, n-3);
	s4 = p4(s3, n-2);
	s3 = p3(s2, n-1);

	p7(s6, n-4);
	s6 = p6(s5, n-3);
	s5 = p5(s4, n-2);
	s4 = p4(s3, n-1);

	p7(s6, n-3);
	s6 = p6(s5, n-2);
	s5 = p5(s4, n-1);

	p7(s6, n-2);
	s6 = p6(s5, n-1);

	p7(s6, n-1);
}

template <typename P1, typename P2, typename P3, typename P4, typename P5,
	typename P6, typename P7, typename P8, typename P9, typename P10>
static FORCE_INLINE void Pipeline(size_t n, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5,
								  const P6& p6, const P7& p7, const P8& p8, const P9& p9, const P10& p10) {
	using S1 = std::result_of_t<P1(size_t)>;
	using S2 = std::result_of_t<P2(S1,size_t)>;
	using S3 = std::result_of_t<P3(S2,size_t)>;
	using S4 = std::result_of_t<P4(S3,size_t)>;
	using S5 = std::result_of_t<P5(S4,size_t)>;
	using S6 = std::result_of_t<P6(S5,size_t)>;
	using S7 = std::result_of_t<P7(S6,size_t)>;
	using S8 = std::result_of_t<P8(S7,size_t)>;
	using S9 = std::result_of_t<P9(S8,size_t)>;
	if (n < 9) {
		std::variant<int,S1,S2,S3,S4,S5,S6,S7,S8,S9> t[8];
		for (size_t i = 0; i < n; i++) std::get<1>(t[i]) = p1(i);
		for (size_t i = 0; i < n; i++) std::get<2>(t[i]) = p2(std::get<1>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<3>(t[i]) = p3(std::get<2>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<4>(t[i]) = p4(std::get<3>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<5>(t[i]) = p5(std::get<4>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<6>(t[i]) = p6(std::get<5>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<7>(t[i]) = p7(std::get<6>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<8>(t[i]) = p8(std::get<7>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<9>(t[i]) = p9(std::get<8>(t[i]), i);
		for (size_t i = 0; i < n; i++) p10(std::get<9>(t[i]), i);
		return;
	}

	S1 s1; S2 s2; S3 s3;
	S4 s4; S5 s5; S6 s6;
	S7 s7; S8 s8; S9 s9;

	s1 = p1(0);

	s2 = p2(s1, 0);
	s1 = p1(1);

	s3 = p3(s2, 0);
	s2 = p2(s1, 1);
	s1 = p1(2);

	s4 = p4(s3, 0);
	s3 = p3(s2, 1);
	s2 = p2(s1, 2);
	s1 = p1(3);

	s5 = p5(s4, 0);
	s4 = p4(s3, 1);
	s3 = p3(s2, 2);
	s2 = p2(s1, 3);
	s1 = p1(4);

	s6 = p6(s5, 0);
	s5 = p5(s4, 1);
	s4 = p4(s3, 2);
	s3 = p3(s2, 3);
	s2 = p2(s1, 4);
	s1 = p1(5);

	s7 = p7(s6, 0);
	s6 = p6(s5, 1);
	s5 = p5(s4, 2);
	s4 = p4(s3, 3);
	s3 = p3(s2, 4);
	s2 = p2(s1, 5);
	s1 = p1(6);

	s8 = p8(s7, 0);
	s7 = p7(s6, 1);
	s6 = p6(s5, 2);
	s5 = p5(s4, 3);
	s4 = p4(s3, 4);
	s3 = p3(s2, 5);
	s2 = p2(s1, 6);
	s1 = p1(7);

	s9 = p9(s8, 0);
	s8 = p8(s7, 1);
	s7 = p7(s6, 2);
	s6 = p6(s5, 3);
	s5 = p5(s4, 4);
	s4 = p4(s3, 5);
	s3 = p3(s2, 6);
	s2 = p2(s1, 7);
	s1 = p1(8);

	for (size_t i = 9; i < n; i++) {
		p10(s9, i-9);
		s9 = p9(s8, i-8);
		s8 = p8(s7, i-7);
		s7 = p7(s6, i-6);
		s6 = p6(s5, i-5);
		s5 = p5(s4, i-4);
		s4 = p4(s3, i-3);
		s3 = p3(s2, i-2);
		s2 = p2(s1, i-1);
		s1 = p1(i);
	}

	p10(s9, n-9);
	s9 = p9(s8, n-8);
	s8 = p8(s7, n-7);
	s7 = p7(s6, n-6);
	s6 = p6(s5, n-5);
	s5 = p5(s4, n-4);
	s4 = p4(s3, n-3);
	s3 = p3(s2, n-2);
	s2 = p2(s1, n-1);

	p10(s9, n-8);
	s9 = p9(s8, n-7);
	s8 = p8(s7, n-6);
	s7 = p7(s6, n-5);
	s6 = p6(s5, n-4);
	s5 = p5(s4, n-3);
	s4 = p4(s3, n-2);
	s3 = p3(s2, n-1);

	p10(s9, n-7);
	s9 = p9(s8, n-6);
	s8 = p8(s7, n-5);
	s7 = p7(s6, n-4);
	s6 = p6(s5, n-3);
	s5 = p5(s4, n-2);
	s4 = p4(s3, n-1);

	p10(s9, n-6);
	s9 = p9(s8, n-5);
	s8 = p8(s7, n-4);
	s7 = p7(s6, n-3);
	s6 = p6(s5, n-2);
	s5 = p5(s4, n-1);

	p10(s9, n-5);
	s9 = p9(s8, n-4);
	s8 = p8(s7, n-3);
	s7 = p7(s6, n-2);
	s6 = p6(s5, n-1);

	p10(s9, n-4);
	s9 = p9(s8, n-3);
	s8 = p8(s7, n-2);
	s7 = p7(s6, n-1);

	p10(s9, n-3);
	s9 = p9(s8, n-2);
	s8 = p8(s7, n-1);

	p10(s9, n-2);
	s9 = p9(s8, n-1);

	p10(s9, n-1);
}


template <typename P01, typename P02, typename P03, typename P04, typename P05, typename P06, typename P07,
        typename P08, typename P09, typename P10, typename P11, typename P12, typename P13>
static FORCE_INLINE void Pipeline(size_t n, const P01& p01, const P02& p02, const P03& p03, const P04& p04,
								  const P05& p05, const P06& p06, const P07& p07, const P08& p08, const P09& p09,
								  const P10& p10, const P11& p11, const P12& p12, const P13& p13) {
	using S01 = std::result_of_t<P01(size_t)>;
	using S02 = std::result_of_t<P02(S01,size_t)>;
	using S03 = std::result_of_t<P03(S02,size_t)>;
	using S04 = std::result_of_t<P04(S03,size_t)>;
	using S05 = std::result_of_t<P05(S04,size_t)>;
	using S06 = std::result_of_t<P06(S05,size_t)>;
	using S07 = std::result_of_t<P07(S06,size_t)>;
	using S08 = std::result_of_t<P08(S07,size_t)>;
	using S09 = std::result_of_t<P09(S08,size_t)>;
	using S10 = std::result_of_t<P10(S09,size_t)>;
	using S11 = std::result_of_t<P11(S10,size_t)>;
	using S12 = std::result_of_t<P12(S11,size_t)>;

	if (n < 12) {
		std::variant<int,S01,S02,S03,S04,S05,S06,S07,S08,S09,S10,S11,S12> t[11];
		for (size_t i = 0; i < n; i++) std::get<1>(t[i]) = p01(i);
		for (size_t i = 0; i < n; i++) std::get<2>(t[i]) = p02(std::get<1>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<3>(t[i]) = p03(std::get<2>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<4>(t[i]) = p04(std::get<3>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<5>(t[i]) = p05(std::get<4>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<6>(t[i]) = p06(std::get<5>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<7>(t[i]) = p07(std::get<6>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<8>(t[i]) = p08(std::get<7>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<9>(t[i]) = p09(std::get<8>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<10>(t[i]) = p10(std::get<9>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<11>(t[i]) = p11(std::get<10>(t[i]), i);
		for (size_t i = 0; i < n; i++) std::get<12>(t[i]) = p12(std::get<11>(t[i]), i);
		for (size_t i = 0; i < n; i++) p13(std::get<12>(t[i]), i);
		return;
	}

	S01 s01; S02 s02; S03 s03;
	S04 s04; S05 s05; S06 s06;
	S07 s07; S08 s08; S09 s09;
	S10 s10; S11 s11; S12 s12;

	s01 = p01(0);

	s02 = p02(s01, 0);
	s01 = p01(1);

	s03 = p03(s02, 0);
	s02 = p02(s01, 1);
	s01 = p01(2);

	s04 = p04(s03, 0);
	s03 = p03(s02, 1);
	s02 = p02(s01, 2);
	s01 = p01(3);

	s05 = p05(s04, 0);
	s04 = p04(s03, 1);
	s03 = p03(s02, 2);
	s02 = p02(s01, 3);
	s01 = p01(4);

	s06 = p06(s05, 0);
	s05 = p05(s04, 1);
	s04 = p04(s03, 2);
	s03 = p03(s02, 3);
	s02 = p02(s01, 4);
	s01 = p01(5);

	s07 = p07(s06, 0);
	s06 = p06(s05, 1);
	s05 = p05(s04, 2);
	s04 = p04(s03, 3);
	s03 = p03(s02, 4);
	s02 = p02(s01, 5);
	s01 = p01(6);

	s08 = p08(s07, 0);
	s07 = p07(s06, 1);
	s06 = p06(s05, 2);
	s05 = p05(s04, 3);
	s04 = p04(s03, 4);
	s03 = p03(s02, 5);
	s02 = p02(s01, 6);
	s01 = p01(7);

	s09 = p09(s08, 0);
	s08 = p08(s07, 1);
	s07 = p07(s06, 2);
	s06 = p06(s05, 3);
	s05 = p05(s04, 4);
	s04 = p04(s03, 5);
	s03 = p03(s02, 6);
	s02 = p02(s01, 7);
	s01 = p01(8);

	s10 = p10(s09, 0);
	s09 = p09(s08, 1);
	s08 = p08(s07, 2);
	s07 = p07(s06, 3);
	s06 = p06(s05, 4);
	s05 = p05(s04, 5);
	s04 = p04(s03, 6);
	s03 = p03(s02, 7);
	s02 = p02(s01, 8);
	s01 = p01(9);

	s11 = p11(s10, 0);
	s10 = p10(s09, 1);
	s09 = p09(s08, 2);
	s08 = p08(s07, 3);
	s07 = p07(s06, 4);
	s06 = p06(s05, 5);
	s05 = p05(s04, 6);
	s04 = p04(s03, 7);
	s03 = p03(s02, 8);
	s02 = p02(s01, 9);
	s01 = p01(10);

	s12 = p12(s11, 0);
	s11 = p11(s10, 1);
	s10 = p10(s09, 2);
	s09 = p09(s08, 3);
	s08 = p08(s07, 4);
	s07 = p07(s06, 5);
	s06 = p06(s05, 6);
	s05 = p05(s04, 7);
	s04 = p04(s03, 8);
	s03 = p03(s02, 9);
	s02 = p02(s01, 10);
	s01 = p01(11);

	for (size_t i = 12; i < n; i++) {
		p13(s12, i-12);
		s12 = p12(s11, i-11);
		s11 = p11(s10, i-10);
		s10 = p10(s09, i-9);
		s09 = p09(s08, i-8);
		s08 = p08(s07, i-7);
		s07 = p07(s06, i-6);
		s06 = p06(s05, i-5);
		s05 = p05(s04, i-4);
		s04 = p04(s03, i-3);
		s03 = p03(s02, i-2);
		s02 = p02(s01, i-1);
		s01 = p01(i);
	}

	p13(s12, n-12);
	s12 = p12(s11, n-11);
	s11 = p11(s10, n-10);
	s10 = p10(s09, n-9);
	s09 = p09(s08, n-8);
	s08 = p08(s07, n-7);
	s07 = p07(s06, n-6);
	s06 = p06(s05, n-5);
	s05 = p05(s04, n-4);
	s04 = p04(s03, n-3);
	s03 = p03(s02, n-2);
	s02 = p02(s01, n-1);

	p13(s12, n-11);
	s12 = p12(s11, n-10);
	s11 = p11(s10, n-9);
	s10 = p10(s09, n-8);
	s09 = p09(s08, n-7);
	s08 = p08(s07, n-6);
	s07 = p07(s06, n-5);
	s06 = p06(s05, n-4);
	s05 = p05(s04, n-3);
	s04 = p04(s03, n-2);
	s03 = p03(s02, n-1);

	p13(s12, n-10);
	s12 = p12(s11, n-9);
	s11 = p11(s10, n-8);
	s10 = p10(s09, n-7);
	s09 = p09(s08, n-6);
	s08 = p08(s07, n-5);
	s07 = p07(s06, n-4);
	s06 = p06(s05, n-3);
	s05 = p05(s04, n-2);
	s04 = p04(s03, n-1);

	p13(s12, n-9);
	s12 = p12(s11, n-8);
	s11 = p11(s10, n-7);
	s10 = p10(s09, n-6);
	s09 = p09(s08, n-5);
	s08 = p08(s07, n-4);
	s07 = p07(s06, n-3);
	s06 = p06(s05, n-2);
	s05 = p05(s04, n-1);

	p13(s12, n-8);
	s12 = p12(s11, n-7);
	s11 = p11(s10, n-6);
	s10 = p10(s09, n-5);
	s09 = p09(s08, n-4);
	s08 = p08(s07, n-3);
	s07 = p07(s06, n-2);
	s06 = p06(s05, n-1);

	p13(s12, n-7);
	s12 = p12(s11, n-6);
	s11 = p11(s10, n-5);
	s10 = p10(s09, n-4);
	s09 = p09(s08, n-3);
	s08 = p08(s07, n-2);
	s07 = p07(s06, n-1);

	p13(s12, n-6);
	s12 = p12(s11, n-5);
	s11 = p11(s10, n-4);
	s10 = p10(s09, n-3);
	s09 = p09(s08, n-2);
	s08 = p08(s07, n-1);

	p13(s12, n-5);
	s12 = p12(s11, n-4);
	s11 = p11(s10, n-3);
	s10 = p10(s09, n-2);
	s09 = p09(s08, n-1);

	p13(s12, n-4);
	s12 = p12(s11, n-3);
	s11 = p11(s10, n-2);
	s10 = p10(s09, n-1);

	p13(s12, n-3);
	s12 = p12(s11, n-2);
	s11 = p11(s10, n-1);

	p13(s12, n-2);
	s12 = p12(s11, n-1);

	p13(s12, n-1);
}

#ifndef PIPELINE_LEVEL
#define PIPELINE_LEVEL 2
#endif

size_t BatchSearch(const PackView& pack, size_t batch, const uint8_t* const keys[], const uint8_t* out[]) {
	if (pack.type != Type::KV_INLINE && pack.type != Type::KEY_SET) {
		return 0;
	}
	size_t hit = 0;

	Pipeline(batch,
			 [&pack, keys](size_t i) -> Step1 {
				 return Process1(pack, keys[i]);
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 1
			 [](const Step1& in, size_t) -> Step1 { return in; },
#if PIPELINE_LEVEL >= 2
			 [](const Step1& in, size_t) -> Step1 { return in; },
#endif
#endif
			 [](const Step1& in, size_t) -> Step2 {
				 return Process2(in);
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 1
			 [](const Step2& in, size_t) -> Step2 { return in; },
#if PIPELINE_LEVEL >= 2
			 [](const Step2& in, size_t) -> Step2 { return in; },
#endif
#endif
			 [&pack](const Step2& in, size_t) -> Step3 {
				 return Process3(pack, in);
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 1
			 [](const Step3& in, size_t) -> Step3 { return in; },
#if PIPELINE_LEVEL >= 2
			 [](const Step3& in, size_t) -> Step3 { return in; },
#endif
#endif
			 [&pack, &hit, keys, out](const Step3& in, size_t i) {
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

size_t BatchFetch(const PackView& pack, const uint8_t* __restrict__ dft_val,
					size_t batch, const uint8_t* __restrict__ keys, uint8_t* __restrict__ data) {
	if (pack.type != Type::KV_INLINE) {
		return 0;
	}
	size_t hit = 0;
	Pipeline(batch,
			 [&pack, keys](size_t i) -> Step1 {
				 auto key = keys+i*pack.key_len;
				 return Process1(pack, key);
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 2
			 [](const Step1& in, size_t) -> Step1 { return in; },
#if PIPELINE_LEVEL >= 3
			 [](const Step1& in, size_t) -> Step1 { return in; },
#endif
#endif
			 [](const Step1& in, size_t) -> Step2 {
				 return Process2(in);
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 2
			 [](const Step2& in, size_t) -> Step2 { return in; },
#if PIPELINE_LEVEL >= 3
			 [](const Step2& in, size_t) -> Step2 { return in; },
#endif
#endif
			 [&pack](const Step2& in, size_t) -> Step3 {
				 return Process3(pack, in, true);
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 2
			 [](const Step3& in, size_t) -> Step3 { return in; },
#if PIPELINE_LEVEL >= 3
			 [](const Step3& in, size_t) -> Step3 { return in; },
#endif
#endif
			 [&pack, &hit, keys, data, dft_val](const Step3& in, size_t i) {
				 auto key = keys + i*pack.key_len;
				 auto out = data + i*pack.val_len;
				 if (LIKELY(in.line != nullptr) && Equal(key, in.line, pack.key_len)) {
					 hit++;
					 memcpy(out, in.line+pack.key_len, pack.val_len);
				 } else if (dft_val != nullptr) {
					 memcpy(out, dft_val, pack.val_len);
				 }
			 }
	);
	return hit;
}

using Step4 = std::tuple<const uint8_t*,Step1>;
using Step5 = std::tuple<const uint8_t*,Step2>;
using Step6 = std::tuple<const uint8_t*,Step3>;

size_t BatchSearch(const PackView& base, const PackView& patch,
					 size_t batch, const uint8_t* const keys[], const uint8_t* out[]) {
	if ((base.type != Type::KV_INLINE && base.type != Type::KEY_SET)
		|| base.type != patch.type || base.key_len != patch.key_len) {
		return 0;
	}
	size_t hit = 0;
	Pipeline(batch,
			 [&patch, keys](size_t i) -> Step1 {
				 return Process1(patch, keys[i]);
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 2
			 [](const Step1& in, size_t) -> Step1 { return in; },
#endif
			 [](const Step1& in, size_t) -> Step2 {
				 return Process2(in);
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 2
			 [](const Step2& in, size_t) -> Step2 { return in; },
#endif
			 [&patch](const Step2& in, size_t) -> Step3 {
				 return Process3(patch, in);
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 2
			 [](const Step3& in, size_t) -> Step3 { return in; },
#endif
			 [&base, &patch, keys](const Step3& in, size_t i) -> Step4 {
				 if (LIKELY(in.line != nullptr) && Equal(keys[i], in.line, patch.key_len)) {
					 return {in.line + patch.key_len, Step1{}};
				 } else {
					 return {nullptr, Process1(base, keys[i])};
				 }
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 2
			 [](const Step4& in, size_t) -> Step4 { return in; },
#endif
			 [](const Step4& in, size_t) -> Step5 {
				 auto& [val, s1] = in;
				 if (val != nullptr) {
					 return {val, Step2{}};
				 } else {
					 return {nullptr, Process2(s1)};
				 }
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 2
			 [](const Step5& in, size_t) -> Step5 { return in; },
#endif
			 [&base](const Step5& in, size_t) -> Step6 {
				 auto& [val, s2] = in;
				 if (val != nullptr) {
					 return {val, Step3{}};
				 } else {
					 return {nullptr, Process3(base, s2)};
				 }
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 2
			 [](const Step6& in, size_t) -> Step6 { return in; },
#endif
			 [&base, &hit, keys, out](const Step6& in, size_t i) {
				 auto& [val, s3] = in;
				 if (val != nullptr) {
					 hit++;
					 out[i] = val;
				 } else if (LIKELY(s3.line != nullptr) && Equal(keys[i], s3.line, base.key_len)) {
					 hit++;
					 out[i] = s3.line + base.key_len;
				 } else {
					 out[i] = nullptr;
				 }
			 }
	);
	return hit;
}

size_t BatchFetch(const PackView& base, const PackView& patch, const uint8_t* __restrict__ dft_val,
					size_t batch, const uint8_t* __restrict__ keys, uint8_t* __restrict__ data) {
	if (base.type != Type::KV_INLINE || base.type != patch.type
		|| base.key_len != patch.key_len || base.val_len != patch.val_len) {
		return 0;
	}
	size_t hit = 0;
	Pipeline(batch,
			 [&patch, keys](size_t i) -> Step1 {
				 auto key = keys+i*patch.key_len;
				 return Process1(patch, key);
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 3
			 [](const Step1& in, size_t) -> Step1 { return in; },
#endif
			 [](const Step1& in, size_t) -> Step2 {
				 return Process2(in);
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 3
			 [](const Step2& in, size_t) -> Step2 { return in; },
#endif
			 [&patch](const Step2& in, size_t) -> Step3 {
				 return Process3(patch, in, true);
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 3
			 [](const Step3& in, size_t) -> Step3 { return in; },
#endif
			 [&base, &patch, keys](const Step3& in, size_t i) -> Step4 {
				 auto key = keys + i*base.key_len;
				 if (LIKELY(in.line != nullptr) && Equal(key, in.line, patch.key_len)) {
					 return {in.line + patch.key_len, Step1{}};
				 } else {
					 return {nullptr, Process1(base, key)};
				 }
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 3
			 [](const Step4& in, size_t) -> Step4 { return in; },
#endif
			 [](const Step4& in, size_t) -> Step5 {
				 auto& [val, s1] = in;
				 if (val != nullptr) {
					 return {val, Step2{}};
				 } else {
					 return {nullptr, Process2(s1)};
				 }
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 3
			 [](const Step5& in, size_t) -> Step5 { return in; },
#endif
			 [&base](const Step5& in, size_t) -> Step6 {
				 auto& [val, s2] = in;
				 if (val != nullptr) {
					 return {val, Step3{}};
				 } else {
					 return {nullptr, Process3(base, s2, true)};
				 }
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 3
			 [](const Step6& in, size_t) -> Step6 { return in; },
#endif
			 [&base, &hit, keys, data, dft_val](const Step6& in, size_t i) {
				 auto key = keys + i*base.key_len;
				 auto out = data + i*base.val_len;
				 auto& [val, s3] = in;
				 if (val != nullptr) {
					 hit++;
					 memcpy(out, val, base.val_len);
				 } else if (LIKELY(s3.line != nullptr) && Equal(key, s3.line, base.key_len)) {
					 hit++;
					 memcpy(out, s3.line+base.key_len, base.val_len);
				 } else if (dft_val != nullptr) {
					 memcpy(out, dft_val, base.val_len);
				 }
			 }
	);
	return hit;
}

static constexpr unsigned PIPELINE_BUFFER_SIZE = 16;	//bigger than depth of pipeline
static_assert((PIPELINE_BUFFER_SIZE & (PIPELINE_BUFFER_SIZE-1)) == 0, "");

void BatchFindPos(const PackView& pack, size_t batch, const std::function<const uint8_t*(uint8_t*)>& reader,
				  const std::function<void(uint64_t)>& output, const uint8_t* bitmap) {
	if (pack.type == Type::INDEX_ONLY) return;
	auto buf = std::make_unique<uint8_t[]>(PIPELINE_BUFFER_SIZE*pack.key_len);
	typedef const uint8_t* Pointer;
	auto cache = std::make_unique<Pointer[]>(PIPELINE_BUFFER_SIZE);
	using Step3X = std::tuple<uint64_t,const uint8_t*>;
	Pipeline(batch,
			 [&pack, &cache, &buf, &reader](size_t i) -> Step1 {
				 auto j = i & (PIPELINE_BUFFER_SIZE-1);
				 cache[j] = reader(buf.get() + j*pack.key_len);
				 return Process1(pack, cache[j], pack.key_len);
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 2
			 [](const Step1& in, size_t) -> Step1 { return in; },
#endif
			 [](const Step1& in, size_t) -> Step2 {
				 return Process2(in);
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 2
			 [](const Step2& in, size_t) -> Step2 { return in; },
#endif
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
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 2
			 [](const Step3X& in, size_t) -> Step3X{ return in; },
#endif
			 [&pack, &cache, &output](const Step3X& in, size_t i) {
				 auto [pos, line] = in;
				 if (Equal(cache[i&(PIPELINE_BUFFER_SIZE-1)], line, pack.key_len)) {
					 output(pos);
				 } else {
					 output(UINT64_MAX);
				 }
			 }
	);
}

void BatchDataMapping(const PackView& index, uint8_t* space, size_t batch,
				 const std::function<const uint8_t*(uint8_t*)>& reader) {
	auto buf = std::make_unique<uint8_t[]>(PIPELINE_BUFFER_SIZE*index.line_size);
	typedef const uint8_t* Pointer;
	auto cache = std::make_unique<Pointer[]>(PIPELINE_BUFFER_SIZE);
	struct Step3X {
		uint8_t* line;
	};
	Pipeline(batch,
			 [&index, &cache, &buf, &reader](size_t i) -> Step1 {
				 auto j = i & (PIPELINE_BUFFER_SIZE-1);
				 cache[j] = reader(buf.get() + j*index.line_size);
				 return Process1(index, cache[j], index.key_len);
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 2
			 [](const Step1& in, size_t) -> Step1 { return in; },
#endif
			 [](const Step1& in, size_t) -> Step2 {
				 return Process2(in);
			 },
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 2
			 [](const Step2& in, size_t) -> Step2 { return in; },
#endif
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
#if defined(PIPELINE_LEVEL) && PIPELINE_LEVEL >= 2
			 [](const Step3X& in, size_t) -> Step3X{ return in; },
#endif
			 [&cache, &index](const Step3X& in, size_t i) {
				 memcpy(in.line, cache[i&(PIPELINE_BUFFER_SIZE-1)], index.line_size);
			 }
	);
}

} //chd
