# ADR 0004: Share-code format PMM1

- Status: Accepted
- Date: 2026-10-29

## Context

Players share a playset (mod identities, exact order, enabled flags) as a
copyable text that another installation decodes without any server (spec
section 5). Codes are untrusted input, are pasted into chats that may truncate
or reflow them, and must stay readable by every future release.

## Decision

- Envelope `PMM1.<payload>.<digest>`: the payload is the canonical manifest
  JSON (UTF-8), compressed as one zlib stream (level 9), encoded as unpadded
  Base64URL; the digest is the lowercase hex SHA-256 of the compressed bytes.
  The digest detects corruption and truncation only; it is not authentication.
- The manifest carries `schemaVersion` (1), `game`, `playsetName`, an optional
  `gameVersionHint`, and `mods` in authoritative order, each with `source`,
  `id` (always a string), `enabled`, and optional display/informational hints.
  No paths, URLs, commands or credentials can be expressed.
- Decoding is strict and bounded: 2 MiB of input, 8 MiB of inflated JSON
  (enforced during inflation), 10,000 entries, 16 nesting levels, 64 keys per
  object; no duplicate or unknown keys, exact types, canonical Base64URL,
  checks in a fixed order, nothing persisted before every check passes.
- Encoding is canonical and deterministic for a given encoder build (fixed key
  order, compact JSON, empty optional fields omitted, no timestamps).
- Two version numbers, both independent of the application version: the
  envelope (`PMM<n>`) and the manifest `schemaVersion`. Readers reject versions
  they do not know with "created by a newer version; update the app". Writers
  emit the oldest version that can express the playset.
- Frozen regression fixtures (`tests/fixtures/sharing/v1/*.code` + `.json`)
  are committed byte-exact and must never be edited or regenerated.

The normative specification is [`docs/share-code-protocol.md`](../share-code-protocol.md).

## Consequences

- Codes are self-contained and typically small (a 10,000-entry playset with
  realistic names encodes to about 0.1 MiB).
- Strictness means any new field or relaxed limit requires `schemaVersion` 2,
  and any change to compression, digest or text layout requires `PMM2`. This is
  deliberate: a lenient reader could silently drop information.
- A future zlib upgrade may change the bytes of newly encoded codes. Old codes
  still decode; the golden re-encode test flags the change so it is deliberate
  and noted in the changelog.
- `scripts/share-code.py` provides an independent decoder for validation and
  for cross-checking new fixtures.

## Alternatives considered

- **Plain JSON or Base64-encoded JSON without compression**: long codes that
  are awkward to paste.
- **A binary format (CBOR, protobuf)**: smaller, but harder to inspect and
  needs another dependency; JSON plus zlib is already compact enough.
- **Hosted short links**: violate the no-server requirement.
- **CRC32 instead of SHA-256**: weaker detection for no practical gain;
  SHA-256 is already needed for local mod fingerprints.
- **Signatures**: would need key distribution and accounts, out of scope; the
  import preview shows exactly what a code contains instead.
- **Lenient parsing ("ignore unknown fields")**: easier evolution, but two
  decoders could then disagree about what a code means.
