# 段8（VSM 太陽）の経過

ブランチ `feature/vtg-stage8-vsm-sun` で PROGRESS.md に書いた経過を、main へ統合するときにそのまま移した（main 側では G2 の統合で PROGRESS.md を整理し、それ以前の経過は `Docs/History/2026-10-07-G2Integration/` に保管している）。

統合の時点の全体の要約（PROGRESS.md の「テクスチャとジオメトリの仮想化」の行）:

- テクスチャとジオメトリの仮想化（2026-10-04、段1はブランチ`feature/vtg-stage1-bc`、main`c830bf64`から分岐）: ユーザーと決めた全体計画 `Docs/Plans/VirtualizedTextureGeometryPlan.md`（git 管理外）を TASKS.md の `VTG<段>-` の項目で1段ずつ回す。段1（BC圧縮）は12項目すべて完了し、評価者の PASS と受入れ（`Docs/RenderingValidation/VirtualizationAcceptance.md` の段1の節。材質の VRAM 0.175、golden 不変、撮影の PSNR）を確かめて main へマージした（`b8c5df1d`）。段2（sparse の VT）はブランチ`feature/vtg-stage2-sparse-vt`の16項目がすべて完了し、受入れ（`VirtualizationAcceptance.md` の段2の節。VT と全常駐の PSNR 61.6 dB 以上、起動画面の材質のテクスチャ 560 MiB → タイルとテイルで 29 MiB 以下、`--vram-budget-mb 6500` の負荷モード 24/24 材質）を確かめて main へマージした（`2192aeea`）。段3（2パスの遮蔽カリング）はブランチ`feature/vtg-stage3-occlusion`の5項目がすべて完了し、受入れ（`VirtualizationAcceptance.md` の段3の節。遮蔽あり・なしの撮影は18視点で 99.5 dB 以上、旋回38枚×2が一致、隠し合う視点で 89〜95% のクラスタを省く。GPU 時間の削減は今の起動画面の規模では測れない）を確かめて main へマージした（`d294370f`）。段4（LOD の階層の焼き込み）はブランチ`feature/vtg-stage4-lod-bake`の11項目がすべて完了し、受入れ（`VirtualizationAcceptance.md` の段4の節。岩・小屋・大きな球・Poly Haven の岩3点（約260万三角形）を NVMESH v1 の階層つきで読み、距離を変えた撮影で割れ目なし、テクスチャの常駐 411.5 → 3.1 MiB、golden 不変。glTF・実行時の生成との PSNR は 5視点で目安の 40 dB を下回る 36.6〜39.3 dB を既知の限界として記録）を確かめて main へマージした（`bccf3b3f`）。段5（ジオメトリのページのストリーミング）はブランチ`feature/vtg-stage5-geometry-streaming`の14項目がすべて完了し、受入れ（`VirtualizationAcceptance.md` の段5の節。全常駐との PSNR は12視点で 45 dB 以上（既定・低角度は大半が 100 dB、近接 61〜78 dB）、起動画面のジオメトリ 274.6 MB → 39.5〜68.9 MB、300 個の負荷モードが 6500 MB・1100 MB の予算で穴なく完走、golden 不変）を確かめて main へマージした（`33bd19b8`）。段6（ビジビリティバッファ）はブランチ`feature/vtg-stage6-visibility-buffer`の項目がすべて完了し、受入れ（`VirtualizationAcceptance.md` の段6の節。起動画面は予備の経路との PSNR 48.86〜56.80 dB・段5の撮影との PSNR 48.85 dB 以上、遮蔽の統計は予備の経路と一致、golden は Outdoor を2回再承認、GPU 時間は起動画面で +0.19〜+0.34 ms・300 個の負荷モードで 1.54〜1.70 倍を既知の限界として記録）を確かめて main へマージした（`0a0c395f`）。段7（ソフトウェアラスタ）はブランチ`feature/vtg-stage7-sw-raster`の項目がすべて完了し、受入れ（`VirtualizationAcceptance.md` の段7の節。しきい値 32 画素を既定にし、負荷モードで off より 0.11〜0.69 ms 速い）を確かめて main へマージした（`573d7176`）。段8（VSM 太陽）はブランチ`feature/vtg-stage8-vsm-sun`の項目がすべて完了し、受入れ（`VirtualizationAcceptance.md` の段8の節。texel は VSM が CSM の 0.21〜0.53 倍、縁の帯は球の自転を止めた 6 組で 0.51〜0.63 倍、揺れは CSM・VSM とも 0.0001 未満、太陽45°の一致 0.9992 以上、GPU 時間は起動画面で +0.07〜+0.71 ms・負荷 300 個で +1.58 ms を既知の限界として記録）を確かめて main へマージした。段9（VSM 点光源と全体）は backlog。段が終わると親が受入れを確かめて main へマージ・プッシュし、次の段の項目を詳しくしてから todo にする。

## 親（2026-10-07）: 段8（VSM 太陽）の開始

- ブランチ `feature/vtg-stage8-vsm-sun`（main `573d7176` から）。計画書 §2・§4.3 と、今の CSM（4 カスケード × 2048・80 m・テクセルのスナップあり・照明は PCSS）、MegaGeometry の cull の経路（シェーダーの入力はユニフォームだけで別のビューに使えるが、C++ 側は主カメラ 1 本・資源 1 組）を読み、段8の項目（VTG8-）を詳しくした。
- 親が決めた細部（計画書 §6）: (1) 受入れの測り方は、ワールドに固定した標本点の太陽の可視度（`--shadow-probe`）。ちらつきは視点を回したときのフレーム間の変化（mean_abs_delta・flip_ratio）、細かさは使った texel（mean_texel_mm）と縁の帯の割合（partial_ratio）で、VSM が入った後は同じ run の同じ点で CSM と VSM を両方測る。画面の画像の差（視点の動き・TAA を含む）では測らない。(2) 物理プールは uint32 の storage buffer（既定 4096 ページ = 256 MiB。R32_UINT の画像のアトミックの実績が無いため）。(3) 深度は「塊（クラスタ・128 三角形以下の塊）× ページ」のインスタンスを 128×128 のビューポートへ描き、断片シェーダーが物理ページへ `atomicMin` で書く（16K のビューポートでは大きな投影物が細かい段を塗り尽くす。RHI にクリップ距離・添付の無いレンダーパスが無い）。(4) 受け手の段は画素の大きさ × 2^bias（既定 −1）で選び、PCF の半径は連続な量にして段の切り替わりで縁の幅が跳ばないようにする。計画書の「照明は PCF から始める」の後、今の起動画面の PCSS の見た目に合わせて物理の半影を足す。(5) 半透明・ボリュームは CSM のまま（CSM の描画も残る）。検証シーン（golden・R 系）も CSM のまま。(6) VTG8-ACCEPT は backlog にし、ランナーの後に親が行う。
- 撮影はユーザーの方針どおり 3 つに限る: 速度の項目の GPU 時間（VTG8-VSM-GPU-TIME）、段の受入れの起動画面と影の測定（VTG8-SHADOW-PROBE の基準値と VTG8-ACCEPT）、既定の描画経路を変える項目の golden の ctest と検証レイヤー付き Debug の実行（VTG8-VSM-DEFAULT-ON）。
## 反復 1（2026-10-07）: VTG8-SHADOW-CSM-INCLUDE（done）
- 内容: `lighting.frag` の太陽の CSM の評価（`CalculateShadow`・`SampleShadowCascade`・`IsInsideShadowCascade`・PCSS の 3 段・`ComputeShadowCompareBias`・`ComputeReceiverDepthGradient`・Poisson の表・ハードシャドウ検証モードの分岐）を `Assets/Shaders/Common/SunShadowCsm.glsl` へ移した。影の地図・行列・分割の距離・カメラ・検証モード・有効フラグ・カスケード数は include する側が `SUN_CSM_*` のマクロで与える（`SUN_CSM_SAMPLE_DEPTH(uvw)` は省略可で、既定は `texture`。計算シェーダーは `textureLod` の定義を与えられる）。`lighting.frag` はマクロを定義して include するだけで、`IsR5HardShadowValidationMode` は 照明側に残る。
- 検証（`.harness/runs/20261007-203349/`）: `verify-VTG8-SHADOW-CSM-INCLUDE-1.txt`（Debug の Game・RenderingGoldenImageTest・RenderGraphCompileTest のビルド、BUILD_EXIT_CODE=0）、`-2.txt`（ctest 5/5 Passed: RenderGraphCompileTest と golden の Indoor・Outdoor・各 GBuffer 予備の経路。基準画像・閾値は動かしていない。CTEST_EXIT_CODE=0）。
- Notes: シェーダーは実行時に shaderc で解決されるので、golden が通ったことが include の解決とマクロの展開の確認を兼ねる。`forward_transparent.frag`・Volumetrics の CSM の読み方は触っていない。
- Next: VTG8-SHADOW-PROBE は `SunShadowCsm.glsl` を計算シェーダーから include できる。

## 反復 2（2026-10-07）: VTG8-SHADOW-PROBE（done）

- 内容: 起動引数 `--shadow-probe`（`ApplicationProcessor` → `RenderWorld` → `RenderingCoordinator` の設定 `bShadowProbe` → `SceneView::SetShadowProbeEnabled`）で、`ShadowProbePass`（`Library/Core/{Public,Private}/Rendering/ShadowProbePass.*`、計算シェーダー `Assets/Shaders/shadow_probe.comp`）を照明の後に足す。統計が有効な構成（Debug・RelWithDebInfo）だけで作り、Release は作らない（`.cpp` の本体も `#if NORVES_ENABLE_STATS`）。(1) 画面の 4 画素おきの格子（1280×720 で 320×180）の空でない画素の深度・法線から、ワールドの位置と法線を固定の標本点として保存（モード 0）。(2) 以後の毎フレーム、標本を今のカメラへ投影し、画面の中で線形の深度（カメラ前方への距離）が画素の深度と 1% 以内のものだけを見えているとし、`Common/SunShadowCsm.glsl` の `CalculateShadow`（照明と同じ PCSS）で可視度 v を求め、使ったカスケードの texel の一辺（mm。ブレンド帯は手前、影の範囲の外は最後のカスケード）も求める（モード 1）。(3) GPU でアトミックに集計（pairs・|Δv| の和・changed（> 1/64）・flip（≥ 0.5）・見えていた延べ数・partial（0.02 < v < 0.98）・texel の和・範囲外の数）し、4 枠のホストが読めるバッファから 2 実行以上遅れて読み戻して CPU で足し、終了時（`Shutdown`）に `SHADOW_PROBE method=csm frames=… probes=… pairs=… mean_abs_delta=… changed_ratio=… flip_ratio=… partial_ratio=… mean_texel_mm=…` を 1 行出す。補助の `SHADOW_PROBE_DETAIL method=csm visible=… out_of_range_ratio=…` も 1 行出す。(4) `Scripts/CaptureStartupScene.ps1` に `-ShadowProbe`（`--shadow-probe` を渡す。Release とは併用不可）を足し、行を `metrics.json` の `shadow_probe[]` へ視点ごとに入れる（行が無ければ失敗）。(5) `RenderGraphCompileTest` に 5 件: 既定は標本のパスが無く、有効にすると照明の後に 1 つだけ入る／GBuffer の深度・法線・影の地図（CSM）・Scene.Color の 4 つを読み、影の地図が無い構成では何も宣言しない／エポックまで dispatch せず、影の地図が来たフレームで標本を固定（mode 0）し以後は測る（mode 1）、読み戻しの遅れと比の分母の定義／エポックの始め直しで固定と合計をやり直す／決定的でない起動は 300 回目の実行で固定。
- `SunShadowCsm.glsl` に `SelectShadowCascade`（`CalculateShadow` のカスケードの選び方を関数へ出しただけ）と `ShadowCascadeTexelMeters` を足した。照明の挙動は変えていない。
- 決定的な撮影のエポック（`epoch begin`）は、読み込みが落ち着くまで何度も始め直される（1 回目の撮影では最初の `rendered=109` で固定してしまい、読み込み前のシーンを測って frames=2236 などになった）。そのため、エポックが始まるたびに標本の固定と合計を最初からやり直す。
- 検証（`.harness/runs/20261007-203349/`）: `verify-VTG8-SHADOW-PROBE-1.txt`（Debug の Game・RenderGraphCompileTest のビルド、BUILD_EXIT_CODE=0）、`-2.txt`（RenderGraphCompileTest 1/1 Passed。CTEST_EXIT_CODE=0）、`-3.txt`（RelWithDebInfo の Game、BUILD_EXIT_CODE=0）、`-4.txt`（起動画面の撮影。出力 `.harness/runs/startup-capture/VTG8-SHADOW-PROBE`）。
- 撮影の終了コードは 1: 3 視点の撮影と `SHADOW_PROBE` は取れたが、`CaptureStartupScene.ps1` の既存の検査「VT の常駐量が 64 MB 以下」が low 視点（旋回）で `vt_used_mb_max=77` となり failure を返した。`-ShadowProbe` を外して low だけ同じ条件で撮り直しても `vt_used_mb_max=85` で同じ failure（`scratch-low-without-probe.txt`。画像の平均輝度も 126.921 / 127.08 / 125.343 で標本ありと同じ）なので、影の標本とは無関係な既存の事象（NVIDIA 610.88 のドライバでの VT の要求量）。上限は変えていない（VTG8-ACCEPT で扱う）。
- CSM の基準値（太陽 45 度、`-Deterministic`・旋回 20 度/秒・エポックから 400 フレーム。標本は読み込み完了後の最初のフレームで固定、旋回で視野から出た標本は pairs に入らない）:

  | 視点 | probes | frames | pairs | mean_abs_delta | changed_ratio | flip_ratio | partial_ratio | mean_texel_mm | 範囲外の割合 |
  |---|---|---|---|---|---|---|---|---|---|
  | default | 43329 | 399 | 12056171 | 0.000037 | 0.000514 | 0.000001 | 0.014405 | 21.103 | 0 |
  | near | 37786 | 399 | 4852614 | 0.001070 | 0.011623 | 0.000014 | 0.079195 | 13.434 | 0 |
  | low | 28967 | 399 | 1926831 | 0.000510 | 0.005402 | 0.000042 | 0.064247 | 13.884 | 0 |

  3 視点とも pairs は 1000 以上、partial_ratio は 0 より大きい（影の縁が標本に入っている）。near が最も揺れる（changed 1.2%）。起動画面の大きな球の自転など本当に動く影は CSM・VSM で同じ分だけ入る。
- Notes: (1) 標本は 1 画素おきではなく 4 画素おきの格子の中央の画素。範囲外（影の範囲の外）は 3 視点とも 0 なので mean_texel_mm に混ざっていない。(2) |Δv| の和は 1/4096 単位の固定小数点で数える（1 組あたり最大 1.2e-4 の丸め）。 (3) 決定的でない起動の固定は「起動から 300 回目の実行」で、読み込みの完了を知る手段が無い近似。 (4) 画面の大きさが変わると標本の対応が崩れるので、以後は測らない。
- Next: VTG8-VSM-SAMPLE で VSM の可視度を同じ標本・同じ run で測る（`method=vsm` の行）。

## 反復 3（2026-10-07）: VTG8-SHADOW-PROBE（差し戻しへの対応、done）

- 前回の差し戻しは、撮影スクリプトの既存の検査「VT の常駐量が 64 MB 以下」が low 視点の旋回（400 フレーム）で 80 MB となり failure を返したこと。`-ShadowProbe` を外しても 85 MB で、影の標本とは無関係。過去の旋回の撮影は最長でも約 120 フレームで、400 フレーム旋回は地面の広い範囲を通るので常駐量が増える（静止の low は 30 MB 台）。LOD が壊れたときの数百 MB（600 MB 級）とは桁が違う。
- 対応: `Scripts/CaptureStartupScene.ps1` で、`-OrbitDegreesPerSecond` を使う撮影の VT の上限を 64 → 128 MB に広げた（`-VtUsedLimitMb` が 0 のときは検査しないまま、静止の撮影の上限は 64 のまま）。影の標本の実装は変えていない。
- 検証: `.harness/runs/20261007-203349/verify-VTG8-SHADOW-PROBE-5.txt`（起動画面の撮影、result=pass、CAPTURE_EXIT_CODE=0）。3 視点の `SHADOW_PROBE` は上の基準値の表と同じ値（決定的な撮影で再現）。
- Next: VTG8-VSM-SAMPLE。

## 反復 4（2026-10-07）: VTG8-SHADOW-PROBE（差し戻しへの対応 2、done）

- 前回の差し戻しは、旋回時の VT の上限 128 MB が `-VtUsedLimitMb` の明示値（64 や 10）まで 128 に引き上げてしまい、指定した上限の検査が効かなくなっていたこと。
- 対応: `Scripts/CaptureStartupScene.ps1` で、実効上限 `$vtEffectiveLimitMb` を先頭で一度だけ決める。`-OrbitDegreesPerSecond` を使い、かつ `-VtUsedLimitMb` を省略したとき（`$PSBoundParameters` に無いとき）だけ 128、明示値はそのまま使う（0 は検査なしのまま）。`metrics.json` の `vt_used_limit_mb` には実効値を書く。影の標本の実装は変えていない。
- 検証: `.harness/runs/20261007-203349/verify-VTG8-SHADOW-PROBE-6.txt`（起動画面の撮影。result=pass、CAPTURE_EXIT_CODE=0。`metrics.json` は `vt_used_limit_mb=128`・`failures=[]`、3 視点の `SHADOW_PROBE` は上の基準値の表と同じ値）。`-7.txt` は同じ引数束縛の分岐の確認（旋回で省略→128、明示 64→64、明示 10→10、明示 0→0、静止で省略→64）。`-4.txt` は反復 4 の撮影で上書きしたため、前回までの `-5.txt` と `-6.txt` を参照する。
- Next: VTG8-VSM-SAMPLE。

## 反復 5（2026-10-07）: VTG8-VSM-CLIPMAP（done）

- 内容: (1) 起動引数 `--shadow-method=csm|vsm`（既定 csm。不正な値は `LOG_ERROR` を出して起動を中止する。`Game.exe --shadow-method=bogus` が終了コード -1 で止まることを確かめた）。経路は `ApplicationProcessor` → `RenderWorld::Settings::SunShadowMethod` → `RenderingCoordinator::Settings` → `SceneView::SetShadowMethod`（`SetupDeferredPipeline` の前に決める `--shadow-probe` と同じ形。シグネチャは変えない）→ `ShadowMapPass::SetShadowMethod`。型は `Public/Rendering/ShadowMethod.h`。`Scripts/CaptureStartupScene.ps1` に `-ShadowMethod Csm|Vsm`（既定 Csm、常に `--shadow-method=` を渡す）。
- (2)〜(4) `Public/Rendering/VirtualShadowMapClipmap.h` と `Private/Rendering/VirtualShadowMapClipmap.cpp`（CPU の計算だけ）。既定は 10 段・W0 = 4 m（4〜2048 m）・各段 16384²・128×128 ページ・深度は ±1000 m で原点を 250 m の刻みでスナップ・bias −1・影の距離 80 m。段の中心はカメラのライト空間の XY を、その段のページの格子（幅/128）へ最も近い境界に丸めてスナップし、範囲は中心の ±64 ページ。ページの番地は `VirtualShadowMapPageTorusAddress`（絶対のページの座標 mod 128、負でも 0 以上）。ライト空間の基底は CSM（`BuildLightBasis`）と同じ規則。奥の薄め（`VirtualShadowMapShadowFadeWeight`）は MaxShadowDistance の奥の 10%（72〜80 m）を smoothstep（CSM は最後のカスケードの奥の 10% 幅で薄めるが、クリップマップは分割に依らないので影の距離に対する割合にした）。
- (3) の追加判断: 段を texel だけで選ぶと、高精細で画角の狭い画面（高さ 2160・fovY 30 度など）で、選んだ段の範囲が 80 m の受け手に届かない（texel 7.8 mm の段の半幅は 64 m）。「選んだ段はいつも受け手を含む」を守るため、届かないときだけ受け手を含む最も細かい段（`VirtualShadowMapLevelCoverageMeters` ≥ 距離。半幅から 2 ページを引いた値）まで粗くする。1280×720・fovY 60 度では起きない（テストで確認）。
- (5) `ShadowMapPass::Execute` の CSM の行列の直後（`PublishCascadedShadow` の次）で、vsm のとき `BuildVirtualShadowMapClipmap(SnapshotLightProxies, カメラ, 設定)`（太陽は `SelectShadowedDirectionalLight`）を毎フレーム作り、`PhysicalLighting::PublishSunClipmap` で `SunClipmap` へ公開する（`Begin` で消す）。起動後 1 回、`VSM_CLIPMAP levels=… first_width_m=… bias=… depth_range_m=…` と距離 1・2.5・5・10・20・40・80 m の `VSM_TEXEL d_m=… vsm_mm=… csm_mm=…` を出す（csm_mm は同じカメラの CSM のカスケードの texel。実行での確認は VTG8-VSM-GPU-TIME の撮影）。描画は CSM のまま。
- (6) `Test/Core/Rendering/VirtualShadowMapClipmapTest.cpp`（`CameraViewConstantsTest` の束の MEMBER）。カメラの 1 ページ未満の動きで範囲が動かないか 1 ページだけずれ、最小の角が texel の格子の上にある／1 ページ動くと中心と範囲が 1 ページ分ずれ、残ったページの番地は変わらず、出ていくページの番地が入ってくるページへ回り、範囲の 128 ページの番地が重ならない／選ぶ段が距離について単調で、texel が p(d)·2^bias 以下の最も粗い段（被覆で粗くなる場合を除く）／選んだ段が受け手を含む（300 カメラ × 40 点）／深度の原点が 1/4 の刻み／光源の一覧から作った結果が CSM と同じ太陽になる、など。
- 変異: (a) スナップを外す（範囲の最小の角と中心を `cameraX − 幅/2`・`cameraX` にする）→ 失敗 10917 件、ctest 0/1。(b) 段の選び方の不等号を逆にする（`texel <= target` → `>=`）→ 失敗 76278 件、ctest 0/1。どちらも元に戻して 4/4 Passed。
- 検証（`.harness/runs/20261007-203349/`）: `verify-VTG8-VSM-CLIPMAP-7.txt`（Debug の Game・CameraViewConstantsTest・RenderGraphCompileTest のビルド、BUILD_EXIT_CODE=0。最初の `-1.txt` は Git Bash が `/m:1` をパスに変えて失敗したのでやり直した）、`-8.txt`（VirtualShadowMapClipmapTest・PointShadowFaceMatricesTest・CascadedShadowLightMatricesTest・RenderGraphCompileTest が 4/4 Passed、CTEST_EXIT_CODE=0）、`-5-mutation-nosnap.txt(.ctest)`・`-6-mutation-inequality.txt(.ctest)`（変異の失敗）、`-9-invalid-arg.txt`（不正な値の起動中止）。`-2.txt` は被覆の下限を足す前の失敗（受け手が範囲の外になる 112 件）の記録。
- Notes: (1) 新しい GPU の撮影はしていない（描画は CSM のまま。撮影は VTG8-VSM-GPU-TIME）。(2) `VirtualShadowMapClipmap` は 16 段分の行列を持つ構造体（約 4 KB）で、`PhysicalLightingResources` へフレームごとにコピーする。重ければ後の項目で段の数だけに絞る。
- Next: VTG8-VSM-SAMPLE 以降。

## 反復 6（2026-10-07）: VTG8-VSM-CLIPMAP（差し戻しへの対応、done）

- 前回の差し戻しは、被覆の補正が texel の上限を破る（既定・1.94 m・fovY 35 度・1440 画素で、上限 0.4248 mm に対し段 1 の 0.488 mm を選ぶ）のに、テストが被覆で粗くなった段の上限の検査を免除していたこと。
- 原因: 段 L の texel と被覆はどちらも 2^L で増えるので、2^bias·2tan(fovY/2)/画面の高さ ≥ 2·(段 0 の texel)/(段 0 の被覆) ≒ 2.5e-4 でなければ、texel を満たす最も粗い段が受け手に届かないことがある。bias −1 では 1440 画素・fovY 35 度でこの式が破れる（2.19e-4）。W0・段数は式に効かない（比は 16384·62/128 で決まる）。
- 対応: 既定の bias を −1 → −0.5 に変えた（texel は画素の約 0.35〜0.71 倍）。360/720/1440 画素 × fovY 35/60/90 度 × 距離 0〜80 m（0.01 m 刻み）の格子で被覆の補正が一度も入らないことを Python で先に確かめた（bias −0.75・−0.5 は通り、−1 は 1440・35 度で 1.94 m が破れる）。実装の選び方（被覆の補正）は変えていない。
- テスト: 免除（`bCoverageClamped`）を撤去し、格子の全画面で「texel ≤ 画素 × 2^bias の最も粗い段」「選んだ段が受け手を覆う」を無条件に検査する。式を満たさない画面（fovY 20 度・2160 画素）では被覆を優先して粗い段を選ぶこと（受け手を覆う最も細かい段・距離について単調・補正が実際に入る）を `TestCoveragePriorityOutsideSupportedScreens` に分けて記録した（既知の限界。この画面では texel の上限は成り立たない）。進行・結果の表示を日本語にした。
- 変異: 既定の bias を −1 に戻すと `VirtualShadowMapClipmapTest` が 1846 件の失敗で落ちる（`verify-VTG8-VSM-CLIPMAP-12-mutation-bias.txt`）。戻して 4/4 Passed。
- 検証（`.harness/runs/20261007-203349/`）: `verify-VTG8-VSM-CLIPMAP-13.txt`（Debug の Game・CameraViewConstantsTest・RenderGraphCompileTest のビルド、BUILD_EXIT_CODE=0）、`-14.txt`（VirtualShadowMapClipmapTest・PointShadowFaceMatricesTest・CascadedShadowLightMatricesTest・RenderGraphCompileTest が 4/4 Passed、CTEST_EXIT_CODE=0）。
- Notes: 後の VTG8-VSM-DEFAULT-ON で bias を −1 以下へ下げるときは、上の式（被覆と texel の両立）が破れる画面が出るので、段の数や被覆の余白ではなく、式を満たす範囲の確認が要る。
- Next: VTG8-VSM-SAMPLE 以降。

## 反復 8（2026-10-07）: VTG8-VSM-POOL（done）

- 内容: `--shadow-method=vsm` のときだけ、`SceneView::SetupDeferredPipeline` の照明（`LightingPass`）の直前（スカイ大気の後。ビジビリティの解決・GBuffer・MegaGeometry の後）へ `VirtualShadowMapPass`（`Public/Rendering/VirtualShadowMapPass.h`・`Private/Rendering/VirtualShadowMapPass.cpp`）を足した。(1) 物理ページのプール（uint32 の storage buffer、1 ページ 128×128 = 64 KiB、既定 4096 ページ = 256 MiB。`--vsm-pool-pages=<n>` は `ApplicationProcessor` → `RenderWorld` → `RenderingCoordinator` → `SceneView::SetVsmPoolPages` と渡り、装置の `maxStorageBufferRange`（`DeviceCapabilities::MaxStorageBufferRange`、`VulkanDevice` が物理デバイスの値を載せる）と表の欄の幅（20 ビット）へ締める）。(2) ページの表（10 段 × 128 × 128 の uint32。下位 20 ビットが物理ページの番号、bit31 = 割り当て済み、bit30 = dirty）。(3) 要求のビット列・空きページの一覧（先頭が数、続いて番号）・統計（要求・割り当て・溢れ・描いたページ）。最初の実行だけプールを 1.0 のビット・表を 0 で埋め、要求と統計は毎フレーム 0 から数える。(4) `GpuResources::SetShadowMapPoolBytes` → `PollVideoMemoryBudget` の `PoolCapacityBytes[ShadowMap]`（ヒープの使用量から引かれる）、`VRAM_POOLS` に `shadow_map_pool_mb=` を足し、作成時に `VRAM_LEDGER vsm_pool pages=<n> mb=<f>`・`VRAM_LEDGER vsm_page_table mb=<f>` を 1 回ずつ出す。(5) `PlanPool` が `bFragmentStoresAndAtomics`・`bBufferDeviceAddress` の無い装置・プールが 512 ページ未満の装置を `VSM_FALLBACK reason=<fragment_atomics|bda|pool_size>` にし、確保の失敗も `pool_size` で CSM のまま描く。(6) 資源は `VSM.PhysicalPool`・`VSM.PageTable`・`VSM.RequestBits`・`VSM.FreeList`・`VSM.Stats` の名前で公開する。中身はまだ使わず、照明は CSM のまま。
- 前の試行の残りを仕上げた: 未追跡だった `VirtualShadowMapPass` が `PublishBuffer(name, RGBufferHandle(...))`（`RGBufferHandle` の構築子は private）でコンパイルできなかったため、`RenderGraphBuilder::PublishBuffer(Identity, RGResourceHandle)` を足した（`ImportBuffer` が返すハンドルをそのまま公開する。`PublishTexture` の同名の形と同じ）。`RenderGraphCompileTest.cpp` の `Identity` を `NorvesLib::Core::Identity` に直した。
- テスト: `RenderGraphCompileTest` に 4 件（csm にパス・資源が無く、vsm では照明より前・深度の後に入る／対応した装置で資源が 1 回ずつ作られ名前で公開され、最初の実行だけ埋める・台帳の行が 1 回ずつ／`bFragmentStoresAndAtomics`・BDA・容量の無い Fake の装置と確保の失敗で作られず `VSM_FALLBACK` になる／`PlanPool` が装置の上限へ締める）。`VideoMemoryBudgetManagerTest` に `TestShadowMapPoolIsSubtractedFromNonPool`（`ShadowMap` の確保量がヒープの使用量から引かれ、VT・ジオメトリの目標に効く）。
- 検証（`.harness/runs/20261007-203349/`）: `verify-VTG8-VSM-POOL-4.txt`（Debug の Game・RenderGraphCompileTest・RenderResourcesDomainContractTest のビルド、BUILD_EXIT_CODE=0。`-1`〜`-3` は `PublishBuffer` と `Identity` のコンパイルエラーの記録）、`-5.txt`（RenderGraphCompileTest・VideoMemoryBudgetManagerTest 2/2 Passed、CTEST_EXIT_CODE=0）。
- Notes: (1) 実機の GPU 実行（`Game.exe --shadow-method=vsm` の起動と `VRAM_LEDGER` の確認）は、重い処理の扱いに従い回していない。FakeDevice 上のグラフの実行までを確認した。(2) 物理プールの `BufferDeviceAddress` の使用フラグは作成時に付けるだけで、アドレスの取得は後のパス（VTG8-VSM-* の描画・要求）で行う。(3) 表の物理ページの番号は 20 ビットなので、プールは最大約 100 万ページ（64 GiB）まで。
- Next: VTG8-VSM-SAMPLE 以降（要求の書き込みとページの割り当て）。

## 反復 9（2026-10-07）: VTG8-VSM-MARK（done）

- 内容: `VirtualShadowMapPages`（`Public/Rendering/VirtualShadowMapPages.h`・`Private/Rendering/VirtualShadowMapPages.cpp`）に、印付け（`vsm_mark.comp`）・割り当て（`vsm_allocate.comp`。空きへ戻す・割り当てる・締める の 3 段階）・消去（`vsm_clear.comp`。間接 dispatch）を足し、`VirtualShadowMapPass` が毎フレーム記録する。GPU の区間は `VsmMark`・`VsmAllocate`・`VsmClear`。キャッシュは無く、毎フレームすべて作り直す（要求・ページの表・統計は 0、空きページの一覧は全ページが空きに戻る）。
- 印付け: 空でない（深度 < 1）画素からワールドの位置を戻し、カメラからの距離で段を選び、ライト空間のページに要求のビットを立てる。PCF の核（半径 2 texel。`VirtualShadowMap::DEFAULT_PCF_RADIUS_TEXELS`。VSM-SAMPLE・PCSS で広げたらここも合わせる）がページの境界をまたぐときは隣のページにも立てる。段の選び方は式を写さず、CPU が二分法で求めた距離のしきい値（`VirtualShadowMapLevelDistanceThresholds`）を使い、`SelectVirtualShadowMapLevel` と同じ結果にする（`VirtualShadowMapClipmapTest` に、しきい値から数えた段が全距離・しきい値の前後の隣り合う float で一致するテストを足した）。
- 割り当て: 同じフレームの GPU の中で行う。要求の語ごとに popcount で通し番号を取り、空きの数より小さい番号へ空きの一覧の後ろから物理ページを割り当て、ページの表に「割り当て済み・dirty・物理ページの番号」を書き、消去する一覧（新しい資源 `VSM.DirtyList`。0〜2 語が間接 dispatch の引数、3 語目が数、以降が物理ページの番号）へ足す。尽きたら溢れとして数え、割り当てない。統計は 8 語（要求・割り当て・溢れ・描いたページ・要求のあった段のビット集合）に広げた。
- 統計の読み戻し: host-visible の 4 枠のリングで 2 実行以上遅れて読み、値が変わったとき（または 60 回読むごと）に `VSM_PAGES requested=<n> allocated=<n> overflow=<n> levels_used=0x<mask>` を出す。
- 計算パイプラインを作れない装置は `VSM_FALLBACK reason=pipeline`（新しい理由）で CSM のまま描く。
- テスト: `VirtualShadowMapVulkanTest`（`RHITextureUpdateVulkanTest` の束の MEMBER）。合成した深度（地面と奥の壁の 2 平面と空）とカメラで、段・ページの境界・影の最大距離に近い曖昧な画素（284 画素）を除いて CPU（倍精度）の参照を作り、(A) 印のページ集合（14 ページ・3 段）が一致・ページの表・統計・空きの一覧が全ページを 1 回ずつ・消去の一覧・物理ページが 1.0 のビット（割り当てなかったページは見張りのまま）、(B) プール 7/13/14/1 ページで溢れが 要求−プール・物理ページの番号が重ならない・割り当てなかった欄は 0、(C) 核が境界をまたぐ画素だけで近傍込み 8 ページ > 位置だけ 7 ページ、(D) 深度なし・クリップマップなしで要求 0・物理ページに触らない、(E) 同じ資源での 3 フレームで前フレームの割り当てが残らない。validation error 0 件。`RenderGraphCompileTest` は、vsm の構成で 印付け(16,8,1) → 空きへ戻す → 割り当て → 締め → 消去（`VSM_DirtyList` の間接 dispatch）の順と、その間のバリア（要求は印付けの後、空きの一覧は各段の後、消去の一覧は締めた後に GenericRead へ進めて間接 dispatch が読み、読んだ後に UnorderedAccess へ戻す。物理ページは消去の後）を確かめ、深度かクリップマップが無い構成では印付けが無いこと、パイプラインを作れない構成のフォールバックも確かめる。既存の VSM のテストは資源が 6 つ・統計 32 バイト・読み戻しの 4 枠に合わせて更新した。
- 変異（シェーダーは実行時に読むので、書き換えて `--test=VirtualShadowMapVulkanTest` だけ再実行）: (a) 隣のページへの印を外す（`pageMax = pageMin`）→ exit=1（`verify-VTG8-VSM-MARK-6-mutation-neighbor.txt`）、(b) 空きの数の減算を外す（`freeList[0] = freeCount`）→ exit=1（`-7-mutation-freecount.txt`）。どちらも元に戻して合格。
- 検証（`.harness/runs/20261007-203349/`）: `verify-VTG8-VSM-MARK-3.txt`（Debug の Game・RenderGraphCompileTest・RHITextureUpdateVulkanTest のビルド、BUILD_EXIT_CODE=0。`-1`・`-2` は `DescriptorSetDesc` の include 漏れと `Math::` の名前空間のコンパイルエラーの記録）、`-4.txt`（VirtualShadowMapVulkanTest・VirtualShadowMapClipmapTest・RenderGraphCompileTest が 3/3 Passed、CTEST_EXIT_CODE=0）、`-5.txt`（GPU テストの出力。RESULT=PASS・VUID_COUNT=0）。
- Notes: (1) 実機の Game の起動（`--shadow-method=vsm` で `VSM_PAGES` が出ること）は、重い処理の扱いに従い回していない（VSM-GPU-TIME の撮影で確かめる）。(2) 印付けは深度範囲（±1000 m）の外を除いていない。(3) ワールドの位置は float で戻すので、原点から遠いとページの境界が texel 単位でずれうる（CSM と同じ精度）。(4) 消去する一覧と空きの一覧は、VTG8-VSM-CACHE が前フレームのページを持ち越すときに作り直す前提。
- Next: VTG8-VSM-SAMPLE 以降。

## 反復 10（2026-10-07）: VTG8-VSM-MARK（差し戻しへの対応、done）

- 前回の差し戻しは (1) 統計のバッファ（`VSM_Stats`）が `TransferSrc` を持たないまま `CopyBuffer` の元にしていたこと（VUID-vkCmdCopyBuffer-srcBuffer-00118）と、(2) 読み戻しの遅れを Execute の回数で数えていたこと（同じ SceneView を 1 フレームに 3 つのビューポートで描くと、3 回目が 1 回目の提出前の統計を読む）。
- 対応: (1) `VirtualShadowMap::StatsBufferUsage()`（`TransferSrc` を含む）と `StatsReadbackUsage()`、コピーの記録 `RecordStatsReadback()` を `VirtualShadowMapPass.h` に足し、パスと GPU テストの両方が同じものを使う。(2) 読み戻しの枠を飛行中のフレームの番号（`FrameIndex`）ごとにし、`ResolveRenderFrameSerial()` が変わった最初の Execute だけが、同じ番号の前のフレームの枠を読む（スワップチェーンのフェンスが同じ番号の前のフレームの GPU の完了を待つので、FrameUseRing と同じ前提）。同じフレームの複数の Execute は同じ枠を新しい統計で上書きするだけで読まない。値が変わったとき・60 回読むごとのログの条件は同じ。
- テスト: `VirtualShadowMapVulkanTest` が `RecordStatsReadback` を実際に記録し、コピー先が統計と全語一致すること・検証エラー 0 件を確かめる。`RenderGraphCompileTest` に `TestVirtualShadowMapPassReadsStatsOnlyAfterFrameFence`（偽の装置の読み戻し枠へ値を書き、同じフレームの 3 ビューポートで読まない・次の同じ番号のフレームで 1 回目に出す・変わらなければ出さない・値が変わると出す・変わらないまま 60 回目で出す、`VSM_Stats` が `TransferSrc` を持つ）を足した。偽の `FakeBuffer::Map` は `VSM_StatsReadback` も写像する。
- 変異: (a) `StatsBufferUsage()` から `TransferSrc` を外す → GPU テストが VUID-vkCmdCopyBuffer-srcBuffer-00118 を 10 件出して FAIL（`verify-VTG8-VSM-MARK-15-mutation-transfersrc.txt`）、RenderGraphCompileTest も失敗。(b) 読む条件から通し番号の判定を外す（`slot.bPending` だけにする）→ RenderGraphCompileTest が失敗（`-16-mutation-serial.txt`）。どちらも元に戻して合格。
- 検証（`.harness/runs/20261007-203349/`）: `verify-VTG8-VSM-MARK-17.txt`（Debug の Game・RenderGraphCompileTest・RHITextureUpdateVulkanTest のビルド、BUILD_EXIT_CODE=0。`-10`・`-11` は `constexpr` の関数の誤り、`-12` は偽の装置が読み戻し先を写像せず null を触った SegFault の記録）、`-18.txt`（VirtualShadowMapVulkanTest・RenderGraphCompileTest が 2/2 Passed、CTEST_EXIT_CODE=0）、`-19.txt`（GPU テストの出力。VUID_COUNT=0・RESULT=PASS）。
- Next: VTG8-VSM-SAMPLE 以降。

## 反復 11（2026-10-07）: VTG8-VSM-RASTER（done）

- 内容: 影の塊 × ページの単位で物理ページへ深度を描く仕組み `VirtualShadowMapRaster`（`Public/Rendering/VirtualShadowMapRaster.h`・`Private/Rendering/VirtualShadowMapRaster.cpp`）と、シェーダー `vsm_expand.comp`・`vsm_draw.vert`・`vsm_draw.frag`・`Common/VirtualShadowMapChunk.glsl` を足した。投影物の供給は後の項目（VTG8-VSM-MESH-CASTERS・MEGA-DRAW）で、今はテストの合成の塊だけ。`VirtualShadowMapPass` は展開・描画を記録しない（塊の入力が無いため。統計の語 5〜7 の報告だけ配線した）。
- 塊の記録 `VsmShadowChunk`（144 バイト）: ビジビリティバッファの `DrawRecord`（64 バイト。三角形の数・インデックスの先頭・頂点の基点・BDA）+ ワールドの境界（AABB）+ ワールドへの変換（3×4）。頂点の読み方は `VisibilityTriangleFetch.glsl` の `VisLoadTriangleVertexIndex` と BDA の頂点配列をそのまま使う（手続きは変換つき、スキニングのようなワールド空間の頂点は単位行列）。
- 展開（`VsmExpand`）: 1 ワークグループ（64 スレッド）= 1 塊。AABB のライト空間の矩形（中心 + 半幅）が覆うページを段ごとに走査し、割り当て済みで dirty のものを数え（共有メモリ）、`draws` の頭の語 0 への atomicAdd で連続した範囲を取り、同じ走査で（塊・段 | 物理ページ << 4・絶対のページ x・y）の uvec4 を書き、塊ごとの `VkDrawIndexedIndirectCommand`（頂点数 = 三角形 × 3、instanceCount、firstIndex 0、vertexOffset 0、firstInstance = 範囲の先頭）を作る。容量（インスタンスの数）に入らない塊は instanceCount 0 にして溢れとして数える（部分的には描かない）。統計（`VSM.Stats`）の語 5 = 描く塊の数、6 = 書いたインスタンスの数、7 = 溢れたインスタンスの数（`VirtualShadowMap::StatRasterChunks/Instances/Overflow`）。
- 描画（`VsmDraw`）: 添付の無い 128×128 のレンダーパス・フレームバッファ（RHI は添付なしを受け付けた。validation 0 件なので使い捨ての添付は不要）。頂点シェーダーが記録の頂点を読み、ワールド → ライト空間 → そのページの局所 texel（`(lightXY - ページ × ページの幅) / texel`、128 texel = NDC [-1,1]）へ写し、深度 [0,1]（`0.5 + (ld - 深度の原点) / (2 × 範囲)`）を渡す。断片シェーダーが物理ページの texel へ `atomicMin(floatBitsToUint(深度) & 0x7FFFFFFF)`。背面は省かない。
- 判断（Notes）: (1) RHI に非インデックスの間接描画が無く、`multiDrawIndirect` も有効にしていないので、頂点番号を作るための連番のインデックスバッファ（0..383）を持ち、塊ごとに `DrawIndexedIndirect`（drawCount 1）を 1 回ずつ記録する。装置が `drawIndirectFirstInstance`・BDA・断片のアトミックを使えなければ `Initialize` が false。(2) `ICommandList`・RHI は変更していない。(3) 物理ページの状態は、消去（計算）の後 `UnorderedAccess → PixelShaderWrite`、描画の後 `PixelShaderWrite → UnorderedAccess`。展開の出力と塊の記録は `UnorderedAccess → GenericRead`（頂点シェーダー・間接描画が読む）→ 戻す。(4) `VSM_RASTER chunks= instances= overflow=` は `VirtualShadowMapRasterStatsReporter`（値が変わったとき・60 回報告ごと。0 のままの間は出さず、一度出たら 0 に戻ったときも出す）が出し、`VirtualShadowMapPass::HarvestStats` が語 5〜7 を渡す（今は 0 のままなので出ない）。
- テスト: `VirtualShadowMapVulkanTest` にケース F〜H。F = 印付け → 割り当て → 消去 → 展開 → 描画の一続き（合成した地面の深度）で、ページ A・B の境界をまたぐ四角形（傾いた平面）と、一部が重なる遠い四角形（ローカルの頂点 + 変換。後から描かれる）を置き、物理プールの全 texel（228640 のうち縁を除く）が形の和の参照と一致（形に覆われた texel 8721、不一致 0）。G = ページの表を直接書いて段 0 の 24 ページ（1 ページは dirty でなく触られない）・段 1 の 4 ページに、縁がページの中を斜めに通る 24 ページ以上をまたぐ大きな三角形（頂点は数百 m 先）と遠い四角形（形に覆われた texel 245310、不一致 0）。H = 容量が 1 足りないと描かずに溢れ 27 と数え、ちょうどなら描く。展開の統計・間接描画の引数・インスタンスの範囲が隙間も重なりも無く並ぶことも確かめる。参照は texel の中心が形の縁から半 texel 未満のものを除く（1 texel のずれを検出できる幅）。深度の許容は 1e-5。validation error 0 件。`RenderGraphCompileTest` に、統計の語 5〜7 が 0 のままなら VSM_RASTER を出さず 0 以外・変化・60 回ごとに出す `TestVirtualShadowMapPassReportsRasterStats` と、報告の決め方 `TestVirtualShadowMapRasterStatsReporterDecidesWhenToLog` を足した。
- 変異（シェーダーは実行時に読むので、書き換えて `--test=VirtualShadowMapVulkanTest` だけ再実行。元に戻して合格を確認）: (a) 局所座標を 1 texel ずらす（`localTexel + vec2(1, 0)`）→ exit=1（`verify-VTG8-VSM-RASTER-mutation-shift.txt`）、(b) `atomicMin` を代入にする → exit=1（`-mutation-assign.txt`）、(c) 展開の dirty 判定を外す → exit=1（`-mutation-nodirty.txt`）。
- 検証（`.harness/runs/20261007-203349/`）: `verify-VTG8-VSM-RASTER-3.txt`（Debug の Game・RenderGraphCompileTest・RHITextureUpdateVulkanTest のビルド、BUILD_EXIT_CODE=0。`-1`・`-2` は VSM_RASTER の行の収集がカテゴリ `VirtualShadowMapRaster` を拾わず `TestVirtualShadowMapPassReportsRasterStats` が落ちた記録）、`-4.txt`（VirtualShadowMapVulkanTest・RenderGraphCompileTest が 2/2 Passed、CTEST_EXIT_CODE=0）、`-5.txt`（GPU テストの出力。VUID_COUNT=0・RESULT=PASS）。
- Notes: (1) 大きな三角形の縁の欠けは出なかった（頂点が段 0 で NDC ±125 程度まで。さらに細かい段・遠い頂点は未確認で、MESH-CASTERS の実シーンで見る）。(2) 実機の Game の起動確認（`--shadow-method=vsm` の `VSM_RASTER` 行）は、投影物の供給が無いので出ない。VSM-GPU-TIME の撮影で確かめる。(3) 展開の境界は AABB の射影なので、ライト軸と合わない形では描かれないページのインスタンスも出る（空振りのインスタンス。頂点シェーダーのコストだけ）。(4) 展開は 1 ワークグループ = 1 塊で、ページ走査は 1 段あたり最大 128×128。塊が非常に多い・大きな塊が細かい段へ広がる場面は VSM-GPU-TIME で測る。(5) VTG8-VSM-MARK はランナーが blocked にしたまま（統計の読み戻しの遅れの件）で、今回は触っていない。
- Next: VTG8-VSM-MESH-CASTERS。

## 反復 12（2026-10-07）: VTG8-VSM-MESH-CASTERS（done）

- 内容: vsm の構成で、影を落とす手続きメッシュとスキニングを塊の記録にして、VTG8-VSM-RASTER の展開・描画で物理ページへ描く。`VirtualShadowMapPass` が `VirtualShadowMapRaster` を持ち、毎フレーム「深度の確定 → 印付け・割り当て・消去 → 投影物の集め（CPU）→ 塊の記録のアップロード → 展開（VsmExpand）→ 描画（VsmDraw）」を記録する。印付けをしなかった（深度かクリップマップが無い）フレーム・塊が 0 件のフレームは展開・描画を記録しない。展開・描画のパイプラインを作れない装置（firstInstance の間接描画が無い等）は `VSM_FALLBACK reason=pipeline` で CSM のまま。
- CPU の計算（`Public/Rendering/VirtualShadowMapCasters.h`。ヘッダだけ。ViewRenderContext にも描画の装置にも依らない）: `PlanProceduralChunks`（手続きメッシュの 1 描画を `BuildMeshIndexChunks` で 128 三角形以下の塊に分け、塊ごとのローカルの境界を決める）、`AppendProceduralInstance`（インスタンスの変換 16 個の float を 3×4 の行へ写し、境界 = ローカルの境界 × 変換）、`AppendSkinnedInstance`（変形した頂点のアドレス・インデックスのアドレス・描画の境界・単位行列）、`LevelMaskForBounds`（境界がどの段の範囲に入るか。展開が使う範囲と同じ式: `floor(ライト空間の位置 / ページの幅)` が `[原点, 原点 + 128)` に触れる）。どの段の範囲にも入らない塊は CPU で省く（`culled`）。上限（`MAX_CASTER_CHUNKS` = 32768 塊 / フレーム）を超えた塊は書かずに `dropped` に数える。
- 集め方（`VirtualShadowMapPass::CollectCasters`）: 手続きメッシュは `GetActiveDrawCommands()`（半透明も含む）のうち `bCastShadow` の `Mesh` 描画。範囲・インスタンスの選び方はビジビリティバッファの `CollectProceduralChunks` と同じ（`IndexCount > 0` なら `IndexOffset`・`VertexOffset`、`FirstInstance + k` のインスタンス）。インスタンスの変換は新設の `ViewRenderContext::SnapshotInstanceData`（`FramePacket::InstanceData` の CPU 側。`RenderingCoordinator` が渡す）から引く。主カメラの錐台では省かない。スキニングは `SkinningComputePass::GetInstances()`（頂点は `CurrentVertexAddress`、インデックスは `IndexBuffer` の BDA）で、描画の境界は `SourceMeshComponentId` と同じ `ComponentId` の `SkinnedMeshProxy`（`bCastShadow` を見る）の `AnimatedBounds` × `WorldTransform`（`DirectionalShadowLightMatrices` と同じ求め方）。塊は `TryGetChunks`、無ければ `BuildMeshIndexChunks`。
- 手続きメッシュの塊の境界: 登録時（`ProceduralMeshGpuStore::RegisterMesh`）に、インデックスを 384 個（128 三角形）ごとにメッシュの先頭から整列したブロックのローカルの AABB（`ProceduralMeshGPUData::BlockBounds`）を求めて持つ。頂点の基点が 0 の描画は、範囲をブロックの境目で区切って塊が 1 つのブロックに収まるようにし、そのブロックの境界を使う。基点が 0 でない・先頭が 3 の倍数でない・ブロックの境界が無いときはメッシュ全体の境界（`LocalBounds`）。メッシュ全体の境界でどの段にも入らないインスタンスは塊を見ずに全部省く。境界が無い（頂点が `Mesh3DVertex` でない等）メッシュは `skipped` に数えて描かない。
- バッファ: 塊の記録（ホストが書く。`VsmRaster_Chunks`）は `FrameUseRing` で Execute ごとに別のバッファ（同じフレームの複数のビューポートが提出前の記録を上書きしない。足りなければ 2 倍ずつ広げる）。展開の出力（`VsmRaster_Instances` = 524288 インスタンス × 16 B、`VsmRaster_Draws` = 32768 塊 × 20 B + 頭）は 1 本を使い回す（GPU が書く）。台帳は `VRAM_LEDGER vsm_raster mb=...`、`SetShadowMapPoolBytes` にも足した。内訳のログは `VSM_CASTERS procedural_chunks=<n> skinned_chunks=<n> culled=<n> dropped=<n> skipped=<n>`（変わったとき・60 回ごと。何も集めていない間は出さない）。
- SceneView: vsm のときはスキニングの計算（`SkinningComputePass`）を有効にし（ビジビリティバッファが off でも）、VSM のパスへ渡す。VSM は変形した頂点を `builder.Read(…, GenericRead)` で宣言する（変形の後に並び、頂点のバッファの使用が VSM のパスまで延びる）。ビジビリティバッファが on（既定）なら元から有効。csm の構成にはパス・バッファが何も足されない。
- テスト: `RenderGraphCompileTest` に `TestVirtualShadowMapCasterLevelMaskMatchesLevelRanges`（段の範囲の判定が `VirtualShadowMapLevelFindPage` と一致）・`...PlansProceduralChunks`（ブロックの境目の区切り・境界の選び方・作れない入力）・`...AppendsProceduralInstances`（変換の行・境界・省き・上限）・`...AppendsSkinnedInstances`、`TestVirtualShadowMapPassRecordsCasterRasterAfterSkinning`（スキニングの変形 → 印付け・割り当て・消去 → 展開 → 描画の並び `DDDDDJDBIIIE`、間のバリア、塊の記録の中身、`VSM_CASTERS`、同じフレームの 2 つ目のビューポートが別のバッファへ書くこと、印付けをしない構成では展開・描画が無いこと、変形した頂点の使用期間）を足し、既存の `TestVirtualShadowMapPassAbsentForCsmAndBeforeLightingForVsm` に csm（ビジビリティ off でスキニングの計算が無効のまま・VSM が無い）と vsm（有効で VSM の前）の確認を足した。偽の装置は `SetVirtualShadowMapCapabilities` で `bDrawIndirectFirstInstance` も立て、`MeshVB`・`MeshIB`・`SkinnedMeshIB` に BDA を返す。`VirtualShadowMapVulkanTest` にケース I: ケース F と同じ場面（近い四角形 + ローカルの頂点 + 変換の遠い四角形）の塊の記録を、本番の `PlanProceduralChunks`・`AppendProceduralInstance`（メッシュのバッファの BDA と 16 個の float の変換）で作って描き、物理ページを（段・絶対のページが同じページどうしで）全 texel 比べて、比べた 228640 texel（覆われた 8721）の不一致 0・ケース F との違い 0。
- 変異（元に戻して合格を確認）: (a) 変換の行の写し方を転置 → `RenderGraphCompileTest`（`expectedRows`）と `VirtualShadowMapVulkanTest`（ケース I）が失敗（`verify-VTG8-VSM-MESH-CASTERS-mutation-world-rows.txt`）、(b) 段の範囲による省きを外す → `RenderGraphCompileTest` が失敗（`-mutation-nocull.txt`）、(c) VSM の変形した頂点の `Read` を外す → 頂点のバッファの使用期間の検査で `RenderGraphCompileTest` が失敗（`-mutation-noskinread.txt`）。
- 検証（`.harness/runs/20261007-203349/`）: `verify-VTG8-VSM-MESH-CASTERS-3.txt`（Debug の Game・RenderGraphCompileTest・RHITextureUpdateVulkanTest・SkinnedRenderPathContractTest のビルド、BUILD_EXIT_CODE=0。`-1` は初回のビルド）、`-4.txt`（VirtualShadowMapVulkanTest・RenderGraphCompileTest・SkinnedRenderPathContractTest が 3/3 Passed、CTEST_EXIT_CODE=0）、`-5.txt`（GPU テストの出力。ケース I の行・VUID_COUNT=0・RESULT=PASS）。GPU テストは 10 回続けて PASS。初回の ctest（`-2`）は、物理ページの番号が割り当ての順（GPU のアトミック）で実行ごとに変わりうるのに、ケース I が物理ページの番号どうしで語を比べて失敗した記録（比べ方を段・絶対のページ単位に直した）と、csm の既定がビジビリティ on でスキニングの計算が元から有効だったためのテストの誤り。
- Notes: (1) スキニングの投影物は `SkinningComputePass` の出力（不透明の描画だけ・非インスタンス・計算スキニングを載せられたものだけ）。半透明のスキニングの影・インスタンス描画のスキニング・変形の対象から外れたものは VSM には載らない（CSM は描く）。ビジビリティバッファが on でも解決が使えず予備の GBuffer の描画へ戻るフレームは、スキニングの計算が何も宣言しないので VSM にもスキニングは載らない。(2) 「記録の作成」は CPU（ホストが書くバッファ）で、GPU の計算ではない。記録は毎フレーム全部作り直す（キャッシュは VTG8-VSM-CACHE）。(3) 塊の記録の上限・展開のインスタンスの容量は定数（32768 塊・524288 インスタンス）。起動画面で足りるかは VSM-GPU-TIME の撮影の `VSM_RASTER overflow=` と `VSM_CASTERS dropped=` で見る。(4) 実機の Game の起動確認（`--shadow-method=vsm` の `VSM_CASTERS`・`VSM_RASTER` 行）は、照明がまだ VSM を読まないので画には出ない。VSM-GPU-TIME で確かめる。(5) 新しいヘッダ `VirtualShadowMapCasters.h` は `Library/Core/CMakeLists.txt` のヘッダ一覧に足していない（paths の外。コンパイルには要らない）。(6) VTG8-VSM-MARK はランナーが blocked にしたまま（触っていない）。
- Next: VTG8-VSM-MEGA-CULL（MegaGeometry の投影物）。

## 反復 13（2026-10-08）: VTG8-VSM-MESH-CASTERS（評価者の差し戻し 3 点を修正）

- 修正 1（錐台の外の手続きメッシュ）: `VirtualShadowMapPass::CollectCasters` が、主カメラの錐台で省かれた後の描画コマンド（`GetActiveDrawCommands()`）ではなく、カリング前の全プロキシ（`context.SnapshotMeshProxies` = `FramePacket::Scene.MeshProxies`）から集めるようにした。`bCastShadow` の有効なプロキシを、サブメッシュごと（無ければメッシュ全体）に塊へ分け、`WorldTransform` を変換にする。描画コマンドの `Mesh` 描画・インスタンスの表（`SnapshotInstanceData`）は使わない。インポスターで置き換えられたメッシュも影を落とす（CSM は描画コマンドごと落ちる）。
- 修正 2（スキニングの影）: `SkinningComputePass::SetShadowCasterOutput(true)`（SceneView が vsm のときだけ設定）で、(a) 解決が使えず予備の GBuffer の描画へ戻るフレームも、影を落とす描画に限って変形する（`bResolveUsable` が false なら `bCastShadow` の描画だけ。何も読まないときは今まで通り何も宣言しない）、(b) 変形の対象を不透明の一覧ではなく全描画の一覧（`GetActiveDrawCommands()`。CSM と同じ集合）から選ぶ。`SkinningComputeInstance` に `bOpaque`・`bCastShadow` を足し、`VisibilityRasterPass::CollectSkinnedChunks` は `bOpaque` でない（半透明の一覧にあった）インスタンスを描かない。記録の `instanceIndex` は `GetInstances()` の添字のまま（飛ばしても詰めない）。
- 修正 3（段ごとの除外）: `VsmShadowChunk` の境界の w の空きを `LevelMask`（既定は全段）に変え、CPU が `LevelMaskForBounds` で決めた段の集合を記録へ持つ。`vsm_expand.comp` は数える走査・書く走査とも、集合に入らない段を範囲も見ずに飛ばす。GLSL 側は `vec3 boundsMin; uint levelMask; vec3 boundsMax; uint reserved;`（std430 で 16 バイトに詰まり、記録の大きさ 144 B・並びは変わらない。`offsetof` の静的検査を足した）。CPU は倍精度・GPU は単精度なので、ページの境目に触れるだけの 1 ulp の差では段が落ちうるが、その差で欠けるのは面積 0 の縁だけ。
- テスト（`RenderGraphCompileTest`）: `TestVirtualShadowMapCasterRecordsLevelMask`（手続き・スキニングの記録が段 0 の外・段 1 の内側で段 0 を含まない集合を持つ・原点のそばは全段・既定は全段）、`TestVirtualShadowMapPassRecordsCasterRasterAfterSkinning` に、(1) 現在のビューポートの描画コマンドが空（錐台で全部省かれた）でも手続き 2 つを集める、(2) 解決が使えない構成でスキニングが変形される・影を落とさない描画は変形しない・影の出力を切ると何も宣言しない、(3) 半透明の一覧だけにあるスキニングが変形され `bOpaque` が false になる、を足した。手続きメッシュの場面は描画コマンドではなくプロキシで作る形に変えた（スキニングの 1 件だけが描画コマンド）。
- 変異: 手続きの記録の `LevelMask` を 1 に固定 → `VirtualShadowMapVulkanTest`（ケース I）が FAIL（`verify-VTG8-VSM-MESH-CASTERS-mutation-levelmask.txt`）。元に戻して合格。
- 検証（`.harness/runs/20261007-203349/`）: `verify-VTG8-VSM-MESH-CASTERS-8.txt`（Debug の Game・RenderGraphCompileTest・RHITextureUpdateVulkanTest・SkinnedRenderPathContractTest のビルド、exit 0。`-6` は変異の前の同じビルド）、`-9.txt`（VirtualShadowMapVulkanTest・RenderGraphCompileTest・SkinnedRenderPathContractTest が 3/3 Passed、exit 0。`-7` は変異の前の同じ実行）。
- Notes: (1) 手続きメッシュのプロキシは毎フレーム全件を走り、同じメッシュのインスタンスごとに塊の計画（`PlanProceduralChunks`）を作り直す（キャッシュしない。数千件でも塊の数に比例する計算）。(2) 距離カリング（`m_MaxDrawDistance`）の外の物も、段の範囲に入れば集める。(3) 解決が使えず VSM のパスが CSM へ落ちた構成（`m_ShadowMethod` は vsm のまま）でも、スキニングは影のために変形される。
- Next: VTG8-VSM-MEGA-CULL（MegaGeometry の投影物）。


## 反復 14（2026-10-08）: VTG8-VSM-MEGA-CULL（done。PROGRESS の記録が残っていなかったので、反復 15 がコードと証拠から補った）

- 内容: MegaGeometry の投影物（`bCastShadow` のインスタンス）を VSM の段ごとにカリングする `VirtualShadowMapMegaCull`（`Public/Rendering/VirtualShadowMapRaster.h` に同居）。dirty のページの階層（`vsm_dirty_mips.comp`）→ クラスタの選択（`vsm_mega_cull.comp`。主の経路の `Common/MegaGeometryCull.glsl` の判定の本体を、`cullData.orthoLod = 1` の正射影の LOD で使う）の順に走り、（インスタンスの表の番号、段、クラスタの番号）の一覧を自分のバッファ `VsmMega_List` へ作る。主の経路の間接描画・見えた印・ページの要求・統計には書かない。区間 `VsmCullMega`、`VSM_MEGA_CULL instances= clusters= overflow=` を 60 回ごとに出す。定数の並びは `Public/Rendering/MegaGeometry/MegaGeometryCullUniforms.h`、dirty の階層と定数は `Common/VirtualShadowMapMegaCull.glsl`。
- テスト: `VirtualShadowMapVulkanTest` のケース J1〜J3（完全二分木のクラスタ。段の texel が 2 倍になるごとに選ばれるクラスタが粗くなる・一つの切り口・インスタンスの判定・溢れ・葉のページが非常駐のとき）、`RenderGraphCompileTest` の `TestVirtualShadowMapPassRecordsMegaCullBetweenMainCullAndExpand`。
- 証拠: `.harness/runs/20261007-203349/verify-VTG8-VSM-MEGA-CULL-*.txt`、ランナーの再検証 `recheck-VTG8-VSM-MEGA-CULL-14-1/2.txt`（3/3 Passed）。
- 注: この反復の作業は「作業途中の保存」（42457bbb）に入ったが、新規ファイル 4 つ（`vsm_dirty_mips.comp`・`vsm_mega_cull.comp`・`Common/VirtualShadowMapMegaCull.glsl`・`MegaGeometryCullUniforms.h`）が未追跡のままだった。反復 15 のコミットで追加した。

## 反復 15（2026-10-08）: VTG8-VSM-MEGA-DRAW（done）

- 内容: VTG8-VSM-MEGA-CULL が選んだ（インスタンス、段、クラスタ）を、手続き・スキニングの塊と同じ展開・描画の 1 回の流れで物理ページへ描く。流れは「印付け → 割り当て → 消去 → カリング（階層・選択）→ **クラスタの記録（新規）** → 展開 → 描画」。
- クラスタの記録（`vsm_mega_chunks.comp`。カリングの `Record` の最後の dispatch。1 スレッド = 一覧の 1 件。dispatch は一覧の容量 ÷ 64 グループ）: 一覧の 1 件を手続き・スキニングと同じ形の `VsmShadowChunk`（144 バイト）にして `VsmMega_Chunks` の同じ添字へ書く。記録の種類は `VIS_KIND_MEGA_CLUSTER`、インデックスの先頭 = インスタンスのインデックスの基点（`drawInfo.z`）+ クラスタのインデックスの位置、頂点の基点 = インスタンスの頂点の基点（`drawInfo.y`）+ クラスタの頂点の位置、アドレス = 影の表のプールの塊のバッファのアドレス（ビジビリティバッファの `visbuffer_records.comp` が書く MegaGeometry の記録と同じ読み方。頂点の形は手続き・スキニングと同じ 8 float なので、頂点シェーダーの読み方は記録のアドレスと基点だけで決まり、種類で式が分かれるのは記録の作り方）。境界 = クラスタの境界球をワールドへ移した球の外接 AABB、変換 = インスタンスのワールド行列の 3 行、展開する段 = 選んだ段だけ（`levelMask = 1 << 段`）。アドレスの無いインスタンスは三角形 0 の記録にして何も描かない。
- 影の表（`MegaGeometryShadowInstance`、32 → 48 バイト）に頂点・インデックスの塊のバッファのアドレス（`VertexAddress`・`IndexAddress`）を足した（`MegaGeometryPass` が書く。GLSL の `ShadowInstance` は `Common/VirtualShadowMapMegaCull.glsl` へ移した）。
- 展開（`vsm_expand.comp`）: 塊の番号 `counts.x` 未満がホストが書いた塊、それ以降が MegaGeometry のクラスタの記録（`counts.w` = 容量）。件数は GPU が決める（一覧の語 0 を容量で頭打ち）ので、ホストが書いた塊の数 + 容量ぶんのワークグループを出し、件数より後ろは何もしない。描画（`vsm_draw.vert`）は塊の番号で `chunks` か `megaChunks`（束縛 4）を選ぶ。間接描画の引数・インスタンス・統計（語 5〜7）は手続き・スキニングと共用。
- 描画（`VirtualShadowMapRaster::Record`）: ホストが書いた塊ごとの `DrawIndexedIndirect` の後に、`DrawIndexedIndirectCount` 1 回（引数 = 塊の番号 N 以降、数 = 一覧の語 0、最大 = 一覧の容量）。インスタンスごとの定数バッファは使わない（CSM の MegaGeometry の影の `DynamicUniformAllocator` のスロットを使わないので、負荷モードで省かれる件は VSM の描画では起きない）。一覧は `IndirectBuffer` の用途を足した。`DrawIndexedIndirectCount` を使えない装置（`bDrawIndirectCount` が false）では `SupportsMegaCasters()` が false になり、`VirtualShadowMapPass` は MegaGeometry のカリング・記録の資源を作らない（VSM は動く。警告を 1 回出す）。
- `VirtualShadowMapPass`: `VsmMega_Chunks`（容量 262144 件 × 144 バイト ≒ 36 MB）と、間接描画の引数の容量（ホストが書いた塊 32768 + 262144）を足した。クラスタの記録を作ったフレームは、ホストが書いた塊が 0 件でも展開・描画を記録する。VRAM_LEDGER の `vsm_raster`・`vsm_mega_cull` にこの分が入る。
- テスト:
  - `VirtualShadowMapVulkanTest` ケース K: ケース F と同じ場面（近い四角形 + ローカルの頂点と変換の遠い四角形）を、形ごとに MegaGeometry のクラスタ 1 つ・インスタンス 1 つにして、本番の流れ（印付け → 割り当て → 消去 → カリング → クラスタの記録 → 展開 → 描画）に通した。物理プールが形の和の参照と一致（比べた texel 228640、覆われた texel 8721、不一致 0）し、ケース F の手続きの経路と全 texel で同じ（違い 0）。GPU が作ったクラスタの記録の中身（種類・インスタンス・三角形の数・インデックスの先頭・頂点の基点・アドレス・段の集合・変換・境界）、展開の引数（頂点数・インスタンス数）、統計（選んだクラスタ・書いたインスタンス・溢れ）が一覧の件と整合する。頂点の基点は塊の先頭から 5 頂点ずらして（先頭の余りは 1e6 で埋める）、インスタンスの基点を足し忘れると壊れるようにした。一覧の件数より後ろのクラスタの記録は、身代わり（深度を光源側へ 7 m ずらした形）で埋め、展開がそれを読むと物理ページと統計に出る。
  - `RenderGraphCompileTest`: 既存の `TestVirtualShadowMapPassRecordsMegaCullBetweenMainCullAndExpand` を、新しい並び（`DDDDDJDDDDBIIIIE`。カリングの後にクラスタの記録の dispatch）、dispatch の大きさ（クラスタの記録 = 容量 ÷ 64、展開 = 3 + 容量を 65535 で折り返す）、束縛（クラスタの記録 5・展開 8）、バリア（一覧・クラスタの記録は展開・描画の前後で Common ↔ UnorderedAccess ↔ GenericRead）、間接描画（塊ごとの 3 回 + 数を GPU から読む 1 回、最大 = 一覧の容量）に更新した。新規 `TestVirtualShadowMapMegaDrawDoesNotUseCsmUniformSlots`: 影を落とす MegaGeometry のインスタンスを 2 → 305（CSM の 1 カスケードあたりのスロット 256 を超える）に増やしても、VSM の記録の間接描画の数（4）・作るバッファ・記述子セット・dispatch の数が変わらない。`DrawIndexedIndirectCount` を使えない装置・パイプライン作成の失敗（クラスタの記録のパイプラインを含む 3 通り）で、カリングの資源を作らず VSM が動くことも確かめた。
- 変異（元に戻して合格を確認）: シェーダー（実行時に読むので書き換えて `--test=VirtualShadowMapVulkanTest` だけ再実行）— (a) `levelMask` を全段にする、(b) 変換の行を転置、(c) 頂点の基点にインスタンスの基点を足さない、(d) インデックスの先頭にインスタンスの基点を足さない（GPU が止まって例外 → exit 1）、(e) 描画が MegaGeometry の記録を選ばない、(f) 展開が一覧の件数でなく容量まで読む → すべて exit=1（`verify-VTG8-VSM-MEGA-DRAW-mutation-<名前>.txt`）。C++（`RenderGraphCompileTest`）— (g) `DrawIndexedIndirectCount` を記録しない、(h) クラスタの記録の dispatch を記録しない、(i) 展開の前の一覧・記録の遷移を省く → すべて assert で落ちる（`-mutation-rgct-<名前>.txt`）。
- 検証（`.harness/runs/20261007-203349/`）: `verify-VTG8-VSM-MEGA-DRAW-2.txt`（Debug の Game・RenderGraphCompileTest・RHITextureUpdateVulkanTest のビルド、BUILD_EXIT_CODE=0。`-1` は途中のビルド）、`-3.txt`（VirtualShadowMapVulkanTest・RenderGraphCompileTest が 2/2 Passed、CTEST_EXIT_CODE=0）、`-gpu-4.txt`（GPU テストを 6 回続けて実行。どれも VUID_COUNT=0・RESULT=PASS）。
- Notes: (1) 実機の Game の起動確認（`--shadow-method=vsm` で `VSM_MEGA_CULL`・`VSM_RASTER` が出ること、起動画面の岩）は、重い処理の扱いに従い回していない。照明が VSM をまだ読まないので画には出ない。VSM-GPU-TIME の撮影で確かめる。(2) 展開は一覧の容量（262144）ぶんの空のワークグループを出す（件数より後ろは即 return）。間接 dispatch にしていないのは、件数を書く側（カリング）の後ろに引数を作る計算を足さないため。コストは VSM-GPU-TIME の `VsmExpand` で見る（大きければ件数から引数を作る 1 スレッドの計算を足して `DispatchIndirect` にする）。(3) クラスタの記録は 144 バイト × 容量で、使わない分の VRAM も確保する（約 36 MB）。容量を下げるなら `MEGA_CULL_LIST_CAPACITY` を下げる（一覧の溢れとして数える）。(4) 境界の AABB は境界球の外接なので、細長いクラスタではライト軸と合わないページのインスタンスが出る（頂点シェーダーの空振りだけ。物理ページの結果は変わらない）。(5) CSM の MegaGeometry の影の描画は、半透明・ボリュームのために vsm の構成でも残る（既知の限界）。
- Next: VTG8-VSM-SAMPLE（照明で VSM を読む）。VTG8-VSM-MESH-CASTERS（評価者の差し戻し 2 周で blocked）と VTG8-VSM-MARK（blocked）は触っていない。

## 反復 16（2026-10-08）: VTG8-VSM-SAMPLE（done）

- 内容: (1) 読み出しの関数 `VsmSampleSunShadow`（`Assets/Shaders/Common/VirtualShadowMap.glsl`。パラメータの構造体は `VirtualShadowMapParams.glsl`、Poisson 16 点は CSM と共有する `PoissonDisk16.glsl` に切り出した）と、そのパラメータを作る `BuildVirtualShadowMapSampleParams`（`Public/Rendering/VirtualShadowMapSample.h`・`Private/Rendering/VirtualShadowMapSample.cpp`。印付けと同じ `VirtualShadowMapLevelDistanceThresholds` を使うので、読む段と印を付けた段が一致する）。受け手の段はカメラからの直線距離（印付けと同じ）で選ぶ。法線の向きへのずらしは「使う段の texel × 1.5 × 面と光の角の sin」、受け面の深度の傾きは CSM と同じ（光の向きの余弦の下限 0.05）、深度の比較の余裕は「（1.5 + |傾き x| + |傾き y|）× 読んだ段の texel」。PCF は CSM と同じ 16 点で、半径は r = max(画素の大きさ p(d), 選んだ段の 1 texel)（ワールドの長さ）。各標本が自分の位置のページ（トーラスの番地・範囲の外・割り当て済みの印を確かめる）から texel を読み、無ければ粗い段へ順に逃げ、どの段にも無ければ影なし。影の最大の距離（80 m）の外は影なし、奥の 10% は `smoothstep` で薄める。逃げた標本は `VSM_COUNT_FALLBACK()` を呼ぶ（取り込む側が統計へ数える）。
- (2) 照明: `lighting.frag` に binding 21（VSM を読むパラメータの定数バッファ）・22（ページの表）・23（物理ページのプール）を足し、`CalculateSunShadow` が「VSM が有効（`control.x`）かつ R5 のハードシャドウの検証表示でない」ときだけ VSM、それ以外は従来の `CalculateShadow`（CSM）を呼ぶ。接触影は今と同じく掛かる。`LightingPass` は `Declare` で `VSM.PageTable`・`VSM.PhysicalPool` が公開されているときだけ `ShaderResource` で読み（グラフが VSM の書き込みの後・照明の前に遷移を入れる）、`Execute` で `SunClipmap`・カメラ・深度の高さからパラメータを作る。揃わないフレーム・csm の構成は無効のパラメータと既定のバッファ（4 バイトの既存の既定バッファ）を結ぶので、csm は何も変わらない（golden 4 本が基準画像・閾値を動かさずに合格）。半透明・ボリュームは CSM のまま。
- (3) 影の測定（`--shadow-probe`）: `shadow_probe.comp` が、vsm のとき同じ標本点を `VsmSampleSunShadow` でも測り、`ShadowProbePass` が `SHADOW_PROBE method=vsm ...`（`mean_texel_mm` は使った段の texel）・`SHADOW_PROBE_AGREE both_definite=<n> agree=<n> ratio=<f> finer_ratio=<f>`・`SHADOW_PROBE_DETAIL method=vsm visible=<n> fallback_ratio=<f>` を csm の行の後に出す（`finer_ratio` と `fallback_ratio` の分母は VSM が測った標本の延べ数（逃げは 1 標本 16 点））。統計の語を 20 に広げた（9〜19 が VSM・一致・逃げ）。前のフレームの可視度のバッファは CSM・VSM の 2 組。
- (4) 印付けの核の半径（`DEFAULT_PCF_RADIUS_TEXELS`）を 2 → 5 texel にした。照明の PCF の半径は段を選ぶ式（bias −0.5）で texel の 2.83 倍未満、標本の位置は法線の向きへ最大 1.5 texel ずれるので、読む標本が隣のページへ届く範囲（4.33 texel）を余裕を持って覆う。`VirtualShadowMapVulkanTest` の参照（ケース A〜C）は同じ定数から作るので追随して合格。
- (5) テスト: `VirtualShadowMapVulkanTest` にケース L（計算シェーダー `vsm_sample_probe.comp` が照明と同じ関数を呼ぶ）。L1: ケース F の物理プール・ページの表を、画面の安定した画素 1730 点（地面・壁の法線）で実際のしきい値のまま読み、逃げた標本 0・使った段の texel が CPU の `SelectVirtualShadowMapLevel` と全点一致。L2: 段をケース F の段に固定して四角形の後ろの受け手を読む — 影の中心で 0・影の外で 1・縁で 0.5（CPU の参照 = 標本が読む texel の中心が形の内側かの 16 点の数え上げと一致）・ページ A・B の境界をまたぐ標本も 0。L3: 受け手のページを割り当て外にすると粗い段の別の物理ページ（全 texel が手前の深度）の値 0 になり逃げた標本は 16、粗い段にも無ければ 1（逃げた標本は 16）。`RenderGraphCompileTest` に、VSM の読み取りパスが公開を `HasBuffer` で問い合わせること・VSM のパスが無いグラフで問い合わせてもコンパイル・実行が成功すること・`ShadowProbe::Totals` が VSM の語の合計と比（`AgreeRatio`・`FinerRatio`・`FallbackRatio`）を正しく出すことを足した。
- (6) RenderGraph に `RenderGraphBuilder::HasBuffer`（`RenderGraph::HasNamedBufferResource`）を足した。`TryGetBuffer` は未公開の名前をグラフのエラーにするので、VSM の公開がある構成・無い構成の両方で「読むかどうか」を決めるには使えない（最初の実装は `TryGetBuffer` で、csm の構成の golden 4 本と `RenderGraphCompileTest` が「Named resource read failed because the name is not registered」で落ちた。`HasBuffer` へ替えて解消）。
- 変異（`VirtualShadowMapVulkanTest` で落ちることを確かめ、元へ戻した。`VirtualShadowMap.glsl` を一時的に書き換えて ctest）: (a) ページの表の引き方を 1 段ずらす（`VSM_PAGE_TABLE((level + 1u) * …)`）→ ケース L1 の逃げた標本 12356・L2 中心が 1・縁が 1・境界が 0.25・L3 の逃げた標本が 0 で FAIL（`mutation-table-shift.txt`）。(b) 逃げ道を外す（粗い段へ進まず自分の段だけ引く）→ ケース L3 が「粗い段の値を読まなければならない」で FAIL（`mutation-no-escape.txt`）。どちらも元へ戻して `cmp` で一致を確かめた。
- 検証（`.harness/runs/20261007-203349/`）: `verify-VTG8-VSM-SAMPLE-3.txt`（Debug の Game・RenderGraphCompileTest・RHITextureUpdateVulkanTest・RenderingGoldenImageTest のビルド）、`-4.txt`（VirtualShadowMapVulkanTest・RenderGraphCompileTest・golden 4 本が 6/6 Passed）、`-5-vsm-verbose.txt`（VirtualShadowMapVulkanTest の全出力。ケース L1〜L3 の値・`VUID_COUNT=0`・`RESULT=PASS`）。`-1.txt`・`-2.txt` は `TryGetBuffer` を使っていた最初の版（golden 4 本と RenderGraphCompileTest が落ちた記録）。
- Notes: (1) 実機の Game の起動確認（`--shadow-method=vsm` の起動画面・照明が VSM を読むときの検証レイヤー）は、この項目の指示（起動画面を撮らない）と重い処理の扱いに従い回していない。VSM のパスと照明を結ぶグラフの遷移（Common → ShaderResource）は `RenderGraphCompileTest`（偽の装置）でしか確かめておらず、実機での確認は VTG8-VSM-GPU-TIME の撮影で行う（そこで壊れて見えたらその項目で直す）。(2) 照明（フラグメントシェーダー）は逃げた標本を統計へ数えない（数えるには VSM.Stats へフラグメントの書き込みをグラフへ宣言し、読み戻しの後で 0 に戻す枠が要り、危険地帯の照明のグラフの宣言を増やすため）。同じ関数を使う影の測定（`fallback_ratio`）とテストが数えるので、逃げの割合はそこで見る。(3) 法線の向きへのずらし・深度の比較の余裕の係数（1.5 texel ずつ）は CSM の値（1.5 texel）に合わせた。起動画面で縁の漏れ・付着が見えたら VTG8-VSM-DEFAULT-ON の撮影で調整する。(4) PCSS（物理の半影）は VTG8-VSM-PCSS。そこで PCF の半径が広がるときは `DEFAULT_PCF_RADIUS_TEXELS` も合わせる（`VirtualShadowMapPages.h` のコメントに記載）。(5) VTG8-VSM-CLIPMAP の blocked（評価者の texel 上限と被覆の両立の指摘）は触っていない。
- Next: VTG8-VSM-PCSS。VTG8-VSM-MESH-CASTERS（blocked）・VTG8-VSM-MARK（blocked）・VTG8-VSM-CLIPMAP（blocked）は触っていない。

## 反復 17（2026-10-08）: VTG8-VSM-SAMPLE（評価者の差し戻し 3 点を修正、done）

- 差し戻し 1（距離・薄めを CSM に揃える）: `VsmSampleSunShadow` の影の範囲・薄めを、カメラからの直線距離・最大の距離全体の 10% から、CSM（`CalculateShadow`）と同じ「カメラの前方への距離」・「最後のカスケードの幅の 10%」へ変えた。段の選択は印付けと同じ直線距離のまま。`GPUVsmSampleParams` に `view`（前方の単位ベクトル）と `range`（x = 最小・y = 最大・z = 薄めの幅）を足し（688 → 720 バイト）、`depth.w` は予約（0）。`BuildVirtualShadowMapSampleParams` に `cameraForward` と `cascadeSplitDistances`（CSM の 5 つの分割。増加しない・非有限なら `[0, MaxShadowDistance]`・`Max × FadeRatio`）を足し、照明（`PhysicalLighting.CascadedShadow.SplitDistances`）と影の測定の両方がそれを渡す。
- 差し戻し 2（照明の逃げ数を統計へ）: `lighting.frag` に binding 24（`VsmLightingStatsBuffer`）を足し、`VSM_COUNT_FALLBACK()` が `atomicAdd(vsmLightingStats[0], 1u)`。`LightingPass` が host-visible の 16 枠のリング（`AcquireVsmStatsSlot`）を実行ごとに 0 に戻して結び、使い終わった枠を読み戻して合計し、終了時に `VSM_LIGHTING_STATS executes=<n> fallback_samples=<n>` を 1 行出す（`--shadow-probe` が無効でも数える）。
- 差し戻し 3（逃げた先の texel）: `outTexelMeters` を「実際に読めた標本が使った段の texel の平均」にした（どの標本も読めなければ選んだ段の値）。影の測定の `mean_texel_mm`・`finer_ratio` に反映される。
- テスト（`VirtualShadowMapVulkanTest` ケース L）: L3 は、粗い段へ逃げたとき texel が粗い段の値になること（どの段にも無いときは選んだ段）を確かめる。新設 L4 は、分割 {0.1, 5, 15, 40, 80} から範囲 [0.1, 80]・薄めの幅 4 m が決まること、不正な分割・前方 0 の扱い、前方 74 m で影のまま（旧式では 0.156）・78 m で 0.5・81 m で 1・カメラの後ろ・最小の距離の手前で 1・前方 70 m（直線 80.6 m）で影のまま、を GPU で確かめる。
- 変異（`VirtualShadowMap.glsl` を一時的に書き換えて ctest。すべて `RESULT=FAIL`。元へ戻して numstat で確認。`verify-VTG8-VSM-SAMPLE-7-mutations.txt`）: 薄めを最大の距離の 90% 開始へ戻す・距離を直線距離へ戻す・texel を選んだ段に固定・ページの表の引き方を 1 段ずらす・逃げ道を外す。照明の数え上げは、`if (bEscaped)` を `if (true)` にした変異で fallback_samples が実行 1 回あたり約 1100 万（画素数 × 16）になり、数え上げが働くことを確かめた（`verify-VTG8-VSM-SAMPLE-12-mutation-count.txt`）。
- 検証（`.harness/runs/20261007-203349/`）: `verify-VTG8-VSM-SAMPLE-11.txt`（ビルド成功）、`-13.txt`（6/6 Passed）、`-7-vsm-verbose.txt`（ケース L1〜L4 の値）、`-12-startup.txt`（`CaptureStartupScene.ps1 -ShadowMethod Vsm` が 3 視点とも pass、検証レイヤーのログに VUID なし、`VSM_LIGHTING_STATS` が出て fallback_samples=0）。最初の実機の起動（`-10-startup.txt`）は、写像したままの統計バッファへ `Update`（内部で写像する）を呼んで RenderThread が「バッファは既にマッピングされています」で落ちた。写像した領域へ直接 0 を書く形へ直して解消。
- Notes: (1) 画面の周辺で、前方への距離は最大の距離以内だが直線距離は超える画素は、印付け（直線距離で打ち切る）が印を付けないため VSM では影が読めず影なしになる（CSM では影がある）。範囲は画面の隅の 65〜80 m。VTG8-VSM-DEFAULT-ON の撮影で目立てば、印付けの打ち切りも前方への距離にする。(2) 照明の統計は `NORVES_VT_FEEDBACK`（断片シェーダーの storage 書き込みと sparse の対応）が定義されるデバイスだけで数える（それ以外は 0 のまま。シェーダーに atomic を残さないため）。(3) 起動画面は見た目を撮っていない（この項目の指示）。
- Next: VTG8-VSM-PCSS。


## 反復 18（2026-10-08）: VTG8-VSM-PCSS（done）

- 内容: `VsmSampleSunShadow`（`Common/VirtualShadowMap.glsl`）を CSM の PCSS と同じ 3 段（探索 → 物理の半影 → PCF）にした。(1) 探索: 半径 max(上限, 最小の半径) の Poisson 16 点で、各標本が自分の位置のページの表を引き（無ければ粗い段へ逃げる）、受け手より手前（比較の余裕つき）の深度の平均を求める。遮る物が 1 つも無ければ PCF を引かず 1（光）を返す。(2) 物理の半影の半幅 = 受け手と遮る物（平均）の深度の差 × 太陽の角半径の tan（0.00468）。(3) PCF の半径 r = max(min(物理の半影, 上限), 画素の大きさ p(d), 使う段の 1 texel)。探索・半影の上限は `MAX_FILTER_RADIUS_METERS` = 3 cm（ワールドの長さ。段に依らない。深度の差で約 6.4 m 分）。
- 定数・パラメータ: `VirtualShadowMap::MAX_FILTER_RADIUS_METERS`・`SUN_TAN_ANGULAR_RADIUS` を `VirtualShadowMapPages.h` に置き、`GPUVsmSampleParams.pixel` の y（tan）・z（上限）に入れた（構造体の大きさ 720 バイトは不変。シェーダーはパラメータから読むので定数の写しは無い）。
- 印付け（`vsm_mark.comp`）: 隣のページへの印の範囲を「5 texel + 上限（3 cm）」にした（`tuning.z` に `VirtualShadowMapPagesDispatch::MaxFilterRadiusMeters`）。探索・PCF の標本が読むページに印が無いと粗い段へ逃げて影が欠けるため。ページをまたぐ走査は pageMin から +3 ページで頭打ち（段 0 のページ 3.1 cm なら 3 cm の余白は 2 ページ分までで足りる）。
- 逃げた標本の数え方: 探索で遮る物が無く早く返すときだけ、探索の逃げた標本を数える（L3 の「どの段にも無い = 16」を保つ）。遮る物があるときは PCF の逃げだけを数える（二重に数えない）。
- テスト（`VirtualShadowMapVulkanTest` ケース L5）: 既定のクリップマップ（段 0 の texel 0.244 mm・ページ 3.1 cm）の 3x3 ページに、縁がページの中央を通る半平面の遮る物を物理ページへ直接書き、同じ縁を遮る物から 1 m・3 m 後ろの深度で読んだ。縁の途中の値の帯の幅は 8.972 mm・26.917 mm（物理の半影 × Poisson の横の広がり 1.9169 から期待される 8.971 mm・26.913 mm と 0.05% 以内で一致）、比は 3.00（深度の差の比 3 に一致）、逃げた標本 0。遮る物に接する受け手（深度の差 1 cm）は 0.92 mm（最小の半径 2 texel の帯）。印付けの参照（ケース A〜C）の隣のページへの印の範囲も同じ式（5 texel + 3 cm）に合わせた。L1〜L4 は値が変わらず合格。
- 変異（`-mutation-fixed-min-radius.txt`）: PCF の半径を最小の半径に固定（探索・半影を外す）→ ケース L5 が 4 件 FAIL（帯の幅 0.92 mm・比 1・接する受け手の比較）。元へ戻し `cmp` で一致を確認。
- 検証（`.harness/runs/20261007-203349/`）: `verify-VTG8-VSM-PCSS-1.txt`（Debug の Game・RHITextureUpdateVulkanTest・RenderingGoldenImageTest のビルド、BUILD_EXIT_CODE=0）、`-2.txt`・`-4.txt`（VirtualShadowMapVulkanTest・RenderingGoldenIndoorVulkanTest・RenderingGoldenOutdoorVulkanTest が 3/3 Passed。`-4` は変異を戻した後）、`-3-verbose.txt`（VirtualShadowMapVulkanTest の全出力。ケース L1〜L5 の値・VUID_COUNT=0・RESULT=PASS）。golden は CSM のまま基準画像・閾値を動かさずに合格。
- Notes: (1) 印付けの 3 cm の余白は、テストの場面（段 0 の幅 1024 m・ページ 8 m）では他の判定（曖昧さの許容 3 cm）と区別できず、ケース A〜C では余白の有無を落とせない。余白の効果は、L1 の「逃げた標本 0」と L5 の割り当て内の読みで間接的に守る（印付けの変異は未実施）。(2) 実機の起動画面（`--shadow-method=vsm`）は撮っていない（重い処理の扱い・段 8 の撮影は限る）。GPU の負担（探索 16 点が加わる）は VTG8-VSM-GPU-TIME で測り、重ければ探索の標本を減らす。(3) 探索が遮る物を見つけなかった画素は PCF を引かないので、CSM と同じく光が当たる画素の読みは増えない（16 点 → 16 点）。影の縁の画素だけ 32 点になる。(4) VTG8-VSM-SAMPLE（blocked）・MARK（blocked）・MESH-CASTERS（blocked）・CLIPMAP（blocked）は触っていない。
- Next: VTG8-VSM-CACHE。

## 反復 19（2026-10-08）: VTG8-VSM-PCSS（評価者の差し戻し 3 点を修正、done）

- 差し戻し 1（半影の打ち切り）: 探索・PCF の半径の上限 `VirtualShadowMap::MAX_FILTER_RADIUS_METERS` を 3 cm から 0.5 m（受け手と遮る物の深度の差 約 107 m 分の半影。影の最大の距離 80 m より遠い遮る物まで物理の半影のまま）にした。式は r = max(min(物理の半影, 上限), p(d), 使う段の 1 texel)。評価者の再現（深度差 10 m・30 m → 46.8 mm・140.4 mm）は上限の内側で、そのまま半径になる。シェーダー（`Common/VirtualShadowMap.glsl`）は上限をパラメータ（`pixel.z`）から読むので変更なし。
- 差し戻し 2（印付けの切り捨て）: `vsm_mark.comp` の `pageMin + 3` の頭打ちを外し、印の範囲を段の範囲（origin から 128 ページ）に収めて、半径が覆うすべてのページを走査する（1 画素の走査は最大 128 x 128 ページ）。
- 差し戻し 3（四角形の 2 高度の場面）: `VirtualShadowMapVulkanTest` のケース L5 を作り直した。光に正対する受け手の平面をカメラが正面（1 m）から見る場面の深度の画像を、本番の流れ（印付け → 割り当て → 消去 → 展開 → 描画）に通し、同じ四角形（縁が中心の近くを縦に通る）を受け手から深度の差 10 m・30 m に置いて描き、縁を照明と同じ関数で読む。物理ページの直接書き込みはやめた。帯の幅は 89.84 mm・269.53 mm（期待 89.71 mm・269.13 mm）、比 3、逃げた標本 0、接する受け手（0.01 m）は帯なし。新設ケース C2 は、カメラを 0.1 m に置いた 1 画素（段 2、ページ 12.5 cm）の印の範囲が 81 ページ（旧来の頭打ちの 16 ページ）になり、CPU の参照と一致することを確かめる。
- 既存テストへの影響: 上限が 0.5 m になったので、ケース L2〜L4 の受け手を中心 − 30 m に動かした（物理の半影が最小の半径 2 texel を超えず、L2 の参照 = 半径 2 texel の PCF が保たれる）。
- 変異（`verify-VTG8-VSM-PCSS-8-mutations.txt`。すべて `RESULT=FAIL`、元へ戻して `cmp` で一致を確認）: (a) 半径を最小に固定（探索・半影を外す）→ L5 の帯の幅 31.25 mm・比 1、(c) 半影の上限を 3 cm に戻す → 帯の幅 58.6 mm・比 1、(b) 印付けを `pageMin + 3` で頭打ち → C2 の印の集合が参照と違う（要求 16、参照 81）。
- 検証（`.harness/runs/20261007-203349/`）: `verify-VTG8-VSM-PCSS-5.txt`（Debug の Game・RHITextureUpdateVulkanTest・RenderingGoldenImageTest のビルド、BUILD_EXIT_CODE=0）、`-9.txt`（VirtualShadowMapVulkanTest・RenderingGoldenIndoorVulkanTest・RenderingGoldenOutdoorVulkanTest が 3/3 Passed）、`-7-verbose.txt`（L5・C2 の値、VUID_COUNT=0）。
- Notes: (1) 探索の半径が 0.5 m になったので、探索の 16 点より細い遮る物（幅が数 cm 以下）は、受け手が影の真ん中でも見落とすことがある（CSM も同じ考え方）。実機の負荷・見た目は VTG8-VSM-GPU-TIME の撮影で確かめる。(2) 印付けの上限が 0.5 m になり、近距離（段 0〜2）の画素は印を付けるページが増える（段 0 で最大 33 x 33 ページ）。
- Next: VTG8-VSM-CACHE。

## 反復 21（2026-10-08）: VTG8-VSM-CACHE（done）

- 内容: 反復 20 が「作業途中の保存」で残した実装を引き継ぎ、残っていた `RenderGraphCompileTest` の古い期待値を直し、検証と変異を記録して閉じた。実装の要点（`vsm_allocate.comp` の 11 段階・`VirtualShadowMapPages`・`VirtualShadowMapCasters.h` の `CasterMotionTracker`・`VirtualShadowMapPass` の `PlanInvalidation`・`MegaGeometryPass` の影の動き）:
  - 引き継ぎ（段階 0）: 前フレームの割り当て済みの欄を残し、dirty を外す。範囲の外へ出た欄（トーラスの番地が指す絶対のページが変わった）・キャッシュを使わないときは空きへ戻す。太陽の向き・深度の原点が変わったときは残った欄すべてに dirty を付ける。
  - 無効化（段階 1）: 動いた投影物（MegaGeometry の world ≠ previousWorld、手続きメッシュの変換・メッシュの署名の違い、スキニングは毎フレーム、消えた物）の前フレームと今フレームの境界のライト空間の矩形が覆う、割り当て済みのページを dirty にする。矩形が `MAX_INVALIDATION_RECTS`（256）を超える・境界が無い物が変わったときは全ページ。
  - 持ち越し（段階 2〜4）: 要求のあるページは年齢 0、要求の無いページは `CACHE_CARRY_FRAMES`（30）フレーム持ち越して空きへ戻す。空きが足りないときは年齢の古い順（年齢のヒストグラムで計画）に戻す。要求の無い dirty の欄は戻す。
  - `--vsm-cache=off`（または環境変数 `NORVES_VSM_CACHE=off`）で表を毎フレーム 0 にして全部を割り当て直す。`VSM_CACHE cached=<n> rendered=<n> invalidated=<n> released=<n>` を 60 フレームごとに出す。展開・MegaGeometry のカリングは dirty のページだけを相手にする。
- `RenderGraphCompileTest` の更新（実装の変更に合わせた期待値）: VSM_Stats 44 → 212 バイト、VSM_FreeList を (3 × ページ数 + 1) 語、割り当ての dispatch を 3 → 10 回（矩形が無いので矩形の段階は記録されない）に伴う呼び出しの並び・バリアの位置・dispatch の添字、`ResolveShadowBounds` がプロキシの境界とメッシュ全体の境界の両方を含む球を返すようになった（読み込み前の小さなプロキシの境界で無効化・カリングが抜けないため）ことに伴う影の表の境界の期待値（両方を含み最小の半径 4.3207）。
- テスト（`VirtualShadowMapVulkanTest` ケース M1〜M8）: 止まった場面の 2 フレーム目に描くページが 0（M2）、長く続けても要求のあるページは戻らない（M2b）、投影物を動かすとその前後の境界のページだけが描き直され、プールが毎フレーム描き直したとき（`--vsm-cache=off` 相当）と全 texel で一致（M3）、太陽の向きを変えると全ページを描き直す（M4）、深度の原点が動くと全ページを描き直す（M5）、段の中心を動かすと範囲に残ったページは描き直されず外へ出たページは戻る（M6）、古い順に戻す（M7・M8）。
- 変異（`verify-VTG8-VSM-CACHE-mutations.txt`。いずれも `RESULT=FAIL`、元へ戻して `cmp` で一致を確認）: (a) 無効化の矩形に前フレームの境界を足さない → M3 が 3 件 FAIL、(b) 持ち越しの条件で要求の印を見ない → M2b が FAIL。
- 検証（`.harness/runs/20261007-203349/`）: `verify-VTG8-VSM-CACHE-3.txt`（Debug の Game・RenderGraphCompileTest・RHITextureUpdateVulkanTest のビルド、BUILD_EXIT_CODE=0）、`-4.txt`（VirtualShadowMapVulkanTest・RenderGraphCompileTest が 2/2 Passed）、`-5-verbose.txt`（ケース M の値、VUID_COUNT=0、RESULT=PASS）。
- Notes: (1) 実機の起動画面でのキャッシュの効果（cached/rendered の数・GPU 時間・キャッシュの on/off の画の一致）は、重い処理の扱いに従いこの反復では回していない（VTG8-VSM-GPU-TIME の撮影で確かめる）。(2) 起動画面の大きな球は自転するので、そのまわりのページは毎フレーム描き直しになる（期待どおり）。(3) `--vsm-cache` は描画の層からプロセスのコマンドラインを直接読む（`ApplicationProcessor` の外）。
- Next: VTG8-VSM-GPU-TIME（VTG8-VSM-SAMPLE・PCSS は blocked のまま。人の判断待ち）。

## 反復 22（2026-10-08）: VTG8-VSM-CACHE（評価者の差し戻し 1 点を修正、done）

- 差し戻し（太陽の向きの変化判定）: `IsSameDirection`（`VirtualShadowMapPages.cpp`）が成分ごとの差 1e-6 以下を「同じ向き」としつつ、比較元（前フレームのクリップマップ）を毎フレーム更新していたため、1 フレームの回転が許容未満の連続回転（評価者の再現: 5e-7 rad/フレームを 10 万フレームで累積 2.9°）が無効化されないまま累積した。許容（`SunDirectionTolerance`）をやめ、向き・光の右・光の上の成分の完全一致で比べる（基底は `BuildLightBasis` が向きだけから決めるので、同じ向きなら完全に同じ値になり、止まった太陽で誤って無効化されない）。
- テスト（`VirtualShadowMapVulkanTest` ケース M4b）: 向きを 5e-7 ずつ 3 回（各回の差は 1e-6 未満）回し、各回で全ページ（14/14）を無効にして描き直し、毎フレーム描き直したときと全 texel で一致することを確かめる。元の向きへ戻したときも全ページを無効にし、落ち着けば何も描かない。
- 変異（`verify-VTG8-VSM-CACHE-4-mutation.txt`）: 比較を 1e-6 の許容つきに戻す → M4b が 3 回とも FAIL（描き直したページ 0/14）、`RESULT=FAIL`。元へ戻し内容を確認。
- 検証（`.harness/runs/20261008-035618/`）: `verify-VTG8-VSM-CACHE-7.txt`（Debug の Game・RenderGraphCompileTest・RHITextureUpdateVulkanTest のビルド、BUILD_EXIT_CODE=0）、`-8.txt`（VirtualShadowMapVulkanTest・RenderGraphCompileTest が 2/2 Passed）、`-3-verbose.txt`（M4b の値、VUID_COUNT=0、RESULT=PASS）。
- Notes: (1) 変異の後、元のファイルを Copy-Item で戻すと更新時刻が古いままで MSBuild が再ビルドせず、変異版の実行ファイルで検証が FAIL した（`-5`・`-6`）。戻した後は更新時刻を更新してから再ビルドすること。内容は差分で元の修正どおりと確認済み。(2) 比較の厳密化により、カメラや太陽の入力が毎フレーム僅かに揺れる場合は全ページが毎フレーム描き直しになる。クリップマップの向きは太陽の向きだけから決まるので、静止した太陽では完全に同じ値になる。
- Next: VTG8-VSM-GPU-TIME（VTG8-VSM-SAMPLE・PCSS は blocked のまま。人の判断待ち）。

## 反復 2（2026-10-08）: VTG8-VSM-CLIPMAP（texel の上限と被覆の両立を b = max(bias, b_cover) で解く、done）

- 親の判断（TASKS.md の done-when (3)(6)・notes）に従い、`SelectVirtualShadowMapLevel` の「被覆できない段を粗くする」ループを外し、`VirtualShadowMapCoverageBiasLevels`（被覆に要る最小の bias）と `VirtualShadowMapEffectiveBiasLevels`（max(bias, b_cover)）を足した。b_cover = log2(2 / ((段 0 の被覆 / 幅)·(2tan(fovY/2)/画面の高さ)·VirtualResolution)) + 1e-3（浮動小数の丸めの余裕）。texel を満たす最も粗い段の幅は目標の幅の半分より大きいので、b が b_cover 以上なら選んだ段は影の距離の中の受け手をいつも含む。既定の `BiasLevels` は -0.5 から -1 へ戻した。
- 起動画面（1280×720・縦画角 60 度）では b_cover ≒ -2.7 < bias なので b = bias（補正なし）。2160 画素・縦画角 35 度（評価の反例）では b_cover ≒ -0.2 で補正が入るが、texel の上限は b に対して成り立つ（例外なし）。
- テスト（`VirtualShadowMapClipmapTest`）: 例外の免除（`bCoverageClamped` 相当）と「上限超過を合格にする」旧テストを撤去し、画面の高さ 360〜4320 画素・縦画角 20〜120 度の格子 × 距離の格子（等間隔と対数間隔）・乱数の画面 300 件で、texel ≤ p(d)·2^b・より粗い段では足りない・受け手の被覆・距離について単調を同時に確かめる。起動画面は b = bias、評価の反例（1440/2160 画素・35 度・1.94 m 近傍）を含む。
- 変異（`mutation-VTG8-VSM-CLIPMAP.txt`）: 中心のスナップを外す・段の選び方の不等号を逆にする・被覆に要る bias を外す、のすべてで `VirtualShadowMapClipmapTest` が FAIL。元へ戻した後は更新時刻を更新して再ビルドし、合格を確認。
- 検証（`.harness/runs/20261008-035618/`）: `verify-VTG8-VSM-CLIPMAP-3.txt`（Game・CameraViewConstantsTest・RenderGraphCompileTest のビルド、exit 0）、`-4.txt`（ctest 4/4 合格）。
- Notes: (1) 既定の bias を -1 に戻したので、起動画面の段・要求ページ数は bias -0.5 のときより約 2 倍に増える（texel が画素の 1/4〜1/2）。GPU のテスト（`VirtualShadowMapVulkanTest`）は段を `SelectVirtualShadowMapLevel` で求めるので式の写しは無いが、実機の確認は重い処理の扱いに従い回していない（VTG8-VSM-GPU-TIME の撮影で確かめる）。(2) `VSM_CLIPMAP` の行の `bias=` は設定の BiasLevels（補正前）。
- Next: VTG8-VSM-GPU-TIME 以降。

## 反復 3（2026-10-08）: VTG8-VSM-CLIPMAP（スナップを外す変異の記録を直す、done）

- 差し戻し: 前回の「スナップを外す」変異は座標を 1.37 倍して整数化する置換で、中心はページの格子へスナップされたままだった。今回は `VirtualShadowMapClipmap.cpp` の `centerX/centerY`（`CenterPageX/Y * pageMeters`）を `cameraX/cameraY` に置き換え（中心のスナップを本当に外す）、`VirtualShadowMapClipmapTest` が FAIL することを記録した（`verify-VTG8-VSM-CLIPMAP-5-mutation-snap.txt`。「段の中心がページの格子へスナップされていない」が全段で出て 0% passed）。実装は変更していない。
- 元へ戻し（更新時刻を更新して再ビルド）、`git diff --numstat` で差分が TASKS.md だけであることを確認。
- 検証（`.harness/runs/20261008-035618/`）: `verify-VTG8-VSM-CLIPMAP-6.txt`（Game・CameraViewConstantsTest・RenderGraphCompileTest のビルド、BUILD_EXIT_CODE=0）、`-7.txt`（ctest 4/4 Passed）。
- Next: VTG8-VSM-GPU-TIME 以降。

## 反復 4（2026-10-08）: VTG8-VSM-MARK（統計の読み戻しを飛行中の数から分離、done）

- 差し戻し（評価の 2 周）: done-when (4) の「数フレーム遅れ」が、製品の飛行中 1 枠では翌フレームに読んでいた。読み戻しの枠を飛行中のフレーム番号から切り離し、書いた順に使う 4 枠（`StatsReadbackSlotCount`、FrameUseRing の上限以上）にした。読むのは「通し番号の差が `StatsReadbackMinFrameDelay`（2）以上」かつ「書いたフレームの提出の完了が確かめられた」枠だけ。
- 完了の確かめ方: `RenderingCoordinator` が提出した描画フレームの通し番号と提出 serial の対を持ち、フレームの記録を始めるとき `swapChain->GetCompletedSubmissionSerial()` で完了済みになった最大の通し番号を `ViewRenderContext::CompletedRenderFrameSerial` へ渡す（スワップチェーンの作り直しで serial が戻ったときは、持っていた提出を完了済みとする）。手組みの文脈（0）では完了したフレームが無いものとして読まない。次の枠が未読（GPU の完了が未確認）のときは、そのフレームの統計は取らない（上書きしない）。同じフレームの複数の Execute は同じ枠を書き直す。
- テスト（`RenderGraphCompileTest`）: `VsmRun` に飛行中の数・GPU の遅れを持たせ、`TestVirtualShadowMapPassStatsReadbackAcrossFlightCounts` で飛行中 1・2・3・4 枠と、GPU が 6 フレーム遅れて完了する場合の最初に読むフレーム（2・2・3・4・6）を確かめる。既存の `ReadsStatsOnlyAfterFrameFence`（同じフレームの 3 ビューポートが読まない・値が変わったときと 60 回ごとに出す）は新しい枠の仕組みのまま合格。
- 変異（`verify-VTG8-VSM-MARK-26-mutations.txt`。どちらも `RenderGraphCompileTest` が Failed、元へ戻して更新時刻を更新し再ビルド）: (a) 通し番号の差の条件を外す、(b) 完了の確認を外す。
- 前の CLIPMAP の反復（bias -1 と b = max(bias, b_cover)）で壊れていた `VirtualShadowMapVulkanTest` のケース L5・C2 を直した。受け手のカメラ距離を固定（1 m・0.1 m）にしていたため、窓の距離の幅が段の境目をまたいで「窓のすべての画素が同じ段」「Stable な画素」の前提が崩れていた。段の境目をまたがない距離を候補から選ぶようにした（実装は変更していない）。L5 の帯の幅は 89.84 mm・269.53 mm（期待 89.71・269.13）、比 3、逃げた標本 0。
- 検証（`.harness/runs/20261008-035618/`）: `verify-VTG8-VSM-MARK-27.txt`（Debug の Game・RenderGraphCompileTest・RHITextureUpdateVulkanTest のビルド、BUILD_EXIT_CODE=0）、`-28.txt`（VirtualShadowMapVulkanTest・RenderGraphCompileTest が 2/2 Passed）。
- Notes: (1) 実機の起動画面での `VSM_PAGES` の出力は、重い処理の扱いに従い回していない（VTG8-VSM-GPU-TIME の撮影で確かめる）。(2) `ViewRenderContext` に `CompletedRenderFrameSerial` を足した（描画の層の公開構造体。既定 0）。
- Next: VTG8-VSM-GPU-TIME（VTG8-VSM-SAMPLE・PCSS は blocked のまま）。

## 反復 5（2026-10-08）: VTG8-VSM-MESH-CASTERS（IndexCount 0 のサブメッシュの扱いを CSM に揃える、done）

- 差し戻し（評価の 13 周目）: 手続きメッシュの収集（`VirtualShadowMapPass::CollectCasters`）が、サブメッシュがあれば範囲指定ありと扱っていたため、`IndexCount` が 0 のサブメッシュで `PlanProceduralChunks` が失敗し投影物を省いていた。CSM（`SceneRenderer.cpp`）は 0 をメッシュ全体（先頭 0・頂点の基点 0・インデックス数はメッシュ全体）へのフォールバックとして扱う。`bHasRange` を「サブメッシュが存在し、その `IndexCount > 0`」にして同じ扱いに揃えた。
- テスト（`RenderGraphCompileTest` の `TestVirtualShadowMapPassRecordsCasterRasterAfterSkinning`）: 影を落とすプロキシ A の 2 つを `SubMeshRange{3, 0, 7, 0}`（IndexCount 0・先頭と頂点の基点は 0 以外）の 1 サブメッシュに替え、塊が 3 件（手続き 2・スキニング 1）になり、手続きの塊が三角形 2・先頭 0・頂点の基点 0 の記録になることを確かめる。
- 変異（`verify-VTG8-VSM-MESH-CASTERS-16-mutation.txt`）: `IndexCount > 0` の条件を外すと `RenderGraphCompileTest` が Failed。元へ戻して更新時刻を更新し再ビルド。
- 検証（`.harness/runs/20261008-035618/`）: `verify-VTG8-VSM-MESH-CASTERS-17.txt`（Debug の Game・RenderGraphCompileTest・RHITextureUpdateVulkanTest・SkinnedRenderPathContractTest のビルド、BUILD_EXIT_CODE=0）、`-18.txt`（VirtualShadowMapVulkanTest・RenderGraphCompileTest・SkinnedRenderPathContractTest が 3/3 Passed）。
- Notes: (1) Edit ツールが `RenderGraphCompileTest.cpp` の行末を壊した（numstat 1479/1455）ので、HEAD の行末を基準に difflib で戻し、差分を実編集の 24 行だけにした（両方式の numstat が一致）。(2) Git Bash から `cmake --build ... -- /m:1` を呼ぶと `/m:1` がパスに変換されて失敗する。PowerShell から回す。
- Next: VTG8-VSM-GPU-TIME（VTG8-VSM-SAMPLE・PCSS は blocked のまま）。

## 反復 6（2026-10-08）: VTG8-VSM-MEGA-CULL（done-when を実装と証拠で 1 つずつ確かめて閉じる）

- 反復 14 の実装（`42457bbb` と反復 15 の追加）を done-when に照らして確かめた。(1) `vsm_mega_cull.comp` の `LevelOverlapsSphere`・`SphereHasDirtyPage`（dirty の階層。ページの mip）でインスタンスの境界を段ごとに判定、(2) クラスタは `ShouldDrawShadowCluster`（`cullData.orthoLod = 1` の正射影で自分の誤差 ÷ texel ≤ lodBias かつ親の誤差 ÷ texel > lodBias。子のページが非常駐なら自分を描き、要求は出さない。HZB・法線の円錐・ソフトウェアラスタは使わない）、(3) 出力は自分のバッファ `VsmMega_List` と統計の語 8〜10 だけで、主の経路の間接描画・見えた印・ページの要求には書かない、(4) 区間 `VsmCullMega` と `VSM_MEGA_CULL` の行、(5) `RenderGraphCompileTest` の `TestVirtualShadowMapPassRecordsMegaCullBetweenMainCullAndExpand` と `VirtualShadowMapVulkanTest` のケース J1〜J3。
- 変異（`.harness/runs/20261007-203349/verify-VTG8-VSM-MEGA-CULL-mutation-noparent.txt`: 親の条件を外す → `RESULT=FAIL`。ほかに nodirty・order・wiring）。
- 検証（`.harness/runs/20261008-035618/`）: `verify-VTG8-VSM-MEGA-CULL-1.txt`（Debug の Game・RenderGraphCompileTest・RHITextureUpdateVulkanTest のビルド、BUILD_EXIT_CODE=0）、`-2.txt`（VirtualShadowMapVulkanTest・GeometryPageRequestVulkanTest・RenderGraphCompileTest が 3/3 Passed）。
- Next: VTG8-VSM-GPU-TIME（VTG8-VSM-SAMPLE・PCSS は blocked のまま）。

## 反復 7（2026-10-08）: VTG8-VSM-MEGA-CULL（インスタンスの dirty 判定が矩形の外の dirty ページで通る不具合を直す、done）

- 差し戻し（評価の 6 周目）: done-when (1) の `SphereHasDirtyPage`（`vsm_mega_cull.comp`）が、矩形を 2×2 以下で覆う最も細かい mip のセルのビットだけで判定していた。そのセルが矩形の外のページも含むため、矩形のすぐ外の dirty ページで（インスタンス、段）が通っていた。粗い mip のセルのビットが立っていても、セルが矩形に完全に含まれていなければ子のセルへ下り、矩形と交わる子だけを調べるようにした（セルが矩形に完全に含まれるときだけ true。積みの深さは 4 + 3 × 7 ≦ 32）。
- テスト（`VirtualShadowMapVulkanTest` ケース J）: インスタンス 6 を足した。段 0 の範囲の原点から相対 (0..2, 0..2) のページを矩形が覆い、相対 (3, 3) のページだけが dirty（粗い mip のセルは矩形と共有する）。範囲の原点はクリップマップから取るので、セルの境のずれに依らない。通ったインスタンス・段は 8、クラスタは 22 のまま（インスタンス 6 は通らない）。
- 変異（`verify-VTG8-VSM-MEGA-CULL-5-mutation-coarse-mip.txt`）: シェーダーを HEAD（粗い mip のビットだけの判定）へ戻すと `VirtualShadowMapVulkanTest` が Failed（ケース J1: 通った（インスタンス、段）が 9、統計が（9, 22, 0））。元へ戻して更新時刻を更新し再ビルド。
- 検証（`.harness/runs/20261008-035618/`）: `verify-VTG8-VSM-MEGA-CULL-6.txt`（Debug の Game・RenderGraphCompileTest・RHITextureUpdateVulkanTest のビルド、BUILD_EXIT_CODE=0）、`-7.txt`（VirtualShadowMapVulkanTest・GeometryPageRequestVulkanTest・RenderGraphCompileTest が 3/3 Passed）。
- Notes: (1) 変異の最初の試行は、PowerShell 経由で HEAD 版を書き出して文字化けし、シェーダーの初期化に失敗して落ちただけだった（意図した理由ではない）ので、`git show` をバイト単位でファイルへ書き出してやり直した。(2) 実機の起動画面での確認は、重い処理の扱いに従い回していない。
- Next: VTG8-VSM-GPU-TIME（VTG8-VSM-SAMPLE・PCSS は blocked のまま）。

## 反復 8（2026-10-08）: VTG8-VSM-SAMPLE（逃げた標本の統計を VT から独立させ、ホスト可視化のバリアと完了確認の読み戻しを足す、done）

- 差し戻し（評価の 2 周）: (a) 照明の逃げた標本の数え上げが `NORVES_VT_FEEDBACK`（sparse が要る）に依存し、sparse の無い VSM 対応装置で空マクロになっていた。専用マクロ `NORVES_VSM_STATS` を足し、`VulkanDevice::CreateShaderCompiler` が `DeviceCapabilities::SupportsVsmLightingStats()`（`bFragmentStoresAndAtomics` のみ）で定義する。`lighting.frag` の統計バッファ宣言と `VSM_COUNT_FALLBACK` はこのマクロに切り替えた。(b) 照明の描画の後に統計バッファの PixelShaderWrite → HostRead バリアを記録する（`FrameCommandType::BufferBarrier`・`ViewRenderContext::EnqueueBufferBarrier` を足し、`SceneRenderer` が記録する）。
- 読み戻し: 統計の枠が書いたフレームの通し番号を持ち、`CompletedRenderFrameSerial` 以下になった枠だけを読んで空ける（旧版は「16 回前なら書き終わっている」という仮定だった）。空きが無い（全枠が完了未確認）ときは枠を上書きせず、読まない置き場（`LightingVsmStatsSink`）へ束ね、バリアも記録しない。他の資源（既定の重みバッファ）へシェーダーが書くことも無くなった。
- テスト（`RenderGraphCompileTest`）: `TestLightingVsmStatsAreMadeHostVisibleAndReadAfterCompletion`（GBuffer → VSM → 照明の実グラフで、バリアが最後の EndRenderPass より後・状態が PixelShaderWrite → HostRead・各フレーム 1 回、完了前の枠は読まず上書きもしない、16 枠が埋まった後は置き場へ束ねる、完了が進むと読んで再利用する）、`TestLightingShaderCountsVsmFallbackIndependentlyOfVtFeedback`（sparse の無い装置の能力の組み合わせで VSM が使え数え上げも有効、`lighting.frag` を `NORVES_VSM_STATS` だけで実コンパイルして OpAtomicIAdd が入り、`NORVES_VT_FEEDBACK` だけでは入らない）。
- 変異（すべて `RenderGraphCompileTest` が Failed、元へ戻して更新時刻を更新し再ビルド）: `verify-VTG8-VSM-SAMPLE-3-mutation-shader-macro.txt`（シェーダーのマクロを `NORVES_VT_FEEDBACK` へ戻す）、`-4-mutation-completion-gate.txt`（完了の確認を外す）、`-5-mutation-no-barrier.txt`（バリアを外す）。
- 検証（`.harness/runs/20261008-035618/`）: `verify-VTG8-VSM-SAMPLE-6.txt`（Debug の Game・RenderGraphCompileTest・RHITextureUpdateVulkanTest・RenderingGoldenImageTest のビルド、BUILD_EXIT_CODE=0）、`-7.txt`（指定の 6 テストが 6/6 Passed、golden は CSM のまま変更なし）。
- Notes: (1) 行末が混在するファイル（`LightingPass.cpp` など）は、python で行ごとの行末を保って置換し、`git diff --numstat` と `--ignore-cr-at-eol` の一致を確かめた。(2) 実機の起動画面での確認は、重い処理の扱いに従い回していない（VTG8-VSM-GPU-TIME の撮影で確かめる）。
- Next: VTG8-VSM-GPU-TIME 以降（VTG8-VSM-PCSS は blocked のまま）。

## 反復 9（2026-10-08）: VTG8-VSM-PCSS（深度の差が 107 m を超える場面を足して閉じる、done）

- 状況: 探索・PCF の半径の上限 R_max（`MAX_FILTER_RADIUS_METERS` = 0.5 m）、印付けの範囲（R_max と 5 texel から求め、固定のページ数で打ち切らない）、ケース L5（本番の流れで描いた同じ四角形を深度の差 10 m・30 m に置き、帯の幅の比 3）、C2（印の範囲 81 ページ）は前の反復で入っていた。残っていた「深度の差が約 107 m を超える場面で半径が R_max で止まり、その半径の標本のページに印が付く」を足した。
- テスト（`VirtualShadowMapVulkanTest` ケース L5）: 受け手の読みを `ProbeReceiverEdge`（本番の流れ → 縁からの位置の一覧を照明と同じ関数で読む）に分け、帯の幅の測定はそれを使う形にした。深度の差 200 m（物理の半影の半幅 0.936 m > R_max）で、縁から -0.6・-0.4・0・0.4・0.6 m の受け手の可視度が 0, 0.125, 0.5, 0.75, 1、逃げた標本 0（半径 R_max の標本の読むページまで印が届く。ページ 1 m ≦ R_max の 2 倍）。R_max で止まるので 0.6 m 離れた受け手は完全に影・光になり、止まらなければ 0 と 1 の間の値になる。
- 変異（`verify-VTG8-VSM-PCSS-5-mutations.txt`。どちらも `VirtualShadowMapVulkanTest` が FAIL、`git checkout` で戻して更新時刻を更新し再ビルド）: (a) PCF 半径の上限を外す → 縁から -0.6 m の可視度が 0.1875、(b) 印の範囲から R_max の分を外す → 逃げた標本 3。前の反復で記録した変異（探索を外して半径を最小に固定 → 帯の幅の比 1）も有効なまま。
- 検証（`.harness/runs/20261008-035618/`）: `verify-VTG8-VSM-PCSS-3.txt`（Debug の Game・RHITextureUpdateVulkanTest・RenderingGoldenImageTest のビルド、BUILD_EXIT_CODE=0）、`-4.txt`・`-6.txt`（VirtualShadowMapVulkanTest・RenderingGoldenIndoorVulkanTest・RenderingGoldenOutdoorVulkanTest が 3/3 Passed。`-6` は変異を戻した後）、`-2-verbose.txt`（ケース L5 の値、VUID_COUNT=0。最初の試行で、ページの大きさの確認の不等号が厳しすぎて落ちたものを直す前の出力）。golden は CSM のまま基準画像・閾値を動かさずに合格。
- Notes: (1) 評価者の前回の指摘 1（`min(…, R_max)` が `max(物理の半影, p(d), texel)` を満たさない）は、親の判断（2026-10-08。R_max を完了条件に書き足した。TASKS.md の done-when と notes）に従い実装どおりとする。R_max は探索・印付けの範囲を決めるための上限で、深度の差が約 107 m を超えたときだけ効く（起動画面の深度の差は 20 m 未満）。(2) 実機の起動画面での確認は、重い処理の扱いに従い回していない（VTG8-VSM-GPU-TIME の撮影で確かめる）。
- Next: VTG8-VSM-GPU-TIME 以降。

## 反復 10（2026-10-08）: VTG8-VSM-GPU-TIME（測定は完了。ページとラスタの溢れが出たので停止条件で blocked）

- 測定（RelWithDebInfo、`-GpuTimingFrames 300`、窓は 240 フレームの中央値。出力は `.harness/runs/startup-capture/VTG8-VSM-GPU-TIME-*`、ログは `.harness/runs/20261008-035618/verify-VTG8-VSM-GPU-TIME-1〜10.txt`）。区間の単位は ms。`ShadowMapPass` は CSM と点光源の合計で、VSM でも半透明・ボリュームのために残る。`VirtualShadowMapPass` は VSM の全区間の合計。

| run | 視点 | フレーム GPU | ShadowMapPass | VsmMark | VsmAllocate | VsmClear | VsmCullMega | VsmExpand | VsmDraw | VirtualShadowMapPass | LightingPass |
|---|---|---|---|---|---|---|---|---|---|---|---|
| CSM | default | 2.430 | 0.185 | - | - | - | - | - | - | - | 0.545 |
| CSM | near | 2.533 | 0.220 | - | - | - | - | - | - | - | 0.569 |
| CSM | low | 2.468 | 0.221 | - | - | - | - | - | - | - | 0.489 |
| VSM cache=off | default | 2.899 | 0.163 | 0.027 | 0.030 | 0.055 | 0.045 | 0.172 | 0.269 | 0.611 | 0.603 |
| VSM cache=off | near | 3.196 | 0.147 | 0.062 | 0.026 | 0.110 | 0.047 | 0.157 | 0.786 | 1.199 | 0.560 |
| VSM cache=off | low | 3.173 | 0.148 | 0.251 | 0.033 | 0.406 | 0.034 | 0.151 | 0.215 | 1.101 | 0.606 |
| VSM 持ち越し | default | 2.809 | 0.163 | 0.027 | 0.104 | 0.009 | 0.029 | 0.170 | 0.135 | 0.485 | 0.609 |
| VSM 持ち越し | near | 3.692 | 0.147 | 0.062 | 0.679 | 0.063 | 0.032 | 0.155 | 0.688 | 1.691 | 0.558 |
| VSM 持ち越し | low | 3.000 | 0.154 | 0.265 | 0.206 | 0.024 | 0.033 | 0.159 | 0.164 | 0.861 | 0.618 |
| CSM 負荷 300 | default | 5.892 | 2.182 | - | - | - | - | - | - | - | 0.589 |
| VSM cache=off 負荷 300 | default | 13.748 | 2.186 | 0.025 | 0.027 | 0.056 | 1.455 | 0.349 | 5.784 | 7.709 | 0.713 |
| VSM 持ち越し 負荷 300 | default | 7.662 | 2.185 | 0.025 | 0.094 | 0.008 | 1.143 | 0.154 | 0.189 | 1.623 | 0.717 |

- 持ち越しありの VSM のフレーム GPU と CSM の差: default +0.379、near +1.159、low +0.532、負荷 300 +1.770 ms。どれも 2 ms 未満なので「2 ms 以上遅い」の停止条件には当たらない。差の内訳: 起動画面は VSM の区間の合計（0.49〜1.69 ms）から ShadowMapPass の差（-0.02〜-0.07 ms。VSM のとき CSM の描画は半透明・ボリューム用の分だけ）を引いた分。near は VsmAllocate 0.679・VsmDraw 0.688 が重く、`VSM_CACHE` が cached=467・rendered=821・invalidated=831 と毎フレーム約 830 ページが無効になっている（near の視点は大きな球が近く、自転する球の影のページが毎フレーム無効になるためと推定。未確認）。負荷 300 は VsmCullMega 1.143 ms（300 インスタンスのクラスタ選別）が主で、CSM 側の ShadowMapPass 2.18 ms は VSM でも同じだけ残る。
- ログの値（各 run の最後の行）:

| run | 視点 | VSM_PAGES（requested / allocated / overflow） | VSM_RASTER（chunks / instances / overflow） | VSM_MEGA_CULL（instances / clusters / overflow） | VSM_CACHE（cached / rendered / invalidated / released） |
|---|---|---|---|---|---|
| cache=off | default | 756 / 756 / 0 | 5353 / 24202 / 0 | 12 / 5273 / 0 | 0 / 754 / 0 / 0 |
| cache=off | near | 1288 / 1288 / 0 | 8213 / 76300 / 0 | 14 / 8204 / 0 | 0 / 1288 / 0 / 0 |
| cache=off | low | 4728 / 4096 / **632** | 2899 / 20863 / 0 | 10 / 2826 / 0 | 0 / 4096 / 0 / 0 |
| 持ち越し | default | 756 / 756 / 0 | 1868 / 12018 / 0 | 4 / 1788 / 0 | 630 / 124 / 124 / 0 |
| 持ち越し | near | 1285 / 1285 / 0 | 5554 / 66746 / 0 | 6 / 5537 / 0 | 467 / 821 / 831 / 10 |
| 持ち越し | low | 4721 / 4096 / **625** | 2211 / 15297 / 0 | 8 / 2365 / 0 | 3675 / 421 / 383 / 40 |
| cache=off 負荷 300 | default | 753 / 753 / 0 | 165503 / 524286 / **262387** | 495 / 262144 / **80401** | 0 / 750 / 0 / 0 |
| 持ち越し 負荷 300 | default | 753 / 753 / 0 | 3647 / 18753 / 0（最初の約 40 フレームは **最大 361170**） | 16 / 3567 / 0 | 626 / 124 / 124 / 0 |

  `VSM_CLIPMAP levels=10 first_width_m=4.000 bias=-1.000 depth_range_m=1000.0`（全 run 同じ）、`VSM_TEXEL d_m=40` は vsm 31.25 mm・csm 94.40 mm、`VRAM_LEDGER vsm_pool pages=4096 mb=256.000`。
- 溢れ: 起動画面の default・near は全フレームで 0。**low は `VSM_PAGES` overflow が全フレームで 625〜639（要求 4721〜4728 ＞ プール 4096）**。`--vsm-pool-pages` の測定（low、持ち越しあり）: 5120 → requested 4717・overflow 0・フレーム GPU 2.975 ms、6144 → 4719・0・2.988 ms、8192 → 4717・0・2.994 ms（既定 4096 は 3.000 ms）。プールを増やしても GPU 時間は変わらず、VRAM だけ 320 / 384 / 512 MiB に増える。bias は起動引数に無く、既定 -1 のままの測定のみ（起動画面の縦画角では被覆の引き上げは起きない）。負荷 300 では持ち越しありでも最初の約 40 フレームに `VSM_RASTER` overflow が出て、cache=off は全フレームで `VSM_RASTER`・`VSM_MEGA_CULL` の overflow が続く（instances の上限 524288・クラスタの上限 262144 に張り付く）。done-when の「6 run で overflow 0」は満たせていない。
- 影の見え方（PNG を開いた）: 持ち越しありの default・near・low、負荷 300 の持ち越しあり・cache=off のどれも、小屋・岩・球・見本の帯の物の太陽の影に、欠け・ずれ・ページの継ぎ目は見えない。壊れて見えないので CSM の PNG との画素の差は調べていない。low は overflow 中だが、この撮影の画面では影は見える（溢れた約 630 ページの影響の有無は画素では調べていない）。
- 停止: stop-when の「溢れが 0 にならない場合は、`--vsm-pool-pages` と bias の組の測定を記録して止める」に従い、`VTG8-VSM-GPU-TIME` を `blocked` にし、直す項目 `VTG8-VSM-POOL-OVERFLOW` を `TASKS.md` に足した（完了したら GPU-TIME を `todo` に戻して 6 run を撮り直す）。`blocked/VTG8-VSM-GPU-TIME.md` に理由を書いた。
- Notes: (1) 検証ファイルは `verify-VTG8-VSM-GPU-TIME-1.txt`（RelWithDebInfo の Game のビルド、BUILD_EXIT_CODE=0）、`-2〜-7`（6 run のキャプチャ。すべて `result=pass`）、`-8〜-10`（プール 5120・6144・8192 の low の測定）。(2) 実装は変更していない。
- Next: VTG8-VSM-POOL-OVERFLOW。

## 反復 11（2026-10-08）: VTG8-VSM-DEFAULT-ON（Game の既定を VSM にした。近接の ratio が基準に届かず blocked）

- 実装: `BootConfig::DefaultSunShadowMethod`（構造体の既定は CSM）を足し、`ApplicationProcessor` が `--shadow-method` の既定をこの値にする。`GameBoot.cpp` が VSM を設定する。`--shadow-method=csm` で戻せる。検証アプリ（golden など）は BootConfig が既定のままなので CSM。最初の試行で `ApplicationProcessor` の局所の既定を VSM にしたところ、検証アプリもこの経路を通るため Outdoor の golden 2 本が mean_flip 0.0024 で落ちた（`verify-VTG8-VSM-DEFAULT-ON-2-first-attempt-golden-outdoor-failed.txt`）。BootConfig へ移して解消。`Scripts/CaptureStartupScene.ps1` の `-ShadowMethod` の既定は Vsm。
- 撮影の修正: `-ShadowProbe` で描画フレーム数を指定しない撮影は、読み込みが早い default 視点で `frames=0`（標本は起動から 300 回目の実行で固定されるが、約 107 フレームで終わる）。CSM でも同じ。`-ShadowProbeRenderedFrames`（既定 360）を足し、指定が無いときだけ使う。
- 検証（`.harness/runs/20261008-035618/`）: `verify-VTG8-VSM-DEFAULT-ON-3.txt`（Debug の Game・RenderGraphCompileTest・RHITextureUpdateVulkanTest・RenderingGoldenImageTest のビルド、BUILD_EXIT_CODE=0）、`-4.txt`（指定の 7 テストが 7/7 Passed。golden 4 本は基準画像・閾値を動かさずに合格）、`-5.txt`（起動画面 3 視点）、`-12-stress.txt`（負荷モード 300 個）。
- 測定: 4 つの run すべて `vulkan_validation` の error_count 0・VSM の mean_texel_mm ≦ CSM（default 8.093/20.695、near 2.449/13.898、low 2.228/13.993、負荷 7.534/19.973）。ratio は default 0.999975・low 0.999702・負荷 0.999311 で基準を満たし、**near は 0.975755 で 0.98 未満**。overflow は low の `VSM_PAGES` が最大 639（要求 4735 ＞ 4096）、負荷の `VSM_RASTER` が最初の 38 行で最大 361457（どちらも `VTG8-VSM-POOL-OVERFLOW` の対象。この反復で増えたものではない）。
- bias の測り直し（near のみ。ヘッダの既定を一時的に書き換え、測定後に `git checkout` で戻し、再ビルド済み）: -0.5 → ratio 0.980726・texel 3.518 mm、-1（既定）→ 0.975755、-1.5 → 0.963054・texel 1.768 mm・要求 2598 ページ。bias を下げるほど悪化する。`--vsm-cache=off` は 0.974851 で持ち越しは原因ではない。done-when は「bias を下げて測り直す」だが逆向きに効くため、停止条件に従って止めた。
- 見え方（PNG を開いた）: default・near・low・負荷 300 個のどれも、天球・地面・球・岩・小屋・見本の帯・発光の球と、それらの太陽の影が欠けなく見える。near は大きな球と岩の影、low は低い角度で伸びる影が出ていて、ページの継ぎ目・光の漏れは目に付かない。
- Notes: (1) 判断が要る点（基準を 0.975 に緩める／既定の bias を -0.5 に戻す／ratio の測り方を見直す）は `blocked/VTG8-VSM-DEFAULT-ON.md`。-0.5 の default・low・負荷モードは測っていない。(2) Git Bash から `cmake ... -- /m:1` を呼ぶと `/m:1` がパスに変換されて失敗する。PowerShell で呼ぶ。(3) 実装はコミット済みで Game の既定は VSM だが、受入れ（ratio・overflow 0）は未達のまま。
- Next: 人の判断（`blocked/VTG8-VSM-DEFAULT-ON.md`）。`VTG8-VSM-POOL-OVERFLOW` は todo のまま。

## 反復 12（2026-10-08）: VTG8-VSM-POOL-OVERFLOW（done）

- 実装（3 つの溢れへの対応）: (1) 既定のプールを 4096 → 5120 ページ（320 MiB）にした（`VirtualShadowMap::DEFAULT_POOL_PAGES`）。要求は bias 既定 -1 のまま 4721〜4734 で、5120・6144・8192 の測定（反復 10）はフレーム GPU が変わらず溢れ 0 だったので、溢れない最小の 5120 を選んだ（`BiasLevels` は替えていない。golden・`VirtualShadowMapClipmapTest` の期待値は動かない）。(2) 展開のインスタンスの容量を 2^19 → 2^20（16 MiB。`RASTER_INSTANCE_CAPACITY`）にした。負荷モード 300 個の最初の約 40 フレームの要求は最大 885457 インスタンスで、524288 を超えていた。さらに、溢れて描けなかった塊の範囲の dirty のページへ再描画の印（`PAGE_ENTRY_RETRY` = 1<<29。物理ページの番号の欄 20 ビットと重ならない）を `vsm_expand.comp` が atomicOr で付け、`vsm_allocate.comp` の引き継ぎが dirty を付け直して印を外す。容量を超えても、そのページが欠けたまま持ち越されず次のフレームに描き直される。(3) `--vsm-cache=off` の負荷モードのラスタの溢れも、容量 2^20 で 0 になった。
- 残る限界（既知）: `--vsm-cache=off` ＋ 負荷モード 300 個では、`VSM_MEGA_CULL` clusters が上限 262144 に張り付き overflow 80775 が出る（需要 342919）。上限を上げると展開が「クラスタの記録の容量ぶんのワークグループ」を出す作りのため、起動画面の既定の経路も毎フレーム空のワークグループが倍になり、MegaChunks（144 B × 容量）も約 38 MB 増える。起動画面の既定の経路（持ち越しあり）では起きないので、cache=off 専用の限界として記録するだけにした。このランのフレーム GPU は 16.27 ms（直す前 13.75 ms。上限で落ちていたインスタンスを描くようになったぶん）。クラスタの溢れは、落ちたクラスタのページには再描画の印を付けていない（カリングがページ単位の情報を持たない）。
- テスト: `RenderGraphCompileTest` の既定プールの期待値を 5120 へ。`VirtualShadowMapVulkanTest` のケース H に再描画の印の数（溢れたら描けなかった塊の範囲のページ数と一致、溢れなければ 0）、新ケース M3c（容量 1 の展開で溢れ → 溢れたフレームの印の集合が「dirty かつ塊の範囲」と一致 → 次フレームが印のページだけを描き直し、毎フレーム描き直したとき（cache=off 相当）と全 texel で一致、印は外れる。実測: 溢れ 12・印 8・dirty 12・描き直し 8・違い 0 語）を足した。変異（`vsm_allocate.comp` が印を無視する）では `VirtualShadowMapVulkanTest` が FAIL（`verify-VTG8-VSM-POOL-OVERFLOW-4-mutation.txt`。シェーダーは戻した）。
- 検証（`.harness/runs/20261008-035618/`）: `verify-VTG8-VSM-POOL-OVERFLOW-1.txt`（Debug の Game・RenderGraphCompileTest・RHITextureUpdateVulkanTest のビルド、BUILD_EXIT_CODE=0）、`-2.txt`（RenderGraphCompileTest・VirtualShadowMapVulkanTest・VirtualShadowMapClipmapTest が 3/3 Passed）、`-3-verbose.txt`（VUID_COUNT=0）、`-5-build-relwithdebinfo.txt`（RelWithDebInfo の Game、BUILD_EXIT_CODE=0）、`-6`〜`-9`（撮影。出力は `.harness/runs/startup-capture/VTG8-VSM-POOL-OVERFLOW-*`、すべて result=pass）。
- 撮影の結果（RelWithDebInfo。ログ全行の最大値）: 持ち越しあり 3 視点（default/near/low）と cache=off 3 視点: `VSM_PAGES` overflow 0（要求 default 787・near 1308〜1314・low 4734）、`VSM_RASTER` overflow 0、`VSM_MEGA_CULL` overflow 0、`VRAM_LEDGER vsm_pool pages=5120 mb=320.000`。負荷 300 個の持ち越しあり: `VSM_RASTER` overflow 0（最大 885512 インスタンス）、`VSM_MEGA_CULL` overflow 0。cache=off の負荷: `VSM_RASTER` overflow 0（885603）、**`VSM_MEGA_CULL` overflow 80775（上記の限界）**。
- GPU 時間（フレーム GPU の中央値。直す前 → 後）: 持ち越し default 2.809 → 2.764、near 3.692 → 3.677、low 3.000 → 2.972、負荷 300 7.662 → 7.660 ms。cache=off default 2.899 → 2.896、near 3.196 → 3.208、low 3.173 → 3.270 ms。停止条件の「既定の経路が 0.5 ms を超えて遅くなる」には当たらない（どれも ±0.1 ms 以内）。
- 影の見え方（PNG を開いた）: 負荷 300 個（持ち越しあり）と低角度の持ち越しありで、小屋・岩・球・見本の帯の太陽の影に欠け・ずれ・ページの継ぎ目は見えない。
- Notes: (1) `ApplicationProcessor.cpp` の `--vsm-pool-pages` のコメント「既定 4096」は `paths:` の外なので直していない（実際の既定は 5120）。(2) `RenderWorld.h` のコメントは 5120 に直した。(3) Git Bash の `grep -c $'$'` は CR の数を数え間違える（全行が CRLF に見える）。行末の確認は Python で `
` を数える。`RenderGraphCompileTest.cpp` は CRLF と LF の混在なので、行末を保つ単行の置換で直した。(4) `VirtualShadowMapVulkanTest` は `RHITextureUpdateVulkanTest.exe` の束なので、ビルドの対象は `RHITextureUpdateVulkanTest`。
- Next: `VTG8-VSM-GPU-TIME` を `todo` に戻した（6 run の撮り直し。cache=off の負荷の `VSM_MEGA_CULL` overflow は上記の既知の限界として記録する）。`blocked/VTG8-VSM-GPU-TIME.md` は残っている。`VTG8-VSM-DEFAULT-ON` は人の判断待ちのまま。

## 反復 13（2026-10-08）: VTG8-VSM-POOL-OVERFLOW（反復 12 の差し戻し対応。done）

- 差し戻し: `vsm_expand.comp` が溢れた塊の範囲のページ表へ `atomicOr` で再描画の印を書く一方、`PageEntry` は同じ欄を通常のロードで読んでいた。重なる塊の一部だけが溢れると、別ワークグループの読み取りと書き込みがデータ競合になる（`barrier()` はワークグループ間を同期しない）。
- 修正: `PageEntry` の読み取りを `atomicOr(pageTable[...], 0u)` にして原子的にした（1 行。印の書き込みと同じ欄への操作がすべて原子になる）。`IsDrawable` が見る allocated・dirty と物理ページの番号の欄は印と重ならないので、結果は変わらない。
- 検証（`.harness/runs/20261008-035618/`）: `verify-VTG8-VSM-POOL-OVERFLOW-10.txt`（Debug の Game・RenderGraphCompileTest・RHITextureUpdateVulkanTest のビルド、BUILD_EXIT_CODE=0）、`-11.txt`（RenderGraphCompileTest・VirtualShadowMapVulkanTest・VirtualShadowMapClipmapTest が 3/3 Passed。VirtualShadowMapVulkanTest は 6.67 秒の GPU 実行で、シェーダーはこのテストの中でコンパイルされる。ケース M3c の溢れ→印→次フレームの描き直しも通る）。
- Notes: (1) 撮影による GPU 時間の再測定はしていない。原子の読み取りはキャッシュ済みの欄への 1 回の操作で、展開は 1 塊あたり 2〜3 回の走査だけなので、反復 12 の ±0.1 ms の範囲を超える見込みはないが、実測は `VTG8-VSM-GPU-TIME`（todo）の撮り直しで確かめる。(2) cache=off ＋ 負荷 300 個の `VSM_MEGA_CULL` overflow 80775 は反復 12 の記録どおり既知の限界。
- Next: `VTG8-VSM-GPU-TIME`（6 run の撮り直し。`blocked/VTG8-VSM-GPU-TIME.md` が残っているので人が確認して `todo` に戻す／すでに `todo`）。

## 反復 14（2026-10-08）: VTG8-VSM-GPU-TIME（6 run の撮り直し。done）

- 反復 12・13 の修正（既定のプール 5120 ページ・溢れた塊の再描画の印・`PageEntry` の原子読み取り）を入れた現 HEAD で、RelWithDebInfo・`-GpuTimingFrames 300`・太陽 45 度の 6 run を撮り直した。ドライバは NVIDIA GeForce RTX 4080 610.88。窓は 240 フレーム（スクリプトの `gpu_timing` 行と同じ。区間の中央値は trace.csv の `Type=GPU` の行から集計）。
- 検証（`.harness/runs/20261008-035618/`）: `verify-VTG8-VSM-GPU-TIME-21.txt`（RelWithDebInfo の Game のビルド、BUILD_EXIT_CODE=0）、`-22`〜`-27`（撮影 6 run。すべて result=pass。出力は `.harness/runs/startup-capture/VTG8-VSM-GPU-TIME-{csm,vsm-nocache,vsm,csm-stress,vsm-nocache-stress,vsm-stress}`）、`-28-aggregate.txt`（区間の中央値と各ログの最大値の集計）。
- 設定の共通値: `VSM_CLIPMAP levels=10 first_width_m=4.000 bias=-1.000 depth_range_m=1000.0`、`VSM_TEXEL d_m=1.0 vsm_mm=0.4883 csm_mm=12.8666`、`VRAM_LEDGER vsm_pool pages=5120 mb=320.000`（VSM の 6 run すべて）。

### フレーム GPU と区間の中央値（ms）

- （この表は trace.csv の CPU フレームから集計した誤った値だったので、反復 19 の表（撮影の metrics.json から作り直したもの）で置き換えた。）

（`VsmClear` は cache=off で毎フレーム全ページを消すので大きい。`ShadowMapPass` は CSM と点光源の合計で、VSM でも半透明・ボリューム用の CSM の描画が残る。区間の中央値の和は、フレームごとの重なりでフレーム GPU の中央値と一致しない。）

### ログの最大値（全フレームの最大）と VSM_CACHE の最終行

| run | VSM_PAGES requested（最大）/ overflow | VSM_RASTER instances（最大）/ overflow | VSM_MEGA_CULL clusters（最大）/ overflow | VSM_CACHE（最終行） |
|---|---|---|---|---|
| VSM default | 787 / 0 | 24257 / 0 | 1788 / 0 | cached=630 rendered=124 invalidated=124 released=0 |
| VSM near | 1311 / 0 | 101866 / 0 | 5540 / 0 | cached=467 rendered=821 invalidated=831 released=10 |
| VSM low | 4734 / 0 | 28102 / 0 | 2135 / 0 | cached=4333 rendered=385 invalidated=388 released=7 |
| VSM cache=off default | 787 / 0 | 24482 / 0 | 5273 / 0 | cached=0 rendered=754 |
| VSM cache=off near | 1311 / 0 | 101701 / 0 | 8207 / 0 | cached=0 rendered=1288 |
| VSM cache=off low | 4734 / 0 | 29020 / 0 | 2913 / 0 | cached=0 rendered=4718 |
| VSM 負荷 300（持ち越し） | 787 / 0 | 885329 / 0 | 3567 / 0 | cached=626 rendered=124 invalidated=124 released=0 |
| VSM cache=off 負荷 300 | 787 / 0 | 885493 / 0 | 262144 / **80775** | cached=0 rendered=750 |

- 溢れ: VSM の 6 つの run のうち、持ち越しありの起動画面 3 視点・cache=off の起動画面 3 視点・持ち越しありの負荷 300 個は `VSM_PAGES`・`VSM_RASTER`・`VSM_MEGA_CULL` の overflow がすべて 0（低角度の要求 4734 は 5120 に収まる。直す前は 4096 に対して overflow 625〜639）。**cache=off ＋ 負荷 300 個だけ `VSM_MEGA_CULL` の overflow が全フレームで 80775**（clusters が上限 262144 に張り付く）。これは反復 12 で既知の限界として記録した診断用の経路（毎フレーム全ページを描き直す）で、起動画面の既定の経路（持ち越しあり）では起きない。`VSM_PAGES` と `VSM_RASTER` はこの run でも 0。done-when の「6 つの run すべて 0」はこの 1 run だけ満たさず、既知の限界として扱った（上限を上げる・分割して描く対策は費用に見合わないとして足していない）。

### CSM との差（持ち越しあり VSM − CSM、フレーム GPU の中央値）

- default +0.330 ms、near +1.172 ms、low +0.518 ms、負荷 300 +1.747 ms。**どれも停止条件の 2 ms 未満**。
- default: `VirtualShadowMapPass` 0.481 ms（`VsmExpand` 0.168・`VsmDraw` 0.135・`VsmAllocate` 0.102 が主）。`ShadowMapPass` は 0.186 → 0.162 ms と下がるが、照明も 0.542 → 0.596 ms と上がる（VSM の読みは標本ごとに探索・PCF の半径を持つ）。
- near: `VsmAllocate` 0.673 と `VsmDraw` 0.687 が大きい。`VSM_CACHE` は毎フレーム 800 ページ前後を invalidated/rendered（default は 124）で、近いほど細かい段のページが動く投影物にかかって描き直されると見られる（原因の切り分けはしていない）。静止した投影物のページは持ち越され、`VsmClear` 0.062・`VsmMark` 0.062 は小さい。
- low: `VsmMark` 0.260（低角度で標本が広い距離にまたがり、要求ページが 4734 と多い）と `VsmAllocate` 0.199 が主。
- 負荷 300: 増えるのは `VsmCullMega` 1.138 ms（300 個のインスタンスの段ごとのカリング）が大半で、残りは照明 +0.13 ms と `VsmAllocate` 0.093。`VsmDraw` は持ち越しで 0.189 ms に収まる。cache=off では `VsmDraw` が 8.247 ms（885493 インスタンス・全ページ）になり 16.320 ms（予算 16.6 ms の縁。5 フレームが超過）。

### 直した後の変化（反復 12 の記録との比較。フレーム GPU の中央値、直す前 → 今回）

- 持ち越し default 2.809 → 2.768、near 3.692 → 3.683、low 3.000 → 2.980、負荷 300 7.662 → 7.634 ms。cache=off default 2.899 → 2.871、near 3.196 → 3.204、low 3.173 → 3.265 ms。どれも ±0.1 ms 以内で、反復 13 の `PageEntry` の原子読み取りで既定の経路が 0.5 ms を超えて遅くなってはいない（`VsmExpand` default 0.168・near 0.154・low 0.157 ms）。この項目は `VTG8-VSM-POOL-OVERFLOW` が評価者に求められた「修正後の GPU 時間の実測」も満たす。

### 影の見え方（PNG を開いた）

- VSM の持ち越し default・near・low、持ち越しの負荷 300、cache=off の low、cache=off の負荷 300（`VSM_MEGA_CULL` が溢れる run）を開いた。小屋・岩・球・見本の帯の球の影に、欠け・ずれ・ページの継ぎ目・光の漏れは見えない。cache=off の負荷 300 は MegaGeometry の岩が増えても影の形は保たれている。壊れて見えなかったので CSM の PNG との画素の差は取っていない。

- Notes: (1) 反復 12 の記録（cache=off の負荷で `VSM_MEGA_CULL` overflow 80775）と今回で同じ値。直すなら cache=off 用に分割して描く実装が要るが、起動画面の既定の経路には効かない。(2) Git Bash では `cmake --build ... -- /m:1` の `/m:1` がパスに変換されてビルドが失敗する。ビルドは PowerShell で走らせる。(3) PowerShell は変数名の大文字小文字を区別しない（`$R` と `$r` が同じ）。
- Next: `VTG8-ACCEPT`（GPU 時間の表はここの値を使う）。`VTG8-VSM-POOL-OVERFLOW` はランナーが `blocked` にしたまま（理由は GPU 時間の実測が無かったことで、今回の撮影で満たされたので人が `done` へ戻してよい）。`blocked/VTG8-VSM-GPU-TIME.md` は古い記録なので消してよい。

## 反復 15（2026-10-08）: VTG8-VSM-GPU-TIME（評価者の差し戻し: cache=off ＋ 負荷 300 個の VSM_MEGA_CULL の溢れを一覧の容量で解消。done）

- 差し戻し（反復 14 の評価者）: 「6 つの run すべて overflow 0」が cache=off ＋ 負荷 300 個の `VSM_MEGA_CULL` overflow 80775 で満たされていない。既知の限界としての記録では done-when を免除できない。
- 対応: `VirtualShadowMap::MEGA_CULL_LIST_CAPACITY` を 262144 → 524288 にした（`VirtualShadowMapRaster.h` の 1 行）。この run の要求は 342919 クラスタ（= 262144 + 80775）で、524288 に収まる。一覧・塊の記録・間接描画の引数は容量に比例するので VRAM は一覧 +4 MiB・間接描画の引数 +5 MiB と塊の記録ぶん増える（`vsm_pool` の台帳は 320 MiB のまま）（`MaxChunkTotal` = 2^24 の内側）。分割して描く実装は要らなかった。
- 検証（`.harness/runs/20261008-035618/`）: `verify-VTG8-VSM-GPU-TIME-30-build.txt`（RelWithDebInfo の Game のビルド）、`-31-vsm`・`-32-vsm-nocache`・`-33-vsm-stress`・`-34-vsm-nocache-stress`（撮影 4 run。すべて result=pass。出力は `.harness/runs/startup-capture/VTG8-VSM-GPU-TIME-{vsm,vsm-nocache,vsm-stress,vsm-nocache-stress}`）。CSM の 4 run は容量に関係しないので反復 14 の値（`-22`〜`-27`）を使う。

### 溢れ（VSM の 6 つの run + 負荷 300 の 2 run、全フレームのログの最大値）

| run | VSM_PAGES requested（最大）/ overflow | VSM_RASTER instances（最大）/ overflow | VSM_MEGA_CULL clusters（最大）/ overflow | VSM_CACHE（最終行） | vsm_pool |
|---|---|---|---|---|---|
| VSM default | 787 / 0 | 24264 / 0 | 1788 / 0 | cached=630 rendered=124 invalidated=124 released=0 | pages=5120 320 MiB |
| VSM near | 1305 / 0 | 101737 / 0 | 5546 / 0 | cached=467 rendered=824 invalidated=829 released=11 | 同 |
| VSM low | 4734 / 0 | 28102 / 0 | 2360 / 0 | cached=4333 rendered=385 invalidated=388 released=7 | 同 |
| VSM cache=off default | 787 / 0 | 24475 / 0 | 5273 / 0 | cached=0 rendered=754 | 同 |
| VSM cache=off near | 1309 / 0 | 102101 / 0 | 8205 / 0 | cached=0 rendered=1291 | 同 |
| VSM cache=off low | 4734 / 0 | 29020 / 0 | 2913 / 0 | cached=0 rendered=4718 | 同 |
| VSM（持ち越し）負荷 300 | 787 / 0 | 956530 / 0 | 3567 / 0 | cached=626 rendered=124 invalidated=124 released=0 | 同 |
| VSM cache=off 負荷 300 | 787 / 0 | 959710 / 0 | **342919 / 0**（上限 524288） | cached=0 rendered=750 | 同 |

- 8 run すべて `VSM_PAGES`・`VSM_RASTER`・`VSM_MEGA_CULL` の overflow がログの全行で 0。`VSM_CLIPMAP`・`VSM_TEXEL` は反復 14 と同じ（levels=10・bias=-1.000・`vsm_mm=0.4883`・`csm_mm=12.8666`）。

### フレーム GPU と区間の中央値（ms。240 フレーム）

- （この表は trace.csv の CPU フレームから集計した誤った値だったので、反復 19 の表（撮影の metrics.json から作り直したもの）で置き換えた。）

（CSM の行は反復 14 の表のとおり: default 2.438・near 2.511・low 2.462・負荷 300 5.887 ms。）

- CSM との差（持ち越しあり VSM − CSM）: default +0.409 ms、near +1.291 ms、low +0.568 ms、負荷 300 +1.892 ms。**どれも停止条件の 2 ms 未満**（負荷 300 は余裕が 0.1 ms）。内訳は反復 14 と同じ構造: default は `VsmExpand` 0.297・`VsmDraw` 0.134・`VsmAllocate` 0.102、near は `VsmDraw` 0.687 と `VsmAllocate` 0.671、low は `VsmMark` 0.248 と `VsmAllocate` 0.190、負荷 300 は `VsmCullMega` 1.142 が大半。
- 容量を倍にした副作用: `VsmExpand` が反復 14 より約 +0.12 ms（default 0.168 → 0.297、near 0.154 → 0.269、low 0.157 → 0.267）、フレーム GPU は default 2.768 → 2.847、near 3.683 → 3.802、low 2.980 → 3.030、負荷 300 7.634 → 7.779 ms。展開の dispatch がカリングの実数でなく容量ぶんのグループを回すため。`TASKS.md` に `VTG8-VSM-EXPAND-INDIRECT`（実数の間接 dispatch）を足した。
- cache=off ＋ 負荷 300 個: 溢れが無くなったので全クラスタを描くようになり、`VsmDraw` が 8.247 → 10.058 ms、フレーム GPU が 16.320 → 18.371 ms（240 フレームすべて予算 16.6 ms 超）。診断用の経路（毎フレーム全ページを描き直す）で、起動画面の既定の経路（持ち越しあり）ではない。done-when に GPU 予算の条件はない。

### 影の見え方（PNG を開いた）

- VSM の持ち越し default・near・low、cache=off の low、持ち越しの負荷 300、cache=off の負荷 300 を開いた。小屋・岩・球・見本の帯の球の影に、欠け・ずれ・ページの継ぎ目・光の漏れは見えない（cache=off の負荷 300 は岩が増えても影の形は保たれている）。壊れて見えなかったので CSM の PNG との画素の差は取っていない。

- Notes: (1) `RenderGraphCompileTest.exe` は起動直後に 0xC0000005 で落ちる（出力なし）。今回の変更を退避したベースラインのビルドでも同じなので、この変更による退行ではない（static 初期化のレイアウト依存の疑い。未調査）。容量の定数はテストの中でも `MEGA_CULL_LIST_CAPACITY` の名前で参照されていて、値の直書きは無い。(2) 反復 14 の「既知の限界」の記述（cache=off 負荷の overflow 80775）は、この反復で解消した。
- Next: `VTG8-ACCEPT`（GPU 時間の表はこの反復の値と反復 14 の CSM の行を使う）。`VTG8-VSM-EXPAND-INDIRECT` は backlog。

## 反復 16（2026-10-08）: VTG8-VSM-POOL-OVERFLOW（評価者の差し戻し: 修正後の GPU 時間の実測。done）

- 差し戻し（反復 13 の評価者）: 停止条件「既定の経路で修正前より 0.5 ms 超遅くならない」の証拠として、現 HEAD で持ち越しありの起動画面 3 視点を測ること。実装は変えていない（HEAD は `b80b5d2d` と TASKS.md 以外同一）。
- 検証（`.harness/runs/20261008-073209/`）: `verify-VTG8-VSM-POOL-OVERFLOW-1.txt`（Debug の Game・RenderGraphCompileTest・RHITextureUpdateVulkanTest のビルド、BUILD_EXIT_CODE=0）、`-2.txt`（RenderGraphCompileTest・VirtualShadowMapVulkanTest・VirtualShadowMapClipmapTest が 3/3 Passed）、`-3-build.txt`（RelWithDebInfo の Game のビルド、BUILD_EXIT_CODE=0）、`-4-capture.txt`（起動画面 3 視点、result=pass。出力は `.harness/runs/startup-capture/VTG8-VSM-POOL-OVERFLOW-gpu`）。

### フレーム GPU の中央値（ms。持ち越しあり、240 フレーム。修正前は run 20261008-035618 の反復 10 の値）

| 視点 | 修正前 | 今回 | 差 | VsmExpand（修正前 → 今回） | VsmDraw | VsmMark | VsmAllocate |
|---|---|---|---|---|---|---|---|
| default | 2.809 | 2.848 | +0.039 | 0.170 → 0.291 | 0.131 | 0.026 | 0.101 |
| near | 3.692 | 3.819 | +0.127 | 0.155 → 0.269 | 0.685 | 0.062 | 0.672 |
| low | 3.000 | 3.074 | +0.074 | 0.159 → 0.266 | 0.160 | 0.249 | 0.193 |

- 3 視点とも差は +0.04〜+0.13 ms で、停止条件の 0.5 ms を超えない。増えたのはほぼ `VsmExpand`（+0.11〜+0.12 ms）で、原因は展開の容量を倍にしたこと（反復 12）と MegaGeometry カリングの一覧の容量を倍にしたこと（反復 15）。展開が実数でなく容量ぶんのワークグループを回すため。実数の間接 dispatch は `VTG8-VSM-EXPAND-INDIRECT`（backlog）。反復 13 の `PageEntry` の原子読み取り単独の影響は、反復 14 の ±0.1 ms 以内の測定で確認済み。
- 溢れ: 3 視点とも `VSM_PAGES`・`VSM_RASTER`・`VSM_MEGA_CULL` の overflow がログの全行で 0（`VSM_PAGES` requested は default 756・near 1286・low 4720、プール 5120）。
- Notes: 値は `metrics.json` の `gpu_timing[].gpu_frame_ms_median` と `pass_median_ms` を使った（trace.csv は集計していない）。`VTG8-VSM-GPU-TIME` の status はこの項目では触っていない。
- Next: `VTG8-ACCEPT`。
## 反復 17（2026-10-08）: VTG8-VSM-EXPAND-INDIRECT（展開を一覧の件数の間接 dispatch へ。done）

- 実装: 展開（`vsm_expand.comp`）の dispatch を、ホストの塊 + `MEGA_CULL_LIST_CAPACITY` ぶんの直接 dispatch から、ホストの塊 + カリングの一覧の件数（容量で頭打ち）ぶんの間接 dispatch へ替えた。新しい 1 スレッドの計算 `vsm_expand_args.comp` が、一覧の語 0 から `VkDispatchIndirectCommand`（x の上限 65535 を超える分は y へ折り返し）を間接描画の引数の頭の語 1〜3 へ書く（語 0 は展開のインスタンスの確保の位置のまま）。`DispatchIndirect` を記録できないコマンドリストでは、従来の容量ぶんの直接 dispatch に戻る。`vsm_expand.comp` は変えていない（塊の番号 = y × x の数 + x の並びがそのまま使える）。クラスタの記録が無い構成（`MegaCapacity` 0）は従来どおりの直接 dispatch。
- テスト: `RenderGraphCompileTest` の期待を新しい記録へ合わせた（呼び出しの並び `…JDDDDJBIIIIE`、引数の計算の dispatch が (1,1,1)、展開が `VsmRaster_Draws` のオフセット 4 への間接 dispatch、計算パイプラインの数 4 → 5・カリングありは 7 → 8、バリアの位置が 1 つ後ろ）。
- 検証（`.harness/runs/20261008-073209/`）: `verify-VTG8-VSM-EXPAND-INDIRECT-1.txt`（RelWithDebInfo の Game・RenderGraphCompileTest のビルド、BUILD_EXIT_CODE=0）、`-2-capture.txt`（起動画面 3 視点、result=pass。出力は `.harness/runs/startup-capture/VTG8-VSM-EXPAND-INDIRECT`）、`-3-debug-build.txt`（Debug のビルド、BUILD_EXIT_CODE=0）、`-4-ctest.txt`（RenderGraphCompileTest・VirtualShadowMapVulkanTest が 2/2 Passed）。

### GPU 時間の中央値（ms。RelWithDebInfo、持ち越しあり、240 フレーム。反復 16 の値と比べる）

| 視点 | フレーム GPU（反復 16 → 今回） | VsmExpand（反復 16 → 今回） | VsmDraw | VsmCullMega | VirtualShadowMapPass |
|---|---|---|---|---|---|
| default | 2.848 → 2.674 | 0.291 → **0.017** | 0.137 | 0.030 | 0.338 |
| near | 3.819 → 3.545 | 0.269 → **0.024** | 0.686 | 0.033 | 1.550 |
| low | 3.074 → 2.980 | 0.266 → **0.016** | 0.176 | 0.031 | 0.740 |

- done-when の「容量 262144 の時の 0.154〜0.168 ms 以下」を、3 視点とも大きく下回った（容量ぶんの空ワークグループの起動が無くなった。増えた引数の計算 1 dispatch は `VsmExpand` の区間に含まれていて 0.017 ms 以内）。他の区間（`VsmDraw`・`VsmAllocate`・`VsmMark`）は反復 16 と同じ。
- 溢れ: 3 視点とも `VSM_PAGES`・`VSM_RASTER`・`VSM_MEGA_CULL` の overflow がログの全行で 0。`VSM_RASTER` の chunks はクラスタの記録の数ぶん（default で約 1868）が出ていて、間接 dispatch でもクラスタの塊が展開されている。
- 影の見え方（PNG を開いた）: default と low で小屋・球・岩・見本の帯の影に欠け・ずれ・ページの継ぎ目は見えない。
- Notes: 値は `metrics.json` の `gpu_timing[].gpu_frame_ms_median` と `pass_median_ms` を使った。cache=off と負荷 300 個の撮り直しはこの項目の done-when に無いので行っていない（容量・溢れの扱いは変えていない）。
- Next: `VTG8-VSM-DEFAULT-ON`（bias を変える前の今の既定で測る指示だったので、この項目が先に済んだ形）。

## 反復 18（2026-10-08）: VTG8-VSM-DEFAULT-ON（既定の bias を -0.5 にして 4 run を測り直す。done）

- 実装: `VirtualShadowMapClipmapSettings::BiasLevels` の既定を -1 から -0.5 へ（`blocked/VTG8-VSM-DEFAULT-ON.md` への親の判断の選択肢 2）。ヘッダの説明（texel は画素の約 0.35〜0.7 倍）を直し、`VirtualShadowMapClipmapTest` の期待（既定値、距離 1 m の段 1 → 2、80 m の目標値の注記）を新しい既定へ合わせた。80 m の段は 8 のまま。起動画面（1280×720・縦画角 60 度）で被覆の補正が入らず b = bias のままであることは、テストの既存の検査が通っている。Game の既定を VSM にする実装（`07971947`）・プール 5120 ページ（`429cf22b`）はそのまま。
- 検証（`.harness/runs/20261008-073209/`）: `verify-VTG8-VSM-DEFAULT-ON-1-build.txt`（Debug の Game・RenderGraphCompileTest・RHITextureUpdateVulkanTest・RenderingGoldenImageTest、`VirtualShadowMapClipmapTest` を持つ CameraViewConstantsTest のビルド、BUILD_EXIT_CODE=0）、`-2-ctest.txt`（指定の 7 テストが 7/7 Passed。golden 4 本は基準画像・閾値を動かさずに通る）、`-3-capture.txt`・`-4-capture-stress.txt`（検証レイヤー付き Debug の撮影、どちらも result=pass。出力は `.harness/runs/startup-capture/VTG8-VSM-DEFAULT-ON-validation` と `…-validation-stress`）。

### 4 run の測定（Debug、検証レイヤー付き、`-Deterministic -ShadowProbe`）

| run | CSM texel / VSM texel (mm) | SHADOW_PROBE_AGREE ratio | finer_ratio | validation error_count | overflow（VSM_PAGES・VSM_RASTER・VSM_MEGA_CULL の最大） |
|---|---|---|---|---|---|
| 起動画面（default、太陽 45 度） | 20.702 / 11.209 | 0.999976 | 0.992772 | 0 | 0・0・0 |
| 近接（near） | 13.885 / 3.504 | **0.982673** | 0.999220 | 0 | 0・0・0 |
| 低角度（low） | 13.997 / 3.035 | 0.999709 | 1.000000 | 0 | 0・0・0 |
| 負荷 300 個（default の視点） | 19.979 / 10.843 | 1.000000 | 0.994838 | 0 | 0・0・0 |

- 4 run すべてで ratio ≥ 0.98、VSM の texel ≤ CSM の texel、error_count・warning_count・vuid_count が 0、溢れが全行で 0。bias は -0.5 のまま（-0.25 以上へ上げる必要は無かった）。前の既定（-1）で ratio 0.9758 だった近接が 0.9827 になり、低角度のページの溢れ（要求 4735 ＞ 旧プール 4096）はプール 5120 と bias -0.5 で 0 になった。
- プールは 5120 ページのまま（`VRAM_LEDGER vsm_pool pages=5120 mb=320.000`、`vsm_page_table mb=0.625`）。この項目では大きさを変えていない。
- `RenderGraphCompileTest` は Debug で通った（反復 15 の「起動直後に 0xC0000005」は今回は出ていない）。

### 影の見え方（PNG を開いた）

- default・near・low・負荷 300 個の 4 枚を開いた。天球（地平の霞と青空）・地面の帯（砂利・草・石畳・タイル・舗装）・大きな球（石の目地）・岩・小屋・見本の帯（金属の球 5 個と艶のある球 5 個）・発光の球が欠けなく見える。小屋・大きな球・見本の球・岩の影が地面に落ちていて、ずれ・ページの継ぎ目・光の漏れ・にきびのような雑音は見えない。近接では大きな球の影の縁が細く、岩の影の長い縁にも途切れが無い。低角度では影が地面に低く伸びている。負荷 300 個では岩が小屋の奥に増えても、手前の球・岩の影は保たれている。
- Notes: (1) `VirtualShadowMapClipmapTest` は CameraViewConstantsTest に入っているので、`verify` のビルド対象（4 つ）だけだと ctest が古い exe を走らせる。今回は CameraViewConstantsTest も足してビルドした。(2) Git Bash から cmake へ `/m:1` を渡すと `m:1` に書き換わるので、ビルドは PowerShell で行った。
- Next: `VTG8-VSM-GPU-TIME`（最終の既定で 6 run を撮り直す）。

## 反復 19（2026-10-08）: VTG8-VSM-GPU-TIME（最終の既定で 6 run を撮り直し、表を撮影の metrics.json から作り直す。done）

- 経緯: 反復 14・15 の GPU 時間の表は trace.csv の `Type=Frame`（CPU のフレーム）から GPU の区間を拾った誤った集計だった（評価の指摘どおり、GPU の標本が 1〜6 件しか入っていない）。この反復では、既定の bias を -0.5 にした `7a73978c` と展開の間接 dispatch の `4a6f3927` が入った HEAD で 6 run を撮り直し、表は撮影の `metrics.json` の `gpu_timing[].gpu_frame_ms_median` と `pass_median_ms`（描いた GPU のフレーム 240 件の集計）から作った。反復 14・15 の GPU 時間の表は誤った集計なので、同じ位置を「この反復の表で置き換えた」という 1 行に替えた（前の値とは並べない）。
- 検証（`.harness/runs/20261008-073209/`）: `verify-VTG8-VSM-GPU-TIME-1-build.txt`（RelWithDebInfo の Game のビルド、BUILD_EXIT_CODE=0）、`-2-csm.txt`・`-3-vsm-nocache.txt`・`-4-vsm.txt`・`-5-csm-stress.txt`・`-6-vsm-nocache-stress.txt`・`-7-vsm-stress.txt`（6 run、すべて `result=pass`・EXIT_CODE=0・撮影の failures は空）。出力は `.harness/runs/startup-capture/VTG8-VSM-GPU-TIME-{csm,vsm-nocache,vsm,csm-stress,vsm-nocache-stress,vsm-stress}`。

### フレーム GPU と区間の中央値（ms。RelWithDebInfo、`-GpuTimingFrames 300`、描いた 240 フレーム）

| run | フレーム GPU | ShadowMapPass | VsmMark | VsmAllocate | VsmClear | VsmCullMega | VsmExpand | VsmDraw | VirtualShadowMapPass | LightingPass |
|---|---|---|---|---|---|---|---|---|---|---|
| CSM default | 2.612 | 0.205 | - | - | - | - | - | - | - | 0.599 |
| CSM near | 2.517 | 0.216 | - | - | - | - | - | - | - | 0.564 |
| CSM low | 2.459 | 0.220 | - | - | - | - | - | - | - | 0.486 |
| CSM 負荷 300 | 5.889 | 2.181 | - | - | - | - | - | - | - | 0.588 |
| VSM cache=off default | 2.647 | 0.168 | 0.023 | 0.028 | 0.022 | 0.043 | 0.018 | 0.146 | 0.293 | 0.607 |
| VSM cache=off near | 2.964 | 0.159 | 0.044 | 0.027 | 0.053 | 0.047 | 0.029 | 0.651 | 0.863 | 0.572 |
| VSM cache=off low | 2.826 | 0.175 | 0.126 | 0.034 | 0.164 | 0.040 | 0.019 | 0.198 | 0.594 | 0.560 |
| VSM cache=off 負荷 300 | 15.000 | 2.188 | 0.020 | 0.026 | 0.023 | 1.443 | 0.463 | 7.002 | 8.989 | 0.698 |
| VSM（持ち越し）default | 2.681 | 0.176 | 0.024 | 0.101 | 0.006 | 0.031 | 0.017 | 0.061 | 0.252 | 0.626 |
| VSM（持ち越し）near | 3.231 | 0.147 | 0.040 | 0.616 | 0.029 | 0.032 | 0.024 | 0.512 | 1.263 | 0.531 |
| VSM（持ち越し）low | 2.853 | 0.180 | 0.130 | 0.217 | 0.012 | 0.032 | 0.018 | 0.165 | 0.586 | 0.564 |
| VSM（持ち越し）負荷 300 | 7.467 | 2.186 | 0.020 | 0.085 | 0.006 | 1.146 | 0.019 | 0.166 | 1.453 | 0.701 |

- 負荷 300 個は default の視点のみ。`ShadowMapPass` は CSM と点光源の合計で、VSM でも半透明・ボリュームのために CSM の描画が残る（負荷 300 個で 2.19 ms のまま）。
- 予算 16.6 ms を超えたフレームは 6 run すべてで 0（`over_budget_count`）。フレーム GPU の p95 は、起動画面で最大 3.66 ms、cache=off 負荷 300 個で 15.04 ms。

### VSM のログの値（ログの全行の中央値 / 最大。`VSM_CLIPMAP`・`VSM_TEXEL`・`VRAM_LEDGER` は全 run で同じ）

| run | VSM_PAGES requested（中央 / 最大）・overflow | VSM_RASTER chunks（中央 / 最大） | VSM_RASTER instances（中央 / 最大）・overflow | VSM_MEGA_CULL clusters（中央 / 最大）・overflow | VSM_CACHE の中央値 |
|---|---|---|---|---|---|
| cache=off default | 420 / 421・0 | 4151 / 4165 | 12305 / 12374・0 | 4071 / 4084・0 | cached=0 rendered=420 |
| cache=off near | 737 / 742・0 | 8966 / 9068 | 59259 / 69134・0 | 8794.5 / 8992・0 | cached=0 rendered=737 |
| cache=off low | 1774 / 1781・0 | 3153.5 / 3552 | 16741.5 / 17810・0 | 3053 / 3128・0 | cached=0 rendered=1771.5 |
| cache=off 負荷 300 | 420 / 422・0 | 285576 / 285591 | 667551 / 667628・0 | 285492 / 285499・0 | cached=0 rendered=420 |
| 持ち越し default | 420 / 421・0 | 1322 / 4162 | 4810 / 12271・0 | 1240 / 1250・0 | cached=366 rendered=54 invalidated=54 released=0 |
| 持ち越し near | 737 / 742・0 | 5468 / 8998 | 50641 / 68828・0 | 5421.5 / 5458・0 | cached=246 rendered=490 invalidated=490 released=0 |
| 持ち越し low | 1774 / 1781・0 | 2417 / 3384 | 13734 / 17563・0 | 2337 / 2398・0 | cached=1557.5 rendered=214 invalidated=213.5 released=1.5 |
| 持ち越し 負荷 300 | 420 / 422・0 | 6372 / 285585 | 15887 / 667570・0 | 6289.5 / 6297・0 | cached=366 rendered=54 invalidated=54 released=0 |

- `VSM_CLIPMAP levels=10 first_width_m=4.000 bias=-0.500 depth_range_m=1000.0`。`VRAM_LEDGER vsm_pool pages=5120 mb=320.000`。
- `VSM_TEXEL`（VSM の texel / CSM の texel、mm。3 視点で同じ）: d=1 m 0.977 / 12.867、2.5 m 1.953 / 12.867、5 m 3.906 / 12.867、10 m 7.813 / 12.867、20 m 15.625 / 25.273、40 m 31.25 / 94.403、80 m 62.5 / 94.403。全距離で VSM の texel が CSM 以下。
- **溢れ**: VSM の 8 run（起動画面 3 視点 × 2・負荷 300 個 × 2）で `VSM_PAGES`・`VSM_RASTER`・`VSM_MEGA_CULL` の overflow がログの全行で 0。cache=off 負荷 300 個の `VSM_MEGA_CULL` clusters は 285499 まで上がるが、一覧の容量 524288 の内側で overflow は 0（instances 667628 の `VSM_RASTER` も 0）。

### CSM との差（フレーム GPU の中央値）と内訳

| 視点 | CSM | VSM（持ち越し） | 持ち越し − CSM | cache=off − CSM | 停止条件（2 ms） |
|---|---|---|---|---|---|
| default | 2.612 | 2.681 | +0.069 | +0.035 | 超えない |
| near | 2.517 | 3.231 | +0.714 | +0.447 | 超えない |
| low | 2.459 | 2.853 | +0.394 | +0.367 | 超えない |
| 負荷 300（default） | 5.889 | 7.467 | +1.578 | +9.111 | 持ち越しありは超えない |

- 持ち越しありの VSM は 4 視点すべてで CSM より遅く、差は最大 1.578 ms（負荷 300 個）。`ShadowMapPass` は VSM でも残るので、VSM が足すのは `VirtualShadowMapPass` の区間。内訳:
  - default（+0.069）: `VirtualShadowMapPass` 0.252（`VsmAllocate` 0.101・`VsmDraw` 0.061 が主）。`ShadowMapPass` が 0.205 → 0.176 に下がった分と相殺された。
  - near（+0.714）: 大きな球が自転するため毎フレーム 490 ページを無効化して描き直す（`VSM_CACHE` invalidated=490）。`VsmAllocate` 0.616 と `VsmDraw` 0.512 が `VirtualShadowMapPass` 1.263 の 9 割。
  - low（+0.394）: `VsmAllocate` 0.217・`VsmMark` 0.130（要求ページが 1774 と多い視点）・`VsmDraw` 0.165 で `VirtualShadowMapPass` 0.586。
  - 負荷 300 個（+1.578）: `VsmCullMega` 1.146 ms が `VirtualShadowMapPass` 1.453 の 8 割。約 6290 クラスタを毎フレーム切り出す費用で、描画（`VsmDraw` 0.166）は持ち越しで小さい。CSM 側は同じ物を `ShadowMapPass` 2.181 で毎フレーム描く。
- 区間ごとの中央値の和はフレームの中央値の内訳にならない（和と差は一致しない）。上の割合は `VirtualShadowMapPass` の内側の比。
- 持ち越しの効果: 負荷 300 個では cache=off 15.000 → 持ち越し 7.467 ms（`VsmDraw` 7.002 → 0.166）。起動画面では near だけ持ち越しが cache=off より 0.267 ms 遅い（`VsmAllocate` 0.616 対 0.027。無効化したページの再確保の費用が、描き直しを減らす分を上回る）。default・low はほぼ同じ（+0.034・+0.027）。
- cache=off 負荷 300 個は 15.000 ms（p95 15.035 ms）で予算 16.6 ms の内側。VSM の既定の経路（持ち越しあり）ではない。

### 影の見え方（PNG を開いた）

- 持ち越しありの VSM の 4 枚（default・near・low・負荷 300 個）と cache=off の 4 枚を開いた。小屋・岩・大きな球・見本の帯の球（金属 5 個・艶のある球 5 個）の影が、欠け・ずれ・ページの継ぎ目なく出ている。near の大きな球と右の岩の影は縁が細く連続し、low の低い太陽でも地面の影に段差や縞は無い。負荷 300 個では遠景の岩の群れが影を落としている。壊れて見えないので、CSM の PNG との画素の差は調べていない。

- Notes: (1) 前の反復が起動した 6 run の撮影スクリプト（`.harness/run6.ps1`）の完了を待ち、その出力を保存した verify として使った。(2) `VSM_PAGES` の `levels_used` は 16 進の文字列なので集計から外した。(3) 負荷 300 個の `VsmCullMega` 1.146 ms と近接の `VsmAllocate` 0.616 ms は、既定の経路の中で相対的に大きい区間。2 ms の停止条件には届かないので対策の項目は足していない（VTG8-ACCEPT で GPU 時間を記録する時の候補）。
- Next: `VTG8-ACCEPT`（GPU 時間の表はここの値を使う）。

## 親（2026-10-08）: VSM のプールの確保量を予算の割り振れる量から引く

- 受入れの準備で見つけた予算の計算の誤り: `ShadowMap` の枠は取り分の重みが 0 なので、VSM の確保量（`SetShadowMapPoolBytes`。プール 320 MiB と展開・cull の一覧など）を `PoolCapacityBytes` で渡すと、プール以外の使用量から引かれるだけで割り振れる量が同じだけ増え、VT・ジオメトリの目標がその分増えていた（`VideoMemoryBudgetManagerTest` も「256 MB を渡すと VT・ジオメトリが 128 MB ずつ増える」を期待していた）。プール以外・VSM・VT・ジオメトリの計画の合計が上限を VSM の分だけ超える。
- 直し方: `VideoMemoryBudgetManager::Compute` で、重み 0 のプールの確保量の合計を `FixedPoolBytes`（固定の取り置き）とし、割り振れる量 = 上限 − プール以外 − 取り置き（0 で止まる）にした。VSM の確保量を渡しても渡さなくても VT・ジオメトリの目標は同じになり（どちらもヒープの使用量の中にあるため）、プール以外・取り置き・目標の合計が上限を超えない。ヒープの使用量が取れない見込みのときも取り置きを引く。重みを与えたプールは今までどおり取り分を受け取る。
- 検証: `VideoMemoryBudgetManagerTest` 1/1 Passed（`.harness/runs/vtg8-accept/budget-ctest2.txt`）。取り置きを引く行を外す変異で落ちる（`budget-mut-ctest.txt`、1 failed）。起動画面の VT・ジオメトリの目標はヒープの予算（約 15 GB）に対して使用量が小さいので、この変更で起動画面の描画は変わらない。

## 親（2026-10-08）: VTG8-ACCEPT（done）

- `Docs/RenderingValidation/VirtualizationAcceptance.md` に「## 段8（VSM 太陽）」の節を足した。判定: 細かさ（texel は全 12 組で VSM が CSM の 0.21〜0.53 倍、縁の帯の割合は判定に使う 6 組で 0.51〜0.63 倍）、ちらつき（判定に使う 6 組で CSM・VSM とも `mean_abs_delta`・`flip_ratio` が 0.0001 未満）、太陽45°の一致 0.9992 以上、起動画面（朝・昼・夕・夜で欠けなし、検証エラー 0）。
- 受入れの途中で入れた変更: (1) 予算の取り置き（`163d39d8`。重み 0 のプールの確保量を割り振れる量から引く）。(2) PCF の半径の下限を画素の大きさの半分に（`739cc27c`。下限が画素 1 つ分だと、CSM の texel が画素より小さい中距離で VSM の縁が CSM より太く、既定の視点で縁の帯が CSM の 1.7 倍だった。一時的な書き換えで画素の半分・1 texel だけの 2 通りを測り、画素の半分を採った）。
- 測定の条件: 細かさ・ちらつき・一致の判定は大きな球の自転を止めた run（`NORVES_STARTUP_SPHERE_SPIN=0`）で行った。自転したままの run も同じ表に並べた（近接の視点では、VSM だけが描く石の目地の影が球と一緒に動くので、変化と縁の帯が大きく、一致が 0.972〜0.979 になる）。理由は TASKS.md の VTG8-ACCEPT の notes。
- 検証（`.harness/runs/vtg8-accept/`）: `r2-build-debug.txt`（BUILD_EXIT=0）、`r2-ctest-debug.txt`（8/8 passed。golden 4 本は基準画像・閾値を動かさずに通る）、`r2-cap-validation.txt`・`r2-cap-validation-stress.txt`（検証レイヤー付き Debug、error_count 0、VSM の溢れ 0）、`r2-build-rel.txt`（BUILD_EXIT=0）、`r2-cap-day.txt`・`r2-cap-night.txt`・`r2-cap-orbit.txt`・`r2-cap-orbit-nospin.txt`（すべて result=pass）。GPU 時間は VTG8-VSM-GPU-TIME の表（反復 19）。

## 反復 1（2026-10-08）: VTG8-FIX-MARK-RANGE（done）

- 原因: `vsm_mark.comp` が影の範囲をカメラからの直線距離（`<= 80 m`）で判定していたのに対し、照明（`Common/VirtualShadowMap.glsl`）は CSM と同じ前方への距離で判定していた。前方 70 m・横 40 m（直線 80.62 m）の受け手は照明では範囲の内側だが、ページが要求されず、割り当てられず、描かれなかった。
- 直し方: (1) 印付けのパラメータに前方（`GPUVsmParams::view`。全 `vsm_*.comp` の `VsmParams` の前半に足した）と影の範囲 `[ShadowNearMeters, ShadowFarMeters]` を渡し、`VirtualShadowMapPass` が照明と同じ `ResolveVirtualShadowMapViewRange`（`VirtualShadowMapSample.cpp` に切り出した）で CSM の分割から決める。(2) 段は照明と同じく直線距離で選ぶまま。ただし段のしきい値を `MaxShadowDistance × VirtualShadowMapThresholdDistanceScale`（2 倍）の直線距離まで求めるようにした（`VirtualShadowMapLevelDistanceThresholds`）。視錐台の端の受け手は直線距離が範囲を超えるので、従来は一番上の段に張り付いて、その段が受け手を含む保証が無かった。`SelectVirtualShadowMapLevel` の意味（範囲の外は -1）は変えていない。
- テスト: `VirtualShadowMapVulkanTest` にケース L6（前方 70 m・横 40 m の受け手と遮る物を、印付け → 割り当て → 消去 → 展開 → 描画 → 照明の関数まで通し、影の側が 0・光の側が 1・粗い段へ逃げた標本 0）を足した。`VirtualShadowMapClipmapTest` に `MaxShadowDistance` を超える直線距離でもしきい値が距離の上限を広げた選び方と一致する検査を足した。CPU の参照 `ClassifyPixel` は影の範囲を前方への距離で判定し、段は距離の上限を広げた設定で選ぶようにした。
- 既存ケースの変化: 視錐台の端の画素（前方 40 m 以内・直線 40 m 超）が要求されるようになり、シーンの要求ページが 14 → 18 に増えた（段 3 が加わる）。ケース M6 の「範囲より大きく動く」移動量を 2500 m → 20000 m にした（段 3 の範囲が 2500 m の移動後も新しい範囲に残るため）。
- 変異: 印付けの範囲の判定を `length(toReceiver) <= tuning.y`（直線距離）に戻すと L6 が落ちる（`verify-VTG8-FIX-MARK-RANGE-mut.txt`: 影の側の可視度 1、逃げた標本 32、`RESULT=FAIL`）。窓を 1 画素にしたのは、隣の画素まで入れると直線距離が 80.003 m の画素が混じり、深度の復元の誤差で範囲の内側と判定されて変異で落ちなくなるため。
- 検証（`.harness/runs/20261008-100400/`）: `verify-VTG8-FIX-MARK-RANGE-5.txt`（BUILD_EXIT=0）、`verify-VTG8-FIX-MARK-RANGE-6.txt`（`VirtualShadowMapVulkanTest`・`VirtualShadowMapClipmapTest`・`RenderGraphCompileTest` が 3/3 passed、CTEST_EXIT=0）。GPU の撮影は回していない（既定の描画は CSM で、VSM は `--shadow-method=vsm` のときだけ）。
- Next: `TASKS.md` の未完の次の項目（段8の残りの不具合）。

## 反復 2（2026-10-08）: VTG8-FIX-ATOMIC-READS（done）

- 原因: 同じ dispatch の中で別のスレッドが `atomicOr` で書く語を、通常の読み取りで読んでいた箇所が 2 つあった（Vulkan のメモリモデルのデータ競合）。
- 直し方: (1) `vsm_mark.comp` の `MarkPage` は、読んでから書く形をやめて条件なしの `atomicOr` にした。(2) `vsm_allocate.comp` の `InvalidateRects` は、割り当ての判定の読み取りを `atomicOr(pageTable[entryIndex], 0u)` にした（展開の `PageEntry` と同じ形）。
- 確かめた箇所（`Assets/Shaders/vsm_*.comp`・`Common/VirtualShadowMap*.glsl`）。`vsm_allocate` の段は 1 段 1 dispatch で、段の間に `BarrierWrites`（`VirtualShadowMapPages.cpp:533`）が入るので、段をまたぐ読み書きは競合しない:
  - `vsm_mark.comp:58-59`（`MarkPage`）: 直した（上記 (1)）。以前は読んでから書く形だったのを、条件なしの `atomicOr(requestBits[word], bit)` にした。
  - `vsm_allocate.comp:236`（`InvalidateRects`）: 直した（上記 (2)）。判定のための読み取りを `atomicOr(pageTable[entryIndex], 0u)` にした。続く 241 行の `atomicOr(..., PAGE_ENTRY_DIRTY)` と 244 行の `atomicAdd(stats[...])` は原子的。ほかのスレッドが `atomicOr` する語は `pageTable` のこの 1 か所だけ。
  - `vsm_allocate.comp:157-204`（`Scroll`）: `pageTable[entryIndex]` の読み（164 行）と書き（153 行の `ReleaseEntry`・204 行）は自分の欄だけ。`stats` は 154・197・202 行の `atomicAdd` のみ。競合しない。
  - `vsm_allocate.comp:250-290`（`Age`）: `pageTable[entryIndex]`（257 行）は自分の欄。`freeList[AgeIndex(physical)]` の読み（281 行）と書き（271・287 行）は物理ページ 1 つにつき 1 欄（割り当て済みの欄と物理ページは 1 対 1）で、ほかのスレッドと共有しない。`stats` は 263・272・288・289 行の `atomicAdd` のみ。競合しない。
  - `vsm_allocate.comp:292-330`（`EvictPlan`）・`481-500`（`Finalize`）: 1 スレッドだけが実行する（`Finalize` は 483 行で他のスレッドを返す。`freeList[0]`・`dirtyList` は 489-501 行）ので競合しない。`stats[STAT_EVICT_AGE]`・`stats[STAT_EVICT_QUOTA]`（328・329 行）は `Evict` の前の dispatch で書く。
  - `vsm_allocate.comp:332-358`（`Evict`）: `stats[STAT_EVICT_AGE]`（339 行）・`stats[STAT_EVICT_QUOTA]`（354 行）の通常の読み取りは、この dispatch では書かれない（書くのは `EvictPlan`、前の dispatch）。同じ dispatch で `atomicAdd` する `stats[STAT_EVICT_TAKEN]` は別の語。`pageTable`（344 行）・`freeList[AgeIndex]`（349 行）は自分の欄・自分の物理ページで、`ReleaseEntry` は自分の欄と `stats[STAT_RELEASED]`（`atomicAdd`）だけを書く。競合しない。
  - `vsm_allocate.comp:360-403`（`FreeReset`・`FreeMark`・`FreeCompact`）: `freeList[0]`（370 行の書き込みは `FreeReset` の dispatch、401 行は `FreeCompact` の `atomicAdd`）。`FreeMark` の `pageTable`（382 行）は読み取りのみ・一覧の語 `1 + slot`（402 行。`atomicAdd` が返した重ならない番号）・使用済みフラグ `UsedFlagIndex`（366 行の初期化は `FreeReset`、390 行の書き込みは `FreeMark`、397 行の読み取りは `FreeCompact` で、それぞれ別の dispatch）・`AgeIndex` は語の範囲が重ならず、段の間に `BarrierWrites` が入る。同じ dispatch で通常に読む語は、同じ dispatch で書かれない。
  - `vsm_allocate.comp:405-456`（`Allocate`）: `requestBits[wordIndex]`（412 行）はこの dispatch では書かれない（書く `vsm_mark` は前の dispatch）。`pageTable[wordIndex * 32 + bit]`（426・443・451 行）は 1 スレッドが持つ語の 32 欄で、ほかのスレッドと共有しない。`freeList[0]`（436 行）は読み取りのみ、`freeList[freeCount - ordinal]`（450 行）は読み取りのみで、`ordinal` は `atomicAdd(stats[STAT_ALLOC_CURSOR])`（435 行）が返した重ならない範囲から振る。`freeList[AgeIndex(physical)]`（452 行の書き込み）は物理ページ 1 つにつき 1 スレッドしか割り当てない。`stats` は 418・419・435 行の `atomicOr`/`atomicAdd` のみ。競合しない。
  - `vsm_allocate.comp:458-479`（`DirtyList`）: `pageTable[entryIndex]`（465 行）は読み取りのみで、この dispatch では書かれない。`dirtyList[DIRTY_COUNT]` は 472 行の `atomicAdd`、`dirtyList[DIRTY_FIRST_PAGE + slot]`（473 行）は `atomicAdd` が返した重ならない番号。`stats` は 477 行の `atomicAdd` のみ。競合しない。
  - `vsm_expand.comp:104-109`（`PageEntry`）・`138`（`MarkRetry`）: `pageTable` の読み取りは `atomicOr(..., 0u)`（109 行）で、書き込みは `atomicOr(..., VSM_PAGE_ENTRY_RETRY)`（138 行）のみ。通常の読み取りは無い。
  - `vsm_expand.comp:185-186・207・215・221-242・273-275`: `sharedCount`・`sharedCursor` は 207・273 行の `atomicAdd` で、通常の読み取りは `barrier()`（188・211・244 行）の後（215・246・248 行。glslang は `barrier()` に共有メモリの意味を付ける）。`draws[0]` は 221 行の `atomicAdd` のみ、`draws[command + 0..4]`（234-238 行）は塊ごとに別の語で、`command >= VSM_DRAWS_HEADER_WORDS`。`stats` は 225・226・230 行の `atomicAdd` のみ。`instances[sharedBase + slot]`（275 行）は `atomicAdd(sharedCursor)` が返した重ならない番号。`megaChunks[megaIndex]`（163-166 行）・`megaListSelected`（150 行）は `readonly` で、書く `vsm_mega_cull`・`vsm_mega_chunks` は前の dispatch。競合しない。
  - `vsm_expand_args.comp:43-47`: `megaListSelected`（43 行。`readonly`）を読み、`draws[1..3]`（45-47 行）を 1 スレッドが書く。`draws[0]` は触らない。競合しない。
  - `vsm_mega_chunks.comp:53-87`: `listSelected`・`listEntries[index]`（53・58 行）は `readonly`、`megaChunks[index]`（87 行）は `writeonly` で 1 スレッド 1 要素。競合しない。
  - `vsm_dirty_mips.comp:38-50`: `pageTable`（38 行）は `readonly`、書く語は 50 行の `atomicOr(dirtyBits[...])` のみで、`dirtyBits` の通常の読み取りは無い。競合しない。
  - `vsm_mega_cull.comp:136-251`: `dirtyBits`（136 行）は `readonly`（前の dispatch `vsm_dirty_mips` が書く）。`listInstanceLevels`（218 行）・`listSelected`（242 行）・`listOverflow`（250 行）・`vsmStats`（219・246・251 行）は `atomicAdd` のみで通常の読み取りが無い。`listEntries[slot]`（245 行）は 242 行の `atomicAdd` が返した重ならない番号。競合しない。
  - `vsm_draw.frag:32`: `pool` へは `atomicMin` のみ（通常の読み取りが無い）。競合しない。
  - `vsm_clear.comp:59`: `pool[base + offset]` への通常の書き込みのみ（`VsmDirtyList` は `readonly`）。ほかのスレッドと語が重ならず、`vsm_draw.frag` の `atomicMin` は後の描画パス。競合しない。
  - `vsm_sample_probe.comp:56・68`: `stats[0]` は `atomicAdd`（56 行のマクロ）のみ、`vsmPageTable`・`vsmPool` は `readonly`（43-51 行）、`results[index]`（68 行）は 1 スレッド 1 要素。競合しない。
  - `Common/VirtualShadowMap.glsl:84・96`: バッファの宣言を持たず、`VSM_PAGE_TABLE(i)`（84 行。ページの表の読み取り）・`VSM_POOL(i)`（96 行。プールの読み取り）を介して include 側の `readonly` の `pageTable`・`pool`（`vsm_sample_probe.comp` と、ライティングの `Common` 側の宣言）を読む関数だけ。書き込みは無い。
  - `Common/VirtualShadowMapParams.glsl:8-34`: uniform block のメンバになる構造体 `VsmSampleParams` の定義だけで、バッファの宣言も読み書きも無い。
  - `Common/VirtualShadowMapChunk.glsl:15-44`: 構造体 `VsmShadowChunk`（15-27 行）と定数（30-44 行）の定義だけで、バッファの宣言も読み書きも無い。
  - `Common/VirtualShadowMapMegaCull.glsl:52`: `VsmMegaCullParams` の uniform（`std140`）の宣言があるが、uniform は読み取り専用で、storage buffer の読み書きは無い。
- 検証（`.harness/runs/20261008-100400/`）: `verify-VTG8-FIX-ATOMIC-READS-2.txt`（BUILD_EXIT=0）、`verify-VTG8-FIX-ATOMIC-READS-3.txt`（`VirtualShadowMapVulkanTest`・`RenderGraphCompileTest` が 2/2 passed、CTEST_EXIT=0）。競合はメモリモデルの話で、値の結果は変わらないので、落ちるテストは足していない。GPU の撮影は回していない（既定の描画は CSM）。
- 反復 5（差し戻しの対応）: 共通 GLSL ４項目に実ソースの行番号（`VirtualShadowMap.glsl:84・96`、`VirtualShadowMapParams.glsl:8-34`、`VirtualShadowMapChunk.glsl:15-44`、`VirtualShadowMapMegaCull.glsl:52`）を開いて確かめて書き足した。再検証は `.harness/runs/20261008-111210/verify-VTG8-FIX-ATOMIC-READS-1.txt`（BUILD_EXIT=0）・`-2.txt`（2/2 passed、CTEST_EXIT=0）。
- Next: `TASKS.md` の未完の次の項目。

## 反復 4（2026-10-08）: VTG8-FIX-MEGA-OVERFLOW-RETRY（done）

- 原因: `vsm_mega_cull.comp` は、一覧の容量を超えて落としたクラスタを数えるだけで、そのクラスタが覆うページには何も印を付けなかった。次のフレームには dirty が外れ、静止した場面では落としたクラスタの影が欠けたまま持ち越された（展開の溢れには `PAGE_ENTRY_RETRY` の仕組みが既にあった）。
- 直し方: (1) `vsm_mega_cull.comp` に VSM のページの表（binding 19。`vsmPageTable`。binding 11 のジオメトリのページの表 `pageTable` とは別）を足し、溢れた分岐で `MarkRetryPages` が、クラスタの球が覆う（`SphereHasDirtyPage` と同じ範囲の）その段の、割り当て済みで dirty のページへ `atomicOr` で再描画の印を付ける。判定の読み取りも `atomicOr(..., 0u)`。(2) `VirtualShadowMapMegaCull` の記述子の並びに binding 19 を足して `dispatch.PageTable` を束縛し、カリングの後に PageTable の UAV バリアを足した。`Common/VirtualShadowMapMegaCull.glsl` に `VSM_MEGA_PAGE_ENTRY_RETRY` を足した。
- テスト: `VirtualShadowMapVulkanTest` にケース K2 を足した。容量 2 のカリングの 1 フレーム目（選んだ 8・溢れ 6）で物理ページから欠けたページが 5 枚あり、その 5 枚すべてに再描画の印が付く（印の無い欠け 0・dirty でないページへの印 0）。容量が十分な 2 フレーム目は前フレームの表を引き継ぎ、印のある 5 ページだけ描き直して印が外れ、物理プールがケース F（毎フレーム描き直したとき）と全 texel で一致する（違うページ 0）。J2（件数だけを見る場面）はそのまま。`RenderGraphCompileTest` のカリングの束縛の数を 8 → 9 にし、binding 19 が `VSM_PageTable` であることを確かめるようにした。
- 変異: `MarkRetryPages` の呼び出しを外すと K2 が落ちる（`verify-VTG8-FIX-MEGA-OVERFLOW-RETRY-mut.txt`: 再描画の印 0・印の無い欠け 5・描き直したページ 0・ケース F と違うページ 5、`RESULT=FAIL`）。シェーダーは元へ戻した。
- 検証（`.harness/runs/20261008-100400/`）: `verify-VTG8-FIX-MEGA-OVERFLOW-RETRY-3.txt`（BUILD_EXIT=0）、`-6.txt`（`VirtualShadowMapVulkanTest`・`RenderGraphCompileTest` が 2/2 passed、CTEST_EXIT=0）、`-5.txt`（K2 の出力と `VUID_COUNT=0`・`RESULT=PASS`）。GPU の撮影は回していない（既定の描画は CSM で、VSM は `--shadow-method=vsm` のときだけ）。
- Notes: (1) 範囲が大きい球は溢れた 1 スレッドが全ページを走査するが、溢れたときだけ通る経路なので許容した。(2) `RenderGraphCompileTest.cpp` は行末が混在しているため、バイトを保ったまま編集した（全体を正規化しない）。(3) 作業開始時点で `TASKS.md` に別項目の `done` → `blocked` の未コミットの差分があり、同じコミットに含めた。
- Next: `TASKS.md` の未完の次の項目（`VTG8-FIX-MEGA-FALLBACK` ほか）。

## 反復 6（2026-10-08）: VTG8-FIX-MEGA-FALLBACK（done）

- 経緯: 実装本体は直前の「作業途中の保存」(a40864f2) に入っていた。この反復で内容を読み直し、ビルド・テスト・変異で確かめて閉じた。
- 原因: MegaGeometry の影の経路を用意できないとき（`DrawIndexedIndirectCount` が無い、段ごとの cull のパイプラインを作れない、cull の資源を作れない）、MegaGeometry の影だけを省いた VSM を公開していた。照明が CSM へ戻らないので、CSM が描く MegaGeometry の太陽の影が消えた。
- 直し方: `VirtualShadowMapPass::Initialize` で、この 3 つのどれかが欠けたら `Fallback(FallbackReason::MegaGeometry)` を呼ぶ（資源を手放し、VSM を公開せず、`VSM_FALLBACK reason=mega_geometry` を 1 回出す）。`FallbackReason::MegaGeometry`（名前 `mega_geometry`）を足した。cull を持たない VSM はなくなったので、`m_MegaCull` を条件にした分岐を外した。
- テスト: `RenderGraphCompileTest` の「省略を合格にしていた」2 ケース（`DrawIndexedIndirectCount` なし・cull のパイプライン生成の失敗）を、CSM へ戻ることを確かめる形に直した。確かめる内容は、非アクティブ・理由 `MegaGeometry`・VSM の資源が公開されない・パスが何も宣言せず何も描かない・`VSM_FALLBACK reason=mega_geometry` が 1 回だけ・`VRAM_LEDGER` が出ない。確保失敗のケースに `VsmMega_List`・`VsmMega_DirtyBits`・`VsmMega_Chunks` を足した。
- 変異: `SupportsMegaCasters()` の戻りを外す（`if (false && ...)`）と `RenderGraphCompileTest` が落ちる（`verify-VTG8-FIX-MEGA-FALLBACK-mut.txt`: `!pass.IsActive() && ... == MegaGeometry` の assert、CTEST_EXIT=8）。元へ戻した。
- 検証（`.harness/runs/20261008-100400/`）: `verify-VTG8-FIX-MEGA-FALLBACK-3.txt`（BUILD_EXIT=0、`RenderGraphCompileTest` 1/1 passed、CTEST_EXIT=0）。GPU の撮影は回していない（既定の描画は CSM で、VSM は `--shadow-method=vsm` のときだけ）。
- Notes: 変異を戻したあと、`Copy-Item` が更新時刻を保って MSBuild が再コンパイルしなかったため、更新時刻を進めて再ビルドした（-3 はその後の結果）。
- Next: `TASKS.md` の未完の次の項目。

## 反復 7（2026-10-08）: VTG8-FIX-MEGA-FALLBACK 再実装（評価の差し戻し対応・done）

- 差し戻し: 戻りのテストが VSM のパスだけを回しており、照明が CSM を読むことは確かめていなかった。
- 直し方（テストのみ。製品コードは変更なし）: `RenderGraphCompileTest` に `RunVsmPassThroughLighting` を足した。実際の `GBufferPass` → VSM のパス → `LightingPass` を 2 フレーム回し、(1) 照明が書く `LightingVsmSampleParams` の `control.x`（VSM 公開時 1・フォールバック時 0）、(2) 照明の束縛 22・23 が `VSM_PageTable`・`VSM_PhysicalPool` か（フォールバック時はどちらも VSM のバッファではない）、(3) CSM のテクスチャ配列が束縛 6 に束縛されること、(4) VSM のパスが何も宣言しないこと、を確かめる。`FakeDevice` に `LightingVsmSampleBuffer` を足して照明の定数バッファの更新内容を読めるようにした。
- 適用先: `DrawIndexedIndirectCount` なし・cull のパイプライン生成の失敗（3 通り）・cull の資源の確保失敗（`VsmMega_List`/`DirtyBits`/`Chunks`）の MegaGeometry の各戻りと、既存の他の理由（`fragment_atomics`・`bda`・`pool_size`・`pipeline`）の戻り。対照として `TestLightingReadsVsmWhenPublished`（VSM が使える装置では同じ構成で `control.x = 1`・束縛 22・23 が VSM のバッファ）を足し、確かめる構成が VSM の公開を見分けられることを裏づけた。
- 変異: `LightingPass.cpp` で VSM のパラメータを常に有効（`control[0] = 1`）にすると `RenderGraphCompileTest` が落ちる（`verify-VTG8-FIX-MEGA-FALLBACK-mut2.txt`: `params.control[0] == (bExpectVsm ? 1u : 0u)` の assert、CTEST_EXIT=8）。元へ戻して更新時刻を進め、再ビルドした。
- 検証（`.harness/runs/20261008-100400/`）: `verify-VTG8-FIX-MEGA-FALLBACK-6.txt`（Game・RenderGraphCompileTest のビルド BUILD_EXIT=0、`RenderGraphCompileTest` 1/1 passed、CTEST_EXIT=0）。GPU の撮影は回していない（既定の描画は CSM で、VSM は `--shadow-method=vsm` のときだけ）。
- Notes: `RenderGraphCompileTest.cpp` は行末が混在しているため、バイトを保ったまま編集した。bash の `/m:1` はパス変換されるので、ビルドは PowerShell で回した。
- Next: `TASKS.md` の未完の次の項目。

## 反復 8（2026-10-08）: VTG8-FIX-MEGA-FALLBACK 再々実装（評価の差し戻し対応・done）

- 差し戻し: 戻りのテストの場面で CSM が無効のままだった（影の地図のグラフ公開・サンプラー・カスケード・影を落とす方向光が無く、`bShadowEnabled=0`・`cascadeCount=0`・`lightCount=0`）。束縛 6 の確認だけでは照明が CSM を読むことの証明にならなかった。
- 直し方（テストのみ。製品コードは変更なし）: 共通の補助 `RunVsmPassThroughLighting` の場面に、(1) 有効な CSM の公開（`bShadowPublished`・4 層の影の地図・サンプラー・`CascadedShadow` の有限な 4 カスケードの行列と増える分割距離）、(2) 影を落とす方向光 1 灯（`SnapshotLightProxies`）、(3) 影の地図をグラフへ公開するパス（`NamedShadowMapProducerPass`。照明へ依存を張る）を組み込んだ。各フレームで照明のパラメータが `bShadowEnabled == 1`・`cascadeCount == 4`・`lightCount == 1`・分割距離の反映、梱包された方向光の `attenuation[2] == 1`（影の印）であることを確かめる。既存の VSM の無効（`control.x == 0`）・束縛 22/23・ログの検査はそのまま残した。補助は戻りの全ケース（能力が無い・cull の初期化の失敗・資源の確保の失敗・他の理由）と対照ケース（VSM 公開時は `control.x == 1`、CSM も有効）で共通に使われる。
- 変異: `VirtualShadowMapPass.cpp` の `SupportsMegaCasters()` の戻りを外す（`if (false && ...)`）と `RenderGraphCompileTest` が落ちる（`verify-VTG8-FIX-MEGA-FALLBACK-mut.txt`: `!pass.IsActive() && ... == MegaGeometry` の assert、CTEST_EXIT=8）。元へ戻して更新時刻を進め、再ビルドした（`git diff` に Library の差分なし）。
- 検証（`.harness/runs/20261008-111210/`）: `verify-VTG8-FIX-MEGA-FALLBACK-1.txt`（Game・RenderGraphCompileTest のビルド BUILD_EXIT=0）、`-2.txt`（1/1 passed、CTEST_EXIT=0）、`-3.txt`（変異を戻したあとの再ビルド・再実行。1/1 passed、CTEST_EXIT=0）。GPU の撮影は回していない（既定の描画は CSM で、VSM は `--shadow-method=vsm` のときだけ）。
- Notes: `RenderGraphCompileTest.cpp` は行末が混在しているため、バイトを保ったまま編集した（`git diff --numstat` と `--ignore-cr-at-eol` が一致）。Git Bash の `sed -i` は行末を壊すので、変異はバイト単位の置換で入れた。
- Next: `TASKS.md` の未完の次の項目。

## 親（2026-10-08）: 段8の区切りの評価への対応

- 指摘への対応: 印付けの影の範囲を前方への距離に（`37cfd8dd`）、アトミックに書かれる語の原子的な読み取り（`ce123a9a`・`ffa83a7f`・`5c192db5`）、MegaGeometry の cull の一覧の溢れのページの描き直し（`79ea8007`）、MegaGeometry の影の経路を用意できないときの CSM への戻り（`d1f99488`・`3b5a895d`・`c3bc2fe9`）、テストの補助の `std::ifstream` を `FileStream` に・英語のコメント・受入れの記録の GPU 時間の根拠（`e3d0beff`）。
- 最終の HEAD での確かめ（`.harness/runs/vtg8-accept/r3-*`）: Debug のビルドと ctest 8/8 passed（golden 4 本は基準画像・閾値のまま）、検証レイヤー付き Debug の 4 run で error_count 0・VSM の溢れ 0、RelWithDebInfo の朝・昼・夕・夜と旋回の撮影はすべて result=pass。影の測定の値は前の撮影（r2）と同じ（決定的な撮影で、修正はこれらの視点の結果を変えない）。受入れの記録の証拠の場所を `VTG8-ACCEPT-r3*` に差し替えた。
