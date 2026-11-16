#!/bin/bash
# Hent den kørende Firefox/WebGL-runtime fra en kendt god GeekBox.
#
# Brug:
#   bash devuan/gpu/export_game_kit.sh <kilde-ip> [output-dir]
#
# system.img og vendor-broerne hentes separat af gpu_setup.sh fra vendor_root.
# Dette kit indeholder den boksspecifikke proxy, X11-platformen, Firefox-shims,
# hjælpescripts og en minimal, spilklar Firefox-profil.
set -euo pipefail

SOURCE=${1:?brug: export_game_kit.sh <kilde-ip> [output-dir]}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${2:-$HERE/game-kit}
KEY=${SSHKEY:-$HOME/.ssh/geekbox_key}
SSH_OPTS=(-F /dev/null -i "$KEY" -o BatchMode=yes
          -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null)

ssh "${SSH_OPTS[@]}" "root@$SOURCE" 'test "$(id -u)" = 0'
if ssh "${SSH_OPTS[@]}" "root@$SOURCE" \
    'pgrep -x firefox-esr >/dev/null 2>&1'; then
    echo "FEJL: luk Firefox på kildeboksen før profilen eksporteres." >&2
    exit 1
fi

mkdir -p "$OUT"
ARCHIVE=$OUT/game-runtime.tar.gz

PATHS=(
    opt/hybris/libEGL.so.1.0.0
    usr/local/lib/firefox-webgl
    usr/local/lib/libhybris/eglplatform_x11.so
    root/start_game_all.sh
    root/play_fast_game.sh
    root/start_cyan_probe.sh
    root/ensure_audio.sh
    root/bidi_cyan.py
    root/kill_firefox.sh
    root/kill_bidi.sh
    root/x_resize
    root/gpu_up.sh
    root/pvrsrvkm_leddaz.ko
    root/egl_proxy_dcc0a68f.so.bak
    root/libEGL_android.orig
    home/kristian/ffprof
)

EXCLUDES=(
    --exclude=home/kristian/ffprof/cache2
    --exclude=home/kristian/ffprof/startupCache
    --exclude=home/kristian/ffprof/minidumps
    --exclude=home/kristian/ffprof/crashes
    --exclude=home/kristian/ffprof/security_state
    --exclude=home/kristian/ffprof/safebrowsing
    --exclude=home/kristian/ffprof/datareporting
    --exclude=home/kristian/ffprof/sessionstore-backups
    --exclude=home/kristian/ffprof/places.sqlite
    --exclude=home/kristian/ffprof/favicons.sqlite
    --exclude=home/kristian/ffprof/cookies.sqlite
    --exclude=home/kristian/ffprof/cookies.sqlite-wal
    --exclude=home/kristian/ffprof/cookies.sqlite-shm
    --exclude=home/kristian/ffprof/.parentlock
    --exclude=home/kristian/ffprof/lock
)

REMOTE_ARCHIVE=/tmp/geekbox-game-runtime.$$.tar.gz
REMOTE_TAR="tar -C / --ignore-failed-read ${EXCLUDES[*]} -czf $REMOTE_ARCHIVE ${PATHS[*]}"
ssh "${SSH_OPTS[@]}" "root@$SOURCE" "$REMOTE_TAR"
scp -F /dev/null -i "$KEY" -o StrictHostKeyChecking=no \
    -o UserKnownHostsFile=/dev/null \
    "root@$SOURCE:$REMOTE_ARCHIVE" "$ARCHIVE"
ssh "${SSH_OPTS[@]}" "root@$SOURCE" "rm -f $REMOTE_ARCHIVE"

{
    echo "source=$SOURCE"
    echo "created=$(date -u '+%FT%TZ')"
    echo "archive_bytes=$(stat -c %s "$ARCHIVE")"
    ssh "${SSH_OPTS[@]}" "root@$SOURCE" \
        'sha256sum /root/start_game_all.sh /root/play_fast_game.sh \
         /root/start_cyan_probe.sh /usr/local/lib/libhybris/eglplatform_x11.so \
         /root/egl_proxy_dcc0a68f.so.bak /root/pvrsrvkm_leddaz.ko'
} >"$OUT/manifest.txt"

gzip -t "$ARCHIVE"
echo "Game-kit klar: $ARCHIVE"
echo "Manifest: $OUT/manifest.txt"
