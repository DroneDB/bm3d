// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa

import { openHeader, readMetadata } from './format.js';
import { openUrl } from './source.js';

/**
 * The metadata block of the model at `url` (docs/FORMAT.md section 7): an object with `generator`,
 * `buildOptions` and `source` and whatever the build added. Reads the header and the block only.
 * `options.fetch` and `options.requestInit` are as for Bm3dObject.
 */
export async function loadMetadata(url, options = {}) {
	const source = await openUrl(url, options);
	return readMetadata(source, await openHeader(source));
}
