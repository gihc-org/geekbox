Title: Auto-compaction triggers prematurely when a provider reports inflated API usage

Summary
Codex's auto-compaction decision reads the API-reported usage from the last response
(`last_token_usage.total_tokens` in `codex-rs/core/src/context_manager/history.rs` via
`get_total_token_usage`, consumed by `session/context_window.rs`). With DeepSeek's Responses API, a
single request can report ~1.1M input tokens (mostly "cached") while the real conversation is only
~95K tokens. That trips `full_context_window_limit_reached` (effective window 996,147) and forces
"Context compacted" minutes into a fresh session.

Observed
- Requests normally grow sanely (~15K -> 95K input). Then one request reports
  `input_tokens: 1,148,149` (1,138,432 cached) for a conversation of ~95K tokens -> immediate
  forced compaction.
- Recurred across several threads; spikes of 978K-3.7M input tokens observed; compaction fires
  whenever the reported number >= the model's effective window.

Suggestion
Sanity-clamp or validate API-reported usage against a local token estimate (e.g. ignore or
lower-bound reports that exceed the context window by a large factor), or add an option to use
local token counting for custom providers. A misreporting provider should not be able to force
compaction.
