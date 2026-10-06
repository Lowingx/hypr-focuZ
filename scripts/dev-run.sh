#!/usr/bin/env bash
# Build depthdeck, install it for the current user, then run Wayfire *nested*
# inside the current session. Safe dev loop: the host session is never touched.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v wayfire >/dev/null 2>&1; then
    echo "wayfire não está instalado. Rode primeiro:" >&2
    echo "    sudo pacman -S wayfire" >&2
    exit 1
fi

if ! meson setup "$root/build" --buildtype=debug >/dev/null 2>&1; then
    # setup already done on previous runs
    test -d "$root/build" || { echo "meson setup falhou" >&2; exit 1; }
fi
meson compile -C "$root/build"

datadir="${XDG_DATA_HOME:-$HOME/.local/share}/wayfire"
install -d "$datadir/plugins" "$datadir/metadata"
install -m644 "$root/build/libdepthdeck.so" "$datadir/plugins/libdepthdeck.so"
install -m644 "$root/metadata/depthdeck.xml" "$datadir/metadata/depthdeck.xml"

echo "→ plugin instalado em $datadir/plugins/libdepthdeck.so"
echo "→ abrindo Wayfire aninhado (feche a janela para encerrar)"
exec wayfire --config "$root/wayfire.ini"
