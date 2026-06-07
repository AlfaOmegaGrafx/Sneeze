# WASM — WebAssembly Sandbox

The `wasm` module provides the sandboxed execution environment for WebAssembly
modules loaded from MSF fabric payloads. It manages the Wasmtime engine,
isolated stores, compiled instances, and host function bindings.

## Architecture

```
WASM_RUNTIME (owns wasm_engine_t)
 ├── WASM_STORE (one per container identity)
 │    ├── WASM_INSTANCE (url + sha256)
 │    └── WASM_INSTANCE (url + sha256)
 └── WASM_STORE (another container)
```

## WASM_RUNTIME

Top-level manager. Owns the shared Wasmtime engine and all active stores.

```cpp
DEP::WASM_RUNTIME runtime;
runtime.Initialize ();

auto* pStore = runtime.Store_Open ();
// ... add instances ...
runtime.Store_Close (pStore);
```

## WASM_STORE

Isolated execution context identified by (persona hash, fingerprint, container
name). Multiple fabrics from the same organization and container share one store.

`Fabric_AddRef()` / `Fabric_ReleaseRef()` track fabric usage. When refcount
reaches zero, the store is eligible for destruction.

## WASM_INSTANCE

A single compiled WASM module within a store. Identity is URL + SHA-256.

Lifecycle:
1. **Compile** (engine, bytes, size)
2. **Open** (fabricIx, params) — refcount 0->1 fires Initialize, then Open
3. **Close** (fabricIx) — Close, then refcount 1->0 fires Finalize

## Host Functions

32 host functions registered with the Wasmtime linker, organized by module:

### Console (module: "Console")

| Function | Signature | Description |
|----------|-----------|-------------|
| `Console_Log` | (ptr, len) | Log message |
| `Console_Debug` | (ptr, len) | Debug message |
| `Console_Info` | (ptr, len) | Info message |
| `Console_Warn` | (ptr, len) | Warning |
| `Console_Error` | (ptr, len) | Error |
| `Console_Assert` | (cond, ptr, len) | Conditional error |
| `Console_Group` | (ptr, len) | Open group |
| `Console_GroupCollapsed` | (ptr, len) | Open collapsed group |
| `Console_GroupEnd` | () | Close group |
| `Console_Count` | (ptr, len) | Increment counter |
| `Console_CountReset` | (ptr, len) | Reset counter |
| `Console_Time` | (ptr, len) | Start timer |
| `Console_TimeEnd` | (ptr, len) | Stop timer |
| `Console_TimeLog` | (ptr, len) | Log timer |

### Storage (module: "Storage")

| Function | Description |
|----------|-------------|
| `Storage_Get` | Read key from container scope |
| `Storage_Set` | Write key to container scope |
| `Storage_Remove` | Delete key |
| `Storage_Has` | Check key existence |
| `Storage_GetJson` | Bulk JSON read |
| `Storage_SetJson` | Bulk JSON write |

### Scene (module: "Scene")

| Function | Description |
|----------|-------------|
| `Scene_Node_Root` | Get root node index |
| `Scene_Node_Open` | Create a node |
| `Scene_Node_Close` | Remove a node |
| `Scene_Node_Position` | Set position |
| `Scene_Node_Scale` | Set scale |
| `Scene_Node_Bound` | Set bounding sphere |
| `Scene_Node_Color` | Set color |
| `Scene_Node_Name` | Set name |
| `Scene_Node_Radius` | Set radius |
| `Scene_Node_Texture` | Set texture URL |

### Timer (module: "Timer")

| Function | Description |
|----------|-------------|
| `Timer_Set` | Schedule a callback |
| `Timer_Clear` | Cancel a scheduled callback |

All host functions receive the store pointer as `pEnv`, providing access to
the calling store's identity for access control and storage scoping.
`ReadWasmString()` extracts a string from WASM linear memory.

## Dependencies

- **Wasmtime** v43.0.0 — C API for WASM compilation and execution.

## Files

| File | Contents |
|------|----------|
| `Wasm.h` | WASM_RUNTIME, WASM_STORE, WASM_INSTANCE declarations |
| `WasmRuntime.cpp` | WASM_RUNTIME implementation |
| `WasmStore.cpp` | WASM_STORE implementation |
| `WasmInstance.cpp` | WASM_INSTANCE implementation |
| `HostFunctions.h` | Host function declarations (32 functions) |
| `HostFunctions.cpp` | Host function implementations |
| `ThreadPool.h/cpp` | Fixed-size worker pool for parallel WASM execution |
