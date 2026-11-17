// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa

import { describe, expect, it } from 'vitest';

import { principalAxes } from '../src/shape.js';

/** The points of a grid of n x n over the square of side `side`, put into space by `place(u, v)`. */
function grid(n, side, place) {
	const positions = [];
	for (let i = 0; i < n; i++) for (let j = 0; j < n; j++) positions.push(...place((i / (n - 1) - 0.5) * side, (j / (n - 1) - 0.5) * side));
	return positions;
}

const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];

describe('principalAxes', () => {
	it('finds the normal of a flat model in the plane of two axes: no thickness along it', () => {
		// one quad in the XZ plane, as the PLY cases of Blender
		const shape = principalAxes(grid(5, 2, (u, v) => [u + 3, 7, v - 1]));
		expect(Math.abs(shape.axes[2][1])).toBeCloseTo(1, 6);
		expect(shape.extents[2]).toBeCloseTo(0, 6);
		expect(shape.extents[0]).toBeGreaterThan(0.5);
		expect(shape.center).toEqual([3, 7, -1].map((x) => expect.closeTo(x, 6)));
	});

	it('finds the normal of a plane in no axis, with the axes orthonormal', () => {
		const n = [1 / Math.sqrt(3), 1 / Math.sqrt(3), 1 / Math.sqrt(3)];
		const a = [1 / Math.sqrt(2), -1 / Math.sqrt(2), 0];
		const b = [n[1] * a[2] - n[2] * a[1], n[2] * a[0] - n[0] * a[2], n[0] * a[1] - n[1] * a[0]];
		const shape = principalAxes(grid(9, 4, (u, v) => [0, 1, 2].map((k) => u * a[k] + 2 * v * b[k])));
		expect(Math.abs(dot(shape.axes[2], n))).toBeCloseTo(1, 6);
		expect(shape.extents[2]).toBeLessThan(1e-6);
		expect(shape.extents[0]).toBeGreaterThan(shape.extents[1]);
		for (let i = 0; i < 3; i++)
			for (let j = 0; j < 3; j++) expect(dot(shape.axes[i], shape.axes[j])).toBeCloseTo(i === j ? 1 : 0, 6);
	});

	it('sees the thickness of a model that is not flat, and nothing in fewer than 3 points', () => {
		const shape = principalAxes(grid(5, 2, (u, v) => [u, v, u * u + v * v]));
		expect(shape.extents[2]).toBeGreaterThan(0.05 * shape.extents[0]);
		expect(principalAxes([0, 0, 0, 1, 1, 1])).toBeNull();
	});
});
