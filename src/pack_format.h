//==============================================================================
// Skew Hash and Displace Algorithm.
// Copyright (C) 2020  Ruan Kunliang
//
// This library is free software; you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License as published by the Free
// Software Foundation; either version 2.1 of the License, or (at your option)
// any later version.
//==============================================================================

#pragma once
#ifndef SHD_PACK_FORMAT_H_
#define SHD_PACK_FORMAT_H_

#include <cstdint>

namespace shd {

static constexpr uint64_t L1H_MAX = 0x7fffffff;
static constexpr uint32_t L1CELL = 5;
static constexpr uint64_t L1TIP = L1H_MAX / L1CELL;

constexpr uint32_t L1Size(uint32_t item) noexcept {
	return ((uint64_t)item + (L1CELL - 1U)) / L1CELL;
}

constexpr uint64_t L1Band(uint32_t item) noexcept {
	const auto l1sz = L1Size(item);
	return (L1H_MAX * (L1H_MAX + L1TIP) + (l1sz - 1U)) / l1sz;
}

constexpr uint64_t L2Size(uint32_t item) noexcept {
	return ((uint64_t)item) * 2U | 1U;
}

struct BitmapSection {
	uint32_t b32[7];
	uint32_t step;
};

static constexpr unsigned BITMAP_SECTION_SIZE = 28U * 8U;

constexpr uint32_t SectionSize(uint32_t item) noexcept {
	return (L2Size(item) + (BITMAP_SECTION_SIZE - 1U)) / BITMAP_SECTION_SIZE;
}

constexpr uint32_t BitmapSize(uint32_t item) noexcept {
	return SectionSize(item) * (BITMAP_SECTION_SIZE / 8U);
}

static constexpr uint32_t SHD_MAGIC = 0x4448537f;

struct Header {
	uint32_t magic = SHD_MAGIC;
	uint8_t type = 0;
	uint8_t key_len = 0;
	uint16_t val_len = 0;
	uint32_t seed = 0;
	uint32_t item = 0;
	uint16_t item_high = 0;
	uint16_t seg_cnt = 0;
	// uint32_t parts[seg_cnt]
	// uint8_t cells[]
	// 32-byte aligned BitmapSection sections[]
	// inline content[]
};

static_assert(sizeof(Header) == 20, "unexpected SHD header layout");
static_assert(sizeof(BitmapSection) == 32, "unexpected SHD bitmap layout");

} // namespace shd

#endif // SHD_PACK_FORMAT_H_
