#!/usr/bin/env python3
# Copyright 2026 Metaversal Corporation. All rights reserved.
#
# Publish docs/**/*.md to omb.wiki (Wiki.js) under /sneeze/...
# Home.md replaces the existing /sneeze landing page.
#
# Usage:
#   python scripts/publish-wiki.py --probe          # verify API read + write before bulk publish
#   python scripts/publish-wiki.py --dry-run --all
#   python scripts/publish-wiki.py --all            # publish all 72 docs pages
#   python scripts/publish-wiki.py --config docs/wiki/publish.rp1.json --probe
#   python scripts/publish-wiki.py --config docs/wiki/publish.rp1.json --all
#
# Environment (live publish only):
#   WIKIJS_GRAPHQL_URL  e.g. https://omb.wiki/graphql
#   WIKIJS_API_TOKEN    bearer token from Wiki.js Administration > API Access
#   CF_ACCESS_CLIENT_ID / CF_ACCESS_CLIENT_SECRET  optional Cloudflare Access service token

from __future__ import annotations

import argparse
import base64
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

# Wiki.js v2 bug: pages.single / singleByPath check manage:pages + delete:pages in the
# resolver even though the schema only requires read:pages. pages.list works with
# read:pages alone, so we index paths from list for upsert lookups.
# https://github.com/requarks/wiki/issues/3205
PAGE_LIST_QUERY = """
query PageList {
   pages {
      list {
         id
         path
         locale
      }
   }
}
"""

RENDER_MUTATION = """
mutation RenderPage($id: Int!) {
   pages {
      render(id: $id) {
         responseResult { succeeded message }
      }
   }
}
"""

DELETE_MUTATION = """
mutation PublishDelete($id: Int!) {
   pages {
      delete(id: $id) {
         responseResult { succeeded message errorCode }
      }
   }
}
"""

PROBE_PATH = "sneeze/_publish_probe"

RENDER_PROBE_MUTATION = """
mutation RenderProbe($id: Int!) {
   pages {
      render(id: $id) {
         responseResult { succeeded message }
      }
   }
}
"""


def decode_jwt_payload(token: str) -> dict:
   parts = token.strip().split(".")
   if len(parts) < 2:
      return {}
   segment = parts[1] + "=" * (-len(parts[1]) % 4)
   try:
      return json.loads(base64.urlsafe_b64decode(segment))
   except (json.JSONDecodeError, ValueError):
      return {}

USER_AGENT = (
   "Sneeze-Docs-Publisher/1.0 "
   "(+https://github.com/MetaversalCorp/Sneeze/actions/workflows/docs.yml)"
)


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


def order_publish_pages(pages: list[Path], docs_root: Path, home: str) -> list[Path]:
   home_path = (docs_root / home).resolve()
   rest = sorted([p for p in pages if p.resolve() != home_path], key=lambda p: doc_rel_path(docs_root, p))
   if any(p.resolve() == home_path for p in pages):
      rest.append(docs_root / home)
   return rest


def doc_rel_path(docs_root: Path, path: Path) -> str:
   return path.relative_to(docs_root).as_posix()


def doc_relpath_to_wiki_path(rel: str, path_prefix: str, home: str) -> str:
   rel = rel.replace("\\", "/")
   prefix = path_prefix.strip("/")
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
      href = f"/{wiki_path}#{anchor}" if anchor else f"/{wiki_path}"
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
      self._page_index: dict[tuple[str, str], int] | None = None

   def _request_headers(self) -> dict[str, str]:
      headers = {
         "Content-Type": "application/json",
         "Authorization": f"Bearer {self.token}",
         "User-Agent": USER_AGENT,
         "Accept": "application/json",
      }
      cf_id = os.environ.get("CF_ACCESS_CLIENT_ID", "").strip()
      cf_secret = os.environ.get("CF_ACCESS_CLIENT_SECRET", "").strip()
      if cf_id and cf_secret:
         headers["CF-Access-Client-Id"] = cf_id
         headers["CF-Access-Client-Secret"] = cf_secret
      return headers

   @staticmethod
   def _http_error_message(code: int, detail: str) -> str:
      if code == 403 and "1010" in detail:
         return (
            f"HTTP 403 (Cloudflare error 1010): {detail}\n"
            "Cloudflare blocked this request before it reached Wiki.js. GitHub Actions "
            "runners are often blocked by Bot Fight Mode or browser-integrity checks.\n"
            "Ask the omb.wiki admin to add a WAF skip rule for POST /graphql (or allow "
            "this User-Agent), or configure CF_ACCESS_CLIENT_ID + CF_ACCESS_CLIENT_SECRET "
            "repository secrets if the site uses Cloudflare Access."
         )
      return f"HTTP {code}: {detail}"

   def graphql_raw(self, query: str, variables: dict | None = None) -> dict:
      payload = json.dumps({"query": query, "variables": variables or {}}).encode("utf-8")
      req = urllib.request.Request(
         self.graphql_url,
         data=payload,
         method="POST",
         headers=self._request_headers(),
      )
      try:
         with urllib.request.urlopen(req, timeout=300) as resp:
            raw = resp.read().decode("utf-8")
            try:
               return json.loads(raw)
            except json.JSONDecodeError as je:
               raise RuntimeError(
                  f"Server returned non-JSON ({len(raw)} bytes). "
                  f"First 500 chars: {raw[:500]}"
               ) from je
      except urllib.error.HTTPError as exc:
         detail = exc.read().decode("utf-8", errors="replace")
         raise RuntimeError(self._http_error_message(exc.code, detail)) from exc

   def graphql(self, query: str, variables: dict | None = None, ignore_codes: set[int] | None = None) -> dict:
      body = self.graphql_raw(query, variables)
      if body.get("errors"):
         if ignore_codes and self._errors_only_codes(body["errors"], ignore_codes):
            return body.get("data") or {}
         raise RuntimeError(self._format_graphql_errors(body["errors"]))
      return body.get("data") or {}

   @staticmethod
   def _format_graphql_errors(errors: list) -> str:
      lines = [json.dumps(errors, indent=2)]
      for err in errors:
         if err.get("message") != "Forbidden":
            continue
         path = err.get("path") or []
         ext = err.get("extensions") or {}
         if ext.get("code") == "INTERNAL_SERVER_ERROR":
            lines.append(
               "\nWiki.js returned Forbidden with extensions.code INTERNAL_SERVER_ERROR. "
               "That is not a server crash — the @auth directive throws a plain Error('Forbidden') "
               "when the API token's permissions array lacks a required scope. The mutation never runs."
            )
         if "create" in path or "update" in path:
            lines.append(
               "\nRequired scope for pages.create / pages.update: one of write:pages, manage:pages, "
               "or manage:system in the token's effective permissions.\n"
               "pages.list succeeding only proves read:pages (or manage:system) — not write access.\n"
               "Full Access API keys are not a bypass: Wiki.js sets JWT grp=1 and loads "
               "WIKI.auth.groups[1].permissions from the database (Administrators group). "
               "If that array is missing write:pages / manage:system, Full Access still fails here.\n"
               "Checks on omb.wiki:\n"
               "  1. SELECT id, name, permissions FROM groups WHERE id = 1; "
               "(must include manage:system or write:pages)\n"
               "  2. Restart Wiki.js after creating or revoking API keys (reloadApiKeys / reloadGroups)\n"
               "  3. Or create a non-Full-Access key bound to a group that has write:pages + "
               "ALLOW page rules for sneeze/\n"
               "Test write access:\n"
               '  curl -X POST https://omb.wiki/graphql -H "Authorization: Bearer <token>" '
               '-H "Content-Type: application/json" -d '
               '\'{"query":"mutation { pages { create(path: \\"sneeze/_write_test\\", title: \\"test\\", '
               'content: \\"x\\", description: \\"\\", editor: \\"markdown\\", isPublished: true, '
               'isPrivate: false, locale: \\"en\\", tags: []) { responseResult { succeeded message errorCode } } } }"}\''
            )
            break
         exc = (err.get("extensions") or {}).get("exception") or {}
         if exc.get("code") == 6013:
            lines.append(
               "\nWiki.js v2 bug: pages.singleByPath requires manage:pages + delete:pages in the "
               "resolver (not just read:pages). publish-wiki.py uses pages.list instead; if you "
               "see this elsewhere, enable delete:pages on the API group or upgrade Wiki.js."
            )
            break
      return "\n".join(lines)

   @staticmethod
   def _errors_only_codes(errors: list, allowed: set[int]) -> bool:
      codes: set[int] = set()
      for err in errors:
         exc = (err.get("extensions") or {}).get("exception") or {}
         if "code" in exc:
            codes.add(int(exc["code"]))
      return bool(codes) and codes <= allowed

   def refresh_page_index(self) -> None:
      data = self.graphql(PAGE_LIST_QUERY)
      pages = ((data.get("pages") or {}).get("list")) or []
      self._page_index = {(p["path"], p["locale"]): int(p["id"]) for p in pages}

   def page_id_for_path(self, path: str, locale: str) -> int | None:
      if self._page_index is None:
         self.refresh_page_index()
      return self._page_index.get((path, locale))

   def note_page_id(self, path: str, locale: str, page_id: int) -> None:
      if self._page_index is not None:
         self._page_index[(path, locale)] = page_id

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
            page = (((data.get("pages") or {}).get("create") or {}).get("page")) or {}
            if page.get("id"):
               self.note_page_id(path, locale, int(page["id"]))
            return
         if result.get("errorCode") == 6002:
            self.refresh_page_index()
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

   def render(self, page_id: int) -> bool:
      body = self.graphql_raw(RENDER_MUTATION, {"id": page_id})
      if body.get("errors"):
         for err in body["errors"]:
            if err.get("message") == "Forbidden" and "render" in (err.get("path") or []):
               return False
         raise RuntimeError(self._format_graphql_errors(body["errors"]))
      result = (((body.get("data") or {}).get("pages") or {}).get("render") or {}).get("responseResult") or {}
      if not result.get("succeeded"):
         raise RuntimeError(result.get("message") or json.dumps(result))
      return True

   def probe(self, locale: str, path_prefix: str) -> int:
      print(f"publish-wiki: probing {self.graphql_url}")
      payload = decode_jwt_payload(self.token)
      api_id = payload.get("api")
      grp_id = payload.get("grp")
      if api_id is not None and grp_id is not None:
         print(f"  token: api key id={api_id}, group id={grp_id}")
         if grp_id == 1:
            print(
               "  note: Full Access tokens use group 1 (Administrators). That group's "
               "permissions are locked in the UI and loaded from the database — if group 1 "
               "lost manage:system, Full Access cannot write."
            )
         else:
            print(
               f"  note: non-Full-Access token — effective permissions come from group {grp_id} only. "
               "Enable write:pages on that group (not just read:pages)."
            )
      else:
         print("  token: could not decode JWT payload (not a Wiki.js API key?)")

      body = self.graphql_raw(PAGE_LIST_QUERY)
      if body.get("errors"):
         print("::error::READ FAILED — pages.list")
         print(self._format_graphql_errors(body["errors"]))
         return 1
      pages = (((body.get("data") or {}).get("pages") or {}).get("list")) or []
      sneeze_pages = [p for p in pages if str(p.get("path", "")).startswith(path_prefix)]
      print(f"  read:  OK ({len(pages)} page(s) visible, {len(sneeze_pages)} under {path_prefix}/)")

      if sneeze_pages:
         probe_render_id = int(sneeze_pages[0]["id"])
         render_body = self.graphql_raw(RENDER_PROBE_MUTATION, {"id": probe_render_id})
         if render_body.get("errors"):
            print("  manage:system: NO (pages.render blocked — token lacks manage:system)")
         else:
            rr = ((((render_body.get("data") or {}).get("pages") or {}).get("render") or {}).get("responseResult")) or {}
            if rr.get("succeeded"):
               print("  manage:system: YES (pages.render succeeded)")
            else:
               print(f"  manage:system: NO ({rr.get('message')})")

      variables = {
         "content": "publish-wiki probe — safe to delete",
         "description": "API write probe",
         "editor": "markdown",
         "isPublished": False,
         "isPrivate": True,
         "locale": locale,
         "path": PROBE_PATH,
         "tags": ["probe"],
         "title": "Publish probe",
      }
      body = self.graphql_raw(CREATE_MUTATION, variables)
      if body.get("errors"):
         print("::error::WRITE FAILED — pages.create (mutation blocked before resolver)")
         print(self._format_graphql_errors(body["errors"]))
         print(
            "\nMost likely fix (pick one):\n"
            "  A) Do NOT use Full Access. Administration → API Access → New API Key →\n"
            "     uncheck Full Access, select your sneeze-api group (must have write:pages),\n"
            "     restart Wiki.js, update WIKIJS_API_TOKEN, run --probe again.\n"
            "  B) If you must use Full Access, fix group 1 in the database (UI cannot edit it):\n"
            '     SELECT id, name, permissions FROM groups WHERE id = 1;\n'
            "     permissions must include manage:system (fresh installs only store that).\n"
            '     UPDATE groups SET permissions = \'["manage:system"]\' WHERE id = 1;\n'
            "     then restart Wiki.js.\n"
            "Then: python scripts/publish-wiki.py --all"
         )
         return 1

      create = (((body.get("data") or {}).get("pages") or {}).get("create")) or {}
      result = create.get("responseResult") or {}
      page = create.get("page") or {}
      if not result.get("succeeded"):
         code = result.get("errorCode")
         print(f"::error::WRITE FAILED — pages.create errorCode {code}: {result.get('message')}")
         if code in (6008, 6009):
            print(
               "\nAuth passed but page rules blocked the write.\n"
               "Groups → (token's group) → Page rules → Allow sneeze/ with write:pages"
            )
         return 1

      page_id = int(page["id"])
      print(f"  write: OK (created {PROBE_PATH} id={page_id})")

      del_body = self.graphql_raw(DELETE_MUTATION, {"id": page_id})
      if del_body.get("errors"):
         print(f"::warning::PROBE PAGE LEFT BEHIND — delete {PROBE_PATH} (id={page_id}) manually")
         print("  write access confirmed; safe to run --all")
         return 0
      del_result = ((((del_body.get("data") or {}).get("pages") or {}).get("delete") or {}).get("responseResult")) or {}
      if del_result.get("succeeded"):
         print("  cleanup: OK (probe page deleted)")
      else:
         print(f"::warning::PROBE PAGE LEFT BEHIND — delete {PROBE_PATH} (id={page_id}) manually")
      print("\nProbe passed. Publish all docs:\n  python scripts/publish-wiki.py --all")
      return 0


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
   parser.add_argument("--probe", action="store_true", help="Test API read + write; exit before bulk publish")
   parser.add_argument("--dry-run", action="store_true", help="Transform only; do not call the API")
   parser.add_argument("--all", action="store_true", help="Publish every docs/**/*.md page")
   parser.add_argument("--page", action="append", default=[], help="Publish one repo-relative docs path")
   parser.add_argument("--skip-render", action="store_true", help="Skip pages.render after upsert (needs manage:system)")
   parser.add_argument("--config", default="", help="Publish config JSON (default: docs/wiki/publish.json; test: docs/wiki/publish.rp1.json)")
   args = parser.parse_args()

   repo_root = find_repo_root(Path(__file__).resolve().parent)
   config_path = Path(args.config) if args.config else repo_root / "docs" / "wiki" / "publish.json"
   with config_path.open(encoding="utf-8") as f:
      config = json.load(f)

   docs_root = repo_root / config["docs_root"]
   if not docs_root.is_dir():
      print(f"::error::Docs root missing: {docs_root}", file=sys.stderr)
      return 2

   if not args.probe and not args.all and not args.page:
      parser.error("Specify --probe, --all, or at least one --page")

   graphql_url = os.environ.get("WIKIJS_GRAPHQL_URL", config.get("graphql_url", "")).strip()
   token = os.environ.get("WIKIJS_API_TOKEN", "").strip()
   locale = config.get("locale", "en")
   path_prefix = config.get("path_prefix", "sneeze").strip("/")

   if args.probe:
      if not graphql_url or not token:
         print("::error::Set WIKIJS_GRAPHQL_URL and WIKIJS_API_TOKEN", file=sys.stderr)
         return 2
      return WikiJsClient(graphql_url, token).probe(locale, path_prefix)

   pages: list[Path] = []
   if args.all:
      pages = list_doc_pages(docs_root)
   elif args.page:
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
   description = f"Synced from MetaversalCorp/Sneeze@{sha[:12]}"

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
   client.refresh_page_index()
   publish_order = order_publish_pages(pages, docs_root, config["home"])
   print(f"publish-wiki: publishing {len(publish_order)} page(s) to {graphql_url}")

   published_paths: list[str] = []
   for path in publish_order:
      wiki_path, title, markdown, rel = prepare_page(path, docs_root, config, path_map, sha)
      print(f"  upsert {wiki_path} <- {rel}")
      client.upsert(wiki_path, title, markdown, description, locale, ["sneeze", "docs"])
      published_paths.append(wiki_path)

   print(f"publish-wiki: re-rendering {len(published_paths)} page(s) to refresh internal links")
   if args.skip_render:
      print("  skipped (--skip-render)")
   else:
      render_ok = 0
      render_skip = False
      for wiki_path in published_paths:
         if render_skip:
            continue
         page_id = client.page_id_for_path(wiki_path, locale)
         if page_id is None:
            continue
         print(f"  render {wiki_path}")
         if client.render(page_id):
            render_ok += 1
         else:
            print(
               "::warning::pages.render requires manage:system; token has write:pages only. "
               "Skipping remaining renders — upserted content is live; Wiki.js will render on view."
            )
            render_skip = True
      if not render_skip:
         print(f"  rendered {render_ok} page(s)")

   print(f"publish-wiki: done ({len(published_paths)} page(s) upserted)")
   return 0


if __name__ == "__main__":
   sys.exit(main())
