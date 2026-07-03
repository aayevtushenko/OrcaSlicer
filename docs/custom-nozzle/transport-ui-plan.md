# Custom physical nozzle: Bambu transport and UI implementation plan

## Scope and invariant

This plan covers the Bambu-facing half of the custom-nozzle feature: device matching, print/send UI, temporary 3MF metadata, project round trips, and the relevant tests. It assumes the core configuration work will add an explicit per-extruder opt-in and reported diameter. The names used below are the recommended contract:

- `bambu_nozzle_diameter_override` (`ConfigOptionBools`, default `false`)
- `bambu_nozzle_diameter` (`ConfigOptionFloats`, ignored unless the corresponding override is true)

If the core implementation chooses different names, substitute them consistently. The critical invariant is independent of naming:

> `nozzle_diameter` remains the physical nozzle diameter everywhere that computes geometry, extrusion, validation, preview, G-code, or calibration. The Bambu compatibility diameter is read only at an explicitly enumerated device/transport boundary.

For a 1.57 mm physical nozzle reported as 0.8 mm, slicing, layer-height validation, flow, widths, adaptive layers, G-code comments, and saved printer presets continue to say 1.57 mm. Only the device match and selected Bambu-consumed fields in a temporary outgoing job say 0.8 mm. With the override disabled, behavior and byte-level field values remain unchanged from upstream.

## Current data flow and exact seams

1. The edited printer preset owns physical `nozzle_diameter`. It is defined in `src/libslic3r/PrintConfig.cpp` in `PrintConfigDef::PrintConfigDef()` near the existing `this->add("nozzle_diameter", coFloats)` definition. `TabPrinter::build_unregular_pages()` in `src/slic3r/GUI/Tab.cpp` exposes it on each Extruder page under **Basic information**.
2. The connected device diameter comes from `DevExtder::GetNozzleDiameter()` and `DevExtderSystem::GetNozzleDiameter()` in `src/slic3r/GUI/DeviceCore/DevExtruderSystem.{h,cpp}`. The value is parsed into `DevNozzleSystem` from device JSON by the parsers reached from `DeviceManager.cpp`. This is authoritative device state and must not be rewritten or faked in memory.
3. `SelectMachineDialog::_is_same_nozzle_diameters()` in `src/slic3r/GUI/SelectMachine.cpp` compares the device value directly with the edited preset's physical `nozzle_diameter`. `SelectMachineDialog::update_print_status()` turns a mismatch into blocking `PrintStatusNozzleDiameterMismatch` and disables Send through `show_status()`.
4. Other direct device/preset comparisons exist in `Plater.cpp`: project-load synchronization near `preset_nozzle_diameter`/`nozzle_mismatch`, multi-extruder preset synchronization near `same_nozzle_diameter`, and `get_printer_preset()`. These need a conscious decision; they must not accidentally switch a custom 1.57 preset back to a stock 0.8 preset.
5. Multi-nozzle task JSON is produced by `SelectMachineDialog::build_nozzles_info()` and the parallel `SyncAmsInfoDialog::build_nozzles_info()`. Their `diameter` members currently come directly from physical `nozzle_diameter`.
6. `Plater::export_3mf()` gets `full_config_secure()`, constructs `PlateData`, and calls `store_bbs_3mf()`. Both ordinary project saves and temporary send artifacts use this same function. `Plater::send_gcode()` and `Plater::export_config_3mf()` are the normal temporary-job entry points; `SendToPrinter`, `SelectMachineDialog`, and `SendMultiMachinePage` call them.
7. `_BBS_3MF_Exporter::_add_slice_info_config_file_to_archive()` in `src/libslic3r/Format/bbs_3mf.cpp` currently emits the physical diameter into all three known slice-info locations:
   - plate metadata `key="nozzle_diameters"`;
   - each `<filament nozzle_diameter="...">`;
   - each `<nozzle nozzle_diameter="...">`.
8. `PlateBBoxData::nozzle_diameter` in `src/libslic3r/GCode/ThumbnailData.hpp` is serialized by `_BBS_3MF_Exporter::_add_bbox_file_to_archive()` to `Metadata/plate_N.json`. It is another possible firmware-consumed diameter and must be included in the compatibility experiment, not silently overlooked.
9. The slice-info importer reads `nozzle_diameters` into `PlateData::nozzle_diameters` and filament values into `FilamentInfo::nozzle_diameter` in `_BBS_3MF_Importer::_handle_start_config_metadata()` and `_handle_start_config_filament()`. Embedded `Metadata/machine_settings_N.config` presets and the full print config are imported separately and already provide the correct place to preserve physical settings.

## Phase 1: a single policy helper

Add a small, GUI-independent helper beside the core config definitions (recommended `src/libslic3r/CustomNozzle.hpp/.cpp`, added to `src/libslic3r/CMakeLists.txt`). Avoid scattering `if (override)` logic across UI and exporter code.

Recommended API:

```cpp
struct BambuNozzleDiameter {
    double physical;
    double reported;
    bool override_active;
};

BambuNozzleDiameter get_bambu_nozzle_diameter(
    const DynamicPrintConfig &config, size_t extruder_index);

std::vector<double> get_bambu_reported_nozzle_diameters(
    const DynamicPrintConfig &config);
```

Contract:

- `physical` always comes from `nozzle_diameter[index]` using the same fallback/index semantics as existing vector options.
- `override_active` is true only when the bool exists and is true, a corresponding reported value exists, and that value is finite and positive.
- `reported == physical` whenever the feature is absent, disabled, malformed, or vector lengths do not align. This is the backward-compatibility fail-safe.
- Validation performed by the core config layer should reject enabled values other than Bambu-supported device identities (initially 0.2, 0.4, 0.6, 0.8, compared with epsilon), while the helper still fails closed to physical values for defensive loading of old/malformed files.
- The helper must not infer activation merely because physical and reported values differ. Activation is explicit.
- Do not put this behavior into `DevExtderSystem::NozzleDiameterMatchesOrUnknown()`: that class represents real device state and is used outside the send compatibility use case.

Unit-test the helper independently in `tests/libslic3r/test_config.cpp` or a new `test_custom_nozzle.cpp` registered in `tests/libslic3r/CMakeLists.txt`: missing options, off/default, enabled valid alias, equal physical/reported, invalid/NaN/zero, vector mismatch, and two extruders.

## Phase 2: printer-settings UI

Modify `TabPrinter::build_unregular_pages()` in `src/slic3r/GUI/Tab.cpp`. On each Extruder page, add a separate **Bambu printer compatibility** option group after **Basic information** (or after **Layer height limits**) containing:

- checkbox: **Report a different nozzle diameter to Bambu printer**;
- value/select: **Bambu-reported nozzle diameter**, choices 0.2, 0.4, 0.6, 0.8 mm;
- read-only explanatory text/tooltip: “Slicing still uses the physical nozzle diameter above. This value is used only for Bambu device matching and outgoing job compatibility metadata.”

UI behavior:

- Hide the entire group unless the selected preset is a Bambu-family preset (`Preset::is_bbl_vendor()` or the existing BBL-printer predicate used in `TabPrinter`). Do not expose a setting that has no effect on generic hosts.
- Default the checkbox off for all existing and new presets. Do not add it enabled to stock profile JSON.
- Disable the reported-diameter control while the checkbox is off. When first enabled, initialize it to the closest supported Bambu diameter, preferring the current device-reported value when a connected matching printer is available, otherwise 0.8 for a physical diameter above 0.8 (and nearest supported value for other sizes). The stored value alone never activates the override.
- Display both values in the control text, e.g. **Physical 1.57 mm → reports 0.8 mm**.
- On enabling, show a one-time modal confirmation for that preset edit: “This bypasses Bambu's nozzle-diameter compatibility check. Orca cannot verify custom hardware, heater capacity, extrusion limits, or firmware behavior.” Require an affirmative action; Cancel restores the checkbox to off.
- Use the existing `optgroup->m_on_change`/`load_config()` pattern. Ensure multi-extruder vector resizing tracks `extruders_count_changed()` in the same way as `nozzle_diameter`; each extruder may opt in separately. Do not copy one extruder's alias to the other unless single-extruder-multi-material semantics explicitly require all physical nozzles to be identical.
- `TabPrinter::toggle_options()` should keep the value disabled when the checkbox is false and update visibility after preset/vendor changes. `on_preset_loaded()` and `reload_config()` should refresh the summary.
- Add searchable documentation anchors/tooltips, but keep the setting Advanced/Expert rather than Simple.

Sidebar diameter selectors in `Plater.cpp` continue to display and switch on the physical diameter. They select slicing presets, not firmware identities. Optionally add a compact warning badge beside the nozzle title when the override is active, but never replace “1.57 mm” with “0.8 mm” there.

## Phase 3: device matching and pre-print warning

Refactor `_is_same_nozzle_diameters()` in `src/slic3r/GUI/SelectMachine.cpp` to accept the relevant config (or call the policy helper on the edited preset config) and return enough structured information for messaging: extruder id, physical diameter, reported diameter, device diameter, and whether the override was active. Compare device state with `reported`, using epsilon rather than raw `!=`.

Matching rules per used physical extruder:

1. Unknown device diameter (`0.0`) retains current “assume matching” behavior.
2. Override off: compare device against physical, preserving current blocking mismatch behavior.
3. Override on: compare device against reported. A mismatch remains blocking; the override is not a blanket “ignore mismatch” switch.
4. Override on and device matches reported: allow Send, but always add a persistent printer warning that states all three values, e.g. “Custom nozzle mode: sliced for physical 1.57 mm; Bambu A1 reports 0.8 mm. Confirm custom hardware is installed.”

Add `PrintStatusCustomNozzleCompatibilityWarning` inside the printer-warning range in `src/slic3r/GUI/PrePrintChecker.hpp`; map it in `PrePrintChecker::get_print_status_info()` and `get_pre_state_msg()` in `.cpp`. Because it is in the warning range, `PrePrintChecker::add()` will route it to `printerList` without disabling Send. Add the warning during the same status refresh that performs diameter matching, before the final Ready state. Verify `show_status()` does not overwrite/clear it when setting `PrintStatusReadyToGo`; the checker list and single `m_print_status` are coupled loosely, so this needs a focused UI-path test/manual check.

Update mismatch copy to distinguish values:

- off: retain current wording;
- on but device differs: “Printer reports 0.6 mm; this preset's Bambu compatibility diameter is 0.8 mm (physical nozzle 1.57 mm). Select the matching reported diameter or update Printer parts.”

Do not weaken `is_nozzle_type_match()`, hardness checks, AMS checks, printer-model checks, or `_is_nozzle_data_valid()`. Diameter compatibility is the only bypassed identity dimension.

Apply the same reported-value helper to Bambu-facing task payloads:

- `SelectMachineDialog::build_nozzles_info()`;
- `SyncAmsInfoDialog::build_nozzles_info()`;
- audit the equivalent payload in `SendMultiMachinePage` if it gains/duplicates nozzle JSON.

The payload should report the alias only when enabled. Keep `nozzle_volume_type`/flow type unchanged; firmware support for pretending a standard 0.8 nozzle while using a custom high-flow nozzle is unknown and should not be fabricated.

### Plater matching call sites

Use two explicit helper names rather than globally changing semantics:

- `matches_physical_nozzle(...)` for choosing a slicing preset;
- `matches_bambu_reported_nozzle(...)` for deciding whether an already-selected preset can target a Bambu device.

Then audit these `Plater.cpp` sites:

- Project-load sync prompt (`preset_nozzle_diameter` / `nozzle_mismatch`): use reported matching when the selected preset is already the same printer model and has an active override, so loading a project does not prompt to replace the 1.57 preset with a stock 0.8 preset.
- Multi-extruder sidebar sync (`same_nozzle_diameter`): use reported matching only for the device-compatibility gate; retain physical values in the sidebar.
- `get_printer_preset()`: keep physical matching because this function searches stock/system slicing presets for a device. An active custom user preset should not cause system-preset discovery to treat 1.57 as 0.8. Document this exception with a code comment.
- Any future call to `NozzleDiameterMatchesOrUnknown()` must choose one of the two semantics explicitly.

## Phase 4: transport-only 3MF projection

Do not mutate `DynamicPrintConfig`, the edited preset, `PlateData`, or generated G-code in place. Ordinary **Save project**, backup/restore, and generic 3MF export must retain physical values.

Add an explicit export context rather than inferring transport from `WithGcode` or `SkipModel` (those flags are also used by user exports and QA paths). Recommended design:

```cpp
enum class BambuMetadataMode { Physical, ReportedCompatibility };

struct StoreParams {
    // existing fields...
    BambuMetadataMode bambu_metadata_mode = BambuMetadataMode::Physical;
};
```

`Plater::export_3mf()` should accept/pass this mode (default Physical). `Plater::send_gcode()` and `Plater::export_config_3mf()` pass `ReportedCompatibility`; ordinary save/export/backup paths pass the default. Calibration export in `CalibUtils.cpp` remains Physical until separately verified, and the UI should state that automated Bambu calibration with custom-nozzle compatibility is unsupported in phase 1.

Inside `_BBS_3MF_Exporter`, derive an immutable vector once from `config` and the mode:

```cpp
const auto transport_diameters = mode == ReportedCompatibility
    ? get_bambu_reported_nozzle_diameters(config)
    : config.option<ConfigOptionFloats>("nozzle_diameter")->values;
```

Use it only in `_add_slice_info_config_file_to_archive()` for:

- plate `nozzle_diameters` metadata;
- filament `nozzle_diameter` (do not let a pre-populated physical `FilamentInfo::nozzle_diameter` override the transport projection);
- nozzle-node `nozzle_diameter`.

For `PlateBBoxData::nozzle_diameter`, pass a projected copy to `_add_bbox_file_to_archive()` in transport mode, selected by `first_extruder`. Never mutate the live `PlateBBoxData`. This field's firmware role is uncertain, so test both with and without projection against an A1 before settling it; start by projecting for internal consistency of every Bambu-facing diameter in the job.

Do **not** replace `nozzle_diameter` in:

- the config passed to slicing or G-code generation;
- embedded printer preset/config files;
- Orca's general project config;
- G-code header/config comments;
- preview/calibration geometry.

Those are provenance and physical-process data. If empirical testing shows firmware also validates an embedded config field, add a narrowly named compatibility projection for the temporary job only and preserve physical data under an Orca-specific key; do not pass an alias config back into the slicer.

### Preservation and import

Add Orca-specific slice-info metadata in transport mode:

```xml
<metadata key="orca_physical_nozzle_diameters" value="1.57"/>
<metadata key="orca_bambu_nozzle_override" value="1"/>
```

Recommended constants belong beside `NOZZLE_DIAMETERS_ATTR` in `bbs_3mf.cpp`. Add corresponding fields to `PlateData` in `bbs_3mf.hpp` (physical diameter string and override-active flag), copy them in both plate-data transfer blocks near the existing `nozzle_diameters` copies, and parse them in `_handle_start_config_metadata()`.

Round-trip precedence:

1. Embedded/full printer config is authoritative for physical `nozzle_diameter` and the opt-in settings.
2. `orca_physical_nozzle_diameters` is fallback provenance for sliced/foreign transport files without embedded presets.
3. Standard `nozzle_diameters` remains the Bambu-reported transport value in a transport artifact.
4. Legacy files lacking Orca keys retain current behavior.

When reopening an Orca-created transport 3MF, use the physical metadata for UI description and for reconstructing a missing config, but preserve the standard reported value in `PlateData` so re-export can remain interoperable. Never silently enable the override from a foreign file that merely has differing metadata; only Orca's explicit override key or an embedded config may activate it.

## Tests and verification seams

### Unit tests (no GUI/device required)

Add BBS exporter tests to `tests/libslic3r/test_3mf.cpp` or a dedicated `test_bbs_3mf.cpp` registered in `tests/libslic3r/CMakeLists.txt`. Use `StoreParams`, a minimal sliced `PlateData`, and miniz archive extraction (the production code already links miniz) to inspect `Metadata/slice_info.config` and `Metadata/plate_1.json`.

Required cases:

1. Default/off export: physical 1.57 appears in plate, filament, nozzle, and bbox fields for both Physical and ReportedCompatibility modes.
2. Enabled transport export: standard plate/filament/nozzle/bbox fields are 0.8; Orca physical metadata is 1.57.
3. Enabled ordinary project export: all standard fields remain 1.57; embedded/full config retains physical 1.57 plus enabled/reporting options.
4. Two extruders: each emitted filament/nozzle uses its mapped group's effective reported value; fallback-to-front behavior is covered.
5. Import round trip: physical 1.57 and reported 0.8 survive save/load/save, the opt-in survives, and legacy files have no behavior change.
6. Invalid/misaligned override vectors fail closed to physical values.

Extract the metadata projection into a pure function if testing the full exporter requires excessive model setup. Still keep at least one archive-level integration test so field wiring cannot regress independently.

### GUI/device logic tests

The current GUI layer has little direct unit-test coverage. Make the diameter comparison a pure function accepting physical, reported, active, and device values; unit-test it without wx/device objects:

- off + device 0.8 + physical 1.57 => blocking mismatch;
- on/report 0.8 + device 0.8 => match plus warning;
- on/report 0.8 + device 0.6 => blocking mismatch;
- unknown device 0 => match under existing policy;
- floating-point values use epsilon;
- only used extruders are checked.

Add enum-classification assertions for the new status (`is_warning`, `is_warning_printer`, not `is_error`). Verify the no-override path still produces `PrintStatusNozzleDiameterMismatch`.

### Manual A1 acceptance matrix

Use a harmless small model and conservative volumetric limits. Save all generated temporary 3MFs for inspection.

| Physical | Override | Reported | A1 setting | Expected |
|---:|:---:|---:|---:|---|
| 0.8 | off | ignored | 0.8 | Existing flow, no custom warning |
| 1.57 | off | ignored | 0.8 | Send blocked by mismatch |
| 1.57 | on | 0.8 | 0.8 | Send enabled, persistent warning, job accepted |
| 1.57 | on | 0.6 | 0.8 | Send blocked by compatibility mismatch |
| 1.57 | on | 0.8 | unknown/offline | Existing unknown-device behavior plus warning |

For the accepted job, unzip the exact uploaded artifact and verify physical G-code/extrusion values against the 1.57 slice while all identified Bambu identity metadata reads 0.8. Confirm the printer starts only after a human checks the installed custom nozzle. Run a dry motion/low-flow test before extrusion testing.

## Firmware uncertainty and safety risks

- **Unknown validation surface:** Source inspection shows three slice-info diameter locations and the bbox JSON field, but closed Bambu firmware/network code may validate additional metadata or derive identity from filenames/profile IDs. Capture printer error responses and compare an accepted stock-0.8 3MF with the compatibility 3MF before broadening projection.
- **Metadata consistency:** Sending mixed 1.57/0.8 values may cause rejection; rewriting every config occurrence to 0.8 would destroy physical provenance and could affect host-side logic. The explicit transport projection is the controlled middle path.
- **Cloud vs LAN differences:** Verify both paths. The cloud service may inspect metadata that LAN firmware ignores. Do not claim support until each intended path passes.
- **Calibration commands:** `DeviceManager.cpp` calibration payloads use a hard-coded supported-diameter formatter and firmware returns errors such as `nozzle_diameter is not supported`. Automated PA/flow calibration should remain disabled or clearly unsupported for custom physical nozzle mode until a separate protocol is proven.
- **Profile synchronization:** Existing device-sync logic tends to replace a mismatching preset with a stock preset. The Plater audit above is required to prevent accidental loss of the custom physical preset.
- **Multi-nozzle ordering:** Bambu config order and device extruder order differ in several paths (left/right remapping). Tests must use the same physical-extruder mapping as `PartPlate::get_physical_extruder_by_filament_id()` and not assume vector index equals device id.
- **Thermal/mechanical capacity:** This feature only separates identity from geometry. It cannot establish safe melt rate, heater power, pressure, cooling, clearance, or firmware motion limits. The warning must not imply those are validated.

## Implementation order and review gates

1. Land config options, validation, vector resizing, and pure policy helper. Gate: all helper/config tests pass and defaults serialize to no behavioral change.
2. Land printer-tab UI. Gate: off by default; Bambu-only visibility; preset save/reload and two-extruder behavior verified.
3. Land pure matching function and `SelectMachine` warning/mismatch behavior, plus `build_nozzles_info()` projection. Gate: tests cover match matrix; stock 0.8 behavior unchanged.
4. Land explicit exporter mode and slice-info/bbox projection. Gate: archive-level tests prove ordinary projects physical and temporary jobs reported.
5. Land importer/provenance fields and round-trip tests. Gate: saved project and transport artifact both reopen with physical 1.57 intact.
6. Audit Plater sync sites, multi-machine send, SD-card/send-to-printer paths, and calibration exclusions. Gate: no direct Bambu match or outgoing identity field still reads physical diameter unintentionally.
7. Run the manual A1 matrix over LAN first, then cloud if used. Record exact firmware version, Orca commit, artifact hashes, and printer response. Only after a successful artifact comparison should the UI describe the feature as supported rather than experimental.

## Acceptance criteria

- A printer preset can store physical 1.57 mm and an explicit, default-off Bambu reported value of 0.8 mm.
- With override off, all current slicing, matching, export, and warning behavior is unchanged.
- Slicing and all safety validation use 1.57 mm; no production geometry path reads the Bambu reported option.
- An A1 reporting 0.8 matches the custom preset only when the explicit 0.8 override is enabled; mismatched reported values still block Send.
- Every enabled send shows a non-dismissible-in-context warning containing physical, reported, and device values; Send remains possible only after normal checks pass.
- The exact temporary 3MF sent to Bambu reports 0.8 in every empirically required Bambu identity field while retaining 1.57 in Orca provenance/config metadata.
- Ordinary project save, backup, generic export, and reopen preserve physical 1.57 and the opt-in setting; they are not transport-aliased.
- Legacy 3MFs and presets load with the override off and retain existing behavior.
- Single- and multi-extruder mapping tests, exporter archive tests, import round-trip tests, and existing libslic3r/FFF tests pass.
- A real Bambu A1 on a recorded firmware version accepts and starts the conservative test job through each supported transport path, and the generated extrusion values are verified to come from the 1.57 mm slice.
