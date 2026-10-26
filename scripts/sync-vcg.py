#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Import the used subset of VCGLib into third_party/vcg, byte for byte, plus our patches.

Upstream is cnr-isti-vclab/vcglib. The files listed in third_party/vcg/FILES are read from
the pinned commit and the patches in third_party/vcg-patches/ (the plylib hardening of the
DroneDB fork) are applied on top. third_party/vcg/VCG_SOURCE records the result.

  sync-vcg.py --source <vcglib clone>            import (after editing FILES or the patches)
  sync-vcg.py --check                            committed files == FILES == VCG_SOURCE hashes
  sync-vcg.py --check --source <vcglib clone>    also re-derive them from upstream + patches
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import syncutil  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

COMPONENT = syncutil.Component(
    name="vcglib",
    dest=os.path.join(ROOT, "third_party", "vcg"),
    manifest_name="VCG_SOURCE",
    repository="https://github.com/cnr-isti-vclab/vcglib",
    default_commit="03484542632225739ca96b28afcde38267561323",
    patch_dir=os.path.join(ROOT, "third_party", "vcg-patches"),
)

if __name__ == "__main__":
    sys.exit(syncutil.main(COMPONENT, "sync-vcg.py"))
