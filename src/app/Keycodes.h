#pragma once

#include <QString>
#include <QVector>
#include <cstdint>

// Curated keycode palette for the key pickers — the "standard QMK stuff":
// media transport, volume, navigation, editing, and the F13–F24 block that
// exists for user shortcuts. Encodings are ZMK's: (usage page << 16) | id,
// consumer page 0x0C, keyboard page 0x07 — identical HID usages to QMK's
// KC_/heat codes, so the labels line up with what KNOB users already know.
struct Keycode {
    const char *label;
    const char *category;
    uint32_t encoded;
};

inline const QVector<Keycode> &keycodeTable() {
    static const QVector<Keycode> table = {
        // Modifiers (keyboard page 0x07): held while the key is held — the
        // natural pairing with the knob (⌘+scroll = zoom, ⇧+scroll = pan).
        {"Command ⌘ (Win key)", "Modifiers", 0x000700E3},
        {"Shift ⇧", "Modifiers", 0x000700E1},
        {"Control ⌃", "Modifiers", 0x000700E0},
        {"Option ⌥ (Alt)", "Modifiers", 0x000700E2},

        // Media (consumer page 0x0C)
        {"Play / Pause", "Media", 0x000C00CD},
        {"Next Track", "Media", 0x000C00B5},
        {"Previous Track", "Media", 0x000C00B6},
        {"Stop", "Media", 0x000C00B7},
        {"Mute", "Media", 0x000C00E2},
        {"Volume Up", "Media", 0x000C00E9},
        {"Volume Down", "Media", 0x000C00EA},
        {"Brightness Up", "Media", 0x000C006F},
        {"Brightness Down", "Media", 0x000C0070},

        // Navigation (keyboard page 0x07)
        {"Left Arrow", "Navigation", 0x00070050},
        {"Right Arrow", "Navigation", 0x0007004F},
        {"Up Arrow", "Navigation", 0x00070052},
        {"Down Arrow", "Navigation", 0x00070051},
        {"Page Up", "Navigation", 0x0007004B},
        {"Page Down", "Navigation", 0x0007004E},
        {"Home", "Navigation", 0x0007004A},
        {"End", "Navigation", 0x0007004D},

        // Editing
        {"Space", "Editing", 0x0007002C},
        {"Enter", "Editing", 0x00070028},
        {"Escape", "Editing", 0x00070029},
        {"Tab", "Editing", 0x0007002B},
        {"Backspace", "Editing", 0x0007002A},
        {"Delete", "Editing", 0x0007004C},

        // F13–F24: the classic "spare keys for app shortcuts" block
        {"F13", "Function", 0x00070068},
        {"F14", "Function", 0x00070069},
        {"F15", "Function", 0x0007006A},
        {"F16", "Function", 0x0007006B},
        {"F17", "Function", 0x0007006C},
        {"F18", "Function", 0x0007006D},
        {"F19", "Function", 0x0007006E},
        {"F20", "Function", 0x0007006F},
        {"F21", "Function", 0x00070070},
        {"F22", "Function", 0x00070071},
        {"F23", "Function", 0x00070072},
        {"F24", "Function", 0x00070073},
    };
    return table;
}

inline QString keycodeLabelFor(uint32_t encoded) {
    for (const auto &kc : keycodeTable()) {
        if (kc.encoded == encoded) {
            return QString::fromUtf8(kc.label);
        }
    }
    return QStringLiteral("0x%1").arg(encoded, 8, 16, QLatin1Char('0'));
}
