#!/bin/bash
# templog.sh — "sort boks": logger SoC-temperatur, CPU-frekvens og last til
# eMMC hvert sekund, så de sidste målinger overlever en pludselig nedlukning.
#
# Kør på boksen (som root):
#   nohup /root/templog.sh /root/templog.txt >/dev/null 2>&1 &
#
# Baggrund (6. okt 2026): boksen slukkede sig selv under SwiftShader-belastning.
# Kernen kan selv kalde orderly_poweroff(true), når temperaturen har været over
# hw_shut_temp i 3 målinger i træk (chip-default 125 C; se
# drivers/thermal/rockchip_thermal.c:762-774), men kernelloggen stoppede 55 min
# før døden, så vi kunne ikke se om grænsen blev nået. Denne logger skriver til
# disk med jævne mellemrum, så vi har kurven uanset hvordan boksen dør.
set -uo pipefail

OUT=${1:-/root/templog.txt}
SYNC_EVERY=${2:-5}
i=0
echo "=== templog start $(date) ===" >> "$OUT"
while true; do
    temp=$(cat /sys/rockchip_thermal/temp 2>/dev/null | sed 's/.*://')
    freq=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null)
    load=$(cut -d' ' -f1-3 /proc/loadavg)
    printf '%s temp=%s freq=%s load=%s\n' \
        "$(date +%H:%M:%S)" "$temp" "$freq" "$load" >> "$OUT"
    i=$((i + 1))
    [ $((i % SYNC_EVERY)) -eq 0 ] && sync
    sleep 1
done
