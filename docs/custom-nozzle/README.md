# Custom Bambu nozzle compatibility

## Objective

Build a launchable OrcaSlicer variant that can slice an A1 job using the true
physical nozzle diameter (initial target: 1.57 mm) while intentionally reporting
a firmware-supported diameter (initial target: 0.8 mm) at the Bambu device and
transport compatibility boundaries.

The feature must preserve Orca's physical slicing checks. It must not turn the
existing `Layer height cannot exceed nozzle diameter` validation into a global
warning or silently weaken it for ordinary profiles.

## Product behavior

- `nozzle_diameter` continues to represent the physical orifice used by slicing,
  flow math, automatic widths, adaptive-layer defaults, bridge checks, and
  calibration calculations.
- A new per-extruder `bambu_nozzle_diameter_override` flag defaults to false.
- A paired per-extruder `bambu_nozzle_diameter` value stores the reported
  compatibility identity and has no effect unless the flag is enabled.
- When disabled, all behavior and serialized output remain byte-for-byte or
  semantically equivalent to current Orca behavior.
- When enabled for a Bambu printer, the compatibility value is used only where a
  Bambu device or Bambu-consumed sliced-file metadata expects its enumerated
  nozzle identity.
- The physical diameter remains recoverable when a project is saved and reopened.
- The UI must state both values and require an explicit acknowledgment that the
  installed hardware and selected compatibility value differ.
- Unsupported or malformed compatibility values fail closed; the initial allowed
  set is 0.2, 0.4, 0.6, and 0.8 mm unless current protocol evidence requires a
  narrower set for a specific printer.

## Non-goals

- Changing A1 firmware.
- Disabling thermal, motion, volumetric-flow, collision, or extrusion safety
  controls.
- Claiming a 1.57 mm nozzle is mechanically safe without hardware validation.
- Changing behavior for non-Bambu printers.

## Implementation tracks

- [User guide](USER_GUIDE.md)
- [Core configuration and slicing plan](core-plan.md)
- [Bambu transport and UI plan](transport-ui-plan.md)
- [macOS build and verification plan](build-verification-plan.md)

## Acceptance gates

1. A default profile without the new option produces unchanged slicing and
   Bambu compatibility behavior.
2. A physical 1.57 mm profile can use a layer height above 0.8 mm but no higher
   than 1.57 mm without suppressing the existing validation.
3. The same profile can opt into reporting 0.8 mm to the connected A1 and pass
   Orca's local nozzle-diameter match check.
4. Bambu-consumed sliced metadata reports 0.8 mm while Orca project state retains
   1.57 mm.
5. Invalid compatibility values cannot be sent.
6. Targeted unit/integration tests pass, followed by the relevant full test suites.
7. A macOS `.app` is built, launched, and smoke-tested on this machine.

## Decision log

- 2026-07-02: Do not remove or downgrade the core layer-height guard. Separate
  physical slicing diameter from Bambu compatibility identity instead.
- 2026-07-02: Preserve the established meaning of `nozzle_diameter` to minimize
  slicing regressions and profile incompatibility.
- 2026-07-02: Use an explicit boolean plus value instead of a numeric sentinel.
  Malformed, missing, disabled, or unsupported overrides resolve fail-closed to
  the physical diameter and cannot silently bypass device matching.
