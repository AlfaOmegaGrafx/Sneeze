# Session Log — Dean Abramson

## 2026-05-27 (Wed) ~6:50 PM – 7:25 PM PDT

- Completed storage rename: `STORAGE::SASSET` -> `STORAGE::UNIT` across project.mdc (all storage-related ASSET references updated to UNIT, network ASSET references preserved)
- Merged orphan `Stream.h` into private `Console.h` — discovered `Stream.h` was never included by anything (public `include/Console.h` already had the full `CONSOLE::STREAM` declaration); removed the redundant copy from private `Console.h` after merge
- Merged `Network_Asset.h` into `Network.h` — `NASSET` class moved alongside `INETWORK_IMPL`; updated includes in `Network_Asset.cpp`, `Network.cpp`, `File.cpp`; carried over `Control.h` include; removed from MSVC project files; deleted `Network_Asset.h`
- Dean renamed `NASSET` back to `ASSET` after the merge (done manually)
- Ran full test suite: 10/10 suites, 295/295 assertions all passing
- Dean committed all changes
- Updated `project.mdc`: console descriptions updated for `m_umpStream` (was `m_apStream`), removed stale `m_umpBlock`/`Block_Open`/`Block_Close` references, noted single-stream-per-CID enforcement, updated file listings (removed `Block.h`/`Stream.h`), added private header notes for Network and Console
- Console module declared ~90% complete; next step is Inspector work in Artemis

## 2026-06-02 (Mon) ~afternoon PDT

- **Phase 1 of scene bootstrap re-architecture: SCENE ownership refactor**
- Planned multi-phase architecture for MSF-driven scene bootstrapping (9 phases total)
- Discussed and resolved CONTEXT/CONTAINER/FABRIC/MSF/WASM relationships: each attachment point spawns one fabric, identified by one MSF file, housed in one container (keyed by fingerprint + container name)
- Moved SCENE ownership from VIEWPORT to CONTEXT — SCENE is now `SNEEZE::SCENE` (un-nested from VIEWPORT), created/destroyed by CONTEXT::Impl alongside CONSOLE, NETWORK, STORAGE
- Created `include/Scene.h` as the new public header for SCENE
- Un-nested FABRIC (`SCENE::FABRIC`) and NODE (`SCENE::FABRIC::NODE`) from VIEWPORT
- Updated VIEWPORT to delegate `Scene()` to `Context()->Scene()`
- Performed large-scale file reorganization: moved `viewport/scene/`, `viewport/msf/`, `sneeze/console/`, `sneeze/network/`, `sneeze/storage/`, and `Context.cpp` into new `src/context/` directory using `git mv`
- Updated CMakeLists.txt, MSVC project files (.vcxproj, .vcxproj.filters), and build scripts for new paths
- Fixed broken includes after moves (Network.h relative path, SneezeTest.vcxproj include directories)
- Updated subsystem documentation (Scene.md, Console.md, Storage.md) for new file paths
- Updated project.mdc for new directory structure and SCENE ownership model
- All tests passing, solar system renders correctly

## June 3, 2026 — ~5:00 PM – 6:10 PM PDT

**Phase 2 cleanup: pImpl for NODE and FABRIC, un-nesting into public header**

- Implemented pImpl for NODE: moved all internal members and NETWORK::IFILE inheritance into NODE::Impl in Node.cpp. Removed m_umpNode (unordered_map keyed by twObjectIx — unreliable at construction time), simplified to linear vector only. Removed unused public methods (Node_Children, Node_Find, ObjectIx_Set, IsPrimary, SetPrimary)
- Implemented pImpl for FABRIC: moved all internal members into FABRIC::Impl in Fabric.cpp
- Fixed crash in NODE construction: m_pImpl was uninitialized when Node_Add was called from Impl's constructor. Fix: moved parent-child linking from Impl constructor to NODE constructor body
- Two-step NODE construction pattern: constructor links into tree, Initialize(MAP_OBJECT*) assigns 3D payload
- Un-nested NODE from FABRIC: moved from SCENE::FABRIC::NODE to top-level SNEEZE::NODE in include/Scene.h. Deleted src/context/scene/Node.h. Updated all SCENE::FABRIC::NODE references across codebase
- Un-nested FABRIC and FABRIC_ROOT from SCENE: moved from SCENE::FABRIC to top-level SNEEZE::FABRIC in include/Scene.h. Deleted src/context/scene/Fabric.h. Updated all SCENE::FABRIC / SCENE::FABRIC_ROOT references across codebase
- All four scene classes (SCENE, FABRIC, FABRIC_ROOT, NODE) are now peers in the SNEEZE namespace, all declared in include/Scene.h
- Dean renamed NETWORK::IENUM to IENUM_FILE
- Updated project.mdc with new architecture
- Builds and runs correctly

## June 3, 2026 — ~7:00 PM – 12:00 AM PDT

**Phase 3 groundwork: MSF refactoring, CID overhaul, Container identity model**

- Relocated MSF class from VIEWPORT::MSF to top-level SNEEZE::MSF in new include/Msf.h public header
- Renamed CertChain.cpp to Chain.cpp, updated class from CERT_CHAIN to MSF::CHAIN
- MSF now accepts both JWS compact serialization (signed) and plain JSON (unsigned) via heuristic detection
- Implemented 5-level trust enum (eTRUST): kTRUST_NONE (black), kTRUST_UNTRUSTED (red), kTRUST_UNVERIFIED (orange), kTRUST_EXPIRED (yellow), kTRUST_VERIFIED (green)
- Refactored CID struct: sCommonName -> sOrganizationHash, sContainerName -> sContainer, bValidated -> eTrust
- Organization now extracted from X.509 cert O field (not MSF payload); hashed to sOrganizationHash via CHAIN::HashString
- Removed "namespace" and "organization" as MSF payload fields; added "container" field
- Eliminated CID_Pool / m_umCID from CONTEXT — replaced with CONTAINER as identity registry (Option B)
- Containers now persist at refcount 0 as lightweight identity shells, deleted only on CONTEXT destruction
- CONTAINER::Identity() returns const CID& as the single authoritative CID source
- Added diagnostic log in CONTAINER::~Impl() for nonzero refcount at destruction
- Updated CID field references across: Node.cpp, Stream.cpp, Silo.cpp, File.cpp, Unit.cpp, ConsoleTest.cpp, StorageTest.cpp, NetworkTest.cpp
- Fixed NetworkTest.cpp: replaced aggregate initialization with factory function (CID has user-defined constructor, non-aggregate in C++17)
- Synced msvc/Sneeze.vcxproj: CertChain.cpp -> Chain.cpp, removed stale Node.h/Fabric.h, updated Msf.h path
- Removed stale Node.h and Fabric.h from src/CMakeLists.txt SOM_HEADERS
- Updated subsystem documentation (Console.md, Storage.md, Network.md) for new CID fields
- Builds and runs (both Sneeze and Artemis)
- Planned next step: create first official MSF file via SignMsf, then wire fetch/parse/verify pipeline in Fabric::Initialize

## June 3–4, 2026 — ~late evening through June 4 ~12:30 PM PDT

**Phase 3 completion: first official MSF file + async fetch/parse/verify pipeline**

- Created first official MSF file: `tests/data/solar-system.json` (payload) + `tests/data/solar-system.msf` (signed JWS via SignMsf with test certs, container name "solar-system", empty services/modules)
- MSF hosted at `https://cdn.rp1.com/test/solar-system.msf` for live testing
- Wired async MSF loading pipeline in FABRIC::Impl:
  - FABRIC::Impl now implements NETWORK::IFILE (OnFileReady/OnFileFailed)
  - Added MSF*, NETWORK::FILE*, CONTAINER* members to FABRIC::Impl
  - Initialize(sUrl) fetches MSF via NETWORK::File_Open using parent fabric's Container()->Identity() as CID
  - OnFileReady: reads data, parses MSF, verifies signature+chain, calls Container_Open to create container
  - astro::InjectSolarSystem moved from FABRIC_ROOT::Initialize to FABRIC::Impl::OnFileReady (conditional on MSF "container" == "solar-system")
- Resolved CID chicken-and-egg problem: FABRIC_ROOT gets synthetic root CID (sFingerprint="Sneeze", sContainer="Root", eTrust=kTRUST_ROOT) via Container_Open detecting null Msf(); child fabrics borrow parent's CID for MSF fetch caching
- Added kTRUST_ROOT to eTRUST enum (6 levels now)
- Changed Container_Open/Close signatures from void* to FABRIC*; added FABRIC forward declaration in Context.h
- Changed CONTAINER::Identity() from const CID& to const CID* (pointer identity required for deduplication)
- Changed all NETWORK::File_Open overloads + FILE constructors to accept const CONTAINER::CID* (const-correctness)
- FABRIC::~Impl destructor: close m_pFile_Msf, delete m_pNode_Root, Container_Close, delete m_pMsf, detach from parent
- Fixed build script: PCH clearing in build-windows.ps1 now conditional on -Fresh or -Rebuild only (was causing full rebuilds on every invocation)
- End-to-end verified: MSF fetched from CDN, parsed, cert data extracted, container created with correct CID and kTRUST_UNVERIFIED
- Discovered bug: inspector attachment re-triggers OnFileReady after Close() due to m_pListener not being nulled in Pending_Close() — Dean studying and fixing
- Phase 3 declared complete; next steps: fix OnFileReady bug, polish Phases 1-4, then Phase 5+6 (WASM solar system)

## June 4, 2026 — ~7:30 PM – ~7:45 PM PDT

**Phase 5 WASM infrastructure cleanup + WasmRuntime symmetry refactor**

- Eliminated `DEP::STORE_IDENTITY` struct — redundant with CONTAINER's uniqueness enforcement
- Simplified `WASM_RUNTIME` API: replaced `FindOrCreateStore()`/`DestroyStore()` with `Store_Open()`/`Store_Close(WASM_STORE*)`; internal storage changed from identity-keyed map to simple `vector<WASM_STORE*>` + mutex
- Added `WasmRuntime()` accessor to `CONTEXT` (pass-through to `Engine()->WasmRuntime()`) — symmetry with `Console()`, `Network()`, `Storage()`, etc.
- Updated `CONTAINER::Open()`/`Close()` to use `m_pContext->WasmRuntime()->Store_Open/Close()` instead of reaching through Engine
- Fixed outdated Storage test assertions: `containerName` -> `container` (field rename), `m_nCreatedCount == 1` -> `>= 1` (root container now creates its own silo during Context_Open)
- All tests pass: 44/44 storage, 40/40 WASM, full suite green
- Artemis verified working

## June 5, 2026 — ~11:30 AM – ~11:55 AM PDT

**WASM pipeline test data + Node.cpp CID blocker fix**

- Created `tests/data/hello-world.json` — MSF payload referencing `hello_wasm.wasm` at `https://cdn.rp1.com/test/hello_wasm.wasm` with SHA-256 hash `c816eb074d864b9a9637213b613536b5447f982573513f6cf7d0f76485bba77a`
- Signed payload with `SignMsf.exe` → `tests/data/hello-world.msf` (3,909 bytes, RS256, test provider cert chain)
- Verified round-trip: signature verified, fingerprint matches (`482cda4c...`), payload round-trips cleanly — first MSF with a hashed WASM module
- Fixed blocker in `Node.cpp`: `Texture_Request()` was fabricating a fake CID with hardcoded fingerprint/org/container; replaced with `m_pFabric->Container()->Identity()` — the real CID from the fabric's container
- Added `m_pFabric->Container()` null guard (no texture fetch when fabric has no container yet — protects solar system kludge path)
- Removed all fake CID code, persona hash lookup, and ENGINE/Persona references from Node.cpp
- Committed for Dave at session end

## June 5-6, 2026 — ~evening – ~12:22 AM PDT

**WASM scene bootstrap — end-to-end milestone achieved**

- Replaced `m_apNode` (`std::vector<NODE*>`) with `m_umpNode` (`std::unordered_map<uint64_t, NODE*>`) in Container.cpp — vector was catastrophic for sparse 48-bit object indices
- Added `m_umpFabric` (`unordered_map<uint64_t, FABRIC*>`) for fabric handle management with `m_twFabricIx_Next` monotonic counter
- Implemented `Node_Root`, `Node_Open`, `Node_Close`, `Node_Find` on CONTAINER — full handle table with RMCOBJECT wire-format deserialization
- Moved mutex guard from internal `Node_Create` to public entry points (`Node_Root`, `Node_Open`)
- Introduced `using WASM_HOST_FN` alias in HostFunctions.h — reduced 32 host function declarations to one-liners
- Host function count grew from 29 to 32 (Scene module: Node_Root, Node_Open, Node_Close, Node_Position, Node_Scale, Node_Bound, Node_Color, Node_Name, Node_Radius, Node_Texture)
- Implemented `Scene_Node_Root` and `Scene_Node_Open` host functions — read RMCOBJECT from WASM linear memory, create MAP_OBJECT_CELESTIAL + NODE, return twObjectIx handle
- Consolidated test files: `scene-test.*` and `hello-world.*` merged into `solar-system.*` (single MSF + single WASM module)
- Created Rust WASM module `tests/wasm/solar_system/` — calls Scene host functions to create Root/Sun/Earth/Moon nodes
- Fixed Rust compilation issues: removed semicolons after return expressions (Rust expression-oriented semantics), added `#![allow(dead_code)]`
- Fixed `WASM_INSTANCE::CallOpen` — was passing 4 args (leftover from hello_wasm), corrected to 3 (i64 twFabricIx, i32 dwOffset, i32 dwLength)
- Built and signed solar-system.msf with solar_system.wasm module reference + SHA-256 hash
- Commented out `astro::InjectSolarSystem()` to let WASM module populate the scene
- **End-to-end success**: MSF fetched -> parsed -> WASM compiled -> instantiated -> Init called -> Open called -> 4 scene nodes created via host functions -> rendered as spheres in viewport
- Vertically aligned Scene.h class declarations, grouped by accessors/mutators/methods
- Updated project.mdc with all architectural changes

## June 6, 2026 — ~afternoon PDT

**WASM full solar system — 1,245 objects, hierarchical rendering, textures**

- Created `gen_solar_system.py` — Python generator that parses the JavaScript Solar System project data (`E:\Dev\SolarSystem\`), computes MSF orbital parameters (quaternions, semi-axes, periods, precession vectors, tilt, spin), and generates 10 Rust source files for the WASM module
- Generated Rust data files: `star.rs`, `planets.rs`, `moons_{earth,mars,jupiter,saturn,uranus,neptune,pluto}.rs`, `debris.rs` — 1,245 total objects in 3-level hierarchy (System→Body→Surface)
- RMCOBJECT wire format fields fully utilized: `m_Orbit` (dA/dB/tmPeriod/tmOrigin for orbital parameters and surface spin), `m_Transform.d4Rotation` (tilt quaternion on bodies, orbital quaternion on systems), `m_Transform.d3Position` (precession axis-angle rate vector), `m_Resource.sReference` (texture URLs)
- Implemented `MAP_OBJECT_CELESTIAL::Rotation()` override with three code paths: BODY subtypes (tilt quaternion + precession composition), SURFACE subtypes (spin around Y-axis using dA=W0Rad and tmPeriod=spin period), SYSTEM subtypes (identity)
- Refactored `Compositor.cpp::TraverseNode` to use `WORLD_FRAME` struct for hierarchical world-space accumulation — SYSTEM nodes compute orbital position and propagate to children, BODY nodes capture radius/color/star-flag, only SURFACE nodes render as spheres (using inherited parent attributes)
- Fixed texture CDN URL: changed `TEX_BASE` from `cdn2-david.rp1.dev/img/` to `cdn.rp1.com/res/texture/celestial/` — resolved HTTP 404 errors
- Fixed Python generator to output single-line Rust function calls (no line splitting)
- Performance discovery: all 1,245 objects cause 1-2 FPS; selectively disabling Jupiter (79 moons) and Saturn (83 moons) restores 60 FPS — bottleneck is in the renderer
- Current configuration: star + planets + Earth/Mars/Uranus/Neptune/Pluto moons + debris = 60 FPS with textures and orbit trails
- `astro::InjectSolarSystem()` fully superseded by WASM module — pending removal after validation
- Updated project.mdc with all architectural changes
