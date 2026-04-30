# UI — User Interface Context

The `ui` module provides a placeholder for in-engine UI rendering (HUD
elements, overlays, debug panels). The host application (Artemis) manages
the platform-level chrome (title bar, menus); this module is for
engine-rendered UI within the viewport.

## UI_CONTEXT

```cpp
#include "ui/UiContext.h"

sneeze::ui::UI_CONTEXT ui;
ui.Initialize ();
// ... engine runs ...
ui.Shutdown ();
```

## Unimplemented / Future Work

This module is a stub. Planned features:

- **Debug overlay** — FPS counter, SOM node count, WASM store status.
- **In-world UI** — 2D panels rendered as textured quads in 3D space
  (e.g., fabric-provided menus, interaction prompts).
- **Input routing** — dispatching mouse/keyboard events to focused UI
  elements before they reach the camera or SOM.
- **Layout engine** — a lightweight layout system for positioning UI
  elements relative to the viewport or world objects.
