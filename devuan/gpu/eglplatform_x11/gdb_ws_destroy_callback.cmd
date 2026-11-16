set pagination off
set print thread-events off
handle SIGSEGV stop print nopass
handle SIGABRT stop print nopass
handle SIGILL stop print nopass
handle SIGBUS stop print nopass
handle SIGSYS stop print nopass

break *ws_DestroyWindow+0x14
commands
  silent
  printf "DESTROY callback=%p win=%p current=%p\n", $r3, *(void **)($r7 + 4), ws_module_info.DestroyWindow
  x/12wx &ws_module_info
  continue
end

continue

printf "=== STOPPED ===\n"
bt 60
info registers pc lr sp r3 r7
x/20wx $r7
thread apply all bt 30
detach
