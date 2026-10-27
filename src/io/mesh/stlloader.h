#ifndef STLLOADER_H
#define STLLOADER_H

#include <cstdint>
#include <string>
#include <vector>

#include "meshloader.h"
#include "../../common/textfile.h"

//Reads an STL file, binary or ASCII. Which one it is does not depend on how it starts, since a
//binary file may begin with "solid" in its header, as many programs write it:
//  - a file of 84 + 50 n bytes, where n is the count in its header, is binary;
//  - otherwise a file that starts with "solid" and goes on with a facet (or the end of the solid)
//    is ASCII;
//  - otherwise a file longer than 84 + 50 n bytes is binary and its n triangles are read; the
//    bytes after them are ignored, with a warning;
//  - anything else (a file shorter than its count asks for, or not an STL at all) is refused.
//The ASCII reader takes words, not lines: any white space, blank lines, Windows line ends,
//keywords in any case, several solids in a file.
//
//A binary file whose header holds "COLOR=" and four bytes (red, green, blue, alpha) is read as
//the Materialise Magics colours: the 16 bits after each triangle are its own colour when the
//highest one is set (red in the lowest five bits, then green, then blue), and the colour of the
//header when it is not. The alpha of the header is not used (MeshLab writes a space there, 32) and
//the colours are opaque. A file in which no triangle has a colour of its own, which is what
//MeshLab writes with that header for any model, has no colours: the colour of the header is the
//colour of a material, not of the vertices. The other convention for these bytes, of VisCAM and
//SolidView (blue in the lowest five bits, red in the highest), is not read: without "COLOR=" the
//bytes are ignored.
class STLLoader: public MeshLoader {
public:
	STLLoader(const std::string &filename);

	void setMaxMemory(uint64_t /*max_memory*/) { /* ignore, here no memory needed */ }
	uint64_t vertexCount() const override { return 3 * current_triangle; }
	uint32_t getTriangles(uint32_t size, Triangle *buffer) {
		return ascii ? getTrianglesAscii(size, buffer) : getTrianglesBinary(size, buffer);
	}

private:
	uint32_t getTrianglesAscii(uint32_t size, Triangle *buffer);
	uint32_t getTrianglesBinary(uint32_t size, Triangle *buffer);

	//the ASCII reader
	bool nextWord(const char *&word, size_t &length);
	void skipLine();
	void expectWord(const char *keyword);
	double expectNumber();
	void readFacet(Triangle &triangle);
	[[noreturn]] void fail(const std::string &what);

	bm3d::detail::InputFile file;
	std::string filename;

	bool ascii = false;
	uint64_t n_triangles = 0;      //binary: the count of the header
	uint64_t current_triangle = 0; //the triangles read so far
	bool materialise = false;                        //the header holds "COLOR="
	unsigned char default_color[4] = { 0, 0, 0, 255 }; //Materialise: for the faces without a colour of their own
	std::vector<unsigned char> records;              //binary: the records being converted

	const char *cursor = ""; //ASCII: what is left of the current line
	uint64_t line_number = 0;
};

#endif // STLLOADER_H
