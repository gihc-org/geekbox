#!/bin/bash
# Instrumenteret genstart af Firefox med Subway Surfers (poki.com).
# Køres som root på boksen; Firefox kører som kristian med remote debugging.
# Eksempel:
#   env GAME_URL="file:///usr/local/lib/firefox-webgl/webgl_test_dump.html" \
#       GAME_LOG=/tmp/ff_test.log nohup /tmp/start_game.sh &
set -uo pipefail

FF=/usr/lib/firefox-esr/firefox-esr
PROFILE=/home/kristian/ffprof
WEBGL=/usr/local/lib/firefox-webgl
URL="${GAME_URL:-https://poki.com/en/g/subway-surfers}"
LOG="${GAME_LOG:-/tmp/ff_game.log}"
MOZ_LOG="${MOZ_LOG:-}"
RUST_LOG="${RUST_LOG:-}"
GL_CAPTURE_SHIM="${GL_CAPTURE_SHIM:-}"
GL_CAPTURE_LOG="${GL_CAPTURE_LOG:-}"

rm -f "$PROFILE/.parentlock" "$PROFILE/lock"

LXPID=$(pidof lxpanel | tr ' ' '\n' | head -1)
DBUS=$(tr '\0' '\n' < "/proc/$LXPID/environ" 2>/dev/null | grep '^DBUS_SESSION_BUS_ADDRESS=' | cut -d= -f2-)

CAPTURE_PRELOAD=""
if [ -n "$GL_CAPTURE_SHIM" ]; then
    CAPTURE_PRELOAD="$GL_CAPTURE_SHIM "
fi

runuser -u kristian -- env -i \
  HOME=/home/kristian USER=kristian LOGNAME=kristian SHELL=/bin/bash \
  PATH=/usr/local/bin:/usr/bin:/bin DISPLAY=:0 \
  DBUS_SESSION_BUS_ADDRESS="$DBUS" \
  XDG_RUNTIME_DIR=/run/user/1000 XDG_CONFIG_HOME=/home/kristian/.config \
  XDG_DATA_HOME=/home/kristian/.local/share XDG_CURRENT_DESKTOP=LXDE \
  XDG_SESSION_TYPE=x11 \
  LD_PRELOAD="${CAPTURE_PRELOAD}$WEBGL/system_shim.so $WEBGL/egl_platform_shim.so" \
  LD_LIBRARY_PATH=/opt/hybris:"$WEBGL" \
  EGL_PLATFORM=x11 MOZ_X11_EGL=1 MOZ_DISABLE_CONTENT_SANDBOX=1 \
  MOZ_DISABLE_GPU_SANDBOX=1 \
  MOZ_LOG="$MOZ_LOG" \
  RUST_LOG="$RUST_LOG" \
  GL_CAPTURE_LOG="$GL_CAPTURE_LOG" \
  "$FF" -no-remote -profile "$PROFILE" --start-debugging-server 9222 "$URL" \
  > "$LOG" 2>&1 &
FFPID=$!

D=:0
WID=""
for _ in $(seq 1 90); do
    WID=$(DISPLAY="$D" xwininfo -root -tree 2>/dev/null | grep '"Navigator"' | grep -oE '0x[0-9a-f]+' | head -1)
    [ -n "$WID" ] && break
    sleep 1
done
if [ -n "$WID" ]; then
    DISPLAY="$D" xprop -id "$WID" -f _MOTIF_WM_HINTS 32c \
        -set _MOTIF_WM_HINTS "0x2, 0x1, 0x0, 0x0, 0x0" 2>/dev/null
fi
echo "start_game: FFPID=$FFPID WID=$WID $(date +%H:%M:%S)" >> "$LOG"
wait "$FFPID"
