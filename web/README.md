<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (C) 2026 Digipa -->

# bm3d-viewer

A [three.js](https://threejs.org) viewer for `.bm3d` models (the format is specified in
[docs/FORMAT.md](../docs/FORMAT.md)). It reads the small header and index of a file, then only the
nodes and textures the view needs, each with one HTTP Range request, decodes them in a web worker
with a WebAssembly build of [corto](https://github.com/cnr-isti-vclab/corto), and draws them
refined as the camera comes closer. It derives from the JavaScript viewer of
[Nexus](https://github.com/cnr-isti-vclab/nexus) (see [NOTICE](NOTICE)).

The package is ES modules only. It is released as a tarball (`bm3d-viewer-<version>.tgz`) attached to
the GitHub release of bm3d, not on npm.

## Use

```js
import * as THREE from 'three';
import { Bm3dObject } from 'bm3d-viewer';

const renderer = new THREE.WebGLRenderer();
const scene = new THREE.Scene();
scene.add(new THREE.AmbientLight(0xffffff, 1), new THREE.DirectionalLight(0xffffff, 2));
const camera = new THREE.PerspectiveCamera(40, innerWidth / innerHeight, 0.01, 1000);

const model = new Bm3dObject(
  'https://example.org/models/terrain.bm3d',
  (object) => {                    // the index has been read: the model can be framed
    const { center, radius } = object.geometry.boundingSphere;
    camera.position.set(center.x, center.y - 2 * radius, center.z + radius);
    camera.lookAt(center);
  },
  () => render(),                  // a node arrived: draw again
  renderer,
  undefined,                       // a material of your own, or undefined for the automatic one
  { decoderPath: '/assets/bm3d/', cache: true },
);
scene.add(model);

function render() { renderer.render(scene, camera); }
```

`Bm3dObject` is a `THREE.Mesh` with an empty placeholder geometry: its draw hook (`onAfterRender`)
chooses the nodes for the current camera, draws them with the shader program three.js chose for the
material, and asks for the nodes it lacks. Call `render()` again when `onUpdate` says a node has
arrived (or draw continuously). Without a `material`, the object makes a white Lambert material (basic
for a model without normals) that shows the vertex colours and the textures the file has.

`model.dispose()` frees what the model holds on the GPU. `model.flush()` drops the loaded nodes.
`model.computeBoundingBox()` gives the box around the finest nodes, and `raycast` works against the
coarsest level.

### Options (the sixth argument)

| Option | Meaning |
|---|---|
| `decoderPath` | The folder that holds `corto-worker.js` and `corto.wasm`, as a URL or a path (absolute, or relative to the page). Default: the folder the bundle was loaded from (`new URL('.', import.meta.url)`). The folder must be on the page's origin, since a worker cannot be started from another one. A bundler that moves the bundle (or an application that serves the worker and the wasm file from elsewhere) sets it. |
| `cache` | `false` (default) or `true`: keep the index, the node payloads and the textures read from the server in IndexedDB, keyed by URL, format version and ETag of the file. A file whose server gives no readable ETag is not cached. A store of your own (see `src/cache.js`) can be passed instead of `true`. |
| `onError(error, node)` | Called when the model cannot be opened (no `node`), or when a node or texture fails (its number): a `Bm3dError`. Default: `console.error`. A node that fails is not asked for again (for good when its bytes are corrupt, after 10 seconds when the network failed). |
| `requestInit`, `fetch` | Options for every request (`credentials`, `headers`), or another `fetch`. |

`getTargetError(gl)`/`setTargetError(gl, pixels)` (default 2) and `getMaxCacheSize(gl)`/
`setMaxCacheSize(gl, bytes)` (default 512 MiB, an estimate of what the nodes occupy) tune the view
for all the models drawn on one WebGL context.

### Metadata

```js
import { loadMetadata } from 'bm3d-viewer';
const metadata = await loadMetadata('https://example.org/models/terrain.bm3d');
// { generator: 'bm3d 1.0.0', buildOptions: {...}, source: { name, vertices, faces, bounds }, ...extra keys }
```

It reads the header and the metadata block only. The block is parsed only here; a file with a bad block
still opens.

### Errors

Every failure is a `Bm3dError` whose `code` is the name the format gives to it (`ErrorCode`):
`bad magic`, `unsupported version` (a newer format than this viewer reads: update the viewer),
`compression flag missing`, `out of range`, `bad index`, `value too large (JavaScript)` and the others of
section 11 of the format, and, found when a node or texture is used, `corrupt node` and `corrupt texture`.
`http error` is a failed request (a server that does not answer Range requests with `206` is refused), and
`decoder unavailable` a worker or `corto.wasm` that could not be started. `openBm3d(source)` and
`openUrl(url)` are exported for tools that read the format without drawing it.

## The server

The file is read with Range requests: the server answers `206` with `Content-Range`, and should send
`Accept-Ranges: bytes`, `Content-Length` and an `ETag`. For a file on another origin, the server
also needs CORS: `Access-Control-Allow-Origin`, `Access-Control-Allow-Headers: Range` (the request is
not simple) and `Access-Control-Expose-Headers: Content-Range, ETag`: the viewer needs the first to
learn the size of the file (without it, it asks with a HEAD request) and the second to cache it.
The MIME type of a `.bm3d` file is `application/vnd.dronedb.bm3d`.

## Files

`dist/` holds `bm3d-viewer.js` and `bm3d-viewer.min.js` (the same, minified),
`corto-worker.js` (a module worker) and `corto.wasm`. The two bundles import `three`, which the
application provides (`>= 0.160`; it is a peer dependency), and nothing else. `bm3d-viewer/corto-worker.js`
and `bm3d-viewer/corto.wasm` are exported so that a bundler can copy them.

## Build and test

```sh
cd web
npm ci
corto-wasm/docker-build.sh      # or, with emsdk (the version in corto-wasm/EMSDK_VERSION) active: corto-wasm/build.sh
npm run build                   # rollup: dist/
npm test                        # vitest, in node
npx playwright install chromium && npm run test:browser     # renders the fixtures in a headless Chromium
```

`corto.wasm` is built from `third_party/corto` (the patched copy bm3d builds its encoder from), with
the emsdk version pinned in `corto-wasm/EMSDK_VERSION`. The tests of the reader write their own files
from the specification (`tests/support/writer.js`), read the two fixtures of `tests/fixtures/`, and
every `.bm3d` file in `BM3D_GOLDEN_DIR` (by default `tests/golden/` of the repository) when it
exists.

## Licence

MIT (see [LICENSE](LICENSE) and [NOTICE](NOTICE)); the rest of the repository is GPL-3.0. three.js is not
part of the package.
