#!/bin/bash
# ensure_audio.sh - kontroller eller genstart PulseAudio med det legacy-ALSA,
# som 3.10-kernen paa GeekBox kraever.
#
# Koer som root paa boksen:
#   bash /root/ensure_audio.sh
#
# Forventet resultat:
#   Default Sink: alsa_output.dmixer
#   /proc/<pulseaudio>/maps indeholder /opt/alsa-da
#
# En manuel `pulseaudio --start` uden LD_LIBRARY_PATH bruger systemets moderne
# libasound. Den kan ikke aabne dmixer og falder tilbage til auto_null, hvor
# Firefox' lyd gaar i ingenting (faelde 3 og 63).

set -euo pipefail

PULSE_USER=${PULSE_USER:-kristian}
PULSE_HOME=${PULSE_HOME:-/home/kristian}
PULSE_RUNTIME=${PULSE_RUNTIME:-/run/user/1000}
PA_LIB=${PA_LIB:-/opt/alsa-da/usr/lib/arm-linux-gnueabihf}
PA_CONFIG="$PULSE_HOME/.config/pulse/client.conf"

say() {
    printf 'ensure_audio: %s\n' "$*"
}

die() {
    printf 'ensure_audio: FEJL: %s\n' "$*" >&2
    exit 1
}

pulse() {
    runuser -u "$PULSE_USER" -- env \
        HOME="$PULSE_HOME" USER="$PULSE_USER" LOGNAME="$PULSE_USER" \
        DISPLAY=:0 XDG_RUNTIME_DIR="$PULSE_RUNTIME" "$@"
}

default_sink() {
    pulse pactl info 2>/dev/null \
        | sed -n 's/^Default Sink: //p' \
        | head -1
}

uses_legacy_alsa() {
    local pid
    pid=$(pgrep -x pulseaudio | head -1)
    [ -n "$pid" ] && grep -q "$PA_LIB" "/proc/$pid/maps" 2>/dev/null
}

audio_ok() {
    [ "$(default_sink)" = "alsa_output.dmixer" ] && uses_legacy_alsa
}

restore_autospawn() {
    if [ "${HAD_CLIENT_CONF:-0}" = "1" ]; then
        cp -p "$CLIENT_CONF_BAK" "$PA_CONFIG"
    else
        rm -f "$PA_CONFIG"
    fi
    rm -f "$CLIENT_CONF_BAK"
}

[ "$(id -u)" -eq 0 ] || die "skal koeres som root paa boksen"
[ "$(id -u "$PULSE_USER")" -eq 1000 ] \
    || die "brugeren $PULSE_USER skal have uid 1000"
[ -d "$PULSE_RUNTIME" ] || die "$PULSE_RUNTIME mangler"
[ -r "$PA_LIB/libasound.so.2" ] || die "$PA_LIB/libasound.so.2 mangler"

if audio_ok; then
    say "OK: alsa_output.dmixer bruger $PA_LIB"
    exit 0
fi

say "PulseAudio bruger ikke den korrekte ALSA-sink; genstarter"
CURRENT_PID=$(pgrep -x pulseaudio | head -1 || true)
if [ -n "$CURRENT_PID" ]; then
    say "nuværende PID $CURRENT_PID: sink=$(default_sink || true)"
fi

mkdir -p "$(dirname "$PA_CONFIG")"
CLIENT_CONF_BAK=$(mktemp /tmp/pulse-client.conf.XXXXXX)
HAD_CLIENT_CONF=0
if [ -f "$PA_CONFIG" ]; then
    cp -p "$PA_CONFIG" "$CLIENT_CONF_BAK"
    HAD_CLIENT_CONF=1
fi
trap restore_autospawn EXIT

# Hindr Firefox/LXDE i at autospawne en ny, forkert PulseAudio i racet mellem
# stop og start. Indstillingen gendannes naar serveren er oppe.
printf 'autospawn = no\n' >"$PA_CONFIG"
chown "$PULSE_USER:$PULSE_USER" "$PA_CONFIG"
chmod 600 "$PA_CONFIG"

if [ -n "$CURRENT_PID" ]; then
    kill -TERM "$CURRENT_PID" 2>/dev/null || true
    for _ in $(seq 1 8); do
        kill -0 "$CURRENT_PID" 2>/dev/null || break
        sleep 1
    done
    kill -KILL "$CURRENT_PID" 2>/dev/null || true
fi

for _ in $(seq 1 5); do
    pgrep -x pulseaudio >/dev/null 2>&1 || break
    sleep 1
done
pgrep -x pulseaudio >/dev/null 2>&1 \
    && die "en ukendt PulseAudio-proces overlevede stop"

rm -f "$PULSE_RUNTIME/pulse/native" "$PULSE_RUNTIME/pulse/pid"
pulse env -i \
    HOME="$PULSE_HOME" USER="$PULSE_USER" LOGNAME="$PULSE_USER" \
    PATH=/usr/local/bin:/usr/bin:/bin DISPLAY=:0 \
    XDG_RUNTIME_DIR="$PULSE_RUNTIME" \
    LD_LIBRARY_PATH="$PA_LIB" \
    pulseaudio --start --log-target=syslog

for _ in $(seq 1 10); do
    [ -n "$(default_sink)" ] && break
    sleep 1
done
[ -n "$(default_sink)" ] || die "PulseAudio startede ikke"

if [ "$(default_sink)" != "alsa_output.dmixer" ]; then
    pulse pactl set-default-sink alsa_output.dmixer 2>/dev/null || true
fi

audio_ok || die "PulseAudio kører stadig uden alsa_output.dmixer"
say "OK: PulseAudio genstartet med legacy-ALSA og alsa_output.dmixer"
