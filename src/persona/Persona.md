# Persona — Local Identity Proxy

The `persona` module provides a temporary local identity used to scope WASM
stores and persistent storage. No network-based identity or authentication
is implemented — this is a local testing proxy.

## PERSONA

```cpp
#include "persona/Persona.h"

persona::PERSONA persona;

persona.Login ("Dean", "Abramson");
// persona.Name()  -> "Dean.Abramson"
// persona.Hash()  -> "a3f1..." (SHA-256 hex digest)
// persona.IsLoggedIn() -> true

persona.Logout ();
// persona.IsLoggedIn() -> false
// persona.Name()    -> ""
// persona.Hash()    -> ""
```

### How the Hash is Used

The persona hash is the first key in the identity triple that scopes WASM
stores and persistent storage:

```
Store identity  = persona_hash + fingerprint + container_name
Storage path    = %APPDATA%/Sneeze/Storage/<persona_hash>/<fingerprint>/
```

When the persona changes, all stores and storage scoped to the old persona
become unreachable. The engine's `ChangePersona()` triggers a full phased
teardown before logging in with the new identity.

### Name Rules

- Names consist of a first part and an optional second part.
- The two parts are joined with a dot: `"First.Second"`.
- If no second part is provided, the name is just the first part (no dot).
- The combined string is hashed using SHA-256 (via BoringSSL / OpenSSL).

## Dependencies

- **OpenSSL / BoringSSL** — used for SHA-256 hashing of the persona name.

## Unimplemented / Future Work

- **Real identity** — this module is a stub. A production implementation
  would verify identity via a remote identity service, JWT tokens, or
  certificate-based authentication.
- **Persona persistence** — the current persona is not saved between
  sessions. The user must log in each time the application starts.
- **Multi-persona** — only one persona is active at a time. There is no
  support for switching between saved personas without re-entering the name.
