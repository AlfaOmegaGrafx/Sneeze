---
title: Documentation Style & Conventions
tier: Meta
sources: []
verified: 92fdc1c
---

# Documentation Style & Conventions

This page defines how the Sneeze wiki is written and maintained. Read it before authoring or editing any page. It exists so that thirty-plus pages written over months still read as one coherent manual, and so that every page stays honest about the code it describes.

If you only remember two things: **the code is the source of truth**, and **every page declares which code it documents** (see [Front matter](#front-matter)).

## Who maintains this wiki

This documentation is written and kept current by **AI coding agents**, working directly from the source tree. We say so plainly rather than pretending otherwise: the wiki is large, the codebase moves quickly, and an agent that can read the entire source in one pass and re-verify a page against it is the right tool for the job.

That choice is *why* this style page reads like an operating procedure — it is one. It is the instruction set an agent follows to produce and maintain pages that are accurate, consistent, and honest. The conventions below (the source-of-truth rule, the `sources`/`verified` front matter, the drift loop) exist so that an agent picking up the task months from now behaves the same way the last one did. A human editor can follow the same rules by hand, but the expected, primary maintainer is an agent.

---

## Audience

Assume the reader is **new to Sneeze and new to the problem domain**. They may be a contributor reading to extend the engine, an integrator reading to embed it, or an evaluator reading to decide whether to adopt it. Write so that all three can follow a page top to bottom without prior exposure. Define every term the first time it appears. Lead with *why* something exists before *what* it is and *how* to use it.

This wiki never names a specific browser or the specific application(s) that embed Sneeze. Sneeze is "an engine"; whatever embeds it is "a host application," "the host," "the embedder," or "a browser" in the generic sense.

---

## The five tiers

Pages live in one of five tiers, each a folder under `docs/`. The tiers form a deliberate reading order — a newcomer starts at the top and descends.

| Tier | Folder | Purpose |
|---|---|---|
| 1. Overview | `overview/` | What the project is and why it exists. No code. |
| 2. Architecture | `architecture/` | How the pieces fit together. Diagrams and data flows. |
| 3. Systems | `systems/` | One page per subsystem: its job, design, and behavior. |
| 4. API | `api/` | Public class reference, one page per public header in `include/`. |
| 5. Guides | `guides/` | Task-oriented how-tos (embedding, building, contributing). |

Each Systems page links across to its matching API page, and vice versa.

---

## Page template

Every content page follows the same skeleton. Not every section is mandatory, but the order is fixed so pages feel consistent.

1. **Front matter** (required — see below).
2. **Title** (`# Heading`) matching the front-matter `title`.
3. **One-paragraph orientation** — what this page covers and who it is for.
4. **Why it exists** — the problem this component solves. Motivation before mechanism.
5. **Concepts** — the vocabulary and mental model, terms defined on first use.
6. **Detail** — how it actually works, in prose, with diagrams where flow is dense.
7. **Examples** — concrete usage where applicable (API and Guides tiers especially).
8. **See also** — links to related pages (the cross-tier links).
9. **Navigation footer** — previous / next within the reading path.

---

## Front matter

Every page begins with a YAML block. This is the machine-readable contract that keeps the wiki synchronized with the code.

```yaml
---
title: Scene System
tier: Systems
audience: [evaluator, integrator, contributor]
sources:
  - include/Scene.h
  - src/context/scene/Scene.cpp
  - src/context/scene/Node.cpp
  - src/context/scene/Fabric.cpp
  - include/Map_Object.h
verified: 92fdc1c
nav:
  prev: systems/storage.md
  next: systems/viewport.md
---
```

| Field | Meaning |
|---|---|
| `title` | Human title; matches the page's `# H1`. |
| `tier` | One of `Overview`, `Architecture`, `Systems`, `API`, `Guides`, `Meta`. |
| `audience` | Optional. The reader(s) the page primarily serves. |
| `sources` | **The list of code files this page documents.** Repo-relative paths. This is the durable link between doc and code. List every file whose behavior the page describes. |
| `verified` | **The commit sha this page was last checked against.** When you write or re-verify a page, set this to the current `HEAD`. |
| `nav` | Optional previous/next pages in the reading path. |

Overview-tier pages describe ideas rather than specific code; their `sources` may be empty. Every Systems and API page must list real `sources`.

---

## Keeping the wiki true to the code (maintenance loop)

Documentation rots when code moves and nobody remembers which page depended on it. The `sources`/`verified` fields make that dependency explicit and checkable.

The loop, run whenever you want to confirm the wiki is current:

1. **Run the drift detector** (`tools/DocDrift/`). For each page it runs, in effect, `git log <verified>..HEAD -- <sources>` and reports any page whose source files changed since its `verified` sha.
2. **Open each flagged page** and compare it against the current code. The code wins on every conflict, always.
3. **Fix any drift**, then **bump `verified`** on that page to the current `HEAD`.

The detector never edits docs — it only tells a human where to look. The full narrative of this workflow lives in [guides/contributing.md](guides/contributing.md).

Known limitation: the `sources` list is hand-maintained, so the detector catches changes to files you *listed* but not coverage you *forgot* to list. When you add a new source file to a subsystem, add it to the relevant page's `sources`.

---

## Source-of-truth rule

Facts come from `include/*.h` and the current source under `src/`. The terse `src/**/*.md` reference docs, any project knowledge base, and external architecture documents are **unverified hints** — they may be stale or misleading. Read the code to confirm class names, signatures, ownership, lifecycles, and control flow before writing them down. When a hint and the code disagree, the code is correct and the hint is wrong.

---

## Formatting

- **Prose over bullet dumps.** Bullets are for genuine lists; explanation is paragraphs.
- **Define then use.** First appearance of a term gets a definition.
- **Code blocks** are fenced with a language tag (` ```cpp `, ` ```text `, ` ```mermaid `).
- **Diagrams** use mermaid. Node IDs have no spaces; quote labels with special characters.
- **Class and type names** use the project's `ALL_CAPS` convention in prose and match the code exactly (`SCENE`, `FABRIC`, `MAP_OBJECT`, `RMCOBJECT`).
- **Relative links** between wiki pages, repo-relative paths to code files.

---

[Home](Home.md)
