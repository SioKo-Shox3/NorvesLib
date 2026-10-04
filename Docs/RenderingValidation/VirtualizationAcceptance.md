# テクスチャとジオメトリの仮想化の受入れ判定

段ごとの受入れ（計画 `Docs/Plans/VirtualizedTextureGeometryPlan.md` の 5）を、段の節に書き足していく。

## 段1（BC圧縮）

判定日: 2026-10-04。ブランチ `feature/vtg-stage1-bc`（コミット `ca1a00bb` の上）。段1の受入れは、起動画面のテクスチャの VRAM が無圧縮の約1/5、
撮影の見た目が無圧縮と区別できない（PSNR を記録）、golden 不変。

撮影はすべて RelWithDebInfo の Game（1280×720、TAA・RTGI 有効の起動画面の既定）。「クック済み」は既定の `build/CookedAssets/`、
「ばら」は `-LooseTextures`（クック済みを使わず元画像を無圧縮で読む）。

### 結果の一覧

| 項目 | 検査 | 結果 |
|---|---|---|
| 関係ターゲットの Debug ビルド | `cmake --build build --config Debug --target RenderResourcesDomainContractTest RHITextureUpdateVulkanTest CookedMeshTest CookedTextureUploadTest MaterialResourcesTest RenderingGoldenImageTest AssetCook -- /m:1` | EXIT_CODE=0（`verify-VTG1-ACCEPT-1.txt`） |
| 関係する ctest（13本） | `ctest -C Debug -R "^(VideoMemoryBudgetVulkanTest\|TextureMemoryLedgerTest\|GpuRetireQueueTest\|RHIBlockCompressedFormatTest\|RHIBlockCompressedTextureVulkanTest\|CookedTextureTest\|CookedTextureUploadTest\|AssetCookBlockCompressSmoke\|AssetCookTextureSmoke\|MaterialResourcesTest\|GBufferMaterialDescriptorCacheTest\|RenderingGoldenIndoorVulkanTest\|RenderingGoldenOutdoorVulkanTest)$"` | 13/13 passed（`-2`） |
| golden | 上の `RenderingGoldenIndoorVulkanTest`・`RenderingGoldenOutdoorVulkanTest` | 2本とも pass。基準画像・閾値は段1で変えていない（`git diff main...HEAD` に golden の基準画像・閾値の変更なし）ので再承認なし |
| 朝10°・昼45°・夕3° × 既定・近接・低角度の撮影 | `Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG1-ACCEPT -Configuration RelWithDebInfo -SunElevations 10,45,3` | 9枚、result=pass、クック済みで撮った（`-3`）。白飛び・黒つぶれの画素率は全視点 0 |
| 夜の撮影 | `Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG1-ACCEPT-night -Configuration RelWithDebInfo -Night` | 3枚、result=pass（`-4`）。白飛び・黒つぶれの画素率は 0 |
| 比べるばらの撮影（2回） | 同じ引数に `-LooseTextures`。出力先 `VTG1-ACCEPT-loose`・`-loose-night`・`-loose-run2`・`-loose-run2-night` | 計4回とも result=pass（`-5-loose.txt`） |

### テクスチャの VRAM

撮影ログの `VRAM_LEDGER`（`GameApplicationHandler::LogVramLedgerOnce`。全テクスチャの確保量をミップ込みで合算）。全視点のログで同じ値（`metrics.json` の `vram_ledger_texture_mb`）。

| | 枚数 | texture_mb | 備考 |
|---|---|---|---|
| ばら（移行前と同じ読み方） | 50 | 3609.6 | `-LooseTextures` の撮影のログ。移行前の基準（VTG1-VRAM-LEDGER）と同じ値 |
| クック済み（移行後） | 41 | 969.6 | 全体の比 969.6 / 3609.6 = **0.269** |

内訳（岩・小屋の glTF のテクスチャは両方で同じ無圧縮の量なので、差し引きで求めた）:

| 区分 | ばら | クック済み | 比 |
|---|---|---|---|
| 起動画面の材質（銀・石畳・地面の見本6種。アルベド・法線・ORM・視差の高さ） | 3200.0 MiB | 560.0 MiB | **0.175** |
| 岩（`boulder_01_4k.gltf` の diff・nor_gl・arm の 4096² の JPG 3枚）と小屋（`Cottage_Clean` の Base_Color・Normal・ARM の PNG 3枚）の glTF のテクスチャ | 409.6 MiB | 409.6 MiB（無圧縮のまま） | 1.0 |

クック済みの撮影ログには、この6枚について `TEXTURE_COOKED_MISSING path=…` の警告が1回ずつ出ている（ばらの撮影では出ない）。
glTF の材質側が BC5 の法線の印と、RGBA8 前提の ARM の分割に対応していないため、この段では焼けない。**段4（VTG4-COOK-STARTUP-MODELS）で焼く残り**で、
焼けば全体も約0.18（岩・小屋が約409 MiB から約110 MiB）になる見込み。

判定: 段1の受入れ「起動画面のテクスチャの VRAM が無圧縮の約1/5」は、クック済みにした材質では 0.175 で満たす。全体の 0.269 は満たさない（残りは上の6枚）。
全体の比は段4で満たす計画とし、段1では材質の比で判定する。

### 見た目（PSNR）

同じ視点・現行コードの、クック済みとばらの 1280×720 RGB の PSNR（dB）。ばらを2回撮り（`loose`・`loose2`）、
同じコードのばら同士の PSNR を揺らぎとして並べる（RTGI・TAA の履歴と、大きな球の自転の位相が撮影ごとに違うため、同じコードでも一致しない）。
目安は 35 dB 以上。

| 視点 | クック済み対ばら | クック済み対ばら2 | ばら対ばら2（揺らぎ） | 平均輝度 クック済み / ばら / ばら2 |
|---|---|---|---|---|
| 既定・朝10° | 40.09 | 40.79 | 45.21 | 113.10 / 114.04 / 113.37 |
| 既定・昼45° | 42.25 | 42.00 | 49.83 | 117.16 / 118.12 / 118.15 |
| 既定・夕3° | 43.68 | 42.95 | 46.22 | 83.71 / 83.78 / 84.10 |
| 既定・夜 | 42.84 | 40.37 | 41.63 | 56.26 / 56.28 / 55.45 |
| 低角度・朝10° | 38.75 | 39.21 | 37.86 | 118.16 / 116.95 / 119.06 |
| 低角度・昼45° | 38.12 | 44.84 | 40.58 | 122.52 / 120.41 / 121.62 |
| 低角度・夕3° | 42.99 | 40.96 | 43.05 | 94.00 / 93.52 / 92.63 |
| 低角度・夜 | 35.84 | 38.65 | 37.39 | 66.69 / 66.46 / 66.65 |
| 近接・朝10° | 26.89 | 26.79 | 26.82 | 115.63 / 116.26 / 116.39 |
| 近接・昼45° | 28.89 | 28.34 | 28.57 | 119.34 / 121.23 / 120.95 |
| 近接・夕3° | 30.47 | 30.51 | 31.01 | 88.30 / 87.71 / 87.94 |
| 近接・夜 | 36.52 | 36.61 | 37.22 | 13.62 / 13.10 / 13.22 |

- 既定・低角度の全8視点で、クック済み対ばらは 35.84〜43.68 dB（目安35以上）。
- 近接の昼・朝・夕は、ばら同士でも 26.8〜31.0 dB しかなく、クック済み対ばらもその揺らぎと同じ（26.79〜30.51 dB）。画面の大半を占める大きな球が、撮影ごとに違う位相で自転しているため。
  球を含む矩形（1280×720 のうち x 360〜920・y 70〜640。画面の約35%）を除いて測り直すと、クック済み対ばら / クック済み対ばら2 / ばら対ばら2 は
  朝10° 43.54 / 43.28 / 49.59、昼45° 40.29 / 42.10 / 48.75、夕3° 45.64 / 45.55 / 49.88、夜 47.68 / 48.84 / 53.33 dB で、目安35以上。
- 平均輝度の差は、クック済み対ばらが最大 2.11（低角度・昼45° の 122.52 対 120.41）で、ばら同士の最大 2.11（低角度・朝10° の 116.95 対 119.06）と同じ大きさ。

### 起動画面の撮影の所見

クック済み（左）とばら（右）を視点ごとに並べた画像（既定・近接・低角度の順に上から）を開いて確かめた（`.harness/runs/startup-capture/VTG1-ACCEPT-compare/cooked-left-loose-right-{sun10,sun45,sun3,night}.png`。原画は各 `VTG1-ACCEPT*/` の PNG）。

- 朝10°: 石畳・見本の帯・金色の球・岩・小屋・太陽・空の色が同じに見える。低い太陽の長い影の向きと濃さも同じ。縞・色ずれ・ブロックノイズは見えない。
  近接の大きな球は縦の継ぎ目の位置が違う（自転の位相の差）。
- 昼45°: 同じく目立つ違いなし。近接の石畳の目地・小口の欠け、低角度の石畳の奥行き（視差）の縞が出ない。
- 夕3°: 暖色の濃さ・太陽のにじみ・球の反射が同じ。石畳と地面の見本の帯の模様が同じ。
- 夜: 点光源の光だまり・影・光源のにじみが同じ。近接の大きな球（暗部）の石畳の模様が同じに見える。
  近接の球の暗部に小さな赤い斑点が見える。球の暗部を切り出して明るさを2.5倍にした画像（`VTG1-ACCEPT-compare/near-night-sphere-crop.png`。左からクック済み・ばら・ばら2）を開くと、
  斑点はばらの2回にも同じ程度で出ており、位置は撮影ごとに違う。`StartupSceneAcceptance.md` の既知の限界「夜の近接でRTGIの斑な雑音」と同じもので、クック済み固有の欠陥ではない。
- 視差の高さ（BC4）の縞は、低角度・近接のどちらでも出ない。

### 既知の限界

- 全体の `VRAM_LEDGER` は 0.269 で、約1/5（0.2）には届かない。岩・小屋の glTF の6枚（409.6 MiB）が無圧縮のまま。段4で焼く。
- 同じコードの撮り直しだけで画素がずれる（RTGI・TAA の履歴、大きな球の自転）。PSNR はこの揺らぎと並べて読む。決定的な撮影は VTG2-CAPTURE-DETERMINISTIC で作る。
- 近接の PSNR は球の自転で、クック済みとばらの差を直接は測れない。球を除いた測り直しで補った（上）。
- 起動画面の材質のクック済みを使う経路で、次の2つの欠落ケースは Game での再現が無く、コードの静的な追跡だけで見つかっている。
  - マニフェストは有るが、ORM のパッケージ（`Cooked/Startup/PolyHaven/<名前>/<名前>_orm.nvpkg` など）だけが欠けているとき、
    ORM の材質のまま元画像へ戻れず、粗さ・AO の読み込みが外れる可能性がある（法線の2チャンネルの印も形式から決まらない）。
  - `build/CookedAssets/manifest.json` 全体が無いときは `COOKED_ASSETS_MISSING` が1回出て、テクスチャごとの `TEXTURE_COOKED_MISSING` は出ない。
  どちらも `Scripts/CookAssets.ps1`（`CookAssets` ターゲット）でクックし直せば通常の経路に戻る。

## 段2（sparse の VT）

判定日: 2026-10-05。ブランチ `feature/vtg-stage2-sparse-vt`（コミット `1914c131` の上）。段2の受入れ（計画 5）は、`--vram-budget-mb 6500` で負荷モードが溢れずに描け、起動画面の見た目が段1と同等であること。

撮影はすべて RelWithDebInfo の Game（1280×720、TAA・RTGI 有効の起動画面の既定）を `-Deterministic`（`--capture-deterministic`。描画を ST にして同じコードの2回の撮影が一致する）で撮った。
「VT」は既定（`--virtual-texture=on`。起動画面の材質のアルベド・法線・ORM・高さを sparse の VT で描く）、「全常駐」は `--virtual-texture=off`（段1と同じ、クック済みの BC を全ミップ常駐させる経路）。
証拠は `.harness/runs/20261004-233408/verify-VTG2-ACCEPT-<n>-*.txt`、撮影の出力は `.harness/runs/startup-capture/VTG2-ACCEPT*/`（`metrics.json`・PNG・各視点の `*.Game.log`）。

### 結果の一覧

| 項目 | 検査 | 結果 |
|---|---|---|
| 関係ターゲットの Debug ビルド | `cmake --build build --config Debug --target RenderResourcesDomainContractTest RHITextureUpdateVulkanTest CookedMeshTest RenderingGoldenImageTest MaterialResourcesTest -- /m:1` | EXIT_CODE=0（`-1-build.txt`） |
| 関係する ctest（12本） | `SparseCapabilitiesVulkanTest`・`SparseTextureVulkanTest`・`SparseBindVulkanTest`・`SparseTileUploadVulkanTest`・`VirtualTextureResidencyVulkanTest`・`VirtualTextureFeedbackVulkanTest`・`VirtualTextureRequestSetTest`・`VirtualTextureStreamerTest`・`VideoMemoryBudgetManagerTest`・`CookedTextureTest`・`RenderingGoldenIndoorVulkanTest`・`RenderingGoldenOutdoorVulkanTest` | 12/12 passed（`-2-ctest.txt`） |
| ビルドしたターゲットの ctest（6本） | `RenderResourcesDomainContractTest`・`RHITextureUpdateVulkanTest`・`CookedMeshTest`・`MaterialResourcesTest`・`GpuRetireQueueTest`・`TextureMemoryLedgerTest` | 6/6 passed（`-13-built-targets-ctest.txt`）。`RenderResourcesDomainContractTest`・`GpuRetireQueueTest` は sparse 非対応・VT 作成失敗での全常駐への復帰の契約を含む |
| golden | 上の `RenderingGoldenIndoorVulkanTest`・`RenderingGoldenOutdoorVulkanTest` | 2本とも pass。基準画像・閾値は段2で変えていない（`git diff main...HEAD --name-only` に golden の基準画像・閾値の変更なし）ので再承認なし |
| 朝10°・昼45°・夕3° × 既定・近接・低角度（VT） | `Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG2-ACCEPT -Configuration RelWithDebInfo -Deterministic -SunElevations 10,45,3` | 9枚、result=pass（`-3-capture-day.txt`）。白飛び・黒つぶれの画素率は全視点 0 |
| 夜 × 既定・近接・低角度（VT） | `... -OutDir .harness/runs/startup-capture/VTG2-ACCEPT-night ... -Deterministic -Night` | 3枚、result=pass（`-4-capture-night.txt`）。白飛び・黒つぶれの画素率は 0 |
| 比べる全常駐の撮影 | 同じ引数に `-ExtraGameArguments --virtual-texture=off` と `-CompareDeterministicWith <VT の出力先>`。出力先 `VTG2-ACCEPT-resident`・`-resident-night` | 朝・昼・夕・夜の12視点とも result=pass（`-5`・`-6`） |
| 同じコードの揺らぎ | VT・全常駐をそれぞれもう1回撮り（`-vt2`・`-resident2`。`-7`〜`-10`）、1回目と比べた | 下の表 |
| 負荷モード | `-StressTextures -VramBudgetMb 6500`（VT・全常駐。`-11`・`-12`） | 下の表。24/24 材質、溢れなし |

### 見た目（PSNR）

同じ視点・同じコードの 1280×720 RGB の PSNR（dB。100 は完全一致）。
「VT 対 全常駐」が段2で見る差、右の2列は同じコードを2回撮った揺らぎ。`-Deterministic` なので、段1のようにコードが同じでも 40 dB 台に揺れることはない。
基準は 45 dB 以上（`CaptureStartupScene.ps1` の `-DeterministicPsnrLimit` と同じ）。

| 視点 | VT 対 全常駐 | 最大差（/255） | VT 2回（揺らぎ） | 全常駐 2回（揺らぎ） |
|---|---|---|---|---|
| 既定・朝10° | 72.56 | 11 | 100 | 100 |
| 既定・昼45° | 69.16 | 8 | 100 | 100 |
| 既定・夕3° | 74.95 | 8 | 100 | 100 |
| 既定・夜 | 76.96 | 11 | 100 | 100 |
| 近接・朝10° | 100 | 0 | 100 | 100 |
| 近接・昼45° | 100 | 0 | 100 | 100 |
| 近接・夕3° | 100 | 0 | 100 | 100 |
| 近接・夜 | 100 | 0 | 100 | 100 |
| 低角度・朝10° | 62.15 | 23 | 100 | 99.995 |
| 低角度・昼45° | 61.59 | 10 | 95.47 | 95.74 |
| 低角度・夕3° | 65.27 | 20 | 100 | 100 |
| 低角度・夜 | 63.43 | 24 | 100 | 100 |

- 12視点とも VT 対 全常駐は 61.59 dB 以上で、基準 45 dB を 16 dB 以上上回る。平均輝度の差は最大 0.0106（低角度・夜）で、`-DeterministicMeanLuminanceLimit` 0.1 の約1/10。
- 同じコードの2回は、VT・全常駐とも 95.47 dB 以上（最大差 5/255、不一致の画素は 0.002% 以下）で、撮影の揺らぎは VT と全常駐の差より十分小さい。よって VT 対 全常駐の差（既定・低角度）は揺らぎではなく、VT で標本した結果の違い。
- 差の大きさ: 最大差は低角度・夜の 24/255。不一致の画素は低角度・朝10° で 9.0%、昼45° で 9.2%、夕3° で 4.3%、夜で 5.3%、既定で 0.2〜1.8%。近接は完全一致。段1の無圧縮対クック済み（35.84〜43.68 dB。近接は球の自転で 26〜31 dB）より小さい。
- 差の出どころ（VTG2-VT-STARTUP の切り分けと同じ）: 差のある画素の大半は ±1/255 で、VT でない部分（空・家・岩）にも散る。±6/255 以上の画素はごく少数で、地面の手前の帯・地平線寄りに出る。トライリニア・異方性の標本が見る隣のタイルがまだ常駐しておらず、粗いミップへ逃げる画素と推定している（逃げた画素の数は未計測）。

### 起動画面の撮影の所見

VT の撮影の PNG（`VTG2-ACCEPT/low-sun10.png` ほか）を開いて確かめた。
- 低角度・朝10°: 石畳の目地・小口の欠け、視差の奥行き、金色の球の反射、家・岩・太陽・空の色が全常駐と同じに見える。ぼけたタイル・黒・縞・継ぎ目は見えない。
- 負荷モードの上から見た視点（`VTG2-ACCEPT-stress6500/top.png`）: 24 材質の板が 6×4 の格子に並び、どの板にも黒・未定義の色・欠けは無い。奥の見本の帯（起動画面の材質）も出ている。
- 全視点で白飛び・黒つぶれの画素率は 0。

### テクスチャの量

撮影ログの `VRAM_LEDGER textures`（`GameApplicationHandler::LogVramLedgerOnce`。全テクスチャの確保量）と `VRAM_LEDGER sparse_pool`（VT のタイルを置くプール）。

| | 全常駐（段1） | VT（段2） |
|---|---|---|
| `texture_mb`（全体） | 969.6 | 411.5 |
| うち岩・小屋の glTF（無圧縮のまま） | 409.6 | 409.6 |
| 起動画面の材質（銀・石畳・地面の見本のアルベド・法線・ORM・高さ） | 560.0 | 1.9 |
| `sparse_pool`（撮影の最後の used_mb。ブロック1つ、capacity 64.0 MB） | 0 | 既定 6.3 / 近接 26.9 / 低角度 17.5 |
| `VRAM_POOLS non_pool_mb`（プールを除いた VRAM の使用量） | 1636 | 1074 |

- 起動画面の材質は、全常駐の 560.0 MiB に対し、VT は常駐するミップテイルの 1.9 MiB と、見えるタイルを置くプールの使用量 6.3〜26.9 MiB（プールの確保は 64 MiB の1ブロック）。合わせても 29 MiB 以下（確保したプールで数えても 66 MiB）で、全常駐の約 1/20（同 約 1/8）。
- 全体の `texture_mb` は 969.6 → 411.5（0.42）。残りの 409.6 MiB は岩・小屋の glTF で、段4で焼く（段1の既知の限界と同じ）。
- 近接が最もタイルを使うのは、画面いっぱいの石畳・球を細かいミップで読むため。視点でタイルの数が変わるのは VT の期待どおり。

### 負荷モードと予算の上限（`--vram-budget-mb 6500`）

`--stress-textures` は、起動画面の外側の奥へ、Poly Haven（CC0）の 24 材質（4K。クック済みは BC7/BC5/BC7 の 4096² の3枚 × 24 で約 1536 MiB。git の管理外、`Scripts/FetchPolyHavenTextures.ps1 -StressSet` で落とし `CookAssets` で焼く）を貼った板を 6×4 の格子に並べる。
`-VramBudgetMb` は `--vram-budget-mb` で、`VideoMemoryBudgetManager` の VRAM の上限を8GB 級（6500 MB）に絞る。`VRAM_POOLS` は撮影の最後の1行（`-11`・`-12`）。

| 視点 | モード | cap_mb | non_pool_mb | vt_target_mb | vt_used_mb | 追い出し | `texture_mb` | 材質 |
|---|---|---|---|---|---|---|---|---|
| 既定 | VT | 6500 | 1075 | 5424 | 3 | 0 | 416.1 | 24/24 |
| 低角度 | VT | 6500 | 1074 | 5425 | 3 | 0 | 416.0 | 24/24 |
| 上から | VT | 6500 | 1074 | 5425 | 3 | 0 | 416.0 | 24/24 |
| 既定・低角度・上から | 全常駐 | 6500 | 3176 | 3323（使われない） | 0 | 0 | 2505.6 | 24/24 |

- 6500 MB で、VT は `non_pool_mb`（1074〜1075）にプールの使用量（3 MB。ログのプールは 6.4〜14.6 MiB）を足しても 1100 MB 前後で、上限の 17% 程度。溢れず、`vt_used_mb` は `vt_target_mb` 以下（`CaptureStartupScene.ps1` が目標超過を失敗にする検査を通っている）。24 材質とも描けている。
- VT と全常駐の PSNR（`-12`）は既定 100・上から 100・低角度 59.07 dB（最大差 22/255、不一致 11.6%）。平均輝度の差は 0.001 以下。
- この 24 材質は全常駐でも `non_pool_mb` 3176 で 6500 に収まるので、**6500 そのものでは VT が全常駐に勝つ場面は出ない**（溢れの判定は上限内に収まること）。VT が効く、上限が全常駐の必要量より小さい場合は、段2の途中で次の予算で確かめている（`VTG2-STRESS-TEXTURES`・`VTG2-VT-STARTUP` の記録）:
  - `--vram-budget-mb 1200`（`vt_target_mb=123`）: 全常駐は `texture_mb` 2505.6 で収まらないが、VT は `vt_used_mb` 7〜14 で目標以下、全常駐との差は既定・上から 100 dB・低角度 59.1 dB。
  - 目標を 8 MB まで絞る `--vram-budget-mb 1085`・2 MB の `1078`: 追い出しが起き（`vt_evicted_tiles` が 26〜184）、`vt_used_mb` は目標以下で止まる。黒・欠けは無く、静止カメラの時間方向の雑音・旋回の連続フレームも全常駐と同程度（予算が極端に狭いと手前の石畳が粗いミップでぼける）。VUID なし。
- 8GB 級の GPU 本体での測定は、開発機（RTX 4080、約 15 GB）で上限だけを `--vram-budget-mb 6500` に絞って模したもの。

### 段2の受入れの判定

- 「`--vram-budget-mb 6500` で負荷モードが溢れずに描ける」: 満たす。VT は 24/24 材質で、上限の 17% 程度（non_pool 1074 + プール）、全常駐も 3176 MB で収まる。
- 「起動画面の見た目が段1と同等」: 満たす。VT と全常駐の PSNR は 12視点で 61.59 dB 以上（基準 45 dB）、同じコードの揺らぎは 95.47 dB 以上、golden 不変。

### 既知の限界

- **sparse に対応しない GPU の扱い**: `TextureResources::SupportsVirtualTexture()` が、sparse の2Dテクスチャ・常駐のシェーディング・`bFragmentStoresAndAtomics`（フィードバックの書き込み）の3つを要る。1つでも欠ける GPU・VT を作れないとき・ミップテイルが常駐しないときは、同じ枠を全常駐で読み直す（`VT_FALLBACK` の警告）。
  この復帰は、機能の欠けた偽デバイスと要求のバッファを作れない偽デバイスの契約テスト（`RenderResourcesDomainContractTest`）で確かめ、一時的に `bFragmentStoresAndAtomics` を無いことにした Game の実機の撮影で全常駐と同じ絵（default/near 100 dB・low 76.6 dB）を確かめた（VTG2-VT-STARTUP の差し戻し対応）。実際に sparse を持たない GPU では試していない。
- **VT と全常駐の差**（上の表）: 低角度・既定で 61.59〜76.96 dB と、全常駐を2回撮った揺らぎ（95〜100 dB）より大きい。画素の差は最大 24/255 で、見た目では区別できない。推定の原因（隣のタイルが未常駐で粗いミップへ逃げる画素）は、逃げた画素の数を数えていないので未検証。
- **PathTracing・RayTracing の `materialTextures[256]`** は VT のテクスチャを引く経路を持たない（起動画面の既定では使わない）。レイ・パストレーシングを有効にすると、VT の非常駐タイルを読みうる。段2では扱わない。
- **岩・小屋の glTF の 409.6 MiB** は無圧縮のまま（全体の `texture_mb` 411.5 のほぼ全部）。段4で焼く。
- **フィードバックの要求は画素の中心のタイルの1件**だけで、異方性・トライリニアが見る隣のタイルは、使われるまで要求されない（上の PSNR の差の原因の推定）。
- **開発機での実測だけ**: 追い出しと予算の割り振りは RTX 4080 で確かめた。ほかの GPU・ドライバでの `vkQueueBindSparse` の遅さ・差は未測定（計画 7 のリスク）。
- 負荷用のテクスチャ（約 0.8 GB）は git に入れず、`Scripts/FetchPolyHavenTextures.ps1 -StressSet` で取得するので、取得していない環境では `--stress-textures` の撮影を回せない。

## 段3（遮蔽カリング）

判定日: 2026-10-05。ブランチ `feature/vtg-stage3-occlusion`（コミット `d6794128` の上）。段3の受入れ（計画 5）は、小屋の陰のクラスタが省かれ（数を記録）、過剰カリング（穴・消失）が撮影で出ないこと。

撮影（PSNR・`MEGA_OCCLUSION`・旋回）はすべて RelWithDebInfo の Game（1280×720、TAA・RTGI 有効の起動画面の既定）を `-Deterministic`（`--capture-deterministic`。描画を ST にして同じコードの2回の撮影が一致する）で撮った。GPU 時間の計測（`-GpuTimingFrames`）は実時間の計測なので `-Deterministic` ではない（保存値は `deterministic=false`）。
「あり」は既定（`--mega-occlusion=on`。MegaGeometry の2パスの遮蔽カリング）、「なし」は `--mega-occlusion=off`（段2までと同じ、視錐台・法線のコーン・LOD の判定だけの1回のカリング）。
証拠は `.harness/runs/20261005-034826/verify-VTG3-ACCEPT-<n>-*.txt`、撮影の出力は `.harness/runs/startup-capture/VTG3-ACCEPT*/`（`metrics.json`・PNG・各視点の `*.Game.log`）。旋回の1フレームごとの所見は `.harness/runs/20261005-014448/verify-VTG3-OCCLUSION-ORBIT-*.txt` と `.harness/runs/startup-capture/VTG3-OCCLUSION-ORBIT-sheets/`。

### 結果の一覧

| 項目 | 検査 | 結果 |
|---|---|---|
| 関係ターゲットの Debug ビルド | `cmake --build build --config Debug --target RHITextureUpdateVulkanTest RenderGraphCompileTest MegaGeometryResourcesTest RenderingGoldenImageTest -- /m:1` | BUILD_EXIT_CODE=0（`-1.txt`） |
| 関係する ctest（6本） | `HiZPyramidVulkanTest`・`HiZOcclusionTestVulkanTest`・`RenderGraphCompileTest`・`MegaGeometryResourcesTest`・`RenderingGoldenIndoorVulkanTest`・`RenderingGoldenOutdoorVulkanTest` | 6/6 passed（`-2.txt`） |
| golden | 上の `RenderingGoldenIndoorVulkanTest`・`RenderingGoldenOutdoorVulkanTest` | 2本とも pass。基準画像・閾値は段3で変えていない（`git diff main...HEAD --name-only` に golden の基準画像・閾値の変更なし）ので再承認なし |
| 朝10°・昼45°・夕3° × 既定・近接・低角度（あり） | `Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG3-ACCEPT -Configuration RelWithDebInfo -Deterministic -SunElevations 10,45,3` | 9枚、result=pass（`-3-capture-day-on.txt`）。白飛び・黒つぶれの画素率は全視点 0 |
| 夜 × 既定・近接・低角度（あり） | `... -OutDir .harness/runs/startup-capture/VTG3-ACCEPT-night ... -Deterministic -Night` | 3枚、result=pass（`-4-capture-night-on.txt`）。白飛び・黒つぶれの画素率は 0 |
| 比べる「なし」の撮影 | 同じ引数に `-MegaOcclusion Off` と `-CompareDeterministicWith <あり の出力先>`（出力先 `VTG3-ACCEPT-off`・`-night-off`） | 朝・昼・夕・夜の12視点とも result=pass（`-5`・`-6`） |
| 隠し合う視点 occ-sphere・occ-cottage・occ-cottage-edge（昼・夜） | 同じ引数に `-ViewNames occ-sphere,occ-cottage,occ-cottage-edge`（夜は `-Night`）。あり・なしの出力先 `VTG3-ACCEPT-occ`・`-occ-off`・`-occ-night`・`-occ-night-off` | 6視点とも result=pass（`-7`〜`-10`） |
| 旋回（20度/秒、フレーム 3,6,…,111,120 の38枚 × 球・小屋） | `-ViewNames occ-sphere-orbit`（`occ-cottage-orbit`）`-OrbitDegreesPerSecond 20 -OrbitRenderedFrames <38枚>` の あり・なし | 球・小屋とも 38/38 枚が 100 dB・最大差 0（`-14-orbit-sphere.txt`・`-14-orbit-cottage.txt`） |
| GPU 時間 | `-GpuTimingFrames 400 -ViewNames default,near,low,occ-sphere,occ-cottage,occ-cottage-edge` の なし・あり | 下の表（`-11-gpu-off.txt`・`-12-gpu-on.txt`・`-13-gpu-summary.txt`） |

### 見た目（PSNR）

同じ視点の「あり」と「なし」の 1280×720 RGB の PSNR（dB。計算は 100 で上限を切っているので、100 は「ほぼ一致」。完全一致かは最大差が 0 かで判断する）。目安は 60 dB 以上（VTG3-OCCLUSION-ORBIT の完了条件）。スクリプトの既定の下限（`-DeterministicPsnrLimit`）は 45 dB で、全視点とも通っている。

| 視点 | あり 対 なし | 最大差（/255） | 不一致の画素率 |
|---|---|---|---|
| 既定・朝10° / 昼45° / 夕3° / 夜 | 100 / 100 / 100 / 100 | 0 | 0 |
| 近接・朝10° / 昼45° / 夕3° / 夜 | 100 / 100 / 100 / 100 | 0 | 0 |
| 低角度・朝10° | 100 | 1 | 1e-6（約1画素） |
| 低角度・昼45° | 99.537 | 3 | 7e-6（約6画素） |
| 低角度・夕3° | 100 | 1 | 1e-6（約1画素） |
| 低角度・夜 | 100 | 0 | 0 |
| occ-sphere（昼・夜） | 100 / 100 | 0 | 0 |
| occ-cottage（昼・夜） | 100 / 100 | 0 | 0 |
| occ-cottage-edge（昼・夜） | 100 / 100 | 0 | 0 |

- 18視点（昼夜の既定・近接・低角度の 12 と、隠し合う視点の昼夜 6）のうち最小は低角度・昼45° の 99.537 dB で、目安の 60 dB を 39 dB 以上上回る。平均輝度の差は 18視点とも 0（`-5`・`-6`・`-8`・`-10` の `deterministic_comparison` はすべて within_limits=True）。
- 12視点の平均輝度は 昼 118.498・124.437・87.693（既定の朝・昼・夕）、119.365・123.946・89.654（近接）、120.885・126.343・95.439（低角度）、夜 56.965（既定）・13.8（近接）・67.579（低角度）。
- 低角度の 3 視点の差（最大 3/255、約 1〜6 画素）は、深度が同じ境界での描画順（先着）の違いによる ±1〜3 と推定している（確かめていない）。同じ遮蔽ありの撮影を2回撮ると全て 100 dB で一致する（旋回の検証 `verify-VTG3-OCCLUSION-ORBIT-6-repeat-on-vs-on.txt`）ので、撮影の揺らぎではない。穴・欠けではない。

### `MEGA_OCCLUSION`（省いたクラスタの数）

撮影ログの最後の `MEGA_OCCLUSION pass1=<n> pass2_tested=<n> pass2_drawn=<n> occluded=<n>`（開発ビルドだけ。30フレームに1回）。「pass1」は1パス目（前のフレームで見えたクラスタ）で描いた数、「pass2_tested」は基本の判定（視錐台・法線のコーン・LOD）を通って2パス目で HZB の判定を受けたクラスタの数、「pass2_drawn」は2パス目で新たに描いた数。「occluded」は HZB で遮蔽と判定した数で、**1パス目で描いたクラスタも判定し直して数える**ので、描画を省いた数ではない（`cluster_cull.comp` の2パス目）。実際に描画を省いた数は `pass2_tested − pass1 − pass2_drawn`（1パス目で描いたクラスタと2パス目で新たに描いたクラスタを除いた残り）で、下の表では別の列にした。

| 視点 | pass1 | pass2_tested | pass2_drawn | occluded（HZB の判定数。最大） | 描画を省いた数 | 省いた割合（省いた数 / pass2_tested） |
|---|---|---|---|---|---|---|
| 既定 | 541 | 574 | 6 | 30（36） | 27 | 5% |
| 近接 | 3692 | 4307 | 36 | 596（602） | 579 | 13% |
| 低角度 | 1752 | 1865 | 7 | 110（122） | 106 | 6% |
| occ-sphere（球が岩を隠す） | 62 | 542 | 0 | 481（481） | 480 | 89% |
| occ-cottage（小屋が球と岩を隠す） | 30 | 572 | 0 | 542（545） | 542 | 95% |
| occ-cottage-edge（小屋の端が球の一部を隠す） | 546 | 570 | 5 | 22（24） | 19 | 3% |

- 値は太陽の高度・昼夜で変わらない（影は別のシェーダーで描くので、このパスの判定の対象外）。
- 隠し合う視点では、小屋の陰で 542 クラスタ（95%）、球の陰で 480 クラスタ（89%）の描画を省く。受入れの「小屋の陰のクラスタが省かれる」を満たす。同じ視点で あり・なしが画素まで一致するので（上の表）、省いたクラスタは見えない部分だった。
- 「occluded」と「描画を省いた数」の差は、1パス目で描いて HZB でも遮蔽と判定されたクラスタ（既定 3・近接 17・低角度 4・occ-sphere 1・occ-cottage 0・occ-cottage-edge 3）。2パス目の判定は1パス目の深度の HZB なので、1パス目で描いたクラスタが他のクラスタに隠れると、描いたうえで遮蔽と数える。
- 静止カメラでも `pass2_drawn` が 0〜36 残るのは、TAA のジッタで境界のクラスタの判定が揺れるため（そのクラスタは次のフレームの1パス目から描く）。

### GPU 時間

`-GpuTimingFrames 400`・RelWithDebInfo・中央値（ms）、窓は各 340 フレーム。`MegaGeometryPass` は2パスと HZB の構築を含む。「なし → あり」。

| 視点 | FrameGPU 中央値 | FrameGPU p95 | MegaGeometryPass | LightingPass |
|---|---|---|---|---|
| 既定 | 2.867 → 2.977 | 3.294 → 3.556 | 0.135 → 0.223 | 0.759 → 0.772 |
| 近接 | 3.988 → 4.086 | 4.563 → 4.683 | 1.870 → 1.955 | 0.624 → 0.628 |
| 低角度 | 2.907 → 2.976 | 3.641 → 3.708 | 0.567 → 0.644 | 0.573 → 0.573 |
| occ-sphere | 2.574 → 2.493 | 4.653 → 4.814 | 0.135 → 0.208 | 0.574 → 0.565 |
| occ-cottage | 2.819 → 2.892 | 5.698 → 3.180 | 0.207 → 0.293 | 0.637 → 0.626 |
| occ-cottage-edge | 2.382 → 2.592 | 4.970 → 3.038 | 0.145 → 0.245 | 0.511 → 0.561 |

- クラスタが少ない視点（既定・低角度・隠し合う視点）では、HZB の構築と2回の判定で `MegaGeometryPass` が 0.07〜0.10 ms 増える。FrameGPU の中央値の変化は -0.08〜+0.21 ms で、いずれも 16.6 ms の予算の 1.3% 以下。
- 近接（クラスタが 4307）でも、省いた割合が 14% なので `MegaGeometryPass` は 1.870 → 1.955 とほぼ同じ。VTG3-OCCLUSION-ORBIT の計測（`.harness/runs/20261005-014448/verify-VTG3-OCCLUSION-ORBIT-8-gpu-summary.txt`）では同じ視点が 2.049 → 1.855 と減っており、近接の増減は約 ±0.2 ms の計測のばらつきの内で、遮蔽による減少は今の起動画面の規模では測れない。
- 隠し合う視点（クラスタの 89〜95% を省く）でも、省いたクラスタの描画の負荷が元々小さいので FrameGPU は変わらない（-0.08〜+0.21 ms）。**段3の単体では GPU 時間の削減は測定できず、起動画面の規模では HZB の構築の約 0.08 ms 分がわずかに増える**。段3の受入れは見た目と省いたクラスタの数で、時間の削減は求めていない。

### 旋回の連続フレームの所見

20度/秒の旋回（`-OrbitDegreesPerSecond 20`。決定的な撮影では最初のヨーが固定刻みで決まるので、フレーム番号が同じなら同じ画像）の、遮蔽あり・なしの画素比較。
- 段3の VTG3-OCCLUSION-ORBIT（`.harness/runs/20261005-014448/verify-VTG3-OCCLUSION-ORBIT-4`・`-5`・`-10`・`-11`）: 目標フレームを +1・+2 ずらした3本の撮影を合わせて、球・小屋のどちらも f3〜f111 の全フレーム（欠番なし）で あり・なしを比べた。球は全フレーム 100 dB・最大差 0。小屋は最小 96.637 dB（f108）・最大差 1・不一致は最大 4.2e-5（約39画素）で、すべて 60 dB 以上。
- 画像を開いた確認: 球の旋回の f54〜f69（16枚連続）で、岩が毎フレーム少しずつ球の陰から出て大きくなり、飛び・ちらつき・欠けが無い。小屋の旋回の f54〜f69 も毎フレーム滑らかに回り、欠け・ちらつきがない。1フレームの遅れがあれば現れる あり・なしの差は、岩の出現前後を含めどのフレームにも無い。
- 今回（全インスタンスが消えて戻る場合の見えたビットの破棄を直した後のコードで）: 同じ旋回を球・小屋とも あり・なしで撮り直した（f3,6,…,111,120 の38枚。`-14-orbit-sphere.txt`・`-14-orbit-cottage.txt`）。球・小屋とも 38 枚すべて 100 dB・最大差 0・不一致 0 で、within_limits=True。小屋で以前の撮影に出ていた ±1 の差（最小 96.637 dB）は、今回は出ていない（同じ深度の境界での先着が実行ごとに変わると推定している。確かめていない）。

### 起動画面の撮影の所見

「あり」の撮影の PNG を開いて確かめた。
- `VTG3-ACCEPT/default-sun45.png`: 小屋・金色の球の列・石畳の球・岩・材質の帯・空・太陽が欠けなく出ている。穴・消失・ちらつきは見えない。
- `VTG3-ACCEPT-occ/occ-cottage.png`: 小屋が球と岩を隠す視点。屋根・煙突・窓・柵・階段が欠けなく出ている。
- `VTG3-ACCEPT-night/near-night.png`: 夜の近接。石畳の球が欠けなく出ている。球の表面の赤い点は、遮蔽なしの撮影と画素まで一致する（100 dB）ので遮蔽による欠けではなく、この視点の既存の描画。
- 全視点で白飛び・黒つぶれの画素率は 0。

### 段3の受入れの判定

- 「小屋の陰のクラスタが省かれ（数を記録）」: 満たす。occ-cottage で 542/572 クラスタ（95%）、occ-sphere で 480/542（89%）を省く。既定・近接・低角度でも 27・579・106 クラスタの描画を省く。
- 「過剰カリング（穴・消失）が撮影で出ない」: 満たす。昼・夜 18 視点と旋回 76 枚（38枚 × 球・小屋）の あり・なしの比較は、全て 99.537 dB 以上（以前の小屋の旋回の撮影を含めた最小は 96.637 dB）で目安の 60 dB を上回り、画像でも穴・欠け・ちらつきは見えない。golden 不変。

### 既知の限界

- **GPU 時間の削減は測れない**: 上の GPU 時間の節のとおり、起動画面の規模ではクラスタが少なく、省いても時間は減らない。HZB の構築と2回の判定で `MegaGeometryPass` が約 0.07〜0.10 ms 増える。クラスタの数が多い場面（段4以降のクラスタの階層・段6のビジビリティバッファ）で改めて測る。
- **判定は保守的**: 隠れていると言えるのは、遮蔽物が対象の投影矩形より HZB の texel の粒度（ミップ m で 2^(m+1) 画素）ほど大きいときだけ。境目の視点（occ-cottage-edge）では 570 のうち 19 しか省かない。完全性より健全性（穴を出さない）を優先した結果。
- **あり・なしの差が ±1〜3 出うる**: 低角度・昼45° が 99.537 dB（最大差 3、約 6 画素）、以前の小屋の旋回が最小 96.637 dB（最大差 1、約 39 画素）。同じ深度の境界での描画順の違いと推定しているが、確かめていない。穴・欠けではない。
- **影は対象外**: CSM・点光源の影は `ShadowMapPass` が別のシェーダーで描くので、カメラの視点の深度による遮蔽の判定を掛けていない（掛けると影のキャスターが欠ける）。影の描画のクラスタの数は減らない。
- **2パスにならない条件**: `--mega-occlusion=off`・HZB が作れない・描く範囲が深度の全体と一致しない・深度範囲が 0〜1 でないとき（`MEGA_OCCLUSION_OFF reason=...` を1回出す）は従来の1回の判定で描く。深度範囲が 0〜1 でない経路の GPU の撮影は無く、契約テスト（`RenderGraphCompileTest`）だけで確かめている。
- **見えたビットの寿命**: 追加・再追加・メッシュの差し替え・コンポーネントの作り直し・全インスタンスが消えて戻る場合は、見えたビットを捨てて全部を2パス目で描く（最初の1フレームは遮蔽の利益が無い）。これらは `RenderGraphCompileTest` の記録の並びで確かめ、実機では全インスタンスが消えて戻る撮影を試していない。
- **`HiZPyramidPass`（RenderGraph のパス）は製品の経路で使っていない**: 2パスの途中で `HiZPyramid` を直接呼んで作る（render pass の途中にコンピュートを挟めないため）。パスの形はテスト（`HiZPyramidVulkanTest`・`RenderGraphCompileTest`）のためだけに残っている。
- **開発機での実測だけ**: RTX 4080 で確かめた。ほかの GPU・ドライバでの深度のコピー・HZB の構築の時間は未測定（計画 7 のリスク）。
- **`MEGA_OCCLUSION` の数は開発ビルド専用**: 省いたクラスタの数は開発ビルド（RelWithDebInfo の撮影）でだけ数えて出す。Release にはデバッグ機能を入れない方針に従う。
