#!/bin/bash
# Bygger egl_trace_lib.so som /root/egl_trace/libEGL.so.1 på boksen.
set -uo pipefail

BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key
SRC=devuan/gpu/eglplatform_x11/egl_trace_lib.c

scp -i "$KEY" "$SRC" "$BOX:/root/egl_trace_lib.c"
ssh -i "$KEY" -o ConnectTimeout=8 "$BOX" 'mkdir -p /root/egl_trace && \
    gcc -O2 -fPIC -shared -o /root/egl_trace/libEGL.so.1 \
    /root/egl_trace_lib.c -I/usr/local/include -ldl -lpthread && \
    cp -a /root/egl_trace/libEGL.so.1 /root/egl_trace/libEGL.so && \
    echo BUILD-OK && ls -la /root/egl_trace/'
