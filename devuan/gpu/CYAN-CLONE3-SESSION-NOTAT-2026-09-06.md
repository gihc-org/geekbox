# Cyan-scene-sporet — session-notat 6. sep 2026 (kernel-byg til clone3-fix)

> Agent/model: [codex:???] — udfyldes ved commit (spørg brugeren).
> Fortsættelse af `CYAN-SCENE-HANDOVER-2026-08-27.md` +
> `GRALLOC-LOCK-SPOR-SESSION-NOTAT-2026-08-26.md`.

## Aftaler og beslutninger

- [aftalt] Kernel-byg + flash med compat-fix (6. sep ~22:0x): udvider 3.10-
  kernens compat-tabel fra 384 → 450 poster; 404..449 → `sys_ni_syscall`
  (ENOSYS). Årsag: Firefox' `glean.upload`-tråd kalder clone3 (syscall 435,
  asm-generic); 3.10-kernen har den ikke og dræber processen (SIGILL via
  do_ni_syscall) → hele Firefox crasher 1½-3 min efter start (målt 2× 6. sep,
  også uden probes). Med ni-poster får glibc/Rust normal ENOSYS-fallback på
  clone. Samme config som kørende test-trace2-kernel (out/test.config) →
  `pvrsrvkm_leddaz.ko` forbliver kompatibel (vermagic uændret).
- [udført] Kernel bygget og pakket (6. sep ~22:12): lollipop_kernel gren
  geekbox HEAD 7578834f (2016-06-04), config = kørende test-trace2
  (out/test.config, diff-verificeret identisk). Compat-tabel 450 poster
  verificeret i vmlinux: 403→clock_gettime, 404..449→sys_ni_syscall (incl.
  435/clone3). Image 15.481.560 B →
  `out/clone3fix/ramfs-clone3fix.img` (30.638.080 B, id d91b6871…).
- [udført] FLASHET (6. sep ~22:2x): boks 1, ny kernel kører (uname:
  3.10.0 #1 SMP PREEMPT Sun Sep 6 22:12:06). Verificeret: `clone3`-syscall
  returnerer nu ENOSYS (38) i stedet for at dræbe processen. Firefox kører
  stabilt >10 min (flere gange målt) — glean/clone3-crash væk. GPU bring-up +
  bindapi genbekræftet på ny IP 192.168.0.142.
- [udført] **EFTER-DRAW-READBACK (afgørende fund, ~23:15):** efter hvert stort
  verdens-draw (prog7/10/22, count 564-13.443) læses read-framebufferen
  (fbo=3, 836x470): indholdet er ENSARTET himmel-cyan (143,239,235) — de
  store meshes skriver 0 pixels. Draw-kaldene udføres fejlfrit (0 GL-fejl),
  men rasteriseringen giver intet output. → cyan er IKKE kun et
  præsentations-/readback-artefakt; geometrien når ikke til fragmenterne.
- [målt] Ekstern readPixels/toDataURL er ubrugelig med
  preserveDrawingBuffer=false (bufferet ryddes efter present): gameplay-
  readPixels gav ensartet sort, menu gav cyan — variabelt. Brug derfor
  GL-side-readback (postdraw/swap).
- [målt] GL-installationens draw-flow: alt 3D går gennem
  `glDrawElementsInstanced` (JS-wrapperen fangede kun drawElements → det
  tidligere "kun 18-verts" var et målehul). Scene-programmer: prog7/10/13/16
  (verdens-geometri), prog22 (skindet mesh, bone-array 29×mat4 læst som 0 i
  menu-dump), prog4/19/25 (2D/UI).
- [målt] Load-stall-tilstande: poki sætter `bot=1` i spil-iframe-URL'en ved
  WebDriver/BiDi-session → spillet fryser ved 0%/blå firkant efter BiDi-load.
  Gentagne hurtige Firefox-genstarter giver samme stall (server/rate);
  kølepause + ren profil (cache/cookies slettet) giver load igen.
  Proxy v3 (glReadPixels-hook, md5 99f1b836) korrelerede med load-stall →
  rullet tilbage; v2-funktionalitet (fbo/tex/draw-hooks) er stabil
  (md5 398e71dd; postdraw 9a8127a4; vertex-readback 90006e6b).
- [afventer] Vertex-buffer-readback (90006e6b installeret): fang vertex-
  positioner + uniform-matricer for store draws → offline clip-space-beregning
  (er geometrien i frustum? NaN?) — kører efter næste vellykkede load.
- [udført] **REPLAY-PROBE UDEN POKI (scene_replay_probe.c, ~23:32):** spillets
  fangede prog7-shaders (p7.vs/p7.fs = shader 5/6) kompileres direkte på
  1.5-stakken og tegner til eget FBO:
  - FS-fuldskærm (spillets fragment-shader + trivial VS): HELE skærmen bliver
    hvid — fragment-shaderen kan skrive normalt.
  - VS+FS med syntetisk kvad og identitets-/simple matricer: kvadet tegnes
    (23575/36864 prøver afviger fra clear) — hele shader-pipelinen
    rasteriserer korrekt på 1.5.
  → konklusion: shaders/rasterisering er IKKE problemet; det usynlige output
    i spillet må skyldes draw-tilstand/data (fx drawBuffers≠attachment0,
    geometri uden for frustum, depth/cull-samspil). glGetBufferSubData findes
    IKKE via eglGetProcAddress på stakken (NULL) — vertex-readback via
    buffer-hook i stedet, hvis vi fortsætter.
- [afventer] drawBuffers-sporet: proxy 715716d0 logger glDrawBuffers + spørger
  GL_DRAW_BUFFER0..3 i hver fuld draw-dump. Firefox crashede under load efter
  hook-aktivering (6 drawBuffers-kald logget, ingen kernel-oops) — kan være
  tilfældig ustabilitet; næste kørsel skal gentages på en vellykket load.
- [målt] Firefox-ustabilitet under poki-load er stadig til stede (sporadiske
  crashes uden kernel-spor; dmesg kun HDMI-toggle). Load-stall efter mange
  genstarter kræver kølepause (5+ min) + evt. ren profil.
- [åbent] Hvis geometrien ligger korrekt i clip-space men intet rasteriseres:
  driver-/shader-problem i 1.5-stakken (fx int-varying/loop/derivative);
  sammenlign med Spor B (4.4 + DDK 1.8).
- [udført] Måling (GL-proxy v1+v2, egl_proxy.c): **3D-scenen tegner HELE
  TIDEN** — store meshes (500-6000 verts/draw) kører som
  `glDrawElementsInstanced` mod Firefox' canvas-default-FBO (fbo=3, 836x470,
  viewport matcher, scissor OFF). Tidligere "kun 18-verts-draws" var et
  målehul: JS-wrapperen fangede ikke instanced-draws. Scene-shaders er ES3
  med konsistente kamera-/projektionsmatricer. → cyan-problemet er IKKE
  manglende draw-kald; næste skridt er at afgøre om draw'ene giver pixels
  (buffer-readback/frustum-analyse + gameplay-capture efter kernel-fix).
- [målt] fbo/tekstur-spor: tex 5/6 = 836x470 (canvas); Firefox opretter en ny
  836x470-FBO pr. ~0,5 s under opstart (tex 19-29) — sandsynligvis readback/
  toDataURL-stien fra BiDi-proben (skal reduceres i næste kørsel).
- [målt] Proxy v2-hooks virker (bindFramebuffer/fboTex/texImage2D i
  /tmp/cyan_draw_probe.log); shader-filer gemmes nu korrekt i /tmp/shaders
  (rettet: dir skal være 777, ikke 666).

## Status i ét blik

- Boksen (192.168.0.109) kører kernel 3.10.0 #1 (26. aug 17:52, test-trace2).
- Firefox crasher pga. clone3/syscall-435 → kernel-byg i gang (laptop):
  compat-tabel 450 + clone3-fix; flash via `upgrade_tool DI -b` (loader-
  tilstand; rollback = `extracted/Image/ramfs.img`).
- Efter flash: GPU bring-up (insmod + gpu_up + bindapi) + ren måling med
  reduceret JS-readback (ingen toDataURL-loop), gameplay-capture, og afgør
  om scene-draws giver pixels (buffer/frustum-analyse).

## Nøglekommandoer (kernel-byg, laptop)

```bash
mkdir -p /tmp/kbcc && ln -s /usr/bin/aarch64-linux-gnu-gcc-9 /tmp/kbcc/aarch64-linux-gnu-gcc
git clone -b geekbox https://github.com/geekboxzone/lollipop_kernel.git /tmp/lollipop_kernel
CONFIG_SRC=$PWD/devuan/gpu/kernelbuild/out/test.config \
  bash devuan/gpu/kernelbuild/build_kernel.sh baseline
python3 devuan/gpu/kernelbuild/package_bootimg.py \
  devuan/gpu/kernelbuild/out/baseline/Image \
  extracted/Image/ramfs.img \
  devuan/gpu/kernelbuild/out/baseline/ramfs-clone3fix.img
```

Flash (boksen i loader-tilstand): `upgrade_tool DI -b <ramfs-clone3fix>.img`.
