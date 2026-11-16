#!/bin/bash
# Kør startkæden inde i lxterminal og hold fejlen synlig for brugeren.

set -u

sudo -n /root/start_game_all.sh
rc=$?

if [ "$rc" -ne 0 ]; then
    printf '\nSubway Surfers kunne ikke starte (kode %s).\n' "$rc"
    printf 'Log: /tmp/geekbox-start-game.log\n'
    printf 'Tryk Enter for at lukke terminalen...'
    read -r _
fi

exit "$rc"
