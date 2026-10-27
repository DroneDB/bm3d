// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reading the input into the build, and saying whose fault it is when that fails.

#ifndef BM3D_INPUT_READER_H
#define BM3D_INPUT_READER_H

#include <new>
#include <string>

#include "../common/error.h"

class StreamSoup;

namespace bm3d::detail {

//Runs one step of reading the input (the mesh, its textures) and says whose fault a failure is,
//by the type of what is thrown. The input's as a rule: a loader reports a file that cannot be
//opened, parsed or decoded with an Error, which becomes an InputError here. The machine's when
//memory runs out (std::bad_alloc) or a scratch file cannot be created, grown, written or mapped
//(ScratchError, which only the scratch layer throws): the input may well be fine, so those pass
//through as they are and make a failed build. The standard library throws nothing else here that
//is not about the input: the file system calls of the reading code take an error_code, and what
//does throw (std::system_error) is the conversion of a name the input holds, a texture name that
//is not UTF-8, into a path; that is the input's too.
template <typename Read>
void readingInput(Read &&read) {
	try {
		read();
	} catch(const InputError &) {
		throw;
	} catch(const ScratchError &) {
		throw;
	} catch(const std::bad_alloc &) {
		throw;
	} catch(const Error &e) {
		throw InputError(e.what());
	} catch(...) {
		throw InputError(describeCurrentException());
	}
}

//Reads the mesh in the file `input` into the out-of-core stream. A file the loaders accept without
//finding a triangle in it (a text file with an .obj name, say), or whose triangles span more than
//the builder can measure, is a problem of the input as well. Throws InputError, or what
//readingInput lets through.
void readMesh(StreamSoup &stream, const std::string &input);

} //namespace

#endif // BM3D_INPUT_READER_H
