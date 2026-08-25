#!/bin/bash
# Overvåger present-kæden i Firefox-loggen; når den stopper (ingen nye
# present-linjer i ~12 s), tager den gdb-backtrace af GPU-processen + hukommelse.
# Kør (root på boksen) efter at spillet er startet:
#   bash /tmp/stall_capture.sh <ff-log> <outdir> [max_sek]
set -u

LOG="${1:-/tmp/ff_game5.log}"
OUT="${2:-/tmp/stall}"
MAX="${3:-240}"
mkdir -p "$OUT"

LAST=""
STALLS=0
END=$(( $(date +%s) + MAX ))
while [ "$(date +%s)" -lt "$END" ]; do
    CUR=$(grep -aoE "x11ws: present #[0-9]+" "$LOG" 2>/dev/null | wc -l)
    if [ -n "$LAST" ] && [ "$CUR" = "$LAST" ]; then
        STALLS=$((STALLS+1))
        if [ "$STALLS" -ge 4 ]; then
            TS=$(date +%H%M%S)
            echo "STALL detekteret kl $TS (present-forekomster: $CUR)"
            GPUPID=$(pgrep -f "contentproc" | while read p; do
                if grep -q " gpu$" /proc/$p/cmdline 2>/dev/null; then echo $p; break; fi
            done)
            echo "GPU-proces: $GPUPID"
            {
                echo "== $(date +%H:%M:%S) present-forekomster=$CUR =="
                ps -eo pid,ppid,pcpu,pmem,rss,args --sort=-pcpu | grep -E "firefox|contentproc" | grep -v grep | head -10
                free -m
                if [ -n "$GPUPID" ]; then
                    timeout 45 gdb -q -batch -ex "set pagination off" \
                        -ex "attach $GPUPID" -ex "thread apply all bt 10" \
                        -ex "detach" 2>&1
                fi
            } > "$OUT/stall_$TS.txt" 2>&1
            echo "gemt: $OUT/stall_$TS.txt ($(wc -l < $OUT/stall_$TS.txt) linjer)"
            exit 0
        fi
    else
        STALLS=0
    fi
    LAST="$CUR"
    sleep 3
done
echo "ingen stall inden for $MAX s"
