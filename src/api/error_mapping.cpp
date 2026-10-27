// Copyright (C) 2026 Digipa
// SPDX-License-Identifier: GPL-3.0-or-later

#include "error_mapping.h"

#include "bm3d/bm3d.h"
#include "../common/error.h"

namespace bm3d::detail {

void rethrowAsApiError() {
    try {
        throw;
    } catch (const bm3d::Error&) {
        throw;  // already classified
    } catch (const InputError& e) {
        throw bm3d::Error(ErrorKind::InvalidInput, e.what());
    } catch (const ArgumentError& e) {
        throw bm3d::Error(ErrorKind::InvalidArgument, e.what());
    } catch (...) {
        throw bm3d::Error(ErrorKind::BuildFailed, describeCurrentException());
    }
}

}  // namespace bm3d::detail
