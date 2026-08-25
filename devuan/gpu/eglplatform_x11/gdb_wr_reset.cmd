# gdb_wr_reset.cmd — fang hvad GPU-processen kalder omkring WR_POST_UPDATE-reset
# (shader-kompilering + MakeCurrent + reset-status), uden at bryde kørslen.
# Bygget til vendor-bibliotekerne (libGLESv2/libEGL_POWERVR_ROGUE.so).
#
# Bruges IKKE direkte længere: capture_gdb_gpu.sh genererer
# /tmp/gdb_wr_reset.<gpu-pid>.cmd med konkrete basisadresser (Android-linkeren
# læser vendor-bibliotekerne uden om gdb's symboltab, så "break glCompileShader"
# rammer forkert — derfor adresse-breakpoints).
#
set pagination off
set confirm off
set print thread-events off
set $n = 0
set $mc = 0
set $ge = 0
set logging overwrite on
set logging file /tmp/gdb_wr_reset.log
set logging on

# Vendor-GL-shader-stien (libGLESv2_POWERVR_ROGUE.so)
break *@GLES2_BASE@
commands
  silent
  set $n = $n + 1
  printf "glCompileShader #%d shader=%u tid=%d\n", $n, $r0, $_thread
  bt 3
  continue
end

# eglMakeCurrent-fejl (libEGL_POWERVR_ROGUE.so, offset 0x11d4)
break *@EGL_BASE@
commands
  silent
  set $mc = $mc + 1
  printf "eglMakeCurrent #%d dpy=%p draw=%p read=%p ctx=%p -> r0=%u tid=%d\n", \
         $mc, $r0, $r1, $r2, $r3, $r0, $_thread
  if $r0 == 0
    printf "  *** eglMakeCurrent FEJL ***\n"
    bt 5
  end
  continue
end

# glGetError: log kun afvigelser (libGLESv2_POWERVR_ROGUE.so, offset 0x2308c)
break *@GLES2_ERROR@
commands
  silent
  set $ge = $ge + 1
  set $err = $r0
  if $err != 0
    printf "glGetError -> 0x%04x tid=%d\n", $err, $_thread
    bt 3
  end
  continue
end

continue
