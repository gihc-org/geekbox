#!/bin/bash
# Provisionér en nyflashet GeekBox med den kendte GPU-/Firefox-/spilruntime.
#
# Forudsætning: basen er flashet med devuan/testflash.sh, boksen er bootet, og
# SSH virker som root.
#
# Brug:
#   bash devuan/gpu/provision_game_box.sh <ny-ip> <kilde-ip>
#
# Kør scriptet fra laptoppen. system.img og vendor-broerne tages fra
# vendor_root; resten eksporteres fra den kendte gode boks.
set -euo pipefail

TARGET=${1:?brug: provision_game_box.sh <ny-ip> <kilde-ip>}
SOURCE=${2:?brug: provision_game_box.sh <ny-ip> <kilde-ip>}
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
KEY=${SSHKEY:-$HOME/.ssh/geekbox_key}
KIT=${GAME_KIT_DIR:-$HERE/game-kit}
SSH_OPTS=(-F /dev/null -i "$KEY" -o BatchMode=yes
          -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null)
SCP_OPTS=(-F /dev/null -i "$KEY" -o StrictHostKeyChecking=no
          -o UserKnownHostsFile=/dev/null)

ssh "${SSH_OPTS[@]}" "root@$TARGET" 'test "$(id -u)" = 0'
ssh "${SSH_OPTS[@]}" "root@$SOURCE" 'test "$(id -u)" = 0'

echo "== 1/6: GPU-system.img og vendor-broer =="
bash "$HERE/gpu_setup.sh" "$TARGET" "$SOURCE"

echo "== 2/6: eksportér game-runtime fra $SOURCE =="
bash "$HERE/export_game_kit.sh" "$SOURCE" "$KIT"

echo "== 3/6: installér game-runtime på $TARGET =="
scp "${SCP_OPTS[@]}" "$KIT/game-runtime.tar.gz" \
    "root@$TARGET:/tmp/game-runtime.tar.gz"
ssh "${SSH_OPTS[@]}" "root@$TARGET" \
    'tar --overwrite -xzf /tmp/game-runtime.tar.gz -C / && rm -f /tmp/game-runtime.tar.gz'

echo "== 4/6: installér fælles launcher og ikon =="
BOXIP=$TARGET bash "$HERE/install_local_launcher.sh"

echo "== 5/6: installer aktuel myinit og tidsfix =="
ssh "${SSH_OPTS[@]}" "root@$TARGET" 'mkdir -p /usr/local/sbin'
scp "${SCP_OPTS[@]}" "$REPO/devuan/myinit.sh" "$TARGET:/root/myinit.sh"
scp "${SCP_OPTS[@]}" "$REPO/devuan/clock_sync.sh" \
    "$TARGET:/usr/local/sbin/geekbox-clock-sync"
scp "${SCP_OPTS[@]}" "$REPO/devuan/chrony-geekbox.sources" \
    "$TARGET:/etc/chrony/sources.d/geekbox-fixed.sources"

ssh "${SSH_OPTS[@]}" "root@$TARGET" '
set -e
chmod 755 /root/myinit.sh /usr/local/sbin/geekbox-clock-sync
getent group inet >/dev/null || groupadd -g 3003 inet
usermod -g inet _chrony
service chrony restart
geekbox-clock-sync sync || true
chown -R kristian:kristian /home/kristian/ffprof
chmod 755 /root/start_game_all.sh /root/play_fast_game.sh \
    /root/start_cyan_probe.sh /root/ensure_audio.sh /root/kill_firefox.sh \
    /root/kill_bidi.sh /root/x_resize /root/gpu_up.sh
chmod 755 /usr/local/lib/libhybris/eglplatform_x11.so
visudo -cf /etc/sudoers.d/geekbox-game >/dev/null
flock -n /run/lock/geekbox-subway-surfers.lock true
'

echo "== 6/6: verifikation =="
ssh "${SSH_OPTS[@]}" "root@$TARGET" '
set -e
date "+tid: %F %T %Z"
test -f /usr/local/share/libhybris/system.img
test -x /root/start_game_all.sh
test -x /usr/local/bin/subway-surfers
test -f /usr/local/lib/libhybris/eglplatform_x11.so
test -f /usr/local/lib/firefox-webgl/system_shim.so
test -f /home/kristian/ffprof/user.js
test "$(id -g _chrony)" = 3003
echo "runtime installeret"
'

cat <<EOF
FÆRDIG.

Næste skridt:
  1. Klik "Subway Surfers" på skrivebordet, eller kør:
       runuser -u kristian -- env HOME=/home/kristian USER=kristian \
         LOGNAME=kristian DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/1000 \
         LANG=da_DK.UTF-8 /usr/local/bin/subway-surfers
  2. Ved næste reboot sætter myinit stabil MAC fra CPU-serial og gendanner/
     synkroniserer tiden via gatewayens HTTP-Date.

Diagnostik på den nye boks:
  /tmp/geekbox-start-game.log
  /root/clock-sync.log
