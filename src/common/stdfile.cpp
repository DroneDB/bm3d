#include "stdfile.h"

#include <filesystem>

#include "pathutil.h"

namespace bm3d::detail {
	void StdFile::setFileName(const char* uri)
	{
		name = uri;
	}
	const char *StdFile::fileName() {
		return name.c_str();
	}

	//only reading: the files are written by the builder and the compressor
	bool StdFile::open(OpenMode openmode)
	{
		if (openmode != OpenMode::Read)
			return false;
		return file.open(name);
	}

	long long int StdFile::read(char* where, size_t length)
	{
		return file.read(where, length);
	}


	long long int StdFile::write(char*, size_t)
	{
		return -1;
	}

	size_t StdFile::size()
	{
		std::error_code ec;
		const auto size = std::filesystem::file_size(nativePath(name), ec);
		return ec ? 0 : size_t(size);
	}

	void* StdFile::map(size_t from, size_t size)
	{
		if (!mapping.isOpen()) {
			try {
				mapping.openReadOnly(name);
			} catch (const Error &) {
				return nullptr;
			}
		}
		return mapping.map(from, size);
	}

	bool StdFile::unmap(void* mapped)
	{
		return mapping.unmap(mapped);
	}

	bool StdFile::seek(size_t to)
	{
		file.seek(to);
		return true;
	}

}
