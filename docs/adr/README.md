# Architecture decision records

Short records of material trade-offs: what was decided, why, and what it
costs. A record is never rewritten after it is accepted; a later decision
supersedes it with a new record that links back.

| # | Decision | Status |
| --- | --- | --- |
| [0001](0001-qt-quick-frontend.md) | Qt 6 Quick/QML frontend over Qt-free C++ core layers | Accepted |
| [0002](0002-dependencies.md) | Dependencies: FetchContent pinned by URL + SHA-256; Qt as a toolchain prerequisite | Accepted |
| [0003](0003-steam-workshop-helper.md) | Steam Workshop acquisition through a per-game helper process using the game's own Steam API library | Accepted |
| [0004](0004-share-code-format.md) | Share-code format `PMM1` (zlib + Base64URL + SHA-256), strict validation, independent versioning | Accepted |
| [0005](0005-journaled-config-writes.md) | Game configuration written through a journaled file transaction with backups and crash recovery | Accepted |
| [0006](0006-probe-mod-verification.md) | Load order verified in-game with observable probe mods | Accepted |
| [0007](0007-license.md) | MIT license; Qt used under LGPLv3 with dynamic linking | Accepted |
| [0008](0008-sqlite-migrations.md) | SQLite schema and migration policy | Accepted |

## Template

```markdown
# ADR NNNN: Title

- Status: Proposed | Accepted | Superseded by NNNN
- Date: YYYY-MM-DD

## Context
What forces the decision: requirements, constraints, evidence.

## Decision
What we do, stated so it can be checked in code review.

## Consequences
What becomes easier or harder, and what must be maintained.

## Alternatives considered
Each option and why it was not chosen.
```
