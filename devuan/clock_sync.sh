#!/bin/sh
# clock_sync.sh - tid uden RTC og uden brug af offentlig NTP.
#
# Boksen har intet RTC-batteri, og UDP/123 er ikke brugbar fra dette netværk.
# Routeren svarer derimod på HTTP med en Date-header. Vi gemmer seneste kendte
# epoch på eMMC, så tiden er omtrent rigtig allerede før netværket er oppe.

set -eu

STATE_DIR=/var/lib/geekbox
STATE=$STATE_DIR/clock.epoch
MODE=${1:-sync}

mkdir -p "$STATE_DIR"

restore_clock()
{
    [ -r "$STATE" ] || return 0
    python3 - "$STATE" <<'PY'
import os
import subprocess
import sys
import time

path = sys.argv[1]
try:
    saved = int(open(path).read().strip())
except Exception:
    raise SystemExit(0)

now = int(time.time())
if now < 1700000000 and saved >= 1700000000:
    subprocess.run(["date", "-s", "@%d" % saved], check=True,
                   stdout=subprocess.DEVNULL)
    print("clock_sync: gendannede %d fra %s" % (saved, path))
PY
}

sync_clock()
{
    GATEWAYS=
    WAIT=${CLOCK_SYNC_WAIT:-60}
    START_TIME=$(date +%s)
    DEADLINE=$((START_TIME + WAIT))
    while [ "$(date +%s)" -lt "$DEADLINE" ]; do
        GATEWAYS=$(ip route show default 2>/dev/null \
            | awk '/^default via/ && $0 !~ /metric 100/ {
                       for (i = 1; i < NF; i++)
                           if ($i == "via") print $(i + 1)
                   }' \
            | awk '!seen[$0]++')
        [ -n "$GATEWAYS" ] && break
        sleep 2
    done
    if [ -z "$GATEWAYS" ]; then
        GATEWAYS=$(ip route show default 2>/dev/null \
            | awk '{ for (i = 1; i < NF; i++) if ($i == "via") print $(i + 1) }' \
            | awk '!seen[$0]++')
    fi

    # Gatewayens Date-header prøves først, fordi den virker uden DNS.
    python3 - "$STATE" $GATEWAYS <<'PY'
import email.utils
import subprocess
import sys
import time
import urllib.request

state = sys.argv[1]
gateways = sys.argv[2:]
urls = ["http://%s/" % gateway for gateway in gateways]
urls.append("http://example.com/")

server_epoch = None
used_url = None
last_error = None
for url in urls:
    try:
        request = urllib.request.Request(
            url, headers={"User-Agent": "GeekBox-clock-sync/1"}
        )
        with urllib.request.urlopen(request, timeout=5) as response:
            date_value = response.headers.get("Date")
        if not date_value:
            raise RuntimeError("svar uden Date-header")
        server_epoch = int(
            email.utils.parsedate_to_datetime(date_value).timestamp()
        )
        used_url = url
        break
    except Exception as exc:
        last_error = exc

if server_epoch is None:
    print("clock_sync: FEJL: ingen HTTP-tidskilde: %s" % last_error)
    raise SystemExit(1)

now = int(time.time())
if abs(now - server_epoch) > 1:
    subprocess.run(
        ["date", "-s", "@%d" % server_epoch],
        check=True,
        stdout=subprocess.DEVNULL,
    )

with open(state, "w") as stream:
    stream.write("%d\n" % server_epoch)

print(
    "clock_sync: %s -> %s (offset %+d s)"
    % (used_url, time.strftime("%F %T %Z", time.localtime(server_epoch)),
       server_epoch - now)
)
PY

    # chrony må ikke blokere tidsfixet. Hvis NTP senere bliver tilgængelig,
    # anvendes step med det samme; i dag er UDP/123 typisk filtreret.
    chronyc makestep >/dev/null 2>&1 || true
}

case "$MODE" in
    restore)
        restore_clock
        ;;
    sync)
        sync_clock
        ;;
    *)
        echo "brug: $0 restore|sync" >&2
        exit 2
        ;;
esac
