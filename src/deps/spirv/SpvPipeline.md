# SPIR-V — Shader Module Validation

The `spirv` module provides validation of SPIR-V binary modules before they
are dispatched to the GPU compute pipeline.

## SPV_PIPELINE

```cpp
#include "spirv/SpvPipeline.h"

DEP::SPV_PIPELINE pipeline;
pipeline.Initialize ();

std::vector<uint32_t> aBinary;
// ... load SPIR-V bytes ...

std::string sError;
if (pipeline.Validate (aBinary, sError))
{
   // SPIR-V is valid — safe to dispatch via COMPUTE_DISPATCH
}
else
{
   // sError contains the validation failure message
}

pipeline.Shutdown ();
```

### Validation

`Validate()` checks that a SPIR-V binary is well-formed and conforms to the
Vulkan compute profile. This is a safety gate — untrusted SPIR-V modules
fetched from remote MSF payloads must pass validation before being submitted
to the GPU.

## Relationship to Other Modules

- **compute** — `COMPUTE_DISPATCH` uses embedded SPIR-V kernels (already
  validated at build time) or runtime-loaded modules (which must pass
  `SPV_PIPELINE::Validate()` first).
- **cache** — verified SPIR-V modules are cached in the Module tier
  alongside WASM binaries, keyed by URL + SHA-256.

## Unimplemented / Future Work

- **spirv-tools integration** — the current implementation is minimal.
  Full integration with the SPIRV-Tools validator would provide more
  thorough validation coverage.
- **Reflection** — extracting binding layouts, push constant sizes, and
  workgroup dimensions from SPIR-V metadata would allow the compute
  dispatcher to validate bindings at submission time.
- **Cross-compilation** — translating SPIR-V to DXIL (DirectX) or MSL
  (Metal) for non-Vulkan backends.
