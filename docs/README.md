# GeekBox-dokumentationen

# GeekBox-håndbogen — alt vi har lært, forklaret fra grunden

Denne fil er skrevet så den kan læses uden at kende projektet i forvejen. Den forklarer
hvad boksen er, hvordan man laver en ny, og — vigtigst — hver enkelt fælde vi er faldet i,
så du ikke skal falde i den igen. Skrevet august 2026, efter at syv bokse er blevet
flashet og fejlsøgt.

**Kort om projektet:** en GeekBox er en lille TV-boks fra 2015 med en Rockchip RK3368-chip.
Den blev solgt med Android og senere Lubuntu. Vi har sat et moderne Devuan Linux på den,
men beholdt producentens gamle Linux-kerne (version 3.10 fra 2013), fordi driverne til
grafik, lyd og netværk kun findes til den. Resultatet er en lille skrivebordscomputer der
kan browse, se YouTube og bruges som almindelig maskine.

---

Indgangen til dokumentationen. Den er delt efter formål, så du kan gå
direkte til det du skal bruge:

| Du vil … | Læs |
|---|---|
| forstå ordene der bruges | `ordbog.md` |
| stå med et problem og få det løst | `faeller.md` |
| lave en ny boks fra bunden | `boksen/flash.md` |
| vide hvad en GeekBox er | `boksen/hardware.md` |
| forstå systemet på boksen | `styresystemet/devuan.md` |
| forstå grafikken og hvad der drillede | `grafik/hvorfor.md`, `grafik/gpu-historien.md` |
| se hvad der skete hvornår | `log/` |

**§-numrene er bevaret** fra de to store dokumenter, så gamle henvisninger
stadig kan følges: `§1`–`§3` er `boksen/hardware.md`, `§4` og `§9`–`§10` er
`boksen/flash.md`, `§5.1`–`§5.12` og `§6`–`§8` er `styresystemet/devuan.md`,
og `§5.13`–`§5.15f` er `grafik/gpu-historien.md`. Fælderne fra håndbogen er
`faeller.md`.

## 8. Hvis du vil vide mere

- `styresystemet/devuan.md` — hvordan systemet blev bygget, inklusive boot-kæden.
- `faeller.md` — hver fælde, med symptom, årsag og fix.
- `docs/boksen/skaerm.md` — hele fejlsøgningen af den sorte skærm, med beviskæden og de
  kildehenvisninger der hører til.
- `docs/grafik/driver-portering.md` — hvorfor vi ikke bare kan bruge en moderne Linux-kerne.
- `docs/grafik/hvorfor.md` — grafikhistorien i hverdagssprog (god at læse selv eller højt).
- `TODO.md` — hvad der mangler.
- `git log` — hver commit forklarer hvad der blev rettet og hvorfor.
