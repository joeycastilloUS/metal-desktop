<!-- CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved -->
# metal-desktop — Architecture

metal-desktop is the operator's AI cockpit: a pure-C HTTP + WebSocket server on
`localhost:5050` that puts the four nous capabilities — ask, judge, summarize,
reconsider — in front of a human in a browser.

It is a **consumer** of nous.api, not the home of its logic. The capability
specs are canonical elsewhere:

- The nous.api capability skills — **fanout**, **judge**, **summarize**,
  **reconsider**, **providers**, and the **api** orchestrator — live in
  `kastil-systems/a-d-d` under the **nous** intent
  (`nous/intelligence/skill/...` and `nous/memory/skill/providers/`).
  Those are the source of truth for what each capability is.

This `a-d-d/` folder is a one-line pointer per the kernel's Easy-button
placement: metal-desktop consumes the kASTIL/nous substrate, so its capability
architecture lives centrally in kastil-systems/a-d-d. Read the nous skill specs
there first; what stays here (`CLAUDE.md`) is only the cockpit-specific
architecture — the HTTP+WS server and the extraction relationship.
