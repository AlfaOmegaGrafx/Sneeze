# Control - Engine Thread, Agent Infrastructure, and Metronome

The `control` module (`SNEEZE::CONTROL` + `SNEEZE::AGENT` hierarchy) owns the engine thread, agent lifecycle, the drift-free metronome, and the disk cleanup queue. It is the engine's scheduler.

## Architecture

```
CONTROL (inherits THREAD)
 ├── Engine thread (Main)
 ├── Metronome (drift-free, fixed-origin, 1ms resolution)
 ├── Agent factory table (AGENT_INIT)
 ├── Per-agent runtime state (AGENT_STATE)
 └── Cleanup queue (m_mxCleanup, m_aCleanupPath, m_bCleanupPending)

AGENT (inherits THREAD, abstract base)
 ├── COMPOSITOR  (self-paced, DwmFlush / 16ms sleep)
 ├── SCRUBBER    (signal-driven, drains cleanup queue)
 ├── C           (signal-driven, 30 Hz placeholder)
 ├── D           (signal-driven, 60 Hz placeholder)
 └── E           (signal-driven, 64 Hz placeholder)
```

## THREAD Base Class

Declared in `sneeze/Engine.h`, implemented in `sneeze/Thread.cpp`. Encapsulates all threading primitives. Every managed thread in the engine (CONTROL + all AGENTs) inherits THREAD.

### Lifecycle

1. `Initialize()` - spawns the thread (`Main()`), blocks caller until the thread calls `Ready()`
2. `Main()` - pure virtual, the thread's entry point
3. `Ready(bResult)` - called by the thread to signal the initializing caller that startup is complete
4. `Signal(bShutdown)` - wakes the thread; if `bShutdown == true`, latches the shutdown flag
5. `~THREAD()` - calls `Signal(true)` then joins and deletes the thread

### Wait Contract

`Wait` has two overloads. Neither reads `m_bShutdown` or calls `IsShutdown()`:

- **Predicate:** `Wait(std::function<bool()> fnWork)` - wraps `m_cvThread.wait(lock, fnWork)`. The predicate returns `true` to stop waiting.
- **Timed:** `Wait(std::chrono::milliseconds duration)` - wraps `m_cvThread.wait_for(lock, duration)`.

Shutdown policy belongs in derived `Main()` implementations and in predicate callables, not in the base class.

### Members

| Member | Type | Purpose |
|--------|------|---------|
| `m_pthThread` | `std::thread*` | The managed thread |
| `m_mxThread` | `std::mutex` | Protects condition variable |
| `m_cvThread` | `std::condition_variable` | Wait/Signal mechanism |
| `m_bReady` | `bool` | Ready handshake flag |
| `m_bResult_Initialize` | `bool` | Initialization result |
| `m_bShutdown` | `bool` | Shutdown latch |

## CONTROL

Inherits THREAD. Constructor takes `ENGINE*`. Owns the engine thread, agent lifecycle, the metronome, and the cleanup queue.

### Initialization

`Initialize(int& nAgentCount)` calls `THREAD::Initialize()` which spawns the engine thread (`Main()`). On return, `nAgentCount` is populated with the number of agents created.

### Main() - Engine Thread Lifecycle

1. **Create agents** from the static `AGENT_INIT` factory table (add-before-init principle)
2. Call `Ready(bInitialized)` to unblock the caller
3. **Metronome loop** (drift-free, fixed-origin scheduling):
   - `if (!IsShutdown()) { ...tick agents... } else break;`
   - `Wait(std::chrono::milliseconds(1))` (1ms resolution via `timeBeginPeriod(1)` on Windows)
   - Under `m_mxCleanup`, read `m_bCleanupPending`; if true, `Signal()` the scrubber
4. **Shutdown**: signal all agents with `Signal(true)`, then delete them

### Agent Factory Table

Static `aAgent_Init` array of `AGENT_INIT` structs. Each entry specifies `nHertz` and a `fnCreate` factory function.

| Index | Agent | Hz | Behavior |
|-------|-------|----|----------|
| 0 | COMPOSITOR | 0 | Self-paced (DwmFlush) |
| 1 | SCRUBBER | 1 | Signal-driven (cleanup relay) |
| 2 | C | 30 | Placeholder |
| 3 | D | 60 | Placeholder |
| 4 | E | 64 | Placeholder |

`nHertz == 0` means the metronome never signals this agent. The agent must pace itself.

### Per-Agent Runtime State

```cpp
struct AGENT_STATE
{
   AGENT*  pAgent;
   int     nHertz;
   int64_t nLastTick;
   int     nSignalCount;
};
```

### Cleanup Queue

Two methods, both protected by `m_mxCleanup`:

- `Cleanup_Queue(sPath)` - appends path, sets `m_bCleanupPending = true`, releases lock, then calls `Signal()` on CONTROL (wakes the metronome so it can relay to scrubber)
- `Cleanup_SwapQueue(aPath)` - swaps `m_aCleanupPath` into the caller's vector, sets `m_bCleanupPending = false`

The pending flag clears only in `Cleanup_SwapQueue` after the scrubber takes ownership of the paths. CONTROL's metronome reads the flag but never clears it.

### Members

| Member | Type | Purpose |
|--------|------|---------|
| `m_pEngine` | `ENGINE*` | Back-pointer to engine |
| `m_aAgent_State` | `vector<AGENT_STATE>` | Per-agent runtime state |
| `m_mxCleanup` | `mutex` | Protects cleanup queue |
| `m_aCleanupPath` | `vector<string>` | Pending paths for deletion |
| `m_bCleanupPending` | `bool` | Flag: paths waiting for scrubber |

## AGENT

Abstract base class for engine agent threads. Inherits THREAD. Constructor takes `(CONTROL* pControl, int nAgentIndex)`.

- `Engine()` delegates to `m_pControl->Engine()` - agents do not cache a separate engine pointer
- `Main()` is pure virtual - each derived agent implements its own thread loop

### Signal-Driven Pattern (SCRUBBER, C, D, E)

```
Main():
   Ready()
   Wait([this] { return Tick(); })   // or DrainQueue()
```

The predicate callable (`Tick()` / `DrainQueue()`) owns whatever work runs on each wake and returns `IsShutdown()` to end the wait.

### Self-Paced Pattern (COMPOSITOR)

```
Main():
   Ready()
   while (!IsShutdown())
      ... render all viewports ...
      DwmFlush()      // Windows: sync to display VSync
      sleep_for(16ms) // non-Windows fallback
```

## AGENT::COMPOSITOR

The rendering agent. Self-paced via `DwmFlush()` (Windows) or `sleep_for(16ms)`. Does NOT own a renderer or camera - those are per-viewport.

Each iteration:
1. Acquire viewport list via `Engine()->Viewport_Capture()`
2. For each viewport (skip if `!IsReady()`):
   - Service any pending renderer shutdown (`ServiceRendererShutdown()`)
   - Consume input, update camera orbit
   - Call `Viewport_Render(pViewport, tpLoopStart)`
3. Release viewport list via `Engine()->Viewport_Release()`
4. `DwmFlush()` (always outside viewport mutex)

Per-frame timing diagnostics logged once per second: FPS, input, scene, submit, render, publish, flush.

Thread affinity: both renderer creation (`InitializeRenderer()`) and destruction (`ShutdownRenderer()`) happen on the compositor thread to satisfy Filament's requirement that its Engine is created and destroyed on the same thread.

### Members

| Member | Type | Purpose |
|--------|------|---------|
| `m_tmNow` | `int64_t` | Current animation time |
| `m_tpLastFrame` | `steady_clock::time_point` | Last frame timestamp |
| `m_nFrameCount` | `int` | Frames since last FPS report |
| `m_dFpsAccum` | `double` | Accumulated frame time |
| `m_dAccumInput` | `double` | Input/camera section time |
| `m_dAccumScene` | `double` | Scene build section time |
| `m_dAccumSubmit` | `double` | ANARI submit section time |
| `m_dAccumRender` | `double` | ANARI render section time |
| `m_dAccumPublish` | `double` | Framebuffer publish section time |
| `m_dAccumFlush` | `double` | DwmFlush section time |

## AGENT::SCRUBBER

Disk cleanup agent. Drains the cleanup queue on wake. Main pattern:

```
Main():
   Ready()
   Wait([this] { return DrainQueue(); })
```

`DrainQueue()` calls `m_pControl->Cleanup_SwapQueue(aPath)` to take ownership of pending paths, validates each path contains the Transitory marker, then calls `std::filesystem::remove_all`. Returns `IsShutdown()`.

## AGENT::C, D, E

Placeholder agents. Same signal-driven pattern with empty `Tick()` that returns `IsShutdown()`. Reserved for future subsystems (animator, spatial indexer, audio, etc.).

## Files

| File | Contents |
|------|----------|
| `Control.h` | All declarations: CONTROL, AGENT, COMPOSITOR, SCRUBBER, C, D, E |
| `Control.cpp` | CONTROL implementation, agent factory table, metronome loop |
| `Agent.cpp` | AGENT base class (constructor, destructor, Engine() accessor) |
| `Compositor.cpp` | COMPOSITOR (render loop, viewport iteration, timing) |
| `Scrubber.cpp` | SCRUBBER (DrainQueue, path validation, remove_all) |
| `AgentC.cpp` | Agent C placeholder |
| `AgentD.cpp` | Agent D placeholder |
| `AgentE.cpp` | Agent E placeholder |
