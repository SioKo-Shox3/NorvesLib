- notes: SS-RTGI-DEFAULT の完了時（2026-10-02）の測定: 判定の帯（0.80〜0.98）は既定1.27倍・近接0.86倍・低角度1.70倍で合格だが、年齢上限128の撮影（`startup-capture/SS-RTGI-DEFAULT-it1-age128-On` と `-it1-base-Off`）で既定の0.33〜0.42の帯は2.03倍（標準偏差1/255超の画素 26.6%、無効時1.6%）、低角度の0.65〜0.80の帯は2.29倍（15.3%、無効時0.5%）。前回の切り分け（`SS-RTGI-DEFAULT-exp-norej`、年齢上限64で法線・材質・深度の棄却を外した撮影）ではこの2つの帯が1.59倍・1.76倍だったので、履歴の棄却が主な原因と見られる。危険地帯（RTGI）。評価者を通す。

## SS-NIGHT-POLISH: 夜の点光源の色とレンズダートの映り方を整える
- status: done
- done-when: 起動画面の点光源の色を白熱電球の色温度（黒体 約2700〜3000 K をリニアのRGBへ直した値。根拠をコミット本文に書く）にし、夜の撮影の既定視点で光だまりの地面の平均色が橙〜黄（R > G > B、G/R が0.6〜0.85）で、黄緑（G/R > 0.9）に寄らない。夜の撮影でレンズダートの模様が光源の周りの淡い斑にとどまり、光源より大きな色付きの円（ゴースト）が目立たない（強さを下げる、または明るい光源の周りでの掛かり方を見直す。昼・夕のダートの見え方が消えない程度）。昼・夕の撮影の SS-LOOK-BALANCE の数値の範囲は保つ。変更前と変更後の夜の既定視点を並べた画像を残す。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-NIGHT-POLISH-night -Night`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-NIGHT-POLISH -SunElevations 45,3`
- stop-when: ダートを弱めると昼・夕のダートが見えなくなる場合は、夜の見え方を優先せず、両立する値と測定を記録して閉じる。
- paths: Game/GameModes/Rendering3DTest, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, TASKS.md, PROGRESS.md
- notes: 2026-10-02 の親の確認（`startup-capture/SS-ACCEPT-night/default-night.png`）で分かった。点光源の色は `Rendering3DTestRoutine.cpp` の `SetLightColor(1.0f, 0.9f, 0.3f)` で、石畳が黄緑がかる。レンズダートは `kStartupLensDirtIntensity = 2.0f`。

## FIX-RGCT-MEGA-VELOCITY: RenderGraphCompileTest の MegaGeometry の記録の検査の準備に GBuffer_Velocity を足す
- status: done
- done-when: `RenderGraphCompileTest.cpp` の MegaGeometryPass の記録の検査（2620行目の `BeginRenderPassCount == 1`）の準備で、`SharedResourceRegistry` に `GBuffer_Velocity`（128×64 の R16G16_FLOAT のレンダーターゲット）を登録し、検査を弱めずに RenderGraphCompileTest が通る。エンジン側（MegaGeometryPass）は変えない。
- verify: `cmake --build build --config Debug --target RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderGraphCompileTest$"`
- paths: Test/Core/Rendering/RenderGraphCompileTest.cpp, TASKS.md, PROGRESS.md
- notes: SS-GPU-PROFILE（2026-10-03）の検証で見つけた。MegaGeometry に velocity を足した変更（`7461c3e`・`f63b2fd`）から、MegaGeometryPass は `GBuffer_Velocity` が無いと GBuffer のフレームバッファを作らず記録の前に戻るが、検査の準備は5枚しか登録していない。2026-10-02 の SS-GTAO の実行（`20261002-084328/verify-SS-GTAO-8.txt`）でも同じ行で落ちていた。登録を一時的に足すと 1/1 で通ることを確かめた（`20261003-034747/verify-SS-GPU-PROFILE-7.txt`）。
- notes: 2026-10-03 親が直した。`.harness/runs/20261003-parent/build-RGCT.txt` で build EXIT_CODE=0、`ctest-RGCT.txt` で RenderGraphCompileTest 1/1 passed。

## SS-GPU-PROFILE: 計測のある構成で、加速構造の更新を含むGPUの時間とパスごとの内訳をトレースへ書けるようにする
- status: done
- done-when: 計測が有効な構成（Debug・RelWithDebInfo。`NORVES_ENABLE_STATS=1`）で、(a) フレームのGPUの区間（`FrameGPU`）が加速構造の更新（`RenderingCoordinator.cpp` の `BuildAccelerationStructures`）を含み、加速構造の更新も別の区間として取れる。(b) RenderGraph のパスごとのGPUの時間が `--trace-file` のトレースに行として出る。Release の構成には計測・トレース・ログの仕組みを足さない（Release の `NORVES_ENABLE_STATS=0` と出力は変えない。Release を有効にするCMakeの選択肢も作らない）。RelWithDebInfo の Game で起動画面を数百フレーム走らせ、フレームごとのGPUの時間・加速構造の更新・パスごとの内訳がトレースに出ることを、トレースを開いて確かめる（PROGRESS に数行の抜粋）。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config Release --target Game -- /m:1`
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderGraphCompileTest$"`
- stop-when: GPUのタイムスタンプを加速構造の記録の前へ動かすとコマンドの記録の順序（RenderThread の同期）を変える必要がある場合は、変えずに、加速構造の更新を別の区間として取るだけにして記録する。
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Library/Core/Private/Boot, Library/Core/Public/Boot, Library/Core/Private/Debug, Library/Core/Public/Debug, Library/Core/Private/RHI, Library/Core/Public/RHI, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: SS-ACCEPT の停止理由（`blocked/SS-ACCEPT.md`）から作った。加速構造の更新は `FrameGPU` の開始より前にあり、パスごとのタイムスタンプはトレースへ書く経路が無い。2026-10-02 ユーザーの指示: Release にGPUの計測やログなどのデバッグの機能を入れない。計測は最適化が有効で計測の残る RelWithDebInfo で行う。危険地帯（RenderThread・RHI）。評価者を通す。
- notes: 2026-10-03 実装は `053aaa3` で済み、検証の RenderGraphCompileTest が前から落ちていたため止めていた（`blocked/SS-GPU-PROFILE.md`）。FIX-RGCT-MEGA-VELOCITY で直したので、verify を回し直して done にする（実装の追加は不要）。

## SS-ACCEPT: 起動画面の改善を受け入れる
- status: done
- done-when: 朝・昼・夕 × 既定・近接・低角度の撮影一式と、変更前（`163ffe5`）の同じ視点の撮影を並べた記録（`Docs/RenderingValidation/StartupSceneAcceptance.md`、画像は `.harness/runs/` への参照）がある。最適化が有効で計測の残る RelWithDebInfo の構成で、起動画面の1フレームのGPUの時間（加速構造の更新を含む）を測り、1280×720で16.6 ms以下であることを記録する（超えるフレームがあれば、そのフレームのパスごとの内訳と、どれを軽くすれば収まるかを記録する）。Release には計測の仕組みを入れないので、Release では build が通り起動画面を撮影できることだけを確かめる。夜（`-Night`）の撮影も並べる。評価者が、各項目の完了条件と撮影を開いて反証を試みる。
- verify: `cmake --build build --config Release --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT -SunElevations 10,45,3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT-night -Night`
- stop-when: フレーム時間の予算を超える場合は、内訳と軽くする案を既知の限界として記録して完了にする（ユーザーへの報告に含める）。
- notes: 受け入れの撮影は、SS-LOOK-BALANCE の数値の範囲（画面の平均・白飛び・黒つぶれ・影の比・空の青・夕の色・夜の背景）も満たしているかを並べて記録する。
- paths: Docs/RenderingValidation, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 区切り。評価者を通す。
- notes: 2026-10-02 再開。評価1周目の指摘3（既定視点の変更前後のカメラ）は `51a22e7` で対応済み。指摘1・2（GPU時間・加速構造の更新を含む区間・予算を超えたフレームのパスごとの内訳と軽くする案）は SS-GPU-PROFILE の後に RelWithDebInfo で測り直して記録する。完了条件の「Releaseの構成で測る」は、2026-10-02 のユーザーの指示（Release にGPUの計測やログなどのデバッグの機能を入れない）により RelWithDebInfo へ改めた。RelWithDebInfo の既存のトレースでは夜の既定視点で540フレーム中6が16.6 msを超え、最大18.16 ms。

## FIX-ASYNC-TEXTURE-MIPS: 非同期で読み込むテクスチャにもミップマップを全段作る
- status: done
- done-when: 非同期の読み込みの経路（`TextureAssetLoader.cpp` の `DecodeLooseBlobWithStbiForWorker` など、ばらのPNGを stb で読む経路で `bFullMipChain=false` を渡している所）でも、同期の経路と同じくミップマップを全段（4096²なら13段）作る。ミップの生成はGPUで行い（既存の `GpuResourceStore` のミップ生成）、RenderThread の同期を変えない。法線マップ・ラフネス・高さなど色でないテクスチャもリニアのまま縮小される（sRGBの扱いが同期の経路と同じ）。起動画面の Game.log で石畳の5枚のテクスチャが `mip_levels=13` とミップ生成の成功を出し、起動から撮影までの時間の増分が2秒以内（ミップ生成の時間をログから合計して PROGRESS に書く）。昼の既定視点で遠くの地面のざらつき（ミップが無いことによるエイリアシング）が消えたことを、変更前後の遠景の拡大画像と、遠景の領域の隣り合う画素の差の平均（高周波の量）で示す。検証シーンの golden が変わる場合は、差がミップの有無だけによることを確かめて再承認する（任されている）。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/FIX-ASYNC-TEXTURE-MIPS -SunElevations 45`
- stop-when: ミップの生成を RenderThread の外の同期なしに行えない理由が見つかった場合は、同期を変えずに済む形（読み込みのワーカーでCPUで縮小して全段を渡す等）へ切り替え、選んだ理由を記録する。
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Library/Core/Private/Asset, Library/Core/Public/Asset, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-03 ユーザーの指摘（石のタイルが4Kのはずなのに近づくと粗い）から調べて分かった。`.harness/runs/startup-capture/SS-ACCEPT/near-sun45.Game.log` で `cobblestone_floor_09_diff_4k.png data_size=67108864 mip_levels=1 mipgen_ms=0.000`。同期の経路（`LoadStbiBlobForCaller`）は `true` を渡すが、非同期・メインの描画の経路の3か所は `false`。2026-06-07 の分離（`dee9822`）からこの形で、意図した記録は無い。異方性フィルタ（x4）もミップが無いと効かない。危険地帯（アセットの読み込み）。評価者を通す。

## SS-CSM-MEGA-CASTERS: 太陽の影（CSM）にMegaGeometryを描く
- status: done
- done-when: CSM の4枚のカスケードへ MegaGeometry（岩・小屋・今後の高ポリの球）を影のキャスターとして描く。カスケードごとにテクセルの大きさに見合う細かさのLOD（クラスタのLODの切り出しか、LOD0 をそのまま）を選び、選び方と三角形数を記録する。CSM の深度範囲に含める境界球が、実際に描くキャスターと一致する（backlog の FIX-CSM-MEGA-CASTER-BOUNDS を閉じる）。撮影の昼（45°）と夕（3°）で、小屋と岩の太陽の影が地面に落ち、影の縁にアクネ（縞）とピーターパン（接地部の浮き）が見えない（変更前後の拡大画像）。MegaGeometry の自己影（岩の窪み・小屋の軒）に縞が出ない。RelWithDebInfo の起動画面の1フレームのGPUの時間が16.6 ms以下のまま（`-GpuTimingFrames 600` の計測。ShadowMapPass の増分を記録）。検証シーンの golden が変わる場合は差の原因と妥当性を確かめて再承認する（任されている）。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest CascadedShadowLightMatricesTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CascadedShadowLightMatricesTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-CSM-MEGA-CASTERS -SunElevations 45,3`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- stop-when: CSM へ MegaGeometry を描くのに RenderThread の同期やパスの順序を変える必要がある場合は、変えずに済む形（ShadowMapPass の中で LOD0 の頂点・インデックスを直接描く等、点光源の影と同じ経路）を採り、理由を記録する。
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 2026-10-03 親が調べて分かった。`ShadowMapPass.cpp` は点光源のキューブへは MegaGeometry の LOD0 を描く（`PointShadowMegaCaster`）が、CSM へは描かない。そのため起動画面の岩と小屋には太陽の影が無く、見えている影はコンタクトシャドウだけ。SS-MEGA-SPHERE で大きな球を MegaGeometry にする前に要る。危険地帯（影・RenderThread）。評価者を通す。

## SS-MEGA-SPHERE: 大きな球を高ポリのMegaGeometryにし、テクスチャの密度を地面に揃える
- status: done
- done-when: 起動画面の大きな石畳の球（半径1 m、今は通常のメッシュの 32×16 の UV 球）を、手続きで作る高ポリの球（三角形 約100万〜200万。クラスタとLODの階層を持つ）の MegaGeometry に置き換える。UV は横3回・縦1.5回の繰り返しにし、テクスチャの密度（約2 mで1枚）と縦横比を地面の石畳に揃える（UVの継ぎ目で模様が切れない）。今の球が持つ機能をすべて保つ: 太陽の影（SS-CSM-MEGA-CASTERS）・点光源の影（数百万三角形を6面×灯数で描く費用が大きければ、影には粗いLODを使う）・コンタクトシャドウ・自転とTAAのvelocity・レイトレのシーン（RTGI）・材質（石畳、POM）。撮影の近接視点で球の輪郭に角（ポリゴンの折れ）が見えず、石の大きさが地面の石と同じくらいに見える（変更前後の拡大画像）。起動から撮影までの時間の増分が10秒以内（クラスタとLODの構築の時間をログに出す。超えるなら三角形数を下げる）。RelWithDebInfo の起動画面の1フレームのGPUの時間が16.6 ms以下のまま（パスごとの増分を記録）。MegaGeometry のクラスタの大きさ（1クラスタあたりの三角形数の平均）を記録し、極端に小さい（例: 平均16未満）なら原因を調べて記録する。
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-MEGA-SPHERE -SunElevations 45,3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-MEGA-SPHERE-night -Night`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- stop-when: 三角形数を下げても起動時間・GPUの時間の条件を満たせない場合は、満たす最大の三角形数で閉じ、測定値を既知の限界として記録する。
- paths: Game/GameModes/Rendering3DTest, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 2026-10-03 ユーザーの要望（球のポリゴンをスムースに。MegaGeometryの経路でポリ数を大きく上げてよい）。今の球は UV が1周（約6.3 m）で4Kを1回だけ貼るため、地面（2 mで1枚）の約1/3の密度で、石が横に引き伸ばされている。既存の MegaGeometry の岩は66122三角形が25562クラスタ（平均約2.6三角形）で、クラスタが小さすぎる疑いがある（`MAX_TRIANGLES_PER_CLUSTER = 128`）。危険地帯（MegaGeometry・RenderThread）。評価者を通す。

## SS-MEGA-SPHERE-DISPLACE: 高ポリの球を石畳の高さマップで実際に凹凸させる
- status: done
- done-when: SS-MEGA-SPHERE の高ポリの球の頂点を、石畳の高さマップ（`cobblestone_floor_09_disp_4k.png`、起動時にCPUで読む）で法線の向きへ動かし、石の盛り上がりと目地の窪みを実際の形にする（高さの尺度は今の POM の見た目に合わせ、値と根拠を記録する）。動かした形から法線を作り直し、法線マップと向きが合う。この球の材質では POM を切る（形で凹凸を出すので二重にしない）。メッシュに穴・割れ（UVの継ぎ目・極で頂点がずれて開く所）が無く、極の付近で棘が出ない（極へ向けて変位を弱める等。方法を記録する）。撮影の近接視点と低角度で、球の輪郭が石の凹凸でガタガタして見え、影（太陽・点光源・コンタクトシャドウ）とレイトレのシーンも同じ形を使う（変更前後の拡大画像）。起動時間とGPUの時間の条件は SS-MEGA-SPHERE と同じ。
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-MEGA-SPHERE-DISPLACE -SunElevations 45,3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-MEGA-SPHERE-DISPLACE-night -Night`
- stop-when: 極・継ぎ目の割れや棘を UV 球のままで消せない場合は、別の球の作り方（立方体を球へ写した形など）へ切り替え、模様の継ぎ目の見え方を変更前後の画像で比べて選んだ理由を記録する。
- paths: Game/GameModes/Rendering3DTest, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, TASKS.md, PROGRESS.md
- notes: 2026-10-03 ユーザーの「MegaGeometryでめちゃめちゃポリ数上げてもいい」を受け、高ポリを生かして輪郭にも凹凸を出す。高さマップは16ビットのグレーのPNG。評価者を通す。

## SS-ACCEPT-DETAIL: テクスチャのミップ・MegaGeometryの影・高ポリの球を受入れ記録へ足す
- status: done
- done-when: `Docs/RenderingValidation/StartupSceneAcceptance.md` に、FIX-ASYNC-TEXTURE-MIPS・SS-CSM-MEGA-CASTERS・SS-MEGA-SPHERE・SS-MEGA-SPHERE-DISPLACE の後の撮影（朝・昼・夕・夜 × 既定・近接・低角度）と、変更前（`b347eb7`）の同じ視点を並べた比較、SS-LOOK-BALANCE の数値の範囲の判定、RelWithDebInfo の1フレームのGPUの時間（加速構造の更新を含む。全フレームが16.6 ms以下か、超えたフレームのパスごとの内訳）、起動から撮影までの時間を足す。Release は build が通り撮影できることだけを確かめる。評価者が撮影を開いて反証を試みる。
- verify: `cmake --build build --config Release --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT-DETAIL -SunElevations 10,45,3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT-DETAIL-night -Night`
- stop-when: GPUの時間の予算を超える場合は、内訳と軽くする案を既知の限界として記録して完了にする。
- paths: Docs/RenderingValidation, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 区切り。評価者を通す。

## FIX-MEGA-CLUSTER-ADJACENCY: 読み込むモデルのMegaGeometryのクラスタを位置でつないで大きくする
- status: done
- done-when: クックの `MeshClusterizer` の三角形の隣接を、頂点の番号ではなく位置（UVの継ぎ目・法線の分かれ目で複製された頂点を同じ位置として扱う）でつなぎ、起動画面の岩（今は66122三角形が25562クラスタ、平均2.59、16未満が25001）と小屋（4281三角形が1323クラスタ、平均3.24）のLOD0の1クラスタあたりの平均三角形数を64以上にする（`stage=megamesh_cluster_stats` の行で確かめる）。描画の見た目（撮影）と影・レイトレの範囲は変わらない。
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/FIX-MEGA-CLUSTER-ADJACENCY -SunElevations 45`
- stop-when: クック済みのアセットの作り直しが要り、その手順が決まっていない場合は、作り直しの手順と影響を blocked に書いて止める。
- paths: Tools/AssetCook, Library/Core/Private/Rendering/MegaGeometry, Library/Core/Public/Rendering/MegaGeometry, Assets, TASKS.md, PROGRESS.md
- notes: 2026-10-03 SS-MEGA-SPHERE で見つけた。岩は頂点67042・三角形66122、小屋は頂点6599・三角形4281で、閉じたメッシュの目安（頂点≒三角形の半分）より頂点が多く、UVの島ごとに頂点が複製されている。`MeshClusterizer::BuildAdjacencyGraph` は頂点の番号の辺だけで隣接をつなぐため、クラスタの成長が島の境で止まる（手続きの球は頂点を共有するので平均127.8）。

## SS-MEGA-LOD-PERF: 大きな球のLODを画面上の誤差で選び、CSMのMegaGeometryをカスケードに見合う段で描く
- status: done
- done-when: (1) 大きな球（MegaGeometry、変位あり）のクラスタのLODを、その段で失われる形の誤差（球面からのずれと変位の差の大きい方）を実際の透視投影で画素へ直した大きさ（変位の前後のクリップ座標を画素へ直した差の上限。角度の近似で過小に見積もらない。この上限を、変位の前後のクリップ座標を画素へ直して比べる契約テストで確かめる）で選び、誤差が閾値以下になる最も粗い段を使う。閾値は撮り比べで見た目の差が出ない値（1画素以下）に決め、選んだ値と各視点の段を記録する。近接視点で LOD0 が選ばれるのは、それより粗い段の誤差が閾値を超えるときで、それは受け入れる（LOD0 は近接で目に見える変位の細部を運ぶ）。段の境目に割れ目が出ない（SS-MEGA-SPHERE の LOD 球の規則を保つ）。(2) CSM の各カスケードへ描く大きな球の段を、カスケードの1テクセルの大きさに見合う誤差で選ぶ（遠いカスケードほど粗い）。点光源のキューブも同じ考えで、遠い面・小さいキューブには粗い段を使う。岩・小屋は LOD0 のまま描き（影だけの粗い段は backlog の FIX-MEGA-SHADOW-LOD-LOADED で扱う）、その三角形数と ShadowMapPass の時間を記録する。どの段を何三角形描いたかを視点・カスケードごとにログへ出す。(3) RelWithDebInfo の `-GpuTimingFrames 600` で、近接視点（昼45°）の1フレームのGPUの中央値が5 ms以下、昼の3視点の全540フレームが16.6 ms以下（MegaGeometryPass・ShadowMapPass の中央値を変更前の SS-ACCEPT-DETAIL の値と並べて記録する）。(4) 見た目が変わらない: 近接・低角度の球の輪郭と目地、地面の影の、変更前後の拡大画像で差が目に見えない（差の画像と、球の領域の平均の差を示す）。
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-MEGA-LOD-PERF -SunElevations 45,3`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-MEGA-LOD-PERF-gpu -Configuration RelWithDebInfo -GpuTimingFrames 600 -SunElevations 45`
- stop-when: 誤差1画素で見た目に差が出る場合は、差が見えない最も粗い閾値を撮り比べで選び、選んだ値と測定を記録する。中央値5 msに届かない場合は、届いた値とパスごとの内訳を既知の限界として記録して完了にする。
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Game/GameModes/Rendering3DTest, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-03 SS-ACCEPT-DETAIL の計測で分かった。近接視点のGPUの中央値が 約3.1 → 8.2〜9.3 ms に増え、増分のほぼすべてが MegaGeometryPass（0.34 → 5.4〜6.2 ms）。変位を LOD の誤差に含めた結果、近接で大きな球の LOD0（1,046,528三角形）が選ばれ、その多くが1画素より小さい。12960フレーム中2フレームが16.6 msを超えた（MegaGeometryPass と ShadowMapPass が同じフレームで跳ねた）。CSM は岩・小屋を LOD0 のまま4カスケードすべてへ描く（`csm_mega_lod=0`）。この計測は FIX-MEGA-CLUSTER-ADJACENCY（法線コーンのカリングの変更を含む）の前。2026-10-03 ユーザーの指示: 最適化を先にやってからマージ・プッシュする。危険地帯（MegaGeometry・影・RenderThread）。評価者を通す。
- result: 2026-10-03 完了（反復2）。投影の式を、角度の変化の最大 projectionFactor·D/(D²−R²) に視線から外れた点での透視の伸び 1/cos²α の上限（視錐台の角と、中心の方向の角＋見かけの半径の小さい方）を掛けた上限に直した（`MegaGeometryLODSelection.h`・`cluster_cull.comp`）。契約テストはエンジンの view・projection（Vulkan の Z 反転と Y 反転の有無）で球の表面の点を法線方向に変位させ、前後のクリップ座標を画素へ直した差が上限以下で、上限の6割以上が実際に出ることを確かめる（角度だけの式では落ちる）。閾値は1画素のまま（撮り比べ: 1画素は近接 LOD0・低角度 LOD1 で変更前と同じ段になり、変更前との差は16超の画素0。0.5画素は低角度も LOD0 で LOD0 の参照と見分けがつかないが、変更前より細かくなる）。既定（10 m）は LOD4（0.866画素）。CSM は球 c0 LOD3・c1〜c3 LOD4、岩 LOD0 66,122・小屋 LOD0 4,281三角形をカスケードごと、ShadowMapPass 中央値 0.74〜0.80 ms。近接（昼45°）のGPUの中央値は 8.42 ms で5 msに届かず、既知の限界（MegaGeometryPass 4.62・LightingPass 1.34・ShadowMapPass 0.80 ms。LOD0 の球の1,046,528三角形が主）。3視点1620フレームの最大 14.41 ms で予算超え0。同じ版の再計測は環境の負荷で ShadowMapPass が 2.1 ms へ増えて跳ね、近接で57フレームが予算を超えた（既知の限界）。
- notes: 2026-10-03 評価1周目（`cb84375`）は NEEDS_WORK（`NEXT_FINDINGS.md`）。反復2が `blocked/SS-MEGA-LOD-PERF.md` に書いた推奨（A: 近接の LOD0 を受け入れ、誤差の閾値だけを条件にする／D: 岩・小屋の影の粗い段は別の項目にする）を親が採り、done-when を改めた。正しい透視投影の式では近接の LOD1 は約1.8画素、中間の段を足しても約1.2画素の見込みで、1画素以下の閾値では LOD0 が残るため、中間の段は求めない。直すのは指摘3（投影の式とその契約テスト）と指摘4（閾値の撮り比べ）。上の result 行は反復1の記録で、2周目の結果で書き直す。

## FIX-STRING-EMPTY-CSTR: 空の Container::String の c_str() が終端の無い値を返すのを直す
- status: done
- done-when: 空の `TString` の `c_str()`・`data()` が空の文字列（終端の0）を指し、`%s` で書いても余計な文字が出ない。既存の呼び出しの挙動を変えない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- paths: Library/Core/Public/Container, Test/Core, TASKS.md, PROGRESS.md
- notes: 2026-10-03 SS-MEGA-LOD-PERF で見つけた。`String.h` の `c_str()` は空のとき `&npos`（size_t の最大値のバイト列）を返し、ログに化けた文字が出た。SS-MEGA-LOD-PERF では呼ぶ側で空なら "" を渡して避けた。

## SS-ACCEPT-PERF: 最適化の後のGPUの時間と撮影を受入れ記録へ反映する
- status: done
- done-when: `Docs/RenderingValidation/StartupSceneAcceptance.md` の「テクスチャのミップ・MegaGeometry の影・高ポリの球」の節の GPU のフレーム時間を、FIX-MEGA-CLUSTER-ADJACENCY と SS-MEGA-LOD-PERF の後の値（RelWithDebInfo、12視点 × 2回、540フレームずつ）で更新し、予算超えのフレームの有無（あればパスごとの内訳）と、SS-ACCEPT-DETAIL の値からの変化を並べる。撮影（朝・昼・夕・夜 × 3視点）を撮り直し、SS-LOOK-BALANCE の数値の範囲の判定と、変更前（`b347eb7`）との比較画像を差し替える。既知の限界を今の状態に合わせて書き直す。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT-PERF -SunElevations 10,45,3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT-PERF-night -Night`
- stop-when: 予算を超えるフレームが残る場合は、内訳と軽くする案を既知の限界として記録して完了にする。
- paths: Docs/RenderingValidation, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 区切り。評価者を通す。
- result: 2026-10-03 完了。撮影12枚 result=pass、SS-LOOK-BALANCE 全項目 PASS、変更前との比較画像は `startup-capture/SS-ACCEPT-PERF-compare/`。GPU の12視点 × 2回は別のアプリが開いた状態で測ることになり（計測の直後の5秒で GPU の使用率36〜40%。計測の最中は未記録）、12960フレーム中468フレームが16.6 msを超えた（最大 37.261 ms、ShadowMapPass・MegaGeometryPass・LightingPass が同じフレームで跳ね、変更していない軽いパスと区間に入らない残り（0.021〜3.115 ms）も伸びる。原因を GPU の共有とするのは推定）。同じコードを約25分前に測った昼の3視点（SS-MEGA-LOD-PERF、占有未記録の参考値）は予算超え 0 / 1620（変更前 1 / 3240）。内訳・軽くする案・再計測待ちを既知の限界に記録した。

## FIX-GTAO-STATIC-NOISE: GTAOの雑音をフレームごとにずらさず、金属の球の鏡面の遮蔽のちらつきを止める
- status: done
- done-when: GTAOの雑音（スライスの向き・段の位置の4×4の並び）をフレームごとにずらさず、雑音除去の4×4の平均だけで均す。大きな球の自転を止めた（`NORVES_STARTUP_SPHERE_SPIN=0`）静止の視点 `-112,4,8.5` を4フレームおきに12枚撮り、各フレームの平均で正規化したフレーム間の平均絶対差（8bitの表示値）が、金属の見本の球（粗さ0.3・0.5・0.7）で変更前より25%以上小さく、地面で±10%に収まる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/FIX-GTAO-STATIC-NOISE -Configuration RelWithDebInfo -ViewNames default -DefaultCamera -112,4,8.5 -StillRenderedFrames 90,94,98,102,106,110,114,118,122,126,130,134,150`（`NORVES_STARTUP_SPHERE_SPIN=0`）
- paths: Library/Core/Private/Rendering/SSAOPass.cpp, Assets/Shaders/gtao.frag, TASKS.md, PROGRESS.md
- notes: 金属の面の見た目はほぼ環境光の鏡面反射×鏡面の遮蔽（GTAOの可視率から求める）だけで、模様が無いのでTAAの近傍のクランプがGTAOのフレームごとの揺れを通す。鏡面の遮蔽からGTAOを外した撮影でもちらつきは同じ水準まで下がった（粗さ0.3で0.439→0.238）。接触影を切っても、RTGIを切っても球のちらつきは変わらなかった。
- result: 2026-10-03 完了。フレーム間の平均絶対差は金属の粗さ0.3で0.439→0.239、0.5で0.361→0.246、0.7で0.349→0.259、粗さ0.1で0.233→0.224、地面0.329→0.329。最終画像の静止の差は平均0.19（8bit）で、AOの模様は見えない。検証シーンのgoldenはジッタを掛けないので、もともと雑音をずらしていない。

## FIX-SSR-SPECULAR-COMPOSITE: SSRを環境光の鏡面反射の置き換えにし、金属の色と反射率を掛ける
- status: done
- done-when: LightingPass が、環境光の鏡面反射として SceneColor へ足した値（露出後）と、その反射率（鏡面の遮蔽込み、無次元、RGB）を別の出力に書き、SSRPass は当たった画素で `scene + a·(反射率·L_hit − 足した値)` にする（金属は反射率にalbedoの色が入る）。a は粗さ・距離・画面の端・厚さの余裕のなめらかな重みと強度の積。デバッグ表示・検証の表示（反射率0）ではSSRを足さない。視点 `-112,4,8.5` で、SSRが当たった金属の球の画素が金色を保ち（SSRの有無で差が出る画素の (R−B)/(R+G+B) がSSRなしの同じ画素の0.8倍以上）、輪郭のくっきりした地面の切り抜きが無いことを拡大画像で確かめる。Indoor/Outdoor のgoldenを回し、差がこの変更だけによるなら再承認する。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: 照明パスの出力を増やすとRenderGraphの資源の寿命やレガシー経路の契約を崩す場合は、理由を記録して止める。
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders/lighting.frag, Assets/Shaders/ssr.frag, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 原因は `ssr.frag` が当たった画素で照明済みの色を `mix(scene, L_hit, alpha)` で置き換えていたこと。金属は `reflectStrength` が1になり alpha が約0.8（強度0.8）に達し、写った地面の色を金色を掛けずに入れるので、球の縁に地面の切り抜きが透けたように見える。外れた画素は空から作る環境マップ（金色が掛かる）のままで、当たり外れの境目がカメラの角度で動く。危険地帯（描画パスの構造）なので評価者を通す。
- result: 2026-10-03 完了（`1129bfd`・`06fb327`・`7f3f34f`）。視点 `-112,4,8.5` でSSRの有無で差が出る金属の球の画素の (R−B)/(R+G+B) のSSRなしに対する比は、粗さ0.1・0.3・0.5で0.71→1.09、0.68→1.13、0.80→1.04。拡大画像で地面の切り抜きは消え、縁には金色の細い映り込みが残る。フレーム間の平均絶対差は粗さ0.3で0.239→0.213。評価者（Astra）の1周目の指摘（フォグ・半透明を重ねた後の色から減衰前の鏡面反射を引いていた）を受け、SSRを照明の直後（フォグ・半透明の前）へ移し、フォグ・半透明・被写界深度・動きぼけはSSRの出力へ重ねるようにした。RenderGraphCompileTest・Indoor/Outdoor golden pass（Outdoorは球の輪郭と接地部の陰の266画素の差を`Docs/RenderingValidation/R1Acceptance.md` に記録して再承認、Indoorは一致）。起動画面の朝・昼・夕の撮影（`startup-capture/FIX-SSR-SPECULAR-COMPOSITE/`）は平均輝度が前回の受入れより0.3〜1.8下がった（SSRの映り込みが反射率どおりの強さになった分）。反射に写る色は照明の色（フォグ・半透明を含まない）。評価者の2周目の指摘（フォグ・半透明がSSRの出力へ重なるのに、ViewはHDRのシーンの色として照明の直後の "Scene.Color" を取り、SceneColorのキャプチャからそれらが抜ける）は、SSRの出力を書き出してViewが優先して取るように直し（`7f3f34f`）、RenderingHdrIndoor/OutdoorScene・RenderingHdrEmissive2本・FrameCaptureFloatReadback・golden 2本・RenderGraphCompileTestの8本 pass で確かめた。評価は2周までなので3周目は回していない。

## FIX-MESH-BOUNDS-CULLING: 手続きメッシュの境界球を登録した頂点から求め、大きなメッシュが見えたままカリングされないようにする
- status: done
- done-when: `MeshComponent::BuildMeshProxy` が、MeshResources に Mesh3DVertex の並びで登録したメッシュの頂点の位置から求めたローカルのAABBを包む球を `MeshProxy::WorldBounds` にする。回転と非一様スケールが重なっても変換後の箱の角を包む。起動画面の既定視点で、地面の外側の区画（中心が視錐台の外）が描かれる。
- verify: `cmake --build build --config Debug --target RenderResourcesDomainContractTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(MeshResourcesProceduralGpuTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest|SceneViewProxyReconcileTest|SceneViewProxyLookupTest|ComponentDirtyTrackingTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/FIX-MESH-BOUNDS-CULLING -Configuration RelWithDebInfo`
- stop-when: Mesh3DVertex 以外の並びで MeshResources に登録する経路があり、頂点の大きさだけでは並びを判別できない場合は、理由を記録して止める。
- paths: Library/Core/Private/Component/MeshComponent.cpp, Library/Core/Private/Rendering/ProceduralMeshGpuStore.cpp, Library/Core/Private/Rendering/ProceduralMeshGpuStore.h, Library/Core/Public/Rendering/ProceduralMeshGPUData.h, Library/Core/Public/Rendering/RenderResources.h, Library/Core/Private/Rendering/RenderResources.cpp, Test/Core/Rendering/MeshResourcesProceduralGpuTest.cpp, TASKS.md, PROGRESS.md
- notes: 原因は `BuildMeshProxy` が `GetLocalBounds()` の既定の単位の箱から求めた半径約0.87 m の球を入れていたこと。`SceneView::FrustumCull` はこの球で判定するので、16 m × 60 m の地面は中心が視錐台の外に出ると見えている部分ごと描かれなかった。GBufferPass はこの置き場のメッシュを `sizeof(Mesh3DVertex)` のストライドで描き、製品コードの登録元はすべて Mesh3DVertex。危険地帯（公開 API・カリング・影のキャスターの範囲）なので評価者を通す。
- result: 2026-10-04 完了（`47382182`・`28755270`）。起動画面の既定視点で外側の石畳が両側に描かれることを撮影で確かめた（修正前は背景が見えていた）。評価者（Astra）の1周目の指摘（行の長さの最大値を半径に掛ける方法は回転と非一様スケールの組み合わせで箱の角が球の外へ出る）を受け、半径を上3x3の絶対値で移した半分の大きさの長さにし、その物体を World から同期した MeshProxy の境界球が8つの角を包む回帰を足した（修正前の式では落ちる）。2周目は PASS。MeshResourcesProceduralGpuTest・Indoor/Outdoor golden・SceneViewProxyReconcile/Lookup・ComponentDirtyTracking pass。ComponentDataRegistryTest・WorldSyncDifferentialTest は TEST-FULL-CTEST-BASELINE に記録済みの既存の失敗（WorldTransform の777）で落ちる。日本語のコメントを足したファイルに BOM を付けた（BOM が無いと MSVC が CP932 として読み、「。」で終わるコメントが次の行を飲み込んで宣言が消え、ビルドが失敗した）。選択の境界（`Entity::GetLocalBounds` → SceneQuery）は既定の単位の箱のままで、大きな地面の端は選択できない（既存）。

## SS-GROUND-SWATCHES: 起動画面の地面に Poly Haven の地面の材質を帯に並べ、質感を見比べられるようにする
- status: done
- done-when: 起動画面の地面の中央 x∈[-14,14] に4 m 幅の帯を7本（雪・泥と落ち葉・森の地面・石畳・大理石・アスファルト・砂）並べ、外側は従来の石畳のまま。各帯は色・法線（DirectX の向き）・粗さ・AO・高さ（大理石は無し）を持ち、タイルの長さは実物の大きさに合わせる。テクスチャは `Scripts/FetchPolyHavenTextures.ps1` が落とし、git に入れない。テクスチャが無い帯は石畳にして警告を出す。既定・近接・低角度の撮影で天球・地面・球・岩が見える。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/FetchPolyHavenTextures.ps1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-GROUND-SWATCHES -Configuration RelWithDebInfo`
- stop-when: Poly Haven の API が取れない、または利用条件が CC0 でない場合は理由を記録して止める。
- paths: Game/GameModes/Rendering3DTest, Scripts/FetchPolyHavenTextures.ps1, .gitignore, TASKS.md, PROGRESS.md
- notes: 地面は9枚の手続きの平面（メッシュのハンドル 110〜118）。帯の UV は地面全体の座標から求めるので、帯の境目で模様がずれない。視差の高さは高さマップの深さ / タイルの長さ。テクスチャは6種 × 5枚の 4k jpg（計 213.8 MB）で、`Assets/Textures/PolyHaven/<id>/` に置く（`.gitignore` 済み）。
- result: 2026-10-04 完了（`9d569868`・`d125bfb7`）。取得スクリプトは30枚を落とし、MD5 が一致した。起動画面の撮影（既定 116.6・近接 117.0・低角度 119.5）は result=pass で、6種の材質のテクスチャがすべて読み込まれ（読み込みの失敗なし）、既定視点に7本の帯と外側の石畳、近接・低角度で球・岩・小屋が見えることを PNG を開いて確かめた。このモードの材質は（既存の地面・球の材質と同じく）モードを抜けても解放しない。

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
