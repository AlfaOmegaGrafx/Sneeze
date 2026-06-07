# SPIR-V — Shader Module Validation

The `spirv` module validates SPIR-V binary modules before they are dispatched
to the GPU compute pipeline. Uses SPIRV-Tools (vulkan-sdk-1.4.341.0).

## SPV_PIPELINE

```cpp
DEP::SPV_PIPELINE pipeline;
pipeline.Initialize ();

std::string sError;
if (pipeline.Validate (aBinary, sError))
{
   // SPIR-V is valid — safe to dispatch
}

pipeline.Shutdown ();
```

`Validate()` checks that a SPIR-V binary is well-formed and conforms to the
Vulkan compute profile. Untrusted SPIR-V from remote MSF payloads must pass
validation before GPU submission.

## Relationship to Other Modules

- **compute** — `COMPUTE_DISPATCH` uses embedded SPIR-V kernels (validated at
  build time) or runtime-loaded modules (which must pass `Validate()` first)

## Files

| File | Contents |
|------|----------|
| `SpvPipeline.h` | SPV_PIPELINE declaration |
| `SpvPipeline.cpp` | Implementation (SPIRV-Tools validation) |
