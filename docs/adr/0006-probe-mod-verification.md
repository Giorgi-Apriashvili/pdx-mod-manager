# ADR 0006: Load order verified in-game with observable probe mods

- Status: Accepted
- Date: 2026-10-29

## Context

The spec requires validating launcher bypass and effective mod ordering in the
running game: a process starting is not evidence that mods loaded (section 7).
Community sources disagree about which end of the Paradox load order wins for
identical file paths ([feasibility](../feasibility.md) §3). The only consistent
observation is that Stellaris reads `dlc_load.json` `enabled_mods` in array
order. Precedence must be measured per game, not assumed, and the measurement
must not depend on gameplay content or third-party mods.

## Decision

- `tools/probe-mods/probe_mods.py` creates small probe mods (default `a`, `b`,
  `c`) in a game's user mod folder. Each contains:
  - `common/scripted_triggers/pmm_probe_only_<X>.txt` with a deliberately
    unknown trigger; its error in `logs/error.log` proves probe X was loaded;
  - `common/scripted_triggers/pmm_probe_shared.txt`, the same relative path in
    every probe; only the winning copy is parsed, so the trigger name in the
    error reveals which mod won the override.
- `probe_mods.py analyze <error.log> --enabled … --disabled …` reports, as
  JSON, which probes loaded, whether any disabled probe loaded, and whether the
  first or the last enabled probe won (`first-enabled-wins` /
  `last-enabled-wins`).
- Each adapter's `LoadOrderInfo` reports `verifiedInGame = true` only with
  recorded evidence. The manual procedure is in
  [`docs/validation.md`](../validation.md).

## Consequences

- Effective order and enabled state are verified from the game's own output,
  not from what the manager wrote.
- The probes contain no gameplay content and are safe to leave disabled; the
  tool removes them with `probe_mods.py remove`.
- The method relies on the game logging unknown scripted triggers with their
  file in `error.log`. That is expected for Clausewitz games but must be
  confirmed per game and version; EU4 is unverified.
- Verification is manual and needs a real installation; CI cannot run it.
  Launch-plan tests in CI verify the generated order instead.

## Alternatives considered

- **Trust the written `dlc_load.json`**: shows intent, not effect.
- **Inspect the game's in-game mod list or screenshots**: depends on UI that
  differs per game and does not reveal file-level precedence.
- **Use popular Workshop mods with known overrides**: results change when the
  mods update and require subscriptions.
- **Read the game's memory or hook it**: invasive, fragile, and conflicts with
  the rule never to patch game binaries.
