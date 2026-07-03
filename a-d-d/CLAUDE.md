<!-- CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved -->
# metal-desktop — Architecture

> Capability specs (fanout / judge / summarize / reconsider / providers / api)
> are canonical in `kastil-systems/a-d-d` under the **nous** intent. This file
> owns only the cockpit: the HTTP+WS server and how it relates to those skills.

## Intent

The operator's AI cockpit. One browser tab where a human asks a question of
many AI models at once and sees them ask, judge, summarize, and reconsider —
live, side by side, on `localhost:5050`.

## Purpose

Give the operator a direct, framework-free surface onto nous.api's four verbs.
No SaaS, no account, no cloud round-trip for the UI: a pure-C server on the
local machine serving vanilla HTML/CSS/JS, talking to AI providers and (via the
relay/wire) to the nous engine.

## What

A pure-C HTTP + WebSocket server (`src/serve.c`, 12 sections) on
`localhost:5050`:

- **HTTP** serves the static cockpit (`wwwroot/`, vanilla, xterm.js vendored).
- **WebSocket** carries live verb traffic between the browser and the engine.
- **Provider fan-out** goes direct HTTPS to the AI providers; everything else
  (auth, store, intelligence loop) rides the relay over wire (NTRP0001).

## The Extraction Relationship

Today the four capabilities are **welded into `src/serve.c`**, not factored out:

- `route_ws_message` (`src/serve.c` ~line 3056) is the verb dispatcher. It
  routes the browser's message types straight into in-file handlers:
  - `"task"` → `handle_task` (~line 1985) — the parallel provider fan-out
  - `"judge"` → `handle_judge` (~line 2364) — LLM-as-judge ranking
  - `"summarize"` → `handle_summarize` (~line 2746) — comparative synthesis
  - reconsider — not yet wired (matches the upstream `serve.c` TODO).

These handlers carry the AI logic AND the WebSocket framing in one place. That
is the thing to undo.

### The plan

Extract the welded logic into the nous skills (the Location A specs in
`kastil-systems/a-d-d`):

```
serve.c handle_task       → fanout      (provider_thread engine, headless)
serve.c handle_judge      → judge       (judge_thread engine)
serve.c handle_summarize  → summarize   (handle_summarize synthesis)
serve.c (TODO)            → reconsider  (adversarial second pass)
serve.c g_providers table → providers   (the one authoritative model table)
```

Then `serve.c` keeps only what is genuinely cockpit: the HTTP server, the
WebSocket framing, and a thin `route_ws_message` that **calls the skills** —
or calls **nous.api** (`api_dispatch`) and lets it compose them. The cockpit
becomes a presentation layer over nous.api; the intelligence lives in the
skills, shared with every other consumer.

Direction is flow-up: the architecture (these skill specs) leads, then tests,
then the `serve.c` refactor lands against them — not the other way around.

## Scope — Out

Same out-of-scope list as nous.api. metal-desktop never exposes:

- The **BASICA dev-loop** (`/ship`, `/refactor`, `/tron`, `/troff`, `/try`).
- **`/search`** and **`/learn`** (power-search / web-acquire) — engine-only,
  the `acquire` skill's territory, never a cockpit verb.

Depth for `ask` comes from deep-research-capable models inside `fanout`, not a
scout pipeline behind the cockpit.

## Designs

- `designs/2026-06-13-1125-design-nous-api-stack.html` — the nous.api stack.
- `designs/2026-06-13-0338-schematic-nous-api-mesh.html` — the nous.api mesh
  schematic.

(Copied from `metal-desktop/be/designs/` so the architecture record carries its
own design artifacts.)
