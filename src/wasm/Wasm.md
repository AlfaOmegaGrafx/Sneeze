# WASM — WebAssembly Host Container

The `wasm` module provides the sandboxed execution environment for
WebAssembly modules loaded from MSF fabric payloads. It manages the
Wasmtime engine, isolated stores, compiled instances, host function
bindings, and a thread pool for parallel execution.

## Architecture

```
WASM_RUNTIME (owns wasm_engine_t)
  ├─ WASM_STORE (persona + fingerprint + container)
  │    ├─ WASM_INSTANCE (url + sha256) — refcount: 2
  │    └─ WASM_INSTANCE (url + sha256) — refcount: 1
  ├─ WASM_STORE (another identity triple)
  │    └─ ...
  └─ THREAD_POOL (N worker threads)
```

## WASM_RUNTIME

Top-level manager. Owns the shared Wasmtime engine and all active stores.

```cpp
#include "wasm/WasmRuntime.h"

wasm::WASM_RUNTIME runtime;
runtime.Initialize ();

wasm::STORE_IDENTITY id;
id.sPersonaHash = "a3f1...";
id.sFingerprint = "b2c4...";
id.sContainer   = "poker";

auto* pStore = runtime.FindOrCreateStore (id);
// ... add instances, bump fabric refs ...

runtime.DestroyAllStores ();   // teardown
runtime.Shutdown ();
```

## WASM_STORE

An isolated execution context identified by the triple
(persona hash, fingerprint, container name). Multiple fabrics from the same
organization and container share one store.

### Fabric Reference Counting

Every fabric that maps to a store calls `AddFabricRef()`. When a fabric is
detached, it calls `ReleaseFabricRef()`. When the refcount reaches zero,
the store is eligible for destruction.

```cpp
pStore->AddFabricRef ();      // fabric loaded
pStore->ReleaseFabricRef ();  // fabric unloaded
```

## WASM_INSTANCE

A single compiled WASM module within a store. Identity is URL + SHA-256 —
identical bytecode from different URLs are treated as separate instances.

### Lifecycle

```
   Compile (pEngine, pBytes, nSize)     compile the .wasm bytecode
        │
        ▼
   AddRef()  ──► refcount 0→1 ──► CallInit()     first fabric references it
        │
        ▼
   CallOpen (twFabricId, pParams, n)              per-fabric attachment
        │
        ▼
   CallClose (twFabricId)                         per-fabric detachment
        │
        ▼
   ReleaseRef()  ──► refcount 1→0 ──► CallShutdown()   instance goes dormant
```

Instances cannot be unloaded from a live store. When dormant they simply
stop receiving calls.

### Compilation

```cpp
auto* pInstance = new wasm::WASM_INSTANCE (pStore, sUrl, sSha256);
pInstance->Compile (runtime.GetEngine (), pBytes, nBytes);
pStore->AddInstance (pInstance);
```

## Host Functions

C-linkage callbacks exposed to WASM modules via the Wasmtime linker.
Naming convention: `Concept_Action`.

### SOM Functions

| Function             | Description                              |
|----------------------|------------------------------------------|
| `SOM_Node_Create`    | Create a new node in the caller's fabric |
| `SOM_Node_Remove`    | Remove a node from the SOM              |
| `SOM_Transform_Set`  | Set position/scale on a map object       |
| `SOM_Transform_Get`  | Read position/scale from a map object    |
| `SOM_Property_Set`   | Set a named property on a node           |
| `SOM_Property_Get`   | Read a named property from a node        |
| `SOM_Watch_Node`     | Register an event watcher on a node      |
| `SOM_Watch_Tree`     | Register a recursive event watcher       |

### Storage Functions

| Function          | Description                                   |
|-------------------|-----------------------------------------------|
| `Storage_Get`     | Read a key from the store's container scope    |
| `Storage_Set`     | Write a key to the store's container scope     |
| `Storage_Remove`  | Delete a key from the store's container scope  |
| `Storage_Has`     | Check if a key exists in the container scope   |

All host functions receive the store pointer as `pEnv`, providing access to
the calling store's identity for access control and storage scoping.

## Thread Pool

`THREAD_POOL` provides a fixed-size pool of worker threads for executing
WASM work items.

```cpp
#include "wasm/ThreadPool.h"

wasm::THREAD_POOL pool;
pool.Initialize ();        // defaults to hardware_concurrency - 2

pool.Submit ([] {
   // execute WASM function call
});

pool.GetPendingCount ();   // queued but not yet started
pool.Shutdown ();          // drains queue, joins all workers
```

## Dependencies

- **Wasmtime** — C API for WASM compilation and execution.

## Unimplemented / Future Work

- **Host function implementations** — all host functions are currently stubs
  that log and return null. They need to be wired to the SOM, storage, and
  event system.
- **Linker registration** — host functions are declared but not yet
  registered with the Wasmtime linker during store creation.
- **CPU budgeting** — no per-store or per-instance execution time limits.
- **Memory caps** — no per-store linear memory limits.
- **WASI integration** — no WASI imports are provided. Modules that require
  filesystem or clock access will trap.
- **Module hot-reload** — when a cached module's hash changes, the old
  instance should be shut down and a new one compiled. This path is not
  yet implemented.
