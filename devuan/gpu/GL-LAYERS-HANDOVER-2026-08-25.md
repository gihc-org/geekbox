# Handover — GL-layers-forsøget (spillet fryser) (25. aug 2026, nat)

Læs først `devuan/gpu/GL-LAYERS-SESSION-NOTAT-2026-08-25.md` (alle målinger)
og `DOKUMENTATION.md` §5.15d (sammenfatning). Dette er den korte overlevering:
hvad der virker, hvad der er målt, og hvad næste session skal gøre.

## Status i ét blik

- **WebGL 2.0 + hele Firefox-UI'et virker** med Basic-kompositoren
  (`gfx.webrender.enabled=false` + `layers.acceleration.disabled=true`) —
  desktop-genvej "Firefox WebGL", klik-verificeret.
- **Subway Surfers (poki.com) fryser** i begge kompositor-tilstande:
  - Basic: spillet tegner første frame, derefter 0 fps (main ~118 % CPU,
    content ~3.800 `clock_gettime`/sek, SoftwareVsyncThread-spin, ingen
    GPU-proces).
  - **GL-layers (`layers.acceleration.disabled=false`): spillet ANIMERER i
    vinduet (70–168k px/2–4 s) — men skærmen (root/fb0) opdaterer ikke**
    efter første frame(s). To svigtmønstre målt (DeviceReset → sort skærm
    mens vinduet kører; eller bare sænket present-rate ~5 fps → frossen
    skærm). To spil-kørsler tog HELE BOKSEN ned (strøm-cykling).
- **Efter genstart + genkørte patches virker GL-layers end-to-end for den
  simple testside** (`webgl_test_dump.html`): animation når root (518.481
  px/2 s) og fb0 (329.280 px/2 s). Stakken er sund — fejlen er spil-specifik.

## Hvad vi ved (målt, ikke gæt)

- Present-kæden kan stoppe UDEN GPU-reset (målt run 4: første frame på root
  kl 20:26:40–44, derefter 0 ændringer; kun én wrap; 0 DeviceReset).
- Vinduets pixels kan animere mens skærmen er frossen (målt run 5: vindue
  72.033 px/4 s, root 49 px/4 s = kun uret; GPU-processen i live, 72–91 %
  CPU).
- X-serveren KAN kompositerer 32-bit vinduer/children til root OG fb0 —
  bevist med `x32probe1–4.c` (openbox-framet, visual 0x1ec, separat
  X-forbindelse, XSync pr. frame, gentagne XPutImage). Vindues-attributter
  matcher Firefox'. Fejlen er Firefox/GPU-genstart-specifik, IKKE serveren.
- GL-layers-stien er ikke en erstatning endnu: spil-kørsler er ustabile og
  kan tage boksen ned. Basic-kompositoren er status quo.

## Værktøjer (nye, alle i `devuan/gpu/eglplatform_x11/`)

| Værktøj | Formål |
|---|---|
| `xdump.c` | XGetImage-dump (root eller vindue) → rå pixels; byg med `gcc -o /tmp/xdump xdump.c -lX11` |
| `rootdiff.c` | 16-bpp-diff mellem to råfiler (tidslinje over skærmopdateringer) |
| `capture_game_black.sh` | snapshot-loop (root + fb0 + vindue-attributter hver 4 s) under spilkørsel |
| `stall_capture.sh` | detekterer present-stall og tager gdb-backtrace af GPU-processen — **NB: ret GPU-lookup først** (pgrep+grep matchede ikke) og gør stall-detektion robust mod log-flush |
| `start_game.sh` | instrumenteret spil-launcher (kristian-session, remote debugging 9222, log) |
| `x32probe1–4.c` | X-kompositerings-prober (beviste serveren kan) |
| `start_firefox_webgl.sh` | den eksisterende launcher (Basic-sti, uændret) |

## Fælder (målt)

- Spil-kørsler kan tage boksen ned — stop med `pkill -9 -x firefox-esr` ved
  høj load; boksen døde to gange i denne session.
- `pkill -9 -x firefox-esr` rammer kun main; børnene lukker kanalen med
  "Exiting due to channel error." — oprydningsartefakt, ikke en fejl.
- `MOZ_GL_SPEW=1` lammer kompositoren — kør aldrig med den.
- body-rød-test slår boksen helt fra — kør aldrig.
- Firefox omskriver `prefs.js` ved exit — `layers.acceleration.disabled=false`
  forsvinder fra filen (det ER default); tjek prefs hvis konfigurationen
  "mangler".
- /tmp på boksen er tmpfs — værktøjer og logs forsvinder ved genstart.
  Genopbyg med scp fra repoet (se nedenfor).
- gdb på ARM kan crashe ved LR-retur-breakpoints — almindelig attach +
  `thread apply all bt` virker (målt på main-processen).

## Efter genstart af boksen (root, i rækkefølge)

```bash
# laptop:
bash devuan/gpu/eglplatform_x11/patch_android_bindapi.sh
bash devuan/gpu/eglplatform_x11/patch_driver_minor.sh
# boks:
sh /root/gpu_up.sh
# værktøjer (tmpfs — genopbyg):
scp devuan/gpu/eglplatform_x11/{start_game.sh,capture_game_black.sh,stall_capture.sh,xdump.c,rootdiff.c,x32probe.c,x32probe2.c,x32probe3.c,x32probe4.c} root@192.168.0.188:/tmp/
# boks: gcc -o /tmp/xdump /tmp/xdump.c -lX11 (og de øvrige .c-filer)
```

Launcher (`/usr/local/bin/firefox-webgl`), platformmodul
(`/usr/local/lib/libhybris/eglplatform_x11.so` md5 `78580702...`), udev-regler,
wrapper-patch (`/opt/hybris/libEGL.so`) og profil-prefs overlever genstart.
Android-bind-mounts og GPU-init gør ikke.

## Næste skridt (prioriteret, delvist aftalt med brugeren)

1. **Byg en lokal WebGL-stress-side** (ingen reklamer/Unity/netværk):
   kontinuerlig rAF-animation, fuld canvas, FPS via `dump()`, justerbar
   opløsning. Kør 5+ min med GL-layers og mål root/fb0.
   - Skærmen tier → fejlen er vores present-sti (fortsæt med 3-4).
   - Skærmen kører → fejlen er poki/Unity-specifik (test uBlock + andre spil).
2. **uBlock Origin** i `/home/kristian/ffprof` (Firefox stoppet) som kontrol —
   reklamer er ikke årsagen, men slider på ressourcerne.
3. **gdb på GPU-processen** når present-kæden sænker farten — ret
   `stall_capture.sh`'s GPU-lookup først (`pgrep -f "contentproc"` + tjek
   `cmdline` på " gpu"-suffiks; brug `ps -eo pid,args`).
4. Installér `xrefresh` (x11-utils) og test om tvungen skærmopdatering får
   indholdet frem (server-redraw-fejl → workaround).

## Boksens tilstand ved sessionslut

Firefox stoppet, VT=tty7, HDMI=1, patches + GPU-init kørt, værktøjer bygget i
/tmp (forsvinder ved genstart). Profil i GL-layers-tilstand (acceleration
default = til, webrender false, gpu-process true, devtools remote prefs sat).
Remote debugging-port 9222 blev aldrig observeret lyttende trods
`--start-debugging-server 9222` + prefs — uafklaret, ikke kritisk for
skærm-diagnosen.
