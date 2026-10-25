# Domain documentation

This is a single-context graphics project. The user keeps the glossary and AI-use log as local ignored files in the code checkout. Research, verification artifacts, and detailed planning notes remain in the external workspace documentation.

## Before exploring domain behavior

Resolve these paths relative to the code repository root:

- `GLOSSARY.md`: the local ignored project glossary.
- `../docs/adr/`: workspace ADRs, when present.

Read the glossary before using domain terms in specs, tickets, or design work. Read any ADR relevant to the area being changed. Use the glossary's canonical vocabulary and surface conflicts with existing ADRs.

The ignored glossary and external ADRs are local context and are not guaranteed to exist in another clone. If absent, continue from the approved issue/spec and supplied project documents; create new domain documents only when a term or decision actually needs recording.

## Maintaining domain documents

Update the root glossary when a term is resolved. Use the root `AI.md` when maintaining the project's AI-use history. Both files, together with the root `AGENTS.md`, are intentionally ignored by Git for this solo project.

Create ADRs lazily in the external ADR directory for decisions that warrant them. Keep one authoritative glossary for this context.

Keep research, verification artifacts, and detailed development notes in the external workspace documentation. Keep repository setup guidance in this configuration directory. Retain applicable source/license notices with any adapted code that is distributed.
