# Model evaluations

Run the NorvesLib model-eval fixtures with the shared evaluator:

```powershell
node ~/.agent-workflow/run-evals.mjs Docs/agent-guide/evals/manifest.json --engine codex --only m4-profile-line-number --label m4-profile-line-number
```

Each fixture is self-contained. Do not point an eval check at the live repository scripts.
