# PROGRESS — NorvesLib

## Done

- R0: `052a7dc`。描画検証基盤。
- R1 P1: `c67dfde`。Camera露出、物理ライト単位、Scene v2。
- R1 P2: `98bd5c9`。表示リニア中間とsRGB一回出力。
- R1 P3: `053e73be13ab49454691105fd91acbe5144140ff`。物理ライトとEV100プリエクスポージャ。
- R1 P4: `f3d4abb20ad283d66f0a39b38b1b9aaa0ee92013`。放射輝度IBL、DFG、GGX補償と数値検証。focused build 13/13、CPU 7/7、GPU 6/6（exit 125なし）、独立レビュー、接続監査、fresh verifierを2026-09-08にPASS。

- R1 P5: `7d134b7d5fef9486210589d698b01d66bf0c7d31`。物理ライト・GGX・IBL・方向影の透明描画を受入完了。focused8/CPU6/skip、P4 GPU6、透明10行/背景10行/固定4輝度、両scene object-presence、lifecycle、独立recheck12件、累積source18の正式Claude評価（blocking 0）を確認した。
- R1 P6a: `93f01220c2bcd899c51e3a9c538265e5723395dc`（P6a forward-fixの受入状態を記録）。`2a50d4c` と `d4cbe3e` のforward-fixを含む最終報告、focused7、CPU6、fixture CPU検査、40 static/68 numerical/70 total/120 captures、両scene object-presence、script self-tests、GPU CTest exact21を確認した。旧P6b候補は再利用しない。
- R1 P6b: `eedec7a681489768645eabf77355886ae8e755c0`。承認済みbaseline（Indoor `3845F8671194A3C55EA929503E7ECB4C777EB7A5A4D900C03E75853E50D9D4EF` / Outdoor `05D202193F8C304E45099EBA6557A48BB7B5BF88E694552CBABC50E898D895B6`）、threshold、BackBuffer20=20/20、targetless full build=exit 0、full CTest=212 passed/7 intentional skipped/0 failedを受入完了。Roadmap trailerは`RenderingRoadmap: R1 complete`、GPU性能はDeferred。

## In progress

- なし（R1 P6b完了）。

## Next

- なし（R1完了）。次フェーズは新しい計画と承認済みscopeを起点に着手する。

## Notes

- P6a completion report: `.superpowers/sdd/RenderingR1PhysicalFoundationPlan/task-6a-report.md`。forward-fixの最終証拠は `.harness/runs/20260908-084154/p6a-forward-fix-gpu/` と `p6a-forward-fix-review/claude-final.json` に保存する。旧P6a reportは同ディレクトリのアーカイブへ保持する。
- 独立再検証: `recheck-R1-P6A-4-1.txt`〜`-11.txt`。集計は `p6a-independent-recheck-audit-1730.json`。統合接続評価は `p6a-r1-integrated-review/receipt.json` / `stdout.json`。
- 統合評価はcapture状態遷移の全寿命と照明単位の全項を追跡していない。blocking 0として受理し、確認範囲を拡大解釈しない。bundle評価本文のS79→319はrawと不一致で、実値S80→320を採用する。
- 初回bundle評価の同時起動、7d1cff3→573a9e4のamend、573a9e4の運転文書を含むEOL差は履歴を保持する。現在のsource20はEOL一致。実施していないSol評価や古いfixed subject/direct parent/cached20をPASSと記録しない。
- 2026-08-15の承認: P6a v4 SHA=B805112EC87BAB673F7D0093FB6BB93DABBACCF5B4682D55992602869D40156F、P6b v3 SHA=37D08DE402478F1D1EBEEEE2D0D8F134492AA0A0EDEB2A4C8FF69527721A2AE3。`Docs/Plans/RenderingR1P6aPhaseDeclaration.md`に承認原文と元sessionの参照を保持する。P6b baseline/thresholdの実物承認原文は`Docs/RenderingValidation/R1Acceptance.md`とcontrol eventへ記録した。
- P4/P5受入の詳細、途中診断、非blocking事項は `.harness/runs/20260908-084154/progress-before-p6a-acceptance.md` および `Docs/Plans/RenderingR1P6aParentNotes20260908.md` に保存した。途中の「未完」は当時の状態である。
- tracked v1 scene、R0基準2文書、P6b開始時のsource-start hashesは証跡へ固定した。P6bの公開tracked範囲はPNG2枚、VisualThresholds.tsv、R1Acceptance.mdで、forward-fixのテスト契約修正と受入れ記録を別コミットに分離した。プッシュは行わない。
