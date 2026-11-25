## Code comments

Write none. A decision the code cannot show goes into an ADR, and a shortcut that needs an excuse becomes an issue (ADR-0041). Before changing code, search `docs/adr` for the symbols you touch: the reasons live there. `scripts/check_comments.ps1` enforces this per file against `scripts/comment_baseline.txt`. After deleting comments, run it with `-Update`, and never raise a count by hand.

## Agent skills

### Issue tracker

GitHub Issues via the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

Default five canonical triage roles. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context layout (`CONTEXT.md` and `docs/adr/` at the repo root). See `docs/agents/domain.md`.
