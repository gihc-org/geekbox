#!/usr/bin/env python3
# logdw_listener.py — kører på boksen: fanger Android-blobs'enes logd-datagrammer
import socket, os, sys, time

SOCK = "/dev/socket/logdw"
os.makedirs("/dev/socket", exist_ok=True)
try:
    os.unlink(SOCK)
except OSError:
    pass
s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
s.bind(SOCK)
logf = open("/root/logdw.log", "ab", buffering=0)

def dec(blob):
    # Android logger_entry: 1 byte len + 1 byte prio + 4 byte pid + 4 byte tid + 4 byte sec + 4 byte nsec + msg
    if len(blob) < 20:
        return "RAW: %r" % blob
    out = []
    i = 0
    while i + 20 <= len(blob):
        l = blob[i] & 0xff
        prio = blob[i+1] & 0xff
        pid = int.from_bytes(blob[i+2:i+6], "little")
        tid = int.from_bytes(blob[i+6:i+10], "little")
        sec = int.from_bytes(blob[i+10:i+14], "little")
        nsec = int.from_bytes(blob[i+14:i+18], "little")
        msg = blob[i+20:i+20+l]
        try:
            text = msg.decode("utf-8", "replace").rstrip("\x00")
        except Exception:
            text = repr(msg)
        out.append("prio=%d pid=%d tid=%d t=%d.%03d: %s" % (prio, pid, tid, sec, nsec//1000000, text))
        i += 20 + l
    return "\n".join(out)

print("lytter på %s" % SOCK, flush=True)
while True:
    try:
        data, _ = s.recvfrom(65536)
        line = dec(data)
        logf.write(("[%d] " % time.time()).encode() + line.encode() + b"\n")
        print(line, flush=True)
    except Exception as e:
        print("fejl: %r" % e, flush=True)
        time.sleep(1)
