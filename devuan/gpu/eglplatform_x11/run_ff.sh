#!/bin/bash
# run_ff.sh — kører firefox-esr på boks 1 med hybris-stakken og fanger loggen.
# Brug efter bindapi-patch for at se om ES-vejen kommer forbi eglBindAPI.
set -uo pipefail

BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key

ssh -i "$KEY" -o ConnectTimeout=8 "$BOX" 'pkill -9 -x firefox-esr 2>/dev/null; sleep 1; \
cd /root && timeout 150 env LD_PRELOAD="/root/system_shim.so /root/egl_platform_shim.so" \
LD_LIBRARY_PATH=/opt/hybris EGL_PLATFORM=x11 DISPLAY=:0 MOZ_X11_EGL=1 \
MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_DISABLE_GPU_SANDBOX=1 \
MOZ_GL_SPEW=1 MOZ_GL_DEBUG=1 \
/usr/lib/firefox-esr/firefox-esr -no-remote -profile /root/ffprof \
file:///root/webgl_test.html > /root/ff_run_after.log 2>&1; \
echo "== nøglelinjer =="; grep -aE "Failed to create EGLContext|Fallback|FEATURE|WebGL|GLContext|GL version|GLSL version|OpenGL vendor|OpenGL renderer|failed|error 3|GPU|RenderThread|x11ws: vindue|present|MakeCurrent|Init" /root/ff_run_after.log | head -50; \
echo "== sidste 15 =="; tail -15 /root/ff_run_after.log; \
pkill -9 -x firefox-esr 2>/dev/null; chvt 7 2>/dev/null; \
for f in /sys/class/display/*/enable; do echo 1 > "$f" 2>/dev/null; done; \
echo "== vt/hdmi =="; cat /sys/class/tty/tty0/active; cat /sys/class/display/HDMI/enable'
