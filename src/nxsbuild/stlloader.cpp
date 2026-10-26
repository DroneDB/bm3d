#include "stlloader.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <system_error>

#include "../common/error.h"
#include "../common/pathutil.h"
#include "../common/textparse.h"
#include "finite.h"

namespace {

const size_t HeaderSize = 80;
const size_t CountOffset = HeaderSize;
const size_t PrefixSize = 84;     //the header and the count
const size_t RecordSize = 50;     //a normal and three vertices of 3 floats each, and 2 bytes
const size_t VerticesOffset = 12; //after the normal
const size_t AttributeOffset = 48;
const size_t AsciiProbeSize = 1 << 16;

//the bytes of a little endian value, whatever the byte order of the machine and the alignment
//of the bytes
uint32_t littleUint32(const unsigned char *p) {
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

float littleFloat(const unsigned char *p) {
	const uint32_t bits = littleUint32(p);
	float value;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

//The size of a binary file of n triangles
uint64_t binarySize(uint64_t n) {
	return PrefixSize + RecordSize * n;
}

//true when the text starts with "solid" (the rest of that line is the name) and goes on with a
//facet, or with the end of the solid when it holds no facet
bool looksAscii(const char *text, size_t length) {
	const auto isSpace = [](char c) { return nx::text::isSpace(c); };
	if(length < 5 || !nx::text::equalsNoCase(text, 5, "solid"))
		return false;
	size_t i = 5;
	while(i < length && text[i] != '\n')
		i++;
	if(i == length) //no line ends in the probe
		return false;
	while(i < length && isSpace(text[i]))
		i++;
	size_t end = i;
	while(end < length && !isSpace(text[end]))
		end++;
	return nx::text::equalsNoCase(text + i, end - i, "facet") || nx::text::equalsNoCase(text + i, end - i, "endsolid");
}

//The colour of 5 bits per channel as 8 bits
unsigned char expand5(unsigned value) {
	return (unsigned char)((value * 255 + 15) / 31);
}

void clear(Triangle &triangle) {
	std::memset(&triangle, 0, sizeof(triangle));
	triangle.tex = -1; //no texture
}

}

STLLoader::STLLoader(const std::string &filename): filename(filename) {

	has_colors = has_normals = has_textures = false;

	if(!file.open(filename))
		throw nx::Error("could not open file " + filename);

	std::error_code error;
	const uint64_t size = std::filesystem::file_size(nx::nativePath(filename), error);
	if(error)
		throw nx::Error("could not read file " + filename + ": " + error.message());

	unsigned char prefix[PrefixSize];
	const bool hasCount = file.read(prefix, PrefixSize) == PrefixSize;
	const uint64_t count = hasCount ? littleUint32(prefix + CountOffset) : 0;

	bool binary = hasCount && size == binarySize(count);
	if(!binary) {
		file.seek(0);
		std::vector<char> probe(AsciiProbeSize);
		probe.resize(file.read(probe.data(), probe.size()));
		ascii = looksAscii(probe.data(), probe.size());
		binary = !ascii && hasCount && size > binarySize(count);
		if(binary)
			std::cerr << "Warning: stl file " << filename << " holds " << size - binarySize(count) << " bytes after its "
			          << count << " triangles; they are ignored" << std::endl;
	}

	if(ascii) {
		file.seek(0);
		return;
	}
	if(!binary) {
		if(!hasCount)
			throw nx::Error("stl file " + filename + " is neither an ASCII STL (which starts with \"solid\" and a facet) nor a "
			                "binary one (which has at least " + std::to_string(PrefixSize) + " bytes): it has " + std::to_string(size));
		throw nx::Error("stl file " + filename + " is truncated or is not an STL file: it is neither an ASCII STL (which starts "
		                "with \"solid\" and a facet) nor a binary one, where " + std::to_string(count) + " triangles, as its "
		                "header says, take " + std::to_string(binarySize(count)) + " bytes and it has " + std::to_string(size));
	}

	n_triangles = count;
	file.seek(PrefixSize);

	//Materialise Magics: "COLOR=" and the colour of the faces that have none, in the header
	for(size_t i = 0; i + 6 + 4 <= HeaderSize; i++) {
		if(std::memcmp(prefix + i, "COLOR=", 6) != 0)
			continue;
		std::memcpy(default_color, prefix + i + 6, 3); //not the alpha
		materialise = true;
		break;
	}
}


/*
solid name
  facet normal nx ny nz
    outer loop
      vertex v1x v1y v1z
      vertex v2x v2y v2z
      vertex v3x v3y v3z
    endloop
  endfacet
endsolid name */

//The next word of the file, wherever it is: on this line or a later one. False at the end.
bool STLLoader::nextWord(const char *&word, size_t &length) {
	while(true) {
		cursor = nx::text::skipSpaces(cursor);
		if(*cursor)
			break;
		size_t lineLength;
		if(!file.readLine(cursor, lineLength))
			return false;
		line_number++;
	}
	word = cursor;
	while(*cursor && !nx::text::isSpace(*cursor))
		cursor++;
	length = size_t(cursor - word);
	return true;
}

//the rest of this line is not read
void STLLoader::skipLine() {
	cursor = "";
}

void STLLoader::fail(const std::string &what) {
	throw nx::Error("invalid STL file " + filename + ", line " + std::to_string(line_number) + ", triangle " +
	                std::to_string(current_triangle) + ": " + what);
}

void STLLoader::expectWord(const char *keyword) {
	const char *word;
	size_t length;
	if(!nextWord(word, length))
		fail(std::string("the file ends where \"") + keyword + "\" is expected");
	if(!nx::text::equalsNoCase(word, length, keyword))
		fail(std::string("\"") + keyword + "\" is expected, found \"" + std::string(word, std::min<size_t>(length, 40)) + "\"");
}

double STLLoader::expectNumber() {
	const char *word;
	size_t length;
	if(!nextWord(word, length))
		fail("the file ends where a number is expected");
	const char *p = word;
	double value;
	if(!nx::text::parseDouble(p, value) || p != word + length)
		fail("a finite number is expected, found \"" + std::string(word, std::min<size_t>(length, 40)) + "\"");
	return value;
}

void STLLoader::readFacet(Triangle &triangle) {
	expectWord("normal");
	for(int k = 0; k < 3; k++) { //the normal is not used, and some programs write it as "nan"
		const char *word;
		size_t length;
		if(!nextWord(word, length))
			fail("the file ends inside a facet");
	}
	expectWord("outer");
	expectWord("loop");

	clear(triangle);
	for(int k = 0; k < 3; k++) {
		expectWord("vertex");
		float *v = triangle.vertices[k].v;
		for(int j = 0; j < 3; j++)
			v[j] = (float)expectNumber();
		nx::requireFinite(v, [&]() {
			return "vertex " + std::to_string(k) + " of triangle " + std::to_string(current_triangle) + " of stl file " + filename;
		});
	}
	expectWord("endloop");
	expectWord("endfacet");
}

uint32_t STLLoader::getTrianglesAscii(uint32_t size, Triangle *buffer) {
	uint32_t count = 0;
	while(count < size) {
		const char *word;
		size_t length;
		if(!nextWord(word, length))
			break;
		if(nx::text::equalsNoCase(word, length, "solid") || nx::text::equalsNoCase(word, length, "endsolid")) {
			skipLine(); //the name
			continue;
		}
		if(!nx::text::equalsNoCase(word, length, "facet"))
			fail("\"facet\" is expected, found \"" + std::string(word, std::min<size_t>(length, 40)) + "\"");
		readFacet(buffer[count]);
		current_triangle++;
		count++;
	}
	return count;
}

uint32_t STLLoader::getTrianglesBinary(uint32_t size, Triangle *buffer) {
	const uint32_t count = (uint32_t)std::min<uint64_t>(size, n_triangles - current_triangle);
	if(count == 0)
		return 0;

	records.resize(RecordSize * count);
	if(file.read(records.data(), records.size()) != records.size())
		throw nx::Error("stl file " + filename + " is truncated: it ends inside triangle " +
		                std::to_string(current_triangle) + " of " + std::to_string(n_triangles));

	for(uint32_t i = 0; i < count; i++) {
		const unsigned char *record = records.data() + RecordSize * i;
		Triangle &triangle = buffer[i];
		clear(triangle);
		for(int t = 0; t < 3; t++) {
			float *v = triangle.vertices[t].v;
			for(int k = 0; k < 3; k++)
				v[k] = littleFloat(record + VerticesOffset + 4*(3*t + k));
			nx::requireFinite(v, [&]() {
				return "vertex " + std::to_string(t) + " of triangle " + std::to_string(current_triangle) + " of stl file " + filename;
			});
		}

		if(materialise) {
			unsigned char color[4] = { default_color[0], default_color[1], default_color[2], default_color[3] };
			const unsigned attribute = record[AttributeOffset] | unsigned(record[AttributeOffset + 1]) << 8;
			if(attribute & 0x8000) { //the face has its own colour
				color[0] = expand5(attribute & 31);
				color[1] = expand5((attribute >> 5) & 31);
				color[2] = expand5((attribute >> 10) & 31);
				has_colors = true; //read by the stream when the whole file is loaded
			}
			for(int t = 0; t < 3; t++)
				std::memcpy(triangle.vertices[t].c, color, 4);
		}
		current_triangle++;
	}
	return count;
}
