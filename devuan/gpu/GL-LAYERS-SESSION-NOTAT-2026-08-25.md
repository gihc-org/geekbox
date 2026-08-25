# GL-layers-forsøg — session-notat 25. aug 2026 (aften/nat)

## Status i ét blik

- **`layers.acceleration.disabled=false` + `gfx.webrender.enabled=false`
  (gamle GL-layers-kompositor via EGL) får spillet til at animere** — modsat
  Basic-kompositoren, hvor Subway Surfers frøs efter første frame (0 fps,
  clock_gettime-storm i content-main ~3,8 kHz + SoftwareVsyncThread-spin).
  Med GL-layers: GPU-proces kører, present-tæller vokser (#900+), vinduets
  pixels animerer (70–168k px ændret pr. 2–4 s).
- **MEN præsentationen til skærmen knækker efter GPU-proces-genstart:**
  ~50 frames inde logges `DeviceResetReason::UNKNOWN WR_POST_UPDATE`, GPU-
  processen genstarter, wrap-genoptages på samme vindue (0x1000057), og de
  nye presents opdaterer KUN vinduets egne pixels — X-root og fb0 forbliver
  statiske (kun panelets ur). Sessionen dør senere
  (`CompositorBridgeChild ... AbnormalShutdown`, `accel canvas lost`,
  channel error), og 3. kørsel tog hele BOKSEN ned (genstart).
- **Efter reboot + genkørte patches virker GL-layers end-to-end for den
  simple side:** WEBGL_RESULT OK, og animationen når root (518.481 px/2 s)
  OG fb0 (329.280 px/2 s). Så skærm-stien virker — den knækker kun i
  spil-sessionen efter GPU-reset.

## Hvad vi ved om den sorte skærm (målt)

- Vindue 0x1000057 (EGL-barn, depth 32): animerer (XGetImage viser spillet).
- Navigator 0x100003c (depth 32): XGetImage inkluderer barnet — spillet.
- Openbox-ramme 0xe006f9 (depth 32): SORT i indholdsområdet.
- X-root (depth 16) + fb0: statisk/sort i vindueområdet.
- `x32probe1–4` (repoet) beviser at X-serveren KAN kompositerer 32-bit
  vinduer/children til root OG fb0: openbox-framet, visual 0x1ec (Firefox'),
  separat X-forbindelse som tegner, XSync efter hver frame, gentagne frames
  (grøn/blå skift) — ALT virker isoleret. Fejlen er altså Firefox/GPU-
  genstart-specifik, IKKE en server-begrænsning. (Uafklaret.)

## Tilføjelse efter run 4+5 (samme nat)

- **Frysen kræver IKKE GPU-reset:** i run 4 (snapshot hver 4 s over 5 min)
  kom spillets første frame på root kl 20:26:40–44 (126.416 px ændret i
  vindueområdet) — derefter 0 ændringer, UDEN DeviceReset (grep = 0) og kun
  ÉN wrap ("pakket ind" = 1). Present-kæden sænkede til ~5 fps (50 presents
  pr. 10 s; loggen skriver hver 50.) og stoppede til sidst helt.
- **Vinduet animerer mens skærmen er frossen:** målt i run 5: vindue 72.033
  px/4 s ændret, X-root 49 px/4 s (kun uret). GPU-processen var i live og
  travl (72–91 % CPU) — den spinner/arbejder, men præsenterer ikke til
  skærmen.
- **Boksen gik ned to gange** under spil-kørsler (strøm-cykling; samme
  mønster som body-rød-testen — ingen panic-log). Stop spillet ved høj load
  (`pkill -9 -x firefox-esr`) i stedet for at vente.
- stall_capture.sh detekterede "stall" for tidligt (log-flush: talte kun 2
  present-linjer mens loggen faktisk havde #150); GPU-proces-lookup fejlede
  (`pgrep -f contentproc` + `grep " gpu$"` matchede ikke) — ret begge før
  brug. gdb-attach på main-processen virkede tidligere fint på boksen.
- `xrefresh` findes ikke (x11-utils ikke installeret) — kunne ikke teste om
  tvungen Expose får indholdet frem. Installér ved lejlighed.
- **Efter genstart + genkørte patches virkede GL-layers end-to-end for den
  simple side igen** (root 518.481 px, fb0 329.280 px ændret/2 s) — så
  stakken er sund; fejlen er spil-specifik.

## Næste skridt

1. **Lokal WebGL-stress-side** (ingen reklamer/Unity/netværk): kontinuerlig
   rAF-animation, fuld canvas, FPS via dump, justerbar opløsning. Kør 5+ min.
   Virker skærmen hele tiden → poki/Unity-specifikt; tier den → vores
   present-sti. (Plan godkendt af bruger; side endnu ikke bygget.)
2. **uBlock Origin** i profilen som kontrol (brugerens forslag; reklamer er
   ikke årsag til frysen, men slider på ressourcerne og kan øge reset-risiko).
   Installeres i /home/kristian/ffprof mens Firefox er stoppet.
3. **gdb på GPU-processen** når present-kæden sænker farten — find den
   blokerende tråd (gralloc-lock? XSync? buffer-tømning?). Ret
   stall_capture.sh's GPU-lookup først.
4. Installér `xrefresh` (x11-utils) og test om tvungen skærmopdatering får
   indholdet frem (server-redraw-fejl → workaround).

## Kommandoer der virker (efter genstart, root)

```bash
# laptop:
bash devuan/gpu/eglplatform_x11/patch_android_bindapi.sh
bash devuan/gpu/eglplatform_x11/patch_driver_minor.sh
# boks:
sh /root/gpu_up.sh
# værktøjer (tmpfs — genopbyg efter reboot):
scp devuan/gpu/eglplatform_x11/{start_game.sh,capture_game_black.sh,xdump.c,x32probe*.c} root@192.168.0.188:/tmp/
# boks: gcc -o /tmp/xdump /tmp/xdump.c -lX11 (og x32probe*)
```

Profilen er i GL-layers-tilstand (acceleration default = til, webrender
false, gpu-process true). Profil-preferences kan blive omskrevet ved
Firefox-exit (fx `layers.acceleration.disabled=false` forsvinder — det ER
default), så tjek `prefs.js` efter behov.
