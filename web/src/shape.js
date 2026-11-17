// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa

// The shape of a set of points, for an application that frames a model: its principal axes. A flat
// model (one quad, a planar drawing, a terrain patch) has one axis along which it is almost no
// thickness, and a camera looking along its plane sees nothing.

/**
 * The eigenvalues and eigenvectors of a symmetric 3 x 3 matrix (row-major, 9 numbers), by Jacobi
 * rotations: [{ value, vector: [x, y, z] }] from the largest value to the smallest.
 */
function symmetricEigen(m) {
	const a = Array.from(m);
	const v = [1, 0, 0, 0, 1, 0, 0, 0, 1];
	for (let sweep = 0; sweep < 50; sweep++) {
		const off = a[1] * a[1] + a[2] * a[2] + a[5] * a[5];
		if (off < 1e-30 * (a[0] * a[0] + a[4] * a[4] + a[8] * a[8]) || off === 0) break;
		for (const [p, q] of [[0, 1], [0, 2], [1, 2]]) {
			const apq = a[p * 3 + q];
			if (apq === 0) continue;
			const theta = (a[q * 3 + q] - a[p * 3 + p]) / (2 * apq);
			const t = Math.sign(theta || 1) / (Math.abs(theta) + Math.sqrt(theta * theta + 1));
			const c = 1 / Math.sqrt(t * t + 1);
			const s = t * c;
			// a = J^T a J and v = v J, J the rotation in the plane (p, q)
			for (let k = 0; k < 3; k++) {
				const akp = a[k * 3 + p];
				const akq = a[k * 3 + q];
				a[k * 3 + p] = c * akp - s * akq;
				a[k * 3 + q] = s * akp + c * akq;
			}
			for (let k = 0; k < 3; k++) {
				const apk = a[p * 3 + k];
				const aqk = a[q * 3 + k];
				a[p * 3 + k] = c * apk - s * aqk;
				a[q * 3 + k] = s * apk + c * aqk;
			}
			for (let k = 0; k < 3; k++) {
				const vkp = v[k * 3 + p];
				const vkq = v[k * 3 + q];
				v[k * 3 + p] = c * vkp - s * vkq;
				v[k * 3 + q] = s * vkp + c * vkq;
			}
		}
	}
	return [0, 1, 2]
		.map((k) => ({ value: a[k * 4], vector: [v[k], v[3 + k], v[6 + k]] }))
		.sort((x, y) => y.value - x.value);
}

/**
 * The principal axes of the points `positions` (x, y, z after each other): their centre, the unit
 * directions of the axes from the longest to the shortest, and the extent along each (the standard
 * deviation of the points along it). null for fewer than 3 points.
 * @param {ArrayLike<number>} positions
 * @returns {{ center: number[], axes: number[][], extents: number[] } | null}
 */
export function principalAxes(positions) {
	const n = Math.floor(positions.length / 3);
	if (n < 3) return null;
	const center = [0, 0, 0];
	for (let i = 0; i < n * 3; i++) center[i % 3] += positions[i] / n;
	const c = [0, 0, 0, 0, 0, 0, 0, 0, 0];
	for (let i = 0; i < n; i++) {
		const d = [positions[i * 3] - center[0], positions[i * 3 + 1] - center[1], positions[i * 3 + 2] - center[2]];
		for (let r = 0; r < 3; r++) for (let k = 0; k < 3; k++) c[r * 3 + k] += (d[r] * d[k]) / n;
	}
	const eigen = symmetricEigen(c);
	return {
		center,
		axes: eigen.map((e) => {
			const length = Math.hypot(...e.vector) || 1;
			return e.vector.map((x) => x / length);
		}),
		extents: eigen.map((e) => Math.sqrt(Math.max(0, e.value))),
	};
}
