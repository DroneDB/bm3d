#ifndef BM3D_STDFILE_H
#define BM3D_STDFILE_H

#include <string>

#include "mappedfile.h"
#include "file.h"
#include "textfile.h"

namespace bm3d::detail {
	//A model file on the local disk, opened read only: read sequentially, or mapped into memory.
	class StdFile
		: public File {
	private:
		InputFile file;
		MappedFile mapping;   //opened by the first map()
		std::string name;     //UTF-8
	public:
		void setFileName(const char* uri) override;
		const char *fileName() override;
		bool open(OpenMode openmode) override;
		long long int read(char* where, size_t length) override;
		long long int write(char* from, size_t length) override;
		size_t size() override;
		void* map(size_t from, size_t size) override;
		bool unmap(void* mapped) override;
		bool seek(size_t to) override;
	};
}

#endif // BM3D_STDFILE_H
