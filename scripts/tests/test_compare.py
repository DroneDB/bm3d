#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests of scripts/compare.py: the comparison that does not depend on the order of equal keys
(the sorted pixels of a texture, the choice of the mode) and the exact one on the fixtures of the viewer.

  python3 -m unittest discover -s scripts/tests
"""
import contextlib
import io
import os
import sys
import unittest

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE))
import compare  # noqa: E402
import dump  # noqa: E402

TERRAIN = os.path.join(ROOT, "web", "tests", "fixtures", "terrain.bm3d")


def picture(width, height, seed=3):
    return np.random.RandomState(seed).randint(0, 256, size=(height, width, 3)).astype(np.float64)


class SortedPixels(unittest.TestCase):
    def test_the_same_pixels_in_another_place_are_the_same(self):
        a = picture(32, 32)
        b = a.reshape(-1, 3)[np.random.RandomState(9).permutation(32 * 32)].reshape(32, 32, 3)
        self.assertLess(compare.psnr(a, b), 20)          # place by place they differ a lot
        self.assertEqual(compare.sorted_psnr(a, b), compare.PSNR_CAP)

    def test_other_colours_are_not(self):
        a = picture(32, 32)
        b = np.clip(a + 40, 0, 255)
        self.assertLess(compare.sorted_psnr(a, b), 30)

    def test_a_picture_of_another_size_is_compared_by_quantiles(self):
        a = picture(32, 32)
        doubled = np.repeat(np.repeat(a, 2, axis=0), 2, axis=1)    # every pixel four times
        self.assertGreater(compare.sorted_psnr(a, doubled), 60)

    def test_a_colour_that_one_has_and_the_other_has_not(self):
        a = np.zeros((16, 16, 3))
        b = a.copy()
        b[:8] = 255
        self.assertLess(compare.sorted_psnr(a, b), 10)


class Mode(unittest.TestCase):
    def test_a_bm3d_candidate_against_references_of_another_format_ignores_the_order(self):
        cand = dump.Bm3dFile(TERRAIN)
        self.assertTrue(compare.default_order_independent(cand, [object()]))
        self.assertFalse(compare.default_order_independent(cand, [cand]))
        self.assertFalse(compare.default_order_independent(object(), [object()]))

    def run_compare(self, *arguments):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            status = compare.main(["--ref", TERRAIN, "--cand", TERRAIN, *arguments])
        return status, out.getvalue()

    def test_a_file_against_itself_passes_in_both_modes(self):
        status, text = self.run_compare()
        self.assertEqual(status, 0, text)
        self.assertIn("mode: exact order", text)
        status, text = self.run_compare("--order-independent")
        self.assertEqual(status, 0, text)
        self.assertIn("mode: order-independent", text)
        self.assertIn("sorted-pixel PSNR", text)
        self.assertIn("errors", text)

    def test_the_tolerances_of_the_mode_are_the_documented_ones(self):
        options = compare.build_parser().parse_args(["--ref", "a", "--cand", "b"])
        self.assertEqual(options.order_count_tolerance, 0.01)
        self.assertEqual(options.order_error_tolerance, 0.10)
        self.assertEqual(options.order_error_total_tolerance, 0.03)
        self.assertEqual(options.order_sphere_tolerance, 0.05)
        self.assertEqual(options.order_centre_tolerance, 0.30)
        self.assertEqual(options.order_texel_tolerance, 0.25)
        self.assertEqual(options.order_place_tolerance, 0.10)
        self.assertEqual(options.min_sorted_psnr, 40.0)
        self.assertEqual(options.order_max_low, 0.02)
        self.assertEqual(options.order_max_unmatched, 0.02)
        self.assertIsNone(options.order_independent)

    def test_a_sphere_that_moved_a_fifth_of_its_radius_passes_without_the_order_and_not_with_it(self):
        base = compare.summarise(dump.Bm3dFile(TERRAIN))
        radius = base["sphere"][3]
        moved = dict(base, sphere=[base["sphere"][0] + 0.2 * radius] + base["sphere"][1:])
        for order_independent, expected in ((True, True), (False, False)):
            options = compare.build_parser().parse_args(["--ref", "a", "--cand", "b"])
            options.order_independent = order_independent
            checks = {c.name: c for c in compare.compare_summaries(moved, [base], options)}
            self.assertEqual(checks["sphere centre x"].ok, expected, order_independent)
            self.assertTrue(checks["sphere radius"].ok)

    def test_a_count_over_the_tolerance_fails(self):
        base = compare.summarise(dump.Bm3dFile(TERRAIN))
        shifted = dict(base, nodes=base["nodes"] + max(2, int(base["nodes"] * 0.05)))
        options = compare.build_parser().parse_args(["--ref", "a", "--cand", "b", "--order-independent"])
        options.order_independent = True
        checks = {c.name: c for c in compare.compare_summaries(shifted, [base], options)}
        self.assertFalse(checks["nodes"].ok)
        self.assertTrue(checks["levels"].ok)


if __name__ == "__main__":
    unittest.main()
