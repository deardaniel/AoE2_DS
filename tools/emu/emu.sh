#!/bin/bash
# Drive a private melonDS instance (Windows build, from WSL) for testing.
# It is tracked by process ID, so other melonDS windows are never touched and
# input is posted to its window without taking focus.
#
#   tools/emu/emu.sh start [seconds]      (re)launch with the current ROM, wait for boot
#   tools/emu/emu.sh shot <name>          capture -> .screenshots/<name>_top.png (768x576)
#                                         and .screenshots/<name>_bottom.png (256x177, native px;
#                                         the capture cuts off the last 15 rows)
#   tools/emu/emu.sh tap X Y              touch the bottom screen at pixel X,Y (0-255, 0-191)
#   tools/emu/emu.sh drag X Y X2 Y2       touch-drag
#   tools/emu/emu.sh key NAME             press a button: A B X Y START SELECT L R
#                                         UP DOWN LEFT RIGHT, FF (toggle fast-forward),
#                                         or a Windows virtual-key code
#   tools/emu/emu.sh stop
#
# Uses its own copy of melonDS in C:\Users\me\Downloads\aoe2_test_emu (exe +
# melonDS.toml), so it can have its own key bindings and be muted without
# touching the emulator Daniel plays in. The bindings this script expects, in
# that folder's [Instance0.Keyboard]: A=68 B=83 X=87 Y=65 Start=84 Select=71
# L=81 R=69 HK_FastForwardToggle=70; [Instance0.Audio] Volume=0.
# Assumes 150% display scaling and the default window size (screens stacked,
# 2x logical).
set -e
PS=/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/.screenshots"
WIN_TOOLS='\\wsl.localhost\Ubuntu'"$(echo "$ROOT/tools/emu" | tr / '\\')"
WIN_OUT='\\wsl.localhost\Ubuntu'"$(echo "$OUT" | tr / '\\')"
PIDFILE="$OUT/melon.pid"
mkdir -p "$OUT"
PY="$ROOT/.venv/bin/python"

case "$1" in
start)
    OLD=$(cat "$PIDFILE" 2>/dev/null || true)
    # melonDS reads the ROM from a Windows path; a copy keeps rebuilds from racing it
    cp "$ROOT/aoe2_dsi.nds" /mnt/c/Users/me/Downloads/aoe2_test_emu/aoe2_dsi_test.nds
    $PS -NoProfile -Command "if ('$OLD' -ne '') { Stop-Process -Id $OLD -Force -ErrorAction SilentlyContinue }; Start-Sleep -Milliseconds 500; \$p = Start-Process 'C:\Users\me\Downloads\aoe2_test_emu\melonDS.exe' -WorkingDirectory 'C:\Users\me\Downloads\aoe2_test_emu' -ArgumentList 'C:\Users\me\Downloads\aoe2_test_emu\aoe2_dsi_test.nds' -PassThru; \$p.Id" 2>/dev/null | tr -d '\r\n ' > "$PIDFILE"
    timeout "${2:-7}" tail -f /dev/null || true
    ;;
stop)
    $PS -NoProfile -Command "Stop-Process -Id $(cat "$PIDFILE") -Force -ErrorAction SilentlyContinue" >/dev/null 2>&1 || true
    rm -f "$PIDFILE"
    ;;
shot)
    $PS -NoProfile -ExecutionPolicy Bypass -File "$WIN_TOOLS\\capture_pid.ps1" -ProcId "$(cat "$PIDFILE")" -Out "$WIN_OUT\\raw.png" >/dev/null
    "$PY" - "$OUT" "$2" <<'PYEOF'
import sys
from PIL import Image
out, name = sys.argv[1], sys.argv[2]
im = Image.open(f'{out}/raw.png').convert('RGB')
top = 83  # title bar + menu, physical px at 150% scaling
im.crop((1, top, 769, top + 576)).save(f'{out}/{name}_top.png')
im.crop((1, top + 576, 769, im.height)).resize((256, (im.height - top - 576) // 3), Image.NEAREST).save(f'{out}/{name}_bottom.png')
PYEOF
    ;;
tap)
    $PS -NoProfile -ExecutionPolicy Bypass -File "$WIN_TOOLS\\input_pid.ps1" -ProcId "$(cat "$PIDFILE")" -Action tap -X $(($2*2+1)) -Y $((409+$3*2+1)) >/dev/null
    ;;
drag)
    $PS -NoProfile -ExecutionPolicy Bypass -File "$WIN_TOOLS\\input_pid.ps1" -ProcId "$(cat "$PIDFILE")" -Action drag -X $(($2*2+1)) -Y $((409+$3*2+1)) -X2 $(($4*2+1)) -Y2 $((409+$5*2+1)) >/dev/null
    ;;
key)
    case "$2" in
        A) VK=68 ;; B) VK=83 ;; X) VK=87 ;; Y) VK=65 ;;
        START) VK=84 ;; SELECT) VK=71 ;; L) VK=81 ;; R) VK=69 ;; FF) VK=70 ;;
        LEFT) VK=37 ;; UP) VK=38 ;; RIGHT) VK=39 ;; DOWN) VK=40 ;;
        *) VK="$2" ;;
    esac
    $PS -NoProfile -ExecutionPolicy Bypass -File "$WIN_TOOLS\\input_pid.ps1" -ProcId "$(cat "$PIDFILE")" -Action key -Key "$VK" >/dev/null
    ;;
*)
    sed -n 2,16p "$0"; exit 1 ;;
esac
