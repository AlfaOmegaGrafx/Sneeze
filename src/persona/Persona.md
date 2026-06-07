# Persona — Local Identity Proxy

The `persona` module provides a temporary local identity used to scope WASM
stores and persistent storage. No network-based identity or authentication —
this is a local testing proxy.

## PERSONA

```cpp
SNEEZE::persona::PERSONA persona (pEngine);

persona.Login ("Dean", "Abramson");
// persona.Name()       -> "Dean.Abramson"
// persona.Hash()       -> "a3f1..." (SHA-256 hex, truncated to 12 chars)
// persona.IsLoggedIn() -> true

persona.Logout ();
```

### How the Hash is Used

The persona hash is the first key in the identity triple that scopes WASM
stores and persistent storage:

```
Store identity = persona_hash + fingerprint + container_name
Storage path   = .../Storage/<persona_hash>/<fingerprint>/
```

The hash is SHA-256 (via BoringSSL), truncated to 12 hex characters for use
in filesystem paths.

## Files

| File | Contents |
|------|----------|
| `include/Persona.h` | PERSONA declaration |
| `Persona.cpp` | Implementation (Login, Logout, SHA-256 hashing) |
