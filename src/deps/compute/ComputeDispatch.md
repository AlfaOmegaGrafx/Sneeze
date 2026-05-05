# Compute Dispatch — GPU/CPU Kernel Execution

The `compute` module provides a unified interface for dispatching compute
kernels on either the GPU (via Vox) or CPU (via registered kernel functions).

## COMPUTE_DISPATCH

The dispatcher tries the GPU path first. If no Vox device is available (or
the kernel has no SPIR-V binary), it falls back to a registered CPU kernel.

```cpp
#include "compute/ComputeDispatch.h"

compute::COMPUTE_DISPATCH dispatch;

// Register a CPU fallback kernel
dispatch.RegisterCpuKernel ("test_proximity", MyProximityKernel);

// Dispatch — GPU if available, CPU otherwise
compute::BUFFER_BINDING bindings[] = {
   { 0, pPositions, nPosSize, true },
   { 1, pResults,   nResSize, false },
};

dispatch.Dispatch ("test_proximity", nGroupsX, 1, 1,
                   bindings, 2, &pushData, sizeof (pushData));
```

### BUFFER_BINDING

| Field      | Type       | Description                               |
|------------|------------|-------------------------------------------|
| `nBinding` | `uint32_t` | Binding slot index in the kernel           |
| `pData`    | `void*`    | Pointer to host memory                     |
| `nSize`    | `size_t`   | Size of the buffer in bytes                |
| `bReadOnly`| `bool`     | If true, buffer is read-only to the kernel |

### GPU Path (Vox)

When `SupportsNativeCompute()` is true, `Dispatch()` looks up the SPIR-V
binary for the named kernel (via `EmbeddedKernels`), creates Vox buffers,
and submits a compute dispatch on the GPU. Results are read back to host
memory.

### CPU Path

If no GPU is available, the dispatcher calls the registered `CpuKernelFn`
with the same binding and push-constant layout. CPU kernels are expected to
iterate the workgroup dimensions themselves.

## Embedded Kernels

`EmbeddedKernels.h` / `.cpp` provide access to SPIR-V binaries that are
compiled at build time and embedded into the Sneeze library as Windows
resources (or platform equivalent).

```cpp
compute::KERNEL_DATA k = compute::GetEmbeddedKernel ("test_proximity");
// k.pBytes, k.nSize
```

## Unimplemented / Future Work

- **Async dispatch** — currently all dispatches are synchronous. GPU
  dispatches block until the fence signals.
- **Multi-dispatch batching** — no support for batching multiple kernels
  into a single command buffer submission.
- **SPIR-V hot-reload** — embedded kernels are baked at build time; runtime
  loading of external SPIR-V is not yet supported.
