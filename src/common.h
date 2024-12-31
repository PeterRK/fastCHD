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

namespace shd {

struct V128 {
	uint64_t l;
	uint64_t h;
};

extern V128 HashTo128(const uint8_t* msg, uint8_t len, uint64_t seed=0);

} // shd

#define FORCE_INLINE inline __attribute__((always_inline))
#define NOINLINE __attribute__((noinline))

#define LIKELY(exp) __builtin_expect((exp),1)
#define UNLIKELY(exp) __builtin_expect((exp),0)

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "little endian only"
#endif

static FORCE_INLINE void PrefetchForNext(const void* ptr) {
	__builtin_prefetch(ptr, 0, 3);
}
static FORCE_INLINE void PrefetchForFuture(const void* ptr) {
	__builtin_prefetch(ptr, 0, 0);
}
static FORCE_INLINE void PrefetchForWrite(const void* ptr) {
	__builtin_prefetch(ptr, 1, 1);
}

#endif // SHD_COMMON_H_