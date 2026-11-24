#!/usr/bin/env bash
# Souls of Harkinian: install the preset and download the third-party packs it uses.
#
#   bash install.sh --soh "/path/to/your/SoH folder" [--no-skins]
#   curl -fsSL https://raw.githubusercontent.com/iretonsean/souls-of-harkinian/souls/souls/install.sh | bash -s -- --soh "/path/to/SoH"
#
# The SoH folder is the one that holds shipofharkinian.json (it is created the first time SoH runs).
# Packs come from GameBanana, the authors' own pages; nothing is redistributed by this repository.
# Weapon skins go into <SoH>/souls-skins/<weapon>/, not into mods/, so they never replace the original swords.
# Needs: curl, unzip, python3; 7z (p7zip) for packs shipped as .7z.
set -euo pipefail

RAW="https://raw.githubusercontent.com/iretonsean/souls-of-harkinian/souls/souls"
SOH=""
SKINS=1
while [ $# -gt 0 ]; do
  case "$1" in
    --soh) SOH="$2"; shift 2 ;;
    --no-skins) SKINS=0; shift ;;
    -h|--help) sed -n 2,12p "$0" 2>/dev/null || true; exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
done

if [ -z "$SOH" ]; then
  for d in "$HOME/.local/share/soh" "$HOME/Library/Application Support/com.shipofharkinian.soh" "$PWD"; do
    [ -f "$d/shipofharkinian.json" ] && SOH="$d" && break
  done
fi
if [ -z "$SOH" ] || [ ! -f "$SOH/shipofharkinian.json" ]; then
  echo "Can't find your SoH folder. Start SoH once, close it, then run: bash install.sh --soh \"/path/to/the folder with shipofharkinian.json\"" >&2
  exit 1
fi
for tool in curl unzip python3; do
  command -v "$tool" >/dev/null || { echo "Missing $tool. Install it and run again." >&2; exit 1; }
done

# Files from this repository: local checkout if run from one, otherwise GitHub
HERE="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" 2>/dev/null && pwd || true)"
fetch_repo_file() { # <path in souls/> <dest>
  if [ -n "$HERE" ] && [ -f "$HERE/$1" ]; then cp "$HERE/$1" "$2"; else curl -fsSL "$RAW/$(printf '%s' "$1" | sed 's/ /%20/g')" -o "$2"; fi
}

echo "SoH folder: $SOH"
mkdir -p "$SOH/presets"
fetch_repo_file "presets/Souls of Harkinian.json" "$SOH/presets/Souls of Harkinian.json"
echo "Preset installed: apply it in SoH under Settings > Presets > Souls of Harkinian."

if [ "$SKINS" = 0 ]; then
  echo "Skipped weapon skins (--no-skins)."
  exit 0
fi

WORK="$SOH/.souls-download"
rm -rf "$WORK"; mkdir -p "$WORK"
fetch_repo_file "dependencies.json" "$WORK/dependencies.json"
python3 - "$WORK/dependencies.json" > "$WORK/list.tsv" <<'PY'
import json, sys
for p in json.load(open(sys.argv[1]))["packs"]:
    print("\t".join([p["slug"], str(p["file"]), p["weapon"], p["name"], p["author"], str(p["gamebanana"])]))
PY

ok=0; failed=0
while IFS=$'\t' read -r slug file weapon name author gb; do
  dest="$SOH/souls-skins/$slug"
  arc="$WORK/$slug.pack"
  if ! curl -fsSL --retry 2 "https://gamebanana.com/dl/$file" -o "$arc"; then
    echo "  FAILED to download: $name (https://gamebanana.com/mods/$gb)"; failed=$((failed + 1)); continue
  fi
  out="$WORK/$slug"; mkdir -p "$out"
  case "$(head -c 4 "$arc" | od -An -tx1 | tr -d ' ')" in
    504b*) unzip -oq "$arc" -d "$out" ;;
    377abcaf) command -v 7z >/dev/null || { echo "  Needs 7z (p7zip) for $name"; failed=$((failed + 1)); continue; }
              7z x -y -o"$out" "$arc" >/dev/null ;;
    *) echo "  Unknown archive type: $name"; failed=$((failed + 1)); continue ;;
  esac
  rm -rf "$dest"; mkdir -p "$dest"
  found=0
  while IFS= read -r -d '' f; do cp "$f" "$dest/"; found=1; done < <(find "$out" -type f \( -iname '*.o2r' -o -iname '*.otr' \) -print0)
  if [ "$found" = 1 ]; then
    printf '%s\nby %s\nhttps://gamebanana.com/mods/%s\n' "$name" "$author" "$gb" > "$dest/SOURCE.txt"
    echo "  $weapon  <-  $name by $author"; ok=$((ok + 1))
  else
    echo "  No .o2r/.otr inside: $name"; failed=$((failed + 1))
  fi
done < "$WORK/list.tsv"
rm -rf "$WORK"
echo "Weapon skins: $ok installed, $failed failed. They live in $SOH/souls-skins and are used by the Souls build only."
