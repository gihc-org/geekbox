#!/usr/bin/env python3
"""socktest.py — test-klient til gles_daemon (M1).

Sender JSON-kommandoer til unix-socketten og printer svarene.
Uden argumenter køres en standardsekvens (ping, fb, scenes, render, clear, quit).

Brug:
  python3 socktest.py
  python3 socktest.py '{"cmd":"render","scene":"triangle","phase":1.5}'
  python3 socktest.py --socket /tmp/gles.sock '{"cmd":"ping"}'
"""
import json
import socket
import sys


def main():
    sock_path = "/tmp/gles.sock"
    args = sys.argv[1:]
    if args and args[0] == "--socket":
        sock_path = args[1]
        args = args[2:]

    if args:
        cmds = [json.loads(a) for a in args]
    else:
        cmds = [
            {"cmd": "ping"},
            {"cmd": "fb"},
            {"cmd": "scenes"},
            {"cmd": "render", "scene": "triangle", "rect": [480, 270, 960, 540], "phase": 0.0},
            {"cmd": "clear", "rect": [0, 0, 240, 120], "color": [255, 0, 0]},
            {"cmd": "quit"},
        ]

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock_path)
    for c in cmds:
        line = json.dumps(c) + "\n"
        s.sendall(line.encode())
        reply = s.recv(65536).decode().strip()
        print(f"> {json.dumps(c)}")
        print(f"< {reply}")
    s.close()


if __name__ == "__main__":
    main()
