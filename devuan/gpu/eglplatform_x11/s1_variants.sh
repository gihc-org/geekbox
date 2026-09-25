#!/bin/bash
# s1_variants.sh — kør S1 (Firefox' kompositor-vej) som A/B på GeekBox'en.
# Se docs/grafik/fps-analysen-2026-09-25.md §5 S1 og §9.1.
#
#   bash s1_variants.sh backup                 # backup af user.js (tidsstempel)
#   bash s1_variants.sh prefs V2               # læg variantens prefs i user.js
#   bash s1_variants.sh restore <ts>           # genskab backup
#   bash s1_variants.sh run <tag> V2 110 [url] [win] [settle] [fbsec]
#   bash s1_variants.sh wait <tag> [maxmin]    # vent til kørslen er færdig
#   bash s1_variants.sh result <tag>           # vis måltallene
#
# Alle ssh-kald går gennem devuan/box.sh (BoxIP/find_box.sh).
set -uo pipefail

REPO=$(cd "$(dirname "$0")/../../.." && pwd)
BOX="bash $REPO/devuan/box.sh"
PREFS=$(cd "$(dirname "$0")" && pwd)/s1_prefs
PROFILE=/home/kristian/ffprof
# MOZ_LOG: standard tom. Målt 25. sep 2026 giver Compositor/LayerManager/
# WebRender NUL linjer i denne ESR-140-build (kun Widget:5 og
# nsRefreshDriver:5 skriver noget, og sidstnævnte druknede loggen med 18.547
# linjer), så kompositor-vejen aflæses i stedet af proces-snapshottet
# (s1_snapshot.sh: GPU-proces + hybris-libEGL + probe.log-GL-aktivitet).
MOZLOG=${MOZLOG:-}
# FBSEC/SETTLE kan sættes udefra (fx SETTLE=90 for at give brugeren tid til at
# trykke play i spillet før målingen starter).
FBSEC=${FBSEC:-40}
SETTLE=${SETTLE:-40}

cmd=${1:-help}; shift || true

case "$cmd" in
backup)
    TS=$(date +%Y%m%d-%H%M%S)
    $BOX "cp -f $PROFILE/user.js /root/user.js.orig.$TS && md5sum /root/user.js.orig.$TS $PROFILE/user.js"
    echo "backup: /root/user.js.orig.$TS" ;;

prefs)
    V=${1:?brug: prefs V1|V2|V3|V4}
    SRC="$PREFS/$V.user.js"
    [ -f "$SRC" ] || { echo "ukendt variant: $V" >&2; exit 2; }
    $BOX "cp -f $PROFILE/user.js /root/user.js.pre-s1.$V 2>/dev/null; true"
    bash "$REPO/devuan/box.sh" --put "$SRC" "$PROFILE/user.js"
    $BOX "md5sum $PROFILE/user.js; grep -c user_pref $PROFILE/user.js" ;;

restore)
    TS=${1:?brug: restore <ts>}
    $BOX "cp -f /root/user.js.orig.$TS $PROFILE/user.js && md5sum $PROFILE/user.js" ;;

run)
    TAG=${1:?brug: run <tag> <variant> <sek> [url] [win]}
    V=${2:?variant mangler}
    SECS=${3:-110}
    URL=${4:-https://poki.com/en/g/subway-surfers}
    WIN=${5:-}
    SETTLE=${6:-$SETTLE}
    FBSEC=${7:-$FBSEC}
    echo "=== run $TAG ($V, $SECS s, settle=$SETTLE fbsec=$FBSEC, url=$URL win='$WIN') $(date +%H:%M:%S)"
    bash "$REPO/devuan/box.sh" --put "$(dirname "$0")/s1_snapshot.sh" /root/s1_snapshot.sh
    $BOX "setsid nohup env ${MOZLOG:+MOZ_LOG='$MOZLOG' }\
        SHIM_NO_DISPLAY_DANCE=1 SHIM_NO_CHVT=1 FBSEC=$FBSEC SETTLE=$SETTLE POLL=20 \
        ${WIN:+WIN=\"$WIN\" }bash /root/cyan_ab_run.sh $TAG $SECS \"\" \"\" '$URL' \
        > /tmp/$TAG.out 2>&1 < /dev/null & \
        setsid nohup bash /root/s1_snapshot.sh $TAG 75 >/dev/null 2>&1 < /dev/null & \
        echo 'startet: $TAG'" ;;

wait)
    TAG=${1:?brug: wait <tag> [maxmin]}
    MAXM=${2:-6}
    for i in $(seq 1 $((MAXM*6))); do
        if $BOX "grep -qa 'faerdig' /tmp/$TAG.out 2>/dev/null" 2>/dev/null; then
            echo "--- $TAG faerdig efter ~$((i*10/60))m$((i*10%60))s"; exit 0
        fi
        sleep 10
    done
    echo "TIMEOUT: $TAG kører stadig (se /tmp/$TAG.out)" >&2; exit 1 ;;

result)
    TAG=${1:?brug: result <tag>}
    $BOX "sed -n '/^--- $TAG skaerm/,\$p' /tmp/$TAG.out 2>/dev/null; \
          echo '--- proces-snapshot:'; cat /tmp/$TAG.ps 2>/dev/null; \
          echo '--- GFX1-linjer (game.log):'; \
          grep -a -m8 'GFX1' /tmp/ab_$TAG/game.log 2>/dev/null" ;;

*)
    sed -n '2,15p' "$0"; exit 2 ;;
esac
