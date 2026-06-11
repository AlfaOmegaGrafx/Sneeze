# DocDrift

DocDrift is a small, read-only tool that detects wiki pages under `docs/` that may have
fallen out of sync with the code they document. It is the mechanical half of the
documentation-maintenance loop described in
[`docs/guides/contributing.md`](../../docs/guides/contributing.md).

## What it does

Every page in the wiki carries YAML front matter linking it to the source tree:

```yaml
sources:
  - include/Scene.h
  - src/context/scene/Scene.cpp
verified: 92fdc1c
```

- **`sources`** lists the repo-relative code files the page documents.
- **`verified`** is the commit the page was last checked against.

For each page, DocDrift runs `git log <verified>..HEAD -- <sources>`. If any listed source
changed since the page was verified — or if a listed source no longer exists — the page is
flagged for review. DocDrift **never edits documentation**; it only reports where to look.

## Usage

Run from anywhere inside the repository (it locates the repo root via git):

```bash
python tools/DocDrift/docdrift.py
```

Options:

| Flag | Effect |
|------|--------|
| `--docs DIR` | Scan a different documentation root (default: `<repo>/docs`). |
| `--strict` | Exit non-zero if any page is flagged (default: warn-only, always exits 0). |
| `--quiet` | Print only flagged pages and the summary line. |

Exit codes: `0` no drift (or warn-only mode); `1` drift found **and** `--strict` given;
`2` usage/environment error (not a git repo, docs directory missing, HEAD unresolved).

## Example output

```
DRIFT -- these pages document code that changed since they were last verified:

  systems/scene.md  (verified: 92fdc1c)
      - a1b2c3d Scene: move node management out of Container
      - e4f5a6b Scene: add Reload()

docdrift: 48 page(s) scanned | 45 current | 1 drifted | 0 warning(s) | 2 without sources (skipped). HEAD=d7e8f9a
```

## The maintenance loop

1. Run DocDrift.
2. Open each flagged page and compare it against the current code — **the code wins on
   every conflict.**
3. Correct the page, then bump its `verified` field to the current `HEAD`.

## CI

DocDrift is intended to run in CI as a **warn-only** check (without `--strict`), so drift is
surfaced on a pull request without blocking the merge. Documentation maintenance is then a
visible follow-up rather than a gate.

## Limitation

The `sources` list is hand-maintained. DocDrift catches changes to files a page *lists*, not
coverage a page *forgot* to list. When you add a new source file to a subsystem, add it to
the relevant page's `sources`.

## Requirements

Python 3 (standard library only) and `git` on `PATH`. No third-party packages.
