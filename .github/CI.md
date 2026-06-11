# Sneeze CI

Per-platform build matrix for `linux`, `macos`, `ios`, `android`.
Each platform builds all 11 deps in parallel tiers, then Sneeze itself.

## Workflows

- **`build.yml`** — orchestrator, one job per platform, calls the reusable workflow
- **`build-platform.yml`** — reusable workflow called per platform; runs the tiered dep build
- **`docs.yml`** — documentation drift check and MediaWiki publish for `docs/`

## Documentation (`docs.yml`)

Publishes the curated wiki under `docs/` to **omb.wiki** (`Sneeze/...` pages). Source
files stay in this repo; the wiki is a mirror.

| Job | When | What |
|-----|------|------|
| `docdrift` | PR + push + dispatch | `tools/DocDrift/docdrift.py` (warn-only) |
| `wiki-transform` | PR + push + dispatch | `scripts/publish-wiki.py --dry-run --all` |
| `wiki-publish` | push to `main` + `workflow_dispatch` | Live API publish (no-op until secrets exist) |

**Secrets** (Settings → Actions): `MEDIAWIKI_API`, `MEDIAWIKI_USER`, `MEDIAWIKI_PASSWORD`.
Until the wiki owner provides a bot password, `wiki-publish` exits successfully with a
notice and changes nothing on omb.wiki.

Config: `docs/wiki/publish.json`. Script: `scripts/publish-wiki.py`.

## Dependency tiers

Deps don't all build in parallel — some depend on others:

```
tier0 (parallel):  spirv-headers, anari-sdk, openxr-sdk, openssl, curl,
                   rmlui, nlohmann-json, wasmtime, filament
tier1:             spirv-tools (needs spirv-headers)
                   halogen     (needs anari-sdk + filament)
tier2:             glslang     (needs spirv-tools)
sneeze:            needs tier0 + tier1 + tier2
```

Each dep = one matrix job so failures are individually visible in the GH UI.

## Per-dep CMake files

Each dep is defined in `deps/<dep>.cmake` as a single `ExternalProject_Add`.
`deps/CMakeLists.txt` is the CI entry point: `cmake -S deps -DDEP=<dep> -DLIBS_DIR=<path>`
configures and builds only that one dep. The user-facing root `CMakeLists.txt`
still builds everything (SuperBuild workflow unchanged).

## Sneeze lib build

The `sneeze` job uses `cmake -S src` (NOT the SuperBuild) to build directly
against pre-built deps. Faster — no ExternalProject overhead, no dep update
steps. `src/CMakeLists.txt` uses find modules in `src/cmake/` to locate each
dep under `LIBS_DIR`.

## Caching

Each dep is cached per-platform. Cache key:
`<platform>-<dep>-<hash of CMakeLists.txt + deps/<dep>.cmake + deps/CMakeLists.txt>`

Cache hit → no rebuild. Filament (30+ min) benefits most.

## Artifacts

Each tier0/1/2 job uploads its dep's install dir as artifact
`<platform>-<dep>`. Downstream tiers + sneeze job download all tier0-2
artifacts and arrange them into `libs-<platform>/` for the build.

## Cross-platform specifics

- **Filament host tools** — Android/iOS need matc/resgen etc. from the native
  host build. The Android job waits for Linux tier0 filament to finish and
  downloads its artifact as `filament-host`. iOS likewise depends on macOS.
- **Dir name case** — ExternalProject dirs use mixed case (SPIRV-Headers) to
  match upstream repos, but matrix `dep` names are lowercase (spirv-headers).
  A resolve step in `build-platform.yml` maps target names → dir names for
  cache paths.
- **Toolchain path** — `-DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-...` is
  relative to repo root. The sneeze job (`cmake -S src`) rewrites it to
  absolute before passing to cmake.
