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

## Board 8 — Put DE on the nous.api gateway (? pts) ⛔ blocked

Source: be/plans/2026-07-26-1337-inspect-de-vs-nous-api-gateway.html

`~i` verdict: metal DE does **not** use `api.3-nous.net` — 22-row direct-vendor
fan-out with keys on the operator's box (`src/serve.c:351-376`), while
`a-d-d/ARCHITECTURE.md:8` already declares DE "a **consumer** of nous.api."
Rule 12 drift; the spec is the correct side. metal CE already consumes the
gateway (`C:\metal\src\ask\nous-ask.c:22`).

**⛔ Blocked on two operator decisions — do not pull until both clear:**
1. `NOUS_API_TOKEN` is unset (User + Machine). Gateway returns 401 without it.
   Nothing here can be built, tested, or production-verified until it lands.
2. OQ2 — does DE call the gateway direct from C99 (CE precedent) or through
   the C# `NousGatewayClient` jacket? Changes the shape of every item below.

Sizing comes from `~e` after `~d`/`~p`. Triples below are the inspect's gap
list, not yet estimated.

### Backlog
- {22-row vendor table, update with, one api.3-nous.net gateway endpoint (src/serve.c:351-376)} · ? · ~u~t
- {model ids, update with, gateway ids from /v1/models + Model Garden enablement check} · ? · ~u~t
- {native anthropic + gemini wire formats, removed from, serve.c (collapse to openai format)} · ? · ~f~t
- {NOUS_API_URL + NOUS_API_TOKEN config surface, add to, src/config.c + desktop.conf.example} · ? · ~a~t
- {resolve_key 4-layer chain, update with, one Bearer token + optional BYOK passthrough} · ? · ~u~t
- {local fan-out offline bridge behind reachability check, add to, handle_task} · ? · ~a~t

### Ready

### In Progress

### Done

---

## Board K — Kaizen

Continuous-improvement items from `~i` / `~retro` / `~demo`. Never auto-pulled
by bare `~b`; opt-in via `~b kaizen`.

### Ready
- {stale "nous.api is spec-only" verdict, fix in, be/plans/2026-07-11-0918-inspect-nous-api.html} · ? · ~f · source:2026-07-26-1337-inspect-de-vs-nous-api-gateway.html · gateway is deployed + serving /v1/models 200 as of 2026-07-26
- {stale "relay.3-nous.net:8080 is dead" verdict, fix in, be/builds/2026-05-11-2255-inspect-fqdn-rollback.html} · ? · ~f · source:2026-07-26-1337-inspect-de-vs-nous-api-gateway.html · TCP 8080 returned OPEN 2026-07-26; original HTTP 000 was a curl at a wire port (Rule 14). Board 6 shelve rationale partly rests on this
- {Board 6 shelve rationale, update with, corrected relay liveness evidence} · ? · ~u · source:2026-07-26-1337-inspect-de-vs-nous-api-gateway.html · known-issue
- {"38 models" claim, update with, actual 22-row count} · ? · ~u · source:2026-07-26-1337-inspect-de-vs-nous-api-gateway.html · be/designs/2026-06-13-0338-schematic-nous-api-mesh.html:86 overstates current reality

### In Progress

### Done

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
