# Handover — Firefox/WebGL på boks 1 (25. aug 2026, aften)

## Status i ét blik

- **WebGL 2.0 + hele Firefox-UI'et virker** på boks 1 (kristian, desktop-genvej
  "Firefox WebGL"): ingen "browseren understøtter ikke WebGL"-fejl længere,
  titelbjælke + maksimer/minimer-knapper på plads.
- **Åben blokering:** spillet på poki.com (Subway Surfers) indlæser, første
  frame tegnes korrekt på skærmen — men spillet **fryser** (ingen nye frames;
  main-processen spinner på ~118 % CPU). Se §5 nedenfor.
- Læs først `FIREFOX-WEBCL-SESSION-NOTAT-2026-08-25.md` §14-15 — de to store
  fund (sort chrome + manglende dekorationer) er løst der.

## 1. Virkende opskrift (pr. i dag)

Profiler: `/home/kristian/ffprof` (og `/root/ffprof`) med:
```
user_pref("gfx.webrender.enabled", false);          # Basic-kompositor
user_pref("layers.acceleration.disabled", true);    # (WebRender-hw er sort, §14a)
user_pref("browser.tabs.drawInTitlebar", false);
user_pref("gfx.x11-egl.force-enabled", true);
user_pref("webgl.force-enabled", true);
user_pref("layers.gpu-process.enabled", true);
```
Launcher `/usr/local/bin/firefox-webgl` (repo:
`devuan/gpu/eglplatform_x11/start_firefox_webgl.sh`, md5
`2890763aa4bbc1eb19c4c9f9e41058a2`) sætter `_MOTIF_WM_HINTS = 0x2,0x1,...`
når Navigator-vinduet er fremme → titelbjælke/knapper (§14b).

Kør-selv (klik-sti):
```bash
ssh -i /home/kristian/.ssh/geekbox_key root@192.168.0.188
rm -f /home/kristian/ffprof/.parentlock /home/kristian/ffprof/lock
runuser -u kristian -- env -i HOME=/home/kristian USER=kristian LOGNAME=kristian \
  SHELL=/bin/bash PATH=/usr/local/bin:/usr/bin:/bin DISPLAY=:0 \
  DBUS_SESSION_BUS_ADDRESS="unix:path=/tmp/dbus-<session>,guid=..." \
  XDG_RUNTIME_DIR=/run/user/1000 XDG_CONFIG_HOME=/home/kristian/.config \
  XDG_DATA_HOME=/home/kristian/.local/share XDG_CURRENT_DESKTOP=LXDE \
  XDG_SESSION_TYPE=x11 /usr/local/bin/firefox-webgl \
  file:///usr/local/lib/firefox-webgl/webgl_test_dump.html
# verifikation: window.dump → WEBGL_RESULT OK; xwininfo → client Relative Y=23;
# skærmdump → canvas 960x540 + lysegrå værktøjslinje
```

## 2. Efter genstart (root på boksen, i rækkefølge)

```bash
bash devuan/gpu/eglplatform_x11/patch_android_bindapi.sh
bash devuan/gpu/eglplatform_x11/patch_driver_minor.sh
sh /root/gpu_up.sh      # logd + servicemanager + pvrsrvctl + /dev/graphics
```
`/root/gpu_up.sh` ligger allerede på boksen (kopi fra repoet). Ude-regler,
wrapper-chvt-patch og profilprefs overlever genstart; bind-mounts og GPU-init
gør ikke.

## 3. Værktøjer på boksen (genopbyg efter reboot — /tmp er tmpfs!)

`/tmp/xdump.c` → `gcc -o /tmp/xdump /tmp/xdump.c -lX11` — XGetImage-dumper
(root/hel skærm; med vindue-id som arg). NB: XGetImage fejler med BadMatch på
Navigator-klientvinduet (dump i stedet barnet 0x1000056 eller root).

## 4. Fælder (målt, ikke gæt)

- **body-rød-test slår boksen HELT fra** (to gange målt; ingen panic-log i
  3.10-kernen) — kør den aldrig igen.
- `MOZ_GL_SPEW=1` lammer kompositoren (KHR_debug-callback) — aldrig sæt den.
- `pkill -9 -x firefox-esr` rammer kun main; børnene (prctl-titler) lukker
  kanalen med "Exiting due to channel error." + `_exit(0)` — oprydningsartefakt.
- Skærmen sortner kort ved hver start/stop = hybris' display-dans (ufarlig;
  VT=7 + HDMI enable=1 restituerer).
- Firefox skriver prefs.js ved exit — rediger prefs kun mens Firefox ikke kører.
- Ryd `.parentlock` før hver kørsel.
- WebRender-hardwarestien renderer CSS sort i EGL-bufferen (§14a) — den vej er
  stadig uafklaret og er den dybe vej til hurtig rendering.

## 5. Den åbne blokering: spillet fryser

**Symptom (målt 25. aug aften):** Subway Surfers på poki.com: første frame
tegnes (mørkegrøn scene 437k px + lysegråt browser-UI), men 0 pixel ændring
over 4 s (kun panelets ur). main ~118 % CPU, spil-content ~21 %.

**Nuværende hypotese (prioriteret):**
1. Basic-kompositor-readback-stien for WebGL-canvas er for langsom/fastlåst:
   canvas → glReadPixels → CPU → X per frame. ARM-boxen kan ikke følge med →
   spillet ser frosset ud mens main spinner. Test: sænk canvas-opløsning
   (spil-indstillinger) og se om fps stiger; eller tæl faktiske frame-updates.
2. rAF-throttling: tab'en behandles som ikke-synlig? Tjek
   `document.visibilityState` / rAF-rate i spillet.
3. glReadPixels-livelock i main (spinner uden at producere frames) — kræver
   gdb-backtrace af main-processen (pas på: gdb kan crashe på ARM, se notat).

**Næste skridt (forslag i rækkefølge):**
1. Mål frame-cadence: skærmdump hver 2-5 s over 30 s → tæl ændringer i
   canvas-området (scriptet i §15-analysen). Er det 0,1 eller 10 fps?
2. Prøv `layers.acceleration.disabled=false` MED `gfx.webrender.enabled=false`
   (gamle GL-layers-kompositor via EGL) — hvis CSS så ikke er sort, får vi
   hardware-sti tilbage og måske hurtige frames. PAS PÅ: test med en simpel
   side først (ikke spillet), og overvåg boksen (to nedbrud i dag).
3. Dyb vej (mange timer): find ud af hvorfor WebRender-hw renderer CSS sort i
   EGL-bufferen — hvis det løses, er Basic-kompositorens readback omvej væk.

## 6. Filer

- `devuan/gpu/FIREFOX-WEBCL-SESSION-NOTAT-2026-08-25.md` — hele historien.
- `devuan/gpu/eglplatform_x11/` — platform, launcher, patches, test-værktøjer.
- `DOKUMENTATION.md` §5.15c + status.
