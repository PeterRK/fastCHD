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

#include <limits>
#include <random>
#if defined(__linux__)
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#endif
#include <gtest/gtest.h>
#include <utils.h>

#if defined(__linux__)
namespace shd {
unsigned GetHugePageShift() noexcept;
}
#endif


template<typename Word>
void DoTestDivisor(Word n) {
	ASSERT_NE(n, 0);
	shd::Divisor<Word> d(n);
	std::mt19937_64 rand;

	auto test = [&d](Word m) {
		ASSERT_EQ(m / d, m / d.value());
		ASSERT_EQ(m % d, m % d.value());
	};
	test(0);
	test(1);
	test(std::numeric_limits<Word>::max());

	for (unsigned i = 0; i < 1000; i++) {
		Word m = rand();
		test(m);
	}
}

template<typename Word>
void TestDivisor() {
	DoTestDivisor<Word>(std::numeric_limits<Word>::max());
	DoTestDivisor<Word>(std::numeric_limits<Word>::max()/2+1);
	DoTestDivisor<Word>(std::numeric_limits<Word>::max()/2);
	DoTestDivisor<Word>(17);
	DoTestDivisor<Word>(13);
	DoTestDivisor<Word>(11);
	DoTestDivisor<Word>(9);
	DoTestDivisor<Word>(7);
	DoTestDivisor<Word>(5);
	DoTestDivisor<Word>(3);
	DoTestDivisor<Word>(2);
	DoTestDivisor<Word>(1);
}

TEST(Divisor, Uint64) {
	TestDivisor<uint64_t>();
}

TEST(Divisor, Uint32) {
	TestDivisor<uint32_t>();
}

TEST(Divisor, Uint16) {
	TestDivisor<uint16_t>();
}

TEST(Divisor, Uint8) {
	TestDivisor<uint8_t>();
}

#if defined(__linux__)
static bool ExpectedHugePageShift(unsigned& shift,
								unsigned long long& kb) noexcept {
	auto* file = std::fopen("/proc/meminfo", "r");
	if (file == nullptr) {
		return false;
	}

	bool found = false;
	char line[256];
	while (std::fgets(line, sizeof(line), file) != nullptr) {
		if (std::strncmp(line, "Hugepagesize:", 13) != 0) {
			continue;
		}
		char unit[3]{};
		found = std::sscanf(line + 13, "%llu %2s", &kb, unit) == 2 &&
				std::strcmp(unit, "kB") == 0;
		break;
	}
	std::fclose(file);
	if (!found) {
		return false;
	}

	shift = 0;
#if defined(MAP_HUGETLB)
	constexpr unsigned long long limit = 16U * 1024U;
	if (kb == 0 || kb > limit) {
		return true;
	}
	auto size = kb * 1024U;
	if ((size & (size - 1U)) != 0) {
		return true;
	}
	while (size > 1U) {
		size >>= 1U;
		++shift;
	}
#endif
	return true;
}

TEST(HugePage, StartupDetection) {
	unsigned expected = 0;
	unsigned long long kb = 0;
	ASSERT_TRUE(ExpectedHugePageShift(expected, kb));
	EXPECT_EQ(shd::GetHugePageShift(), expected)
		<< "Hugepagesize=" << kb << " kB";
}
#endif
