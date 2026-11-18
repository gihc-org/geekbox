# Dokumentationsrevision og NTP-verifikation - 18. nov 2026

> Agent/model: `codex` / `deepseek-flash`. Branch: `firefox-fps-tuning`.

## Status i ét blik

Dokumentationen er afstemt med den kørende boks og de seneste commits.
Boksen har kørt stabilt efter strømcyklussen 16. nov. Tiden er korrekt,
Subway Surfers starter via skrivebordsikonet, stabil MAC er
`02:fe:f1:8b:9f:77`, og både Ethernet og WiFi virker.

NTP blev kontrolleret igen 18. nov: chrony er synkroniseret mod
`91.229.203.9`, stratum 3, `Leap status: Normal` og cirka 50 mikrosekunders
offset. HTTP-`Date` via gatewayen er fortsat fallback.

De forældede oplysninger om "NTP filtreret", "WiFi mangler" og "MAC valideres
først ved næste reboot" er fjernet fra de aktuelle statusafsnit. Historiske
målinger er bevaret som historik.

## Aftaler og beslutninger

- [udført] Laptoppens NTP verificeret: `ntpsec 1.2.3` bruger fire
  Debian-pools, og kilderne har `reach=377`. (21:11)
- [udført] Boksen kan sende og modtage UDP/123 mod både Google og laptoppen.
  (21:14)
- [udført] Chronys oprindelige fejl fundet med strace:
  `socket(AF_INET) = EACCES`. `_chrony` skal have primær GID `3003/inet`,
  fordi supplerende grupper ryddes ved privilege-drop. (21:18)
- [udført] Numeriske NTP-kilder installeret, så chrony ikke afhænger af DNS
  tidligt i boot. (21:19)
- [udført] `09_make_emmc_img.sh` og `provision_game_box.sh` anvender nu
  gruppe- og kildefixet på nye bokse. (21:23)
- [udført] `AGENTS.md`, `TODO.md`, flashvejledningen, NTP-/boot-notatet og
  fælde 70 er afstemt med virkeligheden. (21:24)
- [afventer] Base-imaget skal bygges med `sudo devuan/09_make_emmc_img.sh`.
  (21:24)
- [afventer] Den anden boks skal flashes fysisk og provisioneres med
  `devuan/gpu/provision_game_box.sh`. (21:24)

## Næste skridt

1. Byg base-imaget.
2. Sæt den anden boks i loader-tilstand og flash med `devuan/testflash.sh`.
3. Find IP med `devuan/find_box.sh`.
4. Kør:

   ```bash
   bash devuan/gpu/provision_game_box.sh <ny-ip> 192.168.0.152
   ```

5. Kontrollér tid, NTP, MAC, Ethernet/WiFi og ét-kliks spilstart.

## Relevante filer

- `docs/log/2026-11-16-boot-stability.md`
- `docs/log/2026-11-16-launcher.md`
- `docs/boksen/flash.md` §11
- `docs/faeller.md` fælde 69-70
- `TODO.md` aktuel handover
