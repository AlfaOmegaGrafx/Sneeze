---
title: "VIEWPORT::RENDERER (class reference)"
tier: API
audience: [integrator, contributor]
sources:
  - include/Viewport.h
  - src/context/viewport/Viewport.h
  - src/context/viewport/AnariRenderer.h
  - src/context/viewport/AnariRenderer.cpp
verified: 92fdc1c
nav:
  prev: api/viewport/VIEWPORT.md
  next: api/msf/index.md
---

# `VIEWPORT::RENDERER`

> **Internal / abstract class.** `VIEWPORT::RENDERER` is *forward-declared* in the > public header (`include/Viewport.h`) but **defined privately** in > `src/context/viewport/Viewport.h`; its only concrete implementation, > `RENDERER::ANARI`, lives entirely in `src/context/viewport/AnariRenderer.*`. It > is not part of the surface an application links against and its full definition > is not visible outside the engine. It is documented here because it is essential > to understanding [`VIEWPORT`](VIEWPORT.md): the renderer is the abstraction that > the entire viewport design exists to feed, and contributors working on rendering > need its contract spelled out.

`RENDERER` is the abstract interface every rendering backend implements. It models one frame as a fixed sequence — set the camera, begin, submit geometry, end, read back — over a backend the viewport never names directly. The engine ships one implementation, `RENDERER::ANARI`, built on the [ANARI](../../systems/viewport.md) rendering abstraction. For the conceptual picture see the [Viewport system](../../systems/viewport.md); this page is the exact interface.

```cpp
class VIEWPORT::RENDERER
{
public:
   class ANARI;                 // the concrete implementation (private)

   virtual ~RENDERER () = default;

   virtual void SetNativeWindow (void* pHandle) { (void) pHandle; }
   virtual bool IsRenderingToNativeSurface () const { return false; }

   virtual bool Initialize (int nWidth, int nHeight) = 0;
   virtual void Resize (int nWidth, int nHeight) = 0;

   virtual void SetCamera (const CAMERA_DATA& Camera) = 0;
   virtual void BeginFrame () = 0;
   virtual void SubmitSpheres (const std::vector<SPHERE_DATA>& aSphere_Data) = 0;
   virtual void SubmitCurves (const std::vector<CURVE_DATA>& aCurve_Data) = 0;
   virtual void EndFrame () = 0;

   virtual void InvalidateScene () {}

   virtual const uint32_t* GetFrameBuffer () const = 0;
   virtual int GetWidth () const = 0;
   virtual int GetHeight () const = 0;

   virtual double GetLastSubmitSeconds () const { return 0.0; }
   virtual double GetLastRenderSeconds () const { return 0.0; }
};
```

---

## Role and ownership

- **Owned by** the [`VIEWPORT`](VIEWPORT.md) (its private implementation), created by `VIEWPORT::Renderer_Initialize` and destroyed by `Renderer_Shutdown`.
- **Driven by** the engine's compositor agent, which calls the frame methods in a fixed order each frame. Nothing else touches it.
- **Single-thread-affine.** Every method — construction, the frame sequence, and destruction — runs on compositor agent 0 (creation/destruction) or the compositor pool (per-frame). The backend crashes if used from multiple threads.

---

## Threading and pitfalls

**Strict single-thread use.** The renderer must be created, called, and destroyed on the compositor's lifecycle thread. The viewport guarantees this; a contributor adding a backend must preserve it. The constraint is a hard property of the underlying device, not a convention.

**The frame methods are an ordered protocol.** A frame is exactly: `SetCamera` → `BeginFrame` → `SubmitSpheres` → `SubmitCurves` → `EndFrame`. After `EndFrame` the framebuffer (readback path) is valid. `BeginFrame` clears the backend's submission lists; submitting outside a begin/end pair is undefined.

**The framebuffer pointer is only valid on the readback path.** `GetFrameBuffer` returns the mapped pixels when *not* rendering to a native surface, and null when it is. Check `IsRenderingToNativeSurface()` (or simply a null return) before reading.

**Retained scene + invalidation.** The concrete backend retains its scene across frames for speed and only refreshes transforms on a normal frame. Structural changes (geometry counts, texture presence) trigger a rebuild automatically; whole-scene swaps must be signalled with `InvalidateScene`, which the compositor forwards from [`VIEWPORT::Scene_Invalidate`](VIEWPORT.md#scene-invalidation).

---

## Setup methods

### `virtual void SetNativeWindow (void* pHandle)`
- **Purpose.** Provide a native window handle to render directly into, before `Initialize`. The base does nothing; a backend that supports direct presentation stores the handle.
- **Parameters.** `pHandle` — an opaque platform window handle.

### `virtual bool IsRenderingToNativeSurface () const`
- **Purpose.** Report whether the backend is presenting directly to a native surface (true) or rendering offscreen for readback (false).
- **Returns.** `false` from the base; the ANARI backend returns `true` only when a native window was supplied *and* the device advertised native-surface support at initialization.

### `virtual bool Initialize (int nWidth, int nHeight) = 0`
- **Purpose.** Bring the backend up at the given size (load the device, create the camera/world/renderer/frame, opt into a native surface if available).
- **Parameters.** Initial drawable dimensions.
- **Returns.** `true` on success.

### `virtual void Resize (int nWidth, int nHeight) = 0`
- **Purpose.** Re-size the render target.
- **Parameters.** New dimensions.

---

## Per-frame methods

### `virtual void SetCamera (const CAMERA_DATA& Camera) = 0`
- **Purpose.** Set the camera for the next frame from a `CAMERA_DATA` (eye position, look direction, up vector, vertical FOV, aspect, near/far).

### `virtual void BeginFrame () = 0`
- **Purpose.** Start a frame; clears the pending geometry submission lists.

### `virtual void SubmitSpheres (const std::vector<SPHERE_DATA>& aSphere_Data) = 0`
- **Purpose.** Append spheres to the frame. A `SPHERE_DATA` carries position, radius, RGB color, an optional texture (pixels + dimensions), and an emissive flag.

### `virtual void SubmitCurves (const std::vector<CURVE_DATA>& aCurve_Data) = 0`
- **Purpose.** Append curves (polylines) to the frame. A `CURVE_DATA` is a list of `CURVE_POINT`s (position + per-point radius) plus an RGB color, drawn as tubes (used for orbit trails).

### `virtual void EndFrame () = 0`
- **Purpose.** Build or update the retained scene from the submitted geometry, render the frame, and (on the readback path) map the pixels back to CPU memory. After this call the framebuffer is available.

### `virtual void InvalidateScene ()`
- **Purpose.** Force a full scene rebuild on the next `EndFrame` (used after a whole-scene swap). The base does nothing; a retaining backend sets a dirty flag.

---

## Framebuffer and dimensions

### `virtual const uint32_t* GetFrameBuffer () const = 0`
- **Purpose.** Return the last frame's RGBA pixels for readback.
- **Returns.** The pixel pointer on the readback path; null when rendering to a native surface.

### `virtual int GetWidth () const = 0` / `virtual int GetHeight () const = 0`
- **Purpose / Returns.** The render target's current width and height.

---

## Timing

### `virtual double GetLastSubmitSeconds () const` / `virtual double GetLastRenderSeconds () const`
- **Purpose.** Report the wall-clock seconds the last frame spent building/updating the scene (submit) and rendering it (render), for the viewport's diagnostics.
- **Returns.** `0.0` from the base; real values from a backend that measures them.

---

## The concrete implementation: `RENDERER::ANARI`

`RENDERER::ANARI` (in `AnariRenderer.h/.cpp`) is the engine's only backend. It implements the interface above over the ANARI API backed by a PBR device. Key internal behavior a contributor should know:

- **Scene retention.** ANARI objects (geometry, materials, surfaces, instances, the world, a point light) are created once in an internal `BuildScene` and kept across frames. A normal frame calls `UpdateScene`, which only refreshes transforms and curve positions. A frame triggers a full `ReleaseScene`/`BuildScene` when the scene is dirty (`InvalidateScene`) or when a structural change is detected (different sphere/curve counts, or a sphere gaining or losing its texture).
- **Two presentation paths.** With a native window and device support it renders directly to a swapchain (`IsRenderingToNativeSurface()` true, no readback); otherwise it renders to an offscreen ANARI frame and maps `channel.color` back into a CPU pixel buffer that `GetFrameBuffer` exposes.
- **Textured vs. analytic spheres.** A textured sphere is built as a UV-mapped triangle mesh (a shared unit sphere generated once by `GenerateUVSphere`) with per-vertex colors sampled from the texture and cached by pixel pointer; an untextured sphere uses ANARI's analytic `"sphere"` geometry. Emissive spheres (stars) brighten their sampled colors.
- **Empty scenes.** When no geometry is submitted, the world's instance parameter is unset so the screen clears rather than retaining stale objects.

`RENDERER::ANARI` forward-declares the ANARI handle types it stores, so including its header does not drag the ANARI SDK into every translation unit.

---

## Data types

These structs (declared in the private `Viewport.h`) are the renderer's vocabulary for a frame.

| Type | Fields |
|---|---|
| `CAMERA_DATA` | Eye position, look direction, up vector, vertical FOV, aspect, near, far. |
| `SPHERE_DATA` | Center `x,y,z`, radius, RGB color, optional texture (pixels + width/height), emissive flag. |
| `CURVE_POINT` | Position `x,y,z` and radius. |
| `CURVE_DATA` | A vector of `CURVE_POINT`s and an RGB color. |
| `UV_SPHERE` | Generated unit-sphere mesh: positions, normals, texcoords, indices. |

---

## See also

- [Viewport system](../../systems/viewport.md) — design, the frame loop, threading, limitations.
- [VIEWPORT](VIEWPORT.md) — owns and drives the renderer.
- [Scene API](../scene/index.md) — the model traversed to produce `SPHERE_DATA` / `CURVE_DATA`.

---

[Viewport API](index.md) · Prev: [VIEWPORT](VIEWPORT.md) · Next: [MSF API](../msf/index.md)
