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
