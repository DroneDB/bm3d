// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa

import { defineConfig } from '@playwright/test';

export default defineConfig({
	testDir: 'tests/browser',
	timeout: 60000,
	fullyParallel: false,
	reporter: [['list']],
	use: {
		browserName: 'chromium',
		// WebGL in a headless browser without a GPU: software rendering
		launchOptions: { args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] },
	},
});
