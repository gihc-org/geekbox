#!/bin/bash
# cyan_ab_run.sh — én A/B-målekørsel af cyan-scenen med fokus sat (19. sep 2026).
#
# Kør som root PÅ boksen:
#   bash /root/cyan_ab_run.sh <tag> [sek] [CYAN_LIGHT] [CYAN_FLUSH] [url]
#   fx: bash /root/cyan_ab_run.sh M1_heavy 200 ""      ""
#       bash /root/cyan_ab_run.sh M2_finish 200 1      finish
#   WIN="1000 600" bash /root/cyan_ab_run.sh A2_small 80 "" "" file:///tmp/raf_test.html
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
# POLL (20. sep 2026): fb_fps' poll-interval i ms. Standard 200 ms betyder at
# værktøjet reelt ikke kan maale over ~2-5 skaerm-opdateringer/s (det ser kun
# "skiftede blokken siden sidste poll?"), og ALLE tal fra 17.-20. sep ligger
# lige omkring den graense. Saet fx POLL=50 (20 Hz) naar der maales paa en
# hurtigt animeret side.
POLL=${POLL:-200}
# WIN (20. sep 2026): valgfri vinduesstorrelse til areal-testen, fx WIN="1000 600".
# Baggrund (faelde 45): baade readback og composite skalerer med arealet
# (1920x1080 = 173 ms mod 836x470 = 34 ms), saa det er arealet af browserens
# composite-flade der saetter loftet. Sat til tom => vinduet roeres ikke.
# Vigtigt: vinduet saettes i profilens xulstore.json FOER Firefox starter.
# At kalde x_resize bagefter virker IKKE naar vinduet er maksimeret — openbox
# giver det straks fuld skrivebordsstorrelse igen (maalt 20. sep: bad om
# 1000x600, fik 1920x1054). 17. sep-forsoegget med "1000x600" blev derfor
# aldrig maalt i et lille vindue.
WIN=${WIN:-}

P=/home/kristian/ffprof
D=/tmp/ab_$TAG
mkdir -p "$D"
: > "$D/fb.log"
: > "$D/win.log"

# Vinduesgeometri for og efter et evt. resize — saa vi kan SE at det virkede
# (WM'en kan maximere vinduet igen, hvis det staar maksimeret). Hele traeet
# dumpes med vilje: Firefox har flere smaa hjaelpevinduer, og 20. sep viste at
# et head-6 kun fangede dem — ikke hovedvinduet.
winlog() {
    {
        echo "--- $1 $(date +%H:%M:%S)"
        DISPLAY=:0 xwininfo -root -tree 2>&1
    } >> "$D/win.log"
}

# Ryd op naar scriptet forlader (ogsaa naar frysnings-vagten afbryder), saa en
# afbrudt koersel ikke efterlader en BiDi-klient (fa elde 42: kun ÉN session)
# og en Firefox der holder profilen laast.
cleanup() {
    pkill -TERM -x firefox-esr 2>/dev/null
    sleep 3
    pkill -9 -x firefox-esr 2>/dev/null
    bash /root/kill_bidi.sh >/dev/null 2>&1
    # genskab profilens oprindelige vinduesstorrelse
    [ -f "$D/xulstore.orig.json" ] && \
        cp -f "$D/xulstore.orig.json" "$P/xulstore.json"
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

# Saet vinduets storrelse i xulstore.json (læses ved start) naar WIN er sat:
# sizemode skal vaere "normal", ellers maksimerer Firefox selv vinduet.
if [ -n "$WIN" ]; then
    XW=$(echo "$WIN" | awk '{print $1}')
    XH=$(echo "$WIN" | awk '{print $2}')
    XW=${XW:-1000}; XH=${XH:-600}
    if [ -f "$P/xulstore.json" ]; then
        cp -f "$P/xulstore.json" "$D/xulstore.orig.json"
        python3 - "$P/xulstore.json" "$XW" "$XH" <<'PY' || echo "xulstore-patch fejlede"
import json, sys
path, w, h = sys.argv[1], sys.argv[2], sys.argv[3]
with open(path) as f:
    doc = json.load(f)
mw = doc["chrome://browser/content/browser.xhtml"]["main-window"]
mw["sizemode"] = "normal"
mw["screenX"] = "0"
mw["screenY"] = "0"
mw["width"] = w
mw["height"] = h
with open(path, "w") as f:
    json.dump(doc, f)
print("xulstore: %sx%s, sizemode=normal" % (w, h))
PY
    fi
fi

GAME_URL=about:blank CYAN_LIGHT="$LIGHT" CYAN_FLUSH="$FLUSH" \
  CYAN_DEPTHCLEAR=1 nohup /root/start_cyan_probe.sh > "$D/launch.log" 2>&1 &
sleep 15
winlog "efter Firefox-start (foer resize)"
if [ -n "$WIN" ]; then
    # -big: tag det STOERSTE "firefox"-vindue. Uden den rammer x_resize det
    # foerste match, og Firefox' hjaelpevinduer (200x200) ligger foer
    # hovedvinduet i dybde-foerst-soegningen.
    DISPLAY=:0 /root/x_resize -big firefox $WIN 2>&1 | tee -a "$D/focus.log" \
        || echo "x_resize fejlede (se $D/focus.log)" | tee -a "$D/guard.log"
    sleep 4
    winlog "efter x_resize $WIN"
fi

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

# 20. sep 2026: FEJLEN var ikke Xauthority. Scriptet kørte x_focus via
# "runuser -u kristian", men /root er 0700 → "env: '/root/x_focus': Permission
# denied" (exit 126). x_focus blev derfor ALDRIG kørt, og alle målinger til og
# med 20. sep har haft focus=false. X tager imod root uden .Xauthority, så vi
# kører den nu direkte som root. -big rammer hovedvinduet, ikke Firefox'
# 200x200-hjælpevinduer.
DISPLAY=:0 /root/x_focus -big firefox > "$D/focus.log" 2>&1 \
    || echo "x_focus fejlede (se $D/focus.log)"
{
    echo "--- X-fokus efter x_focus:"
    DISPLAY=:0 xprop -root _NET_ACTIVE_WINDOW 2>&1
} >> "$D/focus.log"
sleep 5
/root/fb_fps /dev/fb0 "$FBSEC" "$POLL" > "$D/fb.log" 2>&1

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
echo "--- $TAG fokus ---"
cat "$D/focus.log" 2>/dev/null | head -6
echo "--- $TAG alpha-shim/fejl ---"
grep -ac "ALPHA-SHIM" "$D/game.log" 2>/dev/null
grep -a "PAGE-ERROR\|CTXLOST" "$D/game.log" 2>/dev/null | head -3
if [ -s "$D/win.log" ]; then
    echo "--- $TAG vinduesgeometri (hovedvinduer) ---"
    grep -E '^--- |1920x1080|1000x600|1280x720|640x360|Firefox|raf-test|urfers' \
        "$D/win.log" | head -25
fi
echo "=== $TAG faerdig $(date +%H:%M:%S) — data i $D ==="
