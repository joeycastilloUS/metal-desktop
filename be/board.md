# metal Desktop — Board

Source: Migrated from nous-desktop. Boards 1-5 archived. Boards 6-7 absorbed into metal Boards 72-73.

---

## Board 6 — Roll back FQDN, restore TOTP-only (17 pts) 🗄 SHELVED 2026-05-11

Source: be/builds/2026-05-11-2255-inspect-fqdn-rollback.html → be/plans/2026-05-11-2257-plan-fqdn-rollback-totp-only.html → be/plans/2026-05-11-2257-estimate-fqdn-rollback-totp-only.html

Surgical strip of FQDN was right-direction but **wrong-scoped** -- the inspect missed that FQDN lives primarily in `prime_server.c` (6,834-line third binary), not just relay. Even with full strip + Windows builds, no auth works without the prime running somewhere. **Shelved in favor of auth-gate bypass** (Board 7) -- 1-line desktop.js patch unblocks Joey for free-AI playing today; deferred relay/prime strip until store/loop features are actually wanted.

### Backlog

### Ready

### In Progress

### Done

(none -- board shelved before pulling any items)

---

## Board 7 — Auth-gate bypass (1 pt)

Source: be/builds/2026-05-11-2255-inspect-fqdn-rollback.html (the same inspect; Board 6 lessons applied)

Quick path: short-circuit the desktop's auth gate so the cockpit is visible immediately. Free-AI provider fan-out works (direct HTTPS, no relay/prime needed). Anything that touches relay/prime (register, login, store, intelligence loop) silently fails. Old auth logic preserved as `initAuthGate_OFF()` for one-line revert when prime gets stood up.

### Backlog

### Ready

### In Progress

### Done
- {auth gate, bypass in, metal-desktop/wwwroot/desktop.js} · 1 · ~f ✅

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
