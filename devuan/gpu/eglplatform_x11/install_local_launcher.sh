#!/bin/bash
# install_local_launcher.sh - installér ét-kliks lokal spilstart på GeekBox.
#
# Kør fra laptoppen:
#   BOXIP=<ip> bash devuan/gpu/eglplatform_x11/install_local_launcher.sh
set -euo pipefail

REPO=$(cd "$(dirname "$0")/../../.." && pwd)
TXTS="$REPO/devuan/gpu/eglplatform_x11"

box() {
    bash "$REPO/devuan/box.sh" "$@"
}

box --put "$TXTS/start_game_all.sh" /root/start_game_all.sh
box --put "$TXTS/subway-surfers-launcher.sh" \
    /usr/local/bin/subway-surfers
box --put "$TXTS/subway-surfers.desktop" \
    "/usr/share/applications/subway-surfers.desktop"
box --put "$TXTS/subway-surfers.desktop" \
    "/home/kristian/Desktop/subway-surfers.desktop"
box --put "$TXTS/sudoers-geekbox-game" /tmp/geekbox-game.sudoers

box "
set -e
install -m 0755 /root/start_game_all.sh /root/start_game_all.sh
install -m 0755 /usr/local/bin/subway-surfers \
    /usr/local/bin/subway-surfers
install -m 0644 /usr/share/applications/subway-surfers.desktop \
    /usr/share/applications/subway-surfers.desktop
install -m 0755 /home/kristian/Desktop/subway-surfers.desktop \
    /home/kristian/Desktop/subway-surfers.desktop
chown kristian:kristian /home/kristian/Desktop/subway-surfers.desktop
visudo -cf /tmp/geekbox-game.sudoers >/dev/null
install -m 0440 -o root -g root /tmp/geekbox-game.sudoers \
    /etc/sudoers.d/geekbox-game
rm -f /tmp/geekbox-game.sudoers
"

echo "Installeret: Subway Surfers på skrivebordet og i programmenuen."
