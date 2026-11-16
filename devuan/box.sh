#!/bin/bash
# box.sh — tynd wrapper om ssh/scp til GeekBox'en (FPS-sporet).
#
# Brug:
#   bash devuan/box.sh '<kommando>'              ssh til boksen
#   bash devuan/box.sh --put <lokal> <fjern>      kopiér fil til boksen
#   bash devuan/box.sh --get <fjern> <lokal>      hent fil fra boksen
#
# IP'en findes med devuan/find_box.sh hvis BOXIP ikke er sat. Efter den nye
# serialbaserede MAC er DHCP-adressen stabil, men ældre boots/images kan stadig
# have skiftende adresser.
set -uo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
SSHKEY=${SSHKEY:-$HOME/.ssh/geekbox_key}

find_ip() {
    if [ -n "${BOXIP:-}" ]; then
        echo "$BOXIP"; return
    fi
    local ip
    ip=$(bash "$REPO/devuan/find_box.sh" 2>/dev/null | awk '/GEEKBOX/{print $2; exit}')
    echo "${ip:-192.168.0.171}"
}

IP=$(find_ip)
COMMON=(-F /dev/null -i "$SSHKEY" -o ConnectTimeout=8 -o ServerAliveInterval=15
        -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null)

case "${1:-}" in
    "")
        echo "brug: box.sh '<kommando>' | --put <lokal> <fjern> | --get <fjern> <lokal>" >&2
        exit 2;;
    --put)
        scp "${COMMON[@]}" -q "$2" "root@$IP:$3";;
    --get)
        scp "${COMMON[@]}" -q "root@$IP:$2" "$3";;
    *)
        exec ssh "${COMMON[@]}" "root@$IP" "$1";;
esac
