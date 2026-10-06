#!/bin/bash
# capture_renderer_crash.sh — vent på Chromiums renderer med et bestemt
# client-id og fang crashet med gdb (backtrace, PC og disassembling).
#
# Brug på boksen (som root), efter at Chromium er startet:
#   nohup /root/capture_renderer_crash.sh 5 > /root/gdb_watch.log 2>&1 &
#
# Baggrund (6. okt 2026): alle renderere har --renderer-client-id=N i deres
# kommandolinje, og på denne stak crashede SAMME renderer (client-id=5) i hver
# kørsel — først SIGSEGV (adresse i heap-området, uden for alle indlæste
# moduler), én gang SIGABRT fra glibc's stack-beskytter. Dette script giver en
# rigtig backtrace i stedet for endnu et minidump.
set -uo pipefail

CID=${1:-5}
OUT=${2:-/root/gdb_renderer_$CID.log}
PATTERN="[r]enderer-client-id=$CID"

pid=""
for _ in $(seq 1 300); do
    pid=$(pgrep -f "$PATTERN" | head -1)
    [ -n "$pid" ] && break
    sleep 1
done
if [ -z "$pid" ]; then
    echo "ingen renderer med client-id=$CID fundet" >&2
    exit 1
fi

echo "renderer pid=$pid — attacher gdb og venter på crash ($(date +%H:%M:%S))"
gdb -p "$pid" -batch \
    -ex 'set pagination off' \
    -ex 'set print frame-arguments none' \
    -ex 'set breakpoint pending on' \
    -ex 'break __stack_chk_fail' \
    -ex 'break __fortify_fail' \
    -ex 'break __libc_message' \
    -ex 'break malloc_printerr' \
    -ex 'handle SIGSEGV stop print nopass' \
    -ex 'handle SIGABRT stop print nopass' \
    -ex 'continue' \
    -ex 'echo \n===== STOP (signal eller glibc-fejl) =====\n' \
    -ex 'info program' \
    -ex 'echo \n===== BACKTRACE (fuld stak) =====\n' \
    -ex 'bt' \
    -ex 'echo \n===== PC-OMKRING =====\n' \
    -ex 'x/8i $pc-16' \
    -ex 'echo \n===== REGISTRE =====\n' \
    -ex 'info registers pc lr sp' \
    -ex 'echo \n===== MODULER (uddrag) =====\n' \
    -ex 'info sharedlibrary' \
    > "$OUT" 2>&1
echo "skrev $OUT ($(date +%H:%M:%S))"
