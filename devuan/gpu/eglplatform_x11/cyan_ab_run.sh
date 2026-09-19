#!/bin/bash
# cyan_ab_run.sh — én A/B-målekørsel af cyan-scenen med fokus sat (19. sep 2026).
#
# Kør som root PÅ boksen:
#   bash /root/cyan_ab_run.sh <tag> [sek] [CYAN_LIGHT] [CYAN_FLUSH] [url]
#   fx: bash /root/cyan_ab_run.sh M1_heavy 200 ""      ""
#       bash /root/cyan_ab_run.sh M2_finish 200 1      finish
#
# Hvorfor et nyt script: 17./19. sep blev tung og light proxy sammenlignet i
# hver sin kørsel, og fokus-tilstanden (x_focus) var ikke sat i dem alle. En
# fokuseret Firefox tegner ~4x oftere end en ufokuseret (1,9 -> 8,9 skift/s
# maalt 17. sep), saa variantforskelle kan let vaere fokus og ikke proxy.
# Dette script maaler derfor BEGGE tal i samme koersel: skaermens faktiske
# opdateringsrate (fb_fps, foelger ypan) og browserens rAF (FPS tr= fra
# preloaden), plus proxyens GL-tick/draw-taelling.
#
# Trin: (1) stop Firefox paent + ren profil (beskidt profil -> frys ved 0 %),
# (2) start Firefox med den spillbare opskrift (CYAN_DEPTHCLEAR=1) og de valgte
# env-variable, (3) BiDi-preload (alpha-shim) + indlaes spillet, (4)
# FRYSNINGS-VAGT (fa elde 44): X kan holde op med at skrive til framebufferen
# efter nok display-danse — et xmessage-vindue skal kunne ses i fb_fps, ellers
# afbrydes koerslen med det samme, (5) saet X-fokus paa Firefox-vinduet,
# (6) maal skaermen med fb_fps MENS spillet kører, (7) saml FPS tr= og
# GL-tick-linjer.
#
# Resultat: /tmp/ab_<tag>/{launch,game,bidi,probe,fb}.log + summary paa stdout.
set -u

TAG=${1:?brug: cyan_ab_run.sh <tag> [sek] [LIGHT] [FLUSH] [url]}
SECS=${2:-150}
LIGHT=${3:-}
FLUSH=${4:-}
URL=${5:-https://poki.com/en/g/subway-surfers}
FBSEC=${FBSEC:-60}   # hvor laenge skaermen maales
SETTLE=${SETTLE:-45} # tid til siden er loadet (attract-scenen staar)

P=/home/kristian/ffprof
D=/tmp/ab_$TAG
mkdir -p "$D"
: > "$D/fb.log"

# Ryd op naar scriptet forlader (ogsaa naar frysnings-vagten afbryder), saa en
# afbrudt koersel ikke efterlader en BiDi-klient (fa elde 42: kun ÉN session)
# og en Firefox der holder profilen laast.
cleanup() {
    pkill -TERM -x firefox-esr 2>/dev/null
    sleep 3
    pkill -9 -x firefox-esr 2>/dev/null
    bash /root/kill_bidi.sh >/dev/null 2>&1
}
trap cleanup EXIT

echo "=== $TAG: sek=$SECS light='$LIGHT' flush='$FLUSH' url=$URL $(date +%H:%M:%S) ==="

pkill -TERM -x firefox-esr 2>/dev/null
sleep 6
pkill -9 -x firefox-esr 2>/dev/null
sleep 1
rm -rf "$P/cache2" "$P/startupCache" "$P/storage" "$P/sessionstore-backups" \
       "$P/cookies.sqlite" "$P/cookies.sqlite-wal" "$P/sessionstore.jsonlz4" \
       "$P/sessionCheckpoints.json" "$P/.parentlock" "$P/lock"

GAME_URL=about:blank CYAN_LIGHT="$LIGHT" CYAN_FLUSH="$FLUSH" \
  CYAN_DEPTHCLEAR=1 nohup /root/start_cyan_probe.sh > "$D/launch.log" 2>&1 &
sleep 15

PYTHONUNBUFFERED=1 nohup python3 /root/bidi_cyan.py preload goto \
  "$URL" "$SECS" > "$D/bidi.log" 2>&1 &
BIDI=$!

sleep "$SETTLE"

# VT-vagt (faelde 19 + 44): hybris' chvt-dans kan efterlade en ANDEN VT aktiv
# end den X kører på — så tegner X videre på sin egen VT, mens skærmen viser en
# anden (billedet står stille). Det rettes direkte med chvt tilbage.
# NB (19. sep 2026): en tidligere version af dette script lavede også et
# xmessage-"frysnings"-tjek. Det gav FALSK "frosset" — xmessage startede ikke
# (0 vinduer ifølge xwininfo), så framebufferen stod naturligt stille. Brug det
# ikke igen: et tomt skrivebord opdaterer 0 gange/s uden at være i stykker.
{
    echo "aktiv VT: $(cat /sys/class/tty/tty0/active 2>/dev/null)"
    pgrep -ax Xorg
} > "$D/guard.log" 2>&1
XVTOPT=$(pgrep -ax Xorg | grep -o 'vt[0-9]*' | head -1)
ACTIVE=$(cat /sys/class/tty/tty0/active 2>/dev/null)
if [ -n "$XVTOPT" ] && [ -n "$ACTIVE" ] && [ "tty${XVTOPT#vt}" != "$ACTIVE" ]; then
    echo "VT-VAGT: aktiv $ACTIVE men X kører på $XVTOPT — skifter tilbage" \
        | tee -a "$D/guard.log"
    chvt "${XVTOPT#vt}" 2>/dev/null
    sleep 1
fi

# 19. sep 2026: x_focus blev kørt som root UDEN DISPLAY, så den fejlede tavst i
# alle målinger (og Firefox drosler ikke-fokuserede vinduer → målingen bliver
# for pessimistisk). Kør den nu som kristian med DISPLAY (og XAUTHORITY hvis
# filen findes).
FOCUS_ENV="DISPLAY=:0 HOME=/home/kristian"
[ -f /home/kristian/.Xauthority ] && \
    FOCUS_ENV="$FOCUS_ENV XAUTHORITY=/home/kristian/.Xauthority"
runuser -u kristian -- env $FOCUS_ENV /root/x_focus firefox \
    > "$D/focus.log" 2>&1 || echo "x_focus fejlede (se $D/focus.log)"
sleep 5
/root/fb_fps /dev/fb0 "$FBSEC" 200 > "$D/fb.log" 2>&1

for _ in $(seq 1 90); do
    kill -0 "$BIDI" 2>/dev/null || break
    sleep 5
done

pkill -TERM -x firefox-esr 2>/dev/null
sleep 4
cp /tmp/cyan_draw_probe.log "$D/probe.log" 2>/dev/null
cp /root/cyan_game.log "$D/game.log" 2>/dev/null

echo "--- $TAG skaerm (fb_fps) ---"
grep -a "mest aktive blok" "$D/fb.log"
grep -a "^polls=" "$D/fb.log"
echo "--- $TAG rAF (FPS tr=, sidste 6) ---"
grep -a "FPS tr=" "$D/game.log" 2>/dev/null | tail -6
echo "--- $TAG GL-tick (proxy, sidste 6) ---"
grep -a "GL-tick:" "$D/probe.log" 2>/dev/null | tail -6
echo "--- $TAG alpha-shim/fejl ---"
grep -ac "ALPHA-SHIM" "$D/game.log" 2>/dev/null
grep -a "PAGE-ERROR\|CTXLOST" "$D/game.log" 2>/dev/null | head -3
echo "=== $TAG faerdig $(date +%H:%M:%S) — data i $D ==="
