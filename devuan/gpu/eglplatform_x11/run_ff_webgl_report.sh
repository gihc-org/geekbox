#!/bin/bash
# Kører firefox-esr mod webgl_test_dump.html og fanger window.dump()-resultatet.
set -uo pipefail

BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key

scp -i "$KEY" devuan/gpu/eglplatform_x11/webgl_test_dump.html "$BOX:/root/webgl_test_dump.html"

ssh -i "$KEY" -o ConnectTimeout=8 "$BOX" 'pkill -9 -x firefox-esr 2>/dev/null; sleep 1; \
grep -q "browser.dom.window.dump.enabled" /root/ffprof/prefs.js || \
  echo "user_pref(\"browser.dom.window.dump.enabled\", true);" >> /root/ffprof/prefs.js; \
chvt 7 2>/dev/null; for f in /sys/class/display/*/enable; do echo 1 > "$f" 2>/dev/null; done; \
cd /root && (timeout 75 env LD_PRELOAD="/root/system_shim.so /root/egl_platform_shim.so /root/egl_trace/libGL.so" \
LD_LIBRARY_PATH=/opt/hybris:/root/egl_trace EGL_PLATFORM=x11 DISPLAY=:0 \
MOZ_X11_EGL=1 MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_DISABLE_GPU_SANDBOX=1 \
MOZ_GL_SPEW=1 \
/usr/lib/firefox-esr/firefox-esr -no-remote -profile /root/ffprof \
file:///root/webgl_test_dump.html > /root/ff_webgl_report.log 2>&1 </dev/null &); \
sleep 55; \
echo "== WEBGL_RESULT =="; grep -a "WEBGL_RESULT" /root/ff_webgl_report.log | head -5; \
echo "== titler =="; DISPLAY=:0 xwininfo -root -tree 2>/dev/null | grep -iE "mozilla|webgl|firefox" | head -8; \
echo "== GL-linjer =="; grep -aE "GL version detected|OpenGL renderer|Failed to create EGLContext|Fallback WR|FEATURE_FAILURE" /root/ff_webgl_report.log | head -10; \
pkill -9 -x firefox-esr 2>/dev/null; sleep 1; \
chvt 7 2>/dev/null; for f in /sys/class/display/*/enable; do echo 1 > "$f" 2>/dev/null; done'
