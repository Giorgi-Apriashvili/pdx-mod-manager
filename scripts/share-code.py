#!/usr/bin/env python3
"""Independent tool for PMM share codes (docs/share-code-protocol.md).

Used for manual validation (docs/validation.md) and to cross-check protocol
fixtures with a second implementation. It is not part of the application and
uses only the Python standard library.

  share-code.py decode <code | file | ->     print the decoded manifest as JSON
  share-code.py encode <manifest.json | ->   print a PMM1 code for a manifest
  share-code.py compare <a> <b>              compare game, identities, order and
                                             enabled flags of two codes

`decode` checks the envelope, digest, Base64URL spelling, bounded inflation,
UTF-8 and duplicate JSON keys like the application, but not the manifest
schema, and prints the JSON as transmitted (not canonicalised: key order and
empty optional fields are kept). `encode` performs no validation, so it can
build deliberately invalid codes for negative tests; its compressed bytes may
differ from the application's (both decode to the same manifest).

Exit codes: 0 success / equal, 1 codes differ, 2 invalid input.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
import sys
import zlib
from pathlib import Path

MAX_CODE_BYTES = 2 * 1024 * 1024
MAX_JSON_BYTES = 8 * 1024 * 1024
ENVELOPE_RE = re.compile(r"PMM([1-9][0-9]*)\.")
DIGEST_RE = re.compile(r"[0-9a-f]{64}")
PAYLOAD_RE = re.compile(r"[A-Za-z0-9_-]+")


class CodeError(Exception):
    pass


def read_text(arg: str) -> str:
    if arg == "-":
        return sys.stdin.read()
    if not arg.lstrip().startswith("PMM"):
        path = Path(arg)
        if path.is_file():
            return path.read_text(encoding="utf-8")
    return arg


def reject_duplicates(pairs: list[tuple[str, object]]) -> dict:
    result: dict = {}
    for key, value in pairs:
        if key in result:
            raise CodeError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def decode(text: str) -> dict:
    if len(text.encode("utf-8")) > MAX_CODE_BYTES:
        raise CodeError("input exceeds 2 MiB")
    code = text.strip(" \t\r\n")
    envelope = ENVELOPE_RE.match(code)
    if not envelope:
        raise CodeError("not a share code (expected 'PMM1.')")
    if envelope.group(1) != "1":
        raise CodeError(f"unsupported protocol version PMM{envelope.group(1)}")
    parts = code.split(".")
    if len(parts) != 3:
        raise CodeError("expected exactly three '.'-separated parts")
    _, payload, digest = parts
    if not DIGEST_RE.fullmatch(digest):
        raise CodeError("digest is not 64 lowercase hex characters (truncated or damaged)")
    if not PAYLOAD_RE.fullmatch(payload) or len(payload) % 4 == 1:
        raise CodeError("payload is not unpadded Base64URL")
    compressed = base64.urlsafe_b64decode(payload + "=" * (-len(payload) % 4))
    if base64.urlsafe_b64encode(compressed).rstrip(b"=").decode("ascii") != payload:
        raise CodeError("payload is not canonical Base64URL (non-zero padding bits)")
    if hashlib.sha256(compressed).hexdigest() != digest:
        raise CodeError("digest mismatch (corrupted code)")
    if len(compressed) >= 2 and compressed[1] & 0x20:
        raise CodeError("zlib preset dictionaries are not allowed")
    inflater = zlib.decompressobj()
    try:
        data = inflater.decompress(compressed, MAX_JSON_BYTES + 1)
    except zlib.error as exc:
        raise CodeError(f"invalid zlib stream: {exc}") from exc
    if len(data) > MAX_JSON_BYTES or inflater.unconsumed_tail:
        raise CodeError("decompressed JSON exceeds 8 MiB")
    if not inflater.eof:
        raise CodeError("zlib stream is truncated")
    if inflater.unused_data:
        raise CodeError("bytes follow the zlib stream")
    try:
        json_text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise CodeError(f"payload is not valid UTF-8: {exc}") from exc
    if json_text.startswith("\ufeff"):
        raise CodeError("payload starts with a byte order mark")
    try:
        manifest = json.loads(json_text, object_pairs_hook=reject_duplicates)
    except json.JSONDecodeError as exc:
        raise CodeError(f"payload is not valid JSON: {exc}") from exc
    if not isinstance(manifest, dict):
        raise CodeError("manifest is not a JSON object")
    return manifest


def encode(manifest: object) -> str:
    data = json.dumps(manifest, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    compressed = zlib.compress(data, 9)
    payload = base64.urlsafe_b64encode(compressed).rstrip(b"=").decode("ascii")
    return f"PMM1.{payload}.{hashlib.sha256(compressed).hexdigest()}"


def entries(manifest: dict) -> list[tuple[object, object, object]]:
    mods = manifest.get("mods")
    if not isinstance(mods, list):
        raise CodeError("manifest has no 'mods' array")
    return [(m.get("source"), m.get("id"), m.get("enabled")) if isinstance(m, dict) else (None, None, None)
            for m in mods]


def compare(a: dict, b: dict) -> list[str]:
    differences = []
    if a.get("game") != b.get("game"):
        differences.append(f"game: {a.get('game')!r} != {b.get('game')!r}")
    ea, eb = entries(a), entries(b)
    if len(ea) != len(eb):
        differences.append(f"entry count: {len(ea)} != {len(eb)}")
    for index, (left, right) in enumerate(zip(ea, eb)):
        if left != right:
            differences.append(f"mods[{index}]: {left} != {right}")
    return differences


def main(argv: list[str]) -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("decode").add_argument("code")
    sub.add_parser("encode").add_argument("manifest")
    p_compare = sub.add_parser("compare")
    p_compare.add_argument("a")
    p_compare.add_argument("b")
    args = parser.parse_args(argv)

    try:
        if args.command == "decode":
            print(json.dumps(decode(read_text(args.code)), indent=2, ensure_ascii=False))
        elif args.command == "encode":
            source = sys.stdin.read() if args.manifest == "-" else Path(args.manifest).read_text(encoding="utf-8")
            print(encode(json.loads(source)))
        else:
            a, b = decode(read_text(args.a)), decode(read_text(args.b))
            differences = compare(a, b)
            for line in differences:
                print(line)
            for key in ("playsetName", "gameVersionHint"):
                if a.get(key) != b.get(key):
                    print(f"note: {key} differs ({a.get(key)!r} vs {b.get(key)!r}); not an identity field")
            if differences:
                return 1
            print(f"equal: game {a.get('game')!r}, {len(entries(a))} entries, same order and enabled flags")
    except (CodeError, OSError, json.JSONDecodeError) as exc:
        print(f"share-code: error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
