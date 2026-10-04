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
