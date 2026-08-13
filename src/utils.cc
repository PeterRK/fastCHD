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
#include <limits>

#include <utils.h>


#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>

#include <algorithm>
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
static unsigned DetectHugePageShift() noexcept {
#if !defined(__linux__) || !defined(MAP_HUGETLB)
	return 0;
#else
	auto* file = std::fopen("/proc/meminfo", "r");
	if (file == nullptr) {
		return 0;
	}

	unsigned shift = 0;
	char line[256];
	while (std::fgets(line, sizeof(line), file) != nullptr) {
		static constexpr char prefix[] = "Hugepagesize:";
		if (std::strncmp(line, prefix, sizeof(prefix) - 1) != 0) {
			continue;
		}

		unsigned long long size_kb = 0;
		char unit[3]{};
		char extra = 0;
		const auto count = std::sscanf(line + sizeof(prefix) - 1,
									   " %llu %2s %c", &size_kb, unit, &extra);
		if (count != 2 || std::strcmp(unit, "kB") != 0 ||
			size_kb > std::numeric_limits<size_t>::max() / 1024U) {
			break;
		}

		const auto size = static_cast<size_t>(size_kb) * 1024U;
		static constexpr size_t max_size = 16U * 1024U * 1024U;
		if (size == 0 || size > max_size || (size & (size - 1U)) != 0) {
			break;
		}
		for (auto value = size; value > 1U; value >>= 1U) {
			++shift;
		}
		break;
	}
	std::fclose(file);
	return shift;
#endif
}

static const unsigned SHD_HUGEPAGE_SHIFT = DetectHugePageShift();

static size_t RoundUp(size_t size) noexcept {
	if (SHD_HUGEPAGE_SHIFT == 0) {
		return size;
	}
	const size_t mask = (size_t{1} << SHD_HUGEPAGE_SHIFT) - 1U;
	if (size > std::numeric_limits<size_t>::max() - mask) {
		return 0;
	}
	return (size + mask) & ~mask;
}
#endif

void* AllocateLarge(size_t size) noexcept {
#if defined(_WIN32)
	return VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
	const auto rounded = RoundUp(size);
	if (rounded == 0) {
		return nullptr;
	}
	void* addr = MAP_FAILED;
#if defined(MAP_ANONYMOUS)
	constexpr int anonymous = MAP_ANONYMOUS;
#else
	constexpr int anonymous = MAP_ANON;
#endif
#if defined(__linux__) && defined(MAP_HUGETLB)
	if (SHD_HUGEPAGE_SHIFT != 0) {
		addr = mmap(nullptr, rounded, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | anonymous | MAP_HUGETLB, -1, 0);
	}
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

unsigned GetHugePageShift() noexcept {
#if defined(_WIN32)
	return 0;
#else
	return SHD_HUGEPAGE_SHIFT;
#endif
}

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
