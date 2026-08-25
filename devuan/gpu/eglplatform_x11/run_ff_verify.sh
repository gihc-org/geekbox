#!/bin/bash
# Ren WebGL-verifikation på boks 1 — STABIL opskrift (25. aug 2026).
#   LD_LIBRARY_PATH=/opt/hybris:/root/glstub  → rigtig wrapper + stub libGL
#   INGEN MOZ_GL_SPEW                         → MOZ_GL_SPEW lammer compositoren
#                                               (KHR_debug-callback, fælde 25)
#   Profilen har layers.gpu-process.enabled=true (separat GPU-proces virker)
# Tjek: WEBGL_RESULT, x11ws present #2 (1280x948), vinduestitel, 0 X-fejl,
#       fbdump af det faktiske billede, VT/HDMI efter oprydning.
set -uo pipefail

BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key

ssh -i "$KEY" -o ConnectTimeout=8 "$BOX" 'pkill -9 -x firefox-esr 2>/dev/null; sleep 1; \
chvt 7 2>/dev/null; for f in /sys/class/display/*/enable; do echo 1 > "$f" 2>/dev/null; done; \
rm -f /root/ffprof/.parentlock /root/ffprof/lock; \
cd /root && (timeout 90 env LD_PRELOAD="/root/system_shim.so /root/egl_platform_shim.so" \
LD_LIBRARY_PATH=/opt/hybris:/root/glstub EGL_PLATFORM=x11 DISPLAY=:0 \
MOZ_X11_EGL=1 MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_DISABLE_GPU_SANDBOX=1 \
/usr/lib/firefox-esr/firefox-esr -no-remote -profile /root/ffprof \
file:///root/webgl_test_dump.html > /root/ff_verify.log 2>&1 </dev/null &); \
sleep 50; \
echo "== vinduer (xwininfo) =="; \
DISPLAY=:0 xwininfo -root -tree 2>/dev/null | grep -iE "firefox|webgl" | head -10; \
echo "== titel (xprop) =="; \
for w in $(DISPLAY=:0 xwininfo -root -tree 2>/dev/null | grep -iE "navigator" | awk "{print \$1}"); do \
  DISPLAY=:0 xprop -id "$w" WM_NAME _NET_WM_NAME 2>/dev/null | head -2; \
done; \
sleep 10; \
echo "== fbdump =="; \
dd if=/dev/fb0 of=/root/ff_fb.raw bs=3840 count=1080 2>/dev/null; ls -la /root/ff_fb.raw; \
pkill -9 -x firefox-esr 2>/dev/null; sleep 1; \
echo "== log-nøglelinjer =="; \
grep -aE "WEBGL_RESULT|x11ws: vindue|x11ws: ændret|x11ws: .*buffer|x11ws: present" /root/ff_verify.log | head -20; \
echo "== X-fejl-antal (skal være 0) =="; grep -acE "X-fejl" /root/ff_verify.log; \
chvt 7 2>/dev/null; for f in /sys/class/display/*/enable; do echo 1 > "$f" 2>/dev/null; done'
