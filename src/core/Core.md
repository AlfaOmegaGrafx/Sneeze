# Core — Engine Runtime and Worker Infrastructure

The `core` module is the heart of the Sneeze engine. It owns the main engine
object (`SNEEZE`), the worker thread infrastructure, the SOM root, and all
subsystem lifecycles.

## SNEEZE

`SNEEZE` is the singleton engine instance, created and owned by the host
application (Artemis). It manages initialization, shutdown, the engine thread
loop, and exposes the public API for input, framebuffer access, persona
management, and subsystem accessors.

```cpp
#include "core/Sneeze.h"

sneeze::core::SNEEZE engine (&myListener);
engine.SetNativeWindow (hWnd);
engine.Initialize (1280, 720, "anari");

// Application event loop feeds input
engine.SetMouseInput (dx, dy, scroll, bLeft, bRight);
engine.SetKeyInput (bSpace, bPlus, bMinus);

// Present
int nW, nH;
const uint32_t* pFB = engine.LockFrameBuffer (nW, nH);
// blit pFB to screen
engine.UnlockFrameBuffer ();

engine.Shutdown ();
```

### Subsystem Lifecycle

`Initialize()` creates, in order:

1. Root fabric + root node
2. Primary attachment node + primary fabric + primary fabric root node
3. File cache, storage system, persona
4. Solar system (ASTRO_SERVICE populates the primary fabric)
5. WASM runtime, SPIR-V pipeline, compute dispatch
6. Worker threads (compositor at 60 Hz, others at configured rates)

`Shutdown()` tears everything down in reverse order.

### Persona and Teardown

```cpp
engine.Login ("Dean", "Abramson");    // Sets persona to "Dean.Abramson"
engine.Logout ();                      // Phased teardown: signal, communicate, shutdown, destroy
engine.ChangePersona ("Jane", "Doe"); // Logout + Login
engine.ChangePrimaryFabric ("https://example.com/world.msf");  // Teardown + fabric reload
```

`Logout()` and `ChangePrimaryFabric()` run a four-phase teardown:

1. **Signal** — notify active stores that teardown is imminent
2. **Communicate** — allow instances time to finalize with their services
3. **Shutdown** — call Shutdown on instances, destroy all WASM stores
4. **Destroy** — clear session caches, logout persona (or reload fabric)

### SNEEZE_LISTENER

The host application implements `SNEEZE_LISTENER` to receive frame-ready
callbacks from the compositor:

```cpp
class SNEEZE_LISTENER
{
public:
   virtual void OnFrameReady (const uint32_t* pFB, int nFbW, int nFbH) = 0;
};
```

## WORKER

`WORKER` is the base class for all engine worker threads. Each worker runs in
its own thread and is woken by the engine thread at a configured frequency.

| Worker               | Class                 | Frequency | Purpose                          |
|----------------------|-----------------------|-----------|----------------------------------|
| Compositor           | `WORKER_COMPOSITOR`   | 60 Hz     | Scene traversal, rendering       |
| B through H          | `WORKER_B` .. `H`     | Varies    | Reserved for future use          |

Workers override `Tick()` to perform their per-frame work.

### WORKER_COMPOSITOR

The compositor traverses the primary fabric's SOM tree, computes orbital
positions for celestial bodies, submits geometry to the ANARI renderer, and
publishes the resulting framebuffer. It also manages the camera, time scale,
and pause state from user input.

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

- **WorkerB** is reserved for the animator thread (64 Hz SOM animation
  interpolation). Not yet connected.
- **Workers C through H** are placeholder slots for future subsystems.
- The phased teardown currently logs phases but does not yet await async
  acknowledgements from WASM instances (phase 2 is synchronous).
- `Resize()` is implemented but hot-resize during rendering is untested.
