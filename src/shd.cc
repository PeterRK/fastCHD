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

#include <fcntl.h>
#include <unistd.h>
#include "internal.h"
#include "shd.h"


namespace shd {

std::unique_ptr<uint8_t[]> CreatePackView(const uint8_t* addr, size_t size) {
	size_t addr_off = sizeof(Header);
	if (size < addr_off) return nullptr;

	auto header = (const Header*)addr;
	if (header->magic != SHD_MAGIC) {
		return nullptr;
	}
	switch (header->type) {
		case PerfectHashtable::KV_SEPARATED: if (header->val_len != OFFSET_FIELD_SIZE) return nullptr;
		case PerfectHashtable::KV_INLINE: if (header->val_len == 0) return nullptr;
		case PerfectHashtable::KEY_SET: if (header->key_len == 0) return nullptr;
		case PerfectHashtable::INDEX_ONLY: break;
		default: return nullptr;
	}

	if (header->seg_cnt == 0 || header->seg_cnt > MAX_SEGMENT) {
		return nullptr;
	}
	const auto parts = (const uint32_t*)(addr + addr_off);
	addr_off += header->seg_cnt*4U;
	if (size < addr_off) return nullptr;

	auto view = std::make_unique<uint8_t[]>(sizeof(PackView) + sizeof(SegmentView) * header->seg_cnt);
	auto index = (PackView*)view.get();
	*index = PackView{};
	index->type = (Type)header->type;
	index->key_len = header->key_len;
	index->val_len = header->val_len;
	index->line_size = ((uint32_t)index->key_len) + index->val_len;
	index->seed = header->seed;
	index->l0sz = header->seg_cnt;
	index->item = ((((uint64_t)header->item_high)<<32U) | header->item);

	uint64_t total_item = 0;
	for (unsigned i = 0; i < header->seg_cnt; i++) {
		index->segments[i] = SegmentView{};
		index->segments[i].l1bd = L1Band(parts[i]);
		index->segments[i].l2sz = L2Size(parts[i]);
		index->segments[i].offset = total_item;
		total_item += parts[i];
		index->segments[i].cells = addr + addr_off;
		addr_off += L1Size(parts[i]);
		if (size < addr_off) return nullptr;
	}
	if (total_item != index->item) {
		return nullptr;
	}
	addr_off = (addr_off+31U)&(~31U);
	if (size < addr_off) return nullptr;
	for (unsigned i = 0; i < header->seg_cnt; i++) {
		index->segments[i].sections = (const BitmapSection*)(addr + addr_off);
		addr_off += SectionSize(parts[i]) * (size_t)sizeof(BitmapSection);
		if (size < addr_off) return nullptr;
	}

	if (header->type != PerfectHashtable::INDEX_ONLY) {
		index->content = addr + addr_off;
		addr_off += index->line_size * total_item;
		if (size < addr_off) return nullptr;
		if (header->type == PerfectHashtable::KV_SEPARATED) {
			index->extend = addr + addr_off;
			if (size < addr_off + total_item*2U) return nullptr;
		}
	}
	index->space_end = addr + size;

	return view;
}

PerfectHashtable::PerfectHashtable(const std::string& path, LoadPolicy load_policy) {
	if (load_policy == COPY_DATA) {
		auto mem = MemBlock::LoadFile(path.c_str());
		if (!mem) {
			return;
		}
		auto view = CreatePackView(mem.addr(), mem.size());
		if (view == nullptr) {
			return;
		}
		m_mem = std::move(mem);
		m_view = std::move(view);
	} else {
		MemMap::Policy policy = MemMap::MAP_ONLY;
		if (load_policy == MAP_FETCH) {
			policy = MemMap::FETCH;
		} else if (load_policy == MAP_OCCUPY) {
			policy = MemMap::OCCUPY;
		}
		MemMap res(path.c_str(), policy);
		if (!res) {
			return;
		}
		auto view = CreatePackView(res.addr(), res.size());
		if (view == nullptr) {
			return;
		}
		m_res = std::move(res);
		m_view = std::move(view);
	}
	_post_init();
}

void PerfectHashtable::_post_init() noexcept {
	auto index = (const PackView*)m_view.get();
	m_type = index->type;
	m_key_len = index->key_len;
	if (index->type == KV_SEPARATED) {
		m_val_len = 0;
	} else {
		m_val_len = index->val_len;
	}
	m_item = index->item;
}

PerfectHashtable::PerfectHashtable(size_t size, const std::function<bool(uint8_t*)>& load) {
	auto mem = MemBlock(size);
	if (!mem || !load(mem.addr())) {
		return;
	}
	auto view = CreatePackView(mem.addr(), mem.size());
	if (view == nullptr) {
		return;
	}
	m_mem = std::move(mem);
	m_view = std::move(view);
	_post_init();
}

size_t PerfectHashtable::locate(const uint8_t* key, uint8_t key_len) const noexcept {
	auto index = (const PackView*)m_view.get();
	if (UNLIKELY(index == nullptr || key == nullptr || key_len == 0)) {
		return 0;
	}
	return CalcPos(*index, key, key_len);
}

void PerfectHashtable::batch_locate(unsigned batch, const uint8_t* __restrict__ keys,
									uint8_t key_len, uint64_t* __restrict__ out) {
	auto index = (const PackView*)m_view.get();
	if (UNLIKELY(index == nullptr || keys == nullptr || key_len == 0
		|| (index->type != INDEX_ONLY && key_len != index->key_len))) {
		return;
	}
	return BatchLocate(*index, batch, keys, key_len, out);
}

Slice SeparatedValue(const uint8_t* pt, const uint8_t* end) {
	static_assert(MAX_VALUE_LEN_BIT % 7U == 0, "MAX_VALUE_LEN_BIT should be 7x");

	uint64_t len = 0;
	for (unsigned sft = 0; sft < MAX_VALUE_LEN_BIT; sft += 7U) {
		if (pt >= end) {
			return {};
		}
		uint8_t b = *pt++;
		if (b & 0x80U) {
			len |= static_cast<uint64_t>(b & 0x7fU) << sft;
		} else {
			len |= static_cast<uint64_t>(b) << sft;
			if (pt+len > end) {
				return {};
			}
			return {pt, len};
		}
	}
	return {};
}

Slice PerfectHashtable::search(const uint8_t* key) const noexcept {
	auto pack = (const PackView*)m_view.get();
	if (UNLIKELY(pack == nullptr || key == nullptr || pack->type == INDEX_ONLY)) {
		return {};
	}
	auto pos = CalcPos(*pack, key, pack->key_len);
	auto line = pack->content + pos*pack->line_size;
	if (UNLIKELY(pos >= pack->item) || !Equal(line, key, pack->key_len)) {
		return {};
	}
	auto field = line + pack->key_len;
	if (pack->type != KV_SEPARATED) {
		return {field, pack->val_len};
	}
	return SeparatedValue(pack->extend+ReadOffsetField(field), pack->space_end);
}

unsigned PerfectHashtable::batch_search(unsigned batch, const uint8_t* const keys[], const uint8_t* out[],
										const PerfectHashtable* patch) const noexcept {
	auto base = (const PackView*)m_view.get();
	if (base == nullptr || keys == nullptr || out == nullptr) {
		return 0;
	}
	if (patch == nullptr) {
		return BatchSearch(*base, batch, keys, out);
	} else {
		auto delta = (const PackView*)patch->m_view.get();
		if (delta == nullptr) {
			return 0;
		}
		return BatchSearch(*base, *delta, batch, keys, out);
	}
}

unsigned PerfectHashtable::batch_fetch(unsigned batch, const uint8_t* __restrict__ keys, uint8_t* __restrict__ data,
									   const uint8_t* __restrict__ dft_val, const PerfectHashtable* patch) const noexcept {
	auto base = (const PackView*)m_view.get();
	if (base == nullptr || keys == nullptr || data == nullptr) {
		return 0;
	}
	if (patch == nullptr) {
		return BatchFetch(*base, dft_val, batch, keys, data, nullptr);
	} else {
		auto delta = (const PackView*)patch->m_view.get();
		if (delta == nullptr) {
			return 0;
		}
		return BatchFetch(*base, *delta, dft_val, batch, keys, data, nullptr);
	}
}

unsigned PerfectHashtable::batch_try_fetch(unsigned batch, const uint8_t* __restrict__ keys, uint8_t* __restrict__ data,
									   unsigned* __restrict__ miss, const PerfectHashtable* patch) const noexcept {
	auto base = (const PackView*)m_view.get();
	if (base == nullptr || keys == nullptr || data == nullptr) {
		return 0;
	}
	if (patch == nullptr) {
		return BatchFetch(*base, nullptr, batch, keys, data, miss);
	} else {
		auto delta = (const PackView*)patch->m_view.get();
		if (delta == nullptr) {
			return 0;
		}
		return BatchFetch(*base, *delta, nullptr, batch, keys, data, miss);
	}
}

BuildStatus PerfectHashtable::derive(const DataReaders& in, IDataWriter& out, Retry retry) const {
	auto base = (const PackView*)m_view.get();
	if (base == nullptr || base->type == INDEX_ONLY) {
		return BUILD_STATUS_BAD_INPUT;
	}
	return Rebuild(*base, in, out, retry);
}

} //shd
