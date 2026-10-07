- notes: 2026-10-06 親（段7の開始時に詳しくした）。VTG7-SW-THRESHOLD で on が off より速い場合だけ行う。 2026-10-06 親: VTG7-SW-PATH-DIFF が done になってから行う（`--sw-raster=on` の経路だけで出る原因の分からない差を残したまま既定にしない）。 2026-10-06 VTG7-SW-PATH-DIFF の結果: その差は 64bit の経路が原因ではなく、同じ構成の run 間の非決定性（VTG7-DETERMINISM-SEED）だった。on と off の PSNR は 100 dB を期待せず、off 同士の繰り返し（各視点 4 回以上）の最小 PSNR より下がらないことで判定する。 2026-10-06 親（VTG7-SW-PATH-DIFF の評価）: on と off の比較は TAA の撮影の PSNR の床（off 同士の最小）ではなく、VTG7-SW-FXAA-COMPARE の FXAA の撮影の結果（差がソフトに回したクラスタに限られること）で判定する。TAA の撮影は 45 dB の限度と欠けの目視だけに使う。 2026-10-06 親: VTG7-SW-THRESHOLD で既定のしきい値は 32 画素になり、on は負荷モードで off より 0.11〜0.69 ms 速い（起動画面は off と同じ水準）。VTG7-DETERMINISM-SEED は段の外の後回しにしたので、TAA の撮影の on と off の比較は 45 dB の限度と欠けの目視だけに使う。危険地帯（描画の既定の経路）。 2026-10-06 親: 実装の反復は 60 分で切れ、止まり損ねた前の反復の子のシェーダーの書き換えで撮影が汚れたので、親が検証を取り直して done にした（PROGRESS の「反復 2（親）」）。撮影は受入れと既定の描画経路の確認に絞った。

## VTG7-ACCEPT: 段7（ソフトウェアラスタ）の受入れを記録する
- status: done
- done-when: `Docs/RenderingValidation/VirtualizationAcceptance.md` に段7の節を足す。小さい三角形の多い視点（負荷モード 300 個の既定・近接・低角度）と起動画面の GPU 時間を、段6の受入れの値（on・off）と、今の `--sw-raster=off`・`on` で並べる（RelWithDebInfo の `-GpuTimingFrames 300`。フレーム GPU・`MegaGeometryDraw1/2`・ソフトのラスタ・合流・記録の compute の区間）。起動画面の朝・昼・夕・夜 × 3 視点の撮影（既定の経路）を開いて確かめ、`--sw-raster=off` との PSNR、`MEGA_OCCLUSION`、VT の常駐、golden（再承認の有無と根拠）、関係する ctest の結果、既知の限界（MegaGeometry だけが対象、64bit アトミックの無い装置ではハードだけ、負荷モードの影の描画の省略など）を書く。判定の行は、計画の受入れ「小さい三角形の多い視点で GPU 時間が下がる」に対して数値で書く。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RHITextureUpdateVulkanTest RenderResourcesDomainContractTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|VisibilityResolveVulkanTest|IntegerAttachmentVulkanTest|VisibilityBufferEncodingTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG7-ACCEPT -Configuration RelWithDebInfo -Deterministic -SunElevations 10,45,3`
- paths: Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-06 親（段7の開始時に詳しくした）。段の区切りの評価にかける。 2026-10-06 親（VTG7-RASTER-TIMING の評価）: `MegaGeometryDraw1/2` は on と off で同じ名前でも範囲が違う（off は `BeginRenderPass`／`EndRenderPass` をまたぎ、on は render pass の内側だけ）。on と off の比較はフレーム GPU と区間の合計で行い、`MegaGeometryDraw1` の比を「書く量の差」とは書かない。 2026-10-06 親: 受入れの撮影は VTG7-SW-DEFAULT-ON の取り直し（`.harness/runs/vtg7-defon-clean/`）を使い、GPU 時間は VTG7-SW-THRESHOLD の測定を使った（撮影の方針を受入れと既定の描画経路の確認に絞った）。

## SS-CAPTURE: 起動画面を撮影して数値を出す経路を作る
- status: done
- done-when: `Game.exe --capture-png=<path> --startup-camera=<yaw>,<pitch>,<arm>` を付けて起動すると、アセットの読み込みが落ち着いた後の起動画面（Rendering3DTest）の最終出力をPNGに保存し、終了コード0で終わる。`Scripts/CaptureStartupScene.ps1 -OutDir <dir>` が既定・近接（球の輪郭が画面の中央付近に来る視点）・低角度（地面すれすれ）の3視点を撮り、各PNGと、平均輝度・白飛び（255）画素率・黒つぶれ（0）画素率を書いた `metrics.json` を出す。Gameの終了コードが0でない、PNGが無い、Game.logにシェーダーのコンパイル失敗（`Failed to compile shader`。Slang SDK未設定の`neural_material_decode.slang`だけは除く）がある場合はスクリプトが終了コード1を返す。撮影は ImGui とデバッグ用のボードを写さない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-CAPTURE`
- stop-when: 既存の `RequestFrameCapture` と `--exit-after-rendered-frames` の組み合わせで最終出力を取れない理由が見つかり、RenderThreadの寿命に手を入れる必要が出たら、その理由を記録して止める。
- paths: Library/Core/Private/Engine, Library/Core/Public/Engine, Game, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 以降の全タスクの見た目の証拠はこのスクリプトで撮り、PNGを開いて確認する。撮影物は `.harness/runs/` に置き、コミットしない。

## SS-POM: 視差オクルージョンの凹凸の向きと輪郭の歪みを直し、3つのシェーダーで共通にする
- status: done
- done-when: POMと余接フレームを共通のシェーダーの取り込みファイルへまとめ、`gbuffer.frag`・`forward_transparent.frag`・`megageometry.frag` が同じ関数を使う。高さマップは白=高いとして読む（`1.0 - height` で深さへ直す）。輪郭のフェードと層数は幾何法線とビュー方向の内積で決め、TBNは元のUVから一度だけ作って法線マップにも使い、接空間のビュー方向はTとBを正規化して作る。マーチ中のサンプルは分岐の前に取ったUV勾配で`textureGrad`にする。撮影の近接視点で、球の石が盛り上がり目地がへこんで見え、輪郭の付近で模様が引き伸ばされたり流れたりしていない。法線マップ（`nor_gl`）の緑の向きが凹凸と合っている（光の当たる側が明るい）ことを点光源の近くで確かめ、逆なら直す。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-POM`
- stop-when: 余接フレームの向き（`8c80695` で正しくなった +∇u, +∇v）を戻さないと合わない場合は、戻さずに原因を記録して止める。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Game/GameModes/Rendering3DTest, TASKS.md, PROGRESS.md
- notes: 原因は `8c80695` で余接フレームが正しい向きになり、それまで打ち消し合っていた「高さを深さとして読む」反転だけが残ったこと。境界の歪みは、正規直交でない余接フレームで `viewDirTS.z` が N·V より大きくなり、`842070e` の輪郭フェードが効かなくなったため（`842070e` の修正自体は残っている）。`megageometry.frag` には `842070e` が入っておらず、`forward_transparent.frag`・`megageometry.frag` は古い閾値 `1e-8` のまま。

## SS-SKY-MODEL-P1: 空のモデルを球殻の大気のレイマーチと多重散乱の近似にし、地平線より下を地面として返す
- status: done
- done-when: `SkyAtmosphere.cpp` の `EvaluateHillaireSkyReference` を、平行平板の τ·e^(-τ) の近似から、球の惑星（`PlanetRadiusMeters`・`AtmosphereHeightMeters`）の大気を視線に沿ってレイマーチする形へ置き換える。各点で Rayleigh・Mie の散乱係数×高度の密度、太陽への透過率（同じ密度の光学的深さの積分）、位相関数で単一散乱を積分し、Hillaire 2020 の等方の多重散乱（2次以降の散乱を ψ_ms と 1/(1−f_ms) の等比級数で近似。前計算の小さな表か解析近似）を足す。視線の透過率は (1−e^(−τ)) の飽和で扱われ、地平線の付近が橙でなく白っぽくなる。地平線より下の視線は0を返さず、地面（`GroundAlbedo` のランバート面を太陽の透過光と空の照度で照らしたもの）の反射に透過率を掛けた値と、そこまでの散乱を返す。透過率LUT・空の太陽の地表照度（`ComputeSunGroundIlluminance`）・`EvaluateSkyViewRadiance` も同じ密度の積分に揃える（空の太陽の方向光と空の背景・空由来のIBLの間で太陽の透過率が食い違わない）。CPUのテスト `SkyAtmosphereModelTest` のアンカーを、テスト側に置いた細かい刻みの独立な数値積分と次の物理量の範囲で置き換える：太陽仰角40°で天頂が青（B > G > R）、天頂の輝度 約2000〜8000 nits、水平面の空の照度が太陽を含む全天の照度の10〜30%、太陽と反対側の地平線の R/B が0.5〜1.2、地平線より下が0でなく有限、仰角3°で太陽の周りが橙。空の放射輝度LUT・透過率LUTの生成は空のパラメータ（太陽の向きを含む）が変わったときだけ走り、1回の生成時間をログに出す（起動画面の解像度で1回 150 ms を超えるなら刻み・解像度を減らすか、生成をGPUへ移す）。撮影の昼（45°）の低角度で空が上ほど濃い青・地平線が白っぽく明るく、既定視点で地面の外（遠景）が黒くない。夕（3°）で太陽側の地平線が橙になる。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game SkyAtmosphereModelTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SkyAtmosphereModelTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-SKY-MODEL-P1 -SunElevations 10,45,3`
- stop-when: Indoor/Outdoor の golden が空を使うかを先に確かめる。使わないなら変わらないはずで、変わるなら止めて原因を直す。空を使っていて変わるなら、空の変更だけによる差であることを確かめて再承認する（任されている。手順は `Docs/RenderingValidation/GoldenBaselines.md`）。空の数値アンカーは「古い値に合わせる」のではなく物理的な範囲と独立な積分で置き換え、根拠を `Docs/RenderingValidation/R2Acceptance.md` に追記する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: SS-DAYLIGHT-P1・P2 の停止理由（`blocked/SS-DAYLIGHT-P1.md`）の選択肢A。2026-09-30、ユーザーの方針（細かな判断で止めず推奨で完走する）により推奨を採った。空の放射輝度LUT（`SkyAtmospherePass::GenerateRadianceLut`）はCPUの `EvaluateHillaireSkyReference` から作られ、ラスタの背景・空由来のIBL・PTの不交差・フォグの内向き散乱の色が同じLUTを引くため、この関数を直すと全部が揃って変わる。危険地帯（ライティング・空）。評価者を通す。

## SS-SKY-MODEL-P2: 新しい空で、空を使う検証（R2の受入れ・PTの屋外）を再照合する
- status: done
- done-when: 空を有効にする検証（`RenderingHdrSceneCaptureTest` の屋外・R2の受入れの数値/画像、`PathTracingOutdoorVulkanTest`、`R7OutdoorPathTracingReferenceVulkanTest`、`LightingLightBufferTest` の空の太陽の値など。`Test/Core/Rendering` で `SkyAtmosphere` を有効にしている箇所を洗い出す）を新しい空で回し、落ちるものは原因を分類する。差が空のモデルの変更だけによるもの（古い空のアンカー値・古い空の基準画像）は、新しい値が物理的な期待に近いことを確かめて再基準化・再承認する。ラスタとPTの比較（同じLUTを引く）の閾値は変えない。閾値を越えるものは原因を直すか、測定値と分類を既知の限界として受入れ記録（`R2Acceptance.md`・`R7OutdoorAcceptance.md`）に書く。
- verify: `cmake --build build --config Debug --target Game RenderingHdrSceneCaptureTest PathTracingVulkanTest R7OutdoorPathTracingReferenceVulkanTest LightingLightBufferTest RenderingGoldenImageComparatorTest R8SequenceRenderer R8ExrSequenceValidator -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingHdrOutdoorSceneVulkanTest|PathTracingOutdoorVulkanTest|PathTracingVolumetricTest|LightingLightBufferTest|R2SkyGoldenCurrentModelTest|R8SequenceSmokeTest)$"`
- stop-when: 閾値や規則そのものを変えないと通らない場合は、変えずに測定値と分類を既知の限界として記録して完了にする。
- notes: 2026-10-02 評価1周目（`68b0f42`）は NEEDS_WORK（`NEXT_FINDINGS.md`）。空を有効にする `PathTracingVolumetricTest`・`R8SequenceSmokeTest` が未実行、R2の空の基準画像3枚を新しい空から作った候補と画素で照合した証拠が無い。`R8SequenceSmokeTest` は単独で9分を超えるなら verify の行へ入れず、反復内で回した記録（所要時間とEXIT_CODE）を PROGRESS と受入れ記録に残す。
- notes: R7屋外の参照比較（`ctest ... --timeout 1800 -R "^R7OutdoorPathTracingReferenceVulkanTest$"`、約12分）は、ランナーの再検証の上限（10分）を超えるため verify の行から外した。`68b0f42` の Core に対して `.harness/runs/20261001-193759/verify-SS-SKY-MODEL-P2-6.txt` で Passed（721.71 s、EXIT_CODE=0）を確かめた（`68b0f42` の変更は `PathTracingVulkanTest.cpp` と文書だけで、R7屋外の実行ファイルには入らない）。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-02 評価1周目の指摘に対応した。`PathTracingVolumetricTest`（8.3 s）と `R8SequenceSmokeTest`（単独で25.8 s、EXIT_CODE=0。9分より短いので verify の行へ入れた）が合格。R2の空のgolden 3枚は今の空のモデルから作った候補と画素で完全一致し（`.harness/runs/20261002-084328/verify-SS-SKY-MODEL-P2-3.txt`・`-4.txt`）、この照合を `R2SkyGoldenCurrentModelTest` として登録した。R7屋外の参照比較は起動画面の変更の後の Core でも合格（603 s、`-7.txt`）。
- notes: 参照比較の段（`-L Reference`）のうち空を使うものだけを回す（R7屋外は単独で約12分）。GPUのテストは同時に2本走らせない。上の verify の対象名が CMake に無い場合は、`Test/Core/Rendering/CMakeLists.txt` の実際の名前へ読み替え、読み替えを PROGRESS に書く。

## SS-DAYLIGHT-P1: 起動画面を物理空と空の太陽による昼の屋外にする
- status: done
- done-when: Rendering3DTest が R2 の物理空（SkyAtmosphere）を有効にし、空の太陽（仰角約40°、カメラの既定視点から球と岩の影が地面に見える方位）が影を落とす方向光になる。シーン独自の方向光は外し、方向ライトの操作（矢印キーとImGui）は空の太陽の仰角・方位を動かす。`f90e7ea` の露出補正を外す（露出は SS-AUTOEXPOSURE-P2 の自動露出）。点光源（Lumen/Candelaの物理単位）と発光球の輝度も物理的にありうる値へ移す。IBLは空から作る（静的HDRは空が無効なときだけ使う）。撮影で、青い昼の空の下、球と岩の影が地面にはっきり見え、影の中の地面の平均輝度が日向の15〜40%（黒くつぶれず、影の中の石畳の模様が見える。PNGの領域の画素をsRGBからリニアへ戻し、Rec.709の重みで求めた輝度の平均の比で測る。8ビットの符号値の比ではない）、白飛び画素率が1%未満。昼の撮影の近接視点で、石畳の球の石の太陽側の斜面が明るく反対側が暗い（法線マップ `nor_gl` の緑の向きが凹凸と合う。逆なら直す）。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-DAYLIGHT-P1 -SunElevations 45`
- stop-when: 影の中の比が範囲に入らない原因が空のモデル（SS-SKY-MODEL-P1）の側にある場合は、そちらへ差し戻さず、測った値と原因を記録してこのタスクで直せる範囲（起動画面の値）で閉じる。
- paths: Game/GameModes/Rendering3DTest, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Component, Library/Core/Private/Component, Assets/Shaders, Assets/Textures, TASKS.md, PROGRESS.md
- notes: ユーザー決定（2026-09-27）「物理的な昼の屋外へ」。影が見えない原因は、静的HDRの環境光が地面の照度の約87%を占め（方向光1 lux）、影で約13%しか暗くならないこと（`f3d4abb` でIBLを E/π にした後、比が約9倍悪化）。空を有効にするとエンジンが空の太陽の方向光を自動で加える（SKY-SUN-P2）。起動画面の見た目を変える変更で、この変更はユーザーの承認済み。
- notes: 起動画面の側は `ad6729a` で実装済み。空のモデルが暗く橙で地平線より下が黒いため止めていた（`blocked/SS-DAYLIGHT-P1.md`）。SS-SKY-MODEL-P1 の後に撮り直し、完了条件を確かめる。法線マップの向きの確認は SS-POM の評価の指摘の残り。
- notes: 2026-10-02 評価1周目（`153c02c`）は NEEDS_WORK（`NEXT_FINDINGS.md`）。ライティングの出力では影の中は日向の約2割あるが、トーンマップ（ACESの足元）で表示のリニア値の比が約10%へ縮み、影の芯がほぼ黒になる。直す場所は起動画面のカメラの表示側（自動露出の露出補正・トーンマップの足元・グレーディング）で、検証シーンのカメラの既定は変えない。

## SS-DAYLIGHT-P2: 太陽の向きを操作・指定でき、高さフォグを掛ける
- status: done
- done-when: `--sun-elevation=<deg>` と `--sun-azimuth=<deg>` で起動時の太陽の向きを指定でき、撮影スクリプトの `-SunElevations` で朝（約10°）・昼（約45°）・夕（約3°）を撮れる。ImGuiに手動露出（EV100）のスライダーがある。R3の高さフォグを起動画面で有効にし（`kStartupHeightFogDensity` を0.02〜0.1の範囲で撮り比べて選ぶ）、遠くの地面と空の境が空の色へ霞む（遠くの地面の色がフォグ無しより空の地平線の色へ近づき、暗くならない）。撮影の3時刻とも空と地面の色が時刻らしく変わり、昼の画像で近景（球）のコントラストがフォグで落ちていない（球の輝度の標準偏差がフォグ無しの90%以上）。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-DAYLIGHT-P2 -SunElevations 10,45,3`
- stop-when: 霧のパラメータの意味（R3の公開契約）を変えないと起動画面に合わない場合は、契約を変えずに記録して、合う範囲の密度で閉じる。
- paths: Game, Library/Core/Private/Engine, Library/Core/Public/Engine, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 太陽・露出の引数とImGuiは `79b0c48` で実装済み。フォグが下向きの視線で黒くなる（空のモデルが地平線より下で0を返す）ため既定の密度を0にして止めていた（`blocked/SS-DAYLIGHT-P2.md`）。SS-SKY-MODEL-P1 の後に密度を決めて撮り直す。

## SS-CSM-DISTANCE: 影の分割をカメラのfarから切り離し、近くの影を細かくする
- status: done
- done-when: CSMの分割が「影の最大距離」（既定 約80 m、設定できる）で決まり、カメラのfar（1000 m）に依存しない。起動画面の既定視点でカスケード0の1テクセルが2.5 cm以下になる（テストか起動ログで数値を示す）。撮影で球と岩の接地部の影がくっきりし、影の縁が階段状に見えない。影の最大距離より遠い物体は影を受けない（急に消えないよう最後のカスケードの端でフェードする）。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game CascadedShadowLightMatricesTest DirectionalShadowLightMatricesTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CascadedShadowLightMatricesTest|DirectionalShadowLightMatricesTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-CSM-DISTANCE`
- stop-when: 承認済みのOutdoor goldenが変わる場合は、差の原因と物理的な妥当性を確かめて再承認を記録する（承認は任されている）。原因が説明できない差なら止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Game/GameModes/Rendering3DTest, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（影）。評価者を通す。現状はfar=1000・λ=0.5でカスケード0が0.1〜125.5 m、1テクセル約0.157 m（R2前の単一シャドウマップの8倍粗い）。既存のFIX-CSM-TERMINATORとFIX-CSM-MEGA-CASTER-BOUNDSはbacklogに残す。

## SS-SHOWCASE: 起動画面に材質見本の球の列とCottageを置き、地面を広いテクスチャ付きの材質にする
- status: done
- done-when: 既存の天球・地面・球・岩・点光源はそのまま残し、材質見本の球（金属0と1の2列 × 粗さ0.1/0.3/0.5/0.7/0.9の5段、同じ色）と、`Assets/Models/Cottage_Clean` の小屋を置く。小屋のOBJは追跡外のため、標準ライブラリだけのPythonスクリプト（`Scripts/ConvertObjToGltf.py`）でglTFに変換し、変換結果を追跡する。材質に粗さ・金属のスカラー値を指定できないならMaterialCreateDataに足す。地面は約60 m四方にし、テクスチャ付きの材質（CobbleStoneFloorのタイル＋POM）にする。撮影の既定視点で、見本の球・小屋・岩・球が重ならずに見え、金属の球に空と周囲が映り込む。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-SHOWCASE`
- stop-when: 小屋のテクスチャ（4K PNG 計約70MB）の読み込みで起動が30秒以上遅くなる場合は、縮小版を作るか遅延読み込みにするかを記録して、軽い方を採る。
- paths: Game/GameModes/Rendering3DTest, Assets/Models/Cottage_Clean, Scripts/ConvertObjToGltf.py, Library/Core/Public/Rendering, Library/Core/Private/Rendering, TASKS.md, PROGRESS.md
- notes: ユーザー決定（2026-09-27）「展示物を足す」。

## SS-POINT-SHADOW-P1: 影を落とす点光源のキューブシャドウマップを描く
- status: done
- done-when: CastShadowsの点光源（表示中のもの、最大4灯、光源からの距離が近い順）ごとに、6面の距離（光源からの線形距離）をキューブ配列の深度へ描くパスがある。キャスターは光源の範囲の球でカリングする。影の灯の選択と6面の行列はRenderThreadへFramePacketのスナップショット越しに渡る。6面の行列（Vulkanの規約での向きと上方向）をCPUのテスト `PointShadowFaceMatricesTest`（`CameraViewConstantsTest` の束へ MEMBER として足す）で確かめる。デバッグ表示でキューブの中身（距離）を確かめられる。影を落とす点光源が無いシーンでは何も描かず、既存の描画結果が変わらない。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game LightingLightBufferTest CameraViewConstantsTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingLightBufferTest|PointShadowFaceMatricesTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-POINT-SHADOW-P1`
- stop-when: RHIにキューブ配列の深度テクスチャや層ごとの描画先が無い場合は、RHIへの追加を同じタスクで行い、Rendering層からVulkanを直接includeしない。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/RHI, Library/Core/Private/RHI, Assets/Shaders, Test/Core/Rendering, Test/TestBundle, TASKS.md, PROGRESS.md
- notes: ユーザー決定（2026-09-27）「キューブシャドウを実装」。危険地帯（RHI・RenderThread・影）。評価者を通す。新しいテストは実行ファイルを増やさず既存の束へ足す。

## SS-POINT-SHADOW-P2: 点光源の影をライティングに掛け、起動画面の点光源に影を落とさせる
- status: done
- done-when: `lighting.frag` と `forward_transparent.frag` が、影を落とす点光源にキューブシャドウ（PCF、法線方向のずらし）を掛ける。影を落とさない点光源の照明は変わらない。Rendering3DTest の点光源を影ありにする。Rendering3DTest に夜の条件（起動引数 `--night`。空の太陽を消し、環境光は空が無効なときの静的HDRを月明かり程度（地面の照度 約0.1〜1 lx）へ落とす。露出は自動のまま）を足し、撮影スクリプトに `-Night` を足して夜の3視点を撮る。夜の撮影で、点光源による球・岩の影が地面に見え、影の縁にアクネ（縞）とピーターパン（接地部の浮き）が見えない。昼の既定の起動画面は変わらない。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game LightingLightBufferTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingLightBufferTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-POINT-SHADOW-P2-night -Night`
- stop-when: 承認済みのgoldenが変わる場合は差の原因と妥当性を記録して再承認する。影なしの点光源の結果が変わるなら止めて直す。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（ライティング・影）。評価者を通す。
- notes: 影の適用は `20babbe` で実装済みでゲートも合格。1600 lm の点光源は夕の地面の約1%の照度しかなく物理的に見えないため、「夕の撮影で見える」を満たせず止めていた（`blocked/SS-POINT-SHADOW-P2.md`）。2026-09-30、推奨の選択肢1（夜の撮影条件を足し、完了条件を夜に替える）を採った。点光源の物理値は変えない。

## SS-AUTOEXPOSURE-P1: 輝度のヒストグラムから露出を求める
- status: done
- done-when: SceneColorの輝度（log2、プリエクスポージャを外した絶対輝度）のヒストグラム（256区間）をcomputeで作り、下位・上位の外れ（例 下位10%・上位2%）を除いた平均から目標のEV100を求め、明るくなる向き・暗くなる向きで別の速さで順応させる。露出補正（EV）と最小・最大のEV100を設定できる。ヒストグラム→EV100の計算とタイムステップの順応をCPUの参照実装と照合するテスト `AutoExposureMathTest`（`CameraViewConstantsTest` の束へ MEMBER として足す）がある。この段では求めたEV100をログ・デバッグ表示に出すだけで、画面の露出はまだ変えない。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game CameraViewConstantsTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^AutoExposureMathTest$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-AUTOEXPOSURE-P1 -SunElevations 10,45,3`
- stop-when: GPUの結果をCPUへ戻す遅延がRenderThreadの同期を変えないと取れない場合は止めて記録する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Test/TestBundle, TASKS.md, PROGRESS.md
- notes: ユーザー決定（2026-09-27）ポスト処理「基本セット+演出系」。危険地帯（RenderThread）。評価者を通す。

## SS-AUTOEXPOSURE-P2: 自動露出を画面に適用し、起動画面の既定にする
- status: done
- done-when: カメラに露出の方式（手動/自動）があり、自動では前のフレームまでに求めたEV100からプリエクスポージャを決める（数フレームの遅れは許す）。検証シーン（Indoor/Outdoorのgolden、R系の受入れ）は手動のままで結果が変わらない。Rendering3DTest は自動にする。撮影の朝・昼・夕の3枚で、トーンマップ後の平均輝度がどれも0.12〜0.40に入り、白飛び画素率が2%未満。太陽の向きを急に変えたとき、露出が振動せず1〜3秒で落ち着く（連続撮影かログで示す）。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game CameraViewConstantsTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(AutoExposureMathTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-AUTOEXPOSURE-P2 -SunElevations 10,45,3`
- stop-when: 検証シーンの結果が変わる場合は止めて直す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Component, Library/Core/Private/Component, Assets/Shaders, Game/GameModes/Rendering3DTest, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（RenderThread・露出の契約）。評価者を通す。

## FIX-AUTOEXPOSURE-READBACK: 自動露出の読み戻しのホスト可視性とEV100のデバッグ表示
- status: done
- done-when: ヒストグラムの読み戻し先へのコピーの後に、転送の書き込み→ホストの読み取りの依存（Vulkanの`VK_ACCESS_HOST_READ_BIT`・`VK_PIPELINE_STAGE_HOST_BIT`）をRHI経由で記録する（RHIにホスト読み取りの状態か同等の手段を足す）。既知のヒストグラムをGPUで作って読み戻した各区間が一致する検査がある。目標と順応後のEV100を、RenderThreadからGameThreadへスナップショットで渡し、起動画面のImGui（「空の太陽」ウィンドウ）に表示する。
- verify: `cmake --build build --config Debug --target Game CameraViewConstantsTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^AutoExposureMathTest$"`
- stop-when: RHIの状態の追加が既存のバリアの割り当て（`ResourceBarrierTracker`）を変える場合は止めて記録する。
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Game/GameModes/Rendering3DTest, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: SS-AUTOEXPOSURE-P1の評価の指摘1・3。P2の範囲（RHIを含まない）で直せないため分けた。危険地帯（RHI/Vulkan・RenderThread）。評価者を通す。

## SS-BLOOM-MIPCHAIN: ブルームを段階的に縮小・拡大する方式に置き換える
- status: done
- done-when: ブルームが、13タップの縮小（最初の段は明るい画素のちらつきを抑える重み付き平均）を約6段、3×3のテントフィルタでの拡大と加算で作られ、元の色へ一定の割合（既定 約0.04、しきい値なしでエネルギーを保つ方式を既定）で混ぜる。広がりは画面の高さの10〜20%に届く。撮影の昼と夕で、太陽の周り・発光球・金属の球の強い反射の周りに柔らかいにじみが見え、縮小の格子や輪状の模様が見えない。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-BLOOM-MIPCHAIN -SunElevations 45,3`
- stop-when: 承認済みのgoldenが変わる場合は差の原因と妥当性を記録して再承認する（任されている）。原因が説明できない差なら止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 今のブルームは1回の16タップで、広がりは最大 約10 px（`bloom.frag`）。値の調整では広がらない。

## SS-EMISSIVE-PREEXPOSE: GBufferへ露出を掛けた発光を書き、明るい発光がRGBA16Fで溢れないようにする
- status: done
- done-when: `gbuffer.frag`・`megageometry.frag`・スキニングのGBuffer（`GBuffer_Emissive` へ書くすべてのシェーダー）が、発光（nits）にそのフレームのプリエクスポージャを掛けてから書き、Lighting 側で GBuffer の発光に露出を掛ける箇所を外す（発光の最終的な寄与は数値的に同じ）。前向きの透明（`forward_transparent.frag`）など SceneColor へ直接書く経路の発光は、既に露出後の値で書いているかを確かめ、揃っていなければ揃える。PT・RTGI・DDGI は材質から発光を読むので変えない（GBufferから読む経路があれば同じ規約に揃える）。Indoor/Outdoor の golden が変わらない（丸めの差で変わるなら、差がこの変更だけによることを確かめて再承認）。昼の露出（EV100 約14.6）で 1,000,000 nits の発光面が SceneColor で有限で、無限大・NaN にならないことを GPU の検査（既存の束へ MEMBER として足すか、既存のGPUテストへケースを足す。新しい実行ファイルは作らない）で確かめる。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-EMISSIVE-PREEXPOSE -SunElevations 45,3`
- stop-when: 自動露出のフレームで GBuffer を書くときの露出と Lighting が使う露出が別のフレームの値になる経路が見つかったら、同じフレームの値を使うように直す（直せないなら止めて記録する）。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: SS-EMISSIVE-GLOW の停止理由（`blocked/SS-EMISSIVE-GLOW.md`）の推奨1。2026-09-30 に採った。今は発光を物理の nits のまま `GBuffer_Emissive`（R16G16B16A16_FLOAT、上限65504）へ書くため、色(1,0.9,0.3)で約57000 nitsを超えると無限大になり発光が消える。危険地帯（GBuffer・ライティング・露出）。評価者を通す。

## SS-EMISSIVE-GLOW: 起動画面の発光球をブルームでにじむ明るさにする
- status: done
- done-when: 起動画面の発光球の輝度（今は1800 nits）を、昼・夕の自動露出で画面の平均輝度に対して十分に明るく（目安: 撮影の夕で周りの背景の30倍以上）なる物理的な値にし、撮影の夕で発光球の周りに柔らかいにじみが見える。昼は発光球が背景より明るく見え、白飛び・露出の悪化が無い（真昼の屋外では電球はほとんどにじまないので、昼のにじみは求めない）。ブルームの既定（しきい値なし・0.04）は変えない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-EMISSIVE-GLOW -SunElevations 45,3`
- stop-when: 発光球が白飛びの割合の基準（2%）を超える、または露出が発光球に引っ張られて画面が暗くなる場合は止めて記録する。
- paths: Game/GameModes/Rendering3DTest, TASKS.md, PROGRESS.md
- notes: SS-BLOOM-MIPCHAINで分かった。夕の自動露出（EV100 約11.1）では発光球はプリエクスポージャ後 約0.7で画面の平均の約4倍しかなく、しきい値なし0.04の補間ではにじみが縁の外5 pxで数%にとどまり見えない。
- notes: 反復15で発光球を45000 nitsにし、夕はにじむ（背景の約70倍）。昼はプリエクスポージャ後 約1.5でにじまない。GBufferの発光がRGBA16Fの物理nitsのため 約57000 nitsが上限で、昼に要る 約150000 nitsに届かない（blocked/SS-EMISSIVE-GLOW.md）。
- notes: 2026-09-30 再開。SS-EMISSIVE-PREEXPOSE で上限を外した後、発光球を物理的にありうる輝度（つや消しの電球の表面 約1〜1.5×10^5 nits の範囲）へ上げて昼・夕を撮り直す。値と根拠をコミット本文に書く。
- notes: 2026-10-02 完了。`85f5c6e` で150000 nits（内面つや消し電球の表面 約15 cd/cm²）。夕は場面の幾何平均の約500倍で縁の外66 pxまで柔らかくにじみ、昼は約22倍でにじみは縁の外6 px（`startup-capture/SS-EMISSIVE-GLOW/before-after-default-sun3-x3.png`・`-sun45-x3.png` を開いて確かめた）。露出は発光球に引っ張られず（平均輝度 178.6→178.5）、白飛び率は最大0.00034。昼のにじみは物理的な値とブルームの既定を保ったままでは出ないため、`blocked/SS-EMISSIVE-GLOW.md` の推奨1で完了条件から外した（600000 nitsでも昼は縁の外26 pxの淡い輪）。

## FIX-CAPTURE-SUN-AZIMUTH: 撮影スクリプトの -SunAzimuth が null のメソッド呼び出しで落ちる
- status: done
- done-when: `Scripts/CaptureStartupScene.ps1 -SunAzimuth <度>`が太陽の方位を渡して撮影でき、太陽を画角に入れる視点（例: 低角度の視点で方位 -120°）を撮れる。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/FIX-CAPTURE-SUN-AZIMUTH -SunElevations 3 -SunAzimuth -120`
- stop-when: なし。
- paths: Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: `[Nullable[double]]`の引数は値が入ると`double`になるため`$SunAzimuth.Value`がnullになる（199行目）。SS-BLOOM-MIPCHAINではGame.exeを同じ引数で直接起動して撮った。

## SS-TAA-P1: ジッタと履歴の再投影でTAAを作る
- status: done
- done-when: 投影行列にHalton(2,3)の8〜16点のサブピクセルのジッタを掛け、R6-aのvelocity（ジッタを除いた動き）で前のフレームの履歴を再投影し、近傍の色の分散（YCoCg）でクリップして混ぜるTAAのパスがある（位置はライティング・半透明の後、ブルームの前）。カメラが止まっているとき、ジッタで輪郭のギザギザが消える。有効/無効を切り替えられ、無効なら今の結果（FXAA）と変わらない。ジッタ列と投影のずらし量をCPUのテスト `TemporalAAJitterTest`（`CameraViewConstantsTest` の束へ MEMBER として足す）で確かめる。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game CameraViewConstantsTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(TemporalAAJitterTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-TAA-P1`
- stop-when: 検証シーン（golden）はTAA無効のまま結果が変わらないこと。変わるなら止めて直す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Component, Library/Core/Private/Component, Assets/Shaders, Test/Core/Rendering, Test/TestBundle, TASKS.md, PROGRESS.md
- notes: 危険地帯（カメラ・投影・RenderThread）。評価者を通す。
- notes: 評価1周目（`06a26a7`）は NEEDS_WORK。指摘は `NEXT_FINDINGS.md`（blocking 2件: 描画がゲームのフレームを飛ばしたときの履歴とvelocityの基準フレームの食い違い、1つのSceneViewに2つのViewportがあるときの毎フレームの履歴破棄）。`7461c3e` は反復の途中で止めた未検証の保存で、この指摘への対応（`HistoryFrameNumber` など）と SS-TAA-P2 の一部（MegaGeometry・スキニングのvelocity、`-OrbitDegreesPerSecond`、シャープ化）が混ざる。この項目では指摘への対応を仕上げてフレームの欠落を意図的に起こす検証を足し、P2 の部分はビルドが通り既定の結果を変えない状態に保つ（検証は SS-TAA-P2 で行う）。

## SS-TAA-P2: TAAをほかのパスと整合させ、起動画面の既定にする
- status: done
- done-when: UI・デバッグ描画・ImGuiはジッタの無い最終解像度に描かれる。スキニング・動く物体のvelocityが正しく、カメラの切り替え・画面サイズの変更で履歴を捨てる。解像感を戻す軽いシャープ化がある。撮影スクリプトにカメラを一定の速さで回す連続撮影（`-OrbitDegreesPerSecond`）を足し、回転中の画像で輪郭のゴースト（残像の筋）が見えない。Rendering3DTest の既定をTAAにし、FXAAは選択肢として残す。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-TAA-P2 -OrbitDegreesPerSecond 30`
- stop-when: 検証シーンの結果が変わるなら止めて直す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（RenderThread）。評価者を通す。

## SS-GTAO: SSAOをGTAOに置き換える
- status: done
- result: 2026-10-02。`gtao.frag`（2スライス×片側4段、半径1 m、外側61.5%で重みを0へ、画面上の上限128画素）と`gtao_denoise.frag`（4×4の深度考慮の平均）で旧SSAO（半球32サンプル）を置き換えた。TAAのジッタがあるときは雑音の並びをフレームごとにずらして履歴で蓄積する。多重反射の近似は`lighting.frag`の拡散の環境光に掛ける。Cornellの部屋（`RenderingGTAOCornellRoomVulkanTest`）で壁・天井・床の中ほど0.97〜1.00、隅・接地部0.77〜0.81。Outdoor goldenは1 LSBの差で再承認（`R1Acceptance.md`）。起動画面の撮影は`.harness/runs/startup-capture/SS-GTAO/`。
- done-when: 地平線ベースのAO（GTAO。画素ごとに2方向×数段、空間の雑音除去、TAAがあれば時間方向にも蓄積）が今のSSAO（半球32サンプル）を置き換え、多重反射の近似で明るい面の遮蔽を弱める。半径は世界の長さ（m）で指定する。撮影で、球・岩・小屋と地面の接地部、小屋の軒下に自然な遮蔽が見え、部屋の大きさの遮蔽で壁全体が黒くならない（Indoorの検証シーンで確かめる）。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-GTAO`
- stop-when: 承認済みのgoldenが変わる場合は差の原因と妥当性を記録して再承認する（任されている）。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 既存のFIX-SSAO-ROOM-SCALE（backlog）はこのタスクで置き換わる見込み。閉じたら、そのタスクに結果を書き添える。

## SS-CONTACT-SHADOW: 影の灯にコンタクトシャドウを足す
- status: done
- done-when: 影を掛ける方向光（空の太陽）について、画面空間で光の方向へ短く（世界で約0.2〜0.5 m）レイマーチする接触影をCSMの結果と掛け合わせる。影を落とす点光源にも同じ仕組みを使えるなら使う。撮影の近接視点と低角度で、球・岩・小屋の接地部に、CSMでは出ない細い影が見え、物体の表面に自己遮蔽の縞が出ない。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-CONTACT-SHADOW`
- stop-when: 承認済みのgoldenが変わる場合は差の原因と妥当性を記録して再承認する（任されている）。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（ライティング・影）。評価者を通す。

## SS-POST-TUNE: SSRを粗さでなめらかに消し、グレーディングとビネットを起動画面に合わせる
- status: done
- done-when: SSRが粗さのしきい値で急に切れず、粗さ約0.3〜0.7の間でなめらかに弱まる（材質の粗さを使う）。起動画面で、濡れていない石畳には弱い反射、金属の見本の球には周囲の反射が見える。起動画面のグレーディング（コントラスト・彩度・色温度）とビネットを、昼・夕の撮影で眠く見えない値にする（値と理由を記録する）。検証シーンのグレーディングは変えない。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-POST-TUNE -SunElevations 45,3`
- stop-when: SSRの変更で承認済みのgoldenが変わる場合は差の原因と妥当性を記録して再承認する（任されている）。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game/GameModes/Rendering3DTest, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 今はテクスチャの無い地面の粗さが既定の128/255（0.502）で、SSRのcutoff 0.5を超えて棄却されている。
- notes: 2026-10-02 完了。地面はSS-SHOWCASEで石畳の材質（粗さのテクスチャはUNORMで中央値0.76、5%点0.715）に替わっていて、フェードの終わり0.7より粗いためSSRは掛からない（乾いた粗い石畳に鏡の反射を足さない）。石畳の弱い反射は従来どおりIBLの鏡面（斜めの空のつや）で、SSRで見えるのは金属の見本の球（粗さ0.1・0.3・0.5）の下半分の地面の映り込み。

## SS-LENS-FX: 色収差とレンズダートを足す
- status: done
- done-when: 画面の端ほど強くなる放射方向の色収差（既定は弱く、R/Bのずれが画面端で約1〜2 px）と、ブルームに掛けるレンズダート（起動時に手続きで作るテクスチャ。外部の画像は使わない）がある。どちらも設定で切り替えられ、起動画面では有効、検証シーンでは無効で結果が変わらない。撮影の夕方（太陽が画面内）で、ブルームにダートの模様が乗り、画面端の輪郭に色のにじみがわずかに見える。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-LENS-FX -SunElevations 3`
- stop-when: 検証シーンの結果が変わるなら止めて直す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game/GameModes/Rendering3DTest, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: ユーザー決定（2026-09-27）「基本セット+演出系」。

## SS-GRADING-LUT: グレーディング用の3D LUTを掛けられるようにする
- status: done
- done-when: トーンマップの後に32³の3D LUTを掛けられ、恒等のLUTを掛けても結果が変わらないこと（GPUの出力の一致）をテスト `GradingLutIdentityVulkanTest`（`R8AcesLutToneMappingVulkanTest` の束へ MEMBER として足す）で確かめる。見た目のLUT（暖かみのある映画調）を作るスクリプト（`Scripts/BakeLookLut.py`、標準ライブラリとnumpyまで）と生成物を追跡し、起動画面で使う。検証シーンではLUTを使わない。撮影の昼・夕で、LUTの有無の2枚を比べて色調が変わり、階調の段差（バンディング）が出ない。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game R8AcesLutToneMappingVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(GradingLutIdentityVulkanTest|R8AcesLutToneMappingVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-GRADING-LUT -SunElevations 45,3`
- stop-when: R8のACES 2.0 LUT（`--tone-map=aces20-lut`）の経路と衝突する場合は、併用の順序を記録して、既定のACES経路にだけ掛ける。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Assets/Textures, Scripts/BakeLookLut.py, Game/GameModes/Rendering3DTest, Test/Core/Rendering, TASKS.md, PROGRESS.md

## SS-RTGI-DEFAULT: TAAの上でRTGIを起動画面の既定にする
- status: done
- done-when: ハードウェアのレイトレが使える環境では、起動画面でRTGIを有効にし（`30f00f8` で切った設定を戻す）、TAAと既存のデノイズで粒状の雑音が見えない。止まったカメラで16フレームを撮り、静止した地面の領域の画素の時間方向の標準偏差が、RTGI無効のときの2倍以内に収まる。レイトレが使えない環境ではIBL（空由来）に落ちて同じシーンが表示される。撮影で、小屋の軒下や球の下の地面に色のにじみ（間接光）が見える。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-RTGI-DEFAULT`
- stop-when: 雑音の基準を満たすのにRTGIの光線数を増やしてフレーム時間が2倍を超える場合は、既定にせず、測った値を既知の限界として記録して完了にする。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game/GameModes/Rendering3DTest, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 危険地帯（RTGI・RenderThread）。評価者を通す。
- notes: 2026-10-02 の反復（run 20261002-084328 の#20）は60分の壁時計で止められ、途中の変更は `b40ce35`（未検証の途中保存。行末は `d3172d6` で元へ戻した）に入っている。中身: RTGIの静止判定を位置と拡大率で見る（球の自転で毎フレーム履歴が捨てられていた）、露出が変わったときに履歴へ比を掛け直す、小屋・岩（MegaGeometry）のLOD0をレイトレのシーンへ入れる、起動画面でRTGIを既定で有効（`NORVES_STARTUP_RTGI=0` で無効）、切り替わりのときだけのログ、撮影スクリプトの静止16フレームの時間方向の雑音の測定。残っていたのは、地面の領域で履歴が棄却される原因（法線・材質の比較）の切り分けと、低角度の手前の石の目地に沿った雑音。まず `b40ce35` の差分を読み、ビルドと撮影で今の状態を測ってから続ける。
- notes: 2026-10-02 の評価1周目の差し戻し（低角度の中ほどの地面が2.49倍）に対応した。静止が続いているフレーム（年齢の上限が8を超える）では、RTGIの履歴を法線・材質の差で棄却せず、距離と裏返りだけで判定する。止まった16フレームの雑音は `CaptureStartupScene.ps1 -StillRenderedFrames ... -Rtgi On -CompareNoiseWith <-Rtgi Off の出力先>` で視点・帯ごとにRTGI無効との比を判定する（2倍を超えると失敗）。

## SS-LOOK-BALANCE: 起動画面の露出とトーンの釣り合いを朝・昼・夕・夜で取り直す
- status: done
- done-when: 起動画面のカメラだけで（検証シーンのカメラの既定のトーンマップ・露出・グレーディングは変えず、goldenは変わらない）、撮影（朝10°・昼45°・夕3°・夜 × 既定・近接・低角度）が次を満たす。(1) 画面全体の平均（`metrics.json` の平均輝度、0〜255）が 昼105〜140・朝95〜135・夕75〜120・夜25〜80。(2) 白飛び画素率1%未満（太陽・発光球を含めて）、黒つぶれ画素率は夜以外で2%未満。(3) 昼の影の中の比（SS-DAYLIGHT-P1 と同じ領域・表示のリニア輝度の比）が15〜40%。(4) 昼の画面上端の帯（y<60）の平均が青（sRGBの符号値で B − R ≥ 40）。(5) 夕の日向の地面の平均色が橙〜黄（R > G > B）で、桃・紫（B ≥ G）に寄らない。(6) 夜は背景が夜に見え（静的HDRの昼の写真がそのまま明るく写らない。画面上端の帯の平均が40/255未満）、点光源の光だまりが周りより明るく、その中で球・岩の影が見える。昼・夕・夜の既定視点で、変更前（このタスクの着手時）と変更後を並べた画像を残す。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-LOOK-BALANCE -SunElevations 10,45,3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-LOOK-BALANCE-night -Night`
- stop-when: すべての範囲を同時に満たす値の組が見つからない場合は、最も近い組と各値の測定値を既知の限界として記録して完了にする。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Component, Library/Core/Private/Component, Assets/Shaders, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-02 の親の確認で分かった。SS-DAYLIGHT-P1 の対応で起動画面の自動露出に一律の+2 EVの露出補正（`8b80473`）を掛けた結果、影の比は満たしたが画面全体の平均が約180/255になり、昼は白っぽく飛んで眠く、夕は桃色がかり、夜は背景の静的HDR（昼の芝生と木の写真）と地面が真っ白に写って夜に見えない（`startup-capture/SS-GRADING-LUT/default-sun45.png`・`default-sun3.png`、`SS-CONTACT-SHADOW-night/default-night.png`）。個々の項目の完了条件は満たしていたが、画面全体の釣り合いを見る条件が無かった。
- notes: やり方の目安: 一律の+2 EVをやめるか小さくし、暗部を縮めにくいトーンカーブ（例: Khronos PBR Neutral や AgX をトーンマップの選択肢に足してカメラごとに選べるようにする、または既存の `Aces20Lut`）と組み合わせる。夜は自動露出が昼並みの明るさへ戻しきらないようにする（明るさに応じた露出補正、または目標の範囲）。夜の背景は昼の写真を出さない（夜の空の色にする等）。SS-POST-TUNE・SS-GRADING-LUT・高さフォグの値は、ほかの値を変えた後に撮り比べ、必要なら合わせ直す。危険地帯（トーンマップ・露出・FramePacketのカメラ）。評価者を通す。

## SS-RTGI-FAR-NOISE: 起動画面の遠い地面に残るRTGIの時間方向の雑音を減らす
- status: done
- done-when: 止まったカメラで16フレームを撮り（`-StillRenderedFrames`）、既定視点の地平線寄りの帯（画面の高さの0.33〜0.42）と低角度視点の中ほどの帯（0.65〜0.80）でも、地面の画素の時間方向の標準偏差（表示の輝度）がRTGI無効のときの2倍以内に収まる（手前の帯 0.80〜0.98 の既存の基準も保つ）。帯ごとの値を `metrics.json` に出せるよう撮影スクリプトの測定領域を視点ごとに複数にする。止まった状態で疎らに明滅する画素（標準偏差が1/255を超える画素）の割合も記録する。
- verify: `cmake --build build --config Debug --target Game RTGIDiffuseIndirectVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RTGIDiffuseIndirectVulkanTest$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-RTGI-FAR-NOISE-Off -StillRenderedFrames 160,168,176,184,192,200,208,216,224,232,240,248,256,264,272,280 -Rtgi Off`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-RTGI-FAR-NOISE-On -StillRenderedFrames 160,168,176,184,192,200,208,216,224,232,240,248,256,264,272,280 -Rtgi On -CompareNoiseWith .harness/runs/startup-capture/SS-RTGI-FAR-NOISE-Off`
- stop-when: 原因が画素ごとの履歴の棄却ではなく、ジッタで変わるGBufferの標本そのもの（遠い画素の中の石の目地）にあり、棄却を緩めると動く物体の残像が出る場合は、帯ごとの測定値と原因を既知の限界として記録して完了にする。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
