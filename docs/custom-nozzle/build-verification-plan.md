# macOS build, test, and launch verification plan

## Purpose and completion criteria

This plan turns the custom physical-nozzle / Bambu compatibility-nozzle feature into a locally launchable Apple Silicon application and proves that the implementation works at the unit, serialization, UI, slicing, and send-validation boundaries.

The work is complete only when all of the following are true:

1. The native `arm64` dependency prefix and OrcaSlicer build configure successfully from this checkout.
2. Focused custom-nozzle tests, all `fff_print_tests`, and the complete CTest suite pass.
3. The packaged app exists at `build/arm64/OrcaSlicer/OrcaSlicer.app` and its main executable is an `arm64` Mach-O.
4. A clean, isolated-data-directory launch reaches the main window without a startup crash.
5. A printer preset can retain a physical diameter such as `1.57 mm` while presenting a Bambu-compatible diameter such as `0.8 mm` to device compatibility and Bambu transport metadata.
6. Slicing still uses the physical diameter: a valid large-nozzle layer height succeeds, while a layer height greater than the physical diameter remains rejected.
7. The send UI clearly discloses the compatibility override and does not silently weaken validation for ordinary presets.

This is a local development build. Producing a distributable, Gatekeeper-trusted application additionally requires an Apple Developer ID certificate and notarization credentials, which are not needed to launch the build on this Mac.

## Evidence collected from this checkout and host

Audit date: 2026-07-02.

- Checkout: `OrcaSlicer`, commit `036bd7bcec` (`feat: add {first_object_name} filename placeholder (#14497)`).
- Host: Apple Silicon (`arm64`), Apple M5 Max, 18 cores, 64 GB RAM, macOS 26.4.1.
- Storage: about 1.6 TiB free on the data volume. The source checkout is about 1.5 GB.
- Compiler: Apple Clang 21 is present through Command Line Tools.
- Active developer directory: `/Library/Developer/CommandLineTools`; full Xcode is not installed/selected.
- Present: Apple Git 2.50.1, GNU Make 3.81, Perl, Python 3.
- Missing from `PATH`: CMake, Ninja, Homebrew, automake, autoconf, GNU libtool, gettext (`msgfmt`, `xgettext`, `msgmerge`), and Git LFS.
- Existing generated artifacts: none. Neither `build/` nor `deps/build/` currently exists, so there is no reusable dependency prefix, CMake cache, test binary, or app bundle.

The repository's macOS CI uses CMake 4.3.x, Ninja Multi-Config, `automake`, `texinfo`, `libtool`, and architecture-specific dependency prefixes. Its relevant commands are:

```sh
./build_release_macos.sh -dx -a arm64 -t 10.15
./build_release_macos.sh -s -n -x -a arm64 -t 10.15
```

For this local development build, use the script's current default deployment target, macOS 11.3, unless compatibility with 10.15 is an explicit deliverable. Use `-x` throughout: it selects Ninja Multi-Config and avoids the script's default Xcode generator. Full Xcode is therefore optional for the native build; the selected Command Line Tools SDK and compiler should be sufficient. If CMake reports a missing SDK/framework/tool, install full Xcode and select it as described under fallbacks.

## Phase 0: preserve state and establish a baseline

Run all commands from the repository root:

```sh
cd /Users/artem/Documents/Git/kojinyo/Codex-Orca-Rebuild/OrcaSlicer
git status --short
git rev-parse --short HEAD
uname -m
sw_vers
df -h .
```

Before building, record the implementation commit or current diff:

```sh
git diff --check
git diff --stat
```

Do not remove `build/` or `deps/build/` once they exist. They are the expensive caches used by incremental compiles.

## Phase 1: install and verify prerequisites

### Required tools

- Apple Command Line Tools, or full Xcode with the developer directory selected.
- CMake (the CI line is 4.3.x; the build script adds its compatibility flag for CMake 4).
- Ninja.
- GNU gettext.
- automake, autoconf, texinfo, and GNU libtool for dependency builds.
- Git and Python 3.
- Network access for the first dependency build, because CMake `ExternalProject` steps fetch upstream sources.

If Homebrew is approved, install Homebrew using its official installer and then run:

```sh
brew update
brew install cmake ninja automake autoconf texinfo libtool gettext
```

Homebrew may keep gettext and some GNU tools keg-only. Put their binaries first for this shell:

```sh
export PATH="$(brew --prefix gettext)/bin:$(brew --prefix libtool)/bin:$(brew --prefix texinfo)/bin:$PATH"
```

Persist that export in the developer's shell profile only if desired. Git LFS is not required for the current build—the CI explicitly checks out with LFS disabled—but may be installed separately if a future branch introduces required LFS assets.

Verify the toolchain before spending time on dependencies:

```sh
xcode-select -p
xcrun --show-sdk-path
clang --version
cmake --version
ninja --version
automake --version
glibtoolize --version
msgfmt --version
xgettext --version
python3 --version
```

Expected result: every command exits zero. `xcrun --show-sdk-path` must name an existing macOS SDK, `clang` must target `arm64-apple-darwin`, and CMake must be at least the repository minimum (3.13). A current CMake 4.3.x is preferred because it matches CI.

## Phase 2: build the native dependency prefix once

Build only `arm64`; a universal app doubles dependency and application build work and is unnecessary for this Apple Silicon machine.

```sh
cd /Users/artem/Documents/Git/kojinyo/Codex-Orca-Rebuild/OrcaSlicer
export PATH="$(brew --prefix gettext)/bin:$(brew --prefix libtool)/bin:$(brew --prefix texinfo)/bin:$PATH"
./build_release_macos.sh -d -x -a arm64 -t 11.3
```

Expected reusable prefix:

```text
deps/build/arm64/OrcaSlicer_dep/
```

Verification:

```sh
test -d deps/build/arm64/OrcaSlicer_dep
find deps/build/arm64/OrcaSlicer_dep -maxdepth 2 -type d | sed -n '1,80p'
du -sh deps/build/arm64 deps/build/arm64/OrcaSlicer_dep
```

On subsequent attempts, preserve this directory. If dependency configuration is unchanged, `./build_release_macos.sh -d -x -b -a arm64 -t 11.3` skips reconfiguration and resumes/rebuilds the existing graph. Use `-1` only to diagnose memory pressure or an unstable third-party build; this host's 64 GB RAM should make normal parallelism appropriate.

## Phase 3: configure tests and build the smallest useful target

Configure the native slicer tree with tests enabled, but initially build only the focused test executable:

```sh
cd /Users/artem/Documents/Git/kojinyo/Codex-Orca-Rebuild/OrcaSlicer
export PATH="$(brew --prefix gettext)/bin:$(brew --prefix libtool)/bin:$(brew --prefix texinfo)/bin:$PATH"
cmake -S . -B build/arm64 \
  -G "Ninja Multi-Config" \
  -DORCA_TOOLS=ON \
  -DBUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.3 \
  -DCMAKE_IGNORE_PREFIX_PATH="/opt/local;/usr/local;/opt/homebrew" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5

cmake --build build/arm64 --config Release --target fff_print_tests
```

The explicit CMake command is equivalent to the important configuration performed by `build_release_macos.sh -s -x -T`, but it permits a focused target first. The `CMAKE_IGNORE_PREFIX_PATH` value deliberately prevents accidental linkage to unrelated Homebrew/MacPorts libraries; Orca should use its architecture-specific prefix. Keep the tool binaries available through `PATH` even though their installation prefixes are ignored for library discovery.

Expected focused binary:

```text
build/arm64/tests/fff_print/Release/fff_print_tests
```

Discover and run the existing `PrintObject` coverage plus any new custom-nozzle tag added by the implementation:

```sh
build/arm64/tests/fff_print/Release/fff_print_tests --list-tests '*custom*'
build/arm64/tests/fff_print/Release/fff_print_tests '[PrintObject]'
build/arm64/tests/fff_print/Release/fff_print_tests '[custom-nozzle]'
```

If the implementation uses a different dedicated Catch2 tag, substitute that tag. The final implementation should add `[custom-nozzle]` to make this gate stable. Required focused cases are:

- Default/no-override behavior remains identical.
- Physical `1.57`, compatibility `0.8`: layer height between `0.8` and `1.57` is accepted.
- A layer height above `1.57` is rejected.
- Automatic widths and flow calculations use `1.57`, not `0.8`.
- Invalid compatibility values and malformed/mismatched vector lengths fail safely or fall back exactly as specified.

If tests are added to another executable (for example `libslic3r_tests` for config or 3MF serialization), build and run that target directly too:

```sh
cmake --build build/arm64 --config Release --target libslic3r_tests
build/arm64/tests/libslic3r/Release/libslic3r_tests '[custom-nozzle]'
```

## Phase 4: progressive automated verification

Run increasingly broad gates so failures remain attributable.

1. Re-run all FFF tests:

   ```sh
   cmake --build build/arm64 --config Release --target fff_print_tests
   build/arm64/tests/fff_print/Release/fff_print_tests
   ```

2. Build all registered test executables:

   ```sh
   cmake --build build/arm64 --config Release --target all
   ctest --test-dir build/arm64 -C Release -N
   ```

3. Run the complete suite, excluding the repository's explicitly tagged non-working cases:

   ```sh
   ctest --test-dir build/arm64 \
     -C Release \
     -LE NotWorking \
     --no-tests=error \
     --output-on-failure \
     --output-junit build/arm64/ctest_results.xml \
     -j 18
   ```

4. Validate source and localization hygiene:

   ```sh
   git diff --check
   ./scripts/run_gettext.sh
   git status --short
   ```

`run_gettext.sh` writes compiled `.mo` files under `resources/i18n`; verify that it does not introduce unexpected tracked changes. If UI strings were added, update the localization sources/template through the repository's localization workflow rather than accepting accidental generated drift.

## Phase 5: produce the packaged native app

After the CMake tree exists, let the repository script perform the build, localization check, and resource-copy packaging. `-b` preserves the tested configuration and skips a fresh configure:

```sh
cd /Users/artem/Documents/Git/kojinyo/Codex-Orca-Rebuild/OrcaSlicer
export PATH="$(brew --prefix gettext)/bin:$(brew --prefix libtool)/bin:$(brew --prefix texinfo)/bin:$PATH"
./build_release_macos.sh -s -x -b -a arm64 -c Release -t 11.3
```

Expected paths:

```text
build/arm64/src/Release/OrcaSlicer.app
build/arm64/OrcaSlicer/OrcaSlicer.app
build/arm64/OrcaSlicer/OrcaSlicer.app/Contents/MacOS/OrcaSlicer
build/arm64/OrcaSlicer/OrcaSlicer_profile_validator.app   (only if that optional target is produced)
```

The app under `build/arm64/OrcaSlicer/` is the handoff artifact. The build script replaces the source-tree resource symlink with a real resource copy there.

Structural checks:

```sh
APP="$PWD/build/arm64/OrcaSlicer/OrcaSlicer.app"
test -x "$APP/Contents/MacOS/OrcaSlicer"
test -d "$APP/Contents/Resources"
test ! -L "$APP/Contents/Resources"
file "$APP/Contents/MacOS/OrcaSlicer"
lipo -archs "$APP/Contents/MacOS/OrcaSlicer"
plutil -lint "$APP/Contents/Info.plist"
otool -L "$APP/Contents/MacOS/OrcaSlicer" | sed -n '1,100p'
codesign --verify --deep --strict "$APP" || true
```

Expected architecture: `arm64`. The last check may report that the local development bundle is unsigned; that is expected and is not a functional failure. Do not mistake `spctl` rejection of an unnotarized local build for an application defect.

Quick CLI smoke test with an isolated data directory:

```sh
mkdir -p /tmp/orcaslicer-custom-nozzle-smoke
"$APP/Contents/MacOS/OrcaSlicer" \
  --datadir /tmp/orcaslicer-custom-nozzle-smoke \
  --help > /tmp/orcaslicer-custom-nozzle-help.txt
test -s /tmp/orcaslicer-custom-nozzle-help.txt
```

## Phase 6: UI and end-to-end runtime verification

Use an isolated data directory so the development app cannot migrate or overwrite the user's normal OrcaSlicer profiles:

```sh
APP="$PWD/build/arm64/OrcaSlicer/OrcaSlicer.app"
SMOKE_DATA="/tmp/orcaslicer-custom-nozzle-ui"
mkdir -p "$SMOKE_DATA"
"$APP/Contents/MacOS/OrcaSlicer" --datadir "$SMOKE_DATA" --no-single-instance
```

Keep the launching terminal visible to capture fatal output. Also inspect the newest log beneath the isolated data directory after each scenario.

### UI acceptance script

1. Complete or dismiss first-run setup in the isolated profile.
2. Create/clone a Bambu Lab A1 printer preset.
3. Set the physical nozzle diameter to `1.57 mm`.
4. Enable the explicit Bambu/device compatibility override and set it to `0.8 mm`.
5. Confirm the UI labels distinguish physical slicing diameter from reported/compatibility diameter, explain the risk, and show the override in the preset's dirty-state/save flow.
6. Save, switch to another preset, switch back, restart the app, and confirm both values persist.
7. Load a simple cube and set an intentionally diagnostic layer height greater than `0.8` but no greater than `1.57` (for example `1.0 mm`). Slice and verify there is no `Layer height cannot exceed nozzle diameter` failure.
8. Set layer height above the physical diameter (for example `1.60 mm`) and verify the safety error still appears.
9. Return to a mechanically conservative layer height for any real-world printing. The diagnostic `1.0 mm` test proves code-path separation; it is not a print recommendation.
10. Inspect Preview/G-code statistics and, if the implementation exposes them, debug/log output to confirm widths/flow derive from `1.57 mm`.
11. Open the Send dialog with an A1 reporting `0.8 mm`. Confirm compatibility succeeds only because of the explicit override, the custom-hardware warning is visible, and Send is not disabled by a comparison against `1.57`.
12. Cancel before transmitting during the first verification pass. A later live-device test may send a non-printing job only with the printer attended and the physical modification independently safety-checked.
13. Export a Bambu 3MF, unzip a copy, and inspect its plate/filament/nozzle metadata. Device-facing diameter fields must be `0.8`; Orca's project/preset data must preserve `1.57` so reopening the 3MF restores the physical value.
14. Reopen the exported 3MF in the isolated app and repeat the value and slice checks.
15. Disable the override. Confirm ordinary preset behavior returns: physical `1.57` versus device `0.8` is rejected, and standard 0.8 presets remain unchanged.

For deterministic metadata evidence, keep a fixture 3MF under `/tmp`, extract it without modifying the working tree, and search it:

```sh
rm -rf /tmp/orca-custom-nozzle-3mf
mkdir -p /tmp/orca-custom-nozzle-3mf
ditto -x -k /path/to/exported-custom-nozzle.3mf /tmp/orca-custom-nozzle-3mf
rg -n '1\.57|0\.8|nozzle' /tmp/orca-custom-nozzle-3mf
```

Capture screenshots of the printer setting, warning, successful slice, retained physical safety error, and Send dialog. Record the exact preset and 3MF fixture used in the feature's implementation notes.

## Phase 7: launch handoff

Once verification passes, launch the app directly:

```sh
open "$PWD/build/arm64/OrcaSlicer/OrcaSlicer.app"
```

The build-directory app is preferable during development because incremental rebuilds replace it predictably. If a standalone copy is desired, copy it to a user-writable destination such as `~/Applications/` only after tests pass. Do not overwrite an installed release of OrcaSlicer without explicit user approval.

If macOS refuses an unsigned local bundle even though it was built locally, ad-hoc sign the handoff copy (this mutates the app bundle) and verify it:

```sh
codesign --force --deep --sign - build/arm64/OrcaSlicer/OrcaSlicer.app
codesign --verify --deep --strict build/arm64/OrcaSlicer/OrcaSlicer.app
open build/arm64/OrcaSlicer/OrcaSlicer.app
```

Do not remove quarantine attributes as a generic fix. A bundle produced in this checkout normally has no downloaded-file quarantine attribute; inspect with `xattr -l` first.

## Expected time and disk cost

These are planning ranges, not guarantees; third-party downloads and compiler thermals dominate the first build.

| Stage | Likely elapsed time on this M5 Max | Additional disk | Notes |
|---|---:|---:|---|
| Tool installation | 5-20 min | 1-3 GB | Full Xcode, if required, adds roughly 15-40+ GB and a much longer download/install. |
| First arm64 dependency build | 30-120 min | 10-30 GB | Network-dependent; preserves source, build, and installed prefix trees. |
| First focused test target | 15-45 min | 5-15 GB | Builds much of `libslic3r`; subsequent edits are far faster. |
| First full app and all tests | 20-60 min after deps | 10-25 GB | Includes GUI, tools, tests, and copied app resources. |
| Incremental feature rebuild | 1-10 min | Usually under 1 GB growth | Header/config changes can fan out farther. |
| Native packaged app | under 5 min after compile | roughly 0.5-2 GB | Resource copy is duplicated from the raw app. |
| Universal build (optional) | roughly 2x native work | another 20-50 GB | Requires complete `x86_64` deps/app plus `lipo`; unnecessary for this host. |

Reserve at least 50 GB free for a comfortable native build and 100 GB for a universal build. Current free space is far above either threshold.

## Failure isolation and fallback paths

### CMake or Ninja is missing

Install current packages, reopen/export the Homebrew paths, and rerun only the failed phase. Do not delete dependency artifacts.

### The selected Command Line Tools SDK is insufficient

Install full Xcode, launch it once to accept its license/components, then select it:

```sh
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -license accept
xcrun --show-sdk-path
```

Continue using `-x`/Ninja unless debugging a Ninja-specific issue. Alternatively, after full Xcode is selected, omit `-x` to use the script's Xcode generator; note that this changes generated paths and invalidates reuse of the Ninja CMake tree.

### Dependency download/build fails

1. Preserve `deps/build/arm64` and rerun the same command; ExternalProject builds commonly resume.
2. Add `-1` to serialize a flaky or memory-heavy build:

   ```sh
   ./build_release_macos.sh -d -x -b -1 -a arm64 -t 11.3
   ```

3. Inspect the failing dependency's logs under `deps/build/arm64` before changing versions or deleting anything.
4. If `-b` fails because configuration never completed, rerun without `-b`.
5. Use the GitHub Actions architecture-specific cache only through the repository's workflow; local cache keys hash `deps/**`, and a prefix from a different commit/architecture is not authoritative.

### CMake finds Homebrew libraries instead of Orca's prefix

Confirm the configure command retains:

```text
-DCMAKE_IGNORE_PREFIX_PATH=/opt/local;/usr/local;/opt/homebrew
```

Delete only `build/arm64/CMakeCache.txt` and `build/arm64/CMakeFiles` if the cache is demonstrably poisoned, then reconfigure. Do not delete `deps/build/arm64/OrcaSlicer_dep`.

### A focused test is not found

Catch2 discovery occurs after the executable is linked. Build its target, then list tests:

```sh
cmake --build build/arm64 --config Release --target fff_print_tests
build/arm64/tests/fff_print/Release/fff_print_tests --list-tests
ctest --test-dir build/arm64 -C Release -N
```

Use the direct Catch2 binary while iterating; use CTest for the final registered-suite gate.

### Full test suite has unrelated `[NotWorking]` failures

The repository's own test runner excludes that label. Keep `-LE NotWorking`, but do not suppress any other failure. Save `build/arm64/ctest_results.xml` and the failing command/output.

### App builds but packaging fails at gettext

Verify `msgfmt` is from Homebrew gettext and rerun `./scripts/run_gettext.sh` directly. Fix actual format errors; do not bypass the localization gate. Then rerun the slicer script with `-b`.

### App bundle launches from `src/Release` but not from the packaged path

Inspect `Contents/Resources`. The raw build app may contain a source-tree symlink; the handoff app must have the real copied resource directory created by `build_release_macos.sh`. Rerun the packaging command rather than manually assembling the bundle.

### Native app works but a universal build is later required

Build the second architecture independently, retaining both app bundles:

```sh
./build_release_macos.sh -d -x -a x86_64 -t 11.3
./build_release_macos.sh -s -x -a x86_64 -c Release -t 11.3
./build_release_macos.sh -u -x -a universal -t 11.3
```

Expected universal artifact:

```text
build/universal/OrcaSlicer/OrcaSlicer.app
```

Verify every Mach-O that matters with `lipo -archs`; the script warns and retains arm64-only files when an x86_64 counterpart is absent. A local universal app is still unsigned/unnotarized unless separately signed.

### A distributable DMG is required

The CI workflow is the authoritative packaging reference. It builds both architectures, combines them, codesigns with hardened runtime/entitlements, creates a DMG with `hdiutil`, notarizes with `notarytool`, and staples the result. Those signing secrets are not present locally. For local-only testing, the native app bundle is the intended deliverable; do not weaken signing or claim public distributability.

## Final evidence checklist

Save the following with the implementation handoff:

- Exact commit/diff and `git diff --check` result.
- CMake configure summary showing `arm64`, Release, tests enabled, and the dependency prefix.
- Focused `[custom-nozzle]` output.
- Complete CTest command, exit status, and `build/arm64/ctest_results.xml`.
- `file`, `lipo -archs`, `plutil -lint`, and bundle structure results.
- Clean isolated-datadir launch result and relevant log path.
- Screenshots for physical/compatibility settings, warning, slice success, retained physical limit, and Send compatibility.
- Extracted 3MF metadata evidence proving `0.8` is device-facing and `1.57` survives project round-trip.
- Confirmation that no actual printer transmission occurred during the first verification pass, or a separately documented attended-device test if the user explicitly authorized one.
