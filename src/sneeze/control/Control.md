# Control - Engine Thread, Agent Pools, and Metronome

The `control` module (`SNEEZE::CONTROL` + `SNEEZE::AGENT` hierarchy) owns the engine thread, agent pools, the drift-free metronome, and the job queues. It is the engine's scheduler.

## Architecture

```
CONTROL (inherits THREAD)
 ├── Engine thread (Main)
 ├── Metronome (drift-free, fixed-origin, 1ms resolution)
 ├── Pool factory table (AGENT_INIT)
 └── Pool vector (m_apPool)

POOL (base class, non-template)
 ├── Agent vector (m_apAgent)
 ├── Metronome state (nHertz, nLastTick, nSignalCount)
 └── Lifecycle (Initialize, Shutdown via destructor)

POOL_QUEUE<JOB_PTR> (template, inherits POOL)
 ├── Typed job queue (m_apJob, m_mxQueue)
 ├── Post (targeted wake of first non-busy agent)
 └── Grab (pops next job; caller updates AGENT::m_bBusy from Grab result)

AGENT (inherits THREAD, abstract base)
 ├── m_bBusy (atomic; queue workers and Post reserve / release around work)
 ├── COMPOSITOR  (self-paced, DwmFlush / 16ms sleep)
 ├── SCRUB       (queue-driven, drains ISCRUB jobs)
 ├── FETCH       (queue-driven, drains IFETCH jobs)
 └── C           (signal-driven, 30 Hz placeholder)
```

## Ownership Chain

```
ENGINE -> CONTROL -> POOL -> AGENT
```

Each child holds a pointer to its immediate owner. Agents access engine services by following the chain: `m_pPool->Engine()` (which delegates to CONTROL's engine).

## Job Interfaces

All jobs derive from `IJOB` (base interface with `Cancel()`, `IsCancelled()`, `Release()`). Concrete jobs are heap-allocated and self-cleaning (`Release()` calls `delete this`). Callers create a job, post it, and walk away.

| Interface | Concrete | Agent | Accessors | Callback |
|-----------|----------|-------|-----------|----------|
| `IFETCH` | `JOB_FETCH` | `AGENT::FETCH` | `Url()`, `Path_Temp()`, `Path_Data()`, `Hash()` | `OnFetch_Complete(FETCH_RESULT)` |
| `ISCRUB` | `JOB_SCRUB` | `AGENT::SCRUB` | `Path()` | `OnScrub_Complete()` |

Naming is symmetric: `JOB_FETCH -> IFETCH -> AGENT::FETCH`, `JOB_SCRUB -> ISCRUB -> AGENT::SCRUB`.

### Cancellation

Jobs support cancellation via `Cancel()` (sets `std::atomic<bool>`) and `IsCancelled()`. The caller calls `pJob->Cancel()` and walks away. The agent eventually grabs the job, sees the flag, skips work, and calls `Release()`.

## THREAD Base Class

Declared in `sneeze/Engine.h`, implemented in `sneeze/Thread.cpp`. Encapsulates all threading primitives. Every managed thread in the engine (CONTROL + all AGENTs) inherits THREAD.

### Lifecycle

1. `Initialize()` - spawns the thread (`Main()`), blocks caller until the thread calls `Ready()`
2. `Main()` - pure virtual, the thread's entry point
3. `Ready(bResult)` - called by the thread to signal the initializing caller that startup is complete
4. `Signal(bShutdown)` - wakes the thread; if `bShutdown == true`, latches the shutdown flag
5. `Join()` - calls `Signal(true)` then joins the thread. Must be called in every derived destructor.
6. `~THREAD()` - calls `Join()` then deletes the thread (safety net)

### Wait Contract

`Wait` has two overloads. Neither reads `m_bShutdown` or calls `IsShutdown()`:

- **Predicate:** `Wait(std::function<bool()> fnWork)` - wraps `m_cvThread.wait(lock, fnWork)`. The predicate returns `true` to stop waiting.
- **Timed:** `Wait(std::chrono::milliseconds duration)` - wraps `m_cvThread.wait_for(lock, duration)`.

Shutdown policy belongs in derived `Main()` implementations and in predicate callables, not in the base class.

## POOL

Non-template base class. Manages a vector of agents, owns agent lifecycle (create, initialize, shutdown, delete), and handles its own metronome tick scheduling.

### Lifecycle

- `Initialize(nHertz, nAgents, fnCreate)` - stores Hz, creates and initializes agents
- `~POOL()` - signals shutdown on all agents, deletes them, clears `m_apAgent`
- `Tick(dElapsed)` - computes whether a tick is due based on Hz and elapsed time; if so, increments signal count and signals every agent in `m_apAgent`. No-ops if Hz is 0.

### Members

| Member | Type | Purpose |
|--------|------|---------|
| `m_pControl` | `CONTROL*` | Back-pointer to owning CONTROL |
| `m_apAgent` | `vector<AGENT*>` | Agents in this pool |
| `m_nHertz` | `int` | Metronome frequency (0 = not metronome-driven) |
| `m_nLastTick` | `int64_t` | Last metronome tick counter |
| `m_nSignalCount` | `int` | Signals since last report |

### Accessors

- `Engine()` - delegates to `m_pControl->Engine()`
- `Hertz()` - returns `m_nHertz`
- `SignalCount()` / `SignalCount_Reset()` - for metronome diagnostics reporting

## POOL_QUEUE\<JOB_PTR\>

Template subclass of POOL. Adds a typed, thread-safe job queue.

- `Post(JOB_PTR pJob)` - appends to queue under `m_mxQueue`, then `Busy()` on each agent in order until one returns `true` (idle→busy via CAS); that agent is `Signal()`d. If every `Busy()` returns `false` (already busy), no signal — workers self-schedule on the next `Grab`.
- `Grab(JOB_PTR& pJob)` - under `m_mxQueue`, pops front if available and returns `true`. On empty queue returns `false` and assigns `pJob = nullptr` (for pointer job types). The caller updates `m_bBusy` from the boolean result while draining (`release` stores).

Implementations and explicit instantiations (`IFETCH*`, `ISCRUB*`) live in `Pool_Queue.cpp`.

## CONTROL

Inherits THREAD. Constructor takes `ENGINE*`. Owns the engine thread, pool vector, and the metronome.

### Initialization

`Initialize(int& nAgentCount)` calls `THREAD::Initialize()` which spawns the engine thread (`Main()`). On return, `nAgentCount` is populated with the total number of agents across all pools.

### Main() - Engine Thread Lifecycle

1. **Create pools** from the static `AGENT_INIT` factory table. Each entry specifies `fnCreate_Pool` (creates the pool) and `fnCreate_Agent` (creates agents within the pool).
2. Call `Ready(bInitialized)` to unblock the caller
3. **Metronome loop** (drift-free, fixed-origin scheduling):
   - `if (!IsShutdown()) { ...tick pools with nHertz > 0... } else break;`
   - `Wait(std::chrono::milliseconds(1))` (1ms resolution via `timeBeginPeriod(1)` on Windows)
4. **Shutdown**: `delete` each pool; `~POOL()` tears down agents

### Pool Configuration Table

| Index | Pool Type | Agent | Pool Size | Hz | Behavior |
|-------|-----------|-------|-----------|----|----------|
| 0 | POOL | COMPOSITOR | 1 | 0 | Self-paced (DwmFlush) |
| 1 | POOL_QUEUE\<ISCRUB*\> | SCRUB | 2 | 0 | Queue-driven (disk cleanup) |
| 2 | POOL_QUEUE\<IFETCH*\> | FETCH | 16 | 0 | Queue-driven (HTTP downloads) |
| 3 | POOL | C | 1 | 30 | Metronome-driven placeholder |

### Public API

| Method | Delegates to |
|--------|-------------|
| `Queue_Post_Fetch(IFETCH*)` | `Pool_Fetch().Post(pFetch)` |
| `Queue_Post_Scrub(ISCRUB*)` | `Pool_Scrub().Post(pScrub)` |
| `Engine()` | Returns `m_pEngine` |

### Members

| Member | Type | Purpose |
|--------|------|---------|
| `m_pEngine` | `ENGINE*` | Back-pointer to engine |
| `m_apPool` | `vector<POOL*>` | All pools |

## AGENT

Abstract base class for engine agent threads. Inherits THREAD. Constructor takes `(POOL* pPool, int nAgentIz)`.

- `Engine()` delegates to `m_pPool->Engine()` -- agents follow the ownership chain
- `Busy()` — `compare_exchange_strong` on `m_bBusy`: if `false`, sets `true` and returns **`true`** (claimed for `Post` / `Signal`); if already `true`, returns **`false`**. Only `POOL_QUEUE::Post` calls this; workers update `m_bBusy` from `Grab` in `Job()`.
- `Main()` is pure virtual -- each derived agent implements its own thread loop
- Agent index is per-pool, not global

### Queue-Driven Pattern (SCRUB, FETCH)

```
Main():
   Ready()
   Wait([this] { return Job(); })
```

`Job()` loops: `Grab` under the queue mutex; on failure `m_bBusy.store(false, release)` and exit; on success `m_bBusy.store(true, release)`, then process the job (`IsCancelled()`, `OnFetch_Complete` / `OnScrub_Complete`, `Release()`). Returns `IsShutdown()`.

### Signal-Driven Pattern (C)

```
Main():
   Ready()
   Wait([this] { return Tick(); })
```

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

The rendering agent. Self-paced via `DwmFlush()` (Windows) or `sleep_for(16ms)`. Does NOT own a renderer or camera -- those are per-viewport.

Each iteration:
1. Acquire viewport list via `Engine()->Viewport_Capture()`
2. For each viewport (skip if `!IsReady()`):
   - Service any pending renderer shutdown (`ServiceRendererShutdown()`)
   - Consume input, update camera orbit
   - Call `Viewport_Render(pViewport, tpLoopStart)`
3. Release viewport list via `Engine()->Viewport_Release()`
4. `DwmFlush()` (always outside viewport mutex)

Per-frame timing diagnostics logged once per second: FPS, input, scene, submit, render, publish, flush.

Thread affinity: both renderer creation (`InitializeRenderer()`) and destruction (`ShutdownRenderer()`) happen on the compositor thread to satisfy Filament's requirement.

## AGENT::SCRUB

Disk cleanup agent. Queue-driven via `POOL_QUEUE<ISCRUB*>`.

`Job()` dequeues `ISCRUB*` jobs, validates each path contains the Transitory marker, then calls `std::filesystem::remove_all`. Calls `OnScrub_Complete()` and `Release()` on each job. Returns `IsShutdown()`.

## AGENT::FETCH

HTTP download agent. Queue-driven via `POOL_QUEUE<IFETCH*>`.

`Job()` dequeues `IFETCH*` jobs. `Execute()` performs the blocking curl download: streams response to a temp file, captures HTTP headers and status, verifies SRI hash if provided, renames temp to final path on success. Calls `OnFetch_Complete(FETCH_RESULT)` and `Release()` on each job. Returns `IsShutdown()`.

Curl progress callback checks `IsCancelled()` to abort in-flight downloads.

## AGENT::C

Placeholder agent. Signal-driven at 30 Hz with empty `Tick()` that returns `IsShutdown()`. Reserved for future subsystems (animator, spatial indexer, audio, etc.).

## Files

| File | Contents |
|------|----------|
| `Control.h` | Declarations: IJOB, IFETCH, ISCRUB, JOB_FETCH, JOB_SCRUB, POOL, POOL_QUEUE, CONTROL, AGENT, COMPOSITOR, SCRUB, FETCH, C |
| `Control.cpp` | CONTROL implementation, pool factory table, metronome loop |
| `Pool.cpp` | POOL implementation |
| `Pool_Queue.cpp` | `POOL_QUEUE` method bodies + explicit instantiations for `IFETCH*` / `ISCRUB*` |
| `Agent.cpp` | AGENT base class (constructor, destructor, Engine() accessor) |
| `Compositor.cpp` | COMPOSITOR (render loop, viewport iteration, timing) |
| `Scrub.cpp` | SCRUB (Job, path validation, remove_all) |
| `Fetch.cpp` | FETCH (Job, Execute, curl logic, hash verification) |
| `AgentC.cpp` | Agent C placeholder |
