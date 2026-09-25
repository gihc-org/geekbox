#!/bin/bash
# s1_snapshot.sh — kører PÅ boksen og tager et snapshot af Firefox-processerne
# midt i en S1-målekørsel (75 s inde), så vi kan se hvilken vej der faktisk
# kører: er der en GPU-proces, og har den hybris-libEGL indlæst?
#
# Baggrund (25. sep 2026): MOZ_LOG-modulerne Compositor/LayerManager/WebRender
# skriver INTET i denne ESR-140-build (målt: 0 linjer, mens Widget:5 giver
# 108-112 linjer), så kompositor-vejen kan ikke læses af loggen.
#
#   bash /root/s1_snapshot.sh <tag> [sek]
set -uo pipefail
TAG=${1:?brug: s1_snapshot.sh <tag> [sek]}
SLEEP=${2:-75}
sleep "$SLEEP"
F=/tmp/$TAG.ps
{
    echo "--- snapshot $(date +%H:%M:%S) ($SLEEP s inde i kørslen)"
    ps -eo pid,args --width 400 | grep -a "[f]irefox-esr" \
        | sed -e 's/-prefsHandle [^ ]*//g' -e 's/-prefMapHandle [^ ]*//g' \
              -e 's/-jsInitHandle [^ ]*//g' -e 's/-initialChannelId [^ ]*//g' \
              -e 's/-parentBuildID [0-9]*//g' -e 's/-ipcHandle [^ ]*//g' \
              -e 's/-sandboxReporter [^ ]*//g' | cut -c1-190
    echo "--- GPU-proces? (antal firefox-born med -isForGPUProcess i args)"
    # klassen [i] forhindrer at grep'ens egen kommandolinje tælles med
    ps -eo args --width 400 | grep -a -c -- "[i]sForGPUProcess" || true
    ps -eo pid,args --width 400 | grep -a -- "[i]sForGPUProcess" | cut -c1-190
    echo "--- libEGL/hybris pr. proces:"
    for p in $(pgrep -f firefox-esr); do
        echo "  $p libEGL=$(grep -c libEGL /proc/$p/maps 2>/dev/null) hybris=$(grep -c hybris /proc/$p/maps 2>/dev/null)"
    done
    echo "--- kompositor-GL? probe.log (GL-tick = WebGL-kald, GLES-linjer = EGL-brug):"
    wc -l /tmp/cyan_draw_probe.log 2>/dev/null
    grep -a -c "GL-tick" /tmp/cyan_draw_probe.log 2>/dev/null || true
    grep -a -m3 -E "eglSwapBuffers|eglCreateWindowSurface" /tmp/cyan_draw_probe.log 2>/dev/null | cut -c1-150
} > "$F" 2>&1
