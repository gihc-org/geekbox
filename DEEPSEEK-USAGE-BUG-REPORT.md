Subject: DeepSeek Responses API reports inflated input/cached token usage (~1M+), causing clients to compact context prematurely

Summary
The DeepSeek Responses API (api.deepseek.com) occasionally returns `usage` with inflated
`input_tokens`/cached-token counts - often near or above the full 1M context - even when the actual
prompt is small (tens of thousands of tokens). Clients that use the API-reported usage to manage
their context window (e.g. OpenAI Codex) then wrongly conclude the window is full and trigger
context compaction, degrading accuracy and wasting tokens.

Environment
- Endpoint: https://api.deepseek.com/ (Responses API), model `deepseek-v4-flash`, 1M context.
- Client: OpenAI Codex CLI 0.149.x, DeepSeek configured as a custom provider (`wire_api = "responses"`),
  model catalog: `context_window = 1048576`, `effective_context_window_percent = 95`
  -> effective window 996,147 tokens.

Observed behavior
Normal requests show input tokens growing sanely with the conversation (~14.7K -> 35K -> 95K).
Then a single ordinary request (a small tool call) reports:

    "last_token_usage": {
      "input_tokens": 1148149,
      "cached_input_tokens": 1138432,
      "cache_write_input_tokens": 0,
      "output_tokens": 5183,
      "total_tokens": 1153332
    }

...while the actual conversation at that moment was ~75-95K tokens - the reported figure is ~12x the
real prompt. Codex uses `usage.total_tokens` from the last response as the active context size;
1,153,332 >= 996,147 triggered a forced compaction ("Context compacted") ~13 minutes into a fresh
session.

This recurred in every observed compaction (actual conversation size -> reported input tokens):
- ~94K  -> 1,148,149 (1,138,432 "cached")  -> compacted
- ~78K  ->   978,810 (967,552 "cached")    -> compacted
- ~158K -> 1,006,954                      -> compacted
- ~620K -> 3,744,022                      -> compacted
- ~384K -> 1,944,363                      -> compacted

The inflated "cached" figure is ~1M-3.7M, i.e. at or above the full context - as if the entire
context were counted as cache hits.

Expected behavior
`usage.input_tokens` should reflect the actual prompt sent; cache hits should be a separate,
accurate figure. Inflated numbers make it impossible for clients to manage context correctly.

Impact
Any client that bases context management on API-reported usage will compact prematurely, losing
accuracy and increasing effective cost (re-summarization, re-reading). Repeated compactions degrade
long sessions.

Suggested fix
Investigate the Responses-API usage accounting (and the Anthropic-compatible endpoint), in
particular `prompt_cache_hit_tokens`/cached-token inflation to ~1M+; ensure `input_tokens` and
`total_tokens` reflect the actual prompt. Happy to provide full request/response logs on request.
