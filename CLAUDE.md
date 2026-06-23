# IPC / VCU Dashboard — Claude Code Session Protocol

## Session start (MANDATORY — runs automatically on every session)

At the start of every session, Claude must do these steps in order before doing anything else:

1. Read `.ai-workspace/Main.md` — project map, CAN protocol, file tree, constraints
2. Read `.ai-workspace/Memory.md` — previous session log and quick state summary
3. Read `.ai-workspace/Task.md` — check for any active task

Then confirm to the user:
> "Workspace loaded. IPC/VCU Dashboard — [one-line summary from Memory.md] — [active task or 'no active task']"

Do NOT ask the user to re-explain the project.
Do NOT re-read any raw files in docs/ — use Main.md summaries only.

---

## Pipeline rules

All work follows the staged AI pipeline defined in `.ai-workspace/Rule.md`.
Read Rule.md if you need the full stage definitions, complexity scoring, or model switch prompts.

Key rules at a glance:
- Every task goes through Stage 0 (clarify) → Stage 1 (read) → Stage 2 (score) → Stage 4 (Task.md) → Stage 5 (approve) → Stage 6 (execute) → Stage 7 (review + Memory.md)
- Hard stop after each stage — print completion prompt and wait for "ready"
- CAN frame/ID changes = 50+ complexity minimum
- Cross-ECU changes = 50+ complexity minimum
- QML ↔ C++ interface changes = 40+ complexity minimum
- Never auto-push to GitHub — only push when user explicitly says so

---

## Files Claude may NOT modify

- `docs/` — read only
- `.ai-workspace/Rule.md` — user managed only
- `image/` — never touch (Pi flash images, STM32 drivers)
