# UI — RmlUi Context

The `ui` module owns the RmlUi lifecycle (init/shutdown), the global system
interface (elapsed time + logging), and the FreeType font engine. RmlUi is a
retained-mode HTML/CSS UI toolkit.

## UI_CONTEXT

```cpp
DEP::UI_CONTEXT ui;
ui.Initialize ();
// ... engine runs ...
ui.Shutdown ();
```

`Initialize()` calls `Rml::Initialise()` with a stub render interface and the
global system interface. FreeType activates automatically.

## Dual-Context Architecture

RmlUi is initialized once by Sneeze, but both Sneeze and Artemis create their
own `Rml::Context` objects. Each context can have its own `RenderInterface`:

- **Artemis contexts** — SDL-backed RenderInterface for browser chrome
  (Inspector, URL bar, menus)
- **Sneeze contexts** (future) — ANARI-backed RenderInterface for in-world UI
  (service-provided HTML/CSS overlays)

Shared globals (system interface, font engine) serve both sides.

## Files

| File | Contents |
|------|----------|
| `UiContext.h` | UI_CONTEXT declaration |
| `UiContext.cpp` | Implementation (Rml::Initialise/Shutdown, system interface) |
