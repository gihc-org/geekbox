#!/bin/bash
# Bygger egl_ret_trace.so på boksen og scp'er kilden dertil.
set -uo pipefail

BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key
SRC=devuan/gpu/eglplatform_x11/egl_ret_trace.c

scp -i "$KEY" "$SRC" "$BOX:/root/egl_ret_trace.c"
ssh -i "$KEY" -o ConnectTimeout=8 "$BOX" 'gcc -O2 -fPIC -shared \
    -o /root/egl_ret_trace.so /root/egl_ret_trace.c \
    -I/usr/local/include -ldl -lpthread && echo BUILD-OK && ls -la /root/egl_ret_trace.so'
