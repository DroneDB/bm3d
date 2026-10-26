// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa

import { defineConfig } from 'vitest/config';

export default defineConfig({
	test: {
		environment: 'node',
		include: ['tests/**/*.test.js'],
		exclude: ['tests/browser/**'],
	},
});
