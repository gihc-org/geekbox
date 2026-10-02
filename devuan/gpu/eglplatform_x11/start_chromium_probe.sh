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

CHROME=${CHROME:-/usr/lib/chromium/chromium}
WEBGL=/usr/local/lib/firefox-webgl
PROFILE=${CHROMIUM_PROFILE:-/home/kristian/.config/chromium-fps}
URL=${CHROMIUM_URL:-https://poki.com}
LOG=${CHROMIUM_LOG:-/root/chromium_game.log}
PORT=${CHROMIUM_DEBUG_PORT:-9223}
USE_GL=${CHROMIUM_USE_GL:-angle}
USE_ANGLE=${CHROMIUM_USE_ANGLE:-gles-egl}
UNSAFE_SWIFTSHADER=${CHROMIUM_UNSAFE_SWIFTSHADER:-0}
IN_PROCESS_GPU=${CHROMIUM_IN_PROCESS_GPU:-0}
DISABLE_GPU_RASTER=${CHROMIUM_DISABLE_GPU_RASTER:-1}
DISABLE_GPU_COMPOSITING=${CHROMIUM_DISABLE_GPU_COMPOSITING:-1}
DISABLE_ACCEL_2D=${CHROMIUM_DISABLE_ACCEL_2D:-1}
DISABLE_GPU_WATCHDOG=${CHROMIUM_DISABLE_GPU_WATCHDOG:-1}
DEPTHCLEAR=${CHROMIUM_DEPTHCLEAR:-1}
ZERO_COPY=${CHROMIUM_ZERO_COPY:-0}
VERBOSE=${CHROMIUM_VERBOSE:-0}
LD_PRELOAD_V=${CHROMIUM_LD_PRELOAD-$WEBGL/system_shim.so $WEBGL/egl_platform_shim.so}
LD_LIBRARY_PATH_V=${CHROMIUM_LD_LIBRARY_PATH:-/opt/hybris}

mkdir -p "$PROFILE"
chown -R kristian:kristian "$PROFILE"
rm -f "$LOG"

# HDMI-udgangen kan blive slået fra, når en Chromium-instans med hardware-EGL
# river sin hybris/gralloc-flade ned (målt 2. okt 2026: kernens "hdmi remove
# from lcdc0" uden efterfølgende connect → sort skærm indtil enable sættes
# igen). Tænd derfor udgangene både efter nedlukningen og efter starten.
restore_display() {
    for f in /sys/class/display/*/enable; do
        [ -e "$f" ] || continue
        if [ "$(cat "$f" 2>/dev/null)" != 1 ]; then
            echo 1 > "$f" 2>/dev/null && echo "display re-enabled: $f"
        fi
    done
}

# Firefox kan køre samtidig uden at dele denne profil, men vi lukker Chromium
# pænt først, så gamle processer ikke holder X eller GPU-stakken.
pkill -TERM -f /usr/lib/chromium/chromium 2>/dev/null
sleep 2
pkill -9 -f /usr/lib/chromium/chromium 2>/dev/null
sleep 1
restore_display

LXPID=$(pidof lxpanel | tr ' ' '\n' | head -1)
DBUS=$(tr '\0' '\n' < "/proc/$LXPID/environ" 2>/dev/null \
       | grep '^DBUS_SESSION_BUS_ADDRESS=' | cut -d= -f2-)

GLFLAGS=(--use-gl="$USE_GL")
[ -n "$USE_ANGLE" ] && GLFLAGS+=(--use-angle="$USE_ANGLE")
[ "$UNSAFE_SWIFTSHADER" = 1 ] && GLFLAGS+=(--enable-unsafe-swiftshader)
[ "$IN_PROCESS_GPU" = 1 ] && GLFLAGS+=(--in-process-gpu)
[ "$DISABLE_GPU_RASTER" = 1 ] && GLFLAGS+=(--disable-gpu-rasterization)
[ "$DISABLE_GPU_COMPOSITING" = 1 ] && GLFLAGS+=(--disable-gpu-compositing)
[ "$DISABLE_ACCEL_2D" = 1 ] && GLFLAGS+=(--disable-accelerated-2d-canvas)
[ "$DISABLE_GPU_WATCHDOG" = 1 ] && GLFLAGS+=(--disable-gpu-watchdog)
# Zero-copy lader compositoren læse canvas-laget direkte fra GPU-bufferen.
# Målt 2. okt 2026: med --enable-zero-copy står spil-canvas' område på fb0
# stille (samme hash i 12 s), mens Chromium selv tegner nye frames; uden
# flaget kopieres laget i stedet. Derfor er det slået FRA som standard.
[ "$ZERO_COPY" = 1 ] && GLFLAGS+=(--enable-zero-copy)
LOGFLAGS=(--enable-logging=stderr)
[ "$VERBOSE" = 1 ] && LOGFLAGS+=(--v=1)

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
  EGL_PROXY_CONFIG_FIX="${EGL_PROXY_CONFIG_FIX:-1}" \
  EGL_PROXY_FAKE_VISUAL="${EGL_PROXY_FAKE_VISUAL:-0x21}" \
  CYAN_DEPTHCLEAR="$DEPTHCLEAR" \
  CYAN_LIGHT="${CHROMIUM_CYAN_LIGHT:-}" \
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
    "${LOGFLAGS[@]}" \
    --remote-debugging-port="$PORT" \
    --remote-allow-origins=* \
    "$URL" \
  > "$LOG" 2>&1 &
PID=$!

( sleep 8; restore_display ) &

echo "start_chromium_probe: PID=$PID profile=$PROFILE url=$URL $(date +%H:%M:%S)"
wait "$PID"
