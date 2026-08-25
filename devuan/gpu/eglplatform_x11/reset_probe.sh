#!/bin/bash
# reset_probe.sh — kør stress-siden N gange (max T sekunder hver) og tæl
# GPU-proces-resets (DeviceReset i loggen). Brug til at måle reset-raten
# før/efter et shim-fix.
# Kør (root på boksen):
#   bash /tmp/reset_probe.sh <N> <max_sek> <outfile>
set -u

N="${1:-5}"
MAX="${2:-300}"
OUT="${3:-/tmp/reset_probe.log}"
URL="file:///usr/local/lib/firefox-webgl/webgl_stress.html?scale=1&tiles=32&tex=0"
: > "$OUT"

for i in $(seq 1 "$N"); do
    rm -f /tmp/ff_probe.log
    GAME_URL="$URL" GAME_LOG=/tmp/ff_probe.log nohup /tmp/start_game.sh \
        >/tmp/start_probe.out 2>&1 &
    # vent på at Firefox faktisk er startet (op til 30 s) — ellers dræber
    # oprydningen den ny-startede proces (målt: 5× "INGEN reset på 0s")
    UP=0
    for w in $(seq 1 15); do
        if pgrep -x firefox-esr >/dev/null 2>&1; then
            UP=1
            break
        fi
        sleep 2
    done
    if [ "$UP" != 1 ]; then
        echo "run $i: START-FEJL (Firefox kom ikke op)" | tee -a "$OUT"
        pkill -9 -x firefox-esr 2>/dev/null
        sleep 2
        pkill -9 -f "/usr/lib/firefox-es[r]/" 2>/dev/null
        sleep 2
        continue
    fi
    T0=$(date +%s)
    RESET=0
    STOP=0
    while [ $(( $(date +%s) - T0 )) -lt "$MAX" ]; do
        if grep -aq "DeviceReset" /tmp/ff_probe.log 2>/dev/null; then
            RESET=1
            break
        fi
        if ! pgrep -x firefox-esr >/dev/null 2>&1; then
            STOP=1
            break
        fi
        sleep 2
    done
    PR=$(grep -aoE "x11ws: present #[0-9]+" /tmp/ff_probe.log 2>/dev/null | tail -1)
    ELAPSED=$(( $(date +%s) - T0 ))
    if [ "$RESET" = 1 ]; then
        echo "run $i: RESET efter ${ELAPSED}s ($PR)" | tee -a "$OUT"
    elif [ "$STOP" = 1 ]; then
        echo "run $i: STOP (Firefox døde) efter ${ELAPSED}s ($PR)" | tee -a "$OUT"
    else
        echo "run $i: INGEN reset på ${ELAPSED}s ($PR)" | tee -a "$OUT"
    fi
    pkill -9 -x firefox-esr 2>/dev/null
    sleep 2
    pkill -9 -f "/usr/lib/firefox-es[r]/" 2>/dev/null
    sleep 2
done
echo "færdig: $(grep -c RESET "$OUT") resets af $N kørsler" | tee -a "$OUT"
