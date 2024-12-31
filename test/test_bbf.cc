//==============================================================================
// Block Bloom Filter with 3.5% false positive rate
// Copyright (C) 2025  Ruan Kunliang
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

#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <bbf.h>

TEST(BBF, SetAndTest) {
	bbf::BloomFilter bf(999);
	ASSERT_FALSE(!bf);
	ASSERT_EQ(1000, bf.capacity());

	for (unsigned i = 0; i < 500; i++) {
		ASSERT_TRUE(bf.set(reinterpret_cast<const uint8_t*>(&i), sizeof(unsigned)));
	}
	ASSERT_EQ(500, bf.item());
	std::vector<unsigned> keys(500);
	for (unsigned i = 0; i < 500; i++) {
		keys[i] = i + 1000;
	}
	bf.batch_set(keys.size(), sizeof(unsigned), reinterpret_cast<const uint8_t*>(keys.data()));
	ASSERT_LE(bf.item(), 1000);
	ASSERT_GE(bf.item(), 990);

	for (unsigned i = 0; i < 500; i++) {
		ASSERT_TRUE(bf.test(reinterpret_cast<const uint8_t*>(&i), sizeof(unsigned)));
	}

	for (unsigned i = 0; i < 500; i++) {
		keys[i] = i * 2;
	}
	std::vector<uint8_t> result(keys.size());
	unsigned hit = bf.batch_test(keys.size(), sizeof(unsigned),
		reinterpret_cast<const uint8_t*>(keys.data()), 
		reinterpret_cast<bool*>(result.data()));
	for (unsigned i = 0; i < 250; i++) {
		ASSERT_TRUE(result[i]);
	}
	ASSERT_FALSE(result.back());
	ASSERT_GE(hit, 250);
	ASSERT_LE(hit, 260);
}

TEST(BBF, DumpAndLoad) {
	bbf::BloomFilter bf1(999);
	ASSERT_FALSE(!bf1);
	for (unsigned i = 0; i < 500; i++) {
		ASSERT_TRUE(bf1.set(reinterpret_cast<const uint8_t*>(&i), sizeof(unsigned)));
	}

	const std::string filename = "tmp.bbf";
	{
		shd::FileWriter output(filename.c_str());
		ASSERT_TRUE(bf1.dump(output));
	}

	bbf::BloomFilter bf2(filename);
	ASSERT_FALSE(!bf2);
	for (unsigned i = 0; i < 500; i++) {
		ASSERT_TRUE(bf2.test(reinterpret_cast<const uint8_t*>(&i), sizeof(unsigned)));
	}
}