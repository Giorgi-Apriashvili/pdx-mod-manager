# Share-code protocol

A share code is a self-contained text string that carries a playset: which mods,
in which order, and which of them are enabled. Decoding needs no server. The
code contains references to mods (provider + provider id) and display hints.
It never contains mod files, credentials, executable commands, URLs to fetch or
machine-specific paths.

This document is normative for protocol version 1. The reference
implementation is `src/sharing/` (`share_code.hpp`, `manifest.hpp`,
`codec.hpp`); the frozen regression fixtures are in `tests/fixtures/sharing/v1/`.

Two independent version numbers exist:

| Version            | Where                              | Current | Changes when                              |
| ------------------ | ---------------------------------- | ------- | ----------------------------------------- |
| Envelope protocol  | `PMM<n>.` prefix                   | 1       | the text/compression/digest layer changes |
| Manifest schema    | `schemaVersion` field in the JSON  | 1       | the JSON structure or its meaning changes |

Both are versioned separately from the application (`PMM_SHARE_PROTOCOL_VERSION`
in the top-level `CMakeLists.txt`).

## 1. Envelope

```
code     = "PMM1." payload "." digest
payload  = 1*base64url-char            ; unpadded Base64URL (RFC 4648 section 5)
digest   = 64( %x30-39 / %x61-66 )     ; lowercase hex SHA-256
```

* **payload**: the manifest JSON (UTF-8) compressed as one zlib stream
  (RFC 1950, deflate, 32 KiB window, compression level 9, no preset
  dictionary), then encoded as Base64URL **without** `=` padding.
* **digest**: lowercase hex SHA-256 of the **compressed bytes** (not of the
  JSON and not of the Base64 text).

The digest only detects accidental corruption or truncation (for example a code
cut short when pasted into a chat). It is not a signature: anyone can create a
valid code, so a valid digest says nothing about who made it.

### Encoding

1. Validate the manifest (section 3). Invalid manifests are never encoded.
2. Serialize it canonically (section 4).
3. Compress with zlib level 9.
4. Emit `PMM1.` + Base64URL(compressed) + `.` + hex(SHA-256(compressed)).
5. Refuse (LimitExceeded) if the JSON exceeds 8 MiB or the code exceeds
   2 MiB, so the encoder never produces a code the decoder would reject.

The same manifest always produces the same code: there are no timestamps,
random salts or volatile fields. This holds for a given encoder build; zlib
versions may differ in their compressed output (see section 7).

### Decoding

Checks run in this order and decoding stops at the first failure. Nothing is
persisted and no download starts until a code has passed every check.

| #  | Check                                                                                       | Error code             |
| -- | ------------------------------------------------------------------------------------------- | ---------------------- |
| 1  | Raw input is at most 2 MiB (checked before anything else, including trimming)               | `LimitExceeded`        |
| 2  | Trim surrounding ASCII whitespace only: space, tab, CR, LF                                  | -                      |
| 3  | Starts with `PMM1.`; `PMM<n>.` with another decimal `n` (no leading zeros) is a different protocol version | `UnsupportedVersion` ("created by a newer version; update the app") |
| 4  | Anything else that does not start with `PMM1.`                                              | `ParseError` ("not a share code") |
| 5  | Exactly three `.`-separated parts                                                           | `ParseError`           |
| 6  | Digest is exactly 64 characters of `[0-9a-f]` (shorter, longer, uppercase or other characters mean truncation or damage) | `IntegrityCheckFailed` |
| 7  | Payload is non-empty, Base64URL alphabet only, no `=`, length mod 4 != 1, unused low bits of the last character are zero (exactly one accepted spelling per byte string) | `ParseError` |
| 8  | SHA-256 of the decoded bytes equals the digest                                              | `IntegrityCheckFailed` |
| 9  | Inflate as one zlib stream; abort as soon as output exceeds 8 MiB (enforced during inflation, not afterwards) | `LimitExceeded` |
| 10 | The stream ends (`Z_STREAM_END`), has no preset dictionary and no bytes follow it           | `ParseError`           |
| 11 | Well-formed UTF-8 (RFC 3629: no overlongs, surrogates or code points above U+10FFFF), no byte order mark | `ParseError` |
| 12 | Valid JSON (RFC 8259, no comments, no trailing commas, one value)                           | `ParseError`           |
| 13 | No duplicate keys in any object; at most 16 levels of nesting; at most 64 keys per object; arrays at most 10,000 elements | `ValidationFailed` / `LimitExceeded`; `UnsupportedVersion` for a limit when the root `schemaVersion` is not 1 (see below) |
| 14 | Manifest schema (section 3)                                                                 | see section 3          |

Whitespace *inside* a code is not accepted: it is a sign that the code was
reflowed or damaged, and silently removing it could hide truncation.

**Version before limits.** Checks 1 to 12 do not depend on the manifest
schema. The structural limits of check 13 (nesting depth, keys per object,
array elements) do, because a later schema may relax them (section 7). They
are enforced while parsing, before the document exists, so when one of them
fails the decoder still determines the root `schemaVersion`: either it was
already parsed, or the rest of the text is scanned for it without building
anything. If it is an integer other than 1, the result is the same
`UnsupportedVersion` that check 14 reports, with the limit named in the error
detail. Otherwise (no root `schemaVersion`, a non-integer value, or malformed
JSON before it) the limit error stands. Only the first root `schemaVersion`
counts; one inside a nested object is ignored. Duplicate keys found before a
limit fails are reported as `ValidationFailed` in every schema version.

## 2. File format

Clipboard and file transfer use the same text. A share-code file contains the
code, optionally followed by a line break, encoded as UTF-8 without a byte
order mark. Readers pass the file content to the same decoder, so surrounding
ASCII whitespace (including CRLF line endings) is accepted.

## 3. Manifest schema version 1

The payload is one JSON object. Key order and insignificant whitespace in the
input do not matter; the encoder always writes the canonical form.

```json
{
  "schemaVersion": 1,
  "game": "stellaris",
  "playsetName": "Example playset",
  "gameVersionHint": "v4.1.5",
  "mods": [
    {"source": "steam-workshop", "id": "1234567890", "nameHint": "Example enabled mod", "enabled": true},
    {"source": "steam-workshop", "id": "1234567891", "nameHint": "Example disabled mod", "enabled": false},
    {"source": "local", "id": "my local mod", "enabled": true, "versionHint": "1.2.3",
     "fingerprint": "sha256:83b7343b837fffe15a56fc365ec278ed80dcc33b7a34afa98acc45443686c80f"}
  ]
}
```

### Root object

| Field             | Type    | Required | Rules                                                                                       |
| ----------------- | ------- | -------- | ------------------------------------------------------------------------------------------- |
| `schemaVersion`   | integer | yes      | Must be `1`. Any other integer: `UnsupportedVersion`. Non-integer (including `1.0`, `"1"`): `ValidationFailed`. Checked before every other field, and takes precedence over the structural limits (section 1, "Version before limits"). |
| `game`            | string  | yes      | A key from the game catalog (`src/core/catalog.cpp`). Unknown key: `ValidationFailed`. Known game without an adapter in this release (e.g. `hoi4`): `UnsupportedGame`. Checked right after `schemaVersion`. |
| `playsetName`     | string  | yes      | 1 to 256 bytes of UTF-8, no control characters.                                             |
| `gameVersionHint` | string  | no       | At most 64 bytes, no control characters. Informational.                                     |
| `mods`            | array   | yes      | At most 10,000 entries (more: `LimitExceeded`). Array position is the authoritative order.  |

### Entry object

| Field         | Type    | Required | Rules                                                                                      |
| ------------- | ------- | -------- | ------------------------------------------------------------------------------------------ |
| `source`      | string  | yes      | Provider key valid for the game (`steam-workshop`, `local`). Otherwise `ValidationFailed`. |
| `id`          | string  | yes      | `steam-workshop`: 1 to 20 ASCII digits, no leading zero, value at most 18446744073709551615. `local`: 1 to 128 bytes of UTF-8, no control characters, no `/`, `\` or `:`, not `.` or `..`. Always a string, never a JSON number, so 64-bit ids keep their precision. |
| `nameHint`    | string  | no       | At most 256 bytes, no control characters. Display hint only.                               |
| `enabled`     | boolean | yes      | `true` or `false`; nothing else (not `1`, not `"true"`).                                   |
| `versionHint` | string  | no       | At most 64 bytes, no control characters. Informational.                                    |
| `fingerprint` | string  | no       | Exactly `sha256:` followed by 64 lowercase hex digits. Informational content fingerprint; compared for local mods. |

Further rules:

* **Strictness.** Unknown keys at any level, wrong types, missing required
  fields and duplicate keys are rejected (`ValidationFailed`). There is no
  "ignore what you do not understand" in version 1: a decoder that silently
  dropped an unknown field could lose information the sender considered
  essential.
* **Duplicate identities.** Two entries with the same `source` and `id` are
  rejected; the message names both positions (`mods[4]: duplicate of mods[1]`).
  The same id under different sources is two different mods.
* **Lengths** are counted in bytes of UTF-8, after JSON unescaping.
* **Control characters** are C0 (U+0000 to U+001F), DEL (U+007F) and C1
  (U+0080 to U+009F), whether written raw or as JSON escapes.
* **Empty optional strings** (`"nameHint": ""`) are accepted and mean the same
  as an omitted field. Encoders omit them.
* **Error messages** start with the JSON path of the offending field, for
  example `mods[3].id: a Steam Workshop id must be ...`. Technical context
  (byte offsets, digests, parser messages) is kept in the separate error
  detail.

### Semantics

* An export contains every entry of the playset, disabled ones included, in
  exact order. It does not contain the rest of the sender's installed mods.
* Index 0 is the first mod handed to the game; each game adapter maps that
  order to the game's precedence rules (see the adapters' documentation).
* Importing preserves the list and every enabled flag exactly. Extra mods
  installed on the recipient's machine stay installed but are not activated.
  Missing mods, including disabled ones, are acquired where supported; entries
  that cannot be resolved stay in the playset, visibly unresolved, and are
  never silently dropped, reordered or re-flagged.
* `local` entries are never downloaded automatically. The recipient maps them
  to an installed local mod or installs them manually; the fingerprint and
  name hint only help to find a candidate.
* Names and version hints are display information. They are never used as an
  identity and never interpreted as a path, URL or command. Provider ids are
  the identity; their game association is verified through the provider when
  possible.
* Identical references, ordering and enabled states do not guarantee identical
  bytes or multiplayer checksums: mods and games update independently. Version
  and fingerprint differences are shown when the information is available.

## 4. Canonical form

The encoder writes compact JSON (no whitespace outside strings) with this key
order, omitting optional fields whose value is empty:

* root: `schemaVersion`, `game`, `playsetName`, `gameVersionHint`, `mods`
* entry: `source`, `id`, `nameHint`, `enabled`, `versionHint`, `fingerprint`

Strings are written as raw UTF-8. Because control characters are rejected, the
only escapes that occur are `\"` and `\\`. `schemaVersion` is written as the
integer `1`; booleans as `true`/`false`.

## 5. Limits

| Limit                           | Value     | Enforced                                         | Can be raised compatibly by |
| ------------------------------- | --------- | ------------------------------------------------ | --------------------------- |
| Encoded input                   | 2 MiB     | on the raw, untrimmed input before any parsing   | nothing                     |
| Decompressed JSON               | 8 MiB     | during inflation; aborts at the first byte over  | a new envelope (`PMM2`)     |
| Entries                         | 10,000    | while parsing and in validation                  | `schemaVersion` 2           |
| Nesting depth / keys per object | 16 / 64   | while parsing, before the document is built      | `schemaVersion` 2           |
| Field lengths and id rules      | section 3 | in validation, after `schemaVersion`             | `schemaVersion` 2           |

A 10,000-entry playset with realistic names (about 1.3 MiB of JSON) encodes to
roughly 0.1 MiB, well within the input limit. The encoder refuses manifests whose code would exceed
2 MiB (possible only with near-maximal, incompressible fields).

A limit can only be raised by a version that old decoders check before they
enforce that limit; otherwise they report "too large" or "invalid" instead of
"update the app":

* The entry, nesting and key limits defer to a root `schemaVersion` other
  than 1 (section 1, "Version before limits"), and the field-level rules of
  section 3 are validated after `schemaVersion`, so `schemaVersion` 2 can
  raise any of them.
* The 8 MiB decompressed limit fires during inflation, before any JSON is
  read, so `schemaVersion` cannot help. Raising it needs `PMM2`: old decoders
  reject `PMM<n>.` at check 3, before inflation, with "update the app".
* The 2 MiB input limit is check 1, before even the envelope prefix. No
  version change can raise it compatibly: old decoders would call a larger
  code too large.

## 6. Security considerations

* Decoding is bounded in memory and time: input size, decompressed size,
  nesting depth, key count and entry count are capped before the work they
  bound is done. A decompression bomb is rejected after at most 8 MiB + 1 byte
  of output. When a structural limit fails before the root `schemaVersion` is
  known, the remaining text is only scanned, without building a document, so
  time and memory stay linear in the at most 8 MiB of JSON.
* The JSON parser runs iteratively (no recursion on attacker-controlled depth)
  and duplicate keys are rejected, so two decoders can never disagree about
  which of two values "wins".
* Codes are untrusted input. Nothing in a code selects an executable, a
  launch argument, a filesystem path or a download URL. Acquisition uses only
  the provider id with the provider's own client.
* The digest is not authentication. The import preview shows what a code
  contains before anything changes.

## 7. Compatibility policy

1. **Codes are forever.** Every future release decodes every valid `PMM1`
   code with `schemaVersion` 1 to the same manifest. The fixtures in
   `tests/fixtures/sharing/v1/` are committed byte-exact and must never be
   edited or regenerated; the tests decode them and re-encode them to the
   identical code. A change that breaks them is a protocol break.
2. **Strict readers, versioned writers.** A version 1 decoder rejects what it
   does not understand instead of guessing. Consequently every change that an
   existing decoder would reject or misread requires a new version:
   * a new or renamed field, a changed type or meaning, or relaxed field
     lengths, id rules, entry, nesting or key limits need `schemaVersion` 2
     (the envelope can stay `PMM1`);
   * a different compression, digest, text encoding or envelope layout, or a
     decompressed size above 8 MiB, needs `PMM2`;
   * the 2 MiB input limit can never be raised compatibly (section 5).
   Old applications then report "created by a newer version; update the app"
   rather than a generic error, because the envelope prefix is checked before
   inflation and `schemaVersion` takes precedence over every rule that
   depends on the schema, including the structural limits enforced while
   parsing (section 1, "Version before limits").
3. **Value-space growth without a version bump.** New catalog game keys and
   new provider keys do not change the schema. Older applications reject such
   codes with an actionable "unknown game/source; update the app" message, and
   a game known but not yet supported yields `UnsupportedGame`.
4. **Writers emit the oldest sufficient version.** An encoder keeps writing
   `PMM1` / schema 1 as long as the playset can be expressed in it, so codes
   stay readable by older releases.
5. **Decoders accept non-canonical input.** Any valid JSON formatting, key
   order or escaping is accepted (fixture `04-foreign-noncanonical` was
   produced by a different zlib implementation from indented, reordered JSON).
   Determinism is an encoder property: the same manifest yields the same code
   from the same encoder build. A future zlib upgrade may change the bytes of
   newly encoded codes; that is compatible (old codes still decode), but the
   golden re-encode test will flag it so the change is deliberate and noted in
   the changelog.
6. **Adding fixtures.** Write the canonical JSON to
   `tests/fixtures/sharing/v1/<nn>-<name>.json` (no trailing newline), add the
   name to `kGoldenFixtures` in `tests/unit/sharing/share_code_tests.cpp`, print
   its code with `pmm_sharing_tests "[.generate]"` and save it as `<nn>-<name>.code`
   (no trailing newline). Verify it with an independent implementation before
   committing; never regenerate existing fixtures.
