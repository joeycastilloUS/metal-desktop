# metal Desktop Edition

## What This Is

metal Desktop is a standalone browser-based AI orchestration cockpit. Pure C
HTTP + WebSocket server on localhost:5050. Rebranded from NOUS Desktop and
extracted from the NOUS monorepo (kastil-systems/nous) as part of the metal
product family.

## Architecture

- **src/serve.c** — The server. 12 sections: globals, provider loading,
  WebSocket framing, HTTP server, WS helpers, relay auth/register, store
  handler, AI provider fan-out, judge/summarize, hints, message router,
  event loop.
- **src/serve.h** — Public API: serve_init, serve_run, serve_stop, ws_send, ws_broadcast.
- **sdk/** — Vendored NOUS libraries: crypt, wire, http, pack.
- **wwwroot/** — Browser UI. Vanilla HTML/CSS/JS. xterm.js vendored.

## Backend

All backend communication goes through the relay via wire protocol (NTRP0001).
One channel, one key (AES-256-GCM), one door.

- Auth: TOTP via relay (FQDN identity — domain ownership = proof)
- Store queries: wire request through relay
- Intelligence loop: wire request through relay
- Provider fan-out: direct HTTPS to AI providers (Claude, GPT, Gemini, etc.)

## Build

```
make
```

Windows: links ws2_32, winhttp, bcrypt.
Linux: links pthread.

## Conventions

- Pure C. Zero frameworks. Zero dependencies beyond OS APIs.
- Trade secret notice on every source file.
- This repo follows Be (Beryllium) workflow conventions.
- Part of the metal product family: metal console (CLI) + metal desktop (cockpit).
