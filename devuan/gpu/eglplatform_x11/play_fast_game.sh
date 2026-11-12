#!/bin/bash
# play_fast_game.sh - start den kendte spillbare Firefox-opsaetning og lad den
# koere, til brugeren selv lukker den.
#
# Koer som root paa boksen:
#   bash /root/play_fast_game.sh
#   WIN="640 360" bash /root/play_fast_game.sh
#
# Scriptet er en fast-play-launcher, ikke en maaling. Det:
#   - stopper en eksisterende Firefox/BiDi-klient (RESTART=0 naegter),
#   - rydder kun de profildata der tidligere har gjort spilstarten ustabil,
#   - saetter vinduets stoerrelse i xulstore.json FOER Firefox-start,
#   - starter Firefox med vnext12 + CYAN_DEPTHCLEAR=1,
#   - indlaeser Subway Surfers via den minimale BiDi-alpha-shim,
#   - gemmer en dokumentations-screenshot og verificerer xwininfo-geometrien,
#   - efterlader Firefox koerende uden CLOSE_WHEN_DONE og uden maaleprofil.
#
# user.js aendres ikke. Logs og screenshot lægges i /tmp/play_fast_game_*.

set -euo pipefail

PROFILE=${FFPROFILE:-/home/kristian/ffprof}
WIN=${WIN:-"960 540"}
URL=${GAME_URL:-https://poki.com/en/g/subway-surfers}
RESTART=${RESTART:-0}
BIDI_SECS=${BIDI_SECS:-120}
SHOT_AT=${SHOT_AT:-65}
READY_TIMEOUT=${READY_TIMEOUT:-120}
KEEP_BIDI=${KEEP_BIDI:-0}

D=$(mktemp -d /tmp/play_fast_game.XXXXXX)
LAUNCH_LOG="$D/launch.log"
BIDI_LOG="$D/bidi.log"
GAME_LOG="$D/game.log"
WIN_LOG="$D/win.log"
SHOT_DIR="$D/shots"

say() {
    printf 'play_fast_game: %s\n' "$*"
}

die() {
    printf 'play_fast_game: FEJL: %s\n' "$*" >&2
    printf 'play_fast_game: logs ligger i %s\n' "$D" >&2
    exit 1
}

first_shot() {
    find "$SHOT_DIR" -maxdepth 1 -type f -name 'shot_*.png' -print -quit \
        2>/dev/null || true
}

content_geometry() {
    local wid=$1
    local tree
    tree=$(DISPLAY=:0 xwininfo -tree -id "$wid" 2>/dev/null) || return 1
    printf '%s\n' "$tree" | awk '
        /^ +0x[0-9a-fA-F]+ / {
            for (i = 1; i <= NF; i++) {
                if ($i ~ /^[0-9]+x[0-9]+\+/) {
                    split($i, part, /[x+]/)
                    area = part[1] * part[2]
                    if (area > best) {
                        best = area
                        width = part[1]
                        height = part[2]
                    }
                }
            }
        }
        END {
            if (!best)
                exit 1
            print width, height
        }
    '
}

[ "$(id -u)" -eq 0 ] || die "skal koeres som root paa boksen"
[ -x /root/start_cyan_probe.sh ] || die "/root/start_cyan_probe.sh mangler"
[ -x /root/ensure_audio.sh ] || die "/root/ensure_audio.sh mangler"
[ -x /root/kill_firefox.sh ] || die "/root/kill_firefox.sh mangler"
[ -f /root/bidi_cyan.py ] || die "/root/bidi_cyan.py mangler"
[ -x /root/x_resize ] || die "/root/x_resize mangler"
[ -f "$PROFILE/xulstore.json" ] || die "$PROFILE/xulstore.json mangler"
command -v xwininfo >/dev/null 2>&1 || die "xwininfo mangler"
DISPLAY=:0 xdpyinfo >/dev/null 2>&1 || die "X svarer ikke paa DISPLAY=:0"

read -r W H <<EOF
$WIN
EOF
case "$W:$H" in
    *[!0-9:]*|:|*:|:*)
        die "WIN skal vaere to positive tal, fx WIN=\"960 540\""
        ;;
esac
[ "$W" -ge 320 ] && [ "$H" -ge 180 ] \
    || die "WIN er for lille; brug mindst 320x180"

[ -r /opt/hybris/libEGL.so.1.0.0 ] \
    || die "vnext12-proxyen mangler i /opt/hybris"
PROXY_MD5=$(md5sum /opt/hybris/libEGL.so.1.0.0 | awk '{print $1}')
[ "$PROXY_MD5" = "dcc0a68f175b21590eb9efbce4c8ce44" ] \
    || die "uventet EGL-proxy ($PROXY_MD5); forventede vnext12 dcc0a68f"
LSMOD=$(lsmod)
printf '%s\n' "$LSMOD" | awk '$1 == "pvrsrvkm" { found=1 } END { exit !found }' \
    || die "pvrsrvkm er ikke indlaest"

USERJS_BEFORE=""
if [ -f "$PROFILE/user.js" ]; then
    USERJS_BEFORE=$(md5sum "$PROFILE/user.js" | awk '{print $1}')
fi

if pgrep -x firefox-esr >/dev/null 2>&1; then
    [ "$RESTART" = "1" ] \
        || die "Firefox koerer allerede; luk den, eller brug RESTART=1"
    say "stopper eksisterende Firefox og alle helper-processer"
    bash /root/kill_firefox.sh
fi

# En doed BiDi-klient kan laase Firefox' ene session (faelde 42).
bash /root/kill_bidi.sh >/dev/null 2>&1 || true

# Efter en manuel PulseAudio-genstart kan serveren vaere autospawnet uden
# /opt/alsa-da og ende paa auto_null (faelde 3/63). Ret det foer Firefox.
bash /root/ensure_audio.sh || die "lydstakken kunne ikke bringes i orden"

say "renser kun de kendte flygtige profildata"
rm -rf "$PROFILE/cache2" "$PROFILE/startupCache" "$PROFILE/storage" \
       "$PROFILE/sessionstore-backups" \
       "$PROFILE/cookies.sqlite" "$PROFILE/cookies.sqlite-wal" \
       "$PROFILE/sessionstore.jsonlz4" "$PROFILE/sessionCheckpoints.json" \
       "$PROFILE/.parentlock" "$PROFILE/lock"

cp -f "$PROFILE/xulstore.json" "$D/xulstore.before.json"
python3 - "$PROFILE/xulstore.json" "$W" "$H" <<'PY'
import json
import sys

path, width, height = sys.argv[1], sys.argv[2], sys.argv[3]
with open(path) as fh:
    doc = json.load(fh)
main = doc["chrome://browser/content/browser.xhtml"]["main-window"]
main["sizemode"] = "normal"
main["screenX"] = "0"
main["screenY"] = "0"
main["width"] = width
main["height"] = height
with open(path, "w") as fh:
    json.dump(doc, fh)
print("xulstore: %sx%s, sizemode=normal" % (width, height))
PY

say "starter Firefox i ${W}x${H} med den kendte spillbare opsaetning"
GAME_URL=about:blank \
CYAN_DEPTHCLEAR=1 \
CYAN_LIGHT= \
CYAN_FLUSH= \
GAME_LOG="$GAME_LOG" \
  nohup /root/start_cyan_probe.sh >"$LAUNCH_LOG" 2>&1 </dev/null &
LAUNCH_PID=$!

port_ready=0
for _ in $(seq 1 90); do
    if python3 -c 'import socket; s=socket.create_connection(("127.0.0.1",9222),1); s.close()' \
        >/dev/null 2>&1; then
        port_ready=1
        break
    fi
    kill -0 "$LAUNCH_PID" 2>/dev/null \
        || die "Firefox-startscriptet stoppede; se $LAUNCH_LOG"
    sleep 1
done
[ "$port_ready" -eq 1 ] || die "port 9222 aabnede ikke; se $LAUNCH_LOG"

window_ready=0
for _ in $(seq 1 90); do
    TREE=$(DISPLAY=:0 xwininfo -root -tree 2>/dev/null || true)
    if grep -q '"Navigator" "firefox-esr"' <<<"$TREE"; then
        window_ready=1
        break
    fi
    sleep 1
done
[ "$window_ready" -eq 1 ] || die "Firefox-hovedvinduet kom ikke; se $WIN_LOG"

printf '%s\n' "$TREE" >"$WIN_LOG"
RESIZE_OUT=$(DISPLAY=:0 /root/x_resize -big firefox "$W" "$H" 2>&1) \
    || die "kunne ikke saette vinduesstoerrelsen: $RESIZE_OUT"
printf '%s\n' "$RESIZE_OUT" >>"$WIN_LOG"
WID=$(sed -n 's/^vindue \(0x[0-9a-fA-F]*\).*/\1/p' <<<"$RESIZE_OUT")
[ -n "$WID" ] || die "x_resize returnerede intet vindues-id: $RESIZE_OUT"
size_ok=0
for _ in $(seq 1 15); do
    sleep 1
    GEOM=$(content_geometry "$WID" 2>/dev/null || true)
    ACT_W=${GEOM%% *}
    ACT_H=${GEOM##* }
    if [ "$ACT_W" = "$W" ] && [ "$ACT_H" = "$H" ]; then
        size_ok=1
        break
    fi
done
[ "$size_ok" -eq 1 ] \
    || die "renderfladen fik ${ACT_W:-?}x${ACT_H:-?}; forventede ${W}x${H}"
printf 'renderflade: %sx%s (X-vindue %s)\n' "$W" "$H" "$WID" >>"$WIN_LOG"

mkdir -p "$SHOT_DIR"
say "indlaeser spillet via minimal BiDi-preload"
SHOT_AT="$SHOT_AT" \
SHOT_DIR="$SHOT_DIR" \
CLOSE_WHEN_DONE=0 \
CYAN_CANVAS_LIVE_SCALE="${CYAN_CANVAS_LIVE_SCALE-0.75}" \
CYAN_CANVAS_LIVE_DELAY="${CYAN_CANVAS_LIVE_DELAY:-20}" \
PYTHONUNBUFFERED=1 \
  nohup python3 /root/bidi_cyan.py preloadmin goto \
    "$URL" "$BIDI_SECS" >"$BIDI_LOG" 2>&1 </dev/null &
BIDI_PID=$!

game_ready=0
shot_ready=0
for _ in $(seq 1 "$READY_TIMEOUT"); do
    if grep -q 'PRELOAD-MIN aktiv' "$GAME_LOG" 2>/dev/null \
        && grep -q 'GETCONTEXT canvas=.* type=webgl.* result=OK' \
            "$GAME_LOG" 2>/dev/null \
        && grep -q 'FPS tr=' "$GAME_LOG" 2>/dev/null; then
        game_ready=1
    fi
    SHOT_FILE=$(first_shot)
    if [ -n "$SHOT_FILE" ] && [ "$(stat -c %s "$SHOT_FILE")" -gt 1024 ]; then
        shot_ready=1
    fi
    [ "$game_ready" -eq 1 ] && [ "$shot_ready" -eq 1 ] && break
    kill -0 "$BIDI_PID" 2>/dev/null \
        || break
    sleep 1
done

pgrep -x firefox-esr >/dev/null 2>&1 \
    || die "Firefox er stoppet under indlaesningen; se $LAUNCH_LOG og $GAME_LOG"
[ "$game_ready" -eq 1 ] \
    || die "preload/WebGL blev ikke bekraeftet; se $GAME_LOG og $BIDI_LOG"
[ "$shot_ready" -eq 1 ] \
    || die "der kom ingen dokumentations-screenshot; se $BIDI_LOG"

# Preloadet er nu udført. Luk kun BiDi-klienten; browseren må ikke lukkes.
# Både den normale 120 s-udgang og screenshot-tråden kan hænge på den ene
# tilladte WebSocket-session, så frigør den aktivt efter en vellykket load.
if [ "$KEEP_BIDI" = "1" ]; then
    say "BiDi-klienten beholdes til live-eksperimenter (PID $BIDI_PID)"
elif kill -0 "$BIDI_PID" 2>/dev/null; then
    kill -TERM "$BIDI_PID" 2>/dev/null || true
    for _ in $(seq 1 10); do
        kill -0 "$BIDI_PID" 2>/dev/null || break
        sleep 1
    done
    kill -KILL "$BIDI_PID" 2>/dev/null || true
fi

if [ -n "$USERJS_BEFORE" ]; then
    USERJS_AFTER=$(md5sum "$PROFILE/user.js" | awk '{print $1}')
    [ "$USERJS_AFTER" = "$USERJS_BEFORE" ] \
        || die "user.js blev aendret ($USERJS_BEFORE -> $USERJS_AFTER)"
fi

say "FAST-PLAY KLAR: ${W}x${H}, Firefox koerer"
say "logs: $D"
say "screenshot: $(first_shot)"
if [ "$KEEP_BIDI" = "1" ]; then
    say "BiDi-klienten koerer fortsat; Firefox bliver koerende."
else
    say "BiDi-klienten er lukket; Firefox bliver koerende."
fi
