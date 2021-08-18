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

#pragma once
#ifndef CHD_PIPELINE_H_
#define CHD_PIPELINE_H_

/* ========================================================================
#include <iostream>

void GenCode(unsigned depth) {
    if (depth < 2) {
        return;
    }
    std::cout << "template <typename P1";
    for (unsigned i = 2; i <= depth; i++) {
        std::cout << ", typename P" << i;
    }
    std::cout << ">\nstatic inline __attribute__((always_inline)) void\nPipeline(size_t n";
    for (unsigned i = 1; i <= depth; i++) {
        std::cout << ", const P" << i << "& p" << i;
    }
    std::cout << ") {\n\tusing S1 = std::result_of_t<P1(size_t)>;\n";
    for (unsigned i = 2; i < depth; i++) {
        std::cout << "\tusing S" << i << " = std::result_of_t<P" << i << "(S" << (i-1) << ",size_t)>;\n";
    }
    std::cout << "\tunion {\n\t\t";
    for (unsigned i = 1; i < depth; i++) {
        std::cout << "S" << i << " s" << i << "; ";
    }
    std::cout << "\n\t} ctx[" << depth << "];\n\n"
              << "\tif (n < " << (depth-1) << ") {\n"
              << "\t\tfor (size_t i = 0; i < n; i++) ctx[i].s1 = p1(i);\n";
    for (unsigned i = 2; i < depth; i++) {
        std::cout << "\t\tfor (size_t i = 0; i < n; i++) ctx[i].s" << i
                  << " = p" << i << "(ctx[i].s" << (i-1) << ", i);\n";
    }
    std::cout << "\t\tfor (size_t i = 0; i < n; i++) p" << depth << "(ctx[i].s" << (depth-1) << ", i);\n"
              << "\t\treturn;\n"
              << "\t}\n";
    for (unsigned i = 1; i < depth; i++) {
        std::cout << "\n\tctx[" << (i-1) << "].s1 = p1(" << (i-1) << ");\n";
        for (unsigned j = 2; j <= i; j++) {
            std::cout << "\tctx[" << (i-j) << "].s" << j << " = p" << j
                      << "(ctx[" << (i-j) << "].s" << (j-1) << ", " << (i-j) << ");\n";
        }
    }
    std::cout << "\n\tint cur = " << (depth-1) << ";"
              << "\n\tauto forward = [&cur]() { if (++cur == " << depth << ") cur = 0; };"
              << "\n\tauto sft = [&cur](int step)->int { return Shift(cur,step," << depth << "-step); };";
    std::cout << "\n\n\tfor (size_t i = " << (depth-1) << "; i < n; i++) {\n"
              << "\t\tctx[cur].s1 = p1(i);\n";
    for (unsigned i = 2; i < depth; i++) {
        std::cout << "\t\tctx[sft(" << (i-1) << ")].s" << i << " = p" << i
                  << "(ctx[sft(" << (i-1) << ")].s" << (i-1) << ", i-" << (i-1) << ");\n";
    }
    std::cout << "\t\tp" << depth << "(ctx[sft(" << (depth-1) << ")].s" << (depth-1) << ", i-" << (depth-1) << ");\n"
              << "\t\tforward();\n"
              << "\t}\n";
    for (unsigned i = 1; i < depth; i++) {
        std::cout << '\n';
        for (unsigned j = i+1; j < depth; j++) {
            std::cout << "\tctx[sft(" << (j-1) << ")].s" << j << " = p" << j
                      << "(ctx[sft(" << (j-1) << ")].s" << (j-1) << ", n-" << (j-i) << ");\n";
        }
        std::cout << "\tp" << depth << "(ctx[sft(" << (depth-1) << ")].s" << (depth-1) << ", n-" << (depth-i) << ");\n"
                  << "\tforward();\n";
    }
    std::cout << "}\n" << std::endl;
}
======================================================================== */

#include <type_traits>

static inline int Shift(int v, int l, int r) {
	int x = v + r;
	v -= l;
	if (v < 0) {
		v = x;
	}
	return v;
}

template <typename P1, typename P2, typename P3>
static inline __attribute__((always_inline)) void
Pipeline(size_t n, const P1& p1, const P2& p2, const P3& p3) {
	using S1 = std::result_of_t<P1(size_t)>;
	using S2 = std::result_of_t<P2(S1,size_t)>;
	union {
		S1 s1; S2 s2;
	} ctx[3];

	if (n < 2) {
		for (size_t i = 0; i < n; i++) ctx[i].s1 = p1(i);
		for (size_t i = 0; i < n; i++) ctx[i].s2 = p2(ctx[i].s1, i);
		for (size_t i = 0; i < n; i++) p3(ctx[i].s2, i);
		return;
	}

	ctx[0].s1 = p1(0);

	ctx[1].s1 = p1(1);
	ctx[0].s2 = p2(ctx[0].s1, 0);

	int cur = 2;
	auto forward = [&cur]() { if (++cur == 3) cur = 0; };
	auto sft = [&cur](int step)->int { return Shift(cur,step,3-step); };

	for (size_t i = 2; i < n; i++) {
		ctx[cur].s1 = p1(i);
		ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, i-1);
		p3(ctx[sft(2)].s2, i-2);
		forward();
	}

	ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, n-1);
	p3(ctx[sft(2)].s2, n-2);
	forward();

	p3(ctx[sft(2)].s2, n-1);
	forward();
}

template <typename P1, typename P2, typename P3, typename P4>
static inline __attribute__((always_inline)) void
Pipeline(size_t n, const P1& p1, const P2& p2, const P3& p3, const P4& p4) {
	using S1 = std::result_of_t<P1(size_t)>;
	using S2 = std::result_of_t<P2(S1,size_t)>;
	using S3 = std::result_of_t<P3(S2,size_t)>;
	union {
		S1 s1; S2 s2; S3 s3;
	} ctx[4];

	if (n < 3) {
		for (size_t i = 0; i < n; i++) ctx[i].s1 = p1(i);
		for (size_t i = 0; i < n; i++) ctx[i].s2 = p2(ctx[i].s1, i);
		for (size_t i = 0; i < n; i++) ctx[i].s3 = p3(ctx[i].s2, i);
		for (size_t i = 0; i < n; i++) p4(ctx[i].s3, i);
		return;
	}

	ctx[0].s1 = p1(0);

	ctx[1].s1 = p1(1);
	ctx[0].s2 = p2(ctx[0].s1, 0);

	ctx[2].s1 = p1(2);
	ctx[1].s2 = p2(ctx[1].s1, 1);
	ctx[0].s3 = p3(ctx[0].s2, 0);

	int cur = 3;
	auto forward = [&cur]() { if (++cur == 4) cur = 0; };
	auto sft = [&cur](int step)->int { return Shift(cur,step,4-step); };

	for (size_t i = 3; i < n; i++) {
		ctx[cur].s1 = p1(i);
		ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, i-1);
		ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, i-2);
		p4(ctx[sft(3)].s3, i-3);
		forward();
	}

	ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, n-1);
	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-2);
	p4(ctx[sft(3)].s3, n-3);
	forward();

	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-1);
	p4(ctx[sft(3)].s3, n-2);
	forward();

	p4(ctx[sft(3)].s3, n-1);
	forward();
}

template <typename P1, typename P2, typename P3, typename P4, typename P5>
static inline __attribute__((always_inline)) void
Pipeline(size_t n, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5) {
	using S1 = std::result_of_t<P1(size_t)>;
	using S2 = std::result_of_t<P2(S1,size_t)>;
	using S3 = std::result_of_t<P3(S2,size_t)>;
	using S4 = std::result_of_t<P4(S3,size_t)>;
	union {
		S1 s1; S2 s2; S3 s3; S4 s4;
	} ctx[5];

	if (n < 4) {
		for (size_t i = 0; i < n; i++) ctx[i].s1 = p1(i);
		for (size_t i = 0; i < n; i++) ctx[i].s2 = p2(ctx[i].s1, i);
		for (size_t i = 0; i < n; i++) ctx[i].s3 = p3(ctx[i].s2, i);
		for (size_t i = 0; i < n; i++) ctx[i].s4 = p4(ctx[i].s3, i);
		for (size_t i = 0; i < n; i++) p5(ctx[i].s4, i);
		return;
	}

	ctx[0].s1 = p1(0);

	ctx[1].s1 = p1(1);
	ctx[0].s2 = p2(ctx[0].s1, 0);

	ctx[2].s1 = p1(2);
	ctx[1].s2 = p2(ctx[1].s1, 1);
	ctx[0].s3 = p3(ctx[0].s2, 0);

	ctx[3].s1 = p1(3);
	ctx[2].s2 = p2(ctx[2].s1, 2);
	ctx[1].s3 = p3(ctx[1].s2, 1);
	ctx[0].s4 = p4(ctx[0].s3, 0);

	int cur = 4;
	auto forward = [&cur]() { if (++cur == 5) cur = 0; };
	auto sft = [&cur](int step)->int { return Shift(cur,step,5-step); };

	for (size_t i = 4; i < n; i++) {
		ctx[cur].s1 = p1(i);
		ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, i-1);
		ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, i-2);
		ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, i-3);
		p5(ctx[sft(4)].s4, i-4);
		forward();
	}

	ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, n-1);
	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-2);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-3);
	p5(ctx[sft(4)].s4, n-4);
	forward();

	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-1);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-2);
	p5(ctx[sft(4)].s4, n-3);
	forward();

	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-1);
	p5(ctx[sft(4)].s4, n-2);
	forward();

	p5(ctx[sft(4)].s4, n-1);
	forward();
}

template <typename P1, typename P2, typename P3, typename P4, typename P5, typename P6>
static inline __attribute__((always_inline)) void
Pipeline(size_t n, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6) {
	using S1 = std::result_of_t<P1(size_t)>;
	using S2 = std::result_of_t<P2(S1,size_t)>;
	using S3 = std::result_of_t<P3(S2,size_t)>;
	using S4 = std::result_of_t<P4(S3,size_t)>;
	using S5 = std::result_of_t<P5(S4,size_t)>;
	union {
		S1 s1; S2 s2; S3 s3; S4 s4; S5 s5;
	} ctx[6];

	if (n < 5) {
		for (size_t i = 0; i < n; i++) ctx[i].s1 = p1(i);
		for (size_t i = 0; i < n; i++) ctx[i].s2 = p2(ctx[i].s1, i);
		for (size_t i = 0; i < n; i++) ctx[i].s3 = p3(ctx[i].s2, i);
		for (size_t i = 0; i < n; i++) ctx[i].s4 = p4(ctx[i].s3, i);
		for (size_t i = 0; i < n; i++) ctx[i].s5 = p5(ctx[i].s4, i);
		for (size_t i = 0; i < n; i++) p6(ctx[i].s5, i);
		return;
	}

	ctx[0].s1 = p1(0);

	ctx[1].s1 = p1(1);
	ctx[0].s2 = p2(ctx[0].s1, 0);

	ctx[2].s1 = p1(2);
	ctx[1].s2 = p2(ctx[1].s1, 1);
	ctx[0].s3 = p3(ctx[0].s2, 0);

	ctx[3].s1 = p1(3);
	ctx[2].s2 = p2(ctx[2].s1, 2);
	ctx[1].s3 = p3(ctx[1].s2, 1);
	ctx[0].s4 = p4(ctx[0].s3, 0);

	ctx[4].s1 = p1(4);
	ctx[3].s2 = p2(ctx[3].s1, 3);
	ctx[2].s3 = p3(ctx[2].s2, 2);
	ctx[1].s4 = p4(ctx[1].s3, 1);
	ctx[0].s5 = p5(ctx[0].s4, 0);

	int cur = 5;
	auto forward = [&cur]() { if (++cur == 6) cur = 0; };
	auto sft = [&cur](int step)->int { return Shift(cur,step,6-step); };

	for (size_t i = 5; i < n; i++) {
		ctx[cur].s1 = p1(i);
		ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, i-1);
		ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, i-2);
		ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, i-3);
		ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, i-4);
		p6(ctx[sft(5)].s5, i-5);
		forward();
	}

	ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, n-1);
	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-2);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-3);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-4);
	p6(ctx[sft(5)].s5, n-5);
	forward();

	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-1);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-2);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-3);
	p6(ctx[sft(5)].s5, n-4);
	forward();

	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-1);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-2);
	p6(ctx[sft(5)].s5, n-3);
	forward();

	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-1);
	p6(ctx[sft(5)].s5, n-2);
	forward();

	p6(ctx[sft(5)].s5, n-1);
	forward();
}

template <typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7>
static inline __attribute__((always_inline)) void
Pipeline(size_t n, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7) {
	using S1 = std::result_of_t<P1(size_t)>;
	using S2 = std::result_of_t<P2(S1,size_t)>;
	using S3 = std::result_of_t<P3(S2,size_t)>;
	using S4 = std::result_of_t<P4(S3,size_t)>;
	using S5 = std::result_of_t<P5(S4,size_t)>;
	using S6 = std::result_of_t<P6(S5,size_t)>;
	union {
		S1 s1; S2 s2; S3 s3; S4 s4; S5 s5; S6 s6;
	} ctx[7];

	if (n < 6) {
		for (size_t i = 0; i < n; i++) ctx[i].s1 = p1(i);
		for (size_t i = 0; i < n; i++) ctx[i].s2 = p2(ctx[i].s1, i);
		for (size_t i = 0; i < n; i++) ctx[i].s3 = p3(ctx[i].s2, i);
		for (size_t i = 0; i < n; i++) ctx[i].s4 = p4(ctx[i].s3, i);
		for (size_t i = 0; i < n; i++) ctx[i].s5 = p5(ctx[i].s4, i);
		for (size_t i = 0; i < n; i++) ctx[i].s6 = p6(ctx[i].s5, i);
		for (size_t i = 0; i < n; i++) p7(ctx[i].s6, i);
		return;
	}

	ctx[0].s1 = p1(0);

	ctx[1].s1 = p1(1);
	ctx[0].s2 = p2(ctx[0].s1, 0);

	ctx[2].s1 = p1(2);
	ctx[1].s2 = p2(ctx[1].s1, 1);
	ctx[0].s3 = p3(ctx[0].s2, 0);

	ctx[3].s1 = p1(3);
	ctx[2].s2 = p2(ctx[2].s1, 2);
	ctx[1].s3 = p3(ctx[1].s2, 1);
	ctx[0].s4 = p4(ctx[0].s3, 0);

	ctx[4].s1 = p1(4);
	ctx[3].s2 = p2(ctx[3].s1, 3);
	ctx[2].s3 = p3(ctx[2].s2, 2);
	ctx[1].s4 = p4(ctx[1].s3, 1);
	ctx[0].s5 = p5(ctx[0].s4, 0);

	ctx[5].s1 = p1(5);
	ctx[4].s2 = p2(ctx[4].s1, 4);
	ctx[3].s3 = p3(ctx[3].s2, 3);
	ctx[2].s4 = p4(ctx[2].s3, 2);
	ctx[1].s5 = p5(ctx[1].s4, 1);
	ctx[0].s6 = p6(ctx[0].s5, 0);

	int cur = 6;
	auto forward = [&cur]() { if (++cur == 7) cur = 0; };
	auto sft = [&cur](int step)->int { return Shift(cur,step,7-step); };

	for (size_t i = 6; i < n; i++) {
		ctx[cur].s1 = p1(i);
		ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, i-1);
		ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, i-2);
		ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, i-3);
		ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, i-4);
		ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, i-5);
		p7(ctx[sft(6)].s6, i-6);
		forward();
	}

	ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, n-1);
	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-2);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-3);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-4);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-5);
	p7(ctx[sft(6)].s6, n-6);
	forward();

	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-1);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-2);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-3);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-4);
	p7(ctx[sft(6)].s6, n-5);
	forward();

	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-1);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-2);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-3);
	p7(ctx[sft(6)].s6, n-4);
	forward();

	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-1);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-2);
	p7(ctx[sft(6)].s6, n-3);
	forward();

	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-1);
	p7(ctx[sft(6)].s6, n-2);
	forward();

	p7(ctx[sft(6)].s6, n-1);
	forward();
}

template <typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8>
static inline __attribute__((always_inline)) void
Pipeline(size_t n, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8) {
	using S1 = std::result_of_t<P1(size_t)>;
	using S2 = std::result_of_t<P2(S1,size_t)>;
	using S3 = std::result_of_t<P3(S2,size_t)>;
	using S4 = std::result_of_t<P4(S3,size_t)>;
	using S5 = std::result_of_t<P5(S4,size_t)>;
	using S6 = std::result_of_t<P6(S5,size_t)>;
	using S7 = std::result_of_t<P7(S6,size_t)>;
	union {
		S1 s1; S2 s2; S3 s3; S4 s4; S5 s5; S6 s6; S7 s7;
	} ctx[8];

	if (n < 7) {
		for (size_t i = 0; i < n; i++) ctx[i].s1 = p1(i);
		for (size_t i = 0; i < n; i++) ctx[i].s2 = p2(ctx[i].s1, i);
		for (size_t i = 0; i < n; i++) ctx[i].s3 = p3(ctx[i].s2, i);
		for (size_t i = 0; i < n; i++) ctx[i].s4 = p4(ctx[i].s3, i);
		for (size_t i = 0; i < n; i++) ctx[i].s5 = p5(ctx[i].s4, i);
		for (size_t i = 0; i < n; i++) ctx[i].s6 = p6(ctx[i].s5, i);
		for (size_t i = 0; i < n; i++) ctx[i].s7 = p7(ctx[i].s6, i);
		for (size_t i = 0; i < n; i++) p8(ctx[i].s7, i);
		return;
	}

	ctx[0].s1 = p1(0);

	ctx[1].s1 = p1(1);
	ctx[0].s2 = p2(ctx[0].s1, 0);

	ctx[2].s1 = p1(2);
	ctx[1].s2 = p2(ctx[1].s1, 1);
	ctx[0].s3 = p3(ctx[0].s2, 0);

	ctx[3].s1 = p1(3);
	ctx[2].s2 = p2(ctx[2].s1, 2);
	ctx[1].s3 = p3(ctx[1].s2, 1);
	ctx[0].s4 = p4(ctx[0].s3, 0);

	ctx[4].s1 = p1(4);
	ctx[3].s2 = p2(ctx[3].s1, 3);
	ctx[2].s3 = p3(ctx[2].s2, 2);
	ctx[1].s4 = p4(ctx[1].s3, 1);
	ctx[0].s5 = p5(ctx[0].s4, 0);

	ctx[5].s1 = p1(5);
	ctx[4].s2 = p2(ctx[4].s1, 4);
	ctx[3].s3 = p3(ctx[3].s2, 3);
	ctx[2].s4 = p4(ctx[2].s3, 2);
	ctx[1].s5 = p5(ctx[1].s4, 1);
	ctx[0].s6 = p6(ctx[0].s5, 0);

	ctx[6].s1 = p1(6);
	ctx[5].s2 = p2(ctx[5].s1, 5);
	ctx[4].s3 = p3(ctx[4].s2, 4);
	ctx[3].s4 = p4(ctx[3].s3, 3);
	ctx[2].s5 = p5(ctx[2].s4, 2);
	ctx[1].s6 = p6(ctx[1].s5, 1);
	ctx[0].s7 = p7(ctx[0].s6, 0);

	int cur = 7;
	auto forward = [&cur]() { if (++cur == 8) cur = 0; };
	auto sft = [&cur](int step)->int { return Shift(cur,step,8-step); };

	for (size_t i = 7; i < n; i++) {
		ctx[cur].s1 = p1(i);
		ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, i-1);
		ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, i-2);
		ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, i-3);
		ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, i-4);
		ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, i-5);
		ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, i-6);
		p8(ctx[sft(7)].s7, i-7);
		forward();
	}

	ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, n-1);
	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-2);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-3);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-4);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-5);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-6);
	p8(ctx[sft(7)].s7, n-7);
	forward();

	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-1);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-2);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-3);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-4);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-5);
	p8(ctx[sft(7)].s7, n-6);
	forward();

	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-1);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-2);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-3);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-4);
	p8(ctx[sft(7)].s7, n-5);
	forward();

	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-1);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-2);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-3);
	p8(ctx[sft(7)].s7, n-4);
	forward();

	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-1);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-2);
	p8(ctx[sft(7)].s7, n-3);
	forward();

	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-1);
	p8(ctx[sft(7)].s7, n-2);
	forward();

	p8(ctx[sft(7)].s7, n-1);
	forward();
}

template <typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9>
static inline __attribute__((always_inline)) void
Pipeline(size_t n, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9) {
	using S1 = std::result_of_t<P1(size_t)>;
	using S2 = std::result_of_t<P2(S1,size_t)>;
	using S3 = std::result_of_t<P3(S2,size_t)>;
	using S4 = std::result_of_t<P4(S3,size_t)>;
	using S5 = std::result_of_t<P5(S4,size_t)>;
	using S6 = std::result_of_t<P6(S5,size_t)>;
	using S7 = std::result_of_t<P7(S6,size_t)>;
	using S8 = std::result_of_t<P8(S7,size_t)>;
	union {
		S1 s1; S2 s2; S3 s3; S4 s4; S5 s5; S6 s6; S7 s7; S8 s8;
	} ctx[9];

	if (n < 8) {
		for (size_t i = 0; i < n; i++) ctx[i].s1 = p1(i);
		for (size_t i = 0; i < n; i++) ctx[i].s2 = p2(ctx[i].s1, i);
		for (size_t i = 0; i < n; i++) ctx[i].s3 = p3(ctx[i].s2, i);
		for (size_t i = 0; i < n; i++) ctx[i].s4 = p4(ctx[i].s3, i);
		for (size_t i = 0; i < n; i++) ctx[i].s5 = p5(ctx[i].s4, i);
		for (size_t i = 0; i < n; i++) ctx[i].s6 = p6(ctx[i].s5, i);
		for (size_t i = 0; i < n; i++) ctx[i].s7 = p7(ctx[i].s6, i);
		for (size_t i = 0; i < n; i++) ctx[i].s8 = p8(ctx[i].s7, i);
		for (size_t i = 0; i < n; i++) p9(ctx[i].s8, i);
		return;
	}

	ctx[0].s1 = p1(0);

	ctx[1].s1 = p1(1);
	ctx[0].s2 = p2(ctx[0].s1, 0);

	ctx[2].s1 = p1(2);
	ctx[1].s2 = p2(ctx[1].s1, 1);
	ctx[0].s3 = p3(ctx[0].s2, 0);

	ctx[3].s1 = p1(3);
	ctx[2].s2 = p2(ctx[2].s1, 2);
	ctx[1].s3 = p3(ctx[1].s2, 1);
	ctx[0].s4 = p4(ctx[0].s3, 0);

	ctx[4].s1 = p1(4);
	ctx[3].s2 = p2(ctx[3].s1, 3);
	ctx[2].s3 = p3(ctx[2].s2, 2);
	ctx[1].s4 = p4(ctx[1].s3, 1);
	ctx[0].s5 = p5(ctx[0].s4, 0);

	ctx[5].s1 = p1(5);
	ctx[4].s2 = p2(ctx[4].s1, 4);
	ctx[3].s3 = p3(ctx[3].s2, 3);
	ctx[2].s4 = p4(ctx[2].s3, 2);
	ctx[1].s5 = p5(ctx[1].s4, 1);
	ctx[0].s6 = p6(ctx[0].s5, 0);

	ctx[6].s1 = p1(6);
	ctx[5].s2 = p2(ctx[5].s1, 5);
	ctx[4].s3 = p3(ctx[4].s2, 4);
	ctx[3].s4 = p4(ctx[3].s3, 3);
	ctx[2].s5 = p5(ctx[2].s4, 2);
	ctx[1].s6 = p6(ctx[1].s5, 1);
	ctx[0].s7 = p7(ctx[0].s6, 0);

	ctx[7].s1 = p1(7);
	ctx[6].s2 = p2(ctx[6].s1, 6);
	ctx[5].s3 = p3(ctx[5].s2, 5);
	ctx[4].s4 = p4(ctx[4].s3, 4);
	ctx[3].s5 = p5(ctx[3].s4, 3);
	ctx[2].s6 = p6(ctx[2].s5, 2);
	ctx[1].s7 = p7(ctx[1].s6, 1);
	ctx[0].s8 = p8(ctx[0].s7, 0);

	int cur = 8;
	auto forward = [&cur]() { if (++cur == 9) cur = 0; };
	auto sft = [&cur](int step)->int { return Shift(cur,step,9-step); };

	for (size_t i = 8; i < n; i++) {
		ctx[cur].s1 = p1(i);
		ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, i-1);
		ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, i-2);
		ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, i-3);
		ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, i-4);
		ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, i-5);
		ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, i-6);
		ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, i-7);
		p9(ctx[sft(8)].s8, i-8);
		forward();
	}

	ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, n-1);
	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-2);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-3);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-4);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-5);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-6);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-7);
	p9(ctx[sft(8)].s8, n-8);
	forward();

	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-1);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-2);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-3);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-4);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-5);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-6);
	p9(ctx[sft(8)].s8, n-7);
	forward();

	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-1);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-2);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-3);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-4);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-5);
	p9(ctx[sft(8)].s8, n-6);
	forward();

	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-1);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-2);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-3);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-4);
	p9(ctx[sft(8)].s8, n-5);
	forward();

	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-1);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-2);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-3);
	p9(ctx[sft(8)].s8, n-4);
	forward();

	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-1);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-2);
	p9(ctx[sft(8)].s8, n-3);
	forward();

	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-1);
	p9(ctx[sft(8)].s8, n-2);
	forward();

	p9(ctx[sft(8)].s8, n-1);
	forward();
}

template <typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9, typename P10>
static inline __attribute__((always_inline)) void
Pipeline(size_t n, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9, const P10& p10) {
	using S1 = std::result_of_t<P1(size_t)>;
	using S2 = std::result_of_t<P2(S1,size_t)>;
	using S3 = std::result_of_t<P3(S2,size_t)>;
	using S4 = std::result_of_t<P4(S3,size_t)>;
	using S5 = std::result_of_t<P5(S4,size_t)>;
	using S6 = std::result_of_t<P6(S5,size_t)>;
	using S7 = std::result_of_t<P7(S6,size_t)>;
	using S8 = std::result_of_t<P8(S7,size_t)>;
	using S9 = std::result_of_t<P9(S8,size_t)>;
	union {
		S1 s1; S2 s2; S3 s3; S4 s4; S5 s5; S6 s6; S7 s7; S8 s8; S9 s9;
	} ctx[10];

	if (n < 9) {
		for (size_t i = 0; i < n; i++) ctx[i].s1 = p1(i);
		for (size_t i = 0; i < n; i++) ctx[i].s2 = p2(ctx[i].s1, i);
		for (size_t i = 0; i < n; i++) ctx[i].s3 = p3(ctx[i].s2, i);
		for (size_t i = 0; i < n; i++) ctx[i].s4 = p4(ctx[i].s3, i);
		for (size_t i = 0; i < n; i++) ctx[i].s5 = p5(ctx[i].s4, i);
		for (size_t i = 0; i < n; i++) ctx[i].s6 = p6(ctx[i].s5, i);
		for (size_t i = 0; i < n; i++) ctx[i].s7 = p7(ctx[i].s6, i);
		for (size_t i = 0; i < n; i++) ctx[i].s8 = p8(ctx[i].s7, i);
		for (size_t i = 0; i < n; i++) ctx[i].s9 = p9(ctx[i].s8, i);
		for (size_t i = 0; i < n; i++) p10(ctx[i].s9, i);
		return;
	}

	ctx[0].s1 = p1(0);

	ctx[1].s1 = p1(1);
	ctx[0].s2 = p2(ctx[0].s1, 0);

	ctx[2].s1 = p1(2);
	ctx[1].s2 = p2(ctx[1].s1, 1);
	ctx[0].s3 = p3(ctx[0].s2, 0);

	ctx[3].s1 = p1(3);
	ctx[2].s2 = p2(ctx[2].s1, 2);
	ctx[1].s3 = p3(ctx[1].s2, 1);
	ctx[0].s4 = p4(ctx[0].s3, 0);

	ctx[4].s1 = p1(4);
	ctx[3].s2 = p2(ctx[3].s1, 3);
	ctx[2].s3 = p3(ctx[2].s2, 2);
	ctx[1].s4 = p4(ctx[1].s3, 1);
	ctx[0].s5 = p5(ctx[0].s4, 0);

	ctx[5].s1 = p1(5);
	ctx[4].s2 = p2(ctx[4].s1, 4);
	ctx[3].s3 = p3(ctx[3].s2, 3);
	ctx[2].s4 = p4(ctx[2].s3, 2);
	ctx[1].s5 = p5(ctx[1].s4, 1);
	ctx[0].s6 = p6(ctx[0].s5, 0);

	ctx[6].s1 = p1(6);
	ctx[5].s2 = p2(ctx[5].s1, 5);
	ctx[4].s3 = p3(ctx[4].s2, 4);
	ctx[3].s4 = p4(ctx[3].s3, 3);
	ctx[2].s5 = p5(ctx[2].s4, 2);
	ctx[1].s6 = p6(ctx[1].s5, 1);
	ctx[0].s7 = p7(ctx[0].s6, 0);

	ctx[7].s1 = p1(7);
	ctx[6].s2 = p2(ctx[6].s1, 6);
	ctx[5].s3 = p3(ctx[5].s2, 5);
	ctx[4].s4 = p4(ctx[4].s3, 4);
	ctx[3].s5 = p5(ctx[3].s4, 3);
	ctx[2].s6 = p6(ctx[2].s5, 2);
	ctx[1].s7 = p7(ctx[1].s6, 1);
	ctx[0].s8 = p8(ctx[0].s7, 0);

	ctx[8].s1 = p1(8);
	ctx[7].s2 = p2(ctx[7].s1, 7);
	ctx[6].s3 = p3(ctx[6].s2, 6);
	ctx[5].s4 = p4(ctx[5].s3, 5);
	ctx[4].s5 = p5(ctx[4].s4, 4);
	ctx[3].s6 = p6(ctx[3].s5, 3);
	ctx[2].s7 = p7(ctx[2].s6, 2);
	ctx[1].s8 = p8(ctx[1].s7, 1);
	ctx[0].s9 = p9(ctx[0].s8, 0);

	int cur = 9;
	auto forward = [&cur]() { if (++cur == 10) cur = 0; };
	auto sft = [&cur](int step)->int { return Shift(cur,step,10-step); };

	for (size_t i = 9; i < n; i++) {
		ctx[cur].s1 = p1(i);
		ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, i-1);
		ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, i-2);
		ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, i-3);
		ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, i-4);
		ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, i-5);
		ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, i-6);
		ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, i-7);
		ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, i-8);
		p10(ctx[sft(9)].s9, i-9);
		forward();
	}

	ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, n-1);
	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-2);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-3);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-4);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-5);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-6);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-7);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-8);
	p10(ctx[sft(9)].s9, n-9);
	forward();

	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-1);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-2);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-3);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-4);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-5);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-6);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-7);
	p10(ctx[sft(9)].s9, n-8);
	forward();

	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-1);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-2);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-3);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-4);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-5);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-6);
	p10(ctx[sft(9)].s9, n-7);
	forward();

	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-1);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-2);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-3);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-4);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-5);
	p10(ctx[sft(9)].s9, n-6);
	forward();

	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-1);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-2);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-3);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-4);
	p10(ctx[sft(9)].s9, n-5);
	forward();

	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-1);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-2);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-3);
	p10(ctx[sft(9)].s9, n-4);
	forward();

	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-1);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-2);
	p10(ctx[sft(9)].s9, n-3);
	forward();

	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-1);
	p10(ctx[sft(9)].s9, n-2);
	forward();

	p10(ctx[sft(9)].s9, n-1);
	forward();
}

template <typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9, typename P10, typename P11>
static inline __attribute__((always_inline)) void
Pipeline(size_t n, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9, const P10& p10, const P11& p11) {
	using S1 = std::result_of_t<P1(size_t)>;
	using S2 = std::result_of_t<P2(S1,size_t)>;
	using S3 = std::result_of_t<P3(S2,size_t)>;
	using S4 = std::result_of_t<P4(S3,size_t)>;
	using S5 = std::result_of_t<P5(S4,size_t)>;
	using S6 = std::result_of_t<P6(S5,size_t)>;
	using S7 = std::result_of_t<P7(S6,size_t)>;
	using S8 = std::result_of_t<P8(S7,size_t)>;
	using S9 = std::result_of_t<P9(S8,size_t)>;
	using S10 = std::result_of_t<P10(S9,size_t)>;
	union {
		S1 s1; S2 s2; S3 s3; S4 s4; S5 s5; S6 s6; S7 s7; S8 s8; S9 s9; S10 s10;
	} ctx[11];

	if (n < 10) {
		for (size_t i = 0; i < n; i++) ctx[i].s1 = p1(i);
		for (size_t i = 0; i < n; i++) ctx[i].s2 = p2(ctx[i].s1, i);
		for (size_t i = 0; i < n; i++) ctx[i].s3 = p3(ctx[i].s2, i);
		for (size_t i = 0; i < n; i++) ctx[i].s4 = p4(ctx[i].s3, i);
		for (size_t i = 0; i < n; i++) ctx[i].s5 = p5(ctx[i].s4, i);
		for (size_t i = 0; i < n; i++) ctx[i].s6 = p6(ctx[i].s5, i);
		for (size_t i = 0; i < n; i++) ctx[i].s7 = p7(ctx[i].s6, i);
		for (size_t i = 0; i < n; i++) ctx[i].s8 = p8(ctx[i].s7, i);
		for (size_t i = 0; i < n; i++) ctx[i].s9 = p9(ctx[i].s8, i);
		for (size_t i = 0; i < n; i++) ctx[i].s10 = p10(ctx[i].s9, i);
		for (size_t i = 0; i < n; i++) p11(ctx[i].s10, i);
		return;
	}

	ctx[0].s1 = p1(0);

	ctx[1].s1 = p1(1);
	ctx[0].s2 = p2(ctx[0].s1, 0);

	ctx[2].s1 = p1(2);
	ctx[1].s2 = p2(ctx[1].s1, 1);
	ctx[0].s3 = p3(ctx[0].s2, 0);

	ctx[3].s1 = p1(3);
	ctx[2].s2 = p2(ctx[2].s1, 2);
	ctx[1].s3 = p3(ctx[1].s2, 1);
	ctx[0].s4 = p4(ctx[0].s3, 0);

	ctx[4].s1 = p1(4);
	ctx[3].s2 = p2(ctx[3].s1, 3);
	ctx[2].s3 = p3(ctx[2].s2, 2);
	ctx[1].s4 = p4(ctx[1].s3, 1);
	ctx[0].s5 = p5(ctx[0].s4, 0);

	ctx[5].s1 = p1(5);
	ctx[4].s2 = p2(ctx[4].s1, 4);
	ctx[3].s3 = p3(ctx[3].s2, 3);
	ctx[2].s4 = p4(ctx[2].s3, 2);
	ctx[1].s5 = p5(ctx[1].s4, 1);
	ctx[0].s6 = p6(ctx[0].s5, 0);

	ctx[6].s1 = p1(6);
	ctx[5].s2 = p2(ctx[5].s1, 5);
	ctx[4].s3 = p3(ctx[4].s2, 4);
	ctx[3].s4 = p4(ctx[3].s3, 3);
	ctx[2].s5 = p5(ctx[2].s4, 2);
	ctx[1].s6 = p6(ctx[1].s5, 1);
	ctx[0].s7 = p7(ctx[0].s6, 0);

	ctx[7].s1 = p1(7);
	ctx[6].s2 = p2(ctx[6].s1, 6);
	ctx[5].s3 = p3(ctx[5].s2, 5);
	ctx[4].s4 = p4(ctx[4].s3, 4);
	ctx[3].s5 = p5(ctx[3].s4, 3);
	ctx[2].s6 = p6(ctx[2].s5, 2);
	ctx[1].s7 = p7(ctx[1].s6, 1);
	ctx[0].s8 = p8(ctx[0].s7, 0);

	ctx[8].s1 = p1(8);
	ctx[7].s2 = p2(ctx[7].s1, 7);
	ctx[6].s3 = p3(ctx[6].s2, 6);
	ctx[5].s4 = p4(ctx[5].s3, 5);
	ctx[4].s5 = p5(ctx[4].s4, 4);
	ctx[3].s6 = p6(ctx[3].s5, 3);
	ctx[2].s7 = p7(ctx[2].s6, 2);
	ctx[1].s8 = p8(ctx[1].s7, 1);
	ctx[0].s9 = p9(ctx[0].s8, 0);

	ctx[9].s1 = p1(9);
	ctx[8].s2 = p2(ctx[8].s1, 8);
	ctx[7].s3 = p3(ctx[7].s2, 7);
	ctx[6].s4 = p4(ctx[6].s3, 6);
	ctx[5].s5 = p5(ctx[5].s4, 5);
	ctx[4].s6 = p6(ctx[4].s5, 4);
	ctx[3].s7 = p7(ctx[3].s6, 3);
	ctx[2].s8 = p8(ctx[2].s7, 2);
	ctx[1].s9 = p9(ctx[1].s8, 1);
	ctx[0].s10 = p10(ctx[0].s9, 0);

	int cur = 10;
	auto forward = [&cur]() { if (++cur == 11) cur = 0; };
	auto sft = [&cur](int step)->int { return Shift(cur,step,11-step); };

	for (size_t i = 10; i < n; i++) {
		ctx[cur].s1 = p1(i);
		ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, i-1);
		ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, i-2);
		ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, i-3);
		ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, i-4);
		ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, i-5);
		ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, i-6);
		ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, i-7);
		ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, i-8);
		ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, i-9);
		p11(ctx[sft(10)].s10, i-10);
		forward();
	}

	ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, n-1);
	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-2);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-3);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-4);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-5);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-6);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-7);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-8);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-9);
	p11(ctx[sft(10)].s10, n-10);
	forward();

	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-1);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-2);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-3);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-4);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-5);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-6);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-7);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-8);
	p11(ctx[sft(10)].s10, n-9);
	forward();

	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-1);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-2);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-3);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-4);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-5);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-6);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-7);
	p11(ctx[sft(10)].s10, n-8);
	forward();

	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-1);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-2);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-3);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-4);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-5);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-6);
	p11(ctx[sft(10)].s10, n-7);
	forward();

	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-1);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-2);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-3);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-4);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-5);
	p11(ctx[sft(10)].s10, n-6);
	forward();

	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-1);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-2);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-3);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-4);
	p11(ctx[sft(10)].s10, n-5);
	forward();

	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-1);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-2);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-3);
	p11(ctx[sft(10)].s10, n-4);
	forward();

	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-1);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-2);
	p11(ctx[sft(10)].s10, n-3);
	forward();

	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-1);
	p11(ctx[sft(10)].s10, n-2);
	forward();

	p11(ctx[sft(10)].s10, n-1);
	forward();
}

template <typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9, typename P10, typename P11, typename P12>
static inline __attribute__((always_inline)) void
Pipeline(size_t n, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9, const P10& p10, const P11& p11, const P12& p12) {
	using S1 = std::result_of_t<P1(size_t)>;
	using S2 = std::result_of_t<P2(S1,size_t)>;
	using S3 = std::result_of_t<P3(S2,size_t)>;
	using S4 = std::result_of_t<P4(S3,size_t)>;
	using S5 = std::result_of_t<P5(S4,size_t)>;
	using S6 = std::result_of_t<P6(S5,size_t)>;
	using S7 = std::result_of_t<P7(S6,size_t)>;
	using S8 = std::result_of_t<P8(S7,size_t)>;
	using S9 = std::result_of_t<P9(S8,size_t)>;
	using S10 = std::result_of_t<P10(S9,size_t)>;
	using S11 = std::result_of_t<P11(S10,size_t)>;
	union {
		S1 s1; S2 s2; S3 s3; S4 s4; S5 s5; S6 s6; S7 s7; S8 s8; S9 s9; S10 s10; S11 s11;
	} ctx[12];

	if (n < 11) {
		for (size_t i = 0; i < n; i++) ctx[i].s1 = p1(i);
		for (size_t i = 0; i < n; i++) ctx[i].s2 = p2(ctx[i].s1, i);
		for (size_t i = 0; i < n; i++) ctx[i].s3 = p3(ctx[i].s2, i);
		for (size_t i = 0; i < n; i++) ctx[i].s4 = p4(ctx[i].s3, i);
		for (size_t i = 0; i < n; i++) ctx[i].s5 = p5(ctx[i].s4, i);
		for (size_t i = 0; i < n; i++) ctx[i].s6 = p6(ctx[i].s5, i);
		for (size_t i = 0; i < n; i++) ctx[i].s7 = p7(ctx[i].s6, i);
		for (size_t i = 0; i < n; i++) ctx[i].s8 = p8(ctx[i].s7, i);
		for (size_t i = 0; i < n; i++) ctx[i].s9 = p9(ctx[i].s8, i);
		for (size_t i = 0; i < n; i++) ctx[i].s10 = p10(ctx[i].s9, i);
		for (size_t i = 0; i < n; i++) ctx[i].s11 = p11(ctx[i].s10, i);
		for (size_t i = 0; i < n; i++) p12(ctx[i].s11, i);
		return;
	}

	ctx[0].s1 = p1(0);

	ctx[1].s1 = p1(1);
	ctx[0].s2 = p2(ctx[0].s1, 0);

	ctx[2].s1 = p1(2);
	ctx[1].s2 = p2(ctx[1].s1, 1);
	ctx[0].s3 = p3(ctx[0].s2, 0);

	ctx[3].s1 = p1(3);
	ctx[2].s2 = p2(ctx[2].s1, 2);
	ctx[1].s3 = p3(ctx[1].s2, 1);
	ctx[0].s4 = p4(ctx[0].s3, 0);

	ctx[4].s1 = p1(4);
	ctx[3].s2 = p2(ctx[3].s1, 3);
	ctx[2].s3 = p3(ctx[2].s2, 2);
	ctx[1].s4 = p4(ctx[1].s3, 1);
	ctx[0].s5 = p5(ctx[0].s4, 0);

	ctx[5].s1 = p1(5);
	ctx[4].s2 = p2(ctx[4].s1, 4);
	ctx[3].s3 = p3(ctx[3].s2, 3);
	ctx[2].s4 = p4(ctx[2].s3, 2);
	ctx[1].s5 = p5(ctx[1].s4, 1);
	ctx[0].s6 = p6(ctx[0].s5, 0);

	ctx[6].s1 = p1(6);
	ctx[5].s2 = p2(ctx[5].s1, 5);
	ctx[4].s3 = p3(ctx[4].s2, 4);
	ctx[3].s4 = p4(ctx[3].s3, 3);
	ctx[2].s5 = p5(ctx[2].s4, 2);
	ctx[1].s6 = p6(ctx[1].s5, 1);
	ctx[0].s7 = p7(ctx[0].s6, 0);

	ctx[7].s1 = p1(7);
	ctx[6].s2 = p2(ctx[6].s1, 6);
	ctx[5].s3 = p3(ctx[5].s2, 5);
	ctx[4].s4 = p4(ctx[4].s3, 4);
	ctx[3].s5 = p5(ctx[3].s4, 3);
	ctx[2].s6 = p6(ctx[2].s5, 2);
	ctx[1].s7 = p7(ctx[1].s6, 1);
	ctx[0].s8 = p8(ctx[0].s7, 0);

	ctx[8].s1 = p1(8);
	ctx[7].s2 = p2(ctx[7].s1, 7);
	ctx[6].s3 = p3(ctx[6].s2, 6);
	ctx[5].s4 = p4(ctx[5].s3, 5);
	ctx[4].s5 = p5(ctx[4].s4, 4);
	ctx[3].s6 = p6(ctx[3].s5, 3);
	ctx[2].s7 = p7(ctx[2].s6, 2);
	ctx[1].s8 = p8(ctx[1].s7, 1);
	ctx[0].s9 = p9(ctx[0].s8, 0);

	ctx[9].s1 = p1(9);
	ctx[8].s2 = p2(ctx[8].s1, 8);
	ctx[7].s3 = p3(ctx[7].s2, 7);
	ctx[6].s4 = p4(ctx[6].s3, 6);
	ctx[5].s5 = p5(ctx[5].s4, 5);
	ctx[4].s6 = p6(ctx[4].s5, 4);
	ctx[3].s7 = p7(ctx[3].s6, 3);
	ctx[2].s8 = p8(ctx[2].s7, 2);
	ctx[1].s9 = p9(ctx[1].s8, 1);
	ctx[0].s10 = p10(ctx[0].s9, 0);

	ctx[10].s1 = p1(10);
	ctx[9].s2 = p2(ctx[9].s1, 9);
	ctx[8].s3 = p3(ctx[8].s2, 8);
	ctx[7].s4 = p4(ctx[7].s3, 7);
	ctx[6].s5 = p5(ctx[6].s4, 6);
	ctx[5].s6 = p6(ctx[5].s5, 5);
	ctx[4].s7 = p7(ctx[4].s6, 4);
	ctx[3].s8 = p8(ctx[3].s7, 3);
	ctx[2].s9 = p9(ctx[2].s8, 2);
	ctx[1].s10 = p10(ctx[1].s9, 1);
	ctx[0].s11 = p11(ctx[0].s10, 0);

	int cur = 11;
	auto forward = [&cur]() { if (++cur == 12) cur = 0; };
	auto sft = [&cur](int step)->int { return Shift(cur,step,12-step); };

	for (size_t i = 11; i < n; i++) {
		ctx[cur].s1 = p1(i);
		ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, i-1);
		ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, i-2);
		ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, i-3);
		ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, i-4);
		ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, i-5);
		ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, i-6);
		ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, i-7);
		ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, i-8);
		ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, i-9);
		ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, i-10);
		p12(ctx[sft(11)].s11, i-11);
		forward();
	}

	ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, n-1);
	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-2);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-3);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-4);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-5);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-6);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-7);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-8);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-9);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-10);
	p12(ctx[sft(11)].s11, n-11);
	forward();

	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-1);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-2);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-3);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-4);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-5);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-6);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-7);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-8);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-9);
	p12(ctx[sft(11)].s11, n-10);
	forward();

	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-1);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-2);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-3);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-4);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-5);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-6);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-7);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-8);
	p12(ctx[sft(11)].s11, n-9);
	forward();

	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-1);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-2);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-3);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-4);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-5);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-6);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-7);
	p12(ctx[sft(11)].s11, n-8);
	forward();

	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-1);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-2);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-3);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-4);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-5);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-6);
	p12(ctx[sft(11)].s11, n-7);
	forward();

	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-1);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-2);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-3);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-4);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-5);
	p12(ctx[sft(11)].s11, n-6);
	forward();

	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-1);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-2);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-3);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-4);
	p12(ctx[sft(11)].s11, n-5);
	forward();

	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-1);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-2);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-3);
	p12(ctx[sft(11)].s11, n-4);
	forward();

	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-1);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-2);
	p12(ctx[sft(11)].s11, n-3);
	forward();

	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-1);
	p12(ctx[sft(11)].s11, n-2);
	forward();

	p12(ctx[sft(11)].s11, n-1);
	forward();
}

template <typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9, typename P10, typename P11, typename P12, typename P13>
static inline __attribute__((always_inline)) void
Pipeline(size_t n, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9, const P10& p10, const P11& p11, const P12& p12, const P13& p13) {
	using S1 = std::result_of_t<P1(size_t)>;
	using S2 = std::result_of_t<P2(S1,size_t)>;
	using S3 = std::result_of_t<P3(S2,size_t)>;
	using S4 = std::result_of_t<P4(S3,size_t)>;
	using S5 = std::result_of_t<P5(S4,size_t)>;
	using S6 = std::result_of_t<P6(S5,size_t)>;
	using S7 = std::result_of_t<P7(S6,size_t)>;
	using S8 = std::result_of_t<P8(S7,size_t)>;
	using S9 = std::result_of_t<P9(S8,size_t)>;
	using S10 = std::result_of_t<P10(S9,size_t)>;
	using S11 = std::result_of_t<P11(S10,size_t)>;
	using S12 = std::result_of_t<P12(S11,size_t)>;
	union {
		S1 s1; S2 s2; S3 s3; S4 s4; S5 s5; S6 s6; S7 s7; S8 s8; S9 s9; S10 s10; S11 s11; S12 s12;
	} ctx[13];

	if (n < 12) {
		for (size_t i = 0; i < n; i++) ctx[i].s1 = p1(i);
		for (size_t i = 0; i < n; i++) ctx[i].s2 = p2(ctx[i].s1, i);
		for (size_t i = 0; i < n; i++) ctx[i].s3 = p3(ctx[i].s2, i);
		for (size_t i = 0; i < n; i++) ctx[i].s4 = p4(ctx[i].s3, i);
		for (size_t i = 0; i < n; i++) ctx[i].s5 = p5(ctx[i].s4, i);
		for (size_t i = 0; i < n; i++) ctx[i].s6 = p6(ctx[i].s5, i);
		for (size_t i = 0; i < n; i++) ctx[i].s7 = p7(ctx[i].s6, i);
		for (size_t i = 0; i < n; i++) ctx[i].s8 = p8(ctx[i].s7, i);
		for (size_t i = 0; i < n; i++) ctx[i].s9 = p9(ctx[i].s8, i);
		for (size_t i = 0; i < n; i++) ctx[i].s10 = p10(ctx[i].s9, i);
		for (size_t i = 0; i < n; i++) ctx[i].s11 = p11(ctx[i].s10, i);
		for (size_t i = 0; i < n; i++) ctx[i].s12 = p12(ctx[i].s11, i);
		for (size_t i = 0; i < n; i++) p13(ctx[i].s12, i);
		return;
	}

	ctx[0].s1 = p1(0);

	ctx[1].s1 = p1(1);
	ctx[0].s2 = p2(ctx[0].s1, 0);

	ctx[2].s1 = p1(2);
	ctx[1].s2 = p2(ctx[1].s1, 1);
	ctx[0].s3 = p3(ctx[0].s2, 0);

	ctx[3].s1 = p1(3);
	ctx[2].s2 = p2(ctx[2].s1, 2);
	ctx[1].s3 = p3(ctx[1].s2, 1);
	ctx[0].s4 = p4(ctx[0].s3, 0);

	ctx[4].s1 = p1(4);
	ctx[3].s2 = p2(ctx[3].s1, 3);
	ctx[2].s3 = p3(ctx[2].s2, 2);
	ctx[1].s4 = p4(ctx[1].s3, 1);
	ctx[0].s5 = p5(ctx[0].s4, 0);

	ctx[5].s1 = p1(5);
	ctx[4].s2 = p2(ctx[4].s1, 4);
	ctx[3].s3 = p3(ctx[3].s2, 3);
	ctx[2].s4 = p4(ctx[2].s3, 2);
	ctx[1].s5 = p5(ctx[1].s4, 1);
	ctx[0].s6 = p6(ctx[0].s5, 0);

	ctx[6].s1 = p1(6);
	ctx[5].s2 = p2(ctx[5].s1, 5);
	ctx[4].s3 = p3(ctx[4].s2, 4);
	ctx[3].s4 = p4(ctx[3].s3, 3);
	ctx[2].s5 = p5(ctx[2].s4, 2);
	ctx[1].s6 = p6(ctx[1].s5, 1);
	ctx[0].s7 = p7(ctx[0].s6, 0);

	ctx[7].s1 = p1(7);
	ctx[6].s2 = p2(ctx[6].s1, 6);
	ctx[5].s3 = p3(ctx[5].s2, 5);
	ctx[4].s4 = p4(ctx[4].s3, 4);
	ctx[3].s5 = p5(ctx[3].s4, 3);
	ctx[2].s6 = p6(ctx[2].s5, 2);
	ctx[1].s7 = p7(ctx[1].s6, 1);
	ctx[0].s8 = p8(ctx[0].s7, 0);

	ctx[8].s1 = p1(8);
	ctx[7].s2 = p2(ctx[7].s1, 7);
	ctx[6].s3 = p3(ctx[6].s2, 6);
	ctx[5].s4 = p4(ctx[5].s3, 5);
	ctx[4].s5 = p5(ctx[4].s4, 4);
	ctx[3].s6 = p6(ctx[3].s5, 3);
	ctx[2].s7 = p7(ctx[2].s6, 2);
	ctx[1].s8 = p8(ctx[1].s7, 1);
	ctx[0].s9 = p9(ctx[0].s8, 0);

	ctx[9].s1 = p1(9);
	ctx[8].s2 = p2(ctx[8].s1, 8);
	ctx[7].s3 = p3(ctx[7].s2, 7);
	ctx[6].s4 = p4(ctx[6].s3, 6);
	ctx[5].s5 = p5(ctx[5].s4, 5);
	ctx[4].s6 = p6(ctx[4].s5, 4);
	ctx[3].s7 = p7(ctx[3].s6, 3);
	ctx[2].s8 = p8(ctx[2].s7, 2);
	ctx[1].s9 = p9(ctx[1].s8, 1);
	ctx[0].s10 = p10(ctx[0].s9, 0);

	ctx[10].s1 = p1(10);
	ctx[9].s2 = p2(ctx[9].s1, 9);
	ctx[8].s3 = p3(ctx[8].s2, 8);
	ctx[7].s4 = p4(ctx[7].s3, 7);
	ctx[6].s5 = p5(ctx[6].s4, 6);
	ctx[5].s6 = p6(ctx[5].s5, 5);
	ctx[4].s7 = p7(ctx[4].s6, 4);
	ctx[3].s8 = p8(ctx[3].s7, 3);
	ctx[2].s9 = p9(ctx[2].s8, 2);
	ctx[1].s10 = p10(ctx[1].s9, 1);
	ctx[0].s11 = p11(ctx[0].s10, 0);

	ctx[11].s1 = p1(11);
	ctx[10].s2 = p2(ctx[10].s1, 10);
	ctx[9].s3 = p3(ctx[9].s2, 9);
	ctx[8].s4 = p4(ctx[8].s3, 8);
	ctx[7].s5 = p5(ctx[7].s4, 7);
	ctx[6].s6 = p6(ctx[6].s5, 6);
	ctx[5].s7 = p7(ctx[5].s6, 5);
	ctx[4].s8 = p8(ctx[4].s7, 4);
	ctx[3].s9 = p9(ctx[3].s8, 3);
	ctx[2].s10 = p10(ctx[2].s9, 2);
	ctx[1].s11 = p11(ctx[1].s10, 1);
	ctx[0].s12 = p12(ctx[0].s11, 0);

	int cur = 12;
	auto forward = [&cur]() { if (++cur == 13) cur = 0; };
	auto sft = [&cur](int step)->int { return Shift(cur,step,13-step); };

	for (size_t i = 12; i < n; i++) {
		ctx[cur].s1 = p1(i);
		ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, i-1);
		ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, i-2);
		ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, i-3);
		ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, i-4);
		ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, i-5);
		ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, i-6);
		ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, i-7);
		ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, i-8);
		ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, i-9);
		ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, i-10);
		ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, i-11);
		p13(ctx[sft(12)].s12, i-12);
		forward();
	}

	ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, n-1);
	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-2);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-3);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-4);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-5);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-6);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-7);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-8);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-9);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-10);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-11);
	p13(ctx[sft(12)].s12, n-12);
	forward();

	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-1);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-2);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-3);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-4);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-5);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-6);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-7);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-8);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-9);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-10);
	p13(ctx[sft(12)].s12, n-11);
	forward();

	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-1);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-2);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-3);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-4);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-5);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-6);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-7);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-8);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-9);
	p13(ctx[sft(12)].s12, n-10);
	forward();

	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-1);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-2);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-3);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-4);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-5);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-6);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-7);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-8);
	p13(ctx[sft(12)].s12, n-9);
	forward();

	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-1);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-2);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-3);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-4);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-5);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-6);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-7);
	p13(ctx[sft(12)].s12, n-8);
	forward();

	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-1);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-2);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-3);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-4);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-5);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-6);
	p13(ctx[sft(12)].s12, n-7);
	forward();

	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-1);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-2);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-3);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-4);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-5);
	p13(ctx[sft(12)].s12, n-6);
	forward();

	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-1);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-2);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-3);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-4);
	p13(ctx[sft(12)].s12, n-5);
	forward();

	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-1);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-2);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-3);
	p13(ctx[sft(12)].s12, n-4);
	forward();

	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-1);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-2);
	p13(ctx[sft(12)].s12, n-3);
	forward();

	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-1);
	p13(ctx[sft(12)].s12, n-2);
	forward();

	p13(ctx[sft(12)].s12, n-1);
	forward();
}

template <typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9, typename P10, typename P11, typename P12, typename P13, typename P14>
static inline __attribute__((always_inline)) void
Pipeline(size_t n, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9, const P10& p10, const P11& p11, const P12& p12, const P13& p13, const P14& p14) {
	using S1 = std::result_of_t<P1(size_t)>;
	using S2 = std::result_of_t<P2(S1,size_t)>;
	using S3 = std::result_of_t<P3(S2,size_t)>;
	using S4 = std::result_of_t<P4(S3,size_t)>;
	using S5 = std::result_of_t<P5(S4,size_t)>;
	using S6 = std::result_of_t<P6(S5,size_t)>;
	using S7 = std::result_of_t<P7(S6,size_t)>;
	using S8 = std::result_of_t<P8(S7,size_t)>;
	using S9 = std::result_of_t<P9(S8,size_t)>;
	using S10 = std::result_of_t<P10(S9,size_t)>;
	using S11 = std::result_of_t<P11(S10,size_t)>;
	using S12 = std::result_of_t<P12(S11,size_t)>;
	using S13 = std::result_of_t<P13(S12,size_t)>;
	union {
		S1 s1; S2 s2; S3 s3; S4 s4; S5 s5; S6 s6; S7 s7; S8 s8; S9 s9; S10 s10; S11 s11; S12 s12; S13 s13;
	} ctx[14];

	if (n < 13) {
		for (size_t i = 0; i < n; i++) ctx[i].s1 = p1(i);
		for (size_t i = 0; i < n; i++) ctx[i].s2 = p2(ctx[i].s1, i);
		for (size_t i = 0; i < n; i++) ctx[i].s3 = p3(ctx[i].s2, i);
		for (size_t i = 0; i < n; i++) ctx[i].s4 = p4(ctx[i].s3, i);
		for (size_t i = 0; i < n; i++) ctx[i].s5 = p5(ctx[i].s4, i);
		for (size_t i = 0; i < n; i++) ctx[i].s6 = p6(ctx[i].s5, i);
		for (size_t i = 0; i < n; i++) ctx[i].s7 = p7(ctx[i].s6, i);
		for (size_t i = 0; i < n; i++) ctx[i].s8 = p8(ctx[i].s7, i);
		for (size_t i = 0; i < n; i++) ctx[i].s9 = p9(ctx[i].s8, i);
		for (size_t i = 0; i < n; i++) ctx[i].s10 = p10(ctx[i].s9, i);
		for (size_t i = 0; i < n; i++) ctx[i].s11 = p11(ctx[i].s10, i);
		for (size_t i = 0; i < n; i++) ctx[i].s12 = p12(ctx[i].s11, i);
		for (size_t i = 0; i < n; i++) ctx[i].s13 = p13(ctx[i].s12, i);
		for (size_t i = 0; i < n; i++) p14(ctx[i].s13, i);
		return;
	}

	ctx[0].s1 = p1(0);

	ctx[1].s1 = p1(1);
	ctx[0].s2 = p2(ctx[0].s1, 0);

	ctx[2].s1 = p1(2);
	ctx[1].s2 = p2(ctx[1].s1, 1);
	ctx[0].s3 = p3(ctx[0].s2, 0);

	ctx[3].s1 = p1(3);
	ctx[2].s2 = p2(ctx[2].s1, 2);
	ctx[1].s3 = p3(ctx[1].s2, 1);
	ctx[0].s4 = p4(ctx[0].s3, 0);

	ctx[4].s1 = p1(4);
	ctx[3].s2 = p2(ctx[3].s1, 3);
	ctx[2].s3 = p3(ctx[2].s2, 2);
	ctx[1].s4 = p4(ctx[1].s3, 1);
	ctx[0].s5 = p5(ctx[0].s4, 0);

	ctx[5].s1 = p1(5);
	ctx[4].s2 = p2(ctx[4].s1, 4);
	ctx[3].s3 = p3(ctx[3].s2, 3);
	ctx[2].s4 = p4(ctx[2].s3, 2);
	ctx[1].s5 = p5(ctx[1].s4, 1);
	ctx[0].s6 = p6(ctx[0].s5, 0);

	ctx[6].s1 = p1(6);
	ctx[5].s2 = p2(ctx[5].s1, 5);
	ctx[4].s3 = p3(ctx[4].s2, 4);
	ctx[3].s4 = p4(ctx[3].s3, 3);
	ctx[2].s5 = p5(ctx[2].s4, 2);
	ctx[1].s6 = p6(ctx[1].s5, 1);
	ctx[0].s7 = p7(ctx[0].s6, 0);

	ctx[7].s1 = p1(7);
	ctx[6].s2 = p2(ctx[6].s1, 6);
	ctx[5].s3 = p3(ctx[5].s2, 5);
	ctx[4].s4 = p4(ctx[4].s3, 4);
	ctx[3].s5 = p5(ctx[3].s4, 3);
	ctx[2].s6 = p6(ctx[2].s5, 2);
	ctx[1].s7 = p7(ctx[1].s6, 1);
	ctx[0].s8 = p8(ctx[0].s7, 0);

	ctx[8].s1 = p1(8);
	ctx[7].s2 = p2(ctx[7].s1, 7);
	ctx[6].s3 = p3(ctx[6].s2, 6);
	ctx[5].s4 = p4(ctx[5].s3, 5);
	ctx[4].s5 = p5(ctx[4].s4, 4);
	ctx[3].s6 = p6(ctx[3].s5, 3);
	ctx[2].s7 = p7(ctx[2].s6, 2);
	ctx[1].s8 = p8(ctx[1].s7, 1);
	ctx[0].s9 = p9(ctx[0].s8, 0);

	ctx[9].s1 = p1(9);
	ctx[8].s2 = p2(ctx[8].s1, 8);
	ctx[7].s3 = p3(ctx[7].s2, 7);
	ctx[6].s4 = p4(ctx[6].s3, 6);
	ctx[5].s5 = p5(ctx[5].s4, 5);
	ctx[4].s6 = p6(ctx[4].s5, 4);
	ctx[3].s7 = p7(ctx[3].s6, 3);
	ctx[2].s8 = p8(ctx[2].s7, 2);
	ctx[1].s9 = p9(ctx[1].s8, 1);
	ctx[0].s10 = p10(ctx[0].s9, 0);

	ctx[10].s1 = p1(10);
	ctx[9].s2 = p2(ctx[9].s1, 9);
	ctx[8].s3 = p3(ctx[8].s2, 8);
	ctx[7].s4 = p4(ctx[7].s3, 7);
	ctx[6].s5 = p5(ctx[6].s4, 6);
	ctx[5].s6 = p6(ctx[5].s5, 5);
	ctx[4].s7 = p7(ctx[4].s6, 4);
	ctx[3].s8 = p8(ctx[3].s7, 3);
	ctx[2].s9 = p9(ctx[2].s8, 2);
	ctx[1].s10 = p10(ctx[1].s9, 1);
	ctx[0].s11 = p11(ctx[0].s10, 0);

	ctx[11].s1 = p1(11);
	ctx[10].s2 = p2(ctx[10].s1, 10);
	ctx[9].s3 = p3(ctx[9].s2, 9);
	ctx[8].s4 = p4(ctx[8].s3, 8);
	ctx[7].s5 = p5(ctx[7].s4, 7);
	ctx[6].s6 = p6(ctx[6].s5, 6);
	ctx[5].s7 = p7(ctx[5].s6, 5);
	ctx[4].s8 = p8(ctx[4].s7, 4);
	ctx[3].s9 = p9(ctx[3].s8, 3);
	ctx[2].s10 = p10(ctx[2].s9, 2);
	ctx[1].s11 = p11(ctx[1].s10, 1);
	ctx[0].s12 = p12(ctx[0].s11, 0);

	ctx[12].s1 = p1(12);
	ctx[11].s2 = p2(ctx[11].s1, 11);
	ctx[10].s3 = p3(ctx[10].s2, 10);
	ctx[9].s4 = p4(ctx[9].s3, 9);
	ctx[8].s5 = p5(ctx[8].s4, 8);
	ctx[7].s6 = p6(ctx[7].s5, 7);
	ctx[6].s7 = p7(ctx[6].s6, 6);
	ctx[5].s8 = p8(ctx[5].s7, 5);
	ctx[4].s9 = p9(ctx[4].s8, 4);
	ctx[3].s10 = p10(ctx[3].s9, 3);
	ctx[2].s11 = p11(ctx[2].s10, 2);
	ctx[1].s12 = p12(ctx[1].s11, 1);
	ctx[0].s13 = p13(ctx[0].s12, 0);

	int cur = 13;
	auto forward = [&cur]() { if (++cur == 14) cur = 0; };
	auto sft = [&cur](int step)->int { return Shift(cur,step,14-step); };

	for (size_t i = 13; i < n; i++) {
		ctx[cur].s1 = p1(i);
		ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, i-1);
		ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, i-2);
		ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, i-3);
		ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, i-4);
		ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, i-5);
		ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, i-6);
		ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, i-7);
		ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, i-8);
		ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, i-9);
		ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, i-10);
		ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, i-11);
		ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, i-12);
		p14(ctx[sft(13)].s13, i-13);
		forward();
	}

	ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, n-1);
	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-2);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-3);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-4);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-5);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-6);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-7);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-8);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-9);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-10);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-11);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-12);
	p14(ctx[sft(13)].s13, n-13);
	forward();

	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-1);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-2);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-3);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-4);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-5);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-6);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-7);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-8);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-9);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-10);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-11);
	p14(ctx[sft(13)].s13, n-12);
	forward();

	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-1);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-2);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-3);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-4);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-5);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-6);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-7);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-8);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-9);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-10);
	p14(ctx[sft(13)].s13, n-11);
	forward();

	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-1);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-2);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-3);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-4);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-5);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-6);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-7);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-8);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-9);
	p14(ctx[sft(13)].s13, n-10);
	forward();

	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-1);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-2);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-3);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-4);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-5);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-6);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-7);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-8);
	p14(ctx[sft(13)].s13, n-9);
	forward();

	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-1);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-2);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-3);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-4);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-5);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-6);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-7);
	p14(ctx[sft(13)].s13, n-8);
	forward();

	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-1);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-2);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-3);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-4);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-5);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-6);
	p14(ctx[sft(13)].s13, n-7);
	forward();

	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-1);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-2);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-3);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-4);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-5);
	p14(ctx[sft(13)].s13, n-6);
	forward();

	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-1);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-2);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-3);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-4);
	p14(ctx[sft(13)].s13, n-5);
	forward();

	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-1);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-2);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-3);
	p14(ctx[sft(13)].s13, n-4);
	forward();

	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-1);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-2);
	p14(ctx[sft(13)].s13, n-3);
	forward();

	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-1);
	p14(ctx[sft(13)].s13, n-2);
	forward();

	p14(ctx[sft(13)].s13, n-1);
	forward();
}

template <typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9, typename P10, typename P11, typename P12, typename P13, typename P14, typename P15>
static inline __attribute__((always_inline)) void
Pipeline(size_t n, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9, const P10& p10, const P11& p11, const P12& p12, const P13& p13, const P14& p14, const P15& p15) {
	using S1 = std::result_of_t<P1(size_t)>;
	using S2 = std::result_of_t<P2(S1,size_t)>;
	using S3 = std::result_of_t<P3(S2,size_t)>;
	using S4 = std::result_of_t<P4(S3,size_t)>;
	using S5 = std::result_of_t<P5(S4,size_t)>;
	using S6 = std::result_of_t<P6(S5,size_t)>;
	using S7 = std::result_of_t<P7(S6,size_t)>;
	using S8 = std::result_of_t<P8(S7,size_t)>;
	using S9 = std::result_of_t<P9(S8,size_t)>;
	using S10 = std::result_of_t<P10(S9,size_t)>;
	using S11 = std::result_of_t<P11(S10,size_t)>;
	using S12 = std::result_of_t<P12(S11,size_t)>;
	using S13 = std::result_of_t<P13(S12,size_t)>;
	using S14 = std::result_of_t<P14(S13,size_t)>;
	union {
		S1 s1; S2 s2; S3 s3; S4 s4; S5 s5; S6 s6; S7 s7; S8 s8; S9 s9; S10 s10; S11 s11; S12 s12; S13 s13; S14 s14;
	} ctx[15];

	if (n < 14) {
		for (size_t i = 0; i < n; i++) ctx[i].s1 = p1(i);
		for (size_t i = 0; i < n; i++) ctx[i].s2 = p2(ctx[i].s1, i);
		for (size_t i = 0; i < n; i++) ctx[i].s3 = p3(ctx[i].s2, i);
		for (size_t i = 0; i < n; i++) ctx[i].s4 = p4(ctx[i].s3, i);
		for (size_t i = 0; i < n; i++) ctx[i].s5 = p5(ctx[i].s4, i);
		for (size_t i = 0; i < n; i++) ctx[i].s6 = p6(ctx[i].s5, i);
		for (size_t i = 0; i < n; i++) ctx[i].s7 = p7(ctx[i].s6, i);
		for (size_t i = 0; i < n; i++) ctx[i].s8 = p8(ctx[i].s7, i);
		for (size_t i = 0; i < n; i++) ctx[i].s9 = p9(ctx[i].s8, i);
		for (size_t i = 0; i < n; i++) ctx[i].s10 = p10(ctx[i].s9, i);
		for (size_t i = 0; i < n; i++) ctx[i].s11 = p11(ctx[i].s10, i);
		for (size_t i = 0; i < n; i++) ctx[i].s12 = p12(ctx[i].s11, i);
		for (size_t i = 0; i < n; i++) ctx[i].s13 = p13(ctx[i].s12, i);
		for (size_t i = 0; i < n; i++) ctx[i].s14 = p14(ctx[i].s13, i);
		for (size_t i = 0; i < n; i++) p15(ctx[i].s14, i);
		return;
	}

	ctx[0].s1 = p1(0);

	ctx[1].s1 = p1(1);
	ctx[0].s2 = p2(ctx[0].s1, 0);

	ctx[2].s1 = p1(2);
	ctx[1].s2 = p2(ctx[1].s1, 1);
	ctx[0].s3 = p3(ctx[0].s2, 0);

	ctx[3].s1 = p1(3);
	ctx[2].s2 = p2(ctx[2].s1, 2);
	ctx[1].s3 = p3(ctx[1].s2, 1);
	ctx[0].s4 = p4(ctx[0].s3, 0);

	ctx[4].s1 = p1(4);
	ctx[3].s2 = p2(ctx[3].s1, 3);
	ctx[2].s3 = p3(ctx[2].s2, 2);
	ctx[1].s4 = p4(ctx[1].s3, 1);
	ctx[0].s5 = p5(ctx[0].s4, 0);

	ctx[5].s1 = p1(5);
	ctx[4].s2 = p2(ctx[4].s1, 4);
	ctx[3].s3 = p3(ctx[3].s2, 3);
	ctx[2].s4 = p4(ctx[2].s3, 2);
	ctx[1].s5 = p5(ctx[1].s4, 1);
	ctx[0].s6 = p6(ctx[0].s5, 0);

	ctx[6].s1 = p1(6);
	ctx[5].s2 = p2(ctx[5].s1, 5);
	ctx[4].s3 = p3(ctx[4].s2, 4);
	ctx[3].s4 = p4(ctx[3].s3, 3);
	ctx[2].s5 = p5(ctx[2].s4, 2);
	ctx[1].s6 = p6(ctx[1].s5, 1);
	ctx[0].s7 = p7(ctx[0].s6, 0);

	ctx[7].s1 = p1(7);
	ctx[6].s2 = p2(ctx[6].s1, 6);
	ctx[5].s3 = p3(ctx[5].s2, 5);
	ctx[4].s4 = p4(ctx[4].s3, 4);
	ctx[3].s5 = p5(ctx[3].s4, 3);
	ctx[2].s6 = p6(ctx[2].s5, 2);
	ctx[1].s7 = p7(ctx[1].s6, 1);
	ctx[0].s8 = p8(ctx[0].s7, 0);

	ctx[8].s1 = p1(8);
	ctx[7].s2 = p2(ctx[7].s1, 7);
	ctx[6].s3 = p3(ctx[6].s2, 6);
	ctx[5].s4 = p4(ctx[5].s3, 5);
	ctx[4].s5 = p5(ctx[4].s4, 4);
	ctx[3].s6 = p6(ctx[3].s5, 3);
	ctx[2].s7 = p7(ctx[2].s6, 2);
	ctx[1].s8 = p8(ctx[1].s7, 1);
	ctx[0].s9 = p9(ctx[0].s8, 0);

	ctx[9].s1 = p1(9);
	ctx[8].s2 = p2(ctx[8].s1, 8);
	ctx[7].s3 = p3(ctx[7].s2, 7);
	ctx[6].s4 = p4(ctx[6].s3, 6);
	ctx[5].s5 = p5(ctx[5].s4, 5);
	ctx[4].s6 = p6(ctx[4].s5, 4);
	ctx[3].s7 = p7(ctx[3].s6, 3);
	ctx[2].s8 = p8(ctx[2].s7, 2);
	ctx[1].s9 = p9(ctx[1].s8, 1);
	ctx[0].s10 = p10(ctx[0].s9, 0);

	ctx[10].s1 = p1(10);
	ctx[9].s2 = p2(ctx[9].s1, 9);
	ctx[8].s3 = p3(ctx[8].s2, 8);
	ctx[7].s4 = p4(ctx[7].s3, 7);
	ctx[6].s5 = p5(ctx[6].s4, 6);
	ctx[5].s6 = p6(ctx[5].s5, 5);
	ctx[4].s7 = p7(ctx[4].s6, 4);
	ctx[3].s8 = p8(ctx[3].s7, 3);
	ctx[2].s9 = p9(ctx[2].s8, 2);
	ctx[1].s10 = p10(ctx[1].s9, 1);
	ctx[0].s11 = p11(ctx[0].s10, 0);

	ctx[11].s1 = p1(11);
	ctx[10].s2 = p2(ctx[10].s1, 10);
	ctx[9].s3 = p3(ctx[9].s2, 9);
	ctx[8].s4 = p4(ctx[8].s3, 8);
	ctx[7].s5 = p5(ctx[7].s4, 7);
	ctx[6].s6 = p6(ctx[6].s5, 6);
	ctx[5].s7 = p7(ctx[5].s6, 5);
	ctx[4].s8 = p8(ctx[4].s7, 4);
	ctx[3].s9 = p9(ctx[3].s8, 3);
	ctx[2].s10 = p10(ctx[2].s9, 2);
	ctx[1].s11 = p11(ctx[1].s10, 1);
	ctx[0].s12 = p12(ctx[0].s11, 0);

	ctx[12].s1 = p1(12);
	ctx[11].s2 = p2(ctx[11].s1, 11);
	ctx[10].s3 = p3(ctx[10].s2, 10);
	ctx[9].s4 = p4(ctx[9].s3, 9);
	ctx[8].s5 = p5(ctx[8].s4, 8);
	ctx[7].s6 = p6(ctx[7].s5, 7);
	ctx[6].s7 = p7(ctx[6].s6, 6);
	ctx[5].s8 = p8(ctx[5].s7, 5);
	ctx[4].s9 = p9(ctx[4].s8, 4);
	ctx[3].s10 = p10(ctx[3].s9, 3);
	ctx[2].s11 = p11(ctx[2].s10, 2);
	ctx[1].s12 = p12(ctx[1].s11, 1);
	ctx[0].s13 = p13(ctx[0].s12, 0);

	ctx[13].s1 = p1(13);
	ctx[12].s2 = p2(ctx[12].s1, 12);
	ctx[11].s3 = p3(ctx[11].s2, 11);
	ctx[10].s4 = p4(ctx[10].s3, 10);
	ctx[9].s5 = p5(ctx[9].s4, 9);
	ctx[8].s6 = p6(ctx[8].s5, 8);
	ctx[7].s7 = p7(ctx[7].s6, 7);
	ctx[6].s8 = p8(ctx[6].s7, 6);
	ctx[5].s9 = p9(ctx[5].s8, 5);
	ctx[4].s10 = p10(ctx[4].s9, 4);
	ctx[3].s11 = p11(ctx[3].s10, 3);
	ctx[2].s12 = p12(ctx[2].s11, 2);
	ctx[1].s13 = p13(ctx[1].s12, 1);
	ctx[0].s14 = p14(ctx[0].s13, 0);

	int cur = 14;
	auto forward = [&cur]() { if (++cur == 15) cur = 0; };
	auto sft = [&cur](int step)->int { return Shift(cur,step,15-step); };

	for (size_t i = 14; i < n; i++) {
		ctx[cur].s1 = p1(i);
		ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, i-1);
		ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, i-2);
		ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, i-3);
		ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, i-4);
		ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, i-5);
		ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, i-6);
		ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, i-7);
		ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, i-8);
		ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, i-9);
		ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, i-10);
		ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, i-11);
		ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, i-12);
		ctx[sft(13)].s14 = p14(ctx[sft(13)].s13, i-13);
		p15(ctx[sft(14)].s14, i-14);
		forward();
	}

	ctx[sft(1)].s2 = p2(ctx[sft(1)].s1, n-1);
	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-2);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-3);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-4);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-5);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-6);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-7);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-8);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-9);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-10);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-11);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-12);
	ctx[sft(13)].s14 = p14(ctx[sft(13)].s13, n-13);
	p15(ctx[sft(14)].s14, n-14);
	forward();

	ctx[sft(2)].s3 = p3(ctx[sft(2)].s2, n-1);
	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-2);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-3);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-4);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-5);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-6);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-7);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-8);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-9);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-10);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-11);
	ctx[sft(13)].s14 = p14(ctx[sft(13)].s13, n-12);
	p15(ctx[sft(14)].s14, n-13);
	forward();

	ctx[sft(3)].s4 = p4(ctx[sft(3)].s3, n-1);
	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-2);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-3);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-4);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-5);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-6);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-7);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-8);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-9);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-10);
	ctx[sft(13)].s14 = p14(ctx[sft(13)].s13, n-11);
	p15(ctx[sft(14)].s14, n-12);
	forward();

	ctx[sft(4)].s5 = p5(ctx[sft(4)].s4, n-1);
	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-2);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-3);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-4);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-5);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-6);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-7);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-8);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-9);
	ctx[sft(13)].s14 = p14(ctx[sft(13)].s13, n-10);
	p15(ctx[sft(14)].s14, n-11);
	forward();

	ctx[sft(5)].s6 = p6(ctx[sft(5)].s5, n-1);
	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-2);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-3);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-4);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-5);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-6);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-7);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-8);
	ctx[sft(13)].s14 = p14(ctx[sft(13)].s13, n-9);
	p15(ctx[sft(14)].s14, n-10);
	forward();

	ctx[sft(6)].s7 = p7(ctx[sft(6)].s6, n-1);
	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-2);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-3);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-4);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-5);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-6);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-7);
	ctx[sft(13)].s14 = p14(ctx[sft(13)].s13, n-8);
	p15(ctx[sft(14)].s14, n-9);
	forward();

	ctx[sft(7)].s8 = p8(ctx[sft(7)].s7, n-1);
	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-2);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-3);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-4);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-5);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-6);
	ctx[sft(13)].s14 = p14(ctx[sft(13)].s13, n-7);
	p15(ctx[sft(14)].s14, n-8);
	forward();

	ctx[sft(8)].s9 = p9(ctx[sft(8)].s8, n-1);
	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-2);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-3);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-4);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-5);
	ctx[sft(13)].s14 = p14(ctx[sft(13)].s13, n-6);
	p15(ctx[sft(14)].s14, n-7);
	forward();

	ctx[sft(9)].s10 = p10(ctx[sft(9)].s9, n-1);
	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-2);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-3);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-4);
	ctx[sft(13)].s14 = p14(ctx[sft(13)].s13, n-5);
	p15(ctx[sft(14)].s14, n-6);
	forward();

	ctx[sft(10)].s11 = p11(ctx[sft(10)].s10, n-1);
	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-2);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-3);
	ctx[sft(13)].s14 = p14(ctx[sft(13)].s13, n-4);
	p15(ctx[sft(14)].s14, n-5);
	forward();

	ctx[sft(11)].s12 = p12(ctx[sft(11)].s11, n-1);
	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-2);
	ctx[sft(13)].s14 = p14(ctx[sft(13)].s13, n-3);
	p15(ctx[sft(14)].s14, n-4);
	forward();

	ctx[sft(12)].s13 = p13(ctx[sft(12)].s12, n-1);
	ctx[sft(13)].s14 = p14(ctx[sft(13)].s13, n-2);
	p15(ctx[sft(14)].s14, n-3);
	forward();

	ctx[sft(13)].s14 = p14(ctx[sft(13)].s13, n-1);
	p15(ctx[sft(14)].s14, n-2);
	forward();

	p15(ctx[sft(14)].s14, n-1);
	forward();
}

#endif //CHD_PIPELINE_H_
