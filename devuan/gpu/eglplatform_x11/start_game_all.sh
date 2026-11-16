#!/bin/bash
# start_game_all.sh - lokal, idempotent start af Subway Surfers på GeekBox.
#
# Kør som root på boksen, typisk via skrivebordsikonet:
#   sudo -n /root/start_game_all.sh
#
# Scriptet bringer GPU, bind-api-lappen, vnext12-proxyen, VT og lyd op og
# kalder derefter den kendte fast-play-launcher. Det venter på X og DNS, men
# ændrer ikke Firefox-profilens user.js.

set -euo pipefail

export DISPLAY="${DISPLAY:-:0}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/1000}"

PROFILE_USER=${PROFILE_USER:-kristian}
PROFILE_HOME=${PROFILE_HOME:-/home/kristian}
PROXY=/opt/hybris/libEGL.so.1.0.0
PROXY_BACKUP=/root/egl_proxy_dcc0a68f.so.bak
PROXY_MD5=dcc0a68f175b21590eb9efbce4c8ce44
ANDROID_EGL=/system/lib/libEGL.so
ANDROID_ORIG=/root/libEGL_android.orig
ANDROID_PATCHED=/root/egl_patch/libEGL.so
LOG=${START_GAME_LOG:-/tmp/geekbox-start-game.log}

say() {
    printf 'start_game_all: %s\n' "$*"
}

die() {
    printf 'start_game_all: FEJL: %s\n' "$*" >&2
    exit 1
}

[ "$(id -u)" -eq 0 ] || die "skal køres som root"
[ "$(id -u "$PROFILE_USER")" -eq 1000 ] \
    || die "brugeren $PROFILE_USER skal have uid 1000"
command -v xdpyinfo >/dev/null 2>&1 || die "xdpyinfo mangler"
command -v mountpoint >/dev/null 2>&1 || die "mountpoint mangler"
command -v flock >/dev/null 2>&1 || die "flock mangler"

mkdir -p "$(dirname "$LOG")"
touch "$LOG"
chown "$PROFILE_USER:$PROFILE_USER" "$LOG"
exec > >(tee -a "$LOG") 2>&1

say "starter $(date '+%F %T %Z')"

LOCK=/run/lock/geekbox-subway-surfers.lock
exec 9>"$LOCK"
flock -n 9 || die "en anden start kører allerede"

if pgrep -x firefox-esr >/dev/null 2>&1; then
    say "Firefox kører allerede; luk vinduet før en ny start"
    sleep 3
    exit 0
fi

for _ in $(seq 1 30); do
    DISPLAY=:0 xdpyinfo >/dev/null 2>&1 && break
    sleep 1
done
DISPLAY=:0 xdpyinfo >/dev/null 2>&1 || die "X svarer ikke på DISPLAY=:0"

for _ in $(seq 1 30); do
    getent hosts poki.com >/dev/null 2>&1 && break
    sleep 1
done
getent hosts poki.com >/dev/null 2>&1 \
    || die "DNS kan ikke finde poki.com; kontrollér netværk/rute"

YEAR=$(date +%Y)
if [ "$YEAR" -lt 2026 ]; then
    say "uret står på $YEAR; synkroniserer via HTTP-Date"
    /usr/local/sbin/geekbox-clock-sync sync || true
    say "ur efter synkronisering: $(date '+%F %T %Z')"
fi

SYSTEM_IMG=/usr/local/share/libhybris/system.img
if [ -f "$SYSTEM_IMG" ] && ! mountpoint -q /system; then
    say "monterer $SYSTEM_IMG på /system"
    mkdir -p /system
    mount -o loop,ro "$SYSTEM_IMG" /system
    ln -sfn /system/vendor /vendor
fi

say "bringer GPU op"
lsmod | grep -q '^pvrsrvkm' || insmod /root/pvrsrvkm_leddaz.ko
sh /root/gpu_up.sh 9>&-
chmod 666 /dev/sw_sync 2>/dev/null || true

say "installerer vnext12-proxyen"
[ -r "$PROXY_BACKUP" ] || die "$PROXY_BACKUP mangler"
cp -f "$PROXY_BACKUP" "$PROXY"
PROXY_NOW=$(md5sum "$PROXY" | awk '{print $1}')
[ "$PROXY_NOW" = "$PROXY_MD5" ] \
    || die "uventet proxy-md5: $PROXY_NOW"

say "klargør Android bind-api-lappen"
mkdir -p "$(dirname "$ANDROID_PATCHED")"
if [ ! -f "$ANDROID_ORIG" ]; then
    mountpoint -q "$ANDROID_EGL" \
        && die "$ANDROID_ORIG mangler, men $ANDROID_EGL er allerede et mountpoint"
    cp -a "$ANDROID_EGL" "$ANDROID_ORIG"
fi
cp -a "$ANDROID_ORIG" "$ANDROID_PATCHED"

# Samme binære lapper som patch_android_bindapi.sh, men uden SSH.
printf '\x43\xf2\xa0\x00\x88\x47\x70\xbd' | \
    dd of="$ANDROID_PATCHED" bs=1 seek=73208 count=8 conv=notrunc status=none
printf '\x03\xe0' | \
    dd of="$ANDROID_PATCHED" bs=1 seek=70338 count=2 conv=notrunc status=none
printf '\x06\xe0' | \
    dd of="$ANDROID_PATCHED" bs=1 seek=70332 count=2 conv=notrunc status=none
printf '\x43\xf2\x40\x02' | \
    dd of="$ANDROID_PATCHED" bs=1 seek=70350 count=4 conv=notrunc status=none
printf '\x44\x20' | \
    dd of="$ANDROID_PATCHED" bs=1 seek=70358 count=2 conv=notrunc status=none
printf '\x43\xf2\x38\x03' | \
    dd of="$ANDROID_PATCHED" bs=1 seek=70360 count=4 conv=notrunc status=none
printf '\x00\x20\x00\xbf' | \
    dd of="$ANDROID_PATCHED" bs=1 seek=70280 count=4 conv=notrunc status=none
printf '\x02\xe0' | \
    dd of="$ANDROID_PATCHED" bs=1 seek=70340 count=2 conv=notrunc status=none

mountpoint -q "$ANDROID_EGL" \
    || mount --bind "$ANDROID_PATCHED" "$ANDROID_EGL"
mountpoint -q "$ANDROID_EGL" \
    || die "kunne ikke bind-mounte $ANDROID_PATCHED"

say "sikrer aktiv X-VT"
XVT=$(pgrep -ax Xorg | grep -o 'vt[0-9]*' | head -1 || true)
ACTIVE=$(cat /sys/class/tty/tty0/active 2>/dev/null || true)
if [ -n "$XVT" ] && [ "tty${XVT#vt}" != "$ACTIVE" ]; then
    chvt "${XVT#vt}" 2>/dev/null || true
    say "skiftede VT fra $ACTIVE til $XVT"
fi
for f in /sys/class/display/*/enable; do
    [ -e "$f" ] && echo 1 >"$f" 2>/dev/null || true
done

say "kontrollerer lydstakken"
bash /root/ensure_audio.sh 9>&-

say "starter fast-play"
RESTART="${RESTART:-0}" bash /root/play_fast_game.sh 9>&-

say "færdig $(date '+%F %T %Z')"
