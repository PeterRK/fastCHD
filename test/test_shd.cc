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

#include <cstring>
#include <memory>
#include <vector>
#include <string>
#include <gtest/gtest.h>
#include <shd.h>
#include "test.h"

static constexpr unsigned PIECE = 1000;

template <typename T, typename Tips>
static shd::DataReaders CreateReaders(unsigned n, Tips tips) {
	shd::DataReaders out;
	out.reserve(n);
	for (unsigned i = 0; i < n; i++) {
		out.push_back(std::make_unique<T>(i*PIECE, PIECE, tips));
	}
	return out;
}

TEST(SHD, Build) {
	FakeWriter fake_output;

	shd::DataReaders fake_input;
	ASSERT_EQ(shd::BuildSet(fake_input, fake_output), shd::BUILD_STATUS_BAD_INPUT);
	ASSERT_EQ(shd::BuildDict(fake_input, fake_output), shd::BUILD_STATUS_BAD_INPUT);
	ASSERT_EQ(shd::BuildDictWithVariedValue(fake_input, fake_output), shd::BUILD_STATUS_BAD_INPUT);

	fake_input.push_back(std::make_unique<EmbeddingGenerator>(0, 0));
	ASSERT_EQ(shd::BuildSet(fake_input, fake_output), shd::BUILD_STATUS_BAD_INPUT);
	ASSERT_EQ(shd::BuildDict(fake_input, fake_output), shd::BUILD_STATUS_BAD_INPUT);
	ASSERT_EQ(shd::BuildDictWithVariedValue(fake_input, fake_output), shd::BUILD_STATUS_BAD_INPUT);

	fake_input.push_back(std::make_unique<EmbeddingGenerator>(0, 1));
	ASSERT_EQ(shd::BuildSet(fake_input, fake_output), shd::BUILD_STATUS_OK);
	ASSERT_EQ(shd::BuildDict(fake_input, fake_output), shd::BUILD_STATUS_OK);
	ASSERT_EQ(shd::BuildDictWithVariedValue(fake_input, fake_output), shd::BUILD_STATUS_OK);

	auto emb_gen = CreateReaders<EmbeddingGenerator>(1, EmbeddingGenerator::MASK0);
	ASSERT_EQ(shd::BuildSet(emb_gen, fake_output), shd::BUILD_STATUS_OK);
	ASSERT_EQ(shd::BuildDict(emb_gen, fake_output), shd::BUILD_STATUS_OK);
	ASSERT_EQ(shd::BuildDictWithVariedValue(emb_gen, fake_output), shd::BUILD_STATUS_OK);

	auto var_gen = CreateReaders<VariedValueGenerator>(1, 5U);
	ASSERT_EQ(shd::BuildDict(var_gen, fake_output), shd::BUILD_STATUS_BAD_INPUT);
	ASSERT_EQ(shd::BuildDictWithVariedValue(var_gen, fake_output), shd::BUILD_STATUS_OK);

	emb_gen = CreateReaders<EmbeddingGenerator>(3, EmbeddingGenerator::MASK0);
	ASSERT_EQ(shd::BuildSet(emb_gen, fake_output), shd::BUILD_STATUS_OK);
	ASSERT_EQ(shd::BuildDict(emb_gen, fake_output), shd::BUILD_STATUS_OK);

	var_gen = CreateReaders<VariedValueGenerator>(3, 5U);
	ASSERT_EQ(shd::BuildDictWithVariedValue(var_gen, fake_output), shd::BUILD_STATUS_OK);
}

TEST(SHD, KeySet) {
	const std::string filename = "keyset.shd";
	{
		shd::FileWriter output(filename.c_str());
		auto input = CreateReaders<EmbeddingGenerator>(2, EmbeddingGenerator::MASK0);
		ASSERT_EQ(shd::BuildSet(input, output), shd::BUILD_STATUS_OK);
	}
	shd::PerfectHashtable dict(filename);
	ASSERT_FALSE(!dict);
	ASSERT_EQ(dict.type(), shd::PerfectHashtable::KEY_SET);
	ASSERT_EQ(dict.key_len(), sizeof(uint64_t));
	ASSERT_EQ(dict.val_len(), 0);
	ASSERT_EQ(dict.item(), PIECE*2);

	union {
		uint64_t v;
		uint8_t p[8];
	} tmp;
	for (unsigned i = 0; i < PIECE*2; i++) {
		tmp.v = i;
		auto val = dict.search(tmp.p);
		ASSERT_NE(val.ptr, nullptr);
		ASSERT_EQ(val.len, 0);
	}
	for (unsigned i = PIECE*2; i < PIECE*3; i++) {
		tmp.v = i;
		auto val = dict.search(tmp.p);
		ASSERT_EQ(val.ptr, nullptr);
		ASSERT_EQ(val.len, 0);
	}

	std::vector<uint64_t> keys(PIECE*2);
	for (unsigned i = 0; i < PIECE; i++) {
		keys[i*2] = i;
		keys[i*2+1] = PIECE*2+i;
	}
	std::vector<const uint8_t*> in(keys.size());
	for (unsigned i = 0; i < keys.size(); i++) {
		in[i] = (const uint8_t*)&keys[i];
	}
	std::vector<const uint8_t*> out(keys.size());

	ASSERT_EQ(dict.batch_search(keys.size(), in.data(), out.data()), PIECE);
	for (unsigned i = 0; i < PIECE; i++) {
		ASSERT_NE(out[i*2], nullptr);
		ASSERT_EQ(out[i*2+1], nullptr);
	}

	ASSERT_EQ(dict.batch_fetch(keys.size(), (const uint8_t*)keys.data(), (uint8_t*)out.data()), 0);
}

TEST(SHD, SmallSet) {
	shd::DataReaders input(1);
	const uint64_t shift = 9999;
	const unsigned limit = 16;
	std::vector<uint64_t> keys(limit);
	for (unsigned i = 0; i < limit; i++) {
		keys[i] = shift + i;
	}
	std::vector<const uint8_t*> in(keys.size());
	for (unsigned i = 0; i < keys.size(); i++) {
		in[i] = (const uint8_t*)&keys[i];
	}
	std::vector<const uint8_t*> out(keys.size());
	const std::string filename = "small.shd";
	for (unsigned i = 1; i < limit; i++) {
		input[0] = std::make_unique<EmbeddingGenerator>(shift, i);
		{
			shd::FileWriter output(filename.c_str());
			ASSERT_EQ(shd::BuildSet(input, output), shd::BUILD_STATUS_OK);
		}
		{
			shd::PerfectHashtable dict(filename);
			ASSERT_FALSE(!dict);
			for (auto& p : out) {
				p = nullptr;
			}
			ASSERT_EQ(dict.batch_search(keys.size(), in.data(), out.data()), i);
			for (unsigned j = 0; j < i; j++) {
				ASSERT_NE(out[j], nullptr);
			}
			for (unsigned j = i; j < limit; j++) {
				ASSERT_EQ(out[j], nullptr);
			}
		}
	}
}

TEST(SHD, InlinedDict) {
	const std::string filename = "dict.shd";
	{
		shd::FileWriter output(filename.c_str());
		auto input = CreateReaders<EmbeddingGenerator>(2, EmbeddingGenerator::MASK0);
		ASSERT_EQ(shd::BuildDict(input, output), shd::BUILD_STATUS_OK);
	}
	shd::PerfectHashtable dict(filename);
	ASSERT_FALSE(!dict);
	ASSERT_EQ(dict.type(), shd::PerfectHashtable::KV_INLINE);
	ASSERT_EQ(dict.key_len(), sizeof(uint64_t));
	ASSERT_EQ(dict.val_len(), EmbeddingGenerator::VALUE_SIZE);
	ASSERT_EQ(dict.item(), PIECE*2);

	EmbeddingGenerator checker(PIECE, PIECE*2);

	std::vector<uint64_t> keys(PIECE*2);

	for (unsigned i = 0; i < PIECE; i++) {
		auto rec = checker.read(false);
		auto val = dict.search(rec.key.ptr);
		ASSERT_NE(val.ptr, nullptr);
		ASSERT_NE(val.ptr, rec.val.ptr);
		ASSERT_EQ(val.len, rec.val.len);
		ASSERT_EQ(memcmp(val.ptr, rec.val.ptr, rec.val.len), 0);
		auto key = *(const uint64_t*)rec.key.ptr;
		keys[i*2] = key;
		keys[i*2+1] = ~key;
	}
	for (unsigned i = 0; i < PIECE; i++) {
		auto rec = checker.read(false);
		auto val = dict.search(rec.key.ptr);
		ASSERT_EQ(val.ptr, nullptr);
		ASSERT_EQ(val.len, 0);
	}

	std::vector<const uint8_t*> in(keys.size());
	for (unsigned i = 0; i < keys.size(); i++) {
		in[i] = (const uint8_t*)&keys[i];
	}
	std::vector<const uint8_t*> out(keys.size());
	auto buf_sz = (PIECE*2)*EmbeddingGenerator::VALUE_SIZE;
	auto buf = std::make_unique<uint8_t[]>(buf_sz);
	memset(buf.get(), 0, buf_sz);
	auto dft_val = std::make_unique<uint8_t[]>(EmbeddingGenerator::VALUE_SIZE);
	memset(dft_val.get(), 0x33, EmbeddingGenerator::VALUE_SIZE);

	ASSERT_EQ(dict.batch_search(keys.size(), in.data(), out.data()), PIECE);
	ASSERT_EQ(dict.batch_fetch(keys.size(), (const uint8_t*)keys.data(), buf.get(), dft_val.get()), PIECE);

	checker.reset();
	auto line = buf.get();
	for (unsigned i = 0; i < PIECE; i++) {
		auto val = checker.read(false).val;
		ASSERT_NE(out[i*2], nullptr);
		ASSERT_EQ(memcmp(out[i*2], val.ptr, val.len), 0);
		ASSERT_EQ(out[i*2+1], nullptr);
		ASSERT_EQ(memcmp(line, val.ptr, val.len), 0);
		ASSERT_EQ(memcmp(line+EmbeddingGenerator::VALUE_SIZE, dft_val.get(), val.len), 0);
		line += EmbeddingGenerator::VALUE_SIZE*2;
	}
}

TEST(SHD, VariedDict) {
	const std::string filename = "var-dict.shd";
	{
		shd::FileWriter output(filename.c_str());
		auto input = CreateReaders<VariedValueGenerator>(2, 5U);
		ASSERT_EQ(shd::BuildDictWithVariedValue(input, output), shd::BUILD_STATUS_OK);
	}
	shd::PerfectHashtable dict(filename);
	ASSERT_FALSE(!dict);
	ASSERT_EQ(dict.type(), shd::PerfectHashtable::KV_SEPARATED);
	ASSERT_EQ(dict.key_len(), sizeof(uint64_t));
	ASSERT_EQ(dict.item(), PIECE*2);

	VariedValueGenerator checker(0, PIECE*3);
	for (unsigned i = 0; i < PIECE*2; i++) {
		auto rec = checker.read(false);
		auto val = dict.search(rec.key.ptr);
		ASSERT_NE(val.ptr, nullptr);
		ASSERT_NE(val.ptr, rec.val.ptr);
		ASSERT_EQ(val.len, rec.val.len);
		ASSERT_EQ(memcmp(val.ptr, rec.val.ptr, rec.val.len), 0);
	}
	for (unsigned i = PIECE*2; i < PIECE*3; i++) {
		auto rec = checker.read(false);
		auto val = dict.search(rec.key.ptr);
		ASSERT_EQ(val.ptr, nullptr);
		ASSERT_EQ(val.len, 0);
	}

	auto junk = std::make_unique<uint8_t[]>(256U);
	ASSERT_EQ(dict.batch_search(1, (const uint8_t**)junk.get(), (const uint8_t**)junk.get()), 0);
	ASSERT_EQ(dict.batch_fetch(1, junk.get(), junk.get()), 0);
}

TEST(SHD, FetchWithPatch) {
	const std::string base_filename = "base.shd";
	const std::string patch_filename = "patch.shd";
	{
		shd::FileWriter base_output(base_filename.c_str());
		auto base_input = CreateReaders<EmbeddingGenerator>(2, EmbeddingGenerator::MASK1);
		ASSERT_EQ(shd::BuildDict(base_input, base_output), shd::BUILD_STATUS_OK);
		shd::FileWriter patch_output(patch_filename.c_str());
		auto patch_input = CreateReaders<EmbeddingGenerator>(1, EmbeddingGenerator::MASK0);
		ASSERT_EQ(shd::BuildDict(patch_input, patch_output), shd::BUILD_STATUS_OK);
	}

	shd::PerfectHashtable base(base_filename);
	ASSERT_FALSE(!base);
	shd::PerfectHashtable patch(patch_filename);
	ASSERT_FALSE(!patch);

	std::vector<uint64_t> keys(PIECE*2);
	for (unsigned i = 0; i < PIECE*2; i++) {
		keys[i] = i;
	}
	std::vector<const uint8_t*> in(keys.size());
	for (unsigned i = 0; i < keys.size(); i++) {
		in[i] = (const uint8_t*)&keys[i];
	}
	std::vector<const uint8_t*> out(keys.size());
	auto buf_sz = (PIECE*2)*EmbeddingGenerator::VALUE_SIZE;
	auto buf = std::make_unique<uint8_t[]>(buf_sz);
	memset(buf.get(), 0, buf_sz);

	ASSERT_EQ(base.batch_search(keys.size(), in.data(), out.data(), &patch), PIECE*2);
	ASSERT_EQ(base.batch_fetch(keys.size(), (const uint8_t*)keys.data(), buf.get(), nullptr, &patch), PIECE*2);

	EmbeddingGenerator checker0(0, PIECE, EmbeddingGenerator::MASK0);
	EmbeddingGenerator checker1(PIECE, PIECE*2, EmbeddingGenerator::MASK1);

	auto line0 = buf.get();
	auto line1 = buf.get() + buf_sz/2;
	for (unsigned i = 0; i < PIECE; i++) {
		ASSERT_NE(out[i], nullptr);
		ASSERT_NE(out[PIECE+i], nullptr);
		auto val0 = checker0.read(false).val;
		auto val1 = checker1.read(false).val;
		ASSERT_EQ(memcmp(val0.ptr, line0, EmbeddingGenerator::VALUE_SIZE), 0);
		ASSERT_EQ(memcmp(val1.ptr, line1, EmbeddingGenerator::VALUE_SIZE), 0);
		ASSERT_NE(memcmp(line0, line1, EmbeddingGenerator::VALUE_SIZE), 0);
		line0 += EmbeddingGenerator::VALUE_SIZE;
		line1 += EmbeddingGenerator::VALUE_SIZE;
	}
}

TEST(SHD, RebuildInlinedDict) {
	std::string filename = "dict-old.shd";
	{
		shd::FileWriter output(filename.c_str());
		auto input = CreateReaders<EmbeddingGenerator>(3, EmbeddingGenerator::MASK1);
		ASSERT_EQ(shd::BuildDict(input, output), shd::BUILD_STATUS_OK);
	}
	{
		shd::PerfectHashtable dict(filename);
		ASSERT_FALSE(!dict);
		filename = "dict-new.shd";
		shd::FileWriter output(filename.c_str());
		auto input = CreateReaders<EmbeddingGenerator>(2, EmbeddingGenerator::MASK0);
		ASSERT_EQ(dict.derive(input, output), shd::BUILD_STATUS_OK);
	}

	shd::PerfectHashtable dict(filename);
	ASSERT_FALSE(!dict);

	std::vector<uint64_t> keys(PIECE*2);
	for (unsigned i = 0; i < PIECE*2; i++) {
		keys[i] = i + PIECE;
	}
	std::vector<const uint8_t*> in(keys.size());
	for (unsigned i = 0; i < keys.size(); i++) {
		in[i] = (const uint8_t*)&keys[i];
	}
	std::vector<const uint8_t*> out(keys.size());
	auto buf_sz = (PIECE*2)*EmbeddingGenerator::VALUE_SIZE;
	auto buf = std::make_unique<uint8_t[]>(buf_sz);
	memset(buf.get(), 0, buf_sz);

	ASSERT_EQ(dict.batch_search(keys.size(), in.data(), out.data()), PIECE*2);
	ASSERT_EQ(dict.batch_fetch(keys.size(), (const uint8_t*)keys.data(), buf.get()), PIECE*2);

	EmbeddingGenerator checker0(PIECE, PIECE*2, EmbeddingGenerator::MASK0);
	EmbeddingGenerator checker1(PIECE*2, PIECE*3, EmbeddingGenerator::MASK1);

	auto line0 = buf.get();
	auto line1 = buf.get() + buf_sz/2;
	for (unsigned i = 0; i < PIECE; i++) {
		ASSERT_NE(out[i], nullptr);
		ASSERT_NE(out[PIECE+i], nullptr);
		auto val0 = checker0.read(false).val;
		auto val1 = checker1.read(false).val;
		ASSERT_EQ(memcmp(val0.ptr, line0, EmbeddingGenerator::VALUE_SIZE), 0);
		ASSERT_EQ(memcmp(val1.ptr, line1, EmbeddingGenerator::VALUE_SIZE), 0);
		ASSERT_NE(memcmp(line0, line1, EmbeddingGenerator::VALUE_SIZE), 0);
		line0 += EmbeddingGenerator::VALUE_SIZE;
		line1 += EmbeddingGenerator::VALUE_SIZE;
	}
}

TEST(SHD, RebuildVariedDict) {
	std::string filename = "var-dict-old.shd";
	{
		shd::FileWriter output(filename.c_str());
		auto input = CreateReaders<VariedValueGenerator>(2, 2U);
		ASSERT_EQ(shd::BuildDictWithVariedValue(input, output), shd::BUILD_STATUS_OK);
	}
	{
		shd::PerfectHashtable dict(filename);
		ASSERT_FALSE(!dict);
		filename = "var-dict-new.shd";
		shd::FileWriter output(filename.c_str());
		auto input = CreateReaders<VariedValueGenerator>(1, 32U);
		ASSERT_EQ(dict.derive(input, output), shd::BUILD_STATUS_OK);
	}

	shd::PerfectHashtable dict(filename);
	ASSERT_FALSE(!dict);

	VariedValueGenerator checker0(0, PIECE, 32U);
	for (unsigned i = 0; i < PIECE; i++) {
		auto rec = checker0.read(false);
		auto val = dict.search(rec.key.ptr);
		ASSERT_NE(val.ptr, nullptr);
		ASSERT_EQ(val.len, rec.val.len);
		ASSERT_EQ(memcmp(val.ptr, rec.val.ptr, rec.val.len), 0);
	}

	VariedValueGenerator checker1(PIECE, PIECE*2, 2U);
	for (unsigned i = 0; i < PIECE; i++) {
		auto rec = checker1.read(false);
		auto val = dict.search(rec.key.ptr);
		ASSERT_NE(val.ptr, nullptr);
		ASSERT_EQ(val.len, rec.val.len);
		ASSERT_EQ(memcmp(val.ptr, rec.val.ptr, rec.val.len), 0);
	}
}