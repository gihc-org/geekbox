#!/bin/bash
# Snapshot-loop: fanger skærm-tilstand (root + fb0 + vindue-attributter) hvert
# 4. sekund i N sekunder, mens spillet kører. Brug til at finde præcis hvornår
# præsentationen til skærmen knækker (GPU-proces-genstart / DeviceReset).
#
#   capture_game_black.sh <sekunder> <outdir>
# Eksempel: bash capture_game_black.sh 240 /tmp/game_snaps
set -u

SECS="${1:-240}"
OUT="${2:-/tmp/game_snaps}"
mkdir -p "$OUT"

LOG="/tmp/ff_game2.log"
END=$(( $(date +%s) + SECS ))
N=0
while [ "$(date +%s)" -lt "$END" ]; do
    TS=$(date +%H%M%S)
    DISPLAY=:0 /tmp/xdump > "$OUT/root_$TS.raw" 2>/dev/null
    dd if=/dev/fb0 of="$OUT/fb_$TS.raw" bs=4096 count=1013 2>/dev/null
    {
        echo "== $TS =="
        DISPLAY=:0 xwininfo -root -tree 2>/dev/null | grep -E "Navigator|0x100005|0x100003|0xe006" | head -6
        WID=$(DISPLAY=:0 xwininfo -root -tree 2>/dev/null | grep '"Navigator"' | grep -oE '0x[0-9a-f]+' | head -1)
        [ -n "$WID" ] && DISPLAY=:0 xwininfo -id "$WID" -all 2>/dev/null | grep -E "Map State|Depth|Visual|Colormap|Backing|Override|Corners" | head -8
        grep -a "DeviceReset" "$LOG" | tail -1
        grep -aoE "x11ws: present #[0-9]+" "$LOG" | tail -1
    } > "$OUT/state_$TS.txt"
    N=$((N+1))
    sleep 4
done
echo "færdig: $N snapshots i $OUT"
