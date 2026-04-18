// Headless test of the MsfViewer parser + signature verification logic.
// Loads the JS from index.html and runs it against tools/MsfViewer/sample.mss.
// This is a dev-only script -- not shipped with the viewer.

import { readFileSync } from "node:fs";
import { webcrypto } from "node:crypto";

// ---------------------------------------------------------------------------
// Inline copy of the viewer's core functions (mirrors index.html).
// Keep this in sync with the HTML's <script> block if you change the logic.
// ---------------------------------------------------------------------------

const crypto = webcrypto;

function base64UrlToBytes(s) {
  s = s.replace(/-/g, "+").replace(/_/g, "/");
  while (s.length % 4) s += "=";
  return Buffer.from(s, "base64");
}
function base64ToBytes(s) {
  return Buffer.from(s.replace(/\s+/g, ""), "base64");
}
function bytesToUtf8(b) { return new TextDecoder("utf-8", { fatal: false }).decode(b); }
function bytesToHex(b)  { return Array.from(b).map(x => x.toString(16).padStart(2, "0")).join(""); }
function utf8ToBytes(s) { return new TextEncoder().encode(s); }

function parseAsn1(bytes, offset) {
  const tag = bytes[offset];
  let p = offset + 1;
  let length;
  if (bytes[p] < 0x80) { length = bytes[p]; p += 1; }
  else {
    const n = bytes[p] & 0x7f;
    length = 0; p += 1;
    for (let i = 0; i < n; i++) length = (length << 8) | bytes[p + i];
    p += n;
  }
  const contentsOffset = p;
  const totalLength    = contentsOffset + length - offset;
  return {
    tag, length,
    contents: bytes.subarray(contentsOffset, contentsOffset + length),
    raw:      bytes.subarray(offset, offset + totalLength),
    totalLength,
  };
}

function walkSequence(bytes) {
  const out = [];
  let offset = 0;
  while (offset < bytes.length) {
    const el = parseAsn1(bytes, offset);
    out.push(el);
    offset += el.totalLength;
  }
  return out;
}

function parseX509(derBytes) {
  const cert = parseAsn1(derBytes, 0);
  const tlvs = walkSequence(cert.contents);
  const tbs  = tlvs[0];
  const tbsItems = walkSequence(tbs.contents);
  let idx = 0;
  if (tbsItems[idx].tag === 0xA0) idx++; // version
  idx++; // serial
  idx++; // sigAlg
  idx++; // issuer
  idx++; // validity
  idx++; // subject
  const spki = tbsItems[idx];
  return { spkiDer: spki.raw };
}

const JWS_ALG_MAP = {
  "RS256": { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" },
  "RS384": { name: "RSASSA-PKCS1-v1_5", hash: "SHA-384" },
  "RS512": { name: "RSASSA-PKCS1-v1_5", hash: "SHA-512" },
  "ES256": { name: "ECDSA", namedCurve: "P-256", hash: "SHA-256" },
};

async function verifyJwsSignature(alg, spkiDer, signedInputBytes, sigBytes) {
  const spec = JWS_ALG_MAP[alg];
  const importAlg = spec.name === "ECDSA"
    ? { name: "ECDSA", namedCurve: spec.namedCurve }
    : { name: spec.name, hash: spec.hash };
  const verifyAlg = spec.name === "ECDSA"
    ? { name: "ECDSA", hash: spec.hash }
    : { name: spec.name };
  const key = await crypto.subtle.importKey("spki", spkiDer, importAlg, false, ["verify"]);
  return crypto.subtle.verify(verifyAlg, key, sigBytes, signedInputBytes);
}

// ---------------------------------------------------------------------------
// Run
// ---------------------------------------------------------------------------

const inputPath = process.argv[2] || "tools/MsfViewer/sample.mss";
const jwsString = readFileSync(inputPath, "utf-8").trim();
console.log("Input:", inputPath);
const [h, p, s] = jwsString.split(".");

const header  = JSON.parse(bytesToUtf8(base64UrlToBytes(h)));
const payload = JSON.parse(bytesToUtf8(base64UrlToBytes(p)));
const sig     = base64UrlToBytes(s);

console.log("Algorithm:", header.alg);
console.log("x5c chain length:", header.x5c ? header.x5c.length : 0);

const leafDer = base64ToBytes(header.x5c[0]);
const leaf    = parseX509(leafDer);

const fp = bytesToHex(new Uint8Array(await crypto.subtle.digest("SHA-256", leaf.spkiDer)));
console.log("Leaf fingerprint:", fp);

const signedInput = utf8ToBytes(h + "." + p);
const ok = await verifyJwsSignature(header.alg, leaf.spkiDer, signedInput, sig);
console.log("Signature valid:", ok);

if (typeof payload.data === "string") {
  const inner = JSON.parse(payload.data);
  console.log("Payload namespace:", inner.namespace);
  console.log("Payload organization:", inner.organization);
  console.log("Payload services:", inner.services.length, "service(s)");
  console.log("Payload successor:", inner.successor);
}

process.exit(ok ? 0 : 1);
