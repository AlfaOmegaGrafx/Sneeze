---
title: Context API
tier: API
audience: [integrator, contributor]
sources:
  - include/Context.h
verified: 92fdc1c
nav:
  prev: api/sneeze/IVIEWPORT.md
  next: api/context/CONTEXT.md
---

# Context API

The context subsystem's public surface is declared in `include/Context.h`. It is a
single class, `CONTEXT`, representing one browsing session — the engine's equivalent of
a browser tab. For the *architecture* — what a context owns, the order it builds and
tears down its subsystems, how it pools containers — read the
[Context system](../../systems/context.md) page. This section is the precise per-class
reference: every public method's purpose, parameters, return value, and the pitfalls
(locking, lifetime, reentrancy) to watch for when calling it.

```cpp
#include <Context.h>   // brought in transitively via <Sneeze.h>
namespace SNEEZE { ... }
```

## Classes

| Class | Page | Role |
|---|---|---|
| `CONTEXT` | [CONTEXT](CONTEXT.md) | One browsing session; owns the console, network, storage, scene, and viewport, and pools the session's containers. |

`CONTEXT` uses the pimpl idiom — it is a thin handle over a private implementation.

> **Who calls this.** A host application does not construct a `CONTEXT` directly. It
> opens one via [`ENGINE::Context_Open`](../sneeze/index.md) and closes it via
> `ENGINE::Context_Close`. Once it has the pointer, the host drives navigation
> (`Url`, `Reload`, `Logout`) and reads the owned subsystems (`Scene()`, `Viewport()`,
> …). The `Container_Open` / `Container_Close` pair is engine-internal — the scene
> calls it during fabric loading.

## The `eSESSION` enum

A context is opened as one of two session kinds, declared on `CONTEXT`:

| Value | Meaning |
|---|---|
| `kSESSION_PERSISTENT` | A session whose cache and storage are meant to survive across runs. |
| `kSESSION_TRANSITORY` | A session whose data is meant to be discarded. |

---

## See also

- [Context system](../../systems/context.md) — design, init/teardown order, pooling.
- [Container API](../container/index.md) — the identity/sandbox `Container_Open` pools.
- [sneeze API](../sneeze/index.md) — `ENGINE::Context_Open` and the `ICONTEXT` host interface.

---

[API index](../index.md) · Prev: [IVIEWPORT](../sneeze/IVIEWPORT.md) · Next: [CONTEXT](CONTEXT.md)
