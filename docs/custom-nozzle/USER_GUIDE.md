# Custom Bambu nozzle compatibility user guide

This guide explains how to use this OrcaSlicer build with a custom physical
nozzle that is larger than the nozzle identities supported by Bambu firmware.

The target use case is a Bambu A1 with a real custom nozzle around `1.57 mm`,
while the printer firmware is configured as one of its supported values, usually
`0.8 mm`.

## The core idea

Bambu printers only accept a small set of nozzle identities from the printer
firmware and job metadata: `0.2`, `0.4`, `0.6`, or `0.8 mm`.

This build lets Orca keep two separate nozzle values:

| Field | Meaning | Used for |
| --- | --- | --- |
| `Nozzle diameter` | The real physical nozzle installed on the printer | Slicing geometry, extrusion math, flow, line width defaults, and layer-height validation |
| `Override Bambu nozzle diameter` | Expert opt-in toggle for Bambu compatibility mode | Enables sending a firmware-supported identity to Bambu-specific checks |
| `Bambu nozzle diameter` | The nozzle identity reported to the Bambu printer | Device matching and Bambu outgoing job compatibility metadata |

The important part: the original Orca `Nozzle diameter` field remains the
truth for slicing.

That means the error:

```text
Layer height cannot exceed nozzle diameter
```

is still enforced against the physical nozzle diameter, not the Bambu-reported
diameter. A profile with a physical `1.57 mm` nozzle can use a `1.0 mm` layer
height. A profile with a physical `0.8 mm` nozzle still cannot.

## Recommended setup for a 1.57 mm nozzle on a Bambu A1

1. Create or duplicate a Bambu A1 printer profile.

2. Open the printer profile settings.

3. Set `Nozzle diameter` to the real physical nozzle size, for example:

   ```text
   1.57 mm
   ```

4. Tune the large-nozzle print settings normally:

   - layer height;
   - first-layer height;
   - line widths;
   - maximum volumetric speed;
   - print speeds;
   - temperatures;
   - cooling;
   - retraction and pressure behavior.

5. Switch to Expert mode if the Bambu compatibility fields are not visible.

6. In the `Bambu printer compatibility` section, enable:

   ```text
   Override Bambu nozzle diameter
   ```

   Orca will show a warning that slicing still uses the physical nozzle and
   that custom hardware cannot be verified automatically.

7. Set:

   ```text
   Bambu nozzle diameter = 0.8 mm
   ```

8. On the physical Bambu printer, configure the printer's nozzle setting to the
   same reported value:

   ```text
   0.8 mm
   ```

9. Slice and send the job.

Orca should now slice as if the real nozzle is `1.57 mm`, while the Bambu
printer compatibility path sees a supported `0.8 mm` identity.

## Example values

For the current custom A1 use case, the profile should look conceptually like
this:

| Setting | Value |
| --- | --- |
| Printer | Bambu A1 |
| Physical nozzle installed | `1.57 mm` |
| Orca `Nozzle diameter` | `1.57 mm` |
| `Override Bambu nozzle diameter` | Enabled |
| `Bambu nozzle diameter` | `0.8 mm` |
| Printer firmware nozzle setting | `0.8 mm` |

This is intentionally not the same as pretending the nozzle is `0.8 mm`
everywhere. Pretending globally would make Orca's slicer math and layer-height
checks wrong for the actual hardware.

## What the new fields do

### Nozzle diameter

This is the physical nozzle diameter. Keep this set to the actual nozzle
installed on the printer.

For a custom `1.57 mm` nozzle, enter `1.57`.

This value controls slicing behavior, including:

- extrusion and flow calculations;
- automatic extrusion widths;
- layer-height checks;
- geometry decisions;
- bridge and line-width validation;
- normal saved Orca project/profile state.

If you see `Layer height cannot exceed nozzle diameter`, check this field first.
That error means the selected layer height is larger than Orca's physical nozzle
diameter.

### Override Bambu nozzle diameter

This is an Expert-mode opt-in setting for Bambu printers.

When disabled, Orca behaves like normal upstream OrcaSlicer: the printer/device
matching path uses the physical nozzle diameter.

When enabled, Orca reports the value from `Bambu nozzle diameter` at the
Bambu-specific compatibility boundary. This is what lets a profile sliced for a
`1.57 mm` physical nozzle match a printer that reports `0.8 mm`.

Enable this only for a Bambu printer profile that you intentionally use with
custom nozzle hardware.

### Bambu nozzle diameter

This is the firmware-compatible identity that Orca reports to the Bambu printer.

Supported values are:

- `0.2 mm`
- `0.4 mm`
- `0.6 mm`
- `0.8 mm`

For a large custom nozzle on an A1, `0.8 mm` is usually the practical choice
because it is the largest identity the Bambu firmware exposes.

This field does not change slicing geometry. It is ignored unless
`Override Bambu nozzle diameter` is enabled.

## What happens when you send a job

With the override enabled and valid:

- Orca slices using the physical `Nozzle diameter`, such as `1.57 mm`.
- Orca compares the Bambu printer's reported nozzle against `Bambu nozzle
  diameter`, such as `0.8 mm`.
- Bambu-facing outgoing job metadata uses the reported Bambu identity where the
  printer expects its supported nozzle value.
- Orca-only metadata keeps enough information to recover the physical nozzle
  value when the project is reopened.

With the override disabled:

- Orca uses the physical nozzle diameter for both slicing and Bambu device
  matching, matching normal Orca behavior.

## What this does not do

This build does not modify Bambu firmware.

It also does not prove that a custom nozzle is mechanically, thermally, or
extrusion-wise safe. The printer still believes it is using the reported Bambu
identity, for example `0.8 mm`.

You still need to validate:

- heater capacity;
- maximum volumetric flow;
- extruder grip and skipping behavior;
- cooling;
- first-layer behavior;
- bed adhesion;
- acceleration and speed limits;
- whether the printer's firmware behavior is acceptable for the custom setup.

Treat the first prints as hardware validation prints, not production prints.

## Calibration limitation

Automated Bambu PA and flow calibration is intentionally blocked while the
Bambu nozzle override is enabled.

Reason: those calibration paths talk to Bambu firmware using supported nozzle
identities and assumptions. Running automatic calibration while the physical
nozzle and reported firmware identity intentionally differ could produce
misleading results or firmware errors.

Recommended workflow:

1. Use the custom profile.
2. Disable automatic Bambu calibration for this mode.
3. Calibrate manually with controlled test prints.
4. Store the resulting flow, pressure advance, temperature, and speed limits in
   the custom profiles.

## Troubleshooting

### Orca still says `Layer height cannot exceed nozzle diameter`

Check the physical `Nozzle diameter` field.

If it is still set to `0.8 mm`, Orca is correctly rejecting a layer height above
`0.8 mm`. Set `Nozzle diameter` to the real physical nozzle size, such as
`1.57 mm`.

Do not fix this by weakening the layer-height validation globally. The safer
design is to make the physical nozzle larger while reporting a separate Bambu
identity only where the printer requires it.

### The printer/nozzle does not match when sending

Check all three values:

| Place | Expected for the 1.57 mm A1 case |
| --- | --- |
| Orca `Nozzle diameter` | `1.57 mm` |
| Orca `Bambu nozzle diameter` | `0.8 mm` |
| Bambu printer nozzle setting | `0.8 mm` |

Also confirm that `Override Bambu nozzle diameter` is enabled.

### The Bambu compatibility fields are missing

The fields are Expert-mode Bambu printer settings. Check that:

- you are editing a Bambu printer profile;
- the settings view is in Expert mode;
- you are in the printer profile section, not just the filament or process
  settings.

### Orca changes the Bambu nozzle value I typed

That is expected if the value is not one of Bambu's supported identities.

Only these values are accepted:

- `0.2 mm`
- `0.4 mm`
- `0.6 mm`
- `0.8 mm`

If you type something like `1.57 mm` into `Bambu nozzle diameter`, Orca will
correct it to the nearest supported Bambu identity. The physical `Nozzle
diameter` field is where `1.57 mm` belongs.

### Automated calibration is unavailable

That is expected while the override is enabled.

Use manual calibration for the custom physical nozzle, or disable the override
and use a standard firmware-supported physical nozzle if you need Bambu's
automatic calibration path.

## Safer first-print checklist

Before a real print, run a small validation print and watch it closely.

- Confirm the physical custom nozzle is installed.
- Confirm the printer firmware is set to the same reported value as Orca's
  `Bambu nozzle diameter`.
- Start with conservative speed and volumetric-flow limits.
- Use a simple shape before a long or complex print.
- Watch the first layer.
- Watch for extruder skipping, heat creep, under-extrusion, over-extrusion, and
  cooling problems.
- Increase throughput only after the hardware behavior is repeatable.

## Mental model

Use this build like this:

```text
Physical nozzle diameter = what the slicer must believe
Bambu nozzle diameter    = what the printer firmware is willing to hear
```

For the custom A1 setup:

```text
Physical nozzle diameter = 1.57 mm
Bambu nozzle diameter    = 0.8 mm
```

That split is the whole feature. Orca slices truthfully for the hardware, while
Bambu receives a nozzle identity its firmware accepts.
