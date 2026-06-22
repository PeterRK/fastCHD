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

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <utils.h>


#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

#include <algorithm>
#include <limits>

#else

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#endif

namespace {

static constexpr size_t BLOCK_SIZE = 16U * 1024U * 1024U;

int OpenRead(const char* path) noexcept {
#if defined(_WIN32)
	return _open(path, _O_RDONLY | _O_BINARY);
#else
	return open(path, O_RDONLY);
#endif
}

int OpenWrite(const char* path) noexcept {
#if defined(_WIN32)
	return _open(path, _O_CREAT | _O_TRUNC | _O_WRONLY | _O_BINARY,
				 _S_IREAD | _S_IWRITE);
#else
	return open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
#endif
}

void Close(int fd) noexcept {
	if (fd < 0) {
		return;
	}
#if defined(_WIN32)
	_close(fd);
#else
	close(fd);
#endif
}

bool GetFileSize(int fd, size_t& size) noexcept {
#if defined(_WIN32)
	struct _stat64 stat{};
	if (_fstat64(fd, &stat) != 0 || stat.st_size <= 0 ||
		static_cast<unsigned long long>(stat.st_size) > std::numeric_limits<size_t>::max()) {
		return false;
	}
	size = static_cast<size_t>(stat.st_size);
	return true;
#else
	struct stat stat{};
	if (fstat(fd, &stat) != 0 || stat.st_size <= 0) {
		return false;
	}
	size = static_cast<size_t>(stat.st_size);
	return static_cast<off_t>(size) == stat.st_size;
#endif
}

bool ReadAll(int fd, void* buf, size_t size) noexcept {
	auto data = static_cast<uint8_t*>(buf);
	size_t done = 0;
	while (done < size) {
		const auto chunk = static_cast<unsigned>(std::min(size - done, BLOCK_SIZE));
#if defined(_WIN32)
		const auto read = _read(fd, data + done, chunk);
		if (read <= 0) {
			return false;
		}
		done += static_cast<size_t>(read);
#else
		const auto offset = static_cast<off_t>(done);
#if defined(__linux__)
		readahead(fd, offset + static_cast<off_t>(chunk), chunk);
#endif
		const auto read = pread(fd, data + done, chunk, offset);
		if (read > 0) {
			done += static_cast<size_t>(read);
			continue;
		}
		if (read < 0 && errno == EINTR) {
			continue;
		}
		return false;
#endif
	}
	return true;
}

bool WriteAll(int fd, const void* buf, size_t size) noexcept {
	auto data = static_cast<const uint8_t*>(buf);
	size_t done = 0;
	while (done < size) {
		const auto chunk = static_cast<unsigned>(std::min(size - done, BLOCK_SIZE));
#if defined(_WIN32)
		const auto written = _write(fd, data + done, chunk);
#else
		const auto written = write(fd, data + done, chunk);
#endif
		if (written > 0) {
			done += static_cast<size_t>(written);
			continue;
		}
#if !defined(_WIN32)
		if (written < 0 && errno == EINTR) {
			continue;
		}
#endif
		return false;
	}
	return true;
}

#if !defined(_WIN32)
static size_t RoundUp(size_t size) noexcept {
	constexpr size_t mask = 0x1fffff;
	return (size + mask) & ~mask;
}
#endif

void* AllocateLarge(size_t size) noexcept {
#if defined(_WIN32)
	return VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
	const auto rounded = RoundUp(size);
	void* addr = MAP_FAILED;
#if defined(MAP_ANONYMOUS)
	constexpr int anonymous = MAP_ANONYMOUS;
#else
	constexpr int anonymous = MAP_ANON;
#endif
#if defined(__linux__) && defined(MAP_HUGETLB)
	addr = mmap(nullptr, rounded, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | anonymous | MAP_HUGETLB, -1, 0);
#endif
	if (addr == MAP_FAILED) {
		addr = mmap(nullptr, rounded, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | anonymous, -1, 0);
	}
	if (addr == MAP_FAILED) {
		return nullptr;
	}
#if defined(__linux__) && defined(MADV_DONTDUMP)
	madvise(addr, rounded, MADV_DONTDUMP);
#endif
	return addr;
#endif
}

void FreeLarge(void* addr, size_t size) noexcept {
	if (addr == nullptr) {
		return;
	}
#if defined(_WIN32)
	(void)size;
	VirtualFree(addr, 0, MEM_RELEASE);
#else
	munmap(addr, RoundUp(size));
#endif
}

bool MapReadOnly(const char* path, bool fetch, bool occupy,
				uint8_t*& addr, size_t& size) noexcept {
	const int fd = OpenRead(path);
	if (fd < 0 || !GetFileSize(fd, size)) {
		Close(fd);
		return false;
	}

#if defined(_WIN32)
	(void)fetch;
	(void)occupy;
	const auto file = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
	const auto bytes = static_cast<unsigned long long>(size);
	const auto mapping = CreateFileMappingA(
		file, nullptr, PAGE_READONLY, static_cast<DWORD>(bytes >> 32U),
		static_cast<DWORD>(bytes), nullptr);
	if (mapping == nullptr) {
		Close(fd);
		return false;
	}
	auto* mapped = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
	CloseHandle(mapping);
	Close(fd);
	if (mapped == nullptr) {
		return false;
	}
	addr = static_cast<uint8_t*>(mapped);
	return true;
#else
	int flags = MAP_PRIVATE;
#if defined(__linux__)
	if (fetch || occupy) {
#ifdef MAP_POPULATE
		flags |= MAP_POPULATE;
#endif
	}
#ifdef MAP_LOCKED
	if (occupy && geteuid() == 0) {
		flags |= MAP_LOCKED;
	}
#endif
#endif
	auto* mapped = mmap(nullptr, size, PROT_READ, flags, fd, 0);
	Close(fd);
	if (mapped == MAP_FAILED) {
		return false;
	}
#if !defined(__linux__) && defined(MADV_WILLNEED)
	if (fetch) {
		madvise(mapped, size, MADV_WILLNEED);
	}
#endif
	addr = static_cast<uint8_t*>(mapped);
	return true;
#endif
}

void UnmapReadOnly(uint8_t* addr, size_t size) noexcept {
	if (addr == nullptr) {
		return;
	}
#if defined(_WIN32)
	(void)size;
	UnmapViewOfFile(addr);
#else
	munmap(addr, size);
#endif
}

} // namespace

namespace shd {

struct DefaultLogger : Logger {
	void printf(const char* format, va_list args) override {
		::vfprintf(stderr, format, args);
	}
	static DefaultLogger instance;
};

DefaultLogger DefaultLogger::instance;
Logger* Logger::s_instance = &DefaultLogger::instance;

void Logger::Printf(const char* format, ...) {
	if (s_instance != nullptr) {
		va_list args;
		va_start(args, format);
		s_instance->printf(format, args);
		va_end(args);
	}
}

MemBlock::MemBlock(size_t size) noexcept : MemBlock() {
	if (size == 0) {
		return;
	}
	if (size >= 0x4000000) {
		if (auto* addr = AllocateLarge(size)) {
			m_addr = static_cast<uint8_t*>(addr);
			m_size = size;
			m_mmap = 1;
			return;
		}
	}
	m_addr = static_cast<uint8_t*>(std::malloc(size));
	if (m_addr != nullptr) {
		m_size = size;
	}
}

MemBlock::~MemBlock() noexcept {
	if (m_addr == nullptr) {
		return;
	}
	if (m_mmap) {
		FreeLarge(m_addr, m_size);
	} else {
		std::free(m_addr);
	}
}

MemBlock MemBlock::LoadFile(const char* path) noexcept {
	const int fd = OpenRead(path);
	if (fd < 0) {
		Logger::Printf("fail to open file: %s\n", path);
		return {};
	}

	size_t size = 0;
	if (!GetFileSize(fd, size)) {
		Close(fd);
		return {};
	}
	MemBlock out(size);
	const bool ok = !out ? false : ReadAll(fd, out.addr(), out.size());
	Close(fd);
	if (!ok) {
		Logger::Printf("fail to read whole file: %s\n", path);
		return {};
	}
	return out;
}

MemMap::MemMap(const char* path, Policy policy) noexcept {
	uint8_t* addr = nullptr;
	if (!MapReadOnly(path, policy == FETCH, policy == OCCUPY, addr, m_size)) {
		return;
	}
	m_addr = addr;
}

MemMap::~MemMap() noexcept {
	if (m_addr != nullptr) {
		UnmapReadOnly(m_addr, m_size);
	}
}

FileWriter::FileWriter(const char* path) {
	m_fd = OpenWrite(path);
	if (m_fd >= 0) {
		m_buf = std::make_unique<uint8_t[]>(BUFSZ);
	}
}

FileWriter::~FileWriter() noexcept {
	if (m_fd >= 0) {
		_flush();
		Close(m_fd);
	}
}

bool FileWriter::operator!() const noexcept {
	return m_fd < 0;
}

bool FileWriter::_write(const void* data, size_t n) noexcept {
	constexpr size_t block = 16U * 1024U * 1024U;
	while (n > block) {
		if (!WriteAll(m_fd, data, block)) {
			Close(m_fd);
			m_fd = -1;
			return false;
		}
		n -= block;
		data = static_cast<const uint8_t*>(data) + block;
	}
	if (!WriteAll(m_fd, data, n)) {
		Close(m_fd);
		m_fd = -1;
		return false;
	}
	return true;
}

bool FileWriter::flush() noexcept {
	return m_fd >= 0 && _flush();
}

bool FileWriter::_flush() noexcept {
	if (m_off == 0) {
		return true;
	}
	const auto n = m_off;
	m_off = 0;
	return _write(m_buf.get(), n);
}

bool FileWriter::write(const void* data, size_t n) noexcept {
	if (m_fd < 0) {
		return false;
	}
	if (m_off + n < BUFSZ) {
		std::memcpy(m_buf.get() + m_off, data, n);
		m_off += static_cast<unsigned>(n);
	} else if (m_off + n < BUFSZ * 2U) {
		const auto m = BUFSZ - m_off;
		std::memcpy(m_buf.get() + m_off, data, m);
		if (!_write(m_buf.get(), BUFSZ)) {
			return false;
		}
		m_off = static_cast<unsigned>(n - m);
		std::memcpy(m_buf.get(), static_cast<const uint8_t*>(data) + m, m_off);
	} else {
		return _flush() && _write(data, n);
	}
	return true;
}

} // namespace shd
