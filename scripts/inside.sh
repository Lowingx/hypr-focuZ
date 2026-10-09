#!/bin/sh
# Run a program inside the nested Wayfire running the depthdeck demo.
#   scripts/inside.sh kitty -e btop
SOCK=$(ls "$XDG_RUNTIME_DIR" 2>/dev/null | sed -n 's/^wayfire-\(wayland-[0-9]*\)-\.socket$/\1/p' | head -1)
if [ -z "$SOCK" ]; then
    echo "nenhum wayfire rodando (socket wayfire-*.socket nao encontrado)" >&2
    exit 1
fi
exec env WAYLAND_DISPLAY="$SOCK" "$@"
