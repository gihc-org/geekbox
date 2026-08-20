# Flash Linux på GeekBox (RK3368)

> **Ny her? Læs [HAANDBOG.md](HAANDBOG.md)** — alt forklaret fra grunden: ordbog, hvordan
> boksen hænger sammen, opskriften på en ny boks, og hver enkelt fælde vi er faldet i med
> symptom, årsag og fix. Skrevet så den kan læses uden forhåndsviden.
>
> **[DOKUMENTATION.md](DOKUMENTATION.md)** er den fulde tekniske historie: arkitektur,
> beslutninger, blindgyder og hvordan hele Devuan-systemet genskabes.
>
> **[GRAFIK-FORKLARET.md](GRAFIK-FORKLARET.md)** fortæller historien om boksens grafik —
> hvorfor WebGL ikke virkede, og hvordan vi vækkede GPU'en — skrevet så en 13-årig kan følge med.

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
| `devuan/` | Scripts og filer til Devuan-sporet (nyere userspace på vendor-kernel, root på SD) — se TODO.md |
| `extracted/` | update.img udpakket: Loader.bin, parameter, uboot/trust/resource/boot-images, rootfs.img |

## Fremgangsmåde

1. **Sæt boksen i flash-tilstand ("Loader"-tilstand):**
   - Tilslut micro-USB-kablet til boksens OTG-port og PC'en
   - Mens boksen har strøm: hold **Update**-knappen nede, tryk kort på **Reboot**, slip Update efter et par sekunder
   - Bemærk: `lsusb` viser altid teksten "RK3368 in Mask ROM mode" for VID/PID 2207:330a, men det er bare en statisk etiket. Update-knappen giver reelt **Loader-tilstand** (det kan ses i `upgrade_tool`s egen enhedsliste). Ægte Mask ROM fås kun, hvis eMMC'ens boot-område er tomt.
2. **Verificér forbindelsen:**
   ```bash
   lsusb | grep 2207
   # Forventet: ID 2207:330a Fuzhou Rockchip Electronics Company
   ```
3. **Flash** (sudo er nødvendig for skriveadgang til USB-enheden):
   ```bash
   sudo Linux_Upgrade_Tool_v1.23/Linux_Upgrade_Tool_v1.23/upgrade_tool uf Geekbox_Lubuntu_V160309/Geekbox_Lubuntu_V160309/update.img
   ```
   - Forventet output: `Loading firmware...` → `Support Type:RK330A` → `Test Device` → `Check Chip` → download med procenttæller → **`Upgrade firmware ok.`**
   - 1,5 GB over USB2 tager ca. 5-10 minutter. Boksen rebooter undervejs — rør ikke kablet
   - Hvis værktøjet spørger `Select input DevNo`, tast `1` + Enter
4. Første boot kan tage et par minutter. Login: **`geekbox`/`geekbox`** (verificeret).

## Boot-arkitektur (verificeret ved læsning af vendor U-Boot-kilde + måling på boks)

- **Boot-kæde:** BootROM → IDB-loader (sector 0x40) → U-Boot (sector 0x2000) → kernel+initramfs (Android bootimg i `boot`-partitionen, sector 0xE000) → root=LABEL=... fundet af initramfs
- **Parameter-filen** styrer kernel cmdline og partitionslayout. Den ligger på **sector 0** i formatet: `"PARM"` + u32 længde + tekst + CRC32 (Rockchips egen variant, `crc32_rk`: MSB-first, poly 0x04C10DB7, init 0). Kilde: `geekboxzone/lollipop_u-boot`, `board/rockchip/common/rkloader/parameter.c` + `lib/crc32_rk.c`
- **U-Boot sammensætter cmdline:** tilføjer selv `earlyprintk=uart8250-32bit,0xff690000` foran og `storagemedia=emmc uboot_logo=... hdmi.vic=16 androidboot.mode=emmc` bagefter
- **Vendor-kernen er monolitisk** (ingen .ko-moduler; rknand/eMMC, dw_mmc/SD, GPU og WiFi er alle indbygget). WiFi-firmware ligger i rootfs under `/system/etc/firmware` (BCM4354/AP6354)
- **Der er ingen framebuffer-konsol på HDMI:** cmdline har kun `console=ttyS2`, så kernel- og initramfs-beskeder (inkl. alle boot-fejl) går kun til seriel. Desktoppen i Lubuntu kommer via X11/GPU, ikke fbcon

## upgrade_tool v1.23 — kommandoliste

Fra binærens egen usage (der findes **ikke** `DB`, `RD`, `TD` i denne version):

```
UF  <Firmware>                        flash hel update.img (virker i Loader-tilstand)
DI  <-p|-b|-k|-s|-r|-m image> [param] flash enkelt-partition; -p = parameter
EF  <Loader|firmware>                 erase
RS  <BeginSec> <SectorLen> [-decode] [File]   læs sektorer
WS  <BeginSec> <File>                 skriv sektorer
RL  <BeginSec> <SectorLen> [File]     læs LBA
WL  <BeginSec> <File>                 skriv LBA
EB  <CS> <BeginBlock> <BlokcLen> [--Force]    erase block
```

Vigtige erfaringer:

- **`DI -p` forventer parameteren som ren tekstfil** (`FIRMWARE_VER:`/`CMDLINE:`/...-linjer, CRLF). Værktøjet tilføjer selv PARM-header og CRC ved skrivning. Giver man den den binære PARM-fil, fejler den med "parameter is invalid, please check!"
- `RL`/`WL` direkte i Loader-tilstand gav "The Device did not support this operation" på denne boks — brug `DI` til partitions-skrivning, `UF` til hele imaget
- **`RS` er ubrugelig i denne build:** parseren afviser alle argument-varianter ("command is invalid"). Readback fra eMMC gøres i stedet med `dd` fra en kørende boks (se DOKUMENTATION.md §9-10). Og kør aldrig `upgrade_tool` helt uden argumenter mens boksen er tilsluttet — uden TTY looper den enhedsvalg-prompten uendeligt
- Boksen reagerer ikke på `RD` — tag strømmen af/på for at genstarte

## Seriel konsol (debugging)

Alt interessant (U-Boot, kernel, initramfs-fejl) skrives til **UART2** — usynligt på TV'et.
For at læse det skal bruges en **USB-til-TTL 3.3V seriel adapter** (fx CP2102 eller FT232RL, 20-60 kr.):

- Forbind GND↔GND, adapter RX↔boks TX, adapter TX↔boks RX (UART2-pins på printet, se schematics via forum-arkivet)
- Indstillinger: **115200 baud, 8N1**
- VIGTIGT: kun 3.3V-niveau — ikke 5V og aldrig RS232 (12V), det ødelægger porten
- Læs på PC'en med fx `minicom -D /dev/ttyUSB0 -b 115200` eller `screen /dev/ttyUSB0 115200`

## Fejlfinding

- **"Check Chip Fail" med nyere værktøjer:** Det moderne `vicharak-in/Linux_Upgrade_Tool` (og muligvis andre nyere builds) kan parse imaget, men fejler ved `Check Chip`, fordi protokollen mod den gamle RK3368-maskrom ikke matcher. Brug `upgrade_tool` v1.23 fra GeekBox' eget arkiv — den taler den rigtige protokol.
- **"Segmentation fault" fra gamle binærer:** Nogle kopier af `upgrade_tool` der cirkulerer, er dynamisk linkede mod forældede biblioteker. v1.23 i dette projekt er statisk linket og virkede uden problemer på Mint 22.3.
- **"Permission denied" mod USB:** Kør med `sudo`, eller lav en udev-regel for VID `2207`.
- **Boksen ses slet ikke i `lsusb`:** Forkert USB-kabel (kun strøm), eller den er ikke rigtigt i flash-tilstand. Prøv igen med Update/Reboot-sekvensen.
- **Boksen hænger ved g-logo uden output:** Fejlen skrives kun til seriel konsol (se ovenfor). Uden seriel adapter er man blind.
- **Alt går galt:** Maskrom ligger i chippens ROM og kan ikke overskrives — boksen kan altid bringes i flash-tilstand igen og flashes forfra med `UF`. Den er praktisk talt ubrickbar via denne metode.

## Reservemetode: rkdeveloptool

Hvis `upgrade_tool` af en eller anden grund ikke virker:

```bash
sudo apt install rkdeveloptool
sudo rkdeveloptool ld                          # skal vise enheden i maskrom
sudo rkdeveloptool db RK3368MiniLoaderAll_V2.26.bin   # upload loader → enheden skifter til loader-tilstand
# derefter kan partitioner skrives med: sudo rkdeveloptool wl <offset> <image>
```

Bemærk: `rkdeveloptool` kan ikke flash en `update.img` direkte — imaget skal pakkes ud i
enkelte partitions-images først (fx med `rkfwtools`/`imgRePackerRK`). Derfor er `upgrade_tool uf` at foretrække.

## Efter dd af et image til SD: udvid filsystemet

Et dd'et image fylder ikke hele SD-partitionen — filsystemet tror, det er lige så stort
som imaget (fx 1,4 GB på en 14,5 GB partition), og man rammer "No space left on device".
Udvid online (kan gøres på den kørende boks, mens / er mountet):

```bash
sudo resize2fs /dev/mmcblk1p1   # SD-kortets root-partition på boksen
```

Eller fra PC'en med kortet i læseren (afmountet): `sudo resize2fs /dev/sda1`

## Kilder (verificeret august 2026)

- **Firmware** (direkte downloads, verificeret fungerende):
  - Lubuntu V160309 (brugt her): http://www.mediafire.com/download/slcgi389dcqd1ae/Geekbox_Lubuntu_V160309.7z (~365 MB)
  - Android+Lubuntu dual-boot V151129: http://www.mediafire.com/download/crmbyl70qqn3crw/Cross_Lollipop_Lubuntu_dualOS_V151129.7z
  - Ældre ren Lubuntu V151221: http://www.mediafire.com/download/834p2zcfjg6idi2/Geekbox_Lubuntu_V151221.7z
  - Nyere DualOS Marshmallow/Lubuntu V170117 findes omtalt på fora, men links (Google Drive) er døde
- **Værktøj + loader:** GitHub-brugeren `geekboxzone`, repo `lollipop_RKTools`, branch `geekbox`:
  - `linux/Linux_Upgrade_Tool/Linux_Upgrade_Tool_v1.23.zip`
  - `windows/AndroidTool_Release_v2.35/rockdev/RK3368MiniLoaderAll_V2.26.bin`
- **U-Boot-kilde (parameter-format, CRC):** `geekboxzone/lollipop_u-boot`
- **Forum-arkiv:** `geekbox.boards.net/thread/6/geekbox-downloads-firmware-tools-schematics` (kræver JavaScript; læs via Wayback Machine)

## Videre muligheder

- **Devuan-sporet (BOOTER ✅ aug 2026):** Devuan Excalibur (armhf) på vendor-kernen med root på SD — se `devuan/` og TODO.md. Opskrift: 01 bygger rootfs (Devuans egen debootstrap), 02 skriver SD (ext4 med `^64bit,^metadata_csum`!), 03 flasher parameter (`DI -p`, tekstformat). Bemærk to fælder løst undervejs: OpenSSH 10's seccomp-sandbox dør på 3.10 → brug **dropbear**; og 14.04-initramfs'en flytter ikke /proc,/sys,/dev ind i det nye root → **`myinit.sh` som PID1-shim** løser det (og giver tidlig netværk+ssh som bonus)
- Mainline-kernen har device-tree for GeekBox (`rk3368-geekbox.dts`), men aktiverer ikke HDMI/GPU/WiFi — realistisk kun som headless server
- Armbian understøtter ikke RK3368
