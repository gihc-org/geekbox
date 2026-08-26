set pagination off
set confirm off
set height 0
set width 0
attach @GPID@
handle SIGSEGV stop print nopass
continue
printf "===== SIGSEGV FANGET =====\n"
bt 30
info registers
detach
quit
