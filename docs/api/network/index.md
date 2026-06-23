---
title: Network API
tier: API
audience: [integrator, contributor]
sources:
  - include/Network.h
verified: 92fdc1c
nav:
  prev: api/container/CID.md
  next: api/network/NETWORK.md
---

# Network API

The network subsystem's public surface is declared in `include/Network.h`. It is the engine's resource loader and on-disk cache: callers open a [`FILE`](FILE.md) handle for a URL, optionally register an [`IFILE`](IFILE.md) listener, and read the bytes when they arrive. For the *architecture* — why there are two object types, how the two-counter asset lifecycle works, how a fetch is dispatched and how deletion is deferred — read the [Network system](../../systems/network.md) page. This section is the precise per-class reference.

```cpp
#include <Network.h>   // brought in transitively via <Sneeze.h>
namespace SNEEZE { ... }
```

## Classes and interfaces

| Type | Page | Role |
|---|---|---|
| `NETWORK` | [NETWORK](NETWORK.md) | The subsystem entry point; opens files, manages the cache and staleness rules. One per [`CONTEXT`](../context/index.md). |
| `FILE` | [FILE](FILE.md) | A per-caller handle to a cached resource; carries a snapshot of display fields and the dual-flag deletion machinery. |
| `IFILE` | [IFILE](IFILE.md) | The completion-listener interface a caller implements (`OnFileReady` / `OnFileFailed`). Also covers `IENUM_FILE`. |

`NETWORK` and `FILE` use the pimpl idiom — each is a thin handle over a private implementation. The shared per-URL state lives in a private `ASSET` class that is not part of the public surface (it is documented conceptually in the [Network system](../../systems/network.md#the-two-core-types-file-and-asset)).

> **Who calls this.** The network surface is mostly engine-internal — the > [scene](../scene/index.md) drives MSF, WASM, and texture fetches through it — but it > is also the seam a host's developer tools attach to, via `File_Enum` and the > `ICONTEXT` file notifications. An application embedding Sneeze rarely opens files > directly.

## Enums

Defined in `Network.h` and surfaced through `FILE`.

### `eASSET_STATE`

The fetch state of a resource, reported by `FILE::State`.

| Value | Meaning |
|---|---|
| `kASSET_STATE_IDLE` | Never fetched; no request in flight. |
| `kASSET_STATE_FETCHING` | A fetch (or a notify-only completion job) is in flight. |
| `kASSET_STATE_VALIDATING` | Reserved; not used by the current flow (hashing is inline). |
| `kASSET_STATE_READY` | Bytes are cached and valid. |
| `kASSET_STATE_FAILED` | The last fetch failed. |

### `eASSET_EXT`

Selects which of an asset's three on-disk files a path refers to.

| Value | File suffix | Contents |
|---|---|---|
| `kASSET_EXT_DATA` | `.data` | The cached payload. |
| `kASSET_EXT_TEMP` | `.temp` | The in-flight download, renamed to `.data` on success. |
| `kASSET_EXT_META` | `.meta` | The JSON sidecar describing the asset. |

---

## See also

- [Network system](../../systems/network.md) — design, fetch flow, threading, and limitations.
- [Scene API](../scene/index.md) — the primary consumer of `FILE` / `IFILE`.
- [Container API](../container/index.md) — supplies the identity that keys a file's disk path.
- [Storage API](../storage/index.md) — the sibling persistence subsystem.

---

[API index](../index.md) · Prev: [CID](../container/CID.md) · Next: [NETWORK](NETWORK.md)
