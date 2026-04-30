# Renderer — Scene Rendering Abstraction

The `renderer` module defines an abstract rendering interface and provides
an ANARI-based implementation. The compositor submits geometry each frame;
the renderer produces a framebuffer (or presents directly to a native
surface).

## RENDERER (base class)

`RENDERER` is the abstract interface that all rendering backends implement.

### Frame Lifecycle

```
SetCamera (...)        // position, direction, up, fov, aspect
BeginFrame ()          // clear / prepare
SubmitSpheres (...)    // batch of positioned, colored spheres
SubmitCurves (...)     // batch of polyline curves
EndFrame ()            // render and produce framebuffer
```

After `EndFrame()`, the framebuffer is available via `GetFrameBuffer()` — a
pointer to `uint32_t` RGBA pixels, `GetWidth() * GetHeight()` elements.

### Native Surface Rendering

If `SetNativeWindow()` is called before `Initialize()` and the backend
supports it, the renderer may present directly to the platform surface
(HWND, CAMetalLayer, etc.). In that case `IsRenderingToNativeSurface()`
returns true and `GetFrameBuffer()` returns nullptr — the application
should skip CPU-side blitting.

### Data Types

**SPHERE_DATA** — a positioned sphere with RGB color:

| Field    | Type    | Description            |
|----------|---------|------------------------|
| `x,y,z`  | `float` | World-space position  |
| `dRadius`| `float` | Sphere radius         |
| `r,g,b`  | `float` | Color (0.0 – 1.0)    |

**CURVE_DATA** — a polyline with per-vertex position and radius:

| Field     | Type                       | Description             |
|-----------|----------------------------|-------------------------|
| `aPoints` | `vector<CURVE_POINT>`      | Vertices with radii     |
| `r,g,b`   | `float`                    | Color (0.0 – 1.0)      |

**CAMERA_DATA** — camera parameters:

| Field              | Type    | Description              |
|--------------------|---------|--------------------------|
| `dPosX/Y/Z`       | `float` | Eye position             |
| `dDirX/Y/Z`       | `float` | Look direction           |
| `dUpX/Y/Z`        | `float` | Up vector                |
| `dFovY`            | `float` | Vertical FOV (degrees)   |
| `dAspect`          | `float` | Width / height           |

## ANARI_RENDERER

The concrete ANARI backend. Loads an ANARI library by name (e.g. `"anari"`),
creates a device, and manages the frame/world/camera lifecycle.

```cpp
#include "renderer/AnariRenderer.h"

sneeze::renderer::ANARI_RENDERER renderer ("anari");
renderer.SetNativeWindow (hWnd);
renderer.Initialize (1280, 720);

// ... per-frame calls ...

renderer.Shutdown ();
```

The ANARI renderer rebuilds the world object each frame from the submitted
sphere and curve batches. Timing for submit and render phases is available
via `GetLastSubmitSeconds()` / `GetLastRenderSeconds()`.

## Unimplemented / Future Work

- **Mesh geometry** — only spheres and curves are supported. Triangle meshes
  (glTF/GLB) are not yet submittable.
- **Materials and textures** — all geometry uses solid RGB colors.
- **Instancing** — no instanced rendering; each sphere is submitted
  individually.
- **Multiple backends** — only the ANARI backend exists. A Vulkan-native or
  software rasterizer backend may be added later.
