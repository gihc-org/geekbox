#!/bin/bash
# cyan_fps_run.sh — én målekørsel af Subway Surfers på boksen (fps-sammenligning).
#
# Kør som root PÅ boksen:
#   bash /root/cyan_fps_run.sh <tag> [sek] [CYAN_LIGHT] [preload|preloadmin] [PLAY_AT]
#   fx: bash /root/cyan_fps_run.sh A_heavy 300 ""      preload    "110,210"
#       bash /root/cyan_fps_run.sh B_light 300 1       preload    "110,210"
#       bash /root/cyan_fps_run.sh C_min   300 1       preloadmin "110,210"
#
# Ren profil hver gang (fælde: beskidt profil → spillet fryser ved 0 %), derefter
# Firefox (start_cyan_probe.sh) + BiDi-preload (alpha-shim) og automatisk
# "start spillet" ved de sekunder der står i PLAY_AT (kommasepareret, to
# forsøg er mere robust end ét). Undervejs tages tre fb0-dumps så skærmens
# faktiske opdateringsrate kan måles bagefter (software-layers: eglSwapBuffers
# kaldes ikke, så GL-siden kan ikke levere det tal alene).
set -u

TAG=${1:?brug: cyan_fps_run.sh <tag> [sek] [CYAN_LIGHT] [preload|preloadmin] [PLAY_AT] [url]}
SECS=${2:-300}
LIGHT=${3:-}
MODE=${4:-preload}
PLAY_AT=${5:-"110,210"}
URL=${6:-https://poki.com/en/g/subway-surfers}

P=/home/kristian/ffprof
D=/tmp/fpsrun_$TAG
mkdir -p "$D"

echo "=== $TAG: sek=$SECS light='$LIGHT' mode=$MODE play_at=$PLAY_AT url=$URL $(date +%H:%M:%S) ==="

# Stop en eventuel kørende Firefox PÆNT først (SIGKILL → beskidt profil).
pkill -TERM -x firefox-esr 2>/dev/null
sleep 6
pkill -9 -x firefox-esr 2>/dev/null
sleep 1
rm -rf "$P/cache2" "$P/startupCache" "$P/storage" "$P/sessionstore-backups" \
       "$P/cookies.sqlite" "$P/cookies.sqlite-wal" "$P/sessionstore.jsonlz4" \
       "$P/sessionCheckpoints.json" "$P/.parentlock" "$P/lock"

GAME_URL=about:blank CYAN_LIGHT="$LIGHT" CYAN_DEPTHCLEAR=1 \
  nohup /root/start_cyan_probe.sh > "$D/launch.log" 2>&1 &
sleep 15

PLAY_AT="$PLAY_AT" PYTHONUNBUFFERED=1 \
  nohup python3 /root/bidi_cyan.py "$MODE" goto \
  "$URL" "$SECS" > "$D/bidi.log" 2>&1 &
BIDI=$!

# fb-prøver midt i kørslen (3 dumps, 3 s mellemrum) til "opdaterer skærmen?"-mål.
sleep 240
for i in 1 2 3; do
    dd if=/dev/fb0 bs=3840 count=540 of="$D/fb$i.raw" 2>/dev/null
    sleep 3
done

# vent på at BiDi-klienten er færdig (den lukker efter SECS)
for i in $(seq 1 90); do
    kill -0 "$BIDI" 2>/dev/null || break
    sleep 5
done

cp /tmp/cyan_draw_probe.log "$D/probe.log" 2>/dev/null
cp /root/cyan_game.log "$D/game.log" 2>/dev/null

echo "--- $TAG: JS-fps (rAF pr. 10 s) ---"
grep -a "FPS raf=" "$D/game.log" | tail -12
echo "--- $TAG: GL-tick (proxy) ---"
grep -a "GL-tick:" "$D/probe.log" | tail -8
echo "--- $TAG: alpha-shim/fejl ---"
grep -ac "ALPHA-SHIM" "$D/game.log"
grep -a "PAGE-ERROR\|CTXLOST" "$D/game.log" | head -3
echo "=== $TAG færdig $(date +%H:%M:%S) — data i $D ==="
