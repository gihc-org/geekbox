#!/bin/bash
# Overvåger en Firefox-kørsel og fanger øjeblikket present-kæden går i stå.
# Signaler:
#   1) X-server-respons (xprop-roundtrip med timeout)
#   2) log-skrivealder: ff-loggen ikke skrevet i 15+ s mens Firefox kører
#      (FPS-dumps stopper når rAF/present-kæden dør — robust mod log-buffering)
#   3) load-kritisk (> 13)
# Ved stall: kør xrefresh (tvungen skærmopdatering), gentjek, og tag gdb-
# backtrace af GPU-processen + X-serveren hvis det ikke hjælper.
# Kør (root på boksen), fx:
#   bash /tmp/wedge_capture.sh /tmp/ff_stress3.log /tmp/wedge3 600
set -u
export DISPLAY=:0

LOG="${1:-/tmp/ff_stress3.log}"
OUT="${2:-/tmp/wedge}"
MAX="${3:-540}"
mkdir -p "$OUT"

END=$(( $(date +%s) + MAX ))
XSTUCK=0
STALLS=0
START=$(date +%s)

gpu_pid() {
    ps -eo pid,args | grep "firefox-esr" | grep " gpu$" | grep -v grep | awk '{print $1}' | head -1
}

log_age() {
    local MTIME
    MTIME=$(stat -c %Y "$LOG" 2>/dev/null || echo 0)
    echo $(( $(date +%s) - MTIME ))
}

x_ok() {
    timeout 3 xprop -root _NET_SUPPORTED >/dev/null 2>&1
}

capture_gdb() {
    local REASON="$1" TS GPUPID XPID
    TS=$(date +%H%M%S)
    GPUPID=$(gpu_pid)
    XPID=$(pgrep -f "/usr/lib/xorg/Xor[g]" | head -1)
    {
        echo "== $(date +%H:%M:%S) $REASON =="
        ps -eo pid,ppid,stat,pcpu,pmem,rss,args --sort=-pcpu | grep -E "firefox|Xorg|xdump|dd if" | grep -v grep | head -12
        echo "--- GPU-proces ($GPUPID) ---"
        if [ -n "$GPUPID" ]; then
            timeout 40 gdb -q -batch -ex "set pagination off" \
                -ex "attach $GPUPID" -ex "info threads" -ex "thread apply all bt 15" \
                -ex "detach" 2>&1
        else
            echo "ingen GPU-proces fundet"
        fi
        echo "--- X-server ($XPID) ---"
        if [ -n "$XPID" ]; then
            timeout 25 gdb -q -batch -ex "set pagination off" \
                -ex "attach $XPID" -ex "thread apply all bt 8" \
                -ex "detach" 2>&1
        fi
        free -m
    } > "$OUT/wedge_$TS.txt" 2>&1
    echo "gemt: $OUT/wedge_$TS.txt ($(wc -l < "$OUT/wedge_$TS.txt") linjer)"
}

while [ "$(date +%s)" -lt "$END" ]; do
    NOW=$(date +%s)
    ELAPSED=$(( NOW - START ))

    # 1) X-server-respons
    if x_ok; then
        XSTUCK=0
    else
        XSTUCK=$((XSTUCK+1))
        if [ "$XSTUCK" -ge 2 ]; then
            TS=$(date +%H%M%S)
            echo "X HÆNGER kl $TS — kører xrefresh" | tee -a "$OUT/wedge.log"
            timeout 5 xrefresh -display :0 >/dev/null 2>&1
            sleep 4
            if x_ok; then
                echo "X kom tilbage efter xrefresh" | tee -a "$OUT/wedge.log"
            else
                echo "X stadig hængt efter xrefresh — gdb" | tee -a "$OUT/wedge.log"
                capture_gdb "X-hæng (xrefresh hjalp ikke)"
                exit 0
            fi
            XSTUCK=0
        fi
    fi

    # 2) present-stall: loggen ikke skrevet i 15+ s (efter 60 s opstart)
    if [ "$ELAPSED" -gt 60 ] && pgrep -x firefox-esr >/dev/null 2>&1; then
        AGE=$(log_age)
        if [ "$AGE" -gt 15 ]; then
            STALLS=$((STALLS+1))
            if [ "$STALLS" -ge 2 ]; then
                TS=$(date +%H%M%S)
                echo "STALL kl $TS (log sidst skrevet for ${AGE}s) — kører xrefresh" | tee -a "$OUT/wedge.log"
                timeout 5 xrefresh -display :0 >/dev/null 2>&1
                sleep 4
                if [ "$(log_age)" -lt 15 ]; then
                    echo "present-kæden kom i gang efter xrefresh" | tee -a "$OUT/wedge.log"
                else
                    echo "stadig stået efter xrefresh — gdb" | tee -a "$OUT/wedge.log"
                    capture_gdb "present-stall (log ${AGE}s gammel, xrefresh hjalp ikke)"
                    exit 0
                fi
                STALLS=0
            fi
        else
            STALLS=0
        fi
    fi

    # 3) sikkerhed: ekstrem load → capture + stop
    LOAD=$(cut -d. -f1 /proc/loadavg)
    if [ "$LOAD" -gt 13 ] 2>/dev/null; then
        capture_gdb "load-kritisk (${LOAD})"
        exit 0
    fi

    sleep 3
done
echo "ingen wedge/stall inden for $MAX s"
