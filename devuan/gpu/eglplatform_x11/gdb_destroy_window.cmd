set pagination off
set print thread-events off
handle SIGSEGV stop print nopass
handle SIGABRT stop print nopass
handle SIGILL stop print nopass
handle SIGBUS stop print nopass
handle SIGSYS stop print nopass

break x11ws_DestroyWindow
commands
  silent
  printf "DESTROY window=%p\n", win
  x/16wx win
  continue
end

continue

printf "=== STOPPED ===\n"
bt 60
info registers pc lr sp
thread apply all bt 30
detach
