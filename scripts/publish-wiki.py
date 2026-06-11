#!/usr/bin/env python3
# Copyright 2026 Metaversal Corporation. All rights reserved.
#
# Publish docs/**/*.md to a MediaWiki site (omb.wiki/Sneeze/...).
# Source of truth is the git tree; the wiki is a mirror.
#
# Usage:
#   python scripts/publish-wiki.py --dry-run
#   python scripts/publish-wiki.py --all
#   python scripts/publish-wiki.py --page docs/systems/scene.md
#
# Environment (live publish only):
#   MEDIAWIKI_API       e.g. https://omb.wiki/w/api.php
#   MEDIAWIKI_USER      e.g. MainAccount@SneezeDocs
#   MEDIAWIKI_PASSWORD  bot password from Special:BotPasswords

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import urllib.parse
import urllib.request
from http.cookiejar import CookieJar
from pathlib import Path

TIER_MAP = {
   "overview": "Overview",
   "architecture": "Architecture",
   "systems": "Systems",
   "api": "API",
   "guides": "Guides",
}

LINK_RE = re.compile(r"\[([^\]]+)\]\(([^)]+)\)")
FRONT_MATTER_RE = re.compile(r"\A---\r?\n.*?\r?\n---\r?\n", re.DOTALL)


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


def doc_relpath_to_wiki_title(rel: str, prefix: str, home: str) -> str:
   rel = rel.replace("\\", "/")
   if rel == home or rel == Path(home).stem:
      return prefix
   stem = rel
   if stem.endswith(".md"):
      stem = stem[:-3]
   parts = stem.split("/")
   if parts and parts[0].lower() in TIER_MAP:
      parts[0] = TIER_MAP[parts[0].lower()]
   return f"{prefix}/{'/'.join(parts)}"


def build_title_map(docs_root: Path, prefix: str, home: str) -> dict[str, str]:
   mapping: dict[str, str] = {}
   for path in list_doc_pages(docs_root):
      rel = doc_rel_path(docs_root, path)
      mapping[rel] = doc_relpath_to_wiki_title(rel, prefix, home)
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


def rewrite_links(body: str, from_doc: Path, docs_root: Path, title_map: dict[str, str], prefix: str, home: str) -> str:
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
      wiki_title = title_map.get(path)
      if not wiki_title:
         rel_try = path
         wiki_title = doc_relpath_to_wiki_title(rel_try, prefix, home)
      if anchor:
         return f"[[{wiki_title}#{anchor}|{label}]]"
      if label and label != wiki_title.split("/")[-1]:
         return f"[[{wiki_title}|{label}]]"
      return f"[[{wiki_title}]]"

   return LINK_RE.sub(replace, body)


def strip_front_matter(text: str) -> str:
   return FRONT_MATTER_RE.sub("", text, count=1)


def markdown_to_wikitext(markdown: str) -> str:
   proc = subprocess.run(
      ["pandoc", "-f", "markdown", "-t", "mediawiki", "--wrap=none"],
      input=markdown,
      capture_output=True,
      text=True,
      check=False,
   )
   if proc.returncode != 0:
      print(proc.stderr, file=sys.stderr)
      raise RuntimeError("pandoc failed")
   return proc.stdout.strip() + "\n"


def append_source_footer(wikitext: str, rel: str, verified: str | None, repo_url: str, sha: str) -> str:
   source_url = f"{repo_url}/blob/{sha}/docs/{rel}"
   footer = f"\n----\n''Source:'' [{rel}]({source_url})"
   if verified:
      footer += f" · ''Verified:'' `{verified}`"
   return wikitext + footer


def parse_verified(text: str) -> str | None:
   m = re.search(r"^verified:\s*([0-9a-f]+)\s*$", text, re.MULTILINE)
   return m.group(1) if m else None


class MediaWikiClient:
   def __init__(self, api_url: str, username: str, password: str) -> None:
      self.api_url = api_url
      self.username = username
      self.password = password
      self.cj = CookieJar()
      self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.cj))
      self.token: str | None = None

   def _post(self, data: dict[str, str]) -> dict:
      body = urllib.parse.urlencode(data).encode("utf-8")
      req = urllib.request.Request(self.api_url, data=body, method="POST")
      with self.opener.open(req, timeout=120) as resp:
         payload = json.loads(resp.read().decode("utf-8"))
      if "error" in payload:
         raise RuntimeError(payload["error"].get("info", str(payload["error"])))
      return payload

   def login(self) -> None:
      token = self._post({"action": "query", "meta": "tokens", "type": "login", "format": "json"})["query"]["tokens"]["logintoken"]
      self._post({
         "action": "login",
         "lgname": self.username,
         "lgpassword": self.password,
         "lgtoken": token,
         "format": "json",
      })
      self.token = self._post({"action": "query", "meta": "tokens", "type": "csrf", "format": "json"})["query"]["tokens"]["csrftoken"]

   def edit(self, title: str, text: str, summary: str) -> None:
      if not self.token:
         raise RuntimeError("Not logged in")
      self._post({
         "action": "edit",
         "title": title,
         "text": text,
         "summary": summary,
         "token": self.token,
         "format": "json",
      })


def prepare_page(path: Path, docs_root: Path, config: dict, title_map: dict[str, str], sha: str) -> tuple[str, str, str]:
   rel = doc_rel_path(docs_root, path)
   raw = path.read_text(encoding="utf-8")
   verified = parse_verified(raw)
   body = strip_front_matter(raw)
   body = rewrite_links(body, path, docs_root, title_map, config["wiki_prefix"], config["home"])
   wikitext = markdown_to_wikitext(body)
   wikitext = append_source_footer(wikitext, rel, verified, config["repo_url"], sha)
   title = doc_relpath_to_wiki_title(rel, config["wiki_prefix"], config["home"])
   return title, wikitext, rel


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
   parser = argparse.ArgumentParser(description="Publish Sneeze docs/ to MediaWiki")
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

   title_map = build_title_map(docs_root, config["wiki_prefix"], config["home"])
   sha = git_head(repo_root)
   summary = f"Sync from MetaversalCorp/Sneeze@{sha[:12]}"

   api_url = os.environ.get("MEDIAWIKI_API", config.get("default_api", "")).strip()
   user = os.environ.get("MEDIAWIKI_USER", "").strip()
   password = os.environ.get("MEDIAWIKI_PASSWORD", "").strip()

   if args.dry_run:
      print(f"publish-wiki: dry-run | pages={len(pages)} | HEAD={sha}")
      for path in pages:
         title, wikitext, rel = prepare_page(path, docs_root, config, title_map, sha)
         print(f"  {rel} -> {title} ({len(wikitext)} bytes wikitext)")
      return 0

   if not api_url or not user or not password:
      print("::notice::MEDIAWIKI_API / MEDIAWIKI_USER / MEDIAWIKI_PASSWORD not set; skipping live publish.")
      print("::notice::Run with --dry-run to validate transforms until wiki API access is configured.")
      return 0

   client = MediaWikiClient(api_url, user, password)
   client.login()
   print(f"publish-wiki: publishing {len(pages)} page(s) to {api_url}")

   for path in pages:
      title, wikitext, rel = prepare_page(path, docs_root, config, title_map, sha)
      print(f"  edit {title} <- {rel}")
      client.edit(title, wikitext, summary)

   print(f"publish-wiki: done ({len(pages)} page(s))")
   return 0


if __name__ == "__main__":
   sys.exit(main())
