#ifndef F3EBF245_52E8_4579_8E93_6A63B9854C05
#define F3EBF245_52E8_4579_8E93_6A63B9854C05

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define assert_errno(x)                                       \
	do {                                                      \
		if (!(x)) {                                           \
			fprintf(stderr,                                   \
					"[assert_errno] %s:%d, errno: %d (%s)\n", \
					__FILE__,                                 \
					__LINE__,                                 \
					errno,                                    \
					strerror(errno));                         \
			exit(EXIT_FAILURE);                               \
		}                                                     \
	} while (0);

#include "type.h"

#include <fcntl.h>
#include <functional>
#include <string>

#define __CDEF (1L << 27) // 128MB

// user interface for testing
void log(std::string const & s);
void stopwatch(std::string const & message, std::function<void()> function);

// data conversion and calculation
uint64_t be6_le8(uint8_t * in);
size_t	 ceil(size_t const x, size_t const y);

// file and folder
sp<bchan<fs::path>> fileList(fs::path const & folder, std::string const & extension);
sp<bchan<fs::path>>
			fileListOver(fs::path const & folder, std::string const & extension, size_t const over);
std::string fileNameEncode(E32 const & grid, std::string const & ext);

// parser
sp<bchan<RowPos>> splitAdj6(sp<std::vector<uint8_t>> adj6);

// parallelism
void parallelDo(size_t workers, std::function<void(size_t)> func);

template <typename T>
auto fileSave(fs::path & path, T * data, size_t byte)
{
	auto fp = open64(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);

	uint64_t chunkSize = (byte < __CDEF) ? byte : __CDEF;
	uint64_t pos	   = 0;

	while (pos < byte) {
		chunkSize = (byte - pos > chunkSize) ? chunkSize : byte - pos;
		auto b	  = write(fp, &(((uint8_t *)data)[pos]), chunkSize);
		pos += b;
	}

	close(fp);
}

template <typename T>
auto fileSaveAppend(fs::path & path, T * data, size_t byte)
{
	auto fp = open64(path.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0644);

	uint64_t chunkSize = (byte < __CDEF) ? byte : __CDEF;
	uint64_t pos	   = 0;

	while (pos < byte) {
		chunkSize = (byte - pos > chunkSize) ? chunkSize : byte - pos;
		auto b	  = write(fp, &(((uint8_t *)data)[pos]), chunkSize);
		pos += b;
	}

	close(fp);
}

template <typename T>
auto fileLoad(fs::path & path)
{
	auto fp	   = open64(path.c_str(), O_RDONLY);
	auto fbyte = fs::file_size(path);
	auto out   = std::make_shared<std::vector<T>>(fbyte / sizeof(T));

	uint64_t chunkSize = (fbyte < __CDEF) ? fbyte : __CDEF;
	uint64_t pos	   = 0;

	while (pos < fbyte) {
		chunkSize = (fbyte - pos > chunkSize) ? chunkSize : fbyte - pos;
		auto b	  = read(fp, &(((uint8_t *)(out->data()))[pos]), chunkSize);
		pos += b;
	}

	close(fp);

	return out;
}

#endif /* F3EBF245_52E8_4579_8E93_6A63B9854C05 */
