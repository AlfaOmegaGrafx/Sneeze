# MSF_FILE — Metaverse Spatial Fabric File

`MSF_FILE` is the single class for working with `.msf` files. It handles the
full lifecycle: parsing an existing file, composing a new one, managing
certificates, managing payload content (services, modules), signing, and
verifying.

An `.msf` file is a JWS (JSON Web Signature) compact string with:

- **Header** — algorithm (`alg`) and X.509 certificate chain (`x5c`)
- **Payload** — JSON wrapped in a `data` claim (namespace, organization,
  services, modules, successor)
- **Signature** — cryptographic signature over header + payload

## Reading and Verifying an MSF File

```cpp
#include "msf/MsfFile.h"

// 1. Create the object and optionally load trusted CA certs
SNEEZE::msf::MSF_FILE msf;
msf.AddTrustedCert (sCaPem);     // feeds the trust store for chain validation

// 2. Parse the JWS string — always populates all fields
msf.Parse (sJwsString);

// 3. Verify (optional — parsed data is available either way)
msf.VerifySignature ();           // checks the cryptographic signature
msf.VerifyChain ();               // checks the certificate chain against the trust store

// 4. Read the results
msf.GetAlgorithm ();              // "RS256"
msf.GetFingerprint ();            // SHA-256 of leaf cert's SPKI
msf.IsSignatureValid ();          // true / false
msf.IsChainTrusted ();            // true / false
msf.GetSignatureError ();         // "" if valid, error message otherwise
msf.GetChainError ();             // "" if trusted, error message otherwise

// 5. Inspect the payload
msf.GetNamespace ();              // "com.pokerstars.poker"
msf.GetOrganization ();           // "PokerStars"
msf.GetSuccessor ();              // successor fingerprint (or "")
msf.GetServices ();               // vector of MSF_SERVICE
msf.GetModules ();                // map of name -> MSF_MODULE
msf.GetPayload ();                // raw nlohmann::json

// 6. Inspect the certificate chain
msf.GetCertInfos ();              // vector of CERT_INFO
msf.GetCertCount ();              // number of certs in the chain
```

### Key point: Parse is unconditional

`Parse()` splits the JWS, decodes the header, payload, and certificate chain
into the object's fields. Everything is populated regardless of validity.
`Parse()` returns `false` only if the string is not parseable as JWS (malformed
dots or base64).

Verification is separate. A viewer that just wants to display contents can call
`Parse()` alone and skip `VerifySignature()` / `VerifyChain()`.

## Composing and Signing a New MSF File

```cpp
#include "msf/MsfFile.h"

// 1. Create the object
SNEEZE::msf::MSF_FILE msf;

// 2. Set payload fields
msf.SetNamespace ("com.pokerstars.poker");
msf.SetOrganization ("PokerStars");

// 3. Add services
msf.AddService ({"game-server", "websocket", "wss://rt.pokerstars.com/game",
                  {"game-client.wasm"}});

// 4. Add modules with their URLs and SHA-256 digests
msf.AddModule ("game-client.wasm",
               "https://cdn.pokerstars.com/modules/game-client.wasm",
               "a1b2c3d4e5f6...");

// 5. Add the signing certificate chain (leaf first, then CAs)
msf.AddCert (sLeafPem);
msf.AddCert (sCaPem);

// 6. Sign — returns the JWS compact string
std::string sJws = msf.Sign (sPrivateKeyPem, "RS256");
```

### Bulk payload access

For cases where the typed methods don't cover a field, use `SetPayload()` and
`GetPayload()` to work with the raw JSON directly:

```cpp
nlohmann::json payload;
payload["namespace"]    = "com.example.app";
payload["organization"] = "Example Corp";
payload["custom_field"] = 42;
msf.SetPayload (payload);
```

## Data Structs

### MSF_SERVICE

| Field       | Type                    | Description                        |
|-------------|-------------------------|------------------------------------|
| `sName`     | `std::string`           | Service name (e.g. "game-server")  |
| `sType`     | `std::string`           | Protocol type (e.g. "websocket")   |
| `sEndpoint` | `std::string`           | Service endpoint URL               |
| `aModules`  | `std::vector<string>`   | Module names this service uses     |

### MSF_MODULE

| Field    | Type          | Description                              |
|----------|---------------|------------------------------------------|
| `sUrl`   | `std::string` | Download URL for the module binary       |
| `sSha256`| `std::string` | SHA-256 hex digest of the module binary  |

### CERT_INFO

| Field        | Type          | Description                          |
|--------------|---------------|--------------------------------------|
| `sSubject`   | `std::string` | Certificate subject (one-line)       |
| `sIssuer`    | `std::string` | Certificate issuer (one-line)        |
| `sSerial`    | `std::string` | Serial number (hex)                  |
| `sNotBefore` | `std::string` | Validity start date                  |
| `sNotAfter`  | `std::string` | Validity end date                    |
| `sKeyType`   | `std::string` | "RSA", "EC", or "unknown"           |
| `nKeyBits`   | `int`         | Key size in bits                     |
| `bIsCA`      | `bool`        | `true` for CA certs, `false` for leaf|

## Certificate Management

**For signing (composition path):**

- `AddCert(sPem)` — adds a PEM certificate to the chain. First call adds the
  leaf cert (index 0); subsequent calls add CA/intermediate certs.
- `RemoveCert(nIndex)` — removes the certificate at the given index.
- Certificates added via `AddCert()` are embedded in the JWS `x5c` header when
  `Sign()` is called.

**For verification (parse path):**

- `AddTrustedCert(sPem)` — loads a PEM certificate into the trust store. This
  does NOT add it to the file's certificate chain — it is used only during
  `VerifyChain()` to determine whether the chain is trusted.
- The operating system's root certificate store is also loaded automatically.

## CERT_CHAIN Static Utilities

`CERT_CHAIN` exposes static methods for working with certificates outside of an
`MSF_FILE` instance. These are useful when you need cert metadata without
parsing a full `.msf` file.

```cpp
#include "msf/CertChain.h"

using SNEEZE::msf::CERT_CHAIN;
using SNEEZE::msf::CERT_INFO;

// Decode a CERT_INFO from a base64 DER string (as found in x5c headers)
CERT_INFO info = CERT_CHAIN::DecodeInfoDerBase64 (sB64Der, false);

// Decode a CERT_INFO from a PEM string
CERT_INFO info = CERT_CHAIN::DecodeInfoPem (sPem, false);

// Compute the SPKI fingerprint (SHA-256) from a base64 DER cert
std::string sFingerprint = CERT_CHAIN::ComputeFingerprint (sB64Der);

// Extract the public key as PEM from a base64 DER cert
std::string sPubKeyPem = CERT_CHAIN::ExtractPublicKeyPem (sB64Der);

// Convert a PEM certificate to base64 DER (for building x5c headers)
std::string sB64Der = CERT_CHAIN::PemToDerBase64 (sPem);
```

## Supported Algorithms

RS256, RS384, RS512, ES256, ES384, ES512.
