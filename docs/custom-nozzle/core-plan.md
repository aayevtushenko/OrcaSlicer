# Custom physical nozzle: core configuration and slicer semantics plan

## Purpose and architectural decision

Support a physical nozzle diameter that is not representable by Bambu firmware while preserving OrcaSlicer's physical model and all existing safety checks.

The central invariant is:

> `nozzle_diameter` remains the physical nozzle diameter and remains the only nozzle diameter used by slicing, flow, geometry, adaptive-layer calculations, extrusion-width validation, G-code extrusion calculations, and material-flow estimates.

Add two separate per-extruder machine-profile settings used only at Bambu compatibility/transport boundaries: `bambu_nozzle_diameter_override` enables the behavior and `bambu_nozzle_diameter` selects the firmware-compatible value. For the target A1 profile, the intended values are:

```text
nozzle_diameter = 1.57
bambu_nozzle_diameter_override = true
bambu_nozzle_diameter = 0.8
```

This deliberately does **not** suppress `Layer height cannot exceed nozzle diameter.` With the values above, that check compares layer height to `1.57`, as it should. It also does not teach the slicing core that the nozzle is `0.8` at any point.

## Requirements and invariants

1. Existing profiles and projects without the new key must slice, export, and send exactly as before.
2. `nozzle_diameter` must continue to drive all physical calculations, including automatic and percentage-based line widths.
3. `initial_layer_print_height` and `layer_height` must continue to be rejected when either exceeds the smallest applicable physical nozzle diameter.
4. The adaptive-layer default maximum must remain `0.75 * nozzle_diameter` when `max_layer_height == 0`.
5. The reporting override must be stored per extruder and resized with the extruder count.
6. The override must survive machine-preset save/reload and project 3MF save/reload. A project reopened without the custom machine preset installed must retain both diameters.
7. The override must affect only Bambu-facing compatibility checks and Bambu transport metadata. It must not affect generated toolpaths, extrusion volume, preview geometry, estimates, generic G-code, non-Bambu hosts, printer-profile selection, or physical-nozzle/filament compatibility logic.
8. A change to only the reporting override must not force reslicing. It may invalidate export/package generation so stale Bambu metadata cannot be reused.
9. No code should globally disable validation and no `no_check`-style bypass should be introduced for this feature.
10. When the override is enabled, only the firmware-supported values `{0.2, 0.4, 0.6, 0.8}` are valid. Core validation and the shared helper enforce this contract.

## Current source behavior that must be preserved

### Configuration and profile mechanics

- `PrintConfigDef::PrintConfigDef()` defines `max_layer_height`, `min_layer_height`, and `nozzle_diameter` in `OrcaSlicer/src/libslic3r/PrintConfig.cpp` (currently around lines 4739, 4835, and 4853).
- `PrintConfig` statically registers `ConfigOptionFloats nozzle_diameter` in `OrcaSlicer/src/libslic3r/PrintConfig.hpp` (currently around line 1568). `FullPrintConfig` derives from `PrintConfig`, so a new static member is required there as well.
- `PrintConfigDef::init_extruder_option_keys()` in `PrintConfig.cpp` identifies fields resized by `DynamicPrintConfig::set_num_extruders()`.
- The global `printer_extruder_options` set in `PrintConfig.cpp` participates in printer/extruder variant normalization and UI handling. A new per-extruder printer field must be included.
- `Preset::normalize()` in `OrcaSlicer/src/libslic3r/Preset.cpp` derives the extruder count from `nozzle_diameter` and calls `set_num_extruders()`. Once the new key is in `extruder_option_keys`, it gains the required vector length automatically.
- `Preset::save()` persists config diffs through `ConfigBase::save_to_json()`. A registered config key is automatically serialized; there should be no bespoke JSON serializer.
- `PrintConfigDef::handle_legacy()` and `handle_legacy_composite()` are the central load-migration hooks. This feature does not require conversion of any old key.

### Physical slicer semantics

- `Print::validate()` in `OrcaSlicer/src/libslic3r/Print.cpp` computes the minimum/maximum nozzle diameter from `m_config.nozzle_diameter`, validates line width against physical nozzle size, rejects first/normal layer heights above physical nozzle size, and constrains bridge width. These references must remain unchanged.
- `Slicing::max_layer_height_from_nozzle()` and its local static-config counterpart in `OrcaSlicer/src/libslic3r/Slicing.cpp` use `nozzle_diameter` for the `75%` adaptive-layer default. These references must remain unchanged.
- `Flow::auto_extrusion_width()`, `Flow::extrusion_width()`, and `Flow::new_from_config_width()` in `OrcaSlicer/src/libslic3r/Flow.cpp` use physical diameter for automatic widths, percentages, spacing, and flow. These references must remain unchanged.
- `OrcaSlicer/src/libslic3r/GCode.cpp`, `PrintRegion.cpp`, `PrintObject.cpp`, perimeter generation, supports, fills, calibration, wipe-tower generation, and other libslic3r consumers use `nozzle_diameter` as a physical value. None should be mechanically replaced with the reporting override.
- `Print::apply()`/configuration invalidation in `Print.cpp` currently treats `nozzle_diameter` as a full slice invalidation. The new reporting key has different invalidation semantics.
- `tests/fff_print/test_printobject.cpp` pins the rejection when layer height exceeds physical nozzle diameter. This test must remain and be extended, not weakened.

## Configuration schema

Add these definitions next to `nozzle_diameter` in `PrintConfigDef::PrintConfigDef()`:

```cpp
def = this->add("bambu_nozzle_diameter_override", coBools);
def->label = L("Override Bambu nozzle diameter");
def->tooltip = L("Report a firmware-supported nozzle diameter to a Bambu printer without changing slicing geometry.");
def->mode = comExpert;
def->set_default_value(new ConfigOptionBools { false });

def = this->add("bambu_nozzle_diameter", coFloats);
def->label = L("Bambu nozzle diameter");
def->tooltip = L("Nozzle diameter reported to a Bambu printer when the override is enabled. This does not change slicing geometry.");
def->sidetext = L("mm");
def->min = 0.2;
def->max = 0.8;
def->mode = comExpert;
def->set_default_value(new ConfigOptionFloats { 0.8 });
```

Use non-nullable `ConfigOptionBools` and `ConfigOptionFloats`. Nullable vectors in this codebase primarily encode inheritance/diff state. The explicit boolean keeps enablement independent of the selected compatibility value and makes disabled behavior unambiguous.

Add the corresponding member in `PrintConfig` in `PrintConfig.hpp`:

```cpp
((ConfigOptionBools,  bambu_nozzle_diameter_override))
((ConfigOptionFloats, bambu_nozzle_diameter))
```

Place it immediately after `nozzle_diameter` so the relationship is obvious.

Add both keys to:

- `PrintConfigDef::m_extruder_option_keys` via `init_extruder_option_keys()`;
- the global `printer_extruder_options` set.

This makes them printer-profile, per-extruder values and ensures that `DynamicPrintConfig::set_num_extruders()` and variant normalization resize them. Do not add them to filament or process variant sets.

### Central resolution helper

Define a narrowly named `CustomNozzle.hpp`/`CustomNozzle.cpp` helper so Bambu callers do not duplicate fallback or supported-value rules. The public contract should be equivalent to:

```cpp
bool is_supported_bambu_nozzle_diameter(double diameter);
bool bambu_nozzle_diameter_override_enabled(const ConfigOptionResolver &config, size_t extruder_id);
double resolved_bambu_nozzle_diameter(const ConfigOptionResolver &config, size_t extruder_id);
```

Behavior:

1. Read physical `nozzle_diameter[extruder_id]` using the existing `get_at()` semantics.
2. If `bambu_nozzle_diameter_override` is absent, empty, or false, return the physical diameter.
3. When enabled, require `bambu_nozzle_diameter` to be one of `{0.2, 0.4, 0.6, 0.8}` and return it.
4. Treat the override as enabled only when the boolean and supported, finite diameter both exist at the exact extruder index. Missing, misaligned, or invalid override data fails closed to the physical diameter; generic config validation still reports the malformed field.

If `ConfigOptionResolver` is inconvenient for the Bambu GUI call sites, supply overloads for `DynamicPrintConfig`/`PrintConfig` that delegate to one implementation. Do not add a second interpretation of the sentinel.

The helper's name must retain `bambu` and `reported` (or an equally explicit transport term). Avoid names such as `effective_nozzle_diameter`, which invite accidental use in geometry.

## Validation and invalidation changes

### Generic configuration validation

In `validate(const FullPrintConfig &cfg, bool under_cli)` in `PrintConfig.cpp`, validate each enabled `cfg.bambu_nozzle_diameter_override` entry against its `cfg.bambu_nozzle_diameter` entry:

- accept only `{0.2, 0.4, 0.6, 0.8}` when enabled;
- allow the stored selection to remain at its default while disabled;
- use the same helper predicate as the transport resolver so validation and runtime behavior cannot diverge.

No change is required in `Print::validate()` for the layer-height error. Its current use of `m_config.nozzle_diameter` is the desired safe behavior. A regression test must prove that setting a smaller reporting diameter neither causes nor suppresses this error.

### Incremental recomputation

Add both `bambu_nozzle_diameter_override` and `bambu_nozzle_diameter` to the `steps_gcode` set in `Print::apply()` rather than beside physical `nozzle_diameter`. This schedules export regeneration without invalidating object slicing. If the transport/package implementation can prove it never consumes cached `psGCodeExport` output, `steps_ignore` may be considered, but `psGCodeExport` is the conservative default because Bambu 3MF metadata is generated during export.

## Serialization, migration, and backward compatibility

### Machine presets

No explicit save code is required after registering the key. `Preset::save()` computes config diffs and `ConfigBase::save_to_json()` serializes registered vector values.

When a user profile inherits `override = false` and diameter `0.8`, normal diff saving may omit both fields. That is correct. An enabled override must persist the boolean; a non-default selected value must persist the diameter.

### Projects and 3MF

The normal project configuration serialization should carry the registered key in the full/project config. Add a round-trip test rather than assuming this. Bambu's separate plate/filament/nozzle metadata is a downstream transport concern; that metadata should contain the resolved reporting diameter, while Orca's project config must retain physical `nozzle_diameter = 1.57` and override `= 0.8`.

This distinction is essential: replacing `nozzle_diameter` in the project config or exported config snapshot would make reopening the project silently revert slicing to `0.8`.

### Old profiles and projects

Do not add a legacy migration that copies or rewrites `nozzle_diameter`.

- Missing new keys resolve to `override = false` and diameter `0.8`, so the helper returns the physical diameter at Bambu boundaries.
- A genuine old `0.8` nozzle profile remains physical/reporting `0.8`.
- An old workaround profile that lied by setting physical diameter to `0.8` cannot be distinguished from a genuine `0.8` profile. It must not be automatically converted to `1.57`; the user opts into the new physical value explicitly.
- Forward compatibility remains normal: older Orca versions will ignore/strip the unknown override keys according to their usual substitution rules, but retain `nozzle_diameter = 1.57`. They may then fail Bambu send compatibility, which is safer than silently slicing as `0.8`.

No application-version-gated migration is needed. `PrintConfigDef::handle_legacy()` should remain untouched unless an earlier experimental key is introduced before release and later renamed.

## Implementation steps by file/function

1. `OrcaSlicer/src/libslic3r/PrintConfig.cpp`
   - Add the option definition beside `nozzle_diameter`.
   - Add both keys to `PrintConfigDef::init_extruder_option_keys()`.
   - Add both keys to `printer_extruder_options`.
   - Validate enabled overrides with the shared supported-diameter predicate.
   - Extend `validate(const FullPrintConfig &, bool)` for invalid override values.
2. `OrcaSlicer/src/libslic3r/PrintConfig.hpp`
   - Add both static `PrintConfig` members.
3. `OrcaSlicer/src/libslic3r/CustomNozzle.hpp` and `CustomNozzle.cpp`
   - Define supported-value, activation-state, and resolved-diameter helpers with transport-only documentation.
4. `OrcaSlicer/src/libslic3r/Print.cpp`
   - Add both override keys to `steps_gcode`.
   - Make no changes to physical-nozzle checks in `Print::validate()`.
5. `OrcaSlicer/tests/libslic3r/test_config.cpp`
   - Add schema/default/vector-resize/validation tests.
6. `OrcaSlicer/tests/libslic3r/test_3mf.cpp` or the Bambu 3MF test area chosen by the export implementation
   - Add project-config round-trip coverage for the physical diameter, override state, and compatibility diameter.
7. `OrcaSlicer/tests/fff_print/test_printobject.cpp` and/or `test_flow.cpp`
   - Add physical-semantics regression coverage described below.

`Slicing.cpp`, `Flow.cpp`, and `GCode.cpp` are review/audit targets, not expected edit targets for the core change. Any proposed edit there requires explicit proof that it preserves the physical/reporting separation.

## Core test plan

### Schema, defaults, and vector behavior (`tests/libslic3r/test_config.cpp`)

1. `DynamicPrintConfig::full_print_config()` contains `ConfigOptionBools { false }` and `ConfigOptionFloats { 0.8 }` for the two new keys.
2. The resolver returns physical `0.4` when the override boolean is false.
3. The resolver returns `0.8` for physical `1.57`, override true, compatibility diameter `0.8`.
4. A partial/legacy dynamic config with physical diameter but no override keys resolves to physical diameter.
5. After `set_num_extruders(2)`, the physical, override, and compatibility-diameter vectors all have length two and predictable defaults.
6. Config validation rejects enabled unsupported values such as `0.5`, while runtime resolution fails closed to the physical diameter.
7. Disabled override and each supported value `{0.2, 0.4, 0.6, 0.8}` pass core validation.
8. JSON save/load of a machine-profile-style config preserves `1.57`, true, and `0.8` exactly.

### Physical behavior (`tests/fff_print/test_printobject.cpp`, `test_flow.cpp`)

1. Physical `1.57`, reported `0.8`, layer height `1.0` passes the nozzle-diameter layer check (subject to other valid widths/config values).
2. Physical `0.4`, reported `0.8`, layer height `0.5` still throws/rejects. This proves the override cannot weaken safety.
3. Physical `1.57`, reported `0.2`, layer height above `1.57` still rejects.
4. With automatic `line_width = 0`, physical `1.57`, reported `0.8`, verify the role width is derived from `1.57` (`1.57` for relevant support/top roles or `1.125 * 1.57` for perimeter/infill roles).
5. With a percentage width, verify the percentage base is `1.57`, not `0.8`.
6. Verify the adaptive max helper returns `0.75 * 1.57` when max is zero regardless of override state or compatibility diameter.
7. If practical, generate the same small model twice with identical physical settings and override disabled versus enabled at `0.8`, then compare physical toolpath/extrusion output after excluding intentionally changed metadata. Geometry and E values must match.

### Project round trip (`tests/libslic3r/test_3mf.cpp` or Bambu-format tests)

1. Store a project containing physical `1.57`, override true, and compatibility diameter `0.8`.
2. Reload it into a fresh `DynamicPrintConfig`.
3. Assert all three values are preserved independently.
4. Assert a project without the new key reloads with fallback behavior equivalent to physical diameter.

### Incremental invalidation

Add or extend a `Print::apply()` invalidation test if that API is already covered conveniently:

- changing physical `nozzle_diameter` invalidates slicing;
- changing only `bambu_nozzle_diameter_override` or `bambu_nozzle_diameter` invalidates export/package generation but not slicing/perimeters/infill.

## Dependency boundaries for parallel implementation

### Core configuration owner

Owns only:

- schema, static/dynamic config registration, defaults, resizing, validation, fallback resolver;
- preservation of the current physical semantics;
- core config/flow/slicing tests.

It must not edit Bambu send-dialog comparisons or Bambu package metadata. It exposes the resolver used by those layers.

### Printer-settings UI owner

Consumes the new key but does not reinterpret it. Responsibilities include visibility for Bambu machines, an explicit "Use printer-compatible reported size" control/dropdown, clear physical-versus-reported wording, allowed values sourced from current Bambu/device capabilities, and warnings. It should store `0` for disabled/inherit and a numeric value when enabled.

The UI must never rewrite `nozzle_diameter` when the compatibility value changes.

### Bambu device/send owner

Uses the shared resolver for comparisons to the connected printer. It must leave all generic/non-Bambu device paths on physical `nozzle_diameter`. It owns model/firmware-supported diameter validation and must fail clearly when the chosen reported value is not accepted by the target device.

### Bambu 3MF/transport metadata owner

Uses the resolved reported value only in Bambu-facing compatibility metadata fields. It must preserve the physical and override values in Orca project configuration. It owns tests that inspect the actual archive members/metadata, not merely the in-memory helper.

### Integration/review owner

Must audit every new reference to `bambu_nozzle_diameter_override` and `bambu_nozzle_diameter`. The expected references are limited to config/schema/tests, the narrowly named helper, UI, device compatibility, and Bambu metadata. A reference in `Flow`, `Slicing`, `PrintRegion`, `PrintObject`, perimeter/fill/support code, extrusion calculations, generic G-code generation, or estimates is a release blocker unless independently justified.

## Risks and mitigations

### Accidental semantic substitution

The largest risk is a broad replacement of `nozzle_diameter` with an "effective" diameter. Mitigate with an explicitly Bambu/transport-named helper, targeted code review, and physical-output regression tests.

### Project data loss

If export code mutates a shared config to `0.8` before serializing, reopening the 3MF will lose the true physical setup. Mitigate by resolving at individual Bambu metadata writes without mutating `PrintConfig`/`DynamicPrintConfig`, then round-trip-test the archive.

### Vector length mismatch

Partial profiles and multi-extruder normalization can produce transitional vector lengths. Mitigate by registering both options in both extruder-key collections, requiring exact-index override entries, falling back to physical diameter for misaligned data, and testing resize plus missing-key behavior.

### Stale cached package metadata

If override changes do not invalidate export, an old package may retain the former diameter. Conservatively classify it as `psGCodeExport` invalidation and test regeneration.

### Hard-coded Bambu list becoming stale

The set of accepted firmware values may change. Keep the current `{0.2, 0.4, 0.6, 0.8}` contract centralized in `CustomNozzle`; the Bambu device/UI layer may further restrict choices by connected-printer capabilities. Updating the global set then requires one predicate and its tests, not scattered comparisons.

### Misleading safety expectations

Reporting `0.8` to firmware does not make a custom `1.57` hotend mechanically or thermally safe. The UI/integration layer must warn about volumetric-flow capacity, collision/clearance, temperatures, heater power, and firmware assumptions. Core validation must continue to constrain geometry against the physical diameter but cannot certify hardware safety.

## Acceptance gate for the core component

The core component is complete only when all of the following are demonstrated:

- Existing config and FFF test suites pass.
- New schema/default/resizing/validation tests pass.
- Physical layer-height, line-width, flow, and adaptive-layer tests prove use of `nozzle_diameter` with a differing reporting override.
- Machine preset and project config round trips preserve the physical diameter, override state, and compatibility diameter.
- A source audit shows no reporting-diameter use in geometry/flow paths.
- Changing only the reporting override does not trigger a reslice but does regenerate the export/package boundary that consumes it.
- The downstream UI, send, and metadata owners can consume one documented resolver without duplicating fallback logic.
