# Implementation status

Last updated: 2026-07-02

## Approved contract

- Physical slicing diameter: existing `nozzle_diameter`.
- Explicit per-extruder opt-in: `bambu_nozzle_diameter_override` (bool,
  default false).
- Per-extruder Bambu identity: `bambu_nozzle_diameter` (float, inert while
  opt-in is false).
- Initial accepted Bambu identities: 0.2, 0.4, 0.6, and 0.8 mm.
- One shared resolver owns fallback and validation semantics.
- Ordinary project save/export remains physically truthful.
- Only an explicit outgoing Bambu transport projection aliases identity fields.

## Work streams

| Stream | Owner | State | Files/notes |
|---|---|---|---|
| Core config, resolver, physical regression tests | `core_plan` agent | Implemented; tests pass | PrintConfig, CustomNozzle, Print invalidation, config/FFF tests |
| Bambu 3MF transport projection | `transport_plan` agent | Implemented; tests pass | bbs_3mf, explicit export mode, archive tests |
| Printer UI and device matching | `build_plan` agent | Implemented; compiled | Tab, SelectMachine, PrePrintChecker, SyncAms |
| Integration review, Plater sync audit | Main agent | Source review complete | Generic PrintHost kept physical; Bambu callers opt in explicitly; project/device sync uses reported identity only for valid active overrides |
| Toolchain/dependencies/build | Main agent | Complete | Native arm64 Release app built at `build/arm64/OrcaSlicer/OrcaSlicer.app` |
| Runtime/A1 verification | Main agent + user hardware | Local app launched; hardware pending | App opened with isolated `/tmp/orcaslicer-custom-nozzle-smoke`; attended printer test remains |

## Review gates

- No reporting-diameter reference is allowed in physical geometry or flow code.
- No duplicate resolver or fallback policy is allowed.
- Override-off paths must preserve upstream behavior.
- Override-on device mismatch must still block Send.
- Active matching override must be visible and non-silent.
- Physical and reported diameters must both survive project round trips.
- Exact outgoing transport archive must be inspected in tests.

## Current verification evidence

- `git diff --check` passes.
- Source audit finds no compatibility-diameter reads in Flow, Slicing, GCode,
  PrintRegion, or PrintObject geometry paths.
- Core layer-height validation remains non-suppressible by this feature.
- Automated Bambu PA/flow calibration now rejects active compatibility mode.
- Tests exist for schema/defaults, malformed fail-closed behavior, physical
  flow/layer semantics, export-only invalidation, JSON round trip, Bambu
  metadata projection, override-off equivalence, two-extruder mapping,
  provenance recovery, and load-save-load preservation.
- Focused `[CustomNozzle]` tests pass: 65 assertions across 3 cases.
- Focused `[custom_nozzle]` archive/provenance tests pass: 129 assertions
  across 2 cases.
- Full `libslic3r_tests` passes: 48,783 assertions across 129 cases.
- Full `fff_print_tests` passes: 580 assertions across 41 cases.
- Combined full-suite result: 49,363 assertions across 170 cases, zero
  failures.
- Native arm64 Release app packaging passes (`OrcaSlicer 2.5.0-dev`, 365 MB).
- macOS accepted the app launch with an isolated data directory. The local
  development bundle is ad-hoc/unsigned and is not suitable for distribution
  without a final signing/notarization pass.
- Live A1 transport acceptance remains an attended hardware verification step.
