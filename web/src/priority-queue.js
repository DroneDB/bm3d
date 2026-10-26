// SPDX-License-Identifier: MIT
//
// Derived from html/js/nexus.js of Nexus (https://github.com/cnr-isti-vclab/nexus, commit
// d805432), modified by Digipa for bm3d-viewer.
// Nexus: Copyright (c) 2012-2020, Visual Computing Lab, ISTI - CNR. All rights reserved.
// MIT licence; the licence text is in the LICENSE file of this package.

/**
 * A max-heap of node numbers ordered by their error (the node with the largest error comes out
 * first); its capacity is fixed.
 */
export class PriorityQueue {
	constructor(maxLength) {
		this.error = new Float32Array(maxLength);
		this.data = new Int32Array(maxLength);
		this.size = 0;
	}

	push(data, error) {
		this.data[this.size] = data;
		this.error[this.size] = error;
		this.bubbleUp(this.size);
		this.size++;
	}

	pop() {
		const result = this.data[0];
		this.size--;
		if (this.size > 0) {
			this.data[0] = this.data[this.size];
			this.error[0] = this.error[this.size];
			this.sinkDown(0);
		}
		return result;
	}

	bubbleUp(n) {
		const data = this.data[n];
		const error = this.error[n];
		while (n > 0) {
			const parent = ((n + 1) >> 1) - 1;
			const parentError = this.error[parent];
			if (parentError > error) break;
			this.data[n] = this.data[parent];
			this.error[n] = parentError;
			this.data[parent] = data;
			this.error[parent] = error;
			n = parent;
		}
	}

	sinkDown(n) {
		const data = this.data[n];
		const error = this.error[n];
		for (;;) {
			const child2 = (n + 1) * 2;
			const child1 = child2 - 1;
			let swap = -1;
			let child1Error = 0;
			if (child1 < this.size) {
				child1Error = this.error[child1];
				if (child1Error > error) swap = child1;
			}
			if (child2 < this.size && this.error[child2] > (swap === -1 ? error : child1Error)) swap = child2;
			if (swap === -1) break;
			this.data[n] = this.data[swap];
			this.error[n] = this.error[swap];
			this.data[swap] = data;
			this.error[swap] = error;
			n = swap;
		}
	}
}
