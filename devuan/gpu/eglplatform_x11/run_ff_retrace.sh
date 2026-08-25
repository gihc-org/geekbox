#!/bin/bash
# Kører firefox-esr med egl_ret_trace.so (argumenter + returværdier).
set -uo pipefail

BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key

ssh -i "$KEY" -o ConnectTimeout=8 "$BOX" 'pkill -9 -x firefox-esr 2>/dev/null; sleep 1; \
cd /root && timeout 90 env LD_PRELOAD="/root/system_shim.so /root/egl_platform_shim.so /root/egl_ret_trace.so" \
LD_LIBRARY_PATH=/opt/hybris EGL_PLATFORM=x11 DISPLAY=:0 MOZ_X11_EGL=1 \
MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_DISABLE_GPU_SANDBOX=1 \
MOZ_GL_SPEW=1 \
/usr/lib/firefox-esr/firefox-esr -no-remote -profile /root/ffprof \
file:///root/webgl_test.html > /root/ff_ret_trace.log 2>&1; \
echo "== create/makecurrent/error-linjer =="; \
grep -aE "EGLTRC.*egl(CreateContext|MakeCurrent|GetError|BindAPI|CreatePbufferSurface)|Failed to create EGLContext|Fallback WR|FEATURE" /root/ff_ret_trace.log | head -80; \
echo "== antal =="; \
for p in "eglCreateContext" "eglMakeCurrent" "eglGetError" "eglBindAPI" "eglCreatePbufferSurface"; do \
  printf "%s %s\n" "$p" "$(grep -ac "EGLTRC.*$p" /root/ff_ret_trace.log)"; done; \
pkill -9 -x firefox-esr 2>/dev/null; \
chvt 7 2>/dev/null; \
for f in /sys/class/display/*/enable; do echo 1 > "$f" 2>/dev/null; done; \
echo "== vt/hdmi =="; cat /sys/class/tty/tty0/active; cat /sys/class/display/HDMI/enable'
