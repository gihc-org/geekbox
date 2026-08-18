#!/bin/bash
# find_box.sh — finder GeekBox'e på det lokale netværk.
#
# Boksen får normalt DHCP på eth0. Får den ikke link i tid (carrier-racen i myinit),
# falder den tilbage til den statiske 192.168.1.50 — derfor tjekkes den altid med.
# Kendetegn: port 22 svarer med et dropbear-banner (ikke OpenSSH).
#
# NB: søg ikke efter MAC-adressen. Boksen har ingen MAC programmeret i sin IDB
# (dmesg: "Read the Ethernet MAC address from IDB:00:00:00:00:00:00"), så kernen laver
# en tilfældig ved hver boot — og DHCP giver derfor en ny IP hver gang.
#
# Kør:  devuan/find_box.sh          (ingen sudo nødvendig)
set -uo pipefail

IF=$(ip -4 route show default | awk '{print $5; exit}')
NET=$(ip -4 -o addr show dev "$IF" 2>/dev/null | awk '{print $4}' | head -1)
BASE=$(echo "$NET" | cut -d. -f1-3)

echo "== scanner ${BASE}.0/24 på $IF (samt 192.168.1.50) =="
seq 1 254 | xargs -P 64 -I{} ping -c1 -W1 "${BASE}.{}" >/dev/null 2>&1
ping -c1 -W1 192.168.1.50 >/dev/null 2>&1

fundet=0
while read -r ip mac; do
    banner=$(timeout 2 bash -c "exec 3<>/dev/tcp/$ip/22 && head -c 30 <&3" 2>/dev/null \
             | tr -d '\0' | head -1)
    case "$banner" in
        *dropbear*)
            echo "  GEEKBOX  $ip  ($mac)  $banner"
            fundet=$((fundet+1));;
        *SSH*)
            echo "  ssh      $ip  ($mac)  $banner  (ikke dropbear — næppe en boks)";;
    esac
done < <(ip neigh | awk '/lladdr/ && !/FAILED/ {print $1, $5}' | sort -u)

echo
if [ "$fundet" = 0 ]; then
    echo "Ingen boks fundet. Prøv igen om et halvt minut — den er måske stadig ved at boote."
    echo "Booter den uden netværk (carrier-racen), så tjek kablet og strømcykl den."
else
    echo "Log ind med:  ssh -i ~/.ssh/geekbox_key root@<ip>"
fi
