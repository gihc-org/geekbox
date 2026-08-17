#!/bin/bash
# 08: Installerer NetworkManager + nm-applet på SD-kortets Devuan.
# Formål: wifi kan vælges/ændres fra LXDE-skrivebordet (eller nmtui via ssh) —
# ikke længere håndkodede netværk i /etc/wpa_supplicant/wpa_supplicant.conf.
#
# Design: NM styrer KUN wlan0. eth0 bliver på ifupdown + myinit's tidlige
# bring-up, så ssh/dropbear-debugstien er uændret. Debians default
# ([ifupdown] managed=false i NetworkManager.conf) betyder at NM lader
# interfaces i /etc/network/interfaces være i fred — derfor fjernes wlan0-
# stanzen derfra, og NM overtager den.
# Kendte netværk i wpa_supplicant.conf migreres til NM-nøglefiler, så boksen
# stadig går på nettet efter genstart (ellers mister man ssh-adgangen!).
#
# Non-destruktiv: kører mod det færdige kort i læseren (samme mønster som 05).
# Kør: sudo devuan/08_network_manager.sh /dev/sdX1
set -euo pipefail
DEV=${1:?Brug: $0 /dev/sdX1}
[ -b "$DEV" ] || { echo "FEJL: $DEV er ikke en blok-enhed"; exit 1; }

MNT=$(mktemp -d)
mount "$DEV" "$MNT"
trap 'rm -f "$MNT/usr/bin/qemu-arm-static"; umount "$MNT"; rmdir "$MNT"' EXIT

cp /usr/bin/qemu-arm-static "$MNT/usr/bin/"
cp -L /etc/resolv.conf "$MNT/etc/resolv.conf"

echo "== pakker (qemu-chroot — postinsts kan ikke køre på boksens 3.10-kerne) =="
chroot "$MNT" /usr/bin/apt-get update
chroot "$MNT" /usr/bin/apt-get install -y --no-install-recommends \
    network-manager network-manager-gnome

echo "== rettigheder: netdev-gruppen må styre netværk uden sudo =="
chroot "$MNT" /usr/sbin/groupadd -f -r netdev
chroot "$MNT" /usr/sbin/usermod -aG netdev kristian
# polkit-regel der virker uden logind-session (nodm starter X uden)
mkdir -p "$MNT/etc/polkit-1/rules.d"
cat > "$MNT/etc/polkit-1/rules.d/50-nm-netdev.rules" <<'EOF'
polkit.addRule(function(action, subject) {
    if (action.id.indexOf("org.freedesktop.NetworkManager.") === 0 &&
        subject.isInGroup("netdev")) {
        return polkit.Result.YES;
    }
});
EOF

echo "== wlan0 ud af ifupdown (NM overtager; eth0 røres ikke) =="
IF="$MNT/etc/network/interfaces"
cp -n "$IF" "$IF.pre-nm"
awk '
    /^iface wlan0[ \t]/                       { skip=1; next }
    skip==1 && /^[ \t]/                       { next }
    skip==1                                   { skip=0 }
    /^(auto|allow-hotplug)[ \t]+wlan0([ \t]|$)/ { next }
    { print }
' "$IF" > "$IF.nm" && mv "$IF.nm" "$IF"

echo "== migrerer kendte wifi-netværk til NM-nøglefiler =="
python3 - "$MNT" <<'PY'
import os, re, sys, uuid

mnt = sys.argv[1]
src = os.path.join(mnt, "etc/wpa_supplicant/wpa_supplicant.conf")
outdir = os.path.join(mnt, "etc/NetworkManager/system-connections")

if not os.path.exists(src):
    print("(ingen wpa_supplicant.conf — intet at migrere)")
    sys.exit(0)

data = open(src, encoding="utf-8", errors="replace").read()
blocks = re.findall(r"network\s*=\s*\{(.*?)\}", data, re.S)
if not blocks:
    print("(ingen network-blokke — intet at migrere)")
    sys.exit(0)

os.makedirs(outdir, exist_ok=True)
n = 0
for b in blocks:
    m = re.search(r'ssid="((?:[^"\\]|\\.)*)"', b)
    if not m:
        print("ADVARSEL: network-blok uden streng-ssid sprunget over")
        continue
    ssid = m.group(1).replace('\\"', '"').replace("\\\\", "\\")

    psk_m = re.search(r'psk="((?:[^"\\]|\\.)*)"', b)
    hex_m = re.search(r"\bpsk=([0-9a-fA-F]{64})\b", b)
    km = re.search(r"key_mgmt=([^\s#]+)", b)

    if re.search(r"\bwep_key[0-9]", b) and not (psk_m or hex_m):
        print(f"ADVARSEL: {ssid}: WEP kan ikke migreres — tilføj manuelt i NM")
        continue

    lines = ["[connection]",
             f"id={ssid}",
             f"uuid={uuid.uuid4()}",
             "type=wifi",
             "autoconnect=true",
             "",
             "[wifi]",
             "mode=infrastructure",
             f"ssid={ssid}"]
    if re.search(r"scan_ssid=1", b):
        lines.append("hidden=true")
    lines.append("")
    if psk_m or hex_m:
        lines += ["[wifi-security]", "key-mgmt=wpa-psk",
                  f"psk={(psk_m or hex_m).group(1)}", ""]
    elif km and km.group(1) != "NONE":
        print(f"ADVARSEL: {ssid}: key_mgmt={km.group(1)} kan ikke migreres — tilføj manuelt i NM")
        continue
    lines += ["[ipv4]", "method=auto", "", "[ipv6]", "method=auto", ""]

    fname = re.sub(r"[^A-Za-z0-9_.-]", "_", ssid) + ".nmconnection"
    path = os.path.join(outdir, fname)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    os.chmod(path, 0o600)
    n += 1
    print(f"migreret: {ssid} -> {fname}")

print(f"({n} netværk migreret)")
PY

chroot "$MNT" /usr/bin/apt-get clean
sync
echo "== FÆRDIG: NetworkManager installeret på $DEV =="
echo "   Ved næste boot forbinder NM selv til de migrerede netværk."
echo "   Nye netværk: nm-applet i LXDE-bakken, eller 'nmtui' i en terminal (også via ssh)."
