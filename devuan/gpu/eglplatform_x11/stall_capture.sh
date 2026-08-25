#!/bin/bash
# Overvåger present-kæden i Firefox-loggen; når den stopper ELLER sænker
# farten (< 1 present/s over ~15 s), tager den gdb-backtrace af
# GPU-processen + hukommelse. Robust mod log-flush (bruger højeste
# present-nummer, ikke antal linjer) og finder GPU-processen via ps-args
# (slutter på " gpu").
# Kør (root på boksen) efter at spillet er startet:
#   bash /tmp/stall_capture.sh <ff-log> <outdir> [max_sek]
set -u

LOG="${1:-/tmp/ff_game5.log}"
OUT="${2:-/tmp/stall}"
MAX="${3:-240}"
mkdir -p "$OUT"

LAST=""
STALLS=0
SLOW=0
NOW=$(date +%s)
HIST=""
END=$(( NOW + MAX ))

last_present() {
    grep -aoE "x11ws: present #[0-9]+" "$LOG" 2>/dev/null | tail -1 | grep -oE "[0-9]+$"
}

capture() {
    local CUR="$1" REASON="$2" TS
    TS=$(date +%H%M%S)
    echo "STALL detekteret kl $TS ($REASON, sidste present: $CUR)"
    GPUPID=$(ps -eo pid,args | grep "firefox-esr" | grep " gpu$" | grep -v grep | awk '{print $1}' | head -1)
    echo "GPU-proces: $GPUPID"
    {
        echo "== $(date +%H:%M:%S) sidste-present=$CUR ($REASON) =="
        ps -eo pid,ppid,pcpu,pmem,rss,args --sort=-pcpu | grep -E "firefox" | grep -v grep | head -10
        free -m
        if [ -n "$GPUPID" ]; then
            timeout 45 gdb -q -batch -ex "set pagination off" \
                -ex "attach $GPUPID" -ex "thread apply all bt 10" \
                -ex "detach" 2>&1
        else
            echo "INGEN GPU-PROCES FUNDET"
        fi
    } > "$OUT/stall_$TS.txt" 2>&1
    echo "gemt: $OUT/stall_$TS.txt ($(wc -l < "$OUT/stall_$TS.txt") linjer)"
}

while [ "$(date +%s)" -lt "$END" ]; do
    NOW=$(date +%s)
    CUR=$(last_present)
    [ -z "$CUR" ] && CUR=0
    if [ -n "$LAST" ] && [ "$CUR" = "$LAST" ] && [ "$CUR" -gt 20 ]; then
        STALLS=$((STALLS+1))
        if [ "$STALLS" -ge 4 ]; then
            capture "$CUR" "present stoppet"
            exit 0
        fi
    else
        # rate-check over ~15 s (6 samples à 3 s)
        HIST="$HIST $NOW:$CUR"
        set -- $HIST
        while [ "$#" -gt 6 ]; do shift; done
        HIST="$*"
        set -- $HIST
        if [ "$#" -ge 5 ]; then
            FIRST=$1
            LASTT=$(eval "echo \${$#}")
            OLD_T=${FIRST%%:*}; OLD_C=${FIRST##*:}
            NEW_T=${LASTT%%:*}; NEW_C=${LASTT##*:}
            DT=$((NEW_T - OLD_T))
            RATE=99
            if [ "$DT" -ge 12 ] && [ "$OLD_C" -gt 20 ]; then
                if [ "$NEW_C" -le "$OLD_C" ]; then
                    RATE=0
                else
                    RATE=$(( (NEW_C - OLD_C) / DT ))
                fi
            fi
            if [ "$RATE" -lt 1 ]; then
                SLOW=$((SLOW+1))
                if [ "$SLOW" -ge 2 ]; then
                    capture "$NEW_C" "present-rate < 1/s over ${DT}s"
                    exit 0
                fi
            else
                SLOW=0
            fi
        fi
        STALLS=0
    fi
    LAST="$CUR"
    sleep 3
done
echo "ingen stall inden for $MAX s"
