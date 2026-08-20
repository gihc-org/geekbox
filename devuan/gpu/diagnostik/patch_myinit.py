#!/usr/bin/env python3
# patch_myinit.py — indsæt /system-loop-mountet i myinit.sh (tidligt, før netværk)
src = open("/root/myinit.sh").read()
anchor = "mount -t devpts devpts /dev/pts 2>/dev/null"
add = anchor + """

# GPU-eksperiment (aug 2026): mount vendors Android-system.img ved /system —
# wifi-firmwaren OG PowerVR-blobs'ene ligger der. Skal ske tidligt: bcmdhd læser
# /system/etc/firmware når wlan0 kommer op, og uden mountet dør wifi for den boot.
mkdir -p /system
mount -o loop,ro /usr/local/share/libhybris/system.img /system 2>/dev/null
ln -sfn /system/vendor /vendor 2>/dev/null
"""
assert anchor in src, "anchor mangler"
if "system.img" in src:
    print("myinit allerede patchet — springer over")
else:
    open("/root/myinit.sh", "w").write(src.replace(anchor, add))
    print("myinit patchet: /system-loop-mount indsat")
