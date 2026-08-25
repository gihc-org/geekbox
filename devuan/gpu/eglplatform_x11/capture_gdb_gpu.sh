#!/bin/bash
# capture_gdb_gpu.sh — vent på GPU-processen og attach gdb med
# gdb_wr_reset.cmd for at fange GL/EGL-kald omkring WR_POST_UPDATE-reset.
# Køres på boksen efter at Firefox er startet.
#
#   bash /tmp/capture_gdb_gpu.sh &
#
set -u

MAXWAIT=120
for _ in $(seq 1 $MAXWAIT); do
    GPID=$(pgrep -f "contentproc.* gpu" | head -1)
    [ -n "$GPID" ] && break
    sleep 1
done
if [ -z "$GPID" ]; then
    echo "capture_gdb_gpu: ingen GPU-proces fundet efter ${MAXWAIT}s" >> /tmp/gdb_wr_reset.log
    exit 1
fi

# Vent til vendor-bibliotekerne er mappet i GPU-processen (Android-linkeren
# læser dem, og gdb skal have baseadresserne for at sætte korrekte breakpoints).
for _ in $(seq 1 60); do
    GLES2=$(grep -m1 "libGLESv2_POWERVR_ROGUE.so" /proc/$GPID/maps | awk '{print $1}' | cut -d- -f1)
    EGL=$(grep -m1 "libEGL_POWERVR_ROGUE.so" /proc/$GPID/maps | awk '{print $1}' | cut -d- -f1)
    [ -n "$GLES2" ] && [ -n "$EGL" ] && break
    sleep 1
done
if [ -z "$GLES2" ] || [ -z "$EGL" ]; then
    echo "capture_gdb_gpu: vendor-biblioteker ikke fundet i /proc/$GPID/maps" >> /tmp/gdb_wr_reset.log
    exit 1
fi
echo "capture_gdb_gpu: GLES2_BASE=$GLES2 EGL_BASE=$EGL" >> /tmp/gdb_wr_reset.log

echo "capture_gdb_gpu: attach til GPU-proces $GPID $(date +%H:%M:%S)" >> /tmp/gdb_wr_reset.log
GLES2_ADDR=$(printf '0x%s' "$GLES2")
EGL_ADDR=$(printf '0x%s' "$EGL")
GLES2_ERROR=$(printf '0x%x' $((0x$GLES2 + 0x2308c)))
sed -e "s/@GLES2_BASE@/${GLES2_ADDR}/" \
    -e "s/@EGL_BASE@/${EGL_ADDR}/" \
    -e "s/@GLES2_ERROR@/${GLES2_ERROR}/" \
    /tmp/gdb_wr_reset.cmd > /tmp/gdb_wr_reset.$GPID.cmd
exec gdb -q -p "$GPID" -x /tmp/gdb_wr_reset.$GPID.cmd -batch
