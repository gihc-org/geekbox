# DeepSeek support – followup-notat

*Klar til når DeepSeek svarer på vores fejlrapport (sendt 25. august 2026 til
`api-service@deepseek.com`).*

## Hvad vi rapporterede

- DeepSeek's Responses API rapporterer indimellem stærkt oppustede
  input/cache-tokens (ofte ~1M, op til 3,7M), selvom prompten er lille.
- Codex bruger `usage.total_tokens` fra sidste svar til at beslutte, hvornår der
  skal kompakteres → derfor kompakterer Codex for tidligt ("Context compacted").
- Bevis: 5 kompakteringer, hvor de reelle samtaler var ~78–620K tokens, men de
  rapporterede tal var 978K–3,7M.

## Nøgletal (hurtig reference)

- Effektivt vindue: 996.147 tokens (1.048.576 × 95 %).
- Eksempel: samtale ~94K tokens → rapporteret 1.148.149 input (heraf 1.138.432
  "cached") → komprimering udløst.
- Fuld forklaring med alle tal: `HVORFOR-CODEX-KOMPAKTERER-FOR-TIDLIGT-2026-08-25.md`

## Hvis DeepSeek spørger om mere

- Fulde request/response-logfiler ligger i `~/.codex/sessions/2026/08/25/`.
- Relevante tråde: `01a038ef` (2 komprimeringer), `01a033a3`, `01a033e6`,
  `01a03034`.
- Codex-kildekode: `codex-rs/core/src/session/context_window.rs` og
  `codex-rs/core/src/context_manager/history.rs` (beslutningen læser
  `last_token_usage.total_tokens`).

## DeepSeeks svar

*(indsæt deres svar her)*

## Næste skridt

- [ ] Notér eventuelle spørgsmål fra DeepSeek og svar med tallene ovenfor.
- [ ] Tjek om fejlen er rettet: kør en session og hold øje med for tidlig
      "Context compacted".
- [ ] Opdater dette notat og forklaringsdokumentet med udfaldet.
