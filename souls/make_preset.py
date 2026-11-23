#!/usr/bin/env python3
"""Build the "Souls of Harkinian" SoH preset from a shipofharkinian.json.

usage: make_preset.py <shipofharkinian.json> <out.json>

Keeps the source config's gameplay settings and enhancements, drops UI state and mod-file names,
and writes the Souls controller layout (see soh/soh/Enhancements/controls/SoulsInput.cpp).
"""
import copy, json, sys

# N64 button bits
A, B, Z, START = 0x8000, 0x4000, 0x2000, 0x1000
DUP, DDOWN, DLEFT, DRIGHT = 0x0800, 0x0400, 0x0200, 0x0100
MOD1, L, R = 0x0040, 0x0020, 0x0010
CUP, CDOWN, CLEFT, CRIGHT = 0x0008, 0x0004, 0x0002, 0x0001
# SDL game controller buttons: 0 Cross, 1 Circle, 2 Square, 3 Triangle, 6 Options, 7 L3, 8 R3, 9 L1, 10 R1,
# 11-14 D-pad up/down/left/right; axes 4+ = L2, 5+ = R2
LAYOUT = {
    A: [("b", 3), ("b", 1)],      # Triangle interact, Circle roll (Circle = back in menus)
    B: [("b", 10), ("a", 5, 1)],  # R1 / R2 sword
    R: [("b", 9), ("b", 7)],      # L1 shield, L3 shield crouch
    L: [("b", 0)],                # Cross: jump in play, confirm in menus
    Z: [("a", 4, 1), ("b", 8)],   # L2 / R3 lock-on
    START: [("b", 6)],            # Options
    DUP: [("b", 11)], DDOWN: [("b", 12)], DLEFT: [("b", 13)], DRIGHT: [("b", 14)],  # C buttons in play, cursor in menus
    CLEFT: [("b", 2)],            # Square item
    CUP: [], CDOWN: [], CRIGHT: [],
    MOD1: [("b", 1)],             # Circle held = sprint (speed modifier)
}

def apply_layout(ctrl):
    bm = ctrl.setdefault("ButtonMappings", {})
    ids = ctrl.setdefault("Port1", {}).setdefault("Buttons", {})
    for mask, srcs in LAYOUT.items():
        key = f"{mask}ButtonMappingIds"
        old = [i for i in dict.fromkeys(ids.get(key, "").split(",")) if i]
        for i in old:
            if "-SDL" in i:
                bm.pop(i, None)
        kept = [i for i in old if "-SDL" not in i]
        for s in srcs:
            if s[0] == "b":
                name = f"P0-B{mask}-SDLB{s[1]}"
                bm[name] = {"Bitmask": mask, "ButtonMappingClass": "SDLButtonToButtonMapping", "SDLControllerButton": s[1]}
            else:
                name = f"P0-B{mask}-SDLA{s[1]}-AD{'P' if s[2] > 0 else 'N'}"
                bm[name] = {"AxisDirection": s[2], "Bitmask": mask, "ButtonMappingClass": "SDLAxisDirectionToButtonMapping",
                            "SDLControllerAxis": s[1]}
            kept.append(name)
        ids[key] = ",".join(kept) + ","
    ctrl["Port1"]["HasConfig"] = 1

def main(src, out):
    cfg = json.load(open(src)); cv = cfg["CVars"]
    settings = copy.deepcopy(cv.get("gSettings", {}))
    for k in ("Menu", "EnabledMods"):  # UI state and local mod file names
        settings.pop(k, None)
    settings["DPadOnPause"] = 1
    settings["DpadInText"] = 1
    settings["DpadHoldChange"] = 1
    apply_layout(settings.setdefault("Controllers", {}))
    enh = copy.deepcopy(cv.get("gEnhancements", {}))
    enh["SoulsControls"] = 1
    enh["DpadEquips"] = 0  # the D-pad is the C buttons in play
    cheats = copy.deepcopy(cv.get("gCheats", {}))
    cheats["MoonJumpOnL"] = 0  # Cross is the built-in jump
    preset = {
        "presetName": "Souls of Harkinian",
        "fileType": 2,  # FILE_TYPE_PRESET
        "ConfigVersion": cfg.get("ConfigVersion", 7),
        "blockStrategy": {"settings": "merge", "enhancements": "merge"},
        "blocks": {"settings": {"gSettings": settings}, "enhancements": {"gEnhancements": enh, "gCheats": cheats}},
    }
    json.dump(preset, open(out, "w"), indent=4)
    print("wrote", out)

if __name__ == "__main__":
    main(*sys.argv[1:3])
