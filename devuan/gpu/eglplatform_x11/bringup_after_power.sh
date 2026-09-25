#!/bin/bash
# bringup_after_power.sh — bring-up efter strøm-cyklus/genstart af GeekBox'en.
#
# Samler opskriften fra TODO.md og
# docs/log/2026-08-26-ddk15-komplet-handover.md (§"Ur") og gør den idempotent:
#   1) vent på at boksen svarer på ssh (IP'en læses med devuan/find_box.sh)
#   2) sæt uret fra laptoppen hvis myinit/chrony ikke nåede det
#   3) insmod /root/pvrsrvkm_leddaz.ko + sh /root/gpu_up.sh
#   4) /dev/sw_sync 0666
#   5) læg den spillbare proxy (vnext12 dcc0a68f) på plads
#   6) bindapi-lappen (bind-mount — holder KUN til næste genstart)
#   7) kontrolsiderne til /tmp (ryddes ved boot) + md5 mod repoet
#   8) GL-verifikation (GL_VERSION + shader_ext_test)
#
# Kør:  bash devuan/gpu/eglplatform_x11/bringup_after_power.sh [--no-gl-test]
set -uo pipefail

REPO=$(cd "$(dirname "$0")/../../.." && pwd)
TXTS="$REPO/devuan/gpu/eglplatform_x11"
BOX="bash $REPO/devuan/box.sh"
WAIT=${WAIT:-240}          # hvor længe der ventes på ssh (sekunder)
GLTEST=1
[ "${1:-}" = "--no-gl-test" ] && GLTEST=0

echo "=== bring-up $(date +%H:%M:%S) — venter op til ${WAIT}s på ssh ==="
t=0
until $BOX "echo ssh-ok" >/dev/null 2>&1; do
    t=$((t+10))
    [ "$t" -ge "$WAIT" ] && { echo "STOP: boksen svarede ikke inden ${WAIT}s (tjek strøm/kabel og kør devuan/find_box.sh)" >&2; exit 1; }
    sleep 10
done
$BOX "echo -n 'boks: '; hostname; uptime; uname -r; date"

# 2) ur — boxen har ingen RTC; myinit synker via HTTP, chrony kan også
YR=$($BOX "date +%Y")
if [ "$YR" -lt 2026 ]; then
    echo "--- uret står på $YR: sætter det fra laptoppen"
    NOW=$(date +%s)
    $BOX "date -s @$NOW >/dev/null && date"
fi
$BOX "chronyc makestep >/dev/null 2>&1; chronyc tracking 2>/dev/null | head -3; date"

# 3) GPU-stakken
echo "--- insmod + gpu_up.sh"
$BOX "lsmod | grep -q '^pvrsrvkm' || insmod /root/pvrsrvkm_leddaz.ko; lsmod | grep '^pvrsrvkm'"
$BOX "sh /root/gpu_up.sh"

# 4) sync-fence-enheden skal kunne åbnes af brugeren (fælde: gralloc-lock EINVAL)
$BOX "chmod 666 /dev/sw_sync 2>/dev/null; ls -l /dev/sw_sync"

# 5) proxy: vnext12 (den spillbare, tung)
echo "--- proxy vnext12"
$BOX "cp -f /root/egl_proxy_dcc0a68f.so.bak /opt/hybris/libEGL.so.1.0.0 && md5sum /opt/hybris/libEGL.so.1.0.0"

# 6) bindapi-lappen (bind-mount over /system/lib/libEGL.so)
echo "--- bindapi-lap"
bash "$TXTS/patch_android_bindapi.sh" 2>&1 | tail -12
# Lappen slutter med "chvt 7" (display-dansen). X kører måske på en anden VT
# (den skifter ved hver genstart), så skift tilbage — ellers står skærmen på en
# tom VT og ser frossen ud (fælde 19/44/54).
$BOX "XVTOPT=\$(pgrep -ax Xorg | grep -o 'vt[0-9]*' | head -1); \
      ACTIVE=\$(cat /sys/class/tty/tty0/active); \
      if [ -n \"\$XVTOPT\" ] && [ \"tty\${XVTOPT#vt}\" != \"\$ACTIVE\" ]; then \
          chvt \"\${XVTOPT#vt}\"; echo \"VT skiftet tilbage: \$ACTIVE -> \$XVTOPT\"; \
      else echo \"VT ok: \$ACTIVE\"; fi"

# 7) kontrolsiderne til /tmp (de ryddes ved genstart)
echo "--- kontrolsider til /tmp"
$BOX --put "$TXTS/raf_test.html" /tmp/raf_test.html
$BOX --put "$TXTS/raf_test_full.html" /tmp/raf_test_full.html
$BOX "md5sum /tmp/raf_test.html /tmp/raf_test_full.html"
md5sum "$TXTS/raf_test.html" "$TXTS/raf_test_full.html"

# 8) GL-verifikation
if [ "$GLTEST" = 1 ]; then
    echo "--- GL-verifikation"
    $BOX "timeout 30 env LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
          EGL_PLATFORM=x11 DISPLAY=:0 /root/gl_version_probe 2>&1 | grep -a -m3 'GL_VERSION\|GL_RENDERER'"
    $BOX "timeout 40 env LD_PRELOAD=/root/system_shim.so LD_LIBRARY_PATH=/opt/hybris \
          EGL_PLATFORM=x11 DISPLAY=:0 /root/shader_ext_test 2>&1 | grep -a -m3 -i 'draw_buffers\|ES3\|ES2'"
fi

# 9) værktøjerne i /root skal matche repoet (ellers måler vi med en anden kode)
echo "--- /root-værktøjer mod repoet (skal give samme md5 to gange pr. par)"
for f in cyan_ab_run.sh bidi_cyan.py start_cyan_probe.sh; do
    $BOX "md5sum /root/$f"
    md5sum "$TXTS/$f"
done

echo "=== bring-up færdig $(date +%H:%M:%S) ==="
