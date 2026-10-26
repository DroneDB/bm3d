// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa
//
// A WebGL context that does nothing and records the calls: enough for the code that draws and
// uploads to run in node.

/**
 * Every constant is a distinct number (its name is kept to read the log), every function records
 * its call in `calls` and returns `returns[name]` (or 0). `overrides` replaces properties.
 */
export function fakeGl({ returns = {}, overrides = {} } = {}) {
	const calls = [];
	const constants = new Map();
	const target = { calls, isTexture: () => false, ...overrides };
	const gl = new Proxy(target, {
		get(object, name) {
			if (name in object) return object[name];
			if (typeof name !== 'string') return undefined;
			if (/^[A-Z][A-Z0-9_]*$/.test(name)) {
				if (!constants.has(name)) constants.set(name, 0x10000 + constants.size);
				return constants.get(name);
			}
			return (...args) => {
				calls.push({ name, args });
				return name in returns ? (typeof returns[name] === 'function' ? returns[name](...args) : returns[name]) : 0;
			};
		},
	});
	return gl;
}
