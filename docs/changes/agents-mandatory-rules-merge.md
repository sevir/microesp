---
created_at: 2026-10-05T19:57:12.385682139Z
updated_at: 2026-10-05T19:57:12.385682139Z
tags:
    - change
    - agent-instructions
---
# Merge mandatory AI operating rules into AGENTS.md

## What changed
Added an explicitly labelled MANDATORY operating policy to the root agent guide. It covers context recovery through the KB and code index, KB graph navigation, external/API/browser research, planning non-trivial work, incremental implementation and verification, recording every change in the KB, correct separation of short memories from structured docs, English/code conduct, delegation, and confirmation before risky outward-facing actions. Integrated the KB recording requirement into the existing Knowledge base guidance.

## Files and symbols touched
- `AGENTS.md` — added the mandatory policy section and clarified the existing knowledge-base rule.
- `CLAUDE.md` was not changed; it delegates to `@AGENTS.md`.

Existing project architecture notes, build/test/flash commands, release conventions, DP/protocol consistency requirements, secrets guidance, and live-deployment safeguards remain in place.

## Why
Ensure agents have a clear, enforceable, project-specific checklist that combines the canonical operating requirements with MicroESP's established tools, conventions, and production safety boundaries, without replacing or duplicating the project guide.

## Verification
Re-read the complete final `AGENTS.md`; checked for all eight mandatory headings and all eight original project sections, plus a final newline. `git status --short` confirmed the working tree contains the intended untracked guide files. No runtime tests were run because this is a Markdown-only instruction change.

Builds on [[plans/agents-mandatory-rules-merge_plan.md]].