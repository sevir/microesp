---
created_at: 2026-10-05T19:56:08.574723294Z
updated_at: 2026-10-05T19:57:17.601153199Z
tags:
    - plan
    - agent-instructions
---
# Plan: Merge mandatory AI operating rules into AGENTS.md

## Goal
Strengthen the root `AGENTS.md` with a clearly labelled, project-specific mandatory operating policy while preserving the existing MicroESP architecture, build/test instructions, conventions, safety constraints, and documentation guidance.

## Phases
1. **Recover and assess context** — Search the KB broadly, inspect the current root `AGENTS.md` and delegated instruction files, then compare every required clause against the existing guidance.
   - Verification: identified existing coverage, weaker wording, and missing requirements before editing.
   - Status: complete.
2. **Merge the policy** — Edit only root `AGENTS.md`, integrating clearly labelled mandatory clauses into the existing structure and adapting tool references and safety rules to this repository.
   - Verification: inspected the resulting Markdown and confirmed no project-specific sections were dropped.
   - Status: complete.
3. **Validate and document** — Re-read the complete final file, check each required clause is present and explicitly marked mandatory, inspect the working-tree diff/status, and record a change summary in the KB.
   - Verification: all eight required policy headings and all eight original project sections present, final newline checked, and `git status --short` inspected; no runtime test needed for Markdown-only instructions.
   - Status: complete.

## Scope
- Target: `/www/MicroESP/microesp/AGENTS.md`.
- `CLAUDE.md` delegates directly to `@AGENTS.md`; no separate policy content is to be altered.
- Preserved existing project instructions, especially the live deployment confirmation requirements and secret-handling guidance.