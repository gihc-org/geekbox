#!/bin/bash
# Bygger en TOM stub-libGL.so.1 i /root/egl_trace/.
#
# Baggrund (målt 24. aug 2026): Firefox' GLContext::InitImpl loader alle
# kerne-GL-symboler via SymbolLoader::GetProcAddress, som prøver
# dlsym(mGLLibrary="libGL.so.1") FØRST — og dlsym finder Mesas libGL
# (libgl1 1.7.0). Dermed peger fActiveTexture m.fl. på Mesas dispatch uden
# nogen Mesa-kontekst, og InitImpl fejler på Mesas glGetError() (ingen
# kontekst) → "Failed to create EGLContext!: 0x3000" → SW-WR.
#
# Lap: læg stubben FØRST i LD_LIBRARY_PATH (/root/egl_trace), så
# PR_LoadLibrary("libGL.so.1") lykkes, men hvert dlsym-symbol fejler →
# SymbolLoader falder tilbage på eglGetProcAddress → wrapper →
# Android/PowerVR-driverens GLES-funktioner (samme vej som glxtest, som
# er GRØN).
set -uo pipefail

BOX=root@192.168.0.188
KEY=/home/kristian/.ssh/geekbox_key

ssh -i "$KEY" -o ConnectTimeout=8 "$BOX" 'mkdir -p /root/egl_trace && \
    printf "int glstub_dummy;\n" > /root/egl_trace/glstub.c && \
    gcc -shared -fPIC -Wl,-soname,libGL.so -o /root/egl_trace/libGL.so \
        /root/egl_trace/glstub.c && \
    gcc -shared -fPIC -Wl,-soname,libGL.so.1 -o /root/egl_trace/libGL.so.1 \
        /root/egl_trace/glstub.c && \
    echo BUILD-OK && \
    echo "== SONAME/eksporter (må IKKE indeholde gl*-funktioner) ==" && \
    for f in libGL.so libGL.so.1; do \
      echo "--- $f ---"; \
      readelf -d /root/egl_trace/$f | grep SONAME; \
      arm-linux-gnueabihf-nm -D /root/egl_trace/$f 2>/dev/null | grep -E " T |glstub" | head; \
    done; \
    ls -la /root/egl_trace/'
