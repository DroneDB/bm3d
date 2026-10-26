// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa
//
// The package in an application's bundle. Bundlers read `new URL('<file>', import.meta.url)`, written
// out, as a file to put in the bundle: webpack resolves the string next to the module and stops when
// it is not a file ("Can't resolve '.'"), Vite and Parcel copy it. Here an application that imports
// the built package is bundled with rollup and a plugin that applies the same rule to every module,
// so a reference of that form to anything but a file of the package fails the test (decoder.test.js
// checks that the default folder of the decoder is still the folder of the module).

import { existsSync, statSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { rollup } from 'rollup';
import { describe, expect, it } from 'vitest';

const here = (path) => fileURLToPath(new URL(path, import.meta.url));

/** `new URL('<string>', import.meta.url)`, as webpack, Vite and Parcel find it */
const ASSET_URL = /new\s+URL\(\s*(['"`])([^'"`]*)\1\s*,\s*import\.meta\.url\s*\)/g;

/** The rule of those bundlers: each such string names a file next to the module, or the build fails. */
function assetUrlsAreFiles() {
	return {
		name: 'asset-urls-are-files',
		transform(code, id) {
			for (const [, , relative] of code.matchAll(ASSET_URL)) {
				const target = fileURLToPath(new URL(relative, pathToFileURL(id)));
				if (!existsSync(target) || !statSync(target).isFile()) {
					this.error(`Can't resolve '${relative}' in ${id}: new URL(..., import.meta.url) must name a file`);
				}
			}
			return null;
		},
	};
}

/** Bundles `entry` (a module of the application) with three left out, as an application would. */
async function bundle(entry) {
	const build = await rollup({ input: entry, external: ['three'], plugins: [assetUrlsAreFiles()], onwarn: () => {} });
	try {
		const { output } = await build.generate({ format: 'es' });
		return output[0].code;
	} finally {
		await build.close();
	}
}

/** An application that imports the package: its entry is a virtual module. */
function application(specifier) {
	const entry = '\0application';
	return {
		input: entry,
		plugin: {
			name: 'application',
			resolveId: (id) => (id === entry ? id : null),
			load: (id) => (id === entry ? `import { Bm3dObject, loadMetadata } from ${JSON.stringify(specifier)};\nexport { Bm3dObject, loadMetadata };\n` : null),
		},
	};
}

describe('the viewer in the bundle of an application', () => {
	for (const file of ['bm3d-viewer.js', 'bm3d-viewer.min.js']) {
		it(`bundles dist/${file} with the rule of webpack for new URL(..., import.meta.url)`, async () => {
			const { input, plugin } = application(here(`../dist/${file}`));
			const build = await rollup({ input, external: ['three'], plugins: [plugin, assetUrlsAreFiles()], onwarn: () => {} });
			const { output } = await build.generate({ format: 'es' });
			await build.close();
			expect(output[0].code).toContain('import.meta.url');
		});
	}

	it('bundles the sources the same way', async () => {
		expect(await bundle(here('../src/index.js'))).toContain('corto-worker.js');
	});

	it('refuses a reference to a folder, as webpack does', async () => {
		const { input } = application('unused');
		const folder = {
			name: 'folder',
			resolveId: (id) => (id === input ? id : null),
			load: (id) => (id === input ? "export const base = new URL('.', import.meta.url);\n" : null),
		};
		const build = rollup({ input, plugins: [folder, assetUrlsAreFiles()], onwarn: () => {} });
		await expect(build).rejects.toThrow(/Can't resolve '\.'/);
	});
});
