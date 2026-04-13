# metal Desktop — Board

Source: Migrated from nous-desktop. Boards 1-5 archived. Boards 6-7 absorbed into metal Boards 72-73.

---

# Archive — Completed Boards (from nous-desktop)

## Board 1 — Free Keys Foundation (18 pts) ✅

### Done
- {nous_keys_builtin.h + .h.example + .gitignore, add to, nous-desktop/src} · 3 · ~a ✅
- {nous_userkeys.c/.h — per-user key store (load/get/set/free), add to, nous-desktop/src} · 5 · ~a ✅
- {resolve_key() with 4-layer resolution, add to, nous-desktop/src/serve.c} · 5 · ~a ✅
- {load_keys_bat() + g_api_keys/g_api_vals, delete from, nous-desktop/src/serve.c} · 5 · ~d ✅

---

## Board 2 — Wire + Test (16 pts) ✅

### Done
- {provider table with builtin_key field, update in, nous-desktop/src/serve.c} · 5 · ~u ✅
- {resolve_key() with 4-layer resolution, add to, nous/intelligence/receive.c} · 5 · ~a ✅
- {bare getenv() provider blocks, update in, nous/intelligence/receive.c} · 3 · ~u ✅
- {free + paid key resolution, test in, nous-desktop} · 3 · ~t ✅

---

## Board 3 — Provider Update (16 pts) ✅

### Done
- {Gemini preview entries + Claude 4.6 + pricing, update in, serve.c} · 5 · ~u ✅
- {GPT 5.4 + nano + Grok 4.20 + pricing, update in, serve.c} · 5 · ~u ✅
- {DeepSeek pricing + perplexity-deep + magistral, update in, serve.c} · 3 · ~u ✅
- {compile + smoke test all providers, test in, nous-desktop} · 3 · ~t ✅

---

## Board 4 — Provider Sync (11 pts) ✅

### Done
- {Gemini stable + GPT-mini fix + nano + pricing, update in, nous/intelligence/receive.c} · 5 · ~u ✅
- {Grok 4.20 + perplexity-deep + magistral, update in, nous/intelligence/receive.c} · 3 · ~u ✅
- {provider parity audit between repos, test in, nous-desktop + nous} · 3 · ~t ✅

---

## Board 5 — Foundation: Prime Auth Core (13 pts) ✅

### Done
- {auth lockdown — last_auth on success + register guard, add to, catalog_server.c} · 5 · ~a ✅
- {FQDN user ID validation — allow dots in username, update in, catalog_server.c} · 3 · ~u ✅
- {HTTP client for domain verification, add to, catalog_server.c} · 5 · ~a ✅
