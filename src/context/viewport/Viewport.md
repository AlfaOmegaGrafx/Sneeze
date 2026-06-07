# Viewport — Rendering and Camera

The viewport module owns the rendering pipeline and camera controls for each
browser context. Each CONTEXT owns one VIEWPORT; the VIEWPORT only renders
when an IVIEWPORT host is attached (enabling headless contexts for inactive
tabs).

## Architecture

```
VIEWPORT (src/viewport/Viewport.cpp)
├── RENDERER (abstract, declared in Viewport.h)
│   └── RENDERER::ANARI (AnariRenderer.h/cpp — Halogen/Filament backend)
├── VIEW (camera orbit state, declared in include/Viewport.h)
├── INPUT (accumulated mouse/key state, declared in include/Viewport.h)
├── UV_SPHERE (mesh generator, UVSphere.cpp)
└── JOB_COMPOSITOR (pool-cycle job, managed by CONTROL)
```

All private headers and source live directly in `src/viewport/` — there are
no subdirectories.

## VIEWPORT

Owned by CONTEXT. pImpl pattern. Key lifecycle:

- `Renderer_Initialize()` — deferred, called from compositor agent 0 thread
  (Filament thread affinity). Creates `RENDERER::ANARI`.
- `Activate(IVIEWPORT*)` — creates JOB_COMPOSITOR, posts to POOL_CYCLE.
- `Deactivate()` — cancels compositor job, blocks until renderer shutdown.
- Input via `Input_Mouse()` / `Input_Key()` (accumulated under mutex).
- Framebuffer via `FrameBuffer_Write()` / `FrameBuffer_Capture()` /
  `FrameBuffer_Release()` (producer-consumer with mutex).

## RENDERER (base class)

Abstract rendering interface in `Viewport.h`. All backends implement this.

### Frame Lifecycle

```
SetCamera (...)        // position, direction, up, fov, aspect
BeginFrame ()          // clear / prepare
SubmitSpheres (...)    // batch of positioned, colored, optionally textured spheres
SubmitCurves (...)     // batch of polyline curves (orbit trails)
EndFrame ()            // render and produce framebuffer
```

After `EndFrame()`, the framebuffer is available via `GetFrameBuffer()`.

### Native Surface Rendering

If `SetNativeWindow()` is called before `Initialize()` and the backend
supports it, the renderer presents directly to the platform surface (HWND,
CAMetalLayer, etc.). `IsRenderingToNativeSurface()` returns true and
`GetFrameBuffer()` returns nullptr — the application skips CPU blitting.

### Data Types

| Type | Purpose |
|------|---------|
| `SPHERE_DATA` | Positioned sphere: x/y/z, radius, r/g/b color, optional texture pixels, emissive flag |
| `CURVE_POINT` | Vertex with position and radius |
| `CURVE_DATA` | Polyline (vector of CURVE_POINTs) with r/g/b color |
| `CAMERA_DATA` | Eye position, look direction, up vector, FOV, aspect, near/far |
| `UV_SPHERE` | Generated mesh: positions, normals, texcoords, indices |

## RENDERER::ANARI

Concrete ANARI backend (`AnariRenderer.h`/`AnariRenderer.cpp`). Loads an ANARI
library by name (e.g. `"halogen"`), creates a device, manages the
frame/world/camera lifecycle. Scene retention: ANARI objects are created once
via `BuildScene()` and retained; `UpdateScene()` updates transforms only.
`SceneNeedsRebuild()` detects structural changes. Submit/render timing exposed
via `GetLastSubmitSeconds()` / `GetLastRenderSeconds()`.

## VIEW (Camera Orbit)

A struct declared in `include/Viewport.h`. Each VIEWPORT owns one VIEW,
updated by the compositor from that viewport's input.

```cpp
SNEEZE::VIEWPORT::VIEW& view = pViewport->View ();
view.Update (nDX, nDY, dScrollY, bMouseLeft, bMouseRight);
```

| Input | Action |
|-------|--------|
| Left drag | Orbit (rotate theta/phi) |
| Right drag | Pan (translate target) |
| Scroll wheel | Zoom (adjust distance) |

Spherical-to-Cartesian conversion from `dTheta`, `dPhi`, `dDistance` looking
at `(dTargetX, dTargetY, dTargetZ)`. The compositor converts this into
`CAMERA_DATA` for the renderer.

## Known Issues

- **Orbit trails render as thick PBR-lit tubes.** ANARI's `"curve"` geometry
  produces volumetric cylinders with the `"matte"` material, resulting in
  over-luminated, thick, visually noisy trails. The original Three.js
  implementation used screen-space lines. Planned: unlit/emissive material,
  camera-distance radius scaling, reduced segment count.

## Future Work

- **First-person camera** — walk/fly mode for terrestrial navigation
- **Camera transitions** — smooth animated transitions between targets
- **Camera constraints** — phi clamping, zoom limits, collision
- **Mesh geometry** — triangle meshes (glTF/GLB) not yet submittable
- **Screen-space lines** — via Filament custom material or post-process
