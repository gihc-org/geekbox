Læs `docs/log/2026-09-30-chromium-fps.md` (HELE notatet, især
"Status i ét blik", "Aftaler og beslutninger", "Gennembrud" og
"Bruger-rapport"), Chromium-afsnittet i `TODO.md` og `OVERBLIK.md`.
Branch er `chromium-fps`; seneste commits er `df63c19` (denne handover-prompt),
`bb0e07f` (software-compositing), `ae025db` (hardware-EGL-lappen) og
`7d2d17e` (første Chromium-spor).

STATUS 1. okt 2026: brugeren ser Subway Surfers-intrografikken, men spillet
er IKKE loadet/spilbart. Hardware-EGL-init og WebGL 2.0 virker med
EGL-proxyens `EGL_PROXY_CONFIG_FIX=1` + `EGL_PROXY_FAKE_VISUAL=0x21`;
software-compositing-varianten giver 0 `GPU process exited` og `fb_fps`
~10,4/s. Det er uvist om regnbuen er væk, om spillet blot står i intro/loading,
eller om `x11ws`-presenten stadig har format-/stride-fejl.

FØRSTE OPGAVE: verificér boksens aktuelle Chromium-proces/flags og skærm;
kør IKKE `bringup_after_power.sh` blindt, for den genskaber den gamle
vnext12-proxy og fjerner EGL-lappen. Tag derefter en ren kørsel uden
`--v=1`/trace, sammenlign et Chromium-screenshot med fb0, og afgør om fejlen
ligger i Chromium-GPU-rasteren, i `x11ws`-presenten eller i spillets
loading/netværk. Mål først fps når billedet er stabilt. Dokumentér i en ny
`docs/log/2026-10-01-chromium-fps.md`, og opdatér TODO/OVERBLIK samme time.
