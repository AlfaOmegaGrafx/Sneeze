---
title: The Standards Sneeze Builds On
tier: Overview
audience: [evaluator, integrator, contributor]
sources: []
verified: 92fdc1c
nav:
  prev: overview/core-concepts.md
  next: architecture/overview.md
---

# The Standards Sneeze Builds On

A neutral client only stays neutral if the things it depends on are themselves open. If the engine's rendering, GPU, device, or code-execution layers were tied to one vendor's proprietary technology, the neutrality argued for in [What is the Open Metaverse Browser?](what-is-omb.md) would be a fiction. So Sneeze is built on a small set of widely adopted, vendor-neutral standards, each the "open by default" choice in its domain. This page explains what each standard is, the job it does in the engine, and why it was chosen over the alternatives — at the level of *ideas*, not code. The Systems tier covers how each is wired in.

A useful framing: these standards abstract away the **hardware and the untrusted code** so the engine can describe *what* it wants — a scene, a shader, a device pose, a unit of logic — and let a conforming implementation decide *how*. That is the same bet the web made with HTML, CSS, and JavaScript, applied to three dimensions.

---

## The core abstractions

Six standards do the heavy lifting. They are genuinely different kinds of thing — APIs, bytecode formats, and a programming model — which is why they are called *core abstractions* rather than "runtime libraries."

### Rendering — an open scene-description API (ANARI)

The engine must draw a 3D scene without binding itself to a particular GPU or graphics API. It uses **ANARI** (Analytic Rendering Interface), a Khronos standard. ANARI is a *scene-description* API, not a GPU API: the engine describes **what** is in the scene — geometry, materials, lights, cameras — and an ANARI *device* decides **how** to render it. The abstraction is extremely thin and imposes no measurable overhead.

This matters because it cleanly separates the engine from the renderer. The same engine can drive a high-end GPU path tracer, a mobile rasterizer, or a CPU fallback, by loading a different ANARI device at runtime — no engine code changes. Content describes a scene; the device renders it; the browser is neutral about which device is present.

*Why not call a GPU API directly?* Because then the engine would have to reimplement a renderer for every backend (Vulkan, Metal, DX12) and every quality tier. ANARI lets a specialized renderer do that, behind a stable interface.

### GPU interchange — an open shader/compute bytecode (SPIR-V)

Content providers ship visual effects and parallel compute as **SPIR-V**, the Khronos standard binary format for GPU shaders and compute kernels. SPIR-V is to the GPU what WASM is to the CPU: a portable, verifiable bytecode rather than vendor source code.

The engine's responsibility at runtime is **validation** — rejecting malformed or malicious SPIR-V before it ever reaches a GPU driver. Accepting raw bytecode from strangers and handing it to a driver unchecked would be reckless; validation is the spatial equivalent of a browser sandboxing untrusted script. See the [SPIR-V system](../systems/spirv.md).

### GPU compute — portable parallel dispatch (SPIR-V via a compute dispatcher)

Some browser-internal work is massively parallel — physics, spatial audio, proximity math, coordinate transforms, inverse kinematics. These are authored as compute shaders, compiled to SPIR-V at build time, and dispatched on the GPU through a compute-dispatch layer that targets Vulkan, DX12, or Metal (translating SPIR-V where a backend needs it), with a CPU fallback when no GPU backend is available. This compute path is an internal engine tool, never exposed to services. See the [Compute system](../systems/compute.md).

### Device I/O — an open XR device API (OpenXR)

VR and AR hardware — headsets, controllers, hand and eye tracking — is reached through **OpenXR**, the Khronos standard for XR device access. One API spans every conforming headset and tracker, so the engine is not locked to a single hardware vendor's SDK. OpenXR is effectively indispensable here: there is no realistic vendor-neutral alternative. See the [XR system](../systems/xr.md).

### Sandboxed execution — portable, isolated code (WebAssembly)

Service logic arrives as **WebAssembly (WASM)** — portable, sandboxed bytecode. Each connected fabric runs its modules with isolated memory, a CPU budget, and tightly controlled access to engine capabilities. A misbehaving or malicious module cannot crash the engine, read another fabric's data, or touch the file system. WASM is the execution counterpart to the web's JavaScript sandbox, but language-agnostic: a module can be authored in Rust, C++, or anything that targets WASM. See the [WASM system](../systems/wasm.md).

*Why WASM and not a scripting language?* Because the engine must run **untrusted** code from many sources at interactive rates with hard isolation and resource limits. WASM was designed for exactly that — fast, sandboxed, multi-tenant execution with memory isolation and fuel-based CPU metering — and it does not impose a particular source language on content authors.

### Fabric connectivity — an open service-access protocol (SMAP)

Connecting to a fabric's services without knowing each service's underlying network protocol requires an abstraction over the network itself. **SMAP** (Service Model Access Protocol) is that layer: a driver model that lets the engine talk to any conforming spatial fabric regardless of transport. SMAP is on the roadmap rather than implemented today, but it is the planned seam between the engine and the open service ecosystem.

---

## Supporting standards and formats

Beyond the core abstractions, the engine leans on a few more open building blocks:

- **JSON** for all manifest and document data — the MSF manifest payload and the per-source storage documents are JSON.
- **JWS (RFC 7515)** — the MSF manifest is a JSON Web Signature in compact serialization, with the signer's X.509 certificate chain embedded in the standard `x5c` header. This is how a fabric proves who published it. See [MSF](../systems/msf.md).
- **X.509 / PKI** — certificate chains and the operating system's trust store are how signer identity is anchored to real-world organizations.
- **Standard image formats** (PNG, JPEG, and others) for textures, decoded into RGBA pixels for the renderer.

And at build time only (never at runtime), the engine uses the Khronos reference GLSL compiler to turn its internal compute shaders into SPIR-V. Content authors are free to use any tool that emits valid SPIR-V; the engine only consumes the bytecode.

---

## The standards organizations

This work is deliberately aligned with the bodies that define and steward these standards, rather than with any single company:

- The **Khronos Group** — rendering (ANARI), GPU bytecode (SPIR-V), and XR device access (OpenXR). The majority of the engine's core abstractions are Khronos standards.
- The **W3C** — web identity (Decentralized Identifiers) and the web's own spatial efforts, relevant to the engine's identity and embedding stories.
- The **OGC** (Open Geospatial Consortium) — real-world spatial positioning, relevant to anchoring fabrics to physical locations.
- The **Metaverse Standards Forum** — the cross-industry body that coordinates the open metaverse effort and under which this initiative sits.

Choosing standards from neutral, multi-vendor bodies is the structural guarantee of neutrality: no single participant can revoke or paywall the foundations the engine is built on.

---

## See also

- [What is the Open Metaverse Browser?](what-is-omb.md) — why openness is the whole point.
- [Core Concepts](core-concepts.md) — the vocabulary these standards serve.
- [WASM](../systems/wasm.md), [SPIR-V](../systems/spirv.md), [Compute](../systems/compute.md), [XR](../systems/xr.md), [MSF](../systems/msf.md) — each standard in the engine, in detail.

---

[Home](../Home.md) · Prev: [Core Concepts](core-concepts.md) · Next: [Architecture Overview](../architecture/overview.md)
