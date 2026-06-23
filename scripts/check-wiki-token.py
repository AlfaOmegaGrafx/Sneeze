#!/usr/bin/env python3
# Copyright 2026 Metaversal Corporation. All rights reserved.
#
# Validate a Wiki.js API token: JWT shape/expiry, server acceptance, read/write scope.
#
# Usage:
#   set WIKIJS_API_TOKEN=...
#   python scripts/check-wiki-token.py

from __future__ import annotations

import argparse
import base64
import json
import os
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

LIST_QUERY = """
query TokenList {
   pages {
      list {
         id
         path
      }
   }
}
"""

CREATE_QUERY = """
mutation TokenWriteProbe {
   pages {
      create(
         path: "sneeze/_token_probe"
         title: "token probe"
         content: "delete me"
         description: ""
         editor: "markdown"
         isPublished: false
         isPrivate: true
         locale: "en"
         tags: []
      ) {
         responseResult { succeeded message errorCode }
      }
   }
}
"""


def decode_jwt_payload(token: str) -> tuple[dict, str | None]:
   parts = token.strip().split(".")
   if len(parts) != 3:
      return {}, "JWT must have 3 dot-separated segments"
   segment = parts[1] + "=" * (-len(parts[1]) % 4)
   try:
      return json.loads(base64.urlsafe_b64decode(segment)), None
   except (json.JSONDecodeError, ValueError) as exc:
      return {}, f"cannot decode payload: {exc}"


def graphql_post(url: str, token: str | None, query: str) -> tuple[int, dict | str]:
   headers = {"Content-Type": "application/json", "Accept": "application/json"}
   if token:
      headers["Authorization"] = f"Bearer {token}"
   payload = json.dumps({"query": query}).encode("utf-8")
   req = urllib.request.Request(url, data=payload, method="POST", headers=headers)
   try:
      with urllib.request.urlopen(req, timeout=120) as resp:
         return resp.status, json.loads(resp.read().decode("utf-8"))
   except urllib.error.HTTPError as exc:
      body = exc.read().decode("utf-8", errors="replace")
      try:
         return exc.code, json.loads(body)
      except json.JSONDecodeError:
         return exc.code, body


def error_messages(body: dict) -> list[str]:
   return [str(e.get("message", e)) for e in body.get("errors") or []]


def body_text(body: dict | str) -> str:
   return body if isinstance(body, str) else json.dumps(body)


def is_revoked_response(body: dict | str) -> bool:
   text = body_text(body).lower()
   return "api key is invalid" in text or "was revoked" in text


def main() -> int:
   parser = argparse.ArgumentParser(description="Validate Wiki.js API token")
   parser.add_argument("--url", default="", help="GraphQL URL override (default: env or config file)")
   parser.add_argument("--token", default="", help="API token (default: WIKIJS_API_TOKEN)")
   args = parser.parse_args()

   token = (args.token or os.environ.get("WIKIJS_API_TOKEN", "")).strip()
   if not token:
      print("::error::Set WIKIJS_API_TOKEN or pass --token", file=sys.stderr)
      return 2

   url = args.url.strip() or os.environ.get("WIKIJS_GRAPHQL_URL", "").strip()
   if not url:
      config_arg = os.environ.get("WIKIJS_PUBLISH_CONFIG", "").strip()
      if config_arg:
         cfg_path = Path(config_arg)
         if not cfg_path.is_file():
            cfg_path = Path(__file__).resolve().parent.parent / config_arg
         if cfg_path.is_file():
            url = json.loads(cfg_path.read_text(encoding="utf-8")).get("graphql_url", "")
      if not url:
         publish_json = Path(__file__).resolve().parent.parent / "docs" / "wiki" / "publish.json"
         if publish_json.is_file():
            url = json.loads(publish_json.read_text(encoding="utf-8")).get("graphql_url", "")
   if not url:
      print("::error::Set WIKIJS_GRAPHQL_URL, --url, or docs/wiki/publish.json graphql_url", file=sys.stderr)
      return 2

   print(f"check-wiki-token: {url}")
   print(f"  token length: {len(token)} chars")

   payload, decode_err = decode_jwt_payload(token)
   if decode_err:
      print(f"::error::JWT decode: {decode_err}")
      return 1

   api_id = payload.get("api")
   grp_id = payload.get("grp")
   exp = payload.get("exp")
   iat = payload.get("iat")

   print("  jwt payload:")
   print(f"    api (key id): {api_id}")
   print(f"    grp (group):  {grp_id}  ← server loads permissions from this group only")
   print(f"    aud:          {payload.get('aud')}")
   print(f"    iss:          {payload.get('iss')}")

   if not api_id or grp_id is None:
      print("::error::Missing api or grp — not a Wiki.js API key JWT")
      return 1

   now = int(time.time())
   if exp is not None:
      exp_dt = datetime.fromtimestamp(int(exp), tz=timezone.utc)
      if int(exp) <= now:
         print(f"::error::JWT expired at {exp_dt.isoformat()}")
         return 1
      print(f"    exp:          {exp_dt.isoformat()} (not expired)")

   if iat is not None:
      iat_dt = datetime.fromtimestamp(int(iat), tz=timezone.utc)
      print(f"    iat:          {iat_dt.isoformat()}")

   # Revoked / unknown keys fail the whole HTTP request with an explicit message.
   print("\n  server acceptance:")
   status, list_body = graphql_post(url, token, LIST_QUERY)
   if isinstance(list_body, str):
      print(f"    HTTP {status} — {list_body[:300]}")
      return 1
   if is_revoked_response(list_body):
      print("    REJECTED — API key id not in Wiki.js validApiKeys (revoked, wrong server, or restart needed)")
      print(f"    detail: {body_text(list_body)[:400]}")
      return 1
   if list_body.get("errors"):
      print(f"    list errors: {error_messages(list_body)}")
      return 1

   pages = (((list_body.get("data") or {}).get("pages") or {}).get("list")) or []
   sneeze = [p for p in pages if str(p.get("path", "")).startswith("sneeze")]
   print(f"    ACCEPTED — pages.list OK ({len(pages)} visible, {len(sneeze)} under sneeze/)")
   print("    (users.profile is not a reliable API-key test — Wiki.js maps API tokens to user id 1)")

   print("\n  write check (pages.create):")
   _, create_body = graphql_post(url, token, CREATE_QUERY)
   if isinstance(create_body, str):
      print(f"    HTTP error — {create_body[:300]}")
      return 1

   if create_body.get("errors"):
      errs = error_messages(create_body)
      print(f"    FAILED @auth — {errs}")
      print(
         f"\nToken is valid. API key {api_id} is accepted; effective permissions come from group {grp_id}.\n"
         "pages.create requires write:pages, manage:pages, or manage:system in that group's\n"
         "permissions array as loaded by Wiki.js (WIKI.auth.groups), not what the UI appears to show.\n"
         "\nOn the Wiki.js database host, run:\n"
         f"  SELECT id, name, permissions, pageRules FROM groups WHERE id = {grp_id};\n"
         f"  SELECT id, name, isRevoked, expiration FROM apiKeys WHERE id = {api_id};\n"
         "\npermissions must include write:pages (or manage:system). If the row looks correct,\n"
         "restart Wiki.js to reload groups/apiKeys cache, then re-run this script.\n"
         "\nIf group permissions are wrong, fix in Administration → Groups → (group id "
         f"{grp_id}), or update the DB row directly."
      )
      return 1

   result = ((((create_body.get("data") or {}).get("pages") or {}).get("create") or {}).get("responseResult")) or {}
   if result.get("succeeded"):
      print("    OK — write permission confirmed")
      print("\nRun: python scripts/publish-wiki.py --all")
      return 0

   print(f"    FAILED — errorCode {result.get('errorCode')}: {result.get('message')}")
   if result.get("errorCode") in (6008, 6009):
      print("Auth passed; page rules blocked the path. Add ALLOW rule for sneeze/ with write:pages on this group.")
   return 1


if __name__ == "__main__":
   sys.exit(main())
