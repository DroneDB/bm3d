// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa
//
// Reads the worked example (section 13) out of docs/FORMAT.md, so that the tests compare the writer and
// the reader with the document itself, byte for byte.

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const DOC = fileURLToPath(new URL('../../../docs/FORMAT.md', import.meta.url));

/** The text of the code block (``` fence) that follows the first occurrence of `marker`. */
function blockAfter(text, marker) {
	const start = text.indexOf(marker);
	if (start < 0) throw new Error(`docs/FORMAT.md has no "${marker}"`);
	const open = text.indexOf('```', start);
	const body = text.indexOf('\n', open) + 1;
	return text.slice(body, text.indexOf('```', body));
}

/** The bytes of a hexadecimal dump: lines of "offset  16 hex bytes   comment". */
function hexDump(block) {
	const bytes = [];
	for (const line of block.split('\n')) {
		const match = /^[0-9a-f]{4}\s{2}((?:[0-9a-f]{2} ?){1,16})/.exec(line);
		if (match) bytes.push(...match[1].trim().split(/\s+/).map((byte) => parseInt(byte, 16)));
	}
	return Uint8Array.from(bytes);
}

/** The pieces of the worked example. */
export function workedExample() {
	const text = readFileSync(DOC, 'utf8');
	const section = text.slice(text.indexOf('## 13. A worked example'), text.indexOf('## 14.'));
	return {
		header: hexDump(blockAfter(section, 'The header (128 bytes')),
		node: hexDump(blockAfter(section, 'Node 0 (a leaf')),
		metadata: blockAfter(section, 'The metadata, 363 bytes').replace(/^```json\n?/, '').trim(),
	};
}
