# Token-forbrug i Kimi Code — hvorfor det koster, og hvordan man holder det nede

Praktisk guide skrevet efter GeekBox-projektet, hvor $20 Extra Usage forsvandt på
ca. et døgn i én lang, ubrudt session. Forklaringen her er kort og brugbar.

## Prismodellen (kort)

- Hver turn betaler for **input-tokens + output-tokens**
- Input-tokens = **hele samtalehistorikken** (system-prompt, alle tidligere beskeder,
  alle tool-resultater) sendes med hver eneste request
- Det betyder: turn 300 koster mange gange mere end turn 5 — *også selvom du bare
  skriver "ok, fortsæt"*. Prisen følger samtale-længde, ikke arbejdsoutput

Tjek forbruget live med **`/usage`** i CLI'en (eller Kimi Code Console på web).

## Hvad der koster mest

1. **Lange ubrudte sessioner** — historikken vokser, hver turn bliver dyrere
2. **Billeder** — hvert foto koster mange tokens at analysere. Brug dem når de er
   nødvendige (skærm-output man ikke kan copy-paste), ikke som standard
3. **Store tool-outputs** — log-dumps, `strace`, `dmesg` i hundredvis af linjer.
   En uventet 16 MB output-oversvømmelse er ren spild
4. **Lange subagent-kørsler** — baggrundsagenter tæller på samme kvote
5. **`kimi -c` / `--continue`** — fortsætter samme session med hele historikken.
   Praktisk, men det er netop det, der lader konteksten (og prisen) vokse uendeligt

## Strategier der virker

- **Ny session pr. ny opgave.** `/new` i TUI, eller start `kimi` uden `-c`.
  Del hukommelse via filerne i stedet: et repo med README/TODO/DOKUMENTATION kan en
  frisk session læse sig ind på med få billige turns
- **`/compact`** komprimerer historikken, når en lang session skal fortsætte
  (mister detaljer, men meget billigere)
- **Beskriv i tekst hvad du ser**, når det rækker — send kun foto når det er nødvendigt
- **Små outputs**: bed om resuméer i stedet for rå dumps, når du kan
- **Overvåg `/usage`** og sammenlign dage — så lærer du hurtigt hvad der driver prisen

## Kimi-kvoter (fra den officielle dokumentation)

- Ugentlig kvote der fornys hver 7. dag + et rullende 5-timers rate-window
- "Extra Usage" (det man topper op med penge) er pay-as-you-go, tæt på API-priser,
  og trækkes først når abonnements-kvoten er brugt
- Meget brug → overvej højere abonnement; det er typisk billigere end Extra Usage

## Udvikling i dette projekt

Én lang session

┌─────────┬──────────────────────────────────────────────┬────────────────────────────────────────────┐                                                                                                                                   
│ Periode │ Arbejde                                      │ Forbrugsdrivere                            │                                                                                                                                   
├─────────┼──────────────────────────────────────────────┼────────────────────────────────────────────┤                                                                                                                                   
│ 7-8/08  │ Flash Lubuntu, udpakning, parameter-research │ 2-timers søge-agent, mange foto-analyser   │                                                                                                                                   
├─────────┼──────────────────────────────────────────────┼────────────────────────────────────────────┤                                                                                                                                   
│ 13/08   │ Devuan-debug: myinit, dropbear, netværksrace │ Lang session, store log-dumps, mange turns │                                                                                                                                   
├─────────┼──────────────────────────────────────────────┼────────────────────────────────────────────┤                                                                                                                                   
│ 14/08   │ WiFi, X/LXDE, lyd, swap, eMMC                │ Billeder af skærme, strace/dmesg-output    │                                                                                                                                   
├─────────┼──────────────────────────────────────────────┼────────────────────────────────────────────┤                                                                                                                                   
│ 15/08   │ Boot-optimering, dokumentation               │ Kortere turns — billigere                  │                                                                                                                                   
└─────────┴──────────────────────────────────────────────┴────────────────────────────────────────────┘

╭ Usage ─────────────────────────────────────────────────────────╮
│ Session usage                                                  │
│   moonshot-ai/kimi-k3  input 176.7M  output 365k  total 177.1M │
│                                                                │
│ Context window                                                 │
│   █████████░░░░░░░░░░░     61%  (618k / 1M)                    │
╰────────────────────────────────────────────────────────────────╯

Kilder: [Membership Benefits](https://www.kimi.com/code/docs/en/kimi-code/membership.html) og
[Sessions and context](https://www.kimi.com/code/docs/en/kimi-code-cli/guides/sessions.html)

## Vores faktiske forbrug, målt (GeekBox-sessionen aug 2026)

Udregnet fra sessionens egen event-stream
(`~/.kimi-code/sessions/.../wire.jsonl`, hændelser af typen `usage.record` — præcise
token-tal pr. request, ingen estimater):

**Kontekst-vækst over tid** (input pr. request ≈ kontekst-størrelse):

```
turn 1   (07/08):  ~21.000 tokens
turn 141 (08/08): ~214.000
turn 281 (14/08): ~393.000
turn 456 (14/08): ~560.000
turn 526 (15/08): ~632.000   ← 30× større end turn 1
```

**Forbrug pr. dag** (faser fra git-log):

| Dato | Fase | Requests | Tokens |
|---|---|---|---|
| 07/08 | Flash + research | 51 | 3,2 M |
| 08/08 | Devuan-start, parameter-CRC | 92 | 14,4 M |
| 09/08 | dmesg-fotos, fb-analyse | 33 | 8,1 M |
| 13/08 | myinit-debug, dropbear, netværk | 67 | 22,1 M |
| 14/08 | WiFi, X, lyd, swap, eMMC, docs | 237 | 112,4 M |
| 15/08 | Boot-optimering, dokumentation | 44 | 26,9 M |
| **Total** | | **530** | **~189 M** |

94% af tokens var cache-reads (billigste kategori), 10,5 M uncached input, 0,4 M output.

**Læren i tal:** 14/08 alene stod for 60% af forbruget — 237 requests med ~½ million
tokens kontekst hver. Havde hver dag været en frisk session (kontekst ~20-50k), havde
den dag kostet omtrent en tiendedel. Samme arbejde, samme resultat — det er prisen på
aldrig at starte en ny session.

**Gør-det-selv:** `./analyze_kimi_usage.py` (i dette repo) analyserer enhver session's
`wire.jsonl` — uden argumenter tager den den senest ændrede session automatisk.
