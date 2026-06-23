---
title: CONTEXT (class reference)
tier: API
audience: [integrator, contributor]
sources:
  - include/Context.h
  - include/Sneeze.h
  - src/context/Context.cpp
verified: 92fdc1c
nav:
  prev: api/context/index.md
  next: api/container/index.md
---

# `CONTEXT`

One browsing session — the engine's equivalent of a browser tab. A `CONTEXT` owns the five per-session subsystems ([`CONSOLE`](../console/index.md), [`NETWORK`](../network/index.md), [`STORAGE`](../storage/index.md), [`SCENE`](../scene/index.md), [`VIEWPORT`](../viewport/index.md)) and pools the [`CONTAINER`](../container/index.md)s that give content its runtime identity. It is the object a host navigates and reads frames through. For the conceptual picture see the [Context system](../../systems/context.md) page; this page is the exact behavior of every public member.

```cpp
class CONTEXT
{
public:
   enum eSESSION
   {
      kSESSION_PERSISTENT,
      kSESSION_TRANSITORY,
   };

   CONTEXT (ENGINE* pEngine, ICONTEXT* pHost, eSESSION kSession,
            const std::string& sPath_Permanent, const std::string& sPath_Temporary);
   ~CONTEXT ();
   // ... see sections below
private:
   class Impl;
   Impl* m_pImpl;
};
```

`CONTEXT` is non-copyable and non-movable (its copy/move constructors and assignment operators are deleted).

---

## Role and ownership

- **Owned by** the [`ENGINE`](../sneeze/index.md), which constructs it inside `Context_Open` and destroys it inside `Context_Close`.
- **Owns** one each of `CONSOLE`, `NETWORK`, `STORAGE`, `SCENE`, and `VIEWPORT`, created in that order during `Initialize` and destroyed in reverse in the destructor.
- **Owns** a pool of `CONTAINER` objects, keyed by container identity ([`CID::Key()`](../container/CID.md)), in an `unordered_map`. The map is the authoritative owner of every container in the session.
- **Holds** a back-pointer to its `ENGINE` and to the host's `ICONTEXT`, plus the two on-disk paths and the session kind it was constructed with.
- **Reaches** the WASM runtime through the engine (`WasmRuntime()` forwards to `ENGINE::WasmRuntime`); it caches no copy of engine-owned services.

---

## Lifecycle

A context is created, initialized, navigated, and destroyed — normally all through the engine, never by the application directly.

1. **Construct.** `ENGINE::Context_Open` builds the `CONTEXT` with the engine pointer, the host `ICONTEXT`, the session kind, and the permanent/temporary paths. The constructor only stores these; it builds no subsystems.
2. **Initialize.** `Initialize(sUrl, bReset)` builds the five subsystems in dependency order (console → network → storage → scene → viewport), aborting and reporting if any fails. Initializing the scene begins the asynchronous load of `sUrl`.
3. **Navigate.** `Url`, `Reload`, and `Logout` move the session; `Url` and `Reload` destroy and rebuild the scene.
4. **Destruct.** `~CONTEXT()` tears the subsystems down in reverse order, then frees the container pool.

---

## Threading, locking, and pitfalls

This is the part to read before calling anything.

**The container pool is guarded by a recursive mutex.** `m_mxContainer` is a `std::recursive_mutex`, held by `Container_Open` and `Container_Close`. It is recursive because closing a container can re-enter the pool's locked paths on the same thread through the scene teardown cascade. The map lookup/insert/erase all run under it, so concurrent `Container_Open` calls (from different fetch completions) are serialized.

**`Scene()` is not stable across navigation.** `Url` and `Reload` `delete` the current scene and construct a new one, replacing the internal pointer. Any `SCENE*` captured before a navigation **dangles** afterward. Re-fetch it from `Scene()` after navigating.

**The other subsystem accessors return unlocked, lifetime-stable pointers.** `Console()`, `Network()`, `Storage()`, `Viewport()`, `Host()`, and `Engine()` return pointers that are valid from the end of `Initialize` to the start of destruction. They are not lock-protected; the subsystems behind them carry their own internal synchronization.

**`Container_Open` / `Container_Close` are engine-internal.** They are called by the scene during fabric loading and teardown, not by the application. `Container_Close` only decrements one reference — it does not remove the container from the pool; the pool is only emptied when the context is destroyed.

**Navigation is not render-synchronized and does not cancel fetches.** `Url` deactivates and reactivates the viewport around the scene rebuild, but does not coordinate with an in-flight render traversal or cancel outstanding fetches into the old scene. See [Current limitations](../../systems/context.md#current-limitations).

---

## Construction and destruction

```cpp
CONTEXT (ENGINE* pEngine, ICONTEXT* pHost, eSESSION kSession,
         const std::string& sPath_Permanent, const std::string& sPath_Temporary);
~CONTEXT ();
```

### `CONTEXT(pEngine, pHost, kSession, sPath_Permanent, sPath_Temporary)`
- **Purpose.** Construct a context bound to an engine and a host interface. Stores its inputs; builds no subsystems (call `Initialize`).
- **Parameters.**
- `pEngine` — the owning engine (required; must outlive the context).
- `pHost` — the host's `ICONTEXT` implementation, which receives inspector callbacks (container/file/silo/console-entry created and deleted).
- `kSession` — `kSESSION_PERSISTENT` or `kSESSION_TRANSITORY`.
- `sPath_Permanent` — the on-disk location for durable per-session data.
- `sPath_Temporary` — the on-disk location for scratch and cache.
- **Note.** Constructed by `ENGINE::Context_Open`; do not construct directly.

### `~CONTEXT()`
- **Purpose.** Destroy the context. Tears down the subsystems in reverse of init order — viewport, then scene (whose deletion cascades through fabrics, releasing their container references), then any remaining pooled containers, then storage, network, and console.
- **Pitfalls.** Invoked by `ENGINE::Context_Close`. A pooled container still holding references after the scene cascade logs an error from the container's own destructor.

---

## Initialization

```cpp
bool Initialize (const std::string& sUrl, bool bReset = false);
```

### `bool Initialize (const std::string& sUrl, bool bReset = false)`
- **Purpose.** Build the session's subsystems and begin loading `sUrl`. Creates and initializes `CONSOLE`, `NETWORK`, `STORAGE`, `SCENE`, and `VIEWPORT` in that order; initializing the scene starts the asynchronous fabric load at `sUrl`.
- **Parameters.** `sUrl` — the start address (may be empty for an empty session); `bReset` — passed to the network layer to request a clean cache.
- **Returns.** `true` if all five subsystems initialized; `false` (with a logged reason) if any failed.
- **Notes.** Call once, after construction. On failure the partially built context should be destroyed.

---

## Navigation

```cpp
bool Reload (bool bReset = false);
bool Url    (const std::string& sUrl, bool bReset = false);
void Logout ();
```

### `bool Url (const std::string& sUrl, bool bReset = false)`
- **Purpose.** Navigate to a new address. Deactivates the viewport, deletes the current scene (cascading teardown of all loaded content), constructs a fresh `SCENE`, initializes it at `sUrl`, and reactivates the viewport on the same host surface.
- **Parameters.** `sUrl` — the new address; `bReset` — request a clean start that bypasses cached data.
- **Returns.** `true` if the new scene initialized; `false` otherwise. No-op returning `false` if there is no current scene.
- **Pitfalls.** Invalidates any previously captured `Scene()` pointer. Not synchronized with an in-progress render traversal and does not cancel in-flight fetches.

### `bool Reload (bool bReset = false)`
- **Purpose.** Re-navigate to the current address. Reads the root fabric's URL (copying it, since the fabric is about to be destroyed) and calls `Url` with that copy.
- **Parameters.** `bReset` — forwarded to `Url`.
- **Returns.** What `Url` returns; `false` if there is no current scene.

### `void Logout ()`
- **Purpose.** Clear the network layer's state (`NETWORK::Clear`), discarding session-bound fetched data. Does not rebuild the scene.
- **Returns.** Nothing.

---

## Accessors

```cpp
ICONTEXT*           Host           () const;
ENGINE*             Engine         () const;
CONSOLE*            Console        () const;
NETWORK*            Network        () const;
STORAGE*            Storage        () const;
DEP::WASM_RUNTIME*  WasmRuntime    () const;
VIEWPORT*           Viewport       () const;
SCENE*              Scene          () const;
const std::string&  Path_Permanent () const;
const std::string&  Path_Temporary () const;
```

| Accessor | Returns | Notes |
|---|---|---|
| `Host()` | The host's `ICONTEXT`. | Set at construction; receives the inspector callbacks. |
| `Engine()` | The owning engine. | Never null for a live context. |
| `Console()` | The session's console. | Valid after `Initialize`. |
| `Network()` | The session's network subsystem. | Valid after `Initialize`. |
| `Storage()` | The session's storage subsystem. | Valid after `Initialize`. |
| `WasmRuntime()` | The engine's WASM runtime. | Forwarded from the engine, not owned by the context. |
| `Viewport()` | The session's viewport. | Valid after `Initialize`. |
| `Scene()` | The session's scene. | **Replaced by `Url`/`Reload`** — do not cache across navigation. |
| `Path_Permanent()` | The durable data path (by const reference). | Set at construction. |
| `Path_Temporary()` | The scratch/cache path (by const reference). | Set at construction. |

---

## Container management (internal)

Engine-internal. The scene calls these during fabric loading and teardown; an application does not.

```cpp
CONTAINER* Container_Open  (MSF* pMsf);
void       Container_Close (CONTAINER* pContainer);
```

### `CONTAINER* Container_Open (MSF* pMsf)`
- **Purpose.** Get (pooling if possible) the container for a source's verified MSF. Builds a [`CID`](../container/CID.md) from the MSF — fingerprint, organization, container name, persona hash, and a trust level from the signature/chain checks — computes its key, and looks it up in the pool. If absent, constructs and inserts a new `CONTAINER`; if present, reuses it. Then calls `CONTAINER::Open()` to reference-count it.
- **Parameters.** `pMsf` — the parsed, verified MSF; `nullptr` builds the synthetic root container (trust `kTRUST_ROOT`, name "Root").
- **Returns.** The opened `CONTAINER*`, or `nullptr` if `Open()` failed (in which case the new entry is removed and the container deleted).
- **Ownership.** The context owns the container (via the pool); the caller holds one open reference and must balance it with `Container_Close`.
- **Pitfalls.** Takes the recursive container mutex. Note the current trust override that forces `kTRUST_EXPIRED` (see [Container → Trust levels](../../systems/container.md#trust-levels)).

### `void Container_Close (CONTAINER* pContainer)`
- **Purpose.** Release one reference to a pooled container by calling `CONTAINER::Close()`.
- **Parameters.** `pContainer` — the container to release (must be non-null).
- **Returns.** Nothing.
- **Pitfalls.** Takes the recursive container mutex. Decrements the refcount only — it does **not** remove the container from the pool. Pooled containers are freed only when the context is destroyed.

---

## See also

- [Context system](../../systems/context.md) — design, init/teardown order, pooling.
- [Container API](../container/index.md) — `CONTAINER` and the `CID` identity record.
- [sneeze API](../sneeze/index.md) — `ENGINE::Context_Open` / `Context_Close`, `ICONTEXT`.
- [Scene API](../scene/index.md) — the scene a context owns and rebuilds on navigation.

---

[Context API](index.md) · Next: [Container API](../container/index.md)
