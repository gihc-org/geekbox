# Boot-stabilitet og spillbar provisionering - session-notat 16. nov 2026

> Agent/model: `codex` / `deepseek-flash`. Branch: `firefox-fps-tuning`.

## Status i ét blik

Boksen blev strømcyklet kl. 09:50 på boksens ur: **2013-01-21**. eMMC, X,
HDMI, Ethernet, PulseAudio og `alsa_output.dmixer` virkede, men uret forblev
forkert. `myinit` forsøgte `http://example.com` før DNS var klar. Chrony kunne
heller ikke oprette IPv4-sockets: `_chrony` havde ikke gruppe 3003/inet, så
vendor-kernen gav `EACCES`. Gatewayens HTTP-Date blev derfor den første
pålidelige tidskilde.

Tiden synkroniseres nu via `geekbox-clock-sync`: gatewayens HTTP-Date bruges
først, seneste kendte epoch gemmes på eMMC og gendannes ved boot, og tiden
opdateres hvert 15. minut. Ved første kontrol viste boksen `20:45:12` mod
laptopens `20:45:11`. Efter den næste strømcyklus var tiden korrekt, da
skrivebordet kom frem.

eth0-MAC'en udledes nu deterministisk fra CPU-serialen. Efter strømcyklussen
blev `02:fe:f1:8b:9f:77` verificeret, og boksen fik fortsat
`192.168.0.152` via Ethernet. Da eth0 er unmanaged i NetworkManager, ændres
den ikke senere i bootforløbet.

Fatal-signal-diagnostik sættes ved boot, og der er lavet en golden game-kit og
et provisioning-script til en nyflashet boks.

## Aftaler og beslutninger

- [aftalt] Boksen skal have korrekt tid efter hver strømcyklus uden manuelle
  indgreb. (20:43)
- [aftalt] Flere GeekBoxe skal kunne flashtes og gøres spilklare fra den
  kendte boks uden at gentage GPU-/Firefox-POC'en manuelt. (20:43)
- [udført] Post-boot-audit: ext4 er clean, root er 53 % fyldt, 2 GB swap er
  aktiv, HDMI/X kører på `tty7`, og PulseAudio bruger `alsa_output.dmixer`.
  (20:44)
- [udført] Gatewayens HTTP-Date verificeret uden DNS:
  `Mon, 16 Nov 2026 19:43:44 GMT`. (20:43)
- [udført] `clock_sync.sh` tilføjet med `restore`- og `sync`-mode samt 15
  minutters baggrundssynk. (20:45)
- [udført] Boksen er synkroniseret til samme tid som laptoppen inden for ét
  sekund. (20:45)
- [udført] `myinit.sh` sætter nu stabil MAC ud fra CPU-serien og aktiverer
  `print-fatal-signals`/`exception-trace`. (20:47)
- [udført] `box.sh`, `gpu_setup.sh` og de nye provisioning-scripts bruger
  eksplicitte SSH-options og er uafhængige af laptopens globale SSH-config.
  (20:48)
- [udført] En 6,2 MB golden runtime-kit er eksporteret og verificeret med
  `gzip -t`. (20:48)
- [udført] Efter post-boot-fixene blev Subway Surfers startet gennem det
  rigtige skrivebordsikon-flow og nåede `FAST-PLAY KLAR: 960x540`. (20:51)
- [udført] Launcherens fejlsti rydder nu BiDi-klienten via trap og opdager
  tidligt, hvis `start_cyan_probe`/Firefox dør under indlæsningen. Det fjerner
  de lange efterladte 120-sekunders ventetider og holder ikke låsen unødigt.
  (20:54)
- [udført] Strømcyklus beviste stabil MAC og korrekt klokke ved skrivebordet.
  (20:50)
- [udført] `bcmdhd` indlæses nu og `wlan0` får DHCP (`192.168.0.188`).
  (20:53)
- [udført] Den statiske nødadresse/rute ryddes nu, når den rigtige DHCP-route
  bliver aktiv. (21:07)
- [udført] HTTP-synken venter nu op til 60 sekunder på den rigtige
  DHCP-gateway og foretrækker den frem for metric-100-fallbacken. (21:07)
- [udført] NTP-roden fundet med strace: `_chrony` manglede primær GID
  `3003/inet`, og chronyd rydder supplerende grupper under privilege-drop.
  (21:18)
- [udført] `_chrony` har nu primær GID `inet`, og numeriske NTP-kilder er
  installeret. Chrony valgte `n3.taur.dk` og rapporterer `Leap status: Normal`.
  (21:19)
- [afventer] Base-imaget skal bygges med `sudo devuan/09_make_emmc_img.sh`;
  den nuværende session kan ikke indtaste sudo-adgangskoden. (20:52)

## Tidskæden

`geekbox-clock-sync restore`:

1. Læser `/var/lib/geekbox/clock.epoch`.
2. Sætter kun uret, hvis systemtiden stadig er før 2023.

`geekbox-clock-sync sync`:

1. Læser alle default-gateways fra `ip route`.
2. Henter `Date` fra `http://<gateway>/` uden DNS.
3. Falder tilbage til `http://example.com/`.
4. Stepper systemtiden og gemmer den nye epoch.
5. Kalder `chronyc makestep` som opportunistisk NTP-fallback.

`myinit` kalder restore før netværk, sync efter DHCP og starter derefter en
loop med 900 sekunders interval.

## Stabil MAC

CPU-serien er `31b1283ccef634fd`. MAC'en beregnes som:

```text
02 + de første 5 bytes af sha256(serial)
= 02:fe:f1:8b:9f:77
```

Første byte `02` gør adressen lokalt administreret og unicast. Da serialen er
forskellig mellem boksene, får hver boks en unik adresse uden en separat
identitetsfil.

## Flash og spillbar boks

Base-imaget indeholder Devuan, X/LXDE, Firefox og bootfixene, men ikke den
store `system.img` eller den færdigbyggede GPU-/spilruntime. Efter første boot:

```bash
bash devuan/gpu/provision_game_box.sh <ny-ip> <kendt-god-ip>
```

Scriptet:

1. installerer vendors `system.img` og Hybris-broer via `gpu_setup.sh`
2. eksporterer GPU-platform, Firefox-shims, hjælpescripts og en minimal
   spilprofil fra den kendte boks
3. installerer launcher, skrivebordsikon og sudo-regel
4. installerer aktuel `myinit.sh` og tidsfix
5. verificerer de kritiske filer

`start_game_all.sh` monterer nu selv `system.img`, så et ekstra reboot er ikke
påkrævet for at starte spillet. Den aktuelle boks har allerede valideret
boot-MAC og tidsgendannelse; den route-bevidste første-synk skal valideres på
næste nyflashede boks eller næste strømcyklus.

## Netværk og NTP

Den nuværende kernel har `bcmdhd`, og efter at `myinit` har monteret
`system.img`, kommer `wlan0` op og får DHCP. Grundet firmware-load tager det
omkring 38 sekunder. Ethernet er primær rute.

Laptoppen bruger `ntpsec 1.2.3` og har reachable Debian-pools. En direkte
UDP/123-test fra boksen fik også svar fra både Google og laptoppen. NTP var
derfor ikke filtreret.

Strace viste den egentlige fejl:

```text
socket(AF_INET, SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK, IPPROTO_IP)
    = -1 EACCES (Permission denied)
```

Vendor-kernen bruger Android-paranoid netværk og kræver aflagte rettigheder.
Chronyd skifter til `_chrony` og rydder supplerende grupper, så et almindeligt
gruppe-tillæg forsvinder. Løsningen er at give `_chrony` primær GID `3003`
(`inet`) og installere DNS-uafhængige numeriske servere i
`/etc/chrony/sources.d/geekbox-fixed.sources`.

HTTP-Date beholdes som fallback, hvis NTP eller DNS midlertidigt svigter.

## Kommandoer

```bash
# På boksen
/usr/local/sbin/geekbox-clock-sync restore
/usr/local/sbin/geekbox-clock-sync sync
date '+%F %T %Z (%s)'

# Fra laptoppen
bash devuan/find_box.sh
bash devuan/gpu/export_game_kit.sh <kilde-ip>
bash devuan/gpu/provision_game_box.sh <ny-ip> <kilde-ip>
```
