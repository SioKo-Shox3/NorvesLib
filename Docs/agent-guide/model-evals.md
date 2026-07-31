# Model evaluations

Run the NorvesLib model-eval fixtures with the shared evaluator:

```powershell
node ~/.agent-workflow/run-evals.mjs Docs/agent-guide/evals/manifest.json --engine codex --only m4-profile-line-number --label m4-profile-line-number
node ~/.agent-workflow/run-evals.mjs Docs/agent-guide/evals/manifest.json --engine codex --only m5-particle-emitter-identity --label m5-particle-emitter-identity
node ~/.agent-workflow/run-evals.mjs Docs/agent-guide/evals/manifest.json --engine codex --only m6-hot-reload-movement-oracle --label m6-hot-reload-movement-oracle
node ~/.agent-workflow/run-evals.mjs Docs/agent-guide/evals/manifest.json --engine codex --only m7-rational-fixed-timestep --label m7-rational-fixed-timestep
cmd.exe /d /s /c "set NORVESLIB_ALLOW_DIRECT_EDIT=1&& node %USERPROFILE%\.agent-workflow\run-evals.mjs Docs/agent-guide/evals/manifest.json --engine codex --only m8-minimal-physics --label m8-minimal-physics"
```

各 fixture は自己完結です。eval check から live repository の script を参照してはいけません。
