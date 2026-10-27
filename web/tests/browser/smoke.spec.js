// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Digipa
//
// Renders the fixtures with three.js in a headless Chromium and checks that something was drawn
// (not a blank canvas), that the model refined (more than its root node was loaded and drawn), that
// the worker and corto.wasm come from where decoderPath says, and that the cache answers a second
// visit. The page is served by a small server that answers Range requests. The golden files of the
// command line in BM3D_GOLDEN_DIR, when it is set (the CI writes them), are drawn as well.

import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import { expect, test } from '@playwright/test';

import { startServer } from '../support/server.js';

const read = (path) => readFileSync(new URL(path, import.meta.url));
const goldenDir = process.env.BM3D_GOLDEN_DIR;
const golden = goldenDir && existsSync(goldenDir) ? readdirSync(goldenDir).filter((name) => name.endsWith('.bm3d')).sort() : [];
const BACKGROUND = [32, 48, 64];

const PAGE = `<!doctype html>
<meta charset="utf-8">
<script type="importmap">{ "imports": { "three": "/three/three.module.js" } }</script>
<body style="margin:0"><script type="module">
import * as THREE from 'three';

// The state exists before the first await: the load event does not wait for the import below, so the
// test can read the state while the viewer is still being fetched
const state = (window.__state = { loaded: false, updates: 0, errors: [], frames: 0 });
const query = new URLSearchParams(location.search);
const { Bm3dObject } = await import(query.get('bundle') === 'min' ? '/dist/bm3d-viewer.min.js' : '/dist/bm3d-viewer.js');
const renderer = new THREE.WebGLRenderer({ preserveDrawingBuffer: true });
renderer.setSize(400, 300);
document.body.append(renderer.domElement);
const scene = new THREE.Scene();
scene.background = new THREE.Color(${BACKGROUND[0] / 255}, ${BACKGROUND[1] / 255}, ${BACKGROUND[2] / 255});
scene.add(new THREE.AmbientLight(0xffffff, 1.2), new THREE.DirectionalLight(0xffffff, 2));
const camera = new THREE.PerspectiveCamera(40, 400 / 300, 0.01, 100);

const options = { onError: (error) => state.errors.push(String(error)) };
if (query.get('decoderPath')) options.decoderPath = query.get('decoderPath');
if (query.get('cache')) options.cache = true;
const object = new Bm3dObject(query.get('model'), (o) => {
	const { center, radius } = o.geometry.boundingSphere;
	camera.position.set(center.x + radius * 0.8, center.y - radius * 2.2, center.z + radius * 1.6);
	camera.up.set(0, 0, 1);
	camera.lookAt(center);
	state.loaded = true;
}, () => state.updates++, renderer, undefined, options);
scene.add(object);

function frame() {
	renderer.render(scene, camera);
	state.frames++;
	const mesh = object.geometry.instance.mesh;
	state.ready = mesh && mesh.isReady ? Array.from(mesh.status).filter((s) => s === 1).length : 0;
	state.glError = renderer.getContext().getError();
	requestAnimationFrame(frame);
}
frame();

window.__pixels = () => {
	const canvas = document.createElement('canvas');
	canvas.width = 400; canvas.height = 300;
	const context = canvas.getContext('2d');
	context.drawImage(renderer.domElement, 0, 0);
	const data = context.getImageData(0, 0, 400, 300).data;
	const colors = new Set();
	let drawn = 0;
	for (let i = 0; i < data.length; i += 4) {
		if (Math.abs(data[i] - ${BACKGROUND[0]}) > 6 || Math.abs(data[i + 1] - ${BACKGROUND[1]}) > 6 || Math.abs(data[i + 2] - ${BACKGROUND[2]}) > 6) {
			drawn++;
			colors.add((data[i] >> 3) << 10 | (data[i + 1] >> 3) << 5 | (data[i + 2] >> 3));
		}
	}
	return { drawn, total: data.length / 4, colors: colors.size };
};
</script>`;

let server;
let files;
test.beforeAll(async () => {
	files = {
		'/': { data: PAGE, type: 'text/html' },
		'/three/three.module.js': { data: read('../../node_modules/three/build/three.module.js'), type: 'text/javascript' },
		'/three/three.core.js': { data: read('../../node_modules/three/build/three.core.js'), type: 'text/javascript' },
		'/dist/bm3d-viewer.js': { data: read('../../dist/bm3d-viewer.js'), type: 'text/javascript' },
		'/dist/bm3d-viewer.min.js': { data: read('../../dist/bm3d-viewer.min.js'), type: 'text/javascript' },
		'/dist/corto-worker.js': { data: read('../../dist/corto-worker.js'), type: 'text/javascript' },
		'/dist/corto.wasm': { data: read('../../dist/corto.wasm'), type: 'application/wasm' },
		'/models/terrain.bm3d': { data: read('../fixtures/terrain.bm3d') },
		'/models/sphere.bm3d': { data: read('../fixtures/sphere.bm3d') },
	};
	for (const name of golden) files[`/models/golden/${name}`] = { data: readFileSync(join(goldenDir, name)) };
	server = await startServer((path) => files[path] || null);
});
test.afterAll(() => server.close());
test.beforeEach(() => {
	server.requests.length = 0;
});

/** Opens the page for a model; returns the errors the page reports from then on. */
async function open(page, model, extra = '') {
	const errors = [];
	page.on('pageerror', (error) => errors.push(error.message));
	page.on('console', (message) => message.type() === 'error' && errors.push(message.text()));
	await page.goto(`${server.url}/?model=${encodeURIComponent(`/models/${model}.bm3d`)}${extra}`);
	return errors;
}

/** Waits until `nodes` nodes of the open page's model are on the GPU and drawn. */
async function waitUntilDrawn(page, nodes = 1) {
	await page.waitForFunction((n) => window.__state.loaded && window.__state.ready >= n && window.__state.frames > 3, nodes, { timeout: 30000 });
	await page.waitForTimeout(500); // let the requests that the last frames asked for arrive and be drawn
}

/** Opens the page for a model and waits until `nodes` nodes are on the GPU and drawn. */
async function show(page, model, { nodes = 1, extra = '' } = {}) {
	const errors = await open(page, model, extra);
	await waitUntilDrawn(page, nodes);
	return errors;
}

const modelRanges = (model) => server.requests.filter((r) => r.path === `/models/${model}.bm3d` && r.range).map((r) => r.range);

test('draws a textured, multiresolution model: not a blank canvas, and refined', async ({ page }) => {
	const errors = await show(page, 'terrain', { nodes: 2 });
	const pixels = await page.evaluate(() => window.__pixels());
	expect(pixels.drawn / pixels.total).toBeGreaterThan(0.1);
	expect(pixels.colors).toBeGreaterThan(20);
	const state = await page.evaluate(() => window.__state);
	expect(state.ready).toBeGreaterThanOrEqual(2);
	expect(state.updates).toBeGreaterThanOrEqual(2);
	expect(state.errors).toEqual([]);
	expect(errors).toEqual([]);
	expect(state.glError).toBe(0);
	// the textures came with the nodes, and the worker and wasm from the folder of the bundle
	expect(modelRanges('terrain').length).toBeGreaterThan(4);
	expect(server.requests.map((r) => r.path)).toEqual(expect.arrayContaining(['/dist/corto-worker.js', '/dist/corto.wasm']));
});

test('the minified bundle draws as well', async ({ page }) => {
	const errors = await show(page, 'terrain', { nodes: 2, extra: '&bundle=min' });
	const pixels = await page.evaluate(() => window.__pixels());
	expect(pixels.drawn / pixels.total).toBeGreaterThan(0.1);
	expect(pixels.colors).toBeGreaterThan(20);
	expect(errors).toEqual([]);
});

test('has its state while the viewer is still being fetched', async ({ page }) => {
	// The page imports the viewer with a dynamic import, which the load event does not wait for. Hold the
	// viewer back: the page has loaded (goto returned) and the test can already wait on its state
	let release;
	const held = new Promise((done) => (release = done));
	await page.route('**/dist/bm3d-viewer.js', async (route) => {
		await held;
		await route.continue();
	});
	const errors = await open(page, 'sphere');
	expect(await page.evaluate(() => typeof window.__state)).toBe('object');
	release();
	await waitUntilDrawn(page);
	expect(errors).toEqual([]);
});

test('draws a model with vertex colours and no texture', async ({ page }) => {
	const errors = await show(page, 'sphere');
	const pixels = await page.evaluate(() => window.__pixels());
	expect(pixels.drawn / pixels.total).toBeGreaterThan(0.05);
	expect(pixels.colors).toBeGreaterThan(20);
	expect(errors).toEqual([]);
	expect((await page.evaluate(() => window.__state)).errors).toEqual([]);
});

test('takes the worker and corto.wasm from the decoderPath it is given', async ({ page }) => {
	// Served only there: the folder of the bundle has none
	const original = { ...files };
	files['/vendor/bm3d/corto-worker.js'] = files['/dist/corto-worker.js'];
	files['/vendor/bm3d/corto.wasm'] = files['/dist/corto.wasm'];
	delete files['/dist/corto-worker.js'];
	delete files['/dist/corto.wasm'];
	try {
		const errors = await show(page, 'sphere', { extra: '&decoderPath=/vendor/bm3d' });
		const pixels = await page.evaluate(() => window.__pixels());
		expect(pixels.drawn / pixels.total).toBeGreaterThan(0.05);
		expect(errors).toEqual([]);
		const paths = server.requests.map((r) => r.path);
		expect(paths).toEqual(expect.arrayContaining(['/vendor/bm3d/corto-worker.js', '/vendor/bm3d/corto.wasm']));
		expect(paths).not.toContain('/dist/corto.wasm');
	} finally {
		Object.keys(files).forEach((key) => delete files[key]);
		Object.assign(files, original);
	}
});

test('answers a second visit from the cache, when it is asked for', async ({ browser }) => {
	const context = await browser.newContext();
	try {
		const first = await context.newPage();
		await show(first, 'terrain', { nodes: 3, extra: '&cache=1' });
		const loaded = (await first.evaluate(() => window.__state.ready));
		expect(modelRanges('terrain').length).toBeGreaterThan(4);
		await first.close();

		server.requests.length = 0;
		const second = await context.newPage();
		await show(second, 'terrain', { nodes: loaded, extra: '&cache=1' });
		// only the header is asked for again (it carries the ETag the cache is keyed on)
		expect(modelRanges('terrain')).toEqual(['bytes=0-127']);
		const pixels = await second.evaluate(() => window.__pixels());
		expect(pixels.drawn / pixels.total).toBeGreaterThan(0.1);
	} finally {
		await context.close();
	}
});

test('does not cache anything unless it is asked to', async ({ browser }) => {
	const context = await browser.newContext();
	try {
		const first = await context.newPage();
		await show(first, 'sphere');
		await first.close();
		server.requests.length = 0;
		const second = await context.newPage();
		await show(second, 'sphere');
		expect(modelRanges('sphere').length).toBeGreaterThan(1);
	} finally {
		await context.close();
	}
});

for (const name of golden) {
	test(`draws the golden file ${name}`, async ({ page }) => {
		const errors = await show(page, `golden/${name.replace(/\.bm3d$/, '')}`);
		const pixels = await page.evaluate(() => window.__pixels());
		expect(pixels.drawn).toBeGreaterThan(0);
		expect(errors).toEqual([]);
		const state = await page.evaluate(() => window.__state);
		expect(state.errors).toEqual([]);
		expect(state.glError).toBe(0);
	});
}
