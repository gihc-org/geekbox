Start med at læse `docs/log/2026-10-01-chromium-fps.md` (HELE notatet — der er
to sessioner i samme fil: "Status i ét blik" for begge, "Aftaler og
beslutninger", "Opfølgning ~00:35" og "Opfølgning 2"), Chromium-afsnittet i
`TODO.md` (prompten "God start i en ny session (Chromium-sporet, 2. okt 2026)")
og `OVERBLIK.md`. Branch er `chromium-fps`; de seneste commits er
dokumentations-committen med zero-copy-fundet og denne handover, `d0384c6`
(depth-clear + HDMI-genoptagelse), `b129651` (ren hardware-EGL-kørsel) og
`128d190` (1. okt-verifikation).

STATUS 2. okt 2026 ~00:50: **Boksen er slukket** — brugeren måtte bruge strømmen
til en anden maskine, så intet kan verificeres før den er tændt igen.
Chromium-vejen virker teknisk: hardware-EGL (ANGLE `gles-egl` mod hybris) giver
WebGL 2.0 på PowerVR G6110 med 0 `GPU process exited`, 0 watchdog-hits og 0
`CONTEXT_LOST`, og præsentationen til fb0 er farvekorrekt (verificeret med
farvebånd og en statisk WebGL-canvas — "regnbuen" var en anden frame af en
animeret testside). Spillet loadede, men skærmen stod fast på logoet; årsagen er
fundet: `--enable-zero-copy` fik compositoren til at vise en forældet
canvas-buffer (fb0's canvas-rektangel havde samme hash i 12 s, mens Chromiums
egen gengivelse ændrede sig ved hvert CDP-screenshot). Uden flaget opdaterer
fb0's canvas-område hvert par sekunder — flaget er nu slået fra som standard i
`start_chromium_probe.sh`. Om spillet dermed når forbi intro til gameplay, og om
dreng/skinner/tog er synlige, er IKKE bekræftet.

FØRSTE OPGAVE: bring boksen op igen UDEN at køre `bringup_after_power.sh` (den
lægger vnext12 tilbage og fjerner Chromium-EGL-lappen). Brug i stedet opskriften
"Gendan boksen efter genstart" i `docs/log/2026-10-01-chromium-fps.md`: sæt
uret, `insmod /root/pvrsrvkm_leddaz.ko` + `sh /root/gpu_up.sh`, `chmod 666
/dev/sw_sync`, kør bindapi-lappen med `BOXIP=<ip>`, og læg repoets
`devuan/gpu/eglplatform_x11/start_chromium_probe.sh` over på boksen. Start
derpå en ren kørsel på `https://poki.com/en/g/subway-surfers` (scriptets
defaults er hardware-EGL + software-compositing + `--disable-gpu-watchdog` +
`CYAN_DEPTHCLEAR=1` + zero-copy FRA) og bekræft med `chromium_cdp.py probe 8`
samt to fb0-dumps af canvas-rektanglet, der SKAL give forskellige hashes.
Klik derefter spillet i gang og afgør sammen med brugeren, om spillet er
spilbart og figuren/skinnerne/toget er synlige; mål først derefter fps (rAF +
`fb_fps`). Vær opmærksom på HDMI-fælden: nedlukning af en hardware-EGL-Chromium
kan slå udgangen fra (`echo 1 > /sys/class/display/HDMI/enable` bringer billedet
tilbage, og start-scriptet gør det nu selv). Dokumentér i
`docs/log/2026-10-02-chromium-fps.md`, og opdatér TODO/OVERBLIK samme time.
