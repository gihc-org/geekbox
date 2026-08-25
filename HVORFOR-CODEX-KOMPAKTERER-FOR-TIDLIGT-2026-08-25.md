# Codex og DeepSeek – hvad vi fandt ud af i dag

*En grundig forklaring af sessionen den 25. august 2026.*

## Kort om hvad vi arbejder med

- **Codex** er en AI-kodeassistent. Det er et program, der selv kan læse kode,
  skrive kode og køre kommandoer i terminalen, når det får en opgave.
- **DeepSeek** er en AI-model (en "hjerne") fra det kinesiske firma DeepSeek. Vi
  bruger den som den hjerne, Codex tænker med. Vi lejer adgang til den over
  internettet og betaler per **token**.
- En **token** er den mindste enhed af tekst, modellen arbejder med. En token
  svarer nogenlunde til 3/4 af et ord. Man kan tænke på tokens som "brændstof":
  jo flere tokens en opgave bruger, jo mere koster den.

Vi har sat DeepSeek op som "hjernen" bag Codex, fordi DeepSeek er billig og har
et stort hukommelsesvindue. Denne session handlede om at forstå, hvorfor Codex
nogle gange skrev **"Context compacted"** – og om vi skulle skifte værktøj.

---

## 1. Opsætningen – og det mystiske tal "1048576"

Vi installerede DeepSeek med DeepSeek's eget setup-script. Scriptet lavede to
filer i Codex' konfigurationsmappe:

- `~/.codex/config.toml` – indstillingerne (hvilken model, hvilken udbyder, nøgle).
- `~/.codex/models.json` – metadata om modellen: bl.a. hvor stort
  hukommelsesvinduet er, hvilke "tænke-niveauer" modellen understøtter, og
  hvordan værktøjer skal kaldes.

I `models.json` stod der:

```json
"context_window": 1048576,
"max_context_window": 1048576,
"effective_context_window_percent": 95
```

Det første spørgsmål var: *"1048576 ser lille ud – har flash ikke 1M?"*

Svaret er nej – **1048576 er præcis 1M**. Computere regner i "binære" enheder:
2²⁰ = 1.048.576. Det er "computerens million", præcis som 1024 MB er 1 GB.
DeepSeek's egne dokumenter bruger selv `1048576` som kontekstvindue for V4.
En kammerat sendte en fil, hvor der stod `1000000` (den almindelige million).
Begge tal betyder i praksis det samme: omkring 1 million tokens. Der var altså
**intet at rette** – opsætningen var allerede korrekt.

`effective_context_window_percent: 95` betyder, at Codex regner med, at 95 % af
vinduet er brugbart til samtalen (de sidste 5 % er reserveret til systemtekster,
værktøjsbeskrivelser og modellens svar). Det effektive vindue bliver:

`1.048.576 × 95 % = 996.147 tokens`

---

## 2. Problemet: "Context compacted"

Når Codex arbejder, sender det hele samtalen med til modellen hver gang. Modellen
har et begrænset vindue – tænk på det som et skrivebord, der kun kan rumme et
bestemt antal papirer. Når bordet nærmer sig fuldt, sker der en **komprimering**:

1. Codex beder modellen skrive en kort sammenfatning af den ældre del af samtalen.
2. Den gamle del (værktøjsoutput, filindhold, tankeprocesser) fjernes.
3. Samtalen fortsætter på sammenfatningen.

Det er en nødvendig funktion, men den er **tabsgivende**: detaljer forsvinder.
Ligesom når man skal fortælle en hel fodboldkamp på tre sætninger – så glemmer
man let, hvem der scorede det andet mål. Efter flere komprimeringer kan modellen
blive upræcis, gentage arbejde eller "glemme" ting, den selv har fundet ud af.

Brugeren oplevede, at komprimering nogle gange kom efter lang tid (det er
forventeligt), men **i én session allerede efter ~15 minutter**. Det var mistænkeligt
– samtalen var der jo næsten ikke blevet tid til at blive stor.

---

## 3. Undersøgelsen – og rodårsagen

Vi gravede i sessionernes logfiler (Codex gemmer alle samtaler og token-tællinger
lokalt). Mønsteret blev tydeligt:

- Normalt voksede input-tokens fornuftigt: 14.678 → 35.071 → 94.976.
- Men ved 14:53:05 rapporterede DeepSeek's API pludselig **1.148.149 input-tokens
  for én enkelt request** – heraf 1.138.432 "cached". Samtalen var reelt kun ~94K
  tokens. Tallet var altså ~12 gange for stort.

Det samme mønster gik igen i **alle** observerede kompakteringer:

| Tråd | Reel samtale | Rapporteret input |
|---|---|---|
| 01a038ef (1. komprimering) | ~94K | 1.148.149 |
| 01a038ef (2. komprimering) | ~78K | 978.810 |
| 01a033a3 | ~158K | 1.006.954 |
| 01a033e6 | ~620K | 3.744.022 |
| 01a03034 | ~384K | 1.944.363 |

**Hvorfor det udløser komprimering:** Codex' beslutning om at kompaktere læser
`usage`-tallet fra API'ets seneste svar (`last_token_usage.total_tokens`) og
sammenligner med det effektive vindue (996.147). Når tallet er ≥ vinduet, tvinges
en komprimering. DeepSeek's API sender altså indimellem et stærkt oppustet tal –
ofte tæt på eller over hele 1M-konteksten (som om hele konteksten var "cached") –
og Codex stoler på det.

**Konklusion:** Det er ikke vores opsætning, der er gal. Det er DeepSeek's
usage-rapportering (især cache-tællingen), der "lyver" – som et speedometer, der
viser 1.000 km, selvom man kun har kørt 5 km. Og der er ingen indstilling i Codex,
der kan omgå det, fordi det er et hårdt "vinduet er fuldt"-check.

---

## 4. Alternativet: Claude Code

Der findes en anden kodeassistent, **Claude Code** (fra firmaet Anthropic). Den
har én vigtig forskel:

- Codex stoler på API'ets usage-tal, når den beslutter at kompaktere.
- Claude Code bruger **sin egen lokale token-tælling** til auto-compact – ikke
  API'ets tal.

Derfor ville DeepSeek's oppustede tal **ikke** udløse for tidlig kompaktering i
Claude Code. DeepSeek understøtter desuden Claude Code officielt via et
Anthropic-kompatibelt endepunkt (`https://api.deepseek.com/anthropic`) og anbefaler
selv `CLAUDE_CODE_AUTO_COMPACT_WINDOW=786432` (= 75 % af 1M).

Vi konfigurerede Claude Code med DeepSeek (`~/.claude/settings.json`, med backup)
og verificerede, at det virkede.

---

## 5. A/B-testen – målinger i stedet for gæt

Vi gav begge værktøjer **den samme opgave**, med **den samme model**
(`deepseek-v4-flash`), i det samme projekt, og målte token-forbruget. Vi regnede
de reelle priser med DeepSeek's off-peak-takster:

- Input, cache-miss: $0,22 pr. million tokens
- Input, cache-hit: $0,007 pr. million tokens
- Output: $0,66 pr. million tokens

**Opgave 1 – læs README og opsummer:**

| | Prompt-tokens | heraf cache-hit | Output-tokens | Reel pris |
|---|---|---|---|---|
| Claude Code | 82.451 | 53.504 | 1.499 | $0,0077 |
| Codex | 26.141 | 19.328 | 493 | $0,0020 |

**Opgave 2 – rigtig opgave: handover-notat + DOKUMENTATION §5.13–5.15:**

| | Prompt-tokens | heraf cache-hit | Output-tokens | Reel pris |
|---|---|---|---|---|
| Claude Code | 199.105 | 159.104 | 2.700 | $0,0117 |
| Codex | 48.019 | 38.528 | 2.257 | $0,0039 |

**Resultat:** Claude Code brugte ~4× så mange prompt-tokens og kostede ~3× så
meget på fil-tunge opgaver. Begge svarede korrekt.

**Vigtig detalje:** Claude Code's egen prisvisning viste $0,216 for opgave 2 – men
den reelle pris var ~$0,0117. Claude Code bruger Anthropic-priser som fallback
for ukendte modeller og er derfor **ikke til at stole på som pris-måler** med
DeepSeek.

---

## 6. Beslutningen

Vi blev ved **Codex**:

- Lavest token-forbrug og lavest omkostning (målt, ikke gættet).
- Opsætningen er allerede på plads.
- Den for tidlige komprimering er en DeepSeek-side-fejl – ikke en Codex-fejl.

Claude Code er stadig konfigureret som et alternativ, hvis vi en dag vil prøve det
igen.

---

## 7. Modforanstaltninger mod "context-rot"

"Context-rot" er det, der sker, når en tråd bliver så lang og komprimeret så mange
gange, at modellen mister detaljer og begynder at gentage sig selv. Vores regler:

- **Ny tråd ved opgavegrænser** – ikke efter tid, men når opgaven skifter.
- **Checkpoint + handoff-notat** ved vigtige milepæle eller efter 2 komprimeringer:
  få Codex til at skrive en kort statusnote (hvad vi ved, hvad der er forsøgt,
  nuværende hypotese, næste skridt, kommandoer der virker).
- **Ny tråd ved gentagelse** – hvis Codex spørger om ting, den selv har fundet ud
  af, eller genkører kommandoer, er det tegn på rot.
- **Vigtige fakta i repo-filer** (dokumenter/notater), ikke kun i samtalen – så de
  overlever både komprimering og nye sessioner.

Reglerne er skrevet ind i projektets `AGENTS.md` (en slags huskeliste, som Codex
læser ved hver sessionstart) og committet som `6144aa4`.

---

## 8. Fejlrapporten til DeepSeek

Vi skrev en engelsk fejlrapport med tallene og sendte den til DeepSeek's
API-support: **`api-service@deepseek.com`**.

Rapporten beskriver:
- at DeepSeek's Responses API indimellem rapporterer stærkt oppustede
  input/cache-tokens (ofte ~1M, op til 3,7M), selvom prompten er lille,
- at Codex bruger det tal til at beslutte kompaktering – og derfor kompakterer
  for tidligt,
- de konkrete observationer (tabellen ovenfor).

Andre har rapporteret beslægtede problemer: NVIDIA's forum ("extremely inflated
token counts"), OmniRoute #9536, latitude-llm #2558 og earendil/pi #3880.

---

## 9. Hvad vi endte med

1. Vores opsætning var korrekt – `1048576` er 1M.
2. Den for tidlige komprimering skyldes DeepSeek's usage-rapportering, ikke os.
3. Codex er det mest økonomiske værktøj til DeepSeek (målt).
4. Vi har lavet huskeregler, så Codex bedre håndterer lange sessioner.
5. Fejlen er rapporteret til DeepSeek, så de kan rette deres "speedometer".

---

## 10. Nøgletal, filer og links

**Nøgletal**

- 1M kontekst = 1.048.576 tokens (2²⁰); effektivt vindue 996.147 (95 %).
- DeepSeek V4 flash (off-peak): $0,22/M input (miss), $0,007/M (hit), $0,66/M output.
- Codex kompakterer, når API-rapporteret usage ≥ effektivt vindue.
- A/B-måling: Claude Code ≈ 3–4× Codex's token-forbrug på fil-tunge opgaver.

**Filer**

- `~/.codex/config.toml` og `~/.codex/models.json` – DeepSeek-opsætning for Codex.
- `~/.claude/settings.json` (+ backup `settings.json.bak-deepseek`) – DeepSeek-opsætning for Claude Code.
- `AGENTS.md` (geekbox) – checkpoint- og tråd-disciplin (commit `6144aa4`).
- `/tmp/deepseek_usage_bug_report.md`, `/tmp/codex_compaction_issue.md` – fejlrapporter.

**Links**

- DeepSeek-priser: https://api-docs.deepseek.com/quick_start/pricing/
- DeepSeek + Claude Code: https://api-docs.deepseek.com/quick_start/agent_integrations/claude_code/
- Relaterede rapporter: NVIDIA-forum, OmniRoute #9536, latitude-llm #2558, earendil/pi #3880.
