# Sneeze — Engine Runtime and Agent Infrastructure

The `sneeze` module is the heart of the Sneeze engine. It owns the main engine
object (`SNEEZE`), the agent thread infrastructure, and all shared subsystem
lifecycles. Per-viewport concerns (scene, renderer, input, framebuffer) are
owned by `VIEWPORT` instances, created and destroyed via `OpenViewport` /
`CloseViewport`.

## SNEEZE

`SNEEZE` is the singleton engine instance, created and owned by the host
application (Artemis). It manages initialization, shutdown, the engine thread
loop, viewport management, and exposes the public API for persona management
and subsystem accessors.

```cpp
#include "Sneeze.h"

auto* pHost = new SNEEZE_HOST ();
pHost->sAppDataPath  = sPath;
pHost->sRenderer     = "halogen";

SNEEZE engine (pHost);
engine.Initialize ();

// Create a viewport (per-window)
auto* pViewportHost = new VIEWPORT_HOST ();
pViewportHost->pNativeWindow = hWnd;
pViewportHost->nWidth        = 1280;
pViewportHost->nHeight       = 720;

SNEEZE::VIEWPORT* pViewport = engine.OpenViewport (pViewportHost, "https://example.com/world.msf");

// Application event loop feeds input to the viewport
pViewport->SetMouseInput (dx, dy, scroll, bLeft, bRight);
pViewport->SetKeyInput (bSpace, bPlus, bMinus);

// Present (fallback path -- native surface rendering skips this)
int nW, nH;
const uint32_t* pFB = pViewport->LockFrameBuffer (nW, nH);
// blit pFB to screen
pViewport->UnlockFrameBuffer ();

engine.CloseViewport (pViewport);
engine.Shutdown ();
```

### Subsystem Lifecycle

`Initialize()` creates shared engine services, in order:

1. WASM runtime, SPIR-V pipeline, XR runtime (if enabled)
2. Network (file cache), storage system, UI context
3. Agent threads (compositor self-paced, others at configured rates)

`OpenViewport()` creates per-viewport resources:

1. SCENE (root fabric, root node, primary attachment node, primary fabric)
2. RENDERER (optional -- headless mode if no renderer library configured)
3. ASTRO_SERVICE (populates the primary fabric with celestial nodes)

`Shutdown()` tears everything down in reverse order.

### Viewport Management

Multiple viewports are supported. Each viewport is independent:

```cpp
SNEEZE::VIEWPORT* pViewport1 = engine.OpenViewport (pHost1, "https://world-a.msf");
SNEEZE::VIEWPORT* pViewport2 = engine.OpenViewport (pHost2, "https://world-b.msf");

// Convenience: first viewport
SNEEZE::VIEWPORT* pViewport = engine.Viewport ();

// Full list
const std::vector<SNEEZE::VIEWPORT*>& apViewport = engine.Viewports ();
```

### Persona and Teardown

```cpp
engine.Login ("Dean", "Abramson");    // Sets persona to "Dean.Abramson"
engine.Logout ();                      // Phased teardown: signal, communicate, shutdown, destroy
engine.ChangePersona ("Jane", "Doe"); // Logout + Login
```

`Logout()` runs a four-phase teardown:

1. **Signal** -- notify active stores that teardown is imminent
2. **Communicate** -- allow instances time to finalize with their services
3. **Shutdown** -- call Shutdown on instances, destroy all WASM stores
4. **Destroy** -- clear session caches, logout persona

### ISNEEZE

`ISNEEZE` is the engine-level interface between the host application and the
engine. Per-viewport concerns have moved to `IVIEWPORT`.

```cpp
class ISNEEZE
{
public:
   // Configuration (set by host before Initialize)
   std::string sAppDataPath;
   std::string sSessionPath;
   std::string sRenderer;

   std::string SessionPath () const;

   // Callbacks
   virtual void Log (eLOGLEVEL Level, const std::string& sModule, const std::string& sMessage) = 0;
};
```

### IVIEWPORT

`IVIEWPORT` is the per-viewport interface. Each viewport gets its own host
instance from the application.

```cpp
class IVIEWPORT
{
public:
   // Configuration (set by host before OpenViewport)
   void*  pNativeWindow = nullptr;
   int    nWidth        = 0;
   int    nHeight       = 0;

   // Callbacks
   virtual void OnFrameReady (const uint32_t* pFB, int nFbW, int nFbH) = 0;

   // Optional inspector callbacks (default no-op)
   virtual void OnNetworkFileCreated (NOTIFICATION* pNotification) {}
   virtual void OnNetworkFileChanged (NOTIFICATION* pNotification) {}
   virtual void OnNetworkFileDeleted (NOTIFICATION* pNotification) {}
   virtual void OnStorageUnitCreated (NOTIFICATION* pNotification) {}
   virtual void OnStorageUnitChanged (NOTIFICATION* pNotification) {}
   virtual void OnStorageUnitDeleted (NOTIFICATION* pNotification) {}
};
```

## AGENT

`AGENT` is the base class for all engine agent threads. Each agent runs in
its own thread and is woken by the engine thread at a configured frequency.

| Agent                | Class                 | Frequency | Purpose                          |
|----------------------|-----------------------|-----------|----------------------------------|
| Compositor           | `AGENT::COMPOSITOR`    | Self-paced| Scene traversal, rendering       |
| C through H          | `AGENT::C` .. `H`     | Varies    | Reserved for future use          |

Agents override `Tick()` to perform their per-frame work. Self-paced agents
(compositor) override `ThreadLoop()` instead.

### AGENT::COMPOSITOR

The compositor iterates all active viewports each frame. For each viewport it
traverses the primary fabric's SOM tree, computes orbital positions for
celestial bodies, submits geometry to the viewport's ANARI renderer, and
publishes the resulting framebuffer via `pViewport->Host()->OnFrameReady()`.
It also manages the camera, time scale, and pause state from user input
(consumed from the first viewport).

The compositor does NOT own a renderer or camera -- those are per-viewport.

## Types

### VEC3, QUAT

```cpp
struct VEC3 { double x, y, z; };
struct QUAT { double dX, dY, dZ, dW; };
```

### Constants

| Name              | Value                    | Description                    |
|-------------------|--------------------------|--------------------------------|
| `AU_M`            | 149,597,870,700          | 1 AU in meters                 |
| `GM_SUN_M3S2`     | 1.327e20                 | Solar GM (m^3/s^2)             |
| `TICKS_PER_S`     | 64                       | Animation tick rate             |
| `TICKS_PER_CY`    | 36525 * 86400 * 64      | Ticks per Julian century        |

### EPOCH

Julian Date utility for astronomical time calculations. Supports parsing
calendar dates, computing delta-years between epochs, and propagating mean
anomalies.

## Unimplemented / Future Work

- **Agent C** is reserved for the animator thread (64 Hz SOM animation
  interpolation). Not yet connected.
- **Agents D through H** are placeholder slots for future subsystems.
- The phased teardown currently logs phases but does not yet await async
  acknowledgements from WASM instances (phase 2 is synchronous).
- `Resize()` is implemented but hot-resize during rendering is untested.
- Multi-viewport input: currently only the first viewport's input drives
  camera and time controls. Per-viewport input independence is future work.
