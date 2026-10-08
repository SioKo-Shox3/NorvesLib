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

## 段4（LODの階層の焼き込み）

判定日: 2026-10-05。ブランチ `feature/vtg-stage4-lod-bake`（コミット `2b69af04` の上）。段4の受入れ（計画 5）は、起動画面に高ポリの資産が並び、距離で段が変わっても割れ目が出ないこと。

撮影はすべて RelWithDebInfo の Game（1280×720、TAA・RTGI 有効の起動画面の既定）を `-Deterministic`（`--capture-deterministic`。同じコードの2回の撮影が一致する）で撮った。
「クック済み」は既定（岩・小屋・Poly Haven の岩 3 点・大きな球を NVMESH v1 の階層つきで、材質を BC の VT で読む）。
「glTF・実行時の生成」は `-ModelSource gltf -BigSphereSource runtime`（岩・小屋を glTF の実行時の経路で、大きな球を従来の実行時の生成で読む）。この経路は地面の外周のスキャン資産を置かないので（`Rendering3DTestRoutine.cpp` は既定のクック済みの経路のときだけ読む）、比べる側のクック済みは `-ScanProps Off`（スキャン資産なし）で撮り直した。
証拠は `.harness/runs/20261005-084356/verify-VTG4-ACCEPT-<n>-*.txt`、撮影の出力は `.harness/runs/startup-capture/VTG4-ACCEPT*/`（`metrics.json`・PNG・各視点の `*.Game.log`）。

### 結果の一覧

| 項目 | 検査 | 結果 |
|---|---|---|
| 関係ターゲットの Debug ビルド | `cmake --build build --config Debug --target AssetCook CookedMeshTest MegaGeometryResourcesTest RayTracingSceneSnapshotTest RenderGraphCompileTest RenderingGoldenImageTest -- /m:1` | BUILD_EXIT_CODE=0（`-1-build.txt`） |
| 関係する ctest（8本） | `CookedMeshTest`・`AssetCookMeshSmoke`・`AssetCookMeshSimplifySmoke`・`MegaGeometryResourcesTest`・`RayTracingSceneSnapshotTest`・`RenderGraphCompileTest`・`RenderingGoldenIndoorVulkanTest`・`RenderingGoldenOutdoorVulkanTest` | 8/8 passed（`-2-ctest.txt`） |
| golden | 上の `RenderingGoldenIndoorVulkanTest`・`RenderingGoldenOutdoorVulkanTest` | 2本とも pass。`git diff main...HEAD --name-only` に golden の基準画像・閾値の変更なし（検証シーンは CSM のまま、段4は基準画像を動かさない）ので再承認なし |
| 朝10°・昼45°・夕3° × 既定・近接・低角度 | `Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG4-ACCEPT -Configuration RelWithDebInfo -Deterministic -SunElevations 10,45,3` | 9枚、result=pass（`-3-capture-day.txt`）。白飛び・黒つぶれの画素率は全視点 0 |
| 夜 × 既定・近接・低角度 | `... -OutDir .harness/runs/startup-capture/VTG4-ACCEPT-night ... -Deterministic -Night` | 3枚、result=pass（`-4-capture-night.txt`）。白飛び・黒つぶれの画素率は 0 |
| 比べる撮影（スキャン資産なしのクック済み） | 同じ引数に `-ScanProps Off`。出力先 `VTG4-ACCEPT-noprops`・`-noprops-night` | 12視点とも pass（`-11`・`-12`） |
| 比べる撮影（glTF・実行時の生成） | 同じ引数に `-ModelSource gltf -BigSphereSource runtime`。出力先 `VTG4-ACCEPT-legacy`・`-legacy-night`。差の出どころを分けるため、`-ModelSource gltf` だけ（`-gltfonly`・`-gltfonly-night`）と `-BigSphereSource runtime` だけ（`-sphereonly`・`-sphereonly-night`）も撮った | すべて撮影は pass（`-5`〜`-10`）。PSNR は下の表（`-9`・`-10`・`-13`〜`-16`） |
| 距離を変える撮影 | `-ScanPropViews -ViewNames scan-d08,scan-d13,scan-d18,scan-d28,scan-d38,scan-d56,scan-d78`（資産との距離 約 8〜78 m）。資産あり・`-ScanProps Off`・資産ありの2回目 | 7視点とも pass（`-17`・`-18`・`-19`） |

焼き込みの性質のテスト（`CookedMeshTest`・`AssetCookMeshSimplifySmoke`・`AssetCookMeshSmoke`。内容は `Docs/Architecture/NVMESHv1.md` の「試験」）: v1 の書き出し・読み込みの往復と壊れた入力の拒否、誤差が子から親へ単調、親の境界球が子を包む、誤差のしきい値を変えて切った各メッシュで穴・非多様体の辺が 0、フォールバックの三角形数が全体の 1/64 以上 1/16 以下（下限の上書きを除く）、継ぎ目だらけの球で許容モードが使われること。
実資産（岩）のクックは `AssetCookMeshSmoke` が読み直して同じ検査を掛ける。`MegaGeometryResourcesTest`・`RayTracingSceneSnapshotTest` は、焼き込み済みの階層のフォールバックの範囲を影・RT が使うこと、壊れたフォールバック 4 種を何も作らずに拒否することを確かめる。

### 見た目（PSNR）

同じ視点の 1280×720 RGB の PSNR（dB）。目安は 40 dB 以上（VTG4-COOK-STARTUP-MODELS・VTG4-BIG-SPHERE-COOK の完了条件）。
スクリプトの下限（`-DeterministicPsnrLimit` 既定 45）は「同じコードの2回」用なので、この比較では 30 を与えた（`-DeterministicMeanLuminanceLimit` は 1）。
同じコードの2回は 100 dB で一致するので、差はすべて経路の違い（階層の段の選び方・頂点の位置）による。
「総合」はクック済み（スキャン資産なし）対 glTF・実行時の生成、「モデルだけ」は岩・小屋だけの経路の違い、「球だけ」は大きな球だけの経路の違い（スキャン資産はどちらも置く）。

| 視点 | 総合 | モデルだけ（岩・小屋） | 球だけ |
|---|---|---|---|
| 既定・朝10° | 39.339 | 41.340 | 43.538 |
| 既定・昼45° | 38.830 | 39.483 | 47.021 |
| 既定・夕3° | 40.645 | 43.019 | 44.248 |
| 既定・夜 | 41.947 | 47.359 | 43.310 |
| 近接・朝10° | 36.607 | 49.977 | 36.773 |
| 近接・昼45° | 37.071 | 46.124 | 37.586 |
| 近接・夕3° | 37.893 | 51.987 | 38.023 |
| 近接・夜 | 43.559 | 52.992 | 44.090 |
| 低角度・朝10° | 42.202 | 44.369 | 46.077 |
| 低角度・昼45° | 41.434 | 42.815 | 46.810 |
| 低角度・夕3° | 46.094 | 48.174 | 50.026 |
| 低角度・夜 | 40.941 | 43.384 | 44.312 |

- 12視点の総合の最小は近接・朝10° の 36.607 dB で、目安の 40 dB を下回る視点は既定・朝10° の 39.339、既定・昼45° の 38.830、近接の朝・昼・夕の 36.607〜37.893 の5視点。残りの7視点は 40.645 dB 以上。
- 近接の 40 dB 未満は、大きな球の経路の違いが原因（球だけが 36.773〜38.023 dB、モデルだけは 46.124〜51.987 dB）。VTG4-BIG-SPHERE-COOK の記録と同じ（階層の段の選び方が約 2 dB、影・RT の形の違いが約 0.5 dB）。
- 既定・昼45° の 40 dB 未満は、岩・小屋の経路の違いが原因（モデルだけが 39.483 dB、球だけは 47.021 dB）。VTG4-COOK-STARTUP-MODELS の記録（既定 39.789 dB）と同じ性質。
- 平均輝度の差は 総合で最大 0.3023（近接・朝10°）。目に見える色のずれ・明るさの違いではない。
- 総合の比較（`-13`・`-14`）の `result=fail` は、撮影時（資産あり対資産なしの `-5`・`-6`）の比較の失敗（下限 45 dB）が `metrics.json` の `failures` に残っているため。今回の比較の `deterministic_comparison` は 12視点とも `within_limits=True`。`-5`・`-6`・`-7`・`-8` は資産あり対なしの比較で、資産の有無の差が入るので PSNR には使わない。

### 起動画面の撮影の所見

PNG を開いて確かめた（`VTG4-ACCEPT/default-sun45.png`・`near-sun45.png`・`default-sun10.png`・`low-sun10.png`・`low-sun3.png`、`VTG4-ACCEPT-night/` の3枚、`VTG4-ACCEPT-legacy/near-sun45.png`）。
- 既定・昼45°: 小屋・金色の球の列・石畳の球・岩・材質の帯・空・太陽が欠けなく出ている。地面の外周の左右の奥に Poly Haven の岩（左 coast_land_rocks_03、右 sand_rocks_small_01 の群れ）が並び、天球・地面・球・岩・小屋・見本の帯は隠れていない。
- 近接・昼45°（クック済み対 glTF・実行時の生成）: 石畳の球の煉瓦の並び・影の形・金色の球・右の岩が同じに見える。穴・継ぎ目・割れ目は無く、煉瓦の面の陰影がわずかに違う（上の PSNR の差の原因）。
- 夕3°・朝10° の低角度: 石畳の奥行き・長い影・太陽・金色の球の列が欠けなく出ている。夜の3視点: 点光源の光だまり・球と岩の影が出ている。
- 夜の近接の球の暗部の小さな赤い点は、段1・段3の記録と同じ既存の描画（RTGI の雑音。`StartupSceneAcceptance.md` の既知の限界）で、階層の欠けではない。
- 全視点で白飛び・黒つぶれの画素率は 0。12視点の平均輝度は 昼 118.572・124.296・87.687（既定の朝・昼・夕）、119.667・124.181・89.855（近接）、120.916・126.361・95.453（低角度）、夜 56.888（既定）・13.762（近接）・67.585（低角度）。
- ログに `COOKED_MODEL_MISSING`・`COOKED_MODEL_TEXTURES_FAILED`・`TEXTURE_COOKED_MISSING`・`VT_FALLBACK`・`SCAN_PROP_MISSING`・`SCAN_PROP_FAILED` は出ていない（昼9視点・夜3視点の全ログで 0 件。昼の各視点で `COOKED_MODEL` ×5・`SCAN_PROP_PLACED` ×3・`cooked_big_sphere_load format_major=1`）。Vulkan の検証エラー（VUID）も 0 件。

### 距離を変える撮影（割れ目・ちらつき）

資産 coast_rocks_05 の延長線上からカメラを引いた 7 視点（`-17`。出力 `VTG4-ACCEPT-scan/`）。7 枚を並べた画像と d08 の原寸を開いた。
- d08〜d78 の全視点で、岩の割れ目・穴・欠け・面の飛びは見えない。近くはクラスタの細かい岩肌、遠くは粗い段で形（輪郭・影）が保たれる。d13 は右下に大きな岩（sand_rocks_small_01）が入り、その群れの表面にも割れ目は無い。d28 以遠は高さフォグで石畳が霞むのは起動画面の既定。
- 資産ありの2回目（`-19`。出力 `VTG4-ACCEPT-scan2/`）と比べた PSNR は d08・d13 が 100 dB（完全一致）、d18 98.398、d28 92.680、d38 100、d56 87.297、d78 100 dB（最大差 11/255、不一致の画素は d28 の 4.7e-5 が最大）。同じ視点の2回で段の選び方が揺れて画素が大きくずれることは無い（最大差は d56 の 11/255、不一致の画素は最大でも約 43 画素（d28 の 4.7e-5））。
- 1視点ごとの起動で撮った静止画の列で、1つの起動の中でカメラが動き続ける間の段の切り替わりを1フレームずつ撮ったものではない（下の既知の限界）。

### 選ばれたクラスタの数

撮影ログの最後の `MEGA_OCCLUSION pass1=<n> pass2_tested=<n> pass2_drawn=<n> occluded=<n>`（開発ビルドだけ）。描いたクラスタ数は `pass1 + pass2_drawn`。資産分は「あり」から `-ScanProps Off` の基準（同じ視点・資産なし。`-18`）を引いた数。

| 視点（資産との距離） | 描いたクラスタ（資産あり） | 基準（資産なし） | 資産分 |
|---|---|---|---|
| scan-d08（約 8 m） | 1949 | 369 | 1580 |
| scan-d13（約 13 m） | 2607 | 359 | 2248 |
| scan-d18（約 18 m） | 1981 | 331 | 1650 |
| scan-d28（約 28 m） | 1545 | 303 | 1242 |
| scan-d38（約 38 m） | 1308 | 280 | 1028 |
| scan-d56（約 56 m） | 1121 | 230 | 891 |
| scan-d78（約 78 m） | 918 | 192 | 726 |

- d13 まで増えるのは、引くにつれて残りの 2 点が視野へ入るため（d08 は 1 点だけ）。d13 以降は距離とともに単調に減り、d13 から d78 で資産分が約 3.1 分の 1（2248 → 726）。
- 資産の全クラスタ数は 13915（coast_rocks_05）・13128（sand_rocks_small_01）・19014（coast_land_rocks_03）・大きな球 17984・岩 1451・小屋 128 で、最も細かい段（段0）の三角形は 3 点の合計で約 260 万（771724 + 739469 + 1096655 = 2607848）。1 視点で描くのはその一部（最大 2607）。
- 起動画面の既定の視点（`-3`）の描いたクラスタ数は 既定 2672・近接 2888〜2896（pass1 2875〜2884 + pass2_drawn 12〜13）・低角度 1760。遮蔽カリングが省いた分は既定 7・近接 90〜98・低角度 37（`occluded`）。

### テクスチャの VRAM

撮影ログの `VRAM_LEDGER textures`（全テクスチャの確保量）・`VRAM_LEDGER sparse_pool`（VT のタイルを置くプール。確保は 64 MiB の1ブロック）・`VRAM_POOLS non_pool_mb`。既定の昼45° の3視点。

| | 枚数 | texture_mb | sparse_pool の使用（最後） | non_pool_mb |
|---|---|---|---|---|
| glTF・実行時の生成（`-legacy`。岩・小屋は無圧縮） | 41 | 411.5 | 既定 6.3 / 近接 26.9 / 低角度 17.5 MiB（VT の材質） | 1078 |
| クック済み（既定・スキャン資産あり） | 46 | 3.1 | 既定 16.7 / 近接 42.1 / 低角度 25.4 MiB | 689 |

- 岩・小屋の glTF の無圧縮のテクスチャ 409.6 MiB は無くなった（段2の 411.5 から 3.1 MiB へ。常駐するのは VT のミップテイルだけ）。タイルを置くプールを足しても、使用量は 3.1 + 16.7〜42.1 = 約 20〜45 MiB で、段1の起動前（ばら）の 3609.6 MiB の約 1/80〜1/180、段2の 411.5 MiB の約 1/9〜1/21。
- `non_pool_mb`（プールを除いた VRAM の使用量）は 1078 → 689 MiB（約 389 MiB 減）。スキャン資産 3 点を足した後の値で、資産のジオメトリ（頂点・インデックス・クラスタ・階層）も含む。
- 段1の既知の限界（岩・小屋の 409.6 MiB が無圧縮のまま）は、これで解けた。

### 起動の時間

ログの先頭の時刻を 0 とした秒（昼45°。1 回ずつの測定）。「球」は大きな球が使える（`cooked_big_sphere_load`）まで、「環境マップ」は `Environment source and derived IBL resources created`、「資産」は最後のクック済みの資産の作成、「撮影」は `capture_png saved`。

| 構成 | 視点 | 球 | 環境マップ | 資産 | 撮影 | 撮影のフレーム |
|---|---|---|---|---|---|---|
| クック済み（スキャン資産あり） | 既定 / 近接 / 低角度 | 0.96 / 0.94 / 0.93 | 9.10 / 8.46 / 8.49 | 11.07 / 10.40 / 10.42 | 14.02 / 13.22 / 12.78 | 178 / 164 / 147 |
| クック済み（スキャン資産なし） | 同 | 1.04 / 1.03 / 1.09 | 7.68 / 7.39 / 7.52 | 9.84 / 9.30 / 9.46 | 11.62 / 11.40 / 11.03 | 133 / 158 / 147 |
| glTF・実行時の生成 | 同 | （別スレッドで生成） | 6.59 / 6.61 / 6.58 | （glTF の読み込み） | 10.78 / 10.89 / 10.56 | 129 / 149 / 140 |

- 球の待ちは `wait_ms=0.0`（`big_sphere_build_wait completed_before_wait=1`）で、律速は環境マップと材質のテクスチャ（段4の途中の記録と同じ）。
- 撮影までは、クック済み（資産あり）が glTF・実行時の生成より約 2.2〜3.2 秒、スキャン資産なしでも約 0.5〜0.8 秒遅い。環境マップの待ちが 6.6〜9.1 秒と測定ごとにばらつくので（同じ構成の3視点でも 7.4〜9.1 秒）、差のほとんどは測定のばらつき・スキャン資産 3 点（クラスタ約 4.6 万・テクスチャ）の読み込みの分と見ている。1 回ずつの測定で、原因の切り分けはしていない。起動の時間の短縮は仮想化の計画の範囲外（VTG4-BIG-SPHERE-COOK の記録のとおり）。

### 段4の受入れの判定

- 「起動画面に高ポリの資産が並ぶ」: 満たす。地面の外周の石畳に Poly Haven の岩 3 点（約 260 万三角形）が並び（`SCAN_PROP_PLACED` ×3、PNG で確認）、既定の視点で天球・地面・球・岩・小屋・見本の帯は隠れていない。岩・小屋・大きな球も NVMESH v1 の階層つきで読む。
- 「距離で段が変わっても割れ目が出ない」: 満たす。約 8〜78 m の 7 視点と、朝・昼・夕・夜の12視点の PNG に割れ目・穴・欠けは無く、選ばれたクラスタ数は距離とともに単調に減る（d13 から d78 で約 3.1 分の 1）。同じ視点の2回は 87.297 dB 以上で一致する。
- 全体の VRAM: テクスチャは 411.5 → 3.1 MiB、golden 不変、関係する ctest 8/8 pass。

### 既知の限界

- **PSNR が目安の 40 dB を下回る視点がある**: 総合で 5 視点（近接・朝・昼・夕の 36.607〜37.893、既定・朝10° の 39.339、既定・昼45° の 38.830）。近接は大きな球の階層の段の選び方（既定 1 画素の LOD 許容）と影・RT の形、既定の昼は岩・小屋の階層の違いによる。見た目では区別がつかない（PNG で確認）。`megageometry.frag` の法線補正の段の扱いの不一致が残るかは `paths:` の外で未確認（VTG4-BIG-SPHERE-COOK からの既知の限界）。
- **距離の連続撮影は静止画の列**: 7 視点を別々の起動で撮った。カメラが動き続ける1つの起動の中で、段が切り替わる瞬間を1フレームずつ撮ったものではない（旋回の連続フレームは段3の遮蔽の確認だけ）。段の切り替わりのちらつきは、同じ視点の2回の一致（87.297 dB 以上）と静止画の割れ目の無さから見ているが、動画での確認は未実施。
- **glTF の経路はスキャン資産を持たない**: glTF・実行時の生成との比較は、クック済みを `-ScanProps Off` で撮り直した基準と比べている。スキャン資産そのものには glTF の基準が無い（約 260 万三角形を実行時に読む経路が無い）。
- **起動の時間はクック済みのほうが遅い**: 上の表のとおり約 0.5〜3.2 秒。環境マップの待ちのばらつきが大きく、原因（スキャン資産の読み込みか測定のばらつきか）は切り分けていない。
- **GPU 時間は測っていない**: 段4の受入れは見た目・クラスタ数・VRAM。階層の選択による GPU 時間の削減は段6以降（ビジビリティバッファ）で測る。
- **`MEGA_OCCLUSION` の数は開発ビルド専用**（段3と同じ。Release にはデバッグ機能を入れない方針）。
- **開発機での実測だけ**: RTX 4080 で確かめた。ほかの GPU・ドライバでの撮影は未実施。

## 段5（ジオメトリのページのストリーミング）

判定日: 2026-10-05。ブランチ `feature/vtg-stage5-geometry-streaming`（コミット `6a8e5b6a` の上）。段5の受入れ（計画 5）は、予算の上限で負荷モードが溢れずに描けること。

撮影はすべて RelWithDebInfo の Game（1280×720、TAA・RTGI 有効の起動画面の既定）を `-Deterministic`（`--capture-deterministic`）で撮った。GPU 時間の計測（`-GpuTimingFrames`）は実時間の計測なので `-Deterministic` ではない。
「ストリーミングあり」は既定（`--geometry-streaming` 既定。メッシュをページに分け、根のページだけを常駐させ、細かい段のページを要求から読む）、「全常駐」は `-GeometryStreaming Off`（`--geometry-streaming=off`。段4までと同じ、全ページを最初から常駐させる）。
証拠は `.harness/runs/20261005-175023/verify-VTG5-ACCEPT-<n>-*.txt`（起動画面の撮影・全常駐との比較・ビルド・ctest）と、負荷モードの `verify-VTG5-STRESS-GEOMETRY-<n>-*.txt`。撮影の出力は `.harness/runs/startup-capture/VTG5-ACCEPT*/`（`metrics.json`・PNG・各視点の `*.Game.log`）と `VTG5-STRESS-GEOMETRY-*/`。
負荷モードの撮影（VTG5-STRESS-GEOMETRY）の後、ストリーマ・シェーダー・Game のコードは変わっていない（`git log` で `6a8e5b6a` は `PROGRESS.md`・`TASKS.md` だけ）ので、同じコードの測定として並べる。

### 結果の一覧

| 項目 | 検査 | 結果 |
|---|---|---|
| 関係ターゲットの Debug ビルド | `cmake --build build --config Debug --target RenderResourcesDomainContractTest RHITextureUpdateVulkanTest CookedMeshTest MegaGeometryResourcesTest RayTracingSceneSnapshotTest RenderGraphCompileTest RenderingGoldenImageTest -- /m:1` | BUILD_EXIT_CODE=0（`-1-build.txt`） |
| 関係する ctest（9本） | `GeometryPoolAllocatorTest`・`GpuUploadRingVulkanTest`・`GeometryPageStreamerTest`・`CookedMeshTest`・`MegaGeometryResourcesTest`・`RayTracingSceneSnapshotTest`・`RenderGraphCompileTest`・`RenderingGoldenIndoorVulkanTest`・`RenderingGoldenOutdoorVulkanTest` | 9/9 passed（`-2-ctest.txt`） |
| golden | 上の `RenderingGoldenIndoorVulkanTest`・`RenderingGoldenOutdoorVulkanTest` | 2本とも pass。`git diff main...HEAD --name-only` に golden の基準画像・閾値の変更なし（検証シーンは CSM のまま、段5は基準画像を動かさない）ので再承認なし |
| RelWithDebInfo の Game のビルド | `cmake --build build --config RelWithDebInfo --target Game -- /m:1` | BUILD_EXIT_CODE=0（`-3-build-rwdi.txt`） |
| 朝10°・昼45°・夕3° × 既定・近接・低角度（ストリーミングあり） | `Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG5-ACCEPT -Configuration RelWithDebInfo -Deterministic -SunElevations 10,45,3` | 9枚、result=pass（`-4-capture-day.txt`）。白飛び・黒つぶれの画素率は全視点 0 |
| 夜 × 既定・近接・低角度（ストリーミングあり） | `... -OutDir .harness/runs/startup-capture/VTG5-ACCEPT-night ... -Deterministic -Night` | 3枚、result=pass（`-5-capture-night.txt`）。白飛び・黒つぶれの画素率は 0 |
| 全常駐の撮影と PSNR | 同じ引数に `-GeometryStreaming Off -CompareDeterministicWith <ストリーミングありの出力先>`。出力先 `VTG5-ACCEPT-off`・`VTG5-ACCEPT-off-night` | 12視点とも pass、`within_limits=True`（`-6`・`-7`）。PSNR は下の表 |
| 負荷モード（`--stress-geometry=300`） | `-StressGeometry` で 300 個を並べ、`--vram-budget-mb` 6500・1100 | 溢れず完走（下の節。証拠は `verify-VTG5-STRESS-GEOMETRY-3`〜`-7`） |

### 見た目（PSNR）

同じ視点の 1280×720 RGB の PSNR（dB）。ストリーミングあり対 全常駐。スクリプトの下限（`-DeterministicPsnrLimit` 既定 45）で比べ、12視点とも超えた。平均輝度の差は最大 0.0002（上限 0.1）。

| 視点 | 朝10° | 昼45° | 夕3° | 夜 |
|---|---|---|---|---|
| 既定 | 100 | 100 | 100 | 100 |
| 近接 | 64.003 | 61.286 | 69.665 | 78.361 |
| 低角度 | 83.305 | 100 | 100 | 100 |

- 100 dB は最大差 1/255 以内（不一致の画素は 0〜8E-06）。近接の差は、移行前の比較から記録している TAA・RTGI の揺れの水準で、最大差は 21/255（昼45°）。
- 平均輝度は段4の記録と同じ（昼 118.572・124.296・87.687 / 119.667・124.181・89.855 / 120.916・126.361・95.453、夜 56.888・13.762・67.585）。ストリーミングで画が変わっていない。
- 段4の golden（検証シーン）は変わっていない（基準画像の変更なし、2本とも pass）。

### 起動画面の撮影の所見

PNG を開いて確かめた（`VTG5-ACCEPT/default-sun45.png`・`near-sun45.png`、`VTG5-ACCEPT-night/default-night.png`）。
- 既定・昼45°: 小屋・金色の球の列・石畳の球・岩・材質の帯・空・太陽が欠けなく出ている。地面の外周の左右の奥のスキャン資産の岩も出ている。天球・地面・球・岩は隠れていない。
- 近接・昼45°: 石畳の球の煉瓦の並び・影が欠け・継ぎ目なく出ている。
- 夜・既定: 点光源の光だまり・球と岩の影が出ている。
- ログに `GEOMETRY_PAGES ... failed=0`、Vulkan の検証エラー（VUID）、`COOKED_MODEL_MISSING`・`SCAN_PROP_FAILED` は出ていない（昼9視点・夜3視点の全ログで 0 件）。

### ジオメトリの量

撮影ログの `VRAM_LEDGER geometry_pool`（プールの使用量）・`GEOMETRY_PAGES`・`VRAM_POOLS`。起動画面の既定の枠（`geometry_target_mb`=3649）の下の値。朝・昼・夕・夜で同じ（光の条件はジオメトリの量に効かない）。

| 構成 | プールの塊 | 使用量（MB） | 常駐ページ | `resident_mb` | 追い出し | 失敗 |
|---|---|---|---|---|---|---|
| 全常駐（段4の経路） | 2 塊 / 容量 512 MB | 274.6（全視点） | （全ページ） | — | 0 | — |
| ストリーミングあり・既定 | 1 塊 / 容量 256 MB | 54.3 | 316 | 34.05 | 0 | 0 |
| 同・近接 | 同 | 68.9 | 446 | 48.63 | 0 | 0 |
| 同・低角度 | 同 | 39.5 | 178 | 19.21 | 0 | 0 |

- ストリーミングで使用量は全常駐の約 1/5（既定）・約 1/4（近接）・約 1/7（低角度）になる。プールの確保（容量）は 512 → 256 MB で、塊が 2 つから 1 つになった。使用量（プールの確保済みの区画の合計）は `resident_mb` より大きい。差は根のページなど常駐のままの区画と区画の端数と見ているが、内訳は確かめていない。
- 既定の枠は広いので追い出しは起きない（0）。追い出しは枠を絞ったときに確かめる（次の節）。

### 負荷モード（`--stress-geometry=300`）

300 個（岩・小屋・大きな球・スキャン資産）が地面の外側へ格子状に並ぶ（`STRESS_GEOMETRY_PLACED`）。全常駐のジオメトリの量は `geometry_used_mb`=274 MB。`--vram-budget-mb 1100` の目標 `geometry_target_mb`=71 MB は全常駐の約 1/3.9、`--vram-budget-mb 6500`（8GB 級）の目標は 1421 MB。

| 予算 | 撮影 | geometry_target_mb | geometry_used_mb | 追い出し（`geometry_evicted_pages`） | 常駐ページ | 失敗 |
|---|---|---|---|---|---|---|
| 指定なし・全常駐（`VRAM_POOLS cap_mb`=15280） | 3 視点 | 3616 | 274（全常駐） | 0 | — | — |
| 6500 MB（8GB 級） | default / low / top | 1421 | 54 / 67 / 32 | 0 / 0 / 0 | 319 / 436 / 118 | 0 |
| 1100 MB | default / low / top（静止） | 71 | 54 / 67 / 32 | 0 / 0 / 0 | 319 / 436 / 118 | 0 |
| 1100 MB・旋回 20°/秒 | default / low / top | 71 | 54 / 71 / 32 | 0 / 33 / 0 | 326 / 465 / 122 | 0 |

- どの予算でも `geometry_used_mb` ≦ `geometry_target_mb` で溢れない。1100 MB の低い視点の旋回だけ作業集合が目標に達して追い出しが起き、撮影は完走した。追い出しは撮影の最後まで続いており、収束は未確認（`geometry_evicted_pages` は最後の予算の照会時点 33、最終のページ統計 `GEOMETRY_PAGES` は 36、その時点も `uploading=2 reading=1`）。静止画の視点は作業集合が目標に収まるので追い出しは起きない。
- 追い出し中の画像（`VTG5-STRESS-GEOMETRY-b1100-orbit/low-orbit-f75.png`）と 8GB 級の上から見た画像（`VTG5-STRESS-GEOMETRY-b6500/top.png`）を開いた。穴・割れ目・欠けは無い。追い出し後の岩は一部がやや粗い段の輪郭になる。上から見た画は 300 個の格子全体が出ている。
- 全常駐との画素比較（PSNR、いずれも 45 dB 以上）: 静止 default 67.2 / low 82.9 / top 69.1 dB（最大差 20 / 4 / 18）、旋回 9 枚は 63.5〜66.1 dB（最大差 10〜24）。撮影した 9 枚（視点ごとに 60・75・90 フレーム）では欠けを認めず、差は TAA・RTGI の揺れの水準。撮影していないフレームを含む連続フレームのちらつきは未確認。8GB 級は全常駐と平均輝度が同じ（121.845 / 123.211 / 125.014）。
- 絞った予算の別の記録: `--vram-budget-mb 900`（目標 54 MB）の起動画面の撮影は 3 視点とも完了し、追い出しは default 1・near 130（目標が 62 → 54 へ下がった一度の分）・low 0 で、以後は増えない。

### GPU 時間と CPU の記録の時間

`-GpuTimingFrames 300`（窓 240 フレーム）、RelWithDebInfo、中央値（ms）。`MegaGeometryPass` の CPU の記録は `mega_record_cpu`（トレースの `MegaGeometryPass.RecordFrameCommand`）。予算は 16.6 ms。

| 構成 | 視点 | フレーム全体の GPU | `MegaGeometryPass` の GPU | `MegaGeometryPass` の CPU の記録 | CPU のフレーム | 予算 |
|---|---|---|---|---|---|---|
| 起動画面（ストリーミングあり） | default / low | 2.771 / 2.603 | 0.305 / 0.267 | 0.123 / 0.114 | 10.611 / 10.593 | 内（超過 0） |
| 負荷モード 1100 MB | default / low / top | 3.672 / 3.415 / 2.849 | 0.844 / 0.744 / 0.952 | 0.156 / 0.152 / 0.153 | 10.666 / 10.657 / 10.704 | 内（超過 0） |
| 負荷モード 6500 MB | default / low / top | 3.608 / 3.312 / 2.834 | 0.837 / 0.733 / 0.951 | 0.157 / 0.155 / 0.159 | 10.668 / 10.652 / 10.702 | 内（超過 0） |

- 300 個でも CPU の記録は 0.16 ms 前後で、起動画面の約 1.3 倍に収まる（個数に比例しない）。フレームの CPU は 10.6〜10.7 ms で、16.6 ms の予算内。
- `MegaGeometryPass` の GPU は 300 個で 0.73〜0.95 ms（起動画面の 0.27〜0.31 ms の約 2.8〜3.1 倍）。予算を絞った 1100 MB と 8GB 級の 6500 MB で同じ値で、枠の違いは GPU 時間に効かない。段3の既定の視点の `MegaGeometryPass` 0.223 ms、まとめたカリング後（VTG5-BATCHED-CULL-PERF）の 0.300 ms と並べて、起動画面の 0.305 ms は同水準。
- 使用の印（描いたクラスタの自分のページの要求）で足した処理は、描いたクラスタごとの `atomicExchange` 1 回と重複しなければ列への 1 書き込みで、上の値に含まれる。

### 段5の受入れの判定

- 「予算の上限で負荷モードが溢れずに描ける」: 満たす。`--vram-budget-mb` 6500（8GB 級）・1100（全常駐の約 1/3.9 の枠）のどちらも `geometry_used_mb` が目標以下で、失敗ページは 0、300 個の負荷モードを穴・割れ目なく完走する。1100 MB の低い視点の旋回では追い出しが起き、撮影の最後まで続く（最後の予算の照会時点で 33 ページ、最終のページ統計で 36 ページ。収束は未確認）。
- 起動画面（絶対規則 7）: 満たす。天球・地面・球・岩・小屋が欠けなく出て、全常駐との PSNR は 12 視点とも 45 dB 以上（既定・低角度は大半が 100 dB）。平均輝度は段4と同じ。
- ジオメトリの量: 全常駐の 274.6 MB に対し、起動画面では 39.5〜68.9 MB（約 1/7〜1/4）。
- 関係する ctest 9/9 pass、golden 不変。

### 既知の限界

- **PSNR が 100 dB でない近接の視点**: 近接は 61.3〜78.4 dB（最大差 21/255）。移行前の比較から記録している TAA・RTGI の揺れの水準だが、ストリーミングの影響との切り分け（同じ構成の2回の比較）は今回は未実施。
- **追い出しの確認は旋回の 1 視点**: 静止画の視点は作業集合が目標に収まって追い出しが起きない。追い出しが起きたのは 1100 MB の低い視点の旋回（最後の予算の照会時点で 33 ページ、最終のページ統計で 36 ページ。撮影の終わりも追い出しが続き、収束は未確認）と 900 MB の起動画面（default 1・near 130）。追い出し中の画素の再現性は未確認。
- **入れ替わりが続く負荷は待ちに数えない**: 目標が作業集合に足りず、追い出しの間隔が 120 Update 以内で 300 Update 以上続くと、`HasPendingWork()` が false を返す（読み込みは止めない）。落ち着くのを待っても落ち着かない撮影を終わらせるための判定で、追い出しが長く続きながら最終的に収束する場合も収束前に待ちを打ち切る。入れ替わりの最中に撮るので、常駐ページが撮影のフレームごとに変わりうる。
- **ストリーミングしないメッシュの使用の印**: ページを複数持つが全て常駐するメッシュの使用の印は `InvalidRequests` に数えられる（統計のみ）。
- **全常駐との GPU 時間の比較は未実施**: ページのストリーミングは VRAM の削減が目的で、GPU 時間は 1100 MB と 6500 MB（どちらもストリーミングあり）の比較だけ。全常駐（`-GeometryStreaming Off`）の 300 個の GPU 時間は測っていない。ほかの GPU・ドライバでの値も未測定。
- **開発機での実測だけ**: RTX 4080 で確かめた。ほかの GPU・ドライバ・VRAM が少ない GPU での撮影は未実施。
- **`GEOMETRY_PAGES`・`VRAM_POOLS` などの記録は開発ビルド専用**（Release にはデバッグ機能を入れない方針）。

## 段6（ビジビリティバッファ）

判定日: 2026-10-06。ブランチ `feature/vtg-stage6-visibility-buffer`（コミット `2e15a905` の上、main `33bd19b8` から分岐）。段6の受入れ（計画 5・6）は、起動画面と golden が移行前と同等であること（差は記録して判断）。

撮影はすべて RelWithDebInfo の Game（1280×720、TAA・RTGI 有効の起動画面の既定）を `-Deterministic`（`--capture-deterministic`）で撮った。GPU 時間の計測（`-GpuTimingFrames 300`、窓 240 フレーム）は実時間の計測なので `-Deterministic` ではない。
「ビジビリティバッファ（on）」は既定（`--visibility-buffer=on`。不透明すべてが ID と深度を描き、材質の解決パスが GBuffer を書く）、「予備の経路（off）」は `-VisibilityBuffer Off`（`--visibility-buffer=off`。段5までと同じ GBufferPass・MegaGeometryPass の GBuffer へのラスタ）。
証拠は `.harness/runs/20261006-111432/verify-VTG6-ACCEPT-<n>-*.txt`（ビルド・ctest・撮影）と `ev-<n>-*.txt`（予備の経路の撮影・画素の比較・GPU 時間）。撮影の出力は `.harness/runs/startup-capture/VTG6-ACCEPT*/`（`metrics.json`・PNG・各視点の `*.Game.log`）。

### 結果の一覧

| 項目 | 検査 | 結果 |
|---|---|---|
| 関係ターゲットの Debug ビルド | `cmake --build build --config Debug --target Game RenderGraphCompileTest RHITextureUpdateVulkanTest RenderingGoldenImageTest RenderingVelocityVulkanTest -- /m:1` | BUILD_EXIT_CODE=0（`verify-…-1-build.txt`） |
| 関係する ctest（8本） | `IntegerAttachmentVulkanTest`・`VisibilityBufferEncodingTest`・`ComputeSkinningVulkanTest`・`MaterialTileClassifyVulkanTest`・`RenderGraphCompileTest`・`RenderingVelocitySkinnedVulkanTest`・`RenderingGoldenIndoorVulkanTest`・`RenderingGoldenOutdoorVulkanTest` | 8/8 passed（`-2-ctest.txt`。GPU のテスト 6 本の実行時間は計 97.75 秒） |
| golden | 上の Indoor・Outdoor（既定 = ビジビリティバッファ）と、予備の経路の `RenderingGoldenIndoorGBufferFallbackVulkanTest`・`RenderingGoldenOutdoorGBufferFallbackVulkanTest`（今回は走らせず、`VTG6-OFF-PATH-TESTS` の記録 5/5 passed を参照） | Indoor・Outdoor とも pass。基準画像は今回動かしていない（下の「golden」） |
| RelWithDebInfo の Game のビルド | `cmake --build build --config RelWithDebInfo --target Game -- /m:1` | BUILD_EXIT_CODE=0（`-3-rwdi-build.txt`） |
| 朝10°・昼45°・夕3° × 既定・近接・低角度（on） | `Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG6-ACCEPT -Configuration RelWithDebInfo -Deterministic -SunElevations 10,45,3` | 9枚、result=pass（`-4-capture-day.txt`）。白飛び・黒つぶれの画素率は全視点 0 |
| 夜 × 既定・近接・低角度（on） | `... -OutDir .harness/runs/startup-capture/VTG6-ACCEPT-night ... -Deterministic -Night` | 3枚、result=pass（`-5-capture-night.txt`）。白飛び・黒つぶれは 0 |
| 予備の経路（off）の撮影と PSNR | 同じ引数に `-VisibilityBuffer Off`（出力先 `VTG6-ACCEPT-off`・`VTG6-ACCEPT-off-night`）。on との比較は `-CompareOnly -CompareDeterministicWith <on の出力先>` | 12視点とも pass、`within_limits=True`（`ev-6`〜`ev-9`）。PSNR は下の表 |
| GPU 時間 | `-GpuTimingFrames 300 -VisibilityBuffer On／Off`、起動画面と `--stress-mega-instances=300` | 下の節（`ev-10`・`ev-11`）。off の 300 個だけ、スクリプトの VT の上限の判定で fail（下の節） |

### 見た目（PSNR）

同じ視点の 1280×720 RGB の PSNR（dB）。on 対 off（予備の経路）。スクリプトの下限（`-DeterministicPsnrLimit` 既定 45）で比べ、12視点とも超えた。平均輝度の差は最大 0.0332（上限 0.1。夜・低角度）。

| 視点 | 朝10° | 昼45° | 夕3° | 夜 |
|---|---|---|---|---|
| 既定 | 52.669 | 56.801 | 54.606 | 53.230 |
| 近接 | 48.863 | 53.184 | 50.302 | 53.679 |
| 低角度 | 52.097 | 54.388 | 55.567 | 49.350 |

最大チャンネル差（/255）は既定 23・17・25・56（朝・昼・夕・夜）、近接 49・23・28・60、低角度 30・29・49・64。不一致の画素は 19〜33%（夜の近接は 9.8%）。

差の出どころ（段6の記録 `VTG6-DEFAULT-ON-SWITCH` と同じ）: on は材質の解決が三角形の 3 頂点と画素の光線の交点から重心座標と解析的な微分を求め、接線の基底を三角形の辺と UV から作る。off は頂点シェーダーが補間した接線と画面の微分を使う。この違いが `textureGrad` の勾配・法線の向きの 1〜数階調の差になり、手前の石畳の目地・球の縁に集まる。夜は最大差が 56〜64 と昼より大きい（差の画像では、夜・既定は石畳の道・小屋の屋根と壁・右の地面に、夜・近接は石の球の表面と手前の石畳に、夜・低角度は手前の石畳の目地・岩の周り・小屋の屋根の縁と壁に点として散らばり、面や形の欠けではない）。平均輝度は on と off で同じ（朝・昼・夕・夜で差 0.0332 以下）。

- 段5の受入れの撮影（`VTG5-ACCEPT`・`VTG5-ACCEPT-night`）との間には、NVIDIA のドライバの更新（591.86 → 610.88）と、off の `gbuffer.frag`・`megageometry.frag` の勾配を明示した標本（`VTG6-VT-LOD-UNIFORM`）が挟まるので、off も段6で変わっている。同じ視点の PSNR は on 対段5が 48.85〜56.81 dB（最大チャンネル差 17〜70）、off 対段5が 55.02〜62.75 dB（同 13〜52）で、どちらも下限 45 dB を超える。材質の解決による差は、同じドライバの on と off の比較（上の表）で測った。
- 予備の経路（off）は段5の記録と近い平均輝度（昼 124.296・126.361 など）で、`VISBUFFER_RESOLVE_TILES`・`VISBUFFER_FALLBACK` は 0（ID のラスタ・解決が走らない）。

### 起動画面の撮影の所見

PNG を開いて確かめた（`VTG6-ACCEPT/default-sun45.png`・`near-sun10.png`、`VTG6-ACCEPT-night/default-night.png`）。
- 既定・昼45°: 小屋・金色の球の列・石畳の球・岩・材質の帯・空・発光の球が欠けなく出ている。外周の左右の奥のスキャン資産の岩も出ている。天球・地面・球・岩は隠れていない。
- 近接・朝10°: 石畳の球の煉瓦の並び・足元の石畳の目地・影が欠け・継ぎ目なく出ている。
- 夜・既定: 点光源の光だまり・球と岩の影が出ている。
- ログに Vulkan の検証エラー（`VUID`・`VK_ERROR`）、`VISBUFFER_FALLBACK`、`COOKED_MODEL_MISSING`・`SCAN_PROP_FAILED` は 0 件（on の昼9視点・夜3視点、off 12視点、GPU 計測の on の全ログ）。
- 解決の記録（on の最後の `VISBUFFER_RESOLVE_TILES`）: 既定・低角度は材質 24・dispatch 24、近接は材質 17・dispatch 17（材質ごとのタイルへの間接 dispatch）。

### VT のフィードバックと常駐量

材質の解決パスが VT のフィードバック（4×4 の画素のうちフレームごとに 1 画素の `textureQueryLod` から欲しいミップ・タイルを求めて GPU のバッファへ書く）を書く。要求の数そのものは撮影のログに出ない。要求の結果を、撮影の最後の `VT_STREAMER`（常駐タイル数）と `vt_used_mb`（`VRAM_LEDGER sparse_pool`）で比べる。どの視点も `wanted=0`・`failed=0`・`bind_failures=0`・追い出し 0（要求を全部さばいて落ち着いた後の値）。朝・昼・夕・夜で同じ。撮影の VT の上限は 64 MB（`vt_used_limit_mb`）。

| 視点 | on: 常駐タイル | on: 使用量（MB） | off: 常駐タイル | off: 使用量（MB） | on / off |
|---|---|---|---|---|---|
| 既定 | 166 | 13 | 225〜228 | 16〜17 | 0.73 |
| 近接 | 450 | 30 | 683 | 45 | 0.66 |
| 低角度 | 348 | 24 | 366 | 25 | 0.95 |

- 段5の撮影（`VTG5-ACCEPT`、段6の前）の値は off と同じ（既定 16・近接 45・低角度 25 MB）。段6で on にすると常駐は少なめになるが、ぼけは出ていない（鮮明さを測るラプラシアンの分散は `VTG6-DEFAULT-ON-SWITCH` の記録で on と off が同じ）。on の常駐が少ないのは、材質の解決が画素の光線から求めた微分で標本の位置を決めるので、off（頂点シェーダーが補間した値と画面の微分）より不要な細かいミップを欲しがらないため、と見ている（原因の切り分けは未実施）。

### GPU 時間

`-GpuTimingFrames 300`（窓 240 フレーム）、RelWithDebInfo、中央値（ms）。予算は 16.6 ms。off と on のラスタの行の名前が違うので、**パスの合計**（on = `VisibilityRasterPass` ＋ `MaterialTileClassifyPass` ＋ `VisibilityResolvePass` ＋ `GBufferPass`、off = `MegaGeometry` ＋ `GBufferPass`）で比べる。`MegaGeometry` の行は on では `VisibilityRasterPass` と同じ値（ID のラスタの描画を含む）で、off と意味が違う。個々のパスの値は隣のパスとの間でずれる（既定で on の `LightingPass` 0.668 に対し off 0.853、`SSAOPass` は on 0.286 に対し off 0.091）ので、判断はフレーム全体と合計で行う。

起動画面:

| 視点 | フレーム GPU（on / off） | 差 | ID のラスタ | 分類 | 解決 | 合計（on / off） | `MegaGeometryPass` の CPU の記録（on / off） | CPU のフレーム（on / off） |
|---|---|---|---|---|---|---|---|---|
| 既定 | 2.412 / 2.214 | +0.198 | 0.195 | 0.161 | 0.413 | 0.778 / 0.600 | 0.174 / 0.136 | 5.466 / 5.284 |
| 近接 | 2.311 / 2.122 | +0.189 | 0.201 | 0.159 | 0.335 | 0.704 / 0.578 | 0.165 / 0.130 | 5.307 / 5.484 |
| 低角度 | 2.215 / 1.879 | +0.336 | 0.172 | 0.157 | 0.446 | 0.784 / 0.467 | 0.171 / 0.134 | 5.331 / 5.484 |

負荷モード（`--stress-mega-instances=300`）:

| 視点 | フレーム GPU（on / off） | 比 | ID のラスタ（on）／ `MegaGeometry`（off） | 分類 | 解決 | 合計（on / off） | CPU のフレーム（on / off） |
|---|---|---|---|---|---|---|---|
| 既定 | 11.201 / 6.608 | 1.70 | 6.606 / 2.365 | 0.239 | 0.625 | 7.479 / 2.753 | 16.990 / 12.328 |
| 近接 | 9.192 / 5.988 | 1.54 | 4.903 / 1.924 | 0.179 | 0.446 | 5.537 / 2.236 | 15.140 / 11.881 |
| 低角度 | 8.732 / 5.597 | 1.56 | 4.580 / 1.761 | 0.169 | 0.515 | 5.273 / 2.060 | 14.728 / 11.460 |

- 起動画面では on が off より 0.19〜0.34 ms 長い（約 +9〜18%）。分類（0.16 ms 前後）と解決（0.34〜0.45 ms）が足され、GBuffer の描画（0.29〜0.39 ms）と MegaGeometry の描画が ID のラスタ 1 本にまとまって（0.17〜0.20 ms）減る分では賄えない。全視点で 16.6 ms の予算の内（`within_budget=True`、超過 0）。分類の GPU は前の記録（0.26〜0.29 ms）より小さい。
- 300 個の負荷モードでは on の ID のラスタが off の `MegaGeometry` の 2.55〜2.79 倍で、フレーム全体が 1.54〜1.70 倍（+3.1〜+4.6 ms）になる。この比は両経路で同じカリング（`MegaGeometryCull1`・`MegaGeometryCull2` の計 0.68〜0.84 ms。`MegaGeometry` の区間の内側で測る）を含み、カリングを除くと 3.4〜3.8 倍（on の区間は手続きメッシュの塊の描画と描画の記録の compute も含み、off の区間には含まない。描画だけの内訳は段6では測っていない）。on の最大は 11.2 ms（既定の視点）で、GPU の予算 16.6 ms の内。**on の既定の視点は CPU のフレームの中央値が 16.99 ms で、16.6 ms を超える**（スクリプトの `within_budget` は GPU のフレーム時間だけで判定している）。off は 12.328 ms。on と off の CPU のフレームの差（+4.66・+3.26・+3.27 ms）は GPU のフレームの差（+4.59・+3.20・+3.14 ms）とほぼ同じで、CPU のフレームの増加は GPU を待つ時間の増加による。
- ID のラスタの区間が重い理由は未調査。`geometryShader` の段は挟んでいない（FS の `gl_PrimitiveID` のための機能だけ）。候補は、描画の記録の compute が積まれたコマンドの数によらず横 `min(ceil(区間の容量/64), 65535)` × 縦 `区間の数 × パスの数` のワークグループを立てること（負荷モードのコマンドの枠は約 109〜161 万、区間は 24）、手続きメッシュの塊の描画、FS の `gl_PrimitiveID`・深度の比較 LessOrEqual・別の render pass での深度の Load（いずれも未検証）。段7（ソフトウェアラスタ）は小さい三角形を計算シェーダーへ移すので、そこで詰められる見込み（原因が未調査なので確かではない）。
- `MegaGeometryPass` の CPU の記録は on が約 0.03〜0.04 ms 長い（既定 0.174 / 0.136）が、300 個でも 0.21〜0.24 ms で個数に比例しない。
- 負荷モード・off の撮影は `result=fail`（`near: VT の常駐量が上限を超えた（vt_used_mb_max=84 / 上限 64 MB）`）で終わった。GPU 時間は 3 視点とも取れており、上の値はその値。on は同じ 300 個で 近接 58 MB・低角度 42 MB・既定 20 MB で上限の内（off は 84・50・27 MB）。上限 64 MB は起動画面の常駐を守る値で、300 個の負荷モードを想定していない。on で減っている点は、上の「VT のフィードバックと常駐量」と同じ向き。

### golden

- 基準画像・閾値の変更（段6のコミット）: Outdoor を 2 回再承認した。`80ec7662` は NVIDIA のドライバの更新（591.86 → 610.88）の後のシェーダーコンパイルによる丸めの違いで、差は 1 画素（(193,197) の R 223→222、最大差 1/255、平均輝度は変わらない）。VT の LOD の問い合わせを移す前のシェーダー（`def594d5`）でも同じ画素・同じ値で落ちたので、LOD の問い合わせを移す変更による差ではない。`83134c09` は既定の on への切り替えによる差で、球の縁の 57 画素・最大 3/255、off では旧 baseline と完全に一致、Indoor は on でも一致。予備の経路用の基準画像 `IndoorGBufferFallback.png`・`OutdoorGBufferFallback.png`（`OutdoorGBufferFallback.png` は `83134c09` の前の版の Outdoor、すなわち `80ec7662` で再承認したものと同じ内容）と、基準画像の選び方（`RenderingGoldenImageTest.cpp` の `BaselineFileName`）は `52b399e7` で足した。閾値（`VisualThresholds.tsv`）は変えていない。手順は `GoldenBaselines.md`、2 回の再承認の根拠は `R1Acceptance.md`。
- 今回（段6の受入れ）は基準画像・閾値を動かしていない。既定の経路の Indoor・Outdoor の golden は 2 本とも pass。

### 段6の受入れの判定

- 「起動画面と golden が移行前と同等（差は記録して判断）」: 満たす。起動画面は朝・昼・夕・夜の 12 視点で欠けなく出て、予備の経路（off）との比較はスクリプトの判定（PSNR の下限 45 dB・平均輝度の差の上限 0.1）で 12 視点とも pass（PSNR 48.86〜56.80 dB、平均輝度の差 0.0332 以下）。最大チャンネル差は 17〜64/255 で、起動画面は golden の閾値（`VisualThresholds.tsv`）では判定していない。差は材質の解決の解析的な微分・接線の基底による手前の石畳・球の縁の差で、平均輝度は変わらない。段5の受入れの撮影との PSNR も 48.85 dB 以上。golden（検証シーン）は別に Indoor・Outdoor とも pass（Outdoor は再承認済み）。
- 起動画面（絶対規則 7）: 満たす。天球・地面・球・岩・小屋・見本の帯・金色の球の反射・発光の球が見え、Vulkan の検証エラー・予備への戻り・資産の欠けは 0。
- 遮蔽カリング: on の `MEGA_OCCLUSION`（pass1 / pass2_tested / pass2_drawn / occluded）は off と完全に一致（既定 2672 / 2679 / 0 / 7、近接 2888 / 2966 / 10 / 69、低角度 1755 / 1793 / 3 / 41）。
- VT: on は常駐が off の 0.66〜0.95 倍、`wanted=0`・`failed=0`。
- 関係する ctest 8/8 pass、golden 2 本 pass。
- GPU 時間: 受入れの条件にはないが、on は起動画面で off より +0.19〜+0.34 ms、300 個で +3.1〜+4.6 ms（1.54〜1.70 倍）。予算（16.6 ms）の内（CPU のフレームの 300 個・既定の視点を除く）。下の限界に記録する。

### 既知の限界

- **NVIDIA のドライバの更新で分岐後の `textureQueryLod` が外れるようになった**: 2026-10-06 0:17 のドライバの更新（591.86 → 610.88）で、分岐の後の `textureQueryLod` が外れ、既定の視点の VT の常駐タイルが約 42 倍（約 9500 対 225〜228）に増えた（`VTG6-VT-LOD-UNIFORM` で直した。POM の直後は一様な位置とみなす。退行の守りは撮影の VT の常駐の上限）。段6の前の撮影・基準画像は旧ドライバのもの（Outdoor の golden は `80ec7662` で再承認した。その差はシェーダーコンパイルの丸めによる 1 画素で、`textureQueryLod` の件とは別）。
- **実行時の同期（バリア）を検証レイヤー付きで確かめていない**: GPU のテストは `bEnableValidation = false`、撮影は RelWithDebInfo。撮影のログの `VUID`・`VK_ERROR` が 0 というのは、検証レイヤーを有効にした結果ではない。
- **カメラが動くときの遮蔽の見え始め（旋回の撮影）を段6では撮っていない**: 段3の受入れの旋回はラスタの経路（GBuffer）の記録。
- **on の VT の常駐は off より少なめ**（0.73・0.66・0.95 倍）だが画像はぼけない。原因の切り分けは未実施。
- **ワイヤーフレームの表示**は材質の境目の線の色が on と off で入れ替わる（深度の比較 `LessOrEqual` と `Less` の違い。塗りの起動画面では 0〜2 階調）。
- **予備の経路（off）の検査**: Indoor・Outdoor の golden と速度の検査で守る。`geometryShader` の無い装置での実機の確認は無い。
- **材質の解決の dispatch と分類の一覧**: 解決は材質ごとのタイルの間接 dispatch（起動画面の定常で 17〜24 回）。分類は GPU が起動画面で 0.16 ms 前後・300 個で 0.17〜0.24 ms（段6の受入れの測定。`VTG6-RESOLVE-TILE-DISPATCH` の測定では 0.258〜0.285 ms）、VRAM は約 5 MB。分類の一覧は最悪の大きさ（4K で約 33 MB、1 ビュー・1 フレーム枠あたり）で確保する（`VTG6-DEFAULT-ON-TILE-VRAM`。溢れて物が欠けるのを避ける）。
- **計算スキニングのパイプラインだけが作れない装置**では、スキニングの無い場面でも予備になる（保守的）。
- **on の GPU 時間は off より長い**: 起動画面で +0.19〜+0.34 ms、300 個で +3.1〜+4.6 ms（ID のラスタの区間が off の `MegaGeometry` の 2.55〜2.79 倍、カリングを除くと 3.4〜3.8 倍。on の区間は描画の記録の compute・手続きメッシュの塊の描画を含む）。300 個・既定の視点では CPU のフレームの中央値が 16.99 ms で 16.6 ms を超える（GPU を待つ時間の増加による）。原因は未調査。段7（ソフトウェアラスタ）で詰められる見込み。
- **on の GPU 時間の行 `MegaGeometry` は `VisibilityRasterPass` と同じ値**（ID のラスタの描画を含む）: 両者を足さない。off との比較は上のパスの合計で行う。
- **RelWithDebInfo の `RenderGraphCompileTest` は NDEBUG で落ちる**（副作用を assert の中に置いているため。既存。`TEST-ASSERT-NO-DIALOG` の記録）。Debug では 8/8 pass。
- **負荷モード（300 個）・off の撮影は VT の上限の判定で fail**（84 MB / 64 MB）。上限は起動画面向けの値で、結果は GPU 時間の記録としては有効。
- **負荷モード（300 個）の GPU 時間は、影の描画を一部省いた状態の値**: on・off とも `DynamicUniformAllocator` の `Out of slots (1024/1024)` が視点ごとに 343〜1102 回出て、既定の視点では CSM の UBO が足りないための `MegaGeometry` の影の描画の省略が 343 回、点光源の影の 1 面あたりの上限（8）を超えた省略が 1032 回ある（`VTG6-ACCEPT-gpu-stress-On`・`-Off` の `*.Game.log`）。段5の `VTG5-BATCHED-CULL-PERF-stress300` にも同じ `Out of slots` が 404 回あり、段6の退行ではない。起動画面の撮影では出ない。
- **段の外の後回し**（`TASKS.md` の backlog）: `PT-NONUNIFORM-SAMPLER`（パストレーサーの一様でない添字の印）、`PT-STARTUP-GEOMETRY`（パストレーサーの撮影の形状の違い）、`TEST-ASSERT-NO-DIALOG`（Debug のテストの assert の対話窓）。
- **開発機での実測だけ**: RTX 4080（ドライバ 610.88）で確かめた。ほかの GPU・ドライバでの撮影・計測は未実施。
- **`VISBUFFER_*`・`VRAM_LEDGER`・`MEGA_OCCLUSION` などの記録は開発ビルド専用**（Release にはデバッグ機能を入れない方針）。

## 段7（ソフトウェアラスタ）

判定日: 2026-10-06。ブランチ `feature/vtg-stage7-sw-raster`（main `0a0c395f` から分岐）。段7の受入れ（計画 5・7）は、小さい三角形の多い視点で GPU 時間が下がること（RelWithDebInfo で測る）。

段7で入れたもの:
- ビジビリティバッファのラスタの GPU 区間の内訳（`VisRasterChunks`・`VisRasterRecords`・`MegaGeometryDraw1/2` など）。
- 描画の記録の compute の間接 dispatch と、記録の表の GPU 専用メモリへの移動（段6の負荷モードの増加分の主因。表がホスト可視のメモリにあり、書き込みが PCIe 越しになっていた）。
- 64bit アトミック（`shaderBufferInt64Atomics`）の能力の公開、深度＋ID の 64bit のバッファと、ID・深度への合流のパス（HZB の前と 2 パス目の後）。
- カリングでの小さいクラスタの振り分け（画面上の半径がしきい値以下で近平面と交わらない MegaGeometry のクラスタ）と、計算シェーダーのソフトウェアラスタ（1 ワークグループ = 1 クラスタ、1/256 画素の固定小数点の辺の関数、top-left 規則、64bit の `atomicMin`）。ソフトに回したクラスタのハードのコマンドは空振りにする。
- 既定: `--sw-raster=on`、しきい値 `--sw-raster-max-px=32`。ハードのラスタは今の深度の比較で ID と深度を書く形のまま（早期 Z が効く）。

### 結果の一覧

| 確かめたこと | 方法 | 結果 |
|---|---|---|
| 関係する ctest（11 本） | RenderGraphCompileTest・SkinnedRenderPathContractTest・速度 5 本・golden の Indoor・Outdoor とその予備の経路 | 11/11 passed（`.harness/runs/vtg7-defon-clean/02-ctest.txt`）。基準画像・閾値は段7で動かしていない |
| ソフトとハードの被覆 | `VisibilityResolveVulkanTest`（同じ三角形をハードとソフトで描く。水平な辺・回転した view・画面の右端・走査の上限の境界の場面を含む） | 被覆の違い 0 画素、ID の違いは同じ深度の交線の数画素、深度の差は最大 1e-5 前後 |
| 起動画面（既定 = on） | 朝10°・昼45°・夕3° × 既定・近接・低角度、夜（`-Deterministic`） | 欠けなし（下の所見）。on 同士は PSNR 94.1 dB 以上、off とは 61.3 dB 以上 |
| 検証レイヤー | Debug の Game、起動画面と負荷モード 300 個（各 3 視点） | `vulkan_validation.error_count` 0 |
| GPU 時間 | RelWithDebInfo、`-GpuTimingFrames 300` | 下の節 |

### GPU 時間

負荷モード 300 個（`--stress-mega-instances=300`、昼45°）の FrameGPU の中央値（ms、窓 240 フレーム）。

| 視点 | 段6の受入れ（ビジビリティバッファ on） | 段6の受入れ（GBuffer の予備の経路） | 段7・ソフトなし（`--sw-raster=off`） | 段7・既定（on、32 画素） |
|---|---|---|---|---|
| 既定 | 11.201 | 6.608 | 6.794〜6.821 | 6.128〜6.202 |
| 近接 | 9.192 | 5.988 | 6.000〜6.008 | 5.683〜5.739 |
| 低角度 | 8.732 | 5.597 | 5.838〜5.915 | 5.471〜5.728 |

区間ごとの値（`VisibilityRasterPass`・`MegaGeometryDraw1/2`・`MegaGeometryCull1/2`・`VisRasterSw1/2`・合流・`VisRasterRecords1`）と、しきい値 8・16・64 画素の値は PROGRESS.md の VTG7-SW-THRESHOLD の表（`.harness/runs/startup-capture/VTG7-SW-THRESHOLD-*`）。段6の予備の経路（GBuffer へのラスタ）と比べると、段7の既定は既定・近接で 0.25〜0.48 ms 短く、低角度は同じ水準（±0.13 ms）。段7のソフトなしは段6の予備の経路より 0.01〜0.32 ms 長い。

- 段6からの減り（約 5.0・3.5・3.0〜3.3 ms）の大半は、描画の記録の表を GPU 専用メモリへ移したこと（`VisRasterRecords` 3.6 → 0.02 ms）による。ソフトウェアラスタ（32 画素）は、それに加えて off より 0.11〜0.69 ms 速い（同じ条件の撮り直しは 2 回ずつ。ハードの描画 `MegaGeometryDraw1` は既定の視点で 1.66 → 0.49 ms、ソフトのラスタ `VisRasterSw1` は 0.40 ms）。
- on の固定の費用（合流 0.02〜0.03 ms、1 パス目の記録 0.02〜0.03 ms、カリングの増え）は約 0.06〜0.10 ms。しきい値 8 画素ではソフトに回るクラスタが少なく（描くクラスタの 1% 未満）、この固定の費用の分だけ off より遅い。64 画素は負荷モードで 32 画素と同等で、起動画面で `VisibilityRasterPass` が 0.10〜0.29 ms 増える。
- 起動画面（3 視点）は on と off で同じ水準（off 3.052・3.693・3.371、on 3.062・3.697・3.275 ms）。起動画面は小さいクラスタが少なく、ハードの描画がもともと 0.04〜0.07 ms なので、ソフトで減る分が無い。起動画面の絶対値は段6の受入れの記録（2.2〜2.4 ms）より高いが、段7の変更の前（VTG7-RASTER-TIMING、区間を足しただけの版）にすでに 3.05 ms で、段7の変更による差ではない（測った日の構成の違い）。
- 同じ条件の撮り直しの揺れは、off・on 8 画素で 0.08 ms 以内、on 32 画素で最大 0.26 ms（負荷モードの低角度は FrameGPU が約 0.37 ms 高い区間がまとまって出ることがある）。

### 起動画面の撮影の所見

PNG を開いて確かめた（`.harness/runs/startup-capture/VTG7-SW-DEFAULT-ON-clean-on/default-sun45.png`・`near-sun10.png`・`low-sun3.png`、`-clean-night-on/default-night.png`）。天球・地面・球・岩・小屋・見本の帯・金色の球・発光の球が欠けなく見え、穴・継ぎ目は無い。既定の視点ではクラスタ 1850 個（描くものの約 69%）がソフトで描かれる。

- 遮蔽カリングの統計（`MEGA_OCCLUSION`）は on と off で完全に一致（既定 2672 / 2679 / 0 / 7、近接 2888 / 2966 / 10 / 69、低角度 1755 / 1793 / 3 / 41）。VT の常駐は on と off で同じ（13・30・24 MB）。
- しきい値 32 画素の FXAA の撮影（TAA の履歴の揺れが無い）で、on と off の差は起動画面の既定で 4438 画素（最大差 30。8 以上は 39 画素）、負荷モードの既定で 54508 画素（約 6%。8 以上は 731 画素、最大 82）。差の画像では、差はソフトで描かれる遠くの岩の上の点で、面の欠け・穴・継ぎ目は無い。ソフトの書き込みを止めた Albedo との差で足あとを取ると、8 以上の差の画素は 6 視点の合計 826 画素のうち 822 画素が足あと +3 画素の中にあり、外の差は空を含む画面全体に散る ±1 が大半（2 以上は起動画面の既定で 18 画素・最大 6、負荷モードの既定で 108 画素・最大 13）。
- on と off の画素の差は、ソフトが描いた画素の深度の小さなずれ（ハードとの補間の違い）が RTGI を通って足あとの近くに出るものと、それが自動露出の入力をわずかに動かして画面全体に ±1 を出すもの（VTG7-SW-FXAA-COMPARE で、露出を丸めると遠い ±1 が消えることを確かめた）。被覆・色・法線の違いは無い（しきい値 8 画素の FXAA の比較で Albedo・Normal は完全一致。ソフトとハードの被覆の一致は上の GPU のテスト）。

### golden

段7では基準画像・閾値を動かしていない。既定（on、しきい値 32 画素）で Indoor・Outdoor の golden とその予備の経路の 2 本が、基準画像のまま合格する。

### 段7の受入れの判定

- 「小さい三角形の多い視点で GPU 時間が下がる」: 満たす。小さいクラスタの多い負荷モード 300 個で、既定（on）は段6の受入れより 3.0〜5.1 ms、ソフトなし（段7の off）より 0.11〜0.69 ms 短い。起動画面は off と同じ水準。
- 起動画面（絶対規則 7）: 満たす。朝・昼・夕・夜で欠けなく見え、Vulkan の検証エラーは 0。
- 遮蔽カリング・VT の常駐・golden は on と off で変わらない。

### 既知の限界

- **ソフトウェアラスタの対象は MegaGeometry のクラスタだけ**: 手続きメッシュ・スキニングの塊はハードのまま。64bit アトミック（`shaderBufferInt64Atomics`）とバッファのアドレス（BDA）の無い装置、Debug の表示、ワイヤーフレーム、予備の経路（`--visibility-buffer=off`）ではソフトを使わない（`SW_RASTER_FALLBACK reason=<理由>`）。
- **ソフトの深度はハードとわずかに違う**（最大 1e-5 前後）。RTGI と自動露出を通って、on と off の画像に差が出る（TAA の起動画面で PSNR 61 dB 以上・夜の最大差 43、FXAA の負荷モードで 57 dB・最大差 82）。
- **同じ深度の勝ち方**: ソフトは同じ深度なら小さい ID が勝ち（`atomicMin`）、合流は深度の比較 LessOrEqual でハードの ID を上書きする。ハードどうしは後に描いた側が勝つ。同じ平面に重なる三角形では、on と off で見える三角形が入れ替わりうる。
- **負荷モード（300 個）の GPU 時間は、影の描画を一部省いた状態の値**: 段5・段6と同じく、`DynamicUniformAllocator` の `Out of slots (1024/1024)` で CSM の UBO が足りない `MegaGeometry` の影の描画の省略（既定の視点で 343 回）と、点光源の影の 1 面あたりの上限（8）を超えた省略（1032 回）がある（`VTG7-SW-THRESHOLD-stress-on32/default-sun45.Game.log`）。on と off で同じ条件。
- **撮影は同じ構成でも run 間で完全には一致しない**（クック済みテクスチャ × TAA × RTGI で 2〜3 の状態に分かれる。負荷モードは FXAA・RTGI 切りでも揺れる）。段7の変更によらず off の経路にもあり、原因の調査は段の外の後回し（`VTG7-DETERMINISM-SEED`）。
- **1 スレッドが走査する三角形の矩形は `max(64, ceil(2 × しきい値) + 2)` 画素四方まで**。超える三角形は描かずに数える（`SW_RASTER_OVERSIZE`。しきい値が保守的なので 0）。
- **VRAM**: 記録の表は負荷モード 300 個で約 128 MiB（`VRAM_LEDGER visbuffer_records`）、ソフトの一覧は約 8 MB（`sw_raster_list`）、64bit のバッファは 1280x720 で 7.03 MB（`visbuffer64`）。VT・ジオメトリの目標がその分減る。
- **検証レイヤーの範囲**: 撮影の検証は API・状態・スレッドの検証で、同期の検証（`validate_sync`）と GPU 上の検証は有効にしていない。
- **GPU で通していない経路**: 間接 dispatch を断られたときの直接 dispatch（記録とソフト）、描画の記録の compute の y への折り返し（約 419 万を超えるコマンド）。CPU の偽の装置のテストでだけ確かめている。ソフトの一覧の y への折り返し（65535 を超える数）は、負荷モードの既定の視点（約 12 万）で GPU を通っており、描いた数の計数は振り分けた数と一致した。
- **ID のフラグメントの `gl_PrimitiveID`**: ハードの ID のラスタの描画で約 0.66 ms（負荷モード）を使う（VTG7-RASTER-TIMING の切り分け）。段7では変えていない。
- **開発機での実測だけ**: RTX 4080（ドライバ 610.88）。

## 段8（VSM 太陽）

判定日: 2026-10-08。ブランチ `feature/vtg-stage8-vsm-sun`（main `573d7176` から分岐）。段8の受入れ（計画 5・8）は、起動画面の太陽の影が CSM 以上に細かく、ちらつかないこと。検証シーン（golden・R 系）の影は CSM のまま（計画 2）。

段8で入れたもの:
- 照明の太陽の CSM の評価を共通の include（`Common/SunShadowCsm.glsl`）へ移し、計算シェーダーからも同じ関数で読めるようにした。
- 影の測定の道具 `--shadow-probe`（Release では作らない）: 読み込み後の最初のフレームで画面の 4 画素おきの格子の空でない画素をワールドの標本点として固定し、以後の毎フレーム、見えている標本で太陽の可視度を CSM と VSM の同じ関数で求めて、前のフレームとの差（`mean_abs_delta`・`changed_ratio`・`flip_ratio`）、縁の帯の割合（`partial_ratio`、0.02 < v < 0.98）、使った texel（`mean_texel_mm`）、CSM と VSM の一致（両方が言い切る標本のうち同じ側の割合）を集計する。
- 影の方式の切り替え `--shadow-method=csm|vsm` と、太陽のクリップマップ（10 段・段 0 の幅 4 m・各段 16384² texel・ページ 128²・正射影・中心はページの格子へスナップ・ページの表の番地はトーラス）。受け手の段は、texel が画素の大きさ × 2^b 以下の最も粗い段（b = max(-0.5, 受け手を必ず含むための最小の bias)）。
- 物理ページのプール（uint32 の storage buffer、既定 5120 ページ = 320 MiB、`--vsm-pool-pages`）とソフトウェアのページの表、GPU の中での印付け（深度の各画素 → 段とページ。PCF・探索の範囲が隣のページへ及ぶときはそこにも印）・割り当て・消去。
- 「影の塊（MegaGeometry のクラスタ、手続きメッシュ・スキニングの 128 三角形以下の塊）× ページ」を 1 インスタンスとして 128×128 のビューポートへ描き、断片シェーダーが物理ページへ深度を `atomicMin` で書く描画。MegaGeometry は段ごとに GPU で cull（正射影の LOD・dirty のページの階層で省く）する。
- 照明の読み: 段の選び方は印付けと同じ。ブロッカーの探索と物理の半影（太陽の角半径、上限 0.5 m）、16 点の PCF。半径の下限は画素の大きさの半分と段の 1 texel の大きいほう（距離に対して連続で、段の切り替わりで縁の幅が跳ばない）。割り当てのないページは粗い段へ逃げる。
- 動かない物のページの持ち越し（要求されないページは 30 フレーム後に空きへ。段の中心の移動・深度の原点のスナップ・太陽の向きの変化・動いた投影物の前後の境界で無効化）。
- 影の範囲は、印付けも照明もカメラの前方への距離で判定する（視錐台の端の受け手も同じ段を選ぶ）。MegaGeometry の段ごとの cull の一覧から容量で落ちたクラスタ・展開から溢れた塊のページは、次のフレームに描き直す。同じ dispatch の中で他のスレッドがアトミックに書く語は、原子的に読む。
- 既定: Game の起動画面は VSM（`--shadow-method=csm` で戻せる）。検証アプリは CSM のまま。半透明（`forward_transparent.frag`）とボリュームは CSM を読む。
- 予算: VSM の確保量（プール・展開の一覧・cull の一覧・ページの表、合計 426 MB）を、取り分を持たない固定の取り置きとして割り振れる量から引く（`VRAM_POOLS shadow_map_pool_mb=426`。VT・ジオメトリの目標がその分減る）。

### 結果の一覧

| 確かめたこと | 方法 | 結果 |
|---|---|---|
| 関係する ctest（8 本） | RenderGraphCompileTest・VirtualShadowMapVulkanTest・VirtualShadowMapClipmapTest・VideoMemoryBudgetManagerTest・golden の Indoor・Outdoor とその予備の経路 | 8/8 passed（`.harness/runs/vtg8-accept/r3-ctest-debug.txt`）。基準画像・閾値は段8で動かしていない |
| 起動画面（既定 = VSM） | 朝10°・昼45°・夕3° × 既定・近接・低角度、夜（`-Deterministic`） | 欠けなし（下の所見） |
| 細かさ・ちらつき | `--shadow-probe` と視点の旋回（20 度/秒、400 フレーム）、既定・近接・低角度 × 太陽 45°・10° | 下の節 |
| 検証レイヤー | Debug の Game、起動画面 3 視点と負荷モード 300 個（`-Deterministic -ShadowProbe`） | `vulkan_validation.error_count`・`warning_count`・`vuid_count` 0、VSM の溢れ 0 |
| GPU 時間 | RelWithDebInfo、`-GpuTimingFrames 300` | 下の節 |

### 細かさとちらつき

標本の道具は「太陽と物が止まっていれば、ワールドに固定した点の可視度は視点が回っても変わらない」を前提にする。起動画面の大きな球は自転し、VSM だけが描く石の目地の自己影が球と一緒に動くので、自転したままでは近接の視点で本当に動く影が揺れとして数えられる。判定は球の自転を止めた run（`NORVES_STARTUP_SPHERE_SPIN=0`、`.harness/runs/startup-capture/VTG8-ACCEPT-r3-orbit-nospin`）で行い、自転したままの run（`VTG8-ACCEPT-r3-orbit`）も下に並べる。値は CSM / VSM（同じ run・同じ標本）。

球の自転を止めた run（判定に使う）:

| 視点 | texel（mm） | 縁の帯の割合 | mean_abs_delta | flip_ratio | 一致 |
|---|---|---|---|---|---|
| 既定・太陽45° | 21.103 / 11.170 | 0.01452 / 0.00802 | 0.000017 / 0.000004 | 0 / 0 | 0.99983 |
| 近接・太陽45° | 13.409 / 2.778 | 0.08307 / 0.04229 | 0.000002 / 0.000038 | 0 / 0.000005 | 0.99922 |
| 低角度・太陽45° | 13.884 / 5.254 | 0.06453 / 0.03355 | 0.000008 / 0.000041 | 0.000001 / 0.000011 | 0.99929 |
| 既定・太陽10° | 21.103 / 11.170 | 0.02761 / 0.01749 | 0.000016 / 0.000005 | 0 / 0 | 0.99995 |
| 近接・太陽10° | 13.409 / 2.778 | 0.04567 / 0.02542 | 0.000003 / 0.000017 | 0 / 0.000003 | 0.99896 |
| 低角度・太陽10° | 13.884 / 5.254 | 0.03131 / 0.01624 | 0.000002 / 0.000020 | 0 / 0.000007 | 0.99958 |

球が自転したままの run（参考）:

| 視点 | texel（mm） | 縁の帯の割合 | mean_abs_delta | flip_ratio | 一致 |
|---|---|---|---|---|---|
| 既定・太陽45° | 21.103 / 11.170 | 0.01441 / 0.00805 | 0.000037 / 0.000017 | 0.000001 / 0.000001 | 0.99983 |
| 近接・太陽45° | 13.434 / 2.819 | 0.07920 / 0.06531 | 0.001070 / 0.009867 | 0.000014 / 0.005306 | 0.97590 |
| 低角度・太陽45° | 13.884 / 5.255 | 0.06425 / 0.03789 | 0.000510 / 0.001006 | 0.000042 / 0.000072 | 0.99925 |
| 既定・太陽10° | 21.103 / 11.170 | 0.02750 / 0.01763 | 0.000037 / 0.000020 | 0 / 0 | 0.99996 |
| 近接・太陽10° | 13.434 / 2.819 | 0.04156 / 0.05321 | 0.000395 / 0.009764 | 0.000003 / 0.005897 | 0.97242 |
| 低角度・太陽10° | 13.884 / 5.255 | 0.03099 / 0.02120 | 0.000146 / 0.000827 | 0 / 0.000120 | 0.99932 |

- texel は全 12 組で VSM が CSM の 0.21〜0.53 倍。VSM の texel が CSM 以下の標本の割合（finer_ratio）は 0.9985〜0.9999。距離ごとの texel（`VSM_TEXEL`、VSM / CSM mm）: 1 m 0.977 / 12.867、5 m 3.906 / 12.867、10 m 7.813 / 12.867、20 m 15.625 / 25.273、40 m 31.25 / 94.403、80 m 62.5 / 94.403。
- 縁の帯の割合は、判定に使う 6 組すべてで VSM が CSM の 0.51〜0.63 倍。同じ縁を横切る線の明るさの変化でも、VSM の縁は CSM より細い（既定の視点の大きな球の影の縁: CSM 約 5 画素、VSM 約 4 画素）。PCF の半径の下限が画素 1 つ分のときは、CSM の texel が画素より小さい中距離で VSM の縁が太くなり、既定の視点で VSM の帯が CSM の 1.7 倍だった。下限を画素の半分にして 0.55 倍になった。
- ちらつき: 判定に使う run では、CSM も VSM も `mean_abs_delta`・`flip_ratio` が 0.0001 未満。VSM の変化は、カメラとの距離が変わって標本の段・PCF の下限が変わる分で、CSM（カスケードのテクセルのスナップ）より大きいが、1 フレームあたり 0.004% 以下。
- 自転したままの近接の視点で VSM の変化と縁の帯が大きく、一致が 0.97 台に下がるのは、VSM だけが描く石の目地の影が球と一緒に動くため（CSM は texel 13 mm と粗い LOD で目地の影を描かない）。撮影の差の画像でも、差は球の目地に沿った細い影と影の縁に集まり、VSM 側が暗い。にきび状の雑音・光の漏れは無い。

### 検証レイヤー付き Debug の実行

`.harness/runs/startup-capture/VTG8-ACCEPT-r3-validation`・`VTG8-ACCEPT-r3-validation-stress`（太陽45°、`-Deterministic -ShadowProbe`、球は自転したまま）。

| run | texel CSM / VSM（mm） | 一致 | error_count | VSM の溢れ |
|---|---|---|---|---|
| 既定 | 20.702 / 11.209 | 0.99927 | 0 | 0 |
| 近接 | 13.885 / 3.504 | 0.97897 | 0 | 0 |
| 低角度 | 13.997 / 3.035 | 0.99969 | 0 | 0 |
| 負荷 300 個（既定の視点） | 19.979 / 10.843 | 0.99993 | 0 | 0 |

近接の一致 0.97897 は、上の自転の影響と同じもの（PCF の下限が画素 1 つ分のときは 0.98267）。

### GPU 時間

RelWithDebInfo、`-GpuTimingFrames 300`、太陽45°、描いた GPU のフレーム 240 件の中央値（ms）。値の根拠は、`7a73978c`・`4a6f3927` の後の撮影の標準出力 `.harness/runs/20261008-073209/verify-VTG8-VSM-GPU-TIME-2-csm.txt`〜`-7-vsm-stress.txt`（`gpu_timing` の行。区間の値は同じ撮影の `pass_median_ms`）。出力先 `.harness/runs/startup-capture/VTG8-VSM-GPU-TIME-*` は後の再検証で撮り直されており、今の `metrics.json` は別の run の値（既定の VSM 2.722 ms など。run 間の揺れの範囲）。予算の取り置きと PCF の下限の変更は GPU の仕事を変えない。

| 視点 | CSM | VSM（持ち越しあり、既定） | 差 | VSM（持ち越しなし） | VirtualShadowMapPass（既定） |
|---|---|---|---|---|---|
| 既定 | 2.612 | 2.681 | +0.069 | 2.647 | 0.252 |
| 近接 | 2.517 | 3.231 | +0.714 | 2.964 | 1.263 |
| 低角度 | 2.459 | 2.853 | +0.394 | 2.826 | 0.586 |
| 負荷 300 個（既定） | 5.889 | 7.467 | +1.578 | 15.000 | 1.453 |

- VSM でも半透明・ボリュームのために CSM の描画が残る（`ShadowMapPass` は既定 0.176、負荷 300 個 2.186）。VSM が足すのは `VirtualShadowMapPass`。
- 近接の増加は、自転する大きな球の周りのページを毎フレーム無効化して描き直す分（`VSM_CACHE` invalidated=490、`VsmAllocate` 0.616・`VsmDraw` 0.512）。負荷 300 個の増加の大半は MegaGeometry の段ごとの cull（`VsmCullMega` 1.146）。持ち越しにより、負荷 300 個は持ち越しなしの 15.000 から 7.467 へ下がる（`VsmDraw` 7.002 → 0.166）。
- 予算 16.6 ms を超えたフレームは無い。

### VSM のページと VRAM

- `VSM_PAGES` requested（中央値）: 既定 420・近接 737・低角度 1774、負荷 300 個 420。overflow は全 run の全行で 0（`VSM_RASTER`・`VSM_MEGA_CULL` の溢れも 0）。
- 持ち越し（`VSM_CACHE` の中央値）: 既定 cached 366・rendered 54、近接 cached 246・rendered 490、低角度 cached 1558・rendered 214。
- VRAM: `vsm_pool` 320 MB（5120 ページ）、`vsm_raster` 26.6 MB、`vsm_mega_cull` 80.0 MB、`vsm_page_table` 0.6 MB（`VRAM_POOLS shadow_map_pool_mb=426`）。起動画面の VT の目標 10601 MB・ジオメトリの目標 3533 MB は、上限 15280 MB からプール以外 717 MB と VSM の 426 MB を引いた残り。

### 起動画面の撮影の所見

PNG を開いて確かめた（`.harness/runs/startup-capture/VTG8-ACCEPT-r3/default-sun45.png`・`near-sun10.png`・`low-sun3.png`、`VTG8-ACCEPT-r3-night/near-night.png`、旋回の `VTG8-ACCEPT-r3-orbit-nospin/default-sun10-orbit-f320.png`、負荷モードの `VTG8-ACCEPT-r3-validation-stress/default-sun45.png`）。天球・地面・球・岩・小屋・見本の帯・金色の球・発光の球が欠けなく見え、太陽の影にページの継ぎ目・欠け・ずれ・光の漏れは無い。夕3°の低角度は段7（CSM）の同じ視点と見分けがつかない。夜は太陽が無く、点光源の影は今までどおりキューブ（段9で VSM にする）。

### golden

段8では基準画像・閾値を動かしていない。検証アプリは CSM のままで、Indoor・Outdoor の golden とその予備の経路の 2 本が基準画像のまま合格する。

### 段8の受入れの判定

- 「CSM 以上に細かく」: 満たす。texel は全 12 組で VSM が CSM の 0.21〜0.53 倍。縁の帯の割合は、判定に使う 6 組（球の自転を止めた run）すべてで VSM が CSM の 0.51〜0.63 倍。
- 「ちらつかない」: 満たす。判定に使う 6 組すべてで CSM も VSM も `mean_abs_delta`・`flip_ratio` が 0.001 未満（同等とみなす範囲。実測は 0.0001 未満）。旋回の撮影に揺れ・継ぎ目は見えない。
- CSM との一致: 太陽45°の 3 視点で 0.9992 以上（判定に使う run）。
- 起動画面（絶対規則 7）: 満たす。朝・昼・夕・夜で欠けなく見え、Vulkan の検証エラーは 0。

### 既知の限界

- **半透明・ボリュームは CSM のまま**: 画面の深度に無い位置のページに印が付かないため。CSM の描画も残り、VSM の分だけ GPU 時間が増える（起動画面 +0.07〜+0.71 ms、負荷 300 個 +1.58 ms）。
- **負荷モード（300 個）の CSM の UBO の省略は CSM 側に残る**（`Out of slots (1024/1024)`、既定の視点で 343 回）。VSM の描画では起きない。省略は半透明・ボリュームの影に効く。点光源の影の 1 面あたりの上限（8）による省略（1032 回）は段9。
- **自転する大きな球**: VSM は球の石の目地の自己影を描くので、球の周りのページは毎フレーム描き直しになる（近接で `VsmAllocate`＋`VsmDraw` 約 1.1 ms）。ワールドに固定した点の測定では、動く影として揺れに数えられる（上の参考の表）。
- **VSM の段の数と影の距離**: 10 段（4〜2048 m）、影の距離は CSM と同じ 80 m。VSM を使えない装置（断片シェーダーの storage の書き込み・アトミック、バッファのアドレス、`DrawIndexedIndirectCount` が無い、プールが 512 ページ未満、MegaGeometry の段ごとの cull の初期化・資源の確保に失敗）では CSM で描く（`VSM_FALLBACK reason=<理由>`）。
- **VRAM**: VSM の確保量 426 MB は固定（プールの大きさは `--vsm-pool-pages`）。起動画面の低角度で要求 1774 ページ、前の既定の bias（-1）では 4735 ページ。
- **測定の前提**: 影の測定の道具は止まった物を前提にし、Release では作らない。標本は 4 画素おきの格子。検証レイヤーの範囲は API・状態・スレッドの検証で、同期の検証と GPU 上の検証は有効にしていない。
- **開発機での実測だけ**: RTX 4080（ドライバ 610.88）。
