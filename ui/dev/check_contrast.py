#!/usr/bin/env python3
"""WCAG 2.x contrast check of the colour pairs the UI uses (docs/accessibility.md).

Reads the light and dark palettes from ui/Theme.qml (`readonly property color
name: dark ? "#dark" : "#light"`) and checks each foreground/background pair
against its minimum: 4.5:1 for text, 3:1 for focus indicators, control
outlines and status glyphs. Exits non-zero when a pair fails.

    python3 ui/dev/check_contrast.py [path/to/Theme.qml]
"""
import os
import re
import sys


def parse(path):
    light, dark = {}, {}
    pat = re.compile(r'readonly property color (\w+):\s*'
                     r'(?:dark \? "(#[0-9a-fA-F]{6,8})" : "(#[0-9a-fA-F]{6,8})"|"(#[0-9a-fA-F]{6,8})")')
    for line in open(path, encoding="utf-8"):
        m = pat.search(line)
        if not m:
            continue
        name = m.group(1)
        if m.group(4):
            light[name] = dark[name] = m.group(4)
        else:
            dark[name] = m.group(2)
            light[name] = m.group(3)
    return light, dark


def rgb(h):
    h = h.lstrip("#")
    if len(h) == 8:  # #AARRGGBB
        h = h[2:]
    return tuple(int(h[i:i + 2], 16) / 255 for i in (0, 2, 4))


def lum(h):
    def ch(c):
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (ch(c) for c in rgb(h))
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def ratio(a, b):
    la, lb = lum(a), lum(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


# (foreground, background, minimum, what)
TEXT = 4.5
UI = 3.0
PAIRS = [
    ("text", "base", TEXT, "body text on list"),
    ("text", "pane", TEXT, "body text on panes"),
    ("text", "window", TEXT, "body text on window"),
    ("text", "selection", TEXT, "row text on selection"),
    ("text", "selectionInactive", TEXT, "row text on inactive selection"),
    ("text", "alternateBase", TEXT, "row text on alternate row"),
    ("text", "hover", TEXT, "row text on hover"),
    ("text", "header", TEXT, "header text"),
    ("text", "button", TEXT, "button text"),
    ("text", "buttonHover", TEXT, "button text hovered"),
    ("textMuted", "base", TEXT, "muted text on list"),
    ("textMuted", "pane", TEXT, "muted text on panes"),
    ("textMuted", "alternateBase", TEXT, "muted text on alternate row"),
    ("textMuted", "selection", TEXT, "muted text on selection"),
    ("textMuted", "selectionInactive", TEXT, "muted text on inactive selection"),
    ("textMuted", "header", TEXT, "column titles"),
    ("textMuted", "hover", TEXT, "muted text on hover"),
    ("textFaint", "pane", TEXT, "faint text on panes (informational uses)"),
    ("textFaint", "base", TEXT, "faint text / placeholder on list"),
    ("textFaint", "input", TEXT, "placeholder text in inputs"),
    ("accentText", "accent", TEXT, "accent button label"),
    ("accentText", "accentHover", TEXT, "accent button hovered"),
    ("link", "pane", TEXT, "link on pane"),
    ("link", "base", TEXT, "link on base"),
    ("tooltipText", "tooltip", TEXT, "tool tip"),
    ("success", "pane", TEXT, "success text"),
    ("warning", "pane", TEXT, "warning text on pane"),
    ("warning", "base", TEXT, "warning text on list"),
    ("warning", "alternateBase", TEXT, "warning text on alternate row"),
    ("warning", "selection", TEXT, "warning text on selection"),
    ("danger", "pane", TEXT, "danger text on pane"),
    ("danger", "base", TEXT, "danger status text on list"),
    ("danger", "alternateBase", TEXT, "danger status on alternate row"),
    ("danger", "selection", TEXT, "danger status on selection"),
    ("info", "base", TEXT, "info status text on list"),
    ("info", "selection", TEXT, "info status text on selection"),
    ("text", "warningBg", TEXT, "banner text (warning)"),
    ("text", "dangerBg", TEXT, "banner text (error)"),
    ("text", "infoBg", TEXT, "banner text (info)"),
    ("link", "infoBg", TEXT, "link in info banner"),
    ("link", "warningBg", TEXT, "link in warning banner"),
    ("link", "dangerBg", TEXT, "link in error banner"),
    ("focus", "base", UI, "focus ring on list"),
    ("focus", "pane", UI, "focus ring on pane"),
    ("focus", "window", UI, "focus ring on window"),
    ("focus", "selection", UI, "focus ring on selected row"),
    ("focus", "selectionInactive", UI, "focus ring on inactive selection"),
    ("focus", "input", UI, "focus border on input"),
    ("focus", "button", UI, "focus ring next to button"),
    ("focus", "infoBg", UI, "focus ring in info banner"),
    ("focus", "warningBg", UI, "focus ring in warning banner"),
    ("controlBorder", "input", UI, "check box / radio / text input outline"),
    ("controlBorder", "base", UI, "check box outline on list"),
    ("controlBorder", "pane", UI, "check box / radio outline on pane"),
    ("controlBorder", "alternateBase", UI, "check box outline on alternate row"),
    ("controlBorder", "selection", UI, "check box outline on selected row"),
    ("controlBorder", "hover", UI, "check box outline on hovered row"),
    ("accentText", "controlChecked", UI, "check mark on checked box"),
    ("controlChecked", "base", UI, "checked check box on list"),
    ("controlChecked", "pane", UI, "checked check box on pane"),
    ("controlChecked", "alternateBase", UI, "checked check box on alternate row"),
    ("controlChecked", "selection", UI, "checked check box on selection"),
    ("controlChecked", "hover", UI, "checked check box on hovered row"),
    ("accent", "pane", UI, "accent button on pane"),
    ("accent", "base", UI, "accent glyphs on list"),
    ("success", "base", UI, "status glyph ready"),
    ("warning", "base", UI, "status glyph warning"),
    ("danger", "base", UI, "status glyph problem"),
    ("textMuted", "base", UI, "pending glyph"),
]


def main():
    default = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Theme.qml")
    light, dark = parse(sys.argv[1] if len(sys.argv) > 1 else default)
    fails = 0
    for theme, pal in (("light", light), ("dark", dark)):
        print(f"--- {theme}")
        for fg, bg, minimum, what in PAIRS:
            r = ratio(pal[fg], pal[bg])
            ok = r >= minimum
            fails += not ok
            print(f"{'ok  ' if ok else 'FAIL'} {r:5.2f} (>= {minimum}) {fg:>14} {pal[fg]} on {bg:<17} "
                  f"{pal[bg]}  {what}")
    print(f"{fails} failing pairs")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
