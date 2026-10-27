/*
Nexus

Copyright (C) 2026 Digipa

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License (http://www.gnu.org/licenses/gpl.txt)
for more details.
*/
#ifndef BM3D_ERROR_H
#define BM3D_ERROR_H

#include <exception>
#include <stdexcept>
#include <string>

namespace bm3d::detail {

//The one exception the builder code throws for a failure it reports to its caller (unreadable or
//malformed input, a file that cannot be written, ...). The message is UTF-8.
class Error: public std::runtime_error {
public:
	explicit Error(const std::string &message): std::runtime_error(message) {}
};

//The input cannot be built from: the mesh or a texture its materials name is missing, unreadable
//or malformed. The stage that reads the input turns the Error of a loader into this one (see
//BuildPipeline), so that a caller tells the input's faults from the machine's by the type.
class InputError: public Error {
public:
	explicit InputError(const std::string &message): Error(message) {}
};

//The machine cannot take a scratch file of the build: the temporary folder cannot be found, or
//the file cannot be created, grown, written or mapped (a disk that is full or read only, an
//address space that is used up). Thrown by the scratch layer (MappedFile::createTemporary and
//resize, ScratchFile, VirtualMemory), never for a file the input names: the input may well be
//fine, so this is a failed build, not an invalid input, wherever in the build it happens.
class ScratchError: public Error {
public:
	explicit ScratchError(const std::string &message): Error(message) {}
};

//An argument of the build that is not about the input is refused: a block of metadata that does
//not fit in a file, say.
class ArgumentError: public Error {
public:
	explicit ArgumentError(const std::string &message): Error(message) {}
};

//A node of the model needs a texture too large to pack (see NodeTextureBaker): the
//input is fine, but the model cannot be built with its textures.
class TexturePackingError: public Error {
public:
	explicit TexturePackingError(const std::string &message): Error(message) {}
};

//A node of the model has more vertices than a node can hold once its texture seams are split (the
//texture parametrization is too fragmented for nodes of this size): the input is fine, and the
//build may succeed with fewer triangles per node.
class NodeTooLargeError: public Error {
public:
	explicit NodeTooLargeError(const std::string &message): Error(message) {}
};

//The text of the exception being handled; call it from a catch block.
inline std::string describeCurrentException() {
	try {
		throw;
	} catch(const Error &e) {
		return e.what();
	} catch(const std::exception &e) {
		return std::string("Exception: ") + e.what();
	} catch(...) {
		return "Unknown exception";
	}
}

} //namespace

#endif // BM3D_ERROR_H
