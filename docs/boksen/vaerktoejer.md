# Værktøjskassen

## 6. Værktøjskassen

| Script | Hvad det gør |
|---|---|
| `devuan/01_build_rootfs.sh` | bygger Devuan-rodfilsystemet fra bunden |
| `devuan/07_desktop_audio.sh` | skrivebord, lyd, bruger, tapet, overscan-opsætning |
| `devuan/extra_packages.sh` | pakkelisten: firefox, sudo, rsyslog, locale m.m. |
| `devuan/09_make_emmc_img.sh` | bygger imaget og sikrer alt det en boks ikke kan undvære |
| `devuan/testflash.sh` | bygger + flasher, med pause til loader-tilstand |
| `devuan/find_box.sh` | finder boksen på netværket (dens IP skifter hver boot) |
| `devuan/08_network_manager.sh` | NetworkManager på et **SD-kort** i læseren, og migrering af kendte wifi-netværk. eMMC-flowet får NM via `extra_packages.sh` + `09` |
| `devuan/emmc_first_boot.sh` | swapfil på 2 GB efter flash |
| `devuan/fb_overscan.py` | skrumper billedet, så fjernsynets beskæring ikke rammer noget |
| `devuan/patch_uboot_logo.py` | ændrer DT-flag i imaget (til eksperimenter) |
| `devuan/rollback.sh` | ruller DTB-flaget tilbage og flasher, hvis en boks ikke booter |

---
