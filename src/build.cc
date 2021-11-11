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
#include <cstdlib>
#include <cstring>
#include <tuple>
#include <vector>
#include <chrono>
#include <atomic>
#include <thread>
#include <memory>
#include <exception>
#include <algorithm>
#include <functional>
#include "internal.h"

namespace chd {

bool g_trace_build_time = false;
static double DurationS(const std::chrono::steady_clock::time_point& start, const std::chrono::steady_clock::time_point& end) {
	return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() / 1000.0;
}

struct BuildException : public std::exception {
	const char* what() const noexcept override;
};
const char* BuildException::what() const noexcept {
	return "build exception";
}

struct InternalException : public std::exception {
	const char *what() const noexcept override;
};

const char *InternalException::what() const noexcept {
	return "this should never occur";
}

static FORCE_INLINE void Assert(bool condition) {
	if (UNLIKELY(!condition)) {
		throw InternalException();
	}
}

#define ALLOC_MEM_BLOCK(mem, size) \
	MemBlock mem(size);			\
	if (!mem) {					\
		throw std::bad_alloc();	\
	}

static bool HasConflict(V96 ids[], uint32_t cnt) {
	qsort(ids, cnt, sizeof(V96), [](const void* a, const void* b)->int {
		const uint64_t c = *(const uint64_t*)a;
		const uint64_t d = *(const uint64_t*)b;
		if (c > d) return 1;
		if (c < d) return -1;
		const auto x = (const uint32_t*)a;
		const auto y = (const uint32_t*)b;
		if (x[2] > y[2]) return 1;
		if (x[2] < y[2]) return -1;
		return 0;
	});
	for (uint32_t i = 1; i < cnt; i++) {
		if (ids[i] == ids[i-1]) {
			return true;
		}
	}
	return false;
}

static FORCE_INLINE std::tuple<uint8_t, BuildStatus>
Mapping(V96 ids[], uint32_t cnt, uint8_t sd8, uint8_t bitmap[], const Divisor<uint64_t>& range) {
	auto mini_batch_mapping = [bitmap,range](uint8_t sd8, V96 ids[], unsigned n) {
		assert(n <= MINI_BATCH);
		uint64_t pos[MINI_BATCH];
		for (unsigned i = 0; i < n; i++) {
			pos[i] = L2Hash(ids[i], sd8) % range;
			PrefetchBit(bitmap, pos[i]);
		}
		for (unsigned i = 0; i < n; i++) {
			if (TestBit(bitmap, pos[i])) {
				for (unsigned j = 0; j < i; j++) {
					ClearBit(bitmap, pos[j]);
				}
				return false;
			}
			SetBit(bitmap, pos[i]);
		}
		return true;
	};
	auto try_to_map = [&mini_batch_mapping,ids,cnt,bitmap,range,&sd8](unsigned n)->bool {
		while (n-- != 0) {
			auto tail = ids;
			auto remain = cnt;
			while (remain > MINI_BATCH) {
				if (!mini_batch_mapping(sd8, tail, MINI_BATCH)) {
					for (auto p = ids; p < tail; p++) {
						ClearBit(bitmap, L2Hash(*p, sd8) % range);
					}
					goto retry;
				}
				tail += MINI_BATCH;
				remain -= MINI_BATCH;
			}
			if (mini_batch_mapping(sd8, tail, remain)) {
				return true;
			}
		retry:
			sd8++;
		}
		return false;
	};
	assert(cnt != 0);
	constexpr unsigned FIRST_TRIES = 56;
	constexpr unsigned SECOND_TRIES = 256 - FIRST_TRIES;
	if (try_to_map(FIRST_TRIES)) {
		return {sd8, BUILD_STATUS_OK};
	}
	if (HasConflict(ids, cnt)) {
		return {sd8, BUILD_STATUS_CONFLICT};
	}
	if (try_to_map(SECOND_TRIES)) {
		return {sd8, BUILD_STATUS_OK};
	}
	return {sd8, BUILD_STATUS_OF_CHANCE};
}

struct IndexPiece {
	uint32_t size = 0;
	std::unique_ptr<uint8_t[]> cells;
	std::unique_ptr<BitmapSection[]> sections;
};

static uint64_t GetSeed() {
	//return 1596176575357415943ULL;
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
}

static size_t SumInputSize(const DataReaders& in) {
	size_t total = 0;
	for (auto& reader : in) {
		total += reader->total();
	}
	return total;
}

template <typename Hash, typename Offset, typename Border>
static FORCE_INLINE void Shuffle(V96 ids[], uint32_t parts,
								 const Hash& hash, const Offset& offset, const Border& border, bool prefetch=true) {
	auto prefetch4 = [ids, prefetch](size_t k) {
		if (prefetch && (k & 3UL) == 0) {
			PrefetchForFuture(&ids[k+4]);
		}
	};
	for (uint32_t p = 0; p < parts; p++) {
		while (offset(p) < border(p)) {
			auto i = offset(p);
			auto q = hash(ids[i]);
			if (q == p) {
				offset(p)++;
				continue;
			}
			prefetch4(i);
			auto tmp = ids[i];
			do {
				size_t j;
				uint32_t qx;
				do {
					j = offset(q)++;
					qx = hash(ids[j]);
				} while (qx == q);
				q = qx;
				prefetch4(j);
				std::swap(tmp, ids[j]);
			} while (q != p);
			offset(p)++;
			ids[i] = tmp;
		}
	}
}

template <typename SizeT, typename Offset>
static FORCE_INLINE void Shuffle(V96 ids[], V96 shadow[], SizeT total, const Offset& offset) {
	if (total < MINI_BATCH*2) {
		for (SizeT i = 0; i < total; i++) {
			shadow[offset(ids[i])++] = ids[i];
		}
	} else {
		static_assert((MINI_BATCH&(MINI_BATCH-1)) == 0);
		constexpr size_t batch = MINI_BATCH;
		constexpr size_t mask = MINI_BATCH-1;

		struct {
			SizeT* poff;
			V96* pout;
		} state[batch];

		for (size_t i = 0; i < batch; i++) {
			auto& c = state[i];
			c.poff = &offset(ids[i]);
			PrefetchForNext(c.poff);
		}
		for (size_t i = batch; i < batch*2; i++) {
			auto& c = state[i & mask];
			c.pout = &shadow[(*c.poff)++];
			PrefetchForNext(c.pout);
			c.poff = &offset(ids[i]);
			PrefetchForNext(c.poff);
		}
		for (size_t i = batch*2; i < total; i++) {
			auto& c = state[i & mask];
			*c.pout = ids[i-batch*2];
			c.pout = &shadow[(*c.poff)++];
			PrefetchForNext(c.pout);
			c.poff = &offset(ids[i]);
			PrefetchForNext(c.poff);
		}
		for (size_t i = total; i < total+batch; i++) {
			auto& c = state[i & mask];
			*c.pout = ids[i-batch*2];
			c.pout = &shadow[(*c.poff)++];
			PrefetchForNext(c.pout);
		}
		for (size_t i = total+batch; i < total+batch*2; i++) {
			auto& c = state[i & mask];
			*c.pout = ids[i-batch*2];
		}
	}
}

template <typename SizeT, typename Slot>
static FORCE_INLINE SizeT Counting(V96 ids[], SizeT total, const Slot& slot, bool prefetch=true) {
	SizeT max = 0;
	if (!prefetch || total < MINI_BATCH) {
		for (SizeT i = 0; i < total; i++) {
			auto tmp = ++slot(ids[i]);
			if (tmp > max) max = tmp;
		}
	} else {
		static_assert((MINI_BATCH&(MINI_BATCH-1)) == 0);
		constexpr size_t batch = MINI_BATCH;
		constexpr size_t mask = MINI_BATCH-1;
		typedef SizeT* Pointer;
		Pointer pcnt[batch];
		for (size_t i = 0; i < batch; i++) {
			pcnt[i] = &slot(ids[i]);
			PrefetchForNext(pcnt[i]);
		}
		for (size_t i = batch; i < total; i++) {
			auto j = i & mask;
			auto tmp = ++(*pcnt[j]);
			if (tmp > max) max = tmp;
			pcnt[j] = &slot(ids[i]);
			PrefetchForNext(pcnt[j]);
		}
		for (size_t i = total; i < total+batch; i++) {
			auto j = i & mask;
			auto tmp = ++(*pcnt[j]);
			if (tmp > max) max = tmp;
		}
	}
	return max;
}

struct L1Mark {
	uint32_t val;
	uint32_t idx;
};

static NOINLINE uint32_t L1SortMarking(V96 ids[], uint32_t total, const Divisor<uint32_t>& l1sz, L1Mark table[]) {
	for (uint32_t i = 0; i < l1sz.value(); i++) {
		table[i] = {0, i};
	}
	return Counting(ids, total,
					[l1sz, table](const V96& id)->uint32_t& {
						return table[L1Hash(id) % l1sz].val;
					});
}

static NOINLINE L1Mark* L1SortReorder(uint32_t max, unsigned n, L1Mark table[], L1Mark temp[]) {
	Assert(n > 0);
	uint32_t memo[256];
	for (unsigned sft = 0; sft < 32U && (1U<<sft) <= max; sft += 8) {
		memset(memo, 0, sizeof(memo));
		for (uint32_t i = 0; i < n; i++) {
			memo[(table[i].val >> sft) & 0xffU]++;
		}
		uint32_t off = 0;
		for (auto& m : memo) {
			auto next = off + m;
			m = off;
			off = next;
		}
		for (uint32_t i = 0; i < n; i++) {
			auto j = memo[(table[i].val >> sft) & 0xffU]++;
			temp[j] = table[i];
		}
		std::swap(table, temp);
	}

	uint32_t off = 0;
	for (int64_t i = n - 1; i >= 0; i--) {
		auto cnt = table[i].val;
		auto& rg = temp[table[i].idx];
		rg.idx = off;
		off += cnt;
		rg.val = off;
	}
	return temp;
}

static NOINLINE void L1SortShuffle(V96 ids[], const Divisor<uint32_t>& l1sz, L1Mark range[]) {
	Shuffle(ids, l1sz.value(),
			[l1sz](const V96& id)->uint32_t {
				return L1Hash(id) % l1sz;
			},
			[range](uint32_t i)->uint32_t& {
				return range[i].idx;
			},
			[range](uint32_t i)->uint32_t {
				return range[i].val;
			},
			false);
}

static NOINLINE void L1SortShuffle(V96 ids[], V96 shadow[], uint32_t total,
								   const Divisor<uint32_t>& l1sz, L1Mark range[]) {
	Shuffle(ids, shadow, total,
			[l1sz, range](const V96& id)->uint32_t& {
				return range[L1Hash(id) % l1sz].idx;
			});
}

static V96* L1Sort(V96 ids[], V96 shadow[], uint32_t total, const Divisor<uint32_t>& l1sz) {
	ALLOC_MEM_BLOCK(mem, ((size_t)l1sz.value()) * sizeof(L1Mark) * 2)
	auto max = L1SortMarking(ids, total, l1sz, (L1Mark*)mem.addr());
	if (max > std::min(l1sz.value()+16U, (uint32_t)UINT16_MAX)) {
		return nullptr;
	}
	auto range = L1SortReorder(max, l1sz.value(), (L1Mark*)mem.addr(), (L1Mark*)mem.addr()+l1sz.value());
	if (shadow == nullptr) {
		L1SortShuffle(ids, l1sz, range);
		return ids;
	} else {
		L1SortShuffle(ids, shadow, total, l1sz, range);
		return shadow;
	}
}

static NOINLINE BuildStatus Build(V96 ids[], V96 shadow[], IndexPiece& out) {
	const Divisor<uint32_t> l1sz(L1Size(out.size));
	const Divisor<uint64_t> l2sz(L2Size(out.size));

	ids = L1Sort(ids, shadow, out.size, l1sz);
	if (ids == nullptr) {
		return BUILD_STATUS_CONFLICT;
	};

	const auto bitmap_size = BitmapSize(out.size);
	auto bitmap = std::make_unique<uint8_t[]>(bitmap_size);
	memset(bitmap.get(), 0, bitmap_size);
	auto cells = std::make_unique<uint8_t[]>(l1sz.value());

	uint8_t magic = 0;

	auto last = L1Hash(ids[0]) % l1sz;
	uint32_t begin = 0;
	for (uint32_t i = 1; i < out.size; i++) {
		auto curr = L1Hash(ids[i]) % l1sz;
		if (curr != last) {
			auto [sd8, status] = Mapping(ids+begin, i-begin, magic--, bitmap.get(), l2sz);
			if (status != BUILD_STATUS_OK) {
				return status;
			}
			cells[last] = sd8;
			last = curr;
			begin = i;
		}
	}
	auto [sd8, status] = Mapping(ids+begin, out.size-begin, magic, bitmap.get(), l2sz);
	if (status != BUILD_STATUS_OK) {
		return status;
	}
	cells[last] = sd8;

	out.cells = std::move(cells);
	const auto sec_sz = SectionSize(out.size);
	out.sections = std::make_unique<BitmapSection[]>(sec_sz);
	auto b32 = (const uint32_t*)bitmap.get();
	uint32_t step = 0;
	for (uint32_t i = 0; i < sec_sz; i++) {
		auto& sec = out.sections[i];
		sec.step = step;
		step += PopCount32(b32[0]);
		auto b64 = (const uint64_t*)(b32 + 1);
		step += PopCount64(b64[0]) + PopCount64(b64[1]) + PopCount64(b64[2]);
		sec.b32[0] = b32[0];
		sec.b32[1] = b32[1];
		sec.b32[2] = b32[2];
		sec.b32[3] = b32[3];
		sec.b32[4] = b32[4];
		sec.b32[5] = b32[5];
		sec.b32[6] = b32[6];
		b32 += 7;
	}
	Assert(step == out.size);
	return BUILD_STATUS_OK;
}

static BuildStatus Build(V96 ids[], V96 shadow[], std::vector<IndexPiece>& out) {
	std::vector<std::thread> threads;
	threads.reserve(out.size());
	std::vector<BuildStatus> part_status(out.size());

	size_t off = 0;
	for (unsigned i = 0; i < out.size(); i++) {
		threads.emplace_back([](V96 ids[], V96 shadow[], IndexPiece* piece, BuildStatus* status) {
			*status = Build(ids, shadow, *piece);
		}, ids+off, shadow!=nullptr? shadow+off : nullptr, &out[i], &part_status[i]);
		off += out[i].size;
	}
	for (auto& t : threads) {
		t.join();
	}
	BuildStatus status = BUILD_STATUS_OK;
	for (auto part : part_status) {
		if (part == BUILD_STATUS_CONFLICT) {
			status = BUILD_STATUS_CONFLICT;
		} else if (part == BUILD_STATUS_OF_CHANCE && status != BUILD_STATUS_CONFLICT) {
			status = BUILD_STATUS_OF_CHANCE;
		}
	}
	return status;
}

static BuildStatus Build(V96 ids[], V96 shadow[], std::vector<size_t>& shuffle, std::vector<IndexPiece>& out) {
	const uint32_t n = shuffle.size();
	Assert(n > 1 && n <= MAX_SEGMENT);
	const Divisor<uint16_t> l0sz(n);
	out.clear();
	out.resize(n);
	for (unsigned i = 0; i < n; i++) {
		if (shuffle[i] == 0 || shuffle[i] > UINT32_MAX) {
			return BUILD_STATUS_BAD_INPUT;
		}
		out[i].size = shuffle[i];
	}

	auto spot1 = std::chrono::steady_clock::now();
	if (shadow == nullptr) {
		size_t off = 0;
		auto border = std::make_unique<size_t[]>(n);
		for (unsigned i = 0; i < n; i++) {
			shuffle[i] = off;
			PrefetchForFuture(&ids[off]);
			off += out[i].size;
			border[i] = off;
		}
		Shuffle(ids, n,
				[l0sz](const V96& id)->uint16_t {
					return L0Hash(id) % l0sz;
				},
				[&shuffle](uint16_t i)->size_t& {
					return shuffle[i];
				},
				[&border](uint16_t i)->size_t {
					return border[i];
				});
	} else {
		size_t total = 0;
		size_t min = std::numeric_limits<size_t>::max();
		for (unsigned i = 0; i < n; i++) {
			shuffle[i] = total;
			auto sz = out[i].size;
			total += sz;
			if (sz < min) {
				min = sz;
			}
		}
#ifdef NDEBUG
		auto heads = min >> 20U;
#else
		auto heads = min >> 5U;
#endif
		if (heads <= 1) {
			Shuffle(ids, shadow, total,
					[l0sz, &shuffle](const V96& id)->size_t& {
						return shuffle[L0Hash(id) % l0sz];
					});
		} else {	//multi-head shuffle
			if (heads > n) {
				heads = n;
			}
			struct Range {
				size_t off;
				size_t end;
			};
			std::vector<std::unique_ptr<Range[]>> ctx(heads);
			for (unsigned i = 0; i < heads; i++) {
				ctx[i] = std::make_unique<Range[]>(n);
			}
			for (unsigned j = 0; j < n; j++) {
				const auto piece = out[j].size / heads;
				const auto remain = out[j].size % heads;
				size_t off = shuffle[j];
				for (unsigned i = 0; i < heads; i++) {
					const auto part = i<remain ? piece+1 : piece;
					ctx[i][j] = {off, off+part};
					off += part;
				}
			}
			std::vector<std::thread> threads;
			threads.reserve(heads);
			const auto piece = total / heads;
			const auto remain = total % heads;
			size_t off = 0;
			for (unsigned i = 0; i < heads; i++) {
				const auto part = i<remain ? piece+1 : piece;
				threads.emplace_back([&ctx, shadow, l0sz](uint16_t self, V96 ids[], size_t cnt){
					auto idx = std::make_unique<uint16_t[]>(l0sz.value());
					for (unsigned j = 0; j < l0sz.value(); j++) {
						idx[j] = self;
					}
					for (size_t i = 0; i < cnt; i++) {
						auto p = L0Hash(ids[i]) % l0sz;
						auto& k = idx[p];
						for (unsigned j = 0; j < ctx.size(); j++) {
							auto& range = ctx[k][p];
							auto off = AddRelaxed(range.off, 1UL);
							if (LIKELY(off < range.end)) {
								shadow[off] = ids[i];
								break;
							}
							k = (k+1) % ctx.size();
						}
					}
				}, i, ids+off, part);
				off += part;
			}
			for (auto& t : threads) {
				t.join();
			}
		}
		std::swap(ids, shadow);
	}
	auto spot2 = std::chrono::steady_clock::now();
	auto status = Build(ids, shadow, out);
	auto spot3 = std::chrono::steady_clock::now();
	if (g_trace_build_time) {
		Logger::Printf("partition: %.3fs\n", DurationS(spot1, spot2));
		Logger::Printf("build: %.3fs\n", DurationS(spot2, spot3));
	}
	return status;
}

static BuildStatus Build(bool use_extra_mem, uint32_t seed, const DataReaders& in, std::vector<IndexPiece>& out) {
	const auto total = SumInputSize(in);
	Assert(!in.empty() && total > 0);

	ALLOC_MEM_BLOCK(mem, total*sizeof(V96)*(use_extra_mem?2U:1U))
	auto ids = (V96*)mem.addr();
	auto shadow = use_extra_mem? ids + total : nullptr;

	const uint32_t n = in.size();
#ifdef NDEBUG
	if (n == 1 || total < 8192U * n) {
#else
	if (n == 1 || total < 32U * n) {
#endif
		if (total > UINT32_MAX) {
			return BUILD_STATUS_BAD_INPUT;
		}
		auto spot1 = std::chrono::steady_clock::now();
		auto p = ids;
		for (auto& reader : in) {
			reader->reset();
			auto cnt = reader->total();
			for (size_t i = 0; i < cnt; i++) {
				auto key = reader->read(true).key;
				if (key.ptr == nullptr || key.len == 0 || key.len > MAX_KEY_LEN) {
					return BUILD_STATUS_BAD_INPUT;
				}
				*p++ = GenID(seed, key.ptr, key.len);
			}
		}
		out.resize(1);
		out.front().size = total;
		auto spot2 = std::chrono::steady_clock::now();
		auto status = Build(ids, shadow, out.front());
		auto spot3 = std::chrono::steady_clock::now();
		if (g_trace_build_time) {
			Logger::Printf("gen-id: %.3fs\n", DurationS(spot1, spot2));
			Logger::Printf("build: %.3fs\n", DurationS(spot2, spot3));
		}
		return status;
	}

	auto spot4 = std::chrono::steady_clock::now();
	const Divisor<uint16_t> l0sz(n);

	std::vector<std::thread> threads;
	threads.reserve(n);
	std::vector<size_t> shuffle(n, 0);

	bool fail = false;
	size_t off = 0;
	for (auto& reader : in) {
		reader->reset();
		threads.emplace_back([seed, &fail, &shuffle, l0sz](IDataReader* reader, V96 ids[]) {
			auto cnt = reader->total();
			const uint32_t n = shuffle.size();
			std::vector<size_t> temp(n, 0);
			for (size_t j = 0; j < cnt; j++) {
				auto key = reader->read(true).key;
				if (key.ptr == nullptr || key.len == 0 || key.len > MAX_KEY_LEN) {
					fail = true;
					return;
				}
				ids[j] = GenID(seed, key.ptr, key.len);
				temp[L0Hash(ids[j])%l0sz]++;
			}
			for (unsigned j = 0; j < n; j++) {
				AddRelaxed(shuffle[j], temp[j]);
			}
		}, reader.get(), ids+off);
		off += reader->total();
	}
	for (auto& t : threads) {
		t.join();
	}
	if (fail) {
		return BUILD_STATUS_BAD_INPUT;
	}
	auto spot5 = std::chrono::steady_clock::now();
	if (g_trace_build_time) {
		Logger::Printf("gen-id: %.3fs\n", DurationS(spot4, spot5));
	}
	return Build(ids, shadow, shuffle, out);
}

static bool DumpIndex(IDataWriter& out, const Header& header, const std::vector<IndexPiece>& pieces) {
	std::vector<uint32_t> items(pieces.size());
	for (unsigned i = 0; i < pieces.size(); i++) {
		items[i] = pieces[i].size;
	}
	if (items.empty()
		|| !out.write(&header, sizeof(header))
		|| !out.write(items.data(), items.size()*4U)
	) return false;

	auto size = sizeof(Header) + items.size()*4U;
	for (auto& res : pieces) {
		auto sz = L1Size(res.size);
		if (!out.write(res.cells.get(), sz)) {
			return false;
		}
		size += sz;
	}
	const uint64_t zeros[4] = {0,0,0,0};
	const auto unaligned = size;
	size = (size+31U)&(~31U);
	if (size > unaligned && !out.write(zeros, size-unaligned)) {
		return false;
	}
	for (auto& res : pieces) {
		auto sz = SectionSize(res.size) * (size_t)sizeof(BitmapSection);
		if (!out.write(res.sections.get(), sz)) {
			return false;
		}
		size += sz;
	}
	return true;
}

struct BasicInfo {
	Type type;
	uint8_t key_len;
	uint16_t val_len;
};

std::unique_ptr<uint8_t[]> CreateIndexView(const BasicInfo& info, uint32_t seed, const std::vector<IndexPiece>& pieces) {
	Assert(!pieces.empty());
	auto view = std::make_unique<uint8_t[]>(sizeof(PackView) + sizeof(SegmentView) * pieces.size());
	auto index = (PackView*)view.get();
	*index = PackView{};
	index->key_len = info.key_len;
	index->val_len = info.val_len;
	index->line_size = info.key_len + (uint32_t)info.val_len;
	index->seed = seed;
	index->l0sz = pieces.size();
	uint64_t off = 0;
	for (unsigned i = 0; i < pieces.size(); i++) {
		index->segments[i] = SegmentView{};
		index->segments[i].l1sz = L1Size(pieces[i].size);
		index->segments[i].l2sz = L2Size(pieces[i].size);
		index->segments[i].sections = pieces[i].sections.get();
		index->segments[i].cells = pieces[i].cells.get();
		index->segments[i].offset = off;
		off += pieces[i].size;
	}
	return view;
}

static BuildStatus BuildAndDump(const DataReaders& in, IDataWriter& out, const BasicInfo& info, Retry retry,
								const std::function<BuildStatus(const PackView&, const DataReaders&, IDataWriter&)>& fill) {
	const size_t total = SumInputSize(in);
	if (in.empty() || in.size() > MAX_SEGMENT || total == 0) {
		return BUILD_STATUS_BAD_INPUT;
	}
	Header header;
	header.type = info.type;
	header.key_len = info.key_len;
	header.val_len = info.val_len;
	header.item = total;
	header.item_high = total >> 32U;

	const bool use_extra_mem = info.key_len + (uint32_t)info.val_len > sizeof(V96)*2+4;

	std::vector<IndexPiece> pieces;
	for (bool done = false; !done; ) {
		header.seed = GetSeed();
		const auto status = Build(use_extra_mem, header.seed, in, pieces);
		switch (status) {
			case BUILD_STATUS_OK:
				done = true;
				break;
			case BUILD_STATUS_CONFLICT:
				if (retry.conflict-- == 0) {
					return status;
				}
			case BUILD_STATUS_OF_CHANCE:
				if (retry.total-- == 0) {
					return status;
				}
				Logger::Printf(status==BUILD_STATUS_CONFLICT? "conflict, retry\n" : "failed, retry\n");
				break;
			default:
				return status;
		}
	}
	header.seg_cnt = pieces.size();
	if (!DumpIndex(out, header, pieces)) {
		return BUILD_STATUS_FAIL_TO_OUTPUT;
	}
	if (fill != nullptr) {
		auto index = CreateIndexView(info, header.seed, pieces);
		assert(index != nullptr);
		return fill(*(PackView*)index.get(), in, out);
	}
	return BUILD_STATUS_OK;
}

static FORCE_INLINE uint8_t* FindLine(uint8_t* space, const PackView& index, const uint8_t* key) {
	const auto pos = CalcPos(index, key, index.key_len);
	return space + pos*index.line_size;
}

static bool FillKeyValue(const PackView& index, IDataReader& reader, uint8_t* space) {
	Assert(index.key_len != 0);
	const auto total = reader.total();
	auto fill_line = [&index](const Record& rec, uint8_t* line)->bool {
		Assert(rec.key.len == index.key_len);
		Assign(line, rec.key.ptr, index.key_len);
		if (index.val_len != 0) {
			if (rec.val.ptr == nullptr || rec.val.len != index.val_len) {
				return false;
			}
			memcpy(line+index.key_len, rec.val.ptr, index.val_len);
		}
		return true;
	};
	reader.reset();
	if (index.line_size <= DOUBLE_COPY_LINE_SIZE_LIMIT) {
		try {
			BatchDataMapping(index, space, total,
					[&reader, &fill_line, &index](uint8_t* buf)->const uint8_t*{
						auto rec = reader.read(index.val_len==0);
						if (rec.key.len != index.key_len || !fill_line(rec, buf)) {
							throw BuildException();
						}
						return buf;
					});
		} catch (const BuildException&) {
			return false;
		}
	} else {
		for (size_t i = 0; i < total; i++) {
			auto rec = reader.read(index.val_len==0);
			if (rec.key.len != index.key_len
				|| !fill_line(rec, FindLine(space, index, rec.key.ptr))) {
				return false;
			}
		}
	}
	return true;
}

static BuildStatus FillInlineKeyValue(const PackView& index, const DataReaders& in, IDataWriter& out) {
	const auto total = SumInputSize(in);
	Assert(!in.empty() && total > 0);
	ALLOC_MEM_BLOCK(space, total*index.line_size);

	auto spot1 = std::chrono::steady_clock::now();
	if (in.size() == 1 || total < 4096U * in.size()) {
		for (auto& reader : in) {
			if (!FillKeyValue(index, *reader, space.addr())) {
				return BUILD_STATUS_BAD_INPUT;
			}
		}
	} else {
		std::vector<std::thread> threads;
		threads.reserve(in.size());
		bool fail = false;
		for (auto& reader : in) {
			threads.emplace_back([&fail, &space, &index](IDataReader* reader) {
				if (!FillKeyValue(index, *reader, space.addr())) {
					fail = true;
				}
			}, reader.get());
		}
		for (auto& t : threads) {
			t.join();
		}
		if (fail) {
			return BUILD_STATUS_BAD_INPUT;
		}
	}
	auto spot2 = std::chrono::steady_clock::now();
	if (!out.write(space.addr(), space.size())) {
		return BUILD_STATUS_FAIL_TO_OUTPUT;
	}
	auto spot3 = std::chrono::steady_clock::now();
	if (g_trace_build_time) {
		Logger::Printf("fill: %.3fs\n", DurationS(spot1, spot2));
		Logger::Printf("dump: %.3fs\n", DurationS(spot2, spot3));
	}
	return BUILD_STATUS_OK;
}

static unsigned VarIntSize(size_t n) {
	unsigned cnt = 1;
	while ((n & ~0x7fULL) != 0) {
		n >>= 7U;
		cnt++;
	}
	return cnt;
}
static bool WriteVarInt(size_t n, IDataWriter& out) {
	uint8_t buf[10];
	unsigned w = 0;
	while ((n & ~0x7fULL) != 0) {
		buf[w++] = 0x80ULL | (n & 0x7fULL);
		n >>= 7U;
	}
	buf[w++] = n;
	return out.write(buf, w);
}

static BuildStatus FillSeparatedKeyValue(const PackView& index, const DataReaders& in, IDataWriter& out) {
	const auto total = SumInputSize(in);
	Assert(total> 0 && index.key_len != 0 && index.line_size == index.key_len + OFFSET_FIELD_SIZE);
	ALLOC_MEM_BLOCK(space, total*index.line_size)

	const auto key_len = index.key_len;
	size_t offset = 0;
	auto fill_line = [key_len, &offset](const Record& rec, uint8_t* line)->bool {
		Assign(line, rec.key.ptr, key_len);
		if (offset > MAX_OFFSET) {
			return false;
		}
		WriteOffsetField(line+key_len, offset);
		if (rec.val.len > MAX_VALUE_LEN || (rec.val.len != 0 && rec.val.ptr == nullptr)) {
			return false;
		}
		offset += VarIntSize(rec.val.len) + rec.val.len;
		return true;
	};

	auto spot1 = std::chrono::steady_clock::now();
	for (auto& reader : in) {
		reader->reset();
		auto cnt = reader->total();
		if (index.line_size <= DOUBLE_COPY_LINE_SIZE_LIMIT) {
			try {
				BatchDataMapping(index, space.addr(), cnt,
								 [&reader, &fill_line, key_len](uint8_t* buf)->const uint8_t*{
									 auto rec = reader->read(false);
									 if (rec.key.len != key_len || !fill_line(rec, buf)) {
										 throw BuildException();
									 }
									 return buf;
								 });
			} catch (const BuildException&) {
				return offset > MAX_OFFSET? BUILD_STATUS_FAIL_TO_OUTPUT : BUILD_STATUS_BAD_INPUT;
			}
		} else {
			for (size_t i = 0; i < cnt; i++) {
				auto rec = reader->read(false);
				if (rec.key.len != key_len
					|| !fill_line(rec, FindLine(space.addr(), index, rec.key.ptr))) {
					return offset > MAX_OFFSET? BUILD_STATUS_FAIL_TO_OUTPUT : BUILD_STATUS_BAD_INPUT;
				}
			}
		}
	}
	auto spot2 = std::chrono::steady_clock::now();
	if (!out.write(space.addr(), space.size())) {
		return BUILD_STATUS_FAIL_TO_OUTPUT;
	}
	space = MemBlock{};
	auto spot3 = std::chrono::steady_clock::now();

	for (auto& reader : in) {
		reader->reset();
		auto cnt = reader->total();
		for (size_t i = 0; i < cnt; i++) {
			auto val = reader->read(false).val;
			if (!WriteVarInt(val.len, out) ||
				(val.len != 0 && !out.write(val.ptr, val.len))) {
				return BUILD_STATUS_FAIL_TO_OUTPUT;
			}
		}
	}
	auto spot4 = std::chrono::steady_clock::now();
	if (g_trace_build_time) {
		Logger::Printf("fill index: %.3fs\n", DurationS(spot1, spot2));
		Logger::Printf("dump index: %.3fs\n", DurationS(spot2, spot3));
		Logger::Printf("dump value: %.3fs\n", DurationS(spot3, spot4));
	}
	return BUILD_STATUS_OK;
}

BuildStatus BuildIndex(const DataReaders& in, IDataWriter& out, Retry retry) {
	return BuildAndDump(in, out, {Type::INDEX_ONLY, 0, 0}, retry, nullptr);
}

static bool DetectKeyValueLen(const DataReaders& in, uint8_t& key_len, uint16_t* val_len) {
	for (auto& reader : in) {
		if (reader->total() == 0) {
			continue;
		}
		auto rec = reader->read(val_len == nullptr);
		if (rec.key.ptr == nullptr || rec.key.len == 0 || rec.key.len > MAX_KEY_LEN) {
			return false;
		}
		key_len =  rec.key.len;
		if (val_len != nullptr) {
			if (rec.val.ptr == nullptr || rec.val.len == 0 || rec.val.len > MAX_INLINE_VALUE_LEN) {
				return false;
			}
			*val_len = rec.val.len;
		}
		reader->reset();
		return true;
	}
	return false;
}

BuildStatus BuildSet(const DataReaders& in, IDataWriter& out, Retry retry) {
	uint8_t key_len;
	if (!DetectKeyValueLen(in, key_len, nullptr)) {
		return BUILD_STATUS_BAD_INPUT;
	}
	return BuildAndDump(in, out, {Type::KEY_SET, key_len, 0}, retry,
						[](const PackView& index, const DataReaders& in, IDataWriter& out)->BuildStatus {
							return FillInlineKeyValue(index, in, out);
						});
}

BuildStatus BuildDict(const DataReaders& in, IDataWriter& out, Retry retry) {
	uint8_t key_len;
	uint16_t val_len;
	if (!DetectKeyValueLen(in, key_len, &val_len)) {
		return BUILD_STATUS_BAD_INPUT;
	}
	return BuildAndDump(in, out, {Type::KV_INLINE, key_len, val_len}, retry,
					 [](const PackView& index, const DataReaders& in, IDataWriter& out)->BuildStatus {
								return FillInlineKeyValue(index, in, out);
							});
}

BuildStatus BuildDictWithVariedValue(const DataReaders& in, IDataWriter& out, Retry retry) {
	uint8_t key_len;
	if (!DetectKeyValueLen(in, key_len, nullptr)) {
		return BUILD_STATUS_BAD_INPUT;
	}
	return BuildAndDump(in, out, {Type::KV_SEPARATED, key_len, OFFSET_FIELD_SIZE}, retry,
						[](const PackView& index, const DataReaders& in, IDataWriter& out)->BuildStatus {
							return FillSeparatedKeyValue(index, in, out);
						});
}


struct Shard {
	size_t begin;
	size_t end;
	size_t valid;
};

class RebuildReader : public IDataReader {
public:
	explicit RebuildReader(const std::shared_ptr<MemBlock>& dirty,
						const Shard& shard, const PackView& base, IDataReader& patch)
		: m_dirty(dirty), m_shard(shard),m_base(base), m_patch(patch),  m_pos(shard.begin) {
		assert(base.type != Type::INDEX_ONLY && dirty != nullptr);
		m_patch.reset();
	}

	void reset() override {
		m_pos = m_shard.begin;
		m_patch.reset();
	}
	size_t total() override {
		return m_shard.valid + m_patch.total();
	}
	Record read(bool key_only) override {
		while (m_pos < m_shard.end) {
			if (TestBit(m_dirty->addr(), m_pos)) {
				m_pos++;
				continue;
			}
			auto line = m_base.content + (m_pos++)*m_base.line_size;
			Record out;
			out.key = {line, m_base.key_len};
			if (!key_only) {
				auto field = line + m_base.key_len;
				if (m_base.type != Type::KV_SEPARATED) {
					out.val = {field, m_base.val_len};
				} else {
					out.val = SeparatedValue(m_base.extend+ReadOffsetField(field), m_base.space_end);
				}
			}
			return out;
		}
		return m_patch.read(key_only);
	}

private:
	const std::shared_ptr<MemBlock> m_dirty;
	const Shard m_shard;
	const PackView& m_base;
	IDataReader& m_patch;
	size_t m_pos = 0;
};

static DataReaders PrepareForRebuild(const PackView& base, const DataReaders& in) {
	DataReaders out;
	if (base.type == Type::INDEX_ONLY || in.empty() || in.size() > MAX_SEGMENT || base.item < in.size()) {
		return out;
	}
	auto dirty = std::make_shared<MemBlock>((base.item+7U)/8U);
	if (!*dirty) throw std::bad_alloc();
	memset(dirty->addr(), 0, dirty->size());

	if (in.size() == 1) {
		Shard shard;
		shard.begin = 0;
		shard.end = base.item;
		shard.valid = base.item;
		auto reader = in.front().get();
		reader->reset();
		try {
			BatchFindPos(base, reader->total(),
						 [reader, &base](uint8_t *buf) -> const uint8_t * {
							 auto key = reader->read(true).key;
							 if (key.ptr == nullptr || key.len != base.key_len) {
								 throw BuildException();
							 }
							 Assign(buf, key.ptr, base.key_len);
							 return buf;
						 },
						 [&shard, &base, &dirty](uint64_t pos) {
							 if (pos < base.item) {
								 if (!TestAndSetBit(dirty->addr(), pos)) {
									 throw BuildException();
								 }
								 shard.valid--;
							 }
						 }, dirty->addr());
		} catch (const BuildException&) {
			return {};
		}
		out.emplace_back(new RebuildReader(dirty, shard, base, *reader));
		return out;
	}

	std::vector<Shard> shards(in.size());
	const auto piece = base.item / in.size();
	const auto remain = base.item % in.size();
	size_t off = 0;
	for (unsigned i = 0; i < shards.size(); i++) {
		shards[i].begin = off;
		shards[i].valid = i<remain ? piece+1 : piece;
		off += shards[i].valid;
		shards[i].end = off;
	}

	std::vector<std::thread> threads;
	threads.reserve(in.size());
	bool fail = false;
	for (auto& reader : in) {
		reader->reset();
		threads.emplace_back([&base, &shards, &fail, dirty](IDataReader* reader) {
			std::vector<size_t> temp(shards.size(), 0);
			try {
				BatchFindPos(base, reader->total(),
							 [reader, &base](uint8_t *buf) -> const uint8_t * {
								 auto key = reader->read(true).key;
								 if (key.ptr == nullptr || key.len != base.key_len) {
									 throw BuildException();
								 }
								 Assign(buf, key.ptr, base.key_len);
								 return buf;
							 },
							 [&shards, &temp, &base, &dirty](uint64_t pos) {
								 if (pos < base.item) {
									 unsigned a = 0;
									 unsigned b = shards.size();
									 while (a < b) {
										 auto m = (a + b) / 2;
										 if (pos < shards[m].end) {
											 b = m;
										 } else {
											 a = m + 1;
										 }
									 }
									 temp[a]++;
									 if (!AtomicTestAndSetBit(dirty->addr(), pos)) {
										 throw BuildException();
									 }
								 }
							 }, dirty->addr());
			} catch (const BuildException&) {
				fail = true;
				return;
			}
			for (unsigned j = 0; j < shards.size(); j++) {
				SubRelaxed(shards[j].valid, temp[j]);
			}
		}, reader.get());
	}
	for (auto& t : threads) {
		t.join();
	}
	if (fail) {
		return {};
	}

	out.reserve(in.size());
	for (unsigned i = 0; i < in.size(); i++) {
		out.emplace_back(new RebuildReader(dirty, shards[i], base, *in[i]));
	}
	return out;
}

BuildStatus Rebuild(const PackView& base, const DataReaders& in, IDataWriter& out, Retry retry) {
	auto spot1 = std::chrono::steady_clock::now();
	auto input = PrepareForRebuild(base, in);
	if (input.empty()) {
		return BUILD_STATUS_BAD_INPUT;
	}
	auto spot2 = std::chrono::steady_clock::now();
	if (g_trace_build_time) {
		Logger::Printf("prepare: %lds\n", DurationS(spot1, spot2));
	}
	switch (base.type) {
		case Type::KEY_SET:
			return BuildSet(input, out, retry);
		case Type::KV_INLINE:
			return BuildDict(input, out, retry);
		case Type::KV_SEPARATED:
			return BuildDictWithVariedValue(input, out, retry);
		default:
			return BUILD_STATUS_BAD_INPUT;
	}
}

} //chd
