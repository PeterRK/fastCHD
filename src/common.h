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
#ifndef SHD_COMMON_H_
#define SHD_COMMON_H_

#include <cstdint>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

namespace shd {

struct V128 {
	uint64_t l;
	uint64_t h;
};

extern V128 HashTo128(const uint8_t* msg, uint8_t len, uint64_t seed=0);

} // shd

#if defined(_MSC_VER) && !defined(__clang__)
#define FORCE_INLINE __forceinline
#define NOINLINE __declspec(noinline)
#define LIKELY(exp) (exp)
#define UNLIKELY(exp) (exp)
#else
#define FORCE_INLINE inline __attribute__((always_inline))
#define NOINLINE __attribute__((noinline))
#define LIKELY(exp) __builtin_expect((exp),1)
#define UNLIKELY(exp) __builtin_expect((exp),0)
#endif

static_assert(sizeof(void*) == sizeof(uint64_t), "fastSHD requires a 64-bit platform");

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "little endian only"
#endif

static FORCE_INLINE void PrefetchForNext(const void* ptr) {
#if defined(_MSC_VER) && !defined(__clang__)
	_mm_prefetch(static_cast<const char*>(ptr), _MM_HINT_T0);
#else
	__builtin_prefetch(ptr, 0, 3);
#endif
}
static FORCE_INLINE void PrefetchForFuture(const void* ptr) {
#if defined(_MSC_VER) && !defined(__clang__)
	_mm_prefetch(static_cast<const char*>(ptr), _MM_HINT_NTA);
#else
	__builtin_prefetch(ptr, 0, 0);
#endif
}
static FORCE_INLINE void PrefetchForWrite(const void* ptr) {
#if defined(_MSC_VER) && !defined(__clang__)
#if defined(_M_IX86) || defined(_M_X64)
	_m_prefetchw(ptr);
#else
	__prefetch(ptr);
#endif
#else
	__builtin_prefetch(ptr, 1, 1);
#endif
}

#endif // SHD_COMMON_H_