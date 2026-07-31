これはevalフィクスチャの試験データであり、本リポジトリの規約ではない

このsubtreeを`run-evals`がコピーしたcandidate workspaceでは、単一source実装ワーカーとして
`PhysicsSceneQueryFacade.cpp`だけを直接編集する。

- `spawn_agent`、collab wait、phase宣言、check-scope、ask-advisor、commitを行わない。
- 上位の`AGENTS.md`や`Docs/agent-guide/`を探索せず、fable-reasoning/phase-gatesも使わない。
- `CMakeLists.txt`、`PhysicsSceneQueryFacadeTest.cpp`、`test.ps1`、`AGENTS.md`、`CLAUDE.md`は編集しない。
- 完了前に`test.ps1`を実行する。

これはrepo配下の規約継承問題に対する暫定措置であり、恒久対処はrepo外temp隔離方式への
`run-evals`改修である。
