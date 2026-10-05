# TASKS — NorvesLib

起動画面（Rendering3DTest）の描画改善（2026-09-27、ユーザー決定）。物理空と空の太陽による昼の屋外、点光源のキューブシャドウ、自動露出・ミップチェーンのブルーム・TAA・GTAO・コンタクトシャドウ・色収差・レンズダート・グレーディングLUT・RTGIの既定化、展示物の追加、視差オクルージョンと影の不具合の修正を `SS-` の項目で進める。見た目の証拠は `Scripts/CaptureStartupScene.ps1` の撮影を開いて確かめる。起動画面の見た目を変えることはこの計画でユーザーが承認済み。検証シーン（Indoor/Outdoorのgolden、R系の受入れ）は、項目に書いた場合を除き結果を変えない。

2026-09-30 再開: 止めていた4項目（SS-DAYLIGHT-P1・P2、SS-POINT-SHADOW-P2、SS-EMISSIVE-GLOW）は各 `blocked/<ID>.md` の推奨の選択肢で再開し、空のモデル（SS-SKY-MODEL-P1・P2）と発光の露出（SS-EMISSIVE-PREEXPOSE）を足した。この3項目は、項目に書いたとおり空を使う検証（R2・R7屋外）とgoldenの結果を変えうる。

それより下はR0〜R8と関連の修正の記録。R8までの完了後に残った `todo` は、起動画面の作業を先に進めるため `backlog`（ループが拾わない）にしてある。再開するときは `todo` へ戻す。

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
- verify: `cmake --build build --config Debug --target Game RenderingHdrSceneCaptureTest PathTracingVulkanTest R7OutdoorPathTracingReferenceVulkanTest LightingLightBufferTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingHdrOutdoorSceneVulkanTest|PathTracingOutdoorVulkanTest|LightingLightBufferTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error --timeout 1800 -R "^R7OutdoorPathTracingReferenceVulkanTest$"`
- stop-when: 閾値や規則そのものを変えないと通らない場合は、変えずに測定値と分類を既知の限界として記録して完了にする。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 参照比較の段（`-L Reference`）のうち空を使うものだけを回す（R7屋外は単独で約12分）。GPUのテストは同時に2本走らせない。上の verify の対象名が CMake に無い場合は、`Test/Core/Rendering/CMakeLists.txt` の実際の名前へ読み替え、読み替えを PROGRESS に書く。

## SS-DAYLIGHT-P1: 起動画面を物理空と空の太陽による昼の屋外にする
- status: todo
- done-when: Rendering3DTest が R2 の物理空（SkyAtmosphere）を有効にし、空の太陽（仰角約40°、カメラの既定視点から球と岩の影が地面に見える方位）が影を落とす方向光になる。シーン独自の方向光は外し、方向ライトの操作（矢印キーとImGui）は空の太陽の仰角・方位を動かす。`f90e7ea` の露出補正を外す（露出は SS-AUTOEXPOSURE-P2 の自動露出）。点光源（Lumen/Candelaの物理単位）と発光球の輝度も物理的にありうる値へ移す。IBLは空から作る（静的HDRは空が無効なときだけ使う）。撮影で、青い昼の空の下、球と岩の影が地面にはっきり見え、影の中の地面の平均輝度が日向の15〜40%（黒くつぶれず、影と分かる。PNGの領域を開いて測る）、白飛び画素率が1%未満。昼の撮影の近接視点で、石畳の球の石の太陽側の斜面が明るく反対側が暗い（法線マップ `nor_gl` の緑の向きが凹凸と合う。逆なら直す）。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-DAYLIGHT-P1 -SunElevations 45`
- stop-when: 影の中の比が範囲に入らない原因が空のモデル（SS-SKY-MODEL-P1）の側にある場合は、そちらへ差し戻さず、測った値と原因を記録してこのタスクで直せる範囲（起動画面の値）で閉じる。
- paths: Game/GameModes/Rendering3DTest, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Component, Library/Core/Private/Component, Assets/Shaders, Assets/Textures, TASKS.md, PROGRESS.md
- notes: ユーザー決定（2026-09-27）「物理的な昼の屋外へ」。影が見えない原因は、静的HDRの環境光が地面の照度の約87%を占め（方向光1 lux）、影で約13%しか暗くならないこと（`f3d4abb` でIBLを E/π にした後、比が約9倍悪化）。空を有効にするとエンジンが空の太陽の方向光を自動で加える（SKY-SUN-P2）。起動画面の見た目を変える変更で、この変更はユーザーの承認済み。
- notes: 起動画面の側は `ad6729a` で実装済み。空のモデルが暗く橙で地平線より下が黒いため止めていた（`blocked/SS-DAYLIGHT-P1.md`）。SS-SKY-MODEL-P1 の後に撮り直し、完了条件を確かめる。法線マップの向きの確認は SS-POM の評価の指摘の残り。

## SS-DAYLIGHT-P2: 太陽の向きを操作・指定でき、高さフォグを掛ける
- status: todo
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
- status: todo
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
- status: todo
- done-when: `gbuffer.frag`・`megageometry.frag`・スキニングのGBuffer（`GBuffer_Emissive` へ書くすべてのシェーダー）が、発光（nits）にそのフレームのプリエクスポージャを掛けてから書き、Lighting 側で GBuffer の発光に露出を掛ける箇所を外す（発光の最終的な寄与は数値的に同じ）。前向きの透明（`forward_transparent.frag`）など SceneColor へ直接書く経路の発光は、既に露出後の値で書いているかを確かめ、揃っていなければ揃える。PT・RTGI・DDGI は材質から発光を読むので変えない（GBufferから読む経路があれば同じ規約に揃える）。Indoor/Outdoor の golden が変わらない（丸めの差で変わるなら、差がこの変更だけによることを確かめて再承認）。昼の露出（EV100 約14.6）で 1,000,000 nits の発光面が SceneColor で有限で、無限大・NaN にならないことを GPU の検査（既存の束へ MEMBER として足すか、既存のGPUテストへケースを足す。新しい実行ファイルは作らない）で確かめる。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-EMISSIVE-PREEXPOSE -SunElevations 45,3`
- stop-when: 自動露出のフレームで GBuffer を書くときの露出と Lighting が使う露出が別のフレームの値になる経路が見つかったら、同じフレームの値を使うように直す（直せないなら止めて記録する）。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: SS-EMISSIVE-GLOW の停止理由（`blocked/SS-EMISSIVE-GLOW.md`）の推奨1。2026-09-30 に採った。今は発光を物理の nits のまま `GBuffer_Emissive`（R16G16B16A16_FLOAT、上限65504）へ書くため、色(1,0.9,0.3)で約57000 nitsを超えると無限大になり発光が消える。危険地帯（GBuffer・ライティング・露出）。評価者を通す。

## SS-EMISSIVE-GLOW: 起動画面の発光球をブルームでにじむ明るさにする
- status: todo
- done-when: 起動画面の発光球の輝度（今は1800 nits）を、昼・夕の自動露出で画面の平均輝度に対して十分に明るく（目安: 撮影の夕で周りの背景の30倍以上）なる物理的な値にし、撮影の昼と夕で発光球の周りに柔らかいにじみが見える。ブルームの既定（しきい値なし・0.04）は変えない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-EMISSIVE-GLOW -SunElevations 45,3`
- stop-when: 発光球が白飛びの割合の基準（2%）を超える、または露出が発光球に引っ張られて画面が暗くなる場合は止めて記録する。
- paths: Game/GameModes/Rendering3DTest, TASKS.md, PROGRESS.md
- notes: SS-BLOOM-MIPCHAINで分かった。夕の自動露出（EV100 約11.1）では発光球はプリエクスポージャ後 約0.7で画面の平均の約4倍しかなく、しきい値なし0.04の補間ではにじみが縁の外5 pxで数%にとどまり見えない。
- notes: 反復15で発光球を45000 nitsにし、夕はにじむ（背景の約70倍）。昼はプリエクスポージャ後 約1.5でにじまない。GBufferの発光がRGBA16Fの物理nitsのため 約57000 nitsが上限で、昼に要る 約150000 nitsに届かない（blocked/SS-EMISSIVE-GLOW.md）。
- notes: 2026-09-30 再開。SS-EMISSIVE-PREEXPOSE で上限を外した後、発光球を物理的にありうる輝度（つや消しの電球の表面 約1〜1.5×10^5 nits の範囲）へ上げて昼・夕を撮り直す。値と根拠をコミット本文に書く。

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
- status: todo
- done-when: UI・デバッグ描画・ImGuiはジッタの無い最終解像度に描かれる。スキニング・動く物体のvelocityが正しく、カメラの切り替え・画面サイズの変更で履歴を捨てる。解像感を戻す軽いシャープ化がある。撮影スクリプトにカメラを一定の速さで回す連続撮影（`-OrbitDegreesPerSecond`）を足し、回転中の画像で輪郭のゴースト（残像の筋）が見えない。Rendering3DTest の既定をTAAにし、FXAAは選択肢として残す。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-TAA-P2 -OrbitDegreesPerSecond 30`
- stop-when: 検証シーンの結果が変わるなら止めて直す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（RenderThread）。評価者を通す。

## SS-GTAO: SSAOをGTAOに置き換える
- status: todo
- done-when: 地平線ベースのAO（GTAO。画素ごとに2方向×数段、空間の雑音除去、TAAがあれば時間方向にも蓄積）が今のSSAO（半球32サンプル）を置き換え、多重反射の近似で明るい面の遮蔽を弱める。半径は世界の長さ（m）で指定する。撮影で、球・岩・小屋と地面の接地部、小屋の軒下に自然な遮蔽が見え、部屋の大きさの遮蔽で壁全体が黒くならない（Indoorの検証シーンで確かめる）。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-GTAO`
- stop-when: 承認済みのgoldenが変わる場合は差の原因と妥当性を記録して再承認する（任されている）。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 既存のFIX-SSAO-ROOM-SCALE（backlog）はこのタスクで置き換わる見込み。閉じたら、そのタスクに結果を書き添える。

## SS-CONTACT-SHADOW: 影の灯にコンタクトシャドウを足す
- status: todo
- done-when: 影を掛ける方向光（空の太陽）について、画面空間で光の方向へ短く（世界で約0.2〜0.5 m）レイマーチする接触影をCSMの結果と掛け合わせる。影を落とす点光源にも同じ仕組みを使えるなら使う。撮影の近接視点と低角度で、球・岩・小屋の接地部に、CSMでは出ない細い影が見え、物体の表面に自己遮蔽の縞が出ない。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-CONTACT-SHADOW`
- stop-when: 承認済みのgoldenが変わる場合は差の原因と妥当性を記録して再承認する（任されている）。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（ライティング・影）。評価者を通す。

## SS-POST-TUNE: SSRを粗さでなめらかに消し、グレーディングとビネットを起動画面に合わせる
- status: todo
- done-when: SSRが粗さのしきい値で急に切れず、粗さ約0.3〜0.7の間でなめらかに弱まる（材質の粗さを使う）。起動画面で、濡れていない石畳には弱い反射、金属の見本の球には周囲の反射が見える。起動画面のグレーディング（コントラスト・彩度・色温度）とビネットを、昼・夕の撮影で眠く見えない値にする（値と理由を記録する）。検証シーンのグレーディングは変えない。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-POST-TUNE -SunElevations 45,3`
- stop-when: SSRの変更で承認済みのgoldenが変わる場合は差の原因と妥当性を記録して再承認する（任されている）。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game/GameModes/Rendering3DTest, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 今はテクスチャの無い地面の粗さが既定の128/255（0.502）で、SSRのcutoff 0.5を超えて棄却されている。

## SS-LENS-FX: 色収差とレンズダートを足す
- status: todo
- done-when: 画面の端ほど強くなる放射方向の色収差（既定は弱く、R/Bのずれが画面端で約1〜2 px）と、ブルームに掛けるレンズダート（起動時に手続きで作るテクスチャ。外部の画像は使わない）がある。どちらも設定で切り替えられ、起動画面では有効、検証シーンでは無効で結果が変わらない。撮影の夕方（太陽が画面内）で、ブルームにダートの模様が乗り、画面端の輪郭に色のにじみがわずかに見える。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-LENS-FX -SunElevations 3`
- stop-when: 検証シーンの結果が変わるなら止めて直す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game/GameModes/Rendering3DTest, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: ユーザー決定（2026-09-27）「基本セット+演出系」。

## SS-GRADING-LUT: グレーディング用の3D LUTを掛けられるようにする
- status: todo
- done-when: トーンマップの後に32³の3D LUTを掛けられ、恒等のLUTを掛けても結果が変わらないこと（GPUの出力の一致）をテスト `GradingLutIdentityVulkanTest`（`R8AcesLutToneMappingVulkanTest` の束へ MEMBER として足す）で確かめる。見た目のLUT（暖かみのある映画調）を作るスクリプト（`Scripts/BakeLookLut.py`、標準ライブラリとnumpyまで）と生成物を追跡し、起動画面で使う。検証シーンではLUTを使わない。撮影の昼・夕で、LUTの有無の2枚を比べて色調が変わり、階調の段差（バンディング）が出ない。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game R8AcesLutToneMappingVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(GradingLutIdentityVulkanTest|R8AcesLutToneMappingVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-GRADING-LUT -SunElevations 45,3`
- stop-when: R8のACES 2.0 LUT（`--tone-map=aces20-lut`）の経路と衝突する場合は、併用の順序を記録して、既定のACES経路にだけ掛ける。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Assets/Textures, Scripts/BakeLookLut.py, Game/GameModes/Rendering3DTest, Test/Core/Rendering, TASKS.md, PROGRESS.md

## SS-RTGI-DEFAULT: TAAの上でRTGIを起動画面の既定にする
- status: todo
- done-when: ハードウェアのレイトレが使える環境では、起動画面でRTGIを有効にし（`30f00f8` で切った設定を戻す）、TAAと既存のデノイズで粒状の雑音が見えない。止まったカメラで16フレームを撮り、静止した地面の領域の画素の時間方向の標準偏差が、RTGI無効のときの2倍以内に収まる。レイトレが使えない環境ではIBL（空由来）に落ちて同じシーンが表示される。撮影で、小屋の軒下や球の下の地面に色のにじみ（間接光）が見える。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-RTGI-DEFAULT`
- stop-when: 雑音の基準を満たすのにRTGIの光線数を増やしてフレーム時間が2倍を超える場合は、既定にせず、測った値を既知の限界として記録して完了にする。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game/GameModes/Rendering3DTest, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 危険地帯（RTGI・RenderThread）。評価者を通す。

## SS-ACCEPT: 起動画面の改善を受け入れる
- status: todo
- done-when: 朝・昼・夕 × 既定・近接・低角度の撮影一式と、変更前（`163ffe5`）の同じ視点の撮影を並べた記録（`Docs/RenderingValidation/StartupSceneAcceptance.md`、画像は `.harness/runs/` への参照）がある。Releaseの構成で起動画面の1フレームの時間（GPU）を測り、1280×720で16.6 ms以下であることを記録する（超える場合はパスごとの内訳と、どれを軽くすれば収まるかを記録する）。夜（`-Night`）の撮影も並べる。評価者が、各項目の完了条件と撮影を開いて反証を試みる。
- verify: `cmake --build build --config Release --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT -SunElevations 10,45,3`
- stop-when: フレーム時間の予算を超える場合は、内訳と軽くする案を既知の限界として記録して完了にする（ユーザーへの報告に含める）。
- paths: Docs/RenderingValidation, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 区切り。評価者を通す。

## R1-P5: 透明描画を物理ライト・GGX・IBLへ接続する
- status: done
- done-when: 条件付き計画v4の固定10行、direct/IBL/shadow/metallic mutation、P4回帰6条件、Indoor/Outdoorのobject-presenceが数値契約を満たす。focused8 build、CPU6、skip契約を通し、source18本を1論理変更としてコミットする。
- verify: `cmake --build build --config Debug --target DirectionalShadowPassWiringContractTest ForwardPassPipelinePlacementTest LightingLightBufferTest RenderGraphCompileTest LightingParamsLayoutTest RenderingValidationSceneContractTest RenderingHdrSceneCaptureTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(DirectionalShadowPassWiringContractTest|ForwardPassPipelinePlacementTest|LightingLightBufferTest|RenderGraphCompileTest|LightingParamsLayoutTest|RenderingValidationSceneContractTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderingHdrSceneVulkanSkipContractTest$"`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=known-cd-lambert`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=ibl-prefilter-nonconstant`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=dfg-lut`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=ibl-roughness-sweep`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=white-furnace`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=direct-conductor-endpoint`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=transparent-physical-lighting`
- verify: `build\Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe --scene=indoor --capture-source=back-buffer --measure-visual`
- verify: `build\Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe --scene=outdoor --capture-source=back-buffer --measure-visual`
- paths: Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Public/Rendering/ShadowMapPass.h, Library/Core/Private/Rendering/ShadowMapPass.cpp, Library/Core/Public/Rendering/LightingPass.h, Library/Core/Private/Rendering/LightingPass.cpp, Test/Core/Rendering/LightingLightBufferTest.cpp, Test/Core/Rendering/DirectionalShadowPassWiringContractTest.cpp, Library/Core/Public/Rendering/ForwardPass.h, Library/Core/Private/Rendering/ForwardPass.cpp, Assets/Shaders/forward_transparent.vert, Assets/Shaders/forward_transparent.frag, Test/Core/Rendering/ForwardPassPipelinePlacementTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.h, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp, Test/Core/Rendering/RenderingGoldenImageTest.cpp, TASKS.md, PROGRESS.md, LESSONS.md, NEXT_FINDINGS.md, Docs/lessons/*
- notes: `.superpowers/sdd/RenderingR1PhysicalFoundationPlan/task-5-conditional-implementation-plan-v4.md` の技術仕様を全文読む。GPUとobject-presenceは同計画§7の実コマンド・全行判定を追加実行しログを開く。P4のsource commit/report SHAはPROGRESS.mdを使用。P5 scope18とsingle-source shadow追加は2026-08-15の承認済み。source baselineとthresholdは更新しない。後続P6aが読むobject-presence log prefixは同計画とP6a入口に一致させる。
- notes: 全フェーズ統合では、承認済み最終fixtureのIndoor +4 EV/Outdoor main directional 10000 luxをobject-presenceのscenario-local stateへ先行適用する。P5ではBuildSceneLayoutの既定値を保持し、P6aで既定値を統合する。数値scenarioの露出・10行・対象ROI・thresholdは保持。固定物理geometryを実GPU行列規約で表すfixture補正も行う。根拠・完了条件は `Docs/Plans/RenderingR1P5FixtureIntegration20260908.md`。数値背景だけを全画面へ広げ、background-only ROIを `[16,47]²` に固定する不整合修正をメインの実装判断で採用する。根拠・変更前契約は `Docs/Plans/RenderingR1P5BackgroundProposal20260908.md`。追加確認は取り下げ、個別承認eventは作らない。

## R1-P6A: 最終fixtureと全数値キャプチャを統合する
- status: done
- done-when: P5完了後、条件付き計画v4の40 static rows/120 captures、全画素sRGB oracle、forced format行、GPU CTest exact21、3 script self-test、focused7 build/CPU6、Indoor/Outdoor object-presenceを満たす。source20本の変更をコミットし、P6bの基点と証跡を記録する。
- verify: `cmake --build build --config Debug --target ForwardPassPipelinePlacementTest RenderingValidationSceneContractTest RenderingHdrSceneCaptureTest RenderingGoldenImageTest RenderingGoldenImageComparatorTest RenderingPerceptualDiffTest SceneSerializerTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(ForwardPassPipelinePlacementTest|RenderingValidationSceneContractTest|RenderingGoldenImageComparatorTest|RenderingPerceptualDiffTest|RenderingPerceptualArtificialDifferenceTest|SceneSerializerTest)$"`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --self-test-r1-fixture-contract`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=back-buffer --r1-scenario=all-numerical`
- verify: `build\Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe --scene=indoor --capture-source=back-buffer --measure-visual`
- verify: `build\Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe --scene=outdoor --capture-source=back-buffer --measure-visual`
- verify: `pwsh -NoProfile -File Scripts/CalibrateRenderingVisualThresholds.ps1 -SelfTestR1Contract`
- verify: `pwsh -NoProfile -File Scripts/UpdateRenderingGoldenBaselines.ps1 -SelfTestR1Contract`
- verify: `pwsh -NoProfile -File Scripts/TestRenderingGpuCTestContract.ps1 -SelfTestR1Contract`
- verify: `pwsh -NoProfile -File Scripts/TestRenderingGpuCTestContract.ps1 -BuildDirectory build -ExpectedCount 21`
- paths: Library/Core/Public/Rendering/SceneView.h, Library/Core/Private/Rendering/SceneView.cpp, Test/Core/Rendering/ForwardPassPipelinePlacementTest.cpp, Assets/Shaders/mesh3d.vert, Assets/Shaders/mesh3d.frag, Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp, Test/Core/Rendering/RenderingGoldenImageTest.cpp, Test/Core/Rendering/RenderingGoldenImageComparatorTest.cpp, Test/Core/Rendering/RenderingPerceptualDiffTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.h, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingValidation/RenderingGoldenImage.h, Test/Core/Rendering/RenderingValidation/RenderingGoldenImage.cpp, Test/Core/Rendering/RenderingValidation/RenderingPerceptualDiff.h, Test/Core/Rendering/RenderingValidation/RenderingPerceptualDiff.cpp, Test/Core/Rendering/CMakeLists.txt, Scripts/UpdateRenderingGoldenBaselines.ps1, Scripts/CalibrateRenderingVisualThresholds.ps1, Scripts/TestRenderingGpuCTestContract.ps1, Docs/RenderingValidation/GoldenBaselines.md, TASKS.md, PROGRESS.md, LESSONS.md, NEXT_FINDINGS.md, Docs/lessons/*
- notes: `.superpowers/sdd/RenderingR1PhysicalFoundationPlan/task-6a-conditional-implementation-plan-v4.md` の技術仕様を全文読む。plan SHA=B805112EC87BAB673F7D0093FB6BB93DABBACCF5B4682D55992602869D40156F は2026-08-15承認済み。§11のGPU/skip/各SelfTestR1Contractも実行し、各raw outputを開く。P6b floor diagnosisを受け、Indoorだけplane補正を適用するforward-fixを `2a50d4c`→`d4cbe3e` で確定した。P6bの正式候補生成・source publish・full build/full CTestはこのタスクでは行わない。
- verify: `pwsh -NoProfile -File .harness/runs/20260908-084154/p6a-verify-scene-freshness.ps1`
- notes: 独立recheck11件、bundle第2周評価、別枠のR1統合接続評価を完了。詳細と未読範囲はPROGRESS.mdおよびP6a acceptance receipt。追加2 verifyは原計画§9.2/11.4の既存条件。

## R1-P6B: 物理描画の基準画像・閾値と受入記録を確定する
- status: done
- done-when: 承認済みv3のcontrol/identityを固定し、baseline2枚とthreshold60行の候補をそれぞれ実物承認後に公開する。BackBuffer20回は全exit0、full build1回とfull CTest1回は219件中212 PASS/7意図的skip/0 fail、受入れ証跡のblocking指摘を修正し、P6b tracked assetsを確定する。
- paths: Test/Core/Rendering/Baselines/RenderingValidation/Indoor.png, Test/Core/Rendering/Baselines/RenderingValidation/Outdoor.png, Test/Core/Rendering/Baselines/RenderingValidation/VisualThresholds.tsv, Docs/RenderingValidation/R1Acceptance.md
- notes: `.superpowers/sdd/RenderingR1PhysicalFoundationPlan/task-6b-conditional-implementation-plan-v3.md` を適用した。baseline/thresholdを別々の承認event後に公開し、BackBuffer20は20/20 PASS、targetless full buildはexit 0、full CTestは219件中212 passed/7 intentional skipped/0 failed。現行証跡は`Docs/RenderingValidation/R1Acceptance.md`、候補reportはforward-fix名で旧discard reportを上書きしていない。P6b実装・受入れコミットは`eedec7a681489768645eabf77355886ae8e755c0`、Roadmap localもR1完了へ更新済み。

## R2-P1: 空パラメータと太陽方向の参照契約を定義する
- status: done
- done-when: `SkyAtmosphereParameters` がRenderThreadへ安全に渡せる値だけを持ち、太陽高度・方位角から正規化された方向を決定できる。無効な入力を有限な既定値へ正規化し、Hillaire 2020系参照評価の天頂/太陽近傍サンプルと太陽ディスク露出値をCPUテストで固定する。Rendering層からRHI/Vulkanへの依存を増やさない。
- verify: `cmake --build build --config Debug --target SkyAtmosphereModelTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^SkyAtmosphereModelTest$"`
- paths: Library/Core/Public/Rendering/SkyAtmosphere.h, Library/Core/Private/Rendering/SkyAtmosphere.cpp, Library/Core/Public/Rendering/SceneProxy.h, Test/Core/Rendering/SkyAtmosphereModelTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R2-P2: 空LUTと太陽ディスクをLighting前段へ接続する
- status: done
- done-when: 同一の空スナップショットから透過率/空放射輝度を生成し、RenderGraph named resourceとしてLightingPassが読める。空背景は固定HDRへ暗黙に戻らず、空無効時だけ既存フォールバックを使う。太陽ディスクはR1プリエクスポージャを通り、有限性・安全域の飽和契約を満たす。
- verify: `cmake --build build --config Debug --target SkyAtmospherePassContractTest LightingParamsLayoutTest ForwardPassPipelinePlacementTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SkyAtmospherePassContractTest|LightingParamsLayoutTest|ForwardPassPipelinePlacementTest)$"`
- paths: Library/Core/Public/Rendering/SkyAtmospherePass.h, Library/Core/Private/Rendering/SkyAtmospherePass.cpp, Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/Public/Rendering/RenderGraph/RenderGraphResourceNames.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Private/Rendering/LightingPass.cpp, Library/Core/Public/Rendering/LightingPass.h, Library/Core/Private/Rendering/LightingPassGpuTypes.h, Assets/Shaders/sky_atmosphere.frag, Assets/Shaders/lighting.frag, Test/Core/Rendering/SkyAtmospherePassContractTest.cpp, Test/Core/Rendering/LightingParamsLayoutTest.cpp, Test/Core/Rendering/ForwardPassPipelinePlacementTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R2-P3: 動的空を放射輝度IBLとEVレンジへ統合する
- status: done
- done-when: 空のequirectangular放射輝度から既存のDiffuseIrradiance/PrefilteredSpecularを生成し、太陽高度の変更で環境光も更新される。朝/昼/夕のEVケースで空・直接太陽・IBLの出力が有限で、R1の静的HDR経路と無効時フォールバックが回帰しない。
- verify: `cmake --build build --config Debug --target SkyAtmosphereIblTest LightingLightBufferTest RenderingValidationSceneContractTest LightingParamsLayoutTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SkyAtmosphereIblTest|LightingLightBufferTest|RenderingValidationSceneContractTest|LightingParamsLayoutTest)$"`
- paths: Library/Core/Private/Rendering/SkyAtmosphere.cpp, Library/Core/Private/Rendering/SkyAtmospherePass.cpp, Library/Core/Private/Rendering/LightingPass.cpp, Library/Core/Public/Rendering/LightingPass.h, Library/Core/Private/Rendering/LightingPassGpuTypes.h, Library/Core/Public/Rendering/SceneProxy.h, Test/Core/Rendering/SkyAtmosphereIblTest.cpp, Test/Core/Rendering/LightingLightBufferTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R2-P4: CSM分割とテクセル安定化のCPU契約を実装する
- status: done
- done-when: 4カスケードの分割距離、各ライト行列、受影対象の深度範囲、テクセル中心スナップが同じカメラ/方向ライトから決まる。near/far逆転、無効方向、非有限bounds、サブテクセル移動に対して有限な安全結果を返し、境界に隣接するカスケードの範囲が連続する。
- verify: `cmake --build build --config Debug --target CascadedShadowLightMatricesTest DirectionalShadowLightMatricesTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CascadedShadowLightMatricesTest|DirectionalShadowLightMatricesTest)$"`
- paths: Library/Core/Private/Rendering/CascadedShadowLightMatrices.h, Library/Core/Private/Rendering/CascadedShadowLightMatrices.cpp, Library/Core/Private/Rendering/DirectionalShadowLightMatrices.h, Library/Core/Private/Rendering/DirectionalShadowLightMatrices.cpp, Test/Core/Rendering/CascadedShadowLightMatricesTest.cpp, Test/Core/Rendering/DirectionalShadowLightMatricesTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R2-P5: RHIの配列layer attachmentとShadowMapPassのCSM深度記録を実装する
- status: done
- done-when: 4層D32 array textureを作成し、各layerを1層Framebufferへ安全にattachできる。ShadowMapPassは各カスケードを別行列で記録し、RenderGraphの公開リソースは配列深度として一貫する。失敗時は部分リソースを公開せず、既存単一影の所有権/破棄順序を壊さない。
- verify: `cmake --build build --config Debug --target ShadowMapArrayLayerContractTest DirectionalShadowPassWiringContractTest RenderGraphTextureUsageContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(ShadowMapArrayLayerContractTest|DirectionalShadowPassWiringContractTest|RenderGraphTextureUsageContractTest)$"`
- paths: Library/Core/Public/RHI/IFramebuffer.h, Library/Core/Public/RHI/IDevice.h, Library/Core/Public/RHI/IGPUResourceAllocator.h, Library/Core/Private/RHI/Vulkan/VulkanTexture.h, Library/Core/Private/RHI/Vulkan/VulkanTexture.cpp, Library/Core/Private/RHI/Vulkan/VulkanFramebuffer.h, Library/Core/Private/RHI/Vulkan/VulkanFramebuffer.cpp, Library/Core/Public/Rendering/ShadowMapPass.h, Library/Core/Private/Rendering/ShadowMapPass.cpp, Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/CMakeLists.txt, Test/Core/Rendering/ShadowMapArrayLayerContractTest.cpp, Test/Core/Rendering/DirectionalShadowPassWiringContractTest.cpp, Test/Core/Rendering/RenderGraphTextureUsageContractTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R2-P6: Lighting/ForwardのCSMサンプリングへ移行する
- status: done
- done-when: GPU lighting params、descriptor、lighting/forward shaderが4行列・分割距離・sampler2DArrayを同じlayoutで使用する。カスケード境界にブレンドを適用し、影なし/単一層/不完全公開値では安全な既定へ戻る。既存R1の単一方向影契約と透明描画契約が維持される。
- verify: `cmake --build build --config Debug --target LightingParamsLayoutTest LightingLightBufferTest ForwardPassPipelinePlacementTest DirectionalShadowPassWiringContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingParamsLayoutTest|LightingLightBufferTest|ForwardPassPipelinePlacementTest|DirectionalShadowPassWiringContractTest)$"`
- paths: Library/Core/Private/Rendering/LightingPassGpuTypes.h, Library/Core/Private/Rendering/LightingPass.cpp, Library/Core/Public/Rendering/LightingPass.h, Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/Private/Rendering/ForwardPass.cpp, Library/Core/Public/Rendering/ForwardPass.h, Assets/Shaders/lighting.frag, Assets/Shaders/forward_transparent.vert, Assets/Shaders/forward_transparent.frag, Test/Core/Rendering/LightingParamsLayoutTest.cpp, Test/Core/Rendering/LightingLightBufferTest.cpp, Test/Core/Rendering/ForwardPassPipelinePlacementTest.cpp, Test/Core/Rendering/DirectionalShadowPassWiringContractTest.cpp, TASKS.md, PROGRESS.md

## R2-P7: 空・太陽・CSMの数値/画像受入れを固定する
- status: done
- done-when: 朝/昼/夕のgolden、天頂/太陽近傍float readback、太陽ディスク有限性、カスケード境界の欠落/二重化、サブテクセル移動の影エッジ変化率を実データで検証する。既存Indoor/Outdoor R1 baselineは上書きせず、R2用シナリオと証拠を分離する。
- verify: `cmake --build build --config Debug --target RenderingHdrSceneCaptureTest RenderingGoldenImageTest RenderingGoldenImageComparatorTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingHdrSceneCaptureTest|RenderingGoldenImageComparatorTest|RenderingPerceptualDiffTest)$"`
- verify: `build\\Test\\Core\\Rendering\\Debug\\RenderingHdrSceneCaptureTest.exe --self-test-r2-sky-csm-contract`
- verify: `build\\Test\\Core\\Rendering\\Debug\\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r2-scenario=sky-time-sweep`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingParamsLayoutTest|LightingLightBufferTest|ForwardPassPipelinePlacementTest|DirectionalShadowPassWiringContractTest|SkyAtmospherePassContractTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RHIImageLayoutVulkanValidationTest|RHIImageLayoutVulkanDrawSceneTest|RHIImageLayoutVulkanDrawThenNoCasterSceneTest|RenderingHdrOutdoorSceneVulkanTest)$"`
- paths: Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp, Test/Core/Rendering/RenderingGoldenImageTest.cpp, Test/Core/Rendering/RenderingGoldenImageComparatorTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingValidation/RenderingGoldenImage.cpp, Test/Core/Rendering/Baselines/RenderingValidation/R2*, Test/Core/Rendering/Thresholds/RenderingValidation/R2*, Docs/RenderingValidation/R2Acceptance.md, Scripts/CalibrateRenderingVisualThresholds.ps1, Scripts/UpdateRenderingGoldenBaselines.ps1, TASKS.md, PROGRESS.md

## R2-P8: R2受入れ記録と後続計画の入口を確定する
- status: done
- done-when: R2の実装コミット、検証ログ、golden/threshold、既知の非対象を一つの受入れ記録へまとめ、RoadmapのR2行を完了へ更新する。R1証跡と候補を再利用せず、R3は未着手の新規M1として残す。
- verify: `git diff --check`
- verify: `git status --short --branch`
- verify: `git log --oneline -10`
- paths: Docs/RenderingValidation/R2Acceptance.md, Docs/Plans/RenderingRoadmap.md, TASKS.md, PROGRESS.md, LESSONS.md, NEXT_FINDINGS.md
# R3: ボリュメトリクス

### 採用する設計

- 既存のSceneColor/SceneDepthを使う全画面 `VolumetricsPass` とし、3D froxel texture・compute pass・Vulkan/RHI実装は追加しない。
- 高さ密度は指数関数モデル、視線方向のBeer-Lambert透過率は解析積分する。方向光の単一散乱だけを固定24ステップで積分し、既存の4カスケードCSMで遮蔽する。
- passはLighting後・Forward透明描画前に置く。R2 `SkyAtmosphere.Radiance` を遠景の散乱先へ連続的にブレンドし、透明物自体への距離フォグ適用はR3の対象外とする。
- GPU性能ゲートはRoadmapどおり延期する。R1 baseline/thresholdとR2証跡は変更・再利用しない。

## R3-P1: 高さフォグの解析モデルとスナップショット契約を実装する
- status: done
- done-when: 有限な既定値と不正入力の安全な正規化を備えた高さフォグパラメータをSceneProxy/FramePacket経由でRenderThreadへ渡し、指数密度に対する水平・上昇・下降レイの解析透過率をCPUテストで固定する。Rendering層にRHI/Vulkan依存を追加しない。
- verify: `cmake --build build --config Debug --target VolumetricFogModelTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^VolumetricFogModelTest$"`
- paths: Library/Core/Public/Rendering/VolumetricFog.h, Library/Core/Private/Rendering/VolumetricFog.cpp, Library/Core/Public/Rendering/SceneProxy.h, Test/Core/Rendering/VolumetricFogModelTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R3-P2: 解析フォグとR2遠景空を描画パスへ接続する
- status: done
- done-when: `VolumetricsPass` がSceneColor/SceneDepthを読み、解析透過率で不透明描画を合成し、遠距離ではR2 SkyAtmosphere radianceへ連続的に収束する。Lighting後・Forward透明前の配置、fog無効時の恒等動作、named resource/descriptor境界を契約テストで固定する。新しい3D/RHI資源を作らない。
- verify: `cmake --build build --config Debug --target VolumetricsPassContractTest ForwardPassPipelinePlacementTest RenderingHdrSceneCaptureTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VolumetricsPassContractTest|ForwardPassPipelinePlacementTest)$"`
- paths: Library/Core/Public/Rendering/VolumetricsPass.h, Library/Core/Private/Rendering/VolumetricsPass.cpp, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Public/Rendering/RenderGraph/RenderGraphResourceNames.h, Assets/Shaders/volumetrics.frag, Test/Core/Rendering/VolumetricsPassContractTest.cpp, Test/Core/Rendering/ForwardPassPipelinePlacementTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R3-P3: 方向光の単一散乱をCSM遮蔽へ接続する
- status: done
- done-when: RenderWorldから設定した高さフォグがCoordinator経由でFramePacket.Sceneへ値コピーされ、固定24ステップの方向光散乱が既存の4カスケード深度配列を安全に参照する。遮蔽物の背後ではshaftが抑制され、非遮蔽領域ではライト方向に沿って現れる。影なし/不完全なCSM資源では既存のLightingPass fallbackを使って散乱だけを安全に抑制し、解析高さフォグとR1/R2の描画契約を保つ。
- verify: `cmake --build build --config Debug --target VolumetricsPassContractTest LightingParamsLayoutTest DirectionalShadowPassWiringContractTest RenderingHdrSceneCaptureTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VolumetricsPassContractTest|LightingParamsLayoutTest|DirectionalShadowPassWiringContractTest)$"`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=shadowed-shafts`
- paths: Library/Core/Public/Rendering/VolumetricsPass.h, Library/Core/Private/Rendering/VolumetricsPass.cpp, Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/Public/Rendering/RenderWorld.h, Library/Core/Private/Rendering/RenderWorld.cpp, Library/Core/Public/Rendering/RenderingCoordinator.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Private/Rendering/LightingPass.cpp, Assets/Shaders/volumetrics.frag, Test/Core/Rendering/VolumetricsPassContractTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.h, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationApplication.cpp, Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md, blocked/R3-P3.md
- notes: FramePacket境界を守り、GameThreadの高さフォグ設定は既存SetSkyAtmosphereと同じRenderWorld→RenderingCoordinator経路で値コピーする。既定の無効状態は維持し、検証fixtureから明示的に有効化する。CSM不在時も既存のLightingPass fallback配列をdescriptorへ渡し、散乱だけを無効化して解析高さフォグを維持する。停止記録`blocked/R3-P3.md`は解消済みで、最終検証ログは`.harness/runs/20260920-r3-p3-final/`に保存する。

## R3-P4: フォグ密度・影・遠景空のGPU受入れを固定する
- status: done
- done-when: 3段階密度のGPU golden sweep、CSM遮蔽物あり/なしのA/B、遠方の空/地平線とR2大気のブレンドを専用R3証拠で検証する。R1/R2 baselineは不変とし、GPU性能は未計測・後続ゲートとして明記する。
- verify: `cmake --build build --config Debug --target RenderingHdrSceneCaptureTest RenderingGoldenImageTest RenderingGoldenImageComparatorTest -- /m:1`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=density-sweep`
- verify: `build\Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=density-sweep`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=shadowed-shafts`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=occluder-ab`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=distant-atmosphere-blend`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SkyAtmosphereModelTest|SkyAtmospherePassContractTest|LightingParamsLayoutTest|ForwardPassPipelinePlacementTest|VolumetricFogModelTest|VolumetricsPassContractTest|RenderingGoldenImageComparatorTest)$"`
- paths: Test/Core/Rendering/RenderingValidation/RenderingValidationScene.h, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp, Test/Core/Rendering/RenderingGoldenImageTest.cpp, Test/Core/Rendering/Baselines/RenderingValidation/R3*, Test/Core/Rendering/Thresholds/RenderingValidation/R3*, Docs/RenderingValidation/R3Acceptance.md, TASKS.md, PROGRESS.md
- notes: P3評価の非blocking指摘は、R3-P4で非遮蔽散乱上限と飽和画素0をR3専用閾値へ追加して解消した。独立評価はPASS。

# R4: プローブGI

### 採用する設計

- S4の選定はDDGI。手動配置の有限な3Dプローブ格子を使い、既定は無効、RT対応Vulkanでのみ更新する。RT非対応・無効・資源作成失敗時は既存のラスタ/IBL描画を維持する。
- RenderWorldからの設定はSceneProxy/FramePacketへ値コピーし、RenderThreadはそのフレームの不変スナップショットとR5のTLASだけを読む。プローブ資源と履歴はLightingPassがRenderThread上で所有する。
- 1 volume最大1024 probes、probeあたり64本の準一様rayを毎フレーム更新する。octahedral irradiance/distanceをprobeごとの8x8 array layerへ格納し、1 texel境界、scene-linear RGBA16F irradiance、RG16F距離1次/2次モーメントを使う。
- Probe hitはlinear BaseColorとemissive、既存LightProxyのdirectional/point/spot照明、直前のDDGI irradianceを使って拡散radianceを求める。missは有効な空/環境radianceを使い、無ければ黒とする。反射・透過・鏡面GI・probe relocation/classification/自動配置は対象外。
- Atlasはscene-referred linearで保存し、LightingPassの最終合成で現在フレームのpre-exposureを一度だけ適用する。Volume内部では既存diffuse IBLをDDGIで置換し二重加算を防ぐ。Volume外またはDDGI無効時は既存diffuse IBLを使う。
- 静的受入れは[Cornell大学Computer Graphicsの公開geometry/reflectanceと合成RGBE](https://bowers.cornell.edu/computer-graphics/data)を参照する。直接照明の白ROIから露出scaleを一度だけ決め、shadow-floor/red-bounce/green-bounceの平均Y相対誤差を各25%以内、red/green bounce ROIの優勢chroma比差を各0.10以内とする。R1/R2/R3のbaselineは変更しない。
- 動的受入れはライトまたは不透明物体を動かしてprobe更新を確認する。変更後8フレーム以内に間接光ROIの最終変化量の80%以上へ単調に収束する。GPU性能gateはRoadmapどおりDeferred。
- R5-P5の独立評価はPASSで完了した。R4はR5の既存GPU hit/miss経路を最初のray-query検証で再確認し、P5の受入れ完了を前提にDDGIへ接続する。

## R4-P1: DDGI volume設定と格子/方向変換のCPU契約を作る
- status: done
- done-when: DDGI volumeの有限値検証、1..1024 probeのchecked grid indexing、octahedral direction mapping、ray方向列と無効入力fallbackをCPU契約テストで固定する。既定volumeは無効である。
- verify: `cmake --build build --config Debug --target DDGIVolumeModelTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^DDGIVolumeModelTest$"`
- paths: Library/Core/Public/Rendering/DDGIVolume.h, Library/Core/Private/Rendering/DDGIVolume.cpp, Test/Core/Rendering/DDGIVolumeModelTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R4-P2: volume設定とray-hit材質をFramePacketへ値スナップショットする
- status: done
- done-when: RenderWorldで設定したvolumeとlinear BaseColor/emissiveがSceneProxy/FramePacketへ値コピーされ、packet clear後に参照が残らない。BaseColor既定値1は既存の描画を変えず、RHI未対応時の設定も無効化される。
- verify: `cmake --build build --config Debug --target DDGISnapshotContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^DDGISnapshotContractTest$"`
- paths: Library/Core/Public/Rendering/DDGIVolume.h, Library/Core/Public/Rendering/SceneProxy.h, Library/Core/Public/Rendering/FramePacket.h, Library/Core/Public/Rendering/RenderWorld.h, Library/Core/Private/Rendering/RenderWorld.cpp, Library/Core/Public/Rendering/RenderingCoordinator.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/MaterialTypes.h, Library/Core/Private/Rendering/RenderMaterialStore.h, Library/Core/Private/Rendering/RenderMaterialStore.cpp, Library/Core/Public/Rendering/RayTracingSceneSubsystem.h, Test/Core/Rendering/DDGISnapshotContractTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: GameThreadからRenderThreadへのライブ参照は禁止し、加速構造・材質値・volume設定をFramePacketの値所有データで渡す。危険地帯の独立評価対象。

## R4-P3: compute ray queryでprobe rayのhit属性を取得する
- status: done
- done-when: LightingPassが記録する同一command listのTLAS build後にCompute shaderのray queryをdispatchし、FramePacketのTLASに対する既知のhit/miss、距離、instance/primitive属性を返す。frame slot再利用と欠落dispatchの番兵値readbackを検証する。RT無効時とDDGI資源作成例外時はdispatchを停止し、既存描画と同一SceneColorを維持する。
- verify: `cmake --build build --config Debug --target Game DDGIProbeRayQueryVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^DDGIProbeRayQueryVulkanTest$"`
- paths: Library/Core/Public/Rendering/DDGIProbePass.h, Library/Core/Private/Rendering/DDGIProbePass.cpp, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Private/Rendering/LightingPass.cpp, Assets/Shaders/DDGI/ProbeRayQuery.comp, Test/Core/Rendering/DDGIProbeRayQueryVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: descriptorはRHI abstractionのCompute stageで宣言し、RenderingからRHI/Vulkanをincludeしない。Vulkan同期・GPU resource寿命・FramePacket境界は独立評価対象。

## R4-P3A: ray hitの拡散radianceを材質と直接光から計算する
- status: done
- done-when: ray hitの三角形normalとFramePacketのBaseColor/emissiveを使い、既存directional/point/spot lightの遮蔽付きLambert radianceをscene-linear ray結果へ保存する。missは設定済み空/環境radianceを使う。probe間接光の再帰寄与は次タスクで加える。
- verify: `cmake --build build --config Debug --target DDGIProbeRadianceVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^DDGIProbeRadianceVulkanTest$"`
- paths: Library/Core/Public/Rendering/DDGIProbePass.h, Library/Core/Private/Rendering/DDGIProbePass.cpp, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Assets/Shaders/DDGI/ProbeRadiance.comp, Test/Core/Rendering/DDGIProbeRadianceVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: GPU geometry/material寿命、direct-light単位、RT query同期は危険地帯の独立評価対象。

## R4-P4: ray結果をirradiance/distance atlasへ積分して更新する
- status: done
- done-when: probe ray hitで直前フレームのDDGI irradianceを1段だけ加算し、radiance ray結果をoctahedral irradiance/distance atlasへvisibility重み付きで積分する。1 texel borderとhysteresis 0.8を適用し、既知の単一平面/遮蔽ケースのGPU readbackで値・距離モーメント・境界を固定する。
- verify: `cmake --build build --config Debug --target DDGIProbeUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^DDGIProbeUpdateVulkanTest$"`
- paths: Library/Core/Public/Rendering/DDGIProbePass.h, Library/Core/Private/Rendering/DDGIProbePass.cpp, Assets/Shaders/DDGI/ProbeIrradianceUpdate.comp, Test/Core/Rendering/DDGIProbeUpdateVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R4-P5: DDGI irradianceをLightingPassへ接続する
- status: done
- done-when: GBuffer world position/normalを使ってprobeをvisibility-weightedに補間し、active volume内でdiffuse IBLを置換して間接拡散項を加える。volume外・無効・RT非対応・不完全resourceでは既存diffuse IBLと同じ出力契約を保つ。
- verify: `cmake --build build --config Debug --target LightingParamsLayoutTest RenderingDDGILightingContractTest RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingParamsLayoutTest|RenderingDDGILightingContractTest|RenderGraphCompileTest)$"`
- paths: Library/Core/Private/Rendering/DDGIProbePass.cpp, Library/Core/Public/Rendering/LightingPass.h, Library/Core/Private/Rendering/LightingPass.cpp, Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/Private/Rendering/LightingPassGpuTypes.h, Assets/Shaders/lighting.frag, Test/Core/Rendering/LightingParamsLayoutTest.cpp, Test/Core/Rendering/RenderingDDGILightingContractTest.cpp, Test/Core/Rendering/RenderGraphCompileTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: direct lightingと現在のpre-exposureは変更せず、DDGI有効/無効、volume内/外、sky IBL二重加算を契約テストする。DDGIProbePassが無効化・失敗フレームで前frame-slotのatlas有効状態を残さないことも確認してからLightingPassへ接続する。危険地帯の独立評価対象。

## R4-P6: Cornell参照と動的更新を実GPUで受け入れる
- status: done
- done-when: 公開Cornell RGBEとgeometry/reflectanceを使うHDR captureで3つの間接ROIの平均Y相対誤差が25%以内、red/green bounceのchroma比差が0.10以内になる。ライト/occluder移動後は8 frame以内に最終間接ROI変化の80%以上へ単調に収束し、DDGI無効時は既存描画の比較閾値内で一致する。
- verify: `cmake --build build --config Debug --target RenderingDDGIVulkanTest RenderingGoldenImageComparatorTest -- /m:1`
- verify: `build\Test\Core\Rendering\Debug\RenderingDDGIVulkanTest.exe --capture-source=scene-color --scenario=cornell-reference`
- verify: `build\Test\Core\Rendering\Debug\RenderingDDGIVulkanTest.exe --capture-source=scene-color --scenario=dynamic-update`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingDDGIVulkanTest|RenderingGoldenImageComparatorTest)$"`
- paths: Test/Core/Rendering/RenderingValidation/RenderingValidationScene.h, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingDDGIVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Test/Core/Rendering/Baselines/RenderingValidation/R4*, Test/Core/Rendering/Thresholds/RenderingValidation/R4*, TASKS.md, PROGRESS.md
- notes: 20260922-r4-p7-final3でHDR scene-color captureを再検証した。Cornellのshadow/red/green ROI相対誤差は0.147881/0.152345/0.106696、red/green chroma差は0.0194377/0.0229986、DDGI有効A/Bのmean/max deltaは0.168949/6.5625、無効A/Bはmean/max delta=0/0、VUID_COUNT=0である。dynamic-updateは4 warmup sample後のred/green ROI平均を使い、FrameNumber差8時点のprogress=0.971325/0.820146で単調収束した。Cornell quadは従来どおり室内向きのvertex normalを保持し、GBufferのFrontFace::Clockwiseに合わせたラスタ巻き順をfixture内で明示した。P4期待値を現行のwrap重み床とhysteresisへ再基準化し、閾値・完了条件は変更していない。

## R4-P7: R4受入れ記録と独立評価を確定する
- status: done
- done-when: R4Acceptanceへ選定、scope、Cornell参照URL/asset hash/ROI閾値、動的更新結果、対象build/CTest/GPU captureの読戻しログ、既知の制限、性能gate保留を記録する。FramePacket/RHI/Vulkan/Rendering境界の独立評価blocking指摘を処理し、未処理はNEXT_FINDINGS.mdに残す。
- verify: `git diff --check`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(DDGIVolumeModelTest|DDGISnapshotContractTest|DDGIProbeRayQueryVulkanTest|DDGIProbeUpdateVulkanTest|LightingParamsLayoutTest|RenderingDDGILightingContractTest|RenderingDDGIVulkanTest|RenderingDDGIVulkanDynamicTest|RenderingGoldenImageComparatorTest)$"`
- paths: Docs/RenderingValidation/R4Acceptance.md, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md
- notes: 20260922-r4-p7-final3でR4Acceptance、RGBE/threshold asset、実GPUログを確定した。対象Debug build、Cornell static/dynamic capture、R4指定9件CTestを再実行し、9/9 passed、VUID_COUNT=0を確認した。独立評価2周目で指摘された帳簿・行末・成果物追跡の不整合を修正し、最終成果物へ反映した。性能gateはDeferred、P4由来のvalidation陽性対照とhalf範囲上限はNEXT_FINDINGS.mdへ非blockingとして残す。

# R5: ハードウェアレイトレーシング基盤

### R5の選定と実行順

- R4のS4はDDGIとし、依存するR5を先行する。probe更新はcompute shaderのray queryを使い、R5初弾の不透明ハードシャドウはRT pipelineで実証する。
- BLAS/TLASの所有・更新は`GEngine`が所有するRayTracingSceneSubsystemへ集約する。RenderThreadはFramePacketの不変スナップショットだけを読む。
- RT非対応または無効時は既存ラスタ経路を維持する。R5はRT影までを対象とし、GI本実装とGPU性能ゲートは後続へ分離する。
- 方式の技術背景: [NVIDIA DDGI Integration Guide](https://developer.nvidia.com/blog/an-engineers-guide-to-integrating-ddgi/)

## R5-P1: Buffer Device Addressを独立して有効化する
- status: done
- done-when: RHIバッファでBDAを明示要求でき、対応デバイスでは要求したバッファだけが非ゼロのdevice addressを返す。未要求・非対応時は0を維持する。BDA/RHIのGPU検証と独立評価が通り、全体ビルド・全CTestの結果とR5-P1対象外の失敗がPROGRESS.mdに記録されている。
- verify: `cmake --build build --config Debug --target Game RHIBufferDeviceAddressVulkanTest -- /m:1`
- verify: `cmake --build build --config Debug --target RHIGPUTimestampVulkanTest RHIBufferDeviceAddressVulkanTest RHIAccelerationStructureVulkanTest RayTracingSceneSnapshotTest RHIRayTracingPipelineVulkanTest RayTracingCapabilityContractTest RHITextureToBufferReadbackVulkanTest RenderingValidationFixtureVulkanTest RHIImageLayoutVulkanValidationTest FrameCaptureFloatReadbackVulkanTest RenderingHdrSceneCaptureTest RenderingRayTracingShadowVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIBufferDeviceAddressVulkanTest$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error`
- paths: Library/Core/Public/RHI/RHITypes.h, Library/Core/Public/RHI/IBuffer.h, Library/Core/Public/RHI/DeviceCapabilities.h, Library/Core/Private/RHI/Vulkan/VulkanBuffer.*, Library/Core/Private/RHI/Vulkan/VulkanDevice.*, Test/Core/Rendering/RHIBufferDeviceAddressVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R5-P2: RT機能を任意機能として検出する
- status: done
- done-when: acceleration structure、ray query、RT pipelineの拡張とfeature chainを個別に照会し、利用可能な組合せだけを論理デバイスで有効化する。未対応・無効時はデバイス初期化を維持し、既存ラスタ描画へ戻る。
- verify: `cmake --build build --config Debug --target Game RayTracingCapabilityContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RayTracingCapabilityContractTest$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RHIImageLayoutVulkanValidationTest|RenderingHdrIndoorSceneVulkanTest)$"`
- paths: Library/Core/Public/RHI/DeviceCapabilities.h, Library/Core/Private/RHI/Vulkan/VulkanDevice.h, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Test/Core/Rendering/RayTracingCapabilityContractTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R5-P3: RT shader stageをshadercへ接続する
- status: done
- done-when: RayGen/Miss/ClosestHit/AnyHit/Intersection/Callableの各stageがRHIからshadercへ一意に写像され、最小shader fixtureのコンパイルが全stageで成功する。
- verify: `cmake --build build --config Debug --target RHIRayTracingShaderStageCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIRayTracingShaderStageCompileTest$"`
- paths: Library/Core/Public/RHI/RHITypes.h, Library/Core/Public/RHI/IShaderCompiler.h, Library/Core/Private/RHI/Vulkan/VulkanShaderCompiler.cpp, Test/Core/Rendering/RHIRayTracingShaderStageCompileTest.cpp, Test/Core/Rendering/CMakeLists.txt, Assets/Shaders/RayTracing/*, TASKS.md, PROGRESS.md

## R5-P4: 加速構造のRHI契約を定義する
- status: done
- done-when: BLAS/TLAS geometry・instance・build/update descriptorsと加速構造resourceがbackend-neutral APIで表現され、無効入力とRT非対応時の戻り値契約がテストで固定される。
- verify: `cmake --build build --config Debug --target RHIRayTracingApiContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIRayTracingApiContractTest$"`
- paths: Library/Core/Public/RHI/RHITypes.h, Library/Core/Public/RHI/IDevice.h, Library/Core/Public/RHI/ICommandList.h, Library/Core/Public/RHI/IAccelerationStructure.h, Library/Core/Private/RHI/Vulkan/VulkanDevice.h, Library/Core/Private/RHI/Vulkan/VulkanCommandList.h, Test/Core/Rendering/RHIRayTracingApiContractTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R5-P5: Vulkan BLAS構築を実装する
- status: done
- done-when: BDA対応vertex/index bufferから不透明triangle BLASを構築し、GPU queryで既知の交差/非交差を判定できる。RT無効時はresourceを公開せず、従来のmesh描画を維持する。
- verify: `cmake --build build --config Debug --target RHIAccelerationStructureVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIAccelerationStructureVulkanTest$"`
- paths: Library/Core/Private/RHI/Vulkan/VulkanBuffer.*, Library/Core/Private/RHI/Vulkan/VulkanDevice.*, Library/Core/Private/RHI/Vulkan/VulkanAccelerationStructure.*, Library/Core/Public/RHI/IAccelerationStructure.h, Test/Core/Rendering/RHIAccelerationStructureVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 20260922-r5-p5-resumeでDebug build exit 0、専用CTest 1/1 passed、直接GPU実行のBLAS/TLAS hit/missとresource寿命を確認した。独立評価はPASS。容量再問い合わせ、build-input usageの追加検証、行末整理はNEXT_FINDINGS.mdへnon-blockingとして残す。

## R5-P6: TLASのinstance build/updateを実装する
- status: done
- done-when: BLAS instance配列からTLASを構築し、既知transformの変更をupdateまたはrebuildで反映する。2フレーム間のGPU query結果が移動後の解析位置と一致し、build/update間のbarrierと寿命が検証される。
- verify: `cmake --build build --config Debug --target RHIAccelerationStructureVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIAccelerationStructureVulkanTest$"`
- paths: Library/Core/Public/RHI/IAccelerationStructure.h, Library/Core/Public/RHI/ICommandList.h, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Library/Core/Private/RHI/Vulkan/VulkanAccelerationStructure.*, Library/Core/Private/RHI/Vulkan/VulkanCommandList.*, Test/Core/Rendering/RHIAccelerationStructureVulkanTest.cpp, TASKS.md, PROGRESS.md

- notes: Updateの適合性は容量だけでは判定できない。TLASはsourceの直近Buildで使った実instance数を記録し、Update入力との一致を検証する。

## R5-P7: RT pipelineとShader Binding Tableを実装する
- status: done
- done-when: RayTracing pipeline descriptorがshader groupsを記述し、Vulkan backendがraygen/miss/closest-hit groupとShader Binding Tableを生成する。最小pipelineを作成し、無効なgroup構成を拒否する。
- verify: `cmake --build build --config Debug --target RHIRayTracingPipelineVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIRayTracingPipelineVulkanTest$"`
- paths: Library/Core/Public/RHI/RHITypes.h, Library/Core/Public/RHI/IPipeline.h, Library/Core/Public/RHI/IDevice.h, Library/Core/Private/RHI/Vulkan/VulkanPipeline.*, Library/Core/Private/RHI/Vulkan/VulkanDevice.*, Library/Core/Private/RHI/Vulkan/VulkanRayTracingPipeline.*, Library/Core/Private/RHI/Vulkan/VulkanDescriptorSet.*, Test/Core/Rendering/RHIRayTracingPipelineVulkanTest.cpp, blocked/R5-P7.md, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R5-P8: TraceRaysをコマンドリストへ接続する
- status: done
- done-when: RHI ICommandListのTraceRaysがVulkanの各軸上限と総呼出し数上限以内のdispatchだけを記録し、同一deviceのTLAS descriptor binding/updateと最小raygen/miss/hit shaderを通じて既知triangleのhit/missをGPU readbackできる。readback両要素は未書込み番兵値から期待値へ更新される。descriptor bindingはnull・BLAS・foreign-device resource・別descriptor種を拒否し、回帰テストで固定する。
- verify: `cmake --build build --config Debug --target RHIRayTracingPipelineVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIRayTracingPipelineVulkanTest$"`
- paths: Library/Core/Public/RHI/RHITypes.h, Library/Core/Public/RHI/IDescriptorSet.h, Library/Core/Public/RHI/ICommandList.h, Library/Core/Private/RHI/Vulkan/VulkanAccelerationStructure.h, Library/Core/Private/RHI/Vulkan/VulkanDescriptorSet.h, Library/Core/Private/RHI/Vulkan/VulkanDescriptorSet.cpp, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Library/Core/Private/RHI/Vulkan/VulkanPipeline.cpp, Library/Core/Private/RHI/Vulkan/VulkanCommandList.h, Library/Core/Private/RHI/Vulkan/VulkanCommandList.cpp, Library/Core/Private/RHI/Vulkan/VulkanRayTracingPipeline.h, Assets/Shaders/RayTracing/RayTracingVisibilityRayGen.glsl, Assets/Shaders/RayTracing/RayTracingVisibilityMiss.glsl, Assets/Shaders/RayTracing/RayTracingVisibilityClosestHit.glsl, Test/Core/Rendering/RHIRayTracingPipelineVulkanTest.cpp, blocked/R5-P8.md, TASKS.md, PROGRESS.md

## R5-P9: GEngine所有のray-tracing sceneをFramePacketへ接続する
- status: done
- done-when: GEngine所有のRayTracingSceneSubsystemがdraw snapshot由来のmesh geometryとinstance transformからBLAS/TLASを管理し、RenderThreadはFramePacket以外のWorld/SceneViewを参照しない。生成・更新・破棄の寿命が契約テストを通る。
- verify: `cmake --build build --config Debug --target RayTracingSceneSnapshotTest RHIAccelerationStructureVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RayTracingSceneSnapshotTest|RHIAccelerationStructureVulkanTest)$"`
- paths: Library/Core/Public/Engine/NorvesEngine.h, Library/Core/Private/Engine/NorvesEngine.cpp, Library/Core/Public/Rendering/FramePacket.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/RenderingCoordinator.h, Library/Core/Public/Rendering/RayTracingSceneSubsystem.h, Test/Core/Rendering/RayTracingSceneSnapshotTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R5-P10: RT pipelineでハードシャドウを描画する
- status: done
- done-when: opaque fixtureでRT visibilityがLightingへ接続され、PCF/PCSSを無効にしたraster hard shadowとのA/BがR5専用閾値内になる。RT無効時は同一fixtureがraster結果を維持する。
- verify: `cmake --build build --config Debug --target RenderingRayTracingShadowVulkanTest RenderingHdrSceneCaptureTest -- /m:1`
- verify: `cmake --build build --config Debug --target RHIGPUTimestampVulkanTest RHIBufferDeviceAddressVulkanTest RHIAccelerationStructureVulkanTest RayTracingSceneSnapshotTest RHIRayTracingPipelineVulkanTest RayTracingCapabilityContractTest RHITextureToBufferReadbackVulkanTest RenderingValidationFixtureVulkanTest RHIImageLayoutVulkanValidationTest FrameCaptureFloatReadbackVulkanTest RenderingHdrSceneCaptureTest RenderingRayTracingShadowVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderingRayTracingShadowVulkanTest$"`
- verify: `build\Test\Core\Rendering\Debug\RenderingRayTracingShadowVulkanTest.exe --capture-source=back-buffer --scenario=raster-rt-shadow-ab`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation --timeout 180`
- paths: Library/Core/Private/Rendering/LightingPass.cpp, Library/Core/Public/Rendering/LightingPass.h, Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Private/Rendering/RayTracingShadowPass.cpp, Library/Core/Public/Rendering/RayTracingShadowPass.h, Library/Core/CMakeLists.txt, Library/Core/Public/RHI/RHITypes.h, Library/Core/Private/RHI/Vulkan/VulkanCommandList.cpp, Library/Core/Private/Rendering/LightingPassGpuTypes.h, Assets/Shaders/lighting.frag, Assets/Shaders/RayTracing/*, Test/Core/Rendering/RenderingRayTracingShadowVulkanTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md
- notes: 通常描画は既存のラスタ影を既定とし、RT影は明示設定またはR5検証モードで有効にする。単一visibility textureの適用対象は有効な方向光が一灯の構成に限る。

## R5-P11: 動的TLAS更新とRT無効fallbackをGPU受入れする
- status: done
- done-when: opaque occluder移動の複数フレームcaptureでTLAS更新後の影位置が解析範囲へ移り、移動後のRT影とraster影が比較可能な範囲で一致し、RT無効化時はraster shadowへ復帰する。R5 shaderのプリエクスポージャmodeをLightingParamsLayoutTestが期待値で検証し、全CTestに新規回帰がない。
- verify: `cmake --build build --config Debug --target LightingParamsLayoutTest RenderingRayTracingShadowVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingParamsLayoutTest|RenderingRayTracingShadowVulkanTest)$"`
- verify: `cmake --build build --config Debug --target RenderingRayTracingShadowVulkanTest -- /m:1`
- verify: `build\Test\Core\Rendering\Debug\RenderingRayTracingShadowVulkanTest.exe --capture-source=back-buffer --scenario=dynamic-occluder-fallback`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error`
- paths: Library/Core/Private/Rendering/RayTracingShadowPass.cpp, Library/Core/Public/Rendering/RayTracingSceneSubsystem.h, Test/Core/Rendering/RenderingRayTracingShadowVulkanTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.*, Test/Core/Rendering/LightingParamsLayoutTest.cpp, Assets/Shaders/RayTracing/RayTracingShadowRayGen.glsl, Assets/Shaders/RayTracing/RayTracingShadowMiss.glsl, Assets/Shaders/RayTracing/RayTracingShadowClosestHit.glsl, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md, blocked/R5-P11.md

## R5-P12: R5の受入れ記録を確定する
- status: done
- done-when: R5の設計選定、RHI/Vulkan/API変更、RT影A/B、動的TLAS、非対応fallback、検証ログ、性能gate保留をR5Acceptanceへ集約し、R4/DDGIを後続として記録する。
- verify: `git diff --check`
- verify: `git status --short --branch`
- verify: `git log --oneline -10`
- paths: Docs/RenderingValidation/R5Acceptance.md, TASKS.md, PROGRESS.md

## R5-P13: VulkanTexture::Updateの同期失敗時staging資源を解放する
- status: done
- done-when: EndSingleTimeCommandsの終了・送信・待機失敗時もstaging bufferと転送先texture資源をGPU完了まで保持し、device teardownの非device-lost待機失敗ではallocator・command pool・device・instanceを破棄しない。失敗経路・teardown保護・Validation error検出と正常なtexture updateを検証する。
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHITextureUpdateVulkanTest$"`
- verify: `build\Test\Core\Rendering\Debug\RHITextureUpdateVulkanTest.exe`
- paths: Library/Core/Private/RHI/Vulkan/VulkanTexture.cpp, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Library/Core/Private/RHI/Vulkan/VulkanDevice.h, Test/Core/Rendering/RHITextureUpdateVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: VulkanDeviceのWaitIdleと共有command poolを使う単発コマンドは、同一device上で呼出側が直列化する。並行更新の内部同期は本タスクの対象外。

## SCENE-P1: 起動時の既定シーン経路を保持し、テストAABBを既定表示から外す
- status: done
- done-when: `GameApplicationHandler::CreateGameModeStateMachine` が従来どおり `Rendering3DTest` を開始し、`Rendering3DTestRoutine::Enter` が球・地面・岩・ライト・HDR環境を構成する経路を変更しない。既定フレームへ常時投入されていた検証用黄色AABBだけを外し、選択表示のAABBは維持する。保存済みのEditorレンダリングシーンを推測して別ファイルへ差し替えない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingValidationSceneContractTest|SceneViewViewportCommandTest|WorldCameraSyncTest)$"`
- verify: `build\\Game\\Debug\\Game.exe --imgui --exit-after-rendered-frames=120`
- verify: `Select-String Game.log -Pattern "Sphere Entity created and added to World|Ground Entity created and added to World|Boulder model loaded and added to World|Environment source and derived IBL resources created|exit-after-rendered-frames reached"`
- stop-when: 実行ログに既定シーン構成または120フレーム終了の証拠がなく、保存済みのレンダリングシーン源を確認できない場合は、別のシーンを発明して起動経路へ接続せず、未実装の永続化入口を後続タスクとして記録する。
- paths: Game/GameModes/Rendering3DTest/Rendering3DTestRoutine.cpp, TASKS.md, PROGRESS.md

## AUDIT-RM-P1: RenderingRoadmapと実装・受入れ履歴の整合を監査する
- status: done
- done-when: R0〜R8のRoadMap依存関係・ステータス表・完了コミットを、追跡対象のコード、受入れ記録、PROGRESS.md、完了トレーラーと突き合わせる。R0〜R5の実装済み範囲、RoadMap表の遅れ、R6以降の着手入口、保存済みレンダリングシーンの有無を明示し、RoadMap本体を変更せず追跡対象の監査記録へ固定する。
- verify: `git check-ignore -v Docs/Plans/RenderingRoadmap.md`
- verify: `git log --all --format="%H%n%s%n%b%n---" --grep="RenderingRoadmap:"`
- verify: `git diff --check`
- stop-when: RoadMapの依存関係または完了証拠が現行履歴から再構成できない場合は、未確認のフェーズを完了扱いにせず、不足証跡を監査記録へ残す。
- paths: Docs/RenderingValidation/RenderingRoadmapAudit.md, TASKS.md, PROGRESS.md

## R6A-M1: velocityの方式と検証契約を定義する
- status: done
- done-when: R6-aのvelocity符号、device用clip行列、初回/無効履歴のゼロ契約、R16G16_FLOATと`GBuffer_Velocity`、FramePacketでのカメラ/オブジェクト履歴、初回対象経路、完了条件、停止条件を追跡対象の設計文書へ固定する。
- verify: `git diff --check`
- verify: `rg -n "currentUV - previousUV|GBuffer_Velocity|FramePacket|停止条件" Docs/RenderingValidation/R6aVelocityPlan.md`
- paths: Docs/RenderingValidation/R6aVelocityPlan.md, TASKS.md, PROGRESS.md

## R6A-P1: velocityのFramePacket履歴とGBuffer出力を実装する
- status: done
- done-when: 通常の遅延不透明MeshProxyについて、現在/前フレームのオブジェクト行列とメインカメラをFramePacketだけでRenderThreadへ渡し、既存GBuffer 4面の契約を維持したまま`GBuffer_Velocity`へR16G16_FLOATのvelocityを書き出す。移動後停止したMeshProxyのvelocityが次フレームでゼロになり、カメラのみ/物体のみ/併用の既知点を解析投影値の0.002以内でreadbackできる。物体のみは対象外背景がゼロで、初回無効履歴と安定静止を別frameで検証し、起動経路とRendering3DTestのシーン構成を変更しない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(FrameCaptureReadbackHelperTest|FramePacketManagerTest|RenderFrameExecutorPlanTest|MeshBatcherTest|MeshBatcherInstancingTest|InstanceDataFlattenTest|GBufferMaterialDescriptorCacheTest|SceneViewViewportCommandTest|BoardTransformTest|CanvasViewRenderTest|RenderGraphTextureUsageContractTest|RenderGraphCompileTest|RenderGraphNamedResourceTest|RenderGraphAttachmentStateTest|WorldCameraSyncTest|PhysicsArchitectureContractTest|PhysicsFixedStepPipelineTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure -V --no-tests=error -R "^RenderingVelocity(Static|Motion|Camera|Object|FirstFrame|MoveThenStop)VulkanTest$"`
- verify: `build\\Game\\Debug\\Game.exe --imgui --exit-after-rendered-frames=120`
- verify: `rg -n "CreateGameModeStateMachine|Sphere Entity created and added to World|Ground Entity created and added to World|Boulder model loaded and added to World|Environment source and derived IBL resources created|exit-after-rendered-frames reached" Game.log`
- stop-when: 既存のRHI/RenderGraph境界を保ったままvelocity attachmentを作成できない、またはGPU readbackで解析契約を観測できない場合は、RTGIや別起動経路へ拡張せずAPI不足を記録する。
- paths: Library/Core/Public/Component/MeshComponent.h, Library/Core/Private/Component/MeshComponent.cpp, Library/Core/Private/Object/World.cpp, Library/Core/Public/Rendering/MeshTypes.h, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Private/Rendering/DrawCommand.cpp, Library/Core/Public/Rendering/FramePacket.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/GBufferPass.h, Library/Core/Private/Rendering/GBufferPass.cpp, Library/Core/Public/Rendering/RenderGraph/RenderGraphResourceNames.h, Library/Core/Public/Rendering/FrameCaptureTypes.h, Library/Core/Private/Rendering/RenderFrameExecutor.cpp, Library/Core/Private/Rendering/FrameCaptureReadbackHelper.cpp, Assets/Shaders/gbuffer.vert, Assets/Shaders/gbuffer.frag, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.h, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingVelocityVulkanTest.cpp, Test/Core/Rendering/RenderGraphCompileTest.cpp, Test/Core/Rendering/CMakeLists.txt, Test/Modules/Physics/PhysicsArchitectureContractTest.cpp, Test/Modules/Physics/PhysicsFixedStepPipelineTest.cpp, Docs/RenderingValidation/R6aVelocityPlan.md, Docs/Rendering/R6aVelocityAcceptance.md, TASKS.md, PROGRESS.md

## R6-M1: RTGIとテンポラルデノイザの方式を選定する
- status: done
- done-when: RoadMapのR6本体について、RTGIの1〜2バウンス方式、ray query/RT pipelineの用途分担、テンポラル蓄積、デノイザ方式、履歴寿命、固定フレーム数ウォームアップ、R6性能gate保留を選定記録へ固定する。R6-aのvelocity契約とR5のRT基盤を前提にし、R7/R8の未実装機能を先取りしない。
- verify: `cmake --build build --config Debug --target Game RHIRayTracingPipelineVulkanTest -- /m:1`（exit 0）
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RHIRayTracingPipelineVulkanTest|RenderingVelocityCameraVulkanTest)$"`（2/2 passed）
- verify: `git diff --check`
- verify: `rg -n "RTGI|デノイザ|R6-a|ウォームアップ|性能" Docs/RenderingValidation/R6TechniquePlan.md`
- stop-when: R6本体の方式選定がR5/R6-aの公開境界やR7のパストレーサー実装を要求する場合は、境界を越えず未決定事項として記録する。
- paths: Docs/RenderingValidation/R6TechniquePlan.md, TASKS.md, PROGRESS.md

## R6-P1: RTGI結果形式とfallback契約を実装する
- status: done
- done-when: R6の1 bounce diffuse出力、ray-query capability、TLAS snapshot、FramePacketのscene/light revision、履歴resourceのcurrent/history公開、R4 DDGI/既存IBL fallbackをbackend-neutralな契約として接続し、RT無効・資源失敗時に既定rasterを維持する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|RenderingDDGILightingContractTest|RayTracingSceneSnapshotTest)$"`
- stop-when: R5のTLAS所有権またはR6-aのFramePacket/velocity契約を変更しないと接続できない場合は、必要なAPI不足を記録して停止する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md

## R6-P1-FIX: revisionとRTGI履歴契約の意味論を修正する
- status: done
- done-when: SceneRevisionが毎フレームの物体変換・PreviousWorldではなくシーン構成の変更を表し、前フレーム履歴のrevision差を動的移動中の即時fallback条件にしない。履歴のrevision差はR6-P3の棄却・weight抑制へ渡せる形で保持し、構成変更時は履歴を不採用にする。履歴revisionが1つ前でもRTGI選択が維持され、構成変更では不採用となる契約テストを追加する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingDDGILightingContractTest|RayTracingSceneSnapshotTest|RenderingVelocityCameraVulkanTest)$"`
- stop-when: R5のTLAS所有権またはR6-aのvelocity符号・FramePacket値所有を変更しないと修正できない場合は、必要な境界差分を記録して停止する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md

## R6-P2: 1 bounce ray-query GIを接続する
- status: done
- done-when: compute shader内のray queryでTLAS hit/missを処理し、有限なdiffuse indirect radianceをLightingPassへ渡す。RT非対応・RT無効・TLAS不完全・dispatch失敗時はR4 DDGIまたは既存IBLへ戻り、R5 RT pipeline/SBTを変更しない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(DDGIProbeRayQueryVulkanTest|RenderingRayTracingShadowVulkanTest)$"`
- stop-when: ray queryのscene snapshotとR5のTLASを共有できず、RT pipelineの新規SBT設計が必須になった場合はR7境界として未決定へ戻す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md

## R6-P1-FIX-2: SceneRevisionをカリング・ソート非依存へ修正する
- status: done
- done-when: SceneRevisionがカリング済みDrawCommandや奥行きソート順、物体変換、UI表示順に依存せず、全MeshProxy/SkinnedMeshProxyの構成集合と材質・環境設定だけで決まり、カメラ移動・物体移動・同一集合の並べ替えでは変化しない。構成追加・削除・メッシュ/材質変更では変化する性質テストを追加する。R6-P1の履歴revision契約とFramePacket値所有を維持する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingDDGILightingContractTest|RayTracingSceneSnapshotTest|RenderingVelocityCameraVulkanTest)$"`
- stop-when: 全proxy集合をFramePacketへ値コピーする既存境界だけでは構成集合を観測できない場合は、RenderThreadからWorldを参照せず不足するsnapshot項目を記録して停止する。
- paths: Library/Core/Public/Rendering/FramePacket.h, Library/Core/Public/Rendering/SceneProxy.h, Library/Core/Public/Rendering/RenderingCoordinator.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Test/Core/Rendering/RenderingDDGILightingContractTest.cpp, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md

## R6-P2-FIX: RTGI陽性経路をGPU readbackで検証する
- status: done
- done-when: RTGI capability・完全TLAS・出力textureを設定した専用GPUテストがLightingPassのray-query computeを実行し、hit/miss結果のfinite radiance、RTGI公開状態、RTGI無効・TLAS不完全時の既存間接光fallbackをreadbackで反証可能にする。既存R5 RT pipeline/SBTとRendering3DTest起動経路を変更しない。
- verify: `cmake --build build --config Debug --target Game RTGIDiffuseIndirectVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RTGIDiffuseIndirectVulkanTest|DDGIProbeRayQueryVulkanTest|RenderingRayTracingShadowVulkanTest)$"`
- verify: `glslangValidator -V --target-env vulkan1.2 -S comp Assets/Shaders/RTGI/DiffuseIndirect.comp -o build/RTGI-DiffuseIndirect.spv`
- stop-when: テストfixtureからLightingPassのRTGI入力を公開できず実GPU陽性経路を観測できない場合は、fallbackだけを合格にせず必要なtest seamを記録して停止する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders/RTGI, Test/Core/Rendering/RTGIDiffuseIndirectVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md

## R6-P3: velocity再投影と8フレーム履歴を実装する
- status: done
- done-when: `previousUV = currentUV - velocity` を使うcurrent/history ping-pong、depth/normal/material/revision棄却、age上限8、confidence、light revisionの2フレームweight制限、resize/初回/TLAS失敗時の無効化を実装する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingVelocityCameraVulkanTest|RenderingVelocityObjectVulkanTest|RenderGraphCompileTest)$"`
- stop-when: velocityの符号またはFramePacketの値所有を変更しないと履歴が成立しない場合は、R6-a契約との差分を記録して停止する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md

## R6-P4: 3x3 cross-bilateralデノイザを実装する
- status: done
- done-when: テンポラル出力へdepth/normal/material境界を越えない3x3 cross-bilateral filterを1回適用し、history confidenceをweightへ反映し、pre-exposureを二重適用せずLightingPassへ合成する。外部NRDや複数段SVGFを導入しない。
- verify: `cmake --build build --config Debug --target Game LightingParamsLayoutTest RTGIDiffuseIndirectVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingParamsLayoutTest|RenderGraphTextureUsageContractTest|RenderGraphCompileTest|RTGIDiffuseIndirectVulkanTest)$"`
- stop-when: 既存GBuffer/Lightingの公開境界を壊さずfilterへ入力できない場合は、追加attachmentを推測で増やさず契約不足を記録する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md
- notes: テンポラルconfidenceをcross-bilateral weightへ反映し、RTGI GPUテストで公開・hit/miss・fallbackを確認した。出力textureの直接readbackと遠景depth閾値の確認はNEXT_FINDINGS.mdへ記録した。

## R6-P5: 動的GIとfallbackのGPU受入れを固定する
- status: done
- done-when: 固定8 rendered-frame warmup後の静止golden、カメラ/物体移動、ライト移動4フレーム以内の追従、RT無効/R4/IBL fallback、履歴棄却と移動後停止をGPU readbackまたはHDR captureで検証する。既定のRendering3DTest起動経路は不変とする。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation --timeout 180`
- stop-when: 既定シーンの球・地面・ライト球・方向ライト・boulder・HDR環境を失う変更が必要になった場合は、シーン起動経路を戻してfixtureを別経路へ分離する。
- paths: Test/Core/Rendering, Assets/Shaders, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-09-23の独立評価（NEEDS_WORK）で再開。(a) `Assets/Shaders/RTGI/DiffuseIndirect.comp`のレイ方向がpixel座標だけで決まり静止中に同じレイを再評価するため、rendered frameごとに決定論的に異なるサンプルへ直す。(b) ライト追従率の分母を収束後の変化量にし、間接光ROIで4 rendered frame以内の80%到達を判定する。(c) 履歴棄却を履歴age/confidenceのreadbackなど棄却そのもので反証する。静止収束画像の参照比較はR6-P5-REFで行う。

## R6-P5-REF: 静止収束画像をR7自前PT参照と知覚diffで比較する
- status: done
- done-when: R6受入れと同じ解像度・camera・geometry・material・light・HDR環境・exposure/pre-exposureで、R6 RTGIの静止収束SceneColorとR7 PTの収束SceneColorを事前に固定したFLIP pool/max-pixel閾値で比較し、結果と閾値根拠を`R6Acceptance.md`へ記録する。R7-P3Dの起動時PT選択と同一シーン比較の仕組みを使う。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^R6RTGIPathTracingReferenceVulkanTest$"`
- stop-when: 閾値超過が出た場合はRoadmap更新ルール5に従いR6を再オープン扱いにし、閾値やgoldenを緩めない。
- paths: Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md


## TEST-RGC: RenderGraphCompileTestのShadowMapPass初期化失敗を直す
- status: done
- done-when: `RenderGraphCompileTest`の`TestShadowMapNativeDeclareImportsDepthOutput`でFakeDevice上の`ShadowMapPass::Initialize`が失敗する原因（R2のCSM配列深度導入以降の既存失敗）を特定し、テストの契約を弱めずに直す。R6/R7の変更前から失敗していることは`c1474fc`時点の再実行で確認済み。
- verify: `cmake --build build --config Debug --target RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderGraphCompileTest$"`
- stop-when: 失敗がShadowMapPassの実装不具合ではなくFakeDeviceの不足による場合は、fakeへ必要な振る舞いを足し、ShadowMapPassの実資源検査を緩めない。
- paths: Test/Core/Rendering/RenderGraphCompileTest.cpp, Library/Core/Private/Rendering/ShadowMapPass.cpp, TASKS.md, PROGRESS.md

## R8-P1: ACES 2.0 SDRの出力変換を3D LUTへ焼き込み、OCIOの基準画像を作る
- status: done
- done-when: `Scripts/BakeAcesOutputLut.py` がOCIO（pipの`opencolorio`、2.4以上の組み込みACES 2.0 studio config）で、scene-linear Rec.709（D65）からdisplay「sRGB - Display」・view「ACES 2.0 - SDR 100 nits (Rec.709)」への変換を、log2 shaper（範囲をスクリプトの定数と記録に固定）付きの65³ RGBA16F LUTへ焼き、`Assets/ColorManagement/` に置く。LUTの値はsRGBの符号化を外したdisplay-linearにする（既存のsRGB提示をそのまま使う）。同じスクリプトが決定論的なHDR試験チャート（露出段のグレー・原色/補色・肌/空の色票）と、そのOCIO厳密変換の基準画像（`Test/Core/Rendering/Baselines/RenderingValidation/`）を作る。`--verify` は再生成がbyte一致し、LUT補間（CPU、三線形）とOCIO厳密変換の差がチャートの全画素でsRGB符号化後2/255以内なら0で終わる。OCIOの版・config名・shaper範囲・SHA-256とS8の決定（ベイクLUT）を `Docs/RenderingValidation/R8ColorManagement.md` に記録する。
- verify: `python Scripts/BakeAcesOutputLut.py --verify`
- stop-when: OCIOの組み込みconfigにACES 2.0のSDR viewがない、またはpipで入らない場合は理由を記録してユーザーへ戻す。
- paths: Scripts/BakeAcesOutputLut.py, Assets/ColorManagement/*, Test/Core/Rendering/Baselines/RenderingValidation/R8Aces*, Docs/RenderingValidation/R8ColorManagement.md, TASKS.md, PROGRESS.md
- notes: 2026-09-26 ユーザー承認のR8計画（S8=ベイクLUT、1280×720・1024spp、フィルムグレインは既定オフで入れる、連番の検査はScripts＋短いCTest）。pipでopencolorioを入れてよい（ユーザー承認済み）。承認済みgolden・閾値は変えない。既定の起動（Rendering3DTest）とトーンマップの既定は変えない。

## R8-P2: 3D textureのLUTでACES 2.0 SDRへ変換するトーンマップを加える
- status: done
- done-when: `ToneMappingPass` に新しい演算子（ACES 2.0 SDRのLUT）を加え、R8-P1のshaperで3D texture（RHIの`Texture3D`をGPUで初めて標本化する）を引く。この演算子では既定のグレーディング（Contrast/Saturation）を掛けない。起動引数（例 `--tone-map=aces20-lut`）で選べ、既定の演算子は変えない。新しいGPUテストがR8-P1の試験チャートをSceneColorとしてpassへ入れ、ToneMappedColorをOCIO基準画像とsRGB符号化後2/255以内で一致させる（閾値は比較の前に固定）。Indoor/Outdoor goldenは変わらない。
- verify: `cmake --build build --config Debug --target Game R8AcesLutToneMappingVulkanTest ToneMappingParamsLayoutTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8AcesLutToneMappingVulkanTest|ToneMappingParamsLayoutTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: 承認済みgoldenが変わる場合、または2/255に収まらずLUTの解像度・shaperを変える必要がある場合は、測定値を記録してユーザーへ戻す。
- paths: Assets/Shaders/tonemapping.frag, Library/Core/Public/Rendering/ToneMappingPass.h, Library/Core/Private/Rendering/ToneMappingPass.cpp, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/Public/RHI/*, Library/Core/Private/RHI/Vulkan/VulkanTexture.cpp, Test/Core/Rendering/R8AcesLutToneMapping*, Test/Core/Rendering/ToneMappingParamsLayoutTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 危険地帯（RHIの3D texture）。評価者を通す。Rendering層からRHI/Vulkanをincludeしない。

## R8-P3: PTの連番の1フレームを、前後のカメラ・変換とシャッター区間を固定して累積する
- status: done
- done-when: PTに連番の1フレームを描く経路を加える。1フレームの累積（1 spp × N dispatch）の間、前後のカメラ・instance変換と、シャッター区間の基準の長さ（起動引数のフレーム長、例 1/24 s。実時間のDeltaTimeを使わない）を固定し、dispatchごとにレンズとシャッター時刻を引き直す。起動引数で絞り（f値）・焦点距離（focus distance）・シャッター時間・フレーム長を指定できる。新しいGPUテストが、既知の速度で動く物体の動きぼけの幅がシャッター時間×速度に1 px以内、焦点外の点のCoCが解析値（`ComputePathTracingCocPixels`）に2%以内で一致し、累積の途中で履歴が消えず、再実行でbyte一致することを確かめる。既存のPTテストは変わらない。
- verify: `cmake --build build --config Debug --target Game R8PathTracingSequenceFrameVulkanTest PathTracingCameraTest PathTracingCameraVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8PathTracingSequenceFrameVulkanTest|PathTracingCameraTest|PathTracingCameraVulkanTest|PathTracingVulkanTest|PathTracingOutdoorVulkanTest)$"`
- stop-when: 既存のR6/R7のPT参照比較の結果が変わる場合は理由を記録してユーザーへ戻す。
- paths: Library/Core/Public/Rendering/PathTracingCamera.h, Library/Core/Public/Rendering/PathTracingPass.h, Library/Core/Private/Rendering/PathTracingPass.inl, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/FramePacket.h, Library/Core/Public/Rendering/SceneProxy.h, Library/Core/Private/Engine/ApplicationProcessor.cpp, Test/Core/Rendering/R8PathTracingSequenceFrame*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 危険地帯（RenderThread・PT・FramePacket）。評価者を通す。GameThread→RenderThreadはFramePacketのsnapshot越しだけ。

## R8-P3-FIX: 連番の1フレームの累積を、同じフレーム内のinstanceの並び替えで捨てない
- status: done
- done-when: R8-P3の評価（反復5）の指摘を直す。`HashPathGeometry`はinstanceの配列順で前後の変換・材質・customIndexを署名にするため、物体ごとの状態が同じでも同じSequenceFrameの中で`P,A,B`→`P,B,A`と並びが変わると累積が捨てられる。連番の経路のRTスナップショットを安定した物体キーの順に揃え、customIndex・材質texture表・発光instance表を整合させる（または署名を順序に依らない形にし、光源標本の表の順序も揃える）。`R8PathTracingSequenceFrameVulkanTest`へ、SequenceFrameと各物体の前後変換を固定したまま並びを変えた2回のdispatchで試料数が1→2と続き、固定順で描いた画像とbyte一致する検査を加える。既存のPTテストは変わらない。
- verify: `cmake --build build --config Debug --target Game R8PathTracingSequenceFrameVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8PathTracingSequenceFrameVulkanTest|PathTracingCameraTest|PathTracingCameraVulkanTest|PathTracingVulkanTest)$"`
- stop-when: 並びの正規化がR6/R7のPT参照比較の結果を変える場合は理由を記録してユーザーへ戻す。
- paths: Library/Core/Private/Rendering/PathTracingPass.inl, Library/Core/Public/Rendering/PathTracingPass.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/FramePacket.h, Test/Core/Rendering/R8PathTracingSequenceFrame*, TASKS.md, PROGRESS.md
- notes: 危険地帯（PT・RenderThread）。評価者を通す。

## R8-P4: ラスタの被写界深度をPTの薄レンズ参照と比べる
- status: done
- done-when: ラスタに被写界深度のpass（深度からCoCを求め、PTと同じ薄レンズの式・撮像面高24 mm・FOVから焦点距離）を加える。focus distanceが0（ピンホール）なら働かず、承認済みgoldenは変わらない。新しい比較テストが、手前・焦点面・奥に物体を置いた静止シーンで、ラスタのDoFとPT参照（R8-P3の経路、独立な3組の画素ごとの中央値）を比べる。閾値は比較の前に固定する規則で作る: PT参照と、f値を±20%変えたPTとの知覚差（FLIP平均・一致画素の画素単位最大・8×8区画最大）のうち小さい方。±40%の変化が3つとも閾値の外にあることを比較のたびに確かめる。
- verify: `cmake --build build --config Debug --target Game R8DepthOfFieldPathTracingReferenceVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8DepthOfFieldPathTracingReferenceVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。比較の入力のPTピンホール像と薄レンズ参照を画素内の一様標本（box）にそろえて取り直す（閾値の規則は不変）。それでも全画素の平均や光源の縁の区画最大がわずかに残る場合は、測定値と差の分類を既知の限界として記録してR8-P4を完了にする。
- paths: Assets/Shaders/*DepthOfField*, Library/Core/Public/Rendering/*DepthOfField*, Library/Core/Private/Rendering/*DepthOfField*, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Component/CameraComponent.h, Library/Core/Private/Component/CameraComponent.cpp, Test/Core/Rendering/R8DepthOfField*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 取得（PTの3組の中央値と±20%/±40%の変種、約20分）は1反復で1回までにし、shaderやCPU比較の変更は取得済みのダンプに対する`--compare-dumps`だけで評価する（shaderは実行時に読む）。取得をやり直すのは、pass本体やC++の変更で画像が変わるときだけ。差の分類と現状は`blocked/R8-P4.md`。
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: 比較入力のbox標本（`91b774b`）と既知の限界の範囲の判定（`7ca5332`。全画素の平均と光源の縁の区画最大に記録値+5%の上限、光源の縁の矩形の外は規則の閾値）。上限の値は中心標本の測定値のままで、box標本の取得の後に測定値へ合わせる。verifyは未実行。
- result: 比較の入力のPTのピンホール像と薄レンズ参照を画素内の一様標本にそろえた取得で、規則の判定PASS（平均0.0184≤0.0254、一致画素の画素最大0.356≤0.524、区画0.0529≤0.0567、±40%は外、負の対照は検出、VUID 0）。既知の限界の範囲の判定は不要になり外した（`b0f365b`）。記録は`Docs/RenderingValidation/R8Acceptance.md`。

## R8-P5: ラスタの動きぼけをPTのシャッター参照と比べる
- status: done
- done-when: ラスタに動きぼけのpass（R6-aのvelocityにシャッター時間/フレーム長を掛け、空の画素はカメラの動きから求める）を加える。シャッター時間が0なら働かず、承認済みgoldenは変わらない。新しい比較テストが、カメラのpanと既知の速度で動く物体のシーンで、ラスタの動きぼけとPT参照（R8-P3の経路、3組の中央値）を比べる。閾値の規則はR8-P4と同じで、シャッター時間を±20%変えたPTを物差しにし、±40%が閾値の外にあることを確かめる。
- verify: `cmake --build build --config Debug --target Game R8MotionBlurPathTracingReferenceVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8MotionBlurPathTracingReferenceVulkanTest|RenderingVelocityObjectVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。
- paths: Assets/Shaders/*MotionBlur*, Library/Core/Public/Rendering/*MotionBlur*, Library/Core/Private/Rendering/*MotionBlur*, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Test/Core/Rendering/R8MotionBlur*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: `1850803`（pass・比較テスト・動く球の床の影を既知の限界の範囲にする判定）。取得済みダンプ（`build/RenderingValidation/R8MotionBlurPathTracingReferenceRuns`）で平均0.0190（内）、影の矩形の内で画素0.794・区画0.1645。負の対照は画素の閾値が光源の縁で決まるため未検出で、既知の限界として記録する。verifyは未実行。
- result: `1850803`・`251fc92`。規則の判定FAIL（動く球の床の影で画素0.794・区画0.1646）、既知の限界の範囲の判定WITHIN（影の矩形の外は画素0.467・区画0.094で閾値内、平均0.0191は閾値内、2×2・4倍の負の対照は検出）。静止面の上を動く影は画面velocityで運べないため既知の限界として記録した。

## R8-P6: フィルムグレインを既定オフで加える
- status: done
- done-when: 出力変換の後（display空間）に、画素・フレーム番号・seedから決まるフィルムグレインを加える。強さは起動引数で指定し、既定はオフ（承認済みgoldenは変わらない）。新しいGPUテストが、オフでは出力が従来と一致し、オンでは中間グレーの平均の変化が0.5/255以内・標準偏差が指定の±10%以内、隣のフレームで模様が変わり、同じフレームの再実行でbyte一致することを確かめる。
- verify: `cmake --build build --config Debug --target Game R8FilmGrainVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8FilmGrainVulkanTest|ToneMappingParamsLayoutTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。
- paths: Assets/Shaders/tonemapping.frag, Library/Core/Public/Rendering/ToneMappingPass.h, Library/Core/Private/Rendering/ToneMappingPass.cpp, Library/Core/Private/Engine/ApplicationProcessor.cpp, Test/Core/Rendering/R8FilmGrain*, Test/Core/Rendering/ToneMappingParamsLayoutTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: `fce422d`。verifyは未実行。
- result: `fce422d`。強さ0で従来とbyte一致、4/255で平均の変化0.043/255・標準偏差3.999/255、隣のフレームの相関−0.004、再実行でbyte一致、VUID 0。golden屋内/屋外・ToneMappingParamsLayoutTestはpassed。

## R8-P7: EXR連番の検証exeを作る
- status: done
- done-when: C++の検証exe（TinyEXRとThirdPartyのFLIPを使う）が、連番のdirectoryと期待するフレーム数から、欠番・寸法の不一致・NaN/Inf画素（フレームと座標を出す）を検出し、R8-P1のLUT（CPU）でdisplayへ変換した隣接フレームのLDR-FLIP平均を並べ、中央値の3倍（かつ下限0.01）を超える組をポッピングとして失敗にする。規則は検査の前に固定する。単体テストが合成の連番で、正常な連番は合格、欠番・NaN・寸法違い・差し込んだ1フレームの跳びをそれぞれ検出することを確かめる。
- verify: `cmake --build build --config Debug --target R8ExrSequenceValidator R8ExrSequenceValidatorTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^R8ExrSequenceValidatorTest$"`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。
- paths: Test/Core/Rendering/R8ExrSequence*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: `31d84e0`（単体テストはCPUだけで動く）。verifyは未実行。
- result: `31d84e0`・`2ef552d`。合成の連番で正常は合格、欠番・NaN・寸法違い・1フレームの跳び・期待と違う寸法をそれぞれ検出。

## R8-P8: 屋内・屋外の決定論的なアニメーションとEXR連番の書き出し、8フレームのCTestを加える
- status: done
- done-when: 検証アプリのhandlerが、フレーム番号iから時刻 i/24 のカメラと物体の変換を決め（屋内: Cornellで箱が滑りカメラがdollyする。屋外: 空・霧の屋外シーンでカメラが回り球が動く）、R8-P3の経路（絞り・シャッター1/48 s・フレーム長1/24 s）で各フレームを累積し、`WritePathTracingExrFrame`で書き出し、manifest（シーン・解像度・spp・フレーム範囲・seed）を残す。起動引数でシーン・フレーム範囲・解像度・spp・出力directoryを指定できる。CTest `R8SequenceSmokeTest` が屋内・屋外それぞれ8フレームを256×144・64 sppで書き、R8-P7の検証exeに合格する。
- verify: `cmake --build build --config Debug --target Game R8SequenceRenderer R8ExrSequenceValidator -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^R8SequenceSmokeTest$"`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。
- paths: Test/Core/Rendering/R8Sequence*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, Library/Core/Public/Rendering/PathTracingExrOutput.h, Library/Core/Private/Rendering/PathTracingExrOutput.cpp, TASKS.md, PROGRESS.md
- notes: GameThread→RenderThreadはFramePacket越しだけ。
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: `e59e3aa`。verifyは未実行。
- result: `e59e3aa`・`2ef552d`。R8SequenceSmokeTestが屋内・屋外それぞれ8フレームを全フレームちょうど64試料で書き、検証exeに合格。

## R8-P9: 屋内の240フレームの連番を1280×720・1024 sppで書き出し、検査する
- status: done
- done-when: `Scripts/RenderR8Sequences.ps1 -Scene indoor` が屋内の240フレーム（24 fps・10秒）を1280×720・1024 sppで`build/R8Sequences/indoor/`へ書き、R8-P7の検証exeで欠番0・NaN/Inf画素0・ポッピング0を確かめ、所要時間・容量・検証結果を`.harness/runs/<日付>-r8-sequences/`へ残して0で終わる。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RenderR8Sequences.ps1 -Scene indoor`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。1反復で240フレームが書き終わらない場合は、書き出し済みの最後のフレームから続きを書く形で反復をまたいで進める。
- paths: Scripts/RenderR8Sequences.ps1, TASKS.md, PROGRESS.md
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: `4250b21`（`Scripts/RenderR8Sequences.ps1`、欠けたフレームだけを24枚ずつ描き、描画のプロセスはCPUの優先度を下げる）。verifyは未実行。
- result: 240フレーム・欠番0・非有限0・ポッピング0（隣接FLIPの中央値0.0108・最大0.0129・閾値0.0324）、全フレームちょうど1024試料、812 s・2.53 GB。記録は`.harness/runs/20260926-r8-sequences/`。

## R8-P10: 屋外の240フレームの連番を1280×720・1024 sppで書き出し、検査する
- status: done
- done-when: `Scripts/RenderR8Sequences.ps1 -Scene outdoor` が屋外の240フレームを同じ条件で`build/R8Sequences/outdoor/`へ書き、欠番0・NaN/Inf画素0・ポッピング0を確かめ、記録を残して0で終わる。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RenderR8Sequences.ps1 -Scene outdoor`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。1反復で240フレームが書き終わらない場合は、書き出し済みの最後のフレームから続きを書く形で反復をまたいで進める。
- paths: Scripts/RenderR8Sequences.ps1, TASKS.md, PROGRESS.md
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: `4250b21`（R8-P9と同じスクリプト）。verifyは未実行。
- result: 240フレーム・欠番0・非有限0・ポッピング0（隣接FLIPの中央値0.0161・最大0.0207・閾値0.0483）、全フレームちょうど1024試料、685 s・1.65 GB。

## R8-P11: R8の受入れ記録を確定する
- status: done
- done-when: `Docs/RenderingValidation/R8Acceptance.md` に、ACES基準画像との一致（R8-P2）、DoF・動きぼけのPT参照比較（R8-P4/P5）、フィルムグレイン（R8-P6）、屋内・屋外の240フレームの連番の検査（R8-P9/P10）の結果とログ、既知の制限、GPU性能はDeferredを記録し、完了コミットの本文末尾に `RenderingRoadmap: R8 complete` trailerを付ける。PROGRESS.mdに完了を記録する。
- verify: `git diff --check`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。既知の限界として完了にしたタスクは、受入れ記録に測定値・閾値・分類を明記したうえでtrailerを付ける。
- paths: Docs/RenderingValidation/R8Acceptance.md, Docs/RenderingValidation/R8ColorManagement.md, TASKS.md, PROGRESS.md
- notes: 危険地帯を含む機能の完了判定。評価者を通す。
- result: `Docs/RenderingValidation/R8Acceptance.md`。R8の変更の独立評価は1周目NEEDS_WORK（4件）、対応差分の2周目PASS。

## TEST-SKINNED: SkinnedRenderPathContractTestの停止を直す
- status: backlog
- done-when: `SkinnedRenderPathContractTest`がCPU 0のまま戻らない（2026-09-24にctest 1350秒で強制終了）原因を特定し、契約を弱めずに完走させる。R5-P12時点でもpending件数assertで失敗していた既存問題として扱う。
- verify: `cmake --build build --config Debug --target SkinnedRenderPathContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error --timeout 300 -R "^SkinnedRenderPathContractTest$"`
- stop-when: 停止がスキニングの実装不具合ではなくテストの待機条件による場合は、待機条件を明示的な上限付きにし、検証内容を減らさない。
- paths: Test/Core/Rendering/SkinnedRenderPathContractTest.cpp, Library/Core/Private/Rendering, TASKS.md, PROGRESS.md

## R6-P7: RTGIで発光三角形を光源標本する
- status: done
- done-when: RTGIの1次面と命中点の直接光に、レイトレーシングシーンの発光三角形の光源標本（影の問い合わせ付き）を加え、面光源の直接光と面光源に照らされた面からの1バウンスを雑音の少ない推定にする。`R6RTGIPathTracingReferenceVulkanTest`がR6-P5-REFで固定した閾値（間接光±20%の物差し）内に入り、R6受入れと既存のRTGI・DDGI・golden testが通る。
- verify: `cmake --build build --config Debug --target Game R6RTGIPathTracingReferenceVulkanTest R6RTGIAcceptanceVulkanTest RTGIDiffuseIndirectVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R6RTGIPathTracingReferenceVulkanTest|R6RTGIAcceptanceVulkanTest|RTGIDiffuseIndirectVulkanTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 承認済みgoldenやR6-P5-REFの閾値を変えないと通らない場合は、方式の再選定としてユーザーへ戻す。
- paths: Assets/Shaders/RTGI, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（RTGI shader・LightingPass・RenderThread）。R6-P5-REFで発見。ラスタは発光三角形を光源として扱わず、面光源の直接光がRTGIの偶然の命中だけで入るため斑点状の雑音になる。
- result: 光源標本（`128294a`、評価対応`02eaa7e`）、RTGIへSSAOを重ねない（`81bbf24`）、デノイズの外れ値抑制（`3f4f39c`）、PTの画素中心標本（`a15c43a`）に加え、ユーザーの判断で画素単位最大を幾何が一致する画素で判定し直接光を解析BRDFで揃え（`c162366`・`f7d0b59`・`775261a`）、静止時だけRTGIの履歴を延長し年齢に応じてデノイズの近傍の重みを下げた。R6参照比較は平均0.0466・一致画素の最大0.150・区画0.100で閾値内（閾値と物差しは不変）。記録は`Docs/RenderingValidation/R6Acceptance.md`の「R6-P7の完了」。

## FIX-NEURAL-BRDF-STREAK: ニューラルBRDFの直接光が点光源の近くに作る筋を直す
- status: backlog
- done-when: 通常表示の直接光（ニューラルBRDF）と解析BRDFの差が、Cornellの天井のように点光源に近い粗い面でも筋を作らない。原因（学習範囲外の入力、かすめ角の鏡面項など）を特定し、学習データか評価の範囲を直すか、範囲外では解析BRDFへ戻す。
- verify: 同じCornell条件で、ニューラルBRDFの通常表示と解析BRDF（検証mode 254の直接光）の画素差を比べる専用テストを追加して通す。
- stop-when: 学習済み重みの再生成が必要でデータがない場合は、範囲外の入力で解析BRDFへ戻す方針をユーザーへ提案する。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: R6-P7で発見。点光源だけの画像でラスタにだけ2画素幅の筋が出る。

## FIX-SSAO-ROOM-SCALE: 部屋の大きさのシーンでSSAOが壁をほぼ全遮蔽にする原因を直す
- status: backlog
- done-when: Cornell（5.5 m四方）の壁でSSAOがほぼ0になる原因（半径・bias・深度の再構成の尺度など）を特定して直し、IBL fallbackの間接光が壁で消えない。既存goldenは変えない。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 承認済みgoldenが変わる場合は基準の更新を提案して止まる。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: R6-P7で発見。RTGIには`81bbf24`でSSAOを掛けないようにしたが、IBL fallbackは引き続きSSAOを掛ける。

## TEST-R6-RESIDUAL-TIMING: R6停止残留の判定が起動の早さで変わる原因を直す
- status: done
- done-when: `R6RTGIAcceptanceVulkanTest`の停止残留（`R6_STOP_RESIDUAL_CHECK`）が、単体実行とRenderingValidationラベル内の実行で同じ物体影響の大きさになり、ラベル全件を3回続けて実行して毎回通る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^R6RTGIAcceptanceVulkanTest$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 物体影響の差がRTGI/DDGIの実装の不具合（履歴や更新順）による場合は、テストの待ち時間で隠さずR6の不具合として記録し直す。
- paths: Test/Core/Rendering, Library/Core/Private/Rendering, TASKS.md, PROGRESS.md
- notes: R7-P3Dで発見。初回取得は資産読み込みの完了（壁時計に依存）で始まり、以降の段階はフレーム数で進む。単体実行は最初の段階がframe 153前後で物体影響0.018〜0.06（残留比0.001〜0.003）、ラベル内はframe 76前後で物体影響が約1.1e-4まで落ち、残留比が閾値0.5付近（R7-P3Cのラベル実行で0.477、R7-P3Dで0.506）になる。R6-P6の前に直す。
- result: 物体影響の差はRTGIの不具合（面光源の直接光がRTGIの偶然の命中だけで入る）によるもので、R6-P7の光源標本で解消した。テストの待ち時間は変えていない。単体実行2回は最初の段階がframe 172〜175・物体影響0.0251〜0.0280、RenderingValidationラベル全件の3回連続実行は各回frame 166〜177・物体影響0.0267〜0.0289・残留0.0058前後（残留比約0.2、閾値0.5）で毎回通過した。ラベルの2・3回目に失敗した1件は、実行中に追加した未完成のR4再照合テスト（R4-REOPEN）で、R6系は3回とも通過。ログは`.harness/runs/20260924-test-r6-residual/`。

## R6-P6: R6受入れと性能gate保留を確定する
- status: done
- done-when: R6-P1〜P5のコードコミット、受入れログ、golden/threshold、fallback、既知の制限、R7暫定参照の再照合条件を記録し、完了コードコミットへ `RenderingRoadmap: R6 complete` trailerを付ける。GPU性能はDeferredとして残す。
- verify: `git diff --check`
- verify: `git log -1 --format=%B`
- verify: `rg -n "R6|RTGI|性能|Deferred|fallback" Docs/RenderingValidation/R6TechniquePlan.md Docs/RenderingValidation/R6Acceptance.md PROGRESS.md`
- stop-when: R6の機能gateが未完、またはR7/R8の実装を前提にしないと受入れできない場合は、完了trailerを付けず残課題を記録する。
- paths: Docs/RenderingValidation/R6TechniquePlan.md, Docs/RenderingValidation/R6Acceptance.md, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md
- result: `R6Acceptance.md`の判定を受入れへ確定し、R6-P5-REF・R6-P7のコミットと検証ログ、既知の制限を加えた。全体gate（`.harness/runs/20260925-r6-p6/`）はtargetless Debug build BUILD_EXIT=0、RenderingValidation 56件中47 passed・8 skipped・1 failed（再オープン中のR4の再照合だけ）。R6の完了判定の独立評価はPASS（blockingなし。non-blocking 3件の記述の正確さは同じコミットで直した）。GPU性能はDeferred。

## R4-REOPEN: R4 DDGIのprobe由来の斑点と漏れを直し、自前PT参照で再照合する
- status: done
- done-when: `R4DDGIPathTracingReferenceVulkanTest`（R4の規定の指標: direct white ROIで露出を1回決め、影・赤・緑ROIの相対輝度誤差≤0.25、赤・緑ROIの優勢色度の差≤0.10）が同じ閾値で通り、R4の受入れ・DDGI系・R6・golden testが通る。
- verify: `cmake --build build --config Debug --target Game R4DDGIPathTracingReferenceVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R4DDGIPathTracingReferenceVulkanTest|DDGI.*|RenderingDDGILightingContractTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 閾値かR4の指標を変えないと通らない場合、または承認済みgoldenが変わる場合はユーザーへ戻す。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（DDGI shader・LightingPass）。R7-P7の再照合（更新ルール5、2026-09-25）で発見。R4受入れと同じCornell状態（DDGI有効、RTGI無効、点光源なし、天井の面光源あり、512×512、128フレーム後）のラスタを、4096 sppの自前PT（全輸送）と比べ、赤ROIの相対輝度誤差0.257（上限0.25）、影0.162、緑0.0566、色度差0.0091/0.0167、露出0.846。ラスタのROI平均はR4受入れ時の記録（direct 0.393677、shadow 0.15236、red 0.0687421、green 0.0951394）と同値で、劣化ではない。画像全体ではprobeの位置に格子状の明るい斑点、奥の壁の暗転、画面の縁の暗い帯があり、64画素区画のラスタ/PT輝度比は0.07〜2.15に散らばる（`.harness/runs/20260925-r7-p7-r4/pt-vs-ddgi.png`、`ratio-map.txt`）。probe格子（原点-0.1、間隔0.82、8×8×8）の外側の層は壁の裏と開口の外にある。
- result: `2588b47`・`effd5e3`・`355f8df`・`3b1f3f4`・`ce9d10f`。probeの分類（面の裏のhitが25%を超えるprobeを無効）、probeでの発光面の直接照度（Lambertの式と影の可視率）、照度atlasの全体/間接光だけの2組のlayer（hit面は間接光の組を読み、直接光の二重計上をなくす）、RTXGIの補間（法線方向0.225倍のずらし、押しつぶし、平方根の補間）、距離のcosineの指数8。`R4DDGIPathTracingReferenceVulkanTest`は影0.117・赤0.224・緑0.130・色度差0.030/0.011で合格（閾値不変）、公開Cornell参照0.104/0.117/0.019、動的0.875/0.871。全target build後のRenderingValidationラベル57件中0件失敗（8件はGPU skip契約）、DDGI系9/9。危険地帯の独立評価2周の指摘（鏡映の表裏、発光面の近くの過大評価、頂点の順による放射の側）は修正して回帰の場面を追加した。記録は`Docs/RenderingValidation/R4Acceptance.md`、ログは`.harness/runs/20260925-r4-reopen/`。

## R6-GATE-DDGI-ORACLE: DDGI放射輝度比較のシナリオ履歴を分離する
- status: done
- done-when: 非遮蔽と点/スポット遮蔽のreadbackがそれぞれ固定の直接照明期待値に一致し、二つ目のケースへ前ケースのprobe irradiance蓄積を持ち越さない。rendererのradiance計算・期待値・閾値は変更しない。
- verify: `cmake --build build --config Debug --target DDGIProbeRadianceVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^DDGIProbeRadianceVulkanTest$"`
- stop-when: pass状態を分離しても期待値とreadbackが一致しない場合、radianceや閾値を調整せず追加原因を記録する。
- paths: Test/Core/Rendering/DDGIProbeRadianceVulkanTest.cpp, TASKS.md, PROGRESS.md

## R6-GATE-OUTDOOR: 承認済みOutdoor goldenとの差分を原因診断する
- status: done
- done-when: `RenderingGoldenOutdoorVulkanTest`が既存golden/thresholdを変更せず通過する。原因と修正を記録し、修正後の`RenderingValidation`全体で新規失敗がないことを確認する。既定のRendering3DTest起動経路と保存シーンは維持する。
- verify: `cmake --build build --config Debug --target RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderingGoldenOutdoorVulkanTest$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation --timeout 180`
- stop-when: 原因が既承認baselineの改定を要する場合はgoldenを書き換えず根拠を保存する。RHI image-layout等の独立不具合を検出した場合は範囲を分けて記録し、該当経路を隠さない。
- paths: Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering/RenderingValidation, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-M1: NEE/MISとEXR出力方式を選定する
- status: done
- done-when: power heuristic MIS、ReSTIR除外、TinyEXR C API version、EXR channel/precision/compression、seed決定論性、R7 core/outdoor分割を技術選定記録と実装計画に固定する。
- verify: `rg -n "S7|NEE|MIS|ReSTIR|TinyEXR|6f470c9|RenderingRoadmap: R7 complete" Docs/RenderingValidation/R7SamplingAndExrSelection.md Docs/RenderingValidation/R7TechniquePlan.md`
- verify: `git diff --check`
- stop-when: 選定したEXR APIが必要なWindows/CMake buildとfloat scanline出力を満たさない場合、実装開始前に代替を根拠付きで記録する。
- paths: Docs/RenderingValidation/R7SamplingAndExrSelection.md, Docs/RenderingValidation/R7TechniquePlan.md, TASKS.md, PROGRESS.md

## R7-P1: ラスタとPTのBRDF・texture評価を共有する
- status: done
- done-when: raster opaque/transparentとRT shaderが共通BRDF・texture evaluationを参照し、R1数値契約とIndoor/Outdoor goldenを維持する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RTGIDiffuseIndirectVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest|ForwardPassPipelinePlacementTest|LightingParamsLayoutTest)$"`
- stop-when: include機構が既存shader compilerで成立しない場合、全shaderを一括移行せず最小の共有境界を記録する。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-P2: 独立path-tracing pipelineとprogressive accumulationを実装する
- status: done
- done-when: 明示選択のPT pipelineがR5 RT pipeline/SBTとFramePacket snapshotだけで1 sample/frameを累積し、静止時に収束、camera/scene revision変更時に履歴をresetする。raster pipelineとRendering3DTest起動経路は不変。
- verify: `cmake --build build --config Debug --target Game PathTracingVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingVulkanTest$"`
- stop-when: RHI/Vulkan APIやRenderThreadからWorldへの参照が不可避となった場合、境界を越えずsnapshot不足を記録する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md

## R7-P3A: RHIに配列combined image samplerとnon-uniform indexingを追加する
- status: done
- done-when: `RHI::DescriptorBinding`に配列数`count`（既定1）と配列要素単位のcombined image sampler bindを追加し、Vulkanのset layout・pipeline layout・descriptor pool・writeが`count`を反映する。Vulkan 1.2の`shaderSampledImageArrayNonUniformIndexing`を対応時だけ有効化して`DeviceCapabilities`へ公開する。`count=1`の既存bindingのlayout・pool容量・writeは変えない。専用GPUテストで4要素配列を呼び出しごとに異なる添字で標本化し、各要素の既知色をreadbackで一致させる。範囲外要素・非配列bindingへのbindは拒否する。
- verify: `cmake --build build --config Debug --target Game RHIDescriptorArrayVulkanTest RHIRayTracingPipelineVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RHIDescriptorArrayVulkanTest|RHIRayTracingPipelineVulkanTest|RHIRayTracingApiContractTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation --timeout 180`
- stop-when: 対応GPUでnon-uniform indexingを有効化できない、または既存descriptor setの割り当て・更新が回帰する場合は、RHI変更を戻して不足した能力を記録する。
- paths: Library/Core/Public/RHI/IDescriptorSet.h, Library/Core/Public/RHI/DeviceCapabilities.h, Library/Core/Private/RHI/Vulkan/VulkanDescriptorSet.h, Library/Core/Private/RHI/Vulkan/VulkanDescriptorSet.cpp, Library/Core/Private/RHI/Vulkan/VulkanDevice.h, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Library/Core/Private/RHI/Vulkan/VulkanPipeline.cpp, Test/Core/Rendering/RHIDescriptorArrayVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 危険地帯（RHI公開API・Vulkan）。独立評価を通す。

## R7-P3B: PTの材質snapshot・texture・シェーディング法線を接続する
- status: done
- done-when: `RayTracingHitMaterialSnapshot`へGBufferと同じ規則のinstance色（custom dataの非0成分、既定1）とalbedo・normal・metallic・roughnessのtexture handleを値として加える。PTはRenderThreadでtexture handleを解決し、重複を除いた配列descriptorへ束ね、未設定はGBufferと同じ既定値（白・平坦法線・metallic 0・roughness中間灰）にする。closest-hitは`Mesh3DVertex`の法線・UVを重心補間し、共通shaderの材質評価でalbedo・法線マップ・metallic・roughnessを得る。発光はGBuffer同様`色×nits`にpre-exposureを掛ける。既存のposition-only頂点（stride 12）は幾何法線・UV 0へfallbackする。GPUテストでtexture UV標本化、metallic/roughness snapshot、既定値、instance色、発光のpre-exposureをreadbackで固定し、既存PT/屋外/霧/カメラ/EXRテストに回帰がない。
- verify: `cmake --build build --config Debug --target Game PathTracingVulkanTest PathTracingMaterialVulkanTest PathTracingOutdoorVulkanTest PathTracingVolumetricTest PathTracingCameraVulkanTest PathTracingExrOutputTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingVulkanTest|PathTracingMaterialVulkanTest|PathTracingOutdoorVulkanTest|PathTracingVolumetricTest|PathTracingCameraTest|PathTracingCameraVulkanTest|PathTracingExrOutputTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: texture資源の寿命をFramePacketとRenderResourcesの既存所有境界で保証できない、またはラスタgoldenが変わる場合は、材質APIを広げず不足を記録する。
- paths: Library/Core/Public/Rendering/MaterialTypes.h, Library/Core/Public/Rendering/PathTracingPass.h, Library/Core/Private/Rendering/PathTracingPass.inl, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Assets/Shaders/PathTracing, Assets/Shaders/Common/PbrMaterialEvaluation.glsl, Assets/Shaders/gbuffer.frag, Test/Core/Rendering/PathTracingVulkanTest.cpp, Test/Core/Rendering/PathTracingMaterialVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（Rendering公開API・RT資源寿命）。独立評価を通す。

## R7-P3C: NEE/MISとpoint・spot・directional・area light・環境光輸送を実装する
- status: done
- done-when: FramePacketのLightProxyからラスタと同じ単位・減衰・spot円錐の光源表をPTへ渡し、delta lightはNEEだけ（weight 1）、発光三角形と太陽円盤はlight sampleとBSDF sampleをpower heuristic β=2で合成する。BSDF sampleは拡散cosineとGGX可視法線分布の混合で、PDFと共通BRDF評価を同じ方向で整合させる。環境光（一様値またはHDR equirect）はBSDF sampleで評価し、固定0.05の仮背景を廃止する。GPUテストで、R1白炉と同じ15行（roughness 5段×metallic 3段、平均相対誤差1%・最大3%）、既知cdの点光源によるLambert面の解析輝度、面光源のNEEのみ・BSDFのみ・MISが同じ期待値へ収束すること、spot円錐外0、directional照度を固定seedで検証する。
- verify: `cmake --build build --config Debug --target Game PathTracingLightingVulkanTest PathTracingVulkanTest PathTracingOutdoorVulkanTest PathTracingVolumetricTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingLightingVulkanTest|PathTracingVulkanTest|PathTracingMaterialVulkanTest|PathTracingOutdoorVulkanTest|PathTracingVolumetricTest)$"`
- stop-when: 白炉・解析照明が共通BRDFの近似に起因して閾値を超える場合は、閾値を緩めず、PT推定量の誤りと共通BRDFのエネルギー差を分けて記録する。
- paths: Library/Core/Public/Rendering/PathTracingPass.h, Library/Core/Private/Rendering/PathTracingPass.inl, Assets/Shaders/PathTracing, Assets/Shaders/Common, Test/Core/Rendering/PathTracingLightingVulkanTest.cpp, Test/Core/Rendering/PathTracingVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（シェーダーパイプライン・RenderThread）。独立評価を通す。

## R7-P3D: 起動時PT選択とラスタ/PT同一シーン比較を固定する
- status: done
- done-when: RenderingCoordinatorの初期化設定でmain SceneViewをPT pipelineにでき（既定はraster、`Rendering3DTest`起動は不変）、PTはLightingPassと同じ環境マップ設定と検証用一様環境（debug mode 252）を使う。描画検証appに`--renderer=path-tracing`と累積試料数の指定を加え、capture時の実累積試料数をcapture結果へ記録する。R1の白炉と解析点光源の行をPTで実行して同じ評価関数に通し、同じ行のラスタSceneColorとPT SceneColorの差を事前に固定した閾値で比較する。
- verify: `cmake --build build --config Debug --target Game RenderingHdrSceneCaptureTest PathTracingRasterParityVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingRasterParityVulkanTest|RenderingHdrSceneCaptureVulkanTest.*)$"`
- verify: `build\\Game\\Debug\\Game.exe --imgui --exit-after-rendered-frames=120`
- stop-when: PT選択がRenderThreadのpass寿命やFramePacket境界を変えないと成立しない場合は、実行時切替を追加せず起動時選択の不足を記録する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（RenderThread・pass寿命）。独立評価を通す。

## FIX-NORMAL-MATRIX-SCALE: 法線行列の小スケール退化判定を尺度不変にする
- status: backlog
- done-when: `MatrixUtils::CreateNormalMatrix`が一様スケール約0.005未満の物体にも逆転置を返し（特異かどうかは尺度に対する比で判定）、R1室内フィクスチャの平面メッシュの頂点法線を幾何と一致させ、PTの閉包命中シェーダから同じ退化規則の写しを外す。R1数値検証、Indoor/Outdoor golden、`PathTracingRasterParityVulkanTest`が変わらず通る。
- verify: `cmake --build build --config Debug --target Game RenderingHdrSceneCaptureTest PathTracingRasterParityVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 承認済みgoldenやR1の数値が変わる場合は、基準の更新を提案して止まる。
- paths: Library/Core/Public/Math, Assets/Shaders/PathTracing, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: R7-P3Dで発見。3x3の行列式の絶対値がFLT_EPSILON未満だと単位行列になり、回転した小さな物体の法線が回らない。R1室内の平面はこの規則を前提に頂点法線を+Zへ書き換えているため、PTも同じ規則でラスタと揃えている。

## R7-P4: SPP収束とCornell参照を固定する
- status: done
- done-when: 同じseedのnested 16/64/256 spp prefixで256 spp自己収束画像に対するMSEが単調減少し、Cornell boxが固定公開参照と規定誤差内で一致する。
- verify: `cmake --build build --config Debug --target PathTracingConvergenceVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingConvergenceVulkanTest$"`
- stop-when: monotonic結果がseed探索だけに依存する場合、閾値を緩めずsample estimatorと測定手順を再検討する。
- paths: Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- result: 収束は合格（入れ子の接頭列のMSEが全体と64区画すべてで単調、MSE比の中央値4.374、引き直しでbyte一致）。公開RGBEは色の符号化が公開されておらず色の壁の彩度を再現できないため、ユーザーの判断で輝度の判定を白い面・影・発光面の位置に限り、赤・緑の壁とR4の赤・緑ROIは優勢色度で判定する範囲へ見直した（数値の上限は事前固定のまま）。影ROI 0.0025、赤・緑ROIの色度差0.0044・0.0217、壁の優勢な成分が一致、発光面のずれ1画素、580区画の中央値0.0101・90%点0.0340で合格。記録は`Docs/RenderingValidation/R7CoreAcceptance.md`。

## R7-P5: thin-lensとshutter time samplingを実装する
- status: done
- done-when: FramePacketのcurrent/previous camera・geometry snapshotからshutter時刻を決定論的に評価し、薄レンズCoCの数値誤差と静止/移動goldenを固定する。
- verify: `cmake --build build --config Debug --target Game PathTracingCameraTest PathTracingCameraVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingCameraTest|PathTracingCameraVulkanTest)$"`
- stop-when: 変換補間が不正なshear/scaleを生む場合、live World参照や別のmotion systemを追加せず対応可能なtransform範囲を定義する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-P6: seed固定EXR sequence出力を実装する
- status: done
- done-when: TinyEXR v3 C APIでlinear RGB float/ZIP scanlineを出力し、NaN/Infを拒否する。同じscene/seed/SPP/frameの2出力がbyte一致または事前閾値内である。
- verify: `cmake --build build --config Debug --target Game PathTracingExrOutputTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingExrOutputTest$"`
- stop-when: independent EXR readerがchannel/precision/windowを一致して読めない場合、sequence APIを広げずwriter integrationを修正する。
- paths: CMakeLists.txt, Library/ThirdParty, Library/Core, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-P7: R7 coreを受入れ、R4/R6暫定参照を再照合する
- status: done
- result: `Docs/RenderingValidation/R7CoreAcceptance.md`（2026-09-25）。R6は自前PT（拡散2バウンス、median of means）と閾値内で合格、R4は赤ROI 0.257（上限0.25）で更新ルール5により再オープン（R4-REOPEN）。全体CTestは`.harness/runs/20260925-r7-gate/ctest-all.txt`。
- done-when: R7 coreの完了条件、公開Cornell参照、R1数値検証、CoC、決定論EXR、既知制限を集約し、同一条件のself PTでR4 DDGIとR6 RTGIを各1回再照合する。Outdoor extension完了前にR7 trailerは付けない。
- verify: R7 core関連Debug build/CTestとEXR finite-scanを実行し、全出力ログを開いて閾値結果を確認する。
- verify: `git diff --check`
- stop-when: R4/R6比較がRoadmap閾値を超えた場合、phaseを完了扱いにせず再オープン理由と再検証単位を記録する。
- paths: Docs/RenderingValidation, Test/Core/Rendering, TASKS.md, PROGRESS.md

## RTGI-HIT-SPECULAR: RTGIの命中面を光沢のある反射でも照らす
- status: backlog
- done-when: RTGIの命中面の直接光が材質の粗さ・金属度の鏡面葉を含み、R7屋外比較の既知差（夕の緑の球の影側の面。命中点をLambertにしたPTとは一致）が全輸送との比較で縮む。R6・R7の参照比較と既存goldenが通る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RTGIDiffuseIndirectVulkanTest|R6RTGIPathTracingReferenceVulkanTest|R7OutdoorPathTracingReferenceVulkanTest)$"`
- stop-when: 命中面の材質をRTのsnapshotへ持たせる方法（粗さ・金属度のtextureの扱い）に設計判断が要る場合はユーザーへ戻す。判定の参照の輸送範囲を変える場合もユーザーへ戻す。
- paths: Assets/Shaders/RTGI, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering
- notes: 危険地帯（RTGI）。R7-O3の既知差2。命中面の反射率は`3c80458`でinstance色にした。

## RTGI-MULTI-BOUNCE: 接地部の3回目以降のバウンスをRTGIで扱う
- status: backlog
- done-when: 球の下の接地部で、ラスタの間接光と全輸送のPTの差（拡散2バウンスの約1.5倍）が縮む。RTGIの光線数の増え方を記録し、R6・R7の参照比較と既存goldenが通る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R6RTGIPathTracingReferenceVulkanTest|R7OutdoorPathTracingReferenceVulkanTest)$"`
- stop-when: 光線数の増加が大きい、または方式（放射輝度の再利用・probe併用など）の選択が要る場合はユーザーへ戻す。
- paths: Assets/Shaders/RTGI, Library/Core/Private/Rendering, Test/Core/Rendering
- notes: 危険地帯（RTGI）。R7-O3の既知差1。

## FIX-CSM-TERMINATOR: 球の明暗境界でCSMの可視が数画素かけて下がるのを直す
- status: backlog
- done-when: 屋外シーンの球の明暗境界で、ラスタのCSMの可視（検証表示245）がPTの可視（1画素で1→0）に近づき、R7屋外比較で影の縁として除く画素が減る。承認済みgoldenが変わる場合はユーザーの承認を得る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R7OutdoorPathTracingReferenceVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: goldenの再承認が要る場合はユーザーへ戻す。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering
- notes: R7-O3の既知差3。例: 夕の緑の球で0.96→0.65→0.48→0.18（PTは1→0）。法線方向のずらし（normal offset）や比較の余裕の見直しが候補。

## FIX-GRAZING-IBL-SPECULAR: 斜めから見た地面のIBLの鏡面反射が強すぎる原因を調べる
- status: backlog
- done-when: 昼の屋外シーンの地平線近くの地面で、ラスタのIBLの鏡面反射による間接光（全輸送の約1.5倍）の原因（split-sumの近似、地平線より下の環境、DFGの補償など）を特定し、直すか既知差として根拠を記録する。
- verify: R7屋外比較の`_mean_luminance`と地平線近くの領域の比を開いて確認する。
- stop-when: 承認済みgoldenが変わる場合はユーザーへ戻す。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering
- notes: R7-O3の既知差4。朝・夕は1.07〜1.11倍。

## FIX-CSM-MEGA-CASTER-BOUNDS: CSMの遮蔽物の境界球にShadowMapPassが描かないMegaGeometryを含めない
- status: backlog
- done-when: CSMの深度範囲に含める境界球が、影の地図に実際に描く物体と一致する（MegaGeometryを影に描くまでは含めない、または描くようにする）。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(DirectionalShadowLightMatricesTest|CascadedShadowLightMatricesTest)$"`
- stop-when: MegaGeometryを影に描くかの判断が要る場合はユーザーへ戻す。
- paths: Library/Core/Private/Rendering, Test/Core/Rendering
- notes: `382489f`の評価のnon-blocking指摘。過大収集で深度範囲とPCSSの探索半径が広がるだけで、影は欠けない。

## TEST-FULL-CTEST-BASELINE: 全体CTestの既存の失敗を直す
- status: backlog
- done-when: 全体CTestで、R7の作業前（`c9a3e33`）から失敗している次のテストが通るか、失敗の理由と扱いが記録される: `VolumetricsPassContractTest`（霧の設定行の文字列）、`RenderResourcesDomainContractTest`（`WaitIdleWithoutResultCheck(`の数3、期待2）、`ViewportCameraIdRenderPlanTest`・`BoardComponentRoutingTest`・`SkeletalFramePacketSnapshotTest`（GPUデバイスのないテストでRenderingCoordinator::GenerateDrawCommandsが`m_Device->GetCapabilities()`をnull参照、`578236d`以来）、`FrameCaptureReadbackHelperTest`・`ComponentDataRegistryTest`・`WorldSyncDifferentialTest`（WorldTransformの777）・`CanvasViewRenderTest`・`RenderGraphTextureUsageContractTest`（ShadowMapPassの初期化失敗）（Debugのassertの対話窓で止まりtimeout）、`M9WorldAcceptanceTest`（負の対照の画素差）。
- verify: `ctest --test-dir build -C Debug --output-on-failure --timeout 600`
- stop-when: テストの期待を変える必要がある場合は理由を記録してユーザーへ戻す。
- paths: Library/Core, Test/Core, Game
- notes: 2026-09-25の全体gate（`.harness/runs/20260925-r7-gate/`）で確認。SkinnedRenderPathContractTestはTEST-SKINNED、R4はR4-REOPEN。

## R7-O1: R2 SkyAtmosphereをPT miss radianceとsolar samplingへ接続する
- status: done
- done-when: rasterと同じSkyAtmosphereParametersからPTのsky miss radianceとsolar disk direct lightingを構成し、朝/昼/夕3時刻の有限性・parameter parityを検証する。
- verify: `cmake --build build --config Debug --target Game PathTracingOutdoorVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingOutdoorVulkanTest$"`
- stop-when: R2 parameter semanticsを変更しないと接続できない場合は、R2を先行修正しgoldenを書き換えない。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-O2: R3 volumetric fogをPT ray transportへ接続する
- status: done
- done-when: rasterと同じfog density/height/anisotropy parameterからfinite transmittanceとsingle-scatteringを評価し、fog/sky disabled fallbackを維持する。
- verify: `cmake --build build --config Debug --target Game PathTracingVolumetricTest PathTracingOutdoorVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingVolumetricTest|PathTracingOutdoorVulkanTest)$"`
- stop-when: R3 public parameter semanticsを拡張しないと一致しない場合はR3側契約差として記録し、RenderingからVulkanを参照しない。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## SKY-SUN-P1: 空の太陽の地表照度と方向光をCPUで一つに定める
- status: done
- result: `fa40e74`。透過率・地表照度・空の太陽の方向光をSkyAtmosphere/SkySunLightの公開関数にし、透過率LUTも同じ関数で作る（既存LUT値は不変）。
- done-when: 公開APIで、空のパラメータから地表での大気の透過率（RGB、透過率LUTと同じ式・同じ定数）と、太陽の地表照度（太陽円盤の照度×透過率、RGB）と、空の太陽を表す方向光（予約LightId、方向=太陽方向の逆、色=透過率、強度=照度の輝度、影あり）を求められる。空が無効なら方向光を作らない。透過率LUTの生成は同じ関数を使い、既存LUTの値は変わらない。
- verify: `cmake --build build --config Debug --target SkyAtmosphereModelTest SkyAtmospherePassContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SkyAtmosphereModelTest|SkyAtmospherePassContractTest|SkyAtmosphereIblTest)$"`
- stop-when: LUTの既存値が変わる場合は共有化を止め、同じ式の複製とLUTとの一致テストへ切り替える。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: ユーザー決定（2026-09-25）「空の太陽に統一」。空が有効なら空の太陽からエンジンが方向光を作り、ラスタはCSMの影付きで照らし、PTは同じ太陽を円盤の光源標本で数える（二重に数えない）。シーンの方向光は追加の光として残す。既定の起動は空が無効で変わらない。

## SKY-SUN-P2: 空の太陽をFramePacketの光源へ加え、ラスタの影をその灯へ掛ける
- status: done
- result: `4abdbcd`。FramePacket作成で空の太陽を1つだけ加え、CSM・RT影・LightingPassが同じ選び方（`603f366`で専用ヘッダへ分離）で影の灯を決める。
- done-when: GameThreadのFramePacket作成で、空が有効なら空の太陽の方向光を光源表へ1つだけ加える（再利用するpacketでも重複しない）。影を落とす方向光の選択は、空の太陽があればそれを選び、なければ従来どおり「表示される方向光がちょうど1つで影あり」のときだけ選ぶ（CSM・RT影で共通の関数）。ラスタ（lighting.frag・forward_transparent.frag）はCSM/RT影をその灯にだけ掛け、他の方向光は影なしで照らす。空が無効なシーンの描画と既存golden・閾値は変わらない。
- verify: `cmake --build build --config Debug --target Game CascadedShadowLightMatricesTest DirectionalShadowLightMatricesTest LightingLightBufferTest LightingParamsLayoutTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CascadedShadowLightMatricesTest|DirectionalShadowLightMatricesTest|LightingLightBufferTest|LightingParamsLayoutTest|RenderingRayTracingShadowVulkanTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 空が無効なシーンのgoldenが変わる場合は止めて原因を直す。承認済みgoldenの更新が必要になったらユーザーへ戻す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（光源・CSM・RT影・RenderThread）。影の係数は現在すべての方向光に同じ値を掛けるため、方向光が2つになるとCSMもRT影も止まる。影を掛ける灯は光源データの未使用の成分で示し、UBOの配置は変えない。

## SKY-SUN-P3: PTの太陽を地表照度（透過率込み、RGB）で数え、方向光と二重にしない
- status: done
- result: `fc7e985`・`e42a185`。PTの太陽円盤はRGBの地表照度で数え、空の太陽の方向光は点・spot・方向光の評価から除く。
- done-when: PTの太陽円盤の光源標本と2次以降の円盤命中のMISが、SKY-SUN-P1の地表照度（RGB）を使う。PTの点・spot・方向光の評価は空の太陽の方向光を除き、霧の単一散乱はラスタと同じ灯（CSMの灯）を使う。空を有効にした同じシーンで、ラスタの空の太陽の方向光とPTの太陽が同じ照度になる。R7-O1の屋外テストの期待値は同じ公開関数から求める。
- verify: `cmake --build build --config Debug --target Game PathTracingVulkanTest PathTracingLightingVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingOutdoorVulkanTest|PathTracingVolumetricTest|PathTracingLightingVulkanTest|PathTracingVulkanTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 空が無効なPTの基準（R6/R7参照比較、R1白炉・解析照明）が変わる場合は止めて原因を直す。
- paths: Library/Core/Private/Rendering, Assets/Shaders/PathTracing, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（PT・光源）。現状のPTの太陽照度はスカラーで大気の透過率を掛けていない（`PathTracingPass.inl`の`SkyState[1]`）。

## R7-O3: 屋外PT/raster相互比較を受入れR7を完了する
- status: done
- result: `Docs/RenderingValidation/R7OutdoorAcceptance.md`（2026-09-25）。3時刻とも閾値内（朝 平均0.0107/0.0274・一致画素最大0.0989/0.1293・区画0.0368/0.0682、昼 0.0153/0.0189・0.0937/0.1704・0.0576/0.0668、夕 0.0127/0.0238・0.0903/0.1367・0.0376/0.0583）。判定の参照はラスタが実装する輸送（拡散2バウンス）のPT、median of means、影の縁の除外と影の内側の負の対照（ユーザーの判断）。全輸送との差は既知差として記録。
- progress (2026-09-25): 比較テスト`6d7628f`。ラスタ側の修正: 低い太陽の影（`fdbc5dc`）、空の照明を地表から見た空へ（`6753496`）、CSMの深度範囲の向き（`6f1dc0b`）と遮蔽物の境界球（`382489f`）、影の余裕（`d3f254a`）、カスケード境界（`fa00d84`）、RTGI/DDGIの命中面の反射率（`3c80458`）、直接光のAO（`49116c3`）、RTGIの拡散2バウンス（`44d9a3a`、ユーザー決定）。Outdoor goldenは`efce943`で再承認。
- progress: 3時刻ともFLIP平均は閾値内（朝0.0127/0.0277、昼0.0168/0.0196、夕0.0163/0.0245）。8×8区画最大は朝のみ閾値内（昼0.0712/0.0709、夕0.0988/0.0736）、画素単位最大は3時刻とも超過（閾値を超える一致画素 朝17・昼33・夕182）。上位200画素の内訳（昼/夕）は影の縁134/152、3回目以降のバウンス64/21、その他2/27（夕の緑の球の影側の面）。ログは`.harness/runs/20260925-rtgi-2bounce/`。
- done-when: sky/volume込みの屋外sceneでPTとrasterを3時刻比較し、FLIP pool/max-pixel threshold内であり、既知近似差を記録する。R7 core + outdoor extension受入れcommit末尾に`RenderingRoadmap: R7 complete`を付ける。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingOutdoorVulkanTest$"`
- verify: relevant R7 Debug build/CTestと3時刻captureの数値レポートを開いて確認する。
- stop-when: 3時刻の一部で閾値超過、NaN/Inf、未解決のR7 core acceptanceが残る場合、完了trailerを付けない。
- paths: Docs/RenderingValidation, Test/Core/Rendering, TASKS.md, PROGRESS.md

## G1-GR08-P1: カプセルの形状間分離距離を追加する
- status: done
- done-when: Capsule 対 Sphere/OBB/Capsule の分離距離・法線・表面点・侵入判定を解析解で確認し、既存接触判定を維持する。
- verify: g++ -std=c++23 -I Library/Core/Public Test/Core/Math/GeometrySeparationTest.cpp Library/Core/Private/Math/GeometryIntersection.cpp -o /tmp/norves-separation && /tmp/norves-separation
- stop-when: 描画への変更、未承認判断、公開契約のblocking指摘が残る場合。
- paths: Library/Core/Public/Math, Library/Core/Private/Math/GeometryIntersection.cpp, Test/Core/Math, TASKS.md, PROGRESS.md
- notes: Windows/Core全体/CTest/GPUの検証は別。既存SS/R/FIX項目の状態と順序は変更しない。

## G1-GR08-P3M: 球とカプセルの並進掃引の数学部分を追加する
- status: done
- done-when: 球/カプセル対球/OBB/カプセルの掃引を解析例と独立接触oracleで確認し、初期重なり・未収束・無効入力を区別する。
- verify: g++ -std=c++23 -O2 -I Library/Core/Public Test/Core/Math/GeometrySweepTest.cpp Library/Core/Private/Math/GeometryIntersection.cpp -o /tmp/norves-sweep && /tmp/norves-sweep
- stop-when: 未承認の意味変更、描画変更、安全側の進行または検証が成立しない場合。
- paths: Library/Core/Public/Math, Library/Core/Private/Math/GeometryIntersection.cpp, Test/Core/Math, TASKS.md, PROGRESS.md
- notes: P1に依存。Physics統合前のCPU数学だけを先行する。未収束を確定ヒットへ変換しない。

## G1-GR08-P2A: 物理クエリの値型とフィルタ契約を用意する
- status: done
- done-when: 既存enum/handleを維持し、OS非依存の値型とレイヤー・trigger・ignore・ペアマスクの判定を試験する。
- verify: g++ -std=c++23 -I Library/Core/Public Test/Core/Object/PhysicsQueryTypesTest.cpp -o /tmp/norves-query-types && /tmp/norves-query-types
- stop-when: 既存型の意味変更、循環依存、公開契約のblocking指摘が残る場合。
- paths: Library/Core/Public/Scene, Library/Core/CMakeLists.txt, Test/Core/Object, TASKS.md, PROGRESS.md
- notes: 実クエリ・コライダーへの接続はP2B。ゲーム固有のレイヤー名は定義しない。

## G1-GR08-P2B: 単一物理プロキシへ統合クエリを追加する
- status: done
- done-when: 実プロキシにray/overlap/sweepを適用し、フィルタ・UserData・法線方向・無効入力・未収束の契約を検証する。
- verify: g++ -std=c++23 -O2 -ffunction-sections -fdata-sections -I Library/Core/Public -I Library/Modules/Physics/Private Test/Modules/Physics/PhysicsProxyQueryTest.cpp Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp Library/Core/Private/Math/GeometryIntersection.cpp -Wl,--gc-sections -o /tmp/norves-proxy-query && /tmp/norves-proxy-query
- stop-when: 描画/OS本体の変更、未承認判断、公開契約のblockingが残る場合。
- paths: Library/Core/Public/Scene, Library/Modules/Physics/Private/Physics/PhysicsBroadphase.h, Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp, Test/Modules/Physics, TASKS.md, PROGRESS.md
- notes: 単一proxyの実コードの検証。コライダー設定/複数proxy集約/SceneQuery接続は後続。

## G1-GR08-P2C: 物理クエリの複数ヒットを集約する
- status: done
- done-when: RaycastClosest/All、overlap/sweepの決定的順序、MaxHits、空/失敗/未収束の出力契約を実コードで検証する。
- verify: g++ -std=c++23 -O2 -ffunction-sections -fdata-sections -I Library/Core/Public -I Library/Modules/Physics/Private Test/Modules/Physics/PhysicsProxyQueryTest.cpp Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp Library/Core/Private/Math/GeometryIntersection.cpp -Wl,--gc-sections -o /tmp/norves-proxy-query && /tmp/norves-proxy-query
- stop-when: 既存動作変更、描画/未承認判断、blocking指摘が残る場合。
- paths: Library/Modules/Physics/Private/Physics/PhysicsBroadphase.h, Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp, Test/Modules/Physics/PhysicsProxyQueryTest.cpp, TASKS.md, PROGRESS.md
- notes: spanの実集約コードを検証。VariableArrayの実メモリシステム結合とEngine起動は未検証。

## G1-GR01-P1: 固定更新群とComponentの更新設定を追加する
- status: done
- done-when: 固定8群、既定Default/priority0、群maskの不正値拒否、Componentの既定OnTickGroup契約を確認する。
- verify: g++ -std=c++23 -I Library/Core/Public Test/Core/Object/TickGroupConfigurationTest.cpp -o /tmp/norves-tick-group && /tmp/norves-tick-group
- stop-when: 未承認方式変更、既存Tickの意味変更、公開契約のblockingが残る場合。
- paths: Library/Core/Public/Component, Library/Core/Private/Component/Component.cpp, Library/Core/CMakeLists.txt, Test/Core/Object, Docs/Architecture/TickStages.md, TASKS.md, PROGRESS.md
- notes: World実行順への接続は後続。個別の依存連携はDelegateで扱う。

## G1-GR01-P2: Worldの段階更新と更新中の遅延破棄を接続する
- status: done
- done-when: 一回収集と前後段/Fixedの安全な実行、翌frame追加/設定反映、破棄前entry無効化、旧fixedstepcleanup順を保つ。
- verify: g++ -std=c++23 -I Library/Core/Public Test/Core/Object/TickDispatchTest.cpp -o /tmp/norves-tick-dispatch && /tmp/norves-tick-dispatch
- verify: WindowsでEntityTreeTestをビルドしWorldTickGroupTestを実行（現環境では未実行）。
- stop-when: 寿命の未解消問題、既存fixedstepcleanup回帰、scope外変更、blockingが残る場合。
- paths: Library/Core/Public/Component, Library/Core/Private/Component/Component.cpp, Library/Core/Public/Object/World.h, Library/Core/Public/Object/Entity.h, Library/Core/Private/Object/World.cpp, Library/Core/Private/Object/Entity.cpp, Library/Core/CMakeLists.txt, Test/Core/Object, Docs/Architecture/TickStages.md, TASKS.md, PROGRESS.md
- notes: ApplicationのLateTick配線とカメラ移設は後続。個別依存のgraphは作らない。

## G1-GR01-P3: シミュレーションの物理後更新を実フレームへ接続する
- status: done
- done-when: 固定0回でも後段が走り、pauseでは止まる。World→Module→Handlerの後段順をSync前に置き、既存fixedstep cleanup/外側処理を保つ。
- verify: WindowsでApplicationFixedStepPipelineTestをビルド・実行（現環境では未実行）。
- stop-when: 既存fixedstep/ポーズ/外側フレームの回帰、handler寿命の問題、blockingが残る場合。
- paths: Library/Core/Public/Application, Library/Core/Public/Module, Library/Core/Private/Module/ModuleRegistry.cpp, Library/Core/Public/Engine/ApplicationProcessor.h, Library/Core/Private/Engine/ApplicationProcessor.cpp, Test/Core/Engine/ApplicationFixedStepPipelineTest.cpp, Docs/Architecture/TickStages.md, TASKS.md, PROGRESS.md
- notes: コードの順序・gateは静的確認、実統合の合格は未主張。

## G1-GR01-P4: 評価済みボーン姿勢をゲーム側へ公開する
- status: done
- done-when: Animation/PoseFinalizeで評価し、model/world行列と名前引き/serialを公開。旧paletteを保持し、無効資産や表現不能なTransformで成功扱いしない。
- verify: SkeletalAnimationSamplingTestの既存bundleへreadback/cache/lookupケース追加（Windows未実行）。
- stop-when: 行列規約不整合、未解決のcache寿命、資産形式変更、blockingが残る場合。
- paths: Library/Core/Public/Animation, Library/Core/Private/Animation, Library/Core/Public/Component/SkinnedMeshComponent.h, Library/Core/Private/Component/SkinnedMeshComponent.cpp, Test/Core/Rendering/SkeletalAnimationSamplingTest.cpp, Docs/Architecture/TickStages.md, TASKS.md, PROGRESS.md
- notes: Owner子階層は既存Worldの変換確定境界が鮮度の前提。

## G1-GR01-P5: SpringArmとGameカメラを物理後へ移す
- status: done
- done-when: Camera群へ割当、GameHandlerのone-shot Delegateでlate proxy確定、weak寿命とID再解決で途中破棄を拒否、既存初期camera同期を保持する。
- verify: 既存SpringArmComponentTestへ群/固定0・1・2step/child変換/途中削除/one-shot寿命ケース追加（Windows未実行）。
- stop-when: callback寿命未解消、mode停止時の誤更新、初期同期破壊、blockingが残る場合。
- paths: Library/Core/Private/Component/SpringArmComponent.cpp, Game/CameraLateUpdate.h, Game/GameApplicationHandler.h, Game/GameApplicationHandler.cpp, Game/GameModes/Rendering3DTest/Rendering3DTestData.h, Game/GameModes/Rendering3DTest/Rendering3DTestRoutine.cpp, Test/Core/Object/SpringArmComponentTest.cpp, Docs/Architecture/TickStages.md, TASKS.md, PROGRESS.md
- notes: Game専用single camera slotを使い、Coreのstate-machineや依存graphは広げない。

## G1-GR03-P1: 入力状態の一括解除と復帰時の座標基準を追加する
- status: done
- done-when: 全キー/ボタン解除、同frame押下後もReleasedを一frame保持、累積クリア、次の絶対位置の再基準化、通常遷移の維持。
- verify: g++ -std=c++20 -I Library/Core/Public Test/Core/Input/InputStateReleaseTest.cpp Library/Core/Private/Input/InputState.cpp -o /tmp/input-release && /tmp/input-release
- stop-when: 通常入力回帰、release永続、OS依存の持込み、blocking未解消。
- paths: Library/Core/Public/Input/InputState.h, Library/Core/Private/Input/InputState.cpp, Test/Core/Input/InputStateReleaseTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Window/InputSystemへのfocus配線は後続。S4=a/S5=a/S8=aは作者承認済み。

## G1-GR03-P2: 入力軸の応答曲線と視点の時間単位を分離する
- status: done
- done-when: 1D/2D radial deadzoneとLinear/Power/Expo、方向/単調/長さ、mouse変位とstick速度のdt分離、無効値の安全な拒否。
- verify: g++ -std=c++20 -I Library/Core/Public Test/Core/Input/InputAxisMathTest.cpp -o /tmp/input-axis && /tmp/input-axis
- stop-when: 方向を曲げる成分別曲線、mouseへのdt適用、NaN漏れ、blocking未解消。
- paths: Library/Core/Public/Input/InputAxisMath.h, Library/Core/CMakeLists.txt, Test/Core/Input/InputAxisMathTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: 後続Mapperが使用する純ロジック。EngineやOS入力との接続は未完。

## G1-GR03-P3: ボタンの時間遷移と固定更新用の押下保持を追加する
- status: done
- done-when: 単調実時間でHold/Tap/DoubleTapを判定、同frameedgeを保持、fixedPressをconsumeまで維持、Cancelで遅延発火を消す。
- verify: g++ -std=c++20 -I Library/Core/Public Test/Core/Input/InputButtonStateTest.cpp -o /tmp/input-button && /tmp/input-button
- stop-when: 時刻/edge不整合、不正入力で状態変更、Cancel後の発火、押下消失、blocking未解消。
- paths: Library/Core/Public/Input/InputButtonState.h, Library/Core/CMakeLists.txt, Test/Core/Input/InputButtonStateTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: binding集約後のdownを受ける純ロジック。Mapper/OS通知への接続は後続。

## G1-GR03-P4A: 入力解除の世代で古い押下許可を失効させる
- status: done
- done-when: 到達Pressedだけarmed、release serialで消費された解除と同frame再押下を検出、Repeat/修飾/Resetを安全に扱う。
- verify: g++ -std=c++20 -I Library/Core/Public Test/Core/Input/InputArmedStateTest.cpp Library/Core/Private/Input/InputState.cpp -o /tmp/input-armed && /tmp/input-armed
- stop-when: UI消費入力で再armed、既存state回帰、OS/Delegate購読持込み、blocking未解消。
- paths: Library/Core/Public/Input/InputState.h, Library/Core/Public/Input/InputArmedState.h, Library/Core/Private/Input/InputState.cpp, Library/Core/CMakeLists.txt, Test/Core/Input, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Router/Mapper接続前のkernel。実InputStateとcallback欠落を使った状態検証であり、ImGui実機の合格ではない。

## G1-GR03-P4B: 入力元とアクションbindingの値型契約を定義する
- status: done
- done-when: backend非依存pad stateとphysical source/bindingを宣言し、code/slot/target/modifier/nonfiniteを検証、変位と正規化の暗黙混在を拒否。
- verify: g++ -std=c++20 -I Library/Core/Public Test/Core/Input/InputBindingTypesTest.cpp -o /tmp/input-bindings && /tmp/input-bindings
- stop-when: WindowsSDK依存、不正値accept、既存名衝突、backend動作済み扱い、blocking未解消。
- paths: Library/Core/Public/Input/GamepadTypes.h, Library/Core/Public/Input/InputBindingTypes.h, Library/Core/CMakeLists.txt, Test/Core/Input/InputBindingTypesTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: XInput adapter/Mapper/JSONは未接続。4slot選定はS5=a承認に基づく。

## G1-GR03-P4C: パッド入力の正本と解除履歴を入力状態へ接続する
- status: done
- done-when: Pad snapshotを検証後一括適用、frame edge/解除serial/neutralと接続維持を実装し、armedのUI消費/Repeat失効をPadへ拡張する。
- verify: g++ -std=c++20 -I Library/Core/Public Test/Core/Input/GamepadInputStateTest.cpp Library/Core/Private/Input/InputState.cpp -o /tmp/pad-state && /tmp/pad-state
- stop-when: 半端な更新、切断後down/armed、KBM回帰、SDK依存、blocking未解消。
- paths: Library/Core/Public/Input/GamepadTypes.h, Library/Core/Public/Input/InputState.h, Library/Core/Public/Input/InputArmedState.h, Library/Core/Private/Input/InputState.cpp, Test/Core/Input/GamepadInputStateTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: XInput polling/振動/InputSystem配送は未接続。Mapper用の正本契約を先に満たす。

## G1-GR03-P4D: アクションとコンテキストの設定をIdentityで所有する
- status: done
- done-when: 純settings検証とIdentity設定所有を実装し、不正/重複/unknownの非変更、deep copy、空unbind、借用viewの寿命を契約化する。
- verify: g++ -std=c++20 -I Library/Core/Public Test/Core/Input/InputActionSettingsTest.cpp -o /tmp/input-settings && /tmp/input-settings
- verify: InputBindingSetTestはWindowsのLoggerSinkTest bundleで実行（現環境では未実行）。
- stop-when: 無効変更の部分適用、長期pointer、String key、OS設定変更混入、blocking未解消。
- paths: Library/Core/Public/Application/CursorMode.h, Library/Core/Public/Input/InputActionSettings.h, Library/Core/Public/Input/InputBindingSet.h, Library/Core/Private/Input/InputBindingSet.cpp, Library/Core/CMakeLists.txt, Test/Core/Input, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Mapper/JSON/OSカーソルは未接続。

## G1-GR03-P4E: バインドからボタンと軸のアクション値を評価する
- status: done
- done-when: 到達入力/armedとbindingからOR button、curve軸、相対変位とrate時間単位を評価し、span非保持、cancel/fixed latch、不正入力非変更を守る。
- verify: g++ -std=c++20 -I Library/Core/Public Test/Core/Input/InputActionRuntimeTest.cpp Library/Core/Private/Input/InputState.cpp -o /tmp/input-runtime && /tmp/input-runtime
- stop-when: UI迂回、span長期保持、dt混同、部分適用、blocking未解消。
- paths: Library/Core/Public/Input/InputActionRuntime.h, Library/Core/Public/Input/InputAxisMath.h, Library/Core/CMakeLists.txt, Test/Core/Input/InputActionRuntimeTest.cpp, Test/Core/Input/InputAxisMathTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Identity/Router/Engineの接着は後続Mapper。

## G1-GR03-P4F: Raw・パッド・解除の入力配送を接続する
- status: done
- done-when: raw相対移動と水平wheelを独立正本へ保持しRouterへ配送、pad snapshotを検証後更新して新Pressed→Released順、接続/全解除をconsume不能通知で届ける。絶対mouseの初回再基準化とevent deltaを一致させる。
- verify: g++で実InputState.cppのRawInputStateTestと既存KBM/Pad/Runtime回帰。InputRoutingExtensionTestはLoggerSinkTestへ登録、Windows依存で未実行なら静的レビューとして明示。
- stop-when: 既存camera event互換性破壊、invalid入力の部分更新、UIのconsume迂回、切断取消し前のrelease、blocking未解消。
- paths: Library/Core/Public/Input/{InputTypes.h,InputState.h,InputSystem.h,IInputController.h,InputRouter.h}, Library/Core/Private/Input/{InputState.cpp,InputSystem.cpp,InputRouter.cpp}, Test/Core/Input/{RawInputStateTest.cpp,InputRoutingExtensionTest.cpp,CMakeLists.txt}, Test/Core/Logging/CMakeLists.txt, Library/Modules/ImGui/Private/ImGuiModule/ImGuiModule.cpp, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: OS Raw/XInput供給とMapper/Engine接着は次段。新raw/pad配送は既存KBMと同じ優先度契約。

## G1-GR03-P4G: アクションMapperとコンテキストをRouterへ接続する
- status: done
- done-when: 設定を所有してcompileするInputMapperをIInputControllerとして接続し、top contextだけ評価、切替/focus/resetでCancel、UI到達armed、fixed press消費、相対入力、Pad切断を処理する。
- verify: InputActionMapTestをLoggerSinkTestへ登録。実portable runtime/state群はg++回帰。Identity/Router依存のMapperはWindows.hによる未実行を明記し、所有/寿命/公開APIの独立レビューを通す。
- stop-when: borrowed設定pointer保持、UI consumed押下の復活、context越しheld継続、解放後Router pointer、blocking未解消。
- paths: Library/Core/Public/Input/InputMapper.h, Library/Core/Private/Input/InputMapper.cpp, Library/Core/CMakeLists.txt, Test/Core/Input/InputActionMapTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Engine/ApplicationProcessorとOS、JSON/rebind captureは後続。既存camera controllerへイベントを透過する。

## G1-GR03-P4H: Engineとフレーム処理へMapperを配線する
- status: done
- done-when: Engineが正本/Routerより短い寿命でMapperを所有し、message前BeginFrameとOnUpdate前Update、Run終了取消とShutdown先行Detachを接続する。入力時間はsteady_clock、dtはgame clamp前を使う。
- verify: InputFramePipelineTestを既存ApplicationFixedStepPipelineTest束へ追加し、所有/配送/実時間/固定press/終了取消を検証。現在Windows依存で未実行なら静的確認と明示。portable入力群/FixedStepScheduler回帰をg++で実行。
- stop-when: 入力更新がOnUpdateより後、clamped dt使用、正本/Router破棄後のMapper参照、起動画面変更、blocking未解消。
- paths: Library/Core/Public/Engine/Engine.h, Library/Core/Private/Engine/Engine.cpp, Library/Core/Public/Engine/ApplicationProcessor.h, Library/Core/Private/Engine/ApplicationProcessor.cpp, Test/Core/Engine/InputFramePipelineTest.cpp, Test/Core/Engine/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Game既定JSON、rebindとOS供給/cursor/focus通知は次段。Mapper未設定の旧camera経路を維持。

## G1-GR03-P5A: bindings.v1設定JSONと差分を読み書きする
- status: done
- done-when: 物理source/codeの安定した名前と安全なwide数値変換、既定JSONの検証読込/書出、既定＋ユーザー差分の全体適用/差分書出を実装。未知field/actionは警告し無視、既知不正/破損/version違いは既定へ退避する。
- verify: portable InputBindingNamesTestで全code/source往復と数値境界をg++通常/NDEBUG/ASanUBSan。InputBindingJsonTestを既存bundleへ登録しroundtrip/unknown/invalid/empty-unbind/alias-fallbackを検証（Windows依存で未実行なら静的確認を明示）。
- stop-when: cast前の検証なし、hashをJSON number化、borrowed JSON view保存、partial override、破損設定で既定破壊、blocking未解消。
- paths: Library/Core/Public/Input/InputBindingNames.h, Library/Core/Public/Input/InputBindingJson.h, Library/Core/Private/Input/InputBindingJson.cpp, Library/Core/Public/Text/JsonDocument.h, Library/Core/Private/Text/JsonDocument.cpp, Library/Core/CMakeLists.txt, Test/Core/Input/InputBindingNamesTest.cpp, Test/Core/Input/InputBindingJsonTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: FileStoreとGame load/save/rebind UI捕捉は次段。schemaはbindings.v1、識別子は文字列。JSON textは1MiB/深さ64以内。

## CORE-DELEGATE-IDENTITY: Delegateの解除対象を正しく識別する
- status: done
- done-when: member/lambdaの同closure型誤一致とfree function保存領域pointer比較を修正し、登録/複製/個別解除の反証試験を通す。
- verify: Delegate/MulticastDelegateの実コードで別instance/別method/copy/free functionを検証する。公開APIと寿命の独立レビュー必須。
- notes: GR03の設定保存を先に進め、focus等のevent基盤を増やす前に独立Taskとして処理。現在のMapperはRouter pointer解除、Camera slotはClearで回避している。

- stop-when: callableと比較情報の例外時不整合、別instance/別method誤一致、copyで解除handle消失、blocking未解消。
- paths: Library/Core/Public/Delegate/Delegate.h, Library/Core/Public/Delegate/MulticastDelegate.h, Test/Core/Delegate/DelegateIdentityTest.cpp, Test/Core/Delegate/DelegateCopyTest.cpp, Test/Core/Delegate/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, TASKS.md, PROGRESS.md
## CORE-JSON-SURROGATE: JSONの非BMP文字列を整合させる
- status: todo
- done-when: JsonDocumentのsurrogate pairを単一Unicode scalarへ合成し、生UTF-8/escape表現が同じ名前になることを検証する。
- verify: escaped emoji/生UTF-8/孤立surrogate/文字列往復の実コード試験と独立レビュー。
- notes: ParseUnicodeEscapeは現状4桁単位、AppendUtf8は3byteまで。P5Aとは別件。既定game action名はASCIIで進め、汎用JSON整合として後続修正する。

## G1-GR03-P5B: 入力設定のStoreと読込保存APIを接続する
- status: done
- done-when: IInputBindingStoreを介してmissing/loaded/errorを分離し、defaults＋user差分の読込結果と保存APIを提供。Windowsの暫定working-directory adapterは絶対pathを一度固定し、同directoryの排他tempへ全書込/flush後に置換して元ファイルを失敗時に保つ。
- verify: InputBindingPersistenceTestを既存bundleへ追加しFakeStoreでmissing/invalid/read error/save failure/差分roundtripを試験。WorkingDirectoryInputBindingStoreTestはWindowsの一時directory内だけでread/write/失敗時保持/cwd変更/サイズ上限を試験（現環境では未実行）。独立IO/寿命レビュー。
- stop-when: 既存targetを先にtruncate、unknown temp削除、CWD変更で保存先移動、既定不正でuser単独採用、startup自動上書き、blocking未解消。
- paths: Library/Core/Public/Input/IInputBindingStore.h, Library/Core/Public/Input/InputBindingPersistence.h, Library/Core/Private/Input/InputBindingPersistence.cpp, Library/Core/Private/Input/WorkingDirectoryInputBindingStore.cpp, Library/Core/CMakeLists.txt, Test/Core/Input/InputBindingPersistenceTest.cpp, Test/Core/Input/WorkingDirectoryInputBindingStoreTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, .gitignore, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: S8承認済み。GameのAsset JSON/初期化配線は次のP5Cで行い、起動時は読込だけにする。GR76ではStoreだけ差し替える。

## G1-GR03-P5C: Gameの既定入力設定を起動時にロードする
- status: done
- done-when: GameInputActions IDとAssets/Config/DefaultInputBindings.jsonを用意し、GameInputSettingsがAssetReader→Store差分→Mapperを接続。初期Debug contextはNormalで既存camera経路を維持。起動読込で自動保存しない。設定と初期contextはMapperへ一括反映する。
- verify: GameInputSettingsTestを既存bundleへ追加し、実default Asset/注入FakeStore/mapperで既定ロード/override/fallback/初期context/明示保存/失敗非変更を検証（Windows依存未実行なら静的レビューを明示）。JSON syntax/dataはPython確認、portable binding/runtime回帰。
- stop-when: Game-specific actionをCoreへ固定、起動時Locked化、既定/userファイルの勝手な書戻し、context設定の部分反映、blocking未解消。
- paths: Game/Input/GameInputActions.h, Game/Input/GameInputSettings.h, Game/Input/GameInputSettings.cpp, Assets/Config/DefaultInputBindings.json, Game/GameApplicationHandler.h, Game/GameApplicationHandler.cpp, Library/Core/Public/Input/InputMapper.h, Library/Core/Private/Input/InputMapper.cpp, Test/Core/Input/GameInputSettingsTest.cpp, Test/Core/Input/InputActionMapTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: defaultsは暫定data。Gameplayは後の実game用、Rendering3DTest起動はDebug/Normal。rebind捕捉/OS供給は後続。

## G1-GR03-P6A: 入力取消を既存操作コントローラーへ接続する
- status: done
- done-when: OnInputResetをCameraInputCollector/MayaCameraController/LightController/PickingController/ImGuiへ接続し、pressed/drag/modifier/queued UI入力を取り消す。camera姿勢/light値/確定selectionは維持する。
- verify: 実CameraInputCollectorとInputStateのportable試験、既存controllerの操作再開/取消/確定selection維持のbundle試験。ImGuiはvendor APIの整合と静的レビュー。Windows未実行は区別。
- stop-when: resetでclick/select実行、確定selectionやcamera姿勢を消去、UIのqueued pressが残留、blocking未解消。
- paths: Game/Input/CameraInputCollector.h, Game/Input/PickingController.h, Game/Input/PickingController.cpp, Library/Core/Public/Input/MayaCameraController.h, Library/Core/Private/Input/MayaCameraController.cpp, Library/Core/Public/Input/LightController.h, Library/Core/Private/Input/LightController.cpp, Library/Modules/ImGui/Private/ImGuiModule/ImGuiModule.cpp, Test/Core/Input/CameraInputResetTest.cpp, Test/Core/Input/LegacyInputResetTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: OS focus通知自体は次段。既存のInputSystem::ReleaseAll→全Router controller取消に応答する。非focus解除で誤ったfocusイベントは作らない。

## G1-GR03-P6B: Windowsの入力フォーカスを取消経路へ接続する
- status: done
- done-when: IWindowの入力focus値/Delegate通知をWindows focus/activationへ接続。Processorが購読寿命を所有し、loss時にMapper停止＋正本/全controller取消、UI focus通知、handler通知はPumpMessages後に順序を保って配送する。非focus入力と復帰後の古いrepeatは注入しない。
- verify: 偽window＋実Engine/InputSystem/Router/Processorのloss/gain/重複/同batch復帰/handler再入/購読解除を既存Engine bundleへ追加。Windows native実行未確認を明示、公開API/寿命独立レビュー。
- stop-when: WM_KILLFOCUS内でGame handlerを呼ぶ、旧window購読残留、focus loss後held/analog継続、イベント順序喪失、callback例外が新focus処理からOSへ漏れる、blocking未解消。
- paths: Library/Core/Private/Platform/Windows/WindowsApplication.cpp, Library/Core/Private/Platform/Windows/WindowsKeyRepeatGate.h, Test/Core/Engine/WindowsKeyRepeatGateTest.cpp, Library/Core/Public/Input/InputSystem.h, Library/Core/Public/Application/IWindow.h, Library/Core/Private/Platform/Windows/WindowsWindow.h, Library/Core/Private/Platform/Windows/WindowsWindow.cpp, Library/Core/Public/Engine/ApplicationProcessor.h, Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/Public/Input/IInputController.h, Library/Core/Public/Input/InputRouter.h, Library/Core/Private/Input/InputRouter.cpp, Library/Modules/ImGui/Private/ImGuiModule/ImGuiModule.cpp, Test/Core/Engine/InputFocusPipelineTest.cpp, Test/Core/Engine/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Raw Input/カーソル制御/rebindは後続。既存IsActiveとkeyboard input focusを分ける。Game handler通知はboot完了後だけ購読し、shutdown時に先行解除する。

## G1-GR03-P6C: WindowsからRaw mouseと拡張ボタンを供給する
- status: done
- done-when: main windowが明示的にRaw mouseを登録/解除し、他のprocess内登録を上書きしない。WM_INPUTの相対/絶対motionを専用laneへ供給し、初回/device/focus/geometry変化を再seed。legacy button/wheelを二重注入せずX1/X2と水平wheel、signed座標を供給する。foreground WM_INPUTはDefWindowProc cleanupを保つ。
- verify: 実RawMouseMotionTrackerのrelative/absolute/multi-device/invalid/geometry/reset/bounded evictionを通常/O2/ASanUBSan、native Windows配線と登録所有の独立レビュー。Windows実行未確認を明示。
- stop-when: 他のRaw登録を奪う/解除、二重button/wheel、absolute初回jump、focus前のdelta残留、DefWindowProc cleanup欠落、blocking未解消。
- paths: Test/Core/Engine/InputFocusPipelineTest.cpp, Library/Core/Private/Platform/Windows/RawMouseMotionTracker.h, Library/Core/Private/Platform/Windows/WindowsWindow.h, Library/Core/Private/Platform/Windows/WindowsWindow.cpp, Library/Core/Public/Application/IWindow.h, Library/Core/Private/Engine/ApplicationProcessor.cpp, Test/Core/Engine/RawMouseMotionTrackerTest.cpp, Test/Core/Engine/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Raw機能を利用できないときは初期化を警告しlegacy起動を維持。cursorの要求/有効modeとLocked時absolute delta抑止は次段で接続する。

## G1-GR03-P6D: カーソルmodeと絶対mouseの基準化を接続する
- status: done
- done-when: requested/effective cursor modeを分離してMapper→Processor→Windowへ同期。Windowsはfocus/visibility/minimizeに応じてClipと非表示を適用/解除し、move/size/DPIで矩形を更新。native失敗はfalseで返し成功扱いしない。Lockedでは絶対位置だけを供給しdeltaを抑止、mode/clip変更を再seed。Run終了/例外/Shutdown/DestroyでNormalを要求する。
- verify: 実InputStateの非累積/再seed/Raw・wheel・buttons維持をportable試験。Mapperのfocus非依存要求とProcessor同期/解除を既存統合試験へ追加。Windows native clip/表示は未検証として独立レビュー。
- stop-when: 非focus要求をNormalで上書き、ShowCursor counter変更、他window上のcursor形状上書き、locked絶対deltaの漏れ、mode切替で逆delta、native失敗を成功報告、blocking未解消。
- paths: Library/Core/Public/Application/IWindow.h, Library/Core/Private/Platform/Windows/WindowsWindow.h, Library/Core/Private/Platform/Windows/WindowsWindow.cpp, Library/Core/Public/Input/InputState.h, Library/Core/Private/Input/InputState.cpp, Library/Core/Public/Input/InputSystem.h, Library/Core/Private/Input/InputSystem.cpp, Library/Core/Public/Input/InputMapper.h, Library/Core/Private/Input/InputMapper.cpp, Library/Core/Public/Engine/ApplicationProcessor.h, Library/Core/Private/Engine/ApplicationProcessor.cpp, Test/Core/Input/AbsoluteMouseTrackingTest.cpp, Test/Core/Input/InputRoutingExtensionTest.cpp, Test/Core/Input/InputActionMapTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Test/Core/Engine/InputFocusPipelineTest.cpp, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: IWindow GetCursorModeは最後に成功したmode。OS失敗時はfalseを扱い再試行する。入力取消やRaw/wheelの取消を絶対mouse基準化に混ぜない。既定Debug/Normalの表示は維持する。

## G1-GR03-P7A: リバインド設定をcontext stack維持で反映する
- status: done
- done-when: Mapperのcontext stackをIdentity順に維持して設定を一括再構築するAPIと、GameInputSettingsのaction bindings変更/個別既定復帰/全設定既定復帰を追加。成功時は旧操作を取消し、失敗時はMapperと保存対象Currentを両方維持。変更だけでStoreへ書き込まない。
- verify: Mapperのstack順/unknown context失敗/held取消とGameの更新/empty-unbind/alias/invalid/明示Save/既定復帰を既存bundle試験へ追加。所有/例外保証を独立レビュー、Windows依存未実行を明示。
- stop-when: 設定変更でMenuの下のGameplayを失う、失敗時の部分反映、借用binding参照を長期保存、暗黙保存、blocking未解消。
- paths: Library/Core/Public/Input/InputMapper.h, Library/Core/Private/Input/InputMapper.cpp, Game/Input/GameInputSettings.h, Game/Input/GameInputSettings.cpp, Test/Core/Input/InputActionMapTest.cpp, Test/Core/Input/GameInputSettingsTest.cpp, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: captureは次段でこの更新口へ接続する。全既定復帰はbindingsだけでなく感度等のCurrent設定全体をDefaultsへ戻す。個別復帰はbindingsだけ。Storeのstartup UserStatusは読込履歴のまま。

## G1-GR03-P7B: 捕捉用の実pad sampleを取消状態と分離する
- status: done
- done-when: InputStateが最後に受理したprovider sampleと受理serialを保持し、ReleaseAllのneutral化で上書きしない。成功したInjectGamepadStateごとにbutton edge後のsample値イベントをDelegate/Routerへ配送し、neutral解除とanalogをcapture側が判別できる。
- verify: 実InputStateのsample履歴/serial/invalid非変更/Reset区別/全slotをportable試験。配送の同値sample/UIconsume/順序/Reset非通知を既存bundle試験へ追加し独立レビュー。
- stop-when: Resetを物理releaseとして通知、invalidで履歴変更、同値provider sampleを省略、callbackへ借用stateを長期保持、blocking未解消。
- paths: Library/Core/Public/Input/GamepadTypes.h, Library/Core/Public/Input/InputState.h, Library/Core/Private/Input/InputState.cpp, Library/Core/Public/Input/InputSystem.h, Library/Core/Private/Input/InputSystem.cpp, Library/Core/Public/Input/IInputController.h, Library/Core/Public/Input/InputRouter.h, Library/Core/Private/Input/InputRouter.cpp, Test/Core/Input/GamepadInputStateTest.cpp, Test/Core/Input/InputRoutingExtensionTest.cpp, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: 最後のsampleは過去の受理値であって現在操作状態ではない。gameplayは従来の正本/Mapperを使う。GR04のproviderもこの注入口を利用する。capture本体は後続。

## G1-GR03-P7C: 物理入力captureと解除待ちを純kernel化する
- status: done
- done-when: source mask、key/mouse/pad/axis/trigger/relative入力、修飾chord、Escape中止、物理neutral/relative静止待ちを値状態で実装。開始時のheldや接続直後sampleを捕捉せず、UIが消費した入力や古いsampleから候補を作らない。結果はsource/modifiers/directionの値で返す。
- verify: 実InputStateとkernelのdigital/chord/initial-held/UIconsume/sample freshness/analog hysteresis/disconnect/quiet-frame/invalid/再利用を通常/O2/ASanUBSanとbundle compileで検証し独立レビュー。
- stop-when: 初期heldの誤捕捉、Escapeをbinding化、解除前にfinished、Resetを物理neutralと誤認、入力消費をpollingで迂回、invalidで状態破壊、blocking未解消。
- paths: Library/Core/Public/Input/InputRebindCaptureState.h, Library/Core/Public/Input/InputState.h, Library/Core/Private/Input/InputState.cpp, Test/Core/Input/RawInputStateTest.cpp, Library/Core/CMakeLists.txt, Test/Core/Input/InputRebindCaptureStateTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: kernelはIdentity/Router/Store/OSを所有せず、同じ正本を各feed/Advanceへ渡す。Managerと設定反映は次段。符号やscaleの最終binding policyはsource結果を利用する側が決める。

## G1-GR03-P7D: capture managerを入力実行経路へ接続する
- status: done
- done-when: 同じSystem/Router/Mapperに単一ownerで常設登録し、request ID付きBegin/Cancel/resultを提供。capture中は全通常eventとMapper polling/fixedPress/cursor要求を抑止し、外部reset/focus lossで中止。配送外Advanceのreset後に解除して結果を公開。Engineの所有/寿命とProcessorのframe/終了へ接続する。
- verify: 実System/Router/Mapperの停止/復帰/古いrequest/owner/不正配線/初期held/analog/focus/reset試験を既存bundleへ追加。portable kernel/state回帰と独立レビュー、Windows統合未実行を明記。
- stop-when: capture中のgameplay漏れ、登録寿命不整合、reset再入、解除前の結果公開、古いrequestによる中止、blocking未解消。
- paths: Library/Core/Public/Input/InputRebindCaptureManager.h, Library/Core/Private/Input/InputRebindCaptureManager.cpp, Library/Core/Public/Input/InputMapper.h, Library/Core/Private/Input/InputMapper.cpp, Library/Core/Public/Input/InputSystem.h, Library/Core/Public/Input/InputRouter.h, Library/Core/Public/Engine/Engine.h, Library/Core/Private/Engine/Engine.cpp, Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/CMakeLists.txt, Test/Core/Input/InputRebindCaptureManagerTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Test/Core/Engine/InputFocusPipelineTest.cpp, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Game側の対象action/slotと設定revision付き適用は別段。request結果だけで設定を暗黙変更/保存しない。Detachは所有終了時に配送外で呼び、借用先の寿命を超えない。

## G1-GR03-P7E: 捕捉結果をGameの入力設定へ安全に反映する
- status: done
- done-when: context/action/slot/出力設定/revisionを不透明な値requestへ保持し、同じmanager要求とMapper、変更前revisionを検証してP7Aへ反映。末尾追加/置換/中止/待機/古い結果/二重適用を区別。managerや設定owner再生成でもIDを再利用せず、暗黙保存しない。
- verify: 純helperの型/方向/modifier/invalid非変更をportable通常/O2/ASanUBSan/bundleで実行。実Game設定＋managerの追加/置換/古いrevision/別owner/中止/保存試験を既存bundleへ追加。独立レビュー、Windows統合未実行を明記。
- stop-when: 古い/別ownerの結果を適用、失敗時にCurrent/Mapper部分反映、借用pointer長期保持、ID wrap、暗黙保存、blocking未解消。
- paths: Game/Input/GameInputRebindTypes.h, Game/Input/GameInputSettings.h, Game/Input/GameInputSettings.cpp, Library/Core/Public/Input/InputRebindCaptureManager.h, Library/Core/Private/Input/InputRebindCaptureManager.cpp, Test/Core/Input/GameInputRebindTypesTest.cpp, Test/Core/Input/GameInputSettingsTest.cpp, Test/Core/Input/InputRebindCaptureManagerTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: 出力component/gain/反転は呼出側が明示し、既存slotの意図を勝手に変換しない。UIはGR68、保存は既存Saveのみ。

## G1-GR03-P8: ImGuiのカーソル解除とGame入力マスクを接続する
- status: done
- done-when: --imgui有効時だけF1でcursorをNormalへ一時解除し、UI受取後のGame Router入力とMapper polling/fixedを停止。配送外で要求反映/旧操作resetし、再F1で元context要求へ戻す。capture最優先と寿命/Run再開を保ち、既定起動画面を変えない。
- verify: 純toggle stateのrepeat/押下世代/enable/変更待ち/Resetを実テスト。Coreのmode-mask/cursor/legacy停止/復帰/capture優先/別owner/終了・再開を既存bundleへ追加して独立レビュー。Windows/ImGui実機は未検証。
- stop-when: cursor解除中のGame入力漏れ、UI自体へ入力が届かない、F1 repeatで反転、captureと抑止理由の競合、callback内の全reset/登録変更、寿命不整合、blocking未解消。
- paths: Library/Core/Private/Input/InputSystem.cpp, Library/Core/Public/Input/InputDebugOverlayState.h, Library/Core/Public/Input/InputDebugOverlayController.h, Library/Core/Private/Input/InputDebugOverlayController.cpp, Library/Core/Public/Input/InputMapper.h, Library/Core/Private/Input/InputMapper.cpp, Library/Core/Public/Input/InputSystem.h, Library/Core/Public/Input/InputRouter.h, Library/Core/Public/Engine/Engine.h, Library/Core/Private/Engine/Engine.cpp, Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/CMakeLists.txt, Library/Modules/ImGui/Private/ImGuiModule/ImGuiModule.cpp, Test/Core/Input/InputDebugOverlayStateTest.cpp, Test/Core/Input/InputDebugOverlayControllerTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Test/Core/Engine/InputFocusPipelineTest.cpp, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: input context stack/JSONは変更しない。UIの描画内容も変更しない。ImGuiはF1だけ予約透過し、controllerはOverlayより下/Gameより上の常設順位を使う。

## G1-GR04-P1: XInputの入出力値を安全に正規化する
- status: done
- done-when: native成功packetの整数値を独自GamepadStateへ非対称軸正規化/trigger分率/定義button maskで変換し、左右motorの0..1を検証後にuint16へ一括量子化。deadzoneは適用せず、invalid motor値は出力保持。
- verify: 全軸値65536/trigger256の範囲・単調性・端点、全channel/予約bit/packet/実InputState受理、motor誤差とinvalid非変更を通常/O2/ASanUBSan/bundleで実行して独立レビュー。
- stop-when: -32768のoverflow/符号反転、範囲外出力、予約bit混入、deadzone二重適用、invalidの部分反映、blocking未解消。
- paths: Library/Core/Private/Platform/Windows/XInputStateConversion.h, Library/Core/CMakeLists.txt, Test/Core/Input/GamepadStateNormalizeTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: native API/polling/接続/非focus配送とhaptics serviceは後続。API定義はMicrosoft一次資料を確認済み。

## G1-GR04-P2: 背景と復帰時のpad sampleを通常入力から分離する
- status: done
- done-when: Live/Baseline/Backgroundを導入し、実sample履歴を保持したまま非Liveの通常button通知/Pressedを抑止。Backgroundの操作正本はneutral、Baselineは物理状態へ同期。captureは非Liveを基準化だけに使い、初回heldを誤捕捉しない。invalid mode/stateで部分更新しない。
- verify: 実InputStateとkernelのmode・held・解除世代・sample履歴・invalid非変更を通常/O2/ASanUBSan/bundle。実System/Router/Mapperの通知/復帰held試験を既存bundleへ追加し独立レビュー、Windows統合未実行を明記。
- stop-when: 背景で操作値が残る、物理sampleを偽neutral化、Baselineから新規Pressed/captureが出る、invalidの部分更新、既定Live回帰、blocking未解消。
- paths: Library/Core/Public/Input/GamepadTypes.h, Library/Core/Public/Input/InputState.h, Library/Core/Private/Input/InputState.cpp, Library/Core/Public/Input/InputSystem.h, Library/Core/Private/Input/InputSystem.cpp, Library/Core/Public/Input/InputRebindCaptureState.h, Test/Core/Input/GamepadInputStateTest.cpp, Test/Core/Input/InputRebindCaptureStateTest.cpp, Test/Core/Input/InputRoutingExtensionTest.cpp, Test/Core/Input/InputActionMapTest.cpp, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: 非Live配送はfocus Cancelの代用ではない。実providerとEngineのfocus/ポーリング接続は後続で行う。

## G1-GR04-P3: 差替APIでXInputのポーリング状態を実装する
- status: done
- done-when: SDK非依存のAPI/sink境界で4slotを読み、接続済み毎frame/未接続低頻度round-robin、同packet配送、接続/切断/error退避、background/復帰baseline、sink拒否再試行、単調clockを処理する。API参照以外を長期借用しない。
- verify: Fake APIと実InputState sinkによる初期probe/周期/連続入力/再接続/error/拒否/Mode/invalid clock/Shutdownを通常/O2/ASanUBSan/bundleで実行し独立レビュー。
- stop-when: 同frame二重poll、欠番slotの放置、未接続の高頻度poll、背景Live注入、復帰held再押下、拒否を配送済み扱い、clock不正で状態更新、blocking未解消。
- paths: Library/Core/Private/Platform/Windows/IXInputApi.h, Library/Core/Private/Platform/Windows/XInputPollingState.h, Library/Core/CMakeLists.txt, Test/Core/Input/XInputDevicePollingTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: この段は入力読取り状態。native adapter/Engine接続と振動は後続。sinkは呼出中だけ、APIはstateより長命で使う。

## G1-GR04-P4: XInputデバイスを実APIと入力正本へ接続する
- status: done
- done-when: Windows APIを明示field変換するadapterと、APIを所有するXInputDeviceを実装。null APIを拒否し、Initialize/Shutdown/焦点/時刻付きpollをIInputDeviceへ接続、InputSystem注入は同期間の借用だけ。Platform factoryは未初期化deviceを返す。
- verify: 公開/非SDK header syntax、P3 portable回帰、実Systemでfake APIの所有/拒否/再初期化/背景/復帰試験を既存bundleへ追加。Windows APIと統合実行の限界を記録し独立レビュー。
- stop-when: null参照、APIより長い借用、copy/moveで参照破壊、入力systemの長期保持、失敗packet使用、既存IInputDevice実装の破壊、blocking未解消。
- paths: Library/Core/Public/Input/IInputDevice.h, Library/Core/Public/Platform/PlatformInputDevices.h, Library/Core/Private/Platform/Windows/XInputDevice.h, Library/Core/Private/Platform/Windows/XInputDevice.cpp, Library/Core/Private/Platform/Windows/WindowsXInputApi.cpp, Library/Core/CMakeLists.txt, Test/Core/Input/XInputDeviceIntegrationTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: Engine登録/frame呼出しは次段。読み取り専用で、振動出力はhaptics接続時に追加する。

## G1-GR04-P5: 入力デバイスの所有とフレーム寿命をEngineへ接続する
- status: done
- done-when: Engineがdeviceを所有し単一pad providerを保証。開始の部分失敗を逆順停止、終了/例外/再Runを扱い、失敗停止は再試行対象に残す。focus message後/Mapper更新前に単調時刻でpollし、標準padはhandler設定後に未登録時だけ生成。停止は即時操作取消と安全地点でのlegacy reset通知を行う。
- verify: fake deviceの所有/順序/再入拒否/開始失敗/停止例外再試行/clock非変更、実Runのmessage→poll→handlerと例外cleanupを既存Engine bundleへ追加。既存P3/P4の関連回帰、静的独立レビュー。Windows依存の未実行を明示。
- stop-when: device二重pad供給、partial開始放置、callback中破棄/登録、Shutdown例外で残り未停止、焦点喪失前のLive poll、終了後held残留、blocking未解消。
- paths: Library/Core/Public/Input/IInputDevice.h, Library/Core/Public/Engine/Engine.h, Library/Core/Private/Engine/Engine.cpp, Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/Public/Input/InputSystem.h, Library/Core/Private/Input/InputDebugOverlayController.cpp, Test/Core/Engine/InputDeviceLifecycleTest.cpp, Test/Core/Engine/InputFocusPipelineTest.cpp, Test/Core/Engine/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: native実機とWindows統合実行は未検証。振動serviceは後続。Shutdown失敗のdeviceは所有/再停止義務を保持し、再初期化を拒否する。

## G1-GR04-P6: 振動包絡線の混合と成功ACKによる出力判定を実装する
- status: done
- done-when: 有限duration/昇順key/値0..1を検証し、線形補間/loop/期間終了を処理。slot内最高priorityのactive sampleだけをmaxまたはadd-clamp合成し、設定倍率を最後に適用。成功ACKだけを記録し、差分1/255未満は抑止するが最終zero/失敗再試行は省略しない。
- verify: 実純ロジックの境界/巨大経過時間/優先度/両合成/倍率/invalid非変更、ACK/失敗/最終zeroを通常/O2/ASanUBSan/bundleで実行し独立レビュー。
- stop-when: 非有限/範囲外出力、優先度無視、invalid部分反映、失敗ACK扱い、微小値の最終zero省略、blocking未解消。
- paths: Library/Core/Public/Input/HapticsEnvelopeMath.h, Library/Core/Public/Input/HapticsOutputState.h, Library/Core/CMakeLists.txt, Test/Core/Input/HapticsMixerTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: この段は純評価/混合/出力記録。voice所有/実device送信/JSON/Engineのfocus・pauseは後続。定義のspanは呼出中だけ借用する。

## G1-INPUT-SPAN: 入力層の借用viewを独自Spanへ統一する
- status: done
- done-when: GR03の入力runtime/settings/names/mapper/binding setで使用しているstd::spanをContainer::Spanへ揃え、所有/寿命/呼出互換を維持する。独自Spanのrange constructorを末尾型制約付きにし、(pointer, 0)がcount指定として一意に解決するようにする。
- verify: 変更header/runtimeの関連portable試験を通常/O2/ASanUBSan/bundle、Mapper/SetはWindows依存の実行限界を明記し公開API差分を独立レビュー。
- stop-when: view寿命の延長、空view/配列/const変換の回帰、規約を満たすための偽platform代替、blocking未解消。
- paths: Library/Core/Public/Container/Span.h, Test/Core/Input/InputActionRuntimeTest.cpp, Library/Core/Public/Input/InputActionRuntime.h, Library/Core/Public/Input/InputBindingNames.h, Library/Core/Public/Input/InputMapper.h, Library/Core/Public/Input/InputActionSettings.h, Library/Core/Private/Input/InputBindingSet.cpp, Library/Core/Private/Input/InputMapper.cpp, TASKS.md, PROGRESS.md
- notes: GR04-P6レビューで独自Span規約との不一致を確認。既存他領域の一括置換は行わず、今回の入力整備で導入した境界に限定する。

## GAME-GR130-VFX: 剣のトレイル（リボン）を実装する
- status: todo
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: CPU の試験で、固定ステップの回数が 0・1・2 のどのフレームでも帯の点列が連続で NaN が無いこと、1フレームで90度以上振っても補間で折れ目の角度が上限以下になること、寿命で点が消えて上限を超えないこと。GPU の試験で、既知の軌跡の帯の画素の位置が期待と一致すること。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR13（ボーンソケット）、GR57（描画の経路と半透明のキューを共有）、GR136、GR02（剣は描画用の補間された Transform を読む。攻撃判定 GR16 はシミュレーションの Transform を読む。段階をまたぐ注意の G4 の項目どおり）、GR30（TAA の整合）。
- stage-proposal: G8（戦闘と進行）。垂直スライスの戦闘で使う。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR131-VFX: 当たりの火花・衝撃（ヒットエフェクト）を実装する
- status: todo
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: 当たり1回で放出の要求がちょうど1回積まれ、位置と法線が当たりの値と一致すること。表面の種類ごとに表のエフェクトが選ばれること。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR16、GR57、GR136、GR115（軟い。無いうちは既定の1種類）、GR02。
- stage-proposal: G8。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR132-VFX: シ者を倒したときの赤い血を実装する
- status: todo
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: 撃破の合図から、血・滲み・消滅が時間割どおりの時刻で始まること（固定刻みのクロックで2回撮って一致）。滲みのマスクが時間に対して単調に広がること。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR57、GR60、GR65、GR26、GR133、GR136。
- stage-proposal: G10（シ者の表現と戦闘の拡張）。垂直スライス（G8）では、最小の形（血しぶきのパーティクルだけ）を入れるかを作者が決める。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR133-VFX: デカール（地面や体に残る跡）を実装する
- status: todo
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: GPU の試験で、既知の箱のデカールが範囲内の GBuffer の色と法線だけを変え、範囲外の画素が変わらないこと。上限を超えると古いものから消えること。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR25（描画の拡張点）、GR26（材質の拡張）、GR37（地形）。草（GR43）の上の扱いは、草には描かないのを既定にする。
- stage-proposal: G10。足跡を垂直スライスで使うなら G7 に前倒し。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR134-VFX: メッシュのエフェクトを実装する
- status: todo
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: 粒子の数だけインスタンスが描かれ、時間の値で溶けの閾値が変わること（GPU の試験の画素で確かめる）。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR57、GR136、GR26、GR27（アルファテストと両面描画）、GR60。
- stage-proposal: G10。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR135-VFX: 空気の歪み（屈折）を実装する
- status: todo
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: 歪みのバッファが空のとき、出力が歪みのパスの有無で画素単位で一致すること。既知のずらしの値で、画素が期待の量だけ動くこと。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR25、GR30、GR57、GR134、GR136。
- stage-proposal: G10。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR136-VFX: Niagara 相当の VFX システムを実装する
- status: todo
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: スキーマに、エミッタとモジュールの型、値の範囲・単位・既定値が出ること。範囲外の値や型の合わない値の設定が拒否されること。クックしたバイナリを読んだ結果が、元の形式から読んだ結果と一致すること（同じ種と刻みで、粒子の位置の列が一致）。動いているゲームでの値の変更が、次のフレームから反映すること。イベントで起動したエミッタが、起動の位置と時刻どおりに生成すること。フリップブックの取り込み設定どおりの UV の矩形と再生の速さ。雨（GR58）を含む既存の要件のエフェクトを少なくとも1つ、このシステムで組めること。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR57（この要件は GR57 と範囲が重なる。GR57 をこの形で設計するか、GR57 の上の層として作るかは着手時に決める）、GR78、GR96、GR02、GR126（軟い）、GR72（軟い。音は後で足せる形）。
- stage-proposal: G7（GR57 と一緒）。雨（GR58）やしぶき（GR59）もこのシステムのエフェクトとして作るので、早めに入れる。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR137-VFX: 資産の編集・決定的な撮影・言語モデルの口（Bridge の拡張。汎用）を実装する
- status: todo
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: Bridge の試験で、資産を開いて値を設定し、保存して開き直すと値が一致すること。型の合わない値や範囲外の値が拒否されること。同じ引数で2回撮影した画像が一致すること（GPU の試験。GPU の無い環境では飛ばす）。MCP の口から、スキーマの一覧、値の設定、撮影が通ること（NorvesEditor の側の作業なら、NorvesLib の試験の対象外）。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR136（エフェクトの資産がリフレクションで公開されていること）、GR78、GR96、GR02、GR126。
- stage-proposal: G7（GR136 と一緒）。エフェクト以外の資産にも使えるので、早く欲しければ前倒しできる。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## G1-GR04-P7: XInputの振動出力と失敗時の停止再試行を接続する
- status: done
- done-when: float0..1を検証/量子化して実XInputSetStateへ送り、成功だけACKとして返す。非zero試行の所有/不確実性を保持し、focus喪失/Shutdownで0、失敗は再試行、未停止状態から再Initializeを拒否する。EngineはTryShutdownの結果で終了義務を保持する。
- verify: SDK非依存実出力stateとfake APIで全slot/不正非変更/失敗非zero/停止失敗/再試行/背景拒否/再初期化を通常/O2/ASanUBSan/bundle。device/既存入力回帰、公開API/寿命/停止の独立レビュー。実nativeとEngine統合未実行を明示。
- stop-when: failed writeを成功扱い、停止義務の消失、背景nonzero、invalid clock/valueでAPI送信、未停止再開始、実装によるcallback中owner再入、blocking未解消。
- paths: Library/Core/Public/Input/IInputDevice.h, Library/Core/Private/Platform/Windows/IXInputApi.h, Library/Core/Private/Platform/Windows/XInputVibrationState.h, Library/Core/Private/Platform/Windows/XInputPollingState.h, Library/Core/Private/Platform/Windows/XInputDevice.h, Library/Core/Private/Platform/Windows/XInputDevice.cpp, Library/Core/Private/Platform/Windows/WindowsXInputApi.cpp, Library/Core/Private/Engine/Engine.cpp, Library/Core/CMakeLists.txt, Test/Core/Input/XInputVibrationStateTest.cpp, Test/Core/Input/XInputDeviceIntegrationTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: serviceのvoice/包絡線/ポーズとの接続とJSONは次段。native停止失敗の物理的成功は保証しない。stop pendingはAPI所有者の寿命内で保持する。

## G1-GR04-P8: 振動効果と再生voiceをHapticsServiceで所有する
- status: done
- done-when: 効果/keyをコピー所有し、最大64voiceを非wrap handleでPlay/Stop。実dt/loop/終了とpriority合成、倍率/onoff、focus/paused取消、成功ACK差分送信/失敗retryを一時sinkへ接続。無効設定/clockは非変更、再入を拒否しowner/APIを長期借用しない。
- verify: 実時間進行kernelの巨大dt/端点/無効非変更を通常/O2/ASanUBSan/bundle。実serviceの所有/handle/voice上限/再設定/倍率/停止/再入/failed ACKを既存bundleへ追加し独立レビュー。Windows依存の未実行を明示。
- stop-when: borrowed effect寿命超過、handle wrap/別owner誤停止、invalid部分更新、停止zero省略、inactiveで振動、sink再入で破損、blocking未解消。
- paths: Library/Core/Public/Input/HapticsPlaybackTime.h, Library/Core/Public/Input/HapticsService.h, Library/Core/Private/Input/HapticsService.cpp, Library/Core/CMakeLists.txt, Test/Core/Input/HapticsPlaybackTimeTest.cpp, Test/Core/Input/HapticsServiceTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: service自体はbackend参照を保持しないが、寿命中の実backend/slot対応は同一とする。動的交換は旧service停止後に新serviceを使う。制御変更は次のUpdate/FlushOutputsで送信、Engineの即時focus/pause/終了接続とJSONは次段。

## G1-GR04-P9: 振動再生をEngineの更新と即時停止へ接続する
- status: done
- done-when: Engineがserviceを値所有し同一pad providerへ一時sinkで送信。simulation前にpauseを反映、後に未clamp実dt更新。focus/pause/終了で即時取消/Flushとnative停止を行い、再Runで旧voiceを再開しない。owner/service再入を拒否し、配送中focusの保留を安全地点で回収する。喪失→復帰の取消履歴と新しい通知を失わない。
- verify: fake providerで4slot/倍率/送信順/非focus/pause/終了失敗再試行/再Run、Processorの実dtヘルパーでclamp前の時間を確認する試験を既存Engine bundleへ追加。純時間/mix/output回帰と独立レビュー、Windows統合未実行を明示。
- stop-when: pause中nonzero、cleanup前にbackend破棄、scaled/clamped dt使用、古いvoice再開、callback中owner再入、停止失敗の偽成功、blocking未解消。
- paths: Library/Core/Public/Engine/Engine.h, Library/Core/Private/Engine/Engine.cpp, Library/Core/Public/Engine/ApplicationProcessor.h, Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/Public/Input/HapticsService.h, Library/Core/Private/Input/HapticsService.cpp, Test/Core/Engine/HapticsFramePipelineTest.cpp, Test/Core/Engine/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: serviceの効果はGame側が設定。既定asset/JSON読込みは次段。実機停止が失敗した場合は再試行義務を保持し、物理成功を保証しない。

## G1-GR04-P10: 振動効果JSONと既定assetの起動読込みを接続する
- status: done
- done-when: haptics.v1のeffect/key/settingsを型・有限値・範囲・重複・サイズ/深さ上限付きでParse/Writeし、invalid時の旧値を保持。serviceへ効果と設定を一括反映、既定assetをGame起動で読込み、通常I/O/validation失敗時は旧設定を維持して起動継続（確保例外は伝播）。自動再生は行わない。
- verify: JSON roundtrip/未知field/既知重複/不正型・範囲・名前・曲線・上限/旧設定保持とGame適用を既存bundleへ追加。既定assetの実Python検証、pure haptics回帰、独立レビュー。Windows依存の実parser/統合未実行を明示。
- stop-when: invalidの部分反映、設定の暗黙保存/自動再生、JSON解析のbyte/depth上限無視、未知fieldの誤採用、起動画面の変更、blocking未解消。
- paths: Library/Core/Public/Input/HapticsJson.h, Library/Core/Private/Input/HapticsJson.cpp, Library/Core/Public/Input/HapticsService.h, Library/Core/Private/Input/HapticsService.cpp, Library/Core/CMakeLists.txt, Game/Input/GameHapticsSettings.h, Game/Input/GameHapticsSettings.cpp, Game/GameApplicationHandler.cpp, Assets/Config/HapticsEffects.json, Test/Core/Input/HapticsJsonTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: 強度/onoffはruntime APIとJSONで表現し、user設定の保存先/UIはGR76/GR68へ接続する。既定の見た目/手触り値は仮、実機調整は未確認。既存AssetFileReaderは全file読込み後にJSON上限を検査し、確保前の個別上限は別課題。

## G1-GR04-P11: 入力方式の活動判定と切替ヒステリシスを実装する
- status: done
- done-when: KeyboardMouse/Gamepadを値状態で識別し、有効clockと活動時だけ切替、最短間隔0.3秒を既定にする。key/button新規押下、mouse移動閾値/scroll/char、Live padの新規button・deadzone外の意味ある変化を判定。背景/基準sample/同値/無効値で切替えず、focus復帰で旧kindを維持する。
- verify: 純状態の全source/境界/noise/同値/clock拒否/最初の切替/cooldown/設定非変更/focus/相対と絶対の二重計上回避を通常/O2/ASanUBSan/bundleで実行し独立レビュー。
- stop-when: analog drift/repeat/背景入力で切替、単調clock違反の状態更新、cooldown無視、mouse2lane二重加算、無効設定の部分反映、blocking未解消。
- paths: Library/Core/Public/Input/ActiveDeviceKind.h, Library/Core/CMakeLists.txt, Test/Core/Input/ActiveDeviceKindTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: この段は純検出state。InputSystem/GetActiveDeviceKind/DelegateとEngine時刻・focus接続は次段。閾値は表示用の活動判定で、Mapperの操作値には適用しない。

## G1-GR04-P12: 使用中入力方式の取得と変更通知をフレームへ接続する
- status: done
- done-when: InputSystemでGetActiveDeviceKind/設定/変更Delegateを公開し、受理された入力を物理履歴からP11へ渡す。明示の非scaled frame時刻とfocusをEngineから供給し、EndFrameで最終kind変更のみ通知する。reset/held/背景/無効入力で切替を合成しない。
- verify: 純検出回帰、実Systemの各source/通知順/集約/cooldown/物理held/reset/focus/無効clock非変更を既存bundleへ登録し実compileを試す。Engineの時刻検証/focus供給を静的確認し独立レビュー。Windows依存で実行不能なら未検証と明記しstubで代用しない。
- stop-when: 正本の受理前に活動を反映、人工resetを新押下扱い、frame検証失敗で正本を更新、callback中の二重通知/寿命逸脱、旧入力経路の変更、blocking未解消。
- paths: Library/Core/Public/Input/InputSystem.h, Library/Core/Private/Input/InputSystem.cpp, Library/Core/Private/Engine/Engine.cpp, Library/Core/Private/Engine/ApplicationProcessor.cpp, Test/Core/Input/ActiveDeviceKindIntegrationTest.cpp, Test/Core/Input/CMakeLists.txt, Test/Core/Logging/CMakeLists.txt, Docs/Architecture/InputFoundation.md, TASKS.md, PROGRESS.md
- notes: 既存BeginFrame()はsteady clockで互換を保つ。明示clockとの異なる時刻原点混用は禁止。初回frame以前の注入は従来通り正本へ受理するが種別判定対象外。変更通知callbackは入力配送外、再入frame/本体破棄/例外は不可。

## G1-GR08-P2D: コライダーのレイヤーと識別値を公開snapshotへ接続する
- status: done
- done-when: ColliderComponentに32bit Layer/Maskとuint64 UserDataを所有させ、SetTrigger同様に登録・owner thread検証後に更新する。既定Layer1/Mask全bit/UserData0を維持し、BuildBroadphaseがtriggerも含めコピーする。旧ray/overlap hit末尾にUserDataを追加し全形状で伝搬する。
- verify: 実proxy/query型のCPU回帰を通常/O2/ASanUBSan/MEMBERで実行。実component/snapshotの既定/設定/公開時点/旧hit/拒否非変更を既存Physics bundleへ追加しcompileを試す。独立公開契約レビュー。Windows依存は未検証と区別し代用品を作らない。
- stop-when: ゲーム固有layer名のCoreへの流入、公開snapshotの暗黙更新、旧hit順序/normalの変更、wrong thread/未登録での部分更新、blocking未解消。
- paths: Library/Core/Public/Scene/PhysicsQueryTypes.h, Library/Modules/Physics/Public/Physics/ColliderComponent.h, Library/Modules/Physics/Private/Physics/ColliderComponent.cpp, Library/Modules/Physics/Private/Physics/PhysicsModule.h, Library/Modules/Physics/Private/Physics/PhysicsModule.cpp, Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp, Test/Modules/Physics/PhysicsBroadphaseQueryTest.cpp, Test/Modules/Physics/PhysicsProxyQueryTest.cpp, Test/Modules/Physics/PhysicsModuleTestAccess.h, Test/Core/Object/PhysicsQueryTypesTest.cpp, Docs/Architecture/PhysicsQueries.md, TASKS.md, PROGRESS.md
- notes: Layerは所属bit集合で0/複数bitも許可。ゲーム側が名前/割当を所有する。Mask0も有効。solver適用と新query facadeは後続で、metadataを保存しただけで衝突挙動変更完了とはしない。

## G1-GR08-P4A: 統合クエリをSceneQueryと物理providerへ接続する
- status: done
- done-when: SceneQuery::ExecuteQueryでPhysicsQueryDescをproviderへ委譲し、PhysicsModuleがreadiness/owner thread検証後に公開snapshotの実ExecuteQueryを呼ぶ。未対応providerの既定実装はUnavailable、非Successと例外で出力を残さない。旧ray/overlap入口は互換維持する。
- verify: fake providerの旧実装互換/完全descriptor伝搬/成功/全失敗/例外/未bind/wrong threadを既存SceneQueryPhysicsFacadeTestへ、実Moduleのray複数hit/filter/ignore/trigger/overlap/sweep/UserData/未準備を既存PhysicsBroadphaseQueryTestへ追加。実compileを試し、実proxy CPU回帰と独立レビューを実施。
- stop-when: CoreがPhysics moduleへ依存、wrong thread/未bindでprovider呼出し、非Success/例外で残留hit、公開snapshot以外への暗黙refresh、blocking未解消。
- paths: Library/Core/Public/Scene/SceneQuery.h, Library/Core/Private/Scene/SceneQuery.cpp, Library/Modules/Physics/Private/Physics/PhysicsModule.h, Library/Modules/Physics/Private/Physics/PhysicsModule.cpp, Test/Core/Object/SceneQueryPhysicsFacadeTest.cpp, Test/Modules/Physics/PhysicsBroadphaseQueryTest.cpp, Docs/Architecture/PhysicsQueries.md, TASKS.md, PROGRESS.md
- notes: generic descriptorでfilter付き7種を公開する。batch/訪問関数最適化/solver mask/refreshは後続。既存APIへ新しい数値制限を強制する経路差替は行わない。Windows依存の統合実行は未検証と区別する。

## G1-GR08-P4B: 同じ公開snapshotへの問い合わせをバッチ化する
- status: done
- done-when: SceneQuery/provider/PhysicsModuleへExecuteBatchを追加し、準備/owner確認をbatch単位にして順次実行する。要求順のResult/FirstHit/HitCountと連続hitを返す。個別失敗は0hitで他要求を継続、全体の非Success/例外は両出力を空にする。準備済み空batchはSuccess。
- verify: fake providerの既定未対応/1回委譲/全体失敗/例外/不正span/wrong threadと、実ModuleのSuccess/NoHit/InvalidArgument混在/offset/MaxHits/空/未準備/単発結果一致/snapshotsequence不変を既存bundleへ追加。実compile試行、CPU回帰、独立レビュー。
- stop-when: itemごとにreadiness再確認、異なるsnapshotへの暗黙更新、失敗itemのhit混入、offset/size overflow、全体失敗/例外で部分出力、blocking未解消。
- paths: Library/Core/Public/Scene/SceneQuery.h, Library/Core/Private/Scene/SceneQuery.cpp, Library/Modules/Physics/Private/Physics/PhysicsModule.h, Library/Modules/Physics/Private/Physics/PhysicsModule.cpp, Test/Core/Object/SceneQueryPhysicsFacadeTest.cpp, Test/Modules/Physics/PhysicsBroadphaseQueryTest.cpp, Docs/Architecture/PhysicsQueries.md, TASKS.md, PROGRESS.md
- notes: 戻り値Successはbatch処理成立で、各要求成否はoutResultsを見る。failure itemのFirstHitはその時点の連続hit末尾、HitCount0。入力span/両出力は非aliasかつ同期呼出し中有効。未対応providerの既定はUnavailable。batch自体の結果と個別結果を混同しない。

## G1-GR08-P5A: 衝突候補とsolverへ対称レイヤーマスクを適用する
- status: done
- done-when: broadphase候補生成前とResolveContactsの接触/trigger判定前に(A.Layer & B.Mask)&&(B.Layer & A.Mask)を適用する。既定は従来通り、0/高bit/複数bitを扱い、許可切替でtrigger Begin/Endが次stepに整合する。queryのLayerMaskと相互作用Maskを混同しない。
- verify: 純bit判定の全32bit/両方向/4096真理値表と実proxy回帰を通常/O2/sanitizer/MEMBERで実行。実broadphaseの順序/接触端点/一方向拒否、実Worldのtrigger通知/solid押出し有無を既存Physics bundleへ追加してcompile試行、独立レビュー。
- stop-when: 片側許可だけで衝突、拒否pairの押出し/Hit/Begin、旧既定の候補順序変更、queryへMaskを誤適用、blocking未解消。
- paths: Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp, Library/Modules/Physics/Private/Physics/PhysicsModule.cpp, Test/Modules/Physics/PhysicsBroadphaseQueryTest.cpp, Test/Core/Object/PhysicsQueryTypesTest.cpp, Docs/Architecture/PhysicsQueries.md, TASKS.md, PROGRESS.md
- notes: Layer/Maskはworking snapshotの値で評価し、callback中の設定は次stepに反映。既存trigger終了通知は許可撤回時にも配信する。実Windows/Core統合は未検証と区別する。

## G1-GR08-P5B: simulationを進めずquery snapshotを明示更新する
- status: done
- done-when: SceneQuery/provider経由のRefreshDynamicSnapshotを追加し、owner/準備/physics処理中を検証してquery公開データだけ再構築する。承認S9=a通りfixed-step sequence/速度/impulse/準備位置/接触・通知状態は保持。候補完成後のnoexcept置換で旧snapshotを保護する。
- verify: fake provider未対応/委譲/wrong threadと実Moduleの移動前後/明示更新/sequence不変/新規・無効化/metadata/Body対応/未準備/通知中拒否を既存bundleへ追加。実compile試行、Broadphaseのnoexcept移動syntax、CPU回帰、独立レビュー。
- stop-when: refreshでsimulation進行/impulse消費/イベント発火/sequence更新、working snapshotの変更、候補失敗で公開値破壊、reentryで通知中のsnapshot変更、blocking未解消。
- paths: Library/Core/Public/Scene/SceneQuery.h, Library/Core/Private/Scene/SceneQuery.cpp, Library/Modules/Physics/Private/Physics/PhysicsModule.h, Library/Modules/Physics/Private/Physics/PhysicsModule.cpp, Test/Core/Object/SceneQueryPhysicsFacadeTest.cpp, Test/Modules/Physics/PhysicsBroadphaseQueryTest.cpp, Docs/Architecture/PhysicsQueries.md, TASKS.md, PROGRESS.md
- notes: GR09の分離最適化までは全proxy再構築。明示呼出しのみで毎frame自動実行しない。現在のlifecycleをquery用に評価するがsimulationのactive cacheは更新しない。sequenceは最後の固定更新公開を示し、明示refreshで同sequence内のquery内容が変わる。

## G1-GR08-P4C: 物理proxy候補のAABBとray訪問境界を追加する
- status: done
- done-when: span上の実proxyをAABB/有限長rayで保守的に絞り、同期callbackへ入力順で渡す訪問APIを追加。任意precheckでfilter/validationを先行でき、失敗の順序を保つ。非有限/逆転bounds等は要求拒否、proxy側の不正boundsは黙って落とさずcallbackへ渡す。
- verify: 実Broadphase.cppを直接リンクし、遠方除外/接触/巨大・微小direction/長さ上限/0距離/順序/早期失敗/precheck拒否/不正値/不正spanを通常/O2-NDEBUG/ASanUBSan/MEMBERで検証し独立レビュー。既存proxy回帰も実行。
- stop-when: 接触候補の取りこぼし、近zero軸をepsilonで平行扱い、callback借用の保持、validator失敗の隠蔽、proxy不正の黙殺、blocking未解消。
- paths: Library/Modules/Physics/Private/Physics/PhysicsBroadphase.h, Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp, Test/Modules/Physics/PhysicsProxyVisitorTest.cpp, Test/Modules/Physics/CMakeLists.txt, Docs/Architecture/PhysicsQueries.md, TASKS.md, PROGRESS.md
- notes: 同期callbackはSuccess/NoHitで継続、他のResultで中断。precheckのNoHitは候補除外。callback/context/proxyの借用は呼出し中だけ。float丸めとsweep許容を見込みboundsを保守的に拡張。既存集約への接続はP4D。

## G1-GR08-RAY: 遠方rayと球・カプセルの桁落ち誤判定を修正する
- status: done
- done-when: 物理query内のray/sphere/capsule計算をdoubleの線距離・断面判定へ変更し、遠方の明確なmissをfalse hitにしない。内部始点0/最寄り正根/端球・円筒・縮退/正規化・距離上限の契約を保つ。共有Math/描画側は変更しない。
- verify: radius1/offset2/distance10000の球とカプセルで旧Success距離10000を実再現済み。軸方向の解析解、遠方hit/miss/tangent/inside/逆向き/縮退/極小線分/向き・倍率を通常/O2-NDEBUG/ASanUBSan/MEMBERで検証し、既存proxy/visitor回帰と独立レビューを行う。
- stop-when: 明確な非交差をhit、真の交差をmiss、非有限結果、近い正根より遠い根を選ぶ、旧inside0の破壊、描画への変更、blocking未解消。
- paths: Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp, Test/Modules/Physics/PhysicsRayPrecisionTest.cpp, Test/Modules/Physics/CMakeLists.txt, Test/Core/Object/SceneQueryPhysicsFacadeTest.cpp, Docs/Architecture/PhysicsQueries.md, TASKS.md, PROGRESS.md
- notes: P4D全探索比較前の既存不具合修正。旧float二次式は明確な球・円筒missにもSuccessを返す。最適化の結果を誤った全探索へ合わせない。距離/点の外部float表現自体の丸めは残る。

- notes: 追加試験のcompileでVector3に単項minusが無いことを確認。P4Aで補強した未実行facade試験にも同じ式が1件あったため、明示Vector3(-1,0,0)へ修正する。

## G1-GR03-CURVES: 入力と振動の最小曲線評価をMathへ共通化する
- status: done
- done-when: RoadmapのG1追加指示に従いMath/Curves.hへ区分線形とイージングの最小APIを置き、入力応答と振動が同じ評価実装を再利用する。既存既定値/JSON/端点/無効値契約を維持し後続GRへ拡張口を示す。
- verify: 曲線端点/区間/不正値/イージングと既存入力軸・振動のCPU回帰、独立レビュー。
- stop-when: 未承認の既定値変更、二重curve適用、非有限値、既存公開契約の破壊。
- notes: 原文Roadmapの段階横断注意G1/GR03・GR04。入力API/GR119・GR121の再利用規約も文書に明記する。

## G1-GR01-CONTRACT: 更新群とpauseと最終cameraの共有契約を明文化する
- status: done
- done-when: 更新群ごとのComponent割当、Bridge進行/非pause/将来bTickWhenPausedの3分類、新Component登録規約、G14 CameraDirector/G16 listenerの参照順を現コードと承認S3に合わせて文書化する。
- verify: TickStages/Processor/World/Module/Gameの実順序を照合し、未実装のbTickWhenPausedや将来hookを既存機能と誤記しない。独立文書レビュー。
- stop-when: IModule::LateTickより後のGame::OnLateUpdate確定を隠す、未承認の更新順/pausingの実装変更。
- notes: 原文Roadmapの段階横断注意G1/GR01。camera最終確定は承認S3=OnLateUpdateと実順序を正確に説明する。

## G1-GR08-CAPSULE-ACCEPTANCE: 後続移動処理向けのカプセル境界試験を固定する
- status: done
- done-when: 縦Capsuleの箱面/辺/角、Capsule同士/回転、開始重なりDistance0/印/押出しNormal、OverlapCapsuleのDepth/Normalを解析例で固定し契約を文書化する。
- verify: 実GeometrySweep/SeparationとPhysicsProxyQueryのCPU試験を通常/最適化/sanitizer/MEMBERで実行し独立レビュー。
- stop-when: 正常ケースを緩めて既存実装へ合わせる、未収束を確定hitと扱う、法線符号/深さ規約の曖昧化。
- notes: GameFeatureHandoffとRoadmapのG1/GR08追加完了条件。既存Sphere試験や中心通過乱数試験だけでは代替しない。

## G1-GR08-P4D: 統合クエリをrayと掃引AABBの候補訪問へ接続する
- status: done
- done-when: 既存query集約をP4C訪問へ接続し、Overlapの形状boundsとSweepの始終点を包むAABBで狭域判定を減らす。proxy検証/filter/数値安全域は除外前に確認し、順序/MaxHits/失敗時クリアを維持する。boundsを安全に構成できなければ全探索へ戻す。
- verify: 固定seedの混在proxyと7種を、独立sortによる全QueryProxy走査と比較し、各hit field/順序/MaxHitsをCPU検証。境界OBB/初期重なり/無効遠方proxy/候補内IterationLimit/巨大掃引fallbackと既存試験を通常/O2/sanitizer/MEMBERで実行し独立レビュー。
- stop-when: 確定hitの取りこぼし、検証失敗の隠蔽、候補内未収束をhitへ変換、出力残留、bounds overflowで不正除外、blocking未解消。
- paths: Library/Core/Public/Math/GeometryIntersection.h, Library/Core/Private/Math/GeometryIntersection.cpp, Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp, Test/Modules/Physics/PhysicsQueryPruningTest.cpp, Test/Modules/Physics/PhysicsRayPrecisionTest.cpp, Test/Modules/Physics/CMakeLists.txt, Docs/Architecture/PhysicsQueries.md, TASKS.md, PROGRESS.md
- notes: AABBで非交差を証明できる対象には狭域反復を行わないため、旧全探索がその対象でIterationLimitだったケースをNoHitと確定できる。候補内のIterationLimitは維持。旧APIの転送と新旧既定比較は続く別タスクで統一する。

- notes: 比較中、旧OBB rayのepsilon平行扱いによる明確な誤hitも実再現。物理private経路をdouble slab/厳密0判定と局所double法線へ修正し、共有Mathは変更しない。

- notes: 第1周でSweep OBBのfloat/double Gram検証差を発見。既存Mathのdouble検証をIsValidSweepBoxへ抽出してSweep本体とprecheckで共有する。Mathの受理条件・計算意味は変更しない。

## G1-GR08-P4E: 旧rayとoverlapを共通query経路へ転送する
- status: done
- done-when: 旧署名を維持してPhysicsModule/Broadphaseのray/overlapを共通queryへ転送し、旧hit型へ値変換する。旧Overlap法線はquery→対象の向きへ戻し、Point/Depth/UserData/順序と同距離rayの大handle優先を維持する。既定filterの新旧結果を固定seedで比較する。
- verify: productionで使うspan版旧型adapterを実Broadphase/Mathへ直接リンクし、混在shape・既定新旧4種・全field/順序・同距離・失敗clear・不十分bufferを通常/O2/sanitizer/MEMBERで検証。実Module入口試験も追加しcompile試行、独立レビュー。
- stop-when: 法線符号/Depth/識別値欠落、順序規則変更、未対応fake providerの既存入口破壊、エラーをsuccess扱い、旧公開署名変更、blocking未解消。
- paths: Library/Modules/Physics/Private/Physics/PhysicsBroadphase.h, Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp, Library/Modules/Physics/Private/Physics/PhysicsModule.cpp, Test/Modules/Physics/PhysicsLegacyQueryTest.cpp, Test/Modules/Physics/PhysicsBroadphaseQueryTest.cpp, Test/Modules/Physics/CMakeLists.txt, Docs/Architecture/PhysicsQueries.md, TASKS.md, PROGRESS.md
- notes: 新旧とも有効な正規直交OBB/共通数値安全域を条件とし、範囲外はInvalidArgumentへ統一。旧有限検査で通っても数学的に無効なOBBやfloat計算保証外の巨大形状は互換保証に含めない。直接BroadphaseのOverlap追記契約は維持、Module/Sceneの失敗出力は空。

- notes: 第1周でLayer0の旧無filter契約を確認。旧adapterだけ内部kernelのfilter適用を無効にし、新APIのbitmask条件は維持。4種類の旧hit/新NoHitを回帰へ追加する。

## G1-GR01-TRANSFORM-BOUNDARY: 群境界で子階層のworld姿勢を確定する
- status: done
- done-when: Roadmap G1リスクの段階境界確定に従い、Movement等で親が動いた後のDefault/Animation/PoseFinalize、Camera後のPreRenderから子world姿勢を同frameで読む。既存群順/収集/削除/Fixed規則を維持する。
- verify: 実WorldTickGroupTestへ親子階層の各群境界・同群の非暗黙確定・追加翌frame・Late境界の回帰を追加。可能なportable群/dispatch回帰と実統合compile試行、独立レビュー。Windows.h阻害は未実行と記録する。
- stop-when: callbackごとの全走査、同群順序/寿命の破壊、未承認scheduler変更、Windows統合の偽装。
- notes: G1最終照合で、現DispatchTickGroupsは前半5群を一括走査し変換確定をLate境界だけに行う点を発見。Entity::GetWorldTransformは親ありdirtyでcached値を返すため、先の群で変更した親への追従が不足する。

## G1-VALIDATION: 実装一覧とクラウド検証範囲を確定する
- status: done
- done-when: GR01/03/04/08の実装、実行済みportable試験、未実行統合/実機確認を区別する一覧をDocs/Architectureへ保存する。G1追加Public header29件の明示登録を揃える（監査でCurves.hのみ漏れ）。
- verify: 実ログ/command/sourceを照合し34件成功とcompile blocked1件を別記する。Core PUBLIC_HEADERSと追加headerを照合し重複を検出する。新たなWindows/CI合格を主張しない。
- stop-when: CPU純kernelをWorld/Engine統合の合格と扱う、未実行項目をpassにする、未承認のG2選定を確定する。

## G2-P0-COOK-LIB: 既存cookerを直接リンク可能な基盤へ分離する
- status: done
- done-when: GR77/GR78以降の共通基盤としてAudio/Mesh/Texture cookerのcpp/hを単一STATIC AssetCookLibへ所属させ、AssetCook実行ファイルはMain.cppのみを持ち同libをリンクする。公開headerとCore依存をconsumerへ伝搬し、二重compileしない。
- verify: target source/include/link関係と既存smoke登録のbyte不変を確認。cooker/Main.cpp内容が不変であることを照合。実AssetCook/CookedMeshTestとRaw/Texture/Mesh/Audio smokeは環境が対応する場合に実行し、できない場合は未実行と明記する。
- stop-when: 分離のために形式/CLI/platform実装/描画の変更が必要、新規test実行ファイル、Windows依存の代替stub。
- paths: Tools/AssetCook/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: G2実装開始は作者承認済み。G2-S1〜S8の推奨案の包括承認ではない。まず形式/既定値に依存しない分離を行う。

## G2-SELECT-S8: GLBの共有解析方針を確定する
- status: done
- done-when: 作者の選定を記録しGR77へ適用する。
- notes: 2026-10-03 UTCに既存JsonDocument/stbを活用するGLB共有処理を承認。新規外部glTF parserは追加しない。BVH/FBXは質問範囲外で、この承認に含めない。

## G2-SELECT-S2: 取り込み設定の正本を確定する
- status: done
- done-when: 作者の選定を記録しGR78/GR96へ適用する。
- notes: 2026-10-03 UTCにAを承認。ソース隣<ソース名>.import.jsonを正本にしてcook/looseで共用する。欠如時は恒等。イベント/ソケット等のruntime定義と取り込み変換は分離する。

## G2-GR86-LIMITS: 現行の関節上限を共有定数へ集約する
- status: done
- done-when: glTF decoder/cooker/cooked loaderの現行128関節上限を1つの公開定数へ揃える。値・拒否条件・エラー・format/vertex ABIを変更しない。
- verify: 対象3箇所の参照と旧128境界の意味不変を照合、公開headerのcompileとPUBLIC_HEADERS登録、可能な関連CPU回帰、実統合compile制約を明記する。
- stop-when: 未承認の256関節化、JOINTS_1拒否変更、format v1への先行変更、既存型/描画ABIの変更。
- notes: GR86前半の選定不要部分。G2-S4の縮約/焼き込み/Strict変更とStage Bの上限拡張は保留したまま進める。

## G2-GR77-CONTAINER: GLBの共有バイトコンテナを解析する
- status: done
- done-when: 共有のGltfContainerが借用SpanでGLB v2 header/JSON/BINを範囲安全に分解し、非GLB入力はBOMを除いたJSON viewとして区別する。未知chunkを無視し、既知chunk順/重複/長さ/切断を拒否する。BINのコピー/ファイルI/O/JSON意味解析は行わない。
- verify: 既存CookedMeshTest束へMEMBERを追加し、正常/未知chunk/切断/overflow/順序/再利用時出力clear/入力非変更と借用寿命契約を実parserの通常/O2-NDEBUG/sanitizer/MEMBER compileで確認する。
- stop-when: 入力越境/overflow、構造上無効なGLBを成功扱い、所有権を偽装、Windows stub導入、3経路への接続完了と誤認。
- paths: Library/Core/Private/Resource/GltfContainer.h, Library/Core/Private/Resource/GltfContainer.cpp, Library/Core/CMakeLists.txt, Test/Core/Asset/GltfContainerTest.cpp, Test/Core/Asset/CMakeLists.txt, Docs/Architecture/AssetImportPipeline.md, TASKS.md, PROGRESS.md
- notes: GR77最初の部品。buffers[0]/byteLength/padding、data URI/base64、画像、cooker/loose/骨格への接続は後続タスク。G2-S8のGLB部分だけを承認済みとして使う。

## G2-GR77-BASE64: 埋め込みdata URI向けの厳密base64を共有化する
- status: done
- done-when: Span入力/出力の無割当primitiveでRFC4648標準alphabet・padding・末尾未使用bitを検証し必要長と復号を提供する。容量/入出力重なりを拒否し、失敗時は出力bytes不変/長さ0。
- verify: RFC既知例、全1/2byte入力と固定seed3byte、非正規padding/無効文字/空白/切断、容量/alias/出力tail保持を通常/O2/sanitizer/MEMBERで実行する。
- stop-when: padding不正の黙認、部分書込み、入力越境/overflow、data URI/JSON/ファイル接続を完了扱いする。
- notes: GltfBufferSet/画像のdata URI解決が使う下層部品。URIの構文・percent decode・MIMEは別層で扱う。

## G2-GR77-BUFFER-SEMANTICS: data URIとGLB BINの意味境界を検証する
- status: done
- done-when: 共有の無割当helperでdata URIの対応MIME/base64形式/parameter/percent escapeを扱い、GLB buffer0の宣言長と0〜3byteのゼロpaddingを検証する。失敗時の出力非変更/空viewと借用寿命を明示する。
- verify: 既知data URI、大小文字/parameter/percent、壊れたescape、対応外MIME/encoding、BIN index/長さ/padding/欠落を実helperで通常/O2/sanitizer/MEMBER確認。ファイルI/O/JSON/ownedBufferSetは後続と区別する。
- stop-when: 非base64や対応外画像の黙認、percent後のNUL/経路を無条件に安全と扱う、出力部分書込み、BINコピー。
- notes: GR77のGltfBufferSet/画像が使う意味検証を先行し、cooker/loose/skeletalは後続で接続する。

## G2-GR77-BUFFER-SET: 外部と埋め込みbufferの所有と参照を共有化する
- status: done
- done-when: 所有する外部/data URI bytesと借用GLB BINを共通BufferSetで解決し、宣言範囲のaccessor viewと外部hash用の全source bytesを区別する。自己所有配列へのSpanを保存せずcopy/moveで参照を壊さない。JSON/消費経路接続は後続へ分ける。
- verify: descriptor・data URI・BIN・reader失敗・容量/宣言長・copy/move/再利用・出力契約の実testを既存束へ追加。実source syntax/型特性と可能なCPU helper回帰を確認し、実allocator/Windows依存の未実行を明記する。
- stop-when: BINコピー、旧.gltf hashのBOM/外部余剰byteの消失、借用寿命の隠蔽、read callbackによる境界迂回を安全と主張、偽allocator/Windows stub。
- notes: 3経路はまだ接続しない。外部URIはpercent後の相対path検証と実filesystem containmentを通す。data URIの受理追加はGR77の意図的仕様変更として旧拒否smokeを更新する。

## G2-GR77-BUFFER-JSON: JSONのbuffer記述を共有resolverへ変換する
- status: done
- done-when: 既存JsonDocumentのbuffersから型/正の安全整数byteLength/uriの有無/ASCII文字列/重複既知fieldを検証し、借用descriptorの寿命を保ってBufferSetへ渡す。3consumerのJSON読取りを共有できる入口を用意する。
- verify: 既存CookedMeshTest束へJSON記述の正常/欠落/型違い/重複/数値境界/BIN/data URIを追加。実型の宣言・呼出を照合し、Windows依存でcompileできない範囲と純helper回帰を区別する。
- stop-when: null uriを省略と誤認、doubleからsizeへの範囲外cast、移動したURI配列へのSpan、未承認の形式/既定値変更。

## G2-GR77-LOOSE-BOUNDS: loose accessorの範囲計算を安全化する
- status: done
- done-when: GLTFAnalyzerのunchecked startOffset/count*strideをcheckedな宣言buffer/view範囲検証へ置換し、GR77共有buffer接続前にoverflowによる越境を防ぐ。正常既存fixtureの属性値は維持する。
- verify: count/stride/offsetの整数境界を実純helperと既存staging試験へ追加し、旧正常値の不変と不正拒否を確認する。
- notes: buffer解決の共有化だけでは現在のValidateAccessorBoundsのunchecked加算/乗算は解消しない。loose接続の前提として別タスク化。

## CORE-MEMORY-HEADER-UTILITY: メモリAPIヘッダの標準includeを自己完結させる
- status: done
- done-when: MemoryOverrides.h単体でstd::forwardの宣言不足にならないよう必要includeを明示し、動作を変更しない。
- verify: 実header単体のsyntax確認。MemorySystem全体のWindows依存解消とは扱わない。
- notes: G2 BufferSetの実allocator接続compileで既存不足を検出。G2の取り込み実装とは別の小修正候補として保存。

## G2-GR77-COOK-BUFFERS: 静的cookerへ共有GLBとbufferを接続する
- status: done
- done-when: CookGltfToNvmeshが共有container/JSON buffer resolverを使い、GLB BINとdata URI bufferを読める。既存geometry/material/default/formatを維持し、旧.gltf hashはBOMと外部buffer全量を保存、新embedded内容を二重hashしない。
- verify: 既存静的cooker fixtureをJSON+external/GLB/data URIで比較する登録試験とsmoke更新を追加。payload一致/metadata/hash変化/不正の拒否を固定し、Windows統合未実行を明記する。
- stop-when: BINのコピー、既存material色空間/既定の変更、既存.gltf外部画像の新規実読込、自前parserへ未承認切替、3経路全完了の誤認。
- notes: embedded imageは次段の所有/借用とMain packagingで接続する。既存data_uri_buffer negativeは1byteで宣言長を満たさず、受理追加後も短さで失敗する。

## G2-GR77-IMAGE-SOURCE: 画像の外部参照と埋込みbytesを共有解決する
- status: done
- done-when: GltfImageSourceでimagesのfile URI/data URI/bufferView+mimeTypeを分類し、埋込みPNG/JPEGのbytesと寿命を明示する。URIとbufferViewの排他・既知field型/重複・buffer宣言範囲を検証し、外部画像はここでは読み込まない。
- verify: 既存束にfile/data URI/BIN view/外部buffer view・MIME/範囲/型/所有copy-moveの契約試験を追加し、実行可能helperとWindows依存未検証を分ける。
- stop-when: BIN全体コピー、所有bytesへの自己Span保存、壊れたdata URIや範囲外viewの受理、material色空間/format既定の変更。
- notes: MeshCookResultへの寿命移譲、Mainのmodel+texture出力/manifest一括更新、loose staging接続は後続タスク。GR77完了とは分ける。

## G2-GR77-COOK-IMAGES: 静的cookerの埋込み画像を結果へ保持する
- status: done
- done-when: GltfImageSourceからPNG/JPEGを材質roleごとに解決し、決定的な仮想pathとsrgb/linear formatをNVMESH参照とEmbeddedImagesへ保存する。GLB画像は元sourceへ借用し、cookerローカルBufferSet/data URIの画像は結果側が所有する。同一画像の互換roleは重複させず、色空間衝突は明示拒否する。
- verify: 外部imageの参照のみ経路維持、GLB/data URI/外部buffer内imageの寿命、決定path/format/重複と衝突、失敗出力不変を既存束へ登録。実行可能範囲とnative未検証を明記する。
- stop-when: BIN全体コピー、局所所有bytesへのdangling Span、既存外部画像の無条件実読込、material既定変更、Main出力まで完了と誤認する。
- notes: Mainのmodel+N texture package/manifest一括更新は別タスク。現行albedo/normal/ARMのみ接続し、occlusion/emissiveの描画仕様を勝手に増やさない。

## G2-GR77-PACKAGE-IMAGES: モデルと埋込みtextureを一括出力する
- status: done
- done-when: MainでEmbeddedImagesを既存texture cookerへ渡しmodel+N個の単一entry packageを作る。既存manifestを任意のAssetCookedReference群で統合し、無関係entry/骨格metadataを保持する。全候補の変換とpackage検証後にmanifestを1回更新し、model-only拒否guardを置換する。
- verify: 小GLBのmodel1+texture3、role format/path/hash、全AssetSystem解決、同key更新/無関係entry維持/重複拒否、失敗時manifest非更新を既存smoke方式へ追加。旧外部画像なし経路とaudioの回帰を固定し、native未検証を明記する。
- stop-when: 多entry NVPKG形式への変更、materialの未承認既定変更、未生成textureを参照するmodelだけを成功出力、formatをmanifest keyと誤認する。
- notes: GR111の多entry形式は導入しない。model外部画像の既存single出力は保持し、manifest keyはlogicalPath/kind/variant。texture参照のdefault variantを守る。

## G2-GR77-SKELETAL-BUFFERS: 骨格デコーダとcookerへ共有GLBを接続する
- status: done
- done-when: SkeletalGltfDecodeのbuffer読込/accessor参照を共有BufferSetへ接続し、元bytesのGLB/JSON入口をcookerとAnalyzeSkeletalから利用する。旧String入口も保持し、既存骨格契約/128上限/単一clip/変換/formatは変えない。hashは元sourceと全外部bufferだけを含み、BINを複製しない。
- verify: M9の既存fixtureをJSON外部/GLB/data URIで表し、頂点/関節/clipとNVSKEL payload/hashの比較、不正buffer拒否を既存束へ追加する。実native未検証と共有helper成功を区別する。
- stop-when: Armature親/clip複数/256関節への未承認拡張、NVSKEL形式変更、String互換APIの破壊、hash/BIN借用寿命の逸脱。
- notes: GR82の骨格契約変更やStage A/Bを先取りしない。画像/材質を骨格側へ新たに導入しない。

## G2-GR77-IMAGE-STAGING: 埋込み画像bytesをCPU texture stagingへ渡す
- status: done
- done-when: ModelStagingの画像decodeをfile読込とbytes読込へ分け、PNG/JPEG bytesから標準RGBA8とARMの3つのR8 stagingを生成できる。既存外部画像の既定format/チャンネル写像を維持し、input長/dimension境界とstb所有解放を守る。
- verify: 同じ小PNGのfile/bytes pixel一致、ARM分解、異常bytes/大きい長さの拒否、stagingの所有と失敗出力契約を既存束へ追加。native未検証と実stb/helper確認を区別する。
- stop-when: albedo既定色空間の変更、GPU生成、stb確保の例外時漏れ、借用pixelsの返却。
- notes: GLTFAnalyzerの画像参照接続は後続。空RequestPathとHasDataによるfinalize契約を保持する。

## G2-GR77-DOCUMENT-PROFILE: 必須glTF拡張の拒否規則を共有する
- status: done
- done-when: extensionsRequiredの型/重複と非対応必須拡張を共有判定し、静的cooker/loose/骨格で黙って無視しない。現時点の許可リストは空で、既存静的cookerの通常診断を維持する。
- verify: 無指定/空配列/型違い/重複/Draco/meshopt/quantization/texture_transformの拒否を既存束へ登録。native未検証を明記する。
- stop-when: 未実装拡張の受理、optional extensionsUsed全体の無差別拒否、骨格/材質の未承認仕様拡張。

## G2-GR77-LOOSE-SOURCES: loose静的モデルへGLBと埋込み画像を接続する
- status: done
- done-when: GLTFAnalyzerの静的stagingを共有container/BufferSet/ImageSourceへ接続し、GLB/data URIのgeometryとPNG/JPEG画像をCPU所有stagingにする。埋込み画像のRequestPathは空、既存外部画像の論理参照とRGBA8/ARM分解を保持する。
- verify: 小GLB・data URI・外部入力でgeometry/5staged textures（元画像3枚）を比較し、元source破棄後の所有と必須拡張拒否を確認する登録試験。Windows/GPU未検証と区別する。
- stop-when: BIN全体コピー、埋込み画像をcooked manifest参照へ回す、既存材質既定の変更、full static parser統合の先取り。

## G2-GR77-LOOSE-INDEX-RANGE: クラスタ生成前に頂点インデックスを検査する
- status: done
- done-when: loose geometryの全indexがvertices範囲内であることを、MeshClusterizerへ渡す前に検査する。不正入力は失敗し既存出力を保持する。
- verify: 3頂点に対するindex=3/65535の拒否と正常0/1/2の受理を既存staging試験へ登録。純粋な共通検査も境界と空/nullを検証する。
- stop-when: clusterizer全体改修、正常meshのwindingや材質変更、native未実行を合格扱いする。
- notes: GLTFAnalyzer::ExtractMeshDataはindex値を未検査で返し、MeshClusterizer::ComputeNormalCone/ComputeBoundingSphereがその値で頂点を参照する。LOOSE-SOURCES前に安全化する。

## G2-GR77-MIME-SIGNATURE: 埋込み画像の宣言MIMEと実形式の一致を検証する
- status: done
- done-when: PNG/JPEG宣言と実signatureの矛盾を、cook packagingとlooseの同じ規則で拒否する。外部画像の従来decoder形式は狭めない。
- verify: PNG宣言/JPEG signatureと逆の拒否、正常一致、失敗時出力保持を既存束へ登録する。
- stop-when: 新形式追加、native未実行を合格扱い、材質や色空間既定を変更。
- notes: 現行は対応signatureかどうかだけを判定する。GR77接続の非blocking残件として登録。

## G2-GR77-NATIVE-ACCEPTANCE: 実物GLBとnative統合の受入れを記録する
- status: blocked
- done-when: 実物GLBと既存glTFのcooker/loose/骨格の受入れを実行し、必要な範囲でgeometry/texture/hashと寿命を確認する。
- verify: CookedMeshTest、AssetCookGlbSmoke、実物大型GLBのcpu/cooked/表示結果を区別して記録する。
- stop-when: pure helperやfixture構文の成功をnative統合の成功として扱う。
- notes: 現cloudはWindows.h依存で本体compileできず、計画書が参照するDogGameの実物大型GLBも未配置。作者は非描画Windows検証を必須から外しているため、独立実装は継続する。

## G2-GR78-SETTINGS: 共有取り込み設定の値とJSONスキーマを定義する
- status: done
- done-when: Core privateにcook/loose共用のv1設定を置き、units/axes/origin/meshとmeta・将来予約blockを厳格解析する。未知/重複/非有限/矛盾を拒否し、出力は成功時だけ更新。固定順LE正規化bytesはmeta/JSON書式に依存しない。
- verify: 純粋な値検証/固定bytesを通常・最適化・sanitizerとMEMBERで検証。実JsonDocument試験は既存束へ登録し、Windows依存による未実行を明記する。
- stop-when: sidecar無しの現行挙動変更、未実装repair/lod/material/collision/clipの非空指定を受理、未承認材質既定やモデル形式の更新。
- notes: S2承認済み。CoreからToolsへ依存させないため解析もCore privateへ配置し、AssetCookLibとlooseが共有する。file探索・変換適用・hash連結・CLIは別反復。

## G2-GR78-TRANSFORM: 取り込み変換の共有演算を実装する
- status: done
- done-when: 軸正規化・一様scale/fit・mirrorX・keep/bounds中心/足元/custom原点・法線/UV/windingをdoubleで合成し、float表現可能性を検証して静的geometryへ適用する。骨格は一様scaleのみの制約を保持する。
- verify: 軸写像、fit、各原点、鏡像、法線、UV、極端値/縮退/失敗出力を検証する。
- stop-when: 骨格への未承認軸/原点変更、描画既定/形式変更、非有限やoverflowの黙認。

- notes: 2026-10-03作者承認によりsurface_centroidを未対応として明示拒否し、他変換を先行する縮小scopeで再開。表面重心は別保留へ分離。

## G2-GR78-SIDECAR: ソース隣の設定読込と変換・hashを接続する
- status: done
- done-when: <source>.import.jsonをcook/looseで読み、無しは既存bytes/hash不変、有りは共有変換と正規化設定hashを適用。骨格の位置/IBM/translation/mesh-nodeを同時scaleする。
- verify: sidecar無し回帰、有無/値変更/書式とmeta変更、失敗保持、static/skeletal/cook/looseの一貫性を既存束へ登録する。
- stop-when: cook/looseの設定解釈差、未承認形式更新、実native未実行の隠蔽。

## G2-GR78-CLI: 設定指定・require・skipと診断の入口を追加する
- status: done
- done-when: import-settings/no-sidecar/require-sidecar、hashとpackage実体一致を条件にしたskip、inspect診断を追加する。既存必須CLIとsidecar無し出力は維持する。
- verify: 設定優先順位/相互排他/失敗、skip時非更新、package欠如/破損時再cook、inspectをsmokeへ登録する。
- stop-when: GR96一括cook/依存追跡の先取り、誤ったskip、未実装オプションの受理。

## G2-GR78-SIDECAR-IO-HASH: 設定fileの読込とhash連結を独立実装する
- status: done
- done-when: source隣/明示override/無効/必須の設定file選択と安全なread・厳格parseを共有化し、sidecar無しの旧hashを保持、有りの正規化bytes+algorithm version連結を定義する。geometryへはまだ適用しない。
- verify: absent/invalid/required/override/conflict/出力保持をnative登録し、純hashの既知値/無し不変/値変更とバージョン変更を実行する。
- stop-when: 保留中ImportTransformを未確認でロードへ接続、file不在以外のI/O失敗を無し扱い、meta/書式差でhash変更。
- notes: GR78-TRANSFORMの独立確認保留中に先行できるSIDECARの部分作業。

## G2-GR78-SURFACE-CENTROID: 表面重心の極端値精度を確認する
- status: blocked
- done-when: 面積重み原点の極端値相殺と循環/面列挙順依存を解消し、独立確認後にsurface_centroidを有効化する。
- verify: 2^100/2^-100の等面積2面、巨大正負座標/微小残差、面積/重心/実頂点結果を独立反証する。
- stop-when: 未確認実装をロードへ接続する、他の原点選択へ無言fallbackする。
- notes: 2026-10-03作者は他変換を先行し、この機能は未対応として明示拒否する方針を承認。以前の候補はa6391b2の履歴に保持、active実装からは除く。

## G2-GR78-STATIC-SIDECAR: 静的cookとlooseへ設定を適用する
- status: done
- done-when: 共有loaderと承認済み変換を静的meshのcluster/bounds前へ挿入し、無しなら旧bytes/hashを保ち、有りなら正規化設定hashを連結する。override/disabled/requiredを内部APIへ渡せ、採用設定を診断可能にする。
- verify: sidecar無し/disabledのpayload同値、scale/fit/origin/UV/windingのcook-loose同値、meta/書式hash不変と値変更、surface拒否と出力保持を既存束へ登録する。
- stop-when: surface_centroidの再有効化、骨格への無言適用、未実装オプションの黙認、native未実行を成功扱い。

## G2-GR78-SKELETAL-SIDECAR: 骨格へ一様scaleと設定hashを適用する
- status: done
- done-when: 頂点位置/IBMの平行移動/animation Translation/mesh-node平行移動を同じ正の一様scaleで変換し、cook/loose/legacyで共有する。軸・鏡像・原点やmesh変更は未対応として拒否する。
- verify: sidecar無し同値、scale前後のskinning結果、fit倍率、回転/scaleチャンネル不変、範囲外と拒否時出力保持を既存束へ登録する。
- stop-when: 未承認NVSKEL形式変更、骨格の軸/原点変換、native未実行を実skinning成功扱い。

## G2-GR78-OUTPUT-SIDECAR-GUARD: cook出力による設定file上書きを拒否する
- status: done
- done-when: 採用したsidecarとmodel package/manifest/embedded texture packageのpath・symlink・hardlink aliasを、書込前に拒否する。
- verify: 有効sidecarをpackage/manifest出力先にしたCLI回帰で失敗と既存設定bytes保持を登録し、通常出力を妨げないことを確認する。
- stop-when: 設定正本を出力で上書きする、途中書込後にaliasを判定する、TOCTOU完全防御と主張する。
- notes: 静的接続で追加された新規入力sidecarの保護。通常出力pathの既存挙動は維持する。

## G2-GR78-CLI-SETTINGS: CLIの設定選択を共有loaderへ接続する
- status: done
- done-when: --import-settings/--no-sidecar/--require-sidecarをstatic/skeletalへ渡し、相互排他/重複/空値/非model用途を明示拒否する。明示設定が自動探索より優先し、無効化は読込を省略する。
- verify: 実option parserの引数/値/順序/失敗保持とCLI設定優先順位/拒否/設定保護を登録する。
- stop-when: 未実装skip/inspectの受理、既存必須引数の緩和、native未実行をCLI成功扱い。

## G2-GR78-CLI-SKIP: 未変更modelのcookを入力hashで省略する
- status: done
- done-when: cook前に元入力/buffers/設定のhashを計算し、同じmanifest keyのsource_hash・要求format/entry/output先と実packageのcooked_hashを検証した場合だけskipped終了する。派生画像packageも欠損/破損なら省略しない。
- verify: preflight hashとcook hash一致、設定編集/無変更/meta変更、出力欠損/破損/要求先変更/画像欠損の回帰を登録する。
- stop-when: 本cookを実行してからskip扱い、hash一致だけでpackage実体未検査、GR96一括依存追跡への拡大。

## G2-GR78-CLI-INSPECT: cookしないmodel診断を追加する
- status: done
- done-when: --inspect <file>でbounds/軸長/頂点三角形数/位置溶接と成分/ゼロ法線/画像寸法・チャンネル統計/材質係数と設定叩き台を診断し、前方向の符号を勝手に決めない。
- verify: 小型既知fixtureの幾何/画像/材質値、無出力cookと入力非変更を確認する。本人実物は入手後に別受入れ。
- stop-when: 診断のためpackage生成/既存sidecar上書き、未知方向を確定として出す、未入手実物を合格扱い。

## G2-GR78-SOURCE-PREFLIGHT: cook前の入力hashと派生画像記述を共有化する
- status: done
- done-when: geometry変換/cluster/画像decode/package生成なしで元source/buffers/設定のcookと同じhashを返し、embedded画像の論理path/format/hashを所有metadataとして返す。設定loaderを本cookと共有し、失敗時出力保持。
- verify: glTF/GLB/data URIと設定有無/変更/meta/無効/必須、骨格、embedded画像metadata、外部buffer全量のhash同値をnative既存束へ登録する。
- stop-when: preflight成功をgeometryの妥当性保証とする、失敗の部分公開、画像bytesの寿命をcallerに漏らす。

## G2-GR78-INSPECT-GEOMETRY: 無変換の幾何診断kernelを実装する
- status: done
- done-when: bounds/軸長/最長軸・頂点/三角形数・数値完全一致の位置溶接数・頂点共有成分数・ゼロ法線を変更なしで計測する。符号付きゼロは同一、未参照頂点は孤立成分と明示する。
- verify: 既知の分離三角形/継ぎ目/孤立/縮退、非有限/不正index/workspace重複、微小値/float両極端、面順序の純実装試験。
- stop-when: epsilonを無言適用、source geometryを書換え、未検証kernelを診断CLIへ接続する。

## G2-GR78-INSPECT-IMAGE: 画像寸法とチャンネル統計を診断する
- status: done
- done-when: PNG/JPEGを既存stbで展開し、8/16bitの寸法/decoded channel数と整数min/max/meanを取得。色空間線形化はせず、最終pixel payloadの512MiB上限を事前検査する（stb内部総メモリの制限ではない）。失敗時結果保持。
- verify: 既知8bit RGBA/16bit gray、壊れたPNG/JPEG/未対応/巨大dimensions/aliasの実stb試験を3modeとMEMBERで確認する。
- stop-when: 16bit画像を無言で8bit化する、巨大decodeを上限なしで始める、source画像を編集する。

## G2-GR78-VALIDATION: 実装範囲と検証状態を整理する
- status: done
- done-when: GR78の承認済み実装とsurface保留、純実装の実行結果とnative/実物の未検証を分けて記録する。
- verify: 各task/commit/実log/CMake登録を照合し、次のG2選定を承認済み扱いにしない。
- stop-when: Windows/native/実物を未実行のままPASSとする、surfaceを実装済みへ含める。

## G2-SELECT-S4: GR86の縮約/焼込/Strict方針を確定する
- status: done
- done-when: 作者の回答を記録し、その範囲内で後続taskを作る。
- notes: 2026-10-03に推奨A（上位4本縮約、CUBICSPLINE焼込、morph明示drop、Strict余剰拒否、v1時256）を作者が承認。

## G2-SELECT-S1: NVSKELの版と骨格識別方針を確定する
- status: done
- done-when: 作者の回答を記録し、GR32/GR82の形式を同じ定義へ揃える。
- notes: 2026-10-03に推奨A（0.2へsubmesh/複数clip統一、材質仕様後v1、SkeletonIdにrest poseを含めず骨長差はGR85）を作者が承認。v1 clip作成時rest pose保持と束縛時の既定拒否/明示許可/差分報告を追加。企画の骨格共有は未定へ訂正。S7のmesh数条件は別未確定。

## G2-S1-S4-DECISIONS: 承認とrest pose安全契約を計画へ反映する
- status: done
- done-when: S4/S1 A採用、企画共有案未定への訂正、v1 clip作成時rest pose/束縛時比較/既定拒否/明示許可/差分報告を正本へ記録する。
- verify: roadmap/requirements/handoffの競合前提を照合し、他選定を承認済みにしない。
- stop-when: rest pose不一致をSkeletonId一致だけで許可する、未定の造形/共有案を採用済みへ戻す。

## G2-GR86-STRICT-INFLUENCES: 追加ウェイトセットの黙認を拒否する
- status: done
- done-when: StrictのJOINTS_n/WEIGHTS_n（n>=1）を専用InfluenceLimitExceededで拒否し、既存status値と通常4影響入力の挙動を保つ。
- verify: 属性名分類の純試験、JOINTS_1/WEIGHTS_1有無/値/片側/大きいn/不正名、raw/legacy/cookerの拒否と出力保持を登録する。
- stop-when: 未実装Reduce optionの受理、関節上限/頂点ABI/形式変更、既存status番号の変動。

## G2-GR82-V1-REST-POSE-GUARD: clip作成時骨格の差を束縛時に検査する
- status: blocked
- done-when: v1で各clipの作成時rest snapshotを保存し、現在骨格との差が許容超過なら既定拒否、明示許可時だけ通し、差量と超過関節を報告する。
- verify: 同名同階層・異なるrest、全関節Translationを含む古いclip、許容内/外/明示許可/q符号同値、欠落/不正snapshot、cook/parse/bind往復。
- stop-when: SkeletonIdからrestを除いたことを無条件互換と扱う、古いclipの歪みを黙認する。
- notes: 作者承認済みのStage B必須要件。blocked理由はv1形式とStage A等の前提待ちで、作者の再承認待ちではない。

## G2-GR86-REDUCE-KERNEL: 4影響への決定的な縮約を実装する
- status: done
- done-when: 全影響の有限/非負/関節範囲/総和を検査し、同一関節をまとめ、重み降順/同値joint番号順で4本を選び正規化する。脱落量・警告・許容超過を返し、失敗時vertex出力保持。
- verify: 5影響既知値、同一joint合算、入力順置換、同重みtie、微小重み、ゼロ/負/非有限/不正joint/総和、閾値境界、出力保持/aliasを純試験で検証する。
- stop-when: glTF正規化整数のraw総和検査を省く接続、未検証kernelのdecode適用、Strict/頂点ABI/形式の変更。

## G2-GR86-OPTIONS-HASH: 縮約指定と診断の型・hashを定義する
- status: done
- done-when: Strict既定/ReduceToFourの明示指定と警告・失敗閾値、走査範囲付きreportを独立型にし、Strictは既存hash不変・Reduceだけ正規化policyをsource hashへ連結可能にする。
- verify: 値域/未対応enum/Strict未使用閾値/符号付きゼロ/既知bytes/FNV状態/既定不変/閾値・algorithm変更を純試験で固定する。
- stop-when: 未実装Morph/Cubic optionの受理、未接続をCLI完了扱い、struct paddingでhashが変わる。

## G2-GR86-INFLUENCE-SETS: 複数joint/weightセットの記述を検証する
- status: done
- done-when: Reduce用にJOINTS_n/WEIGHTS_nを正準順へ集め、0始まり連続・同数pair・重複なし・accessor番号の型/範囲を検証して所有結果を返す。Strict gateは変更しない。
- verify: 順序違い/複数pair/欠損/非連続/重複/不正名/不正番号と失敗出力保持をpure/nativeに分けて検証する。
- stop-when: raw整数weight総和やjoint範囲検査を省くdecode接続、未検証のReduce受理、Strict既定の緩和。

## G2-GR86-REDUCE-DECODE: 明示Reduceを骨格decodeへ接続する
- status: done
- done-when: options指定時だけ全joint/weightセットの型/count/range/整数raw総和を検査し、共有kernelで4本へ縮約する。reportは正常prefixと失敗頂点の測定を区別し、raw/legacy/file入口で一致する。既定Strictは不変。
- verify: 5影響fixture、raw/GLB/legacy/fileの一致、閾値超過/負値/追加セット内の不正joint/UNORM総和/不正options、失敗Data/sourcebufferの非公開を登録する。
- stop-when: cooked wire/頂点ABI/128上限変更、未接続のcooker/CLI成功主張、捨てる影響の不正値黙認。

## G2-GR86-REDUCE-COOK: 縮約指定とhashを骨格cookへ接続する
- status: done
- done-when: 明示optionsをcook/preflightへ伝播し、同一source/settings/policy/algorithmから同じhashを得る。Strict hashとwireは不変、失敗out保持、縮約診断を返す。
- verify: Strict旧hash、Reduce cooked値/診断、preflight同値、warn/fail変更hash、閾値超過out保持をnativeへ登録。純hash回帰を実行する。
- stop-when: CLI未接続なのに利用可能と主張、形式/ABI/128上限変更、設定指定の黙殺。

## G2-GR86-REDUCE-CLI: 明示縮約と閾値をCLIから指定する
- status: done
- done-when: --skin-influences strict|reduceとwarn/fail閾値を厳密parseし、skeletal cook/skipの両方へ同じpolicyを伝播する。既定Strict/非骨格拒否/重複・不正・無意味指定拒否と脱落量診断を維持する。
- verify: pure parserの通常/O2/sanitizer/MEMBER、native CLI smokeで縮約成功・既定拒否・閾値失敗出力保持・policy変更でcache miss/同値でhitを登録する。
- stop-when: 未実装のCUBICSPLINE/morph/joint-policyを受理する、skipだけStrictのまま、native未実行を成功と主張する。

## G2-GR86-IMPORT-REPORT: 縮約・焼込・dropのJSON診断を共有形式へまとめる
- status: done
- done-when: 実装済みpolicyの測定と失敗を版付きImportReportへまとめ、GR84の将来拡張と区別する。CLI stderr警告と同じ測定を持つ。
- verify: JSON文法/数値/未完走査と失敗計測の分離/決定的出力、書込経路を足す場合は既存入力・出力保護。
- stop-when: 未実装のBVH/retargetを実装済みとして報告、既存出力保護の迂回。

## G2-GR86-CUBIC-KERNEL: CUBICSPLINEの評価と保守的誤差境界を実装する
- status: done
- done-when: double Hermite/Bezierの区間包絡・評価・分割と理想LINEAR近似の全区間誤差上界を定義する。float保存端点との差を含め、vector長さと回転角の単位を区別する。実samplerの数値丸め認証/短区間は次taskで扱い、このkernelだけでruntime保証としない。
- verify: 公式式の既知値、非単位時間、S字中点相殺、回転正規化/ゼロ/半球と実sampler条件、誤差境界を純kernelで反証する。
- stop-when: 中点標本だけで全区間保証とする、tangent符号の暗黙変更、誤差のメートル/角度混同、未接続decode受理。

## G2-GR86-CUBIC-RUNTIME: 実sampler条件で焼込誤差を認証する
- status: done
- done-when: 数学kernel上界に保存float時刻/値と実runtime数値条件を合わせ、短duration/半球/NLERPを扱う。保証対象・数値仮定・許容の下限を明記し、深さ/sample予算内で全区間が認証できた場合だけ所有LINEAR列を返す。
- verify: 中点相殺/巨大tangent/非単位dt/float時刻衝突/近ゼロquaternion/短duration/予算超過、最終キーと全区間境界。
- stop-when: 任意epsilonだけで形式的runtime保証とする、未認証区間を成功公開、元cubic符号変更、変換後メートル誤差を無視する。

## G2-GR86-CUBIC-POLICY: 焼込指定・許容・診断・hashを定義する
- status: done
- done-when: Reject既定/Bake明示、translationメートル/rotationラジアン/scale無次元の許容、depth/channel/asset sample予算と診断を定義。未使用指定を拒否し、Bake無しの既存Strict/Reduce canonical/hashを維持する。Bake時だけ全設定/algorithmをhashへ追加する。
- verify: pure validation/canonical/hash golden、既存Strict/Reduce無変更、数値下限/不正/上限/無意味指定の拒否。
- stop-when: 未接続decoderがBakeを黙って無視して成功する、既存hash回帰、未実装morph/256を受理。

## G2-GR86-CUBIC-DECODE: 明示焼込を骨格decodeへ接続する
- status: done
- done-when: CUBICSPLINE triplet/count/有限時刻を検査して共通bakerへ渡す。fitを含む最終translation倍率のメートル空間で認証し二重scaleしない。raw/legacy/file/cookを同じ経路にし、prefix診断と失敗Data/source非公開を維持する。既定Rejectの挙動を維持する。
- verify: Translation/Rotation/Scaleの既知曲線、glTF/GLB、source triplet不正・zeroq・容量/閾値超過、sidecar scale/fitと実helper/parse往復。native未実行は区別する。
- stop-when: 元cubicの符号変更、scale後の許容超過黙認、partial成功、native/GPU実行済みと偽る。

## G2-GR86-CUBIC-CLI: 焼込CLIとJSON診断を接続する
- status: done
- done-when: --cubicspline reject|bakeと許容指定をstrict parseし、cook/skipへ同じpolicyを渡す。焼込channels/keys/単位別上界/失敗をImportReportへ格納し、未走査はnullとする。
- verify: pure parser/JSON、nativeCLI成功・既定拒否・失敗保持・設定差cache鍵、診断単位。
- stop-when: 未実装morph/256指定を受理、cacheと本cookの設定不一致、未測定をゼロと偽る。

## G2-GR86-MORPH-POLICY: 明示dropの方針・数量診断・hashを定義する
- status: done
- done-when: Reject既定/Drop明示を公開optionsへ定義し、既存Strict/Reduce/Bakeのみのhashを維持する。Dropはalgorithmを含む別canonicalで区別し、未接続段階で指定を黙って無視しない。
- verify: enum/旧hash golden/Dropの全既存policy組合せと型単独includeを純試験で検査する。
- stop-when: 未接続Dropの成功黙認、既存cache鍵の予期しない変更、sparse/256の受理。

## G2-GR86-MORPH-DECODE: 明示dropを共通decodeとcookへ接続する
- status: done
- done-when: morph targets・初期weight・weight animationの除去を明示Dropだけに限定し、base geometryとTRSを維持する。対象の構造を検査し、除去数/走査状況を報告する。既定Reject/sparse拒否/失敗出力保持を維持する。
- verify: raw/legacy/file/GLB/cookの既定拒否/明示成功/数量/不正・sparse/他policy併用と出力保持を登録し、純部品の試験を実行する。
- stop-when: malformedをDropで成功扱い、baseへmorphを黙って焼込、未測定数を確定と偽る。

## G2-GR86-MORPH-CLI: 明示dropをCLIとJSONへ接続する
- status: done
- done-when: --morph reject|dropを厳密parseしcook/skipへ同じpolicyを渡す。除去数・警告・未走査nullをJSONへ格納し、既定は拒否する。
- verify: pure parser/JSONとnativeCLI既定拒否/明示成功/出力保持/設定差cache鍵を検査または登録する。
- stop-when: 指定無しの無言drop、sparse/256受理、native未実行を成功と主張。

## G2-GR86-VALIDATION: 前半の実装と検証範囲を固定する
- status: done
- done-when: Strict/Reduce/Bake/Dropの現行実装、純検証、native未実行、既知の入力制限、v1待ち256を分けて記録する。
- verify: 現行ソースから関連portable testsを再ビルド・実行し、native登録/ログ/公開branchと照合する。
- stop-when: Windows/GPU/実物assetの受入れを未実行のまま合格とする、GR86後半256まで完了と扱う。

## G2-SELECT-S3-MATERIAL-FORMAT: 共通材質レコードの載せ先を確定する
- status: done
- done-when: S3(a)のNVMESH v1/128B材質レコード/v0併読案と代案を作者へ確認し、GR79/GR32/GR82の共有仕様へ反映する。
- notes: S1のNVSKEL版承認はS3の材質選定承認を含まない。ARM既定とemissive nitsは別判断として保留する。

- decision: 2026-10-04作者承認。S3(a)はNVMESH v1/128B材質/v0併読、S7は1mesh/Nprimitiveの案A。ARM/emissive既定は別途保留。

## G2-SELECT-S7-MESH-PROFILE: 骨格mesh数の受理範囲を確定する
- status: done
- done-when: S7の1mesh/Nprimitiveまたは同skin・同transform複数mesh案を作者へ確認してGR32へ反映する。
- notes: S1承認だけでmesh/node契約は緩和しない。

- decision: 2026-10-04作者承認。S3(a)はNVMESH v1/128B材質/v0併読、S7は1mesh/Nprimitiveの案A。ARM/emissive既定は別途保留。

## G2-GR32-GR82-MIGRATION-INVENTORY: 承認済み形式移行の変更箇所を整理する
- status: done
- done-when: 現行0.0/0.1の単一primitive/clip契約、統一0.2への影響箇所、v1作成時restの現在の欠落箇所と必須試験を棚卸しする。未選定S3/S7の結論・wire詳細を先取りしない。
- verify: 実header/decoder/cooker/loader/resource/component/testと作者の決定記録を照合する。
- stop-when: 受理条件/API/wireを変更、比較空間や閾値を未決のまま固定、未実装0.2/v1を実装済みと表現する。

## G2-GR32-SUBMESH-CONTRACT: 素のsubmesh型とpacked範囲検証を定義する
- status: done
- done-when: Asset/Resource層でRenderingに依存しないSubMesh/MaterialSlot型を持ち、1..8の三角形範囲が全indexを昇順・隙間/重複なしで覆うこととslot境界を検査する。両表空は旧互換の1範囲/slot0へ解釈し、片側空は拒否する。まだdecoder/wire/描画の受理は変えない。
- verify: pure kernelの正常1/2/8範囲、欠落/重複/隙間/逆順/非三角形/u32境界/片側空/null入力/失敗結果、MEMBER compileと既存pure回帰。
- stop-when: 0.2を書き始める、複数meshを受理、Render型へ依存、旧形式の判定/頂点ABI/描画既定を変更。

## G2-GR32-V02-SCHEMA: 統一0.2の版別wire契約を固定する
- status: done
- done-when: 旧0.0/0.1の256B/hash/単一clip契約を保ち、0.2の320B、submesh64B、Name16B+予約48Bのslot64B、複数clipの所有範囲と128関節を同じschemaへ定義する。版別header/hash/節検証のpure部品を用意し、writer/loaderの受理はまだ切り替えない。
- verify: 固定byte/独立hash golden、版別定数/節境界/overflow/reserved/未対応版、旧hash不変、MEMBER/CTest登録。
- stop-when: 同じminor2の予約を後でtextureへ転用、0.2へ共有128B材質を暗黙追加、旧版判定の全面緩和、writerを先に切り替える。

## G2-GR32-PRIMITIVE-DECODE: 1mesh内の複数primitiveを所有表へ変換する
- status: done
- done-when: 1mesh/1skin/1mesh-nodeを維持して1..8primitiveを連結し、local index検証後にbaseVertexを加えた絶対indexとpacked submesh/一意slot名を返す。Reduce/Morph/Bake/scaleの資産集計を保ち、下流未接続時は黙って材質を落とさず拒否する。
- verify: 2/8primitiveの連結/巻き/材質省略とindex0の区別、範囲/9件/複数mesh拒否、Reduce全体prefix/Morph幅と総数/fitを登録する。
- stop-when: primitiveを跨いだindex越境を許す、scale二重適用、未接続cook/runtimeが成功して表を失う。

## G2-GR32-V02-TEXT: 0.2の名前byte列と参照境界を検証する
- status: done
- done-when: 0.0/0.1のprintable ASCIIを維持し、0.2のUTF-8/NUL拒否とbyte単位StringRefを共通部品で検査する。CoreのTCHARと保存byteの変換契約を明確にし、失敗出力保持を満たす。writer/readerはまだ切り替えない。
- verify: ASCII/多byte/境界codepoint/不正UTF-8/overlong/surrogate/NUL/範囲overflow/失敗保持の純試験とMEMBER/CTest登録。
- stop-when: 旧版で非ASCIIを受理、壊れた文字列を置換して成功、UTF-16単位数を保存byte長と混同する。

## G2-GR32-V02-READER: 版別の骨格表とclip所有を読み込む
- status: done
- done-when: 旧0.0/0.1を保持して0.2の320B/submesh/名前slot/版別文字列を所有dataへ読む。record値/reserved/padding/範囲/hashとclip/channel/sampleの一意所有を検査する。新表を未対応下流が捨てないよう明示ガードを置く。
- verify: 手書き0.2/複数clip/旧golden、重複/隙間/跨ぎ/不正flags/bounds/文字列、失敗Data非公開。
- stop-when: 旧clipCount拒否を緩和、同minorで表を読み分ける裏分岐、部分資産公開、未対応runtimeへ無言で流す。

## G2-GR32-V02-WRITER: 統一0.2をcookして旧cacheを再生成する
- status: done
- done-when: submesh/slot/複数clip共通schemaの0.2を生成し再parseで照合する。1primitiveも名前slotへ具体化し、旧空表は互換1件へ変換する。cacheはcurrent writer minorを検査し旧版のままskipしない。既存出力保護を保つ。
- verify: raw/GLB/cook/parseの表一致、旧版loadとcache再生成、slot/UTF-8/範囲、CLI metadata/失敗保持。
- stop-when: table/nameを失う、旧cacheを新writer済み扱い、Resource/描画の未接続を成功と偽る。

## G2-GR32-RESOURCE-TABLES: 所有資産と不変leaseへsubmesh表を保持する
- status: done
- done-when: SkinnedMeshResourceがsubmesh/slot名を所有・検証し、不変AssetLeaseへ世代単位で保持する。旧空表/3引数lease互換を保ち、未対応描画への新表の無言破棄はguardする。
- verify: 旧互換/1・2・8表/片側空/範囲/名前保持/世代とlease寿命/不正時の非公開をCPU契約へ登録する。native未実行は明記する。
- stop-when: RenderThreadがmutable Resourceを参照、表の無言破棄、旧frame leaseの内容を後から書換える。

## G2-GR32-COMPONENT-SLOTS: 材質slotの名前APIとframe snapshotを接続する
- status: done
- done-when: SkinnedMeshComponentがindex/名前でslot材質を設定し、従来SetMaterial/GetMaterialの互換を保つ。FramePacketへ値所有で渡し、欠落slot/古いresource世代の扱いを定義する。
- verify: index/名前/重複・不在/旧単一材質fallback/世代差し替え/描画thread越境のCPU契約。
- stop-when: mutable componentを描画threadが読む、曖昧な名前を無言で別slotへ適用、既存材質を破壊する。

## G2-GR32-COOK-METADATA: cook metadataへ表数量を記録する
- status: done
- done-when: 新cookのmanifest metadataへsubmesh_count/material_slot_countを保存し、旧manifestの省略を未知として保持する。既存項目/merge/cacheを壊さず、欠落を1件と捏造しない。
- verify: 1/2/8表の数量、旧省略/不正値、manifest再読込/merge、既存CLI metadata回帰を登録する。
- stop-when: 旧省略から件数を推測して表示、wireとmetadataが不一致、既存metadataをmergeで失う。

## G2-GR32-DRAW-RANGES: submesh別描画とcomponent単位palette共有を接続する
- status: done
- done-when: AppendSkinnedDrawCommandsとRecordSkinnedDrawCallが絶対index範囲/baseVertex0/slot/NoShadowを使う。途中guardを接続済み経路で解除し、palette共有は別taskへ分ける。
- verify: 計画指定の独立SkinnedSubmeshDrawContractTestへdraw数/範囲/影flag/palette共有を登録する。GPU受入れは別gateに残す。
- stop-when: submeshごとに重いskinning準備を複製、範囲外draw、既定描画/シェーダーABIを変更、GPU実行無しでGR32全体完了を宣言。

## G2-GR32-GPU-ACCEPTANCE: 多材質と旧描画互換をWindows GPUで確認する
- status: blocked
- done-when: 2材質板のGPU readbackと旧NVSKEL/M9SkinnedのGBuffer全画素一致、既定Rendering3DTestを実機で確認する。
- verify: Windows/Vulkanで独立draw契約とGPU readback、起動画面を取得して実画像/結果を確認する。
- blocked-by: 現在のクラウドにWindows/Vulkan実行環境がない。非描画のWindows免除を描画へ拡張しない。
- stop-when: mock/source確認を実GPU合格と扱う、ユーザー指定を変えて別環境へ無断移動する。

## G2-GR32-PALETTE-SHARING: component単位のpalette共有を接続する
- status: done
- done-when: GBuffer/CSM/点光源影/viewport/submeshで同componentの準備を共有し、別component・世代・フレームや異なるposeを混同しない。frame leaseとsubmitted serialの寿命を保つ。
- verify: 1component・1frameのSkinnedPalette作成1回、SkinnedPreviousPaletteはGBuffer対象だけ最大1回。影はprevious非束縛を維持し、両パス順序・複数submesh/viewport・世代/別pose・abort/寿命を独立draw契約で検査する。
- stop-when: 他component/古いframeのpaletteを流用、prepared tokenを寿命検査なしに使う、参照中またはGPU使用中のbufferを解放する。

## G2-GR79-MATERIAL-WIRE: 共有128B材質レコードの純codecを固定する
- status: done
- done-when: 承認済み128B配置/4StringRef/係数/flagsをRendering非依存の共通型とlittle-endian codecへ固定し、予約/参照境界/数値を検査する。wireのDefaultLit=0とruntime enumを混同しない。ARM方針やnits換算既定は決めず、現reader/writerへまだ接続しない。
- verify: 独立byte golden、flags/alpha/負の未指定係数、非有限/不正reserved/参照overflow/失敗保持、通常/O2/sanitizer、MEMBER/CTest登録。
- stop-when: ARM既定やemissive nits既定を採用したと扱う、未知flag/予約を黙って無視、native/GPU未実行を受入れ済みとする。

## G2-GR32-POINT-SHADOW-BUDGET: 点光源影のUBO消費をcomponent単位に共有する
- status: done
- done-when: 各light/faceで同componentの全submeshが同じUBO/descriptorを共有し、16componentの容量で途中の部位だけを落とさない。影にpreviousを束縛しない。
- verify: 8submesh×16component、17番目の全体省略、face分離、旧匿名fallback、binding10無しをCPU契約へ登録する。
- stop-when: material/submesh数でUBO枠が増える、faceを跨いだdescriptor上書き、GPU未検証を合格扱いする。

## G2-GR32-POSE-HISTORY-GENERATION: 前姿勢を資産世代へ束縛する
- status: done
- done-when: GameThreadとRenderedObjectHistoryの前姿勢がcomponentIdに加えて資産handle/generation一致を要求し、同bone数の別資産を混同しない。component単位で履歴を記録する。
- verify: 同bone数のreload・別資産・frame gap・複数submesh/viewportで前姿勢とfallbackを検査する。
- stop-when: mutable Resource参照、別世代poseの流用、既存velocity基準の変更。

## G2-GR82-A1-MULTI-CLIP-DECODE: Stage Aの複数clip取り込みを接続する
- status: done
- done-when: 新DecodeRigGltfのStage A入口で1mesh/1skin/1mesh-node・primitive8/joint128を維持しanimations1以上を取り込み、cookへ接続する。旧DecodeSkeletalGltf/AnalyzerのTwoClips/IntermediateNode拒否は維持。NVSKEL0.2は既存の統一定義を使う。
- verify: 3clip glTF/GLB→cook→parseで名前/duration/channel一致、3本目失敗でData/source buffers非公開、Cubic全clip予算/global prefix、Morph root数量1回+全clip weight channels、scale1回を検証する。pure report試験とnative未実行を区別する。
- stop-when: 旧strict入口を緩める、budgetをclipごとリセット、root morphをclip数倍、Stage BやM9まで対応済み扱いする。

## G2-GR82-A2-CLIP-RESOURCES: 複数clipの保持と一意名前引きを接続する
- status: done
- done-when: SkeletalAssetResourceで複数clipを保持しGetClipCount/GetClip(name)を追加。単数SetResources互換とGetAnimationClip先頭を維持。空/重複名は名前引きで曖昧拒否し資産自体を一律拒否しない。
- verify: 単数互換/配列所有/子Load/不正子/Unload/名前欠落重複/強参照寿命/メモリ計上をnative登録。
- stop-when: M9の複数clip guardを無断解除、制作alias規約を勝手に採用、Resource寿命破壊。

## G2-GR79-IMPORT-POLICY-CORE: ARM百分位と発光の必須設定を固定する
- status: done
- done-when: AI生成profileのAO/metallic ignore・roughness auto、素材override、1〜99百分位幅での定数判定、emissiveFactor×strengthの非0判定とnits換算明示必須を純関数にする。asset-set単位のfallbackを受けるが高優先の不正値を無言fallbackしない。
- verify: TRELLIS型B192〜255/Pixal型G249〜255外れ値、texture override/因子/正しいAO式、百分位rank境界、textureのみ/strength0/未設定/非有限/上限、設定優先順位を通常/O2/sanitizerで反証する。
- stop-when: 発光textureだけで未設定拒否、AI犬の金属をautoへ戻す、max-minを定数判定に使う、asset/material名付き診断やasset-set実配線を未実装のままGR79全体完了扱いする。

## G2-GR79-IMPORT-POLICY-CONNECTION: 材質設定と出所付き診断を接続する
- status: todo
- done-when: material/asset/asset-setの設定を解決し、発光換算の未設定拒否に資産名・材質名・emissiveNitsPerUnitを表示。AI生成profileを明示的に適用し素材単位overrideを保持する。canonical/hash/cacheとJSON/CLIへ接続する。
- verify: asset-set単位指定/素材上書き/欠落・不正/発光textureのみ/診断名/設定差cache失効と旧非発光・v0互換を確認する。
- stop-when: provenanceを拡張子だけで決める、sidecarよりasset-set設定を無言優先、非発光を不必要に拒否、見た目未確認を受入れ済みとする。

## G2-GR79-MESH-V1-WIRE: NVMESH v1の外枠とLOD0レコードを固定する
- status: done
- done-when: v0定数を不変の純headerへ分離し、v1.0 Magic/Header256/Material128/Cluster128を定義。外枠のversion/サイズ/予約/整列/packed節/hashとLOD0 clusterを純検証する。旧parser/cooker受理は変えない。
- verify: 独立byte golden、v0定数無変更、v1サイズ/未知版/予約/overflow/整列/padding/hash/LOD予約・範囲、失敗保持/alias拒否を通常/O2/sanitizerとMEMBERで検証。
- stop-when: 未接続のv1 runtimeを受理済み扱い、material係数を無言で捨てる、GR80用の拡張48Bを別用途へ転用、旧v0受理契約を破壊する。

## G2-GR79-MESH-V1-READER: v1材質と複数submeshを所有読込する
- status: done
- done-when: v0既存読込を維持し、v1の材質128/4参照/cluster128を意味検証して保持する。submeshが全index/clusterを一意分割し材質対応・実index/絶対上限・unique128を検査。v0は従来値へ昇格。runtimeは係数adapter未接続のv1とN>1を明示拒否し黙示的な情報欠落を防ぐ。
- verify: v0/v1/複数材質golden、係数bit/4path/寿命/不正所有とreserved/数値、pure partition通常/O2/sanitizer、native登録。既存v0手組みstaging互換を保つ。
- stop-when: v0の受理や見えを変更、v1材質をpathだけへ落とす、full readerとGPU受入れを混同、unique数をVertexCount fieldと混同。

## G2-RESOURCE-IO-NAMESPACE: 読込名前空間とResource基底クラスの衝突を解消する
- status: done
- done-when: Core::Resourceの基底クラスを維持し、GLTFAnalyzer/ModelStaging/loader群の名前空間だけをResourceIOへ分ける。全参照を更新し、同TUで骨格Resourceと共存する。wire/アルゴリズム/既定描画は変更しない。
- verify: 宣言/参照の列挙、旧namespace残存ゼロ・class参照不変、置換を戻したsourceの同一性、両include順のnative compile契約登録、実Windows未実行を区別する。
- stop-when: Resource基底クラス/反射名を変更、loader参照の取り残し、文字列置換で本体の挙動を変える、独立言語例をnative成功扱いする。

## G2-S6-CLI-BYTE-SMOKE: 単体CLI分割前後のバイト比較手順を固定する
- status: done
- done-when: 既存raw/texture/audio/model/GLB/import/骨格smokeを変更せず実行し、成功した全caseのpackage/manifestを正規化せず保存・比較するツールと反証テストを追加する。Main分割は実smokeと前後byte一致確認まで保留。
- verify: 比較器の一致/追加/欠落/byte差/JSON空白/recipe差/改竄/空出力/pathをPythonで検証。実AssetCookとWindows smokeは別の未実行gateとして記録する。
- stop-when: 比較器の単体成功をCLIの実成功扱い、JSON正規化で差を消す、既存出力を上書き、Mainを先に分割する。

## G2-S6-ASSET-SET: C++一括cookと増分判定を接続する
- status: doing
- done-when: AssetCook --asset-setへ一括cookと増分判定を集約。origin/main CookTextureAssetSet.ps1 + Rendering3DTestSilverTextures/Rendering3DTestSilverGltfTexturesに対してcooked/manifestのbyte一致を確認。glTF外部ファイルとsidecarを印に含む。
- verify: 単体CLIの分割前後比較、旧texture spec v1の2spec同値、外部buffer/画像/sidecarの変更・不在・復帰・破損で正しい再cook/拒否、失敗時出力保持。
- stop-when: 手元確認用CookAssets.ps1/StartupMaterialsを対象に戻す、PS側へ増分判定を重複実装、Windows実byte比較を未実施で完了とする。

## G2-GR79-MATERIAL-SETTINGS-VALUE: 材質設定の解決と正規形を固定する
- status: done
- done-when: profileと素材/資産/asset-setの設定を失敗保持で解決し、解決済み材質の独立canonical/hashを用意。幾何52Bと旧v0経路を変更しない。
- verify: AI既定と素材上書き/明示source復帰/不正下位値拒否/換算不在許可、canonical独立golden/bit/未使用値/-0、設定差hashを通常/O2/sanitizerとMEMBERで検証。
- stop-when: 未接続JSON/CLI/asset-set/素材識別子を実装済みとする、発光の有無を見る前に換算不在だけで拒否、旧hashを変更する。

## G2-GR79-MATERIAL-SETTINGS-JSON: 素材選択と独立した設定値JSONを厳密解析する
- status: done
- done-when: ARM mode/constant、profile、両面、alpha、換算値のJSON blockを重複/未知/型/値域の拒否と失敗保持で解析。素材selectorは作者回答待ち、既存sidecar APIは未接続のmaterialを引き続き拒否する。
- verify: ARM token通常/O2/sanitizer/MEMBER、実JsonDocumentの省略/空/正常/未知/重複/null/非ASCII/NUL/後段失敗をnative登録。旧sidecar受理条件とMainのbyte不変を確認。
- stop-when: parserの用意だけでsidecar/CLIに接続済みと言う、素材識別方式を先取り、警告だけで発光換算未設定を通す。

## G2-GR79-GLTF-MATERIAL-SOURCE: glTF材質の係数と参照を読み取る
- status: done
- done-when: glTF標準PBR/normal/AO/emissive/alpha/両面とKHR emissiveStrengthをdouble精度のsource値へ読み、重複/型/値/texture範囲/UV・texture拡張を検査して失敗時出力保持。既存cook経路の受理はまだ変更しない。
- verify: 純source値域/default/texture/発光factor×strengthを通常/O2/sanitizerとMEMBERで検証。実JsonDocumentの読込/失敗保持をnative登録しWindows未実行を区別する。公式glTF schemaのnormalScale符号・cutoff>1・strength既定1/最小0と整合。
- stop-when: texture有無で発光判定、normalScale負やcutoff>1を誤拒否、source doubleを換算前にfloatへ縮める、optional拡張のfallbackと必須拡張の受理を混同、wire/cook/描画へ接続済みとする。

## G2-GR79-DOUBLE-SIDED-AUTO: 両面autoの意味を閉鎖性判定待ちとして保持する
- status: done
- done-when: GR79(6)のdoubleSided=autoは単なるsource復帰ではなく閉鎖性に応じた判断を後段で行うmodeとして保持する。private enum FromSourceをAutoへ訂正しJSON/既定/説明/テストを整合。geometry判定はこの値層で捏造せず未接続を明示する。
- verify: enum値/canonical67B/hashのbyte不変、親forceから素材autoへ戻ること、未判定のautoをsource値確定と扱わないこと。
- stop-when: autoを無条件にsourceと同義にする、境界辺しきい値を値parserに埋める、未実装topologyを実装済みとする。

## G2-GR79-ARM-PIXELS: RGBA8からARM判定と焼込を生成する
- status: done
- done-when: linear RGBA8のAO/MR画像から実histogramを作り、profile済みchannel policyとfactorで畳み込みを判定。全定数なら画像なし、textureが残る場合だけ同寸法を要求して係数焼込RGBAを生成。入力/出力保持とalias拒否を保証。
- verify: 合成AI犬の外れ値/metallic ignore・texture override、factor1同byte/factor焼込、source無し既定・実測区別、異寸法のactive/folded、overflow/不正/出力不足/aliasを通常/O2/sanitizerとMEMBERで反証。
- stop-when: absent textureを実測扱い、画像alphaの不必要な変更、異寸法を黙って誤結合、失敗時部分出力、画像IO/実物/GPU/cook接続まで済みとする。

## G2-MATERIAL-SELECTION-SHARED: 材質識別の共通関数を実装する
- status: done
- done-when: 作者09:00承認通り一意元名/番号+元名を共通resolverへ集約、未一致/同targetへの二重指定拒否、同名警告情報を返す。SourceMaterialとGeneratedSlotのcatalogを区別し、GR79/GR78/GR32へ接続可能な単一の照合規則を固定する。
- verify: 無名1材質/Material_0・同名・番号/名前両指定・逆primitive順・生成名衝突・期待名不一致・invalidUTF8/NUL・失敗保持。旧GR32番号はslot番号のまま。
- stop-when: 生成slot名を元名に復元扱い、別関数へ解決ルールを複製、同名swapを検出可能と主張、未一致を無視して成功。

## G2-JSON-UNICODE-INPUT: UTF8入口とnative文字幅を整合する
- status: done
- done-when: JSON bytes入口を厳密UTF8→nativeの共通APIへ接続し、lexerのchar縮約/数値tokenのwide型不整合を修正。ANSI/UTF16/UTF32のraw/escape/native同名性を契約化し、material resolverで推測修復しない。
- verify: 骨+狼emoji/Latin、構文文字と同じ下位byteのUnicode、invalidUTF8/surrogate/NUL、ASCII/数値互換を純helperと実native登録で検証。Corewideの実compile/実行未確認を明示。
- stop-when: byte拡幅だけや符号修正だけでdecode済みとする、path用ToCoreStringをJSON修正で一括変更、native実行未実施を合格扱いする。

## G2-S6-WINDOWS-CLI-CI: Windowsで実CLIの分割前基準を採取する
- status: done
- done-when: feature限定の標準Windows runnerで公式Vulkan SDKと実Core/AssetCook/CookedMeshTestをビルドし、固定7smokeの実exe出力・manifest・ログを保存。before baselineを実成功確認する。--asset-set比較とMain分割後比較は後続gateとして残す。
- verify: 最小権限workflow、固定SDK/debug component、実run結果と各caseログ・snapshotを開いて確認。GPU検証と混同しない。
- stop-when: mainへ変更、課金/権限を拡大、失敗を成功扱い、Mainを基準採取前に分割。
- notes: 2026-10-04 10:02 UTC 作者がWindows CI新設・公式Vulkan SDKライセンス同意/導入を承認。既存workflow/run/checkは確認できなかった。

## G2-MATERIAL-SELECTION-INTEGRATION: 共通照合を設定とslot名へ接続する
- status: todo
- done-when: GR79 ARM/発光、GR78 材質→SurfaceName、GR32 slot名が同じResolveMaterialSelectionを使う。元catalog/生成slotを明示し、全設定の未一致/二重指定をcookと増分preflight双方で拒否。同名GLBは元での改名推奨を資産名付きで警告する。
- verify: raw無名1/Blender Material_0、逆primitive順/同名/生成名衝突、name+index二重指定、不在、unicode名、incremental skipの検証迂回なし、設定値/SurfaceName/slotへの実到達。
- stop-when: 未実装SurfaceNameを受理して捨てる、共通核の存在だけで全接続完了とする、元indexとslotindexの混同、旧wire予約領域へ勝手に保存。

## G2-GR32-MATERIAL-SELECTION-ADAPTER: slot名検索を共通resolverへ接続する
- status: done
- done-when: SkinnedMeshComponentのslot名検索が厳密UTF8変換後ResolveMaterialSelectionを呼び、独立したcallback照合を廃止。番号API/世代fallback/Default/一意空slot名は保持し、重複/NUL/不正Unicodeを拒否する。
- verify: 純adapterの行順とslotindex/Default/重複/空名/NUL/不正UTF8を3mode、native componentの既存fallback+NUL入力拒否。実Core実行はWindowsCI結果と区別。
- stop-when: 元材質番号と生成slot番号を混同、NUL前方一致、GR79/GR78接続まで済みとする。

## G2-S6-NATIVE-TEST-COMPILE: 実Windowsで判明した2件のテスト不備を直す
- status: done
- done-when: CookedMeshV1TestのAnsiStringView/literal不正比較と、CookedSkeletalAssetTestのWindows nearマクロ衝突を修正し、テストの値域/厳しさを維持する。
- verify: 差分の意味不変・名前衝突回帰、Windows CIでCookedMeshTestビルドと既存CLI smokeを再検証。Core/AssetCookはrun37195379624でビルド成功、テスト/CLI実行は未到達。
- stop-when: assertionを削除/弱化して通す、エンジン実装の挙動を巻き込む、未再実行のnative成功を主張。

## G2-S6-SMOKE-ENVIRONMENT: Windows PowerShell検証の子環境を整える
- status: done
- done-when: PowerShell7→Python→CMake→Windows PowerShellでPSModulePathを継承して標準Utilityを見失う経路を、子process環境だけの除去で避ける。親環境・smoke command/fixture/byte条件は保持し、実Glb以降を再検証する。
- verify: 大小文字の異なる環境key/他変数/親保持/Skeletal指定の単体試験、実Windows CLI7case。基準未採取なのでdriver recipe修正は今回を含めて固定する。
- stop-when: OS/ユーザーの恒久環境を変更、manifest比較を弱化、実再試験前に環境原因の確定/全smoke合格を主張。

## G2-MATERIAL-IMPORT-PLAN: sidecarと元材質の解決結果を値所有する
- status: done
- done-when: 幾何設定52Bを維持した新document/loaderが資産layerと素材selector+layer+surfaceを厳密解析。glTF元index/name catalogを所有し、全selectorを共通関数1回で解決、ARM/発光設定とSurfaceNameを値所有planへ届ける。旧loader/v0受理は拡張せず、実cook接続は次の垂直slice。
- verify: raw無名/Material_0/入替/同名警告情報/未一致/二重target/Unicode/JSON寿命後所有/失敗保持、旧幾何APIの拒否互換、sidecarfile入口。native testをCIで実行する。
- stop-when: SurfaceNameをreportだけでruntime対応とする、未対応cookに設定を捨てて渡す、resolver照合を複製、元indexを生成slotへ置換。

## G2-S6-SKELETAL-FIXTURE-URI: 骨格GLB試験の置換対象URIを明示する
- status: done
- done-when: ChangeBufferUriが期待する外部URIを引数で受け、ExtraZeroInfluencesのextra_zero.binだけを正しく除去する。従来fixture.binの既定とちょうど1件の置換assertを維持する。
- verify: 既存JSON/binaryは無変更、抽出後JSONとbuffer長を確認。実Skeletal unit/CLIを再実行し、失敗時出力保持やInfluenceLimitExceededの期待を変更しない。
- stop-when: URIの不一致を黙殺、JSON全体を正規化、エンジン判定やassertを弱化、実全7smoke前に基準採取完了とする。

## G2-S6-MAIN-SPLIT: 単体cookを再利用できる境界へMainを分割する
- status: done
- done-when: d1307c32の実Windows基準（run37200047966）を固定し、CLI外殻と再利用可能な単体cookを分ける。新機能/型体系の全面変更を混ぜず、分割後の実7smoke package/manifest79件が全byte一致する。
- verify: 同じWindows checkout byte/driver/fixture、既存10CPUと単体cook境界1契約、7smoke、保存済みbeforeとafterのraw比較。recipe31項目のうち11はWindowsCRLFであることに注意する。
- stop-when: 条件を弱化/正規化して差を消す、比較未実施で分割完了、baseline期限切れを黙認、asset-set実装を同じ差分へ混ぜる。

## G2-S6-TEXTURE-BASELINE: origin/mainのtexture spec v1出力を固定する
- status: done
- done-when: 固定main b8c5df1のCookTextureAssetSet.ps1とSilverTextures/SilverGltfTexturesを、固定before AssetCook.exeと明示Windows PowerShell5.1で実行し8package+2manifestの生byteを保存する。
- verify: 11入力のGit/checkout hash、exe hash、PowerShell実version/hash、全10file一覧/size/hash、終了コードと各5/3asset。2回の独立出力が全byte一致。
- stop-when: PS serializerを推測/正規化、基準の実行失敗を成功扱い、withdrawn対象で代用、texture基準だけでmodel依存印まで完了とする。

## G2-S6-TEXTURE-SPEC: texture spec v1を所有するC++要求へ解析する
- status: done
- done-when: 固定2specとv1必須field/default variant/Assets prefix/順序を保持した値所有型へ解析する。整数version、重複/未一致型/unsafe出力pathを拒否し、失敗時の出力を保持する。未知fieldとusageは旧仕様どおり無視。
- verify: 実JsonDocumentで2spec相当・寿命・default/override・case-insensitive重複・traversal・version literal・NUL/type不正を検証。既存11CPU/7CLI/79byte一致を回帰。
- stop-when: runtime cook/増分/serializerを未接続なのに--asset-set完了とする、独自型でない公開所有containerを増やす。

## G2-S6-TEXTURE-SPEC-FIXTURE: JSON派生fixtureをbyte安全に構築する
- status: done
- done-when: TString::replaceの終端NULによるsuffix先頭破損を試験入力へ持ち込まないよう、prefix/replacement/suffixをappendする。失敗時は全fixture byteを記録する。parserの検査条件は維持。
- verify: 全Changed needleはBaseに1回だけ、置換後JSONを独立確認し、実Windows12CPU/7CLI/79byte一致を再実行。
- stop-when: malformed fixtureをparserが通すよう変更、試験をskip、汎用TString修正を同じ差分に混ぜる。

## CORE-STRING-REPLACE-TERMINATOR: 部分置換によるsuffixのNUL破損を修正する
- status: todo
- done-when: TString::replaceが同長/増加/縮小/末尾/自己参照の置換で意図したbyte列を保ち、終端は末尾だけに置く。
- verify: 実Coreのchar/wchar/member契約、部分置換直後のsuffix先頭と全size、関連文字列試験。
- stop-when: StringCopyの全呼出し規約を検証なしに変更、Windows CRTの動作を偽shimで合格扱い。

## G2-S6-TEXTURE-BATCH: native --asset-setでv1を新規rootへcookする
- status: done
- done-when: C++サービスとCLIがv1を全件cookし、PS5.1集約manifestをspec順に生成。存在しないRuntimeRootだけへ同volume no-replace renameで公開し、固定Silver2specの10fileと全byte一致する。
- verify: 既存12CPU/7CLI/79byte一致、2spec各2回10byte一致、PS5.1 ASCII escape実probe、late画像不正/既存root/出力prefix衝突/公開先競合で不変。
- stop-when: 既存root上書きやmanifest-lastを原子的と呼ぶ、genericrenameで競合先を置換、増分/モデルbatch/production caller移行を完了扱い。

## G2-S6-DEPENDENCY-SNAPSHOT: cook入力の依存fileと生byte印を共通化する
- status: done
- done-when: 単体要求からsource/glTF外部buffer・image/選択sidecarのlocator・presence・全raw byteを値所有metadataへ集め、正規化optionとcooker revisionを含む増分用fingerprintを生成する。sidecar/URIの既存解決を共有し、runtime SourceHashやcooked bytesを変えない。
- verify: raw/texture/modelGLTF/GLB、buffer余剰byte/image変更・欠落復帰、percentURI、sidecar空白変更・auto不在/required/disabled/override移動、revision/option変更、失敗時出力保持。既存13CPU/7CLI79byte/native2spec10byteを回帰。
- stop-when: これだけでSkip/既存root公開を完了扱い、hashだけで出力を信頼、disabled sidecarを読む、不在とpermission errorを混同、競合編集のatomic snapshotを主張。

## G2-S6-OUTPUT-PACKAGE: 増分照合の共通package検証を作る
- status: done
- done-when: callerが渡すpackage bytesと期待manifest参照を照合し、V1単一entry・payload hash・kind/formatの実parse・骨格metadataを検証する。成功時に全packageのsize/hashを返す。ファイルpath解決・所有権・Skip決定は含めない。
- verify: 実単体cookのraw/custom FourCC/texture4形式/audio/static+派生画像/skeletalを受理し、不正table/payload/format/metadata/複数entryを拒否する。失敗時出力保持、padding差の全体hash検出、既存16CPU/79+10byte gateを維持。
- stop-when: 型parseをpayload hashだけで代用、v1を現v0として受理、source freshness/安全path/ownershipを検証したと主張する。

## G2-S6-CACHE-DECISION: 保存出力recordと共通増分判定を作る
- status: done
- done-when: 正規化した現要求と依存snapshotから独立に出力一覧を導き、値所有record・現在manifestの自分の参照・全package印を照合してCook/Skip/Errorを返す。成功cook後のrecord採取は前後依存の一致と全出力検証を必須にする。
- verify: 全kindの実cook→record→Skip、派生画像一覧、入力/sidecar/出力の変更、無関係manifest変更、失敗時出力保持、Windows alias/reparse/危険path/旧record path非採用、既存79+10byte gate。
- stop-when: recordに書かれた任意pathを開く、別keyのfileやsourceを上書き可能な要求をCookにする、全manifest hashをentry cache identityにする、永続化/既存root公開を完了扱い。

## G2-S6-MODEL-NATIVE-PATH: modelのfile locatorをnative pathで共有する
- status: done
- done-when: model fingerprint/cookの外部URIとsidecar探索が同じnative filesystem pathを使い、narrow変換で失われるUnicode source/overrideを黙って別fileへ変えない。既存ASCII入出力のbyte互換を保つ。
- verify: Windowsの日本語・非BMP source/base/overrideと外部buffer・image、cacheと実cookの一致、失敗保持、既存79+10byte gate。
- stop-when: ANSI文字化けを許容、runtime asset logical pathの規約を同時に変更、実Windows未検証のままUTF対応と宣言する。

## G2-S6-COOK-STYLE: 新規cook検証コードの書式を揃える
- status: done
- done-when: 新しい出力検証・共通増分判断とその試験をAllman/制御文brace/bool命名へ揃え、headerのusing namespaceを明示修飾へ置換する。既存単体cooker本体は新設adapter範囲以外を整形しない。
- verify: 明示した識別子・名前修飾の変更後はbrace以外のC++ token一致、文字列literal不変、BOM/CRLF/numstat、既存19CPU/79+10byte gate。
- stop-when: cook/hash/path判断式やserialized文字列を変える、既存全fileの無関係な整形、実検証なしの互換宣言。

## G2-S6-STATE-CODEC: cookの所有recordを厳密な保存形式にする
- status: done
- done-when: owner/root/manifestの現在bindingに結び付いた値所有stateを無損失で解析/直列化し、未知/重複field・危険path・所有衝突・過大入力を拒否する。保存値からfileを開かず、増分判断は既存C++へ渡す。
- verify: 実cook recordの往復とSkip、uint64境界、寿命/失敗保持、全階層schema/所有alias拒否、Windows CPUと79+10byte gate。
- stop-when: stateを所有権の認証と扱う、record欠落を既存fileの上書き許可にする、runtime manifestを変更、file公開とtransactionを未検証で接続する。

## G2-S6-STATE-FILE: 所有stateを安全に新規保存して読み戻す
- status: done
- done-when: caller指定のRuntimeRoot外の同volume stateを排他新規保存し、Missing/Loaded/Errorを区別して読む。既存stateを置換せず、自分のtemp以外を採用/削除しない。
- verify: cook→capture→save→load→共通Skip、write/flush/verify/renameの失敗、既存file/directory/junction/競合、out保持、Windowsと既存byte gate。
- stop-when: locked journalなしでproduction rootとstateを別々に公開、unknown ownershipの自動採用、atomic reader/powerloss耐久性を保証する。

## G2-S6-STAGED-OUTPUT-PLAN: 出力一覧とstage捕捉の境界を共有する
- status: done
- done-when: 共通Prepare/BuildInventory由来の値所有planを公開し、final→stageの変更を出力rootだけへ限定する。始点snapshot・要求意味・fragment全件を照合して共通record採取へ渡す。
- verify: 全kind/派生画像一覧、実stage cook/捕捉、始点後のsource/外部file/sidecar変化とout保持、要求/fragmentの差し替え拒否、read-only計画、Windows CPUと79+10byte/16拒否 gate。
- stop-when: 別の命名/増分authorityを作る、新snapshotで途中変更を黙認する、stage locatorを保存stateへ入れる、lock/journalなしでproduction公開や既存root採用を始める。

## G2-S6-OUTPUT-SET-GUARD: asset集合を跨ぐ出力と入力の衝突を拒否する
- status: done
- done-when: 検証済みplan集合のprimary/派生key・物理path・prefixを、他assetの全依存/spec/control locatorと照合する。書込/所有取得を行わず、単体命名・増分判断を複製しない。
- verify: primary/派生/variantの衝突、case/short alias/hardlink/prefix罠、他asset入力/外部file/不在sidecarとの衝突、無関係file保持、規模上限/計算量、Windowsと既存byte gate。
- stop-when: unknown所有fileを採用する、出力命名を独自実装する、衝突確認なしでstage作成/公開をproductionへ接続する。

## G2-S6-OWNER-ID: 独立したtupleからowner識別子を導出する
- status: done
- done-when: callerが独立に確定したspec/final-root/manifest identityから、versioned length-delimited UTF8 tupleとSHA-256で安定した128bit識別子を導出する。SourceRootや保存stateからownerを採用しない。
- verify: 独立算出の固定vector、field境界/Unicode/上限/不正入力/失敗保持、state codecとの照合、Windows CPUと79+10byte gate。
- stop-when: filesystem alias解決やroot作成を暗黙に行う、識別子を認証/既存root上書き許可と扱う、未確定のresolverをproductionへ接続する。

## G2-S6-OWNER-RESOLVER: owner identityとfile locatorを独立に解決する
- status: done
- done-when: existing specとexisting root/既存直親下の不在rootから物理UTF8 owner identityを導出し、ASCII drive-form保存bindingとnative I/O locatorを分けて返す。read-onlyで失敗時outを保持する。
- verify: 既存/不在から作成後の再照合、Unicode・case・8.3・SUBST、同名spec置換、危険raw名/type/reparse拒否・shareなしspecの属性観測、独立state保存読戻し、Windowsと79+10byte gate。conditional試験の未実行を明記する。
- stop-when: 不在名の将来canonicalを無条件に保証する、保存ownerを採用する、lexical binding差を黙認する、lock/journalなしでroot/stateをproduction公開する。

## G2-S6-DESTINATION-LOCK: destination volume単位で協調writerを排他する
- status: done
- done-when: FINALのcanonical volume GUIDだけでGlobal mutexを共有し、同期callbackの取得/再観測/解除を同一threadで閉じる。Busyは即時、abandonedを明示し、全取得で後段journal検査が必要とする。
- verify: 実thread/process排他、同volume親子/sibling/case/作成前後、callback失敗/例外/再入、実process終了のabandonedとobject再作成、private故障注入、無書込、Windowsと79+10byte gate。cross-session実測は環境がある場合だけ。
- stop-when: owner/spec/stateごとにlockを分ける、aliasや親子rootで同volume排他を回避する、永続crash印/transaction/認証を保証する、ACL/privilege変更やlockfile採用を始める。

## G2-S6-MANAGED-UPDATE-INVENTORY: 旧stateと新planの更新対象対応を値所有する
- status: doing
- done-when: 独立ExpectedBinding・state scope・旧state・FINAL plan集合のkey/package対応を値として検査し、package/manifest/stateのbefore-image要件へ写す。初期profileは所有inventory固定、file I/O/Skip/publish許可は行わない。
- verify: 全kind実recordの対応、順序差・値所有、binding/追加除去rename/primary派生/上限/失敗保持、世代上限で変更不可の明示、Windowsと79+10byte gate。
- stop-when: 値の対応だけでfilesystem所有を証明する、未知fileを採用する、独自cook/cache判定を作る、journal discovery/locked再照合/実before-imageなしで公開へ使う。
