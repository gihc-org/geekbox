#!/bin/bash
# start_firefox_webgl.sh — start Firefox med hybris/WebGL-stakken som ALMINDELIG
# bruger (ikke root). Installeres på boksen som /usr/local/bin/firefox-webgl.
#
#   firefox-webgl [URL]          # default: about:blank
#
# Forudsætninger (opsat 25. aug 2026):
#   /usr/local/lib/firefox-webgl/  = shims + stub-libGL + webgl_test_dump.html
#   /home/kristian/ffprof          = Firefox-profil (ejes af brugeren)
# Profilen skal have layers.gpu-process.enabled=true og
# browser.dom.window.dump.enabled=true. INGEN MOZ_GL_SPEW (HAANDBOG fælde 25).
set -uo pipefail

FF=/usr/lib/firefox-esr/firefox-esr
PROFILE="${FIREFOX_WEBGL_PROFILE:-/home/kristian/ffprof}"
WEBGL=/usr/local/lib/firefox-webgl
URL="${1:-about:blank}"

if [ ! -x "$FF" ] || [ ! -d "$PROFILE" ]; then
    echo "firefox-webgl: mangler $FF eller profilen $PROFILE" >&2
    exit 1
fi

# Ryd evt. stale låse (Troubleshoot-dialog) — men kun hvis ingen session kører.
if pgrep -x firefox-esr >/dev/null 2>&1; then
    echo "firefox-webgl: Firefox kører allerede — afslut den først." >&2
    exit 1
fi
rm -f "$PROFILE/.parentlock" "$PROFILE/lock"

exec env LD_PRELOAD="$WEBGL/system_shim.so $WEBGL/egl_platform_shim.so" \
    LD_LIBRARY_PATH=/opt/hybris:"$WEBGL" \
    EGL_PLATFORM=x11 DISPLAY="${DISPLAY:-:0}" \
    MOZ_X11_EGL=1 MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_DISABLE_GPU_SANDBOX=1 \
    "$FF" -no-remote -profile "$PROFILE" "$URL"
