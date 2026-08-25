#!/bin/bash
# Ren WebGL-verifikation på boks 1 med stub-libGL-fixet (uden trace-lib).
#   LD_LIBRARY_PATH=/opt/hybris:/root/egl_trace  → rigtig wrapper + stub libGL
#   LD_PRELOAD=... /root/egl_trace/libGL.so       → shadow Mesas libGL.so
# Tjek: vinduestitel (WEBGL <renderer>), GL-version i log, Fallback-linjer,
#       og fbdump af det faktiske billede.
set -uo pipefail

BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key

ssh -i "$KEY" -o ConnectTimeout=8 "$BOX" 'pkill -9 -x firefox-esr 2>/dev/null; sleep 1; \
chvt 7 2>/dev/null; for f in /sys/class/display/*/enable; do echo 1 > "$f" 2>/dev/null; done; \
cd /root && (timeout 75 env LD_PRELOAD="/root/system_shim.so /root/egl_platform_shim.so /root/egl_trace/libGL.so" \
LD_LIBRARY_PATH=/opt/hybris:/root/egl_trace EGL_PLATFORM=x11 DISPLAY=:0 \
MOZ_X11_EGL=1 MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_DISABLE_GPU_SANDBOX=1 \
MOZ_GL_SPEW=1 \
/usr/lib/firefox-esr/firefox-esr -no-remote -profile /root/ffprof \
file:///root/webgl_test.html > /root/ff_verify.log 2>&1 </dev/null &); \
sleep 50; \
echo "== vinduer (xwininfo) =="; \
DISPLAY=:0 xwininfo -root -tree 2>/dev/null | grep -iE "firefox|webgl" | head -10; \
echo "== titel (xprop) =="; \
for w in $(DISPLAY=:0 xwininfo -root -tree 2>/dev/null | grep -iE "firefox" | awk "{print \$1}"); do \
  DISPLAY=:0 xprop -id "$w" WM_NAME _NET_WM_NAME 2>/dev/null | head -2; \
done; \
sleep 15; \
echo "== fbdump =="; \
dd if=/dev/fb0 of=/root/ff_fb.raw bs=3840 count=1080 2>/dev/null; ls -la /root/ff_fb.raw; \
pkill -9 -x firefox-esr 2>/dev/null; sleep 1; \
echo "== log-nøglelinjer =="; \
grep -aE "GL version detected|GLSL version|OpenGL vendor|OpenGL renderer|Failed to create EGLContext|Fallback WR|FEATURE_FAILURE|Failed GL context|x11ws: vindue|x11ws: .*buffer|present" /root/ff_verify.log | head -30; \
chvt 7 2>/dev/null; for f in /sys/class/display/*/enable; do echo 1 > "$f" 2>/dev/null; done'
