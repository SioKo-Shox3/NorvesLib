# GR78 取り込み設定と診断の実装・検証記録

記録日: 2026-10-03。対象: G2 / GR78、feature/game-asset-import。
これは承認済み範囲の実装整理であり、Windows/native統合や実物描画を合格とする記録ではない。

## 実装済み

| 項目 | 範囲 |
|---|---|
| 設定正本 | `<source>.import.json`、明示override/required/disabled、厳格v1解析・正規化 |
| 静的モデル | 軸・鏡像・scale/fit・keep/箱中心/足元/custom原点・法線/UV/windingをcook/loose共有 |
| 骨格 | 頂点位置・IBM平行移動・Translation sample・mesh-node平行移動を同倍率でscale/fit。軸/原点等は明示拒否 |
| hash | 元source全量と外部buffer全量へ正規化設定を連結。設定無しは旧hash、meta/書式のみ変更は同値 |
| 出力保護 | 採用設定のpackage/manifest/派生画像出力aliasをwrite前に拒否 |
| CLI設定 | `--import-settings`、`--no-sidecar`、`--require-sidecar` |
| 増分判定 | `--skip-if-unchanged`。cook前hash・manifest key/format/entry/path・modelと全embedded画像の実package/hashを検査 |
| 診断 | `--inspect <file>`。変換前mesh-local幾何・位置完全一致溶接/成分・画像8/16bit統計・材質係数、manual方向候補とstdoutのみの恒等設定template |

詳細契約は [ImportSettings.md](ImportSettings.md)、利用入口は [AssetCookWorkflow.md](AssetCookWorkflow.md)。
inspectは現行static profileの単一mesh/primitiveが対象。材質係数の表示はGR79の描画/cook導入を意味しない。

## クラウドで実行した検査

次の6系統を通常、`-O2 -DNDEBUG`、AddressSanitizer/UndefinedBehaviorSanitizerで実行し、計18実行で成功を確認。
最終照合も同日に実行した。LeakSanitizerは環境制約により`ASAN_OPTIONS=detect_leaks=0`とした。

| 実試験 | 主な対象 |
|---|---|
| ImportSettingsValueTest | 値域・軸・固定canonical bytes・符号付きゼロ |
| ImportTransformTest | 48方向、scale/fit/原点/normal/UV/winding、失敗非変更、骨格用倍率計算 |
| ImportSettingsHashTest | 独立既知FNV状態、無し不変、値/版/ゼロ正規化 |
| ImportCliOptionsTest | 設定/skip/inspectの引数、排他/重複/空値/独立mode |
| GeometryInspectionTest | bounds/完全一致weld/連結成分/ゼロ法線/極端値/全Span重複拒否 |
| ImageInspectionTest | 実stbによる既知PNG8/PNG16/JPEG、統計・不正入力・alias・最終pixel寸法上限 |

上記6件は既存bundleのMEMBER相当（mainの名前変更）のobject compileも成功。
ImportSettingsFileTestもMEMBER compileは成功したが、設定file I/Oと実JsonDocumentのruntime実行を意味しない。
実stb診断では通常の8/16bitとdecode失敗を検査している。最終pixel payloadの512MiB上限は、
stb内部作業領域や過剰IDAT inflateを含む総メモリ制限ではない。

## 未実行・保留

- Windows.hを使うCoreのため、現クラウドではMain/実cooker/loose/実JsonDocument/実skinningのnative build・統合試験は未実行
- ImportSettingsJsonTest、ImportSettingsFileTest、ImportStaticIntegrationTest、StaticGltfBufferCookTest、CookedSkeletalAssetTest、AssetCookImportSmoke、AssetCookInspectSmoke等は登録・ソース確認の状態。全体成功とは扱わない
- Windows/GPU描画、実際の犬GLB 4本との寸法・画像・見た目照合は未実行。実物がまだ配置されていない
- `surface_centroid`は作者合意により明示拒否を維持。精度確認と再有効化は別保留
- GitHubのcombined statusは返却されたチェックが空で、CI合格という証拠ではない
- 非blockingの追補候補: required不在時preflight単独assert、inspectの必須拡張に対する詳細理由保持、CLI入力変更時の再診断回帰、geometryの追加alias/全出力保持境界

### 対応環境での後続確認例

Windows検証は現在の継続条件ではない。以下は対応環境で検証する際の例。

```powershell
cmake --build build --config Debug --target CookedMeshTest AssetCook -- /m:1
ctest --test-dir build -C Debug --output-on-failure -R "^(ImportSettingsValueTest|ImportSettingsJsonTest|ImportSettingsFileTest|ImportSettingsHashTest|ImportTransformTest|ImportStaticIntegrationTest|ImportCliOptionsTest|GeometryInspectionTest|ImageInspectionTest|StaticGltfBufferCookTest|CookedSkeletalAssetTest|AssetCookImportSmoke|AssetCookInspectSmoke|AssetCookGlbSmoke|AssetCookMeshSmoke)$"
```

## 次の選定

G2-S4（GR86の縮約/焼込/Strict変更とv1時の256関節化）、G2-S1（GR32/GR82の0.2統一と後続v1、SkeletonIdのrest pose扱い）は確認待ち。
GR78の承認をこれらの承認へ広げない。S3/S5/S6/S7とS8のBVH/FBX部分も別途未確定。
