# RmlUi in the Viewport — Status & Game Plan

> Working note (not a module reference doc). Tracks progress against the three
> RmlUi rendering objectives and the plan to finish them. Delete or fold into
> `Ui_Context.md` once the UI path is productized.

## The three objectives

1. **HUD overlay.** Flat panel(s) that sit *on top of* everything in the scene,
   independent of scene content — menus the user interacts with to customize
   their experience. May have a distinctive look, but no spatial relationship to
   what's behind it. Screen-space, camera-locked.

2. **In-scene textured UI panels.** 2D RmlUi panels (RML + CSS) mapped onto 3D
   surfaces in the world, subject to all normal 3D effects (perspective, depth,
   occlusion, lighting). Example: an information panel floating beside an object.

3. **3D lines, shapes, and text.** Vector primitives (lines, rectangles,
   ellipses, planes) and labels placed *in* the scene as real 3D content, subject
   to 3D effects, **without** relying on triangle meshes. Example: replacing the
   orbit-trail tubes with line-art ellipses, plus measurement lines and labels.

## What exists right now

The CPU path is built and proven end-to-end:

- **Software rasterizer** — `Ui_Render.{h,cpp}` implements `Rml::RenderInterface`
  into a premultiplied-alpha RGBA8 canvas (textured triangles, top-left fill
  rule, scissor clipping). Deterministic pixel-assert unit test in the `--ui`
  suite.
- **UI context** — `Ui_Context.{h,cpp}` owns the `Rml::Context`, renders a
  default RML document, and exposes the live canvas via
  `Width()/Height()/Pixels()`. It loads no fonts: the host application supplies
  its faces through RmlUi's process-global font registry.
- **Engine hand-off** — `ENGINE::Ui_Context()` (in `Sneeze.h`) exposes the context
  so the renderer can pull the canvas.
- **Unlit material** — `HALOGEN_MATERIAL_UNLIT` extension implemented in Halogen
  (opaque/blend/masked, vertex colors) and consumed here so UI texels render at
  their authored RGBA, lighting-independent, with per-texel alpha blend.
- **In-scene proof** — a test quad in `AnariRenderer.cpp` (`m_bTestQuad`,
  `BuildTestQuad`/`UpdateTestQuad`) samples the UI canvas onto an unlit,
  alpha-blended quad. It is **world-anchored near the sun** and billboards to
  face the camera (`ComputeWorldQuadTransform`, anchor captured in `m_dQuadC*`).

### This is all test scaffolding

The `m_bTestQuad` path and the hard-coded RML document in `Ui_Context.cpp` are
proofs, not the product. They prove the pipeline; they are not the API.

## Status against each objective

| Objective | State | Notes |
|-----------|-------|-------|
| 2. In-scene panel | **Largely proven** | Textured, alpha-blended, depth-correct, billboarded panel renders in-world via the unlit material. Needs a real API (anchor a panel to a node/transform) + input. |
| 1. HUD overlay | **Not started** | Same canvas, different placement: composite screen-space instead of as a world quad. |
| 3. 3D vector primitives | **Not started** | Distinct path from the textured-quad work. |

Cross-cutting gap for 1 & 2: **input** (pointer/keyboard) is not wired — the
panel is display-only.

## Game plan

### A. Productize the in-scene panel (finish #2)
1. Replace the `m_bTestQuad` scaffold with a real renderer entry: submit a UI
   surface = (canvas texture + world transform/anchor + size), owned by the
   scene rather than hard-wired to the sun.
2. Move the RML document out of `Ui_Context.cpp`: load from a fabric/service asset
   (or host-provided string) instead of `szDEFAULT_DOC`.
3. **Done.** Fonts are host-owned: the engine loads none (`EnsureFont()` and its
   absolute test path are gone). The host loads its faces into RmlUi's
   process-global font registry, which the engine instance shares.
4. Re-render the canvas on change (dirty flag) instead of once at build.

### B. Input routing (serves #1 and #2)
1. Convert a viewport pointer event to a ray, intersect the panel quad, map the
   hit to canvas UV → RmlUi coordinates.
2. Feed `ProcessMouseMove/Down/Up` and key/text events into the `Rml::Context`.
3. Re-rasterize on UI state change (hover/press/focus).

### C. HUD overlay (finish #1)
1. Reuse the same `UI_CONTEXT` canvas, composited in screen space:
   - **Now:** a camera-locked quad placed at the near plane (works through ANARI,
     stays renderer-portable).
   - **Later (optional):** a direct RmlUi-over-native-surface pass after the
     ANARI frame, per Jonathan's note — a presentation-layer feature, not a
     scene/ANARI change.
2. Same input routing as B, in screen space (no ray needed).

### D. 3D vector primitives (finish #3)
Separate effort from the textured-quad work. Likely a thin geometry emitter
(lines/curves/labels) rendered through ANARI with the unlit material, replacing
the orbit-trail tubes and adding measurement annotations. Text here is the open
question: billboarded text quads from the same font atlas vs. true 3D glyph
geometry. Decide when we start D.

## Open items / cleanup before productizing

- Remove the test scaffold (`m_bTestQuad`, `BuildTestQuad`, `UpdateTestQuad`,
  `m_nTestQuadMode`, `GenerateTestTexture`, the `TEST_QUAD` struct) once A lands.
- The green/grey backdrop quad behind the panel has been removed.
- `Ui_Context.md` has been rewritten to the real shape (UI_CONTEXT manager +
  UI_PANEL + UI_RENDER, single shared render interface); revisit when the panel
  API settles.
