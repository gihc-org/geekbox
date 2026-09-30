#!/bin/bash
# start_chromium_probe.sh — start Chromium 150 mod GeekBox' hybris-EGL.
#
# Køres som root på boksen:
#   nohup /root/start_chromium_probe.sh > /root/chromium_launch.log 2>&1 &
#
# Formålet er at efterprøve Chromium-vejen (fx fra poki.com) med samme
# EGL/GLES-stak som Firefox-sporet. --no-sandbox er nødvendig her, fordi
# Chromium-pakken er installeret uden chromium-sandbox, og 3.10-kernen ikke
# giver den nødvendige sandbox-støtte. De øvrige flags er brugerens forslag.
set -uo pipefail

CHROME=${CHROME:-/usr/bin/chromium}
WEBGL=/usr/local/lib/firefox-webgl
PROFILE=${CHROMIUM_PROFILE:-/home/kristian/.config/chromium-fps}
URL=${CHROMIUM_URL:-https://poki.com}
LOG=${CHROMIUM_LOG:-/root/chromium_game.log}
PORT=${CHROMIUM_DEBUG_PORT:-9223}
USE_GL=${CHROMIUM_USE_GL:-angle}
USE_ANGLE=${CHROMIUM_USE_ANGLE:-gles-egl}
UNSAFE_SWIFTSHADER=${CHROMIUM_UNSAFE_SWIFTSHADER:-0}
IN_PROCESS_GPU=${CHROMIUM_IN_PROCESS_GPU:-0}
LD_PRELOAD_V=${CHROMIUM_LD_PRELOAD-$WEBGL/system_shim.so $WEBGL/egl_platform_shim.so}
LD_LIBRARY_PATH_V=${CHROMIUM_LD_LIBRARY_PATH:-/opt/hybris}

mkdir -p "$PROFILE"
chown -R kristian:kristian "$PROFILE"
rm -f "$LOG"

# Firefox kan køre samtidig uden at dele denne profil, men vi lukker Chromium
# pænt først, så gamle processer ikke holder X eller GPU-stakken.
pkill -TERM -x chromium 2>/dev/null
sleep 2
pkill -9 -x chromium 2>/dev/null
sleep 1

LXPID=$(pidof lxpanel | tr ' ' '\n' | head -1)
DBUS=$(tr '\0' '\n' < "/proc/$LXPID/environ" 2>/dev/null \
       | grep '^DBUS_SESSION_BUS_ADDRESS=' | cut -d= -f2-)

GLFLAGS=(--use-gl="$USE_GL")
[ -n "$USE_ANGLE" ] && GLFLAGS+=(--use-angle="$USE_ANGLE")
[ "$UNSAFE_SWIFTSHADER" = 1 ] && GLFLAGS+=(--enable-unsafe-swiftshader)
[ "$IN_PROCESS_GPU" = 1 ] && GLFLAGS+=(--in-process-gpu)

runuser -u kristian -- env -i \
  HOME=/home/kristian USER=kristian LOGNAME=kristian SHELL=/bin/bash \
  PATH=/usr/local/bin:/usr/bin:/bin DISPLAY=:0 \
  DBUS_SESSION_BUS_ADDRESS="$DBUS" \
  XDG_RUNTIME_DIR=/run/user/1000 XDG_CONFIG_HOME=/home/kristian/.config \
  XDG_DATA_HOME=/home/kristian/.local/share XDG_CACHE_HOME=/home/kristian/.cache \
  XDG_CURRENT_DESKTOP=LXDE XDG_SESSION_TYPE=x11 \
  LD_PRELOAD="$LD_PRELOAD_V" \
  LD_LIBRARY_PATH="$LD_LIBRARY_PATH_V" \
  EGL_PROXY_TRACE="${EGL_PROXY_TRACE:-}" \
  EGL_PROXY_CONFIG_FIX="${EGL_PROXY_CONFIG_FIX:-}" \
  EGL_PROXY_FAKE_VISUAL="${EGL_PROXY_FAKE_VISUAL:-}" \
  EGL_PLATFORM=x11 \
  "$CHROME" \
    --kiosk \
    --no-sandbox \
    --disable-gpu-sandbox \
    --ignore-gpu-blocklist \
    --enable-zero-copy \
    "${GLFLAGS[@]}" \
    --user-data-dir="$PROFILE" \
    --no-first-run \
    --no-default-browser-check \
    --disable-dev-shm-usage \
    --enable-logging=stderr \
    --v=1 \
    --remote-debugging-port="$PORT" \
    "$URL" \
  > "$LOG" 2>&1 &
PID=$!

echo "start_chromium_probe: PID=$PID profile=$PROFILE url=$URL $(date +%H:%M:%S)"
wait "$PID"
