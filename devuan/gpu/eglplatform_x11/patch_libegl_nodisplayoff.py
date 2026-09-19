#!/usr/bin/env python3
"""patch_libegl_nodisplayoff.py — fjern "sluk displayet"-halvdelen af hybris'
display-dans i libEGL_r.so.

Baggrund (19. sep 2026): hybris' EGL-wrapper (den oprindelige libEGL, her
omdøbt til libEGL_r.so) kører ved EGL-init to shell-kommandoer:

    /usr/bin/find /sys/class/display/*/enable|/usr/bin/xargs -I {} \
        /bin/sh -c '/bin/echo 0 > {}'      <- slukker displayet
    ... '/bin/echo 1 > {}'                 <- tænder det igen

(+ `/bin/chvt 11`). Kommandoerne ses som `[system-shim]`-linjer i Firefox'
log, og dmesg viser `hdmi remove from lcdc0` -> `connect to lcdc0` hver gang.
Målt 19. sep 2026: med hybris/EGL-laget opdaterer skærmen 0,2 gange/s på en
triviel side; uden laget 3,2 gange/s — altså (næsten) frosset skærm. Denne
patch ændrer det ene byte '0' til '1' i "sluk"-strengen, så EGL-init kun
tænder displayet og aldrig slukker det. Reversibel: tag backup først.

Brug:
    cp /opt/hybris/libEGL_r.so /root/libEGL_r.so.bak-<dato>
    python3 patch_libegl_nodisplayoff.py /opt/hybris/libEGL_r.so
"""
import sys

NEEDLE = b"/bin/echo 0 > {}"


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    path = sys.argv[1]
    with open(path, "rb") as fh:
        data = bytearray(fh.read())
    n = data.count(NEEDLE)
    if n == 0:
        print("strengen findes ikke — er filen allerede patchet?")
        return 1
    if n > 1:
        print("strengen findes %d gange; kun ét sted forventet" % n)
        return 1
    off = data.index(NEEDLE)
    print("finder '%s' ved offset 0x%x" % (NEEDLE.decode(), off))
    patched = NEEDLE.replace(b"echo 0", b"echo 1")
    data[off:off + len(NEEDLE)] = patched
    with open(path, "wb") as fh:
        fh.write(data)
    print("patchet: '%s' -> '%s'" % (NEEDLE.decode(), patched.decode()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
