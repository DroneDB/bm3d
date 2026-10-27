#!/usr/bin/env python3
# Copyright (C) 2026 Digipa
# SPDX-License-Identifier: GPL-3.0-or-later
"""Structural parity between the models built by a reference program and by a candidate.

  compare.py --ref a.nxz [--ref b.nxz ...] --cand c.bm3d [--json out.json]

The references are format 0 files (.nxs / .nxz, the container Nexus wrote, read by format0.py) or
.bm3d files; so is the candidate (dump.py reads those). The comparison is of the model, not of
the container, so that a candidate in the new format is held to the references the Nexus builder
made: the layout is what the vertices carry, the totals are the ones of the header and of the
nodes, and the size is the size of the file.

Exit status 0 when the candidate matches, 1 when it does not (a table says which checks), 2 for
a file that cannot be read.

The references are several builds of the same model by the reference program: their spread is
what "the same" means, because a build is not bit-for-bit repeatable. A candidate value passes
when it lies in [min, max] of the references widened by a relative tolerance. What is compared:

  layout        whether the vertices carry normals, colours and texture coordinates: equal
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

Order-independent mode (--order-independent; the default when the candidate is a .bm3d file and
every reference is a format 0 file; --exact-order asks for the comparison above). The references
made the collapses of equal cost, the triangles of equal keys and the pieces of equal size in the
order of their library, and bm3d does them in an order of its own that is the same on every
platform, so the models are the same model with its nodes cut, its levels filled and its pieces placed
a little differently. What is compared is what that moves little, with the tolerances the builds of
the six reference models needed (the largest deviation measured, in the second column, is from the
build of 2026-10-27 against the references of 2026-10; each tolerance widens the spread of the
references, as above):

  check                                       tolerance   measured at most
  levels, layout                              equal
  nodes, patches, textures, leaves            1 %         0.67 % (patches of brighton.ply)
  vertices, faces (header, node sums)         1 %         0.07 %
  sum of all the node errors                  3 %         2.06 % (moulin)
  sum of the errors of each level             10 %        8.0 % (OfficePlan, level 5)
  error of the root, radius of the sphere     5 %         4.13 % (moulin)
  centre of the sphere, in radii              30 %        21.8 % (OfficePlan, z)
  texels of all the levels                    2 %         0.11 %
  texels of each level                        25 %        22.6 % (OfficePlan, level 3)
  file size                                   3 %         0.94 % (but brighton_beach.obj: 35 %,
                                                          it keeps the colours of its vertices)

A level holds the nodes at one depth, and a node that moves one level takes its error and its texels
with it: the sums of each level move more than the sums of all of them. The sphere of the header is
approximated by adding the vertices of the root one by one, so its centre and radius depend on their
order. The structural similarity is not compared.

  pixels        the colours of the texture without their places, for the nodes matched by place as
                above (a candidate node is the one of the same level whose sphere centre is nearest
                to the reference node's, within 10 % of the root radius: --order-place-tolerance;
                2 % of the sampled nodes may find none): the pixels of each channel sorted (the
                histogram), the two lists brought to one length by taking the same quantiles of both
                when the textures differ in size, and the PSNR of the sorted lists >= 40 dB
                (--min-sorted-psnr; 100 dB when they hold the same colours in the same numbers).
                2 % of the compared nodes may be below (--order-max-low): a node that was cut
                otherwise has another content. Measured: 0 to 3 of about 200 nodes below, the
                mean 74 to 98 dB; the lowest 22.6 dB (OfficePlan, whose node 21 has the nearest
                centre among the nodes of its level and the wrong content).
"""
import argparse
import json
import math
import random
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dump import Bm3dFile, open_model  # noqa: E402
from modelfile import ModelFileError  # noqa: E402

PSNR_CAP = 100.0
SSIM_WINDOW = 8


# --- statistics of one file ---------------------------------------------------------------
def texture_pixels(model, node):
    """Pixels of the textures a node uses (0 for a node without texture)."""
    total = 0
    for index in model.node_textures(node):
        width, height = model.texture_size(index)
        total += width * height
    return total


def level_texels(model):
    """Pixels of the node textures, summed per level."""
    texels = [0] * model.n_levels
    if model.n_real_textures > 0:
        for node in range(model.n_real_nodes):
            texels[int(model.level[node])] += texture_pixels(model, node)
    return texels


def level_errors(model):
    """The errors of the nodes, summed per level."""
    count = model.n_real_nodes
    return [float(v) for v in np.bincount(model.level[:count].astype(np.int64),
                                          weights=model.real_nodes["error"].astype(np.float64), minlength=model.n_levels)]


def summarise(model):
    nodes = model.real_nodes
    return {
        "layout": model.layout,
        "nodes": model.n_nodes,
        "patches": model.n_patches,
        "textures": model.n_real_textures,
        "levels": model.n_levels,
        "leaves": int(model.leaf.sum()),
        "vertices": int(model.nvert),
        "faces": int(model.nface),
        "node vertices": int(nodes["nvert"].astype(np.int64).sum()),
        "node faces": int(nodes["nface"].astype(np.int64).sum()),
        "error sum": float(nodes["error"].astype(np.float64).sum()),
        "level errors": level_errors(model),
        "root error": float(nodes["error"][0]),
        "sphere": [float(v) for v in model.sphere],
        "texels": level_texels(model),
        "size": model.size,
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
    ordered = not options.order_independent
    count_tolerance = options.count_tolerance
    if count_tolerance is None:
        count_tolerance = (0.01 if len(refs) == 1 else 0.0) if ordered else options.order_count_tolerance
    count_keys = ("nodes", "patches", "textures", "levels", "leaves")
    for key in count_keys:
        # the levels of a model are the same however its nodes were cut
        tolerance = 0.0 if key == "levels" and not ordered else count_tolerance
        checks.append(range_check("counts", key, cand[key], [r[key] for r in refs], tolerance))
    total_keys = ("vertices", "faces", "node vertices", "node faces") + (("error sum", "root error") if ordered else ())
    for key in total_keys:
        checks.append(range_check("totals", key, cand[key], [r[key] for r in refs], options.total_tolerance))
    if not ordered:
        checks.append(range_check("errors", "sum of all levels", cand["error sum"], [r["error sum"] for r in refs],
                                  options.order_error_total_tolerance))
        checks.append(range_check("errors", "root", cand["root error"], [r["root error"] for r in refs],
                                  options.order_sphere_tolerance))
        levels = max([len(cand["level errors"])] + [len(r["level errors"]) for r in refs])
        for level in range(levels):
            checks.append(range_check("errors", "level %d" % level, at(cand["level errors"], level),
                                      [at(r["level errors"], level) for r in refs], options.order_error_tolerance))
    radius = max(abs(r["sphere"][3]) for r in refs)
    centre_tolerance = options.total_tolerance if ordered else options.order_centre_tolerance
    radius_tolerance = options.total_tolerance if ordered else options.order_sphere_tolerance
    for index, axis in enumerate("xyz"):
        checks.append(range_check("totals", "sphere centre " + axis, cand["sphere"][index],
                                  [r["sphere"][index] for r in refs], centre_tolerance, radius))
    checks.append(range_check("totals", "sphere radius", cand["sphere"][3],
                              [r["sphere"][3] for r in refs], radius_tolerance))
    levels = max([len(cand["texels"])] + [len(r["texels"]) for r in refs])

    if any(any(r["texels"]) for r in refs) or any(cand["texels"]):
        if not ordered:
            checks.append(range_check("texels", "all levels", sum(cand["texels"]), [sum(r["texels"]) for r in refs],
                                      options.texel_tolerance))
        for level in range(levels):
            checks.append(range_check("texels", "level %d" % level, at(cand["texels"], level),
                                      [at(r["texels"], level) for r in refs],
                                      options.texel_tolerance if ordered else options.order_texel_tolerance))
    else:
        checks.append(Check("texels", "texels", "none", "none", True, "no textures in the references or the candidate"))
    checks.append(range_check("size", "file size", cand["size"], [r["size"] for r in refs], options.size_tolerance))
    return checks


def at(values, level):
    return values[level] if level < len(values) else 0


# --- pixels -------------------------------------------------------------------------------
def to_array(image):
    return np.asarray(image.convert("RGB"), dtype=np.float64)


def psnr(a, b):
    """Peak signal to noise ratio over all channels, 8 bit range; PSNR_CAP when identical."""
    mse = float(np.mean((a - b) ** 2))
    if mse == 0.0:
        return PSNR_CAP
    return min(PSNR_CAP, 10.0 * math.log10(255.0 * 255.0 / mse))


def sorted_channels(a, length):
    """Each channel of an H x W x 3 array with its pixels sorted, as `length` values (the same
    quantiles of the sorted list when the array has more or fewer pixels)."""
    flat = a.reshape(-1, a.shape[-1])
    columns = []
    for channel in range(flat.shape[1]):
        values = np.sort(flat[:, channel])
        if values.size != length:
            values = np.interp(np.linspace(0.0, values.size - 1, length), np.arange(values.size), values)
        columns.append(values)
    return np.stack(columns, axis=1)


def sorted_psnr(a, b):
    """PSNR between the sorted pixels of each channel of two pictures: whether they hold the
    same colours in the same numbers wherever they put them."""
    length = min(a.shape[0] * a.shape[1], b.shape[0] * b.shape[1])
    return psnr(sorted_channels(a, length), sorted_channels(b, length))


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


def sample_nodes(model, count, seed):
    """All the textured nodes when they are few, else a seeded sample with every level in it."""
    nodes = [n for n in range(model.n_real_nodes) if model.node_textures(n)]
    if len(nodes) <= count:
        return nodes
    rng = random.Random(seed)
    chosen = set()
    by_level = {}
    for n in nodes:
        by_level.setdefault(int(model.level[n]), []).append(n)
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
    if ref.n_real_textures == 0 and cand.n_real_textures == 0:
        return [Check("pixels", "textures", "none", "none", True, "no textures in the reference or the candidate")], {}
    nodes = sample_nodes(ref, options.samples, options.seed)
    matches = match_by_place(ref, cand, nodes, options.order_place_tolerance if options.order_independent else options.place_tolerance)
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
                    if not options.order_independent:
                        ca = ca.resize(ra.size)
                a, b = to_array(ra), to_array(ca)
        except (OSError, ValueError, SyntaxError):
            # A candidate texture that does not decode is a failed comparison, not a crash.
            scores.append((0.0, 0.0, node, other))
            continue
        if options.order_independent:
            scores.append((sorted_psnr(a, b), 1.0, node, other))
        else:
            scores.append((psnr(a, b), ssim(a, b), node, other))
    total = len(nodes)
    unmatched_fraction = (unmatched + untextured) / float(total) if total else 0.0
    checks = [Check("pixels", "nodes compared", str(len(scores)), "sample of %d" % total,
                    unmatched_fraction <= (options.order_max_unmatched if options.order_independent else options.max_unmatched) and len(scores) > 0,
                    "%d unmatched, %d without texture, %d resized" % (unmatched, untextured, resized))]
    detail = {"sampled": total, "unmatched": unmatched, "untextured": untextured, "resized": resized}
    if scores:
        mean_psnr = sum(s[0] for s in scores) / len(scores)
        mean_ssim = sum(s[1] for s in scores) / len(scores)
        worst_psnr = min(scores, key=lambda s: s[0])
        worst_ssim = min(scores, key=lambda s: s[1])
        low_psnr, low_ssim = worst_psnr[0], worst_ssim[1]
        if options.order_independent:
            low = sum(1 for score in scores if score[0] < options.min_sorted_psnr)
            checks.append(Check("pixels", "sorted-pixel PSNR", "%d of %d below %g dB" % (low, len(scores), options.min_sorted_psnr),
                                "at most %g%%" % (100 * options.order_max_low),
                                low <= options.order_max_low * len(scores),
                                "min %.2f dB, mean %.2f dB; worst: reference node %d, candidate node %d" % (low_psnr, mean_psnr, worst_psnr[2], worst_psnr[3])))
        else:
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
    parser.add_argument("--ref", action="append", required=True, metavar="FILE", help="reference .nxz/.nxs/.bm3d (repeatable)")
    parser.add_argument("--cand", required=True, metavar="FILE", help="candidate .nxz/.nxs/.bm3d")
    parser.add_argument("--json", metavar="FILE", help="also write the checks as JSON")
    parser.add_argument("--count-tolerance", type=float, default=None,
                        help="relative widening of the reference spread of the counts (default 0; 0.01 with one reference)")
    parser.add_argument("--total-tolerance", type=float, default=0.01)
    parser.add_argument("--texel-tolerance", type=float, default=0.02)
    parser.add_argument("--size-tolerance", type=float, default=0.03)
    parser.add_argument("--order-independent", dest="order_independent", action="store_true", default=None,
                        help="compare what does not depend on the order of equal keys (the default when the candidate is "
                             "a .bm3d file and every reference a format 0 file)")
    parser.add_argument("--exact-order", dest="order_independent", action="store_false",
                        help="compare as models made in the same order (the default otherwise)")
    parser.add_argument("--order-count-tolerance", type=float, default=0.01,
                        help="order-independent mode: widening of the counts of nodes, patches, textures and leaves (1%%)")
    parser.add_argument("--order-error-tolerance", type=float, default=0.10,
                        help="order-independent mode: widening of the sum of the errors of each level (10%%)")
    parser.add_argument("--order-error-total-tolerance", type=float, default=0.03,
                        help="order-independent mode: widening of the sum of the errors of all the nodes (3%%)")
    parser.add_argument("--order-sphere-tolerance", type=float, default=0.05,
                        help="order-independent mode: widening of the radius of the bounding sphere and of the error of the root (5%%)")
    parser.add_argument("--order-centre-tolerance", type=float, default=0.30,
                        help="order-independent mode: widening of the centre of the bounding sphere, as a fraction of its radius (30%%)")
    parser.add_argument("--order-texel-tolerance", type=float, default=0.25,
                        help="order-independent mode: widening of the texels of each level (25%%)")
    parser.add_argument("--order-place-tolerance", type=float, default=0.10,
                        help="order-independent mode: largest distance between matched sphere centres, as a fraction of the root radius (10%%)")
    parser.add_argument("--min-sorted-psnr", type=float, default=40.0,
                        help="order-independent mode: PSNR of the sorted pixels of matched textures (40 dB)")
    parser.add_argument("--order-max-low", type=float, default=0.02,
                        help="order-independent mode: fraction of the compared nodes allowed below --min-sorted-psnr (2%%)")
    parser.add_argument("--order-max-unmatched", type=float, default=0.02,
                        help="order-independent mode: fraction of the sampled nodes allowed to find no counterpart (2%%)")
    parser.add_argument("--min-psnr", type=float, default=40.0)
    parser.add_argument("--min-ssim", type=float, default=0.98)
    parser.add_argument("--samples", type=int, default=200, help="nodes whose textures are compared (default 200)")
    parser.add_argument("--seed", type=int, default=1, help="seed of the node sample")
    parser.add_argument("--place-tolerance", type=float, default=0.01,
                        help="largest distance between matched sphere centres, as a fraction of the root radius")
    parser.add_argument("--max-unmatched", type=float, default=0.0,
                        help="fraction of the sampled nodes allowed to find no counterpart (default 0)")
    return parser


def default_order_independent(cand, refs):
    """A .bm3d candidate against references of format 0 (the order of equal keys of their library
    is not the one of bm3d) is compared without the order; two files of one format, as models made
    in one order."""
    return isinstance(cand, Bm3dFile) and not any(isinstance(r, Bm3dFile) for r in refs)


def main(argv=None):
    options = build_parser().parse_args(argv)
    try:
        refs = [open_model(path) for path in options.ref]
        cand = open_model(options.cand)
        if options.order_independent is None:
            options.order_independent = default_order_independent(cand, refs)
        checks = compare_summaries(summarise(cand), [summarise(r) for r in refs], options)
        pixels, detail = pixel_checks(refs[0], cand, options)
        checks.extend(pixels)
    except (OSError, ModelFileError) as error:
        print("compare.py: %s" % error, file=sys.stderr)
        return 2
    print("mode: %s" % ("order-independent" if options.order_independent else "exact order"))
    print(render(checks))
    passed = all(c.ok for c in checks)
    print("\n%s: %d of %d checks passed (%d reference file%s)"
          % ("PASS" if passed else "FAIL", sum(c.ok for c in checks), len(checks), len(refs), "" if len(refs) == 1 else "s"))
    if options.json:
        with open(options.json, "w", encoding="utf-8") as handle:
            json.dump({"passed": passed, "order independent": bool(options.order_independent), "references": options.ref, "candidate": options.cand,
                       "pixels": detail, "checks": [c.as_dict() for c in checks]}, handle, indent=2)
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
