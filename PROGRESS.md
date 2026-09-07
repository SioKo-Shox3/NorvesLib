# PROGRESS — NorvesLib

## Done

- R0: `052a7dc`。描画検証基盤。
- R1 P1: `c67dfde`。Camera露出、物理ライト単位、Scene v2。
- R1 P2: `98bd5c9`。表示リニア中間とsRGB一回出力。
- R1 P3: `053e73be13ab49454691105fd91acbe5144140ff`。物理ライトとEV100プリエクスポージャ。
- R1 P4: `f3d4abb20ad283d66f0a39b38b1b9aaa0ee92013`。放射輝度IBL、DFG、GGX補償と数値検証。focused build 13/13、CPU 7/7、GPU 6/6（exit 125なし）、独立レビュー、接続監査、fresh verifierを2026-09-08にPASS。

## In progress

- 後続ループの初回タスクは `R1-P5`。P5の実装は未着手。

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
