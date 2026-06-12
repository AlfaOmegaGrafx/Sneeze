#!/usr/bin/env python3
# Copyright 2026 Metaversal Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
DocDrift -- detect wiki pages that may be out of date with the code they document.

Every page under docs/ carries YAML front matter with two fields that link it to the
source tree:

    sources:               repo-relative code files the page documents
      - include/Scene.h
      - src/context/scene/Scene.cpp
    verified: 92fdc1c      the commit the page was last checked against

For each page, DocDrift asks git whether any of the page's `sources` changed between the
page's `verified` commit and HEAD. If so, the page is flagged for review. DocDrift is
READ-ONLY: it never edits documentation. It only reports where a human (or an agent)
should look, per the maintenance loop in docs/guides/contributing.md.

Usage:
    python tools/DocDrift/docdrift.py [--docs DIR] [--strict] [--quiet]

    --docs DIR   Documentation root to scan (default: docs/ at the repo root).
    --strict     Exit non-zero if any page is flagged (default: warn-only, exit 0).
    --quiet      Only print flagged pages and the summary line.

Exit codes:
    0  no drift (or warn-only mode, regardless of findings)
    1  drift found AND --strict was given
    2  a usage / environment error (not a git repo, docs dir missing, etc.)
"""

import argparse
import os
import subprocess
import sys


def run_git(args, cwd):
   """Run a git command, returning (stdout, returncode). Never raises on git error."""
   try:
      result = subprocess.run(
         ["git"] + args,
         cwd=cwd,
         stdout=subprocess.PIPE,
         stderr=subprocess.PIPE,
         text=True,
      )
      return result.stdout.strip(), result.returncode
   except FileNotFoundError:
      return "", 127


def repo_root():
   """Locate the git repository root containing this script."""
   here = os.path.dirname(os.path.abspath(__file__))
   out, code = run_git(["rev-parse", "--show-toplevel"], here)
   return out if code == 0 and out else None


def parse_front_matter(path):
   """
   Extract `sources` (list) and `verified` (str) from a page's YAML front matter.

   Deliberately a minimal parser -- it handles exactly the front-matter shape this wiki
   uses (a leading '---' fenced block, a `sources:` list of `  - path` items, and a
   `verified: sha` scalar) without depending on a YAML library.
   """
   sources = []
   verified = None

   try:
      with open(path, "r", encoding="utf-8") as f:
         lines = f.read().splitlines()
   except OSError:
      return sources, verified

   if not lines or lines[0].strip() != "---":
      return sources, verified

   in_sources = False
   for line in lines[1:]:
      stripped = line.strip()

      if stripped == "---":
         break

      # A new top-level key ends any list we were collecting.
      if line and not line[0].isspace() and ":" in line:
         in_sources = False

      if stripped.startswith("sources:"):
         in_sources = True
         remainder = stripped[len("sources:"):].strip()
         # Support inline empty list: "sources: []"
         if remainder == "[]":
            in_sources = False
         continue

      if stripped.startswith("verified:"):
         verified = stripped[len("verified:"):].strip()
         in_sources = False
         continue

      if in_sources and stripped.startswith("- "):
         sources.append(stripped[2:].strip())

   return sources, verified


def collect_pages(docs_dir):
   """Return every *.md file under docs_dir, sorted."""
   pages = []
   for root, _dirs, files in os.walk(docs_dir):
      for name in files:
         if name.lower().endswith(".md"):
            pages.append(os.path.join(root, name))
   pages.sort()
   return pages


def check_page(root, page, default_branch):
   """
   Return a result dict describing the page's drift status.

   status is one of:
      "ok"        sources unchanged since `verified`
      "drift"     one or more sources changed since `verified`
      "no-sources" page documents no code files (overview/meta pages)
      "no-verified" page lacks a `verified` sha
      "bad-verified" the `verified` sha is not a valid commit
   """
   sources, verified = parse_front_matter(page)
   rel = os.path.relpath(page, root).replace(os.sep, "/")

   if not sources:
      return {"page": rel, "status": "no-sources"}

   if not verified:
      return {"page": rel, "status": "no-verified", "sources": sources}

   # Confirm the verified sha actually resolves to a commit.
   _out, code = run_git(["rev-parse", "--verify", "--quiet", verified + "^{commit}"], root)
   if code != 0:
      return {"page": rel, "status": "bad-verified", "verified": verified}

   # Which sources actually exist on disk? A removed source is itself a drift signal.
   missing = [s for s in sources if not os.path.exists(os.path.join(root, s))]

   # Ask git what changed in these files since `verified`.
   log, _code = run_git(
      ["log", "--oneline", f"{verified}..{default_branch}", "--"] + sources,
      root,
   )
   commits = [c for c in log.splitlines() if c.strip()]

   if commits or missing:
      return {
         "page": rel,
         "status": "drift",
         "verified": verified,
         "commits": commits,
         "missing": missing,
         "sources": sources,
      }

   return {"page": rel, "status": "ok", "verified": verified}


def main():
   parser = argparse.ArgumentParser(description="Detect documentation drift against the code.")
   parser.add_argument("--docs", default=None, help="Documentation root (default: <repo>/docs).")
   parser.add_argument("--strict", action="store_true", help="Exit non-zero if any page drifts.")
   parser.add_argument("--quiet", action="store_true", help="Print only flagged pages and the summary.")
   args = parser.parse_args()

   root = repo_root()
   if not root:
      print("docdrift: not inside a git repository (git not found or no repo).", file=sys.stderr)
      return 2

   docs_dir = args.docs if args.docs else os.path.join(root, "docs")
   if not os.path.isdir(docs_dir):
      print(f"docdrift: docs directory not found: {docs_dir}", file=sys.stderr)
      return 2

   head, code = run_git(["rev-parse", "HEAD"], root)
   if code != 0:
      print("docdrift: could not resolve HEAD.", file=sys.stderr)
      return 2

   pages = collect_pages(docs_dir)

   drift = []
   warnings = []
   ok = 0
   skipped = 0

   for page in pages:
      result = check_page(root, page, "HEAD")
      status = result["status"]
      if status == "drift":
         drift.append(result)
      elif status in ("no-verified", "bad-verified"):
         warnings.append(result)
      elif status == "no-sources":
         skipped += 1
      else:
         ok += 1

   # --- Report ---------------------------------------------------------------

   if drift:
      print("DRIFT -- these pages document code that changed since they were last verified:\n")
      for r in drift:
         print(f"  {r['page']}  (verified: {r['verified']})")
         for s in r.get("missing", []):
            print(f"      ! source no longer exists: {s}")
         for c in r.get("commits", []):
            print(f"      - {c}")
         print()

   if warnings and not args.quiet:
      print("WARNINGS -- pages with missing or invalid 'verified' front matter:\n")
      for r in warnings:
         if r["status"] == "no-verified":
            print(f"  {r['page']}  (no 'verified' field)")
         else:
            print(f"  {r['page']}  (unknown commit: {r['verified']})")
      print()

   total = len(pages)
   print(
      f"docdrift: {total} page(s) scanned | "
      f"{ok} current | {len(drift)} drifted | {len(warnings)} warning(s) | "
      f"{skipped} without sources (skipped). HEAD={head[:7]}"
   )

   if drift and args.strict:
      return 1
   return 0


if __name__ == "__main__":
   sys.exit(main())
