#!/bin/bash
# Mål skærmopdateringer (root + fb0 + Firefox-vindue) hvert 4. sekund under
# en Firefox-kørsel og byg en tidslinje over præsentationen. Diff'er hver
# fil mod forrige snapshot (16-bpp root/fb0, 32-bpp vindue) med /tmp/rootdiff.
#
# Kør (root på boksen), fx:
#   bash /tmp/capture_stress.sh 320 /tmp/stress_snaps /tmp/ff_stress.log
set -u

SECS="${1:-320}"
OUT="${2:-/tmp/stress_snaps}"
LOG="${3:-/tmp/ff_stress.log}"
W="${4:-1920}"
H="${5:-1080}"
mkdir -p "$OUT"

END=$(( $(date +%s) + SECS ))
PREV_ROOT=""; PREV_FB=""; PREV_WIN=""
N=0
while [ "$(date +%s)" -lt "$END" ]; do
    TS=$(date +%H%M%S)
    DISPLAY=:0 /tmp/xdump > "$OUT/root_$TS.raw" 2> "$OUT/root_$TS.info"
    dd if=/dev/fb0 of="$OUT/fb_$TS.raw" bs=4096 count=1013 2>/dev/null
    WID=$(DISPLAY=:0 xwininfo -root -tree 2>/dev/null | grep '"Navigator"' | grep -oE '0x[0-9a-f]+' | head -1)
    WIN=""
    if [ -n "$WID" ]; then
        DISPLAY=:0 /tmp/xdump "$WID" > "$OUT/win_$TS.raw" 2> "$OUT/win_$TS.info"
        WIN="$OUT/win_$TS.raw"
    fi

    RD="n/a"; FD="n/a"; WD="n/a"
    if [ -n "$PREV_ROOT" ]; then
        RD=$(/tmp/rootdiff "$PREV_ROOT" "$OUT/root_$TS.raw" "$W" "$H")
        FD=$(/tmp/rootdiff "$PREV_FB" "$OUT/fb_$TS.raw" "$W" "$H")
        if [ -n "$WIN" ] && [ -n "$PREV_WIN" ]; then
            INFO=$(cat "$OUT/win_$TS.info" 2>/dev/null)
            WH=$(echo "$INFO" | awk '{print $1}')
            WW=${WH%x*}; WHH=${WH#*x}
            if [ -n "$WW" ] && [ -n "$WHH" ] && [ "$WW" -gt 0 ] 2>/dev/null; then
                WD=$(/tmp/rootdiff "$PREV_WIN" "$WIN" "$WW" "$WHH" 32)
            fi
        fi
    fi

    PRESENT=$(grep -aoE "x11ws: present #[0-9]+" "$LOG" 2>/dev/null | tail -1)
    PRESENT_CNT=$(grep -ac "x11ws: present #" "$LOG" 2>/dev/null)
    DRESET=$(grep -ac "DeviceReset" "$LOG" 2>/dev/null)
    {
        echo "== $TS root[$RD] fb[$FD] win[$WD] present=$PRESENT ($PRESENT_CNT linjer) deviceReset=$DRESET =="
        [ -n "$WID" ] && DISPLAY=:0 xwininfo -id "$WID" -all 2>/dev/null | grep -E "Map State|Width|Height|Depth|Visual" | head -6
        if [ $((N % 4)) -eq 0 ]; then
            ps -eo pid,ppid,pcpu,args --sort=-pcpu | grep -E "firefox-esr" | grep -v grep | head -5 | awk '{printf "%s %s %s %s\n", $1, $2, $3, $NF}'
        fi
    } > "$OUT/state_$TS.txt"

    PREV_ROOT="$OUT/root_$TS.raw"; PREV_FB="$OUT/fb_$TS.raw"; PREV_WIN="$WIN"
    N=$((N+1))
    sleep 4
done
echo "færdig: $N snapshots i $OUT"
