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
MOD2, MOD1, L, R = 0x0080, 0x0040, 0x0020, 0x0010
CUP, CDOWN, CLEFT, CRIGHT = 0x0008, 0x0004, 0x0002, 0x0001
# SDL game controller buttons: 0 Cross, 1 Circle, 2 Square, 3 Triangle, 4 Create, 5 PS, 6 Options, 7 L3, 8 R3, 9 L1,
# 10 R1, 11-14 D-pad up/down/left/right, 20 touchpad; axes 4+ = L2, 5+ = R2.
# Elden Ring layout (souls/ER-SYSTEM.md): SoulsInput.cpp turns these N64 buttons into the play and menu actions.
LAYOUT = {
    A: [("b", 3), ("b", 1)],      # Triangle interact / pouch; Circle (with Modifier 1) roll, back in menus
    B: [("b", 10)],               # R1 right hand
    CRIGHT: [("a", 5, 1)],        # R2 right hand strong attack (tap = jump attack, hold = spin)
    R: [("b", 9)],                # L1 left hand (tools, catalyst)
    CDOWN: [("a", 4, 1)],         # L2 guard
    CLEFT: [("b", 2)],            # Square quick item
    CUP: [("b", 7), ("b", 4)],    # L3 / Create: first person, Navi
    Z: [("b", 8)],                # R3 lock-on
    L: [("b", 20), ("b", 5)],     # touchpad (PS button too): minimap
    START: [("b", 6)],            # Options
    DUP: [("b", 11)], DDOWN: [("b", 12)], DLEFT: [("b", 13)], DRIGHT: [("b", 14)],  # cycle slots; cursor in menus
    MOD1: [("b", 1)],             # Circle
    MOD2: [("b", 0)],             # Cross: jump in play, confirm in menus, A note on the ocarina
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
    enh["SoulsPerFileLook"] = 1  # each save file keeps its own look
    enh["SoulsCreator"] = 1  # character creator before the name entry of a new file
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
