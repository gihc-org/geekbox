# Flash Linux på GeekBox (RK3368)

Metode til at flashe GeekBox-boksen med Lubuntu Linux fra en moderne Linux-maskine
(testet på Linux Mint 22.3, august 2026). Erstatter boksens Android helt.

## Forudsætninger

- GeekBox med RK3368, strømforsyning, micro-USB-kabel **med dataforbindelse** (mange billige kabler er kun til opladning)
- `7z` installeret (`sudo apt install p7zip-full`)
- `rkdeveloptool` som reserveværktøj (`sudo apt install rkdeveloptool`) — ikke strengt nødvendig for hovedmetoden

## Filer i dette projekt

| Fil/mappe | Beskrivelse |
|---|---|
| `Geekbox_Lubuntu_V160309/Geekbox_Lubuntu_V160309/update.img` | Selve firmwaren: Lubuntu (Ubuntu 14.04.3, kernel 3.10.79), marts 2016. Indeholder selv loaderen (v2.40), så ingen separat loader-fil er nødvendig |
| `Linux_Upgrade_Tool_v1.23/Linux_Upgrade_Tool_v1.23/upgrade_tool` | Rockchips originale flash-værktøj (32-bit, statisk linket — kører fint på moderne 64-bit distroer) |
| `RK3368MiniLoaderAll_V2.26.bin` | Maskrom-loader til reservemetoden med `rkdeveloptool` |
| `UpgradeTool_Geekbox_v1.39/` | Windows-værktøj (FactoryTool.exe) — ikke brugt, kun hentet for fuldstændighed |

## Fremgangsmåde

1. **Sæt boksen i Mask ROM-tilstand:**
   - Tilslut micro-USB-kablet til boksens OTG-port og PC'en
   - Mens boksen har strøm: hold **Update**-knappen nede, tryk kort på **Reboot**, slip Update efter et par sekunder
2. **Verificér forbindelsen:**
   ```bash
   lsusb | grep 2207
   # Forventet: ID 2207:330a Fuzhou Rockchip Electronics Company RK3368 in Mask ROM mode
   ```
3. **Flash** (sudo er nødvendig for skriveadgang til USB-enheden):
   ```bash
   sudo Linux_Upgrade_Tool_v1.23/Linux_Upgrade_Tool_v1.23/upgrade_tool uf Geekbox_Lubuntu_V160309/Geekbox_Lubuntu_V160309/update.img
   ```
   - Forventet output: `Loading firmware...` → `Support Type:RK330A` → `Test Device` → `Check Chip` → download med procenttæller → **`Upgrade firmware ok.`**
   - 1,5 GB over USB2 tager ca. 5-10 minutter. Boksen rebooter undervejs — rør ikke kablet
   - Hvis værktøjet spørger `Select input DevNo`, tast `1` + Enter
4. Første boot kan tage et par minutter. Standard-login er formentlig `geekbox`/`geekbox` eller `linaro`/`linaro`.

## Fejlfinding

- **"Check Chip Fail" med nyere værktøjer:** Det moderne `vicharak-in/Linux_Upgrade_Tool` (og muligvis andre nyere builds) kan parse imaget, men fejler ved `Check Chip`, fordi protokollen mod den gamle RK3368-maskrom ikke matcher. Brug `upgrade_tool` v1.23 fra GeekBox' eget arkiv — den taler den rigtige protokol.
- **"Segmentation fault" fra gamle binærer:** Nogle kopier af `upgrade_tool` der cirkulerer, er dynamisk linkede mod forældede biblioteker. v1.23 i dette projekt er statisk linket og virkede uden problemer på Mint 22.3.
- **"Permission denied" mod USB:** Kør med `sudo`, eller lav en udev-regel for VID `2207`.
- **Boksen ses slet ikke i `lsusb`:** Forkert USB-kabel (kun strøm), eller den er ikke rigtigt i Mask ROM-tilstand. Prøv igen med Update/Reboot-sekvensen.
- **Alt går galt:** Maskrom ligger i chippens ROM og kan ikke overskrives — boksen kan altid bringes i Mask ROM-tilstand igen og flashes forfra. Den er praktisk talt ubrickbar via denne metode.

## Reservemetode: rkdeveloptool

Hvis `upgrade_tool` af en eller anden grund ikke virker:

```bash
sudo apt install rkdeveloptool
sudo rkdeveloptool ld                          # skal vise enheden i maskrom
sudo rkdeveloptool db RK3368MiniLoaderAll_V2.26.bin   # upload loader → enheden skifter til loader-tilstand
# derefter kan partitioner skrives med: sudo rkdeveloptool wl <offset> <image>
```

Bemærk: `rkdeveloptool` kan ikke flash en `update.img` direkte — imaget skal pakkes ud i
enkelte partitions-images først (fx med imgRePackerRK). Derfor er `upgrade_tool uf` at foretrække.

## Kilder (verificeret august 2026)

- **Firmware** (direkte downloads, verificeret fungerende):
  - Lubuntu V160309 (brugt her): http://www.mediafire.com/download/slcgi389dcqd1ae/Geekbox_Lubuntu_V160309.7z (~365 MB)
  - Android+Lubuntu dual-boot V151129: http://www.mediafire.com/download/crmbyl70qqn3crw/Cross_Lollipop_Lubuntu_dualOS_V151129.7z
  - Ældre ren Lubuntu V151221: http://www.mediafire.com/download/834p2zcfjg6idi2/Geekbox_Lubuntu_V151221.7z
  - Nyere DualOS Marshmallow/Lubuntu V170117 findes omtalt på fora, men links (Google Drive) er døde
- **Værktøj + loader:** GitHub-brugeren `geekboxzone`, repo `lollipop_RKTools`, branch `geekbox`:
  - `linux/Linux_Upgrade_Tool/Linux_Upgrade_Tool_v1.23.zip`
  - `windows/AndroidTool_Release_v2.35/rockdev/RK3368MiniLoaderAll_V2.26.bin`
- **Forum-arkiv:** `geekbox.boards.net/thread/6/geekbox-downloads-firmware-tools-schematics` (kræver JavaScript; læs via Wayback Machine)

## Videre muligheder

- Opdatér userspace oven på den gamle kernel (`apt update && apt upgrade` — basen er Ubuntu 14.04, så forvent begrænsninger)
- Mainline-kernen har device-tree for GeekBox (`rk3368-geekbox.dts`), så en moderne kernel er et muligt (men stort) DIY-projekt — WiFi (AP6354) og GPU virker formentlig ikke med mainline
- Armbian understøtter ikke RK3368
