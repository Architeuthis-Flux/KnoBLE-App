# Knob (companion app)

Cross-platform companion app for [BaselineDesign KNOB](https://github.com/BaselineDesign/BaselineDesign-Knob)
devices. Two jobs:

1. **Rock-solid scrolling** — intercepts the knob's discrete mouse-wheel
   events and re-emits them as smooth, strictly-linear continuous scrolling.
   No OS acceleration, no inertia beyond the knob's own flywheel, no more
   multi-line hops at slow speeds. Replaces the LinearMouse setup for the
   knob (and only the knob — other mice are untouched).
2. **Configuring the knob** *(planned, v0.2)* — runtime controls for detent
   count, speed range, and haptic strength over a raw-HID settings channel,
   recovering what VIA offered on the wired KNOBs.

Status: **v0.1, macOS backend**. Windows backend planned (low-level mouse
hook + SendInput). Linux intentionally has no backend — flash the
`knoble_hires` firmware instead: libinput honors the HID Resolution
Multiplier natively, zero host software needed.

## How the smoothing works

The knob reports exact rotation quanta (wheel counts). The app never guesses
intent from event rates the way generic smoothers must: it accumulates counts
into a pixel *position target* and animates the page toward it with a
first-order lag (`response` = time constant). Distance scrolled always equals
rotation turned. The macOS backend does this with a session CGEventTap that
swallows only the knob's scroll events (matched per-event via the HID sender
ID) and re-posts phase-annotated continuous pixel scrolling at 120 Hz —
the same event shape a trackpad produces, which is why pages glide.

## Build (macOS)

```bash
brew install qt cmake ninja
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build
open build/KnobApp.app
```

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
src/engine/mac/                CGEventTap backend (Objective-C++)
src/app/                       Qt tray + settings GUI
```

The scroll-feel research this implements lives in the
[KnoBLE repo](https://github.com/BaselineDesign/KnoBLE)
(`docs/scroll-feel-handoff.md`), including why firmware-only and
LinearMouse-based approaches fall short. LinearMouse (MIT) served as the
reference for the macOS event synthesis recipe.
