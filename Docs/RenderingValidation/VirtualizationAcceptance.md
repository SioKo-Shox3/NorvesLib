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
