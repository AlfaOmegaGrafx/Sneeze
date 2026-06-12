---
title: CONTAINER::CID (class reference)
tier: API
audience: [integrator, contributor]
sources:
  - include/Container.h
  - src/context/Container.cpp
verified: 92fdc1c
nav:
  prev: api/container/CONTAINER.md
  next: api/console/index.md
---

# `CONTAINER::CID`

The identity record of a [`CONTAINER`](CONTAINER.md) — the small, copyable value that
names a signed content source and decides how two sources compare. A `CID` is built by
the [`CONTEXT`](../context/index.md) from a verified [`MSF`](../msf/index.md): its fields
come from the source's signing certificate and manifest plus the current
[persona](../persona/index.md). The container's pool key and display name are both derived
from it. For the conceptual picture see the [Container system](../../systems/container.md)
page; this page is the exact behavior of every public member.

```cpp
class CONTAINER::CID
{
public:
   std::string sFingerprint;
   std::string sOrganization;
   std::string sOrganizationHash;
   std::string sContainer;
   std::string sPersonaHash;
   eTRUST      eTrust;

   CID () : eTrust (kTRUST_NONE) {}

   std::string DisplayName () const;
   std::string Key         () const;
};
```

---

## Role and ownership

- **Built by** `CONTEXT::Container_Open`, which populates the fields from an MSF (or
  synthesizes a "Root" identity for the source-less root fabric) and assigns `eTrust`.
- **Copied into** the `CONTAINER` it identifies — the container keeps its own copy,
  returned by [`CONTAINER::Identity()`](CONTAINER.md).
- **Plain value type.** All fields are public; there is no pimpl and no internal locking.
  Treat it as an immutable record once built.

---

## Fields

| Field | Type | Meaning |
|---|---|---|
| `sFingerprint` | `std::string` | The signing certificate's fingerprint. The dominant component of the pooling key. |
| `sOrganization` | `std::string` | The human-readable organization name from the certificate. |
| `sOrganizationHash` | `std::string` | A hash standing in for the organization when its name is not trusted enough to display. |
| `sContainer` | `std::string` | The container name the source declared in its manifest. |
| `sPersonaHash` | `std::string` | The hash of the local persona under which the content is loaded — part of identity, so one persona's data never leaks into another's. |
| `eTrust` | `eTRUST` | The trust level (see [`eTRUST`](index.md#the-etrust-enum)); defaults to `kTRUST_NONE`. |

---

## Construction

```cpp
CID ();
```

### `CID()`
- **Purpose.** Construct an empty identity with `eTrust` defaulted to `kTRUST_NONE`. All
  string fields are empty.
- **Notes.** The context fills the fields in immediately after constructing one; you
  rarely build a `CID` yourself.

---

## Derived strings

```cpp
std::string DisplayName () const;
std::string Key         () const;
```

### `std::string DisplayName () const`
- **Purpose.** The name a host shows the user for this source. Returns
  `organization/container` when the source is trusted enough (`eTrust >= kTRUST_EXPIRED`),
  and `organizationHash/container` otherwise — so an untrusted source cannot present a
  recognizable organization name.
- **Returns.** The display string.
- **Notes.** Because trust is currently forced to `kTRUST_EXPIRED` for MSF-derived
  containers (see [Container → Trust levels](../../systems/container.md#trust-levels)),
  `DisplayName` reflects that forced value, not the real verification result, until the
  override is removed.

### `std::string Key () const`
- **Purpose.** The pooling key — the string the context's container map is keyed by.
  Identical key means identical identity means one shared container.
- **Returns.** A string of the form `persona/fp2/fp22/container`, composed from the first
  12 characters of `sPersonaHash`, the first 2 characters of `sFingerprint`, the next 22
  characters of `sFingerprint`, and `sContainer`. Pieces shorter than expected are taken
  as far as available.
- **Notes.** Built once and cached by the `CONTAINER` (returned by
  [`CONTAINER::Key()`](CONTAINER.md)). Note that `eTrust` is **not** part of the key — the
  same source at different trust levels still pools to one container.

---

## See also

- [Container system](../../systems/container.md) — design, identity, trust, lifecycle.
- [CONTAINER](CONTAINER.md) — the object this record identifies.
- [Context API](../context/index.md) — builds the `CID` and pools containers by `Key()`.
- [MSF API](../msf/index.md) — the signed manifest the fields are derived from.

---

[Container API](index.md) · Prev: [CONTAINER](CONTAINER.md) · Next: [Console API](../console/index.md)
