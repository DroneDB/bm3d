// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The kinds of error the public API reports, from the types the build throws.

#pragma once

namespace bm3d::detail {

// Rethrows the exception being handled as a bm3d::Error, whose kind comes from the type of the
// exception and never from its message. Call it from a catch block.
//   bm3d::Error                       as it is (already classified)
//   InputError                        ErrorKind::InvalidInput: the mesh or a texture it names
//   ArgumentError                     ErrorKind::InvalidArgument: a block of metadata that does not fit
//   anything else                     ErrorKind::BuildFailed, with the text of the exception: a failure
//                                     after the input was read, a scratch file that cannot be written
//                                     (ScratchError), memory that ran out (std::bad_alloc)
[[noreturn]] void rethrowAsApiError();

}  // namespace bm3d::detail
