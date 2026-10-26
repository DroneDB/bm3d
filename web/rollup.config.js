// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa
//
// Builds dist/: the viewer as an ES module (plain and minified), the decoder's worker, and
// corto.wasm (built by corto-wasm/build.sh), which sits next to the worker because the worker
// fetches it from its own folder.

import { copyFileSync, existsSync, mkdirSync } from 'node:fs';
import terser from '@rollup/plugin-terser';

const WASM = 'corto-wasm/out/corto.wasm';

const banner = `/*! bm3d-viewer - SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Digipa.
 * Contains code derived from Nexus (Copyright (c) 2012-2020, Visual Computing Lab, ISTI - CNR)
 * and corto (Copyright (c) 2015-2022, Visual Computing Lab, ISTI - CNR), both under the MIT
 * licence: see the LICENSE and NOTICE files of the package. */`;

/** Copies corto.wasm to dist/; it is a build product, so a missing one is an error. */
function copyWasm() {
	return {
		name: 'copy-wasm',
		writeBundle() {
			if (!existsSync(WASM)) {
				throw new Error(`${WASM} not found: run corto-wasm/build.sh (or corto-wasm/docker-build.sh) first`);
			}
			mkdirSync('dist', { recursive: true });
			copyFileSync(WASM, 'dist/corto.wasm');
		},
	};
}

export default [
	{
		input: 'src/index.js',
		external: ['three'],
		output: [
			{ file: 'dist/bm3d-viewer.js', format: 'es', banner },
			{ file: 'dist/bm3d-viewer.min.js', format: 'es', banner, plugins: [terser({ format: { comments: /^!/ } })] },
		],
		plugins: [copyWasm()],
	},
	{
		input: 'src/corto-worker.js',
		output: { file: 'dist/corto-worker.js', format: 'es', banner },
	},
];
