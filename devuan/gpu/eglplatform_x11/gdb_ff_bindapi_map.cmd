# Trin 1 i mønster-A-sporingen:
# Stop ved FØRSTE eglBindAPI (wrapper) og dump /proc/<pid>/maps, så vi kan
# finde /system/lib/libEGL.so-basen og sætte break på Android-intern
# eglCreateContext (+0x6534) i trin 2. gdb slår ASLR fra, så basen er
# deterministisk i trin 2.
set pagination off
set breakpoint pending on
set follow-fork-mode parent
set detach-on-fork on
set environment LD_PRELOAD=/root/system_shim.so:/root/egl_platform_shim.so
set environment LD_LIBRARY_PATH=/opt/hybris
set environment EGL_PLATFORM=x11
set environment DISPLAY=:0
set environment MOZ_X11_EGL=1
set environment MOZ_DISABLE_CONTENT_SANDBOX=1
set environment MOZ_DISABLE_GPU_SANDBOX=1
break eglBindAPI
run
printf "\n=== HIT eglBindAPI: dump maps (system/lib/libEGL.so) ===\n"
info proc mappings
quit
