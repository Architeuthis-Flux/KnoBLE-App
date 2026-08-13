# Knob (companion app)

Cross-platform companion app for [BaselineDesign KNOB](https://github.com/BaselineDesign/BaselineDesign-Knob)
devices. Two jobs:

1. **Rock-solid scrolling** — reads the knob's rotation as raw HID counts
   (before the OS can apply any acceleration curve) and re-emits it as
   smooth, strictly-linear continuous scrolling. No OS acceleration, no
   inertia beyond the knob's own flywheel. Only the knob is affected —
   other mice are untouched.
2. **Configuring the knob** — remap the keys from the Keys tab over a
   raw-HID settings channel (QMK-style, USB), with changes staged live and
   saved to the knob's flash. Detents/haptics/pot dials come next.

Status: **v0.3 — macOS (tested on hardware) + Windows (builds in CI,
⚠️ not yet hardware-tested)**. The Windows app is deliberately
configurator-only: Windows honors the hi-res wheel firmware natively, so a
host scroll engine would only duplicate what the OS already does well.
Linux likewise needs no app for scrolling.

**📦 Download: [Releases](https://github.com/Architeuthis-Flux/KnoBLE-App/releases)** — `Knob.dmg` (macOS) and `Knob-windows.zip` (Windows, unzip + run `Knob.exe`).

## Setup per platform

| Platform | Scrolling | Keys & pot config |
|---|---|---|
| macOS | this app (grant Accessibility + Input Monitoring on first run — the app walks you through it) | this app, knob plugged in over USB |
| Windows | flash the `knoble_hires` firmware — smooth natively, zero host software | this app (tray + Controls/Device tabs), knob over USB |
| Linux | flash `knoble_hires` — same, zero software | *(CLI/GUI planned; protocol is plain hidapi)* |

Mappings live **on the knob**, so keys/pot configured on any OS carry to
every OS.

## How the smoothing works

The knob reports exact rotation quanta (wheel counts). The app never guesses
intent from event rates the way generic smoothers must: it accumulates counts
into a pixel *position target* and animates the page toward it with a
first-order lag (`response` = time constant). Distance scrolled always equals
rotation turned. The macOS backend does this with a session CGEventTap that
swallows only the knob's scroll events (matched per-event via the HID sender
ID) and re-posts phase-annotated continuous pixel scrolling at 120 Hz —
the same event shape a trackpad produces, which is why pages glide.

## Build

**macOS:**

```bash
brew install qt cmake ninja
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build
open build/KnobApp.app
```

**Windows** (Qt 6 + MSVC; links `hid.lib`/`setupapi.lib`, no other deps):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
windeployqt build/Release/KnobApp.exe
```

**CI**: `.github/workflows/build.yml` builds both on every push — a
`Knob-windows` folder artifact (exe + Qt runtime via windeployqt) and an
unsigned `Knob-macos` DMG. For release-grade macOS artifacts build locally:
`packaging/make-dmg.sh "Developer ID Application: …"` (bundles Qt, signs,
makes `Knob.dmg`; notarize after — instructions in the script).

First run: grant **Accessibility** to “Knob” in System Settings → Privacy &
Security (the app prompts). The app lives in the menu bar (no Dock icon):
knob glyph, filled center dot when the knob is connected.

⚠️ If LinearMouse is running with a scheme for this knob, remove that block
from `~/.config/linearmouse/linearmouse.json` (or quit LinearMouse) — two
transformers on the same device compound. The Settings window warns if it
detects this.

## Settings

| Control | Meaning |
|---|---|
| Speed | pixels per wheel count (48 counts/rev at the knob's ×1) |
| Response | catch-up time constant; lower = glued tighter, higher = softer glide |
| Invert direction | flip scroll direction for the knob only |
| Report gesture phases | tag events as trackpad-style gestures (enables overscroll bounce in apps); disable if any app misbehaves |

All changes apply live — turn the knob while dragging a slider.

## Repo layout

```
src/engine/KnobScrollModel.h   portable scroll model (no OS deps)
src/engine/ScrollEngine.h      per-OS backend interface
src/engine/mac/                CGEventTap + raw-HID backend (Objective-C++)
src/app/                       Qt tray + settings GUI (cross-platform)
src/app/KnobHidChannel.h       settings-channel interface (per-OS impls)
src/app/mac/                   IOHIDManager channel + native NSStatusItem tray
src/app/win/                   SetupAPI/hid.dll channel (⚠️ untested on hardware)
packaging/                     make-dmg.sh (bundle Qt, sign, DMG)
```

### Windows status, honestly

The Windows build compiles in CI and uses only bedrock Win32 APIs
(SetupAPI enumeration, overlapped `ReadFile`/`WriteFile` on the vendor HID
collection), but it has **not been run against hardware yet**. First
tester: grab the `Knob-windows` CI artifact, plug the knob in over USB,
and the tray icon + Controls tab should find it within ~2 s. If it
doesn't, the failure is almost certainly in device matching — file an
issue with the output of Device Manager → knob → Hardware IDs.

The scroll-feel research this implements lives in the
[KnoBLE repo](https://github.com/Architeuthis-Flux/KnoBLE)
(`docs/scroll-feel-handoff.md`), including why firmware-only and
LinearMouse-based approaches fall short. LinearMouse (MIT) served as the
reference for the macOS event synthesis recipe.
