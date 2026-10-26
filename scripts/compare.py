#!/usr/bin/env python3
# Copyright (C) 2026 Digipa
# SPDX-License-Identifier: GPL-3.0-or-later
"""Structural parity between Nexus files built by a reference program and by a candidate.

  compare.py --ref a.nxz [--ref b.nxz ...] --cand c.nxz [--json out.json]

Exit status 0 when the candidate matches, 1 when it does not (a table says which checks), 2 for
a file that cannot be read.

The references are several builds of the same model by the reference program: their spread is
what "the same" means, because a build is not bit-for-bit repeatable. A candidate value passes
when it lies in [min, max] of the references widened by a relative tolerance. What is compared:

  layout        magic, version, flags and the vertex and face attribute layout: equal
  counts        nodes, patches, textures, levels, leaves: the reference spread, widened by
                --count-tolerance (default 0 with two or more references, 1% with one)
  totals        vertices and faces (the header and the sum over the nodes), the sum of the node
                errors, the error of the root and the bounding sphere (centre and radius):
                the spread widened by 1% (--total-tolerance; the centre by 1% of the radius)
  texels        pixels of the node textures, per level (a node's level is its depth below the
                roots in the DAG): the spread widened by 2% (--texel-tolerance)
  file size     the spread widened by 3% (--size-tolerance)
  pixels        PSNR >= 40 dB and SSIM >= 0.98 between the textures of nodes matched by place:
                a candidate node is the one of the same level whose bounding-sphere centre is
                nearest to the reference node's. At most --samples nodes are compared (200):
                all of them when there are fewer, otherwise a seeded sample (--seed) that has
                at least one node of every level. The reference of the pixels is the first
                --ref. PSNR is over the RGB channels (capped at 100 dB for identical images);
                SSIM is the mean structural similarity of the luma (ITU-R BT.601), with the
                8x8 uniform window of Wang et al. (2004) at every position, K1 = 0.01,
                K2 = 0.03, dynamic range 255. Every sampled node must reach both thresholds.

A model without textures (colours in the vertices) has nothing to compare in the last two
groups of rows, and the table says so: the geometry of a .nxz is compressed and is not decoded.
"""
import argparse
import json
import math
import random
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from nxzread import NexusFile, NexusFormatError  # noqa: E402

PSNR_CAP = 100.0
SSIM_WINDOW = 8


# --- statistics of one file ---------------------------------------------------------------
def texture_pixels(nexus, node):
    """Pixels of the textures a node uses (0 for a node without texture)."""
    total = 0
    for index in nexus.node_textures(node):
        width, height = nexus.texture_size(index)
        total += width * height
    return total


def level_texels(nexus):
    """Pixels of the node textures, summed per level."""
    texels = [0] * nexus.n_levels
    if nexus.n_textures > 1:
        for node in range(nexus.n_real_nodes):
            texels[int(nexus.level[node])] += texture_pixels(nexus, node)
    return texels


def summarise(nexus):
    nodes = nexus.real_nodes
    return {
        "layout": nexus.layout,
        "nodes": nexus.n_nodes,
        "patches": nexus.n_patches,
        "textures": nexus.n_textures,
        "levels": nexus.n_levels,
        "leaves": int(nexus.leaf.sum()),
        "vertices": int(nexus.nvert),
        "faces": int(nexus.nface),
        "node vertices": int(nodes["nvert"].astype(np.int64).sum()),
        "node faces": int(nodes["nface"].astype(np.int64).sum()),
        "error sum": float(nodes["error"].astype(np.float64).sum()),
        "root error": float(nodes["error"][0]),
        "sphere": [float(v) for v in nexus.sphere],
        "texels": level_texels(nexus),
        "size": nexus.size,
    }


# --- checks -------------------------------------------------------------------------------
class Check:
    def __init__(self, group, name, candidate, reference, ok, note=""):
        self.group, self.name, self.candidate, self.reference, self.ok, self.note = \
            group, name, candidate, reference, ok, note

    def as_dict(self):
        return {"group": self.group, "name": self.name, "candidate": self.candidate,
                "reference": self.reference, "ok": bool(self.ok), "note": self.note}


def widened(values, tolerance, scale=None):
    """[min, max] of the values, widened by tolerance * scale (default: the largest magnitude)."""
    low, high = min(values), max(values)
    margin = tolerance * (max(abs(low), abs(high)) if scale is None else scale)
    return low - margin, high + margin


def fmt(value):
    if isinstance(value, float):
        return "%.6g" % value
    return str(value)


def range_check(group, name, candidate, values, tolerance, scale=None):
    low, high = widened(values, tolerance, scale)
    # A margin of the order of float rounding keeps an equal value inside its own spread.
    slack = 1e-9 * max(1.0, abs(low), abs(high))
    ok = low - slack <= candidate <= high + slack
    reference = fmt(min(values)) if min(values) == max(values) else "%s..%s" % (fmt(min(values)), fmt(max(values)))
    note = "" if tolerance == 0 or ok else "allowed %s..%s" % (fmt(low), fmt(high))
    return Check(group, name, fmt(candidate), reference, ok, note)


def compare_summaries(cand, refs, options):
    checks = []
    for key in ("layout",):
        same = all(ref[key] == cand[key] for ref in refs)
        checks.append(Check("layout", key, "equal" if same else "different", "equal", same,
                            "" if same else json.dumps({"candidate": cand[key], "reference": refs[0][key]})))
    count_tolerance = options.count_tolerance
    if count_tolerance is None:
        count_tolerance = 0.01 if len(refs) == 1 else 0.0
    for key in ("nodes", "patches", "textures", "levels", "leaves"):
        checks.append(range_check("counts", key, cand[key], [r[key] for r in refs], count_tolerance))
    for key in ("vertices", "faces", "node vertices", "node faces", "error sum", "root error"):
        checks.append(range_check("totals", key, cand[key], [r[key] for r in refs], options.total_tolerance))
    radius = max(abs(r["sphere"][3]) for r in refs)
    for index, axis in enumerate("xyz"):
        checks.append(range_check("totals", "sphere centre " + axis, cand["sphere"][index],
                                  [r["sphere"][index] for r in refs], options.total_tolerance, radius))
    checks.append(range_check("totals", "sphere radius", cand["sphere"][3],
                              [r["sphere"][3] for r in refs], options.total_tolerance))
    levels = max([len(cand["texels"])] + [len(r["texels"]) for r in refs])

    def at(values, level):
        return values[level] if level < len(values) else 0

    if any(any(r["texels"]) for r in refs) or any(cand["texels"]):
        for level in range(levels):
            checks.append(range_check("texels", "level %d" % level, at(cand["texels"], level),
                                      [at(r["texels"], level) for r in refs], options.texel_tolerance))
    else:
        checks.append(Check("texels", "texels", "none", "none", True, "no textures in the references or the candidate"))
    checks.append(range_check("size", "file size", cand["size"], [r["size"] for r in refs], options.size_tolerance))
    return checks


# --- pixels -------------------------------------------------------------------------------
def to_array(image):
    return np.asarray(image.convert("RGB"), dtype=np.float64)


def psnr(a, b):
    """Peak signal to noise ratio over all channels, 8 bit range; PSNR_CAP when identical."""
    mse = float(np.mean((a - b) ** 2))
    if mse == 0.0:
        return PSNR_CAP
    return min(PSNR_CAP, 10.0 * math.log10(255.0 * 255.0 / mse))


def luma(rgb):
    return rgb @ np.array([0.299, 0.587, 0.114])


def box_mean(a, size):
    """Mean over every size x size window that lies inside the image (a summed-area table)."""
    c = np.cumsum(np.cumsum(a, axis=0), axis=1)
    c = np.pad(c, ((1, 0), (1, 0)))
    total = c[size:, size:] - c[:-size, size:] - c[size:, :-size] + c[:-size, :-size]
    return total / float(size * size)


def ssim(a, b):
    """Mean structural similarity of the luma of two RGB arrays of one size."""
    x, y = luma(a), luma(b)
    size = min(SSIM_WINDOW, x.shape[0], x.shape[1])
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    mx, my = box_mean(x, size), box_mean(y, size)
    n = float(size * size)
    correction = n / (n - 1.0) if n > 1 else 1.0   # sample (unbiased) variance, as in the reference code
    vx = (box_mean(x * x, size) - mx * mx) * correction
    vy = (box_mean(y * y, size) - my * my) * correction
    cxy = (box_mean(x * y, size) - mx * my) * correction
    score = ((2 * mx * my + c1) * (2 * cxy + c2)) / ((mx * mx + my * my + c1) * (vx + vy + c2))
    return float(score.mean())


def sample_nodes(nexus, count, seed):
    """All the textured nodes when they are few, else a seeded sample with every level in it."""
    nodes = [n for n in range(nexus.n_real_nodes) if nexus.node_textures(n)]
    if len(nodes) <= count:
        return nodes
    rng = random.Random(seed)
    chosen = set()
    by_level = {}
    for n in nodes:
        by_level.setdefault(int(nexus.level[n]), []).append(n)
    for level in sorted(by_level):
        chosen.add(rng.choice(by_level[level]))
    rest = [n for n in nodes if n not in chosen]
    chosen.update(rng.sample(rest, count - len(chosen)))
    return sorted(chosen)


def match_by_place(ref, cand, nodes, tolerance):
    """For each reference node, the candidate node of the same level with the nearest sphere
    centre, or None when that centre is farther than `tolerance` (a fraction of the root
    radius) from the reference's."""
    limit = tolerance * float(ref.sphere[3])
    matches = {}
    candidate_centres = {}
    for level in np.unique(cand.level):
        members = np.nonzero(cand.level == level)[0]
        candidate_centres[int(level)] = (members, cand.real_nodes["center"][members].astype(np.float64))
    for node in nodes:
        level = int(ref.level[node])
        if level not in candidate_centres:
            matches[node] = None
            continue
        members, centres = candidate_centres[level]
        distance = np.linalg.norm(centres - ref.real_nodes["center"][node].astype(np.float64), axis=1)
        best = int(np.argmin(distance))
        matches[node] = int(members[best]) if distance[best] <= limit else None
    return matches


def pixel_checks(ref, cand, options):
    if ref.n_textures <= 1 and cand.n_textures <= 1:
        return [Check("pixels", "textures", "none", "none", True, "no textures in the reference or the candidate")], {}
    nodes = sample_nodes(ref, options.samples, options.seed)
    matches = match_by_place(ref, cand, nodes, options.place_tolerance)
    scores = []
    unmatched = untextured = resized = 0
    for node in nodes:
        other = matches[node]
        if other is None:
            unmatched += 1
            continue
        cand_textures = cand.node_textures(other)
        if not cand_textures:
            untextured += 1
            continue
        try:
            with ref.texture_image(ref.node_textures(node)[0]) as ra, cand.texture_image(cand_textures[0]) as ca:
                ra.load()
                ca.load()
                if ra.size != ca.size:
                    resized += 1
                    ca = ca.resize(ra.size)
                a, b = to_array(ra), to_array(ca)
        except (OSError, ValueError, SyntaxError):
            # A candidate texture that does not decode is a failed comparison, not a crash.
            scores.append((0.0, 0.0, node, other))
            continue
        scores.append((psnr(a, b), ssim(a, b), node, other))
    total = len(nodes)
    unmatched_fraction = (unmatched + untextured) / float(total) if total else 0.0
    checks = [Check("pixels", "nodes compared", str(len(scores)), "sample of %d" % total,
                    unmatched_fraction <= options.max_unmatched and len(scores) > 0,
                    "%d unmatched, %d without texture, %d resized" % (unmatched, untextured, resized))]
    detail = {"sampled": total, "unmatched": unmatched, "untextured": untextured, "resized": resized}
    if scores:
        mean_psnr = sum(s[0] for s in scores) / len(scores)
        mean_ssim = sum(s[1] for s in scores) / len(scores)
        worst_psnr = min(scores, key=lambda s: s[0])
        worst_ssim = min(scores, key=lambda s: s[1])
        low_psnr, low_ssim = worst_psnr[0], worst_ssim[1]
        checks.append(Check("pixels", "PSNR (min)", "%.2f dB" % low_psnr, ">= %g dB" % options.min_psnr,
                            low_psnr >= options.min_psnr,
                            "mean %.2f dB; worst: reference node %d, candidate node %d" % (mean_psnr, worst_psnr[2], worst_psnr[3])))
        checks.append(Check("pixels", "SSIM (min)", "%.4f" % low_ssim, ">= %g" % options.min_ssim,
                            low_ssim >= options.min_ssim,
                            "mean %.4f; worst: reference node %d, candidate node %d" % (mean_ssim, worst_ssim[2], worst_ssim[3])))
        detail.update({"psnr min": low_psnr, "psnr mean": mean_psnr, "ssim min": low_ssim, "ssim mean": mean_ssim})
    return checks, detail


# --- report -------------------------------------------------------------------------------
def render(checks):
    rows = [("group", "check", "candidate", "reference", "result")]
    for c in checks:
        rows.append((c.group, c.name, c.candidate, c.reference, ("ok" if c.ok else "FAIL") + (" (" + c.note + ")" if c.note else "")))
    widths = [max(len(row[i]) for row in rows) for i in range(4)]
    lines = []
    for index, row in enumerate(rows):
        lines.append("  ".join(row[i].ljust(widths[i]) for i in range(4)) + "  " + row[4])
        if index == 0:
            lines.append("  ".join("-" * widths[i] for i in range(4)) + "  ------")
    return "\n".join(lines)


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--ref", action="append", required=True, metavar="FILE", help="reference .nxz/.nxs (repeatable)")
    parser.add_argument("--cand", required=True, metavar="FILE", help="candidate .nxz/.nxs")
    parser.add_argument("--json", metavar="FILE", help="also write the checks as JSON")
    parser.add_argument("--count-tolerance", type=float, default=None,
                        help="relative widening of the reference spread of the counts (default 0; 0.01 with one reference)")
    parser.add_argument("--total-tolerance", type=float, default=0.01)
    parser.add_argument("--texel-tolerance", type=float, default=0.02)
    parser.add_argument("--size-tolerance", type=float, default=0.03)
    parser.add_argument("--min-psnr", type=float, default=40.0)
    parser.add_argument("--min-ssim", type=float, default=0.98)
    parser.add_argument("--samples", type=int, default=200, help="nodes whose textures are compared (default 200)")
    parser.add_argument("--seed", type=int, default=1, help="seed of the node sample")
    parser.add_argument("--place-tolerance", type=float, default=0.01,
                        help="largest distance between matched sphere centres, as a fraction of the root radius")
    parser.add_argument("--max-unmatched", type=float, default=0.0,
                        help="fraction of the sampled nodes allowed to find no counterpart (default 0)")
    return parser


def main(argv=None):
    options = build_parser().parse_args(argv)
    try:
        refs = [NexusFile(path) for path in options.ref]
        cand = NexusFile(options.cand)
        checks = compare_summaries(summarise(cand), [summarise(r) for r in refs], options)
        pixels, detail = pixel_checks(refs[0], cand, options)
        checks.extend(pixels)
    except (OSError, NexusFormatError) as error:
        print("compare.py: %s" % error, file=sys.stderr)
        return 2
    print(render(checks))
    passed = all(c.ok for c in checks)
    print("\n%s: %d of %d checks passed (%d reference file%s)"
          % ("PASS" if passed else "FAIL", sum(c.ok for c in checks), len(checks), len(refs), "" if len(refs) == 1 else "s"))
    if options.json:
        with open(options.json, "w", encoding="utf-8") as handle:
            json.dump({"passed": passed, "references": options.ref, "candidate": options.cand,
                       "pixels": detail, "checks": [c.as_dict() for c in checks]}, handle, indent=2)
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
