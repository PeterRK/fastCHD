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

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <utils.h>

namespace chd {

struct DefaultLogger : public Logger {
	void printf(const char* format, va_list args) override;
	static DefaultLogger instance;
};
void DefaultLogger::printf(const char *format, va_list args) {
	::vfprintf(stderr, format, args);
}
DefaultLogger DefaultLogger::instance;
Logger* Logger::s_instance = &DefaultLogger::instance;

void Logger::Printf(const char* format, ... ) {
	if (s_instance != nullptr) {
		va_list args;
		va_start(args, format);
		s_instance->printf(format, args);
		va_end(args);
	}
}

static inline constexpr size_t RoundUp(size_t n) {
	constexpr size_t m = 0x1fffff;
	return (n+m)&(~m);
};

MemBlock::MemBlock(size_t size) noexcept : MemBlock() {
	if (size == 0) {
		return;
	}
	if (size >= 0x4000000) {
		auto round_up_size = RoundUp(size);
		void* addr = mmap(nullptr, round_up_size, PROT_READ | PROT_WRITE,
						  MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
		if (addr == MAP_FAILED && errno == ENOMEM) {
			addr = mmap(nullptr, round_up_size, PROT_READ | PROT_WRITE,
						MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		}
		if (addr != MAP_FAILED) {
			m_addr = static_cast<uint8_t*>(addr);
			m_size = size;
			m_mmap = 1;
			if (madvise(addr, round_up_size, MADV_DONTDUMP) != 0) {
				Logger::Printf("fail to madvise[%d]: %p | %lu\n", errno, addr, round_up_size);
			}
			return;
		}
	}
	m_addr = static_cast<uint8_t*>(malloc(size));
	if (m_addr != nullptr) {
		m_size = size;
	}
}

MemBlock::~MemBlock() noexcept {
	if (m_addr != nullptr) {
		if (m_mmap) {
			if (munmap(m_addr, RoundUp(m_size)) != 0) {
				Logger::Printf("fail to munmap[%d]: %p | %lu\n", errno, m_addr, m_size);
			};
		} else {
			free(m_addr);
		}
	}
}

static MemBlock LoadAll(int fd) noexcept {
	struct stat stat;
	if (fstat(fd, &stat) != 0 || stat.st_size <= 0) {
		return {};
	}
	MemBlock out(stat.st_size);
	if (!out) {
		return {};
	}
	auto data = out.addr();
	auto remain = out.size();
	constexpr size_t block = 16*1024*1024;
	size_t off = 0;
	while (remain > block) {
		auto next = off + block;
		readahead(fd, next, block);
		if (pread(fd, data, block, off) != block) {
			return {};
		}
		off = next;
		data += block;
		remain -= block;
	}
	if (pread(fd, data, remain, off) != remain) {
		return {};
	}
	return out;
}

MemBlock MemBlock::LoadFile(const char* path) noexcept {
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		Logger::Printf("fail to open file: %s\n", path);
		return {};
	}
	auto out = LoadAll(fd);
	close(fd);
	if (!out) {
		Logger::Printf("fail to read whole file: %s\n", path);
	}
	return out;
}


MemMap::MemMap(const char* path, Policy policy) noexcept {
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		Logger::Printf("fail to open file: %s\n", path);
		return;
	}
	struct stat stat;
	if (fstat(fd, &stat) != 0 || stat.st_size <= 0) {
		close(fd);
		return;
	}
	int flag = MAP_PRIVATE;
	if (policy != MAP_ONLY) {
		flag |= MAP_POPULATE;
	}
	if (policy == OCCUPY && geteuid() == 0) {
		flag |= MAP_LOCKED;
	}
	auto addr = mmap(nullptr, stat.st_size, PROT_READ, flag, fd, 0);
	close(fd);
	if (addr == MAP_FAILED) {
		return;
	}
	m_addr = static_cast<uint8_t*>(addr);
	m_size = stat.st_size;
}

MemMap::~MemMap() noexcept {
	if (m_addr != nullptr) {
		if (munmap(m_addr, m_size) != 0) {
			Logger::Printf("fail to munmap[%d]: %p | %lu\n", errno, m_addr, m_size);
		};
	}
}

FileWriter::FileWriter(const char* path) {
	m_fd = open(path, O_CREAT|O_TRUNC|O_WRONLY, 0644);
	if (m_fd < 0) {
		return;
	}
	m_buf = std::make_unique<uint8_t[]>(BUFSZ);
}
FileWriter::~FileWriter() noexcept {
	_flush();
	if (m_fd >= 0) {
		::close(m_fd);
	}
}
bool FileWriter::operator!() const noexcept {
	return m_fd < 0;
}

bool FileWriter::_write(const void* data, size_t n) noexcept {
	if (::write(m_fd, data, n) != n) {
		::close(m_fd);
		m_fd = -1;
		return false;
	}
	return true;
}

bool FileWriter::flush() noexcept {
	return _flush();
}
bool FileWriter::_flush() noexcept {
	if (m_off == 0) {
		return true;
	}
	if (m_fd < 0) {
		return false;
	}
	auto n = m_off;
	m_off = 0;
	return _write(m_buf.get(), n);
}

bool FileWriter::write(const void* data, size_t n) noexcept {
	if (m_fd < 0) {
		return false;
	}
	if (m_off + n < BUFSZ) {
		memcpy(m_buf.get()+m_off, data, n);
		m_off += n;
	} else if (m_off + n < BUFSZ*2) {
		auto m = BUFSZ - m_off;
		memcpy(m_buf.get()+m_off, data, m);
		if (!_write(m_buf.get(), BUFSZ)) {
			return false;
		}
		m_off = n - m;
		memcpy(m_buf.get(), (const uint8_t*)data+m, m_off);
	} else {
		_flush();
		constexpr size_t block = 16*1024*1024;
		static_assert(block > BUFSZ*64, "block should be large enough");
		while (n > block) {
			if (!_write(data, block)) {
				return false;
			}
			n -= block;
			data = (uint8_t*)data + block;
		}
		return _write(data, n);
	}
	return true;
}

} //chd