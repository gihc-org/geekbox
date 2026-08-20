#!/bin/bash
# stress_test.sh — belastningstest der afslører svage strømforsyninger.
#
# KØRES PÅ BOKSEN, fx:  ssh -i ~/.ssh/geekbox_key root@<ip> 'bash -s' < devuan/stress_test.sh
# Genskaber belastningen der brownout-crashede boksen på en "5V 2A"-adapter:
#   1. udpakker alle .deb'er i /var/cache/apt/archives tre gange (xz = tung CPU)
#   2. 8 travle CPU-løkker (alle kerner mættet)
#   3. 600 MB sekventiel skrivning med fsync (eMMC-belastning)
# Overvåger samtidig hvert 2. sek: uptime, load, vdd_arm (PMIC-regulator), cpu-frekvens.
# Loggen ligger i /root/stress_mon.log — den overlever en boot (/tmp ryddes).
#
# Fortolkning: dør boksen under testen, er det næsten sikkert strømmen (brownout →
# PMIC-reset). Overlever den her, men ikke på en anden adapter: adapteren er synderen.
# Se HAANDBOG.md fælde 17 + DOKUMENTATION.md §5.14 for den fulde beviskæde.
#
# Pas på: testen kan med vilje crashe boksen. Den booter fint igen.
set -u

MON=/root/stress_mon.log
rm -f "$MON"
R=$(grep -l vdd_arm /sys/class/regulator/regulator.*/name 2>/dev/null | head -1 | sed 's|/name$||')
echo "regulator-sti: ${R:-IKKE FUNDET}"
echo "start: epoch=$(date +%s)  up=$(cut -d. -f1 /proc/uptime) s"

( while :; do
    echo "$(date +%s) up=$(cut -d. -f1 /proc/uptime) load=$(cut -d' ' -f1-3 /proc/loadavg) vdd_arm=$(cat "$R/microvolts" 2>/dev/null) cpu0=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null)"
    sleep 2
  done > "$MON" ) &
MONPID=$!

# 8 travle CPU-løkker (max belastning på alle kerner)
BUSY=""
for i in $(seq 1 8); do
    (while :; do :; done) &
    BUSY="$BUSY $!"
done

# stor eMMC-skrivning i baggrunden
dd if=/dev/zero of=/tmp/stressfile bs=1M count=600 conv=fsync >/dev/null 2>&1 &
DDPID=$!

echo "belastning startet: $(date +%s)"

# hovedbelastningen: udpak de cachede .deb'er tre gange
DEBS=$(ls /var/cache/apt/archives/*.deb 2>/dev/null)
if [ -n "$DEBS" ]; then
    for pass in 1 2 3; do
        for f in $DEBS; do
            rm -rf /tmp/unp && mkdir -p /tmp/unp
            dpkg-deb -x "$f" /tmp/unp >/dev/null 2>&1
        done
        echo "pass $pass færdig: epoch=$(date +%s) up=$(cut -d. -f1 /proc/uptime)"
    done
else
    echo "(ingen .deb-filer i /var/cache/apt/archives — springer udpakningen over; CPU+disk kører stadig)"
fi

# ryd op og vis overvågningsloggen
kill $DDPID $BUSY $MONPID 2>/dev/null
rm -rf /tmp/unp /tmp/stressfile
echo
echo "=== STRESS-OVERLEVET. Overvågningslog (vdd_arm i uV): ==="
tail -30 "$MON"
