#!/bin/bash
# Kører firefox-esr med den loggende libEGL.so.1 (LD_LIBRARY_PATH forrest).
set -uo pipefail

BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key

ssh -i "$KEY" -o ConnectTimeout=8 "$BOX" 'pkill -9 -x firefox-esr 2>/dev/null; sleep 1; \
cd /root && (timeout 90 env LD_PRELOAD="/root/system_shim.so /root/egl_platform_shim.so /root/egl_trace/libGL.so" \
LD_LIBRARY_PATH=/root/egl_trace:/opt/hybris EGL_PLATFORM=x11 DISPLAY=:0 \
MOZ_X11_EGL=1 MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_DISABLE_GPU_SANDBOX=1 \
MOZ_GL_SPEW=1 \
/usr/lib/firefox-esr/firefox-esr -no-remote -profile /root/ffprof \
file:///root/webgl_test.html > /root/ff_eglshim.log 2>&1 </dev/null &); \
sleep 95; pkill -9 -x firefox-esr 2>/dev/null; sleep 2; \
echo "== create/makecurrent/error/init-linjer =="; \
grep -aE "EGLSHIM.*egl(CreateContext|MakeCurrent|GetError|BindAPI|Initialize|Terminate|CreatePbufferSurface|CreateWindowSurface|QueryString|GetCurrentContext|ReleaseThread)|Failed to create EGLContext|Fallback WR|FEATURE" /root/ff_eglshim.log | head -120; \
echo "== antal =="; \
for p in "eglCreateContext" "eglMakeCurrent" "eglGetError" "eglBindAPI" "eglCreatePbufferSurface" "eglCreateWindowSurface" "eglInitialize"; do \
  printf "%s %s\n" "$p" "$(grep -ac "EGLSHIM.*$p" /root/ff_eglshim.log)"; done; \
pkill -9 -x firefox-esr 2>/dev/null; \
chvt 7 2>/dev/null; \
for f in /sys/class/display/*/enable; do echo 1 > "$f" 2>/dev/null; done; \
echo "== vt/hdmi =="; cat /sys/class/tty/tty0/active; cat /sys/class/display/HDMI/enable'
