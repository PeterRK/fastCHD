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
	std::cout << "template <unsigned Bubble";
	for (unsigned i = 1; i <= depth; i++) {
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
	std::cout << "\tconstexpr unsigned M = Bubble + 1;\n"
			  << "\tif (n < M*" << (depth-1) << ") {\n"
			  << "\t\tunion {\n\t\t\t";
	for (unsigned i = 1; i < depth; i++) {
		std::cout << "S" << i << " s" << i << "; ";
	}
	std::cout << "\n\t\t} ctx[M*" << (depth-1) << "-1];\n"
			  << "\t\tfor (size_t i = 0; i < n; i++) ctx[i].s1 = p1(i);\n";
	for (unsigned i = 2; i < depth; i++) {
		std::cout << "\t\tfor (size_t i = 0; i < n; i++) ctx[i].s" << i
				  << " = p" << i << "(ctx[i].s" << (i-1) << ", i);\n";
	}
	std::cout << "\t\tfor (size_t i = 0; i < n; i++) p" << depth << "(ctx[i].s" << (depth-1) << ", i);\n"
			  << "\t\treturn;\n"
			  << "\t}\n";
	for (unsigned i = 1; i < depth; i++) {
		std::cout << "\tS" << i << " s" << i << "[M];\n";
	}
	for (unsigned i = 1; i < depth; i++) {
		std::cout << "\tfor (unsigned j = 0; j < M; j++) {\n";
		for (unsigned j = i; j > 1; j--) {
			std::cout << "\t\ts" << j << "[j] = p" << j << "(s" << (j-1) << "[j], M*" << (i-j) << "+j);\n";
		}
		std::cout << "\t\ts1[j] = p1(M*" << (i-1) << "+j);\n"
				  << "\t}\n";
	}
	std::cout << "\tunsigned k = 0;\n"
			  << "\tfor (size_t i = M*" << (depth-1) << "; i < n; i++) {\n"
			  << "\t\tp" << depth << "(s" << (depth-1) << "[k], i-M*" << (depth-1) << ");\n";
	for (unsigned i = depth-1; i > 1; i--) {
		std::cout << "\t\ts" << i << "[k] = p" << i << "(s" << (i-1) << "[k], i-M*" << (i-1) << ");\n";
	}
	std::cout << "\t\ts1[k] = p1(i);\n"
			  << "\t\tif (++k >= M) k = 0;\n"
			  << "\t}\n";
	for (unsigned i = 1; i < depth; i++) {
		std::cout << "\tfor (unsigned j = 0; j < M; j++) {\n"
					<< "\t\tp" << depth << "(s" << (depth-1) << "[k], n-M*" << (depth-i) << "+j);\n";
		for (unsigned j = depth-1; j > i; j--) {
			std::cout << "\t\ts" << j << "[k] = p" << j << "(s" << (j-1) << "[k], n-M*" << (j-i) << "+j);\n";
		}
		std::cout << "\t\tif (++k >= M) k = 0;\n"
				  << "\t}\n";
	}
	std::cout << "}\n" << std::endl;
}
======================================================================== */

#include <type_traits>

template <unsigned Bubble, typename P1, typename P2, typename P3>
static inline __attribute__((always_inline)) void
Pipeline(size_t n, const P1& p1, const P2& p2, const P3& p3) {
	using S1 = std::result_of_t<P1(size_t)>;
	using S2 = std::result_of_t<P2(S1,size_t)>;
	constexpr unsigned M = Bubble + 1;
	if (n < M*2) {
		union {
			S1 s1; S2 s2;
		} ctx[M*2-1];
		for (size_t i = 0; i < n; i++) ctx[i].s1 = p1(i);
		for (size_t i = 0; i < n; i++) ctx[i].s2 = p2(ctx[i].s1, i);
		for (size_t i = 0; i < n; i++) p3(ctx[i].s2, i);
		return;
	}
	S1 s1[M];
	S2 s2[M];
	for (unsigned j = 0; j < M; j++) {
		s1[j] = p1(M*0+j);
	}
	for (unsigned j = 0; j < M; j++) {
		s2[j] = p2(s1[j], M*0+j);
		s1[j] = p1(M*1+j);
	}
	unsigned k = 0;
	for (size_t i = M*2; i < n; i++) {
		p3(s2[k], i-M*2);
		s2[k] = p2(s1[k], i-M*1);
		s1[k] = p1(i);
		if (++k >= M) k = 0;
	}
	for (unsigned j = 0; j < M; j++) {
		p3(s2[k], n-M*2+j);
		s2[k] = p2(s1[k], n-M*1+j);
		if (++k >= M) k = 0;
	}
	for (unsigned j = 0; j < M; j++) {
		p3(s2[k], n-M*1+j);
		if (++k >= M) k = 0;
	}
}

template <unsigned Bubble, typename P1, typename P2, typename P3, typename P4>
static inline __attribute__((always_inline)) void
Pipeline(size_t n, const P1& p1, const P2& p2, const P3& p3, const P4& p4) {
	using S1 = std::result_of_t<P1(size_t)>;
	using S2 = std::result_of_t<P2(S1,size_t)>;
	using S3 = std::result_of_t<P3(S2,size_t)>;
	constexpr unsigned M = Bubble + 1;
	if (n < M*3) {
		union {
			S1 s1; S2 s2; S3 s3;
		} ctx[M*3-1];
		for (size_t i = 0; i < n; i++) ctx[i].s1 = p1(i);
		for (size_t i = 0; i < n; i++) ctx[i].s2 = p2(ctx[i].s1, i);
		for (size_t i = 0; i < n; i++) ctx[i].s3 = p3(ctx[i].s2, i);
		for (size_t i = 0; i < n; i++) p4(ctx[i].s3, i);
		return;
	}
	S1 s1[M];
	S2 s2[M];
	S3 s3[M];
	for (unsigned j = 0; j < M; j++) {
		s1[j] = p1(M*0+j);
	}
	for (unsigned j = 0; j < M; j++) {
		s2[j] = p2(s1[j], M*0+j);
		s1[j] = p1(M*1+j);
	}
	for (unsigned j = 0; j < M; j++) {
		s3[j] = p3(s2[j], M*0+j);
		s2[j] = p2(s1[j], M*1+j);
		s1[j] = p1(M*2+j);
	}
	unsigned k = 0;
	for (size_t i = M*3; i < n; i++) {
		p4(s3[k], i-M*3);
		s3[k] = p3(s2[k], i-M*2);
		s2[k] = p2(s1[k], i-M*1);
		s1[k] = p1(i);
		if (++k >= M) k = 0;
	}
	for (unsigned j = 0; j < M; j++) {
		p4(s3[k], n-M*3+j);
		s3[k] = p3(s2[k], n-M*2+j);
		s2[k] = p2(s1[k], n-M*1+j);
		if (++k >= M) k = 0;
	}
	for (unsigned j = 0; j < M; j++) {
		p4(s3[k], n-M*2+j);
		s3[k] = p3(s2[k], n-M*1+j);
		if (++k >= M) k = 0;
	}
	for (unsigned j = 0; j < M; j++) {
		p4(s3[k], n-M*1+j);
		if (++k >= M) k = 0;
	}
}

template <unsigned Bubble, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7>
static inline __attribute__((always_inline)) void
Pipeline(size_t n, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7) {
	using S1 = std::result_of_t<P1(size_t)>;
	using S2 = std::result_of_t<P2(S1,size_t)>;
	using S3 = std::result_of_t<P3(S2,size_t)>;
	using S4 = std::result_of_t<P4(S3,size_t)>;
	using S5 = std::result_of_t<P5(S4,size_t)>;
	using S6 = std::result_of_t<P6(S5,size_t)>;
	constexpr unsigned M = Bubble + 1;
	if (n < M*6) {
		union {
			S1 s1; S2 s2; S3 s3; S4 s4; S5 s5; S6 s6;
		} ctx[M*6-1];
		for (size_t i = 0; i < n; i++) ctx[i].s1 = p1(i);
		for (size_t i = 0; i < n; i++) ctx[i].s2 = p2(ctx[i].s1, i);
		for (size_t i = 0; i < n; i++) ctx[i].s3 = p3(ctx[i].s2, i);
		for (size_t i = 0; i < n; i++) ctx[i].s4 = p4(ctx[i].s3, i);
		for (size_t i = 0; i < n; i++) ctx[i].s5 = p5(ctx[i].s4, i);
		for (size_t i = 0; i < n; i++) ctx[i].s6 = p6(ctx[i].s5, i);
		for (size_t i = 0; i < n; i++) p7(ctx[i].s6, i);
		return;
	}
	S1 s1[M];
	S2 s2[M];
	S3 s3[M];
	S4 s4[M];
	S5 s5[M];
	S6 s6[M];
	for (unsigned j = 0; j < M; j++) {
		s1[j] = p1(M*0+j);
	}
	for (unsigned j = 0; j < M; j++) {
		s2[j] = p2(s1[j], M*0+j);
		s1[j] = p1(M*1+j);
	}
	for (unsigned j = 0; j < M; j++) {
		s3[j] = p3(s2[j], M*0+j);
		s2[j] = p2(s1[j], M*1+j);
		s1[j] = p1(M*2+j);
	}
	for (unsigned j = 0; j < M; j++) {
		s4[j] = p4(s3[j], M*0+j);
		s3[j] = p3(s2[j], M*1+j);
		s2[j] = p2(s1[j], M*2+j);
		s1[j] = p1(M*3+j);
	}
	for (unsigned j = 0; j < M; j++) {
		s5[j] = p5(s4[j], M*0+j);
		s4[j] = p4(s3[j], M*1+j);
		s3[j] = p3(s2[j], M*2+j);
		s2[j] = p2(s1[j], M*3+j);
		s1[j] = p1(M*4+j);
	}
	for (unsigned j = 0; j < M; j++) {
		s6[j] = p6(s5[j], M*0+j);
		s5[j] = p5(s4[j], M*1+j);
		s4[j] = p4(s3[j], M*2+j);
		s3[j] = p3(s2[j], M*3+j);
		s2[j] = p2(s1[j], M*4+j);
		s1[j] = p1(M*5+j);
	}
	unsigned k = 0;
	for (size_t i = M*6; i < n; i++) {
		p7(s6[k], i-M*6);
		s6[k] = p6(s5[k], i-M*5);
		s5[k] = p5(s4[k], i-M*4);
		s4[k] = p4(s3[k], i-M*3);
		s3[k] = p3(s2[k], i-M*2);
		s2[k] = p2(s1[k], i-M*1);
		s1[k] = p1(i);
		if (++k >= M) k = 0;
	}
	for (unsigned j = 0; j < M; j++) {
		p7(s6[k], n-M*6+j);
		s6[k] = p6(s5[k], n-M*5+j);
		s5[k] = p5(s4[k], n-M*4+j);
		s4[k] = p4(s3[k], n-M*3+j);
		s3[k] = p3(s2[k], n-M*2+j);
		s2[k] = p2(s1[k], n-M*1+j);
		if (++k >= M) k = 0;
	}
	for (unsigned j = 0; j < M; j++) {
		p7(s6[k], n-M*5+j);
		s6[k] = p6(s5[k], n-M*4+j);
		s5[k] = p5(s4[k], n-M*3+j);
		s4[k] = p4(s3[k], n-M*2+j);
		s3[k] = p3(s2[k], n-M*1+j);
		if (++k >= M) k = 0;
	}
	for (unsigned j = 0; j < M; j++) {
		p7(s6[k], n-M*4+j);
		s6[k] = p6(s5[k], n-M*3+j);
		s5[k] = p5(s4[k], n-M*2+j);
		s4[k] = p4(s3[k], n-M*1+j);
		if (++k >= M) k = 0;
	}
	for (unsigned j = 0; j < M; j++) {
		p7(s6[k], n-M*3+j);
		s6[k] = p6(s5[k], n-M*2+j);
		s5[k] = p5(s4[k], n-M*1+j);
		if (++k >= M) k = 0;
	}
	for (unsigned j = 0; j < M; j++) {
		p7(s6[k], n-M*2+j);
		s6[k] = p6(s5[k], n-M*1+j);
		if (++k >= M) k = 0;
	}
	for (unsigned j = 0; j < M; j++) {
		p7(s6[k], n-M*1+j);
		if (++k >= M) k = 0;
	}
}

#endif //CHD_PIPELINE_H_
