# Compute — GPU/CPU Kernel Dispatch

The `compute` module dispatches SPIR-V compute kernels on the GPU (via Vox) or
CPU (via registered kernel functions).

## COMPUTE_DISPATCH

Tries the GPU path first. Falls back to a registered CPU kernel if no Vox
device is available or the kernel has no embedded SPIR-V binary.

```cpp
DEP::COMPUTE_DISPATCH dispatch;

// Register a CPU fallback
dispatch.RegisterCpuKernel ("test_proximity", MyProximityKernel);

// Dispatch — GPU if available, CPU otherwise
DEP::BUFFER_BINDING bindings[] = {
   { 0, pPositions, nPosSize, true },
   { 1, pResults,   nResSize, false },
};
dispatch.Dispatch ("test_proximity", nGroupsX, 1, 1,
                   bindings, 2, &pushData, sizeof (pushData));
```

### BUFFER_BINDING

| Field | Type | Description |
|-------|------|-------------|
| `nBinding` | `uint32_t` | Binding slot in the kernel |
| `pData` | `void*` | Host memory pointer |
| `nSize` | `size_t` | Buffer size in bytes |
| `bReadOnly` | `bool` | Read-only to kernel |

### GPU Path (Vox)

`vox::DEVICE::Create(Backend::Auto)` selects Vulkan / DX12 / Metal at
construction. SPIR-V is cross-compiled via SPIRV-Cross for DX12 (HLSL) and
Metal (MSL). Results read back to host memory.

### CPU Path

Registered `CpuKernelFn` with the same binding and push-constant layout.
Built-in CPU fallback for `TEST_PROXIMITY`.

## Embedded Kernels

GLSL compute shaders compiled to SPIR-V at build time via glslang, embedded
as Win32 resources (or platform equivalent).

```cpp
DEP::KERNEL_DATA k = DEP::GetEmbeddedKernel ("test_proximity");
```

## Files

| File | Contents |
|------|----------|
| `ComputeDispatch.h` | COMPUTE_DISPATCH, BUFFER_BINDING, CpuKernelFn |
| `ComputeDispatch.cpp` | Implementation (Vox dispatch, CPU fallback) |
| `EmbeddedKernels.h` | GetEmbeddedKernel declaration |
| `EmbeddedKernels.cpp` | Resource loading (Win32/ELF/Mach-O) |
