set pagination off
set print thread-events off
handle SIGSEGV stop print nopass
handle SIGABRT stop print nopass
handle SIGILL stop print nopass
handle SIGBUS stop print nopass
handle SIGSYS stop print nopass

printf "ws_module_info=%p DestroyWindow=%p\n", &ws_module_info, ws_module_info.DestroyWindow
watch -l ws_module_info.DestroyWindow
commands
  silent
  printf "=== WS_MODULE DESTROYWINDOW CHANGED ===\n"
  x/12wx &ws_module_info
  bt 60
  info registers pc lr sp
  thread apply all bt 30
  detach
  quit
end

continue

printf "=== STOPPED WITHOUT WATCHPOINT ===\n"
bt 60
info registers pc lr sp
thread apply all bt 30
detach
