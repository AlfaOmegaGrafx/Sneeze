# Control — Engine Thread, Agent Pools, and Metronome

The `control` module owns the engine thread, agent pools, the drift-free
metronome, and the job queues. It is the engine's scheduler.

## Architecture

```
CONTROL (inherits THREAD)
 ├── Engine thread (Main)
 ├── Metronome (drift-free, fixed-origin, 1ms resolution)
 ├── Pool factory table (AGENT_INIT)
 └── Pool vector (m_apPool)

POOL (base class, non-template)
 ├── Agent vector (m_apAgent)
 ├── Metronome state (nHertz, nLastTick)
 └── Lifecycle (Initialize, ~POOL tears down agents)

POOL_QUEUE<JOB_PTR> (template, inherits POOL)
 ├── Typed job queue (m_apJob, m_mxQueue)
 ├── Post (targeted wake of first idle agent)
 └── Grab (pops next job)

POOL_CYCLE (inherits POOL)
 ├── Perpetual job pool for compositor
 ├── Post/Remove lifecycle
 └── Grab routes Create/Destroy to agent 0 (Filament thread affinity)

AGENT (inherits THREAD, abstract base)
 ├── COMPOSITOR  (queue-driven via POOL_CYCLE, 1 agent)
 ├── SCRUB       (queue-driven via POOL_QUEUE, 2 agents)
 ├── FETCH       (queue-driven via POOL_QUEUE, 16 agents)
 └── C           (signal-driven, 30 Hz placeholder)
```

## Ownership Chain

```
ENGINE -> CONTROL -> POOL -> AGENT
```

Each child holds a pointer to its immediate owner. Agents access engine services
via `m_pPool->Engine()`.

## Job Classes

All jobs derive from `IJOB` (Cancel/IsCancelled/Complete). Concrete jobs are
heap-allocated and self-cleaning (`Release()` calls `delete this`).

| Job | Agent | Key Fields |
|-----|-------|------------|
| `JOB_FETCH` | FETCH | `Url()`, `Path_Temp()`, `Path_Data()`, `Hash()`, `IsFetch()` |
| `JOB_SCRUB` | SCRUB | `Path()` |
| `JOB_COMPOSITOR` | COMPOSITOR | `Viewport()`, state machine, `m_nLastFrame` |

`JOB_FETCH` carries `bool m_bFetch` to distinguish real HTTP fetches from
notify-only jobs (asynchronous notifications for cached/failed files).

`JOB_COMPOSITOR` has a state machine: kSTATE_CREATE -> kSTATE_RENDER ->
kSTATE_PRESENT -> kSTATE_DESTROY. `Cancel()` blocks until the compositor
thread completes destruction on agent 0.

## Pool Configuration

| Index | Pool Type | Agent | Size | Hz | Behavior |
|-------|-----------|-------|------|----|----------|
| 0 | POOL_CYCLE | COMPOSITOR | 1 | 0 | Queue-driven (Filament thread affinity) |
| 1 | POOL_QUEUE\<JOB_SCRUB*\> | SCRUB | 2 | 0 | Queue-driven (disk cleanup) |
| 2 | POOL_QUEUE\<JOB_FETCH*\> | FETCH | 16 | 0 | Queue-driven (HTTP downloads) |
| 3 | POOL | C | 1 | 30 | Metronome-driven placeholder |

## CONTROL

Inherits THREAD. Constructor takes `ENGINE*`. `Main()` creates pools from the
static `AGENT_INIT` factory table, then runs the drift-free metronome loop
(`Wait(1ms)`, fixed-origin scheduling, `timeBeginPeriod(1)` on Windows).

Public API:
- `Queue_Post_Fetch(JOB_FETCH*)` — routes to fetch pool
- `Queue_Post_Scrub(JOB_SCRUB*)` — routes to scrub pool
- `Queue_Post_Compositor(JOB_COMPOSITOR*)` — routes to compositor pool
- `Engine()` — returns `m_pEngine`

## AGENT Patterns

### Queue-Driven (SCRUB, FETCH)

```
Main():  Ready() → Wait([this] { return Job(); })
```

`Job()` loops: Grab from queue, process, Release. Returns `IsShutdown()`.

### Signal-Driven (C)

```
Main():  Ready() → Wait([this] { return Tick(); })
```

### AGENT::COMPOSITOR

Queue-driven via POOL_CYCLE. `Job()` grabs the best available
JOB_COMPOSITOR and dispatches by state:

- **Execute_Create** (agent 0 only) — calls `Viewport::Renderer_Initialize()`
- **Execute_Render** (any agent) — consumes input, updates camera, builds scene,
  calls renderer BeginFrame/EndFrame
- **Execute_Present** (any agent) — readback framebuffer, calls `OnFrameReady`,
  runs `Diagnostics()`
- **Execute_Destroy** (agent 0 only) — calls `Viewport::Renderer_Shutdown()`

### AGENT::FETCH

Checks `IsFetch()`: if true, performs blocking curl download with SRI hash
verification; if false (notify-only), skips network work. Curl progress
callback checks `IsCancelled()` to abort in-flight downloads.

## Files

| File | Contents |
|------|----------|
| `Control.h` | All declarations: IJOB, JOB_FETCH, JOB_SCRUB, JOB_COMPOSITOR, POOL, POOL_QUEUE, POOL_CYCLE, CONTROL, AGENT + derived |
| `Control.cpp` | CONTROL implementation, pool factory table, metronome |
| `Pool.cpp` | POOL base class |
| `Pool_Queue.cpp` | POOL_QUEUE template + explicit instantiations |
| `Pool_Cycle.cpp` | POOL_CYCLE (perpetual compositor pool) |
| `Agent.cpp` | AGENT base class |
| `Compositor.cpp` | AGENT::COMPOSITOR (render loop, state machine) |
| `Scrub.cpp` | AGENT::SCRUB (disk cleanup) |
| `Fetch.cpp` | AGENT::FETCH (HTTP downloads) |
| `AgentC.cpp` | AGENT::C placeholder |
| `IJob.cpp` | IJOB base (Cancel/IsCancelled/Complete) |
