# MSF — Metaverse Spatial Fabric File

The `msf` module provides signing and verification of `.msf` files using
RFC 7515 JWS compact serialization, plus plain JSON (unsigned) MSF support.

`MSF` is the single class for the full lifecycle: parsing, signing, verification,
certificate management, and typed payload access. `MSF::CHAIN` handles X.509
chain validation. Both are in the `SNEEZE` namespace.

Public header: `include/Msf.h`.

## Reading and Verifying

```cpp
#include <Msf.h>

SNEEZE::MSF msf;
msf.AddTrustedCert (sCaPem);

// Parse — always populates all fields, even if invalid
msf.Parse (sJwsOrJson, sUrl);

// Verify (optional — parsed data is available either way)
msf.VerifySignature ();
msf.VerifyChain ();

// Read results
msf.Algorithm ();            // "RS256"
msf.Fingerprint ();          // SHA-256 of leaf cert's SPKI
msf.Organization ();         // from cert subject
msf.OrganizationHash ();     // truncated SHA-256 of organization
msf.IsSignatureValid ();
msf.IsChainTrusted ();
msf.IsChainExpired ();

// Inspect payload
msf.Container ();            // container name
msf.Services ();             // vector<SERVICE>
msf.Modules ();              // vector<MODULE>
msf.Payload ();              // raw nlohmann::json
msf.Successor ();            // successor fingerprint
```

### Parse accepts both JWS and plain JSON

`Parse(sInput, sUrl)` detects the format:
- **JWS compact serialization** (signed) — splits header.payload.signature,
  decodes certificates, extracts fingerprint
- **Plain JSON** (unsigned) — parses directly, generates a synthetic fingerprint
  from SHA-256 of the URL + content (100% untrustworthy, unique per file)

The `sUrl` parameter is always required.

## Composing and Signing

```cpp
SNEEZE::MSF msf;
msf.SetContainer ("poker-table");
msf.AddService ({"game-server", "websocket", "wss://rt.example.com/game", {"game.wasm"}});
msf.AddModule ("https://cdn.example.com/game.wasm", "sha256-a1b2c3...");
msf.AddCert (sLeafPem);
msf.AddCert (sCaPem);

std::string sJws = msf.Sign (sPrivateKeyPem, "RS256");
```

## Nested Types

### MSF::SERVICE

| Field | Type | Description |
|-------|------|-------------|
| `sName` | `string` | Service name |
| `sType` | `string` | Protocol type (e.g. "websocket") |
| `sEndpoint` | `string` | Service endpoint URL |
| `aModules` | `vector<string>` | Module names this service uses |

### MSF::MODULE

| Field | Type | Description |
|-------|------|-------------|
| `sUrl` | `string` | Download URL |
| `sHash` | `string` | SHA-256 hex digest |

### MSF::CERT

| Field | Type | Description |
|-------|------|-------------|
| `sSubject` | `string` | Certificate subject |
| `sIssuer` | `string` | Certificate issuer |
| `sOrganization` | `string` | Organization from subject |
| `sSerial` | `string` | Serial number (hex) |
| `sNotBefore/After` | `string` | Validity dates |
| `sKeyType` | `string` | "RSA", "EC", or "unknown" |
| `nKeyBits` | `int` | Key size in bits |
| `bIsCA` | `bool` | CA or leaf |

### MSF::CHAIN

X.509 chain validation via BoringSSL's `X509_STORE`. Loads the OS root
certificate store automatically. Computes the leaf certificate's SPKI
fingerprint (SHA-256).

Static utilities (no MSF instance needed):
- `DecodeInfoDerBase64()` / `DecodeInfoPem()` — parse cert metadata
- `ComputeFingerprint()` — SHA-256 of SPKI from base64 DER
- `ExtractPublicKeyPem()` — extract public key as PEM
- `PemToDerBase64()` — convert PEM to base64 DER
- `HashString()` — SHA-256 of arbitrary string

## Supported Algorithms

RS256, RS384, RS512, ES256, ES384, ES512.

## Files

| File | Contents |
|------|----------|
| `include/Msf.h` | MSF class, SERVICE, MODULE, CERT, CHAIN |
| `MsfFile.cpp` | MSF implementation (parse, sign, verify, payload access) |
| `Chain.cpp` | CHAIN implementation (X509_STORE, fingerprint, cert utilities) |
