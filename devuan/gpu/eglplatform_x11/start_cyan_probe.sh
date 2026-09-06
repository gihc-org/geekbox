#!/bin/bash
# start_cyan_probe.sh — instrumenteret Firefox-kørsel til cyan-scene-diagnose
# (Subway Surfers). Samme opskrift som start_game.sh, men med
# --remote-debugging-port 9222 (BiDi) + rene probe-logs ved start.
#
# Køres som root på boksen:
#   nohup /root/start_cyan_probe.sh > /root/cyan_launch.log 2>&1 &
set -uo pipefail

FF=/usr/lib/firefox-esr/firefox-esr
PROFILE=/home/kristian/ffprof
WEBGL=/usr/local/lib/firefox-webgl
URL="${GAME_URL:-https://poki.com/en/g/subway-surfers}"
LOG="${GAME_LOG:-/root/cyan_game.log}"

# Ryd gamle probe-log + eventuelle Firefox-låse
rm -f "$PROFILE/.parentlock" "$PROFILE/lock"
rm -f /tmp/cyan_draw_probe.log /tmp/fragdepth_probe.log
rm -rf /tmp/shaders && mkdir -p /tmp/shaders && chmod 777 /tmp/shaders

pkill -9 -x firefox-esr 2>/dev/null
sleep 1

LXPID=$(pidof lxpanel | tr ' ' '\n' | head -1)
DBUS=$(tr '\0' '\n' < "/proc/$LXPID/environ" 2>/dev/null | grep '^DBUS_SESSION_BUS_ADDRESS=' | cut -d= -f2-)

runuser -u kristian -- env -i \
  HOME=/home/kristian USER=kristian LOGNAME=kristian SHELL=/bin/bash \
  PATH=/usr/local/bin:/usr/bin:/bin DISPLAY=:0 \
  DBUS_SESSION_BUS_ADDRESS="$DBUS" \
  XDG_RUNTIME_DIR=/run/user/1000 XDG_CONFIG_HOME=/home/kristian/.config \
  XDG_DATA_HOME=/home/kristian/.local/share XDG_CURRENT_DESKTOP=LXDE \
  XDG_SESSION_TYPE=x11 \
  LD_PRELOAD="$WEBGL/system_shim.so $WEBGL/egl_platform_shim.so" \
  LD_LIBRARY_PATH=/opt/hybris:"$WEBGL" \
  EGL_PLATFORM=x11 MOZ_X11_EGL=1 MOZ_DISABLE_CONTENT_SANDBOX=1 \
  MOZ_DISABLE_GPU_SANDBOX=1 \
  "$FF" -no-remote -profile "$PROFILE" --remote-debugging-port 9222 "$URL" \
  > "$LOG" 2>&1 &
FFPID=$!

echo "start_cyan_probe: FFPID=$FFPID $(date +%H:%M:%S)" >> "$LOG"
echo "start_cyan_probe: FFPID=$FFPID $(date +%H:%M:%S)"
wait "$FFPID"
