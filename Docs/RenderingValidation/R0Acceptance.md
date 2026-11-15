# R0 描画検証受入れ記録

## 記録の範囲

この文書を R0 受入れの一次記録とする。各項目の結果、Git オブジェクトの識別情報、画像の数値を以下に記録する。

- branch: `feature/rendering-r0-validation`
- 実走HEAD: `1d122fb732870d59c7cdb33cfffe446bdfd536b3`
- 採用HEAD: `3d86104aa2d18a9f941a21aece45ec6ee3f89f65`
- GPU: `NVIDIA GeForce RTX 4080`
- driver: `591.86`
- 証拠採取日: 2026-08-12 JST

G04／G05／G07のfocused GPU／10回連続は実走HEADで取得した。2026-08-12にユーザー／gate ownerが承認した限定evidence-transfer契約に基づき、保護対象のsource／dependency／assetのGit object identity一致、独立したsource-text contract test 1pathだけの差分、採用HEADのnormal full suite exit 0を条件として採用HEADへ適用する。保証範囲は後述のGit object identityであり、それを超える同一性は主張しない。

2026-08-12 の通常 GPU の受入れでは、実 GPU の試験の終了コード 125 または `Skipped` を受入れなかった。当日の通常全体実行の7件の `Skipped` は、スキップの挙動を確認する専用の試験の結果だった。

| Evidence ID | ゲート | ステータス | command／結果の要約 |
|---|---|---|---|
| R0-G01 | branch／commit chain／Deferred決定 | Passed | `git branch --show-current`、`git rev-parse HEAD`、`git log --oneline --decorate -12`、tracked／all `git status`、`git stash list`、tracked performance path検索は各exit 0。decision文書あり、branch／採用HEAD一致、tracked clean、all statusはacceptance 1path、stash exact 1件、performance path 0件。 |
| R0-G02 | CPU の負の対照 | Passed | 1 画素の差、既知の浮動小数点値と NaN／+Inf／-Inf、知覚差の人工的な差分、候補画像 2 件、数値の解析は各終了コード 0。結果の判定行は本文に記録。 |
| R0-G03 | GPU のスキップ契約 | Passed | 終了コード 0、GPU 対象 21 件。強制スキップ時は同 21 件が明示的にスキップされた。 |
| R0-G04 | float／HDR SceneColor GPU | Passed | 実走HEAD `1d122fb732870d59c7cdb33cfffe446bdfd536b3`、採用HEAD `3d86104aa2d18a9f941a21aece45ec6ee3f89f65`。focused commandはexit 0、通常3 Passed／通常skip 0、forced contract 2 Skipped。GPU／driverは上記。限定evidence transferを適用。 |
| R0-G05 | 画像の比較 20 回 | Passed | 実走 HEAD `1d122fb732870d59c7cdb33cfffe446bdfd536b3`、採用 HEAD `3d86104aa2d18a9f941a21aece45ec6ee3f89f65`。終了コード 0、比較 20 回、スキップ 0、両シーンの最大平均 FLIP `0.000000000`／最大チャンネル差 `0`。 |
| R0-G06 | GPU性能予算 | Deferred | 2026-08-12のユーザーdecision。test過多を避けて将来CI GPU runnerの性能回帰トラックへ移管する。tracked performance pathは0。R0を阻害しないが、性能評価を成功扱いするものではない。 |
| R0-G07 | 開発機10回連続 | Passed | 実走HEAD `1d122fb732870d59c7cdb33cfffe446bdfd536b3`、採用HEAD `3d86104aa2d18a9f941a21aece45ec6ee3f89f65`。indoor／outdoor各`successful_runs=10`、各`skipped_runs=0`、合計20 compare、exit 0。限定evidence transferを適用。 |
| R0-G08 | 画像・しきい値の同一性 | Passed | 2026-08-10 の実走記録と 3 件の SHA-256 を照合し、実走 HEAD から採用 HEAD へ結果を適用した。画像は再生成していない。 |
| R0-G09 | GPU 強制スキップ時の結果 | Passed | 終了コード 0、`219 total = 198 Passed + 21 Skipped`、失敗 0、175.36 秒。 |
| R0-G10 | 通常時の全体結果 | Passed | 採用 HEAD で終了コード 0、`219 total = 212 Passed + 7 Skipped`、失敗 0、272.81 秒。7 件は意図したスキップ契約で、通常 GPU のスキップは 0。 |
| R0-G11 | 独立review round 2 | Passed | 2026-08-12、採用HEAD `3d86104aa2d18a9f941a21aece45ec6ee3f89f65`。独立Codex round 2 `r0_final_impl_reviewer` とClaude fable round 2を実施。Codexの残件はparser command／marker 1件、ClaudeのB1–B9はall ADDRESSEDで残件は`trigger diff exit 0` marker 1件だった。final mechanical fix後、G08 identity gateは`G08_IDENTITY_GATE=PASS trigger_diff_exit=0 g05_g07_transfer=True`、exit 0。open blocking 0。最大2周のためround 3は未実施。 |
| R0-G12 | hygiene／最終scope | Passed | hygiene before G12: ChangedPath=`Docs/RenderingValidation/R0Acceptance.md`、Utf8Bom=True、CrlfOnly=True、TrailingWhitespace=False、DiffCheckExit=0、IgnoreExit=0、Status=`?? Docs/RenderingValidation/R0Acceptance.md`。最終scope exact 1path、plans ignored、stageなし。 |

## R0-G01 Git／decision provenance

| command | exit | 記録結果 |
|---|---:|---|
| `git branch --show-current` | 0 | `feature/rendering-r0-validation` |
| `git rev-parse HEAD` | 0 | `3d86104aa2d18a9f941a21aece45ec6ee3f89f65` |
| `git log --oneline --decorate -12` | 0 | 下記commit chain |
| `git status --short --untracked-files=no` | 0 | 出力なし。tracked tree clean。 |
| `git status --short` | 0 | `?? Docs/RenderingValidation/R0Acceptance.md` のみ。 |
| `Test-Path Docs/RenderingValidation/PerceptualDiffSelection.md` | — | `True`。tracked decision文書あり。 |
| `git stash list` のmessage照合 | 0 | `保留: R0 GPU性能予算ゲート 2026-08-12` exact 1件。 |
| `git ls-files \| Where-Object { $_ -match 'RenderingPerformance\|PerformanceBudget' }` | 0 | tracked performance path 0件。 |

新しい順のR0 commit chain:

- `3d86104` test: 空shadowの描画パス契約を更新する
- `1d122fb` feat: RenderGraph pass GPU 計測を追加する
- `17991bf` fix: 空のshadow passでもdepthを確定する
- `fa11f51` fix: Vulkan image layout契約を整合させる
- `1cbb259` test: LDR FLIP 描画差分ゲートを追加する
- `2da856a` docs: S1 知覚差分方式を選定する
- `b491d4a` feat: SceneColor capture を FramePacket 化する
- `dc51733` feat: HDR float readback 検証を追加する
- `6ee7300` test: PNG golden 描画回帰を追加する
- `ff91dfb` test: R0 描画 fixture を固定化する
- `8b0ae77` GPU CTest のスキップ契約を整備する

## 限定evidence-transfer certificate

- 承認日: 2026-08-12
- 承認者: ユーザー／R0 gate owner
- 実走HEAD: `1d122fb732870d59c7cdb33cfffe446bdfd536b3`
- 採用HEAD: `3d86104aa2d18a9f941a21aece45ec6ee3f89f65`
- 両 HEAD 間の変更は、影パスのソース文字列の契約確認 1 件だけだった。
- 上記変更fileは`ShadowMapPass.cpp`のsource textを検査する独立executableの唯一のsourceであり、G04／G05／G07 targetのsourceではない。
- 採用HEADのnormal full suiteはexit 0、`219 total = 212 Passed + 7 Skipped`、failure 0。変更されたcontract targetを含むsuite全体を実行済み。

両HEADでexact一致した保護対象Git object:

| path | object ID |
|---|---|
| `Library` | `425dc247a63cfb25ba48ba9d62275f0b72856f80` |
| `Assets` | `a4c73ac12f47cbbf2768dc6cbdd7f4ade0e60eb3` |
| `Scripts` | `5dd29797f5b2ee2f0c3196b61f53fca2c4ee0b51` |

このcertificateにより、G04／G05／G07の実走revisionを書き換えず、保護対象source／dependency／asset identityと採用HEADのfull-suite結果に限定して証拠を移管する。

## R0-G02 負の対照の結果

| 対象 | 結果 |
|---|---|
| 1 画素の差 | `one_pixel_negative=rejected max_coordinate=(0,0)`、終了コード 0 |
| 既知の浮動小数点値・非有限値 | `known_float=green nan_negative=rejected positive_inf_negative=rejected negative_inf_negative=rejected`、終了コード 0 |
| 知覚差の人工的な差分 | 合格、終了コード 0 |
| 壊れた候補画像 | `DecodeFailed` として拒否、元の画像のハッシュは不変 |
| 寸法の違う候補画像 | `InvalidDimensions` として拒否、元の画像のハッシュは不変 |
| 数値の解析 | 全負例を理由付きで拒否、`measurement_parser_self_test=PASS` |

## R0-G03／G04 GPU の記録

2026-08-12 の記録では、GPU 対象 21 件のすべてが強制スキップ時に明示的にスキップされた。終了コードは 0。

結果はsuite exit 0。通常3件（known float、indoor HDR、outdoor HDR）は3 Passed／0 Skipped、forced contract 2件は2 Skipped。通常GPU実行はGPU `NVIDIA GeForce RTX 4080`、driver `591.86`で、known FP16、実RGBA16F textureのNaN／+Inf／-Inf reject、indoor／outdoor finite SceneColorを確認した。採用HEADへの適用根拠は限定evidence-transfer certificateに記録した。

## R0-G05／G07 10回連続

結果はexit 0、合計20 compare、GPU skip 0。

| scene | successful_runs | skipped_runs | max_mean_flip | max_raw |
|---|---:|---:|---:|---:|
| indoor | 10 | 0 | `0.000000000` | 0 |
| outdoor | 10 | 0 | `0.000000000` | 0 |

採用HEADへの適用根拠は限定evidence-transfer certificateに記録した。

## R0-G06 性能保留decision

- 日付: 2026-08-12
- ステータス: Deferred
- 決定者: ユーザー／R0 gate owner
- 判断: R0の性能予算、性能negative、改定手順はtest過多を避けるため実装・実走しない。
- 移管先: 将来のCI GPU runnerによる独立した性能回帰トラック。
- 永続根拠: このtracked acceptanceにdecision、理由、移管先、R0非阻害を記録する。
- 補助整合証拠: stash message `保留: R0 GPU性能予算ゲート 2026-08-12` exact 1件。
- tracked実装確認: `git ls-files | Where-Object { $_ -match 'RenderingPerformance|PerformanceBudget' }` はexit 0、0path。
- R0への影響: このDeferredはR0を阻害しない。性能を評価済みとは扱わない。

## R0-G08 identity／再校正trigger

2026-08-10 00:27 JST の実走記録では、公開 2 回、2 シーンの比較、および 20 回の比較で終了コード 0 を記録した。`PerceptualDiffSelection.md` は同日のユーザー承認と下記のしきい値のハッシュを保持する。

| artifact | SHA-256 |
|---|---|
| `Indoor.png` | `545E745CE9958F310A551B0D71BEAB4DAD35743C6367E0DA0724F9930E6F49E7` |
| `Outdoor.png` | `3676A470814C68841BF1C8E4BF8612802042AB936AAA77E1A9937C5EC642BA7E` |
| `VisualThresholds.tsv` | `01755EC8E6091400B3589802411DCF1AD0AB9D3AF6EA5D9658390AC820DC414F` |

記録時の画像・しきい値と、sRGB 変換、PPD、FLIP の版、提示時のガンマの入力は不変だった。G05／G07 の結果は、実走 HEAD から採用 HEAD へ適用した。画像の再生成は行っていない。

## R0-G09／G10 full suite

通常時の全体結果は採用 HEAD `3d86104aa2d18a9f941a21aece45ec6ee3f89f65` の記録である。

結果は終了コード 0、`219 total = 212 Passed + 7 Skipped`、失敗 0、272.81 秒。7 件は意図したスキップ契約で、通常 GPU のスキップは 0。

同じ採用 HEAD の強制スキップ時は終了コード 0、`219 total = 198 Passed + 21 Skipped`、失敗 0、175.36 秒。GPU 対象 21 件がすべて明示的にスキップされた。この結果は実 GPU での受入れの代用ではない。

## review状態とcommit前gate

engine実装auditはR0 blocking 0、test auditの実装test指摘はnonblockingまたはinvalidと裁定済み。2026-08-12の独立Codex round 2 `r0_final_impl_reviewer` とClaude fable round 2後に残った2件の機械修正を完了し、G08 identity gateもexit 0で確認した。round 2後のopen blockingは0件であり、レビュー最大2周のためround 3は実施していない。R0-G11は`Passed`とする。

hygiene before G12はBOM／CRLF／末尾空白、diff check、plan ignore、changed path exact 1件、stageなしを満たしたため、R0-G12は`Passed`とする。completion commit直前には同じhygieneを再実行する。manual push前にロードマップ上のR0を完了として更新しない。
