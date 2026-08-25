#!/bin/bash
# capture_egl_makecurrent.sh — fang eglMakeCurrent-returværdier i GPU-processen
# ved at break'e på libEGL_POWERVR_ROGUE's eglMakeCurrent (offset 0x11d4).
# Bruges til at teste hypotesen: WR_POST_UPDATE-reset uden PVR-fault = 
# MakeCurrent(true)-fejl → fGetGraphicsResetStatus()=UNKNOWN (ESR 140-kilde).
#
#   bash /tmp/capture_egl_makecurrent.sh &
#
set -u

MAXWAIT=120
for _ in $(seq 1 $MAXWAIT); do
    GPID=$(pgrep -f "contentproc.* gpu" | head -1)
    [ -n "$GPID" ] && break
    sleep 1
done
if [ -z "$GPID" ]; then
    echo "capture_egl_makecurrent: ingen GPU-proces fundet" >> /tmp/egl_mc.log
    exit 1
fi

for _ in $(seq 1 60); do
    EGL=$(grep -m1 "libEGL_POWERVR_ROGUE.so" /proc/$GPID/maps | awk '{print $1}' | cut -d- -f1)
    [ -n "$EGL" ] && break
    sleep 1
done
if [ -z "$EGL" ]; then
    echo "capture_egl_makecurrent: libEGL_POWERVR ikke fundet" >> /tmp/egl_mc.log
    exit 1
fi

cat > /tmp/egl_mc.cmd <<EOF
set pagination off
set confirm off
set \$mc = 0
set logging overwrite on
set logging file /tmp/egl_mc.log
set logging on
break *0x${EGL} + 0x11d4
commands
  silent
  set \$mc = \$mc + 1
  printf "eglMakeCurrent #%d ret=%u tid=%d\\n", \$mc, \$r0, \$_thread
  if \$r0 == 0
    printf "  *** EGL_FALSE ***\\n"
    bt 5
  end
  continue
end
continue
EOF

echo "capture_egl_makecurrent: attach $GPID EGL_BASE=$EGL $(date +%H:%M:%S)" >> /tmp/egl_mc.log
exec gdb -q -p "$GPID" -x /tmp/egl_mc.cmd -batch
