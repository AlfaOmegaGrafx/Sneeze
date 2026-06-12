#!/usr/bin/env python3
# Copyright 2026 Metaversal Corporation. All rights reserved.
#
# Publish docs/**/*.md to omb.wiki (Wiki.js) under /sneeze/...
# Home.md replaces the existing /sneeze landing page.
#
# Usage:
#   python scripts/publish-wiki.py --dry-run
#   python scripts/publish-wiki.py --all
#   python scripts/publish-wiki.py --page docs/systems/scene.md
#
# Environment (live publish only):
#   WIKIJS_GRAPHQL_URL  e.g. https://omb.wiki/graphql
#   WIKIJS_API_TOKEN    bearer token from Wiki.js Administration > API Access

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

LINK_RE = re.compile(r"\[([^\]]+)\]\(([^)]+)\)")
FRONT_MATTER_RE = re.compile(r"\A---\r?\n.*?\r?\n---\r?\n", re.DOTALL)
TITLE_RE = re.compile(r"^title:\s*(.+?)\s*$", re.MULTILINE)
VERIFIED_RE = re.compile(r"^verified:\s*([0-9a-f]+)\s*$", re.MULTILINE)

CREATE_MUTATION = """
mutation PublishCreate($content: String!, $description: String!, $editor: String!, $isPublished: Boolean!, $isPrivate: Boolean!, $locale: String!, $path: String!, $tags: [String]!, $title: String!) {
   pages {
      create(content: $content, description: $description, editor: $editor, isPublished: $isPublished, isPrivate: $isPrivate, locale: $locale, path: $path, tags: $tags, title: $title) {
         responseResult { succeeded errorCode slug message }
         page { id path }
      }
   }
}
"""

UPDATE_MUTATION = """
mutation PublishUpdate($id: Int!, $content: String!, $description: String!, $editor: String!, $isPublished: Boolean!, $isPrivate: Boolean!, $locale: String!, $path: String!, $tags: [String]!, $title: String!) {
   pages {
      update(id: $id, content: $content, description: $description, editor: $editor, isPublished: $isPublished, isPrivate: $isPrivate, locale: $locale, path: $path, tags: $tags, title: $title) {
         responseResult { succeeded errorCode slug message }
         page { id path }
      }
   }
}
"""

PAGE_BY_PATH_QUERY = """
query PageByPath($path: String!, $locale: String!) {
   pages {
      singleByPath(path: $path, locale: $locale) {
         id
         path
         title
      }
   }
}
"""


def find_repo_root(start: Path) -> Path:
   p = start.resolve()
   for candidate in [p, *p.parents]:
      if (candidate / ".git").is_dir():
         return candidate
   print("::error::Not inside a git repository", file=sys.stderr)
   sys.exit(2)


def load_config(repo_root: Path) -> dict:
   config_path = repo_root / "docs" / "wiki" / "publish.json"
   if not config_path.is_file():
      print(f"::error::Missing {config_path}", file=sys.stderr)
      sys.exit(2)
   with config_path.open(encoding="utf-8") as f:
      return json.load(f)


def list_doc_pages(docs_root: Path) -> list[Path]:
   pages: list[Path] = []
   for path in sorted(docs_root.rglob("*.md")):
      if not path.is_file():
         continue
      rel = path.relative_to(docs_root)
      if rel.parts and rel.parts[0].lower() == "wiki":
         continue
      pages.append(path)
   return pages


def doc_rel_path(docs_root: Path, path: Path) -> str:
   return path.relative_to(docs_root).as_posix()


def doc_relpath_to_wiki_path(rel: str, path_prefix: str, home: str) -> str:
   rel = rel.replace("\\", "/")
   prefix = path_prefix.rstrip("/")
   if rel == home:
      return prefix
   stem = rel[:-3] if rel.endswith(".md") else rel
   return f"{prefix}/{stem}"


def build_path_map(docs_root: Path, path_prefix: str, home: str) -> dict[str, str]:
   mapping: dict[str, str] = {}
   for path in list_doc_pages(docs_root):
      rel = doc_rel_path(docs_root, path)
      mapping[rel] = doc_relpath_to_wiki_path(rel, path_prefix, home)
      mapping[Path(rel).stem + ".md"] = mapping[rel]
   return mapping


def resolve_link(from_doc: Path, docs_root: Path, target: str) -> str | None:
   target = target.strip()
   if not target or target.startswith("#"):
      return None
   if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target):
      return None
   anchor = ""
   if "#" in target:
      target, anchor = target.split("#", 1)
   if not target:
      return anchor or None
   if target.startswith("/"):
      resolved = (docs_root / target.lstrip("/")).resolve()
   else:
      resolved = (from_doc.parent / target).resolve()
   try:
      resolved.relative_to(docs_root.resolve())
   except ValueError:
      return None
   rel = resolved.relative_to(docs_root.resolve()).as_posix()
   if not rel.endswith(".md"):
      rel += ".md"
   return rel + (f"#{anchor}" if anchor else "")


def rewrite_links(body: str, from_doc: Path, docs_root: Path, path_map: dict[str, str], path_prefix: str, home: str) -> str:
   def replace(match: re.Match[str]) -> str:
      label = match.group(1)
      raw = match.group(2)
      resolved = resolve_link(from_doc, docs_root, raw)
      if resolved is None:
         return match.group(0)
      anchor = ""
      path = resolved
      if "#" in resolved:
         path, anchor = resolved.split("#", 1)
      wiki_path = path_map.get(path)
      if not wiki_path:
         wiki_path = doc_relpath_to_wiki_path(path, path_prefix, home)
      href = f"{wiki_path}#{anchor}" if anchor else wiki_path
      return f"[{label}]({href})"

   return LINK_RE.sub(replace, body)


def strip_front_matter(text: str) -> str:
   return FRONT_MATTER_RE.sub("", text, count=1)


def parse_title(text: str, rel: str) -> str:
   match = TITLE_RE.search(text)
   if match:
      return match.group(1).strip().strip('"').strip("'")
   stem = Path(rel).stem
   if stem == "Home":
      return "Sneeze Documentation"
   return stem.replace("-", " ").replace("_", " ")


def parse_verified(text: str) -> str | None:
   match = VERIFIED_RE.search(text)
   return match.group(1) if match else None


def append_source_footer(markdown: str, rel: str, verified: str | None, repo_url: str, sha: str) -> str:
   source_url = f"{repo_url}/blob/{sha}/docs/{rel}"
   footer = f"\n\n---\n\n*Source:* [{rel}]({source_url})"
   if verified:
      footer += f" · *Verified:* `{verified}`"
   return markdown.rstrip() + footer


class WikiJsClient:
   def __init__(self, graphql_url: str, token: str) -> None:
      self.graphql_url = graphql_url
      self.token = token

   def graphql(self, query: str, variables: dict | None = None) -> dict:
      payload = json.dumps({"query": query, "variables": variables or {}}).encode("utf-8")
      req = urllib.request.Request(
         self.graphql_url,
         data=payload,
         method="POST",
         headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {self.token}",
         },
      )
      try:
         with urllib.request.urlopen(req, timeout=120) as resp:
            body = json.loads(resp.read().decode("utf-8"))
      except urllib.error.HTTPError as exc:
         detail = exc.read().decode("utf-8", errors="replace")
         raise RuntimeError(f"HTTP {exc.code}: {detail}") from exc
      if body.get("errors"):
         raise RuntimeError(json.dumps(body["errors"], indent=2))
      return body.get("data") or {}

   def page_id_for_path(self, path: str, locale: str) -> int | None:
      data = self.graphql(PAGE_BY_PATH_QUERY, {"path": path, "locale": locale})
      page = (((data.get("pages") or {}).get("singleByPath")) or None)
      if not page:
         return None
      return int(page["id"])

   def upsert(self, path: str, title: str, content: str, description: str, locale: str, tags: list[str]) -> None:
      page_id = self.page_id_for_path(path, locale)
      variables = {
         "content": content,
         "description": description,
         "editor": "markdown",
         "isPublished": True,
         "isPrivate": False,
         "locale": locale,
         "path": path,
         "tags": tags,
         "title": title,
      }
      if page_id is None:
         data = self.graphql(CREATE_MUTATION, variables)
         result = (((data.get("pages") or {}).get("create") or {}).get("responseResult")) or {}
         if result.get("succeeded"):
            return
         if result.get("errorCode") == 6002:
            page_id = self.page_id_for_path(path, locale)
         else:
            raise RuntimeError(result.get("message") or json.dumps(result))
      if page_id is None:
         raise RuntimeError("page exists but could not be resolved for update")
      variables["id"] = page_id
      data = self.graphql(UPDATE_MUTATION, variables)
      result = (((data.get("pages") or {}).get("update") or {}).get("responseResult")) or {}
      if not result.get("succeeded"):
         raise RuntimeError(result.get("message") or json.dumps(result))


def prepare_page(path: Path, docs_root: Path, config: dict, path_map: dict[str, str], sha: str) -> tuple[str, str, str, str]:
   rel = doc_rel_path(docs_root, path)
   raw = path.read_text(encoding="utf-8")
   verified = parse_verified(raw)
   title = parse_title(raw, rel)
   body = strip_front_matter(raw)
   body = rewrite_links(body, path, docs_root, path_map, config["path_prefix"], config["home"])
   body = append_source_footer(body, rel, verified, config["repo_url"], sha)
   wiki_path = doc_relpath_to_wiki_path(rel, config["path_prefix"], config["home"])
   return wiki_path, title, body, rel


def git_head(repo_root: Path) -> str:
   proc = subprocess.run(
      ["git", "rev-parse", "HEAD"],
      cwd=repo_root,
      capture_output=True,
      text=True,
      check=True,
   )
   return proc.stdout.strip()


def main() -> int:
   parser = argparse.ArgumentParser(description="Publish Sneeze docs/ to omb.wiki (Wiki.js)")
   parser.add_argument("--dry-run", action="store_true", help="Transform only; do not call the API")
   parser.add_argument("--all", action="store_true", help="Publish every docs/**/*.md page")
   parser.add_argument("--page", action="append", default=[], help="Publish one repo-relative docs path")
   parser.add_argument("--config", default="", help="Override publish.json path")
   args = parser.parse_args()

   repo_root = find_repo_root(Path(__file__).resolve().parent)
   config_path = Path(args.config) if args.config else repo_root / "docs" / "wiki" / "publish.json"
   with config_path.open(encoding="utf-8") as f:
      config = json.load(f)

   docs_root = repo_root / config["docs_root"]
   if not docs_root.is_dir():
      print(f"::error::Docs root missing: {docs_root}", file=sys.stderr)
      return 2

   if not args.all and not args.page:
      parser.error("Specify --all or at least one --page")

   pages: list[Path] = []
   if args.all:
      pages = list_doc_pages(docs_root)
   else:
      for item in args.page:
         path = Path(item)
         if not path.is_absolute():
            path = repo_root / path
         if not path.is_file():
            print(f"::error::Page not found: {item}", file=sys.stderr)
            return 2
         pages.append(path)

   path_map = build_path_map(docs_root, config["path_prefix"], config["home"])
   sha = git_head(repo_root)
   locale = config.get("locale", "en")
   description = f"Synced from MetaversalCorp/Sneeze@{sha[:12]}"

   graphql_url = os.environ.get("WIKIJS_GRAPHQL_URL", config.get("graphql_url", "")).strip()
   token = os.environ.get("WIKIJS_API_TOKEN", "").strip()

   if args.dry_run:
      print(f"publish-wiki: dry-run | pages={len(pages)} | HEAD={sha}")
      for path in pages:
         wiki_path, title, markdown, rel = prepare_page(path, docs_root, config, path_map, sha)
         print(f"  {rel} -> {wiki_path} ({title}, {len(markdown)} bytes markdown)")
      return 0

   if not graphql_url or not token:
      print("::notice::WIKIJS_GRAPHQL_URL / WIKIJS_API_TOKEN not set; skipping live publish.")
      print("::notice::Run with --dry-run to validate transforms until wiki API access is configured.")
      return 0

   client = WikiJsClient(graphql_url, token)
   print(f"publish-wiki: publishing {len(pages)} page(s) to {graphql_url}")

   for path in pages:
      wiki_path, title, markdown, rel = prepare_page(path, docs_root, config, path_map, sha)
      print(f"  upsert {wiki_path} <- {rel}")
      client.upsert(wiki_path, title, markdown, description, locale, ["sneeze", "docs"])

   print(f"publish-wiki: done ({len(pages)} page(s))")
   return 0


if __name__ == "__main__":
   sys.exit(main())
