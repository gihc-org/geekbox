set pagination off
set confirm off
set height 0
set width 0
set $hits = 0
attach @GPID@

b *@BASE@+0x4935
commands
silent
printf "LOCK entry: module=%08x handle=%08x usage=%08x l=%d t=%d w=%d h=%d vaddrp=%08x\n", $r0, $r1, $r2, $r3, *(unsigned int*)($sp+0x30), *(unsigned int*)($sp+0x34), *(unsigned int*)($sp+0x38), *(unsigned int*)($sp+0x3c)
set $hits = $hits + 1
if $hits >= 8
  detach
  quit
end
continue
end

b *@BASE@+0x33d3
commands
silent
printf ">>> HIT BEFORE-REGISTER (line 1730, lock private data)\n"
set $hits = $hits + 1
if $hits >= 8
  detach
  quit
end
continue
end

b *@BASE@+0x33af
commands
silent
printf ">>> HIT PVRSRV-CPUMAP-FAIL (line 1751, lock private data)\n"
set $hits = $hits + 1
if $hits >= 8
  detach
  quit
end
continue
end

b *@BASE@+0x4a6b
commands
silent
printf ">>> HIT HANDLE-FLAGS-FAIL (4178 returned 0)\n"
set $hits = $hits + 1
if $hits >= 8
  detach
  quit
end
continue
end

b *@BASE@+0x4a35
commands
silent
printf ">>> HIT VADDR-NULL / usage-check fail (line 2073)\n"
set $hits = $hits + 1
if $hits >= 8
  detach
  quit
end
continue
end

b *@BASE@+0x4a45
commands
silent
printf ">>> HIT MUTEX-FAIL (line 156)\n"
set $hits = $hits + 1
if $hits >= 8
  detach
  quit
end
continue
end

b *@BASE@+0x4a71
commands
silent
printf ">>> HIT MODULE-NULL (line 2060)\n"
set $hits = $hits + 1
if $hits >= 8
  detach
  quit
end
continue
end

b *@BASE@+0x4a3f
commands
silent
printf ">>> HIT SERVICES-DISCONNECTED (-25)\n"
set $hits = $hits + 1
if $hits >= 8
  detach
  quit
end
continue
end

continue
