# PROGRESS — NorvesLib

## Done

- R0: `052a7dc`。描画検証基盤。
- R1 P1: `c67dfde`。Camera露出、物理ライト単位、Scene v2。
- R1 P2: `98bd5c9`。表示リニア中間とsRGB一回出力。
- R1 P3: `053e73be13ab49454691105fd91acbe5144140ff`。物理ライトとEV100プリエクスポージャ。
- R1 P4: `f3d4abb20ad283d66f0a39b38b1b9aaa0ee92013`。放射輝度IBL、DFG、GGX補償と数値検証。focused build 13/13、CPU 7/7、GPU 6/6（exit 125なし）、独立レビュー、接続監査、fresh verifierを2026-09-08にPASS。

## In progress

- `R1-P5`を実装中。ループrun-id `20260908-084154`（PID 34936）で、透明描画へのライト/影/IBL接続と固定fixture・10行captureを追加している。実ログは `.harness/runs/20260908-084154/iter-1.err.txt`。
- 開始時のfocused8 buildは成功、CPU6は5/6（RenderGraphCompileTestのSSBO更新順序assertで失敗）。コア接続後のbuildは成功。CPU/GPUの最終受入は未完。
- 接続確認で、CreateLightingDescriptorSetのbinding5削除がLightingParamsLayoutTest:3097の既存P4契約に抵触する点を検出。初期bindingとSSBO更新順序の両立を確認してからP5を完了にする。
- 親セッションのgoalでP5→P6a→P6b完了を追跡する。プロセス生存だけで完了を扱わず、停止・失敗・未記録の検証を実ログから確認する。

## Next

- `R1-P5` → `R1-P6A` の順に実装・検証・コミットする。
- P6aが閉じたら、承認済みP6b計画に従って正式なIndoor/Outdoor候補を生成する。実物とcandidate hashへの人間の承認前にbaselineを公開しない。thresholdも60行の候補とhashへの承認後に公開する。
- R1全体の完了はP6bの最終検証とacceptanceまで保留する。

## Notes

- 2026-09-08開始時: `git log --oneline -10`を確認し、対象CPU7件を実行。`100% tests passed, 0 tests failed out of 7`、exit 0。
- P4保存コミットの実subjectは「作業途中の保存」。最終レビューsnapshotと11ファイルすべてbyte一致し、P3→P4のdirect diffはexact11、8212追加/608削除。保存済み履歴をそのまま使用する。
- P4 report: `.superpowers/sdd/RenderingR1PhysicalFoundationPlan/task-4-report.md`、SHA256=`78AFC2D4FD987F39BD582A7DF6DFD298AFFCCD396CCA8D140B194CE3BC86BC94`。旧計画がsubjectを固定値にしている箇所は、実commit hash・source11の直接比較・本受入証跡に読み替える。reportのSUBJECTも実subjectを記録する。
- 作業一覧と進捗だけのコミットを挟んでも描画基点を取り直さない。記録されたP4/P5の描画コミット、祖先関係、対象source同一性を確認する。技術仕様の数式・固定threshold・sample・source範囲は変更しない。
- P4検証の詳細: `Docs/Plans/SessionRecovery20260905.md`。最新の独立再実行ログは `%TEMP%/norveslib-p4-fresh-verifier-20260907T231637Z-026345e9/`。各JSONのexitとSHA、対応log本文を開いて確認する。
- P4のsource snapshot: `%TEMP%/norveslib-p4-review-r2-20260905T152810Z/manifest.json`。combined SHA=`10fa95181d028d6ef41bcaf310aeb4e671dabe1c55b4439968430a81dd09f69e`。
- P5 scope18およびP6a fixture変更は2026-08-15に承認済み。P6a v4 hash=`B805112EC87BAB673F7D0093FB6BB93DABBACCF5B4682D55992602869D40156F`、P6b v3 hash=`37D08DE402478F1D1EBEEEE2D0D8F134492AA0A0EDEB2A4C8FF69527721A2AE3`も承認済み。旧親ログのlines 6190、7574、7587、7593が根拠。
- 承認ログ: `C:/Users/KINGkawamura/.codex/sessions/2026/08/13/rollout-2026-08-13T09-18-12-019ff87b-d252-7260-94ae-580b5adfc698.jsonl`。8月16日の停止後、9月5日と9月8日に再開指示を受けている。
- 実装主体・モデル・評価頻度は現行AGENTS.mdを適用する。旧計画のメイン実装禁止や反復ごとの追加承認は再導入しない。数式や画像公開の承認を変更する意味ではない。
- 残課題: `LightingParamsLayoutTest`のEss lexical assertionは演算子まで反証しない。現shader式はレビュー済みで正しいためP4の非blocking事項として保持。品質返済はR1の次の自然な区切りでまとめる。

- 2026-09-08 09:33 JSTの読取照会: CTest登録219/GPU21/意図的skip7のname hashとsource-start6ファイルhashは計画に一致。固定name hashはPowerShell `Sort-Object -CaseSensitive` による。証拠: `%TEMP%/norveslib-r1-final-prerequisite-inventory-20260908.json`。正式P6b実行やfull build/full CTestは未実行。
