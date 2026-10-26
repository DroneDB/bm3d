#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Import the used subset of corto into third_party/corto, byte for byte.

Upstream is cnr-isti-vclab/corto. The files listed in third_party/corto/FILES are read from
the pinned commit; there are no patches. third_party/corto/CORTO_SOURCE records the result.

  sync-corto.py --source <corto clone>           import (after editing FILES)
  sync-corto.py --check                          committed files == FILES == CORTO_SOURCE hashes
  sync-corto.py --check --source <corto clone>   also re-derive them from upstream
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import syncutil  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

COMPONENT = syncutil.Component(
    name="corto",
    dest=os.path.join(ROOT, "third_party", "corto"),
    manifest_name="CORTO_SOURCE",
    repository="https://github.com/cnr-isti-vclab/corto",
    default_commit="d240e69306a39417c098475d7002eb16b7264248",
)

if __name__ == "__main__":
    sys.exit(syncutil.main(COMPONENT, "sync-corto.py"))
