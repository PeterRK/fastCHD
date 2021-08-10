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
#ifndef CHD_UTILS_H_
#define CHD_UTILS_H_

#include <cstdint>
#include <cstdarg>
#include <memory>
#include <utility>
#include <type_traits>

namespace chd {

class MemBlock final {
public:
	MemBlock() noexcept
		: m_addr(nullptr), m_size(0), m_mmap(0)
	{}
	~MemBlock() noexcept;
	explicit MemBlock(size_t size) noexcept;

	MemBlock(MemBlock&& other) noexcept
		: m_addr(other.m_addr), m_size(other.m_size), m_mmap(other.m_mmap) {
		other.m_addr = nullptr;
		other.m_size = 0;
		other.m_mmap = 0;
	}
	MemBlock& operator=(MemBlock&& other) noexcept {
		if (&other != this) {
			this->~MemBlock();
			new(this)MemBlock(std::move(other));
		}
		return *this;
	}

	size_t size() const noexcept { return m_size; }
	uint8_t* addr() const noexcept { return m_addr; }
	uint8_t* end() const noexcept { return m_addr + m_size; }
	bool operator!() const noexcept { return m_addr == nullptr; }

	static MemBlock LoadFile(const char* path) noexcept;
private:
	MemBlock(const MemBlock&) noexcept = delete;
	MemBlock& operator=(const MemBlock&) noexcept = delete;
	uint8_t* m_addr;
	size_t m_size : (sizeof(size_t) * 8 - 1);
	size_t m_mmap : 1;
};


class MemMap final {
public:
	MemMap() noexcept = default;
	~MemMap() noexcept;

	enum Policy {MAP_ONLY, FETCH, OCCUPY};
	explicit MemMap(const char* path, Policy policy=MAP_ONLY) noexcept;

	MemMap(MemMap&& other) noexcept
		: m_addr(other.m_addr), m_size(other.m_size) {
		other.m_addr = nullptr;
		other.m_size = 0;
	}
	MemMap& operator=(MemMap&& other) noexcept {
		if (&other != this) {
			this->~MemMap();
			new(this)MemMap(std::move(other));
		}
		return *this;
	}

	size_t size() const noexcept { return m_size; }
	const uint8_t* addr() const noexcept { return m_addr; }
	const uint8_t* end() const noexcept { return m_addr + m_size; }
	bool operator!() const noexcept { return m_addr == nullptr; }
private:
	MemMap(const MemMap&) noexcept = delete;
	MemMap& operator=(const MemMap&) noexcept = delete;
	uint8_t* m_addr = nullptr;
	size_t m_size = 0;
};


class Logger {
public:
	virtual ~Logger() = default;
	virtual void printf(const char* format, va_list args) = 0;
	static void Printf(const char* format, ...);
	static Logger* Bind(Logger* logger) noexcept {
		auto old = s_instance;
		s_instance = logger;
		return old;
	}
private:
	static Logger* s_instance;
};

struct Slice {
	const uint8_t* ptr = nullptr;
	size_t len = 0;
};

struct Record {
	Slice key;
	Slice val;
};

struct IDataReader {
	virtual void reset() = 0;
	virtual size_t total() = 0;
	virtual Record read(bool key_only) = 0;
	virtual ~IDataReader() noexcept = default;
};

struct IDataWriter {
	virtual bool operator!() const noexcept = 0;
	virtual bool flush() = 0;
	virtual bool write(const void* data, size_t n) = 0;
	virtual ~IDataWriter() noexcept = default;
};

class FileWriter : public IDataWriter {
public:
	FileWriter() = default;
	explicit FileWriter(const char* path);
	virtual ~FileWriter() noexcept;

	FileWriter(FileWriter&& other) noexcept
		: m_buf(std::move(other.m_buf)), m_fd(other.m_fd) {
		other.m_fd = -1;
	}
	FileWriter& operator=(FileWriter&& other) noexcept {
		if (&other != this) {
			this->~FileWriter();
			new(this)FileWriter(std::move(other));
		}
		return *this;
	}

	bool operator!() const noexcept override;
	bool flush() noexcept override;
	bool write(const void* data, size_t n) noexcept override;

private:
	static constexpr size_t BUFSZ = 8192;
	std::unique_ptr<uint8_t[]> m_buf;
	unsigned m_off = 0;
	int m_fd = -1;
	bool _flush() noexcept;
	bool _write(const void* data, size_t n) noexcept;
};


//Granlund-Montgomery
template <typename Word>
class DivisorGM {
private:
	static_assert(std::is_same<Word,uint8_t>::value | std::is_same<Word,uint16_t>::value
				  || std::is_same<Word,uint32_t>::value || std::is_same<Word,uint64_t>::value);
	Word m_val = 0;
#ifndef DISABLE_SOFT_DIVIDE
	Word m_fac = 0;
	unsigned m_sft = 0;
	using DoubleWord = typename std::conditional<std::is_same<Word,uint8_t>::value, uint16_t,
		typename std::conditional<std::is_same<Word,uint16_t>::value, uint32_t,
		typename std::conditional<std::is_same<Word,uint32_t>::value, uint64_t, __uint128_t>::type>::type>::type;
	static constexpr unsigned BITWIDTH = sizeof(Word)*8;
#endif

protected:
	void _init(Word n) noexcept {
		m_val = n;
#ifndef DISABLE_SOFT_DIVIDE
		if (n == 0) {
			m_fac = 0;
			m_sft = 0;
		} else {
			m_sft = BITWIDTH - 1;
			constexpr Word one = 1;
			for (auto t = one << m_sft; t > n; t >>= 1U) {
				m_sft--;
			}
			constexpr DoubleWord dw_one = 1;
			const DoubleWord c = dw_one << (m_sft + BITWIDTH);
			m_fac = 2 * (c / n) + (2 * (c % n)) / n + 1 - (dw_one << BITWIDTH);
		}
#endif
	}

public:
	Word value() const noexcept { return m_val; }

	Word div(Word m) const noexcept {
#ifdef DISABLE_SOFT_DIVIDE
		return m / m_val;
#else
		Word t = (m * (DoubleWord)m_fac) >> BITWIDTH;
		t += (m - t) >> 1U;
		if (m_fac <= 1U) {
			t = m;
		}
		return t >> m_sft;
#endif
	}

	Word mod(Word m) const noexcept {
#ifdef DISABLE_SOFT_DIVIDE
		return m % m_val;
#else
		Word t = (m * (DoubleWord)m_fac) >> BITWIDTH;
		t += (m - t) >> 1U;
		auto out = m - m_val * (t >> m_sft);
		constexpr Word one = 1U;
		const auto tmp = m & ((one << m_sft) - 1U);
		if (m_fac <= 1U) {
			out = tmp;
		}
		return out;
#endif
	}
};

//Lemire-Kaser-Kurz
template <typename Word>
class DivisorLKK {
private:
	static_assert(std::is_same<Word, uint8_t>::value | std::is_same<Word, uint16_t>::value
				  || std::is_same<Word, uint32_t>::value);
	Word m_val = 0;
#ifndef DISABLE_SOFT_DIVIDE
	static constexpr unsigned BITWIDTH = sizeof(Word)*8;
	using DoubleWord = typename std::conditional<std::is_same<Word,uint8_t>::value, uint16_t,
		typename std::conditional<std::is_same<Word,uint16_t>::value, uint32_t, uint64_t>::type>::type;
	using QuaterWord = typename std::conditional<std::is_same<Word,uint8_t>::value, uint32_t,
		typename std::conditional<std::is_same<Word,uint16_t>::value, uint64_t, __uint128_t>::type>::type;
	DoubleWord m_fac = 0;
#endif
protected:
	void _init(Word n) noexcept {
		m_val = n;
#ifndef DISABLE_SOFT_DIVIDE
		if (n == 0) {
			m_fac = 0;
		} else {
			constexpr DoubleWord zero = 0;
			m_fac = (DoubleWord)~zero / n + 1;
		}
#endif
	}

public:
	Word value() const noexcept { return m_val; }

	Word div(Word m) const noexcept {
#ifdef DISABLE_SOFT_DIVIDE
		return m / m_val;
#else
		return m_fac == 0 ? m : (m * (QuaterWord)m_fac) >> (BITWIDTH * 2);
#endif
	}

	Word mod(Word m) const noexcept {
#ifdef DISABLE_SOFT_DIVIDE
		return m % m_val;
#else
		return ((QuaterWord)m_val * (DoubleWord)(m * m_fac)) >> (BITWIDTH * 2);
#endif
	}
};

template <typename Word>
struct Divisor : public DivisorLKK<Word> {
	Divisor() noexcept = default;
	explicit Divisor(Word n) noexcept { this->_init(n); }
	Divisor& operator=(Word n) noexcept {
		this->_init(n);
		return *this;
	}
};

template <>
struct Divisor<uint64_t> : public DivisorGM<uint64_t> {
	Divisor() noexcept = default;
	explicit Divisor(uint64_t n) noexcept { this->_init(n); }
	Divisor& operator=(uint64_t n) noexcept {
		this->_init(n);
		return *this;
	}
};

template <typename Word>
static inline Word operator/(Word m, const Divisor<Word>& d) noexcept {
	return d.div(m);
}

template <typename Word>
static inline Word operator%(Word m, const Divisor<Word>& d) noexcept {
	return d.mod(m);
}

} //chd
#endif //CHD_UTILS_H_