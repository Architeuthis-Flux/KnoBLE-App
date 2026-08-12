# Knob settings channel (v0.2 design)

Goal: the Device tab's read-only firmware values become live controls — key
mapping, slide-pot mapping and sensitivity, detent/haptic tuning — over raw
HID, on USB and BLE, persisted on the knob. This recovers what VIA offered
on the wired KNOBs and replaces nothing about scrolling (v0.1's engine stays
as is).

## Transport: badjeff/zmk-hid-io (SHA-pinned), not a ZMK patch

zmk-hid-io (source-read 2026-08-12) adds its **own HID device** alongside
ZMK's: a vendor-usage-page descriptor with INPUT (device→host) and OUTPUT
(host→device) reports, wired on both transports — its own USB interface and
its own BLE HID service with a writable output-report characteristic
(`src/hid-io/hog.c`). KnoBLE already pins two badjeff modules the same way,
so this fits the repo's dependency policy. We enable `ZMK_HID_IO` +
`ZMK_HID_IO_OUTPUT` and speak a settings protocol over those reports; no
fork of ZMK, no descriptor surgery.

## Protocol sketch (TLV over reports)

Every message: `[cmd:u8] [seq:u8] [payload…]`, device echoes `seq` in its
reply so the host can pipeline.

| cmd | dir | meaning |
|---|---|---|
| 0x01 GET_INFO | h→d | reply: protocol ver, fw version, settings schema ver |
| 0x02 GET | h→d | payload: setting id → reply: id + value |
| 0x03 SET | h→d | payload: id + value → reply: status |
| 0x04 COMMIT | h→d | persist staged values to flash (NVS) |
| 0x05 RESET | h→d | back to compile-time defaults |

Setting ids (u8), values little-endian:

| id | setting | type |
|---|---|---|
| 0x10 | detents-per-rev (scroll profile) | u16 |
| 0x11 | lines-per-rev | u16 |
| 0x12 | wheel-scale-max | u8 |
| 0x13 | wheel-scale-min-div | u8 |
| 0x14 | slider-curve-power | u8 |
| 0x15 | slider role (none / wheel-scale / own-control) | u8 |
| 0x20 | haptic effect id (scroll profile) | u8 |
| 0x21 | haptic strength / enable | u8 |
| 0x30..0x32 | key 0..2 keycode (ZMK encoded u32, layer 0) | u32 |
| 0x38..0x3A | key 0..2 keycode, layer 1 | u32 |
| 0x40 | volume-layer detents-per-rev | u16 |

SET stages in RAM (applies immediately for feel-testing); COMMIT persists.
That gives free "try before save" semantics in the GUI.

## Firmware work (KnoBLE repo)

1. Pin zmk-hid-io in `config/west.yml` (SHA), enable in `knoble.conf`.
2. **knob-engine runtime settings struct**: today every profile value is a
   compile-time devicetree constant. Add a RAM `knob_settings` initialized
   from DT, overridden from Zephyr settings (NVS) at boot; the poll loop
   reads the struct instead of `DT_PROP`. This is the bulk of the work and
   is worth doing regardless of transport.
3. Settings protocol handler: parse OUTPUT reports, apply/stage, reply via
   INPUT reports. Key mapping = swap the keymap bindings' encoded keycodes
   at runtime (ZMK behavior: raise position events as usual; simplest v1 is
   a small custom behavior reading from `knob_settings`).
4. Keep ZMK Studio (`knoble_studio` build) as the escape hatch for full
   keymap editing; our channel covers the product knobs' 3 keys + engine
   dials, not arbitrary keymaps.

## Host work (this repo)

1. `KnobSettingsChannel` (IOKit HID): match the vendor usage page collection
   of the hid-io device, `IOHIDDeviceOpen` + `setReport`/input-report
   callback. **Needs the Input Monitoring permission** (second TCC prompt —
   onboarding copy needed). Windows later: same protocol via hidapi.
2. Device tab v2: report reads live values via GET; sections become
   editable — key pickers (curated keycode list: media, arrows, shortcuts),
   pot role + range sliders, detent/haptic controls; Apply = SET, Save =
   COMMIT, Defaults = RESET.

## Order of attack

1. Firmware: runtime settings struct + NVS (feel-testable via debug console
   before any HID work)
2. Firmware: hid-io + protocol handler
3. Host: channel + live report (GET only) — proves the pipe end to end
4. Host: editing UI (SET/COMMIT), keycode picker last
