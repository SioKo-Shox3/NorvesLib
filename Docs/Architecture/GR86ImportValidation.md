# GR86前半の実装・検証状態

## 対象

2026-10-03のG2-S4推奨A承認に基づく、既定Strictの追加影響拒否、明示ReduceToFour、
CUBICSPLINEの明示Bake、morphの明示Dropを対象とする。
実装基準は [b2aa6ff](https://github.com/SioKo-Shox3/NorvesLib/commit/b2aa6ff708898522809f1ab9d861d81f69fdb679)。
詳細契約・CLI・JSON・制限は [SkeletalImportLimits.md](SkeletalImportLimits.md)、
焼込の数値前提は [SkeletalCubicBakeBounds.md](SkeletalCubicBakeBounds.md) を参照。

GR86後半の256関節は未実装。現行0.0/0.1は128。計画中の0.2も128を維持し、256はGR82 Stage Bのv1と同時に進める。
GR82 v1のclip作成時rest保持・束縛時差分検査は承認済みだが、本変更の実装範囲ではない。

## 実装した境界

- 既定StrictはJOINTS_n/WEIGHTS_nの追加セットを黙認しない。cook前cache照合も同じgateを使う
- Reduceは同一joint合算、決定的な上位4本、脱落量とwarn/fail閾値、float再正規化を扱う
- Bakeは最終translation単位で適応LINEAR化し、種類別誤差上界とdepth/channel/asset予算を検査する
- Dropは位置/法線/接線morph、mesh/node初期weights、weight animationを検証して除去する。baseへ焼付けない
- API/raw/legacy/file/cookへの接続、CLIの厳密指定、同じpolicyによるcook/skip、JSONの未測定null/実測0/失敗時の検証済み数量を実装した
- Drop無しの既存Strict/Reduce/Bake hashを保ち、Drop時は別canonicalで区別する。骨格wire・64B頂点ABI・既定描画経路は変更していない

## このLinux環境で実行した検証

実装基準のソースからg++ 14.2.0で再ビルドし、次の8試験をそれぞれ実行した。
モードは通常、-O2 -DNDEBUG、ASan/UBSanの3種で、計24実行すべて成功した。
テスト内のassertはNDEBUGでも有効化する構成。LeakSanitizerは実行環境の制約で無効にした。

| 試験 | 実行で確かめた範囲 |
|---|---|
| SkeletalInfluenceAttributeTest | 属性名の正準/追加set/不正/overflow/custom属性 |
| SkeletalInfluenceSetPairTest | 0始まり連続pair、重複、欠落、境界 |
| SkeletalInfluenceReductionTest | 合算/順位/同値順/脱落量/閾値/失敗出力保持 |
| SkeletalImportPolicyTest | enum/数値/予算、旧hash、SCBK/SMDP固定列・hash |
| SkeletalCubicBoundsTest | Hermite/Bezier区間包絡、分割、全区間上界、回転の拒否境界 |
| SkeletalCubicBakeTest | 適応焼込、実共有補間helper、数値予算、時刻/深さ/sample制限、失敗出力保持 |
| SkeletalCliOptionsTest | Strict/Reduce/Bake/Drop、値/順序/重複/用途、引数失敗時の保持 |
| SkeletalImportReportTest | 旧形式維持、成功/失敗/cache、単位、未測定null/実測0、組合せ |

各試験はCMakeのMEMBER生成と同じmain改名・実InvokeBundledMainを使うwrapperでもコンパイル成功した。
これはWindowsのCookedMeshTest束全体をリンク・実行したという意味ではない。
JSON出力は各モード18件、計54件をPythonで独立解析し、構文とnull/0/後段失敗の意味を照合した。

共通ビルド指定:

```
-std=c++23 -Wall -Wextra -Werror
-I. -ILibrary/Core/Public -ILibrary/Core/Private -ILibrary/Math/Public
```

ASan/UBSanではさらに `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`、
実行環境変数 `ASAN_OPTIONS=detect_leaks=0` を使用した。
依存実装はテストに応じ、SkeletalInfluenceReduction.cpp、SkeletalImportPolicy.cpp、
SkeletalCubicBounds.cpp、SkeletalCubicBake.cpp、Tools/AssetCook/SkeletalImportReport.cppをリンクする。
代替のWindows/Json/allocator/Math実装を作って通した結果ではない。

fixtureのbuffer範囲、native試験のJSON置換、GLB準備、実API名も静的に照合した。
この静的確認は、次節の未実行項目を動作確認済みに変更しない。

## 登録済みだが未実行の検証

- JsonDocumentによるStrict gate/追加set collector/morph存在判定
- 実decoderのraw/legacy/file/GLB入口、sidecar scale/fit、cook→NVSKEL parse往復
- Reduceの複数set/整数UNORM、Cubicの3種類・失敗prefix/予算、Dropの数量/不正構造・値/複数target/LINEAR・STEP/weightのみclip/後段TRS失敗
- AssetCook CLIでの出力保持、警告、JSON、cache hit時の非更新、設定差によるmiss
- Windows実補間・全pose/skinning/GPU・実物Blender資産での受入れ

前4項目はCookedMeshTestのMEMBER、CookedSkeletalAssetTest.cppと同名CMake smokeなどへ登録している。
この環境はCoreのString等が要求するWindows.hを持たず、native構文確認がそこで停止した。
CMake/PowerShellも利用できず、native実行の成功とは記録しない。
GitHubの当該commitのcombined statusは空であり、CI成功の証拠にもしていない。
作者が非描画Windowsビルドを必須から外したため、これらを明示したうえで非描画実装を先行している。

## 既知の制限と未完

- Bakeの保証は記載したIEEE/精密FP/実補間枝の前提付き。別targetのFP設定やWindows全poseは未確認
- 短時間隔、近ゼロquaternion、数値/誤差/深さ/sample予算を認証できない入力は失敗する
- DropはUV/色/custom/拡張semanticのmorphを受けない。除去後にTRS channelが無くなるclipも失敗する
- morph検査は完全glTF validatorではない。POSITION boundsのfloat32範囲/実値一致、weight入力min/maxまでは検査せず、構造不正の理由はInvalidAccessorへまとめる
- 実物大型GLB、描画、v1/256、retarget/GR84・GR85、全体の制作フロー受入れは未完

G2-S3(a)の材質レコード載せ先とG2-S7のmesh数受理範囲は2026-10-04に作者承認済み（G2ImportDecisions.md）。
この検証記録は、未選定のARM/emissive既定やS5/S6を承認済みとするものではない。
