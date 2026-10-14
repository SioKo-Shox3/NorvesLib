# PROGRESS — NorvesLib

## G2全体の進捗（2026-10-07）

G2の実装とCPU/CLI受入れは完了。c1690c の CI run 37652726746 は全項目成功（Release CPU 74件、rig/retarget/Role Profile CLI、単体CLI旧出力・texture v1旧PS1のbyte互換、関連Debugケース）。GPU・DCC・実物の見た目は未検証として分離する。

- GR77/78/79/86/32: 取り込みと材質、明示縮約、256関節、submesh/paletteの経路を接続。surface_centroid、複数材質runtimeと対応外描画は明示保留
- GR82/83: 作者rest/frame検査、Skeleton/SkinnedMesh/ClipBank分離、Armature/clip-only、ANLY、非同期公開とdelegateを接続
- GR84: BVH/glTF共通補正、出力fps、周期、平面ルートと向きの分離、独立bankとCLI/asset-setを接続。再生時の適用はG3
- GR96: spec v2混在、依存追跡、増分、最小レポート、予算、jobs同値。詳細異常検出は後段

統合の確認: 2形式を識別する実wire検査2件（g++14）、owner配線8件（通常/最適化）、独立root-frame oracle12件をクラウドで通過。全体のネイティブbuildはWindows依存のため区切りのCIで確認する。

G2統合: main 573d7176との意味上の衝突修正・2回のレビュー・クラウドCPU検査を終えた。G2完了としてmainへ反映し、次はG3のGR12へ進む。

## 過去の履歴

完了済みの経緯とmain側の描画改善履歴は、内容を保持したまま[履歴一覧](Docs/History/2026-10-07-G2Integration/README.md)へ移しました。

統合run94（37690356732）はbuild前の旧recipe照合で停止。mainが更新したクラスタ化（既存mesh smokeの岩のクラスタ数25562→1029）と新しいusage/LOD smokeに対し、分割前の手順hashを同一と見なしていたため。分割時のbyte一致の証拠はc1690c/run93に保持し、統合の検証は現行のCLI smoke・CPU契約・Game buildへ分ける。製品コードのテスト結果はまだ得られていない。

## 統合時点の確認範囲（2026-10-07）

run95（37690973847）のReleaseでCore.lib、AssetCook.exe、AssetSystemTest.exe、RenderResourcesDomainContractTest.exe、JobSystemShutdownTest.exeのビルド成功を確認。CookedMeshTestは追加テスト1行の局所別名V0がスコープ外で停止したため、完全修飾名へ修正した。実ヘッダの名前解決をg++で確認し、wire契約2件も通過した。

統合後の全件CPU実行・merged CLI smoke・Game buildは未実行。run95全体は失敗であり、成功とは扱わない。作者のWindows必須解除と変更ごとのCI禁止に従い、テストの名前修正だけで全CIは回し直さない。G2実装の受入れはc1690c/run93、統合は上述の範囲を証拠として残す。GPU/DCC/実資産の見た目は引き続き別途。

## テクスチャとジオメトリの仮想化（段8まで、2026-10-08）

段1〜7 はそれぞれ main へマージ済み（経過は `Docs/History/2026-10-07-G2Integration/`）。段8（VSM 太陽）はブランチ `feature/vtg-stage8-vsm-sun` の項目がすべて完了し、受入れ（`Docs/RenderingValidation/VirtualizationAcceptance.md` の段8の節。texel は VSM が CSM の 0.21〜0.53 倍、縁の帯は球の自転を止めた 6 組で 0.51〜0.63 倍、揺れは CSM・VSM とも 0.0001 未満、太陽45°の一致 0.9992 以上、GPU 時間は起動画面で +0.07〜+0.71 ms・負荷 300 個で +1.58 ms）を確かめた。main への統合（このブランチを `--no-ff` でマージ）の前に、新しい main（G2 の資産取り込み）を取り込んで、MSVC で読めない BOM の無いソース 7 件への BOM・スキニングのテストの補助のインデックス・サブメッシュごとの影の印と範囲での VSM のスキニングの投影物を直した。段8の経過と完了した項目は `Docs/History/2026-10-08-VTG8/`。段9（VSM 点光源と全体）は TASKS.md の `VTG9-` の項目で進める。
## G3 GR12の姿勢評価（2026-10-07）

骨格の親優先順・bind/rest、クリップの検証と疎なチャンネル索引、メッシュの関節別境界を資産設定時に準備する経路を追加した。LocalPose、再利用scratch、FK・palette・関節モデル行列の公開経路を用意し、既存Samplerは互換wrapper、Componentは準備済みcontextを再利用する。キー探索は二分探索。既定の境界は影響を切り捨てず、浮動小数の範囲外などでは頂点の正確な評価へ戻る。

旧legacy/split実装をテスト用oracleとして保持し、姿勢出力・不正入力・bounds包含・1000回の作業配列再利用・split profile 1/2/3・52関節/6万三角形相当の計測を既存テストへ追加。クラウドのキー探索単体テストは通過。実Coreをリンクした姿勢テストと計測は未実行であり、GR12は検証中。Windows依存の全体をクラウド用に書き換えず、ロードマップ単位のCPU検証としてまとめる。

GR12のrun37698585325ではCoreと関連テストのビルド、SkeletalAnimationSamplingTest（独立旧oracle、bounds、1000回のscratch/data安定、既存baseline30ケース）が成功した。合成52関節・18万頂点（6万三角形相当）・240frameのRelease計測は旧10977.421ms、準備済み新経路6.718ms。準備時間を除く姿勢評価単体であり、ゲーム全体や実素材の倍率ではない。

同runのSkinnedRenderPathContractTestはReleaseでsegfault。既存テストがassert内でregistry/resources初期化とPrepareDrawを行い、その結果を通常コードで読むため、NDEBUGで必要な呼出が消えていた。SkeletalAnimationSamplingTestと同じ常時有効のassertへ直した。修正後の同テスト実行は未確認。GR10の本体は別途進行中。

GR12修正後run37699941320（f5ec362a）は全項目成功。Core・両テストbuild、SkeletalAnimationSamplingTestとSkinnedRenderPathContractTestの2/2が通過し、GR12のCPU受入れを完了とする。再計測は旧11023.486ms、新6.791ms。境界/renderer側テストのRelease segfaultは解消した。

## G3 GR10の実行系（実装中）

PoseOps、typed parameter、検証付きJSONグラフ、7種ノード、状態遷移と割込時のローカル姿勢保存、Update/Evaluate分離、Animator接続、外部駆動、modifier用の遅延FK、AngelScriptの添字APIとdebug viewを追加中。GR11のmetadata/event/root motion、GR10の同期・歩幅合わせ、明示デモ接続は未実装。GR10の本体はWindowsでまだコンパイル・実行していない。数学部分PoseOpsの単体はg++で通過したが、テスト用allocator adapterでありproductionメモリ系の検証を代替しない。

GR10本体の差分では共有nodeの再生時刻、逆向nonloopの初期時刻、非active機械の割込snapshot、重複名の骨格照合、失効資産の再準備拒否、加算の動的基準姿勢を修正した。loader/graph/外部駆動/スクリプトのCPUケースを既存Sampler bundleへ追加した。本体の実行確認をまとめて行い、GR11のmetadata/event/root motion・GR10の同期は引き続き別の未完部分として扱う。

## G3 GR10 / GR11のCPU受入れ（2026-10-08）

4ae627f4のWindows run37714129004は全項目成功。SkeletalAnimationSamplingTest、SkinnedRenderPathContractTest、CookedClipBankV1Test、SkeletalClipBankBindingTestの4件が通過し、AnimGraphRuntimeTest / RootMotionMathTestの完走を実ログで確認した。metadataのloose/cooked、イベント、root motion、marker同期、歩幅倍率を含む。GR10の合成52関節のUpdate/Evaluate計測はDocs/Performance/AnimationGraphRuntime.mdに記録した。実素材・足滑りの目視品質はこの計測から評価しない。

## G3 GR13と調整画面（2026-10-08、検証中）

6db0ac20にソケット定義・runtime sidecar・任意SOCK節、保持slot、profile補間、同フレーム依存順、解除時の速度、script/event/parameterの共通切替を実装した。component除去後の購読・保持予約の清算をCPU回帰に含める。明示--animation-debug時だけ合成sceneを追加し、ImGuiの状態・位相・profile操作と当frameのsocket軸表示へ接続した。

run37717801857はSamplerテストのDelegate型指定でコンパイル停止。戻り値voidの指定漏れを修正し、実Delegateヘッダの同形式lambdaで構築・呼出しを確認した。再検証には優先度・exitTime・補間曲線・割込規則・dt分割・2D clamp・再生再現性を追加し、既存の読み込み・cooked・資産寿命・FramePacketのCPU回帰4件もまとめた。ImGuiの全頂点による厳密境界との比較は明示ON時だけ実行する。Windowsでの再検証前であり、全9テストとGameビルドはまだ合格と扱わない。GameビルドはGPU起動ではない。実リグ・実クリップ、GUI実操作・録画、既定起動画面の比較は未検証。G3全体は受入れ途中で、mainへ未反映。

fa0a1a73のrun37719099230ではSkeletalAnimationSamplingTestとSkinnedRenderPathContractTestが成功し、追加した遷移・再生再現性のケースも通過した。CookedMeshTestのビルドは追加テストのローカル名smallとWindows SDKマクロの衝突で停止。limitedBudgetへ改名して再確認する。保存形式・残り回帰・Gameはまだ未実行。

0bb9b52bのrun37720293720は実行系2件と保存形式3件の計5/5が成功。追加のFramePacket回帰はDeviceCapabilitiesがWindows印刷APIマクロでDeviceCapabilitiesAへ変わり、Core側との型名相違でリンク停止した。テストtargetのWindows SDK露出をCoreと同じWIN32_LEAN_AND_MEANへ揃える。追加4回帰とGameはまだ未実行。

## G3 アニメーション実行系の実装統合（2026-10-08）

GR10〜GR13の実装をmainへ統合する。姿勢評価、アニメーショングラフ、イベント・位相・ルートモーション、ソケット・保持スロット、明示起動の調整画面を含む。mainのVTG8描画実装を保持する。以降はG4の移動とカメラを進める。
## G4 移動・カメラ・モーション調整（2026-10-08）

固定60Hzと描画補間を既定とし、120Hzおよび可変更新へ切り替える経路を接続した。入力の押下/解放を固定更新まで保持し、犬のVisualRootを補間する。カメラ入力は描画フレームで反映する。時間チャンネルとヒットストップを追加した。

キャラクターのカプセル移動、足場、四足移動モデル、Animationのルート移動をGame側で接続した。カメラは平滑追従、衝突回避、速度に応じたarm/FOV、Entity/関節へのロックオン、任意の自動焦点、揺れ、profileの再読込を持つ。比較シーンは明示起動で、既定のRendering3DTestを維持する。

動画由来クリップの調整をAssetCookのretarget経路へ追加した。role-profileから外れ値除去、周期平滑/平均、時間倍率、接地イベント/marker、自然速度に合わせた時間補正、欠けた前進軌跡の導出を指定できる。処理結果はmotion_report.jsonへ記録する。

## 段9（VSM 点光源と全体）の開始（2026-10-08）

- ブランチ `feature/vtg-stage9-vsm-point`（main `a2e6dd81` から）。計画書 §4.3「VSM（点光源）: 6面のキューブを同じ物理プールで持つ」と §5 の段9の受入れ「夜の電球の影、8GB 級の上限での全体の負荷モード」を、TASKS.md の `VTG9-` の 12 項目にした（スライスへの一般化・点光源の面と解像度の段・印付け・描画・MegaGeometry・照明と影の測定・持ち越し・GPU 時間・既定化・負荷モードの影の省略・全体の負荷モード・受入れ）。受入れ（VTG9-ACCEPT）は親が行う。
- 親が決めた細部（計画書 §6）: 点光源は 1 灯 6 面 × 解像度の段（面 4096²、段 0〜5）を、太陽の段と同じ「スライス」の表に並べ、同じ物理プールを使う。深度は面の軸の向きの線形の距離 ÷ Range。照明の PCF の下限は太陽と同じ連続な量。半透明はキューブのまま。影の測定（揺れ・一致）は段8と同じく大きな球の自転を止めて測る（撮影のスクリプトの `-SphereSpin Off`）。全体の負荷モードは段2・段5と同じ `--vram-budget-mb=6500` で模す。
- ランナーの対象を段9の項目に限るため、main 側の別の作業の項目のうち todo・doing だったものを、このブランチの上でだけ一時的に backlog にした。段9を main へマージする前に、次の元の状態へ戻す: `CORE-JSON-SURROGATE`（todo）、`GAME-GR130-VFX`（todo）、`GAME-GR131-VFX`（todo）、`GAME-GR132-VFX`（todo）、`GAME-GR133-VFX`（todo）、`GAME-GR134-VFX`（todo）、`GAME-GR135-VFX`（todo）、`GAME-GR136-VFX`（todo）、`GAME-GR137-VFX`（todo）、`G2-GR79-IMPORT-POLICY-CONNECTION`（todo）、`G2-S6-ASSET-SET`（doing）、`G2-MATERIAL-SELECTION-INTEGRATION`（todo）、`CORE-STRING-REPLACE-TERMINATOR`（todo）、`G2-GR82-B4-STATIC-ROOT-FRAME128`（doing）、`G3-GR12`（todo）。

## 段9 VTG9-VSM-SLICES（2026-10-08）

- Done: 段ごとの値を固定長の uniform（`levelInfo[16]`・`levelOrigin[16]`・`previousOrigin[16]`）から、スライスの表（storage buffer。`GPUVsmSlice` = 96 バイト: 投影の行列の上 3 行・ページの一辺と texel・範囲の原点・ページの表の先頭と一辺・前フレームの原点・投影の種類）へ移した。ページの表の番地は「スライスの先頭 + トーラスの番地」（`VsmSliceEntryIndex`）で、太陽の 10 段は 128 × 128 を先頭から並べる（先頭 = 段 × 16384）ので表・要求のビット列の並びは変わらない。印付け・割り当て・展開・描画・MegaGeometry の cull と dirty の階層・照明・影の測定・`vsm_sample_probe.comp` がすべて表を引く。表は `BuildVirtualShadowMapSlices` がクリップマップから作り、各クラスが自分の使用枠のバッファへ書く（Pages・Raster・MegaCull・照明・影の測定）。
- 検証: ビルド（`verify-VTG9-VSM-SLICES-1.txt`、EXIT=0）。ctest（`-2.txt`）は VirtualShadowMapVulkanTest・VirtualShadowMapClipmapTest・golden 4 本が通り、RenderGraphCompileTest だけが束縛の数（dirty 3→4、cull 9→10、展開 8→9）と cull の定数の大きさ・段ごとの値の読み先の表明で落ちたので、番地の式に合わせて書き換え（比べる値は変えない）、再実行で通った（`-3.txt`）。
- Notes: 段の欄（4 bit）と塊の段の印（32 bit）・スライスの上限 16 は据え置いた。stop-when に従い、(3)(4) と「スライスが 33 個以上の場面」は `VTG9-VSM-SLICES-WIDE`（TASKS.md）へ移した。投影の行列は表に持つが、太陽ではライトの基底が全スライスで同じなので、読むのは印付け（`vsm_mark.comp`）と描画の頂点（`vsm_draw.vert`）だけで、展開・cull・照明の読み出しは従来どおり uniform の基底を使う（点光源の面で基底が変わる項目で表から読む）。ページの表の一辺は表に持つが、dirty の階層の形（128²）と割り当ての欄の全体走査は 128 × 128 のスライスが詰まる前提のまま。
- Next: `VTG9-VSM-SLICES-WIDE`。

## 段9 VTG9-VSM-SLICES-WIDE（2026-10-08）

- Done: スライスを 33 個以上（上限 256）にできるようにした。(1) 展開のインスタンス（`uvec4` の y）を `スライス | 物理ページ << 8` にし、`vsm_expand.comp` の書き込みと `vsm_draw.vert` の読みを合わせた。(2) 塊の段の印（`levelMask`）を 32 スライスずつの組の印にし、`reserved` を組の番号にした。CPU は `SliceMasksForBounds`（スライスの表から組ごとの印）と `PushChunkPerGroup`（複数の組にまたがる投影物は同じ記録の塊を組ごとに出す）で作り、`vsm_mega_chunks.comp` は `1 << (スライス % 32)`・組 = スライス / 32、展開は組のスライスだけを処理する。(3) ページの表・要求のビット列・dirty の階層の大きさを `PageTableBytes(n)`・`RequestWords(n)`・`MegaDirtyBitsBytes(n)` に（`REQUEST_WORDS` を廃止、引数なしは太陽の段の数）。`StatLevelsUsed` は先頭 32 スライスの集合、新しい語 `StatLevelsUsedBeyond`（`STATS_WORD_COUNT` 54）が 33 番目以降の使用の有無。(4) `Pages`・`Raster`・`MegaCull` の dispatch に `SliceCount` と外から渡す `Slices`（null ならクリップマップから作る）を足した。`BuildVirtualShadowMapSlices` は作る件数を引数に取る（照明・影の測定は従来の 16 件）。
- 検証: ビルド（`verify-VTG9-VSM-SLICES-WIDE-1.txt`、EXIT=0）。ctest（`-2.txt`）は VirtualShadowMapVulkanTest・VirtualShadowMapClipmapTest・RenderGraphCompileTest・golden 4 本が通った。VirtualShadowMapVulkanTest にケース W（太陽の 10 段をスライス 30〜39 に置いた 40 スライスで、印付け → 割り当て → 消去 → 展開 → 描画。ページ 18・書いたインスタンス 18・先頭の 10 段のケース F と全 texel 一致、段の集合 0xc0000000・33 番目以降 1、塊 2 → 4）とケース J4（40 スライスの cull・dirty の階層。選んだクラスタ 220・階層は参照と全語一致）、RenderGraphCompileTest に組ごとの印の CPU の検査を足した。期待値の書き換えは大きさの式だけ（`VSM_Stats` 212 → 216 バイト、スライスの表の更新の大きさがスライスの数ぶん、`REQUEST_WORDS` → `RequestWords(LEVEL_COUNT)`）で、比べる値・しきい値は変えていない。
- Notes: 印付けが太陽の段を選ぶスライスの先頭を `MarkFirstSlice`（既定 0。段 L はスライス MarkFirstSlice + L、`vsm_mark.comp` の `params.screen.w`、使っていなかった「1 段の一辺」の欄を転用）にした。点光源の面が太陽の段の後ろに並ぶ構成と、33 番目以降のスライスで要求・割り当てを実際に起こす検査のため。展開の dispatch・ライトの基底は引き続きクリップマップの基底を全スライスで使う（透視のスライスは `SliceMasksForBounds` が印を付けない。点光源の面は VTG9-VSM-POINT-SETUP 以降）。階層の一辺（128²）と割り当ての欄の全体走査は 128 × 128 のスライスが詰まる前提のまま。MegaCull は階層のバッファが専用なので、スライスの数より大きければバッファ全体を 0 にする（従来の 10 段ぶんを必ず 0 にする挙動を保つ）。本番のバッファは太陽の 10 スライスのまま（点光源の項目で増やす）。
- Next: `VTG9-VSM-POINT-SETUP`。

## 段9 VTG9-VSM-POINT-SETUP（2026-10-08）

- Done: (1) 起動引数 `--point-shadow-method=cube|vsm`（既定 cube。`ShadowMethod.h` に `PointShadowMethod`、ApplicationProcessor → RenderWorld/RenderingCoordinator の `PointLightShadowMethod` → `SceneView::SetPointShadowMethod` → `VirtualShadowMapPass`）と `CaptureStartupScene.ps1` の `-PointShadowMethod Cube|Vsm`（常に渡す）・`-SphereSpin On|Off`（Off で `NORVES_STARTUP_SPHERE_SPIN=0` を Game へ渡し、起動直後に環境変数を戻す）。太陽が CSM のときは SceneView が、装置が VSM を作れないときは VirtualShadowMapPass が `VSM_FALLBACK reason=point_requires_vsm` を 1 回出してキューブのまま。(2) `VirtualShadowMapPointLights`（Public/Rendering。`VirtualShadowMapPointLights.h/.cpp`）: 設定（面 4096²・ページ 128・段 6・bias −0.5）、`PointShadowSnapshot` の灯の順に 6 面 × 段をスライスへ並べる（太陽の 10 段の後ろ。`FirstSlice + (灯 × 6 + 面) × 段数 + 段`。4 灯で 144 スライス、上限 256 を超える並びは作らない）、`BuildVirtualShadowMapPointSlices` が透視のスライス（行は面の座標 sc・tc・軸の距離、`info` は NDC の幅、`origin[3]` は 32・16・8・4・2・1）を書く。(3) `SelectVirtualShadowMapPointFace`（主軸）・`SelectVirtualShadowMapPointMip`（texel 2z ÷ 一辺 が p(d)·2^b 以下の最も粗い段、段 0 より細かくは選ばない）・`LocateVirtualShadowMapPointReceiver`（面・段・ページ・NDC・深度 = 軸の距離 ÷ Range）。(5) `VirtualShadowMapPass::Execute` が vsm のとき起動後と灯の数・位置・Range・LightId が変わったときに `VSM_POINT lights= slices= face_res= mips=` を出す。描画はキューブのまま。
- 検証: ビルド（`verify-VTG9-VSM-POINT-SETUP-5.txt`、EXIT=0）。ctest（`-6.txt`）は VirtualShadowMapPointTest・PointShadowFaceMatricesTest・VirtualShadowMapClipmapTest・RenderGraphCompileTest が通った。`VirtualShadowMapPointTest`（CameraViewConstantsTest の束）は、面の選び方が `PointShadowFaceMatrices` の面と一致（NDC・軸の距離 = 行列の w）・全向きがどれかの面に入る・段の texel が目標以下で距離について単調・縁/角を含むページが範囲内・スライスの行が面の座標へ写すことを確かめる。変異（`SelectVirtualShadowMapPointFace` の `ax >= ay && ax >= az` を `<=` に逆転）で VirtualShadowMapPointTest が落ちた（`verify-VTG9-VSM-POINT-SETUP-mutation.txt`）後、元に戻して再ビルド・全件通過。RenderGraphCompileTest に `TestPointShadowMethodWiringAndFallback`（既定 Cube・VSM のパスへ届く・`point_requires_vsm` が 1 回）を足した。
- Notes: ページの表の先頭は太陽と同じ `スライス番号 × 128 × 128`（PageTableBytes(n) のまま。点光源の面は実際には 32² 以下しか使わないので、表の詰め方は描画の項目で見直せる）。透視のスライスの `info` は NDC の幅（2 / ページ数、2 / 一辺）で、太陽の m とは単位が違う。本番のバッファはまだ太陽の 10 スライスのままで、点光源のスライスを書くのは印付けの項目。`-SphereSpin` の `NORVES_STARTUP_SPHERE_SPIN` は Game に既にあった。bash の `/m:1` はパス変換で壊れるので、ビルドは PowerShell で回した。
- Next: 点光源の印付け（`VTG9-VSM-POINT-MARK`）。

## 段9 VTG9-VSM-POINT-MARK（2026-10-08）

- Done: (1) `vsm_mark.comp` を `MarkSun`（従来どおり）と `MarkPoints` に分けた。点光源は binding 9 の `VsmPointParams`（std140 112 バイト。`GPUVsmPointParams`）を引き、`points.header.x` 灯について、Range の内側の画素の面（`SelectPointFace`＝CPU の `SelectVirtualShadowMapPointFace` と同じ順位）・軸の距離 ≥ near・段（粗い段から、texel 2z ÷ 解像度 ≤ 距離 × 画素の大きさ × 2^bias の最初。どの段も超えれば 0）を選び、核の半径（`PointPcfRadiusTexels` 既定 5 texel × 段の texel + `PointFilterRadiusMeters` 既定 0 m。ページの幅で抑える）を面の座標の 3 × 3 の標本にして、標本ごとに向きの主軸の面（スライスの表の `axisX/Y/Z`）を引き直し、NDC をページに写して `MarkPage`。(2) `VirtualShadowMapPagesDispatch` に `PointLights`・`PointPcfRadiusTexels`・`PointFilterRadiusMeters`。`Slices` が null なら Pages が太陽の段の後ろへ点光源のスライスを書く（印付け・割り当ての両方）。点光源の入力が使えない（灯 0・スライスが `SliceCount` に収まらない・ページが 128 を超える等）ときは灯の数 0 で、印付けは太陽だけ。(3) `vsm_allocate.comp`: 投影の種類が透視のスライスの要求・割り当て（既割り当て + 新規）を `StatPointRequested`/`StatPointAllocated` に加算。`STATS_WORD_COUNT` 54→56。(4) `VirtualShadowMapPass`: 点光源の VSM のとき資源を 154 スライスぶんで作り、`SliceCount`・`PointLights` を渡す。`VSM_PAGES` の末尾に `point_requested=%u point_allocated=%u`（requested/allocated は太陽 + 点光源の合計）。
- 検証: ビルド（`verify-VTG9-VSM-POINT-MARK-6.txt`、EXIT=0）。ctest（`-8.txt`）は VirtualShadowMapVulkanTest・RenderGraphCompileTest が通った。VirtualShadowMapVulkanTest にケース P を足した: P1（合成の深度・床と奥の壁、2 灯。灯 0 = (1.5,2,-12) Range 30、灯 1 = (-6,1,-2) Range 6）で太陽のページ 18 + 点光源のページ 22 の集合が倍精度の参照と一致、段 2〜5・面 5 種・面をまたぐ核の画素 368、P1b（プール 20）で溢れ 20・点光源の割り当て 13/22、P2（面の縁をまたぎ隣の面のページが増える 6 画素）で点光源のページ 3（隣の面へ印を付けない変異では 2）。変異（`MarkPointSample` を受け手の面に固定）で P2 が「印が付いたページの集合が参照と違う」で落ちた（`verify-VTG9-VSM-POINT-MARK-mutation.txt`。P1 は他の画素が同じ鍵を覆うので通る）→ 元に戻して全件通過。RenderGraphCompileTest の期待値は `VSM_Stats` の大きさ 216→224 と統計の語の並びの表明（最後の語が点光源の割り当て）、`VSM_PAGES` に点光源の語が出る表明の追加だけ。
- Notes: 点光源のスライスのページの表の先頭は太陽と同じ `スライス番号 × 128 × 128`（点光源の面は 32² 以下しか使わないので表は疎。割り当ての各段の走査は 154 × 16384 欄になる。詰め方は描画の項目で見直せる）。点光源のページのキャッシュ（灯が動いたときの無効化）は VTG9-VSM-POINT-CACHE の項目で、この項目では描画しないので古い内容は見えない。起動画面は撮っていない（既定は `--point-shadow-method=cube` のまま）。GPU の `entryGroups` の増加の費用は VTG9-VSM-POINT-GPU-TIME で測る。
- Next: `VTG9-VSM-POINT-RASTER`。

## 段9 VTG9-VSM-POINT-MARK 差し戻しの修正（2026-10-08）

- 評価者の指摘 2 件を直した。(1) 太陽が無いと点光源にも印が付かなかった: `VirtualShadowMapPages::Record` の `bMark` を `bSunMark`（太陽のクリップマップ由来の入力）と `bPointMark`（点光源の入力。深度があれば成り立つ）に分け、共通の入力（逆射影行列・カメラ位置・画面の大きさ）は `FillCommonMarkParams` に出した。太陽の段の数（`control.w`）が 0 のとき `vsm_mark.comp` は太陽の印付けを飛ばす。キャッシュの引き継ぎは太陽があるフレームだけ（点光源の無効化は VTG9-VSM-POINT-CACHE）。(2) 面をまたぐ核の内側で隣の面のページを取りこぼした: 3 × 3 の標本をやめ、核を受け手の面の接平面上の正方形として 6 面の錐台（近い面と 4 つの縁）で切り、残った多角形の頂点を NDC へ写した外接の範囲のページすべてに印を付ける（`MarkPointFootprint`）。平面の多角形の透視像は凸なので外接の範囲は取りこぼさない。受け手から届かない面は「面の向きの成分が最大成分より 2 × 余裕以上小さい」で飛ばす。
- 検証: ビルド（`verify-VTG9-VSM-POINT-MARK-11.txt`、EXIT=0）。ctest（`-12.txt`）は VirtualShadowMapVulkanTest・RenderGraphCompileTest が通った。VirtualShadowMapVulkanTest の参照は、核の正方形を各面で切る倍精度の実装に差し替え、核の大きさとページ番号の境を縮めた場合と広げた場合で集合が変わる画素は曖昧として除く。ケース P3（評価者の反例と同じ「面の縁の近く」の向きを 4 種類の縁から探し、3 × 3 の標本では取りこぼす 4 灯を 1 画素の周りに置く。印の集合 16 ページが参照と一致）と N1（太陽のクリップマップなしで全画素。点光源 22 ページ・太陽の段には要求も割り当ても無い）を足した。変異（受け手の面以外に印を付けない）で P1・P2・P3 が落ちた（`verify-VTG9-VSM-POINT-MARK-mutation.txt`。P3 は GPU 10 ページ・参照 18 ページ）→ 戻して全件通過。
- Notes: 評価者の数値例（光源からの相対位置 (0.9999, −0.4985, 1)・段 0・解像度 4096）は、Python の倍精度で再現し、新しい方式が取りこぼしていた (+X,0,24) を含むことを確かめた。この向きは面の選び方の境（0.0001 × 距離）に近すぎて単精度の GPU とは一致が保証できないので、テストでは境から 0.0006 以上離した向きを使う。起動画面は撮っていない（既定は `--point-shadow-method=cube` のまま）。
- Next: `VTG9-VSM-POINT-RASTER`。

## 段9 VTG9-VSM-POINT-RASTER（2026-10-08）

- 実装: 展開（`vsm_expand.comp`）に透視のスライス用の `PerspectivePageRange` を足した。塊の境界球（AABB の中心と、半径 = 半寸法の長さ）を面の座標へ写し、Range の外・近い平面より全部手前・遠い平面の外・錐台の 4 側面の外を除き、球が近い平面（z ≤ near）をまたぐときは面全体、そうでなければ原点から球への接線の傾き（`x = t z` の 2 根）を NDC の範囲にして、覆うページを数える。描画（`vsm_draw.vert`）の透視の枝は、ワールドの位置を面の行列で面の座標へ写し、`gl_Position = (ページの局所の NDC × z, 近い平面で 0・Range で w になる線形の z, w = 面の軸の距離)`、`outDepth = 軸の距離 ÷ Range` にする（断片シェーダーは太陽と同じ `atomicMin`）。透視のスライスの `info[2]` = Range・`info[3]` = 近い平面（`BuildVirtualShadowMapPointSlices` が書く。正射影は 0 のまま）。
- 集め方: `SliceMasksForBounds` が透視のスライスにも印を付ける（`PerspectiveSliceTouchesBounds`: 展開と同じ手順を倍精度で、球をわずかに広げて判定）。`VirtualShadowMapPass::CollectCasters` は点光源の VSM で灯があるとき、太陽の段の後ろに点光源の面を並べたスライスの表を作って手続きメッシュ・スキニングの塊の印に使い（太陽が無効でも集める）、`RecordRaster` が同じ表を展開・描画へ渡す。`VirtualShadowMapRaster::Record` は表が渡されていれば太陽のクリップマップが無効でも記録する（正射影の基底・深度の範囲は既定値。透視のスライスは読まない）。太陽が無いフレームでは MegaGeometry のカリング・キャッシュの無効化の計画は行わない（MegaGeometry の点光源は VTG9-VSM-POINT-MEGA、キャッシュは VTG9-VSM-POINT-CACHE）。cube の既定ではスライスの表を使わず従来どおり。
- 検証: ビルド（`verify-VTG9-VSM-POINT-RASTER-4.txt`、EXIT=0）。ctest（`-5.txt`）は VirtualShadowMapVulkanTest・RenderGraphCompileTest が通った（`-6.txt` は VirtualShadowMapClipmapTest・VirtualShadowMapPointTest も通り、ケース R の出力を含む）。VirtualShadowMapVulkanTest にケース R を足した: 灯 1（Range 20）の面 0 の段 0（4096²）の 16 × 12 ページと全 6 面の段 2（1024²）の 8 × 8 ページをホストが割り当て済み・dirty で書き、軸に垂直な四角形・傾いた四角形・面 0 と隣の面の境をまたぐ四角形・光源の後ろの頂点を持つ三角形・面 0 の中心の小さな四角形・Range の外の四角形を展開 → 描画した。期待値は倍精度の光線と多角形の交点の「軸の距離 ÷ Range」で、縁・近い平面・Range の近くは曖昧として除き、比べた 9376334 texel（覆われた 5905427）で不一致 0・最大誤差 1.8e-5。境際の覆われた texel は面 0 で 3798・隣の面で 14717、隣の面の段 2 に 1034318 texel が描かれ、近い平面より手前の深度は 0。Range の外の四角形だけが塊の印が空で、小さな四角形のインスタンスは面 0 のスライスだけ。インスタンスのスライスは塊の印と倍精度の判定の両方で写るもの。太陽のクリップマップを無効にして渡しても物理ページ・インスタンスの数が同じ。RenderGraphCompileTest に `SliceMasksForBounds` の透視の判定（軸の上の物は面 0 だけ・Range の外は空・光源を含む物は全面・面の境の物は隣の面と両方・光源の後ろは面 0 に付かない・info が 0 のスライスと非有限の境界は空）を足した。変異は `-mutation-w1.txt`（`gl_Position` の w を 1 にする。不一致 5506726・最大誤差 0.99）と `-mutation-euclid.txt`（`outDepth` を Euclid の距離 ÷ Range にする。不一致 5895339・最大誤差 0.29）でどちらも VirtualShadowMapVulkanTest が落ち、シェーダーを戻して通過を確かめた。
- Notes: 点光源のスライスの `info[0]`・`info[1]` は面の NDC の幅（ページ・texel）なので、描画の透視の枝は「面の NDC + 1」からページの局所の texel を求める。表の既存のコメント（GPUVsmSlice・VirtualShadowMapSlice.glsl・Raster.h）は更新した。球の判定は保守的（4 側面は球と平面を別々に見るので、角の近くで錐台の外の球が残ることがある。余計な印はインスタンスが 0 になるだけ）。起動画面は撮っていない（既定は `--point-shadow-method=cube`）。
- Next: `VTG9-VSM-POINT-MEGA`。

## 段9 VTG9-VSM-POINT-MEGA（2026-10-08）

- Done: MegaGeometry の投影物のカリング（`vsm_mega_cull.comp`）が透視のスライス（点光源の面）も太陽の段と同じ dispatch で処理する。判定は展開と共有の `VsmPerspectivePageRange`（Common/VirtualShadowMapSlice.glsl へ移動）で、Range・近い/遠い平面・4 側面で球を切り、透視像が覆うページに dirty があるか dirty の階層（面の座標のページ。`vsm_dirty_mips.comp` はページの表の一辺の外を読まない）で見る。LOD は texel = 球の最も近い点の面の軸の距離 z での 2z ÷ 段の解像度（自分・親・メッシュ共通の球でそれぞれ）。HZB・円錐・ページの要求・1 面あたりの上限は無い。Raster の MegaCull は外から渡すスライスの表があれば太陽が無効でも記録し、Pass は点光源の VSM のとき dirty の階層をスライスの数で作り、スライスの表を渡して夜のフレームも記録する。統計の語 56・57（`STATS_WORD_COUNT` 58）と `VSM_MEGA_CULL` の `point_instances`・`point_clusters` を足した。
- 検証: ビルド（`verify-VTG9-VSM-POINT-MEGA-3.txt`、EXIT=0）。ctest（`-4.txt`）は VirtualShadowMapVulkanTest・GeometryPageRequestVulkanTest・RenderGraphCompileTest が通った。ケース J5 は 1 灯 × 36 スライス・太陽なしで、12 m は葉 8・4・2・根、40 m は 2・根（遠いほど粗い）、一つの切り口、面のページの表（一辺 32〜1）の dirty の階層が参照と全語一致、溢れで再描画の印。変異（親の条件を外す）で J5-1 が落ちた（`-mutation.txt`）→ 戻して通過。RenderGraphCompileTest は点光源の VSM のカリングが太陽と同じ流れ（dispatch 1 組・z = 154・主の経路のバッファに触れない）で、太陽なしでも記録されることを確かめる。期待値の書き換えは `VSM_Stats` 224 → 232 と統計の語の並びだけ。
- Notes: MegaGeometry のクラスタの記録から点光源の面の物理ページへ描く GPU の通しの検査は無い（ケース K = 太陽、ケース R = 手続きの塊）。点光源 × MegaGeometry の通しは VTG9-VSM-POINT-GPU-TIME の撮影（`VSM_MEGA_CULL` の point_*）と VTG9-VSM-POINT-DEFAULT-ON の起動画面で初めて見る。起動画面は撮っていない（既定は cube）。
- Next: `VTG9-VSM-POINT-SAMPLE`。

## 段9 VTG9-VSM-POINT-SAMPLE（2026-10-08）

- Done: (1) `Common/VirtualShadowMapPoint.glsl`（新規）の `VsmSamplePointShadow` が点光源の VSM を読む。受け手の面（主軸）・軸の距離・解像度の段は `vsm_mark.comp` の `MarkPoints` と同じ式と順序（texel 2z ÷ 一辺 ≤ カメラ距離 × 画素の大きさ × 2^bias の最も粗い段、どの段も超えれば段 0）。法線の向きへのずらしは texel × (0.6 + 1.4 × (1 − NdotL))、深度の比較の余裕は texel × (1.0 + 受け面の傾き)（傾きは 1 texel ぶん動いたときの軸の距離の変化、上限 8）で、`PointShadow.glsl` と同じ考え方を使う段の texel に比例させた。(2) PCF は 16 点（Poisson）を受け手の接平面上の半径 r = max(カメラ距離 × 画素の大きさ × `PCF_MIN_RADIUS_PIXELS`, 受け手の段の 1 texel) の円盤に置き、標本ごとに自分の面・ページの表を引く（面の縁をまたぐ円盤は隣の面のページを読む）。段が無ければ粗い段へ順に逃げ、どの段にも無ければ影なし（キューブの値は使わない）。逃げた標本は照明の統計の語 1 に数え、終了時の `VSM_LIGHTING_STATS` に `point_fallback_samples=` を足した。(3) `GPUVsmPointSampleParams`（`VirtualShadowMapPointLights.h`。std140 128 バイト）と `BuildVirtualShadowMapPointSampleParams`。`VirtualShadowMapPass` が点光源の並びを `PhysicalLighting.PointVsmLights` へ公開し、`LightingPass` が binding 26 のパラメータと、太陽の段の後ろに点光源の面を並べたスライスの表（256 件）を作る。ページの表・プールは太陽が無効（夜）でも点光源だけで束縛する。`lighting.frag` は灯の番号が公開した灯の数の内側のとき VSM、それ以外はキューブ（`forward_transparent.frag` は変更なし）。(4) 影の測定: `--shadow-probe=point`（`ApplicationProcessor` → `RenderWorld`/`RenderingCoordinator` の `bShadowProbePointOnly` → `SceneView` → `ShadowProbePass::SetPointOnly`）と、太陽が使えないフレーム（夜）で、影を持つ最初の灯（キューブの番号 0）の可視度を同じ標本でキューブ（`SamplePointShadow`）と VSM（`VsmSamplePointShadow`）で求め、`SHADOW_PROBE light=point method=cube|vsm ...`・`SHADOW_PROBE_AGREE light=point ...`・`SHADOW_PROBE_DETAIL light=point method=vsm ...` を出す（mean_texel_mm はキューブは面の軸の距離 z で 2z ÷ 一辺、VSM は使った段の texel。Range の外・光の当たらない向き・近い平面の内側の標本は数えない）。統計の語は 20 → 40、前のフレームの可視度は標本の数 × 4。`--shadow-probe=point` と夜は太陽の csm の行を出さない。`CaptureStartupScene.ps1` に `-ShadowProbeLight Auto|Point` と、`light=point` の行を `metrics.json` の `shadow_probe_point` へ書く処理を足した。
- 検証: ビルド（`verify-VTG9-VSM-POINT-SAMPLE-6.txt`、EXIT=0）。ctest（`-7.txt`）は VirtualShadowMapVulkanTest・RenderGraphCompileTest・golden 4 本が通った（golden は変わらない）。`VirtualShadowMapVulkanTest` のケース S（ケース R の物理ページの四角形の場面。`vsm_sample_probe.comp` から照明と同じ関数を受け手の点で評価。出力は `-vsm-verbose.txt`）: S1 影の中心 5 点は 0・texel は受け手の段（5.86 mm）・逃げた 0、S2 影の外は 1、S3 縁（Q1 の縁を 0.00025 刻みで 13 点）は 0,0,0,0,0.25,0.3125,0.625,0.8125,1,… で単調、独立の参照（Poisson の 16 点の面・ページ・texel を倍精度で求め、多角形との光線の交点で判定）と不一致 0、S4 段 0 のページが無い位置は段 2 の値（影 0・texel 23.4 mm・逃げた 48 = 3 点 × 16）、S5 どの段にも無ければ影なし（1）・逃げた 32、S6 面 0 と隣の面の境をまたぐ円盤が Q3 の影（0）。変異: 面の選び方をずらす（+X と -X を入れ替え）で S1〜S6 が落ち（`-mutation-face.txt`）、逃げ道を外す（候補を自分の段だけ）で S4 が落ちた（`-mutation-fallback.txt`）→ 戻して通過。`RenderGraphCompileTest` に点光源の統計の語の合計と比の検査（`TestShadowProbeTotalsAggregatePointWords`）を足した。`lighting.frag`・`shadow_probe.comp`・`vsm_sample_probe.comp` は glslc（`NORVES_VSM_STATS` あり/なし）でもコンパイルが通る。
- Notes: 起動画面・Game の実行では確かめていない（既定は `--point-shadow-method=cube` で、照明は従来のキューブのまま）。照明の VSM の読み出しと `--shadow-probe` の点光源の測定は、コンパイルと GPU テスト（照明と同じ関数）までで、実フレームでの通しは VTG9-VSM-POINT-GPU-TIME の撮影と VTG9-VSM-POINT-DEFAULT-ON の起動画面で初めて見る。テストの受け手は四角形 Q1 の真後ろ（軸の距離 12）に置く。光源の後ろへ伸びる三角形 T が面 0 の NDC で (0.167, 0) から右下に広がるので、縁・影の外の受け手は v = 0.3 付近にした。`LightingPassLightPacking` は実キューブが公開されたフレームだけ影の番号（`attenuation.w`）を詰める。キューブを描かない構成へ進むとき（VTG9-VSM-POINT-DEFAULT-ON 以降）は、ここも直す。深度の比較の余裕・法線のずらしの係数（余裕 1.0 texel + 傾き、ずらし 0.6 + 1.4 × (1 − NdotL) texel）は、起動画面でのアクネ・浮きの確認がまだなので、DEFAULT-ON の撮影で見て調整する。
- Next: `VTG9-VSM-POINT-CACHE`。

## 段9 VTG9-VSM-POINT-CACHE（2026-10-08）

- Done: (1) 灯の変化によるスライスの無効化: `BuildVirtualShadowMapPointSliceInvalidation`（`VirtualShadowMapPointLights.cpp`）が、前フレームの点光源の並びと比べて、灯の識別子・位置・Range が前フレームの同じ番号の灯と違うスライスを求める（先頭の番号・1 灯のスライスの数・面の解像度・ページの一辺・段の数が違うときは前後どちらのスライスも対象。`BiasLevels` の違いは見ない）。`VirtualShadowMapPages` が前フレームの並びを持ち、印を付けたスライスの `extra.w`（`SLICE_FLAG_INVALIDATE`）を `vsm_allocate.comp` の引き継ぎ（段階 0）が見て、全ページに dirty を付ける。灯が無くなったスライスは、点光源の領域のスライスとして透視の種類にしたうえで dirty にするので、要求が無く dirty の欄として次の年齢の段階で空きへ戻る。(2) 動いた投影物の無効化: `PlanInvalidation` が太陽が無くても点光源のスライスの表があれば動きを追い、動いた物の前後の境界を覆う球（`BuildInvalidationSpheres`。箱の外接球）を `InvalidationSpheres` として渡す。`vsm_allocate.comp` の段階 1 は、透視のスライスでは矩形の代わりに球を使い、展開・カリングと同じ `VsmPerspectivePageRange`（Range の内側・近い平面・錐台の側面・近い平面をまたぐと面全体）が覆うページのうち割り当て済みのものに dirty を付ける。(3) 太陽の段（正射影）と点光源の面（透視）の無効化の指定を分けた（`CACHE_FLAG_INVALIDATE_ALL` は太陽の段、`CACHE_FLAG_INVALIDATE_POINT` は点光源の面）。太陽の向きの変化は点光源のページに及ばない。キャッシュは太陽か点光源のどちらかが印付けをしたフレームで引き継ぐ（以前は太陽が無いフレームは引き継がなかった）。太陽の印付けの有無が前フレームと変わったフレーム（昼夜の切り替え）は全部を割り当て直す。(4) `VSM_CACHE` に `point_cached=` `point_rendered=` `point_invalidated=` `point_released=` を足した（統計の語 58〜61。`STATS_WORD_COUNT` 58 → 62、`VSM_Stats` 232 → 248 バイト）。
- 検証: ビルド（`verify-VTG9-VSM-POINT-CACHE-4.txt`、EXIT=0）。ctest（`-5.txt`）は VirtualShadowMapVulkanTest・RenderGraphCompileTest が通った（`VirtualShadowMapPointTest` も通過、`-pointtest.txt`）。`VirtualShadowMapVulkanTest` のケース T（太陽なしの 2 灯 × 6 面 × 6 段。深度から印を付けて 22 ページ = 灯 0 が 15・灯 1 が 7。各灯の各面の背景の四角形 12 枚と、灯 0 の要求のあるページの内側に置いた小さな四角形を、展開・描画する。どのフレームも、同じ場面をキャッシュを使わない別の記録で描き直した物理ページと全 texel で比べる。出力は `-vsm-verbose.txt`）: T1 最初のフレームは 22 ページすべてを描き持ち越し 0。T2 止まった灯と投影物の 2 フレーム目は描いたページ 0・無効にしたページ 0・持ち越し 22・展開のインスタンス 0・物理プールが不変。T3 四角形を 0.9 ページぶん動かすと球 2 つ（前後の境界）で 6/22 ページだけが描き直され、箱を面へ写した範囲のページの取りこぼし 0・球の範囲（倍精度の参照）の外 0・描き直さなかった場合に変わる texel は 100 超・毎フレーム描き直した結果との texel の違い 0。T4 灯 0 を 0.5 m 動かすと無効なスライスが 36、灯 0 の 15 ページがすべて描き直され、灯 1 の 7 ページは 0 枚・texel の違い 0。T5 灯 0 と灯 1 を入れ替えると無効なスライスが 72 で 22 ページがすべて描き直され、texel の違い 0。T6 灯 1 が無くなると 36 スライスが無効で、灯 1 の 7 ページが空きへ戻り（`point_released` 7）、灯 0 の 15 ページは描き直されず、texel の違い 0。CPU の `VirtualShadowMapPointTest` に `TestSliceInvalidation`（位置・Range・識別子の変化・入れ替え・増減・null・先頭の番号と解像度の変化・`BiasLevels` の無視）を足した。変異: 灯の判定から位置・Range の比較を外す（識別子だけ見る）と T4 が落ち（灯 0 のページ 15 枚が描き直されず、毎フレーム描き直した結果との texel の違い 102982）、`TestSliceInvalidation` も落ちた（`-mutation-lightmove.txt`）。動いた投影物の球を渡さないと T3 が落ちた（描き直したページ 0/22、取りこぼし 6、texel の違い 3904。`-mutation-spheres.txt`）。どちらも戻して通過。`RenderGraphCompileTest` の統計の語の並びの検査と `VSM_Stats` の大きさ（232 → 248）を新しい並びに合わせた。
- Notes: 起動画面・Game の実行では確かめていない（既定は `--point-shadow-method=cube`）。灯の並びが変わったとき（カメラに近い順に選ぶので、カメラが動くと入れ替わる）は、同じ識別子の灯のページを新しい番号のスライスへ移さず、新しい番号のスライスを無効にして描き直す（ページの表の欄がスライスの番号で決まり、照明が灯の番号からスライスを求めるため。移すには欄の入れ替えの段階と作業用のバッファが要る）。灯が近い順に入れ替わり続ける場面では持ち越しの利得が小さい。移す方式にするかは VTG9-VSM-POINT-GPU-TIME で灯が入れ替わる場面の費用を見て決める。動く投影物の球は箱の外接球なので細長い物は広めに無効になる。無効にするスライスの印は Pages が自分で作るスライスの表にだけ入る（外から渡す表では点光源のページを引き継がない）。ケース T の場面は小さい（点光源のページ 22 枚）。起動画面の電球（Range 10 m）の範囲に自転する大きな球が入るので、そのページは毎フレーム描き直しになる（VTG9-VSM-POINT-GPU-TIME で測る）。出力の日本語は端末の文字コードで化ける（数値は読める）。
- Next: `VTG9-VSM-POINT-GPU-TIME`。

## 段9 VTG9-VSM-POINT-CACHE 差し戻しの修正（2026-10-08）

- 評価者の指摘 2 件を直した。(1) 灯の並びが変わると同じ識別子の灯のページも全部描き直していた: `BuildVirtualShadowMapPointSliceInvalidation` が灯の識別子で前フレームの灯と今フレームの灯を対応づけ（前フレームの灯は 1 回だけ使う）、`VirtualShadowMapPointRemap`（今フレームの灯の領域ごとの出どころ）を返す。`VirtualShadowMapPages::RemapPointPageTable` が、点光源のページの表の領域を作業用バッファ（`VsmPointRemapScratch`）へ退避してから新しい番号の領域へ書き戻す（入れ替えでも読み終えてから書く）。別の番号へ移った灯の元の領域は 0 にして物理ページの二重所有を避ける。対応する灯が無い領域は従来どおり古い内容を無効にして、要求の無いページは空きへ戻す。位置か Range が変わった灯は新しい番号のスライスの全ページを無効にする。ページの表のバッファに `TransferSrc` を足した（`VirtualShadowMapPass`・テストの `CreateResources`）。(2) 境界球が単精度の丸めで箱を覆えなかった: `BuildInvalidationSpheres` は単精度へ丸めた中心から箱の最も遠い隅までの距離を半径にし、半径は単精度の外側（大きいほう）へ丸める。
- 検証: ビルド（`verify-VTG9-VSM-POINT-CACHE-15.txt`、EXIT=0）。ctest（`-16.txt`）は VirtualShadowMapVulkanTest・RenderGraphCompileTest・VirtualShadowMapPointTest が通った。ケース T5 は、灯の入れ替え（位置は同じ）でスライスの無効 0・描き直し 0・22 ページが新しい番号へ移り、毎フレーム描き直した結果と全 texel で一致（`-14-vsm-verbose.txt`）。T5b は入れ替わって灯が動くと、動いた灯の 15 ページだけが描き直され、動かない灯の 7 ページは 0、texel の違い 0。T6 は並びを戻しても描き直し 0。`VirtualShadowMapPointTest` に入れ替え・先頭の灯の増減・同じ識別子の重複のときの出どころの検査と、`(1048576, 0, 0.5)`〜`(1048576.125, 0.125, 0.5)` の薄い箱の隅がすべて球の内側にある検査を足した。変異: ページの移し替えを外すと T5・T5b が落ち（`-mutation-remap.txt`）、球の半径を箱の対角線の半分に戻すと `VirtualShadowMapPointTest` が落ちた（`-mutation-sphere.txt`）→ 戻して通過。
- Notes: 起動画面・Game の実行では確かめていない（既定は `--point-shadow-method=cube`）。灯がカメラに近い順に入れ替わり続けても、位置が同じなら描き直しは起きない。移し替えの費用は点光源のページの表（1 灯 36 スライス × 16384 欄 × 4 バイト ≒ 2.4 MB）の退避と書き戻しで、並びが変わったフレームだけ。
- Next: `VTG9-VSM-POINT-GPU-TIME`。

## 段9 VTG9-VSM-POINT-GPU-TIME（2026-10-08）

- 結果: 測定は完了。止め条件（負荷モードの VSM のフレーム GPU がキューブより 2 ms 以上遅い）に当たったので、ここで止めて対策の項目 `VTG9-VSM-POINT-CULL-PERF` を TASKS.md に足した。RelWithDebInfo・RTX 4080・`-GpuTimingFrames 300`（240 フレームを集計）・夜。値は撮影の `metrics.json` の `gpu_timing[]`（`gpu_frame_ms_median`・`pass_median_ms`）と各 run の `*.Game.log` の最後の `VSM_*` 行。
- GPU 時間（中央値 ms。Cube / Vsm）:

| 視点 | フレーム GPU | LightingPass | ShadowMapPass | VirtualShadowMapPass | 内訳 Mark / Allocate / CullMega / Expand / Draw | 差（Vsm − Cube） |
|---|---|---|---|---|---|---|
| 既定 | 2.409 / 2.799 | 0.736 / 0.709 | 0.113 / 0.083 | 0.044 / 0.740 | 0.144 / 0.249 / 0.158 / 0.019 / 0.146 | +0.39 |
| 近接 | 2.335 / 2.878 | 0.736 / 0.753 | 0.111 / 0.084 | 0.045 / 0.868 | 0.174 / 0.254 / 0.160 / 0.020 / 0.235 | +0.54 |
| 低角度 | 2.421 / 2.859 | 0.698 / 0.648 | 0.122 / 0.088 | 0.046 / 0.832 | 0.139 / 0.274 / 0.163 / 0.021 / 0.206 | +0.44 |
| 負荷 300 個（既定の視点） | 3.682 / 17.299 | 0.653 / 0.786 | 0.118 / 0.118 | 0.034 / 13.412 | 0.141 / 0.232 / 11.723 / 0.103 / 1.153 | **+13.62** |

  キューブ側の `VirtualShadowMapPass`（0.03〜0.05 ms）は太陽の VSM の空の段で、夜は何も描かない。VSM 側の p95 は 4.5〜4.9 ms（負荷は 18.0 ms）で、負荷の VSM は 16.6 ms の予算を超えるフレームが続く（`gpu_over_budget` の注記では `VsmCullMega` が 10〜12 ms）。
- ページ・統計（VSM の run の最後の行）:

| 視点 | VSM_POINT | VSM_PAGES（点光源の分）requested / allocated | VSM_RASTER chunks / instances | VSM_MEGA_CULL instances / clusters（点光源の分は同値） | VSM_CACHE cached / rendered / invalidated / released（点光源の分は同値） |
|---|---|---|---|---|---|
| 既定 | 1 灯・36 スライス・面 4096・6 段 | 181 / 181 | 3290 / 13873 | 26 / 3217 | 25 / 154 / 156 / 3 |
| 近接 | 同上 | 165 / 165 | 3177 / 20016 | 15 / 3107 | 0 / 165 / 165 / 0 |
| 低角度 | 同上 | 296 / 296 | 3670 / 18487 | 28 / 3606 | 2 / 293 / 293 / 5 |
| 負荷 300 個 | 同上 | 183 / 183 | 42612 / 113929 | 167 / 42536 | 25 / 155 / 158 / 3 |

  夜は太陽が無いので、`VSM_PAGES` の requested・allocated はすべて点光源の分（太陽の分は 0）。キューブの run の `VSM_PAGES`・`VSM_CACHE` は全部 0（VSM を使わない）。
- 溢れ: VSM の 4 run の全行（`VSM_PAGES` の `overflow`、`VSM_RASTER` の `overflow`、`VSM_MEGA_CULL` の `overflow`）で最大値が 0。`failures` は 8 つの `metrics.json` で空。
- 画: VSM の 4 枚（既定・近接・低角度・負荷の既定。`.harness/runs/startup-capture/VTG9-VSM-POINT-GPU-TIME-vsm*/`）の PNG を開いた。電球の影は、並んだ小さな球の群れの長い影、大きな球の接地の影、岩（見本の球と岩）の影、近接の大きな球が石畳に落とす影が、欠け・ずれ・面の継ぎ目・ページの継ぎ目なく連続して見える（負荷の既定は岩が増えて電球のまわりの岩にも影が落ち、小屋は電球の光の範囲の外で暗いまま）。壊れて見える箇所は無いので、キューブの PNG との画素の差は取らなかった。
- 重い区間: 負荷の `VsmCullMega` が 11.7 ms（`VirtualShadowMapPass` 13.4 ms のうち）。通常の場面の同じ区間は 0.16 ms で、選ばれたクラスタが 13 倍（3217 → 42536）に対して時間は約 74 倍なので、選ばれた数ではなく、クラスタごとの判定（点光源の 36 スライスぶんの `LevelOverlapsSphere`・`SphereHasDirtyPage`・LOD）の総量が支配していそう（未確認。シェーダーの構造から: 1 スレッド = 1 クラスタ × 1 スライスで、インスタンスの境界の判定を通ったワークグループだけがクラスタを調べる）。`VsmDraw` は 1.15 ms で問題ない。
- Notes: `VTG9-VSM-POINT-CACHE` は評価者の 2 周で blocked（`blocked/VTG9-VSM-POINT-CACHE.md`。先頭の灯を消すと `released` が返却数を数えない統計の退行）のまま。この測定は統計の値に影響しない（灯は 1 つ）。測定は VTG9-VSM-POINT-CACHE の最新の実装（c713c80b）で行った。`-Deterministic` は使っていない。出力の日本語は端末の文字コードで化ける。
- Next: `VTG9-VSM-POINT-CULL-PERF`（負荷の `VsmCullMega` を縮める）。直ったら `VTG9-VSM-POINT-GPU-TIME` を `todo` に戻して負荷の 2 run を測り直す。

## 段9 VTG9-VSM-POINT-CULL-PERF（2026-10-08）

- 結果: カリングは縮んだ（`VsmCullMega` 11.72 → 0.17 ms）が、done-when の「フレーム GPU の差 2 ms 未満」には届かないので止めた（`blocked/VTG9-VSM-POINT-CULL-PERF.md`）。残りの差はカリング以外の区間の合計で、カリングを 0 にしても 2 ms を切らない。実装・テストはこのコミットに入れた。
- 切り分け（負荷 300 個・夜・既定の視点・RelWithDebInfo・`-GpuTimingFrames 300`、`VsmCullMega` を `VsmCullDirty`・`VsmCullPairs`・`VsmCullSelect`・`VsmCullChunks` に分けて測った。`.harness/runs/startup-capture/VTG9-VSM-POINT-CULL-PERF-base/`）: 11.70 ms のうち `VsmCullSelect` が 11.64、dirty の階層 0.03、塊の記録 0.02。支配していたのは選択の dispatch の数で、1 インスタンスが 281 ワークグループ（約 1.8 万クラスタ）、インスタンス 306 個 × スライス 154 個ぶんを出し、そのうち使うスライスは 36 だけ。通る（インスタンス、スライス）の組は 167 個（全体の 1.5%）しかないのに、約 3.1M のワークグループのすべてが二分探索とインスタンスの判定（`LevelOverlapsSphere`・`SphereHasDirtyPage`）を繰り返していた。クラスタごとの判定の中身ではなく、組の判定の繰り返しが原因。
- 実装: `vsm_mega_cull_pairs.comp`（新。1 スレッド = 1 組。インスタンスの判定を組ごとに 1 回行い、通った組を一覧にして、組の最大のワークグループ数を持つ）→ `vsm_mega_cull_args.comp`（新。1 スレッド。組の数 × 最大のワークグループ数から間接 dispatch の引数を書く）→ `vsm_mega_cull.comp`（通った組だけを間接 dispatch で受け持つ。通し番号 ÷ 最大のワークグループ数で組、余りで組の中のワークグループ。クラスタの判定は従来のまま）。インスタンスの判定の関数は `Common/VirtualShadowMapMegaSphere.glsl` に移して 2 つのシェーダーが共有する。組の一覧は `VirtualShadowMapMegaCull::Use` が持つ（`VsmMegaCullPairs`。頭 8 語 + インスタンスの数 × スライスの数の組。足りなければ 2 のべきに切り上げて作り直す）。`VsmCullMega` の内訳の区間を足した。
- 結果の測定（同じ run 設定。`VTG9-VSM-POINT-CULL-PERF-vsm-stress`、`-final-stress.txt`）: `VsmCullMega` 0.169（Select 0.103・Dirty 0.032・Chunks 0.019・Pairs 0.009）、`VirtualShadowMapPass` 13.36 → 1.83。選んだクラスタ 42.5k（従来 42.5k と同じ）、`VSM_RASTER` の chunks 42.6k・instances 114k（従来 42.6k・114k）。VSM の run のページの表・ラスタ・カリングの 3 種の overflow は、負荷 1 run と通常の 3 視点（`VTG9-VSM-POINT-CULL-PERF-vsm/`）の全行で 0。`failures` は空。
- フレーム GPU の差（キューブ − VSM。同じ設定の対の run）: 静かな run で 3.69 → 5.96 ms（+2.27）。その後 GPU に別の負荷が乗って測定がぶれた（キューブ 3.97〜4.47、VSM 6.18〜7.70。`VTG9-VSM-POINT-CULL-PERF-pair-*`）ので、フレーム全体の差は ±1 ms 以上のぶれがある。ぶれの主因は `MegaGeometry`（1.93〜2.45 ms。VSM と無関係）。VSM が足す GPU 時間そのもの（`VirtualShadowMapPass` − キューブ側の 0.034 + `LightingPass` の差）は 1.78 + 0.13 = 1.91 ms と 1.83 − 0.03 + 0.135 = 1.93 ms で安定。
- 残りの内訳（中央値 ms。静かな run）: `VsmDraw` 1.15（選んだクラスタ 4.26 万個の間接描画 = 1 描画 1 クラスタ。描かれるページを変えない限り減らせない）、`VsmAllocate` 0.23（11 段の dispatch の固定費。走査を 46 スライスに絞る実験でも 0.20 にしか減らない）、`VsmMark` 0.14、`VsmExpand` 0.10、`VsmCullMega` 0.17、照明の差 0.13、区間に入らない分が約 0.4。
- 試して採らなかった案: (1) クラスタの判定の順を LOD 先行に入れ替える → `VsmCullSelect` が 0.104 → 0.167 に悪化（ページの階層のほうが先に落とす割合が高い）。(2) 割り当ての走査を 46 スライスに限る → `VsmAllocate` 0.231 → 0.200（-0.03）で、灯が減る・消える場面の取りこぼしを防ぐ計画が要るのに見合わない。どちらも戻した。
- 検証: ビルド（`verify-VTG9-VSM-POINT-CULL-PERF-final-build.txt`、EXIT=0）。撮影（`-final-stress.txt`・`-final-views.txt`、result=pass）。Debug ビルド（`-13.txt`、EXIT=0）と ctest（`-14.txt`）は VirtualShadowMapVulkanTest・VirtualShadowMapClipmapTest・VirtualShadowMapPointTest・RenderGraphCompileTest が通った。VirtualShadowMapVulkanTest の J・J4・J5・K・R は書き換えなしで通った。`VirtualShadowMapVulkanTest` にケース J6（影を落とすインスタンスのワークグループ数が 1・2・1・2・3・1 で違う場面。選ばれる集合・通った組の数・統計が J1 と同じ）を足した（`-16.txt`）。変異（組の番号を求める割り算を `max(最大 - 1, 1)` にする）で J6 だけが落ち、戻して通過（`-mutation.txt`・`-17.txt`）。`RenderGraphCompileTest` は dispatch の並び（`DDDDDDDDDDDDJDDDJDDJBIIIIE`）・束縛・バリアの位置・区間の内訳・パイプラインの作成数（8 → 10）を新しい流れに合わせた。
- Notes: 実行時のコンパイラ（shaderc）が、`0xFFFFFFFFu / maxGroups` と `%` を含む 1 スレッドのシェーダー（`barrier()` と shared 変数を使う「最後のワークグループが引数を書く」方式も同様）でこの環境の `vkCreateComputePipelines` を失敗させたため、引数のシェーダーは浮動小数の上限判定と定数での割り算だけにした。`GetLastGroupCount` は従来どおり入力の `TotalGroups`（選択が実際に出すのは通った組ぶん）。GPU のぶれがある間は、フレーム GPU の差ではなく VSM が足す区間の合計（上の 1.91〜1.93 ms）で見るのがよい。
- Next: 人の判断待ち（`blocked/VTG9-VSM-POINT-CULL-PERF.md`）。

## 段9 VTG9-VSM-POINT-DEFAULT-ON（2026-10-08）

- Done: `BootConfig::DefaultPointShadowMethod`（構造体の既定はキューブ）を足し、`ApplicationProcessor` が `--point-shadow-method` の既定をこの値にした。`GameBoot.cpp` が VSM を設定する（`--point-shadow-method=cube` でキューブへ戻せる）。`SceneView`・`RenderWorld` の構造体の既定はキューブのままなので、検証アプリ（golden）は引数を変えずにキューブ（と CSM）。`Scripts/CaptureStartupScene.ps1` の `-PointShadowMethod` の既定を Vsm にした。
- 検証: ビルド（`verify-VTG9-VSM-POINT-DEFAULT-ON-1.txt`、EXIT=0）。ctest（`-2.txt`）は RenderGraphCompileTest・VirtualShadowMapVulkanTest・VirtualShadowMapClipmapTest・VirtualShadowMapPointTest・golden 4 本（Indoor・Outdoor・各 GBufferFallback）が 8/8 通過。基準画像・閾値は動かしていない。撮影（`-3.txt`・`-4.txt`、どちらも result=pass、`failures` 空）。Game のログで `point_shadow_method=1`（引数なしの既定ではなく撮影が明示した値だが、既定の経路は ps1 の既定値と GameBoot の設定で同じ Vsm になる）を確認。
- 測定（Debug・検証レイヤー付き・`-Deterministic -Night -ShadowProbe -SphereSpin Off`。`.harness/runs/startup-capture/VTG9-VSM-POINT-DEFAULT-ON-validation{,-stress}/metrics.json`）:

| run | `vulkan_validation` error / warning | 点光源 `SHADOW_PROBE_AGREE` ratio | mean_texel_mm（キューブ / VSM） | 溢れ |
|---|---|---|---|---|
| 既定の視点 | 0 / 0 | 0.999961 | 19.844 / 7.730 | 0 |
| 近接 | 0 / 0 | 0.994926 | 16.601 / 3.009 | 0 |
| 低角度 | 0 / 0 | 0.999814 | 18.708 / 2.822 | 0 |
| 負荷 300 個（既定の視点） | 0 / 0 | 0.993496 | 19.885 / 8.055 | 0 |

  `fallback_ratio` は 4 run とも 0、`VSM_LIGHTING_STATS` の `fallback_samples`・`point_fallback_samples` も 0。ログの `overflow=` は 1932 行すべて 0（`VSM_PAGES`・`VSM_RASTER`・`VSM_MEGA_CULL`）。VSM のページは 4 run とも点光源の分（requested = allocated = 165〜296）。
- 画: 4 枚の PNG を開いた。天球（夜の暗い空と木のシルエット）・地面（石畳と芝とタイル）・大きな球・岩・小屋・見本の小さな球の帯・発光の球（電球）が欠けなく見える。電球の影（見本の球の帯の長い影、大きな球の接地の影、岩の影、近接の球が石畳に落とす影）は欠け・ずれ・面やページの継ぎ目なく連続している。負荷の既定は岩が増え、岩の間にも影が落ちる。キューブの PNG との画素の差は取っていない。
- Notes: 影の測定の ratio は止まった物の前提なので球の自転を止めて測った（自転する球の細かい影は VSM だけが描く）。深度の比較の余裕・法線のずらしの係数（VTG9-VSM-POINT-SAMPLE の Notes）は、この撮影で浮き・アクネが見えなかったので変えていない。`LightingPassLightPacking` は実キューブが公開されたフレームだけ影の番号を詰める。VSM の構成でもキューブはまだ描いている（GPU-TIME の `ShadowMapPass` 0.08〜0.12 ms）ので、この項目では直していない。GPU 時間は Debug なので見ていない。`VTG9-VSM-POINT-GPU-TIME`・`-CACHE`・`-CULL-PERF` は blocked のまま（人の判断待ち。負荷モードの GPU 時間の基準は `blocked/VTG9-VSM-POINT-CULL-PERF.md`）。
- Next: 人の判断待ちの 3 件（`blocked/`）。それ以外の未完は TASKS.md の次の `todo`。

## 段9 VTG9-STRESS-SHADOW-SKIPS（2026-10-08）

- 結果: 負荷モード 300 個（RelWithDebInfo・昼の既定の視点・夜の既定の視点）で、影の描画の省略の警告を 0 にした。
- 原因と実装: CSM の `DynamicUniformAllocator` は 1024 スロット固定で、半透明・ボリュームの描画 + MegaGeometry の投影物 306 個 × 4 カスケードを超えると `Out of slots (1024/1024)` で省いていた。点光源のキューブは MegaGeometry の描画を 1 面 8 個（`PointShadowMaxMegaDrawsPerFace`）で打ち切っていた。`DynamicUniformAllocator::SetGrowthLimit` を足し、事前確保を使い切ると上限までスロットを増やす（足したスロットは Reset のあとも残る。バッファが作れなければ作れた所で頭打ち）。CSM は上限 4096 × 4 カスケード、点光源は 32768。事前確保の数は変えていない。キューブの 1 面あたりの上限は外し、投影物の数と上限のスロット数で決まる。増やさない既定の挙動（GBufferPass など他の利用者）は変わらない。
- 省略の警告の数（Game ログ。昼 `VTG9-STRESS-SHADOW-SKIPS-day-base` / 夜 `-night-base` が変更前、`-day` / `-night` が変更後）:

| | `Out of slots` | ShadowMapPass の WARN（キューブの 1 面上限・CSM・点光源の UBO 不足） |
|---|---|---|
| 昼 変更前 | 343 | 1375（343 + 1032） |
| 夜 変更前 | 0 | 1032 |
| 昼 変更後 | 0 | 0 |
| 夜 変更後 | 0 | 0 |

  変更後の昼の `csm_mega_draws=148,306,306,306`（306 個すべてを 3 カスケードで描く）、夜の `point_mega_levels` は 1 面で 12・13・37 個（従来は 8 個で打ち切り）。
- フレーム GPU の中央値（`-GpuTimingFrames 300`、240 フレーム。同じ GPU 状態の連続した run）: 昼 10.366 → 10.626 ms（+0.26）、夜 6.128 → 6.203 ms（+0.08）。止め条件の 3 ms 未満。`failures` は 4 つの `metrics.json` で空。
- 画: 夜の既定の PNG を開いた。小屋・岩・大きな球・石畳・電球が見え、岩と球の影が欠けなく連続している。
- 検証: Debug ビルド（`verify-VTG9-STRESS-SHADOW-SKIPS-1.txt`、EXIT=0）、ctest RenderGraphCompileTest（`-2.txt`、通過）、RelWithDebInfo ビルド（`-3.txt`）、撮影（`-4.txt`・`-5.txt`、result=pass）。`RenderGraphCompileTest` に `TestDynamicUniformAllocatorGrowsToCoverShadowCasters` を足した（1024 で頭打ちの既定・4 カスケード × (300 + 64) が別スロットで取れる・Reset 後に作り足さない・上限で失敗・増やす途中のバッファ作成失敗で頭打ち）。
- Notes: 変更前の測定は、同じ作業ツリーで該当の 4 ファイルを stash して RelWithDebInfo を作り直して取った（変更後の撮影が先。比較は連続して取った 2 組）。
- Next: TASKS.md の次の `todo`。

## 段9 VTG9-STRESS-ALL（2026-10-08）

- 結果: `--vram-budget-mb=6500` でテクスチャ・ジオメトリ・MegaGeometry の 3 つの負荷を同時に有効にした全体の負荷モードを、昼（太陽45°）と夜（点光源の VSM）の既定の視点で撮った。予算の内側・VSM の溢れ 0・影の描画の省略 0・穴なし。止め条件には当たっていない。
- 同時に有効にするための変更（Game の負荷モードだけ。Core は触っていない）: 変更前は (1) `--stress-geometry` が `--stress-mega-instances` の個数を上書きして、地面の近くへ複製する負荷が効かない、(2) ジオメトリの格子（z 42〜210）がテクスチャの板の格子（z 41〜95）と重なり、板が物で隠れる、(3) カメラの軸がテクスチャ側（z=68）だけで、ジオメトリの手前 6 行ほどしか映らない、(4) 夜は電球から遠い格子だけが映り、点光源の VSM が 0 ページだった。変更後は、`--stress-geometry` と併せた `--stress-mega-instances=<N>` を別の個数（`m_StressGroundMegaInstanceCount`）として地面の近く（x±26・z -5〜-27。複製元はスキャン資産 3 種だけ。`StressMegaInstanceSource::bScanProp`）へ複製する。テクスチャの負荷と併せるときはジオメトリの格子を z=105 から始め、カメラの軸を z=60（地面と板の間）へ置く。`CaptureStartupScene.ps1` は併用のとき default `180,20,160`・low `180,-4,110`・top `180,75,300`（-Z 側から +Z の向きに、地面・板・物を遠くから見る）にする。引数なしの起動画面・どちらか片方の負荷モードの配置は変えていない（複製を作る処理は関数に切り出しただけで、スキャン資産だけの元の扱いも同じ）。
- 予算とプール（Game ログ。`VTG9-STRESS-ALL-day`・`-night`。cap 6500 MB）:

| | 昼 | 夜 |
|---|---|---|
| `VRAM_BUDGET` heap_usage（最大 / cap） | 1899 / 6500 MB | 1898 / 6500 MB |
| VT（使用 最大 / 目標） | 8 / 3690〜4806 MB | 8 / 3690〜4806 MB |
| ジオメトリ（使用 最大 / 目標） | 29 / 1230〜1602 MB | 34 / 1230〜1602 MB |
| VSM のプール（`shadow_map_pool_mb`） | 427 MB | 427 MB |
| 追い出し（VT のタイル・ジオメトリのページ） | 0・0 | 0・0 |
| `VRAM_POOLS` の使用 > 目標（全 6 行） | 0 行 | 0 行 |
| VSM の溢れ（`VSM_PAGES` 217・173 行 / `VSM_RASTER` 45・42 行 / `VSM_MEGA_CULL` 6・6 行） | 最大 0 | 最大 0 |
| `Out of slots`・影の描画の省略の警告 | 0 | 0 |
| VSM の要求ページ（点光源） | 最大 31（14） | 最大 14（14） |

  `metrics.json` の `failures` は 2 つとも空。ログの WARN は 4 件で、どちらも影と無関係の既知（`ResourceCache is null`、Slang SDK が無いための `neural_material_decode.slang`）。置いた数は `STRESS_TEXTURES materials=24 of 24`・`STRESS_GEOMETRY_PLACED count=300 sources=6`・`STRESS_MEGA_INSTANCES_PLACED count=300 sources=3`。
- フレーム GPU（RelWithDebInfo・`-GpuTimingFrames 300`・240 フレーム・既定の視点）:

| | 中央値 ms | p95 ms | 内訳の上位（中央値 ms） |
|---|---|---|---|
| 昼 | 4.699 | 9.138 | VisibilityRasterPass 1.50（MegaGeometry）・ShadowMapPass 1.18・VirtualShadowMapPass 0.65・LightingPass 0.31 |
| 夜 | 3.462 | 7.719 | VisibilityRasterPass 1.51・VirtualShadowMapPass 0.65・LightingPass 0.25 |

- 画: 昼・夜の PNG を開いた。昼は、手前の地面に複製した岩の群れと小屋、その奥にテクスチャの板（24 枚）、さらに奥にジオメトリの格子（小屋・岩・球 300 個）が並び、穴・欠け・板の抜けは無い。夜は電球が手前の岩の群れと地面の板を照らし、岩の間の影に欠けは見えない。板・ジオメトリの格子は電球の光の範囲の外で暗い。板は遠景（板 1 枚が横 40 px ほど）なので、解像度の判定は次の近景で行った。
- テクスチャの近景（同じ 6500 MB・3 負荷併用・昼。`VTG9-STRESS-ALL-day-plates`。`CaptureStartupScene.ps1` に併用用の視点 `plates`（180,12,40）・`plates-near`（180,35,22）を足した）: `plates-near-sun45.png` を開いた。手前の行の板 1 枚が画面の幅の 4 割ほどを占め、石畳・砂利・板目・ひび割れた土の細部が鮮明で、ブロック状の粗いミップ・タイルの抜け・黒や白の欠けは無い。奥の行へ向かうミップの切り替えも滑らか。`plates-sun45.png`（遠め）も同様。ログ（2 視点とも）は `VRAM_POOLS` の vt_used 最大 7・9 MB ≤ vt_target 3692〜4806 MB、geometry_used 最大 39・43 MB ≤ geometry_target 1230〜1602 MB、追い出し 0、`heap_usage` 最大 1896 MB ≤ cap 6500 MB、VSM の overflow は 342・354 行すべて 0、`Out of slots` 0、`metrics.json` の `failures` は空。GPU は `plates` が中央値 6.01 ms・p95 11.17 ms、`plates-near` が中央値 7.00 ms・p95 11.85 ms（近景は板が大きく映るぶん既定の視点より重い）。夜は板が電球の光の外で暗く解像度を判定できないので近景は昼だけ撮った。
- Notes: (1) 8GB 級の上限（6500 MB）では VT・ジオメトリの使用量が目標よりずっと小さく（8 / 29〜34 MB）、追い出しは起きなかった。この視点で要るタイル・ページが少ないため。予算を縛るのは段2・段5の絞った上限（`VTG2-STRESS-TEXTURES`・段5）で確かめ済み。(2) `heap_usage` 約 1.9 GB のうち `non_pool_mb` が約 1.15 GB、VSM のプールが 427 MB。(3) 夜のジオメトリの格子は光源の外なので、点光源の影の負荷は地面の近くの複製 300 個が受け持つ（`point_requested` 14）。(4) 昼の `VSM_PAGES` の要求は最大 31（うち点光源 14）。
- 検証: ビルド（`verify-VTG9-STRESS-ALL-5.txt`、EXIT=0）。昼・夜の撮影（`verify-VTG9-STRESS-ALL-6.txt`・`-7.txt`、result=pass、EXIT=0）。変更前の昼の挙動（併用がジオメトリの手前の行だけを映し、複製が効かない）は `try0-day.txt`・`VTG9-STRESS-ALL-day-try0/` に残した。 近景の撮影は `verify-VTG9-STRESS-ALL-9.txt`（`plates`・`plates-near`、result=pass、EXIT=0）。`-8.txt` は `plates` だけを撮った最初の回。
- Next: TASKS.md の次の `todo`。

## 段9 VTG9-VSM-POINT-CACHE 解放の統計の修正（2026-10-08）

- 評価者の指摘（先頭の灯を除いて残る灯を詰めると、移し替えで捨てる旧領域の割り当て済みページが `released`・`point_released` に数えられない）を直した。`vsm_allocate.comp` に段階 11（`STAGE_REMAP_COUNT`）を足し、点光源のページの表を移し替えるフレームだけ、移し替えの直前に 1 回 dispatch する。どの灯にも引き継がれない旧領域（`Source` に現れない番号）の割り当て済みの欄を `STAT_RELEASED`・`STAT_POINT_RELEASED` へ数える（欄は書かない。物理ページは空きの一覧を作り直す段階で空きへ戻る）。`VirtualShadowMapPages::Record` は捨てる領域の集合を作って段階 11 に渡し、移し替え（`RemapPointPageTable`）は統計を零にした後・段階 11 の後へ移した。
- 検証: ビルド（`verify-VTG9-VSM-POINT-CACHE-4.txt`、EXIT=0）。ctest（`-5.txt`）は VirtualShadowMapVulkanTest・RenderGraphCompileTest が通った。`VirtualShadowMapVulkanTest` に T7（先頭の灯を除く）を足した: 灯 0・灯 1 の両方のページが揃った 1 フレームのあと、灯 1 だけを番号 0 へ詰める。無効なスライス 0・残る灯の 7 ページは番号 0 へ移って描き直し 0（`StatPointRendered` 0）・空きへ戻した数が `StatPointReleased` と `StatReleased` のどちらも消えた灯の 15 ページと一致・毎フレーム描き直した結果と全 texel で一致（`-3-verbose.txt`）。変異: 段階 11 の dispatch を外すと T7 が落ちた（空きへ戻した数 0、期待 15。`-mutation-remapcount.txt`）→ 戻して通過。
- Notes: 費用は並びが変わって移し替えるフレームだけ（捨てる領域がある場合に、点光源の領域の欄を 1 回走査する）。起動画面・Game の実行では確かめていない（既定は `--point-shadow-method=cube`）。
- Next: TASKS.md の次の `todo`。

## 段9 VTG9-VSM-POINT-CACHE 解放の統計の二重計上の修正（2026-10-08）

- 評価者の指摘（3 灯から「入れ替え＋末尾の灯の削除」をすると、移し替えのあとも残る末尾の領域が、新しい集計段階と Age の解放で二重に `released`・`point_released` へ数えられる）を直した。`VirtualShadowMapPages::Record` の捨てる領域の集合は、`Source` を今フレームの灯の数までしか調べなかったため、今フレームの灯の数より後ろの番号に残る領域（`Source[block] == block`）を捨てるものと誤っていた。確認範囲を `min(max(前フレームの灯の数, 今フレームの灯の数), PointShadowMaxLights)` へ広げ、書き戻しの対象のどのブロックの元にもならない領域だけを捨てる領域にした。残る末尾の領域は Age の解放だけが数える。
- 検証: ビルド（`verify-VTG9-VSM-POINT-CACHE-6.txt`、EXIT=0）。ctest（`-9.txt`）は VirtualShadowMapVulkanTest・RenderGraphCompileTest が通った（`-7.txt` も同じ結果）。`VirtualShadowMapVulkanTest` に T8 を足した: 3 灯（識別子 101・102・103。3 灯目は Range 5 m）を 2 フレーム続けて揃えたあと、`[101,102,103] → [102,101]` にする。無効なスライスは末尾の灯の 36、残る 2 灯のページは入れ替わった番号へ移って描き直し 0、末尾の灯のページ（7）は空きへ戻り、`StatPointReleased` と `StatReleased` のどちらも 7 と一致、毎フレーム描き直した結果と全 texel で一致（`-8-verbose.txt`）。変異: 確認範囲を今フレームの灯の数までに戻すと T8 が落ちた（空きへ戻した数 14、期待 7。`-mutation-range.txt`）→ 戻して通過。
- Notes: 費用の変更はない。T7（先頭の灯を除く）も通過のまま（捨てる 15 ページを数える）。
- Next: TASKS.md の次の `todo`。

## 段9 VTG9-VSM-POINT-DRAW-PERF（2026-10-08、停止）

- 結果: 2 対のどちらも 2 ms 未満に届かず（+2.14・+2.60 ms）、止め条件に当たったので止める。ここまでの縮みは保存コミット `cb3c6ae8` に入っている（`VirtualShadowMapPass` 1.82 → 1.26 ms）。判断は `blocked/VTG9-VSM-POINT-DRAW-PERF.md`。
- 実装（選ばれるクラスタ・LOD・描かれるページ・texel の深度は変えていない）:
  1. `vsm_expand.comp`: 覆うページが 2〜1024 のスライスで、塊の三角形を描画と同じ変換でページの座標へ写し、どの三角形も触れないページにはインスタンスを作らない（触れるページの印は共有メモリ。近い平面をまたぐ三角形・投影が決まらない頂点・1 三角形が 64 ページを超えて覆う場合は、そのスライスは従来どおり範囲の全ページ）。インスタンス 約 11.4 万 → 4.7 万。
  2. `vsm_allocate.comp` 段階 1（無効化）: 1 組（矩形か球、スライス）を 16 本のスレッドが行ごとに分け合う（負荷モードは電球の範囲の大きな球が毎フレーム無効にし、1 本のスレッドが千ページ近くを順に処理していた）。`VsmAllocate` 0.232 → 0.087 ms。
- 切り分け（`VsmDraw`。負荷 300 個・夜の既定の視点・RelWithDebInfo・`-GpuTimingFrames 300`。一時的な変異を入れて測り、戻した）:

| run | 変異 | `VsmDraw` ms | 読み取り |
|---|---|---|---|
| `idle-base` | なし（実装前） | 1.158 | 基準 |
| `idle-m1` | 頂点シェーダーが位置を読まず即 return | 1.047 | 頂点の読み取り・計算は 0.11 ms 程度 |
| `idle-m2` | 断片が `atomicMin` を出さない | 1.158 | 断片の書き込みは時間に効かない |
| `idle-m3` | 頂点の処理は全部して最後にビューポートの外へ | 1.158 | 変換・出力は効かない |
| `mut-m6` | 描画ごとの三角形を 1 個にする | 0.227 | 時間は三角形の数に比例（1 インスタンスの固定費は小さい） |
| `mut-m7` | インデックスを `index % 3`（頂点の再利用を最大に） | 0.823 | 頂点の取得の局所性は 0.33 ms 分 |

  触れる割合（`mut-count`・`mut-count2`。展開で実際に測った）: 描くインスタンス 約 11.4 万・三角形 約 1163 万のうち、ページに触れる三角形は 390 万（33.5%）、1 つでも触れる三角形があるインスタンスは 4.59 万（40%）。→ 支配するのは頂点・断片の処理でなく、触れないページへ送る三角形の数。実装 1 でインスタンスを 4.7 万に絞って `VsmDraw` 0.51 ms になった。
- 実装後の `VirtualShadowMapPass` の内訳（`vsm-stress-1`。ms）:

| 段 | 実装前（`idle-base`） | 実装後 |
|---|---|---|
| VirtualShadowMapPass 合計 | 1.842 | 1.258 |
| VsmDraw | 1.158 | 0.509 |
| VsmCullMega | 0.173 | 0.173 |
| VsmMark | 0.142 | 0.142 |
| VsmExpand | 0.101 | 0.138 |
| VsmCullSelect | 0.105 | 0.106 |
| VsmAllocate | 0.232 | 0.087 |
| 残り（CullDirty・CullChunks・Clear・CullPairs） | 0.07 | 0.07 |

- 対の差（`gpu_frame_ms_median`、VSM − キューブ。`failures` は全 run で空）:

| 対 | キューブ ms | VSM ms | 差 ms |
|---|---|---|---|
| 負荷 1（`cube-stress-1`・`vsm-stress-1`） | 5.183 | 7.325 | +2.142 |
| 負荷 2（`cube-stress-2`・`vsm-stress-2`） | 5.193 | 7.792 | +2.599 |
| 追加 1（`pairA1`、前回反復） | 4.949 | 6.991 | +2.042 |
| 追加 2（`pairA2`、前回反復） | 4.714 | 6.082 | +1.368 |
| 既定の視点（`cube`・`vsm`） | 2.286 | 3.113 | +0.827 |
| 近接 | 2.226 | 3.895 | +1.669 |
| 低角度 | 2.204 | 3.076 | +0.872 |

  負荷の 4 対の平均は +2.04 ms。キューブ側の run ごとのぶれ（`MegaGeometry` 2.26〜2.58 ms、`LightingPass` 0.66〜0.88 ms）が 0.5 ms 近くあり、2 ms の境では合否が run で入れ替わる。差の内訳は `VirtualShadowMapPass` 1.26 ms（キューブ側の同名の段 0.035 ms を引いて 1.22）+ `LightingPass` の +0.30 ms（0.682 → 0.982、VSM の 16 点の読み取り）+ ぶれ。
- overflow: VSM の 3 run（`vsm-stress-1`・`-2`・`vsm`）の `VSM_PAGES`（304・304・302 行）・`VSM_RASTER`（338・343・330 行）・`VSM_MEGA_CULL`（各 6 行）はすべて 0。
- 検証: RelWithDebInfo ビルド（`verify-VTG9-VSM-POINT-DRAW-PERF-1.txt`、EXIT=0）。撮影は `-2`〜`-5`（負荷の 2 対）・`-6`・`-7`（通常の 3 視点）で result=pass。Debug ビルド（`-10.txt`、EXIT=0）、ctest（`-11.txt`）は VirtualShadowMapVulkanTest・VirtualShadowMapClipmapTest・VirtualShadowMapPointTest・RenderGraphCompileTest が 4/4 通過。`VirtualShadowMapVulkanTest` の J・J4・J5・J6・K・R は書き換えず通った。テストの変更: 展開の参照を「三角形が触れるページだけ」に改めた。変異（透視の触れる判定を壊す `mutation-perspective`、無効化の分割の 2 つの走査を壊す `mutation-split`・`mutation-split2`）で VirtualShadowMapVulkanTest が落ちることを確かめ、戻して通過。
- Notes: 試して外した案: 断片で手前でなければ `atomicMin` を出さない（`idle-m2` で断片の書き込みが時間に効かないと分かったので作らなかった）。固定費の dispatch の融合は 0.1〜0.2 ms 見込みで、2 ms の境を安定して越えないので未着手。`VTG9-VSM-POINT-CULL-PERF`・`VTG9-VSM-POINT-GPU-TIME` は blocked のまま。
- Next: 人の判断待ち（`blocked/VTG9-VSM-POINT-DRAW-PERF.md`）。それ以外の未完は TASKS.md の次の `todo`。

## 段9 親の確認: 1 回目の VTG9-VSM-POINT-DRAW-PERF の測定の汚れ（2026-10-08）

- GPU の他の負荷: Game を動かしていない状態で `nvidia-smi` の利用率が 31〜40%（P0、2.0〜2.5 GHz）。常駐のアプリ（プロセスごとの内訳は権限で見えない）。システムのイベントログでは 17:24 にセッションが 6 → 7、17:54 に 7 → 9 へ移った。
- フレームの区間の外の時間（`gpu_frame_ms_median` − 一番上の区間の中央値の合計）:

| 時間帯 | キューブ | VSM | 負荷のキューブの `MegaGeometry` |
|---|---|---|---|
| 17:27〜17:29（VTG9-VSM-POINT-GPU-TIME） | 0.02〜0.03 ms | 0.02〜0.08 ms | 1.93 ms |
| 17:47〜17:51（VTG9-VSM-POINT-CULL-PERF の後） | 0.03 ms | 0.38〜0.39 ms | 1.93 ms |
| 18:00 以降（CULL-PERF の対・DRAW-PERF の全 run） | 0.07〜2.32 ms | 0.30〜1.88 ms | 2.26〜7.00 ms |

- 判断: 18 時以降の対の差（+1.37〜+2.60 ms）は他の負荷のぶれを含むので、2 ms の判定に使わない。VTG9-VSM-POINT-DRAW-PERF を todo に戻し、撮影の直前の GPU の利用率を記録して、静かなときの対で判定する。17:47〜17:51 の VSM の区間の外の約 0.35 ms は静かな run でも出ているので、DRAW-PERF で原因を切り分ける。
- 判断の変更（ユーザーの判断）: キューブとの差の 2 ms は計画書・段の受入れに無い止め条件の目安だったので、合否から外して表で示す（フレーム全体の差・パスの合計の差・撮影の直前の GPU の利用率を並べる）。異常の見張りは 16.6 ms（60fps の 1 フレーム）。VTG9-VSM-POINT-GPU-TIME・-CULL-PERF・-DRAW-PERF を todo に戻し、今の文面で測り直す。上の「静かなときの対で判定する」はこれに置き換わる。

## 段9 VTG9-VSM-POINT-GPU-TIME（2026-10-08、完了。評価の差し戻しで撮影直前の利用率を視点ごとに測り直した）

- 条件: RelWithDebInfo・`-Night -GpuTimingFrames 300`・`-PointShadowMethod Cube|Vsm`。通常の 3 視点は `-ViewNames default|near|low` で 1 視点ずつ別の起動にし、負荷は `-ViewNames default` と `--stress-mega-instances=300`。各撮影の直前（Game を動かしていない状態）に `nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits -lms 500` の先頭 10 回を取って最大を記録した（`gpu-util.txt`）。証拠: `.harness/runs/20261008-203356/verify-VTG9-VSM-POINT-GPU-TIME-1.txt`（ビルド）・`-2`〜`-9`（撮影。EXIT=0・result=pass）。撮影は `.harness/runs/startup-capture/VTG9-VSM-POINT-GPU-TIME-{cube,vsm}-{default,near,low,stress}/`（`metrics.json` の `failures` は 8 run とも空）。
- 前回の 1 回の起動で 3 視点を続けて撮った表は、近接・低角度の利用率が撮影直前の値ではなかったので、この表に置き換えた（`-cube`・`-vsm` の旧ディレクトリは古い測定）。
- 区間の数え方: パスの合計 = `pass_median_ms` のうち入れ子の区間（`Vsm*`・`VisRaster*`・`MegaGeometryCull*`・`MegaGeometryDraw*`・`MegaGeometry`。`MegaGeometry` は `VisibilityRasterPass` と同じ時間の入れ子）を除いた一番上の区間の中央値の合計。「区間の外」はフレーム − パスの合計。

撮影直前の GPU の利用率（Game を動かしていない状態の 10 回の最大、%。常駐のアプリ由来で 1〜35% の間を揺れる）とフレーム GPU・パスの合計（ms、中央値）:

| 視点 | 方式 | 利用率 最大 % | フレーム | パスの合計 | 区間の外 | p95 | 最大 |
|---|---|---|---|---|---|---|---|
| 既定 | Cube | 35 | 1.828 | 1.806 | 0.022 | 2.351 | 2.666 |
| 既定 | Vsm | 23 | 2.297 | 2.276 | 0.021 | 2.928 | 4.374 |
| 近接 | Cube | 33 | 2.056 | 1.875 | 0.181 | 2.842 | 6.649 |
| 近接 | Vsm | 28 | 2.441 | 2.252 | 0.189 | 2.873 | 3.504 |
| 低角度 | Cube | 31 | 2.319 | 1.937 | 0.382 | 2.699 | 3.939 |
| 低角度 | Vsm | 32 | 2.538 | 2.233 | 0.305 | 3.040 | 3.231 |
| 負荷（既定） | Cube | 23 | 4.261 | 4.037 | 0.224 | 5.206 | 8.427 |
| 負荷（既定） | Vsm | 25 | 5.414 | 5.056 | 0.358 | 6.013 | 6.791 |

キューブとの差（VSM − Cube、ms）:

| 視点 | フレーム全体の差 | パスの合計の差 |
|---|---|---|
| 既定 | +0.469 | +0.470 |
| 近接 | +0.385 | +0.377 |
| 低角度 | +0.219 | +0.296 |
| 負荷（既定） | +1.153 | +1.019 |

区間の中央値（ms）:

| 視点・方式 | ShadowMapPass | VirtualShadowMapPass | VsmDraw | VsmCullMega | VsmMark | VsmExpand | VsmAllocate | VsmCullSelect | VsmCullDirty | VsmClear | 他の Vsm（CullPairs・CullChunks） | LightingPass |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 既定 Cube | 0.078 | 0.034 | - | - | 0.003 | - | 0.026 | - | - | 0.002 | - | 0.528 |
| 既定 Vsm | 0.078 | 0.374 | 0.048 | 0.061 | 0.132 | 0.026 | 0.085 | 0.012 | 0.032 | 0.009 | 0.007・0.005 | 0.663 |
| 近接 Cube | 0.077 | 0.034 | - | - | 0.003 | - | 0.026 | - | - | 0.002 | - | 0.537 |
| 近接 Vsm | 0.077 | 0.428 | 0.075 | 0.061 | 0.155 | 0.028 | 0.087 | 0.010 | 0.032 | 0.010 | 0.008・0.005 | 0.693 |
| 低角度 Cube | 0.078 | 0.034 | - | - | 0.003 | - | 0.026 | - | - | 0.002 | - | 0.449 |
| 低角度 Vsm | 0.077 | 0.382 | 0.058 | 0.063 | 0.121 | 0.027 | 0.087 | 0.014 | 0.032 | 0.013 | 0.008・0.005 | 0.590 |
| 負荷 Cube | 0.168 | 0.034 | - | - | 0.003 | - | 0.026 | - | - | 0.002 | - | 0.660 |
| 負荷 Vsm | 0.168 | 1.061 | 0.507 | 0.168 | 0.141 | 0.138 | 0.084 | 0.103 | 0.032 | 0.010 | 0.009・0.019 | 0.787 |

（Cube でも `VirtualShadowMapPass` が 0.034 ms 出るのは、点光源の VSM が無い夜でもパスが `VsmMark`・`VsmAllocate`・`VsmClear` を記録するため。`MegaGeometry` は負荷でキューブ 2.233・VSM 2.117、通常は 0.22〜0.23 で、方式の差ではない。）

統計（VSM の run。点光源の分。最大値。`VSM_POINT` は 4 run とも lights=1 slices=36 face_res=4096 mips=6）:

| 視点 | VSM_PAGES point_requested = point_allocated（overflow） | VSM_RASTER chunks・instances（overflow） | VSM_MEGA_CULL point_instances・point_clusters（overflow） | VSM_CACHE point_cached・point_rendered |
|---|---|---|---|---|
| 既定 | 181（0） | 2886・4073（0） | 26・3206（0） | 25・153 |
| 近接 | 202（0） | 2511・4408（0） | 15・3133（0） | 0・166 |
| 低角度 | 298（0） | 3088・4582（0） | 28・3541（0） | 2・293 |
| 負荷（既定） | 183（0） | 46615・55108（0） | 167・42561（0） | 25・155 |

- overflow: `VSM_PAGES`（303・1461・675・305 行）・`VSM_RASTER`（330・1820・768・335 行）・`VSM_MEGA_CULL`（6・31・13・6 行）の全行で 0（Game.log を全行走査）。`VSM_PAGES` の要求は全部が点光源の分（夜は太陽が無い）。
- 影の目視: VSM の PNG 4 枚（既定・近接・低角度・負荷の既定）を開いた。小さな球の群れの長い影、大きな球の接地の影、岩（見本の球と岩）の影、近接の大きな球と岩が石畳に落とす影が、欠け・ずれ・面の継ぎ目・ページの継ぎ目なく連続して見える。小屋は電球の光の範囲の外で暗いまま。壊れて見えないので、キューブの PNG との画素の差は調べていない。
- 判定: 16.6 ms の見張りには最大の負荷でも 5.4 ms（p95 6.0 ms）で当たらない。止め条件に当たらないので完了。
- Notes: 近接・低角度の Cube は「区間の外」が 0.18・0.38 ms と既定の Cube（0.02 ms）より大きく、フレーム全体の差が +0.22〜+0.39 ms とパスの合計の差（+0.30〜+0.38 ms）より小さく出る。利用率が 31〜33% の常駐のアプリの影響を含むので、方式の差はパスの合計の差のほうが安定している。キューブとの差の主な内訳は `VirtualShadowMapPass`（通常 +0.34〜+0.39、負荷 +1.03）と `LightingPass`（+0.14〜+0.16、負荷 +0.13。VSM の 16 点の読み取り）。負荷の重い区間は `VsmDraw` 0.51・`VsmCullMega` 0.17・`VsmMark` 0.14・`VsmExpand` 0.14・`VsmCullSelect` 0.10。`VTG9-VSM-POINT-CULL-PERF`・`VTG9-VSM-POINT-DRAW-PERF` は todo のまま（差が合否から外れたので、必要かどうかは人が決める）。撮影の PowerShell は `powershell`（5.1）で動かす（`pwsh` では `Add-Type` の `System.Drawing` が解決できず撮影が失敗する）。
- Next: TASKS.md の次の `todo`。

## 段9 VTG9-VSM-POINT-CULL-PERF の測り直し（2026-10-08）

- 結果: 完了。実装は fcaee6f0（通る（インスタンス、スライス）の組だけを間接 dispatch で選ぶ）。キューブとの差を合否から外した新しい文面（`VsmCullMega` の縮小とフレーム GPU 16.6 ms 未満）で測り直して満たした。
- 測定（`.harness/runs/startup-capture/VTG9-VSM-POINT-CULL-PERF-vsm-stress/`、`verify-VTG9-VSM-POINT-CULL-PERF-2.txt`。RelWithDebInfo・夜・負荷 300 個・既定の視点・240 フレーム集計）: フレーム GPU の中央値 6.627 ms（p95 8.924・最大 11.775。測定前 17.30）。`VsmCullMega` 0.174 ms（測定前 11.72。Select 0.107・Dirty 0.033・Chunks 0.019・Pairs 0.010）、`VirtualShadowMapPass` 1.27、`VsmDraw` 0.506。`VSM_PAGES`・`VSM_RASTER`・`VSM_MEGA_CULL` の overflow は全行 0、`failures` は空。
- 検証: RelWithDebInfo ビルド（`-1.txt`、EXIT=0）・撮影（`-2.txt`、result=pass）・Debug ビルド（`-3.txt`、EXIT=0）・ctest（`-4.txt`、VirtualShadowMapVulkanTest・VirtualShadowMapClipmapTest・VirtualShadowMapPointTest・RenderGraphCompileTest の 4 件が通過）。
- Notes: `VSM_RASTER` の instances は 933c1fdf（展開で触れるページだけにインスタンスを作る）以降の値で、選ばれるクラスタ数（chunks）はカリング側の変更で変わっていない。
- Next: TASKS.md の次の `todo`。

## 段9 VTG9-VSM-POINT-DRAW-PERF の測り直し（2026-10-08、完了）

- 結果: 完了。実装は 933c1fdf（展開で三角形が触れるページだけにインスタンスを作る・ページの無効化を 16 本のスレッドで分け合う）。キューブとの差を合否から外した今の文面で測り直し、(a) 2 対のどちらでも VSM のフレーム GPU が 16.6 ms を大きく下回り、(b) `VsmDraw` が開始時の 1.158 ms から 0.506・0.507 ms に縮んだ、の 2 点を満たした。
- 条件: RelWithDebInfo・`-Night -GpuTimingFrames 300`・`-PointShadowMethod Cube|Vsm`。負荷は `-ViewNames default --stress-mega-instances=300` の 2 対、通常の 3 視点は 1 回の起動で続けて撮った（`cube`・`vsm`）。撮影の直前（Game を動かしていない状態）の `nvidia-smi` 利用率の 10 回の最大を `gpu-util-DRAW-PERF.txt` に記録した。区間の数え方は VTG9-VSM-POINT-GPU-TIME の節と同じ（パスの合計 = 入れ子を除いた一番上の区間の中央値の合計、区間の外 = フレーム − パスの合計）。
- 証拠: `.harness/runs/20261008-203356/verify-VTG9-VSM-POINT-DRAW-PERF-1.txt`（RelWithDebInfo ビルド、EXIT=0）・`-2`〜`-7`（撮影、result=pass・EXIT=0）・`-8`（Debug ビルド、EXIT=0）・`-9`（ctest 4/4 通過）。撮影は `.harness/runs/startup-capture/VTG9-VSM-POINT-DRAW-PERF-{cube,vsm}-stress-{1,2}`・`-cube`・`-vsm`。`metrics.json` の `failures` は 6 run とも空。

負荷（夜・300 個・既定の視点）の 2 対（ms、中央値）:

| 対 | 方式 | 利用率 最大 % | フレーム | パスの合計 | 区間の外 | p95 | 最大 |
|---|---|---|---|---|---|---|---|
| 1 | Cube | 35 | 5.426 | 4.392 | 1.034 | 8.952 | 10.285 |
| 1 | Vsm | 18 | 6.243 | 5.305 | 0.938 | 8.212 | 10.267 |
| 2 | Cube | 30 | 4.290 | 4.011 | 0.279 | 4.816 | 9.153 |
| 2 | Vsm | 39 | 5.454 | 5.150 | 0.304 | 5.847 | 7.256 |

通常の 3 視点（1 回の起動で続けて撮った）:

| 視点 | 方式 | 利用率 最大 % | フレーム | パスの合計 | 区間の外 | p95 | 最大 |
|---|---|---|---|---|---|---|---|
| 既定 | Cube | 42 | 2.537 | 2.192 | 0.345 | 3.043 | 4.440 |
| 既定 | Vsm | 23 | 2.695 | 2.431 | 0.264 | 3.185 | 4.001 |
| 近接 | Cube | 42 | 2.065 | 1.742 | 0.323 | 2.481 | 2.871 |
| 近接 | Vsm | 23 | 2.761 | 2.279 | 0.482 | 3.358 | 3.807 |
| 低角度 | Cube | 42 | 2.113 | 1.862 | 0.251 | 2.465 | 2.715 |
| 低角度 | Vsm | 23 | 2.708 | 2.347 | 0.361 | 3.218 | 3.561 |

（通常の 3 視点は 1 回の起動なので、利用率は起動前の 1 回の値。）

キューブとの差（VSM − Cube、ms）:

| 場面 | フレーム全体の差 | パスの合計の差 |
|---|---|---|
| 負荷 対 1 | +0.817 | +0.913 |
| 負荷 対 2 | +1.164 | +1.139 |
| 既定 | +0.158 | +0.239 |
| 近接 | +0.696 | +0.537 |
| 低角度 | +0.595 | +0.485 |

区間の中央値（ms）。開始時の基準は 1 回目の反復の `idle-base`（実装前、負荷・既定の視点）:

| run | VirtualShadowMapPass | VsmDraw | VsmCullMega | VsmMark | VsmExpand | VsmAllocate | LightingPass |
|---|---|---|---|---|---|---|---|
| 開始時（`idle-base`） | 1.842 | 1.158 | 0.173 | 0.142 | 0.101 | 0.232 | - |
| 負荷 対 1 Vsm | 1.071 | 0.506 | 0.172 | 0.142 | 0.138 | 0.087 | 0.787 |
| 負荷 対 2 Vsm | 1.063 | 0.507 | 0.169 | 0.142 | 0.137 | 0.084 | 0.789 |
| 既定 Vsm | 0.380 | 0.049 | 0.062 | 0.131 | 0.027 | 0.083 | 0.671 |
| 近接 Vsm | 0.433 | 0.075 | 0.061 | 0.156 | 0.029 | 0.087 | 0.698 |
| 低角度 Vsm | 0.386 | 0.058 | 0.064 | 0.122 | 0.028 | 0.088 | 0.590 |

- overflow: VSM の 3 run（`vsm-stress-1`・`-2`・`vsm`）の `VSM_PAGES`（305・304・1354 行）・`VSM_RASTER`（338・339・1559 行）・`VSM_MEGA_CULL`（各 6・6・27 行）はすべて 0（Game.log を全行走査）。負荷のインスタンスは 47140〜47144（実装前 約 11.4 万）。
- 判定: 16.6 ms の見張りには最大の負荷でも 6.24 ms（p95 8.2 ms）で当たらない。`VsmDraw` は 1.158 → 0.506 ms。選ばれるクラスタ・LOD・描かれるページ・texel の深度は変えていない（`VirtualShadowMapVulkanTest` の J・J4・J5・J6・K・R が書き換えなしで通る。ctest は VirtualShadowMapVulkanTest・VirtualShadowMapClipmapTest・VirtualShadowMapPointTest・RenderGraphCompileTest の 4/4 通過）。
- Notes: 区間の外の時間は、今回の run ではキューブ側も 0.25〜1.03 ms と VSM と同じ程度に出ており（静かな時間帯の 0.02〜0.08 ms と違う）、VSM 固有の約 0.35 ms ではなく、常駐のアプリの利用率（18〜42%）によるぶれ。負荷のキューブ 対 1 は区間の外 1.03 ms・p95 8.95 ms とぶれが大きく、差は対 2 のほうが安定している。方式の差はパスの合計の差のほうが安定して読める。差の主な内訳は `VirtualShadowMapPass`（負荷 +1.03、通常 +0.35〜+0.40）と `LightingPass`（+0.13〜+0.15。VSM の 16 点の読み取り）。起動画面・既定の描画経路は変えていない（既定は `--point-shadow-method=cube`）。
- Next: TASKS.md の次の `todo`。

## 段9 VTG9-ACCEPT（2026-10-08）

- 結果: 段9の受入れを満たす。記録は `Docs/RenderingValidation/VirtualizationAcceptance.md` の「段9（VSM 点光源と全体）」と「段1〜9 の受入れのまとめ」。
- 検査（`.harness/runs/vtg9-accept/`）: Debug ビルド（`a1-build-debug.txt`、EXIT=0）。ctest 9 本（`a2-ctest.txt`。RenderGraphCompileTest・VirtualShadowMapVulkanTest・VirtualShadowMapClipmapTest・VirtualShadowMapPointTest・VideoMemoryBudgetManagerTest・golden 4 本）が 9/9 通過。検証レイヤー付き Debug の夜 3 視点（`a3-validation.txt`、`VTG9-ACCEPT-validation`）は error・warning・vuid 0、溢れ 0。RelWithDebInfo ビルド（`a4`）。撮影は朝・昼・夕（`a5`、`VTG9-ACCEPT`）・夜（`a6`、`VTG9-ACCEPT-night`）・夜の旋回の影の測定（`a7`、`VTG9-ACCEPT-night-orbit`、球の自転を止める）で、`failures` は空。
- 夜の電球の影（キューブ / VSM）: texel 既定 19.593 / 8.502・近接 16.510 / 2.394・低角度 17.048 / 4.339 mm。縁の帯 0.026249 / 0.002016・0.107684 / 0.010192・0.038117 / 0.003978。mean_abs_delta・flip_ratio はキューブ 0、VSM 0.000003 以下（両方 0.001 未満で同等）。一致 0.999964・0.993166・0.999582。
- 画: 12 枚と旋回の 1 枚を開いた。天球・地面・球・岩・小屋・見本の帯・電球が欠けなく、電球の影に継ぎ目・欠けは無い。近接の夜の大きな球の赤い点は段8（キューブ）の同じ視点にもある。
- ログ: 受入れの撮影 18 本の全行で VSM の 3 種の溢れ 0、`VSM_FALLBACK` 0。ERROR は既知の 2 件（Slang SDK が無いための `neural_material_decode.slang`）。
- 段9の開始時に backlog へ退避した 15 件は、main（`3ac00b05`）の今の状態へ戻した（G3-GR12 は main で done）。TASKS.md の main との差は VTG9 の項目だけ。
- Next: 段の区切りの評価、main へマージ。

## 段9 段の区切りの評価の 1 周目と、夜の粒の指摘への対応（2026-10-08）

- 段の区切りの評価（1 周目）の指摘: (1) `vsm_expand.comp` の共有メモリの印への非原子的な書き込み → `addc72dc` で直した。(2) 照明のパスの点光源の VSM のパラメータ・スライスの表・descriptor set を同じフレームの複数の Execute が共有する → `VTG9-FIX-LIGHTING-PER-EXECUTE`。(3) 受入れの表の値と撮り直された出力先の不一致 → `11fa107c` で出典と撮り直した値を書いた。
- ユーザーの指摘: 夜の近接の大きな球の暗い側に橙赤の粒が出るのは許容できない（段8にもあったことは理由にならない）。原因は夜の静的な環境光（夕焼けの HDRI の 0.08 倍）に残った太陽の鏡面反射 → `VTG9-FIX-NIGHT-ENV-SUN`。受入れの記録の「VSM によるものではない」は、この項目の後に直す。
- ランナーが段9の項目だけを拾うよう、別の作業の todo・doing 16 件を、このブランチの上で再び backlog にした（CORE-JSON-SURROGATE・GAME-GR130〜GR137-VFX・G2-GR79-IMPORT-POLICY-CONNECTION・G2-MATERIAL-SELECTION-INTEGRATION・CORE-STRING-REPLACE-TERMINATOR は todo、G2-S6-ASSET-SET・G2-GR82-B4-STATIC-ROOT-FRAME128・G3-GR10・G3-GR13 は doing）。main へマージする前に main の状態へ戻す。

## VTG9-FIX-NIGHT-ENV-SUN（2026-10-08）

- 結果: 夜の近接の大きな球の暗い側の橙赤の粒がなくなった。証拠は `.harness/runs/20261008-221440/verify-VTG9-FIX-NIGHT-ENV-SUN-1〜5.txt`（Debug ビルド・ctest 6/6・検証レイヤー付き Debug の夜 `error_count` 0・`warning_count` 0・`vuid_count` 0・RelWithDebInfo ビルド・夜 3 視点の撮影、すべて EXIT=0）。追加で RTGIDiffuseIndirectVulkanTest・DDGIProbeRadianceVulkanTest・LightingLightBufferTest も通過（`extra-build.txt`・`extra-ctest.txt`）。
- 実装: `StaticEnvironmentMaxRadiance`（HDR の値、0 は上限なしで既定）を `RenderWorld::SetStaticEnvironmentMaxRadiance` → `RenderingCoordinator` → FramePacket の `SceneProxy` → `LightingPass`（`GPULightingParams` の旧 `shadowPadding2`、`ViewRenderContext::PhysicalLighting`）へ、`StaticEnvironmentIntensityScale` と同じ経路で渡す。空が無効で静的 HDR を実際に読むフレーム（倍率と同じ条件、検証用の環境は除く）だけ値を渡し、それ以外は 0。倍率を掛ける前の色の最大の成分が上限を超えたら色相を保って上限まで縮める。
- 効く場所: `lighting.frag`（鏡面の IBL の前計算の値・背景）、`forward_transparent.frag`（半透明の鏡面の IBL。UBO の旧 `padding2`）、`RTGI/DiffuseIndirect.comp`（外れた光線。`RTGIComputeParameters` を 160 → 176 バイト、`environmentParameters.x`）、`DDGI/ProbeRadiance.comp`（外れた光線。`environmentParameters.y`）。経路追跡（`PathTracing/*`）は検証用で静的環境の倍率も使わないので触らない。拡散の IBL の前計算の放射照度は太陽の分を含んだまま（既知の限界。全エネルギーの約 3.7%）。
- Game: `--night` で上限 10（`kNightStaticEnvironmentMaxRadiance`、根拠は定数の隣）。ほかのモードへは `SetStaticEnvironmentMaxRadiance(0)` で持ち越さない。
- 切り分け（タスクの前提の訂正）: 粒の原因は鏡面の IBL ではなく **RTGI の外れた光線が太陽に当たること**だった。鏡面の IBL にだけ上限を入れた最初の実装は near-night.png が修正前と 1 画素も違わなかった（`exp-*` は一時的にシェーダーの出力を絞った実験: 直接光の鏡面だけ・直接光の拡散だけでは粒が出ず、環境光だけで粒が出た。RTGI を切った `VTG9-DOTS-rtgi-off` は粒が無い）。RTGI の外れた光線にも上限を入れて消えた（`VTG9-FIX-NIGHT-ENV-SUN-try2`、最終の `-night`・`-validation`）。
- 粒の数: タスクの指定の測り方（輝度が 9×9 の箱の平均より 10 以上高く R−G>15、x 370〜909・y 100〜599、輝度は床）は修正前 2667 → 修正後 2548（Debug・RelWithDebInfo とも）で、100 未満にならない。この測り方は粒を測れていない: 粒の無い `VTG9-DOTS-rtgi-off` でも 3105 で、レンガの凹凸の縁の橙色を数えている。粒は暗い側（x 370〜699・y 100〜599）で R−G>40 の画素を数えると分けられ、修正前 223・RTGI を切った 0・修正後 0（R−G の最大は 98 → 35）。上の 2548 の残りは RTGI を切った画像と同じレンガの縁で、別の原因ではない。
- 画: 夜の 3 視点（`VTG9-FIX-NIGHT-ENV-SUN-night`）を開いた。近接は球の暗い側に粒が無く、既定・低角度とも天球・地面・球・岩・小屋・電球と電球の影が欠けなく見える。
- golden 4 本は基準画像・閾値を動かさずに通る（検証アプリは上限を使わない）。
- Notes: Edit ツールが混在行末のファイルを正規化して全行を書き換えるため、混在のファイルは行末を保って置換する別の手順で編集した（numstat 2 通りは一致を確認）。受入れの記録（`VirtualizationAcceptance.md`）の「VSM によるものではない」の直しはこの項目の後に行う（段9の受入れの追従）。
- Next: TASKS.md の次の `todo`。

## VTG9-FIX-NIGHT-ENV-SUN 反復 2（2026-10-08）

- 評価者の NEEDS_WORK（指定の測り方で 2548、100 未満に届かない）を受け、stop-when に従って BLOCKED にした。理由と選択肢は `blocked/VTG9-FIX-NIGHT-ENV-SUN.md`。
- 実装は 2e74630c のまま。測り方（レンガの縁を数える）の再定義はユーザー判断なので、基準は変えていない。
- Next: 人が done-when (4) を直して `status:` を `todo` に戻すと再開。それまで TASKS.md の次の `todo` へ。

## VTG9-FIX-LIGHTING-PER-EXECUTE（2026-10-08）

- 結果: 照明のパスが 1 フレームに複数回 Execute されても、Execute ごとに自分の descriptor set と、Execute ごとに書くバッファ（`m_LightDataBuffer`・`m_LightArrayBuffer`・`m_VsmSampleBuffer`・`m_VsmPointSampleBuffer`・`m_VsmSliceBuffer`）の組を使う。証拠は `.harness/runs/20261008-221440/verify-VTG9-FIX-LIGHTING-PER-EXECUTE-1.txt`（Debug ビルド EXIT=0）・`-5.txt`（再ビルドと ctest 6/6 通過: RenderGraphCompileTest・VirtualShadowMapVulkanTest・golden 4 本。基準画像・閾値は動かしていない）。`-3.txt` は既存テストのヘルパー修正後の RenderGraphCompileTest、`-2.txt` は修正前に RenderGraphCompileTest が落ちた記録。
- 前提の確認（stop-when）: タスクの文面の `VulkanCommandList::Begin` は描画の経路では使われない（`RenderingCoordinator` は `BeginRecording` で記録を始める）。前のフレームの提出の完了は `VulkanSwapChain::BeginFrame` が `m_inFlightFences[m_currentFrame]` を待つことで保証され（`MAX_FRAMES_IN_FLIGHT = 1`）、`RenderingCoordinator::RenderFrame` はその後に記録を始める。したがって通し番号が変わった時点で、前のフレームの組を読む GPU の仕事は終わっている。フレームをまたぐ使い方ではないので止めない。`MAX_FRAMES_IN_FLIGHT` を 2 以上にする変更をするときは、組を飛行中のフレームの番号ごとに持つ（`FrameUseRing` の形）必要がある（`LightingPass.h` の組の説明に書いた）。
- 実装: `LightingPass` に `ExecuteResourceSet`（5 つのバッファ・descriptor set・ライト配列の容量）を最大 4 組（`MaxExecuteResourceSets`）持たせた。`ExecuteWithInputs` が GBuffer の入力の確認の直後に `AcquireExecuteResourceSet(context.ResolveRenderFrameSerial())` で組を選ぶ。通し番号が前回と違えば先頭の組から使い直し、同じ間は Execute のたびに次の組へ進む。組は要るときに作る（組 0 は Initialize が作ったもの）。以降の本体のコード（`m_LightDataBuffer` などの参照）は、選んだ組が載った「現在の枠」をそのまま使う。4 つを超えた Execute は `LIGHTING_EXECUTE_SETS_EXCEEDED limit=4` を 1 回だけ出して描かない（`TryEnqueueNativeTransitionPass` だけ記録）。組を作れなかったときは現在の枠を元の組へ戻し、次の Execute でやり直す。1 ビューポートの描画（起動画面）は常に組 0 で、今までと同じ資源・同じ内容。
- テスト: `RenderGraphCompileTest` の `TestLightingExecutesInOneFrameUseSeparateResourceSets`。同じ通し番号で別のカメラの位置の 4 ビューポートを回し、(1) Execute のたびに照明の descriptor set が 1 つずつ増える、(2) 4 組の descriptor set と 5 つのバッファ（束縛 4・5・21・25・26）がすべて別、(3) 各組のバッファの中身がその Execute のカメラの値（`GPUVsmSampleParams` の `cameraPosition`）で、後の Execute の後も変わらない、(4) 5 つ目・6 つ目は描かず（描画数が 1 ビューポートぶん減る）組を増やさず、エラーは 1 回、(5) 通し番号が変わると先頭の組から使い直し、2 番目の組は触られない、を確かめる。変異（`AcquireExecuteResourceSet` の組の位置を常に 0 にする）では `sets.size() == viewport + 1u` の assert で落ちる（`-4-mutation.txt`）。変異は戻した。
- 既存テストの修正: `ExecuteLightingGraphWithLights`（ライト配列の増減のテスト）は呼び出しごとに「別のフレーム」を表すのに通し番号を変えておらず、同じフレームの別ビューポートとして別の組を作って assert が落ちた。呼び出しごとに `RenderFrameSerial = frameIndex + 1` を渡すようにした（検査の内容は変えていない）。
- 既知の限界（直さない）: 同じ形の「1 本の定数・storage buffer を Execute ごとに書く」資源は SSAO・SSR・Bloom・トーンマップ・ボリュームにも残る。照明の中でも RTGI・DDGI の計算のパラメータのバッファ（`m_RTGIComputeParametersBuffer` など）は組に含めていない（段9の差分ではない）。同じフレームに複数のビューポートを描くと、これらは後の Execute が先に記録した dispatch の読む値を上書きしうる。
- 撮影はしていない（描画の経路・内容は 1 ビューポートで変わらず、golden 4 本が通る）。
- Next: TASKS.md の次の `todo`。

## VTG9-FIX-LIGHTING-PER-EXECUTE 反復 4（2026-10-08）

- 評価者の指摘（実 Vulkan の装置で、描画が束縛する descriptor set・5 つのバッファの期待値・2 回目後の 1 組目の保持を確かめていない）に対応した。証拠は `.harness/runs/20261008-221440/verify-VTG9-FIX-LIGHTING-PER-EXECUTE-6-vulkan-test.txt`（通過）・`-6-stdout.txt`（`lighting_execute_sets_per_viewport=true ...`、stderr は空）・`-9-build.txt`（verify の Debug ビルド EXIT=0）・`-9-ctest.txt`（6/6 通過。golden 4 本は基準画像・閾値を動かしていない）。
- 試験: `RTGIDiffuseIndirectVulkanTest` の末尾に `RunMultiViewportResourceSetTest`（実 Vulkan の装置、検証レイヤー有効）を足した。同じ通し番号で別のカメラ・ライト・太陽のクリップマップ・点光源の灯の 4 ビューポートを回し、描画を提出前の `FrameCommand`（全画面パス）に積んだまま、(1) 積まれた全画面パスが束縛する descriptor set が、その Execute の組の descriptor set と同じで 4 つとも別、(2) 4 組の descriptor set と 5 つのバッファ（照明の定数・ライト配列・太陽の VSM・点光源の VSM・スライスの表）がすべて別、(3) 4 回目の Execute の後、各組のバッファをマップして読み、その Execute のカメラの位置・ライトの数・ライト配列・VSM のパラメータ・スライスの表が、同じ入力から作った期待値（`BuildVirtualShadowMap*`・`PackLightingPassLights`）とバイト単位で一致、を確かめてから、積んだ描画を記録して提出する（描画数 4）。次のフレームで 5 回 Execute すると、先頭の組から使い直し（束縛する descriptor set が前のフレームの同じ番目と同じ）、5 回目は描かず、4 組の中身が壊れていない。カメラの順を逆にしたフレームでは各組の中身がその順番のカメラの値になる。
- 変異: (a) `AcquireExecuteResourceSet` の組の位置を常に 0 にすると、`-7-mutation-vulkan.txt` のとおり「別の Execute が同じ descriptor set またはバッファを使っています viewport=0 other=1」で落ちる。(b) 点光源の VSM のバッファの `Update` を抜くと、`-8-mutation-content.txt` のとおり中身の検査で落ちる。どちらも戻した（`git diff` は空）。
- 副産物の修正: `RTGIDiffuseIndirectVulkanTest` の `RunLightingFrame` は、別々に提出する呼び出しの間で `FrameNumber` を使い回していた（通し番号が同じ → 同じフレームの別ビューポート扱いで組が進み、5 回目以降は描かれず、RTGI の出力の layout が合わなくなって検証レイヤーのエラーで落ちた）。前の反復の変更（06227648）で、この試験は verify の対象外のため見逃していた。呼び出しごとに `RenderFrameSerial` を進めるようにした。`TASKS.md` の verify に `RTGIDiffuseIndirectVulkanTest` を足した（ビルドの対象と ctest の対象）。
- 他の照明を実行する試験: DDGIProbeRadianceVulkanTest・DDGIProbeRayQueryVulkanTest・PathTracingLightingVulkanTest・LightingLightBufferTest・RenderGraphGPUTimestampScopeTest・RenderingCoordinatorGPUTimestampPublicationTest は通過（`extra-ctest-2.txt`）。`RHIGPUTimestampVulkanTest` は 1 回目に「completed GPU timing batch is missing or dropped」で落ち、単体の再実行で通過（`extra-ctest-3-timestamp.txt`。GPU 計測の取りこぼしで、環境のぶれ）。`RenderGraphTextureUsageContractTest` は `ShadowMapPass::Initialize`（空のシェーダーディレクトリ、150 秒後）の assert で落ちる。LightingPass に入る前の失敗で、この変更とは無関係に見える（基準の状態では未確認）。
- 既知の限界は前の反復のとおり（SSAO・SSR・Bloom・トーンマップ・ボリューム・RTGI/DDGI の計算のパラメータのバッファは、同じフレームに複数のビューポートを描くと後の Execute が先の dispatch の値を上書きしうる）。
- Next: TASKS.md の次の `todo`。

## VTG9-FIX-NIGHT-ENV-SUN 反復 3（2026-10-08）

- 結果: 親が直した (4) の測り方（暗い側 x 370〜699・y 100〜599 で R−G>40 の画素数）で合格にした。実装は 2e74630c のまま変えていない。証拠は `.harness/runs/20261008-235324/verify-VTG9-FIX-NIGHT-ENV-SUN-1〜5.txt`（Debug ビルド EXIT=0・ctest 6/6 通過で golden 4 本は基準画像・閾値を動かさず・検証レイヤー付き Debug の夜 `error_count` 0・`warning_count` 0・`vuid_count` 0・RelWithDebInfo ビルド・夜 3 視点の撮影、すべて EXIT=0）。
- 粒の数（`dots-count.txt`、数えた手順は `count_dots.py`）: 修正前 223（R−G の最大 98）・RTGI を切った画 0・修正後 0（RelWithDebInfo・Debug とも、R−G の最大 35）。10 未満。
- 画: `VTG9-FIX-NIGHT-ENV-SUN-night` の 3 視点を開いた。近接は球の暗い側に粒が無く、既定・低角度とも天球・地面・球・岩・小屋・電球と電球の影が欠けなく見える。
- ほかに静的な環境を読むシェーダー: RTGI の外れた光線（`RTGI/DiffuseIndirect.comp`）と DDGI（`DDGI/ProbeRadiance.comp`）にも同じ上限を入れてある。経路追跡（`PathTracing/*`）は検証用で静的環境の倍率も使わないので触らない（前の反復の記録のとおり）。
- 既知の限界: 拡散の IBL の前計算の放射照度は太陽の分を含んだまま（全エネルギーの約 3.7%）。
- Next: TASKS.md の次の `todo`。
- 2026-10-09: main（`1b5e925c`、G4 の移動とカメラ）を取り込み、一時的に退避した 16 件を main の今の状態へ戻した（G3-GR10・G3-GR13 は main で done）。TASKS.md の main との差は VTG9 の項目だけ。

## 段9 夜の見本の球の RTGI の粒（2026-10-09）

- 夕日の修正の後の受入れの撮り直し（`.harness/runs/vtg9-accept-r2/`、`VTG9-ACCEPT-r2*`）で、夜の近接の大きな球の粒は 0 になったが、夜の低角度で見本の小さな球の暗い側に橙の点が残る（夕日の修正の前後で同じ）。RTGI を切ると消える（`VTG9-DOTS2-rtgi-off`）→ `VTG9-FIX-NIGHT-RTGI-FIREFLIES`。
- ランナーが段9の項目だけを拾うよう、別の作業の todo・doing 14 件を、このブランチの上で再び backlog にした。main へマージする前に main の状態へ戻す。

## VTG9-FIX-NIGHT-RTGI-FIREFLIES（2026-10-09）

- 結果: 粒の原因を切り分けて直した。ただし done-when (3) の測り方（x 240〜519・y 370〜469 で 10 未満）は、直した後も 52 で満たせない。残りは粒ではなく、地面の遠い縁（y 455〜460）にある照らされた地面の楔の先で、RTGI の正しい照り返し。stop-when（照り返しを消さずに 10 未満にする方法が無い）に従い BLOCKED。理由と選択肢は `blocked/VTG9-FIX-NIGHT-RTGI-FIREFLIES.md`。
- 測り方の校正: 修正前 92・照り返しの面 137 は、輝度 l は浮動小数（0.2126R+0.7152G+0.0722B）のまま、m は 8 ビット輝度（床）に `MedianFilter(7)` を掛けた値で再現する（`count_rtgi_dots.py`）。ベースラインを今のコードで撮り直しても 92 / 137（決定的）。
- 切り分け（`.harness/runs/20261009-003040/exp.py` で一時的にシェーダーを書き換えて撮り、`git checkout` で戻した。夜の低角度・RelWithDebInfo）。球の中の粒は y<450、地面の縁の帯は y≥450 で分けて数えた（`region_stats.py`）:

| 変異 | 球の中の粒 | 縁の帯 | 読み取り |
|---|---|---|---|
| なし（修正前） | 32 | 60 | |
| 発光体の光源標本を全部切る（`noemit`） | 0 | 0 | 粒も縁の楔も発光体（電球）の標本から来る。画面全体の平均輝度も 67.6 → 33.4 |
| 命中点の発光体の標本だけ切る（`primaryonly`） | 0 | 56 | 粒は命中点側。1 次面の標本だけでは出ない |
| 1 回目の命中点の標本だけ切る（`nohit1emit`） | （全体 81） | | 粒の約 1/8 |
| 2 回目の命中点の標本だけ切る（`nohit2emit`） | 0 | 52 | 粒の大半 |
| 2 回目の拡散バウンスを切る（`nobounce2`） | （全体 52） | | 同上 |
| 点・方向光の直接光を切る（`nodirect`） | （全体 92） | | 粒に寄与しない（夜は月明かり程度の差が別の場所に出るだけ） |
| 環境に抜けた光線を切る（`noenv`） | （全体 97） | | 粒に寄与しない（環境は先の修正で上限済み） |
| 命中点の光線 1 本の値を固定の値で縮める（上限 30・100・300・1000・3000） | （全体 54・48・53・70・93） | | 上限が低いほど消える。上限なし≒3000 |
| 命中点の光線 1 本を露出後の値（白=1）で縮める（上限 2・4・8・16・32） | 0・2・8・18・33 | 48・50・54・58・60 | 同上。白の 32 倍以上が粒を作る |

- 結論: 粒は **電球（発光の球）そのものに当たった光線ではない**。電球は影を落とさない物体（`SetCastShadow(false)`）なので、RTGI の光線の対象（`RayTracingInstanceMaskShadowCaster`）に入らず、当たらない。粒は、**照らされた面（岩など、電球のすぐ近くで 1 m 未満の面）に当たった稀な光線**が、命中点の発光体の標本（`SampleEmitterIrradiance`、特に 2 回目の命中点）で桁違いに大きな値を持つことから来る（期待値は正しく、分散が大きいホタル）。点光源の直接光との二重の数えは、直接光の項（`ComputeDirectRadiance`）を切っても粒が変わらないので、この粒の原因ではない。環境に抜けた光線も原因ではない。
- 直し: `DiffuseIndirect.comp` の `TraceDiffuseRay` が命中面で返す値（直接光＋発光体の標本）に、露出を掛けた後の値で白の 4 倍の上限（`RTGI_RAY_RADIANCE_DISPLAY_LIMIT`、`LimitRayRadiance`。最大の成分が超えたら色相を保って縮める）を入れた。根拠: 発光体から離れた面の照り返しは露出後でおおむね白以下で、削らない。粒を作る稀な光線は白の 32 倍以上で、4 倍まで縮めれば球の中の粒は 32 → 2（2 で 0）。露出を掛けた後の値で比べるので、昼と夜の自動露出の違いで境目が動かない。環境の上限（`LimitStaticEnvironmentRadiance`）とは別（そちらは環境に抜けた光線）。1 次面の発光体の標本には掛けない（見えている面自身の直接光）。
- 結果の数（修正後、夜の低角度。Debug の検証レイヤー付きと RelWithDebInfo は画素まで同じ）: 球の中の粒 2（修正前 32）、縁の帯 50（修正前 60）、指定の測り方の合計 **52**（修正前 92）、照り返しの面（m>25）132（修正前 137）。夜の近接の暗い側の R−G>40 の画素は 0 のまま。RTGI を切った画は 0。
- 残る 52 の中身: 連結成分に分けると 5 つの塊（x 370〜373・385〜389・418〜424・455〜459・485〜489、y 456〜460、6〜14 画素ずつ、最大輝度 34〜63）と孤立した 1 画素が 4 つ（球の中 2・帯 2）。塊は見本の球の間から見える照らされた地面の楔の先で、RTGI の正しい照り返し（3 倍に持ち上げて開いたとき、修正前後とも楔の形は同じで、修正後は孤立した明るい粒だけが消えている）。RTGI を切ると楔ごと消えるので 0 になる。「照り返しの面は数えない」はずが、細い楔は 7×7 の中央値が暗く、輝度−m>12 と R−G>20 を満たして数えられる。
- 輝度の変化（上限の副作用の確認）: 夜の低角度で球の領域の平均 8.106 → 8.045、縁の帯 13.851 → 13.797、画面全体 67.623 → 67.321（−0.4%）。昼の 3 視点（太陽 45°）の平均輝度は修正前（`VTG9-ACCEPT-r2`）と完全に一致: default 124.273 → 124.273、near 124.201 → 124.201、low 126.359 → 126.359（上限に当たる光線が無い）。
- 画: 夜の 3 視点（`VTG9-FIX-NIGHT-RTGI-FIREFLIES-night`）・昼の 3 視点（`-day`）を開いた。見本の球に橙の照り返しが残り（明るい側・地面の楔）、天球・地面・球・岩・小屋・電球と電球の影が欠けなく見える。昼は修正前の画と同じ。3 倍に持ち上げた比較で、球の暗い側の孤立した明るい粒は消え、細かい粒状の濃淡（暗い、孤立していない）だけが残る。
- 証拠: `.harness/runs/20261009-003040/verify-VTG9-FIX-NIGHT-RTGI-FIREFLIES-1〜6.txt`（Debug ビルド EXIT=0・ctest 7/7 通過で golden 4 本は基準画像・閾値を動かさず・検証レイヤー付き Debug の夜 `error_count` 0・`warning_count` 0・`vuid_count` 0・RelWithDebInfo ビルド・夜 3 視点・昼 3 視点の撮影、すべて EXIT=0）。実験の撮影は `.harness/runs/startup-capture/VTG9-FIX-NIGHT-RTGI-FIREFLIES-exp-*`。
- Notes: シェーダーは行末が混在しているので、行末を保ったまま挿入・置換した（numstat 2 通りは一致）。実験で書き換えたシェーダーは `git checkout` で戻してから本実装を入れた。Git Bash から `cmake --build ... -- /m:1` を呼ぶと `/m:1` がパスとして変換されて MSBuild が失敗する（`MSYS_NO_PATHCONV=1` を付ける）。
- Next: 下の「再開」のとおり done。

### 再開（2026-10-09、反復 1・run 20261009-005300）

- (3) の領域が球の中（x 240〜519・y 370〜449）に直って再開。実装（0eb21517、`DiffuseIndirect.comp` の命中面の光線 1 本の値の上限）は変えず、検証を取り直した。証拠: `.harness/runs/20261009-005300/verify-VTG9-FIX-NIGHT-RTGI-FIREFLIES-1〜6.txt`（すべて EXIT=0）、数えた結果は同フォルダの `dots-count.txt`（`count_dots.py`）。
- Debug ビルド EXIT=0。ctest 7/7 通過（RenderGraphCompileTest・RTGIDiffuseIndirectVulkanTest・VirtualShadowMapVulkanTest・golden 4 本）。基準画像・閾値は動かしていない。検証レイヤー付き Debug の夜（低角度）は `error_count` 0・`warning_count` 0・`vuid_count` 0。
- 粒の数（夜の低角度、`-Deterministic -Night`、RelWithDebInfo と Debug は画素まで同じ）:

| 画 | 球の中の粒（y 370〜449、m≤25） | 照り返しの面（同領域 m>25） | 球の下の帯の粒（y 450〜469、m≤25） | 帯の m>25 |
|---|---|---|---|---|
| 修正前（`VTG9-ACCEPT-r2-night`） | 32 | 12 | 60 | 125 |
| 修正後（`VTG9-FIX-NIGHT-RTGI-FIREFLIES-night`） | **2**（基準 10 未満） | 8 | 50 | 124 |
| RTGI を切った画（`VTG9-DOTS2-rtgi-off`） | 0 | 0 | 0 | 0 |

- 夜の近接の暗い側（x 370〜699・y 100〜599）の R−G>40 の画素は 0（修正前も 0）。
- 昼の 3 視点（太陽 45°）の平均輝度は修正前（`VTG9-ACCEPT-r2`）と完全に一致: default 124.273 → 124.273、near 124.201 → 124.201、low 126.359 → 126.359。夜は default 56.768・near 13.779・low 67.321（低角度は修正前 67.623 → −0.4%）。
- 画: 夜の 3 視点・昼の 3 視点を開いた。天球・地面・球・岩・小屋・電球と電球の影が欠けず、夜の見本の球（小さい球の列・大きい球）に橙の照り返しが残り、RTGI を切った画のような真っ黒にはなっていない。昼は修正前と同じ見た目。
- 帯の楔（y 456〜460 の 5 つの塊）は RTGI の正しい照り返しとして残す。地面の照り返しの質は別の項目で見る。
- 2026-10-09: 一時的に退避した 14 件を main（`1b5e925c`）の状態へ戻した。TASKS.md の main との差は VTG9 の項目だけ。

## 段9 受入れの撮り直し（2026-10-09）

- 最終のコード（夜の粒と照明のパスの修正、main `1b5e925c` の取り込みの後）で受入れを撮り直した（`.harness/runs/vtg9-accept-r3/`）。Debug ビルド・ctest 14 本（`a2-ctest.txt`、14/14）・検証レイヤー付き Debug の夜 3 視点（error・warning・vuid 0）・RelWithDebInfo ビルド・朝昼夕・夜・夜の旋回の撮影、すべて EXIT=0、`failures` は空。18 本のログで VSM の溢れ・`VSM_FALLBACK`・`LIGHTING_EXECUTE_SETS_EXCEEDED` は 0、ERROR は既知の 2 件だけ。
- 夜の電球の影の測定と検証レイヤーの run の値は前の撮影と同じ。夜の粒は、近接の大きな球の暗い側（R − G > 40）0、低角度の見本の球（暗い側の粒）2。昼の 9 枚の平均輝度は前の撮影と同じ。夜の画面の平均は既定 56.768・近接 13.779・低角度 67.321。
- 受入れの記録（`VirtualizationAcceptance.md`）の段9の節を最終の値に直し、「VSM によるものではない」の一文を、粒の原因（RTGI のホタル 2 つ）・直し方・数に置き換えた。

## ランチャーと見せるシーン（2026-10-09 開始、ブランチ `feature/scene-launcher`）

- ユーザーの決定: 起動するとランチャー（作り込んだ ImGui のメニュー）、シーンは Rendering3DTest・仮想化ジオメトリのバナナ 1万本・荒野の前哨地（地形・材質の混ぜ合わせ・遠くまでの影・空気遠近・雲・枯れ草・砂塵）・夜の集落・材質ギャラリー・室内の光。岩は Poly Haven、建物・小物・家具・バナナは手元の TRELLIS.2 で生成。重い処理は許可済み。計画書は `Docs/Plans/SceneLauncherPlan.md`、項目は TASKS.md の `SCN-`（34 件）。
- 開始時の確認: main `4974821a` から分岐。`cmake --build build --config Debug --target Game -- /m:1` は成功（`.harness/runs/scn-start-build.log`）。
- 走行の間、他の系の未完の項目 14 件（todo 12・doing 2）を `backlog` に退避した。一覧は TASKS.md の冒頭。main へマージする前に元の status へ戻す。

### SCN-SCENE-ARG（2026-10-10、反復 2・run 20261009-233224）

- `Game/Scenes/SceneCatalog.h` にシーンの一覧（id・表示名・説明・見本画像・GameMode・名前付き視点・必要な資産）を1か所に定義し、`--scene=<id>`・`--scene-view=<name>` で起動直後にそのシーンへ入る。未知の id は `SCENE_UNKNOWN` を、未知の視点は `SCENE_VIEW_UNKNOWN` をエラーログに出して既定へ落ちる。引数なしの既定はまだ Rendering3DTest。`Game/Scenes/SceneReadiness.h` が SceneReady（`SCENE_READY scene=<id>`）を `--capture-deterministic` へ伝える全シーン共通の窓口。窓の大きさの引数は `ApplicationProcessor.cpp` に足した。
- `Scripts/CaptureScene.ps1`（`-Scene`・`-Views`・`-Configuration`・`-Width/-Height`・`-GameArgs`・`-RequireAssets`）を追加。Game の起動失敗・PNG の欠け・Game.log のエラー・SCENE_READY の欠けで非 0、成功で `CAPTURE_SCENE result=pass`。`CaptureStartupScene.ps1` と Rendering3DTest 前提の他のスクリプト 6 本は `--scene=rendering3dtest` を渡す。
- 証拠: `.harness/runs/20261009-233224/verify-SCN-SCENE-ARG-1〜3.txt`（すべて EXIT=0）。Debug の Game ビルド成功、`CaptureStartupScene.ps1` の 3 視点（平均輝度 124.087・123.784・125.951、白飛び・黒つぶれ 0）、`CaptureScene.ps1 -Scene rendering3dtest`（pass）。
- 決定的な撮影（`-Deterministic`）で main の撮影と画素を比べた（`png-compare.txt`）: 差のある画素は視点ごとに 0〜11 個・差は最大 3（浮動小数の揺れ）で、平均輝度は main と完全に一致。非決定の撮影の `default`・`near`・`low` を main の撮影と並べて開き、天球・地面・球・岩・小屋が同じ見た目であることを確かめた（球の自転の位相の差だけ）。
- 負の確認: 未知の id（`scene-unknown.txt`）・未知の視点（`scene-badview.txt`）で `result=fail`、`-Width 960 -Height 540`（`scene-size.txt`）で 960×540 の PNG、`-Views default,near,low`（`scene-views.txt`）で 3 枚。
- Notes: `Test/Game/Bridge` の `NorvesLibBridgeAdapterScriptComponentTest` は M9SkeletalPreparation などの既存ソースが `CMakeLists.txt` に無くリンクできない（この項目の前から。新しいソースはヘッダのみなので CMake へ足す物は無い）。Git Bash から `cmake --build ... -- /m:1` を呼ぶと `/m:1` がパス変換で壊れるので PowerShell から呼ぶ。

### SCN-SCENE-ARG（2026-10-10、反復 3・run 20261009-233224）

- 評価者の指摘 2（Release では `-RequireAssets` の検査が省略される）を直した。`Game` に `--scene-report=<path>` を足し、`ResolveStartupScene` が未知の id・視点（`unknown_scene=`・`unknown_view=`）と欠けた必要資産（`missing_asset=`）をログとは別にテキストで書く。`CaptureScene.ps1` は視点ごとに報告を読み、未知の id・視点を失敗に、`-RequireAssets` のとき欠けを失敗にする（Game.log が無い Release でも効く）。報告が無いのに Game が正常終了したときも失敗。Rendering3DTest の読み込み失敗の印（`COOKED_MODEL_MISSING` など）は引き続きログからだけ拾う。
- 証拠: Debug ビルド EXIT=0（`verify-SCN-SCENE-ARG-1.txt`）、`CaptureStartupScene.ps1` の 3 視点 pass（平均輝度 124.095・123.74・125.973、`-2.txt`）、`CaptureScene.ps1 -Scene rendering3dtest` pass（`-3.txt`、報告は `scene=rendering3dtest` のみ）。負の確認: boulder の gltf を一時退避して `-RequireAssets` で `missing_asset=` により fail（`neg-require-assets.txt`、退避は戻した）、未知の id で `unknown_scene=` により fail（`neg-unknown-scene.txt`）。Release のビルドは回していない（報告は std::ofstream で書き、ログのマクロに依存しない）。
- 指摘 1（neural_material_decode.slang のコンパイル失敗の除外）は、直す場所が `paths` の外（RHI/Vulkan・Rendering の危険地帯）なので進めず、`blocked/SCN-SCENE-ARG.md` に選択肢と推奨を書いた。スクリプトの例外はそのまま。

### SCN-R3D-REENTRY（2026-10-10、反復 4・run 20261009-233224）

- 周回の検査 `--scene-cycle-smoke=<id,...>`・`--scene-cycle-count=<n>`（既定 3）を足した。何も置かない `SceneCycleHome` モードとシーンの間を `RequestChange` で周回し（`Game/Scenes/SceneCycleSmoke.*`。GameApplicationHandler::OnUpdate が毎フレーム進める）、シーンを出て値が落ち着いた後に材質・テクスチャ・メッシュ・Model・Entity の数とテクスチャ・バッファの確保量・sparse の使用量を `SCENE_CYCLE_SAMPLE` で出す。2周目以降が同じシーンの1周目と違えば `SCENE_CYCLE_DIFF`（grew/shrank）と `SCENE_CYCLE_SMOKE result=fail changed=<種類>`、同じなら `result=pass`。終了コードは pass で 0。待ちは描画されたフレームで数える（GameThread の更新は RenderThread のシェーダーのコンパイル中も回るので、更新回数で待つと描画の前に尽きる）。値が 90 描画フレーム変わらず、非同期の読み込み・遅延解放待ち（`GetPendingRetireCount`）が無いことを「落ち着いた」とする。
- 資源の返却: `GameModeScope` に `TrackMaterial`・`TrackTexture` を足し、Cleanup の最後（実体・メッシュ・Model の後）に材質と追跡したテクスチャを返す。材質が参照する VT は材質ごとに作られて共有されないので、材質のデータから集めて返す（パスで読んだテクスチャはキャッシュが所有するので返さない）。個数の観測用に `MaterialResources::GetCount`・`MeshResources::GetCount`・`MegaGeometryResources::GetModelCount` を足した。
- Rendering3DTest: Enter の材質（銀・石畳・地面・見本 10・地面の見本・負荷・電球）とチェッカーのテクスチャをスコープで追跡。クック済みの岩・小屋・スキャン資産の VT（MegaMesh は材質のテクスチャを所有しない）は、MegaMesh を作った後に追跡する。Leave で、材質へ入る前の更新（読み込み中・モデル用）の VT を返し、以降に届く読み込みの結果は `PendingMaterialUpdate::bCancelled` で捨てる（VT なら返す）。
- 検証: ビルド `verify-SCN-R3D-REENTRY-5.txt`（EXIT=0）。周回 `-6.txt`: rendering3dtest を 3 周で `result=pass`（3 周とも materials=0 textures=0 meshes=0 models=0 entities=0 buffers=0、bytes 0）。修正前の周回（`-4.txt`）は VT が 15 本（岩・小屋・スキャン資産 5 モデル × 3）ずつ増えて fail したので、検査は漏れを検出する。撮影 `-7.txt`: 3 視点 pass（平均輝度 124.061・123.708・125.961、前回 124.095・123.74・125.973 と同じ範囲）。default.png を開いて天球・地面・球・岩・小屋・電球が欠けないことを確かめた。
- 決定: 2周目以降の Enter で、検証レイヤーの `UNASSIGNED-Threading-MultipleThreads-Write` が出る（`GpuResourceStore::UploadTextureData` が GameThread から共有コマンドプールでコマンドリストを作り、RenderThread と競合）。この項目の paths の外（RHI）の危険地帯なので、別項目 `SCN-RHI-UPLOAD-THREAD` を TASKS.md に足した。`RunSceneCycleSmoke.ps1` は、このエラーだけは警告として数え（`-StrictValidation` で失敗）、解放した資源の使用を示す他の検証エラーは失敗にする。
- Notes: Release は Game.log を書かないので `RunSceneCycleSmoke.ps1` は Debug・RelWithDebInfo で使う。`RenderMaterialStore.h` は BOM 無しの ASCII で、日本語のコメントを足すと MSVC（cp932）が次の行を飲み込んだので BOM を付けた。ランチャー（SCN-LAUNCHER）ができたら、ホームの代わりにランチャーへ戻る周回へ差し替える。

### SCN-R3D-REENTRY（2026-10-10、反復 5・run 20261009-233224）

- 評価者の差し戻し 3 点に対応。(2) `ResourceStats` に `GeometryPoolCapacityBytes`・`GeometryPoolUsedBytes`・`MeshBufferCount`・`MeshBufferBytes` を足し（`GpuResources::GetResourceStats`、`ProceduralMeshGpuStore::GetBufferUsage`）、周回の観測と比較を 9 → 12 項目にした。ジオメトリのプールの確保量（256 MB）と手続きメッシュの直接バッファが比較に入る。3 周とも一致（`geometry_pool_capacity_bytes=268435456`、他は 0）。(3) `Test/Game/Bridge/CMakeLists.txt`・`Test/Core/Rendering/CMakeLists.txt` のソース一覧に `SceneCycleSmoke.cpp` を追加（Bridge 側でコンパイルされ、`SceneCycleSmoke` の未解決シンボルなし。リンクの未解決 15 件は既存）。
- (1) 検証エラーの除外は外した: `RunSceneCycleSmoke.ps1` は Validation Error をスレッド競合を含め全て失敗にする（`-StrictValidation` は廃止、`SCN-RHI-UPLOAD-THREAD` の verify からも外した）。その結果、周回スモークは資源の数が一致していても Validation.log の競合 3 件で失敗する（`verify-SCN-R3D-REENTRY-2.txt`）。競合の解消は VulkanDevice の共有コマンドプールの分離（RHI/Vulkan、paths の外）が要るので `blocked/SCN-R3D-REENTRY.md` に選択肢と推奨を書いた。推奨は `SCN-RHI-UPLOAD-THREAD` を先に済ませてから本タスクを `todo` に戻すこと。
- 証拠: `.harness/runs/20261009-233224/verify-SCN-R3D-REENTRY-1.txt`（Debug ビルド EXIT=0）、`-2.txt`（周回 EXIT=1: 資源は一致、競合 3 件で失敗）、`-3.txt`（撮影 pass、平均輝度 124.065・123.711・125.96）。

### SCN-SCENE-ARG（2026-10-10、反復 6・run 20261009-233224）

- 評価者の指摘 1（シェーダーのコンパイル失敗の除外）を `blocked/SCN-SCENE-ARG.md` の推奨の案1で直した。`VulkanDevice::CreateSlangShaderCompiler` は Slang SDK が無い（`IsAvailable()` が偽）とき nullptr を返し、`NeuralMaterialDecodePass::Initialize` は `HasSlangCompiler()` が偽なら「pass will be skipped」の INFO を出して `m_Decoder.Initialize` を呼ばない。これでこの環境のログから `Failed to compile shader [neural_material_decode.slang]` が消えたので、`CaptureScene.ps1`・`CaptureStartupScene.ps1` の `Test-AllowedShaderFailure`（例外）を外した。
- 証拠: `.harness/runs/20261009-233224/verify-SCN-SCENE-ARG-1.txt`（Debug ビルド EXIT=0）、`-2.txt`（3 視点 pass、平均輝度 124.038・123.711・125.965、白飛び・黒つぶれ 0）、`-3.txt`（`CaptureScene.ps1 -Scene rendering3dtest` pass）。撮影のログ（`.harness/runs/scene-capture/SCN-SCENE-ARG/default.Game.log`）に `Failed to compile shader` は無く、`NeuralMaterialDecodePass ... pass will be skipped` の INFO が出ている。default.png を開き、天球・地面・球・岩・小屋・電球が欠けないことを確かめた。
- Notes: `Scripts/RunSceneCycleSmoke.ps1` にも同じ例外が残っている（SCN-R3D-REENTRY の範囲。そのタスクを再開するときに外す）。

### SCN-RHI-UPLOAD-THREAD（2026-10-10、反復 7・run 20261009-233224）

- 原因: `VulkanDevice` の共有コマンドプール 1 つを、描画スレッドのコマンドリストの記録と、ゲームスレッドの `UploadTextureData`（ミップ生成の `CreateCommandList`）・`Texture::Update` の単発コマンドが同時に触っていた（プールの確保・解放・`vkCmd*` の記録はどれもプールへの書き込みで外部同期が要る）。
- 修正（RHI/Vulkan。`paths` の `Library/RHI` は旧称で、実体は `Library/Core/Private/RHI/Vulkan`）: (1) `VulkanCommandList` はリストごとに専用のコマンドプールを持つ（`CreateExclusiveCommandPool`、解体で破棄）。(2) 単発コマンド（`BeginSingleTimeCommands`/`EndSingleTimeCommands`）は呼んだスレッドごとのプール（`GetSingleTimeCommandPool`、デバイスの破棄まで保持）。共有の `GetCommandPool`/`m_commandPool` は廃止。(3) プールを分けたあとに出た `VkQueue` の競合（`vkQueueSubmit` とゲームスレッドの `vkQueueWaitIdle`・`vkDeviceWaitIdle`・描画スレッドの `vkQueuePresentKHR`）を、`GetGraphicsQueueMutex`（提出の窓口と同じミューテックス）の下でプレゼント・キュー待機・デバイス待機を行うことで直した。
- 証拠: `.harness/runs/20261009-233224/verify-SCN-RHI-UPLOAD-THREAD-1.txt`（Debug ビルド EXIT=0）、`-2.txt`・`-3.txt`（`RunSceneCycleSmoke.ps1 -Scenes rendering3dtest -Cycles 3` が 2 回とも `result=pass`、Validation.log は 0 バイト。修正前は `UNASSIGNED-Threading-MultipleThreads-Write` が 3 件）、`-4.txt`（`CaptureScene.ps1 -Scene rendering3dtest` pass、平均輝度 124.143、default.png を開いて天球・地面・球・岩・小屋・電球が欠けないことを確認）。
- Notes: 周回スモークは競合が時間依存なので、2 回の pass で再現しなかったことを確かめた（保証ではない）。デバイス待機をミューテックスの下で行うので、`GraphicsSubmitScope` を持ったまま `WaitIdle` を呼ぶと自己待ちになる（現在の呼び出し元は Commit 後か窓口の外）。これで `SCN-R3D-REENTRY`（blocked）を `todo` に戻せる。

### SCN-LAUNCHER（2026-10-10、反復 9・run 20261009-233224）

- 引数なしの起動（と `--scene=launcher`・`--scene-cycle-smoke`）で `Launcher` モードに入り、ImGui の全画面メニューを出す（`Game/GameModes/Launcher/`）。背景は物理空の夕景、左上にタイトル「NorvesLib」と副題、`SceneCatalog` から作ったカード（見本画像の枠・表示名・説明・資産が足りないときの取得コマンド）、「ゲームを終了」。見本画像が無いカードは暗いグラデーションにシーン名。選択は矢印・WASD・マウスのホバーとクリック・パッドの十字キーと左スティック、決定は Enter・クリック・A（Menu の入力文脈）。ImGui は `--imgui` が無くてもランチャーのために登録し、デバッグ窓は今どおり `--imgui` のときだけ。字の大きさは窓の大きさから決める。`SceneCycleHome` は廃止し、周回スモークはランチャーを通る。
- 証拠: `.harness/runs/20261009-233224/verify-SCN-LAUNCHER-1.txt`（Debug ビルド EXIT=0）、`-2.txt`（Release ビルド EXIT=0）、`-3.txt`（1280×720 の撮影 pass）、`-3b.txt`（1920×1080 の撮影 pass）、`-4.txt`（周回スモーク rendering3dtest 3 周 `result=pass`、Validation.log 0 バイト）、`-5.txt`（`CaptureStartupScene.ps1` 3 視点 pass、平均輝度 124.043・123.863・125.962）。
- 画: 両サイズの `default.png` を開いた。ImGui の既定の灰色の窓・タイトルバー・スクロールバーは出ておらず、タイトル・副題・カード（選択の橙の枠）・終了ボタン・操作の案内が読める。
- Notes: 見本画像の実物は SCN-THUMBNAILS で作る（今はグラデーションの代用）。1920×1080 では説明文が改行でカードの右端に近づく（読める範囲）。

### SCN-LAUNCHER（2026-10-10、反復 10・run 20261009-233224）

- 評価者の差し戻し 2 点に対応。(1) `LauncherMenu` の方向入力と決定（Enter・Space・A）を同じキューに積み、`ApplyNavigation` が配送順に処理する。決定は、それより前に積まれた移動を適用した後の選択を対象にする（同じフレームの「下、Enter」は移動後の項目を決定。「Enter、下」は移動前の項目を決定し、その後に移動する）。マウスのクリックは添字を指す `RequestActivate` のまま。非ブロッキングの英語コメントも日本語にした。(2) RelWithDebInfo の Game をビルドして撮影した。
- 入力順を反証する検査 `LauncherMenuTest`（`Test/Game/Bridge/LauncherMenuTest.cpp`）を足した。Core にリンクしない小さな実行ファイル（`LauncherMenu.cpp` だけ）。4 ケース: キーボードの下→Enter、Enter→下、パッドの十字キー下×N→A（終了を決定）、ApplyNavigation 前は決定が確定しない。
- 証拠（`.harness/runs/20261009-233224/`）: `verify-SCN-LAUNCHER-6-menu-test.txt`（LauncherMenuTest passed、EXIT=0）、`-6-1.txt`（Debug ビルド EXIT=0）、`-6-2.txt`（Release）、`-6-2r.txt`（RelWithDebInfo）、`-6-3.txt`・`-6-3b.txt`（Debug の 1280×720・1920×1080 撮影 pass）、`-6-3r.txt`（RelWithDebInfo 撮影 pass）、`-6-3rel.txt`（Release 撮影 pass）、`-6-4.txt`（周回スモーク rendering3dtest 3 周 `result=pass`）、`-6-5.txt`（`CaptureStartupScene.ps1` 3 視点 pass）。RelWithDebInfo の `default.png` を開き、灰色の窓・タイトルバー・スクロールバーが無く、タイトル・カード・終了ボタンが読めることを確かめた。
- Notes: `NorvesLibBridgeAdapterScriptComponentTest`（Test/Game/Bridge の束）は、ソース一覧が Game の増えた分（M9SkeletalPreparation・StartupSkinnedProbe・SequenceFrameCapture・DogMovementSmokeRoutine・GameHapticsSettings・GameInputSettings・QuadrupedLocomotion ほか）に追いついておらず、このコミットより前から Debug でリンクできない。範囲外なので触らず、`LauncherMenuTest` を独立させた。

### SCN-PAUSE-MENU（2026-10-10、反復 11・run 20261009-233224）

- ランチャー以外の全シーンで Esc（ゲームパッドは Start）を押すとポーズメニュー（続ける／ランチャーへ戻る／ゲームを終了）が開き、もう一度 Esc（Start・B）か「続ける」で閉じる。「ランチャーへ戻る」は `RequestChange(Launcher)`、「ゲームを終了」は `RequestExit(0)`。メニューは `Game/Pause/`（`PauseMenu` = 状態と入力、`PauseMenuView` = ImGui の暗幕と中央のパネル。ランチャーと同じ配色・フォント）。
- 入力: `PauseMenu` は入力ルーターの `PriorityGame + 100`（シーンのカメラ・ピッキング・F1〜F5 の上、デバッグの入力マスクの下）に登録する。開いている間は押下・マウス移動・スクロール・パッドのボタンを下位へ渡さず（F1〜F5・Alt も同じ）、離した入力だけ通す。`GameApplicationHandler::UpdatePauseMenu`（OnUpdate）が、開いたときにメニュー用の入力文脈（`Menu`、カーソルは通常）を積んで `InputSystem::ReleaseAll` で押下状態を捨て、閉じたら外す。入力イベントの配送中は文脈を替えられないので、開閉の反映は更新で行う。
- ランチャー: `LauncherData::m_pPauseMenu` を `GameApplicationHandler` のランチャーの生成子が渡し、Enter で無効・Leave で有効に戻す（ランチャーには戻る先が無い）。
- 撮影・自動実行（`--capture-png`・`--capture-sequence`・`--exit-after-frames`）の起動はポーズメニューを使わず、ImGui モジュールも登録しない（既定の撮影・計測の描画を変えない）。それ以外の起動では、ポーズメニューのために ImGui モジュールを登録する（`--scene=<id>` の直接起動でも Esc が効く）。開発ビルドだけの `--open-pause-menu` は、起動して 3 回目の更新で開き、撮影でも使う（開いている間だけ撮影に ImGui を重ねる）。
- 証拠（`.harness/runs/20261009-233224/`）: `verify-SCN-PAUSE-MENU-1.txt`（Debug の Game ビルド EXIT=0）、`-2.txt`（`CaptureScene.ps1 -Scene rendering3dtest -GameArgs open-pause-menu` pass。平均輝度 70.6 で暗幕が掛かる。`default.png` を開き、背景のシーンの上に半透明の暗幕と中央のパネル（一時停止・続ける（選択）・ランチャーへ戻る・ゲームを終了・操作の案内）が出て、ランチャーと同じ見た目であることを確かめた。ログに `PAUSE_MENU state=open`）、`-3.txt`（周回スモーク 3 周 `result=pass`）、`-4.txt`（`CaptureStartupScene.ps1` 3 視点 pass、平均輝度 124.026・123.696・125.966 で前回と同じ範囲）、`-test.txt`（`PauseMenuTest` passed）、`-flow.txt`（実機の流れ）。
- 実機の流れ（`-flow.txt` と同名の `.Game.log`）: `--scene=rendering3dtest` を撮影なしで起動し、ウィンドウへ WM_KEYDOWN を送った。Esc で `state=open`、F2 を挟んで Esc で `state=closed`、Esc→下→Enter で `action=return_to_launcher` からランチャー開始、ランチャーで Esc を押しても開かず、ランチャーの Enter でシーンへ入り直した後の Esc で再び開く。
- `PauseMenuTest`（`Test/Game/Bridge/PauseMenuTest.cpp`、Core にリンクしない小さな実行ファイル）: 開閉、無効なとき開かない、開いている間に F1〜F5・Alt・マウスを消費し離した入力は通す、Start・B、下→Enter が移動後の項目を決定。
- Notes: `Test/Game/Bridge`・`Test/Core/Rendering` の `GameApplicationHandler.cpp` を含む束に `Pause/PauseMenu.cpp` を足した（`PauseMenuView.cpp` は ImGui 無効のそれらの束では要らない）。実機でのキー送信は WM_KEYDOWN の投函で、ゲームパッドとマウスのクリックは実機で試していない（入力の型は `PauseMenuTest`、描画は撮影で確認）。

### SCN-PAUSE-MENU（2026-10-10、反復 12・run 20261009-233224）

- 評価者の差し戻し 2 点に対応。(1) F1 との衝突: ImGui モジュールは Install で入力デバッグのマスク（F1 で下位の入力を止める予約。優先度 999）を有効にする。ランチャーやポーズメニューのためだけに登録した通常起動でも有効になり、F2→F1→Esc で F1 が Rendering3DTest に届かず、続く Esc もポーズメニューの手前で消費されていた。`GameApplicationHandler` は `--imgui` が無いとき、登録後にこの予約を外す（`GetInputDebugOverlay().SetEnabled(false)`）。`--imgui` のときは従来どおり。(2) Start の押しっぱなし: 開いた直後の `ReleaseAll()` でパッドの状態が 0 になり、次のサンプルが押し続けの Start を新しい押下と判定してメニューが閉じていた。`PauseMenu` は Start の押下を受けるとラッチし、離した通知か、Start が上がったサンプルを見るまで再押下を受け付けない（開く側・閉じる側とも）。
- 検査: `PauseMenuTest` に `TestStartHeldAcrossInputReset` を足した（Start 押下→ReleaseAll 相当→同じ押下と押しっぱなしのサンプルを繰り返しても開いたまま、離したあとの押し直しで閉じる、離しの通知が無くてもサンプルで外れる）。InputSystem は Core に依存するのでリンクせず、InputSystem が配る順（ボタンの変化の後にサンプル）を並べて模擬した。英語だった起動オプションの警告と、触ったテストの診断文は日本語にした。
- 証拠（`.harness/runs/20261009-233224/`）: `verify-SCN-PAUSE-MENU-12-test.txt`（`PauseMenuTest passed`、EXIT=0）、`-12-1.txt`（Debug の Game ビルド EXIT=0）、`-12-2.txt`（`CaptureScene.ps1 -Scene rendering3dtest -GameArgs open-pause-menu` pass、平均輝度 70.646。`default.png` を開き、暗幕・中央のパネル・3 項目・案内を確認）、`-12-3.txt`（周回スモーク 3 周 `result=pass`）、`-12-4.txt`（`CaptureStartupScene.ps1` 3 視点 pass、平均輝度 124.03・123.769・125.964）、`-12-flow.txt`（実機: `--scene=rendering3dtest` で F2→F1→Esc の後に `state=open`、もう一度 Esc で `state=closed`。ログ `ImGuiModule registered debug=0 launcher=0 pause=1`）。
- Notes: パッドの Start 押しっぱなしの実機試験は未実施（接続機が無い。入力の型は `PauseMenuTest`）。

### SCN-FREE-CAMERA（2026-10-10、反復 14・run 20261009-233224）

- 自由飛行カメラ `FreeFlyCameraController`（`Library/Core/Public/Input/`。位置と Yaw/Pitch だけを持ち、フレームごとの `InputState` から更新する部品）を足した。右ボタンを押している間マウスで視点、WASD で前後左右・QE で上下、Shift で速く（4 倍）、ホイールで移動の速さ（1 段 ×1.25、0.1〜500 m/s）、ゲームパッドは左スティックで移動・右スティックで視点・RT/LT で上下・左スティックの押し込みで速く。`CameraInputCollector` に WASD・QE・Shift とパッドのサンプルを持たせ、Maya 式のカメラの入力はそのまま。
- シーンの一覧（`SceneCatalog.h`）の視点を `SceneViewKind`（SpringArm=Rendering3DTest の今のカメラ、FreePose=自由飛行）つきにした。`--scene-view=<name>` で起動時に選べ、`CaptureScene.ps1` は `-Views` を省くと Game が `--scene-report` に書く `views=` の全視点を撮る（先頭を default.png、残りを `<名前>.png`）。Rendering3DTest は Maya 式のまま。
- `sandbox` シーン（`Game/GameModes/Sandbox/`。空と 8 m 周期の格子の地面だけ。赤＝x=0 の列、青＝z=0 の行、黄＝原点の升。開発ビルドだけ登録・ランチャーに出る）。視点は `overview`（既定）・`low`・`top`。`FreeCameraRig`（`Game/Scenes/`）が入力の登録・カメラの実体・ポーズメニュー中の停止を持つ。
- `--camera-script=<file>`（開発ビルドだけ。書式は `Game/Input/CameraScript.h`）で、決まった入力を 1/60 秒刻みで流す。流し終えるまで撮影を待たせ、`FREE_CAMERA_POSE phase=start/end` をログに出す。台本は `Scripts/CameraScripts/sandbox-walk.txt`（キーとマウス）・`sandbox-pad.txt`（パッド）。
- 証拠（`.harness/runs/20261009-233224/`）: `verify-SCN-FREE-CAMERA-14-1.txt`（Debug の Game ビルド EXIT=0）、`-14-2.txt`（`CaptureScene.ps1 -Scene sandbox` 全視点 default(overview)・low・top が pass）、`-14-3.txt`（walk 台本）、`-14-4.txt`（pad 台本）、`-14-5.txt`（rendering3dtest 3 視点 pass、平均輝度 124.143・123.973・126.023）、`-14-6.txt`（`CaptureStartupScene.ps1` 3 視点 pass、124.024・123.744・125.939）、`-14-7.txt`（Release の Game ビルド EXIT=0）。
- 画: 前（`scene-capture/SCN-FREE-CAMERA/default.png`、位置 (0, 4, -12)・Yaw 0°・Pitch -15°）と後の2枚を開いた。walk は W 前進 8 m（やや下向き）・D 右へ 4 m・右ボタンでマウスを右へ 27° 回転・E 上へ 2 m で、ログの終わりの姿勢が (-4, 3.929, -4.273)・Yaw 27°・Pitch -15° と台本の見込みに一致し、画像も赤い線の消失点が画面の左へ寄って、手前の黄の升が右下へ動いている。pad は左スティックの前進・右・右スティックで 90° 右へ回転・RT 上昇で (-4, 5.929, -4.273)・Yaw 90°・Pitch -15° に一致し、画像は赤の行（Z 軸の線）が水平になり、青の線が中央へ収束して視線が -X を向いている。`top` は真上から見た格子で、赤の列が縦・青の行が横。
- Notes: 実機のマウス・キー・パッド（現物の入力）での操作は未試験（台本は `InputState` を直に流すので、`CameraInputCollector` から `InputState` への写しは撮影で確かめていない）。型の振る舞いは台本の終端姿勢で確かめた。

### SCN-FREE-CAMERA（2026-10-10、反復 15・run 20261009-233224）

- 評価者の差し戻し（Rendering3DTest に `overview` が無い）に対応。`SceneCatalog.h` の Rendering3DTest の先頭視点（Maya 式の既定のカメラ）の名前を `default` から `overview` に改め、視点は `overview・near・low` になった。`FindSceneView` は `default` を先頭視点の別名として受け付ける（従来の `--scene-view=default` も効く）。カメラの中身は変えていない。
- 証拠（`.harness/runs/20261009-233224/`）: `verify-SCN-FREE-CAMERA-15-1.txt`（Debug の Game ビルド EXIT=0）、`-15-2.txt`（`CaptureScene.ps1 -Scene sandbox` pass）、`-15-3.txt`（rendering3dtest 全視点 pass。平均輝度 124.143・123.973・126.023 で反復 14 と同一。`default.SceneReport.txt` が `views=overview,near,low`）、`-15-4.txt`（`-Views overview` pass、平均輝度 124.143、`unknown_view` なし）。

### SCN-CAMERA-PATH（2026-10-10、反復 16・run 20261009-233224）

- カメラパス `CameraPath`（`Library/Core/Public/Input/CameraPath.h`。キー列は呼ぶ側の静的配列で所有しない）を足した。キー = 時刻・カメラの位置・注視点。位置と注視点をそれぞれ、キーの時刻に合わせたエルミート補間（接線は両隣の差 ÷ 時刻の差。間隔が不均一でも速度がつながる）でつなぐ。一周する（最初のキーの時刻 0、周期 > 最後のキーの時刻）か、端に留まるかを選べる。注視点から Yaw/Pitch を求める `LookAngles` は自由飛行カメラの角度の約束と同じ。
- シーンごとの定義は `SceneDescriptor::CameraPath`（`ScenePathDescriptor`）。`sandbox` に、原点の周り 16 m の円（高さ 4〜5 m）を 2.5 秒ごと 45° ずつ進む周期 20 秒の一周パスを置いた（注視点は周り 4 m の円で位置の 90° 先）。Rendering3DTest は nullptr（見た目は不変）。
- 操作: `FreeCameraRig` が P キー（ゲームパッドは Y。`CameraInputCollector` が押下の要求を覚える）で再生・停止。再生中に自由飛行の入力（WASD・QE・右ボタン・ホイール・スティック・トリガー。Shift と左ボタンだけは含めない。`FreeFlyCameraController::HasMoveOrLookInput`）があると止まり、そのフレームの入力で自由飛行へ戻る。止めたあとの再生はパスの時刻から続け（カメラはパスへ戻る）、一周しないパスは終わると停止して次は最初から。ポーズメニュー中は時刻が進まない。台本（`--camera-script`）の間は実入力と同様に P も無視する。
- `--camera-path-time=<秒>`（`GameApplicationHandler`）: 起動時にパスのその時刻の位置・向きへ停止したまま置く（最初の視点より優先）。ログに `CAMERA_PATH state=placed time=…`、`FREE_CAMERA_POSE phase=start`。
- 証拠（`.harness/runs/20261009-233224/`）: `verify-SCN-CAMERA-PATH-1.txt`（Debug の Game ビルド EXIT=0）、`-2.txt`（`CaptureScene.ps1 -Scene sandbox -GameArgs camera-path-time=5` pass。3 視点とも同じパスの時刻 5 なので同じ画像）、`-3-t11.txt`・`-3-t16.txt`（`-Views overview` で時刻 11・16 の撮影 pass）、`-flow.txt`（実機の流れ）。
- 画: t=5・11・16 の 3 枚（`scene-capture/SCN-CAMERA-PATH/default.png`・`-t11/overview.png`・`-t16/overview.png`）を開いた。ログの姿勢は独立に計算したエルミート補間と一致（t=5: (16, 4, 0)・yaw 75.964°・pitch -10.309° = キー 2 のちょうど真ん中、t=11: (-4.844, 4.352, 15.118)・yaw -176.272°・pitch -11.576°、t=16: (-15.118, 4.352, -4.844)・yaw -86.272°・pitch -11.576°）。画像は、t=5 で +X 側から -X へ向き（青の行が奥へ伸び、8 m 手前に赤の列と黄の升）、t=11 で +Z 側から -Z へ向き（赤の列が 2 本、左右の距離比が 3.2 : 4.8 m と合う）、t=16 で -X 側から +X へ向く（青の行が 2 本、比が 3.2 : 4.8 m）。パスに沿って位置と向きが変わっている。
- 実機の流れ（`-flow.txt` と同名の `.Game.log`）: `--scene=sandbox` を起動し、ウィンドウへ WM_KEYDOWN を送った。P で `state=playing time=0.000`、約 3 秒後に W を押すと `state=stopped reason=input time=3.087 position=(13.066, 4.861, -9.114)`（キー 1 とキー 2 の間）、もう一度 P で `playing time=3.087`（止めた時刻から再開）、P で `stopped reason=toggle time=5.138 position=(15.982, 4.009, 0.632)`（キー 2 の直後）。
- Notes: ゲームパッドの Y は実機で試していない（接続機が無い。P と同じ要求の経路）。`CameraPath` の単体テストは足していない（補間は撮影の姿勢と独立計算の一致で確かめた）。

### SCN-GEN-PIPELINE（2026-10-10、反復 17・run 20261009-233224）

- `Scripts/GenerateSceneModels.py`（標準ライブラリだけ）を足した。`--ids banana` で、ComfyUI（`Y:\ComfyUI\ComfyUI_windows_portable`）が動いていなければ起動し、Z-Image Turbo で参照画像（`Tools/ComfyWorkflows/text_to_image_zimage_turbo_api.json`）→ 背景除去 → TRELLIS.2（`image_to_3d_trellis2_api.json`。保存済みの Pixal3D 兼用ワークフローからスイッチを TRELLIS.2 側へ解消し、出力に届かないノードを落とした）で GLB を作り、`Assets/Models/Generated/<id>/` に GLB・`reference.png`・`manifest.json`（プロンプト・seed・目標面数・テクスチャの大きさ・面数・テクスチャの種類・位置の範囲・MD5・ワークフローの MD5・生成日時）を置く。終了時は `/free` でモデルを降ろし、自分で起動した ComfyUI だけ止める（既に動いていたものは止めない）。`--reference-only`（seed の選び直し）・`--reuse-reference`（3D だけ作り直し）・`--preview`（`Scripts/BlenderPreviewGlb.py` を Blender ヘッドレスで走らせ、正面・側面・上面の PNG）・`--verify [--require-cooked]`（GPU 不使用。定義と manifest と実ファイルの MD5 の一致、クック済みのモデルと内蔵テクスチャの項目と実パッケージ）。
- 定義は `Assets/AssetSets/GeneratedModels.json`、バナナは seed 1001・目標 10 万面・テクスチャ 2048・実寸の高さ 0.2 m。生成物は `.gitignore` に `/Assets/Models/Generated/` を足して管理外。クックの一覧は `Assets/AssetSets/MegaBananasModels.json`（論理パスは `Models/Generated/banana/banana.glb`）。
- `Scripts/CookAssets.ps1` を直した: GLB を `AssetCook` に渡すと、内蔵の画像がテクスチャとして同じ呼び出しで焼かれ、マニフェストに 2 項目目以降（`<モデル>.img<n>.png`）として並ぶ。従来は 1 項目だけを前提にして失敗していたので、2 項目目以降を埋め込みテクスチャとして検証し（種類が texture・論理パスとパッケージ名がモデルの接頭辞）、状態ファイルへ `embedded_assets` として残して、スキップ時も同じ項目を返す。
- 証拠（`.harness/runs/20261009-233224/`）: `verify-SCN-GEN-PIPELINE-1.txt`（`--verify --require-cooked --ids banana` が `VERIFY_OK`・EXIT=0）、`-gen.txt`（生成のログ: `faces=99722 textures=base_color,metallic_roughness,normal,occlusion`、`COMFY models_released`・`COMFY stopped`）、`-preview.txt`、`-cook.txt`（`COOK_ASSETS cooked=125 skipped=0`）、`-cook2.txt`（`cooked=0 skipped=125`）。reference.png の末尾を 1 バイト足すと `--verify` が `VERIFY_FAIL`（MD5 不一致）で exit 1 になることも確かめ、戻した。
- 画: `reference.png`（背景が単色の灰色で、反った黄色いバナナ。茎と先端が褐色）と、`preview_front.png`・`preview_side.png`・`preview_top.png` を開いた。側面で反り・茎・先端の形が出ていて、色は黄〜淡い黄、崩れや穴は無い。seed は 1001 のまま。
- Notes: 生成物の GLB の位置の範囲は Y 方向 約 1.0（x 0.27・z 0.77）で、元の単位のまま。実寸 0.2 m に合わせる倍率（`height_m` ÷ Y の高さ）は取り込む側（SCN-BANANAS-SCENE）で掛ける。`manifest.json` の `bounds_min/max` と `height_m` から求められる。GPU は生成後に空いている（ComfyUI は止まっている）。AssetCook は `build/Tools/AssetCook/Debug` の最新版を使った（Release は 8 月版で `--fallback-min-triangles` を知らない）。印に AssetCook の内容が含まれるので、Debug 版で焼いたことで他の一覧も再クックされた（`cooked=125`）。通常のビルドが使う版で次に焼くとまた再クックになる。

### SCN-GEN-PIPELINE（2026-10-10、反復 18・run 20261009-233224）

- 評価者の差し戻し 2 点に対応。(1) `--verify` が定義と manifest の `id`・`usage` を比べていなかったので、比較項目に足した。定義の `usage` を書き換えたとき、manifest の `id` を書き換えたとき、それぞれ不一致を報告することを確かめた（`verify-SCN-GEN-PIPELINE-18-mutation.txt`。manifest は元に戻した）。(2) 生成した GLB の高さが約 1.0（元の単位）のままで、定義の 0.2 m が反映されていなかった。`fit_glb_height` を足し、GLB 保存の直後・manifest 作成の前に、頂点位置を原点まわりに一様に拡縮して Y 方向の高さを `height_m` に合わせる（POSITION の実データと min/max を書き換え、JSON チャンクの長さは変えない。ノードの変換・スキン・アニメーションがあるときは失敗にする）。`--verify` は GLB の位置の範囲から高さを求め、定義の `height_m` と 1% 以内で一致することも確かめる。
- 既存のバナナは、GPU を使わない新オプション `--refit` で合わせ直した（倍率 0.199654、2 回目は 1.0 で冪等）。頂点の実データから独立に求めた高さは 0.200000003 m。プレビューを描き直し、`preview_side.png` を開いて反ったバナナの形と黄色い色が崩れていないことを確かめた。クックは banana の 1 件だけ再クックされた（`cooked=1 skipped=124`）。
- 証拠（`.harness/runs/20261009-233224/`）: `verify-SCN-GEN-PIPELINE-18-before.txt`（合わせ直し前の `--verify` が高さの不一致で exit 1）、`-18-refit.txt`、`-18-mutation.txt`、`-18-preview.txt`、`-18-cook.txt`（`COOK_ASSETS cooked=1 skipped=124`、EXIT=0）、`verify-SCN-GEN-PIPELINE-18.txt`（`--verify --require-cooked --ids banana` が `VERIFY_OK id=banana cooked`・EXIT=0）。
- Notes: 反復 17 の「実寸への倍率は取り込む側で掛ける」は取り下げ。GLB 自体が実寸（高さ 0.2 m、中心は原点）になったので、SCN-BANANAS-SCENE は倍率なしで置ける。`manifest.json` の `bounds_min/max` も実寸になった。

### SCN-BANANAS-SCENE（2026-10-10、反復 20・run 20261009-233224）

- `mega-bananas` シーン（`Game/GameModes/MegaBananas/`。モード・ルーチン・情報表示）を足した。昼の物理空（太陽高度 35°）と太陽の影の下、一辺 160 m の木の床（Poly Haven の brown_planks_03、1.5 m で 1 枚）に、生成したバナナを MegaGeometry で `--banana-count=<n>`（既定 4096・上限 4096）本敷く。全インスタンスが 1 つの Model（MegaMesh）を共有し、向き（ヨー）・大きさ（0.92〜1.08）・位置をばらす。バナナはローカル Y が長手・X が太さなので、Z 軸まわりに 90° 倒してから床へ接地させ、手前に 3 つの山（底 7×7 から 6 段）を置いて残りを手前の端から奥へ升目で敷く（山の下は空ける）。バナナの材質は生成 GLB の内蔵画像（`.img0/1/2.png` = ベースカラー・ORM・法線）。
- 視点は `overview`（既定）・`close`（手前の山へ寄る）・`low`（床すれすれ）、カメラパスは山を囲む周期 20 秒の一周（P キー・パッドの Y）。ランチャーに載せ、資産（クック済みの GLB と内蔵テクスチャ）が無いときは警告を出して床だけで続け、ランチャーのカードに `python Scripts/GenerateSceneModels.py --ids banana` を出す。
- H キーの情報表示（ImGui の半透明パネル）: 本数・うち山に積んだ本数・1 本の三角形数（99,722）・元の三角形の総数（4096 本で 408,461,312）・1 本のクラスタ数（2,105・10 段）。開発ビルドは MegaGeometry の GPU 時間と描いたクラスタ数も出す。GPU 時間はエンジンの計測が動いているとき（`--trace` 付きの起動）だけ取れ、無いときは「--trace を付けて起動すると出ます」と出す。描いたクラスタ数は、エンジンが統計をログ（`MEGA_OCCLUSION`）へだけ出し公開口を持たないので、`Logger::AddSink`（開発ビルドだけ）でその行の `pass1 + pass2_drawn` を読む（約 30 フレームごとの更新）。起動オプション `--open-scene-info`（開発ビルドだけ）で、表示を出した状態から始められる（撮影でも ImGui を重ねる）。
- 周回スモークで見つけた不具合: バナナのシーンを出てランチャーへ戻った直後にアクセス違反（`ucrtbased.dll`）。WER のダンプ（`%LOCALAPPDATA%\CrashDumps\Game.exe.*.dmp`）の例外スレッドのスタックを読むと、`ShadowMapPass::Execute` → `AppendMegaShadowLevel` → `snprintf` で、`GetReadyMegaMeshGPUData` が返した生のポインタ（`DebugName` を読む）が、Cleanup のモデル解放で消えた後の領域だった。描画スレッドが 4096 本ぶんの影を描く間（Debug では 1 フレームが約 0.5 秒）にゲームスレッドがモデルを返す競合。Rendering3DTest と同じく、Leave で `RenderWorld::WaitForRender()` を呼んで Cleanup の前に描画中のフレームを完了させて解消した。エンジン側の根本（`GetReadyMegaMeshGPUData` が生のポインタを返す・ロックの外で読む）は `Library/` で `paths:` の外のため触っていない。
- 証拠（`.harness/runs/20261009-233224/`）: `verify-SCN-BANANAS-SCENE-1.txt`（Debug の Game ビルド EXIT=0）、`-2.txt`（`CaptureScene.ps1 -Scene mega-bananas -RequireAssets` 3 視点 pass。平均輝度 default 121.027・close 123.239・low 109.206）、`-3.txt`（周回スモーク 3 周 `result=pass`。3 周とも textures=6・texture_bytes=134217804 で同じ）、`-4.txt`（`-Views close -GameArgs open-scene-info` 情報表示の撮影 pass）、`-6.txt`（`-GameArgs open-scene-info,trace,...` で GPU 時間 2.93 ms の撮影 pass）、`-5.txt`（直前のビルド）。
- 画: 3 視点の PNG（`scene-capture/SCN-BANANAS-SCENE/default.png`・`close.png`・`low.png`）を開いた。overview は手前から奥へ升目に並んだバナナと 3 つの山、床の板目、遠景の物理空。close は山の斜面を数十 cm から見て、反り・茎・先端の傷・皮の筋が出ていて（高ポリの形）、1 本ごとに床へ影が落ちる。low は床すれすれでバナナの断面（先端の切り口）まで見え、影が長く伸びる。情報表示の撮影（`scene-capture/SCN-BANANAS-SCENE-info/close.png`・`-trace/close.png`）は左上のパネルに 4,096・417・99,722・408,461,312・2,105（10 段）・GPU 時間・描いたクラスタ数 90,717 が出る。
- Notes: 山の高さは物理で積まず 1 段 5 cm の升目なので、山の上のバナナの重なりは近景で少し食い込んで見える所がある（遠景は自然）。`Test/Game/Bridge` の `NorvesLibBridgeAdapterScriptComponentTest` は今回の変更と無関係な既存のリンク不足（`DogMovementSmokeRoutine`・`GameInputSettings`・`SequenceFrameCapture`・`M9SkeletalPreparation` などの未解決。バナナ関連は無し）で、このターゲットのビルドは Game と別に直す必要がある（`MegaBananasRoutine.cpp` は両テスト束のソース一覧に足してある）。ランチャーのサムネイル（`Assets/Launcher/Thumbnails/mega-bananas.png`）は他のシーンと同じく未配置。SCN-MEGA-10K で上限を 1 万へ上げる。

### SCN-BANANAS-SCENE（2026-10-10、反復 21・run 20261009-233224）

- 評価者の差し戻し（向きのばらしと接地・重なり）に対応。原因は `SpawnBanana` の回転の合成 `turn * roll`。描画側は行ベクトル規約（四元数は逆回転として点へ作用する）なので、先にヨーが掛かって長手（ローカル Y）に何も起きず、そのあと全個体が同じ向きへ倒れ、乱数の角度が反りを上下へ回していた（床へ食い込む）。`roll * turn` へ直し、理由をコメントに残した。
- 検算（`verify-SCN-BANANAS-SCENE-21-geometry.txt`）: 生成 GLB の頂点 59,755 個に、描画の規約（共役の回転）で 5 つのヨーを掛けた高さの範囲。`roll * turn` は最下点 -0.027373 m（= 太さの最大 x。コードの持ち上げ量と一致するので、沈み込み 2 mm だけで床に接する）、旧順序は ±0.07 m で床を貫く。
- 証拠（`.harness/runs/20261009-233224/`）: `verify-SCN-BANANAS-SCENE-21-1.txt`（Debug の Game ビルド EXIT=0）、`-21-2.txt`（`CaptureScene.ps1 -Scene mega-bananas -RequireAssets` 3 視点 pass。平均輝度 default 121.535・close 122.22・low 112.512）、`-21-3.txt`（周回スモーク 3 周 `result=pass`）。
- 画: 再撮影した 3 視点を開いた。overview は 1 本ごとに向きが違うバナナが升目に並び、3 つの山が見える。close は手前のバナナが床に横たわって影を落とし、山の斜面は食い込みの見えない積み方になった（反・茎・先端の傷も出る）。low は床すれすれで、バナナが板の上に載っている。
- Notes: 山は物理で積まず 1 段 5 cm の升目なので、近景で隣と軽く触れる所はある（太さ約 5.5 cm に対し段差 5 cm）。ランチャーのサムネイルは未配置のまま。
### SCN-MEGA-10K（2026-10-10、反復 23・run 20261009-233224）

- バナナ 1 万本で描かれないクラスタ・影の欠け・溢れが無いようにした。`--banana-count` の既定を 10000、上限を 65536 にした（`MegaBananasMode.h`・`GameApplicationHandler.h`）。
- 捨てた数・省いた数を開発ビルドのログに出す: (1) `MEGA_CAPACITY draw_dropped=… max_draw_count=… command_capacity=… visibility_bytes=… indirect_bytes=… draw_info_bytes=… bvh_queue_bytes=… sw_raster_bytes=…`（`MegaGeometryPass`。`cluster_cull.comp` の統計 `stats[11]` = 区間の描画コマンドの上限 `MaxDrawCount` を超えて積めなかった可視クラスタ。両パスの合計）、(2) CSM に切り替えたときの `csm_mega_omitted=c0,c1,c2,c3`（UBO の枠が足りず影を描けなかった MegaGeometry の数）、(3) VSM は既存の `VSM_RASTER … overflow=`・`VSM_MEGA_CULL … overflow=`・`VSM_PAGES … overflow=`。
- 0 にならなかった上限を上げた: `MegaGeometryPass::Settings::MaxDrawCount` 262144 → 1048576（1 万本の見渡しの 1 パスは約 30 万クラスタ。確保は材質の全クラスタ数との小さい方なので小さなシーンでは増えない。環境変数 `NORVES_MEGA_MAX_DRAW_COUNT` で替えられる）、VSM の展開のインスタンスの容量 `RASTER_INSTANCE_CAPACITY` 2^20 → 2^21（最初の約 30 フレームに最大約 128 万。旧容量では溢れた）、VSM の MegaGeometry のカリングの出力 `MEGA_CULL_LIST_CAPACITY` 524288 → 1572864、CSM の UBO の枠の上限 `MAX_OBJECTS_LIMIT` 4096 → 65536 × カスケード数（使った分だけ増える）。
- 証拠（`.harness/runs/20261009-233224/`）: `verify-SCN-MEGA-10K-7.txt`（Debug の Game ビルド EXIT=0）、`-9.txt`（`CaptureScene.ps1 -Scene mega-bananas -RequireAssets -GameArgs banana-count=10000` 3 視点 pass。平均輝度 default 122.964・close 122.516・low 109.816、Vulkan 検証のエラー 0）、`-10.txt`（`CaptureStartupScene.ps1` 3 視点 pass。124.046・123.838・125.952）、`-11-stress.txt`（`-StressGeometry -StressGeometryCount 300` 3 視点 pass。121.787・123.19・124.875）。
- ログ（`scene-capture/SCN-MEGA-10K/{default,close,low}.Game.log`）: 3 視点とも `draw_dropped=0`（`max_draw_count=1048576`）、`csm_mega_omitted=0,0,0,0`（`csm_mega_casters=10000`）、`VSM_RASTER overflow` は全行 0（インスタンスの最大 default 1,281,010・close 895,463・low 828,120）、`VSM_MEGA_CULL overflow=0`、`VSM_PAGES overflow=0`。負荷モード 300 個の 3 視点も `draw_dropped=0`・VSM の溢れ 0。MegaGeometry のバッファの大きさ（1 万本）: `visibility_bytes=84,200,000`（4 B × 全インスタンスのクラスタ数 2,105 万。65536 本では約 552 MB に比例して増える）、`indirect_bytes=41,943,040`、`draw_info_bytes=16,777,216`、`bvh_queue_bytes=134,217,728`、`sw_raster_bytes=8,388,640`。
- 画: 3 視点の PNG（`scene-capture/SCN-MEGA-10K/default.png`・`close.png`・`low.png`）を開いた。overview は 1 万本が升目に敷かれ 3 つの山も出て、地面の透けや欠けは無い。close は手前の山と 1 本ごとの影、low は床すれすれの先まで影が出ていて、バナナの欠けは無い。
- Rendering3DTest は撮影の平均輝度が基準（反復 14 の `startup-capture/SCN-FREE-CAMERA`: 124.024・123.744・125.939。実行間の揺れは 0.1 程度）と一致し、`--stress-geometry=300` も VTG5 の記録（121.844・123.211・125.014）と 0.15 以内で一致した。画素単位の比較（PSNR）は撮っていない。
- Notes: 可視ビットは 1 クラスタ 1 bit に詰めていない（1 万本では 84 MB で収まるため。65536 本まで上げると約 552 MB）。1 万本を超える本数での確認は SCN-BANANAS-PERF（1万・2万・3万本）で行う。Debug の撮影は 1 視点約 5 分かかり、ランナーの背景実行の既定の 10 分では打ち切られるので、`timeout` を長くして走らせる。

### SCN-MEGA-10K（2026-10-10、反復 24・run 20261009-233224）

- 反復 23 の差し戻し（`CaptureScene.ps1` の再検証が spawnSync ETIMEDOUT）に対応。原因は実装ではなく検証の長さで、Debug の 1 万本は 1 視点が約 4 分かかり、3 視点を 1 コマンドで撮るとランナーの検証の上限 10 分（`VERIFY_TIMEOUT_MS`）を超える。取り残された Game.exe（`low` の撮影中。親の PowerShell ごと）を止め、`TASKS.md` の verify を `-Views overview`・`close`・`low` の 3 コマンドに分けた（出力先は `scene-capture/SCN-MEGA-10K/<視点>/`。実装は変えていない）。
- 証拠（`.harness/runs/20261009-233224/`）: `verify-SCN-MEGA-10K-24-1.txt`（Debug の Game ビルド EXIT=0）、`-24-2.txt`・`-24-3.txt`・`-24-4.txt`（overview・close・low それぞれ pass。平均輝度 122.964・122.516・109.816 で反復 23 と一致）、`-24-5.txt`（`CaptureStartupScene.ps1` 3 視点 pass。124.04・123.749・125.948）。
- ログ（`scene-capture/SCN-MEGA-10K/<視点>/<視点>.Game.log`）: 3 視点とも `MEGA_CAPACITY draw_dropped=0`（全行。`max_draw_count=1048576`）、VSM の `overflow=` に 0 でない行なし、`visibility_bytes=84200000`。
- 画: 3 視点の PNG を開いた。overview は 1 万本の升目と 3 つの山で地面の透けは無く、close は手前のバナナと山に影、low は床すれすれまで影が出て、欠けは無い。
- Notes: 検証コマンドの分割は技術的な調整（paths の内側）。Debug の撮影は 1 視点約 4 分で、1 コマンドに 3 視点を入れるとランナーの 10 分を超える点は、他の重いシーンの verify でも同じ。

### SCN-BANANAS-PERF（2026-10-10、反復 26・run 20261009-233224）

- バナナ 1 万・2 万・3 万本 × overview・close の GPU 時間・MegaGeometry の GPU 時間・描いたクラスタ数・バッファの大きさを、RelWithDebInfo・1920×1080・`-GpuTimingFrames 300` で測り、`Docs/RenderingValidation/SceneLauncherAcceptance.md`（新規）のバナナの節に表で書いた。1 万本 overview の内訳（パスごと）と、直したもの・既知の限界も同じ節に書いた。
- 直したもの（前の反復の作業途中の保存コミットに入っている）: (1) 太陽の影を VSM が持つ間は CSM に MegaGeometry を描かない（1 万本 overview 31.4 → 17.3 ms）、(2) `vsm_mark.comp` が立っているビットへ atomicOr しない（1 万 close の `VsmMark` 11.9 → 4.1 ms）、(3) `MaxDrawCount` 1,048,576 → 4,194,304（2 万・3 万本の捨てたクラスタ 0）。CSM を省く前後で起動画面・バナナの平均輝度は同じ。
- 結果（最新の計測 `vm2`）: フレームの GPU は 1 万 16.99（overview）・16.45（close）ms、2 万 29.6・32.0 ms、3 万 41.0・55.9 ms。1 万本 overview は予算 16.6 ms を 2% 超える（残りは MegaGeometry のカリングとラスタ。LOD の閾値を 1.5 画素にすると 16.47 ms だが、全シーンの LOD が変わるので既定は変えていない）。
- 証拠（`.harness/runs/20261009-233224/`）: `verify-SCN-BANANAS-PERF-1.txt`（RelWithDebInfo の Game ビルド EXIT=0）、`-3.txt`（`CaptureScene.ps1 -GameArgs banana-count=30000` 3 視点 pass、約 261 秒）、`-vm2-<本数>-<視点>.txt` 6 本（最新の計測）、`-base-*`（変更前）・`-fix1-*`（CSM の省略と上限）・`-eq-*`（見た目の同一確認）・`-lod1.5-`・`-lod2-`・`-rerun1/2-30000-close`。
- 画: 1 万・2 万・3 万本の overview と close、3 万本の low を開いた。穴・欠け・影の欠けは無い。
- Notes: CPU のフレームが GPU より長く、本数で線形より速く伸びる（1 万 72 ms・3 万 579 ms）。内訳はトレースのスコープに無く未調査。別件として `TASKS.md` に SCN-MEGA-CPU-FRAME を追記した。3 万本の GPU 時間は CPU 律速のため計測ごとに揺れる（close 45.7〜57 ms）。

### SCN-BANANAS-PERF 差し戻し対応（2026-10-10、反復 27・run 20261009-233224）

- 反復 26 の差し戻し（VRAM の記録）に対応。`SceneLauncherAcceptance.md` のバナナの節で、(1) ジオメトリのプールの使用量を、読み込み完了後の最終 `VRAM_LEDGER geometry_pool` の値（overview 3.9 MiB・close 5.7 MiB、容量 256 MiB、確保数 30・46。1 万・2 万・3 万本とも同じ）の表に差し替え、(2) 「ソフトウェアラスタの一覧 32 MB」を別の確保として数えていた二重計上を直し、`sw_raster_bytes` と同じ `m_SwRasterBuffer` で合計に含まれると明記した。
- 変更は文書だけ（実装・計測値は変えていない）。GPU 時間・クラスタ数の表は反復 26 の `verify-SCN-BANANAS-PERF-vm2-*.txt` のまま。6 条件のログ（`scene-capture/SCN-BANANAS-PERF/vm2-<本数>-<視点>/<視点>.Game.log`）の最終 `VRAM_LEDGER geometry_pool` を開いて確かめた。verify（ビルド・3 万本の撮影）は実装を変えていないので重い再撮影はせず、反復 26 の保存出力を根拠にする。

### SCN-PH-WASTELAND（2026-10-10、反復 28・run 20261009-233224）

- `FetchPolyHavenModels.ps1`・`FetchPolyHavenTextures.ps1` に `-Set startup|wasteland`（既定 `startup`）を足した。引数なしの組は従来どおり（モデル 3 種 15 ファイル、地面の区画 6 種 30 ファイル。再実行で全て MD5 一致のため飛ばした）。`-Set wasteland` は 4K・MD5 照合・git 管理外で同じ形。
- 荒野のモデル 7 種（Namaqualand コレクション）: `namaqualand_boulder_02`・`_03`・`_04`・`_05`・`namaqualand_cliff_01`・`_02`・`dead_quiver_trunk`。テクスチャ 5 種（色・法線・粗さ・AO・高さ）: 砂 `sandy_gravel_02`、乾いた土 `dry_ground_01`、岩肌 `rock_face`、石の床 `rock_tile_floor`・`stone_floor`。選んだ理由は `Docs/Plans/SceneLauncherPlan.md` の資産の節（git 管理外）に書いた。
- 当初の `namaqualand_boulders_01` は glTF に 2 メッシュあり、AssetCook（NVMESH は 1 メッシュだけ）が `requires exactly one glTF mesh` で落ちたので `namaqualand_boulder_04` に替えた（ディスクの旧ファイルも消した）。
- クックの一覧 `Assets/AssetSets/WastelandModels.json`（`Cooked/WastelandModels`、モデル 7・テクスチャ 21）を足し、`Scripts/CookAssets.ps1` で通した（`COOK_ASSETS cooked=7 skipped=146`、パッケージ 28 件がマニフェストに入った）。
- 証拠（`.harness/runs/20261009-233224/`）: `verify-SCN-PH-WASTELAND-5.txt`（`-Set wasteland` モデル `result=pass`・EXIT=0。最終の 7 種）、`-2.txt`（`-Set wasteland` テクスチャ `result=pass downloaded=25`・EXIT=0）、`-4-default.txt`（既定の組と再実行の結果）、`-6-cook.txt`（クック EXIT=0）。`-1.txt` は差し替え前の 7 種（`boulders_01` 入り）の取得、`-3-cook.txt` は 1 メッシュでない失敗の記録。
- 画: `ph-wasteland-preview/contact_front.png`（Blender の `BlenderPreviewGlb.py` で 7 種を描いた）を開いた。茶・黄土・赤みの岩と橙の地衣類の岩、積み重なった岩の崖、層状に割れた崖の壁、白く風化した枯れ幹で、乾燥地に合う。
- Notes: `CookAssets` という CMake ターゲットはこのビルドツリーに無く、`Scripts/CookAssets.ps1 -AssetCookExe build/Tools/AssetCook/Debug/AssetCook.exe` で呼ぶ（RelWithDebInfo の `AssetCook.exe` は 10/5 のままで古く、GLB を読めずバナナで落ちる。差分クックの印が Debug 版のものなので Debug 版を使う）。`build/CookedAssets` には外した `boulders_01` のテクスチャ 3 件がマニフェスト外で残っている（害は無い）。

### SCN-R3D-REENTRY（2026-10-10、反復 6・run 20261010-132327）

- SCN-RHI-UPLOAD-THREAD（スレッドごとのコマンドプール）が入ったので、検証レイヤーの除外なしで周回スモークを再検証した。実装の変更は無い（`RunSceneCycleSmoke.ps1` は `Validation Error:` を 1 件でも失敗にする）。
- 証拠（`.harness/runs/20261010-132327/`）: `verify-SCN-R3D-REENTRY-1.txt`（Debug の Game ビルド EXIT=0）、`-2.txt`（`RunSceneCycleSmoke.ps1 -Scenes rendering3dtest -Cycles 3` が `result=pass`・EXIT=0。3 周とも materials=0 textures=0 meshes=0 models=0 entities=2 buffers=0 で一致、`geometry_pool_capacity_bytes=268435456`）、`-3.txt`（`CaptureStartupScene.ps1` 3 視点 pass。平均輝度 124.09・123.716・125.968）。
- 確認: `.harness/runs/scene-cycle-smoke/Validation.log` は 0 バイト（検証エラー 0 件。ファイルはあるので検証レイヤーは動いている）。`default.png` を開いて天球・地面・球・岩・小屋・電球が欠けないことを確かめた。
- Notes: `blocked/SCN-R3D-REENTRY.md` の理由（共有コマンドプールの競合）は解消した。競合は時間依存なので 1 回の pass は保証ではない（反復 5 の記録と同じ）。
### SCN-PAUSE-MENU 差し戻し対応（2026-10-10、反復 2・run 20261010-132327）

- 評価者の指摘（Start のラッチが `event.Slot` を見ずに解除され、別スロットの未押下サンプルで押し続けた Start が再押下に見えて閉じる）を直した。`PauseMenu` のラッチをスロット別のビット（`m_StartLatchedMask`、スロット数は `GamepadSlotCount`）にし、同じスロットの解放通知・サンプルだけで外す。範囲外のスロットは無視する。
- `Test/Game/Bridge/PauseMenuTest.cpp` に `TestStartLatchIsPerSlot` を足した（slot 0 の Start 押下 → slot 1 の未押下サンプル → ReleaseAll → slot 0 の押しっぱなしサンプルでも閉じない／別スロットの解放でも外れない／同じスロットの未押下サンプルの後は押し直しで閉じる）。`PauseMenuTest passed`・EXIT=0。
- 証拠（`.harness/runs/20261010-132327/`）: `verify-SCN-PAUSE-MENU-1.txt`（Debug の Game ビルド EXIT=0）、`-2.txt`（`CaptureScene.ps1 -GameArgs open-pause-menu` が `result=pass`）、`-3.txt`（`RunSceneCycleSmoke.ps1 -Scenes rendering3dtest -Cycles 3` が `result=pass`、3 周とも entities=2 で一致）、`-4.txt`（`CaptureStartupScene.ps1` 3 視点 pass）。
- Notes: 回帰の検査は `PauseMenu` を直接呼ぶ形（InputSystem→Router の経路は通していない）。ルーターは `event.Slot` をそのまま渡す前提。

### SCN-PAUSE-MENU 差し戻し対応・実配送経路の回帰検査（2026-10-10、反復 3・run 20261010-132327）

- 反復 2 の差し戻し（回帰検査が `PauseMenu` を直接呼ぶだけで、`InputSystem → Router → PauseMenu` を通っていない）に対応。実装（スロット別ラッチ）は前の反復のまま。
- `Test/Game/Bridge/PauseMenuTest.cpp` に `TestStartHeldThroughInputSystem` を足した。実物の `InputSystem` と `InputRouter` に `PauseMenu` を登録し、`InjectGamepadState` と実際の `ReleaseAll()` で次の順を確かめる: slot 0 の Start 押下で開く → 同じフレームの slot 1 の未押下サンプルでも閉じない → `ReleaseAll()` ＋ slot 0 の押しっぱなしサンプルを 3 フレーム繰り返しても開いたまま → slot 0 を離しただけでは閉じない → 押し直しで閉じる。`Test/Game/Bridge/CMakeLists.txt` で `PauseMenuTest` を `Core` にリンクした（新しい実行ファイルは増やしていない）。
- 検査が効くことの確認: `SetStartLatched` の解除を全スロット共通（`m_StartLatchedMask = 0`）に一時的に戻すと、新旧の検査が 5 件失敗し EXIT=1。元に戻して再ビルドし `PauseMenuTest passed`・EXIT=0。
- 証拠（`.harness/runs/20261010-132327/`）: `verify-SCN-PAUSE-MENU-1-test.txt`（`PauseMenuTest passed`・EXIT=0）、`-1.txt`（Debug の Game ビルド EXIT=0）、`-2.txt`（`CaptureScene.ps1 -GameArgs open-pause-menu` が `result=pass`）、`-3.txt`（`RunSceneCycleSmoke.ps1 -Scenes rendering3dtest -Cycles 3` が `result=pass`、3 周とも entities=2 で一致）、`-4.txt`（`CaptureStartupScene.ps1` 3 視点 pass。平均輝度 124.067・123.762・125.946）。
- 画: `scene-capture/SCN-PAUSE-MENU/default.png` に暗幕と中央のパネル（続ける・ランチャーへ戻る・ゲームを終了）、`startup-capture/SCN-PAUSE-MENU/default.png` はメニューの出ない通常の見た目（天球・地面・球・岩・小屋・電球が欠けない）。
- Notes: 実機パッドでの確認はしていない（検証は `InjectGamepadState` による注入）。

### SCN-BANANAS-SCENE 差し戻し対応（2026-10-10、反復 5・run 20261010-132327）

- 反復 21 の差し戻し（山の上段が下段に支えられず浮く）と、親が overview で見た「デバッグっぽさ」を直した（実装は前の作業途中の保存コミットに入っている。この反復で撮り直しと検証を通した）。
- 山: 物理を使わない落とし込み。山の範囲に高さの格子を持ち、置いたバナナの変換後の頂点の最大の高さを書き込み、次のバナナは向きを決めたあと自分の下面の点が格子に初めて触れる高さに置く（少しめり込む）。山は中心から正規分布で落とす塚の形にし、3 つの山は各 260 本。
- 敷き詰め: 位置をポアソン円盤（最小間隔 0.25 m）で崩し、升目に見えない。床は 1600 m 四方に広げ、地平線の手前に木の床の端が出ない。
- 隙間の記録（`Scripts/VerifyBananaPile.py`、`verify-SCN-BANANAS-SCENE-9-pile-gap.txt`）: 生成 GLB の全頂点を描画の規約で変換し、水平 5 mm の格子で、各バナナの最下点と自分より前に置いた形（と床）の最大の高さの差を測った。3 つの山の 780 本で、浮いているもの 0（許容 5 mm 超なし）、支えあり 777、食い込み 3（最大 -28.4 mm。許容 -7 mm を超える。見た目では下段への軽い食い込み）。他のバナナに載っているもの 692、床だけに載るもの 88。山の頂点は床から 0.467〜0.474 m。
- 証拠（`.harness/runs/20261010-132327/`）: `verify-SCN-BANANAS-SCENE-9-build.txt`（Debug の Game ビルド EXIT=0）、`-9-overview.txt`・`-9-close.txt`・`-9-low.txt`（`CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Views <視点>` が各 pass。平均輝度 123.489・120.544・106.031、検証レイヤーのエラー 0）、`-9-pile-gap.txt`（上記）、`-10-cycle.txt`（`RunSceneCycleSmoke.ps1 -Scenes mega-bananas -Cycles 3 -GameArgs banana-count=1024` が `result=pass`。3 周とも textures=6・entities=2・texture_bytes=134217804 で一致）。
- 画: 3 視点の PNG（`scene-capture/SCN-BANANAS-SCENE/{overview,close,low}/*.png`）を開いた。overview は敷き詰めが升目に見えず、3 つの塚が下段から積み上がり、床は地平線まで続く。close は山の各段が下の段に載り、1 本ごとに影が落ちる。low は床すれすれで 2 つの塚と床の影が出て、欠けは無い。
- 検証コマンドの調整（paths の内側）: Debug の撮影は 1 視点が約 3〜4 分で、1 コマンドに 3 視点を入れるとランナーの検証の上限 10 分を超えるため、`TASKS.md` の verify を視点ごとの 3 コマンドに分け、隙間の検算（`VerifyBananaPile.py`）を足した。周回スモークは既定の 1 万本だと 1 周が約 5 分（Debug・検証レイヤー付き）で 3 周が 15 分を超えるので、`RunSceneCycleSmoke.ps1` に `-GameArgs` を足し `banana-count=1024` で回す（資源の増減の検査なので本数には依存しない）。既定 1 万本の 1 周目は `settled=1 frames=196` で正常に終わるのを確かめた。
- Notes: 食い込み 3 本は許容を超えるが、浮きは 0 で山の見た目に影響しない（格子の刻みが 1〜2 cm のため、細い茎の先などで最大 28 mm）。
### SCN-GEN-BUILDINGS-1(2026-10-10、反復 6・run 20261010-132327。途中。adobe_ruin が未生成)

- `Scripts/GenerateSceneModels.py` に上限時間とやり直しを足した。定義ごとの `upsample_resolution`（既定 1536 を `Trellis2UpsampleStage`（ノード 94）の `target_resolution` へ渡す。manifest にも記録）、1 体あたりの上限 `--model-time-limit`（既定 900 秒。超えたら `/interrupt`、止まらなければ自分で起動した ComfyUI を作り直し、`upsample_resolution` > 1024 なら 1024 で 1 回だけやり直す。やり直しも超えたら失敗として次の id へ）、1 回の実行の合計 `--run-budget`（既定 1800 秒、起動を含む。残り 180 秒未満なら見送り）、manifest が定義と一致する id は飛ばす（`--force` で作り直す）。時間切れの動作は `adobe_ruin` で実際に確かめた（`interrupt_sent` → `restart_after_interrupt` → 失敗して終了）。
- 生成できたもの: `watchtower`（前の反復。59.9 万面・高さ 9.0 m）、`stone_arch`（今回。59.97 万面・テクスチャ 4096・高さ 5.0 m、276 秒）。プレビューを開いた: 見張り塔は 4 本脚・筋交い・足場・小屋・ハシゴ側の梁が揃い底は地面に立つ。石のアーチは 2 本の柱とアーチと平らな基壇で、浮いた破片・底の抜けは無い。
- 未完: `adobe_ruin` は `upsample_resolution` 1024 でも 900 秒を超えて終わらなかった（`.harness/runs/20261010-132327/verify-SCN-GEN-BUILDINGS-1-gen1.txt`。ComfyUI のログは再起動で上書きされ、止まった段は特定できていない）。定義を 768 に下げた（未検証）。次の反復は `python Scripts/GenerateSceneModels.py --ids adobe_ruin --reuse-reference` から（参照画像は `adobe_ruin/reference.png` に有り。壁と屋根が崩れた日干し煉瓦の家で形は良い）。それでも終わらなければ seed かプロンプト（壁面を単純に、屋根の梁を減らす）を変えて参照画像から作り直す。生成後に `--preview` で形を確かめ、`Scripts/CookAssets.ps1 -AssetCookExe build/Tools/AssetCook/Debug/AssetCook.exe` でクックして verify を通す。
- クックの一覧 `Assets/AssetSets/WastelandBuildingsModels.json`（3 種）を足した。クックは `adobe_ruin` の GLB が無いので未実施。
- `TASKS.md` の status は `todo` のまま（`done` の条件を満たしていない）。`TASKS.md` には親の未コミットの変更があるため、この反復では触っていない。

### SCN-GEN-BUILDINGS-1 完了(2026-10-10、反復 7・run 20261010-132327)

- 前回の差し戻し3点に対応。(1) `--refit` が `write_manifest` を引数不足で呼ぶ退行: manifest に記録済みの `upsample_resolution`(無ければ定義・既定)を渡すようにした。(2) 合計の上限を超える経路: `recover_after_timeout` は待ち時間を引数で受け、残り時間で絞る(止めたら True を返すだけで、再起動は呼び出し側が `残り - 180 秒` までの起動待ちで行い、残りが無ければ見送る)。ComfyUI の初回起動待ちも残りで絞る。仮想時計での再現は 900＋復旧待ち＋再起動＋715 秒で 1805 秒(予算 1800 秒＋停止処理)に収まった。
- `upsample_resolution` の最小値は 1024(768 は ComfyUI が `Value 768 smaller than min of 1024` で拒否する。`verify-SCN-GEN-BUILDINGS-1-gen2.txt`)。`adobe_ruin` は 1024 の元のプロンプトでは 900 秒で終わらないため、プロンプトを壁面が厚く単純な形(上の角だけ階段状に欠け、扉と窓は各 1 つ)に、seed を 2011 に替えて参照画像から作り直した。結果は 3D 化が約 280 秒(参照画像の生成を含む実行全体 822 秒)、59.98 万面・テクスチャ 4096・高さ 3.5 m。
- プレビュー(`Assets/Models/Generated/adobe_ruin/preview_{front,side,top}.png`)と参照画像を開いた: 厚い壁の平屋根の家で、扉の開口が保たれ、壁の抜け・浮いた破片は無く、底はほぼ平ら。屋根の梁の先が小さく出る以外に突起は無い。見張り塔・石のアーチは前の反復で確認済み。
- クック: `Scripts/CookAssets.ps1 -AssetCookExe build/Tools/AssetCook/Debug/AssetCook.exe` が `cooked=3`・EXIT=0(`verify-SCN-GEN-BUILDINGS-1-cook.txt`)。`--verify --require-cooked --ids adobe_ruin,watchtower,stone_arch` は 3 種とも `VERIFY_OK ... cooked`・EXIT=0(`verify-SCN-GEN-BUILDINGS-1-final.txt`)。生成の証拠は `-gen2.txt`(768 の拒否)・`-gen3.txt`(成功)。

### SCN-MEGA-METRICS（2026-10-10、反復 8・run 20261010-132327）

- `Scripts/CaptureScene.ps1` の `gpu_timing` を拡張した（C++ は変えていない）。`game_thread_ms_median/_p95`・`render_thread_ms_median/_p95`（トレースの `GameThreadMs`・`RenderThreadMs`。`RenderFrameMs > 0` の行の窓で、GPU と同じく撮影の直前 2 つを除く）、`cpu_frame_ms_p95`、パスごとの median・p95・max（`pass_ms`。従来の `pass_median_ms` は残した）、CPU スコープ（Type=Scope の行）の median 上位 15（`cpu_scope_median_ms`。p95・標本数付き）。`CAPTURE_SCENE gpu_timing` の行に `gt_median_ms`・`gt_p95_ms`・`rt_median_ms`・`rt_p95_ms` を足し、`gpu_pass_p95`（p95/max）・`cpu_scope_median` の行も出す。
- `-CompareWith <dir>` を足した（`-CompareMeanLuminanceLimit` 既定 0.1、`-ComparePsnrLimit` 既定 45）。同じ視点名の PNG を `CAPTURE_SCENE comparison` の行と metrics.json の `comparison` に書き、式は `StartupCaptureMetrics.Compare` と同じ（`SceneCaptureMetrics.Compare` として移した）。両方に gpu_timing があれば GPU・ゲームスレッド・描画スレッドの median の比も `comparison` に入る。
- 証拠（`.harness/runs/20261010-132327/`）: `verify-SCN-MEGA-METRICS-1.txt`（RelWithDebInfo の Game ビルド EXIT=0）、`-2.txt`（バナナ 1 万本 overview の基準 `SCN-MEGA-BASE` が pass。平均輝度 123.246）、`-3.txt`（同じコードの 2 回目を `-CompareWith` で比較: 平均輝度の差 0.0001・PSNR 67.759 dB・不一致の画素 0.027%・pass）、`-4.txt`（起動画面の基準 `startup-capture/SCN-MEGA-BASE`、3 視点 pass）、`-5.txt`（遮蔽 Off の旋回の基準 `SCN-MEGA-ORBIT-OFF`、24 視点 pass）、`-6.txt`（今の遮蔽 On の旋回を Off と `-CompareDeterministicWith` で比較: 24 視点すべて平均輝度の差 ≤ 0.0008・PSNR 59.46〜62.47 dB で pass）、`-7-timing.txt`（`-GpuTimingFrames 300` の実走。`gt_median_ms=1.624`・`rt_median_ms=10.624` などが行に出て pass。1024 本）。
- 判断: バナナの決定的な撮影は 2 回で一致した（PSNR 67.8 dB で下限 45 dB を大きく超える）ので、以後のバナナの比較は画像で行える（`MEGA_OCCLUSION` の数での代替は不要）。旋回の On は Off と一致した（不一致の画素は 8〜17%、最大 8〜27 の差だが PSNR は 59 dB 超で、遮蔽の有無による見た目の差は無い）ため、On の撮影を基準に替える必要は無い。
- Notes: `-GpuTimingFrames` の集計は 30 万行のトレースで約 11 秒（トレースを 2 回読む）。スコープの標本は 1 フレームに複数回記録される区間もあるので、median は記録 1 件ごとの継続時間（フレーム合計ではない）。

### SCN-MEGA-METRICS 差し戻し対応（2026-10-10、反復 9・run 20261010-132327）

- 差し戻し: `cpu_scope_median_ms` が、描画完了を記録した疎な Frame 番号への所属で Scope 行を除いていたため、同じ計測窓の描画スコープが落ちていた（`RenderGraph.Build` は全件欠落、`RenderGraph.Compile` は 241 件中 1 件だけ採用）。Scope 行の Frame は記録時点のゲームフレーム番号で、窓の行の番号とは一致しない。
- 修正: `Scripts/CaptureScene.ps1` の `Get-GpuTimingFromTrace` で、窓の最初〜最後のフレーム番号の連続した範囲に記録された Scope 行を数えるようにした（C++ は変えていない）。
- 既存トレース（`scene-capture/SCN-MEGA-METRICS/timing/overview.trace.csv`）で関数を再実行した結果（`verify-SCN-MEGA-METRICS-8-scope.txt`）: `RenderGraph.Execute` 1.662/1.906 ms、`RenderGraph.Compile` 0.329/0.674 ms、`MegaGeometryPass.RecordFrameCommand` 0.329/0.404 ms、`RenderGraph.Build` 0.003/0.005 ms がいずれも 241 件（窓の 240 フレーム＋境界 1）。評価者の再計算（Compile 0.329/0.674）と一致。
- 実走（`verify-SCN-MEGA-METRICS-8-timing.txt`、`-GpuTimingFrames 300`、1024 本、EXIT=0、pass）: `cpu_scope_median` の行に `RenderGraph.Execute=1.671 MegaGeometryPass.RecordFrameCommand=0.332 RenderGraph.Compile=0.328 RenderGraph.Build=0.003` が出る。gt/rt の median・p95 も行に出る。
- 画像の比較・基準の撮影（verify 2〜6）は集計の経路を通らないので、反復 8 の証拠（`-1`〜`-6`）をそのまま有効とした。

### SCN-MEGA-PROBE-ROOT（2026-10-10、反復 10・run 20261010-132327）

- `--mega-lod-error-px=<px>` を足した（`Game/GameApplicationHandler.cpp`。0 より大きい有限の数だけ。開発ビルドだけ有効で、リリース系では引数自体を扱わない）。値は `MegaGeometryPass` が最初に作られる前に、既存の環境変数 `NORVES_MEGA_LOD_ERROR_PX` へ渡す（パス側の読み取りは変えていない）。指定が無ければ挙動は従来のまま。
- クックの計測用に `Tools/AssetCook/CookMeshDag.cpp` へ、環境変数 `NORVES_COOK_DAG_TRACE=1` のときだけ段ごとの量をログ（`NORVES_LOG_WARNING`。AssetCook は標準出力に出る）へ出す記録（`COOK_DAG level=...`）を足した。クックの結果は変えない。
- 描画の計測（1 万本 overview、RelWithDebInfo、1920x1080、`-GpuTimingFrames 300` の 240 フレーム）。1 クラスタあたりの ns = パスの中央値 ms ÷ `clusters_drawn_last`:

| 閾値 | 描いたクラスタ数 | 1 本あたり | VisRasterSw1 中央値 | VisRasterSw1 の ns/クラスタ | MegaGeometry 全体 ns/クラスタ | gt / rt 中央値 ms |
|---|---|---|---|---|---|---|
| 1 px | 365,355 | 36.5 | 2.434 ms | 6.66 | 22.33（Draw1 4.30・Cull1 4.49・Cull2 4.25） | 72.46 / 30.88 |
| 100000 px（全本が根） | 112,804 | 11.3 | 0.888 ms | 7.87 | 33.29（Draw1 4.56・Cull1 9.75・Cull2 8.44） | 74.61 / 31.17 |

  - 根では描くクラスタが約 1/3.2 になり VisRasterSw1 は 2.74 倍速いが、gt・rt はほぼ変わらない（72.5→74.6、30.9→31.2 ms）。CPU 側（`RenderGraph.Execute` 26.5 ms、`RecordFrameCommand` 2.17 ms）は量に依存しない。GPU 全体は 17.76→12.87 ms（MegaGeometry 8.16→3.76 ms）。
  - 根の Cull1/Cull2 の ns/クラスタが大きいのは、Cull が 1 万インスタンス単位の固定費を持ち、描いた数で割ると高く見えるため。1 クラスタの単価としては VisRasterSw1 が 6.7〜7.9 ns で、段に依らずほぼ一定。
  - 根でも 1 本あたり 11.3 クラスタしか描かれないのは、視錐台・遮蔽で根の 44 クラスタの一部が落ちるためと考えられる（cooked の根は 1 本 44 クラスタ。落ちた内訳は未計測）。1 px でも 1 本 36.5 クラスタで、LOD0（780）には遠く、overview の距離では細かい段はほとんど選ばれない。
- banana のクック（`fallback_min_triangles=4096`、`NORVES_COOK_DAG_TRACE=1`、`verify-SCN-MEGA-PROBE-ROOT-4-cook.txt`）。段ごとのクラスタ数/三角形数/グループ数/次の段の三角形/減少率/安全な簡略化が無く残したグループ数:

| 段 | クラスタ | 三角形 | グループ | 次の三角形 | 減少 | rejected |
|---|---|---|---|---|---|---|
| 0 | 780 | 99,722 | 186 | 51,786 | 48.1% | 7 |
| 1 | 456 | 51,786 | 107 | 27,896 | 46.1% | 8 |
| 2 | 276 | 27,896 | 64 | 16,062 | 42.4% | 8 |
| 3 | 164 | 16,062 | 40 | 11,308 | 29.6% | 8 |
| 4 | 115 | 11,308 | 26 | 8,988 | 20.5% | 8 |
| 5 | 90 | 8,988 | 21 | 7,290 | 18.9% | 8 |
| 6 | 70 | 7,290 | 16 | 6,048 | 17.0% | 7 |
| 7 | 60 | 6,048 | 13 | 4,986 | 17.6% | 6 |
| 8 | 50 | 4,986 | 12 | 4,112 | 17.5% | 6 |
| 9（根） | 44 | 4,112 | 10 | 3,690 | 10.3% | 7 |

  - 段 0 は 780 クラスタ/99,722 三角形、根は 44/4,112 で、GPT の復号と一致。`MESH_COOK dag_levels=10 clusters=2105 rejected_groups=66`。
  - **根で止まった理由は `MinProgressPercent`（15%）**: 段 9 の簡略化で 4,112→3,690 三角形（減少 10.3%）に留まり、`decision=min-progress`（`CookMeshDag.cpp` の `output.NextTriangles * 100 > currentTriangles * (100 - 15)`）。クラスタが 1 つになる条件（`single-cluster`）や段の上限 64 ではない。
  - 減少が鈍る原因は、段 3 以降で毎段 6〜8 グループが「安全な簡略化が無く簡略化せずに残した」（rejected。非多様体の辺を作る出力を除く判定）こと。段 9 では 10 グループ中 7 が rejected で、実質 3 グループ分しか減らない。境界の固定（`ComputeGroupBoundaryLocks`）自体の影響は、この記録からは分けられていない（rejected の数まで）。段 5〜8 の減少率は 17〜19% で閾値 15% のすぐ上にあり、閾値を少し上げ下げすると根の段数が変わる。
- 証拠（`.harness/runs/20261010-132327/`）: `verify-SCN-MEGA-PROBE-ROOT-1.txt`（RelWithDebInfo の Game ビルド EXIT=0）、`-2-px1.txt`・`-2-root.txt`（`CaptureScene.ps1 ... banana-count=10000,mega-lod-error-px=1|100000` が各 pass。`mega_stats` と `gpu_pass_median` はこの行）、`-3-cookbuild.txt`（AssetCook のビルド EXIT=0）、`-4-cook.txt`（クックの段ごとの記録）。撮影の出力は `scene-capture/SCN-MEGA-PROBE-ROOT/{px1,root}/`。
- 反復 11（評価者の差し戻しへの対応）: (1) `--mega-lod-error-px` の検査を含む分岐全体と定数を `#if NORVES_BUILD_DEVELOPMENT` の中へ移した（リリース系では引数を扱わず、従来どおり）。(2) 検査を `std::string`/`strtof` から既存の `TryParseBoundedFloat`（下限 1e-6・上限 1e9）に替え、環境変数へは `snprintf` した固定長バッファで渡す。(3) クックのトレースを `std::fprintf` から `NORVES_LOG_WARNING` に替えた。AssetCook はロガーを初期化せず、フォーマッタが空だとログが捨てられ、コンソールの既定は Warning 以上なので、トレースを有効にしたときだけ標準フォーマッタを 1 回入れ、Warning で出す（Info では 0 行になることを実走で確認した）。再実走: `verify-SCN-MEGA-PROBE-ROOT-11-cook.out.txt` に `COOK_DAG` が 10 行出て、段 0 = 780/99,722、段 9（根）= 44/4,112・減少 10.3%・`decision=min-progress` で前回と同じ。`-11-build.txt`（Game の RelWithDebInfo ビルド EXIT=0）、`-11-cookexebuild.txt`（AssetCook のビルド EXIT=0）、`-11-capture.txt`（`mega-lod-error-px=100000` の撮影が pass。`clusters_drawn_last=112804`、`VisRasterSw1` 0.942 ms ≒ 8.35 ns/クラスタ。前回の 7.87 ns とは run 間の揺れの範囲で、結論は変わらない）。
- Notes: 製品の挙動は変えていない（新しい起動引数と環境変数つきの記録は、指定したときだけ効く）。次の手: 根の段数と rejected を減らす案（MinProgressPercent の見直し、rejected の原因の調査）は別タスクで。

### SCN-MEGA-SETUP-LINEAR（2026-10-10、反復 12・run 20261010-132327）

- `MegaGeometryPass::Setup` の、区画（`m_VisibilityEntries`）ごとに全インスタンスを探していた二重ループを、鍵（`ObjectId`、0 のときは `0x8000000000000000 | インスタンス番号`。`VisibilityRequest::Key` と同じ式）の `Container::UnorderedSet` を 1 回だけ作って引く形に替えた（`MegaGeometryPass.cpp`）。見つからない区画の `LastUsedFrame = 0` は従来どおり。
- 3万本 overview（RelWithDebInfo、1920x1080、240 フレーム、`verify-SCN-MEGA-SETUP-LINEAR-2.txt`）: `RenderGraph.Execute` 中央値 28.545 ms（前は 313.9、基準 ≤ 60）、`RecordFrameCommand` 6.843 ms、`rt` median 40.308 ms。
- 1 万本の同条件（`-2-ref10k.txt`）: `RenderGraph.Execute` 8.646 ms、`rt` median 12.872 ms → `rt` の 3万/1万 比 = 3.13（基準 ≤ 3.5）。
- 比較: バナナ 1 万本は `SCN-MEGA-BASE` と pass（PSNR 65.35 dB、輝度差 0.0001、`-3.txt`）、起動画面の決定的比較は 3 視点とも pass（輝度差 0、PSNR 100、`-4.txt`）。
- Notes: ビルド（`-1.txt`）は PowerShell から実行（Git Bash は `/m:1` をパスに変換して失敗する）。3万本の gt は 548 ms と GameThread 側が重い（別の線形処理が残っている可能性。このタスクの範囲外）。
- 反復 13（評価者の差し戻しへの対応）: 起動画面の比較は `CaptureStartupScene.ps1` の既定の Debug の `Game.exe` を撮るが、直前の Debug は変更前の実装だった（`MegaGeometryPass.obj` が古い）。`cmake --build build --config Debug --target Game -- /m:1` で再ビルドしてから（obj 17:58:40・exe 17:58:49 でソース 17:40:50 より新しい）、起動画面の決定的比較を再実行した。3 視点とも pass（輝度差 0、PSNR 100、不一致画素率は最大 1.1E-05・最大チャネル差 1）。`TASKS.md` の verify に Debug ビルドを足した。証拠: `verify-SCN-MEGA-SETUP-LINEAR-13-debugbuild.txt`（EXIT=0）、`-13-startup.txt`（`result=pass`）。実装コードは変えていない。

### SCN-MEGA-PROBE-100K（2026-10-10、反復 14・run 20261010-132327）

- 変更: `kMaxBananaCount` を 1,048,576 に（`MegaBananasMode.h`）。視点 `horizon`（目の高さ 1.7 m、位置 (0, 1.7, -15)、ヨー・ピッチ 0）を `SceneCatalog.h` の `kMegaBananasViews` に足した。`MegaGeometryPass.cpp` の BVH の段ごとの dispatch に GPU タイムスタンプを足した（`NORVES_ENABLE_STATS` の構成だけ。名前は 1 パス目 `MegaBvhC1S<段>`・`MegaBvhC1Leaf`、2 パス目 `MegaBvhC2S<段>`・`MegaBvhC2Leaf`。段 0 = インスタンスごとの根の判定、Leaf = 葉のクラスタの判定）。開発ビルドの `MegaBananasRoutine.cpp` が、根の球・メッシュの球の半径を `MEGA_BANANAS_SPHERES` でログへ出す。
- 実走の引数: 10 万本はゲームスレッドが 1 フレーム約 7 秒かかり、指定の `-GpuTimingFrames 300` では 300 秒の既定のタイムアウトに入りきらない（初回の実走は 45 フレームで打切り、`verify-...-2.txt`）。そこで `-GpuTimingFrames 120 -TimeoutSeconds 2400`（窓は 60 フレーム）に替え、`TASKS.md` の verify も同じにした。
- 10 万本 horizon（RelWithDebInfo、1920x1080、窓 60 フレーム、`verify-SCN-MEGA-PROBE-100K-3.txt`・`-4-stages.txt`）:
  - 全体: GPU フレーム median 213.7 ms（p95 263.4）、gt median 7,167.6 ms（p95 7,778）、rt median 189.2 ms（p95 233.3）。`RenderGraph.Execute` 135.9 ms、`RecordFrameCommand` 29.5 ms。
  - 描いたクラスタ数 3,880,187（1 本あたり 38.8）、`VisRasterSw1` 35.75 ms（9.2 ns/クラスタ）、`MegaGeometry` 139.5 ms、`VirtualShadowMapPass` 64.7 ms。draw_dropped 0。
  - BVH の段（median / p95、ms）: 1 パス目 S0 4.115/7.509・S1 2.220/4.047・S2 2.407/4.406・S3 1.912/5.524・Leaf 26.328/34.238（Cull1 全体 36.56）。2 パス目 S0 4.012/6.989・S1 1.583/3.628・S2 1.408/4.231・S3 1.591/5.112・Leaf 18.846/31.761（Cull2 全体 30.27）。
  - 球の半径（バナナのローカル、`MEGA_BANANAS_SPHERES`）: 根の節 0.1818、メッシュ全体 0.1032、根のクラスタの最大 0.1800、LOD 用の共通球は無効（0）、BVH の節 564。根の節がメッシュの球より大きいのは、下のクラスタと親の球をすべて包むため。
  - 他: `bvh_queue_bytes` 1,073,741,824（1 GiB）、`visbuffer_records_mb` 1024、`MEGA_BVH` は 10 万インスタンス・levels=4・葉の容量 49,100,000。
- **範囲（セル）の判定が要るか: 要る。** 段 0 は 1 パスあたり 4.0〜4.1 ms（2 パス合計 8.1 ms）で、基準の 0.5 ms の約 8 倍。段 0 は 10 万インスタンスの根の判定を全部やり直すため、本数に比例する。さらに段 1〜3 と Leaf を含む BVH の判定は 1 パス目で 36.6 ms、2 パス目で 30.3 ms と、MegaGeometry 全体（139.5 ms）の約 48%。視点の外の本数を、セル単位で先に落とす判定が要る。
- 補足: ゲームスレッドの 7 秒/フレームは今回の変更によるものではない（変更はログとタイムスタンプだけ）。3 万本で 548 ms だった gt が 10 万本で 7 秒になっており、本数に対して線形より重い。次のタスクの対象の候補。水平線の先に、敷き詰めの端（奥行き約 70 m）の外の床が帯で見える。
- 証拠（`.harness/runs/20261010-132327/`）: `verify-SCN-MEGA-PROBE-100K-1.txt`（RelWithDebInfo の Game ビルド EXIT=0）、`-2.txt`（300 フレームの初回。300 秒で打切り）、`-3.txt`（実走。pass・EXIT=0）、`-4-stages.txt`（metrics.json からの段ごとの表）。撮影の出力は `scene-capture/SCN-MEGA-PROBE-100K/`（`horizon.png`・`metrics.json`・`horizon.Game.log`）。

- 反復 15（再検証の ETIMEDOUT への対応）: 前回の再検証が `spawnSync cmd.exe ETIMEDOUT` で落ちたのは、撮影が約 19 分（ゲームスレッド約 7 秒/フレーム × 120 フレーム＋読み込み）かかり、ランナーの 1 verify の上限 10 分に入らないため。`-GpuTimingFrames` の下限は 100 でもゲームスレッドだけで 700 秒を超え、コマンドの引数では 10 分に収められない。そこで撮影は反復の中で実走し、`TASKS.md` の verify は撮影が書いた `metrics.json`（失敗なし・view=horizon・描いたクラスタ数・gt/rt・BVH の段の各パスの有無）を検査する形に替え、撮影コマンドは notes に残した。打ち切られた再検証が残した Game.exe（PID 20788）は止めた。
- 撮り直し（`verify-SCN-MEGA-PROBE-100K-5.txt`、pass・EXIT=0、段の表は `-5-stages.txt`）。前回（反復 14）と同じ傾向で、数値は run 間で揺れた: GPU フレーム median 185.6 ms（p95 218.9）、gt median 6,875 ms、rt median 247.6 ms、`RenderGraph.Execute` 179.1 ms、`RecordFrameCommand` 38.6 ms、描いたクラスタ数 3,880,187、`VisRasterSw1` 23.6 ms、`MegaGeometry` 115.9 ms。BVH の段（median ms）: 1 パス目 S0 4.652・S1 2.395・S2 2.124・S3 2.025・Leaf 19.631、2 パス目 S0 4.477・S1 2.467・S2 1.681・S3 1.357・Leaf 16.038。球の半径は前回と同じ（根の節 0.1818・メッシュ 0.1032）。**結論は変わらない: 段 0 は 1 パス 4.5 ms 前後で 0.5 ms の約 9 倍、範囲（セル）の判定が要る。** 前の節の数値は反復 14 の実走のもの。
- 検査の証拠: `verify-SCN-MEGA-PROBE-100K-6-check.txt`（`CHECK pass clusters=3880187 gt=6875.28 rt=247.616`・EXIT=0）。

### 計測の運用の見直し（2026-10-10、親）

- 古い経路（1 本 1 Entity）の 10万本の撮影は 1 回約 19 分で、ランナーの再検証（上限 10 分）が必ず時間切れになり、SCN-MEGA-PROBE-100K が 3 反復続けて撮り直していた。計測の結果は反復 15 の評価者が確認済みなので、親が完了にした。
- 計測の項目の verify を、反復の中で 1 回撮った結果を検査する `Scripts/CheckCaptureMetrics.py`（結果が揃い、失敗・溢れが無く、計測の値があり、いまの Game.exe より後に撮られたこと）に替えた。ランナーは重い撮影をやり直さない。
- 古い経路で撮影が 10 分を超える PROBE-VSM（10万本）は ECS の後へ回し、負荷シーンの基準（STRESS-STREAMING）は 2万個に減らした。
- 改修前の計測をやめた（ユーザーの指摘「改善が確定している土台で測って意味があるのか」）: 作り変えが決まっている経路の基準は、比べても判断が変わらない。節目の計測の合否は目標の絶対値で決める。基準の計測の項目 `SCN-MEGA-STRESS-BASELINE` と VSM の事前調査 `SCN-MEGA-PROBE-VSM` を backlog にし、`SCN-MEGA-MEASURE-VSM` の合否を「PROBE-VSM の 1/5」から `VsmCullPairs` の p95 ≤ 1.0 ms に替えた。`--camera-path-play` は `SCN-MEGA-MIXED-LOAD` へ移した。
- ECS 前の参考値（撮り直さず、既に撮った分だけ。mega-stress 2万個・RelWithDebInfo・1920×1080。計測は組み立ての完了を待たないので p95 は揺れが大きい）:
  | 負荷 | 視点 | gt median / p95 | rt median / p95 | GPU median / p95 |
  |---|---|---|---|---|
  | 静止 | default | 480 / 675 ms | 51 / 389 ms | 42 / 503 ms |
  | 静止 | close | 451 / 545 ms | 119 / 565 ms | 377 / 559 ms |
  | 静止 | aerial | 427 / 646 ms | 53 / 1026 ms | 91 / 977 ms |
  | 1% 移動 | default | 416 / 529 ms | 42 / 55 ms | 82 / 113 ms |
  出典: `.harness/runs/scene-capture/SCN-MEGA-STRESS-STREAMING/static/metrics.json`（静止）、`.harness/runs/20261010-191942/verify-SCN-MEGA-STRESS-STREAMING-7-moving.txt`（1% 移動。trace.csv の読み取りで撮影が失敗し metrics.json は無い）。

### SCN-MEGA-BASE-RWDI（2026-10-10、反復 1・run 20261010-191942）

- RelWithDebInfo の決定的な撮影の基準 `SCN-MEGA-BASE-RWDI` を `.harness/runs/startup-capture/SCN-MEGA-BASE-RWDI` に撮った（`metrics.json` の `configuration` = RelWithDebInfo、Debug の基準は Debug。exe は別: Debug 49,030,144 B / RelWithDebInfo 20,708,352 B）。平均輝度は default 124.143・near 123.973・low 126.023。C++ は変えていない。
- Debug の基準 `SCN-MEGA-BASE` との差（RelWithDebInfo の撮影を `-CompareDeterministicWith SCN-MEGA-BASE` で比較、`-4-vs-debug.txt`）: 平均輝度の差は 3 視点とも 0（0.0001 未満）、PSNR は 3 視点とも 100 dB、不一致画素率は最大 9E-06・最大チャネル差 1。構成の違いによる差は、この 3 視点では無視できる大きさ。
- 同じコードでの撮り直し（`-3.txt`、`CompareDeterministicWith SCN-MEGA-BASE-RWDI`）は pass。default・near は輝度差 0・PSNR 100、low は輝度差 0.0001・PSNR 62.65 dB・不一致画素率 0.075・最大チャネル差 11（基準 ≤ 0.1・≥ 45 dB の内）。low は撮影ごとに揺れる画素がある。
- 証拠（`.harness/runs/20261010-191942/`）: `verify-SCN-MEGA-BASE-RWDI-1.txt`（Game の RelWithDebInfo ビルド EXIT=0）、`-2.txt`（基準の撮影 pass）、`-3.txt`（撮り直しの比較 pass）、`-4-vs-debug.txt`（Debug 基準との比較）。

### SCN-MEGA-ROOT-COOK（2026-10-10、反復 2・run 20261010-191942）

- 変更: `AssetSet` の項目に `dag_root_max_clusters`（0〜1000 の整数。省略か 0 は従来どおり）を足した。指定があるメッシュだけ、根のクラスタ数がこの数以下になるまで簡略化を続ける。`Scripts/CookAssets.ps1` → `AssetCook --dag-root-max-clusters` → `SingleAssetCook`・`MeshCooker` → `BakeMeshLodDag(rootMaxClusters)` へ渡す（`fallback_min_triangles` と同じ経路。管理キャッシュ経由の単体クックは非対応で、指定があれば拒否）。`MegaBananasModels.json` のバナナに 2 を指定した。他の資産は指定が無いので階層は変わらない（既定の経路は、打ち切り 15%・グループの目標 4 のまま。`AssetCookMeshSimplifySmoke` が pass）。
- 根のクラスタ数を指定した資産の簡略化（`CookMeshDag.cpp`）:
  1. 打ち切りの基準を 15% → 1% に緩める。
  2. 段の進みが 15% に満たないとき、グループの目標を 4→8→16→32 に倍にしてやり直し、三角形が最も少ない出力を採る（境界の固定を減らす）。
  3. それでも進まないとき、最も大きなグループ（目標 32）で、**非多様体の辺を許して**やり直す（最後の手段。安全な候補が無いグループは、三角形が最も少ない候補を辺の共有数の検査なしで採る）。
- 原因の測定（段 9 以降）: 実験でグループの簡略化の試行を記録した。簡略化は目標の三角形数に誤差 0.0002〜0.0006（バナナの外形 0.2 に対して 0.1〜0.3%）で届くが、出力の辺の共有数の検査（`IsOutputEdgeSafe`）で落ちる。落ちた理由は、入力にある辺の共有数が増えた（理由 1: 249 件）、入力にない新しい辺が 3 枚以上を共有（理由 2: 103 件）、新しい辺が段の入力の辺と重なる（理由 3: 135 件）。1 グループ（段全体）にしても理由 1・2 で落ちる。継ぎ目をまたぐ統合を許す許容モードでも同じ理由で落ちるので、継ぎ目の固定を緩めても進まない（辺が重なる根本の原因までは特定していない）。そこで辺の検査を最後の手段の段だけ外す手に決めた。
- バナナのクック（`NORVES_COOK_DAG_TRACE=1`、`verify-SCN-MEGA-ROOT-COOK-4-cook.txt`）: 段 0 は 780 クラスタ/99,722 三角形（従来と同じ）。段 9 から大きなグループで進み、段 14 でクラスタ 2 つになって打ち切り（`decision=single-cluster`）。`COOK_DAG_ROOT requested_max_clusters=2 root_clusters=2 root_triangles=212 root_error=0.173360 levels=15 relaxed_groups=4`（根のクラスタ数・三角形数・誤差は、指定があるときは環境変数なしで常にログに出る）。段数は 10 → 15、非多様体の辺を許したグループは 4（段 9・11・13 の各 1〜2 グループ）。根の誤差 0.173 はメッシュの球の半径（0.103）より大きく、根は遠景専用の粗さ。`MESH_COOK_PAGES` の `max_group_bytes` は 101,852（ページの上限 131,056 の内）。
- 描画（1 万本 overview、RelWithDebInfo、1920x1080、`mega-lod-error-px=100000`、`verify-SCN-MEGA-ROOT-COOK-6-capture.txt`・`-8-visraster.txt`）: 描いたクラスタ数 112,804 → 5,206。`VisRasterSw1` の窓 240 フレームの中央値 **0.928 ms → 0.0717 ms（1/12.9。目標 1/10）**（PROBE-ROOT の撮影のトレースを同じ窓の取り方で再計算した値）。gt 79.3 ms・rt 15.9 ms、MegaGeometry 全体 2.30 ms（PROBE-ROOT は 3.76 ms）。`CheckCaptureMetrics.py ... --views overview --timing` は pass（`-7-check.txt`）。
- 近景: `close` 視点（1 万本、既定の閾値、`-9-close.txt`）の画像を開いて確認した。手前のバナナはヘタ・先端・斑点・テクスチャまで元の細かさのままで、山の形も崩れていない（`SCN-MEGA-10K/close` は Debug・別寸法で画素比較は不可。目視）。
- **基準画像の再承認**: 1 万本 overview を `scene-capture/SCN-MEGA-BASE` と比べると PSNR 29.96 dB・不一致画素率 7.7%（輝度差 0.0172）で基準の 45 dB を割った。差の位置は、山（遠めのバナナの塊）の行（400〜600 行目）に集中し（不一致 7.4%）、空・遠景の床・手前（800 行以降）は 0.01% 以下。上の段（遠景で選ばれる粗い段）の階層と誤差が変わったことによる LOD の切り口の違いで、この変更だけが原因。画像を開いて形・色・配置は同じと確認し、新しい撮影を基準にした（旧基準は `scene-capture/SCN-MEGA-BASE-before-root-cook` に残す）。新基準との撮り直しの比較は PSNR 64.76 dB・輝度差 0 で pass（`-11-rebase.txt`。従来の撮影間の揺れ 65.35 dB と同程度）。以後の項目の `SCN-MEGA-BASE` との比較はこの基準を使う。
- 起動画面（`SCN-MEGA-BASE-RWDI` との決定的比較、`-10-startup.txt`）: 3 視点とも pass（default: 輝度差 0・PSNR 100、near: 0.0001・65.4 dB、low: 0.0001・70.4 dB）。
- 全体のクック（`verify-SCN-MEGA-ROOT-COOK-1.txt`）: `AssetCook` の実行ファイルが変わったので全資産を焼き直し、`COOK_ASSETS cooked=155 skipped=1`、6 分 27 秒で EXIT=0。途中で試したバナナだけの `-SpecDir` のクックは `manifest.json` を 4 項目に置き換えるので、その後に全体のクックで戻した。`TASKS.md` の 1 つ目の verify は、必須の `-AssetCookExe` が無く動かなかったので、引数を足した。
- `paths:` の外を触った点: `Scripts/CookAssets.ps1`（AssetSet の項目を読む唯一の入口で、verify も使う。8 行の追加のみ）。`Docs/Architecture/NVMESHv1LodGraph.md` の「打ち切り」の節は未更新（指定時の挙動を追記するのは次に文書を触るとき）。
- 証拠（`.harness/runs/20261010-191942/`）: `verify-SCN-MEGA-ROOT-COOK-1.txt`（全体のクック EXIT=0）、`-2-*`・`-3-*`（途中の試行）、`-4-build.txt`・`-4-cook.txt`（最終実装の AssetCook ビルドとバナナのクック）、`-5-gamebuild.txt`（Game ビルド EXIT=0）、`-6-capture.txt`、`-7-check.txt`、`-8-visraster.txt`、`-9-close.txt`・`-9-overview.txt`（基準の再承認前の比較。fail は再承認の根拠）、`-10-startup.txt`、`-11-rebase.txt`、`-12-simplifysmoke.txt`（`MESH_SIMPLIFY_SMOKE_RESULT passed`）。撮影の出力は `scene-capture/SCN-MEGA-ROOT-COOK/{root,close,ov,ov2}/`。

- 反復 3（評価者の差し戻しへの対応: 他の資産を焼き直さない）: `Scripts/CookAssets.ps1` に `-OnlyLogicalPath`（論理パス、ワイルドカード可）と `-Force` を足した。対象に合う項目だけ焼き（`-Force` で印が同じでも焼き直す）、対象外は焼かず・パッケージにも状態ファイルにも触れずに、既存の `manifest.json` の項目（埋め込みの画像を含む）をそのまま引き継ぐ。AssetCook の実行ファイルが変わって全項目の印が変わっていても、対象外は焼き直されない。既存のマニフェストが無い、または対象外の項目がそこに無いときは失敗する（先に全体をクックする）。標準出力に `COOK_ASSETS only=<パターン> untouched=<数>` を足した（最終行の形は従来どおり）。
- 実走（`verify-SCN-MEGA-ROOT-COOK-13-only.txt`）: バナナだけ `-OnlyLogicalPath Models/Generated/banana/banana.glb -Force` で 3.2 秒、`cooked=1`・`untouched=155`、`COOK_DAG_ROOT root_clusters=2 root_triangles=212 root_error=0.173360` が出る。クック前後で `build/CookedAssets` の全 366 ファイルの大きさ・更新日時・SHA-256 を比べた（`-13-compare.txt`、`snap-before.json`・`snap-after.json`）: 変わったのはバナナのパッケージ 4 つ（モデル 1・埋め込みテクスチャ 3）とバナナの状態ファイル 1 の更新日時だけ（中身のハッシュは同じ = 決定的）。それ以外の 361 ファイルは更新日時・ハッシュとも不変、`manifest.json` はバイト単位で同一。`COMPARE pass`。
- 回帰: 全体のクック（`-14-full.txt`）は `cooked=0 skipped=156` で EXIT=0。`-OnlyLogicalPath "Models/Generated/banana/*"` を `-Force` なしで実行（`-15-only-nochange.txt`）は `untouched=155`・`cooked=0 skipped=1`。`TASKS.md` の verify に、対象を絞ったクックの行を足した。
- Notes: 実行ファイルが変わった後の全体のクックは従来どおり全項目を焼き直す（印に実行ファイルのハッシュを含めるのは残した）。今回の差し戻しの指摘（クックが 155 項目を書き出した）は、AssetCook を再ビルドした反復で全体のクックをしたことによるもの。以後は `-OnlyLogicalPath` で絞れる。`Scripts/CookAssets.ps1` は `paths:` の外だが、AssetSet の項目を読む唯一の入口で、差し戻しの修正の置き場所として妥当。

### SCN-MEGA-STRESS-RESIDENT（2026-10-10、反復 4・run 20261010-191942）

- 変更: 開発ビルドだけの負荷シーン `mega-stress` を足した（`Game/GameModes/MegaStress/`、シーンの一覧 `SceneCatalog.h` の `kMegaStressSceneId`・視点 overview/close/aerial、`GameModeIds.h` の `MegaStress`、ランチャーのカードは並びの最後）。メッシュの一覧は `Assets/AssetSets/MegaStressMeshes.json`（`cook_assets` を持たないのでクックの対象ではない。14 件: Boulder・Cottage・BigCobbleSphere・ScanProps 3 種・watchtower・banana・Poly Haven の岩 6 種。件ごとに論理パス・クック済みの材質のテクスチャ 3 枚・仮想テクスチャか・置く高さ・重み）。`--stress-entities=<n>`（1〜200000、既定 2000）で、読めたメッシュから重みで選んで個別の Entity を、升目（5 m）ごとにずらして置く。メッシュ 1 種類 = 1 MegaMesh = 1 Model（Entity は共有）。クック済みが無い件は `MEGA_STRESS_ASSET_SKIPPED` を警告して読み飛ばす。ログ: `MEGA_STRESS_MESH`（件ごと）・`MEGA_STRESS_KIND`（種類ごとの数）・`MEGA_STRESS_PLACED entities=<置いた数> requested=<指定> resources=<読めた資源の数> sections=<区間の数>`。区間は材質とプールの塊が同じ Entity の集まりで、メッシュごとに 1 つなので sections = 置いた種類の数。
- 結果: 2000 個（cycle smoke）・5000 個（撮影）とも `resources=14 sections=14`。5000 個の撮影（`.harness/runs/scene-capture/SCN-MEGA-STRESS-RESIDENT/`、default・close・aerial）の画像を開いて、小屋・見張り塔・石畳の球・岩・バナナが並んでいることを確かめた。`MEGA_CAPACITY draw_dropped=0 command_capacity=33554432`。cycle smoke（3 周）は pass で、周回の後の資源は 1 周目と同じ。
- **見つけた問題（別件。`TASKS.md` に `SCN-MEGA-COMMAND-CAPACITY` として追記）**: 同じ 14 種類で `--stress-entities=3300` 以上（5000 個で確認）にすると、`MEGA_CAPACITY command_capacity=67108864`（`indirect_bytes` 1.34 GB）へ跳ねて、空と床だけが写る（`MEGA_OCCLUSION` は 44 万クラスタを描いたと言う。`draw_dropped=0`・検証レイヤーのエラーなし）。2500 個（`command_capacity=33554432`）までは描かれる。`NORVES_MEGA_MAX_DRAW_COUNT=1000000` を渡すと 5000 個でも描かれるので、mega-stress は `--stress-entities` を指定したとき（開発ビルド）、環境変数が無ければ 1000000 を渡す（`GameApplicationHandler.cpp`。`--mega-lod-error-px` と同じく MegaGeometryPass が作られる前）。回避なしでも 2500 個までは描かれることを確かめた（ランチャーから入る既定の 2000 個はこの範囲）。この回避は容量の上限を縮めるだけで、後の項目で 2 万個を置くときに区間ごとの上限（100 万）を超えて描かれないクラスタが出れば `draw_dropped` に出る。
- 材質のテクスチャ: クック済みの BC のものは仮想テクスチャ（失敗したら全常駐で読み直し）、生成した GLB の埋め込み画像（RGBA8）は全常駐。床は無地（一辺 1600 m）。
- 証拠（`.harness/runs/20261010-191942/`）: `verify-SCN-MEGA-STRESS-RESIDENT-4-build.txt`（Game の RelWithDebInfo ビルド EXIT=0、最終実装）、`-5-cycle.txt`（cycle smoke 3 周 `result=pass`）、`-6-capture.txt`（5000 個の撮影 `result=pass`）。途中の確認: `-1.txt`〜`-3.txt`（回避を入れる前のビルド・cycle・撮影。撮影は pass だが画像は空と床だけで、これが上の問題）。切り分けの撮影は `.harness/runs/scene-capture/SCN-MEGA-STRESS-RESIDENT-dbg{40,1000,2500,3300,4200,Max1M,Debug3300}/`（Debug の 3300 個でも検証レイヤーのログは空）。
- Notes: `CaptureScene.ps1` の pass は、画像が空でも出る（`mean_luminance` と白飛び・黒つぶしの比率だけを見る）。5000 個の撮影は画像を開いて確かめた。

### SCN-MEGA-STRESS-MOVING（2026-10-10、反復 5・run 20261010-191942）

- 変更: mega-stress に `--stress-moving-percent=<p>`（0〜100 の数。既定 0 = 動かさない）を足した。Entity を置くときに、番号のハッシュ（100 万分率）が割合の閾値より小さいものを「動く Entity」にする（実行ごとに同じ集合）。毎フレーム `Tick` で、置いた位置の周りを半径 1〜2 m・角速度 0.8〜1.6 rad/s の円運動（水平面）にし、ヨーを ±1.5 rad/s の範囲で回す（半径・角速度・位相・ヨーの角速度は番号から決める。高さは変えない）。動きは番号と経過時間だけで決まる。ポーズ中は止める。置いた直後の 1 フレーム目から動いた位置で描く。
- 撮影で時刻を変えるため、`--stress-moving-time=<秒>`（動きの時刻の底上げ。既定 0）も足した。ログは `MEGA_STRESS_MOVING movers=<数> percent=<p> start_time=<秒>`。
- 変更ファイル: `Game/GameApplicationHandler.{h,cpp}`（引数の解析と受け渡し）、`Game/GameModes/MegaStress/MegaStressMode.h`・`MegaStressRoutine.cpp`。
- 結果（5000 個・10%）: `MEGA_STRESS_PLACED entities=5000 resources=14 sections=14`、`MEGA_STRESS_MOVING movers=528`（10.56%。ハッシュによる選び方なので正確に 10% ではない）、`draw_dropped=0`。
- 動いていることの確認: 時刻の底上げを 0 秒と 3 秒にした default 視点の 2 枚（`scene-capture/SCN-MEGA-STRESS-MOVING-t0/default.png`・`-t3/default.png`）を開いた。右側の小屋が回転して位置が変わり、バナナ・岩の位置も違う。差が 8 を超える画素は 278,696（13.44%）。同じ時刻（0 秒）を撮り直した対照は 221 画素（0.011%）なので、差は撮影の揺れでなく動きによる（`-4-diff.txt`・`-5-control.txt`）。
- 証拠（`.harness/runs/20261010-191942/`）: `verify-SCN-MEGA-STRESS-MOVING-1.txt`（Game の RelWithDebInfo ビルド EXIT=0）、`-2-capture.txt`（verify の撮影 `result=pass`。出力は `scene-capture/SCN-MEGA-STRESS-MOVING/`）、`-3-t0.txt`・`-3-t3.txt`（時刻を変えた撮影）、`-4-diff.txt`、`-5-control.txt`。
- Notes: 計測は SCN-MEGA-STRESS-STREAMING でまとめて行う（このタスクでは GPU 時間を測っていない）。PowerShell でなく Git Bash から `cmake --build ... -- /m:1` を打つと `/m:1` がパスに変換されて失敗する（PowerShell から実行する）。

- 反復 6（評価者の差し戻しへの対応: 動かす個数を割合どおりに揃える）: 動く Entity の選び方を、番号のハッシュの閾値（個数がずれる）から、**置けた数 × 割合を四捨五入した個数**に変えた。置き終わった Entity を候補にして番号のハッシュの小さい順に並べ、先頭からその個数を選ぶ（同じハッシュは置いた順で決め、選んだ分は置いた順に戻す。実行ごとに同じ集合）。割合が 0 のときは候補を溜めない。変更は `MegaStressRoutine.cpp` の `FinishAssembly` だけ。
- 結果（5000 個・10%）: `MEGA_STRESS_PLACED entities=5000` → `MEGA_STRESS_MOVING movers=500 percent=10.000`（前回は 528）。時刻を 0 秒と 3 秒にした default 視点の 2 枚（`scene-capture/SCN-MEGA-STRESS-MOVING-t0/default.png`・`-t3/default.png`）を開いた。左の小屋の向き、バナナ・岩・石畳の球の位置が違う。差が 8 を超える画素は 392,223（18.9%。`-9-diff.txt`）。前回の対照（同じ時刻の撮り直しは 0.011%）から、差は動きによる。
- 証拠（`.harness/runs/20261010-191942/`）: `verify-SCN-MEGA-STRESS-MOVING-6-build.txt`（Game の RelWithDebInfo ビルド EXIT=0）、`-7-capture.txt`（verify の撮影 `result=pass`。出力は `scene-capture/SCN-MEGA-STRESS-MOVING/`、ログに `movers=500`）、`-8-t0.txt`・`-8-t3.txt`（時刻を変えた撮影）、`-9-diff.txt`。

### SCN-MEGA-STRESS-STREAMING（2026-10-10、反復 1・run 20261010-220740）

- 変更: mega-stress の `--stress-stream-cell=<m>`（4〜2000）と `--stress-stream-period=<秒>`（既定 1、0.01〜3600）。置いた Entity を一辺 m[m] の区画に分け、周期ごとに 1 手: カメラに最も近い読み込み中の区画を解放し、解放中が窓（`kStreamWindowCells` = 4）に達したら解放した順に 1 つずつ読み直す。解放した区画が全部戻った次の Tick に `MEGA_STRESS_STREAM_CYCLE` で、生きている Entity・World の Entity・材質・メッシュ・モデルの数が置き終えた直後の基準と一致するかを出す（`result=pass|fail`）。
- 前の反復の途中保存（a005038e・403e93b1）の続きとして、材質・メッシュ・モデルの照合を含めてビルドし、動作を確かめた。
- 見つけた問題と修正: 区画の引数付きの cycle smoke が、区画の出入りが止まらないため Entity の数が落ち着かず（スモークは値が 90 フレーム続けて同じなのを待つ）、300 秒の上限まで待って SCENE_CYCLE の行が Game.log の 10 MB の上限に押し出されて失敗した。cycle smoke の中だけ周期を `kSmokeStreamCycles` = 3 回で止める（`m_StreamMaxCycles`。`GameApplicationHandler.cpp` が `m_SceneCycle.IsEnabled()` のときに渡す）。通常の起動では止めない。
- 結果: 素の引数・区画の引数（2000 個・64 m・0.05 秒）とも cycle smoke 3 周が `result=pass`。区画の引数のログは 15 区画・窓 4・`MEGA_STRESS_STREAM_CYCLE` が 9 件すべて `result=pass`（`live=2000 world_objects=2002 materials=1/1 meshes=1/1 models=14/14`）、`result=fail` は 0 件。
- 証拠（`.harness/runs/20261010-220740/`）: `verify-SCN-MEGA-STRESS-STREAMING-1.txt`（ビルド EXIT=0、最初の実装）、`-3.txt`（区画の引数の smoke の失敗、上記の問題）、`-4.txt`（修正後のビルド EXIT=0）、`-5.txt`（素の smoke pass）、`-6.txt`（区画の引数の smoke pass）、`-7.txt`（STREAM_CYCLE の検査 EXIT=0）。
- Notes: このタスクでは GPU 時間を撮っていない（基準の計測は SCN-MEGA-STRESS-BASELINE）。

### SCN-ECS-CHECK-GATE（2026-10-10、反復 1・run 20261010-223034）

- 検査の窓口: `Debug/EngineChecks.h`（`NorvesLib::Debug::EngineChecks`）。`--engine-checks=<名前,...>`（名前は大文字・数字・下線。開発ビルドだけ。Release は警告して無視）で有効にした検査だけが、`Report(名前, pass, 件数)` で `ENGINE_CHECK name=<名前> result=pass|fail count=<n>` をログへ出す（fail は ERROR）。状態は `Engine::GetEngineChecks()`（GEngine のメンバ。シングルトンにしない）。配線の確認用に `ENGINE_CHECK_SELFTEST`（Game が起動後の最初の更新で 1 回報告）と `--engine-checks-selftest-fail`（fail で報告）を足した。
- スクリプト: `CaptureScene.ps1`・`CaptureStartupScene.ps1` に `-RequireChecks`（各名前の pass が 1 回以上・fail が 0。名前の指定が無くても fail の行があれば失敗。結果は metrics.json の `engine_checks`）、3 本に `-Build`（撮る構成の Game をビルドし、失敗なら撮らずに失敗。ログは OutDir の `Build.log`）、`CaptureStartupScene.ps1` に `-GameArgs`。`RunSceneCycleSmoke.ps1` は fail の行で落ち、`-GameArgs engine-checks=<名前>` で有効にした検査は pass が無くても落ちる。
- 計測の窓（`CaptureScene.ps1 -GpuTimingFrames N`）: Game は SceneReady のフレームを `SCENE_READY_FRAME scene=<id> frame=<n>` で出す。窓は SceneReady の後のウォームアップ（`-TimingWarmupFrames`、既定 60）を除き、末尾の撮影の直前 2 つも除いた最後の N 件（有効な標本）。Game は N + ウォームアップ + 2 + `-TimingReadyMarginFrames`（既定 240）の描画フレームの後に終わる。標本が N 件に満たなければ失敗（余白を大きくして撮り直す）。metrics.json の視点ごとに `placed_count`（`MEGA_STRESS_PLACED`・`MEGA_BANANAS_PLACED`）・`scene_ready_frame`・`timing_start_frame`、`gpu_timing` に `valid_samples`・`start_frame`・`window_first_frame`・`window_last_frame`。`CheckCaptureMetrics.py --min-frames` は `valid_samples` を見て、`timing_start_frame` が無い撮影（旧形式）は通さない。
- **以後の計測の注意**: 有効な標本は N 件になった（以前は N − 60 件）。重いシーン（2 万個など）は、SceneReady までの描画フレームが余白を超えうるので `-TimingReadyMarginFrames` を大きくする。SCN-MEGA-STRESS-BASELINE の `--min-frames 180` は `-GpuTimingFrames 240` なら 240 件取れる（以前は 180）。
- 実走: verify（RelWithDebInfo の Game をビルドして起動画面 3 視点を決定的に撮影、`SCN-MEGA-BASE-RWDI` との比較）は pass。3 視点とも `ENGINE_CHECK_SELFTEST pass=1 fail=0`、比較は default PSNR 100 dB・near 65.36 dB・low 70.35 dB（輝度差 0.0001 以下）。`--engine-checks-selftest-fail` の撮影は失敗し（EXIT=1）、出力は `verify-SCN-ECS-CHECK-GATE-2-fail.txt`: `failure: default: 検査が fail: ... ENGINE_CHECK name=ENGINE_CHECK_SELFTEST result=fail count=1` と `検査 ENGINE_CHECK_SELFTEST の pass がログに無い`（日本語の部分は端末の文字コードで化けて記録されている）。
- 計測の実走: `CaptureScene.ps1 -Scene mega-stress -Configuration RelWithDebInfo -GpuTimingFrames 120 -TimingReadyMarginFrames 600 -GameArgs stress-entities=500 -RequireChecks ENGINE_CHECK_SELFTEST` は pass（`-3-timing.txt`）: `scene_ready_frame=288009 timing_start_frame=288069 window=299403..301229 valid_samples=120 placed_count=500`。`CheckCaptureMetrics.py ... --timing --min-frames 120` も pass。cycle smoke は pass（`-4-cycle-pass.txt`）と、fail 報告で落ちる（`-5-cycle-fail.txt`、EXIT=1）を確認した。
- `paths:` の外を触った点: `Library/Core/Public/Engine/Engine.h`（`GetEngineChecks()` の追加。窓口を GEngine のメンバに置くため）、`Game/Scenes/SceneReadiness.h`（SCENE_READY を決定的な撮影でないときも状態として持ち、`SCENE_READY_FRAME` を出す。ApplicationProcessor・SequenceFrameCapture は `IsEnabled()` も見るので、決定的な撮影でない起動の挙動は変わらない）、`Scripts/CheckCaptureMetrics.py`（done-when が `--min-frames` の変更を求めている）。
- Notes: 計測の窓の「ゲームフレーム番号」は、描画より速く進むゲームスレッドのフレーム（GPU の行は描画したフレームだけ）なので、`window` の幅は標本数より大きい。余白は描画フレーム数、SceneReady は ゲームフレーム番号 なので、余白の見積りは過大側に倒れる（安全側）。
- 証拠（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-CHECK-GATE-1.txt`（verify、EXIT=0）、`-2-fail.txt`、`-3-timing.txt`、`-4-cycle-pass.txt`、`-5-cycle-fail.txt`。

- 反復 2（評価者の差し戻しへの対応: 計測は SceneReady を必須にし、ウォームアップを標本の件数で除く）:
  - SceneReady 必須: `Rendering3DTestRoutine.cpp` の SceneReady の報告（`UpdateVirtualTextureSettle`・`UpdateSceneAssembly`）を、決定的な撮影のときだけでなく常に呼ぶ（状態と印のログだけで、撮影でない起動の挙動は変わらない）。`CaptureScene.ps1` は SCENE_READY を計測でも必須にし、SCENE_READY_FRAME が無ければ計測せず失敗にする（代替の開始番号 60 を削除）。
  - ウォームアップの件数除外: `Get-GpuTimingFromTrace` は、SceneReady のフレーム以降の有効な描画標本から、先頭の `-TimingWarmupFrames` 件を除き、末尾の 2 件を別に除いた残りの最後の N 件を取る（GPU・CPU とも）。残りが N 件に満たなければ失敗。`gpu_timing` に `scene_ready_frame`・`warmup_frames_removed`、`timing_start_frame` は除外後の最初の標本のフレーム番号。
  - `CheckCaptureMetrics.py`: 計測でも `scene_ready`・`scene_ready_frame` を必須にし、`--min-frames` では窓が SceneReady より後であること・`warmup_frames_removed` が指定のウォームアップ件数以上であることも見る。
  - 英語の終端コメント `// namespace` を `EngineChecks.{h,cpp}` から削除。
  - 実走: verify（Game を再ビルドして起動画面 3 視点の決定的撮影）は pass（`verify-SCN-ECS-CHECK-GATE-6.txt`、EXIT=0。ENGINE_CHECK_SELFTEST pass=1 fail=0、default PSNR 100・near 65.36・low 70.35）。計測（mega-stress 500 個、`-GpuTimingFrames 120 -TimingReadyMarginFrames 600`、`-7-timing.txt`）は 3 視点とも `valid_samples=120`・`scene_ready_frame` の後に `timing_start_frame`、`CheckCaptureMetrics.py --timing --min-frames 120` は pass。metrics.json を改変した 3 通り（SceneReady なし／ウォームアップ除外 1 件／窓が SceneReady より前）はいずれも `CHECK_CAPTURE_METRICS result=fail`（`-8-negative.txt`）。
  - `paths:` の外を触った点: `Game/GameModes/Rendering3DTest/Rendering3DTestRoutine.cpp`（上の報告の呼び出し。評価者の指摘どおり）。

### SCN-ECS-CORE-CHUNKS（2026-10-10、反復 3・run 20261010-223034）

- 変更: 新しい領域 `Library/Core/Public/Ecs/`・`Private/Ecs/` を足した（まだどこからも使わない）。`EcsTypes.h` は `EcsEntity{Index, Generation}`（Generation は 1 から、全ビット 0 が null）と、列の記述 `EcsColumnInfo{Type, Size, Align}`・`EcsColumnInfoOf<T>()`（trivially copyable を static_assert、型の番号は `TypeRegistry::GetTypeId<T>()` を関数内 static に覚える）。`EcsWorld.h/.cpp` は実体の表（Index → チャンク・行・Generation、空き一覧は LIFO で Generation を進めて再利用）・アーキタイプ（型の番号の昇順の列 + タグのビットで同一性。同じ型の重複・大きさ 0・16 KB に収まらない行・同じ型で大きさの違う記述は `kInvalidEcsArchetype`）・16 KB のチャンク（`PoolAllocator<false>`、64 byte 配置。先頭 64 byte が見出し、続けて EcsEntity の配列、その後に列ごとの連続配列）・行の作成（値は 0）と削除（アーキタイプの末尾の行との入れ替え。末尾のチャンクが空になったら解放）・まとめての作成 `CreateEntities`。走査用に `GetArchetypeChunks`・`FindColumnIndex`・`GetChunkColumn`、数の確認用に `GetEntityCount`・`GetChunkCount`・`GetChunkPoolBytes`。
- テスト: `EcsStorageTest`（EntityTreeTest の束）が、アーキタイプの同一性と不正な記述の拒否、チャンクの配置（列が重ならず 16 KB に収まり型の配置を満たす）、Generation で古い識別子を拒む（解放後・再利用後・範囲外・世代違い・null）、入れ替え削除で残りの値が保たれ実体の表とチャンクの配列が対応する、チャンクの解放と再利用（プールの大きさが増えない）、100 万行を作って半分ずつ消すと行とチャンクの数が 0 に戻る、を確かめる。
- 結果: `verify-SCN-ECS-CORE-CHUNKS-4.txt`（EntityTreeTest のビルド EXIT=0）、`-5-ctest.txt`（`EcsStorageTest` Passed、`100% tests passed`）、`-6-direct.txt`（`EcsStorageTest passed`）。
- **見つけた問題と対応（`paths:` の外）**: `EntityTreeTest` の束は 2026-09-27 以降リンクされておらず、HEAD の時点でビルドが通らなかった（`-1.txt`: 3 ファイルで 11 件のエラー）。どれも本体のヘッダの変更（10-08 の `[skip ci]` の変更で `Entity::SetLocalTransform` が private の節に入った、`GetLocalPosition` の廃止、`World::DispatchFixedTick`・`CleanupAfterFixedStep` が非公開）にテストが追随していなかったもの。以後の ECS の項目はどれも同じ verify なので、テストだけを最小限で直した（本体は変えていない）。`TickDispatchTest.cpp`: `Component::Component` を完全修飾（namespace との曖昧さ）。`WorldTickGroupTest.cpp`: `SetLocalTransform(位置だけ変えた恒等)` を `SetLocalPosition(x, 0, 0)` へ。`SpringArmComponentTest.cpp`: `IUnknown` を完全修飾、`GetLocalPosition()` を `GetLocalTransform().position` へ、固定ステップ送りは `WorldTickGroupTest.cpp` と同じ定義の友達構造体 `WorldTickGroupTestAccess`（World の friend 宣言が名指ししている）を足して通した。同じ束の `TickDispatchTest`・`WorldTickGroupTest`・`SpringArmComponentTest`・`EntityTreeTest`・`TickGroupConfigurationTest`・`WorldTimeChannelTest`・`TransformHierarchyTest` は ctest で全て Passed（`-7-neighbors.txt`）。
- Notes: 束の規約（無名名前空間の外に同名の定義を置かない）に対し、`WorldTickGroupTestAccess` は 2 ファイルに同一の定義がある（トークン列が同じなので ODR 上は問題ない。`World` の friend 宣言が名前を固定しているため別名にできない）。`EcsWorld` は GameThread 専用でスレッドセーフではない。チャンクのプールは 1 スラブ 16 ブロック（256 KB）をコンストラクタで確保する。`Test/Core/Object/CMakeLists.txt` には束の MEMBER に加えて `add_test(NAME EcsStorageTest ...)` も足した（ctest の登録に必要）。
- 証拠（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-CORE-CHUNKS-1.txt`（修正前のビルドの失敗）、`-2.txt`（テストの修正後のビルド EXIT=0）、`-3-ctest.txt`（登録前の ctest: No tests were found）、`-4.txt`・`-5-ctest.txt`・`-6-direct.txt`・`-7-neighbors.txt`。

- 反復 4（評価者の差し戻しへの対応）:
  - 型の記述の照合: `GetOrCreateArchetype` が、既存のアーキタイプとのタグ・列数の違いに関わらず、各列の記述（Size・Align）を `TypeRegistry::Find(Type)` の登録と毎回照合し、未登録の型や食い違いは `kInvalidEcsArchetype` を返す（以前はタグか列数が違うと照合が省かれ、タグ違いで Size を偽った記述が通った）。`EcsStorageTest` に、Align 違い・タグ違いで Size を縮めた記述・列の組が違う場合・未登録の型番号の拒否を足した。
  - チャンク再利用の証明: `TestReleasedBlocksAreReusedAfterSlabExhausted` を足した。最初のスラブ（16 ブロック）を使い切ってプールを広げ（40 チャンク）、末尾の 10 チャンクを空にして解放し、作り直したチャンクのポインタがすべて解放前のものの中にあって重複しないこと、プールの大きさが増えないこと、全部消してから作り直してもプールが増えないことを確かめる。
  - 成功表示を日本語（`EcsStorageTest 合格`）にした。
  - 証拠（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-CORE-CHUNKS-8.txt`（EntityTreeTest のビルド EXIT=0）、`-9-ctest.txt`（`EcsStorageTest` Passed、`100% tests passed`）。

### SCN-ECS-CORE-MOVE（2026-10-10、反復 5・run 20261010-223034）

- 変更: `EcsWorld::AddColumn(entity, 記述, 初期値)`・`RemoveColumn(entity, 型番号)`（型付きは `AddColumnOf<T>`・`RemoveColumnOf<T>`）を足した。行は列を 1 つ増減したアーキタイプ（タグは同じ。列が 0 本になってもよい）へ、移動先の末尾に共通の列の値をコピーして移り、移動元は削除と同じ末尾との入れ替えで詰める（`RemoveRow` に切り出し、`DestroyEntity` も使う）。EcsEntity は変わらない。すでに持つ列の追加・持たない列の削除・死んだ実体・登録と食い違う記述は false で何も変えない。
- 型の事前登録: `EcsRegisterColumn<T>()`（`EcsTypes.h`）が型を TypeRegistry に登録して番号を固定する（`EcsColumnInfoOf<T>()` は同じ関数。並列の外なら未登録でも使った時点で登録する）。並列の区間は `EcsParallelScope`（入れ子可）で表し、区間の中で未登録の型を使うと `Detail::EcsReportUnregisteredColumn()` が数え、`NORVES_BUILD_DEVELOPMENT` では assert する（`EcsSetUnregisteredColumnHandler` で assert の代わりに受けられる。テスト用）。未登録のときの記述は型の番号が `InvalidSchemaId` で、アーキタイプには使えない。新ファイル `Private/Ecs/EcsTypes.cpp`。
- テスト: `EcsMoveTest`（EntityTreeTest の束、ctest 登録済み）が、移動の前後の値と実体の識別子の保持、初期値あり・なし、列を外して元のアーキタイプの番号へ戻ること、拒否された変更が何も変えないこと、移動元の入れ替え（中ほど・末尾・全部移してチャンク解放）と実体の表・チャンクの実体配列の整合、複数チャンクにまたがる 3 チャンク超の移動と戻し、タグの保持と列 0 本のアーキタイプ、並列の中の未登録の検出（回数・入れ子・登録後は報告なし）を確かめる。
- 結果: `verify-SCN-ECS-CORE-MOVE-2.txt`（EntityTreeTest のビルド EXIT=0、警告 0 件）、`-3-ctest.txt`（`EcsStorageTest`・`EcsMoveTest` Passed、`100% tests passed`）、`-4-direct.txt`（`EcsMoveTest` 合格。端末の文字コードで化けて記録されている）。`-1.txt` は `[[nodiscard]]` の警告が 1 件あった最初のビルド（テストの `(void)` で解消）。
- Notes: RelWithDebInfo では `assert` が無効なので、並列の中の未登録の検出は回数（`EcsGetUnregisteredColumnReportCount`）だけが残る。移動のたびに `GetOrCreateArchetype` で行き先を引く（毎回ソートと線形の照合）ので、大量の構造変更で目立てば、アーキタイプごとの「列を足す・外す」の辺のキャッシュを足す。

- 反復 6（評価者の差し戻しへの対応）: 並列の中の未登録の型は、`NORVES_BUILD_DEVELOPMENT` なら `NDEBUG` に依らず stderr へ `ECS_UNREGISTERED_COLUMN ...` を出して `std::abort()` する（`assert` をやめた。RelWithDebInfo でも止まる）。`EcsSetUnregisteredColumnHandler` のハンドラを差した場合は従来どおり数えて戻る。`EcsMoveTest` に、ハンドラなしの既定の動作を別プロセスで確かめるケースを足した（自分自身を `--test=EcsMoveTest --unregistered-column-child` で起動し、終了コードが 0 でないことと出力に `ECS_UNREGISTERED_COLUMN` があることを照合。開発ビルドのみ）。
- 結果: `verify-SCN-ECS-CORE-MOVE-6.txt`（EntityTreeTest のビルド EXIT=0、警告・エラー 0 件）、`-6-ctest.txt`（`EcsStorageTest`・`EcsMoveTest` Passed、`100% tests passed`）、`-6-child.txt`（子を直接起動: 終了コード 3、`ECS_UNREGISTERED_COLUMN` を出力）。
- Notes: 上の反復 5 までの Notes の「RelWithDebInfo では assert が無効で回数だけが残る」は、この反復で解消した。

### SCN-ECS-CORE-CHANGES（2026-10-11、反復 7・run 20261010-223034）

- 変更（`EcsWorld.h/.cpp` のみ。まだどこからも使わない）:
  - 版: World の版は uint64（1 から。0 は「一度も書かれていない」）。`AdvanceVersion()` で進む（システム 1 回ぶん・再生ごと）。「チャンク × 列」ごとの最後に書いた版は全列がチャンク内の末尾（`uint64 × 列数`）に持つ。
  - 追跡する列: `SetColumnTracked(型)`（最初のその型を持つアーキタイプより前に呼ぶ。後なら false）。追跡する列は行ごとの版の配列（チャンク内、列の後ろ）と変更ログを持ち、チャンクの行数はその分だけ減る。
  - 書き口: `MarkWritten` / `MarkWrittenRange` が、チャンク × 列の版を進め、追跡する列なら行の版を比べて、まだその版でない行だけを `EcsChangeBundle` へ積む（並列の仕事ごとの束。1 チャンク 1 仕事の前提）。`CommitChanges` が束を渡した順に連結してログへ移し、束を空にする。段の外の書き込みの口は `WriteColumnValue`。
  - 段の外の書き込みと構造変更（作成・列の追加と削除・入れ替え・孤児）の版: 消費者が読んだ（または登録した）後の最初の書き込みで版を +1 してから書く（`m_bVersionSealed`）。読む前の書き込みは同じ版にまとまる。だから読み落としが無い。作成・移動した行と、削除や移動元の詰めで入れ替わった行は、全追跡列で「変わった」になる（ログにも積む）。行は自分の版を持って入れ替わり、作った行・移動先の行は版を 0 に戻してから付けるので、行に残った前の持ち主の同じ版がログを隠さない。
  - 消費者: `RegisterChangeConsumer(型)` は既読をログの末尾・いまの版から始める。`ReadChanges` は前回より後の項目（死んだ実体を除く。同じ行でも版が違えば別の項目）を返して既読を進め、`GetConsumerReadVersion` を `EcsQuery::ChangedSince` に渡せる。全員が読み終えた分は捨てる。消費者がいない列にはログを積まない。
  - クエリ: `EcsQuery`（All/Any/None の列、AllTags/AnyTags/NoneTags、Changed + ChangedSince、`bIncludeOrphans`）。`MatchArchetypes`（アーキタイプ）と `CollectChunks`（チャンク。Changed があればチャンクの列の版が ChangedSince より後のものだけ）。孤児は予約タグ `kEcsTagOrphan`: `MarkOrphan` が行を同じ列の組でタグを足したアーキタイプへ移し、既定のクエリには出ない（`bIncludeOrphans` で出る）。
- テスト: `EcsChangeTest`（EntityTreeTest の束、ctest 登録済み）が、追跡の宣言の順序とチャンクの配置、書いた行だけが版ごとに 1 回ずつログに出る（同じ版の二重書き・追跡しない列はログに出ない）、束の連結が仕事の順、段の外の書き込みが読んだ後なら新しい版になる（チャンクの版の絞り込みも）、作成・移動・入れ替え・孤児で動いた行が「変わった」になる、前の持ち主の版が残ってもログに出る、2 つの消費者の既読が独立（後から登録した消費者は過去を見ない・解除・全員解除後は積まない）、クエリの All/Any/None/タグ/Changed と孤児の除外、チャンクの版の絞り込み、10 万行を 1 チャンク 1 束で書いて 1 回ずつ、を確かめる。
- 確認: 行に残った版を 0 に戻す処理を一時的に外すとテスト（`TestMovedRowsDoNotInheritStaleVersions`）が落ちること、戻すと通ることを見た。
- 結果: `verify-SCN-ECS-CORE-CHANGES-1.txt`（EntityTreeTest のビルド EXIT=0）、`-2-ctest.txt`（3 本 Passed）、`-3-direct.txt`（`EcsChangeTest 合格`。端末の文字コードで化けて記録されている）、`-4-rebuild.txt`・`-5-ctest.txt`（変異を戻した後の再ビルドと ctest、`100% tests passed`）。
- Notes: 追跡する列の宣言は最初のアーキタイプより前に必要（配置が変わるため）。`GetColumnValue` の可変ポインタで書いた値は版を付けない（版を付けるのは `MarkWritten` と `WriteColumnValue`）。`MatchArchetypes` は毎回全アーキタイプを走査する（アーキタイプが数百を超えて目立てば、クエリごとの結果のキャッシュを足す）。`AdvanceVersion` を呼ぶ側（スケジューラ・再生）はまだ無い。

- 反復 8（評価者の差し戻しへの対応）: 書いた直後（同じ版）に列を足す・外すと、移動先の行の版が 0 に戻り、同じ実体・版がログに 2 件出た。`MoveRow` が、移動元と移動先に共通する追跡列の行の版を値と一緒に引き継ぐようにした（`TouchRow` は同じ版なら積まない）。追跡列を外して付け直した行は版を引き継げないので、その追跡列のログに `bMayDuplicate` を立て、`ReadChanges` が同じ実体・版の項目を 1 回にまとめる（全員が読み切るとフラグが戻る）。`EcsChangeTest` に `TestWriteThenMoveLogsOncePerVersion`（書く→足す→外す、書く→追跡列を外す→付け直す を読まずに続けて 1 件）を足した。
- 結果: `verify-SCN-ECS-CORE-CHANGES-8-1.txt`（EntityTreeTest のビルド EXIT=0）、`-8-2.txt`（`EcsStorageTest`・`EcsMoveTest`・`EcsChangeTest` Passed、`100% tests passed`）。
- Notes: 重複の除去は `bMayDuplicate` が立っている間の読み取りだけで、通常の経路の費用は増えない。

### SCN-ECS-CORE-COMMANDS（2026-10-11、反復 9・run 20261010-223034）

- 変更（`Library/Core/{Public,Private}/Ecs/`。まだどこからも使わない）:
  - `EcsCommandBuffer`（新規 `EcsCommandBuffer.h/.cpp`）: 仕事ごとに 1 つのバッファへ Create / Destroy / AddColumn / RemoveColumn / Write を積む（値は積むときにコピー）。`EcsCommandBuffer::Replay(world, 仕事の順のバッファ列)` が、渡された順・各バッファの中は積んだ順に当てる。当てるコマンドが 1 つでもあれば先に `AdvanceVersion()` を 1 回呼ぶ。再生済みのバッファは `Clear()` まで二重に当てない。再生のとき、対象が死んでいる（Generation 違い）コマンドは捨てて `DiscardedStale` に数え、生きた対象を World が拒んだものは `Rejected`（Write は列の大きさと食い違えば拒む）。
  - 作成の予約札: `Create` が `EcsCreateTicket` を返す。再生の前なら `CancelCreate` で取り消せて、その作成は再生されず（`CancelledCreates`）、札を対象にした後続のコマンドは捨てる。再生後は `ResolveCreate` で実体を引く（再生の後の取り消しは false）。札はそれを積んだバッファの番号（コンストラクタの引数）と対にしか効かない。
  - 列の型ごとのフック（`EcsWorld::SetColumnHooks`）: `OnCreate`・`OnDestroy` と理由 `EcsHookReason{Create, ColumnAdded, Destroy, ColumnRemoved}`。行の作成は全列に Create、AddColumn は足した列だけ ColumnAdded、行の削除は全列に Destroy（行が消える前）、RemoveColumn は外す列だけ ColumnRemoved（値が残っているうち）。列の組が変わるだけの Move で残る列と MarkOrphan には、どちらも呼ばない。フックの中の構造変更は拒む。`CreateEntity` は初期値（`EcsColumnValue`、Size が列と一致しなければ拒む）を OnCreate の前に入れる。EcsWorld の破棄では呼ばない。
- テスト: `EcsCommandTest`（EntityTreeTest の束、ctest 登録済み）が、64 の仕事のバッファを 1/2/4/8 本のスレッドで（仕事を取る順も正順・逆順で）積み、番号順に再生した World の状態と集計がすべて一致すること、バッファの渡す順で結果が変わること、取り消した予約が再生されないこと（札を対象にした後続も捨てる・再生後は取り消せない・別バッファの札は無効）、古い識別子（同じ番号の再利用）と二重の削除を捨てること、拒まれるコマンドが何も変えないこと、空の再生は版を進めないこと、フックの理由と回数（AddColumn / RemoveColumn / MarkOrphan / 入れ替え削除で残る列に Destroy が呼ばれない）、再生経由でも Move で Destroy が呼ばれないこと、フックの中の構造変更の拒否を確かめる。
- 確認: 再生の取り消し判定を一時的に外すと `EcsCommandTest` が通らなくなる（assert で止まりタイムアウト）こと、戻すと通ることを見た。
- 結果: `verify-SCN-ECS-CORE-COMMANDS-1.txt`（EntityTreeTest のビルド EXIT=0、警告・エラー 0 件）、`-3-ctest.txt`（`EcsStorageTest`・`EcsChangeTest`・`EcsCommandTest` Passed、`100% tests passed`）。`-2-ctest.txt` は変異を入れる前の同じ実行。EcsMoveTest・EntityTreeTest も Passed。
- Notes: Git Bash は `/m:1` をパスに変換してビルドが失敗するので、verify は PowerShell で走らせる。バッファの番号（札の持ち主）は呼び出し側が重ならないように渡す（スケジューラができたら仕事の番号を使う）。フックの検索は型ごとの線形（フックを付ける型が数個の前提。増えるならアーキタイプごとの索引を足す）。コマンドを積むのは GameThread か、自分の仕事のバッファを持つ並列の仕事だけ（バッファ同士は触れ合わない）。
- 証拠（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-CORE-COMMANDS-1.txt`、`-2-ctest.txt`、`-3-ctest.txt`。

- 反復 10（評価者の差し戻しへの対応）: `Clear()` 後に旧札が新しい予約に作用した（Buffer・Index が同じになるため）。`EcsCreateTicket` に `Generation` を足し、バッファの世代（`m_Generation`）を `Clear()` ごとに進め、`CancelCreate`・`ResolveCreate`・コマンドを積むときの対象解決（`OwnsTicket`）で照合するようにした。Clear 前の札は取り消せず、札宛てのコマンドは積んでも再生で捨てられ（`DiscardedStale`）、新予約の再生後も旧札では解決できない。`EcsCommandTest` に `TestTicketFromBeforeClearDoesNotAffectNewReservation` を足した。
- 結果: `verify-SCN-ECS-CORE-COMMANDS-10a.txt`（EntityTreeTest のビルド exit=0）、`-10b.txt`（`EcsStorageTest`・`EcsChangeTest`・`EcsCommandTest` Passed、`100% tests passed`）。
- Notes: Git Bash から verify を走らせるときは `MSYS_NO_PATHCONV=1` を付ければ `/m:1` が通る。

### SCN-ECS-JOBS（2026-10-11、反復 11・run 20261010-223034）

- 変更: `JobSystem::ParallelFor(count, grain, fn)`（`fn(size_t index)`）を足した（`JobSystem.h/.cpp`。まだどこからも使わない）。1 回の呼び出しごとに共有の文脈（`TSharedPtr`。原子のカーソル・終わった数・最初の例外・呼び手の関数への参照）を作り、呼び手と補助タスクはこのカーソルからだけ区間（grain 個）を CAS で取る。他のキューの仕事は手伝わない。補助は `min(ワーカー数, 区間数 - 1)` 個を HIGH で `SubmitTask` し、拒まれたら以降は出さず呼び手が残りを行う。呼び手はカーソルが尽きた後、他のスレッドが取った区間の終わりを専用の条件変数で待つ（`WaitForAll` は使わない）。カーソルは Count を超えて進まないので、返った後に起きた補助は関数に触れずに抜ける。例外は区間ごとに捕まえて最初の 1 件を保持し、以後まだ始めていない区間は呼ばずに数えだけ進め、全部の終わりを待ってから呼び手で投げ直す。入れ子（`thread_local` の目印）と区間が 1 つだけの場合は、補助を出さずその場で番号の順に直列で実行する（例外はそのまま伝わる）。
- テスト: `JobParallelForTest`（JobSystemShutdownTest の束、ctest 登録済み。ワークスチーリングと単純の両モード）が、個数・区間幅の組ごとに各番号がちょうど 1 回、全ワーカーを長いタスクで塞いでも呼び手のスレッドだけで終わり補助の数がワーカー数以内、返った後に塞ぎを解いて起きた補助が関数を呼ばない、`StopAcceptingTasks` の後でも呼び手だけで終わる、入れ子が同じスレッドで昇順に直列（3 重も）、ワーカーのタスクの中から呼べる、例外の投げ直し（走っている分の終わりを待つ・返った後に何も走らない・全番号が投げても 1 件・`std::exception` でない型・直列の経路・塞がれた呼び手のみ・投げた後も再利用できる）を確かめる。検査は NDEBUG に依らず止まる自前の関数。
- 結果: `verify-SCN-ECS-JOBS-1.txt`（JobSystemShutdownTest のビルド EXIT=0、`JobSystem.cpp`・`JobParallelForTest.cpp` に警告なし）、`-2.txt`（`JobParallelForTest`・`JobSystemShutdownTest` Passed、`100% tests passed`）、`-3-repeat.txt`（直接 40 回、失敗 0）。
- Notes: 補助は 1 区間も取れずに終わることがある（呼び手が先に尽くした場合）。fn の中で `WaitForAll` のようにプール全体を待つ呼び出しはしない前提。`ParallelFor` が使う補助は有限タスク（`SubmitTask`）なので `DrainAcceptedFiniteTasks` の対象になるが、塞がれたキューで待たされるのは補助の後始末だけで、呼び手の完了には影響しない。

- 反復 12（評価者の差し戻しへの対応）: 補助の提出（`Task::Create`／`SubmitTask`）が例外を投げると、カーソルの消化と完了待機を通らずに抜け、受理済みの補助が返った後に関数を呼びうる状態だった。提出ループを try/catch で囲み、例外はバッチの最初の例外として記録して `bFailed` を立て、提出を打ち切る。以降は通常の経路で未着手の区間を関数を呼ばずに数え、走っている分の終わりを待ってから投げ直す。`JobSystem.h` に `friend struct JobParallelForTestAccess` を足した（提出フックをテストから設定する口。`JobSystemTestAccess` は別 TU の定義なので重複させない）。
- テスト: `JobParallelForTest` に `TestSubmitFaultDuringHelperSubmission` を足した。ワーカーを塞ぎ、2 個目の補助の提出（`BeforeTicket`）で例外を注入し、例外が呼び手へ出る・関数が 1 回も呼ばれない・塞ぎを解いて起きた 1 個目の補助が返った後に関数を呼ばない（関数を生かしたまま `callsAfterReturn` を数える）・その後も再利用できることを確かめる。規約所見のとおり、テスト内の `std::vector` を `VariableArray`、`std::string` を `std::strcmp` に置き換え、表示名も日本語にした。
- 確認: 提出ループの捕捉を `catch (int)` に変えた変異版では `callsAfterReturn.load() == 0` の検査で失敗して止まること、戻すと通ることを見た。最初に破棄済みの関数を使う形で書いたテストは変異版でも偶然通ったため、関数を生かして数える形に直した。
- 結果: `verify-SCN-ECS-JOBS-12-3.txt`（JobSystemShutdownTest のビルド EXIT=0、`JobSystem.cpp`・`JobParallelForTest.cpp` に警告なし）、`-12-4.txt`（`JobParallelForTest`・`JobSystemShutdownTest` Passed、`100% tests passed`）、`-12-repeat.txt`（直接 30 回、失敗 0。変異の記録も同じファイル）。
- Notes: 復元したファイルの更新時刻が古いとビルドが変異版のオブジェクトを使い回す。変異を戻したら `LastWriteTime` を進めてから再ビルドする。

### SCN-ECS-SCHEDULER（2026-10-11、反復 13・run 20261010-223034）

- 変更（新規 `Ecs/EcsScheduler.h`・`Private/Ecs/EcsScheduler.cpp`、`Library/Core/CMakeLists.txt` に登録。まだどこからも使わない）:
  - `SystemScheduler(EcsWorld&)`: 段（`AddStage`、足した順に走る）・`SystemDesc{Name, Stage, Reads, Writes, Thread, After, TimeChannel, Query, Run/RunChunk, User}`・`RegisterSystem`・`AddAfter`。衝突は同じ列への W-W・R-W（登録順。直接の After があればそれに従う）。GameThreadOnly はすべてと衝突して単独の波になる。RenderThreadOnly・空/重複の名前・未登録/後ろの段の After・循環する After（`AddAfter` が拒む。段の波を求め直して循環を検出）は拒む。波は依存の深さで決め、波の中は登録順。
  - 実行（`RunStage(段, channelごとのdelta)`）: 波ごとに、チャンクのシステムは全システムの対象チャンクを 1 つの `JobSystem::ParallelFor`（`EcsParallelScope` の中）にまとめ、GameThreadOnly は呼び手のスレッドで単独で走る。システム 1 回ぶんごとに `AdvanceVersion`、波の後に仕事の順で `CommitChanges`。構造変更は仕事ごと（GameThread 系はシステムごと）の `EcsCommandBuffer` に積み、段の終わりの同期点で（システムの登録順 → チャンクの順）に再生してから `SetSyncHook` の関数を呼ぶ。例外は積んだコマンドを捨てて投げ直す（次の実行は正常）。再入は false。
  - 順序付きの GameThread 系（`RegisterOrderedSystem`、呼び先は `AddOrderedEntry`/`RemoveOrderedEntry`/`SetOrderedEntryKey`）: 呼び先を `TickDispatchLess`（群 → 優先度 → 順番、同値は足した順）の順に呼ぶ。群の mask で絞れる。順の一覧を作り直すのは、足す・外す・付け替えがあったときの次の収集の先頭だけ（`SchedulerStats::OrderRebuilds`）。外した呼び先は走っている収集の中でもすぐ呼ばれなくなる。
  - 収集と参加の番号: `BeginCollection`/`EndCollection`/`EcsCollectionScope`。最も外側で番号が進み入れ子は共有する（同じフレームの複数の固定ステップを 1 つの外側の収集で包めば番号を共有できる）。収集の外から `RunStage` を単独で呼ぶと 1 つの収集（単独の固定ステップ相当）。参加の番号 = 「次の収集」の番号（`GetNextCollectionSerial`、呼び先ごとに `GetOrderedEntryJoinSerial`）。収集の中で足した呼び先は次の収集まで呼ばれない。
  - トレース: 統計が有効な構成で、`Ecs.Stage.<段>`・`Ecs.Sys.<系>`・`Ecs.Sync.<段>`（カテゴリ `Ecs`）を `Debug::ScopedStat` で記録する。チャンクの仕事は走ったスレッドで系の名前の区間に入る。名前は登録時に 1 度だけ作った `String`（`GetSystemScopeName` の c_str が変わらない）。統計が無効な構成では `ScopedStat` が何もしない。
  - `paths:` の内側の変更 `Library/Core/Private/Debug/Stats.cpp`: `RecordScope` が `FrameProfile::Events` の上限（256 件）で打ち切ると、トレースにも書かれなくなっていた。上限は Events の保持だけにして、トレースへは全件を書くようにした（チャンクごとの区間が 1 フレームで 256 を超えるため）。
- テスト: `EcsSchedulerTest`（EntityTreeTest の束、ctest 登録済み）が、衝突するシステムが同時に走らない（システムごとの稼働数で W-W・W-R を検査。独立と R-R は同じ波）、波の仕事が 2 システムの 70 チャンクを 1 回の ParallelFor にまたがり各チャンクがちょうど 1 回・版と channel の delta が渡る、GameThreadOnly が呼び手のスレッドで単独（稼働中のチャンクが 0）、RenderThreadOnly と循環する After と重複・未登録を拒む（登録順と逆の After は循環しなければ受け入れる）、段の中の構造変更が同期点まで見えず再生は登録順 → チャンクの順で次の段には見える・例外後も復帰、仕事の書き込みが変更ログに版つきで 1 回ずつ入る、足した呼び先が次の収集まで呼ばれず参加の番号が合う、作り直しが変更のときだけ・付け替えは次の収集から・外した呼び先はすぐ呼ばれない・古い持ち手は効かない、300 件の呼び先の順が `TickDispatchLess` と一致（群の mask 付きと同値の足した順も）、トレースの区間（名前・段の中の入れ子・ワーカーのスレッドの番号が仕事を走らせたスレッドと一致・256 件超でも全件）を確かめる。統計が無効な構成の分岐（トレースが始まらず何も記録されない）も書いた（Debug 構成で一度 `#if 0` に替えてコンパイルが通ることを確認）。
- 確認: 変異で落ちること、戻して通ることを見た（`verify-SCN-ECS-SCHEDULER-mutations.txt`）。W-R の衝突の判定を外す → `GetSystemWave(sysC) == 2` で停止、`RecordScope` の上限の打ち切りを戻す → 300 件のトレースの検査で停止、参加の番号を「いまの収集」にする → `GetOrderedEntryJoinSerial == 1` で停止。
- 結果: `verify-SCN-ECS-SCHEDULER-5.txt`（EntityTreeTest のビルド EXIT=0、警告 0 件）、`-6.txt`（`EcsSchedulerTest`・`TickDispatchTest` Passed、`100% tests passed`）、`-7-repeat.txt`（直接 30 回、失敗 0）、`-8-neighbors.txt`（Ecs*Test 4 本・WorldTickGroup/TimeChannel/EntityTree/SpringArm/TickGroupConfiguration/TransformHierarchy、10/10 Passed）。`-1`〜`-4` は最初の版（衝突の検査の誤りと `fopen` の警告を直す前）。
- Notes:
  - 解釈: 「同じフレームに足した行が次の Tick まで呼ばれない」は、順序付きの系の呼び先（行）のこととした。チャンクのシステムは World の行を走査するので、前の段の同期点で作られた行は同じ Tick の後ろの段から見える（構造変更は同期点まで見えない、で守る）。
  - GameThread 系も構造変更を `ctx.Commands` に積む前提（直接の `CreateEntity` などは段の中でも即時に効く。強制はしていない）。チャンクのシステムが宣言した Reads/Writes と実際の書き込みの一致は検査していない。
  - スロット（変更の束とコマンドバッファ）は段の仕事の総数ぶんをプールして使い回す（縮めない）。チャンクの仕事 1 件ごとに区間を 1 つ記録するので、トレースを取る間は仕事の数に比例して `RecordScope` のロックを取る（トレースを取らないときは `IsTraceActive` で早く抜ける）。WORLD-SCHEDULE で実際の規模を見て、重ければ区間をまとめる。
  - 段の波の計画は `RegisterSystem`/`AddAfter` の後の最初の実行（または `GetWaveCount`/`GetSystemWave`）で作り直す。ComputeWaves は段内のシステム数の 2 乗。
- 証拠（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-SCHEDULER-1.txt`〜`-8-neighbors.txt`、`-mutations.txt`。

- 反復 14（評価者の差し戻しへの対応）: チャンクのシステムが、同期点で作られた行を同じ収集の後ろの段で処理していた。設計 1.4 の「存在と参加を分ける」に従い、ECS の行に参加の番号を持たせた。
  - `EcsWorld`/`EcsChunk`: チャンクの実体の配列の直後に行ごとの参加の番号（uint64）の配列を置く（1 チャンクの行数は 8 byte ぶん減る）。見出しに `MaxJoinSerial` を持ち、`AllRowsJoined(収集の番号)`／`IsRowJoined(行, 収集の番号)` で判定する。作った行は `SetCreateJoinSerial` の番号（既定 0 = 最初から参加）を受け取り、入れ替え削除（`RemoveRow`）・列の追加と削除・孤児の印（`MoveRow`）で動いても番号を保つ。`GetJoinSerial(実体)` で引ける。行の存在（クエリ・`IsAlive`・値）は参加に依らず同期点の後から見える。
  - `SystemScheduler`: 構築時と最も外側の `BeginCollection` で `SetCreateJoinSerial(次の収集の番号)` を World に渡す。`EcsChunkJob` に `CollectionSerial`・`AllRowsJoined()`・`IsRowJoined(row)` を足した（チャンクのシステムが未参加の行を飛ばす口）。
  - テスト: `EcsSchedulerTest` に `TestRowsCreatedAtSyncJoinAtNextCollection` を足した。同じ収集の `Build`→`Use` で同期点の 4 行が呼ばれない（既存 5 行は呼ばれる）、次の収集から 9 行すべて、同じフレームの複数の固定ステップを包んだ外側の収集で番号を共有（2 回作っても次の収集まで呼ばれない）、入れ替え削除・列の追加・孤児の印の後も動いた行の参加の番号が保たれる。
- 確認: 収集の先頭で World へ番号を渡す行を外した変異では `EcsSchedulerTest` が `joined.Joined == 5 && joined.Skipped == 4` で止まり、戻すと通る（`verify-SCN-ECS-SCHEDULER-14-mutation.txt`）。
- 結果: `verify-SCN-ECS-SCHEDULER-14-1.txt`（EntityTreeTest のビルド EXIT=0、警告 0 件）、`-14-2.txt`（Ecs 系 4 本・EntityTreeTest・TickDispatchTest の 7 本 Passed）、`-14-3.txt`・`-14-4.txt`（変異を戻した後の再ビルドと `EcsSchedulerTest`・`TickDispatchTest` 2/2 Passed）。
- Notes: 参加の番号の運用は `World::Tick` と `DispatchFixedTick` への配線のとき（WORLD-SCHEDULE）に、通常の Tick と単独の固定ステップの両方を `EcsCollectionScope` で包んで合わせる。`MaxJoinSerial` はチャンクを確保し直すまで下がらない（次の収集で全行が参加した後は `AllRowsJoined` が true に戻る）。

### SCN-ECS-WORLD-SCHEDULE（2026-10-11、反復 15・run 20261010-223034）

- 変更（`World.h/.cpp`・`ApplicationProcessor.cpp`）:
  - `World` が `EcsWorld` と `SystemScheduler` を持つ（`GetEcsWorld()`・`GetScheduler()`。コンストラクタ 3 種から `InitializeSchedule()`）。段は `Tick`・`LateTick`・`FixedBegin`・`FixedTick`・`FixedEnd`・`Sync`。従来の処理は各段の GameThreadOnly のシステムとして、元の順・位置で呼ぶ: Tick = スナップショット作成 → UpdateWorldTransforms → 群の配信（Input〜PoseFinalize）→ cleanup。LateTick = UpdateWorldTransforms → PostPhysics → UpdateRenderTransforms → Camera〜PreRender → cleanup → スナップショット破棄。FixedBegin = UpdateWorldTransforms → 補間履歴の準備。FixedTick = 従来の `DispatchFixedTick` の本体。FixedEnd = UpdateWorldTransforms → 補間履歴の記録 → cleanup。Sync = `SyncToSceneView` の本体。呼び出しごとの引数（FrameTimes・delta・固定 delta・Materials/Meshes）は World のメンバに置いて、システムが読む。
  - `Tick`/`LateTick`/`DispatchFixedTick`/`SyncToSceneView` は、従来の早期 return（未初期化・配信中）を保ったまま段を走らせる。`SyncToSceneView` は段の最中に呼ばれたときだけ段を通さず本体を直接呼ぶ（再入の保険）。
  - 参加の番号: `Tick` で収集を開き、`LateTick` の終わりで閉じる（フレームで 1 つ。間の固定ステップはこの番号を共有）。LateTick が来ないまま次の Tick が来たとき、`Finalize` のときは先に閉じる。単独の `DispatchFixedTick` は `RunStage` が 1 つの収集として番号を進める。`ApplicationProcessor::AdvanceFixedSimulation` は、複数の固定ステップを `EcsCollectionScope` で包んで番号を共有し、1 ステップを `BeginFixedStep` → Module PreFixedTick → `DispatchFixedTick` → Module FixedTick → `EndFixedStep` で回す（従来の呼び順のまま）。
- 結果: `verify-SCN-ECS-WORLD-SCHEDULE-5.txt`（EntityTreeTest・ApplicationFixedStepPipelineTest のビルド EXIT=0）、`-6.txt`（WorldTickGroupTest・WorldTimeChannelTest・WorldRenderInterpolationTest・ApplicationFixedStepPipelineTest・RenderInterpolationCadenceTest 5/5 Passed）、`-4-capture.txt`（起動画面の決定的な比較 default/near/low とも within_limits=True、`result=pass`）。`-1`・`-2` は最初のビルド（`ThreadPolicy` の名前空間の誤りを直す前後）と同じテストの実行。
- 確認: `paths:` の外のため追加はしなかったが、一時的に `WorldTickGroupTest` へ「段の並び（システムの波の番号が登録順）・Tick〜LateTick が 1 収集・Tick が重なっても収集が重ならない・単独の固定ステップが番号を 1 進める・外側の収集で複数の固定ステップが番号を共有・Finalize で閉じる」を足して通ることを確かめ（`verify-SCN-ECS-WORLD-SCHEDULE-3-adhoc.txt`）、戻した。
- Notes: 段の再入（RunStage の最中の RunStage）は false を返すだけで処理は走らない（Tick 系は従来も配信中は早期 return）。恒久のテストは `paths:` を広げる別のタスクで足すとよい。名前（`Ecs.Sys.Tick.*` など）は SCN-ECS-SCOPE-METRICS の検査に使える。

### SCN-ECS-SCOPE-METRICS（2026-10-11、反復 16・run 20261010-223034）

- 変更（`Scripts/CaptureScene.ps1`・`Scripts/CheckCaptureMetrics.py`）:
  - `gpu_timing.cpu_scope_ms`: トレースの全部の区間を名前ごとに、フレームあたりの合計（全スレッド）の `median_ms`・`p95_ms`・`max_ms`、区間が出たスレッドの数 `threads`、出たフレーム数 `frames_present`、記録件数 `samples` で書く（上位 15 をやめた。旧 `cpu_scope_median_ms` は廃止）。窓は従来どおり（窓の最初〜最後のフレーム番号の範囲。長さは `cpu_scope_frames`）。範囲内でその名前が出なかったフレームは 0 で数える。
  - `gpu_timing.cpu_scope_group_ms`: 名前の先頭 2 つの部分が同じで 3 部分以上の名前を `A.B.*` ごとにフレームで合計した値（`Ecs.Stage.*`・`Ecs.Sys.*`・`Ecs.Sync.*`・`RG.Record.*` など）。入れ子の区間は二重に数える（`Ecs.Sys.*` は `Ecs.Stage.*` の中身）。
  - `gpu_timing.gpu_pass_ms`: パスごとの median・p95・max を全部（既存の `pass_ms`・`pass_median_ms` は他の項目が使うので残した）。
  - 標準出力: `cpu_scope_median`（上位 15 の median/max/スレッド数と区間の総数）と `cpu_scope_group` の行。
  - `CheckCaptureMetrics.py`: `--require-scope`・`--require-pass`（繰り返し可。完全一致、末尾 `*` などのワイルドカードは前方一致。区間は個別の名前とグループの両方を探す。無ければ失敗）、`--max-scope-median-ms`・`--max-scope-p95-ms`・`--max-pass-median-ms`・`--max-pass-p95-ms`（`<名前>=<ms>`、繰り返し可。一致する区間・パスが 1 つでも超えれば失敗。一致が無い指定も失敗）。
- 結果: `verify-SCN-ECS-SCOPE-METRICS-1.txt`（ビルド pass、2000 個・120 フレームの撮影 pass、34 区間。`Ecs.Stage.Tick`・`Ecs.Stage.Sync`・`Ecs.Sys.Sync.SyncToSceneView` など、グループ `Ecs.Stage.*`・`Ecs.Sys.*`・`Ecs.Sync.*`）、`-2.txt`（`--require-scope Ecs.Stage.*`・`Ecs.Sync.*`・`--require-pass MegaGeometry` が pass）、`-3.txt`（存在しない区間で rc=1 を確認）、`-4-limits.txt`（閾値を通す・超える・存在しない名前の 5 通り）。
- Notes: このシーンでは `RG.Record.*` の区間はまだ出ない（出れば `cpu_scope_group_ms` に入る）。`Ecs.Stage.Sync`（段の名前が Sync）と `Ecs.Sync.*`（段の同期点の区間）は別物。チャンクの仕事の区間はこの規模では出ていない（`Ecs.Sys.*` は GameThread 系のみ）。

### SCN-ECS-SCOPE-METRICS 差し戻し対応（2026-10-11、反復 17・run 20261010-223034）

- 反復 16 の NEEDS_WORK 2 点を直した。(1) `CheckCaptureMetrics.py`: 名前の検査（`--require-*`・`--max-scope-*`・`--max-pass-*`）を `--timing` の外へ出し、指定したら `--timing` が無くても必ず見る（`gpu_timing` が無ければ「無い」で失敗）。(2) `CaptureScene.ps1`: `ConvertTo-ScopeStatList` の戻りを `@(...)` で包み、0 件・1 件でも JSON が配列になる。検査側も辞書 1 件を配列として扱う。
- 結果: `verify-SCN-ECS-SCOPE-METRICS-1.txt`（撮影 pass、`cpu_scope_ms` 34・`cpu_scope_group_ms` 3・`gpu_pass_ms` 63 の配列）、`-2.txt`（pass）、`-3.txt`（存在しない区間で失敗＝期待どおり）。`--timing` 無しでも、存在しない区間は失敗・低い閾値は失敗・正しい指定は pass を確認。単一オブジェクト形の metrics でも落ちない。

### SCN-ECS-CLEANUP-LIST（2026-10-11、反復 18・run 20261010-223034）

- 変更（`IUnknown.h`・`World.h/.cpp`・`Object.cpp`・`Entity.cpp`）:
  - `World` が破棄予約の一覧（`m_PendingDestroyEntities`・`m_PendingDestroyComponents`、積んだ順）を持つ。載っているものは新しい旗 `OF_InDestroyList` で分かる（重複して積まない。外すときの探索は旗が立っているときだけ）。`RegisterPendingDestroy`・`ForgetPendingDestroy`・`ForgetPendingDestroySubtree` を公開。
  - `Object::Destroy` が、World の木にいる Entity・Component を一覧へ積む（`Entity::MarkForDestroy`・`Component::MarkForDestroy`・`World::RemoveEntity` などの遅延経路はすべてここを通る。`Component.h` は `paths:` の外なので、入口は `Object::Destroy` に一本化した）。
  - `CleanupDestroyedObjects` は一覧だけを処理する（全木の 2 回の走査をやめた）。Component は「所有する Entity と祖先が予約されていない」ものだけを先に外し、Entity は「祖先が予約されていない」最上位だけを部分木ごと破棄する。判定は破棄の前に全部済ませる。`InvalidateTickComponent` が待ち参照を nullptr にする仕組み（`m_PendingTickComponents`）は従来どおり残し、除去ループで null を飛ばす。
  - 一覧のポインタが壊れないよう、`Entity::RemoveInner`（Component・部分木）・`DestroyEntitySubtree`（部分木）・`Entity::Finalize`（自身）・`World::Finalize`（一覧を空に）で外す。`Entity::AddInner` で予約済みの Component を木に戻したとき、`AssignFreshObjectIdsRecursive`（World に入る部分木）で木に入る前に予約されたものを積む。
  - 検査 `WORLD_CLEANUP`（`--engine-checks=WORLD_CLEANUP`）: 有効なときだけ、従来の全木走査（`CollectPendingComponents`・`CollectPendingDestroyRecursive`）で見つけた予約と、一覧から求めた処理対象が順序を問わず一致するかを照合し、`ENGINE_CHECK` の行を出す。破棄の無い回は最初の 1 回だけ pass（count=0）を出し、破棄がある回は毎回出す。
- 結果（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-CLEANUP-LIST-1.txt`（EntityTreeTest のビルド EXIT=0）、`-2.txt`（`WorldTickGroupTest`・`EntityTreeTest` 2/2 Passed）、`-7-rendering3dtest.txt`・`-7-mega-bananas.txt`・`-7-mega-stress.txt`（周回スモーク 3 周が各 `result=pass`、`engine_check name=WORLD_CLEANUP pass=1`）、`-6-adhoc-check.txt`（検査を一時的に常時有効にして `WorldTickGroupTest`・`EntityTreeTest` を走らせた出力。破棄のある回で `WCPASS_1`・`WCPASS_2` が出て、不一致の `WCFAIL` は 0。恒久の変更ではない）。
- 最初に `WorldTickGroupTest` が SegFault した（除去ループの null 判定を落としていた）。`-1`・`-2` は直した後の実行。
- Notes:
  - 検証コマンドの調整（`paths:` の内側）: 3 シーンを 1 回の周回に入れると、検査と無関係に `SCENE_CYCLE_DIFF field=textures`（1 周目の 0 → 2 周目以降の 9 など、別シーンが先に読んだテクスチャのキャッシュ）で失敗する。変更前（`git stash`）でも同じ 8 件で落ちる（`verify-SCN-ECS-CLEANUP-LIST-3.txt` が変更後、`-5-baseline.txt` が変更前）。シーンごとの周回は pass するので、`TASKS.md` の verify を 3 シーンの個別 3 コマンドに分けた。混在の周回のテクスチャ差は別件（このタスクの範囲外）。
  - 3 シーンは破棄予約を使わない（`RemoveObject` の即時破棄だけ）ため、スモークの検査は count=0 の pass。破棄のある照合は `-6-adhoc-check.txt` が根拠。恒久の「破棄の契約」のテスト（一覧が壊れない・予約済み Component を木に戻す・World に入る前の予約）は `Test/` が `paths:` の外のため足していない。必要なら別タスクで `WorldTickGroupTest` に足す。
  - 一覧への積み込みは GameThread 前提（`Object::Destroy` を別スレッドから呼ばない）。`-4-neighbors.txt` は ObjectHeapTest 以外の実行ファイルが再ビルドされていない古い物なので証拠にしない。

### SCN-ECS-CLEANUP-LIST 差し戻し対応（2026-10-11、反復 19・run 20261010-223034）

- 反復 18 の NEEDS_WORK 3 点を直した。
  1. 除去の通知中の再予約: `World::DestroyEntitySubtree` の `ForgetPendingDestroySubtree` を、`OnRemovedFromWorld` の通知と待ち参照の無効化が済んだ後・木を切り離す直前へ移した（通知内で `MarkForDestroy` しても一覧に残らない）。
  2. ObjectHeap 経由の予約: `ObjectHeap::EnqueueDestroy` がフラグを直接立てるのをやめ、`Object::Destroy()` を通す（World の木にいる Entity・Component は一覧へ積まれる）。`ObjectHeap.cpp` は BOM なしの ASCII だけだったので、日本語コメントの文字コード警告（C4819）を避けて BOM を付けた。
  3. 公開 `RemoveInner`: `World::RemoveInner` を足し、切り離す前に Entity なら部分木、Component なら単体の待ち参照の無効化と一覧からの解除を行う。
- 契約テスト（`WorldTickGroupTest.cpp` に 3 本。`paths:` に `Test/Core/Object/WorldTickGroupTest.cpp` を足した。差し戻しの「各経路を契約テストに追加」と done-when の「破棄の契約のテスト」を満たすために必要な最小の拡張）: 通知中の自己予約が一覧に残らない、heap 経由の Component 予約が一覧へ入り World の回収で木から外れる、公開 `RemoveInner` で切り離した部分木が一覧から外れる。一覧の件数は `WorldTickGroupTestAccess`（既存の friend）経由で見る。
- 結果（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-CLEANUP-LIST-10.txt`（EntityTreeTest のビルド EXIT=0、`WorldTickGroupTest`・`EntityTreeTest` 2/2 Passed）、`-11-rendering3dtest.txt`・`-11-mega-bananas.txt`・`-11-mega-stress.txt`（周回 3 周が各 `result=pass`、`engine_check name=WORLD_CLEANUP pass=1`）。`-8`・`-9` は直す途中の実行（ビルド引数の誤り・テストの落下）で証拠にしない。
- Notes:
  - 修正前に新テストが落ちることは、再ビルド（Core）の負荷を避けて実行していない。各テストの最初の件数の検査は、修正前のコードでは 1 以上（通知中の再登録・heap 経路が未登録・`RemoveInner` が一覧を残す）になる構造。
  - 公開 `World::RemoveInner` は従来どおり `ComponentDataRegistry` の登録を解除しない（`DestroyEntitySubtree` だけが解除する）。木から外した Entity を削除する呼び出し側は解除が要る（テストでは明示している）。この反復の範囲外の既存の性質。
  - ファイルの行末: `World.cpp`・`World.h` は混在（CRLF/LF）で、編集ツールが LF に正規化したため difflib で元の行末へ戻した（`git diff --numstat` と `--ignore-cr-at-eol` が一致）。

### SCN-ECS-PROP-STORAGE（2026-10-11、反復 20・run 20261010-223034）

- 変更:
  - `RuntimeSchema.h`: `StorageKind::EcsColumn` を末尾に足した（既存の値は不変）。`PropertyDesc` に `Source`（元の `ClassProperty`）を足し、`Get`/`Set` の関数ポインタの第 1 引数を `const PropertyDesc &` にした（関数ポインタは文脈を持てないため。これまで `Get`/`Set` を呼ぶ側は無かった）。
  - `IClass.h`: `ClassProperty` に `GetStorageKind()`（既定 Member）・`GetPropertyValue()`・`MoveValueToColumn()`・`MoveValueToFallback()` を足した。`TEcsColumnProperty<T>`・`EcsPropertyRef<T>`（取得 `Get()`/`operator T` と代入だけ。`operator->`/`operator*` は無い）・`ConstEcsPropertyRef<T>` を足した。`BuildClassInfoSnapshot` は `Storage` を `GetStorageKind()` から取り、`Source`/`Get`/`Set` を全プロパティで埋める。クラス単位の移動の口として `MoveEcsPropertyValuesToColumns(IUnknown*)`・`MoveEcsPropertyValuesToFallback(IUnknown*)` を足した（BIND-ENTITY が行を付ける直後・消す直前に呼ぶ想定）。
  - `Reflection.h`: `ECS_PROPERTY(Column, Field, ValueType, Name)` と `PropertyRegistry::RegisterEcsColumn`。クラスに `Ecs::EcsRowRef GetEcsRow() const` が要る。`TPropertyValue` は作らず、予備の値は `m_EcsFallback_<Name>`。マクロの引数名を `Type` にすると `EcsColumnInfo::Type` の `.Type` まで置換されるので `ValueType` にした。
  - `Ecs`: `EcsRowRef`（World + Entity）、読み書きの口 `EcsReadColumnPart`・`EcsWriteColumnPart`・`EcsRowHasColumn`・`EcsGetColumnPartPtr`（`EcsTypes.h/.cpp`。並列の中で呼ぶと assert）、`EcsWorld::WriteColumnPart`（列の一部だけ書いて `WriteColumnValue` と同じく変更を記録。`WriteColumnValue` はこれに通した）。
  - 値の置き場: 行が付いていてその行が列を持つ間は列（Field の位置）、それ以外は予備。移すとき予備は `T{}` に戻す（値は 1 か所）。書き込みは `ApplyValue`/`CopyValueFrom`/`ApplyDefaultValue`/代入だけで版を付ける。可変の `GetValuePtr` は常に nullptr、const は列の中（なければ予備）を指す。
- テスト: `EcsPropertyStorageTest`（ObjectReflectionTest の束、`Test/Core/Object/CMakeLists.txt` に MEMBER と add_test）。テスト用の Component で、置き場とスキーマ記述（Stable ID・Get/Set）、予備→列→予備の移動と「予備に値が残らない」こと、`ApplyValue`・`CopyValueFrom`（行ありから行なしも）・`CopyEditableProperties`・`ApplyDefaultValue(s)` が変更ログに新しい版で 1 件ずつ積まれ型違いの値は版を付けないこと、可変 `GetValuePtr`/`GetPropertyValue(名前)` が nullptr、`BuildObjectSnapshot`→直列化→`DeserializeStable`→`ApplyValue` の往復（行なし・別の行のどちらにも）で値と Stable ID が変わらないこと、スキーマ投影の Storage が EcsColumn で Stable ID が行の有無に依らないこと、を確かめる。
- 結果（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-PROP-STORAGE-2.txt`（ObjectReflectionTest・SceneSerializerTest のビルド EXIT=0）、`-5.txt`（EcsPropertyStorageTest・RuntimeSchemaSnapshotTest・SchemaProjectionTest・SceneSerializerTest・SceneFileRoundTripTest 5/5 Passed）。`-1` は Git Bash の `/m:1` 変換でのビルド引数の誤り、`-3` はテストの誤り（行を付け直すと予備の 0 で列が上書きされる）の実行で証拠にしない。`-4` は直した後の再ビルドと単体の実行。
- Notes:
  - 「保存の往復」は、`SceneSerializer` の World 経由ではなくスナップショットの直列化値の往復で確かめた（テスト用の Component を World に付ける経路は BIND 系で行が付いてから）。既存の `SceneSerializerTest`/`SceneFileRoundTripTest` は変更なしで通る。
  - `Library/Core/CMakeLists.txt` は `paths:` の外のため、新しい .cpp/.h は足していない（既存の `EcsTypes.*`・`EcsWorld.*`・`IClass.h`・`Reflection.h` に入れた）。
  - ビルドは Git Bash から `cmake ... -- /m:1` を呼ぶと `m:1` に化けて失敗する。PowerShell から呼ぶ。
  - 行末: `IClass.h`・`Reflection.h`・`TASKS.md`・`PROGRESS.md` は混在。編集ツールが全行を CRLF に寄せたので difflib で元に戻した（numstat 2 通りが一致）。

### SCN-ECS-PROP-STORAGE 差し戻し対応（2026-10-11、反復 21・run 20261010-223034）

- 反復 20 の NEEDS_WORK 2 点を直した。
  1. 並列中アクセスの検出: `Ecs::Detail::EcsCheckSerialAccess()` を足した（`EcsTypes.h/.cpp`）。開発ビルド（`NORVES_BUILD_DEVELOPMENT`。Debug・RelWithDebInfo）では `NDEBUG` に依らず、並列の中なら報告して false を返す（ハンドラが無ければ `ECS_PARALLEL_PROPERTY_ACCESS` を出して abort）。`EcsGetColumnPartPtr`・`EcsWriteColumnPart`・`EcsRowHasColumn` の通常の `assert` をこれに置き換え、`TEcsColumnProperty::Write` も行なし（予備への書き込み）の前に通す。false のときは列にも予備にも書かない。Release は検査なし（デバッグ機能を入れない方針）。テスト用に `EcsSetParallelAccessHandler`・`EcsGetParallelAccessReportCount`。
  2. `Reflection.h` の `RegisterEcsColumn` の `std::make_shared` を `Container::MakeShared` にした（既存の `Register` の `std::make_shared` は今回の範囲外）。
  - 副次: `Read` の `EcsReadColumnPart` の戻り値破棄の警告（C4834）を `(void)` で消した。
- 負例テスト `TestParallelAccessRejected`（`EcsPropertyStorageTest.cpp`。`NORVES_BUILD_DEVELOPMENT` のときだけ）: 行あり（列）は `ApplyValue` が失敗し列の値・版が変わらない、行なし（予備）は `ApplyValue`・`CopyValueFrom` が失敗し `ApplyDefaultValue` でも予備が変わらない、いずれも報告数が増える。並列を出れば書ける。
- 結果（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-PROP-STORAGE-8.txt`（ObjectReflectionTest・SceneSerializerTest のビルド EXIT=0）、`-9.txt`（指定 5 テスト 5/5 Passed）。`-6` は文字列リテラルに生の改行を混ぜたビルド失敗、`-7` は警告を直す前のビルドで証拠にしない。
- Notes: RelWithDebInfo の abort 経路（ハンドラ無し）は、プロセスが落ちるためテストしていない。ハンドラ経由の報告・拒否と同じ関数を通る。

### SCN-ECS-BIND-ENTITY（2026-10-11、反復 22・run 20261010-223034）

- 変更:
  - `EcsTypes.h`: 行の列 `EcsObjectRef{Object, Generation}`（Object への弱い参照）と `EcsHierarchy{Parent, FirstChild, LastChild, NextSibling, PrevSibling, Depth}` を足した。設計の `{Parent, FirstChild, NextSibling, Depth}` に `LastChild`・`PrevSibling` を足した（末尾への追加と途中の除去を子の数に依らず一定の費用にするため）。根（World 直下）は兄弟の連結を持たない。
  - `Entity`: `GetEcsEntity()`・`GetEcsRow()`（`EcsRowRef`）と、World が付け外しする行 `m_EcsEntity`。
  - `World`: 全 Entity の行用のアーキタイプ（EcsObjectRef + EcsHierarchy）を `InitializeSchedule` で作る。`BindEntitySubtreeRows`（親から順に行を作り、親の子の連結の末尾へ付ける。`AttachRootEntity`・`AttachChildEntity` で Outer/Inner を結んだ直後・BeginPlay の前）、`UnbindEntitySubtreeRows`（最上位だけ親の連結から外し、子から順に `ReleaseEntityRow`）、`RelinkEntityRow`（`MoveEntityInnerAtomic` の成功後。古い親の連結から外し、新しい親の末尾へ付け、部分木の深さを直す）。
  - 解放の前に行を切る入口: `DestroyEntitySubtree`（通知の後・木から切り離す前）、`World::RemoveInner`（公開）、`Entity::RemoveInner`（子 Entity。ObjectHeap の `ReleaseSlot` もここを通る）、`Entity::Finalize`（木にいるまま解放される場合）。どれも idempotent（行が無ければ何もしない）。
  - `ReleaseEntityRow`: EcsObjectRef を null にして孤児の印を付け、段の外なら行を即時に消す。段の最中（`SystemScheduler::IsRunning()`）は `m_DeferredEntityRows` に積み、各段の同期点（`SetSyncHook`）で消す。並列の段の中は assert。
  - 検査 `ECS_BIND`: `World::VerifyEcsBinding()`（公開。段の外で呼ぶ）が、木を歩いて Entity ごとに「行が生きていて孤児でない・EcsObjectRef が Entity を指す・Parent/Depth が Outer と一致・子の連結（FirstChild/LastChild/兄弟）が Inner の並びと一致」を確かめ、行の側からも EcsObjectRef を持つ行（孤児を含む）の数が Entity 数と等しく、孤児と同期点待ちが 0 であることを確かめる。`--engine-checks=ECS_BIND` のとき、LateTick の後に結び付けの変化（`GetEcsBindingEpoch`）があった回だけ照合して `ENGINE_CHECK name=ECS_BIND` を出す。`World::Finalize` では必ず行い、行が 0 に戻ることも見る。
- テスト `EcsLifetimeTest`（EntityTreeTest の束、`Test/Core/Object/CMakeLists.txt` に MEMBER と add_test）: 付与と階層・付け替え（中ほどの兄弟・根へ・根から子へ。深さと連結）・`RemoveEntity`/`RemoveObject` で EndPlay の時点に行が消えていること・Tick と LateTick の間の除去・破棄予約の直後の除去（一覧と行が残らない）・段の最中の回収（EndPlay の時点で孤児かつ参照 null、祖先と子孫を両方予約、同期点で消える）・heap が持つ Component の遅延回収（行は除去の時点で消え、heap の回収は後）・公開 `RemoveInner` と付け直し・古い識別子の拒否・作って消す周回で 0 に戻ること。
- 結果（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-BIND-ENTITY-1.txt`（EntityTreeTest のビルド EXIT=0）、`-2.txt`（EcsLifetimeTest・WorldTickGroupTest・EntityTreeTest・TransformHierarchyTest 4/4 Passed）、`-4-rendering3dtest.txt`・`-4-mega-bananas.txt`・`-4-mega-stress.txt`（周回 3 周が各 `result=pass`、`engine_check name=ECS_BIND pass=14/9/11`、fail 0）。`-3.txt` は 3 シーンを 1 回の周回にしたもので、`ECS_BIND pass=31`・fail 0 だが、前回（CLEANUP-LIST）と同じ別シーンのテクスチャキャッシュ差（`SCENE_CYCLE_DIFF field=textures`）で失敗する（検査と無関係。証拠にしない）。このため `TASKS.md` の smoke の verify を 3 シーンの個別 3 コマンドにした。
- Notes:
  - 構造変更は段の最中でも即時（作成は参加の番号が「次の収集」になるので処理は次から）。設計 1.4 の「作成の予約札」は入れていない: Object の生成は即時で、行は Attach で同期的にできるため。段の中の消去だけ同期点へ遅らせている。チャンクを走査するシステムが入る TRANSFORM 系で、走査中に Object を解放する経路が無いことを改めて確かめること。
  - Component の行・flags・時間倍率は BIND-FLAGS。値は Object にある（このタスクは行と階層だけ）。
  - `Hierarchy` の書き込みは `WriteColumnValue`（変更として記録）を通す。まだ追跡する列にはしていない（TRANSFORM 系で消費者を足すとき `SetColumnTracked` を最初のアーキタイプの前に呼ぶ必要がある）。
  - 行末: `World.h`・`World.cpp` は混在。編集は Python で行い、`difflib` で元の行末に揃えた（numstat 2 通りが一致）。

### SCN-ECS-BIND-ENTITY 差し戻し対応（2026-10-11、反復 23・run 20261010-223034）

- 反復 22 の NEEDS_WORK 3 点への対応。
  1. 設計 1.4 の作成予約・取消し（実装）: 段の最中（`SystemScheduler::IsRunning()`）の `BindEntitySubtreeRows` は行を作らず、World が持つ `EcsCommandBuffer`（`m_EntityCreateCommands`）へ作成を積んで予約札を取り、Entity が予約の位置（`m_EcsCreateSlot`）を持つ。段の外は従来どおり即時。`ReleaseEntityRow` は札が再生前なら `CancelCreate` して予約の項目を空にする（行が無いので消すものも無い）。同期点（`FlushDeferredEntityRows` → `PlaybackEntityRowCreates`）で `Replay` し、`ResolveCreate` で引いた行に `EcsObjectRef` を書き、親を `RebuildEntityRowChildren` で Inner の並びから連結・深さごと作り直す（祖先の少ない親から順）。予約中の親の下へ付け替えた既存の行は `m_PendingEntityRelinks` に積み、親の行ができてから結び直す。再生に失敗した Entity は行なしで残し（値は Object）、`LOG_ERROR` と、`engine-checks=ECS_PLAYBACK` のとき `ENGINE_CHECK name=ECS_PLAYBACK` を出す。`VerifyEcsBinding` は再生されていない予約・待たせた付け替えが残っていれば fail にする。
  2. `EcsLifetimeTest` に 2 本足した: `TestReservedCreateAndCancelInStage`（Tick の中で Attach・付け替え・RemoveEntity。段の最中は行なし、同期点の後に行と階層（兄弟の並び・深さ・予約中の親へ付け替えた既存の行）ができ、再生前に破棄した 2 つは作られない）、`TestDestroyNowBetweenTickAndLateTick`（Tick の後の実際の `ObjectHeap::DestroyNow`。持ち主が生きている間は EndPlay の時点で行は孤児でなく階層も不変、持ち主を除去した後の DestroyNow でも行・孤児が残らない）。heap が持つ Entity は `ContainsHeapOwnedEntity` で World に付けられないため、`DestroyNow` の対象は heap の Component。
  3. 英語の文言: `World.cpp` の `LOG_ERROR` 2 件（アーキタイプ・行の作成失敗）と `EcsLifetimeTest.cpp` の完了表示を日本語にした。
- 結果（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-BIND-ENTITY-5.txt`（EntityTreeTest のビルド EXIT=0）、`-6.txt`（EcsLifetimeTest・WorldTickGroupTest・EntityTreeTest・TransformHierarchyTest 4/4 Passed）、`-7-rendering3dtest.txt`・`-7-mega-bananas.txt`・`-7-mega-stress.txt`（周回 3 周が各 `result=pass`、`engine_check name=ECS_BIND pass=14/9/11`）。`-8-combined.txt` は元の 3 シーン混在のコマンドで、`ECS_BIND pass=31`・fail 0 だが `SCENE_CYCLE_SMOKE result=fail`（`changed=textures,texture_bytes`、1 周目の基準が別シーンより前に読んだテクスチャのキャッシュ分だけ小さい）。
- Notes:
  - 混在の周回の失敗は変更前でも同じ（CLEANUP-LIST の `-5-baseline.txt`）。原因は `Game` 側の `SceneCycleSmoke` の判定（1 周目を基準にする）で、`paths:` の外。`TASKS.md` の smoke の verify は 3 シーンの個別 3 コマンドのままにした（検査の合否は同じ行を見る）。混在コマンドを正とするか、`SceneCycleSmoke` の基準を直すかは人の判断（別タスク）。
  - 作成の予約は段の最中の Attach*/ReparentEntity だけが通る。TRANSFORM 系などチャンクを走査するシステムが入るときは、走査中に Object を付ける・解放する経路が無いことを改めて確かめること（並列の中は assert）。
  - 行末: `World.cpp`（混在）は difflib で元の行末に揃えた。`sed -i` は CRLF を落とすので Windows では使わない（`EcsLifetimeTest.cpp` で起きて CRLF に戻した）。numstat 2 通りが一致。

### SCN-ECS-BIND-FLAGS（2026-10-11、反復 24・run 20261010-223034）

- 変更:
  - `EcsTypes.h`: 列 `EcsEntityFlags{bActive, bTickEnabled, bPendingDestroy}`・`EcsTimeDilation`・`EcsComponentFlags{bEnabled, bTickEnabled}`・`EcsTickConfig{Group, Priority, Mask}` を足した。`EcsHierarchy` に `FirstComponent`・`LastComponent`・`Kind`（Entity / Component）を足した。
  - `Entity`: `bTickEnabled`・`bActive`・`bPendingDestroy` を `ECS_PROPERTY`（`EcsEntityFlags`）にした。`GetCustomTimeDilation`/`SetCustomTimeDilation` は行が付いている間は列（`EcsTimeDilation`）を読み書きし、無い間は `m_CustomTimeDilation`（予備）を使う。`AddInner`（Component）で行を作り、`RemoveInner`（Component）で行を切る前に値を予備へ戻す。
  - `Component`: `bEnabled`・`bTickEnabled` を `ECS_PROPERTY`（`EcsComponentFlags`）にした。`TickGroupConfiguration` は読み書きの口（`LoadTickConfiguration`/`StoreTickConfiguration`）を通し、行が付いている間は列（`EcsTickConfig`）、無い間は `m_TickConfiguration` が正。検証（主群を変えると mask が 1 群に戻る等）は従来の `TickGroupConfiguration` に任せたまま。`GetEcsRow()`・`m_EcsEntity`・`m_EcsCreateSlot` を足した。
  - `World`: Component 用のアーキタイプ（EcsObjectRef + EcsHierarchy + EcsComponentFlags + EcsTickConfig）。Entity の行は 4 列（+ EcsEntityFlags・EcsTimeDilation）。`BindComponentRow`（所有者が段の最中・予約中なら作成を予約）・`ReleaseComponentRow`（連結から外し、値を予備へ戻し、孤児にして消す。部分木の解放では連結を直さない）。行を付けた直後に `MoveEntityValuesToRow`/`MoveComponentValuesToRow`、消す前に `...ToFallback`（ECS_PROPERTY は `MoveEcsPropertyValuesToColumns`/`...ToFallback`、時間倍率と Tick の設定は直接）。作成の予約（`PendingEntityCreate`）は Entity と Component の両方を持ち、同期点の再生で `ObjectRef` と値を書き、`RebuildEntityRowChildren` が子 Entity の連結と Component の連結を Inner の並びから作り直す。
  - 階層: Component の行は所有者の子の葉（`Kind=Component`、`Parent`=所有者、`Depth`=所有者+1）で、所有者の `FirstComponent`〜`LastComponent` の別の連結に Inner の並び（Component だけ）で入る。子 Entity の連結（`FirstChild`〜）には入らない（既存の Entity 階層の契約・テストを保つため）。
  - 検査 `ECS_BIND`: Component の行も照合する（行・孤児でない・`EcsObjectRef`・`Kind`/`Parent`/`Depth`・葉・Component の連結が Inner の並びと一致、行の総数 = Entity + Component）。
  - 旧の配信は変更なし: `World` の収集・配信が読む `IsActive`/`IsTickEnabled`/`IsPendingDestroy`/`GetCustomTimeDilation`/`GetTickGroup`/`GetTickPriority`/`GetTickGroupMask` は、すべて列を読む口（行が無い間は予備）に変わった。
- テスト（`paths:` に `Test/Core/Object/EcsLifetimeTest.cpp` を足した。完了条件の「列に持つ」と「付け外しで値が戻る」を確かめるのに必要な最小の拡張）: `ExpectBound` が Component の行数も数える。新規 `TestFlagsAndTickConfigColumns`（World に付く前の設定が列にある・列が正のとき書き込みが列に出る・主群変更の mask 規則・外すと値が Object に戻る・Component の連結と葉の階層）、`TestComponentRowsAddRemoveAndReserve`（中ほど・末尾・先頭の除去で連結が直る・付け替えで深さが直る・段の最中に足した Component は予約され、再生後に Tick の設定と flags が列にあり、再生前に外した分は作られない・破棄予約と Entity ごとの破棄で行が残らない）。
- 結果（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-BIND-FLAGS-4.txt`（EntityTreeTest のビルド EXIT=0）、`-5.txt`（EcsLifetimeTest・WorldTickGroupTest・WorldTimeChannelTest・EntityTreeTest・TickGroupConfigurationTest 5/5 Passed）、`-6-neighbors.txt`（EcsPropertyStorageTest・RuntimeSchemaSnapshotTest・SchemaProjectionTest・SceneSerializerTest・SceneFileRoundTripTest・EntitySubtreeSnapshotTest・PrefabRoundTripTest・TransformHierarchyTest・EntityWorldAABBTest・SpringArmComponentTest・WorldRenderInterpolationTest・ScriptComponentTickTest 12/12 Passed）、`-7-startup.txt`（起動画面の決定論の比較が `result=pass`、3 視点とも `within_limits=True`。`default.png` を開いて確認）、`-8-rendering3dtest.txt`・`-8-mega-bananas.txt`・`-8-mega-stress.txt`（周回 3 周が各 `result=pass`、`engine_check name=ECS_BIND pass=14/9/11`）。`-1`〜`-3` は途中（古いテストが Component の行数を数えず assert で止まった）の実行で証拠にしない。
- Notes:
  - `NorvesLibBridgeAdapterScriptComponentTest` は Game 側のシンボル未解決（`GameInputSettings`・`SequenceFrameCapture` など）でリンクできず実行していない（`-6-neighbors-build.txt`）。変更前から build 済みの実行ファイルが無く、今回の変更の範囲外。このテストが期待する Entity/Component のプロパティ名（`bTickEnabled`・`bActive`・`bPendingDestroy`・`bEnabled`）と型は変えていない。
  - 値を読む費用: flags・時間倍率・Tick の設定の読み出しが、行を引く（World を Outer からたどる + 列の探索）ぶん重くなる。TRANSFORM 系以降でシステムが列を直接走査するようになると、旧の配信の読み出しは消える。節目の計測（MEASURE-CPU）で見る。
  - 並列の中でこれらの読み出しを呼ぶと `ECS_PROPERTY` の検査で止まる（既存の契約）。ワーカーは Object に触れない。
  - 行末: `World.cpp`・`World.h`・`Component.cpp` は混在。difflib で元の行末に揃えた（numstat 2 通りが一致）。

### SCN-ECS-TRANSFORM-VALUES（2026-10-11、反復 25・run 20261010-223034）

- 変更:
  - `EcsTypes.h`: 列 `EcsLocalTransform{position, rotation, scale}`（プロパティ Position・Rotation・Scale の置き場）と `EcsWorldTransform{position, rotation, scale}`（ワールドの変換のキャッシュ）を足した。
  - `Entity`: `Position`・`Rotation`・`Scale` を `ECS_PROPERTY`（`EcsLocalTransform`）にした（宣言の順・名前は変えていない）。重複していた `m_LocalTransform` と、プロパティとの同期（`SyncLocalTransformFromProperties`）を消した。`m_CachedWorldTransform` は `m_WorldTransformFallback`（行が無い間の予備。行がある間は列 `EcsWorldTransform` が正）にした。読み書きは `LoadLocalTransform`/`StoreLocalTransform`/`LoadWorldTransformCache`/`StoreWorldTransformCache`（列全体を 1 回で読み書きする。並列の中は拒否）。
  - Public API: `GetLocalTransform`・`GetWorldTransform`・`GetRenderWorldTransform`・`GetPosition`・`GetRotation`・`GetScale` を値で返すようにした（参照で受ける呼び出し側は const 参照のまま通る。Library・Game・Test は変更なしでビルドできた）。
  - `World`: Entity の行のアーキタイプに `EcsLocalTransform`・`EcsWorldTransform` を足した（4 列 → 6 列）。`MoveEntityValuesToRow`/`MoveEntityValuesToFallback` が世界のキャッシュの予備を列と行き来させる（局所の変換は ECS_PROPERTY の移動）。今の再帰（`UpdateEntityTransformRecursive` → `RecomputeWorldTransform`）はそのままで、列を読み書きする。
- テスト: `EcsLifetimeTest` に `TestTransformColumns` を足した（`paths:` の `Test/Core/Object`）。World に付く前の値が行を付けると列にある・書き込みが列に出る・ワールドのキャッシュが列にあって親の動きに追従する・外すと値が Object に戻る・外している間に書いた値が付け直しで列に入る、を確かめる。
- 結果（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-TRANSFORM-VALUES-1.txt`（EntityTreeTest・SceneSerializerTest・ScriptComponentTickTest のビルドは通り、Core・Game の C++ のコンパイルエラーなし。`NorvesLibBridgeAdapterScriptComponentTest` のリンクだけ Game 側のシンボル未解決で失敗）、`-2.txt`（指定の ctest 6/7 Passed、Bridge は実行ファイルが無く Not Run）、`-3-startup.txt`（起動画面の決定論の比較 `result=pass`、3 視点とも `within_limits=True`。`default.png` を開いて確認）、`-4-bananas.txt`（mega-bananas の比較 `result=pass`、`within_limits=True`）、`-5-neighbors.txt`（EcsLifetimeTest・EcsPropertyStorageTest・WorldTickGroupTest・WorldRenderInterpolationTest・SpringArmComponentTest・EntityTreeTest・SceneSerializerTest・RuntimeSchemaSnapshotTest・SchemaProjectionTest 9/9 Passed）、`-6-test.txt`（`TestTransformColumns` を足した後の再ビルドと、変換系の ctest 8/8 Passed）。
- Notes:
  - `NorvesLibBridgeAdapterScriptComponentTest` は、反復 24 から続く `Test/Game/Bridge` のリンクエラー（`GameInputSettings`・`SequenceFrameCapture`・`M9SkeletalPreparation` などの未解決）で実行できていない。`paths:` の外（`Test/Game`）で、この変更が触るシンボルではない。このテストが期待するプロパティ名・型（Position・Rotation・Scale）は変えていない。
  - 読む費用: `GetWorldTransform` などが、Outer をたどって World を引き、列を探すぶん重くなった（1 回の再帰で局所とキャッシュを各 1 回読む）。階層のシステム（TRANSFORM-PREP/SWITCH）で列を直接走査するようになると、この経路は消える。節目の計測（MEASURE-CPU）で見る。
  - 行が付いている間、`Position` などを `ApplyValue`（保存の読み込み・ブリッジ）で書くと列に入る（`GetValuePtr` の可変は nullptr）。ワールドのキャッシュは従来どおり `UpdateWorldTransforms` で更新される。
  - 行末: 変更した 5 ファイル（`World.cpp` は混在）は、行末を保つ置換で編集した。`git diff --numstat` と `--ignore-cr-at-eol` が一致。

### SCN-ECS-TRANSFORM-VALUES 差し戻し対応（2026-10-11、反復 26・run 20261010-223034）

- 原因: `Test/Game/Bridge/CMakeLists.txt` の `NorvesLibBridgeAdapterScriptComponentTest` のソース一覧が、`Game` に後から足されたファイル（GLOB_RECURSE で自動収集される `Game` と違い明示列挙）に追従しておらず、リンクが未解決になっていた。変換の列への移行とは無関係の、変更前からの壊れ。
- 変更: ソース一覧に `M9SkeletalPreparation.cpp`・`StartupSkinnedProbe.cpp`・`DogMovementSmokeRoutine.cpp`・`GameInputSettings.cpp`・`GameHapticsSettings.cpp`・`SequenceFrameCapture.cpp`・`FollowCameraComponent.cpp`・`FollowCameraProfile.cpp`・`QuadrupedLocomotionComponent.cpp` を足した（BOM + CRLF を保った。numstat 2 通りが一致）。
- 範囲の注記: このファイルは `paths:` の外（`Test/Game/Bridge`）だが、タスクの verify がこのターゲットのビルドと実行を要求しており、これを直さないと合格できないため、最小の追記だけ行った。
- 結果（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-TRANSFORM-VALUES-7-build.txt`（指定の 4 ターゲットのビルド EXIT=0）、`-8-ctest.txt`（指定の ctest 7/7 Passed、NorvesLibBridgeAdapterScriptComponentTest を含む）。起動画面・mega-bananas の比較は反復 25 の `-3-startup.txt`・`-4-bananas.txt`（pass）から、Core・Game のコードは変わっていないので再実行していない。

### SCN-ECS-TRANSFORM-PREP（2026-10-11、反復 27・run 20261010-223034）

- 変更:
  - `Ecs/EcsTransformSystem.h`（新規。ヘッダのみ。`Library/Core/CMakeLists.txt` のソース一覧を触らないため）: 階層のシステム `EcsTransformSystem`。局所の変換・`EcsHierarchy`・`EcsEntityFlags` の変更ログ（初回と消費者を持てないときは全行）を Depth ごとのバケツに積み、Depth 0 から順に「親のワールドの変換 × 局所の変換」（旧の `ComposeTransform` と同じ式）と EffectiveActive（親 && Active && !PendingDestroy）を求める。変わった行（ワールドの変換か EffectiveActive。初めて求めた行を含む）だけ子を次の Depth へ積み、行の `Visited`（伝播の番号）で二重の処理を除く。行ごとの計算（列の読み取りと式）は `ParallelFor`、列への書き込みと子の積み込みは GameThread が行の順に行うので、直列でも並列でも列の値と処理した順（`GetProcessedOrder`）が同じになる。
  - `EcsTypes.h`: 比較用の列 `EcsPropagatedTransform{World, Visited, bValid, bEffectiveActive}`。
  - `World`: Entity の行のアーキタイプに比較用の列を足した（7 列）。`EcsLocalTransform`・`EcsHierarchy`・`EcsEntityFlags` を追跡する列にした（最初のアーキタイプの前。消費者がいない間はログを積まない）。`GetEcsTransformSystem()`（遅延作成）、`VerifyEcsTransform()`（システムを 1 回走らせ、全 Entity の比較用の列を、旧の再帰が置いた `EcsWorldTransform` と bit まで、EffectiveActive を祖先をたどる従来の判定と照合）、`RunEcsTransformCheck()`（`UpdateWorldTransforms` の末尾から。`--engine-checks=ECS_TRANSFORM` のときだけ。fail は毎回、pass は最初に Entity が居た回と 64 回ごとに出す）。まだ旧の再帰が正。
- テスト `EcsHierarchyTest`（EntityTreeTest の束）: 深い鎖（400 段で処理する Depth 数が一致）、変更の無い回は何も処理しない・同じ値の書き込みは子孫へ伝えない・親子同時の変更は 1 回だけ処理、付け替え（鎖の途中・根へ・別の枝の下へ）、非アクティブ・破棄予約の伝播（孫だけ止めて親を戻す等）、部分木の出入り、後から作ったシステムの全行の取り込み、直列・並列（grain 1 と 3）の 3 つの World が 40 回の変更列で列の値も処理した順も一致、並列の中で ECS_PROPERTY の検査に触れないこと。
- 結果（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-TRANSFORM-PREP-3.txt`（EntityTreeTest のビルド EXIT=0）、`-4.txt`（EcsHierarchyTest・TransformHierarchyTest 2/2 Passed）、`-5.txt`（mega-stress の撮影が `result=pass`、`engine_check name=ECS_TRANSFORM` が default 16・close 16・aerial 12、fail 0）、`-6-neighbors.txt`（隣接の ECS・World 系 13/13 Passed）。`-1`・`-2` は最初の版（テストの前提の誤り）で証拠にしない。
- Notes:
  - `paths:` の外は `World.h` だけ触った（メンバと公開関数の宣言。World.cpp だけでは持ち主を置けない）。`Library/Core/CMakeLists.txt` は触らず、システムをヘッダのみにした。
  - 初版の失敗は `EcsEqualWorldTransform` を `==` で比べたため、深い鎖で拡縮が発散して NaN になった行が不一致になったこと。bit 比較にした（旧の再帰と同じ式なので同じ入力は同じ bit になることは、Debug のテストと RelWithDebInfo の撮影で確かめた）。
  - 予約中の親の下へ付け替えた行がある間（`m_PendingEntityRelinks` が空でない間）は、階層の列が Outer/Inner と食い違うのが正当なので、伝播も照合もしない。
  - 切替（SCN-ECS-TRANSFORM-SWITCH）で `EcsWorldTransform` を書く側に替えるとき、比較用の列は消す。比較用の列は追跡せず、チャンクの版も進めない。切替では書き込みを `MarkWritten` と `CommitChanges` を通す形にすること（今は GameThread が行の順に直接書く）。
  - 行末: `World.cpp`・`World.h`・`TASKS.md`・`PROGRESS.md` は混在。挿入は元の行の行末に合わせた（numstat 2 通りが一致）。

### SCN-ECS-TRANSFORM-PREP 差し戻し対応（2026-10-11、反復 28・run 20261010-223034）

- 原因: 照合の `EcsSameBits` が bit 比較だったため、旧の再帰が「数値として等しいのでキャッシュを更新しない」場合（局所のスケール x が +0 → -0 など符号付きゼロだけの違い）に、比較用の列は -0、旧のキャッシュは +0 となって不一致になった。
- 変更: `EcsSameBits` を `lhs == rhs || 同じ bit`（+0 と -0 は同じ、NaN は同じ bit のときだけ同じ）にした。旧の再帰の `Transform::operator==` と同じ関係なので、変更の検出（`bChanged`）と照合（`EcsEqualWorldTransform`）が旧と同じ判定になる。`EcsHierarchyTest` に `TestSignedZeroMatchesLegacy` を足した（比較の関係そのものと、根の局所スケール x を +0 → 列の直接書き込みで -0 → setter で -0 にして、そのつど旧と一致すること）。
- 結果（`.harness/runs/20261010-223034/`、反復 28 の保存）: `verify-SCN-ECS-TRANSFORM-PREP-1.txt`（EntityTreeTest のビルド EXIT=0）、`-2.txt`（EcsHierarchyTest・TransformHierarchyTest 2/2 Passed）、`-3.txt`（mega-stress の撮影が `result=pass`、`engine_check name=ECS_TRANSFORM` が default 16・close 16・aerial 12、fail 0）。
- Notes: 反復 27 の記述中の `-3`〜`-5` のファイル名は、この反復の保存で上書きされた。

### SCN-ECS-TRANSFORM-SWITCH（2026-10-11、反復 29・run 20261010-223034）

- 変更:
  - `EcsTransformSystem.h`: 伝播の結果を `EcsWorldTransform` の列へ直接書く（値が変わった行だけ `WriteColumnValue`。チャンクの版が進む）。親の値も `EcsWorldTransform` から読む。`EcsPropagatedTransform` から `World` を消した（`Visited`・`bValid`・`bEffectiveActive` だけ。比較用の列は無くなった）。直近の Run で値を求めた行を `GetApplied()`（行と「ワールドの変換が変わった」印）で返す。
  - `World`: `UpdateWorldTransforms` は既定で伝播（`PropagateEcsTransforms`: Run → 変わった Entity の `m_TransformVersion` を進め、処理した Entity の dirty を戻す。版は旧の再帰と同じく「ワールドの変換が変わった」ときだけ）。呼び出し元（Tick/LateTick/FixedBegin/FixedEnd の各段、`UpdateRenderTransforms`、`ReparentEntity`、`SocketAttachmentComponent`）は変えず、公開の `UpdateWorldTransforms` が伝播を呼ぶ形にした（`SpringArmComponentTest` が呼ぶ口はそのまま）。`SetLegacyTransformPropagation(bool)` で旧の再帰へ戻せる。同期点まで待たせた付け替えがある間（階層の列が Outer/Inner と食い違う間）は、旧の再帰で求める。階層のシステムは作成時に並列を有効にする（行が 512 未満のバケツは直列。列の値と処理した順は直列と同じ）。
  - `CanDispatchEntity`: 自分の今の Active・PendingDestroy を見てから、階層のシステムが求めた `EffectiveActive` を読む（行が無い・まだ求めていない・旧の経路・付け替え待ちの間は、従来どおり祖先をたどる）。
  - 検査 `ECS_TRANSFORM`: 旧の再帰との照合をやめ、`EcsWorldTransform` の列を、根から局所の変換をたどって求め直した値（親の列の値 × 局所の変換）と全行 bit まで照合する形に替えた。EffectiveActive は従来どおり祖先をたどる判定と照合する。
  - `ApplicationProcessor`: 起動オプション `--ecs-legacy-transform`（World の初期化の直後に `SetLegacyTransformPropagation(true)`）。
  - 旧の再帰の呼び出し位置の行番号（World.cpp:920 など）は古く、実際の呼び出しはすべて `UpdateWorldTransforms` 経由だった。`ApplicationProcessor.cpp` の固定ステップ（`BeginFixedStep`/`EndFixedStep`）は World の段（FixedBegin/FixedEnd）を通るので、そこは変更なし。
- テスト（`paths:` の外の `Test/Core/Object/EcsHierarchyTest.cpp` を、比較用の列の廃止と UpdateWorldTransforms の意味の変更に合わせて直した。必要な最小の範囲）: 旧の再帰を先に走らせる前提を外し、照合の名前を `ExpectMatchesReference` にした。新規 `TestUpdateMatchesLegacyRecursion`（既定の伝播と `SetLegacyTransformPropagation(true)` の 2 つの World が、決まった変更の列で全 Entity のワールドの変換・`GetTransformVersion` まで一致し、変更の無い回は版を進めない）。
- 結果（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-TRANSFORM-SWITCH-1.txt`（EntityTreeTest・ApplicationFixedStepPipelineTest のビルド EXIT=0）、`-2.txt`（TransformHierarchyTest・EcsHierarchyTest・WorldTickGroupTest・SpringArmComponentTest・ApplicationFixedStepPipelineTest 5/5 Passed）、`-3-startup.txt`（起動画面の決定論の比較 `result=pass`、3 視点とも `within_limits=True`。`default.png` を開いて確認）、`-4-stress.txt`（mega-stress の新経路で `engine_check name=ECS_TRANSFORM` が default 16・close 16・aerial 12、fail 0）、`-5-legacy.txt`（`--ecs-legacy-transform` を付けた同じ撮影が `result=pass`、ログに `ecs_legacy_transform=true`）。
- Notes:
  - `-5-legacy.txt` の最初の実行は `-GameArgs` に `--` を付けて失敗した（スクリプトが `--` を足すので不要）。上書き保存した版が正。
  - `CanDispatchEntity` は祖先の Active の変更を、次の伝播まで見ない（自分の変更は即座に見る）。段の途中で祖先を止めた場合、その段の残りの群では子孫が 1 回余分に Tick し得る。WorldTickGroupTest は pass。
  - 行末: `World.cpp`・`World.h`・`ApplicationProcessor.cpp` は混在。元の行末を保つ置換で編集した（numstat 2 通りが一致）。`sed -i` は CRLF を LF にしてしまうので使わない。

### SCN-ECS-TRANSFORM-SWITCH 差し戻し対応（2026-10-11、反復 30・run 20261010-223034）

- 原因:
  1. 作成予約中の Entity（段の最中に作った子。行がまだ無い）は伝播の対象に載らず、その間に付け替えると未更新のキャッシュの座標を保存していた。
  2. `CanDispatchEntity` が `bValid` のキャッシュをそのまま採るため、同じ群の中で祖先が `SetActive(false)`・`MarkForDestroy()` されても、後続の子は古い EffectiveActive で呼ばれた。
- 変更:
  - `World::UpdateWorldTransforms`: 作成予約（`m_PendingEntityCreates`）がある間も、付け替え待ちと同じく旧の再帰で求める。
  - `World::CanDispatchEntity`: 作成予約・付け替え待ちがある間と、EcsEntityFlags の変更が次の伝播まで未消費の間は、キャッシュを採らず祖先をたどる。変換全体は伝播しないので、同じ群の中の姿勢公開規則は変わらない。
  - `EcsTransformSystem::HasUnpropagatedFlagChanges()`: EcsEntityFlags のログに未消費の変更があるか（消費者を持てないときは常に true）。
  - テスト（`EcsHierarchyTest`）: `TestSpawnedChildReparentKeepsPosition`（段の最中に作った子の付け替えで座標 12、旧と一致）、`TestAncestorStopSuppressesDescendantsImmediately`（親の SetActive(false)・MarkForDestroy の後に子・孫が呼ばれない、旧と一致）。修正を外すと、前者は `propagated == legacy`、後者は `RunAncestorStop(false, …) == 0` で落ちることを確かめた。
- 結果（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-TRANSFORM-SWITCH-8.txt`（EntityTreeTest・ApplicationFixedStepPipelineTest のビルド EXIT=0）、`-9.txt`（5 件の ctest 5/5 Passed）、`-10-startup.txt`（起動画面の決定論の比較 `result=pass`、3 視点とも `within_limits=True`）。`-6`・`-7` はテストの前提の誤り（破棄予約で Entity が解放された後に Count を読んだ）の版で、証拠にしない。
- Notes: `HasUnpropagatedFlagChanges` が true の間（例: 伝播の前に Entity を作った直後）は祖先をたどるので、その間の判定は旧の経路と同じ費用になる。次の伝播で戻る。

### SCN-ECS-TRANSFORM-SWITCH 再検証の失敗への対応（2026-10-11、反復 31・run 20261010-223034）

- 状況: 再実行で `EcsHierarchyTest` が `RunAncestorStop(false, bDestroy) == 0` の assert で止まった（Debug の assert ダイアログ待ちで 180 秒のタイムアウト）。反復 30 の保存（-9）は 5/5 Passed だった。
- 調べたこと: コミット済みのソース（作業ツリーはクリーン）で同じ失敗を再現できたが、`CanDispatchEntity` に診断出力を足して World.cpp を再コンパイルすると通り、診断を外して再コンパイルしても通った。失敗した時点の Debug のバイナリがソースと食い違っていた可能性が高い（World.cpp・EcsHierarchyTest.cpp の再コンパイルで解消した。確証はない）。診断の結果、新経路では親の `SetActive(false)` の直後に `HasUnpropagatedFlagChanges` が true になり、祖先をたどって子・孫が呼ばれない（count=0）ことを、Active・破棄予約の両方で確かめた。
- 変更: ソースの変更なし（診断用の出力は戻した）。
- 結果（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-TRANSFORM-SWITCH-11.txt`（EntityTreeTest・ApplicationFixedStepPipelineTest のビルド EXIT=0）、`-12.txt`（5 件の ctest 5/5 Passed）、`-13-startup.txt`（起動画面の決定論の比較 `result=pass`、3 視点とも `within_limits=True`）。再コンパイル後の `EcsHierarchyTest` を 30 回続けて実行し、失敗 0 回。
- Notes: 食い違ったバイナリになった原因は特定できていない。同じ症状が再発したら、World.cpp を再コンパイルしてから疑うこと。

### SCN-ECS-INTERP（2026-10-11、反復 32・run 20261010-223034）

- 変更:
  - `EcsTypes.h`: 列 `EcsRenderTransform`（描画の変換 + 値を求めたか + 前回の親の ID・継承の有無）・タグ `EcsRenderInterpolated`・`EcsTransformHistory`（`Scene::RenderTransformHistory` を包む）。`EcsPropagatedTransform` に「描画の更新へ積み済み」の印 `bRenderQueued` を足した。
  - `EcsTransformSystem.h`: `AppliedRow` に `bFirst`（値を初めて求めた行）を足した。
  - `World`: 描画の変換の補間を全走査から外した。補間する Entity（要求あり・許可あり）の行にタグ・履歴・描画の変換の列を付け、その子孫の行には描画の変換の列だけを付ける（`AddColumn`/`RemoveColumn`。補間しない行は専用の列を持たず、描画の変換 = ワールドの変換）。
    - 列の付け外しは `ReconcileRenderInterpolation`（要求・許可・付け替え・行の作成の通知が溜まったときだけ。浅い行から処理し、描画の変換の有無が変わった行の子へ広げる）。Prepare/Capture/UpdateRenderTransforms の先頭で呼ぶ。
    - Prepare/Capture（`StepRenderHistory`）は履歴の列を持つ行だけ、`UpdateRenderRows` は補間する行とその子孫（一覧 `m_RenderRows`、深さ順）だけを処理する。親の ID・継承の有無が変わったら履歴を捨てる、`ResetRenderInterpolation`（子孫まで）と `SetRenderInterpolationAllowed` で履歴と値を下ろす、は従来の規則のまま。
    - 補間しない行は、階層のシステムの結果（ワールドの変換が変わった・初めて求めた行）と旧の再帰（版が進んだ行）・行の作成から `m_RenderChangedRows` へ積み、`FlushRenderChangedRows` がその行だけ版を進め Mesh・SkinnedMesh の描画状態を dirty にする。
  - `Entity`: 履歴・描画の変換・継承の各メンバを廃止（列へ移した）。`GetRenderWorldTransform` は描画の変換の列（値を求めていれば）を返す。補間の要求・Reset は World へ通知する。
  - 検査 `ECS_RENDER_TRANSFORM`（`VerifyEcsRenderTransform`）: 木を根からたどり、補間する行は履歴の写しに Prepare/Evaluate、その子孫は親の期待値 × 局所の変換、それ以外はワールドの変換を期待値として、`GetRenderWorldTransform` と全成分で照合し、列・タグ・履歴の有無が要求と許可から決まる組と一致するかも確かめる。
- テスト: `WorldRenderInterpolationTest` に `TestRenderRowsAreIncremental`（描画の変換の列は補間する行とその子孫だけ・動かない行の描画の版は更新を重ねても進まず動いた行だけ 1 進む・許可の切替・付け替え・要求の解除・子孫の新規作成・部分木の破棄のたびに旧の式と一致）を足した。
- paths の外を触った:
  - `World.h`（宣言・メンバ）。
  - `Test/Core/Object/WorldRenderInterpolationTest.cpp`（上のテスト）。
  - `Test/Modules/Physics/PhysicsModuleTestAccess.h`・`PhysicsFixedStepPipelineTest.cpp`: HEAD の時点で `PhysicsFixedStepPipelineTest` が `IPhysicsModule::RefreshDynamicSnapshot` を呼んでコンパイルできなかったので、テスト用アクセサ経由にした。さらにケース 12（`TestCharacterBeforeSimulationRemoval`）は HEAD の Library だけでも失敗した（収集を開いたまま作った replacement は次のフレームから参加するため、固定ステップに呼ばれない）ので、replacement の作成前に `LateTick` でフレームを閉じた（表明は変えていない）。
- 結果（`.harness/runs/20261010-223034/`）:
  - `verify-SCN-ECS-INTERP-4.txt`: 指定 3 ターゲットのビルド EXIT=0。
  - `-5.txt`: 指定 ctest（WorldRenderInterpolationTest・RenderInterpolationCadenceTest・PhysicsFixedStepPipelineTest）3/3 Passed。`-1`〜`-3` はビルドの引数の誤り・テストのコンパイル不能の版で証拠にしない。
  - `-6-neighbors.txt`: 隣接の ECS・World 系 20 件中 19 件 Passed。`WorldSyncDifferentialTest` だけ失敗（Timeout の実体は assert。第 2 回の同期で Mesh のプロキシが 777 のまま残る前提が崩れている）。これは `World.cpp` の `if (!bNeedsSync)` 側で `UpdateMeshProxy` を呼ぶ 1718b077c（2026-09-22）からの既存の失敗で、診断出力でこの変更が第 2 回の同期で dirty を立てていないことを確かめた。
  - `-7-startup.txt`: 起動画面の決定論の比較 `result=pass`。`engine_check name=ECS_RENDER_TRANSFORM` が default 9・near 11・low 8、fail 0。3 視点とも `within_limits=True`。`default.png` を開いて確認。
- Notes:
  - 撮影は `Deterministic` で補間が許可されないので、補間の経路を通して値を照合するのは `WorldRenderInterpolationTest` と物理の固定ステップのテスト。
  - 描画の変換の列は追跡せず（`MarkWritten` を通さない）、チャンクの版も進めない。描画への同期の切替（SYNC-PREP 以降）で変更ログに載せるときは、書き込みを `MarkWritten` と `CommitChanges` を通す形にすること。
  - 構造変更（列の付け外し）は `UpdateRenderTransforms`・Prepare・Capture の中だけで行う。段の最中に要求を変えても、行が動くのは次の呼び出しから。
  - `WorldSyncDifferentialTest` は別の項目で直す（`!bNeedsSync` 側の更新は、非同期の読み込みの完了を拾うための意図的な再構築かもしれない。テストの前提のほうが古い可能性が高い）。
  - 行末: `World.cpp`・`World.h` は混在。Edit ツールは混在ファイルを LF にしてしまうので、編集のたびに HEAD の行末を基準に difflib で直した（numstat 2 通りが一致）。

### SCN-ECS-INTERP 差し戻し対応（2026-10-11、反復 33・run 20261010-223034）

- 差し戻し: 補間の更新が自身の破棄予約しか見ず、親が破棄予約の間（回収前）も子孫の履歴・描画の変換・版を更新していた（旧の再帰は部分木ごと飛ばす）。
- 変更: `World.cpp` の `FlushRenderChangedRows`・`StepRenderHistory`・`UpdateRenderRows` の 3 箇所で `HasPendingEntityAncestor` も確かめ、破棄予約の部分木を触らないようにした。
- テスト: `WorldRenderInterpolationTest` に `TestPendingDestroyAncestorKeepsRender`（補間しない親を `MarkForDestroy` した後、alpha を変えて更新・Prepare/Capture しても、子の描画位置 2.5 と版が変わらない）を足した。`World.cpp` を HEAD に戻して再ビルドすると、このテストが落ちる（assert でタイムアウト）ことを確かめた。
- 結果（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-INTERP-10.txt`（3 ターゲットのビルド EXIT=0）、`-11.txt`（ctest 3/3 Passed）、`-12-startup.txt`（`ECS_RENDER_TRANSFORM` が default 9・near 11・low 8、fail 0。3 視点とも `within_limits=True`）。`-8`・`-9` は同じ内容の途中版で証拠にしない。
- Notes: 物理テスト 2 ファイル（`PhysicsModuleTestAccess.h`・`PhysicsFixedStepPipelineTest.cpp`）の修正は反復 32 のコミットに入っている。履歴を書き換えて分離はしない。

### SCN-ECS-RENDER-COLUMNS-MESH（2026-10-11、反復 34・run 20261010-223034）

- 変更:
  - `Ecs/EcsRenderTypes.h`（新規）: 列 `EcsMeshRender`（メッシュの handle・可視・影を落とす・影を受ける・レイヤー）・`EcsMegaMesh`（MegaMesh の handle）・`EcsSkinnedRender`（材質の handle・可視・影を落とす）。`EcsTypes.h` に Component の行の種類 `EcsComponentRowKind`（Basic・Mesh・MegaGeometry・SkinnedMesh）を足した。
  - `Component`: `GetEcsRowKind()`（行に足す列の種類）と `OnEcsRowAttached`・`OnEcsRowDetaching`（ECS_PROPERTY にしていない値を予備と列の間で移す口）を足した。
  - `MeshComponent`: MeshHandle・bVisible・bCastShadow・bReceiveShadow・RenderLayerProp を `ECS_PROPERTY(EcsMeshRender)` にした。`MegaGeometryComponent`: MegaMeshHandleValue を `ECS_PROPERTY(EcsMegaMesh)` にした（Mesh の列も持つ）。保存・プレハブの経路は ECS_PROPERTY の口を通り、プロパティの名前・型・順は変えていない。
  - `SkinnedMeshComponent`: 可視・影・材質の handle は元からプロパティではなく保存の対象でもないので、ECS_PROPERTY にして保存の形式を変えることはせず、`EcsSkinnedRender` を Component が Load/Store で読み書きする形にした（`OnEcsRowAttached/Detaching` で予備のメンバと列を移す）。
  - `World`: Component の行のアーキタイプを種類ごとの配列 `m_ComponentArchetypes` にし、`ComponentArchetypeFor` で行の作成（即時・予約の両方）に使う。検査 `ECS_BIND` に、行の描画の値の列が Component の種類と一致するかの照合を足した。同期は全走査のまま、読む先だけが列になった。
- paths の外を触った:
  - `Test/Core/Rendering/WorldSyncDifferentialTest.cpp`: この項目の verify に含まれるテストで、反復 32 で記録した既存の失敗（`!bNeedsSync` 側の Mesh が現フレームの proxy を毎フレーム公開し直す仕様 = 1718b077c の R6-a の速度の修正に、期待値が追いついていない）だった。Mesh の proxy の書き換えは残らない（0）という現行の契約に期待値を直した（Light と MegaGeometry は公開し直さないので元のまま）。列に値が入る契約の `TestRenderValuesLiveInComponentRow`（Mesh・Mega・Skinned の列の中身と、種類ごとに列が付くか）も足した。
- 結果（`.harness/runs/20261010-223034/`）:
  - `verify-SCN-ECS-RENDER-COLUMNS-MESH-1.txt`: 指定 4 ターゲットのビルド EXIT=0。`-2.txt`: ctest 3/4（WorldSyncDifferentialTest が上の既存の失敗）。`-3.txt` は途中の版で証拠にしない。
  - `-5.txt`: 4 ターゲットのビルド EXIT=0 と ctest 4/4 Passed（SceneFileRoundTripTest・SceneViewProxyLookupTest・WorldSyncDifferentialTest・SkinnedRenderPathContractTest）。`-4.txt` は同じ内容の途中版。
  - `-6-neighbors.txt`: 隣接の 11 件（EcsLifetimeTest・EcsPropertyStorageTest・EcsHierarchyTest・EntityTreeTest・WorldTickGroupTest・TransformHierarchyTest・WorldRenderInterpolationTest・SceneSerializerTest・PrefabRoundTripTest・SchemaProjectionTest・ComponentDirtyTrackingTest）がすべて Passed。
  - `-7-startup.txt`: 起動画面の決定論の比較 `result=pass`。3 視点とも輝度差 0.0001 以下・PSNR 65 dB 以上で、`default.png` を開いて確認。
- Notes:
  - 描画の値の列は追跡（`MarkWritten`）の設定をまだ持たない。変更ログに載せるのは SYNC-PREP 以降の項目。
  - ECS_PROPERTY の読み書きは 1 プロパティごとに行を引く。Mesh の proxy の構築は 1 回で 4〜5 回引くので、同期を変わった行だけにする項目（SYNC-SWITCH）で、列をまとめて読む形にできる。
  - 行末: 編集したファイルは numstat の 2 通りが一致（`World.cpp`・`World.h` は混在のまま、位置ごとに元の行末で置換）。新規の `EcsRenderTypes.h` は UTF-8 BOM + CRLF。

### SCN-ECS-RENDER-COLUMNS-MESH 差し戻し対応（2026-10-11、反復 35・run 20261010-223034）

- 差し戻し: SkinnedMesh の可視・影が `ECS_PROPERTY` でなく手動の Load/Store だった点と、メッシュの handle が列に無くアセットから毎回求めていた点。
- 変更:
  - `EcsRenderTypes.h`: `EcsSkinnedRender` に `SkinnedMeshHandle MeshHandle` を足した。
  - `SkinnedMeshComponent`: メッシュの handle・可視・影を落とすを `ECS_PROPERTY(EcsSkinnedRender, ...)` にし、手動の `LoadRenderValues`/`StoreRenderValues` と予備のメンバ `m_bVisible`・`m_bCastShadow` を廃止した。材質の handle はスロットごとの上書きと一緒に扱うので、列の一部（`Material` の位置）だけを読み書きする `LoadMaterial`/`StoreMaterial` に縮めた（行の付け外しの `OnEcsRowAttached/Detaching` は材質だけ移す）。メッシュの handle は `RefreshMeshHandle` が資産の描画用 handle を列へ写す（`SetSkeletalAsset` の末尾と、proxy を作る直前＝lease の作り直しへの追従）。proxy は列の値を読む。
  - 保存の形式: `bVisible`・`bCastShadow`（bool）が SkinnedMesh のプロパティとして増える。`MeshHandle` は型がシリアライズ不能なので保存されない（従来の保存の対象は変わらず、増えた bool は古いデータに無くても既定の true で読める）。
  - `MegaGeometryComponent.h`: 差分の文脈の英語の doc コメントを日本語にした。
- テスト: `WorldSyncDifferentialTest::TestRenderValuesLiveInComponentRow` に、SkinnedMesh の 3 プロパティの `StorageKind::EcsColumn` と、列を直接書き換えると `IsVisible`/`CastsShadow` がその値を返すことの照合を足した。
- 結果（`.harness/runs/20261010-223034/`）:
  - `verify-SCN-ECS-RENDER-COLUMNS-MESH-9.txt`: 指定 4 ターゲットのビルド EXIT=0。`-8` は Git Bash が `/m:1` を変換して失敗した試行で証拠にしない。
  - `-10.txt`: 指定 ctest 4/4 Passed。
  - `-13-neighbors.txt`: 隣接の 11 件（Ecs*・EntityTree・WorldTickGroup・TransformHierarchy・WorldRenderInterpolation・SceneSerializer・PrefabRoundTrip・SchemaProjection・ComponentDirtyTracking）すべて Passed（実体の `EntityTreeTest`・`ObjectReflectionTest`・`SceneSerializerTest` は再ビルド済み。`-11`・`-12` はターゲット名の誤りで証拠にしない）。
  - `-14-startup.txt`: 起動画面の決定論の比較 `result=pass`。3 視点とも `within_limits=True`（default は PSNR 100 dB）。`default.png` を開いて確認。
- Notes: Bash から MSBuild の `/m:1` を渡すとパスに変換される。ビルドは PowerShell で実行すること。`MegaGeometryComponent.h` は BOM 無しの UTF-8 に日本語があり C4819 の警告が出る（前の反復から。BOM の付与は別件）。

### SCN-ECS-RENDER-COLUMNS-LIGHT（2026-10-11、反復 36・run 20261010-223034）

- 変更:
  - `Ecs/EcsRenderTypes.h`: 列 `EcsLightRender`（種類・強度・単位・色・影・可視・影のバイアス・影の解像度・影響レイヤー）、`EcsLightFalloff`（Point・Spot 共通の範囲と減衰）、`EcsSpotCone`（コーン角）、`EcsBoardRender`（テクスチャ・可視・レイヤー・空間・ブレンド・Tint・反転・Pivot・SizePx・UV・LayerPriority・OrderInLayer）を足した。`EcsTypes.h` の `EcsComponentRowKind` に Light・PointLight・SpotLight・Board を足した。
  - `LightComponent` 系: 共通の 9 プロパティを `ECS_PROPERTY(EcsLightRender, ...)`、Point・Spot の範囲と減衰 4 つを `EcsLightFalloff`、Spot のコーン角 2 つを `EcsSpotCone` にした。行の種類は Light（Directional を含む）・PointLight・SpotLight。プロパティの名前・型・順は変えていない。`LightIntensityUnit` は列の構造体から参照するので、`Component/LightIntensityUnit.h`（新規。名前空間は従来のまま）へ移した。
  - `BoardComponent`: 描画の値 13 個を `ECS_PROPERTY(EcsBoardRender, ...)` にした。フリップブックの設定（コマ数・速度・ループ・アトラス・開始コマ）は描画の値ではないので `PROPERTY` のまま。`GetTint`・`GetPivot`・`GetSizePx`・`GetUVRect` の戻りは列が動くので参照から値渡しにした。BillboardComponent・ImpostorComponent・TextComponent は派生なので Board の行を使う（ImpostorComponent の `Space` の書き込みだけ読み口に直した）。
  - `World`: 行のアーキタイプを 4 種類足し、検査 `ECS_BIND` の列と種類の照合に Light・Falloff・SpotCone・Board を足した。同期は全走査のまま、読む先だけが列。
- テスト（paths の外）: `WorldSyncDifferentialTest` に `TestLightAndBoardValuesLiveInComponentRow`（種類ごとに付く列が違う・Set の値が列にある・列を直接書き換えると読み口が追従・対象プロパティが `StorageKind::EcsColumn`・フリップブックの設定は列でない）を足した。
- 結果（`.harness/runs/20261010-223034/`）:
  - `verify-SCN-ECS-RENDER-COLUMNS-LIGHT-3.txt`: 指定 2 ターゲット + WorldSyncDifferentialTest のビルド EXIT=0。`-1`・`-2` は `LightComponentBuildProxyTest`・`BillboardComponentTest` をターゲット名に入れて失敗した試行（EntityTreeTest・BoardComponentRoutingTest のバンドルの一員）と、ImpostorComponent の `Space` を直す前の版で証拠にしない。
  - `-4.txt`: 指定 ctest（LightComponentBuildProxyTest・BoardComponentRoutingTest・BillboardComponentTest）と WorldSyncDifferentialTest が 4/4 Passed。
  - `-5-neighbors.txt`: 隣接の 26 件（Ecs*・EntityTree・SceneSerializer・PrefabRoundTrip・SchemaProjection・ComponentDirtyTracking・FlipbookAdvance・TextComponentBoardProxy・BoardTransform・SpriteSheetUVRect・ObjectCast・EntitySubtreeSnapshot・SceneFileRoundTrip・WorldRenderInterpolation・WorldTickGroup・TransformHierarchy・ObjectReflection・SkinnedRenderPathContract・SceneViewProxyLookup）すべて Passed。
  - `-6-startup.txt`: 起動画面の決定論の比較 `result=pass`。3 視点とも `within_limits=True`（default は PSNR 100 dB、near 65.4 dB、low 70.4 dB）。`default.png` を開いて確認（太陽の影・球・家・光源の点とも基準どおり）。
- Notes:
  - 光と Board の列は追跡（`MarkWritten`）の設定をまだ持たない。変更ログに載せるのは SYNC-PREP 以降の項目。
  - `SetIntensityUnit` の検証などは従来どおり読み口に残る。列へ直接書く側は検証を通らないので、変更ログを通す形にするときに書き込み口を一本にすること。
  - 行末: 編集したファイルは numstat の 2 通りが一致。新規 `LightIntensityUnit.h` は UTF-8 BOM + CRLF。

### SCN-ECS-RENDER-COLUMNS-LIGHT 差し戻し対応（2026-10-11、反復 37・run 20261010-223034）

- 差し戻し: ライト方向（`m_LightDirection`）と Billboard の `SizeWorld` が列に移っていなかった点。
- 変更:
  - `EcsRenderTypes.h`: `EcsLightRender` に `Math::Vector3 Direction` を足し、新しい列 `EcsBillboardSize`（`SizeWorld`）を足した。`EcsTypes.h` の `EcsComponentRowKind` に `Billboard` を足した。
  - `LightComponent`: 方向は保存の対象外なので ECS_PROPERTY にせず、`SkinnedMeshComponent` の材質と同じ形で列の一部（`Direction` の位置）だけを読み書きする `LoadLightDirection`/`StoreLightDirection` にした。`OnEcsRowAttached/Detaching` で行の付け外しのたびに予備のメンバ（`m_LightDirection`）と列の間で値を移す。`SetLightDirection`・`GetLightDirection`・proxy 構築はこの口を通る。
  - `BillboardComponent`: `SizeWorld` を `ECS_PROPERTY(Ecs::EcsBillboardSize, ...)` にし、proxy 構築と境界計算は `getSizeWorld().Get()` から読む。`GetSizeWorld()` の戻りは列が動くので参照から値渡しにした。行の種類は `Billboard`（Board の列に大きさの列を足したアーキタイプ。ImpostorComponent も継承で同じ行）。
  - `World`: Billboard 用のアーキタイプを足し、検査 `ECS_BIND` の列と種類の照合に Billboard の大きさの列を足した。
- テスト（paths の外）: `WorldSyncDifferentialTest::TestLightDirectionAndBillboardSizeLiveInComponentRow` を足した（既定値が列にある・setter→列・列→getter/proxy・外した間の読み書きと付け直しで列へ戻る・`SizeWorld` が `StorageKind::EcsColumn`）。
- 結果（`.harness/runs/20261010-223034/`）:
  - `verify-SCN-ECS-RENDER-COLUMNS-LIGHT-7.txt`: 指定 2 ターゲット + WorldSyncDifferentialTest のビルド EXIT=0。
  - `-8.txt`: 指定 ctest（LightComponentBuildProxyTest・BoardComponentRoutingTest・BillboardComponentTest）と WorldSyncDifferentialTest が 4/4 Passed。
  - `-9-neighbors-build.txt` / `-10-neighbors.txt`: 隣接のテスト 27 件（Ecs*・EntityTree・SceneSerializer・PrefabRoundTrip・SchemaProjection・ComponentDirtyTracking・FlipbookAdvance・TextComponentBoardProxy・BoardTransform・SpriteSheetUVRect・ObjectCast・EntitySubtreeSnapshot・SceneFileRoundTrip・WorldRenderInterpolation・WorldTickGroup・TransformHierarchy・ObjectReflection・SkinnedRenderPathContract・SceneViewProxyLookup）がすべて Passed。`-9` の exit=1 はバンドルの一員でターゲット名の無いテスト（実体は EntityTreeTest・ObjectReflectionTest・SceneSerializerTest で、ビルド済み）。
  - `-11-startup.txt`: 起動画面の決定論の比較 `result=pass`。3 視点とも `within_limits=True`（default は PSNR 100 dB、near 65.4 dB、low 70.4 dB）。`default.png` を開いて確認（太陽の影の向き・球・家・光源の点とも基準どおり）。
- Notes:
  - 方向の書き込みは列へ直接で、変更ログには載らない（他の光の列と同じ。SYNC-PREP 以降の項目）。
  - 行末: 編集したファイルは numstat の 2 通りが一致（`World.cpp` は混在のまま、差分の行だけ元の行末で当て直した）。

### SCN-ECS-RENDER-INVALIDATION（2026-10-11、反復 38・run 20261010-223034）

- 変更:
  - 資源の変化の記録 `Rendering/ResourceChangeLog.h`（新規。スレッド間で積め、溢れたら「全部変わった」とだけ覚える）。`RenderResources::ChangeLog()` が持ち、`MeshResources::Register/Unregister`・`MaterialResources::Update/Release` が積む。
  - 逆引き: `Component::CollectRenderResourceRefs`（Mesh はメッシュと材質、SkinnedMesh はメッシュと slot 材質、Mega は何も申告しない）。World が proxy を作り直すたびに「資源 → 行」の表へ登録し、`NotifyRenderResourceChanged` が行を dirty にして通知済みに数える。死んだ行は通知のときに表から外す。
  - ポーズの版: 列 `EcsPoseVersion`（SkinnedMesh の行。ポーズを求め直すたびに Component が書く）と、公開した版との比較（違えば作り直し）。
  - 接続先の交換: `SetSceneView`・`SetScreenSpaceBoardSink` が `NotifyRenderRepublishAll` を呼ぶ（全行を dirty にして、同期の終わりまで通知済みに数える）。
  - 公開の状態の列 `EcsPublishState`（公開済みか・公開した回数・公開時の描画の変換の版とポーズの版）を、描画へ出す全種類の行のアーキタイプに足した（検査 ECS_BIND の種類との照合にも足した）。公開しようとして proxy を作れなかった行（アセット待ちの SkinnedMesh）は再試行の一覧に入る。
  - 見つけた通知の種類: Mesh は公開の翌フレームにもう一度公開して前フレームの変換を揃える（速度の履歴）。これを「履歴が落ち着くまでの公開し直し」として次の同期の通知にした。
  - 検査 `ECS_RENDER_INVALIDATION`: 公開した proxy の内容の hash（`Rendering/SceneProxyHash.h`。SceneView が書き換えるソート値は除く）を覚え、dirty でも変換の版違いでも通知でもない行を作り直して同じか照合する。食い違えば「変更も通知も無いのに proxy が変わった」で fail。再試行の一覧の行が公開済みになっていないかも見る。
  - 同期はまだ全走査のまま（挙動は変えていない。SkinnedMesh の「ポーズの版が違う」だけ新しい作り直しの理由）。
- テスト（paths の外）: `WorldSyncDifferentialTest::TestRenderInvalidationNotifications`（列の有無、逆引きで対象の行だけ dirty、材質の更新が記録を通って proxy に届く、記録を横取りして通知を欠くと検査が違反 1 件を見つける、動かした直後の公開し直しで違反が出ない、接続先の交換、再試行の一覧、消えた行への通知）。
- 結果（`.harness/runs/20261010-223034/`）:
  - `verify-SCN-ECS-RENDER-INVALIDATION-1.txt`（Debug の Core ビルド EXIT=0）、`-2.txt`（WorldSyncDifferentialTest ビルド EXIT=0）、`-3.txt`（同テスト Passed）。
  - `-4-startup.txt`: 起動画面の決定論の比較 `result=pass`、`ECS_RENDER_INVALIDATION` が default 3・near 4・low 3 pass、fail 0。3 視点とも `within_limits=True`。
  - `-6-rendering3dtest.txt`・`-6-mega-bananas.txt`・`-6-mega-stress.txt`: 周回 3 周が各 `result=pass`、`ECS_RENDER_INVALIDATION` pass=1506/715/2303、fail 0。
  - `-7-neighbors.txt`: 隣接の 26 件（Ecs*・EntityTree・SceneSerializer・PrefabRoundTrip・SchemaProjection・ComponentDirtyTracking・Light/Board/Billboard・WorldSync・SceneViewProxy*・SkinnedRenderPathContract・WorldRenderInterpolation・WorldTickGroup・TransformHierarchy など）すべて Passed。
  - `-5-cycle.txt` は 3 シーン混在の周回で、検査は pass=4541・fail 0 だが別件のテクスチャキャッシュ差（`SCENE_CYCLE_DIFF field=textures`、CLEANUP-LIST 以来既知）で失敗する。証拠にしない。`TASKS.md` の verify を前の項目と同じくシーンごとの 3 コマンドにした。
- Notes:
  - 次の SYNC-PREP: 描画の値の列は追跡（`MarkWritten`）をまだ持たない。変更ログに載せるのは SYNC-PREP 以降。dirty フラグが当面その代わり。
  - 公開の状態の `bPublished` は、非アクティブな Entity の行（走査されない）では更新されない。SYNC-PREP の Remove の一覧はこの点を扱うこと。
  - 逆引きに載せない資源: Board のテクスチャ・Mega の MegaMesh（proxy が handle を渡すだけで状態を読まない）、SkinnedMesh の登録（`RenderResources` の窓口に登録の口が無い。アセット側の dirty とポーズの版に任せる）。
  - 行末: 編集したファイルは numstat の 2 通りが一致。新規 `ResourceChangeLog.h`・`SceneProxyHash.h` は UTF-8 BOM + CRLF。

### SCN-ECS-RENDER-INVALIDATION 差し戻し対応（2026-10-11、反復 39・run 20261010-223034）

- 差し戻し（反復 38 の評価）への対応:
  - 逆引きの旧参照: `World` に行ごとの旧参照 `m_RenderRowRefKeys` を足し、`RegisterRenderResourceUsers` が再登録の前に交換前の資源の集合から行を外す（空になった集合は消す）。死んだ行の通知で旧参照も外す。
  - 公開状態の列: `SyncEntityRecursive` が非アクティブ・破棄待ちの部分木を飛ばすとき、`MarkEntitySubtreeRenderUnpublished` で公開済みの行の列を「公開していない」にし（検査の覚えも消す）、`RemoveStale*` が消した proxy と合わせた。再びアクティブにすると SetActive が部分木を dirty にするので公開し直す。
  - 今回触ったブロックの英語コメントを日本語にした。
  - 元の混在周回の verify を `TASKS.md` に戻した（シーンごとの 3 本は補助）。
- テスト（paths の外）: `WorldSyncDifferentialTest::TestRenderInvalidationStaleRefsAndInactiveRows`（材質・メッシュの handle を交換したあと、交換前の資源の通知が行に届かず新しい資源の通知は届く。非アクティブで proxy と公開の列が消え、戻すと公開し直す）。
- 結果（`.harness/runs/20261010-223034/`）:
  - `verify-SCN-ECS-RENDER-INVALIDATION-8-build.txt`（WorldSyncDifferentialTest の Debug ビルド EXIT=0）、`-9-ctest.txt`（Passed）。
  - `-10-startup.txt`: 起動画面の決定論の比較 `result=pass`、`ECS_RENDER_INVALIDATION` が default 3・near 4・low 3 pass、fail 0。
  - `-11-rendering3dtest.txt`・`-11-mega-bananas.txt`・`-11-mega-stress.txt`: シーンごとの周回 3 周が各 `result=pass`、pass=1459/741/2318、fail 0。
  - `-12-mixed.txt`: 元の混在周回は `result=fail`（EXIT=1）。`SCENE_CYCLE_DIFF field=textures,texture_bytes` で、CLEANUP-LIST の基準でも同じ既知の差。`Game/Scenes` は `paths:` の外のため、`blocked/SCN-ECS-RENDER-INVALIDATION.md` に判断を書いて BLOCKED にした。
- 隣接の 26 件は今回は走らせていない（変更が World の逆引き・公開状態の列に限られ、WorldSyncDifferentialTest とシーンごとの周回で確かめた）。
- 行末: 編集したファイルは numstat の 2 通りが一致（`World.cpp`・`World.h` は Edit で全体が書き換わったので、difflib で元の行末に当て直した）。

### SCN-ECS-RENDER-SYNC-PREP（2026-10-11、反復 40・run 20261010-223034）

- 変更:
  - 新規 `Ecs/EcsRenderSync.h`（ヘッダのみ）: 一覧（変わった行 `Changed`・Remove `Removed`。鍵は種類 + proxy の鍵）と、全走査との照合 `EcsRenderSyncCompare`（走査で変わった行が一覧に無い／一覧にあるのに変わっていない／外された proxy の説明が無い／Remove なのに走査後も生存、の 4 種を数える）。
  - 変更ログ: `Component::MarkRenderStateDirty` が「不要 → 必要」に変わったときだけ World の `NoteComponentRenderDirty` へ知らせる（`m_RenderLogWorld`。行の付け外しで World が設定し、付けたとき既に必要なら種として積む）。ワールドの変換の版が進んだ Entity の行も `m_RenderSyncTransformLog` に積む（ECS 伝播と旧の再帰の両方）。
  - 公開の状態 `EcsPublishState` に `ProxyKey`・`Kind`（出している先。板は World / 画面空間まで分ける）を足し、行の削除のフック（`OnDestroy`）が出していた proxy の Remove を積む。
  - 一覧の作成 `BuildRenderSyncList`: (1) 板のフリップブックを先に進める（走査での呼び出しは何もしない）、(2) 変更ログ（必要になった Component）、(3) 変換の版ログ × 公開した版との差、(4) ポーズの版 × 公開の状態の差（SkinnedMesh の列を走査）、(5) Remove = 行の削除のフック + 公開の状態が「出している」のに走査に入らない（非アクティブ・破棄待ち）部分木の行。
  - 検査 `ECS_RENDER_SYNC`: 同期の頭で一覧を作り、走査の結果（変わった行・SceneView から外される proxy・走査後の生存）と照合する。違反は毎回、pass は最初と 64 回ごとに報告。有効でない間は変更ログを取らない（挙動は変えていない。同期は全走査のまま）。
- テスト（paths の外）: `WorldSyncDifferentialTest::TestRenderSyncListMatchesFullScan`（最初・静止・移動・非表示・非アクティブ化と復帰・破棄で一覧が全走査と一致し違反 0、公開した版だけ古くした行は違反 1 になる）、`TestRenderSyncCompareDetectsEachMismatch`（4 種の食い違いと種類の取り違えを検出）。
- 結果（`.harness/runs/20261010-223034/`）:
  - `verify-SCN-ECS-RENDER-SYNC-PREP-1.txt`（Debug の Core ビルド EXIT=0）、`-2.txt`（WorldSyncDifferentialTest ビルド EXIT=0）、`-3.txt`（同テスト Passed）。
  - `-4-capture.txt`: 指定の撮影 `result=pass`、`ECS_RENDER_SYNC` が default 3・close 3・aerial 2 pass、fail 0。
  - `-5-cycle.txt`: 指定の混在周回は `ECS_RENDER_SYNC` pass=1382・fail 無しだが、`SCENE_CYCLE_DIFF field=textures,texture_bytes`（既知。INVALIDATION と同じ）で EXIT=1。
  - `-6-rendering3dtest.txt`・`-6-mega-bananas.txt`・`-6-mega-stress.txt`: シーンごとの周回 3 周が各 `result=pass`、`ECS_RENDER_SYNC` pass=1428/1/1、fail 無し。
- BLOCKED: 混在周回の verify は ECS 側と無関係な既知のテクスチャ差で通らない。`blocked/SCN-ECS-RENDER-INVALIDATION.md` の判断（混在周回の判定を直す別タスクを起こす案が推奨）に従う。実装は完了・コミット済み。判断後に `status:` を戻す。
- Notes:
  - 次の SYNC-SWITCH へ: Remove のうち「非アクティブ・破棄待ち」は今は公開の状態の列の走査（公開数に比例）で拾っている。切替では EcsEntityFlags の変更ログ（追跡済み）か SetActive・破棄予約の通知に置き換えて O(変化) にすること。ポーズの版も同じく列の走査（SkinnedMesh の行数に比例）。
  - 描画の値の列は追跡（MarkWritten）をまだ持たず、変更ログは Component の dirty の通知で代用している（`MarkRenderStateDirty` が 103 箇所から呼ばれ、列以外の変化も含むため）。
  - 非表示の SkinnedMesh は公開の状態のポーズの版が更新されず、走査では毎フレーム「変わった行」になる（既存の挙動。一覧も同じ列の差で合わせた）。切替で直す。
  - 画面空間の板 sink は proxy の一覧を持てないので、走査で外される proxy の照合は World の板までで、画面空間は「Remove なのに生存」の側だけ見ている。
  - World.h は `paths:` に書かれていないが、World.cpp の宣言として前の項目と同じく編集した。
  - 行末: 編集したファイルは numstat の 2 通りが一致。新規 `EcsRenderSync.h` は UTF-8 BOM + CRLF。

### SCN-ECS-RENDER-SYNC-SWITCH（2026-10-11、反復 41〜42・run 20261010-223034）

- 変更（反復 41 の実装を反復 42 で検証。コードは保存コミット 2d5be294 に入っている）:
  - 既定の同期を「変わった行だけ」にした。`SyncRenderRowsDelta`: 一覧（`BuildRenderSyncList`）を作り、Remove を SceneView・画面空間の sink から外し（`ApplyRenderSyncRemovals`）、変わった行だけ proxy を作り直して公開する（`PublishRenderRow`。Mesh・Skinned・Light・Board）。生存集合と `RemoveStale*`、dirty でない行の作り直しは、この経路では使わない。
  - Remove は一覧だけで決める: 行の削除のフック、破棄予約の部分木（`NoteSubtreeRenderRemoval`）、走査に入らない行の dirty（`AddRenderSyncRemoval`）。走査に入らない行の公開の状態は Remove を外すときに「公開していない」へ戻す。
  - Mesh の履歴が落ち着くまでの公開し直しは `m_RenderSettleRows`（公開した proxy の前フレームの変換が現在と違う行を次の同期でもう一度公開する）。非表示の SkinnedMesh も見た時点のポーズの版を覚えるので、毎フレーム「変わった行」にならない。
  - ComponentDataRegistry への公開は、レジストリが有効なときだけ（既定は無効なので毎フレームの公開は無い）。MegaGeometry と Text は SCENE-DELTA まで旧の走査（`SyncEntityRecursive`）が行い、`RemoveStaleMegaGeometryProxies` だけ残る。
  - `--ecs-legacy-sync`（`World::SetLegacyRenderSync`）で旧の全走査に戻せる。検査 `ECS_RENDER_SYNC` は、既定の同期では公開の結果を全行から求め直した proxy と照合する（`VerifyRenderSyncDelta`）。
- テスト（paths の外）: `WorldSyncDifferentialTest` に既定の同期と旧の全走査の差分・切り戻しのケースを追加。`BoardComponentRoutingTest` は画面空間 sink の `StaleCount` の期待を既定の同期（0）に合わせ、旧の全走査を `--ecs-legacy-sync` 相当で確かめるケースを足した。
- 結果（`.harness/runs/20261010-223034/`）:
  - `verify-SCN-ECS-RENDER-SYNC-SWITCH-7-build.txt`（SceneViewProxyLookupTest・WorldSyncDifferentialTest の Debug ビルド EXIT=0）、`-8-ctest.txt`（3 件 Passed）。
  - `-11-startup.txt`: 起動画面の決定論の比較 `result=pass`（default 平均輝度差 0・near 0.0001・low 0.0001、すべて limit 内）。
  - `-13-rendering3dtest.txt`・`-13-mega-bananas.txt`・`-13-mega-stress.txt`: `ECS_RENDER_SYNC` を有効にしたシーンごとの周回 3 周が各 `result=pass`、pass=1323/633/2078、fail 無し。
  - `-12-mixed.txt`: 指定の混在周回は `result=fail`（EXIT=1）。`SCENE_CYCLE_DIFF field=textures,texture_bytes` だけで、他の項目の増減は無い。INVALIDATION・PREP と同じ既知の差。
  - 隣接 69 件（反復 41 の `-3-neighbors.txt`）は `BoardComponentRoutingTest` だけタイムアウトだったが、単体の ctest（`-10-board-ctest.txt`）と単独実行 10 回（各 20〜40ms）で通り、再現しない。環境起因の単発。
- BLOCKED: 混在周回の verify だけが ECS 側と無関係な既知のテクスチャ差で通らない。`blocked/SCN-ECS-RENDER-SYNC-SWITCH.md` に、INVALIDATION・PREP と同じ判断を書いた。実装は完了・コミット済み。判断後に `status:` を戻す。
- Notes:
  - 同期の走査（`SyncEntityRecursive`）自体は MegaGeometry・Text のために全 Entity を巡回する。O(変化) になるのは SCENE-DELTA で MegaGeometry を移したあと。
  - 行末: 編集したファイルは numstat の 2 通りが一致。

### SCN-ECS-SCENEQUERY（2026-10-11、反復 43・run 20261010-223034）

- 変更:
  - `SceneQuery::Update(World&)` を足し、毎フレームの `Rebuild` をやめた（ApplicationProcessor）。最初の呼び出しで全 Entity を拾い、以後は変更だけを反映する。
  - 変更の拾い方: ECS の変更ログ（局所の変換・階層・フラグの消費者）、行の削除のフック（EcsEntityFlags の OnDestroy）、描画同期の一覧（`GetRenderSyncList().Changed` の Mesh・SkinnedMesh を、ComponentId → 持ち主の行の逆引きで Entity へ）。Active・破棄待ちの変化は部分木ごと、他は Entity 単体を見直す。
  - BVH: 節に親を持たせ、Entry に葉を覚えさせて、境界が変わった Entry だけ葉から親へ refit（変わらなくなったら止める）。追加は保留の一覧（照会は BVH の後に線形に見る）、削除は墓石。保留・墓石・累積した refit が増えたときだけ作り直す。
  - 作り直しの分割は `std::nth_element` の中央値分割（挿入ソートをやめた）。O(N log N)。
  - 描画同期が旧の全走査（`--ecs-legacy-sync`）のとき・SceneView が無いときは、毎回 Rebuild と同じ全量。
  - 検査 `ECS_SCENEQUERY`: `RunCheck` が全量で作った SceneQuery と、Entry の集合・境界、球・箱・下向き光線の照会結果を照合する。開発ログ `SCENEQUERY updated=` は 120 回ごと。
- テスト（paths の外）: `SceneQueryRaycastTest::TestIncrementalUpdateMatchesRebuild`（最初の全量、静止で全 0、1 件移動で Updated=1、親の移動で子孫も、非アクティブ化と復帰、破棄、追加、保留あふれの作り直し、旧の全走査への切り戻し。各段で全量と照合）。
- 結果（`.harness/runs/20261010-223034/`）:
  - `verify-SCN-ECS-SCENEQUERY-2-build.txt`（SceneQueryRaycastTest の Debug ビルド EXIT=0。新ケースを含めて単体実行も通った）。
  - `-3-ctest.txt`: 4 本 Passed（Overlap・PhysicsFacade・CameraProbe は SceneQueryRaycastTest の束の MEMBER）。
  - `-4-capture.txt`: mega-stress の撮影 `result=pass`、`ECS_SCENEQUERY` が default 3・close 3・aerial 2 pass、fail 0。開発ログ（`.harness/runs/scene-capture/SCN-ECS-SCENEQUERY/check/*.Game.log`）は 1% 移動で `updated=45 entries=4838 rebuilt=0`（累計 updated=5515 / 120 回、作り直し 2 回）。
- Notes:
  - `paths:` の外は World を触っていない。`GetRenderSyncList()` の Changed が描画の同期に依存するので、SceneView が無い／旧の全走査では全量に切り替わる。
  - 描画同期の一覧が MeshComponent の変化を落とす変更が将来入ると、検査 ECS_SCENEQUERY が fail になる（検出用）。
  - 行末: 編集したファイルは numstat の 2 通りが一致。

### SCN-ECS-SCENEQUERY 差し戻し対応（2026-10-11、反復 44・run 20261010-223034）

- 変更（評価者の指摘 3 件）:
  - 局所の変換・階層の変化も、Active と同じく部分木ごと見直す（`RefreshSubtree`）。Mesh を持たない親が動いたときの MegaGeometry を持つ子と、非アクティブな親への付け替えで入らなくなる子孫を拾う。描画同期の一覧は、この 2 つを教えないため。Component の行の階層変化は持ち主の Entity だけ見る。
  - 行の削除のフックの識別子を登録ごとに作る（`HookUser::bLive`）。World の切替・解除で `bLive` を落とし、古い World に残ったフックは何もしない。識別子は SceneQuery が破棄まで持つ。`~SceneQuery` で追跡中の World の購読を解く（World は SceneQuery より長く生きるか、先に `Clear()` を呼ぶ前提）。
  - テストの出力文を日本語にした。
- テスト（paths の外）: `SceneQueryRaycastTest` に MegaGeometry の子・非アクティブな親への付け替えと復帰・World 切替後の古い World の削除、の 3 ケースを追加。各段で全量と照合。
- 結果（`.harness/runs/20261010-223034/`）: `verify-SCN-ECS-SCENEQUERY-44-build.txt`（Debug ビルド EXIT=0）、`-44-ctest.txt`（4 件 Passed）、`-44-capture.txt`（mega-stress の `result=pass`、`ECS_SCENEQUERY` が default 3・close 3・aerial 2 pass、fail 無し。1% 移動で `updated=45 entries=4838 rebuilt=0`、累計 作り直し 2 回）。
- Notes: 行末は numstat の 2 通りが一致。旧の World が SceneQuery より長く生きて切替前に Clear されない場合、その World に残る識別子は無効のまま（SceneQuery 破棄後は呼ばれない前提）。

### SCN-MEGA-SCENE-CONFIRM（2026-10-11、反復 45・run 20261010-223034）

- 変更:
  - `FramePacket` に `SceneGeneration`（シーンの世代）と `SendSerial`（送った番号）を足した。番号は `FramePacketManager::FinishWrite` が 1 から順に振る（飛ばされたパケットがあっても欠けない）。世代は `SceneView` が持ち（`GetSceneGeneration`・`AdvanceSceneGeneration`。`ClearAllProxies`・`ClearMegaGeometryProxies` でも進む）、`FramePacketManager::ResolveSceneGeneration` が源の SceneView の替わり・世代の進みを見てパケットに書く。
  - 共通の場所は `RenderingCoordinator::RenderFrame`（RenderThread のループも、RenderWorld.cpp の直接の経路も通る）。パケットを取った直後に `PacketConfirmation::TryApply` を呼び、適用済みより古い番号・世代のパケットは描かずに捨てる。適用したら、描画を見送っても（スワップチェーンが使えない等の早期 return）スコープの終わりで `NoteFinished` を呼ぶ。EndFrame の提出ごとに `NoteSubmitted` で「適用した番号 ↔ 提出番号」を持ち、`AdvanceCompletedRenderFrameSerial` から `Retire` で完了を取り込む。
  - 公開（atomic）: 確認の番号・確認の世代・完了した番号・捨てた数（`PacketConfirmation::Get*`）。世代を先に、完了を最後に書く。
  - GameThread 側の積み直し: `PacketDeltaLedger`（送った番号ごとの変化の鍵を覚え、確認以前を捨て、確認より後を次のパケットが積み直す。世代が替わると古い世代の分を捨てる）。まだ差分は運んでいない（全量の写しも運ぶまま）。DELTA の項目が使う。
  - 検査 `MEGA_SCENE_CONFIRM`（`RunPacketConfirmCheck`、EndFrame から）: 毎回、確認・世代・完了が単調で、完了 ≤ 確認 ≤ 送った番号、確認の世代 ≤ 送った世代であること（`PacketConfirmationMonitor`）。最初の回と 256 回ごとに、本物の型で 3 つの場面（飛ばされたパケットの差分が積み直される／世代が替わったら古い差分を拒み古いパケットを捨てる／適用した番号と提出番号の対応・提出なしの完了・スワップチェーン作り直しでの完了）を確かめる。開発ログ `MEGA_SCENE_CONFIRM sent= confirmed= completed=` は 120 回ごと。
- 結果（`.harness/runs/20261010-223034/`）:
  - `verify-SCN-MEGA-SCENE-CONFIRM-1.txt`: 指定の撮影 `result=pass`（RelWithDebInfo のビルド pass）。`MEGA_SCENE_CONFIRM` が default 3・close 3・aerial 2 pass、fail 0。開発ログは `sent=120 confirmed=119 completed=118 stale=0`。
  - `-2.txt`: 指定の周回（mega-stress,mega-bananas）は `MEGA_SCENE_CONFIRM` pass=5585・fail 0 だが、`SCENE_CYCLE_DIFF field=textures,texture_bytes`（mega-stress の 2 周目以降 6→9。別シーンが先に読んだテクスチャのキャッシュ。既知）で EXIT=1。
  - `-3-mega-stress.txt`・`-3-mega-bananas.txt`: シーンごとの周回 3 周が各 `result=pass`、`MEGA_SCENE_CONFIRM` pass=3907 / 3878、fail 無し。
  - `-4-build.txt`・`-4-ctest.txt`: Debug の FramePacketManagerTest・RenderThreadFrameCompletionTest・SkeletalFramePacketSnapshotTest のビルド EXIT=0、3 件 Passed。
- BLOCKED: 混在周回の verify だけが、ECS・描画と無関係な既知のテクスチャ差で通らない（`blocked/SCN-MEGA-SCENE-CONFIRM.md`。INVALIDATION・PREP・SWITCH と同じ判断）。実装は完了・コミット済み。
- Notes:
  - `RenderingCoordinator.h` は触っていない（確認の状態は `FramePacketManager` に持たせ、検査は cpp の無名名前空間）。`RenderThread.cpp` も触っていない（`NotifyNewFrame` での置き換え・確認用の破棄はパケットを適用しないので確認されず、飛んだ扱いになる）。
  - 実際のシーンの切り替え（World の SetSceneView・シーンの破棄）は、まだ `SceneView::AdvanceSceneGeneration` を呼んでいない。今は `ClearAllProxies` 系でだけ世代が進む。SCENE-DELTA（World.cpp が paths）で切り替えの口から呼ぶこと。
  - 確認の番号は「その番号のパケットを適用した」であり、それ以前のパケットがすべて適用された意味ではない。差分は「確認より後」を全部積み直す累積にして、飛んだ分を落とさない（`PacketDeltaLedger`）。差分を運ぶ項目では、RenderThread 側で `CanApplyDelta` を使うこと。
  - 起動の読み込み中は RenderThread が最初のパケットで止まり、GameThread だけが数万フレーム先へ進む（周回のログで `confirmed=1`、`sent` は増える）。再開すると確認・完了とも単調に追いつく。既存の挙動で、この項目では変えていない。
  - 行末: 編集したファイルは numstat の 2 通りが一致。

### SCN-MEGA-SCENE-SLOTS（2026-10-11、反復 46・run 20261010-223034）

- 変更:
  - 席の列 `EcsGpuSceneSlot{Value(席+1), Range}` と、席の表 `EcsGpuSceneSlotTable`（新規 `Ecs/EcsGpuSceneSlots.h`。.cpp なしのヘッダ完結）。`EcsWorld` が表を 1 つ持ち、列のフックで割り当て（Create・ColumnAdded）と解放を行う。解放は理由が `Destroy` の削除だけ。移動（AddColumn/RemoveColumn の Move）・入れ替え・チャンクの返却ではフックが呼ばれず、席は行と一緒に動く。`Destroy` 以外の理由で席の列を外したら規則違反として数える。
  - 空きリストは範囲単位（既定 64 席。使える空きを持つ範囲の一覧から LIFO で使い、無ければ末尾の未使用、無ければ新しい範囲）。範囲の AABB は `NoteBounds` で所属する席の境界を包むまで広がり、範囲に使用中も解放待ちも無くなったら捨てる。
  - 再利用の条件（`EcsSlotGate`、新規 `Ecs/EcsSlotGate.h`）: 削除の適用の確認（解放を運ぶパケット = 解放時に送り終えた番号 +1 以上が適用された。世代が進んでいれば満たす）と、最後に使いうる提出の完了（解放時に送り終えた最後のパケットまで完了）の両方がそろうまで、解放した席は待ちの一覧に置く。ゲートが無効（SceneView が無い／書かれていない）の間は直ちに再利用する。
  - ゲートは `RenderingCoordinator`（パケットを送り終えた直後）が `SceneView::SetSlotGate` へ写し、`World` が毎フレーム `AdvanceGpuSceneSlots` で表へ渡す。MegaGeometry の行（`megaColumns`）が席の列を持つ。軽い行用の列 `EcsMegaInstance`・`EcsScatterOwner` と組の作り方 `EcsMegaInstanceColumns` も定義した（MASS-ROWS が使う。席の寿命の検査は軽い行でも確かめた）。
  - 検査 `MEGA_SLOTS`: (1) World が 32 フレームごとに全行と表を突き合わせる（重複・未割り当て・表との食い違い・解放漏れ・範囲の AABB の包含・早すぎる再利用・不正な解放・規則違反が 0）。さらに SceneView の MegaGeometry の proxy の境界が、行の席の範囲の AABB に包まれることを確かめる。(2) RenderingCoordinator が最初のパケットと 256 パケットごとに、本物の `EcsWorld` で場面 A〜D を確かめる（A: 入れ替え・チャンクの返却・列の組の移動で席が変わらず解放もされない／B: 確認だけ・完了だけでは再利用せず両方で再利用、世代が進んだら再利用、消費者がいなければ直ちに再利用／C: 範囲の AABB の包含と空になった範囲の捨て方／D: 突き合わせが重複・未割り当て・表に無い席・Destroy 以外の列の除去を見つける）。
- 結果（`.harness/runs/20261010-223034/`）:
  - `verify-SCN-MEGA-SCENE-SLOTS-1-build.txt`: Core の RelWithDebInfo ビルド EXIT=0。
  - `-4.txt`: 指定の撮影 `result=pass`、`MEGA_SLOTS` が default 5・close 5・aerial 4 pass、fail 0。開発ログは `rows=4675 slots=5056 ranges=79 live=4675 pending=0 ready=325 allocated=5000 released=325 proxies=4675`（proxy の境界の包含も 4675 件確かめた）。
  - `-5-cycle.txt`: 指定の周回（mega-stress,mega-bananas）は `MEGA_SLOTS` pass=1483・fail 無しだが、`SCENE_CYCLE_DIFF field=textures,texture_bytes`（既知。CONFIRM と同じ）で EXIT=1。
  - `-6-mega-stress.txt`・`-6-mega-bananas.txt`: シーンごとの周回 3 周が各 `result=pass`、`MEGA_SLOTS` pass=1018 / 986、fail 無し。mega-bananas の周回の開発ログは `slots=10048 ranges=157 live=0 allocated=30000 released=30000 reused=20000`（周回しても表は増えず、3 周で席を再利用して元に戻る）。
  - `-7-build.txt`・`-8-ctest.txt`: Debug の EntityTreeTest・WorldSyncDifferentialTest のビルド EXIT=0、EcsStorageTest・EcsMoveTest・EcsCommandTest・WorldSyncDifferentialTest の 4 件 Passed。
- BLOCKED: 混在周回の verify だけが、ECS・描画と無関係な既知のテクスチャ差で通らない（`blocked/SCN-MEGA-SCENE-SLOTS.md`。CONFIRM と同じ判断）。実装は完了・コミット済み。
- Notes:
  - `paths:` の外の `World.cpp` を最小限で編集した（アーキタイプの列 1 つ・proxy の公開に境界を書く 1 行・毎フレームの更新の 1 行・検査の関数）。`World.h`・`CMakeLists.txt`・`RenderingCoordinator.h` は触っていない（新しい型はヘッダ完結、状態は EcsWorld と SceneView が持つ）。
  - 席の解放を運ぶパケットは「解放時に送り終えた番号 + 1」と見なしている。SCENE-DELTA は、解放を必ず次に送るパケットに載せること（載せ損ねた分は確認の番号が追い付かないので再利用が遅れるだけで安全側）。
  - 席の表は SceneView が違う（メイン以外）と、ゲートが書かれず消費者なしとして扱う。
  - 検査の履歴（席ごとの境界・解放の記録）は検査が有効なときだけ持つ。World の作成後に有効になっても、その時点から持ち始める（すでにある席は境界なしで、包含の照合から外れる）。
  - 範囲の割り当ては作成順で、個別の実体の範囲は空間的にはまとまらない（AABB は緩い）。散布の範囲（連続した席・Morton 順）は MASS-ROWS で `CreateRange` を足すこと。
  - 行末: 編集したファイルは numstat の 2 通りが一致。新規 `EcsGpuSceneSlots.h`・`EcsSlotGate.h` は UTF-8 BOM + CRLF。

### SCN-MEGA-SCENE-DELTA（2026-10-11、反復 47・run 20261010-223034）

- 変更:
  - World（MegaGeometry の公開）: 変わった行は `PublishRenderRow` の MegaGeometry の枝で公開し、外す proxy は Remove の一覧（`ApplyRenderSyncRemovals`）が外す。既定の同期の走査（`SyncEntityRecursive`）は MegaGeometry の生存集合を作らず、`RemoveStaleMegaGeometryProxies` を呼ばない。proxy には行の席（`SceneSlot` = EcsGpuSceneSlot の値）を書く。検査 `ECS_RENDER_SYNC`（`VerifyRenderSyncDelta`）が MegaGeometry も全行から求め直して照合する。
  - SceneView: 配列の「位置」を鍵にした変化の記録（`TakeMegaGeometryChangedIndices`。追加・更新はその位置、外すと末尾が入った位置）。前の変換の履歴は SceneView が持つ（`PublishMegaGeometryProxy` は既にある proxy の変換を前の変換にし、`BeginMegaGeometrySync` が同期の最初に前の変換を今の変換へ揃える。World が毎同期の最初に呼ぶ）。`SetMegaFullSync` / `IsMegaFullSync`。
  - パケットの差分（`FramePacket::MegaDelta`、新規 `Rendering/MegaGeometryMirror.h`）: 送り手（`MegaGeometryDeltaSender`）が、確認より後の送信の変化を全部積み直す累積の差分（位置・proxy・配列の大きさ）を書く。台帳は既存の `PacketDeltaLedger`（重複の除去を二乗から線形へ直した）。適用された世代が現在の世代でないあいだは全量（基準）を送る。RenderThread の写し（`MegaGeometryMirror`）は `RenderFrame` が `TryApply` の直後（描画を見送っても）に当て、GameThread の SceneView と同じ並びの配列を持つ。変わった席（`GetChangedSlots`・全部が変わった印・版）は `ViewRenderContext::SnapshotMegaChangedSlots`・`bSnapshotMegaAllChanged`・`SnapshotMegaRevision` で渡す。
  - 消費者: MegaGeometryPass（VSM の投影物の入力もここ）と ShadowMapPass（CSM）は `SnapshotMegaGeometryProxies` = 写しを読む。MegaGeometryPass は写しの版が前回と同じフレームは、インスタンスの作り直しとビジビリティの鍵の突き合わせを省く。RT の scene snapshot は GameThread で作るので、パケットではなく GameThread の SceneView の配列を渡す（`BuildFrameSnapshot` の末尾の引数）。
  - やめたもの: `ApplyPreviousObjectStates` の MegaGeometry の毎回の作り直し（`m_PreviousMegaGeometryWorlds`）・全量の写し（`Scene.MegaGeometryProxies = GetMegaGeometryProxies()`）は、`--mega-scene-full-sync` のときだけ残る。`RenderedObjectHistory` の MegaGeometry の覚え直しは、差分の同期ではパケットが全量を持たないので空の走査になる。飛んだフレームの前の変換の付け替え（TAA）は写しの `Apply` が行う。
  - `--mega-scene-full-sync`（Game が OnPostInitialize でメインの SceneView へ渡す）: World が ECS の MegaGeometry の全行を毎フレーム公開し直して `RemoveStaleMegaGeometryProxies` で外し、パケットは配列を全量で写して前の変換を毎回付け直す（写しは使わない）。
  - 検査 `MEGA_SCENE_DELTA`: (1) 送った時点の GameThread の配列の hash をパケットに載せ、RenderThread が差分を当てた写しの hash と照合する（食い違うと fail）。(2) 最初の回と 256 回ごとに、本物の送り手・写し・確認で決まった場面（飛ばされるパケット・遅れて当たる古いパケット・世代の替わりで基準を飛ばされて送り直す・古い世代の差分は当たらない）を確かめる。開発ログ `MEGA_SCENE_DELTA sent= entries= baselines= unconfirmed=` は 120 回ごと。
- テスト（paths の外）: `SceneViewProxyLookupTest` に `TestMegaGeometryDeltaTracking`（位置の記録・取り出し・外したときの末尾の移動・生存集合での外し・クリアで世代が進む・全量の同期は記録しない・前の変換の履歴と揃え）を足した。
- 結果（`.harness/runs/20261010-223034/`）:
  - `verify-SCN-MEGA-SCENE-DELTA-1.txt`: 起動画面の比較（既定）`result=pass`。default / near / low の mean_luminance_difference 0 / 0.0001 / 0.0001、psnr 100 / 65.4 / 70.3 dB、max_channel_difference 1 / 9 / 12（直前の SWITCH の記録と同じ水準）。ログに `MEGA_SCENE_DELTA sent=120 entries=16 baselines=1 unconfirmed=1`。PNG（default.png）を開いて、小屋・岩・球・地面が出ていることを見た。
  - `-2.txt`: `--mega-scene-full-sync` の比較も `result=pass`（同じ水準。ログに「MegaGeometry の同期を全量にしました」、`MEGA_SCENE_DELTA` の行は無い）。
  - `-3-stress.txt`: mega-stress（5000 個・1% 移動・64 m 区画）の撮影 `result=pass`。`MEGA_SCENE_DELTA` default 140・close 144・aerial 107 pass / fail 0、`MEGA_SCENE_CONFIRM`・`MEGA_SLOTS`・`ECS_RENDER_SYNC`（MegaGeometry の照合を足した）・`ECS_RENDER_INVALIDATION`・`ECS_SCENEQUERY` も fail 0。
  - `-4-cycle-*.txt`: シーンごとの周回 3 周が全部 `result=pass`（mega-stress・mega-bananas・rendering3dtest の `MEGA_SCENE_DELTA` pass=1729 / 1740 / 2030、mega-stress・mega-bananas の `ECS_RENDER_SYNC` pass=1938 / 625。fail 無し）。
  - `-5-tests-*.txt`・`-6-lookup.txt`・`-7-neighbors.txt`: Debug の EntityTreeTest・FramePacketManagerTest・RayTracingSceneSnapshotTest・SceneViewProxyLookupTest（新ケースを含む）・SceneViewProxyReconcileTest・WorldSyncDifferentialTest・SceneQueryRaycastTest・RenderThreadFrameCompletionTest と、隣接 10 件（ObjectCast・WorldRenderInterpolation・ApplicationFixedStepPipeline・ComponentDataRegistry・BoardComponentRouting・TextComponentBoardProxy・ParticleFrameSnapshot・SkeletalFramePacketSnapshot・PhysicsFixedStepPipeline・EntityTree）がすべて Passed。
- Notes:
  - `paths:` に World.h が無いので、World の状態は増やしていない（フラグ・履歴・位置の記録は SceneView、送り手・写しは RenderingCoordinator）。World.h の「MegaGeometry は SCENE-DELTA まで旧の走査」のコメントは古いまま（World.h を触れるタスクで直す）。
  - 混合周回（mega-stress,mega-bananas を 1 回の起動で回す）は、この項目の verify に無いので走らせていない（前の項目の記録では既知のテクスチャ差で通らない）。周回はシーンごとに回して pass を確かめた。
  - シーンの切り替えで `AdvanceSceneGeneration` は呼んでいない。差分は状態の累積（位置ごとの最新の proxy と配列の大きさ）なので、切り替えで行・proxy が消えても整合する。世代が進むのは `ClearAllProxies` / `ClearMegaGeometryProxies` だけで、その直後のパケットは基準（全量）になる。
  - 席の解放は、行の Destroy 後の同期でその proxy を SceneView から外し、次に送るパケットの差分（外れた位置・配列の大きさ）が運ぶ。席の再利用の確認（SLOTS のゲート）は「解放時に送り終えた番号 + 1」の適用を待つので、外れたことを運ぶパケットより早く再利用されない。
  - 変わった席の一覧は、最後に適用した差分の分だけ（描画を見送った・飛ばした差分の席は含まない）。GPU の表を散布で書き換える項目（GPU-SCENE-*）は、`SnapshotMegaRevision` が前回見た版より 1 より大きく進んでいたら全部が変わったものとして扱うこと。
  - MegaGeometryPass の作り直しの省略は、`SnapshotMegaChangedSlots` がある（写しを読む）ときだけ。手で足したインスタンス（`AddMegaMeshInstance`）・全量の同期・手で組んだパケットは従来どおり毎フレーム作り直す。
  - 行末: 編集したファイルは numstat の 2 通りが一致。新規 `MegaGeometryMirror.h` は UTF-8 BOM + CRLF。

### SCN-MEGA-SCENE-DELTA 差し戻し対応（2026-10-11、反復 48・run 20261010-223034）

- 直したこと（反復 47 の評価者 NEEDS_WORK の 2 点）:
  - TAA の前の変換: 写しは `Apply` で前の変換を付け替えず（GameThread の値のまま）、描く場所（`RenderedObjectHistory::Apply` の直後）で `MegaGeometryMirror::NoteDrawn(bRebased)` が付け替える。最後に描いたフレームの変換は ObjectId ごとに差分だけで保ち（最後に描いてから変わった位置・外れた物体の印から直す）、描かなかった適用では動かさない。評価者の再現（10 で x=0 を描画 → 11 で x=10 を適用して見送り → 12 で x=20 を適用して描画）は前の変換が 0 になる。検査 `MEGA_SCENE_DELTA` の場面に、この再現・続けて描くフレーム・位置を移った物体・新しい物体・外れて戻った物体を足した。
  - 消費者: 写しに席 → 配列の位置の表（`TryGetPositionBySlot`/`FindBySlot`）を持たせ、席を持たない proxy（`SceneSlot` が 0）が変わったときは全部が変わった扱いにした。RT の MegaGeometry のインスタンスは RenderThread が写しから足す（`RayTracingSceneSubsystem::AppendMegaGeometryInstances`。`BuildAccelerationStructures` の直前）。連番のパストレーシング（`SequenceFrame != 0`）だけ、前の変換の持ち越しが GameThread の snapshot の並びに依るので従来どおり GameThread で作る（`FramePacket::DefersMegaRayTracingToRenderThread`）。MegaGeometryPass（VSM の入力を含む）は、版が 1 だけ進んだフレームを変わった席のインスタンスだけ直す（外れた席・無効な proxy・ObjectId の替わる位置があれば作り直す）。検査が有効なときは直した結果を作り直した結果と照合する。
  - 英語だったエラー文を日本語にした。
- 結果（`.harness/runs/20261010-223034/`）:
  - `verify-SCN-MEGA-SCENE-DELTA-11.txt`: 起動画面の比較（既定）`result=pass`、default/near/low の psnr 100 / 65.4 / 70.4 dB（前回と同水準）。
  - `-12.txt`: `--mega-scene-full-sync` も `result=pass`（同水準）。
  - `-14-checks.txt`: mega-stress の撮影 pass。`MEGA_SCENE_DELTA` default 140・close 144・aerial 107 pass、fail 0。ログに `consumer_patches=113`（直したインスタンスを作り直した結果と照合し、不一致なし）。
  - `-15-cycle-*.txt`: mega-stress・mega-bananas の周回 3 周が各 `result=pass`（`MEGA_SCENE_DELTA` pass=1718 / 1716、`ECS_RENDER_SYNC` fail 無し）。
- Notes:
  - CSM（ShadowMapPass）は写しの配列を読むが、投影物の集め方（メッシュの準備完了を proxy ごとに引く）は変えていない。準備完了は GPU 完了で遅延して変わり、版を持たないので、キャッシュは安全にできない。変わった席で絞るのは SCN-MEGA-SHADOW-DIRTY、RT の席ごとの配列は SCN-MEGA-RT-SELECT / RT-SKIP の範囲。
  - 席の表への散布は SCN-MEGA-GPU-SCENE-SCATTER が `SnapshotMegaRevision` が 1 より大きく進んだら全部が変わったものとして扱うこと（`SnapshotMegaMirror` で席から proxy を引ける）。
  - 行末: 編集したファイルは numstat の 2 通りが一致（混在ファイルは行ごとに保って編集した）。

### SCN-SCENE-CYCLE-SHARED-CACHE（2026-10-11、反復 49・run 20261010-223034）

- 変更（`Game/Scenes/SceneCycleSmoke.cpp/.h`・`Scripts/RunSceneCycleSmoke.ps1` のコメント）: `Finish` の基準を、シーンが 2 つ以上なら 2周目、1 つなら 1周目にした。基準より後の周回を、同じシーンの基準の周回と比べる（増え続ける漏れは従来どおり `SCENE_CYCLE_DIFF` で失敗）。必要な周回数は 基準 + 2（混在は 3、単一は 2）で、足りなければ `SCENE_CYCLE_TOO_FEW cycles= scenes=` で失敗。使った基準を `SCENE_CYCLE_BASELINE cycle=<n> scenes=<数>` で出す。
- 結果（`.harness/runs/20261010-223034/`）:
  - `verify-SCN-SCENE-CYCLE-SHARED-CACHE-1.txt`: Game の RelWithDebInfo ビルド成功（exit 0）。
  - `-2.txt` / `-4.txt`（+ `-4-gamelog.txt`）: rendering3dtest,mega-bananas,mega-stress の 3 周が `SCENE_CYCLE_SMOKE_RUN result=pass`。Game.log に `SCENE_CYCLE_BASELINE cycle=2 scenes=3`、`SCENE_CYCLE_SMOKE result=pass cycles=3 scenes=3 changed=none unsettled=0`。1周目の textures は 0 / 6 / 9、2周目以降はすべて 9 で揃う。
  - `-3.txt`（+ `-3-gamelog.txt`）: mega-stress 単独の 3 周が pass。`SCENE_CYCLE_BASELINE cycle=1 scenes=1`、`changed=none`。
  - `-5-toofew.txt`: 混在で `-Cycles 2` にすると `SCENE_CYCLE_TOO_FEW cycles=2 scenes=3` で fail（終了コード 1）。
- Notes: 夜に止まった 5 項目（SCN-ECS-RENDER-INVALIDATION・RENDER-SYNC-PREP・RENDER-SYNC-SWITCH・SCN-MEGA-SCENE-CONFIRM・SCENE-SLOTS）は、混在の周回が通るようになったので、各項目の verify と評価をやり直せる。TASKS.md の作業ツリーにあった SCN-MEGA-SCENE-DELTA の `status: blocked`（ランナーの記録）はそのまま同じコミットに入れた。

### SCN-ECS-FIX-DESTROY-FLAGS（2026-10-11、反復 1・run 20261011-130759）

- 変更: `Entity::Destroy()` をオーバーライドし、ECS の破棄の印（`EcsEntityFlags` の `bPendingDestroy`）を立ててから `Object::Destroy()`（World の破棄予約の一覧への登録）を呼ぶ。`MarkForDestroy()` は `Destroy()` を呼ぶだけにした。公開の `Destroy()`・`ObjectHeap::EnqueueDestroy()`（`Object::Destroy` の仮想呼び出し）・`MarkForDestroy()` が同じ入口を通るので、変更ログが更新され、祖先の破棄予約で子・孫の Tick が古い EffectiveActive で呼ばれない。
- テスト（`EcsHierarchyTest`、既存の束 EntityTreeTest の中）: 祖先の停止の検査を SetActive(false)・MarkForDestroy・公開の Destroy() の 3 通り × 旧/新の伝播で count=0 にした。Heap 経由と直接の Destroy() で ECS の印が立つ検査を足した。
- 結果（`.harness/runs/20261011-130759/`）: `verify-SCN-ECS-FIX-DESTROY-FLAGS-1.txt` Game の RelWithDebInfo ビルド exit=0、`-2.txt` Debug の EntityTreeTest・ApplicationFixedStepPipelineTest のビルド exit=0、`-3.txt` TransformHierarchyTest・EcsHierarchyTest・WorldTickGroupTest・SpringArmComponentTest・ApplicationFixedStepPipelineTest が 5/5 Passed。
- Notes:
  - Heap 経由の検査は、Entity を World の木へ Heap から入れられない（`AddObject` が拒否）ので、World に付かない Heap の Entity で ECS の印（予備の値）が立つことを見ている。World の木での子・孫の抑止は、直接の Destroy() の検査が受け持つ。
  - 検証の ctest は束の EntityTreeTest.exe を使う。個別の名前（EcsHierarchyTest など）では cmake のターゲットが無いので、EntityTreeTest をビルドする。
  - Git Bash から `cmake --build ... -- /m:1` を呼ぶと `/m:1` がパスに変換されて失敗する。PowerShell から呼ぶこと。
  - SCN-ECS-TRANSFORM-SWITCH を done にした（評価の 2 周目の指摘はこの 1 点だけだった）。

### SCN-ECS-FIX-DESTROY-FLAGS 差し戻し対応（2026-10-11、反復 2・run 20261011-130759）

- 直したこと（反復 1 の評価者 NEEDS_WORK の 2 点）:
  - `World::RegisterPendingDestroy` が Entity を積むとき、`getbPendingDestroy().Set(true)` で ECS の破棄の印と変更ログも更新するようにした。Object のフラグだけ立てて直接登録する経路でも、祖先の破棄予約で子・孫の古い EffectiveActive が採用されない。`Destroy()` は呼ばない（再帰を避ける）。
  - `EcsHierarchyTest` の祖先の停止の検査に、`Object` 経由の仮想呼び出し（`ObjectHeap::EnqueueDestroy` が `slot.Instance->Destroy()` で呼ぶ形と同じ）と、`SetFlag(OF_PendingDestroy)` + `RegisterPendingDestroy` の直接呼び出しを足した。どちらも旧/新の伝播で子・孫の count=0。
- 結果（`.harness/runs/20261011-130759/`）: `verify-SCN-ECS-FIX-DESTROY-FLAGS-4.txt` Game の RelWithDebInfo ビルド exit=0、`-5.txt` Debug の EntityTreeTest・ApplicationFixedStepPipelineTest のビルド exit=0、`-6.txt` 5 件が 5/5 Passed。
- Notes: 現行の World は Heap 所有の Entity を木へ入れられない（`AddObject` が拒否）ので、Heap 経由の「子・孫の count=0」は、Heap の予約が通る仮想呼び出し（`Object::Destroy`）を World の木の Entity に対して行う形で検査した。Heap の Entity 単体の ECS の印の検査（予備の値）も残している。

### SCN-ECS-FIX-SCENEQUERY-HOOKS（2026-10-11、反復 3・run 20261011-130759）

- 変更: 行の削除のフックの相手 `SceneQuery::HookUser` を参照カウント（`Thread::Atomic`）にし、`EcsColumnHooks` に `ReleaseUser` を足した。`SetColumnHooks` が true を返した時点で EcsWorld が 1 参照を持ち、フックの差し替え・解除・EcsWorld の破棄で 1 度だけ `ReleaseUser` を呼ぶ（false のときは呼び出し側に残る）。SceneQuery は自分の 1 参照を `RetireHookUser`（World の切替・`DetachTracking`）で手放すので、SceneQuery が先に消えても、切り替えた前の World に残ったフックが指す識別子はその World が持つ参照で生きる（bLive は false なので何もしない）。`m_HookUsers` の配列は廃止。
- テスト（`SceneQueryRaycastTest` の束、新しい実行ファイルは足していない）: `TestIncrementalUpdateSurvivesQueryDestroyedBeforeOldWorldRows`（World A・B を生かしたまま Update(A)→Update(B)→Clear→SceneQuery 破棄→A・B の Entity を削除）、`TestEcsColumnHooksReleaseUser`（差し替え・解除・EcsWorld の破棄で手放しが 1 度ずつ）。
- 結果（`.harness/runs/20261011-130759/`）: `verify-SCN-ECS-FIX-SCENEQUERY-HOOKS-1.txt` Debug の SceneQueryRaycastTest ビルド成功、`-2.txt` 4 テスト 4/4 Passed、`-3.txt` mega-stress の撮影 `result=pass`・`ECS_SCENEQUERY pass=3 fail=0`。
- Notes: SCN-ECS-SCENEQUERY を done にした（残りの指摘はこの 1 点だけだった）。切り替えた前の World に登録した変更ログの消費者は、その World が消えるまで残る（古い World は触れないため）。行末は numstat の 2 通りが一致。TASKS.md は誤って LF にしかけたので CRLF に戻した。

### SCN-MEGA-FIX-DELTA-SKIP（2026-10-11、反復 4・run 20261011-130759）

- 変更:
  - `MegaGeometryMirror`: 変わった席を、消費者が取り込む（`NoteDrawn` を通る）まで溜める。取り込んだあとの最初の `Apply` で数え直し、数え始めた版を `GetChangedBaseRevision()` で出す。`CanPatchFrom(消費者が前に見た版)` が true のとき（全部が変わった扱いでなく、見た版から数えているとき）だけ、変わった席で直せる。描画を見送った差分や空の差分を挟んでも席を取り逃がさない。
  - 前の変換の付け替えをフレーム限定にした。`NoteDrawn(true)` が付け替えた席は GameThread の値を覚え、次の `Apply`（または `NoteDrawn`）の頭で戻して、変わった席に入れ、版を進める。静止した物体の前の変換が、付け替えのまま残らない。
  - `MegaGeometryPass::Setup`: 作り直しの省略の条件を `版 == 前回 + 1` から `mirror.CanPatchFrom(前回の版)` へ。
- テスト（`WorldSyncDifferentialTest`、新しい実行ファイルは足していない）: `TestMegaMirrorKeepsChangesAcrossSkippedDraw`（影ありで描画 → 影なしを適用して見送り → 空の差分で描画。基準を見送った場合・版が離れた消費者も）、`TestMegaMirrorRebaseLastsOnlyOneFrame`（10 で x=0 → 11 見送り → 12 静止 x=10 を描画 → 13 空の差分で前の変換が 10）。消費者の模型は、変わった席で直せるときは直し、直せないときは取り直す。
- 結果（`.harness/runs/20261011-130759/`）: `verify-SCN-MEGA-FIX-DELTA-SKIP-1.txt` Debug の WorldSyncDifferentialTest ビルド exit=0、`-2.txt` ctest 1/1 Passed、`-3.txt` 起動画面の決定論比較 `result=pass`（default の psnr 100 dB、基準 SCN-MEGA-BASE-RWDI と同水準）。
- Notes:
  - SCN-MEGA-SCENE-DELTA を done にした（評価の 2 周目の指摘 2・3 はこの項目で解消。1 は前の反復で消費者を写しへ切り替え済み）。
  - 変わった席は「消費者が前に見た版から今まで」の累積になった。GPU の表を散布で書き換える項目（GPU-SCENE-*）は、`mirror.CanPatchFrom(前回見た版)` が false なら全部が変わったものとして扱うこと（前の Notes の「版が 1 より大きく進んだら」は、この判定に置き換わる）。
  - 付け替えの戻しは、付け替えた席の `Index` と `ObjectId` が同じときだけ行う。同じフレームで差分が席を書き換えるなら、差分の値が勝つ。
  - 行末: 編集したファイルは numstat の 2 通りが一致するか確認する（混在ファイルは行ごとに保った）。

### SCN-MEGA-FIX-DELTA-SKIP 差し戻し対応（2026-10-11、反復 5・run 20261011-130759）

- 直したこと（反復 4 の評価者 NEEDS_WORK）: 検査 `MEGA_SCENE_DELTA` の決まった場面（`VerifyMegaDeltaProtocol`、`RenderingCoordinator.cpp`）が、変わった席の累積方式に合っていなかった。基準→非基準差分を続けて当てると `AreAllChanged()` が真のまま席の一覧が空になり、「運んだ proxy の席が、変わった席に入っていない」で落ちた。`AreAllChanged()` のあいだは席の包含を求めず（全部が変わった扱い）、基準を当てた直後は `AreAllChanged()` を求める形に分けた。検査用の消費者が取り込んだ地点で `mirror.NoteDrawn(false)` を呼び、次の適用から変わった席を数え直す。
- 結果（`.harness/runs/20261011-130759/`）: `verify-SCN-MEGA-FIX-DELTA-SKIP-4.txt` Debug の WorldSyncDifferentialTest ビルド exit=0、`-5.txt` ctest 1/1 Passed、`-6.txt` 起動画面の決定論比較 `result=pass`、`-8-checks.txt` mega-stress の撮影（`-RequireChecks MEGA_SCENE_DELTA,MEGA_SCENE_CONFIRM,ECS_RENDER_SYNC`）で `MEGA_SCENE_DELTA` default 286・close 294・aerial 107 pass / fail 0、他の 2 検査も fail 0。
- Notes: 指定の撮影（CaptureStartupScene）では `MEGA_SCENE_DELTA` が有効にならない。この検査を見るには CaptureScene.ps1 に `-RequireChecks MEGA_SCENE_DELTA` を付ける。行末は numstat の 2 通りが一致（11/5）。

### SCN-ECS-RENDER-INVALIDATION 再検証（2026-10-11、反復 6・run 20261011-130759）

- 変更なし。差し戻し対応（逆引きの旧参照・走査しない部分木の公開状態の列・英語コメント）は 3d8e475e でコミット済みで、混在周回の判定は SCN-SCENE-CYCLE-SHARED-CACHE で直っているので、元の verify を通し直した。
- 結果（`.harness/runs/20261011-130759/`）:
  - `verify-SCN-ECS-RENDER-INVALIDATION-1.txt`: 起動画面の決定論比較 `result=pass`（default の psnr 100 dB、基準 SCN-MEGA-BASE-RWDI と同水準）、`ECS_RENDER_INVALIDATION` default pass=3・fail=0、EXIT=0。
  - `-2.txt`: 3 シーン混在の周回（rendering3dtest,mega-bananas,mega-stress、3 周）が `SCENE_CYCLE_SMOKE_RUN result=pass`、`ECS_RENDER_INVALIDATION` pass=4282・fail 無し、Game.log に `SCENE_CYCLE_BASELINE cycle=2 scenes=3`・`changed=none unsettled=0`、EXIT=0。
- Notes: SCN-ECS-RENDER-INVALIDATION を done にした。同期はまだ全走査のまま（次の SYNC-SWITCH で一覧へ切り替える）。
### SCN-ECS-RENDER-SYNC-PREP 再検証（2026-10-11、反復 7・run 20261011-130759）

- 変更なし。実装はコミット済み（3d8e475..3311c3f）で、止まった理由の混在周回の判定は SCN-SCENE-CYCLE-SHARED-CACHE で直っているので、元の verify を通し直した。
- 結果（`.harness/runs/20261011-130759/`）:
  - `verify-SCN-ECS-RENDER-SYNC-PREP-1.txt`: mega-stress の撮影（5000 体・動く 1%・ストリーミング）で `ECS_RENDER_SYNC pass=3 fail=0`、`CAPTURE_SCENE result=pass`、EXIT=0。
  - `-2.txt`: 3 シーン混在の周回（rendering3dtest,mega-bananas,mega-stress、3 周）が `SCENE_CYCLE_SMOKE_RUN result=pass`、`ECS_RENDER_SYNC pass=4108`、2 周目以降の textures は 9 で揃う、EXIT=0。
- Notes: SCN-ECS-RENDER-SYNC-PREP を done にした。同期はまだ全走査のまま（次の SYNC-SWITCH で一覧へ切り替える）。

### SCN-ECS-RENDER-SYNC-PREP 全走査モードでの再検証（2026-10-11、run 20261011-144300）

- 既定は差分同期（SYNC-SWITCH の実装）なので、一覧を全走査の結果と直接照合する `EcsRenderSyncCompare` は全走査のときだけ通る。両 verify の `-GameArgs` に `ecs-legacy-sync` を足し、全走査で通し直した。実装の変更はなし。
- 結果（`.harness/runs/20261011-144300/`）:
  - `verify-SCN-ECS-RENDER-SYNC-PREP-1.txt`: mega-stress の撮影（5000 体・動く 1%・ストリーミング、overview.Game.log に `Arg[11]=--ecs-legacy-sync`）で `ECS_RENDER_SYNC pass=3 fail=0`、`CAPTURE_SCENE result=pass`、EXIT=0。
  - `-2.txt`: 3 シーン混在の周回（rendering3dtest,mega-bananas,mega-stress、3 周、Game.log に `Arg[5]=--ecs-legacy-sync`）が `SCENE_CYCLE_SMOKE_RUN result=pass`、`ECS_RENDER_SYNC pass=1336`・fail 0、`SCENE_CYCLE_BASELINE cycle=2 scenes=3`・`changed=none unsettled=0`、EXIT=0。
- Notes: SCN-ECS-RENDER-SYNC-PREP を done にした。差分同期側の照合（`VerifyRenderSyncDelta`）は SYNC-SWITCH の verify で扱う。

### SCN-ECS-RENDER-SYNC-SWITCH 再検証（2026-10-11、反復 2・run 20261011-144300）

- 変更なし。実装は保存コミット 2d5be294（記録は 0eae6a22）に入っており、止まった理由の混在周回の判定は SCN-SCENE-CYCLE-SHARED-CACHE で直っているので、元の verify を通し直した。
- 結果（`.harness/runs/20261011-144300/`）:
  - `verify-SCN-ECS-RENDER-SYNC-SWITCH-1.txt`: SceneViewProxyLookupTest・WorldSyncDifferentialTest の Debug ビルド EXIT=0。
  - `-2.txt`: SceneViewProxyReconcileTest・SceneViewProxyLookupTest・WorldSyncDifferentialTest の 3 件 Passed、EXIT=0。
  - `-3.txt`: 起動画面（default、RelWithDebInfo、決定論）を SCN-MEGA-BASE-RWDI と比較し `result=pass`（平均輝度差 0、PSNR 100 dB、不一致画素 2E-06）。
  - `-4.txt`: 3 シーン混在の周回（rendering3dtest,mega-bananas,mega-stress、3 周）が `SCENE_CYCLE_SMOKE_RUN result=pass`、2 周目以降の textures は 9・texture_bytes は揃う、EXIT=0。
- Next: SCN-MEGA-SCENE-DELTA で MegaGeometry を差分同期へ移すと、同期の走査が O(変化) になる。
- Notes: SCN-ECS-RENDER-SYNC-SWITCH を done にした。Git Bash から `cmake --build ... -- /m:1` を呼ぶとスイッチがパスに変換されて MSBuild が失敗するので、verify は PowerShell で走らせた。

### SCN-ECS-RENDER-SYNC-SWITCH 差し戻しの修正（2026-10-11、反復 3・run 20261011-144300）

- 変更:
  - ComponentDataRegistry の捕捉（`BeginFrameCapture`・全 Entity の `PublishTransform`・Mesh/MegaGeometry の `Publish*Proxy`）を旧の全走査（`--ecs-legacy-sync`）だけに限った。既定の同期では、レジストリが有効でも消去・作り直し・公開をしない（走査には `nullptr` を渡し、既定の同期用の Mesh の作り直しと公開のブロックを消した）。ハンドルの登録・解除はそのまま。
- テスト（paths の外）: `ComponentDataRegistryTest` の `TestEnabledNoSyncPublishesWithoutTouchingSceneView` を `TestEnabledDefaultSyncDoesNotCaptureOrRebuild` に置き換えた（レジストリ有効・既定の同期で、変わらない行の proxy が作り直されず、レジストリの捕捉が空のまま）。捕捉を見る他のケースと `ComponentDataBenchmark` は `SetLegacyRenderSync(true)` にした。
- 結果（`.harness/runs/20261011-144300/`）:
  - `verify-SCN-ECS-RENDER-SYNC-SWITCH-5.txt`: SceneViewProxyLookupTest・WorldSyncDifferentialTest の Debug ビルド EXIT=0。
  - `-6.txt`: SceneViewProxyReconcileTest・SceneViewProxyLookupTest・WorldSyncDifferentialTest の 3 件 Passed、EXIT=0。
  - `-7.txt`: 起動画面（default、RelWithDebInfo、決定論）を SCN-MEGA-BASE-RWDI と比較し `result=pass`（平均輝度差 0、PSNR 100 dB、不一致画素 5E-06）。PNG を開いて確認。
  - `-8.txt`: 3 シーン混在の周回 3 周が `SCENE_CYCLE_SMOKE_RUN result=pass`、2 周目以降の textures 9・texture_bytes は揃う、EXIT=0。
  - `regtest-SCN-ECS-RENDER-SYNC-SWITCH-3.txt`: ComponentDataRegistryTest・BoardComponentRoutingTest・SceneQueryRaycastTest Passed、ComponentDataBenchmark は旧の全走査で transformData=1000・meshData=1000。
- Next: SCN-MEGA-SCENE-DELTA。
- Notes:
  - 旧の全走査から既定の同期へ実行中に切り替えると、レジストリには最後の旧の捕捉が残る（切り替えは起動オプションで、レジストリの利用者はテストとベンチマークだけ。SCN-ECS-RETIRE-LEGACY でレジストリごと消す）。
  - 行末: World.cpp・World.h は編集で全体が LF になったので、HEAD の行末へ戻した。numstat の 2 通りが一致。

### SCN-MEGA-SCENE-CONFIRM の検証の通し直し（2026-10-11、反復 4・run 20261011-144300）

- 実装はコミット済み（bf927fa..d78eea3）。混在周回の判定の修正（SCN-SCENE-CYCLE-SHARED-CACHE）の後に verify だけを通し直した。コードの変更は無い。
- 結果（`.harness/runs/20261011-144300/`）:
  - `verify-SCN-MEGA-SCENE-CONFIRM-1.txt`: mega-stress（5000 本、動く 1%、セル 64）の overview を RelWithDebInfo で撮影し、`engine_check name=MEGA_SCENE_CONFIRM pass=3 fail=0`、`result=pass`、EXIT=0。
  - `-2.txt`: mega-stress・mega-bananas の混在周回 3 周が `SCENE_CYCLE_SMOKE_RUN result=pass`、`MEGA_SCENE_CONFIRM pass=3284`、ログに `SCENE_CYCLE_BASELINE cycle=2 scenes=2`、2 周目以降の textures 9・texture_bytes は揃う、「約束が破れた」は 0 件、EXIT=0。
- Next: SCN-MEGA-SCENE-SLOTS（verify の通し直し）。
- Notes: Git Bash の `sed -i` は TASKS.md の CRLF を LF に落とすので、書き換えの後に CRLF へ戻した（numstat の 2 通りが一致）。

### SCN-MEGA-SCENE-CONFIRM 差し戻しの修正（2026-10-11、反復 5・run 20261011-144300）

- 変更:
  - `PacketConfirmation` に、GPU が完了した最後の提出番号を atomic で公開する `GetCompletedSubmissionSerial()` を足した。`Retire()` が完了の番号を進めた後に書き、`Reset()` で 0 に戻す。GPU 側の番号をそのまま公開するので、スワップチェーンの作り直しで提出番号が戻るとこの値も戻る。
  - 検査 `MEGA_SCENE_CONFIRM` の場面 C で、提出番号 99・100 の完了と、作り直し後の 4 が公開されることを確かめるようにした。120 回ごとのログに `submission=` を足した。
  - 古いパケットを捨てたときの警告の説明を日本語にした。
- 結果（`.harness/runs/20261011-144300/`）:
  - `verify-SCN-MEGA-SCENE-CONFIRM-3.txt`: mega-stress（5000 本、動く 1%、セル 64）の overview を RelWithDebInfo で撮影し、`MEGA_SCENE_CONFIRM pass=3 fail=0`、`result=pass`、EXIT=0。overview.Game.log に `submission=118` が出て、新しいバイナリで走ったことを確認。
  - `-4.txt`: mega-stress・mega-bananas の混在周回 3 周が `SCENE_CYCLE_SMOKE_RUN result=pass`、`MEGA_SCENE_CONFIRM pass=3403`、ログに `SCENE_CYCLE_BASELINE cycle=2 scenes=2`、2 周目以降の textures 9・texture_bytes は揃う、「約束が破れた」は 0 件、EXIT=0。
- Next: SCN-MEGA-SCENE-SLOTS（verify の通し直し）。
- Notes: FramePacket.h・RenderingCoordinator.cpp は編集で行末が崩れたので、HEAD の行末へ戻した（numstat の 2 通りが一致）。

### SCN-MEGA-SCENE-SLOTS の検証の通し直し（2026-10-11、反復 6・run 20261011-144300）

- 実装はコミット済み（d78eea3..490cabf）。混在周回の判定の修正（SCN-SCENE-CYCLE-SHARED-CACHE）の後に verify だけを通し直した。コードの変更は無い。
- 結果（`.harness/runs/20261011-144300/`）:
  - `verify-SCN-MEGA-SCENE-SLOTS-1.txt`: mega-stress（5000 本、動く 1%、セル 64）の overview を RelWithDebInfo で撮影し、`engine_check name=MEGA_SLOTS pass=5 fail=0`、`result=pass`、EXIT=0。overview.Game.log の最後の突き合わせは `rows=4675 ranges=79 released=325 ready=325 proxies=4675`（ストリーミングで席が解放されている）、「席の規則が破れた」は 0 件。
  - `-2.txt`: mega-stress・mega-bananas の混在周回 3 周が `SCENE_CYCLE_SMOKE_RUN result=pass`、`MEGA_SLOTS pass=950`、ログに `SCENE_CYCLE_BASELINE cycle=2 scenes=2`、2 周目以降の textures 9・texture_bytes は揃う、周回中に `released=36000 reused=26000` まで再利用が起き、「席の規則が破れた」は 0 件、EXIT=0。
- Next: SCN-ECS-PACKET-DELTA。
- Notes: 前回の BLOCKED の記録（`blocked/SCN-MEGA-SCENE-SLOTS.md`）にあるとおり、実装は `paths:` に当時無かった World.cpp を最小限編集している（今の `paths:` には含まれる）。

### SCN-MEGA-SCENE-SLOTS の差し戻しの修正（2026-10-11、反復 7・run 20261011-144300）

- 差し戻し: Destroy の解放が、前の同期で取り込んだゲート（その後に送ったパケットを知らない）の番号で記録され、席を含むパケットの完了と、削除を運ぶパケットの確認を待たずに再利用できた。
- 修正: `EcsGpuSceneSlotTable::Release` は解放を運ぶ番号を決めずに待ちへ積み、解放の後の最初の `Advance`（同期の先頭。ゲートは直前に送ったパケットの番号を持ち、削除はこの同期で SceneView へ反映される）で、運ぶ番号 = 送った番号 + 1、最後に使いうる提出 = 送った番号を記録する。ゲートが無効な間の解放も次の Advance までは使える一覧へ移さない（最初のパケットを送った直後の Destroy も同じ穴だった）。
- 検査 `MEGA_SLOTS` の場面 B を実際の呼び出しの順（同期で Advance → 送る → Tick で Destroy → 次の同期で Advance）に書き直し、差し戻しの再現の順で再利用しないこと・最初のフレームの解放・世代の前進・消費者がいない場合を確かめる。場面 C も同じ順に合わせた。
- 結果（`.harness/runs/20261011-144300/`）:
  - `verify-SCN-MEGA-SCENE-SLOTS-3.txt`: mega-stress（5000 本、動く 1%、セル 64）の overview を RelWithDebInfo で撮影し、`engine_check name=MEGA_SLOTS pass=5 fail=0`、`result=pass`、EXIT=0。Game.log の最後の突き合わせは `rows=4675 ranges=79 pending=0 ready=325 released=325`、「席の規則が破れた」は 0 件。
  - `-4.txt`: mega-stress・mega-bananas の混在周回 3 周が `SCENE_CYCLE_SMOKE_RUN result=pass`、`MEGA_SLOTS pass=941`、ログの `MEGA_SLOTS result=fail` と「席の規則が破れた」は 0 件、`released=36000 reused=26000`、EXIT=0。
- Next: SCN-ECS-PACKET-DELTA。
- Notes: World.cpp・RenderingCoordinator.cpp は編集で行末が崩れたので、HEAD の行末へ戻した（numstat の 2 通りが一致）。

### SCN-ECS-PACKET-DELTA（2026-10-11、反復 8・run 20261011-144300）

- 変更:
  - パケットに `ProxyArrayDelta<MeshProxy/SkinnedMeshProxy/LightProxy>` を足し、GameThread の `ProxyArrayDeltaSender` が SceneView の変わった位置（`Take*ProxyChangedIndices`）と確認されていない送信の分だけを積む（MegaGeometry と同じ確認の台帳 `PacketDeltaLedger`）。基準（確認された世代が違うあいだ）だけパケットの `Scene.*Proxies` に全量を書く。毎フレームの `Scene.MeshProxies / SkinnedMeshProxies / LightProxies = SceneView` の全量の写しはやめた。
  - RenderThread は適用の確認と同じ場所で `ProxyArrayMirror` に差分を当て、`SnapshotMeshProxies` などの消費者と `RenderedObjectHistory` は写しを読む（差分の無い手組みのパケットは従来どおり `Scene` の配列）。
  - 構成の revision の hash の Mesh・Skinned の集合（数・XOR・和）を `ProxyRevisionSet` で変わった位置だけ更新する。光源表（SceneView の光源 + 空の太陽）は GameThread で変わった位置だけ保ち、その hash は変わったときだけ計算し直す。`BuildPointShadowSnapshot` と RT の物体 ID の照合は GameThread の配列を読む。
  - 検査 `PACKET_DELTA`: 3 種の写しの hash を送った時点の全量と照合し、増分の集合を全部を数え直した値と照合する。最初と 256 回ごとに、飛ばし・遅れた適用・世代替わりの基準の飛ばし・古い世代の拒否の場面を確かめる。
- 結果（`.harness/runs/20261011-144300/`）:
  - `verify-SCN-ECS-PACKET-DELTA-1.txt`: 起動画面（default、RelWithDebInfo、決定論）が `PACKET_DELTA pass=171 fail=0`、SCN-MEGA-BASE-RWDI と比較し平均輝度差 0・PSNR 100 dB・不一致画素 4E-06 で `result=pass`、EXIT=0。PNG を開いて確認。Game.log の `PACKET_DELTA sent=120 mesh_entries=33 light_entries=2`（基準の 33 件の後は運んだ件数 0）。
  - `-2.txt`: mega-stress（5000 本・動く 1%・セル 64）の overview が `PACKET_DELTA pass=140 fail=0`、`result=pass`、EXIT=0。PNG を開いて確認。
- Next: SCN-ECS-MASS-ROWS。
- Notes:
  - RenderThread の `RenderedObjectHistory` は記録に MeshProxy を毎フレーム全部回す（既存。写しを読むだけに変えた）。差分化は別の項目。
  - 写しの SortDepth は、SceneView がカリングで書いた値でなく最後に運んだ値になる（RenderThread の消費者は読まない。照合の hash も含めない）。
  - 手組みのパケットの経路を残したので、生成したパケットの `Scene.SkinnedMeshProxies` を見る SkeletalFramePacketSnapshotTest は最初のパケットが基準になるため変わらない見込み（この反復では走らせていない）。

### SCN-ECS-MASS-ROWS（2026-10-11、反復 9・run 20261011-144300）

- 変更:
  - `MegaGeometryScatterComponent`（Object 1 つ）を足した。入力（局所の位置・向き・倍率）を `SetInstances` で受け、入力の位置の Morton 順に軽い行（`EcsMegaInstanceColumns`: ワールドの変換・MegaGeometry の値・席・持ち主の散布）を作る。入力の番号 → 格納の順の表を持ち、`UpdateInstances(first, values)` は入力の番号を指す。保存に見えるのはメッシュ・影・可視・種・本数のプロパティだけ。
  - 席の表 `EcsGpuSceneSlotTable` に塊（連続した範囲の組）の予約を足した（`ReserveBlock`・`BeginBlockAllocation`・`ReleaseBlock`）。散布の行は塊の席を先頭から順に取るので、席の番号と範囲（64 席）が Morton 順に連続する。塊の範囲は散布の専用で、解放した席を個別の実体へ回さず、全席の再利用の条件がそろった塊を次の予約が使い直す。
  - 公開は範囲ごと（範囲の全行を書いた後に、その範囲の proxy をまとめて公開し、席の範囲の AABB を広げる）。Active・可視は範囲の印（`SetRangeActive`・`SetRangeVisible`）と Component の有効・可視で、変わった範囲だけを公開・取り下げる。破棄（World の木から外れる `OnEcsRowDetaching`）で proxy を取り下げ、全行を削除し（席は理由が Destroy の削除で解放）、塊を返す。
  - proxy の鍵は `MakeMegaScatterProxyKey`（bit 62 の印 | 散布の番号 | 入力の番号）。`SceneView::RemoveStaleMegaGeometryProxies`（旧の走査・全量の同期）は散布の鍵を外さない。変換と境界は 1 本ずつの MegaGeometryComponent と同じ式。
  - mega-bananas は 1 本ずつの Entity + MegaGeometryComponent をやめ、配置の計算はそのままに入力の配列へ積み、散布 1 つで置く。`MEGA_BANANAS_PLACED` に `batches=1 scatter_rows scatter_ranges` を足した。
- 結果（`.harness/runs/20261011-144300/`）:
  - `verify-SCN-ECS-MASS-ROWS-1.txt`: 1 万本の overview（RelWithDebInfo）を SCN-MEGA-BASE と比べ、平均輝度差 0・PSNR 65.4 dB・不一致画素 0.024%（最大 160）で `result=pass`、EXIT=0。PNG を開いて確認（山 3 つと敷き詰め・影）。Game.log は `MEGA_BANANAS_PLACED count=10000 ... batches=1 scatter_rows=10000 scatter_ranges=157`、`MEGA_SCATTER_ROWS ... ecs_rows=10006 slots=10048 table_ranges=157 blocks=1`。山の 780 本の姿勢の行（`MEGA_BANANAS_PILE_ITEM`）は基準のログと diff が空（配置の値と順が同じ）。
  - `-2.txt`: mega-bananas の周回 3 周が `SCENE_CYCLE_SMOKE_RUN result=pass`（entities=2・資源の数は 3 周とも同じ）、EXIT=0。周回のログの `MEGA_SCATTER_ROWS` は 3 周とも `first_slot=0 slots=10048 table_ranges=157 blocks=1`（塊を使い直す）、`MEGA_SCATTER_RELEASED` は 3 周とも `ecs_rows=2 slots_live=0 blocks_in_use=0`。ERROR の行は 0。
- Next: SCN-ECS-MASS-STREAM。
- Notes:
  - 画像の差（不一致画素 0.024%）は、SceneView の配列への公開の順が作成の順から席の順（Morton 順）に変わったことによる重なりの順の差。閾値の内側。
  - 散布の行はすべて 1 つのアーキタイプに入れ、破棄は後ろの行から 1 行ずつ消す（設計の「BatchId で分けたチャンクをまとめて返す」は入れていない）。
  - 所有者の変換の変化と SceneView の付け替えは、まだ散布へ自動で反映しない（入力を与え直すと反映する）。予算つきの反映は MASS-STREAM。
  - 範囲の印の切り替えは、SceneView では範囲の行の数だけ公開・取り下げる（描画側に範囲単位の印は無い）。
  - World.cpp（この項目の paths の外）の検査 ECS_RENDER_SYNC・RENDER_INVALIDATION の照合（`VerifyRenderSyncDelta`）は、散布の proxy を「行が無いのに残っている」と数える見込み。`IsMegaScatterProxyKey` で外す 1 行が要る（この反復の verify では検査を有効にしていない）。テストの追加はしていない。

### SCN-ECS-MASS-ROWS 差し戻し 1 回目の対応（2026-10-11、反復 10・run 20261011-144300）

- 変更（指摘 2: BatchId ごとのチャンク分離とチャンク単位の返却）:
  - `EcsWorld` にチャンクの組を足した。`GetOrCreateArchetype(columns, tags, chunkGroup)` の組はアーキタイプの同一性に含み、列の追加・削除・孤児の移動でも保つ。`AcquireChunkGroup` は返された組を使い直す（アーキタイプの数は同時に生きている組の数で止まる）。`ReleaseChunkGroup` は組に行が残っていれば拒む。
  - `EcsWorld::DestroyArchetypeRows`: 後ろのチャンクから、チャンク内の全行の OnDestroy（理由 Destroy。席も解放）を呼んでから番号を世代を進めて空きへ戻し、チャンクを丸ごと返す（行の入れ替えをしない）。
  - 散布は自分の組のアーキタイプに行を作り、破棄でそのアーキタイプの全行をチャンクごと返してから、組と塊を返す。ログに `chunk_group chunks archetype_rows`（作成）と `destroyed chunks_returned`（解放）を足した。
- 結果（`.harness/runs/20261011-144300/`）:
  - `verify-SCN-ECS-MASS-ROWS-3.txt`: 1 万本の overview を SCN-MEGA-BASE と比べ、平均輝度差 0・PSNR 69.2 dB・不一致画素 0.02%（最大 67）で `result=pass`、EXIT=0。PNG を開いて確認（山 3 つと敷き詰め・影）。Game.log は `MEGA_SCATTER_ROWS ... chunk_group=1 chunks=55 archetype_rows=10000`、`MEGA_BANANAS_PLACED count=10000 ... batches=1`、ERROR は 0。
  - `-4.txt`: mega-bananas の周回 3 周が `SCENE_CYCLE_SMOKE_RUN result=pass`（entities=2・資源の数は 3 周とも同じ）、EXIT=0。周回のログは 3 周とも `chunk_group=1 chunks=55`（組を使い直す）、解放は `destroyed=10000 chunks_returned=55 ecs_rows=2 slots_live=0 blocks_in_use=0`。ERROR は 0。
- 未対応（指摘 1）: 検査 ECS_RENDER_SYNC・RENDER_INVALIDATION の照合（`World::VerifyRenderSyncDelta` の `viewMega` と、全走査側の `scanStale` の MegaGeometry のループ）が散布の proxy を残留と数える。直す先の `Library/Core/Private/Object/World.cpp` がこのタスクの paths の外なので、`blocked/SCN-ECS-MASS-ROWS.md` に記録して止めた（paths に足せば `IsMegaScatterProxyKey` の proxy を飛ばす数行で直る）。status は doing のまま。
- Next: SCN-ECS-MASS-ROWS（World.cpp を paths に足した後、指摘 1 を直して検査付きの起動と verify 2 本を通し直す）。
- Notes: チャンクの組と `DestroyArchetypeRows` の ECS の単体テストは足していない（verify の周回のログで返却の数を確かめた）。

### SCN-ECS-MASS-STREAM（2026-10-11、反復 11・run 20261011-144300）

- 変更:
  - `Ecs/EcsMassWork.h`（新規・ヘッダのみ）: 予算（1 フレームの行数と ms）と仕事の列 `EcsMassWorkQueue` を足した。仕事は範囲 1 つ・チャンク 1 つの単位で進め、列は全項目の Poll の後、先頭をずらしながら 1 単位ずつ回し、行数か時間を使い切ったら止める（1 フレームに少なくとも 1 単位）。`EcsWorld` が 1 つ持ち（`GetMassWork`）、World の LateTick の段の系 `LateTick.MassWork`（最初の散布が登録）が毎フレーム回す。
  - `EcsWorld::DestroyArchetypeTailChunks(archetype, maxRows)`: 末尾のチャンクから丸ごと返しながら maxRows 行以上を消す（`DestroyArchetypeRows` はこれの全行版）。
  - `MegaGeometryScatterComponent`: `SetInstances` は席の塊とチャンクの組を予約するだけにし、行は列が範囲ごとに作って、範囲の全行を作って書いてから公開する。所有者のワールドの変換（Poll で値の変化を見る）・メッシュ・影の変化は版を進め、範囲ごとに全行を書き直してから公開し直す（それまでは前の値で描く）。解放（破棄・入力の与え直し）は公開中の proxy をすぐ全部取り下げ（持ち主が直後にメッシュを返しても描画が読まない）、行の削除はチャンクごとに予算で分ける後始末の仕事を列に持たせる（Component が先に消えても続く）。メッシュは表で持ち、入力ごとに `MeshIndex` で選ぶ。
  - 検査 `MASS_RANGES`: 公開中の範囲は全行が生きていて、描ける全行の proxy が行の値（席・メッシュ・変換・影）と一致し、描かない印のまま公開していない。公開していない範囲と解放した散布の鍵の proxy は無い。仕事のあったフレームと、無いフレームの 60 回に 1 回突き合わせる。
  - Game: `--stress-batch-cells`（区画の動かない Entity を区画ごとの散布 1 つにまとめ、区画の解放・読み込みは散布の Entity ごと出し入れする。動く Entity は 1 つずつのまま）、`--mass-budget-rows`・`--mass-budget-ms`（予算）。mega-stress・mega-bananas は散布の作成が全範囲の公開まで進んでから SceneReady を出す（一度そろえば待たない）。ひと続きの仕事が終わると `MASS_WORK_SETTLED frames rows max_ms` をログに出す。
- 結果（`.harness/runs/20261011-144300/`）:
  - `verify-SCN-ECS-MASS-STREAM-3.txt`: mega-stress（5000 個・動く 1%・64 m 区画・散布）の overview が `MASS_RANGES pass=10 fail=0`、`result=pass`、EXIT=0。PNG を開いて確認（散布前の撮影と同じ並び。カメラ手前の解放した区画は描かれない）。Game.log は `MEGA_STRESS_STREAM_SETUP cells=32 batches=32 batch_rows=4950`、最初の作成は `MASS_WORK_SETTLED frames=6 rows=4950`、ERROR は 0。
  - `-4.txt`: mega-stress・mega-bananas の周回 3 周が `SCENE_CYCLE_SMOKE_RUN result=pass`、EXIT=0。バナナ 1 万本は 4〜5 フレームで作成（1 フレーム最大 1.02 ms）、解放は 2 フレームで `destroyed=10000 chunks_returned=55 slots_live=0 blocks_in_use=0`、塊を使い直す（`blocks=1`）。ERROR は 0。
  - （-1・-2 は同じ verify を、計測用のログを足す前のコードで通したもの）
  - 解放と読み戻しの経路（verify の短い撮影では読み戻しに届かない）: 周期 0.05 秒で 300 フレーム撮った起動（`.harness/runs/scene-capture/SCN-ECS-MASS-STREAM/reload`）が load 188 回・unload 192 回で `MASS_RANGES pass=1238 fail=0`、`MEGA_STRESS_STREAM_CYCLE` は全周期 `result=pass`（席の表は slots=6208・範囲 97 で止まる）。
- 計測（RelWithDebInfo・1920x1080・overview・300 フレーム・動く 1%・64 m 区画・周期 1 秒・散布）:
  - 5 万個は描画側が GPU メモリ不足で落ちる（`.harness/runs/scene-capture/SCN-ECS-MASS-STREAM/measure-default`。`MEGA_BVH bvh_instances=48159 leaf_capacity=75146943 queue_entries=165171094` の後に RenderThread が「バッファメモリの割り当てに失敗」、`VRAM_BUDGET heap_budget_mb=11847 heap_usage_mb=14975`）。BVH の容量はインスタンスの数とメッシュで決まり（MegaGeometryPass.cpp の bvhLeafCapacity）、散布か 1 つずつの Entity かに依らない。落ちる前の作成は `MASS_WORK_SETTLED frames=39 rows=49500 max_ms=2.814`（325 区画）。
  - 3 万個（200 区画・29,700 行）: 既定の予算（8192 行・1 ms）で gt median 2.424 ms・p95 3.935 ms（GPU median 51.4 ms・rt median 113.4 ms）。最初の作成は 22 フレーム・1 フレーム最大 2.044 ms、区画 1 つの出し入れは 1 フレーム・最大 0.416 ms、`Ecs.Sys.LateTick.MassWork` は median 0.014 ms・max 0.563 ms（`measure-30k-default`）。
  - 同じ 3 万個を予算なし（`mass-budget-rows=10000000,mass-budget-ms=0`）: 最初の作成が 1 フレームで 18.412 ms。窓の gt median 1.749 ms・p95 2.9 ms（GPU median 37.4 ms。区画の出し入れは予算に掛からない大きさなので、窓の差は描画側のばらつき）（`measure-30k-unlimited`）。
  - 予算の既定値: 1 フレーム 8192 行・1 ms（`Ecs::kDefaultMassRowsPerFrame`・`kDefaultMassMillisecondsPerFrame`）。行 1 本の作成は約 0.6 µs（29,700 行で 18.4 ms）なので、1 ms で約 1,600 行・64 m 区画（約 160 行）なら 1 フレームで済む。大きな散布の作成はフレームあたり 2 ms 程度に収まる（予算なしの 18 ms のスパイクを避ける）。8192 行は速い機械での上限。
- Next: SCN-MEGA-MIXED-LOAD。
- Notes:
  - 5 万個の gt p95 は、描画の BVH のメモリ（paths の外）が 5 万個を載せられないため測れず、載る最大の 3 万個で測った。5 万個の計測は BVH のメモリを減らした後（SCN-MEGA-MEASURE-CPU など）で取り直す。
  - `EcsMassWork.h` は Core の CMakeLists（paths の外）のヘッダの一覧に足していない（ヘッダのみなのでビルドには要らない）。
  - 1 万本のバナナの作成が 4〜5 フレームに分かれたので、撮影は全範囲の公開を待ってから撮る（SceneReady の条件に足した）。100 万本では約 1 ms × 625 フレームかかる見込み。読み込み画面のように待つ場面で予算を広げるかは、MIXED-LOAD の計測で決める。
  - 範囲の Active・可視の印と Component の有効・可視の切り替えは、従来どおり予算に掛けず、その場で範囲の行の数だけ公開・取り下げる。
  - テストは足していない（列と散布の仕事の単体テストは計画に無い。verify の検査とログで確かめた）。

### SCN-ECS-MASS-STREAM 反復 12（2026-10-11、run 20261011-144300。評価の差し戻しへの対応）

- 変更:
  - メッシュの表を版ごとに持つ（`MegaGeometryScatterComponent::MeshTable`）。範囲は行を書いたときの表の世代を指し（`ScatterRange::MeshGeneration`）、公開（`ReflectRange`）と検査（`Audit`）は範囲が指す表で proxy を作る。書き直しを待つ範囲は前の表のメッシュで描き、どの範囲も指さなくなった表は捨てる（指している範囲の数で数える）。
  - 散布の番号（proxy の鍵）を、席を予約するたびに列から新しく取る（`EcsMassWorkQueue::AcquireBatchId`。Component の番号から作るのをやめた）。同じ Component に入力を与え直しても、新しい範囲の proxy と解放中の前の行の鍵が重ならないので、解放の検査が新しい proxy を残留と数えない。
  - 解放の後始末を 1 単位ずつにした: チャンク 1 つの返却（`DestroyArchetypeTailChunks(archetype, 1)`）、アーキタイプの外へ移った行の 1,024 行ずつの削除（継続位置を持つ）、塊と組の返却のどれか 1 つを 1 単位とし、列が単位の間で行数と時間の予算を見る。移った行の走査は、散布のアーキタイプの行の数が作った行の数と合わない（作成の失敗・列の追加）ときだけ行い、合えば行の表を渡さない。
- 結果（`.harness/runs/20261011-144300/`）:
  - `verify-SCN-ECS-MASS-STREAM-5.txt`: mega-stress（5000 個・動く 1%・64 m 区画・散布）の overview が `MASS_RANGES pass=8 fail=0`、`result=pass`、EXIT=0。PNG を開いて確認（前の撮影と同じ並び）。Game.log の ERROR は 0。最初の作成は `MASS_WORK_SETTLED frames=4 rows=4950 max_ms=1.064`、区画の解放は `MEGA_SCATTER_RELEASED ... chunks_returned=1 frames=1`。
  - `-6.txt`: mega-stress・mega-bananas の周回 3 周が `SCENE_CYCLE_SMOKE_RUN result=pass`、EXIT=0、Game.log の ERROR は 0。バナナ 1 万本の解放はチャンク 1 つずつで 2 フレーム（`destroyed=10000 chunks_returned=55 slots_live=0 blocks_in_use=0`、1 フレーム最大 0.198 ms）。
- 未対応（指摘 4）: 5 万個 + 64 m 区画の gt p95 は、描画の BVH の列が VRAM に載らず落ちるため測れない（反復 11 の measure-default。直す先は paths の外の Rendering）。条件の扱いをユーザーの判断に回した（`blocked/SCN-ECS-MASS-STREAM.md`。推奨は 3 万個の計測で完了とし、5 万個は MEASURE-CPU で測る）。status は doing のまま。
- Next: SCN-ECS-MASS-STREAM の 5 万個の条件の判断待ち。判断が出るまでは次の todo へ。
- Notes:
  - 差し戻しの API の再現（予算 64 行でのメッシュの差し替え・同じ Component への与え直し・0.001 ms の予算での破棄）の単体テストは足していない（計画に無い。修正は制御の流れで確かめ、verify の検査とログで回帰が無いことを確かめた）。

### SCN-ECS-MASS-ROWS 差し戻し 1 回目の対応・指摘 1（2026-10-11、反復 13・run 20261011-144300）

- 変更（指摘 1: 検査 ECS_RENDER_SYNC・ECS_RENDER_INVALIDATION の照合が散布の proxy を残留と数える）:
  - `World::VerifyRenderSyncDelta` の `viewMega` と、旧の全走査の `FinishRenderSyncFrame` の `scanStale` の MegaGeometry のループで、`Rendering::IsMegaScatterProxyKey` の proxy を飛ばす。散布の行の proxy は散布の Component が公開と取り下げを持ち、照合は検査 MASS_RANGES が行う。
- 結果（`.harness/runs/20261011-144300/`）:
  - `check-SCN-ECS-MASS-ROWS-syncchecks.txt`: mega-bananas 1 万本を `--engine-checks=ECS_RENDER_SYNC,ECS_RENDER_INVALIDATION` 付きで撮影し、`ECS_RENDER_SYNC pass=2 fail=0`・`ECS_RENDER_INVALIDATION pass=2 fail=0`、`result=pass`、EXIT=0。pass の 2 回目は散布の公開（`MASS_WORK_SETTLED`）の後。Game.log の ERROR は 0。
  - `check-SCN-ECS-MASS-ROWS-syncchecks-legacy.txt`: 同じ撮影を `--ecs-legacy-sync`（全走査の照合）で、`ECS_RENDER_SYNC pass=1 fail=0`、EXIT=0、ERROR は 0（fail は毎回報告されるので、散布の公開後も残留の検出は無い）。
  - `verify-SCN-ECS-MASS-ROWS-5.txt`: 1 万本の overview を SCN-MEGA-BASE と比べ、平均輝度差 0.0001・PSNR 64.9 dB・不一致画素 0.026%（最大 160）で `result=pass`、EXIT=0。PNG を開いて確認（山 3 つと敷き詰め・影）。Game.log は `MEGA_BANANAS_PLACED count=10000 ... batches=1 scatter_rows=10000 scatter_ranges=157`、山の 780 本の `MEGA_BANANAS_PILE_ITEM` は基準のログと同じ。ERROR は 0。
  - `-6.txt`: mega-bananas の周回 3 周が `SCENE_CYCLE_SMOKE_RUN result=pass`（entities=2・資源の数は 3 周とも同じ）、EXIT=0。解放は 3 周とも `destroyed=10000 chunks_returned=55 ecs_rows=4 slots_live=0 blocks_in_use=0`。ERROR は 0。
- Next: 次の todo。
- Notes: 検査名は `ECS_RENDER_INVALIDATION`（`RENDER_INVALIDATION` を与えると検査が動かず pass も fail も出ない）。

### SCN-ECS-MASS-STREAM 反復 14（2026-10-11、run 20261011-144300。計測を 3 万個に直した後の検証）

- 変更: なし（反復 12 の直しと、SCN-ECS-MASS-ROWS の World.cpp の照合の修正の後のコードで verify を通し直し、3 万個を取り直した）。
- 結果（`.harness/runs/20261011-144300/`）:
  - `verify-SCN-ECS-MASS-STREAM-7.txt`: mega-stress（5000 個・動く 1%・64 m 区画・散布）の overview が `MASS_RANGES pass=7 fail=0`、`result=pass`、EXIT=0。PNG を開いて確認（前の撮影と同じ並び）。Game.log の ERROR は 0。最初の作成は `MASS_WORK_SETTLED frames=3 rows=4950 max_ms=1.045`、区画の解放は `MEGA_SCATTER_RELEASED ... chunks_returned=1 frames=1`。
  - `-8.txt`: mega-stress・mega-bananas の周回 3 周が `SCENE_CYCLE_SMOKE_RUN result=pass`、EXIT=0、Game.log の ERROR は 0。バナナ 1 万本の解放は 3 周とも `destroyed=10000 chunks_returned=55 frames=2 slots_live=0 blocks_in_use=0`、作成は 4〜6 フレーム（1 フレーム最大 1.085 ms）。
  - `measure-SCN-ECS-MASS-STREAM-30k-iter14.txt`: 3 万個（200 区画・29,700 行）+ 64 m 区画・既定の予算（8192 行・1 ms）・RelWithDebInfo・1920x1080・300 フレームで **gt median 1.92 ms・p95 3.104 ms**（GPU median 65.4 ms・rt median 119.8 ms）、EXIT=0、ERROR は 0。最初の作成は 22 フレーム・1 フレーム最大 2.652 ms、区画 1 つの出し入れは 1 フレーム・最大 0.325 ms、`Ecs.Sys.LateTick.MassWork` は median 0.012 ms・max 0.294 ms（`.harness/runs/scene-capture/SCN-ECS-MASS-STREAM/measure-30k-iter14`）。反復 11 の値（gt p95 3.935 ms）と同じ程度。
- 予算の既定値: 反復 11 で決めた 1 フレーム 8192 行・1 ms（`Ecs::kDefaultMassRowsPerFrame`・`kDefaultMassMillisecondsPerFrame`）のまま。区画の出し入れは予算に掛からず 1 フレームで済む。
- Next: SCN-MEGA-MIXED-LOAD。
- Notes: 5 万個 + 64 m 区画の gt p95 は、描画の BVH の列が VRAM に載らないため、BVH の改修の後の節目の計測（SCN-MEGA-MEASURE-CPU）で取る。

### SCN-MEGA-MIXED-LOAD（2026-10-11、反復 15・run 20261011-144300）

- 変更:
  - `Game/GameModes/MegaBananas/BananaLayout.h/.cpp`（新規）: mega-bananas のバナナの配置（山 3 つ・ポアソン円盤の敷き詰め・形の点）を共有の関数 `BuildBananaShape`・`PlaceBananas` に移した。敷き詰めはマスク（`IBananaFieldMask`）が塞いだ位置を飛ばす。mega-bananas はマスク無しで呼ぶので配置は変わらない（1 万本の山の 780 本の座標と `MEGA_BANANAS_PLACED` の行が SCN-ECS-MASS-ROWS の撮影のログと一致）。
  - mega-stress: `--banana-count=<n>` を指定したときだけ、mega-bananas と同じ配置のバナナ n 本を散布 1 つ（Object 1 つと軽い行）で個別の Entity と同時に置く。敷き詰めは置いた Entity の足元（メッシュの原点から最も遠い頂点までの水平の距離 × 拡大率 + 0.15 m）と、動く Entity の円運動の範囲を空ける。置いた数は `MEGA_STRESS_BANANAS count requested pile field scatter_rows scatter_ranges` と `MEGA_STRESS_PLACED ... bananas bananas_requested` に出る。指定が無ければ今までどおりバナナを置かない（既定の見た目は変わらない）。
  - `--camera-path-play`（開発ビルドだけ）: カメラパスを持つシーン（mega-bananas・sandbox）で、組み立てが済んだ（SceneReady の条件がそろった）ところから `--camera-path-time`（無ければ 0 秒）でパスを再生したままにする。組み立ての間はその時刻の位置に止めて置く。再生中は 60 フレームごとに `CAMERA_PATH_TIME time frames` をログに出す。パスの無い mega-stress では警告して無視する。
  - `Scripts/CaptureScene.ps1`: 視点ごとに `placed_entities`（`MEGA_STRESS_PLACED` の entities）と `placed_bananas`（`MEGA_STRESS_PLACED` の bananas か `MEGA_BANANAS_PLACED` の count）を metrics.json に書き、`-RequireAssets` のときは `-GameArgs` の `stress-entities`・`banana-count` と違えば失敗にする。
- 混在の負荷の 4 つの起動の引数（ACCEPT-1M で使う。`-GameArgs` の形）:
  - 静止: `-Scene mega-bananas -Views horizon -GameArgs banana-count=1000000`
  - カメラパスの再生: `-Scene mega-bananas -Views horizon -GameArgs banana-count=1000000,camera-path-play,camera-path-time=0`（視点はパスが決める。計測の窓はパスの 0 秒から）
  - 1% 移動: `-Scene mega-stress -GameArgs banana-count=1000000,stress-entities=50000,stress-moving-percent=1`
  - 64 m 区画のストリーミング: `-Scene mega-stress -GameArgs banana-count=1000000,stress-entities=50000,stress-moving-percent=1,stress-stream-cell=64,stress-batch-cells`
- 結果（`.harness/runs/20261011-144300/`）:
  - `verify-SCN-MEGA-MIXED-LOAD-1.txt`: RelWithDebInfo の Game のビルド EXIT=0（新しい .cpp を拾うため、その前に `cmake -S . -B build` で構成し直した）。
  - `verify-SCN-MEGA-MIXED-LOAD-2.txt`: **fail**（EXIT=1）。10万本 + 5000 個は置く所までは引数どおり（`MEGA_STRESS_PLACED entities=5000 requested=5000 ... bananas=100000 bananas_requested=100000`、`MASS_WORK_SETTLED frames=68 rows=100000`）だが、描画側の VRAM が尽きて Game が abort（-1073740791。非プール 1.0 GB → 14.1 GB、予算 11.7 GB、`MEGA_BVH bvh_instances=104837 levels=6 leaf_capacity=57588508`）。単体の 10万本（4.3 GB）と 5000 個（4.6 GB）はどちらも載る。
  - `check-SCN-MEGA-MIXED-LOAD-30k.txt`: 同じ引数で 3万本にした混在が `result=pass`・EXIT=0・ERROR 0。`CAPTURE_SCENE placed view=overview entities=5000 bananas=30000 requested_entities=5000 requested_bananas=30000`、metrics.json に `placed_entities=5000`・`placed_bananas=30000`。World の Object は 5003（床・カメラ・散布 1 つ・Entity 5000）でバナナの本数によらない。非プール 8.0 GB。PNG を開いて確認（奥に Entity の列、手前は出し入れで解放した区画で、バナナは区画の Entity の足元を空けて残る）。
  - `check-SCN-MEGA-MIXED-LOAD-bananas100k.txt`: mega-bananas 10万本の単体が `result=pass`・`placed_bananas=100000`。
  - `check-SCN-MEGA-MIXED-LOAD-pathplay.txt`: mega-bananas 1 万本 + `camera-path-play,camera-path-time=5` が pass。ログは `CAMERA_PATH state=placed time=5.000` → `CAMERA_PATH state=playing time=5.000` → `SCENE_READY` → `CAMERA_PATH_TIME time=6.000 frames=60`。PNG を開いて確認（パスの 6 秒の位置から山を見る絵）。
- 未完: verify 2 本目（10万本 + 5000 個）が描画の VRAM（paths の外の Rendering。SCN-MEGA-BVH-INDIRECT の対象と同じ系統）で落ちる。verify の規模の扱いをユーザーの判断に回した（`blocked/SCN-MEGA-MIXED-LOAD.md`。推奨は 3万本に下げて完了、10万本以上は BVH-INDIRECT の後の節目の計測で確かめる）。status は doing。
- Next: SCN-MEGA-MIXED-LOAD の verify の規模の判断待ち。判断が出るまでは次の todo へ。
- Notes:
  - mega-stress の床が overview で白く飛んで見えるのは、この変更の前の撮影（SCN-ECS-MASS-STREAM/check）でも同じ（この項目では触っていない）。
  - 混在の敷き詰めは Entity の足元を空けるので、Entity の多い所ではバナナが隙間に斑に残る。出し入れで解放した区画も足元は空けたまま。
  - マスクと `--camera-path-play` の単体テストは足していない（計画に無い。撮影のログで確かめた）。

### SCN-ECS-TICK-SIMPLE（2026-10-11、反復 16・run 20261011-144300）

- 変更:
  - 型ごとの Tick の登録（設計 1.12）: `Component::ComponentTickRegistration`（本体の非仮想の関数・主群だけで呼ぶか・呼ぶ群）と `World::RegisterComponentTick`/`FindComponentTick` を足した。照合は型（IClass）の完全一致で、リフレクションを宣言しない派生（テストの TestBoardComponent・SocketTimeProbe など）は基底の型として扱われる。World の順序付きの系（Tick.DispatchGroups ほか、同じ（群, 優先度, 深さ優先の順）の一覧）は、登録した型の項目では本体を、それ以外は旧の仮想の OnTickGroup を呼ぶ。登録の無い Game の FollowCamera（SpringArm の派生）は自前の OnTickGroup のまま。
  - Core の型の登録（World の初期化）: Board・Billboard・Impostor・Text は `BoardComponent::TickFlipbook`（主群）、SpringArm は `TickArm`（主群 = Camera）、HoldSlot は `TickHolds`・SocketAttachment は `TickAttachment`（PostPhysics の群だけ。主群によらない）。Mesh・MegaGeometry・Camera・Light 系は本体なし（一覧に載せない）。仮想の Tick/OnTickGroup の上書きはこれらの型から消した。channel は型の GetTimeChannel のまま（仮想の GetTimeChannel は TICK-REMOVE で消す）。
  - Mesh・Board の変換のキャッシュ: Tick での初回の計算をやめ、`RefreshRenderTransformCache` が初期化から一度も求めていなければ先に 1 回求める（前フレームの変換が今の値に揃い、初回の Tick の後に同期していた頃と同じ値になる）。
  - 検査 `ECS_TICK_DUP`: 収集ごとに（呼び先, 群）を数え、同じ収集で 2 回呼んだもの、登録のある型を仮想の経路で呼んだものを fail にする。pass は最初と 64 収集ごと。
  - テスト: FlipbookAdvanceTest・EntitySubtreeSnapshotTest の `Tick` を `TickFlipbook` に、SpringArmComponentTest の `Tick` を `TickArm` に直した。
- 結果（`.harness/runs/20261011-144300/`）:
  - `verify-SCN-ECS-TICK-SIMPLE-1.txt`: Debug の EntityTreeTest・FlipbookAdvanceTest・BoardComponentRoutingTest のビルド EXIT=0（エラー・警告なし）。
  - `-2.txt`: SpringArmComponentTest・FlipbookAdvanceTest・BoardComponentRoutingTest・BillboardComponentTest・WorldTickGroupTest の 5/5 が pass、EXIT=0。
  - `-3.txt`: 起動画面 default を `ECS_TICK_DUP` 付きで撮り、`engine_check name=ECS_TICK_DUP pass=3 fail=0`（count 25〜29）、SCN-MEGA-BASE-RWDI との比較は平均輝度差 0・PSNR 100 dB・不一致画素 2E-06（最大差 1）で `result=pass`、EXIT=0。PNG を開いて確認（天球・地面・球・岩・家）。Game.log の ERROR は 0。
  - `check-SCN-ECS-TICK-SIMPLE-anim-serializer.txt`: SocketAttachment・HoldSlot を World の Tick で回す SkeletalAnimationSamplingTest と、直した EntitySubtreeSnapshotTest が pass（verify の外の確認）。
- Next: SCN-ECS-TICK-ANIM。
- Notes:
  - 検査 ECS_TICK_DUP と型の登録の単体テストは足していない（計画に無い。起動画面の検査で確かめた）。
  - 一覧を作り直すのは今も毎フレーム（BuildTickSnapshot）。一覧と仮想の経路を消すのは TICK-REMOVE。
  - verify 1 本目の `/m:1` は Git Bash だとパスに変換されて MSBuild が拒む。PowerShell から回した。

### SCN-ECS-TICK-SIMPLE 差し戻しへの対応（2026-10-11、反復 17・run 20261011-144300）

- 指摘: 移した本体が旧の一覧（BuildTickSnapshot）に載って毎フレーム集めてソートされ、スケジューラの順序付きの系（AddOrderedEntry）に繋がっていない。登録に Reads/Writes・channel が無い。
- 変更:
  - スケジューラ: `OrderedSystemDesc::bCallerDriven`（段の中では呼ばず、同じ段の GameThread のシステムが順の一覧を読んで呼ぶ）、読み口 `GetOrderedRunCount`/`GetOrderedRun`（`OrderedRunView`）、順番まで付け替える `SetOrderedEntryKey(handle, group, priority, ordinal)` を足した。
  - World: 順序付きの系を 3 つ登録（`Tick.OrderedInputToPoseFinalize`・`LateTick.OrderedPostPhysics`・`LateTick.OrderedCameraToPreRender`）。登録のある型の Component は（Component, 群）ごとの呼び先（`World::OrderedTickRecord`、User が指す）として AddOrderedEntry し、旧の一覧には載せない。`DispatchTickGroups` は順序付きの系の順の一覧と旧の一覧（未移行の Entity::Tick・仮想の OnTickGroup）を TickDispatchLess で合流させて呼ぶ（群が替わるときの UpdateWorldTransforms も同じ位置）。
  - 一覧の更新: 収集（深さ優先の番号の振り方は旧と同じ）を `Tick.BuildSnapshot` のシステムから `TickWithFrameTimes` の収集を開く直前へ移した（間に走る処理は無い）。そこで新しい呼び先を足し、優先度・順番が変わったものだけ `SetOrderedEntryKey`、channel が変わったものは足し直し、見つからなかったもの（破棄予約の部分木・群や mask から外れたもの）を外す。スケジューラの並べ直しは、この足し・付け替え・外しがあった収集の先頭だけ。収集の中の変更は旧の一覧と同じく次の収集から。木から外れた Component は `InvalidateTickComponent` で即座に外す（走っている収集の中でも呼ばれない）。Finalize とスケジューラの作り直しで全部外す。
  - 登録: `ComponentTickRegistration` に `TimeChannel`・`Reads`・`Writes` を足した（Board: 書く EcsBoardRender。SpringArm: Unscaled、読む EcsWorldTransform・書く EcsLocalTransform。HoldSlot・SocketAttachment: 読む EcsWorldTransform・EcsSkinnedRender、書く EcsLocalTransform）。呼び先の delta は項目の channel（個体の channel を収集で写す）から取る。
  - `TickDispatchEntry::Run` を消した（旧の一覧には登録のある型が載らないため）。検査 ECS_TICK_DUP のログの説明文を日本語にした。
- 結果（`.harness/runs/20261011-144300/`）:
  - `verify-SCN-ECS-TICK-SIMPLE-4.txt`: Debug の EntityTreeTest・FlipbookAdvanceTest・BoardComponentRoutingTest のビルド EXIT=0。
  - `-5.txt`: SpringArmComponentTest・FlipbookAdvanceTest・BoardComponentRoutingTest・BillboardComponentTest・WorldTickGroupTest の 5/5 が pass、EXIT=0。
  - `-7.txt`: 起動画面 default を `ECS_TICK_DUP` 付きで撮り、`engine_check name=ECS_TICK_DUP pass=3 fail=0`（count 25・29・29）、SCN-MEGA-BASE-RWDI との比較は平均輝度差 0・PSNR 100 dB・不一致画素 2E-06（最大差 1）で `result=pass`、EXIT=0。PNG を開いて確認（天球・地面・球・岩・家）。Game.log の ERROR は 0。Game.exe（20:24）と Core.lib（20:22）は World.cpp の編集（20:18）より後。`-6.txt` は途中で出力の受け口が切れた 1 回目の走行が後から書いたもの（内容は同じく pass）。
  - `check-SCN-ECS-TICK-SIMPLE-ordered-related.txt`: EcsSchedulerTest・TickDispatchTest・WorldTimeChannelTest・EntityTreeTest・SkeletalAnimationSamplingTest（SocketTimeProbe の channel を含む）・EntitySubtreeSnapshotTest の 6/6 が pass（verify の外の確認）。
- Next: SCN-ECS-TICK-ANIM。
- Notes:
  - 収集の走査（旧の一覧を作るもの）は未移行の型のために毎フレーム残る。順序付きの系の項目は、その走査の番号と比べて変わったときだけ付け替える。走査と旧の一覧を消すのは TICK-REMOVE。
  - スケジューラに足した口（bCallerDriven・GetOrderedRun・順番の付け替え）の単体テストは足していない（計画に無い）。
  - World.h（paths の外だが World.cpp の非公開のメンバの宣言）に呼び先の記録と関数の宣言を足した。

### SCN-MEGA-MIXED-LOAD 完了（2026-10-11、反復 18・run 20261011-144300）

- 変更: 無し（実装は反復 15 の ce9c5906。verify の規模はユーザーの判断で 3万本 + 5000 個に下げた）。反復 16・17 の Tick の変更が入ったので、今のコードで verify を通し直した。
- 4 つの起動の引数は反復 15 の節（上）のとおり。ACCEPT-1M の 100万本 + 5万個の混在は BVH の列の改修（SCN-MEGA-BVH-INDIRECT）の後の節目の計測で確かめる。
- 結果（`.harness/runs/20261011-144300/`）:
  - `verify-SCN-MEGA-MIXED-LOAD-3.txt`: RelWithDebInfo の Game のビルド EXIT=0。
  - `verify-SCN-MEGA-MIXED-LOAD-4.txt`: `CaptureScene.ps1 -Scene mega-stress -Views overview -GameArgs banana-count=30000,stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64` が `result=pass`・EXIT=0。`CAPTURE_SCENE placed view=overview entities=5000 bananas=30000 requested_entities=5000 requested_bananas=30000`。metrics.json に `placed_entities=5000`・`placed_bananas=30000`。Game のログは ERROR 0、`MEGA_STRESS_BANANAS count=30000 requested=30000 pile=780 field=29220 scatter_rows=30000 scatter_ranges=469`。PNG を開いて確認（奥に Entity の列、手前の解放した区画にバナナが Entity の足元を空けて残る。反復 15 の絵と同じ）。
- Next: TASKS.md の次の todo。
- Notes:
  - Git Bash から `cmake --build ... -- /m:1` を呼ぶと `/m:1` がパスに変換されて MSBuild が失敗する。verify のビルドは PowerShell で走らせる。

### SCN-ECS-TICK-ANIM（2026-10-11、反復 19・run 20261011-144300）

- 変更:
  - Animator・SkinnedMesh の仮想の OnTickGroup と SkinnedMesh の Tick を消し、群ごとの非仮想の本体にした（Animator: `TickAnimationGroup`＝UpdateAnimation、`TickPoseFinalizeGroup`＝評価の後にイベントを配る。SkinnedMesh: `AdvanceAnimationTime`、`TickPoseFinalizeGroup`＝外部駆動でなければ EvaluatePose）。イベントの配信は今と同じく PoseFinalize の本体の最後（GameThread）。
  - 登録に群ごとの本体 `ComponentTickRegistration::GroupRuns`（null の群は Run）と `BodyFor`・`HasBody` を足し、World の収集は群ごとに `BodyFor(group)` を呼び先の本体にする。
  - World の初期化で Animator・SkinnedMesh を登録（主群によらず Animation・PoseFinalize の群、channel Animation、書く EcsSkinnedRender）。群・mask・優先度・深さ優先の順は Component の設定のまま。
  - Tick・OnTickGroup を直接呼ぶテストは無かった（テストは World の Tick 経由）。
- 結果（`.harness/runs/20261011-144300/`）:
  - 基準: 最初の編集の前に `check-SCN-ECS-TICK-ANIM-base.txt`（同じ引数・比較なし）で `SCN-ECS-TICK-ANIM-base` を撮った。`ECS_TICK_DUP pass=3 fail=0`、result=pass、EXIT=0。
  - `verify-SCN-ECS-TICK-ANIM-1.txt`: Debug の SkeletalAnimationSamplingTest・SkinnedRenderPathContractTest・EntityTreeTest のビルド EXIT=0（警告は触っていないファイルの既存のものだけ）。
  - `-2.txt`: WorldTimeChannelTest・SkeletalAnimationSamplingTest・SkinnedRenderPathContractTest の 3/3 が pass、EXIT=0。
  - `-3.txt`: `-SkinnedProbe` 付きの決定的な撮影が `ECS_TICK_DUP pass=3 fail=0`、基準との比較は平均輝度差 0・PSNR 100 dB・不一致画素 8E-06（最大差 1）で `result=pass`、EXIT=0。Game.log は `STARTUP_SKINNED_PROBE bodies=2`・`VIS_RASTER skinned_chunks=4 dropped_chunks=0`・ERROR 0。PNG を開いて確認（天球・地面・球・岩・家と骨付きのパネル 2 枚）。Game.exe は World.cpp の編集より後。
  - `check-SCN-ECS-TICK-ANIM-related.txt`: WorldTickGroupTest・EntityTreeTest・WorldTimeChannelTest の 3/3 が pass（verify の外の確認。冒頭のビルドは対象名が EntityTreeTest の中の 1 本なので MSBuild が見つけられず失敗、テストは verify 1 で作った EntityTreeTest で走った）。
- Next: TASKS.md の次の todo（SCN-ECS-TICK-PHYSICS ほか）。
- Notes:
  - 登録は World.cpp の RegisterCoreComponentTicks（paths の外）に置いた。群ごとの本体を呼び先へ渡す 2 行の変更も World.cpp。他の Core の型の登録と同じ場所で、Component の側だけでは群を区別できないため。
  - 群ごとの本体（GroupRuns）の単体テストは足していない（計画に無い。撮影と既存の Animator のテストで確かめた）。

### SCN-ECS-TICK-PHYSICS（2026-10-11、反復 1・run 20261011-211043）

- 変更:
  - CharacterBody: 仮想の OnTickGroup・Tick・FixedTick を消し、非仮想の本体にした（`TickMovementGroup`・`TickPoseFinalizeGroup`＝可変の要求、`TickFixedStep`＝固定の要求。中身は旧と同じ）。Initialize で物理モジュールの一覧へ入り、Finalize で外れる（RigidBody と同じく、モジュールの登録の後に作られたものが対象）。
  - 物理モジュールの登録（Install で `RegisterWorldSystems`）: CharacterBody の可変の処理を `World::RegisterComponentTick` で Movement・PoseFinalize の群に登録（主群によらない。読む EcsWorldTransform・書く EcsLocalTransform）。固定ステップの段に GameThreadOnly のシステムを 3 つ登録: 段 FixedBegin の `Physics.FixedPre`（旧 PreFixedTick）、段 FixedTick の `Physics.CharacterFixed`（CharacterBody の固定の要求。旧の配信と同じ条件）→ `Physics.FixedStep`（旧 FixedTick）。World のシステムより後に登録するので、段の中では World の処理（姿勢の確定・補間の準備、Component の FixedTick）の後に走る。同じ World へ入れ直したときは名前で見つけて登録しない。呼び先は毎回モジュールの登録から引き、その World に束ねられた初期化済みのモジュールだけを呼ぶ（スケジューラにシステムを外す口が無いため）。
  - PhysicsModule の PreFixedTick・FixedTick の上書きを消し、`IPhysicsModule::StepPreFixed`・`StepFixed`（1 歩を直接進める口。段のシステムとテストが呼ぶ）にした。モジュールの DispatchPreFixedTick・DispatchFixedTick は物理以外のモジュールのために残る（設計 1.8）。
  - スケジューラ: `SetStageDefaultDelta`（RunStage に delta が渡されない段の全 channel に使う値）。ApplicationProcessor の固定ステップが FixedBegin・FixedTick・FixedEnd に 1 歩の時間を入れ、抜けるときに 0 に戻す（World は段に delta を渡さないため）。
  - テスト: 物理のテストの `PreFixedTick`/`FixedTick` の直接の呼び出しを `StepPreFixed`/`StepFixed` に、CharacterBody の `FixedTick` を `TickFixedStep` に直した。
- 結果（`.harness/runs/20261011-211043/`）:
  - `verify-SCN-ECS-TICK-PHYSICS-1.txt`: RelWithDebInfo の Game のビルド EXIT=0（EcsScheduler・ApplicationProcessor・物理モジュールを再コンパイル）。
  - `-2.txt`: Debug の PhysicsDynamicsTest・ApplicationFixedStepPipelineTest のビルド EXIT=0（エラー・警告はリンクの PDB の既存のものだけ）。
  - `-3.txt`: ApplicationFixedStepPipelineTest・PhysicsLifecycleEventTest・PhysicsFixedStepPipelineTest（pre → Component → 物理 → 後のモジュールの順、Game の QuadrupedLocomotion を含む）・PhysicsCharacterMoveTest・CharacterPlatformMotionTest の 5/5 が pass、EXIT=0。
  - `check-SCN-ECS-TICK-PHYSICS-related.txt`（verify の外）: EcsSchedulerTest・WorldTickGroupTest・PhysicsModuleLinkTest・PhysicsDynamicsTest・CharacterDriveOptionsTest は pass。PhysicsBroadphaseQueryTest は Debug で 2 個目の fixture の `InstallAll` の assert で止まる（同じプロセスで ShutdownAll の後の InstallAll は ModuleRegistry の冪等ガードで false。ModuleRegistry は触っていない既存の挙動で、assert が消える Release でだけ通る）。
- Next: TASKS.md の次の todo（SCN-ECS-TICK-SCRIPT ほか）。
- Notes:
  - CharacterBody 同士の固定の要求（BeforeSimulation）の順は、旧の深さ優先から作成の順になった（Game のキャラクターは 1 体。テストの複数体のケースは pass）。固定の要求を出す Component の FixedTick は Core と Game には CharacterBody のほかに無く、他の Component との前後は変わらない。
  - 固定の要求の中で BeforeSimulation が Component を消すと、旧は配信中なので破棄が遅れたが、今は段のシステムなので即時に消える（RequestSimulation は消えた後を引き直すので、旧の可変の経路と同じ扱い）。
  - 段 FixedTick は World が初期化されていないと走らない（World::DispatchFixedTick の条件）。旧はモジュールの FixedTick だけはその場合も呼ばれていた。
  - PhysicsBroadphaseQueryTest の Debug の失敗（上）は直していない（paths の内側のテストだが、このタスクの範囲の外）。
  - スケジューラの既定の delta と物理の段のシステムの単体テストは足していない（計画に無い。固定ステップの経路のテストで確かめた）。

### SCN-ECS-TICK-PHYSICS 差し戻しの対応（2026-10-11、反復 2・run 20261011-211043）

- 変更:
  - 物理モジュールの CharacterBody の一覧に参加の番号を持たせた（登録したときの `GetNextCollectionSerial`。スケジューラの順序付きの呼び先と同じ規則）。段 FixedTick の `Physics.CharacterFixed` は `context.CollectionSerial` より後に参加したものを飛ばす。Tick の後に足した CharacterBody は、同じフレームの固定ステップ（同じ収集で番号を共有）では要求を出さず、次の Tick の収集から加わる（旧の Tick の先頭の一覧と同じ）。収集の外で走る固定ステップは段が自分で収集を開くので、その前に足したものは加わる（旧の単独の固定ステップが一覧をその場で作るのと同じ）。
  - テスト: PhysicsFixedStepPipelineTest にケース 19（Tick の後に足した CharacterBody が同じフレームの 2 歩で BeforeSimulation を呼ばず、次の Tick で呼ばれる。収集の外で足したものは次の歩で呼ばれる）を足した。差し戻しの再現手順そのもの。
- 結果（`.harness/runs/20261011-211043/`）:
  - `verify-SCN-ECS-TICK-PHYSICS-4.txt`: RelWithDebInfo の Game のビルド EXIT=0。
  - `-5.txt`: Debug の PhysicsDynamicsTest・ApplicationFixedStepPipelineTest のビルド EXIT=0。
  - `-6.txt`: ApplicationFixedStepPipelineTest・PhysicsLifecycleEventTest・PhysicsFixedStepPipelineTest・PhysicsCharacterMoveTest・CharacterPlatformMotionTest の 5/5 が pass、EXIT=0。
  - 判定を一時的に外した版では PhysicsFixedStepPipelineTest が Failed になることを確かめ、元に戻して再ビルドした（verify の外。保存なし）。
- Next: TASKS.md の次の todo（SCN-ECS-TICK-SCRIPT ほか）。
- Notes:
  - 参加の番号は CharacterBody の Owner の World のスケジューラから取る（Owner が無ければ束ねた World）。別の World の CharacterBody は要求の側で World の一致を見て飛ばすので、番号の出所の違いは影響しない。

### SCN-ECS-TICK-PHYSICS 再検証の失敗の対応（2026-10-11、反復 3・run 20261011-211043）

- 原因: 再検証で落ちたのは PhysicsFixedStepPipelineTest の case 19（Tick の後に足した CharacterBody）だけで、同じフレームの固定ステップで要求が 2 回出ていた。コードの誤りではなく古いオブジェクト: 反復 2 で判定を一時的に外して失敗を確かめたあと、元に戻したソースの更新時刻が外した版のオブジェクトより古く、Debug の PhysicsModuleCharacter.obj が作り直されないまま判定なしの版がリンクされていた（反復 2 の verify の 5/5 はこの確認の前の実行）。
- 対応: コードの変更はない。ソースを触って Debug と RelWithDebInfo の両方で PhysicsModuleCharacter.cpp を再コンパイルさせ、case 19 が参加の番号どおり（Tick の後の追加は同じ収集の 2 歩で 0 回、次の収集で 1 回、収集の外の追加は次の歩から）に通ることを確かめた。
- 検証（`.harness/runs/20261011-211043/`）:
  - `verify-SCN-ECS-TICK-PHYSICS-7.txt`: RelWithDebInfo の Game のビルド EXIT=0（PhysicsModuleCharacter.cpp を再コンパイル）。
  - `-8.txt`: Debug の PhysicsDynamicsTest・ApplicationFixedStepPipelineTest のビルド EXIT=0（同じく再コンパイル）。
  - `-9.txt`: ApplicationFixedStepPipelineTest・PhysicsLifecycleEventTest・PhysicsFixedStepPipelineTest・PhysicsCharacterMoveTest・CharacterPlatformMotionTest の 5/5 が pass、EXIT=0。
- Next: TASKS.md の次の todo（SCN-ECS-TICK-SCRIPT ほか）。
- Notes:
  - 判定を外して失敗を確かめる類の一時的な書き換えは、git checkout で戻して更新時刻を新しくし、戻した後に対象の verify を走らせ直してから保存する。

### SCN-ECS-TICK-SCRIPT（2026-10-11、反復 4・run 20261011-211043）

- 変更:
  - ScriptComponent: 仮想の Tick の上書きを消し、非仮想の本体 `TickScript`（中身は旧の Tick と同じ）と、World へ登録する `RegisterTick(World&)` を足した。登録は主群だけで呼ぶ（`bPrimaryGroupOnly`。旧の OnTickGroup→Tick と同じ）、channel は World、読む EcsWorldTransform・書く EcsLocalTransform。群・mask・優先度・深さ優先の順は Component の設定のまま（順序付きの系の呼び先として旧の一覧と同じ比較で合流）。
  - ScriptRuntime::Initialize が成功の直前に、束ねる World へ `ScriptComponent::RegisterTick` を呼ぶ。束ねられるのは Owner がその World の ScriptComponent だけなので、他の World では登録が無くても呼ぶものが無い（基底の空の Tick）。
  - テスト: ScriptComponentTickTest の `staleComponent->Tick(1.0f)` を `TickScript(1.0f)` に直した。
- 結果（`.harness/runs/20261011-211043/`）:
  - `verify-SCN-ECS-TICK-SCRIPT-1.txt`: Debug の ScriptComponentTickTest・ScriptApplicationLifecycleContractTest・ScriptHotReloadTest のビルド EXIT=0（ScriptComponent.cpp・ScriptRuntime.cpp を再コンパイル。警告はリンクの PDB の既存のものだけ）。
  - `-2.txt`: ScriptApplicationLifecycleContractTest・ScriptComponentTickTest・ScriptComponentSceneRoundTripTest・ScriptHotReloadTest・M6AngelScriptAcceptanceTest の 5/5 が pass、EXIT=0。ScriptComponent に仮想の Tick が無くなったので、移動・無効化・非 Active・ホットリロードの検査は登録した本体だけで通っている。
- Next: TASKS.md の次の todo（SCN-ECS-TICK-GAME ほか）。
- Notes:
  - スクリプトの束縛（EntityRef）には Entity・Component を生成・破棄する口が無い。スクリプトの Tick は順序付きの系（スケジューラの段の最中）で走るので、そこから C++ 経由で起きた生成は World の既存の経路（`BindEntitySubtreeRows`・`ReserveComponentRowCreate`）で Object が即時、行が段の終わりの同期点で作られ、破棄の行は `m_DeferredEntityRows` で同期点に回る。この経路のスクリプト専用のテストは足していない（計画に無い）。
  - 登録の場所は World.cpp の RegisterCoreComponentTicks（paths の外）ではなく、物理モジュールと同じく束ねる側（ScriptRuntime）にした。
  - verify のビルドは Git Bash から走らせると `/m:1` が `m:1` に書き換えられて MSBuild が失敗する。PowerShell から走らせる。

### SCN-ECS-TICK-GAME（2026-10-11、反復 6・run 20261011-211043、BLOCKED）

- 変更:
  - FollowCamera: 仮想の OnTickGroup を消し、群ごとの非仮想の本体 `TickInputGroup`（ReadLook）・`TickCameraGroup`（Camera 群の回数を数えて UpdateCamera）と、World へ登録する `RegisterTick(World&)` を足した。Input・Camera の群で呼ぶ（主群によらない）、channel は Unscaled、読む EcsWorldTransform・書く EcsLocalTransform。SpringArm の登録は型の完全一致なので、旧と同じく FollowCamera には SpringArm の本体は呼ばれない。
  - QuadrupedLocomotion: 同じく `TickInputGroup`（CollectInput）・`TickPoseFinalizeGroup`（CaptureRootMotion）・`TickPostPhysicsGroup`（FinishRootMotionFrame→PublishVisual）と `RegisterTick(World&)`。Input・PoseFinalize・PostPhysics の群、channel は Unscaled。TryGetComponentTickDelta と GetTickSerial の使い方は変えていない。
  - GameApplicationHandler::OnInitialize で `GEngine->GetWorld()` へ 2 つを登録する（シーンが作るより前）。
  - paths の外: PhysicsFixedStepPipelineTest の fixture で同じ登録をし、消した `OnTickGroup` の直接の呼び出しを `TickInputGroup` に替えた（3 行）。
- 結果（`.harness/runs/20261011-211043/`）:
  - `verify-SCN-ECS-TICK-GAME-1.txt`: RelWithDebInfo の Game のビルド EXIT=0（3 ファイルを再コンパイル）。
  - `-2.txt`: Debug の LoggerSinkTest のビルドが EXIT=1。束ねた別のテスト 5 本（Test/Core/Input の 4 本と JsonWriterTest）が今の Core に追随しておらずコンパイルできない（このタスクの変更と無関係。実行ファイルは 2026-09-27 のまま）。
  - `-3.txt`: 古い実行ファイルに 3 本が無く「unknown test」で Failed。
  - `check-SCN-ECS-TICK-GAME-physics-pipeline.txt`（verify の外）: PhysicsDynamicsTest のバンドルを再ビルドし（FollowCamera・QuadrupedLocomotion・PhysicsFixedStepPipelineTest を再コンパイル）、PhysicsFixedStepPipelineTest・PhysicsLifecycleEventTest・PhysicsCharacterMoveTest・CharacterPlatformMotionTest が 4/4 pass。
- Next: `blocked/SCN-ECS-TICK-GAME.md` の判断（LoggerSinkTest のバンドルを別の項目で直すか、paths を広げるか、verify を差し替えるか）。
- Notes:
  - LoggerSinkTest・PhysicsDynamicsTest はテストを束ねた実行ファイルで、ctest の名前の対象（PhysicsFixedStepPipelineTest など）はビルドの対象として存在しない。ビルドはバンドル名で行う。

### SCN-ECS-TICK-SCRIPT（2026-10-11、反復 7・run 20261011-211043、差し戻しへの対応）

- 前提: 親が「破棄は即時」の意味を、破棄予約が即時に立つこと（解放は同期点）と決め、done-when を直した。Tick の登録（反復 4）はそのまま。
- 変更:
  - ScriptRuntime の EntityRef の解決（スクリプトからの取得）が、ScriptComponent 自身の破棄予約と、Owner の祖先の破棄予約も見て失敗するようにした。旧は Owner 自身の予約だけを見ていたので、Tick の最中に `RemoveComponent` された ScriptComponent や、破棄予約が立った Entity の子は、同期点まで取得できていた。
  - テスト: ScriptComponentTickTest に `TestDestroyDuringTickReservesUntilSyncPoint` を足した。スクリプトの Tick より前（優先度 -100）の Component が、Tick の最中に Entity（子を持つ）を `RemoveEntity` し、別の Entity の ScriptComponent を `RemoveComponent` し、Entity を生成する。呼んだ直後に `IsPendingDestroy` が真で Object はまだ所有者に残り、生成した Entity は Object があって行がまだ無いことを確かめる。後（優先度 100）の Component が、3 つのスクリプトが同じフレームで呼ばれていない（位置が動いていない）ことと、取得が失敗する（Mover の `GetPosition` が例外になる）ことを確かめる。Tick の後に束縛が解かれ、行が消え、生成した Entity の行ができていることを確かめる。
- 結果（`.harness/runs/20261011-211043/`）:
  - `verify-SCN-ECS-TICK-SCRIPT-3.txt`: Debug の ScriptComponentTickTest・ScriptApplicationLifecycleContractTest・ScriptHotReloadTest のビルド EXIT=0。
  - `-4.txt`: 5/5 が pass、EXIT=0。
  - 取得の修正を一時的に外すと、追加したテストが取得の検査（LastResult が ExecutionFailed）で落ちることを確かめた（git の差分を戻して再ビルドし、上の verify はその後に走らせた）。
- Next: TASKS.md の次の todo。
- Notes:
  - 「取得」は、スクリプトの束縛にある唯一の取得の口（EntityRef の解決。IsValid・GetPosition などが通る）とした。C++ の `Entity::GetComponent` は破棄予約の Component も返す（paths の外。束縛の側で予約を見て外している）。

### SCN-FIX-LOGGERSINK-BUNDLE（2026-10-11、反復 8・run 20261011-211043）

- 変更（テスト側の追随だけ。確かめる中身は変えていない）:
  - InputRebindCaptureManagerTest・InputDebugOverlayControllerTest: `ECursorMode` が `NorvesLib` 直下へ移ったので `using NorvesLib::ECursorMode;` を足した。
  - InputBindingJsonTest: `assert(...)` の中の raw 文字列（文字列中の括弧の検査）を変数 `bracketsInString` へ出した。
  - ActiveDeviceKindIntegrationTest: 偽物の ConsumingUI に、IInputController に増えた純粋仮想 `DebugName()` を実装した。
  - JsonWriterTest: サロゲートの `\u` を raw 文字列に書いていた所を、逆斜線をエスケープした通常の文字列にした（解析させる JSON の文字は同じ）。
- 結果（`.harness/runs/20261011-211043/`）:
  - `verify-SCN-FIX-LOGGERSINK-BUNDLE-1.txt`: Debug の LoggerSinkTest のビルド EXIT=0（実行ファイルが 2026-09-27 以来はじめて更新された）。
  - `-2.txt`: LoggerSinkTest・QuadrupedLocomotionModelTest・LocomotionProfileReloadTest・CameraCollisionSolverTest の 4/4 が pass、EXIT=0。
  - `-3-allmembers.txt`（verify の外）: LoggerSinkTest.exe を使う ctest の全 50 本が pass、EXIT=0。直した 5 本も含む。落ちる member は無く、製品のコードの不具合として足す項目は無い。
- Next: SCN-ECS-TICK-GAME（verify の 2・3 本目がこの束を使う）。
- Notes:
  - JSON のサロゲートの扱いそのもの（CORE-JSON-SURROGATE）には触れていない。書き方を変えただけ。

### SCN-ECS-FIX-SCRIPT-TICK-COUNT（2026-10-11、反復 9・run 20261011-211043）

- 変更（テストだけ。製品のコードは変えていない）:
  - ScriptComponentTickTest の `TestDestroyDuringTickReservesUntilSyncPoint` で、ScriptRuntime が World に登録した ScriptComponent の Tick の呼び先を `FindComponentTick` で取り出し、対象ごとの呼び出し回数を数えてから元の本体を呼ぶ包みに `RegisterComponentTick` で置き換えた。
  - 後の Tick（優先度 100）で、破棄予約した 3 つの対象（破棄した Entity・その子・`RemoveComponent` した ScriptComponent）の回数が 0 であることを、直接の `TickScript` より前に確かめる。破棄していない対象が同じフレームでちょうど 1 回（次のフレームで 2 回）呼ばれたことで、計数が効いていることも確かめる。位置と取得の失敗の検査はその後に残した。
- 結果（`.harness/runs/20261011-211043/`）:
  - `verify-SCN-ECS-FIX-SCRIPT-TICK-COUNT-3.txt`: Debug の 3 つのビルド EXIT=0。
  - `-4.txt`: 5/5 が pass、EXIT=0。
  - `-1.txt`・`-2.txt` は無効（下の変異の確認で戻した World.cpp の時刻が古く再コンパイルされず、変異のまま走った）。
  - 変異の確認: `World::ExecuteOrderedTick` の `target->IsPendingDestroy()` の検査を一時的に外すと、追加した回数の検査（`CountOf(script) == 0`）で落ちた。旧の位置の検査はこの変異では落ちない（取得が失敗して位置が動かないため）。
- Next: TASKS.md の次の todo。SCN-ECS-TICK-SCRIPT も done にした。
- Notes:
  - ScriptApplicationLifecycleContractTest は 1 本で約 245 秒かかる（ctest の `--timeout 180` より長いが pass する。テスト側の TIMEOUT の設定が優先されているとみられる）。
  - Git Bash から `cmake --build ... -- /m:1` を呼ぶと `/m:1` がパスに書き換わり MSBuild が失敗する。ビルドは PowerShell から呼ぶ。

### SCN-ECS-TICK-GAME（2026-10-11、反復 10・run 20261011-211043）

- 変更: 無し（実装は反復 6 のコミット 7031680 のまま）。前提の SCN-FIX-LOGGERSINK-BUNDLE で束がビルドできるようになったので、verify を通し直した。
  - Game の FollowCamera・QuadrupedLocomotion には OnTickGroup の override が残っていない。GameApplicationHandler::OnInitialize が両者の RegisterTick で World へ登録する。QuadrupedLocomotion の TryGetComponentTickDelta・GetTickSerial の使い方は保っている。
- 結果（`.harness/runs/20261011-211043/`）:
  - `verify-SCN-ECS-TICK-GAME-4.txt`: RelWithDebInfo の Game のビルド EXIT=0。
  - `-5.txt`: Debug の LoggerSinkTest のビルド EXIT=0。
  - `-6.txt`: QuadrupedLocomotionModelTest・LocomotionProfileReloadTest・CameraCollisionSolverTest の 3/3 が pass、EXIT=0。
  - 起動画面の比較は、2026-10-11 の検証の軽量化に従い verify に無いので回していない。
- Next: TASKS.md の次の todo。

### SCN-ECS-FRAME-HOOKS（2026-10-11、反復 11・run 20261011-211043）

- 変更:
  - ApplicationProcessor にフレームの段のスケジューラ（`Ecs::SystemScheduler`。行を持たない専用の `EcsWorld` に付ける）を足し、初めて使うときに作る。段は `PreUpdate`・`PreSimulation`・`PostTick`・`FixedModulePre`・`FixedModulePost`・`PostLateTick`・`PostUpdate`（1 フレームの中で走る順）。各段の GameThreadOnly のシステムは持ち主の関数を呼ぶだけで、delta は RunStage の channel 0 で渡す（値は従来と同じ float）。
  - 置き換えた呼び出し（順は従来のまま）: `PreUpdate.HandlerUpdate`（骨格の資産の反映と handler の OnUpdate。失敗したら終了を求め、このフレームの残りを走らせない）→ `PreUpdate.ScriptBegin`、`PreSimulation.GameModeStateMachine`、`PostTick.Particles`、`FixedModulePre.PreFixedTick`、`FixedModulePost.FixedTick`、`PostLateTick.ModuleLateTick` → `PostLateTick.HandlerLateUpdate`、`PostUpdate.ScriptEnd` → `PostUpdate.ModuleTick`。区間は `Ecs.Stage.<段>`・`Ecs.Sys.<系>` で記録される。
- 1 フレームの順と、シミュレーションが止まっているとき（`ShouldAdvanceSimulation` が偽）に走るか:

| 順 | 段・処理 | 呼び出し | 停止中 |
|---|---|---|---|
| 1 | 段 PreUpdate | 骨格の資産の反映 → handler OnUpdate → スクリプトの前処理 | 走る |
| 2 | （段の外） | 入力の焦点の反映・進行の判定・振動の一時停止 | 走る |
| 3 | 段 PreSimulation | GameMode の状態機械 | 走らない |
| 4 | World の段 Tick | 群 Input〜PoseFinalize | 走らない |
| 5 | 段 PostTick | 粒子の Tick | 走らない |
| 6 | 固定ステップ × 歩数 | World FixedBegin → 段 FixedModulePre（モジュール PreFixedTick）→ World FixedTick → 段 FixedModulePost（モジュール FixedTick）→ World FixedEnd | 走らない（Paused で 0 歩） |
| 7 | （段の外） | 描画の補間の alpha | 走る |
| 8 | World の段 LateTick → 段 PostLateTick | 群 PostPhysics〜PreRender → モジュール LateTick → handler OnLateUpdate | 走らない |
| 9 | （段の外） | 振動の更新 | 走る |
| 10 | World の段 Sync ほか | SyncToSceneView・一時の板・粒子の板・SceneQuery | 走る |
| 11 | 段 PostUpdate | スクリプトの後処理 → モジュール TickAll | 走る |

- 結果（`.harness/runs/20261011-211043/`）:
  - `verify-SCN-ECS-FRAME-HOOKS-2.txt`: Debug の ApplicationFixedStepPipelineTest・GameModeStateMachineTest・LoggerSinkTest・CanvasTransientBoardSnapshotTest のビルド EXIT=0（`-1.txt` は定義の引数の型の直し忘れでコンパイルエラー。無効）。
  - `-3.txt`: ApplicationFixedStepPipelineTest・ParticleFrameSnapshotTest・ModuleRegistryTest・GameModeRegistryTest・GameModeStateMachineTest・SubRoutineTest の 6/6 が pass、EXIT=0（モジュールの Pre/Fixed/Late と handler の OnLateUpdate の順、停止中に Late の段が走らないことを含む）。
  - `-4.txt`: 起動画面 default を RelWithDebInfo で撮り、SCN-MEGA-BASE-RWDI との比較は平均輝度差 0・PSNR 100 dB・不一致画素 7E-06（最大差 1）で `result=pass`、EXIT=0。PNG を開いて確認（天球・地面・球・岩・家）。Game.log の ERROR は 0。
- Next: SCN-ECS-TICK-REMOVE。
- Notes:
  - フレームの段は World のスケジューラに載せず別のスケジューラにした。handler の OnUpdate の中のシーンの切り替えが World のスケジューラへ系を登録する（散布の `EnsureMassWorkSystem`・物理の段の系）ので、World の段の最中（登録を拒む）に OnUpdate を呼ぶと登録が落ちるため。World の収集の番号も増やさない。専用の EcsWorld はチャンクの pool の最初の 1 枚（約 256 KB）を持つ。
  - 段を走らせる順は呼び手（ApplicationProcessor）が決める。World の段（Tick・Fixed*・LateTick・Sync）との前後は上の表のとおりで、設計 1.7 の PreUpdate/PostUpdate を実際の順に合わせて 7 段に分けた。


### SCN-ECS-TICK-REMOVE（2026-10-11、反復 12〜13・run 20261011-211043）

- 変更:
  - Component の仮想（Tick・GetTimeChannel・OnTickGroup・FixedTick）と主群の受け渡し（`m_DispatchPrimaryGroup`）、`Entity::Tick`・`TickComponents`、`TickDispatch.h`（と TickDispatchTest）、`BuildTickSnapshot`・旧の一覧（`m_TickEntries`・`m_FixedTickEntries`・`m_bHasTickSnapshot`）、旧の配信のシステム（`FixedTick.Dispatch`・`DispatchFixedTickBody`・`TickEntityRecursive`・`FixedTickEntityRecursive`・`TryResolveTickDelta`）を消した。段 Tick/LateTick の呼び手は順序付きの系の順の一覧だけを呼ぶ。
  - 順の比較は `TickOrderLess`（`Component/TickGroup.h`）に移し、スケジューラの順の一覧もこれで並べる。
  - 呼び先の付け直し（`SyncOrderedTicks`）は毎フレームではなく、`m_bOrderedTicksDirty` が立ったときだけ収集の先頭で行う。立てるのは、部分木の追加（`BindEntitySubtreeRows`）・Component の追加（`BindComponentRow`）・親の付け替え（`RelinkEntityRow`）・型の登録（`RegisterComponentTick`）・Tick の設定の変更（`Component::StoreTickConfiguration`。値は BIND-FLAGS の列に書く形のまま）・スケジューラの作り直し。外れるものは従来どおり `InvalidateTickComponent`/`InvalidateTickEntitySubtree` で即時に外し、呼ぶ直前の検査（有効・Tick 有効・破棄予約）も残る。
  - channel は型の登録の `TimeChannel` だけになった（`World::GetComponentTimeChannel`）。Camera・SpringArm・FollowCamera・QuadrupedLocomotion は Unscaled、Animator・SkinnedMesh は Animation を登録に持つ。個体ごとに channel を変えていたテストは、channel の違う派生の型を登録する形に直した（AnimGraphRuntimeTest の Socket）。
  - 検査 ECS_TICK_DUP は「同じ収集で同じ呼び先と群を 2 回」だけを見る（仮想の経路が無くなったため）。収集の番号の増分と検査の数え直しは収集の先頭（Tick）へ、Tick の外の単独の固定ステップの番号は `DispatchFixedTick` へ移した。
  - 検証シーンの FixedStepSentinel は、仮想の FixedTick の代わりに段 FixedTick のシステム（`RenderingValidation.FixedStepSentinel`）から呼ぶ。
- 結果（`.harness/runs/20261011-211043/`）:
  - `verify-SCN-ECS-TICK-REMOVE-3.txt`: Debug の 4 つのビルド EXIT=0。
  - `-4.txt`: 7/7 が pass、EXIT=0。
  - `-5-neighbors.txt`（verify の外）: SkeletalAnimationSamplingTest・RenderingValidationFixtureSupport のビルド EXIT=0、SpringArmComponentTest・EcsSchedulerTest・EcsLifetimeTest・EcsHierarchyTest・SkeletalAnimationSamplingTest の 5/5 が pass。
  - `-6.txt`: 起動画面 default を RelWithDebInfo で撮り、SCN-MEGA-BASE-RWDI との比較は平均輝度差 0・PSNR 100 dB・不一致画素 7E-06（最大差 1）で `result=pass`、EXIT=0。PNG を開いて確認（天球・地面・球・岩・家）。Game.log の ERROR は 0。
  - `-1.txt`・`-2.txt` は反復 12（途中で止まり、作業は「作業途中の保存」のコミットに入った）の実行。反復 13 は追加の変更なしで通し直した。
- Next: TASKS.md の次の todo。
- Notes:
  - `TickDispatch.h` を消したので、paths の外の `Library/Core/CMakeLists.txt`（ヘッダの一覧）と `Ecs/EcsScheduler.*`（比較の参照）も合わせて直した。

### SCN-MEGA-MEASURE-CPU（2026-10-12、反復 14・run 20261011-211043。未完: FIX の後に撮り直す）

- 変更（計測の口だけ。paths に足した）:
  - 冒頭の方針の名前の区間を足した: 変換の伝播（`World::UpdateWorldTransforms`）を `Ecs.Sync.TransformPropagate`、描画の変換（`UpdateRenderTransforms` の伝播の後）を `Ecs.Sys.RenderTransform`、破棄の回収（`CleanupDestroyedObjects`）を `Ecs.Sync.Cleanup`、SceneQuery の更新を `Ecs.Sys.SceneQuery`。World の段 `Sync` を `RenderSync` に改名した（`Ecs.Stage.RenderSync`・`Ecs.Sys.RenderSync.SyncToSceneView`）。
  - 統計が有効な構成だけ、120 フレームごとに 1 フレームあたりの行の数を `ECS_TRANSFORM_COUNTS seeds= processed= changed= render_rows= sync_rows=` で出す（変わった行・伝播で処理した行（変わった行 + 影響する子孫）・補間の部分木の行・同期へ渡した行）。
- 結果（RelWithDebInfo・1920×1080・300 フレーム。`.harness/runs/scene-capture/SCN-MEGA-MEASURE-CPU/<static|move1>/metrics.json`）:

| 区間（median / p95 ms） | 5万個 静止（default） | 5万個 1% 移動（default） | 増分 | 行の増分 | 1 行あたり |
|---|---|---|---|---|---|
| 段の合計 `Ecs.Stage.*` | 13.44 / 15.08 | 15.45 / 17.91 | +2.01 | | |
| `Ecs.Stage.RenderSync` | **13.24 / 14.86** | 14.50 / 16.77 | +1.26 | sync_rows 0 → 500 | 2.5 µs |
| `Ecs.Sync.TransformPropagate` | 0.003 / 0.005 | 0.212 / 0.297 | +0.209 | processed 1 → 501 | 0.42 µs |
| `Ecs.Sys.RenderTransform` | 0.000 / 0.000 | 0.104 / 0.176 | +0.104 | changed 0 → 500 | 0.21 µs |
| `Ecs.Sys.SceneQuery` | 0.008 / 0.012 | 1.264 / 1.529 | +1.256 | 500 | 2.5 µs |
| `Ecs.Sync.Cleanup` | 0.000 / 0.000 | 0.000 / 0.000 | 0 | | |
| `Ecs.Stage.PreSimulation`（移動のロジック） | 0.022 / 0.031 | 0.403 / 0.532 | +0.38 | 500 | 0.76 µs |
| gt | 24.22 / 26.81 | 27.84 / 31.42 | | | |
| rt | 121.1 / 132.9 | 129.5 / 142.5 | | | |
| GPU | 48.3 / 56.4 | 54.8 / 61.9 | | | |

- 静止の close・aerial も同じ傾向（`Ecs.Stage.RenderSync` 13.07・13.11 ms、gt の p95 26.0・26.4 ms）。補間の部分木は mega-stress では 0 行（`render_rows=0.0`）。
- 合否:
  - 5万個の静止でゲームスレッドの段の合計 ≤ 2 ms: **不合格**（13.44 ms）。ほぼ全部が `Ecs.Stage.RenderSync`（変わった行 0 件で 13.2 ms）。差分の同期でも「Text は旧の走査」として根から全 Entity をたどっている。
  - 各区間 ≤ 0.1 ms: 変換の伝播 0.003・描画の変換 0.000・SceneQuery 0.008・cleanup 0.000 は合格、RenderSync 13.24 は不合格。
  - 1% 移動の増分が数に比例: 静止（行 0〜1）で伝播・描画の変換・SceneQuery が 0.01 ms 未満、500 行で上の 1 行あたりの値。行の数に依らない固定の重さは RenderSync の全走査だけ。
  - 並列の系: この負荷で走る系は全部 GameThreadOnly で、どの区間もスレッドは 1（ワーカーに出る系が無い）。
  - verify 1（`verify-SCN-MEGA-MEASURE-CPU-1.txt`）: 区間はすべて揃ったが、gt の p95 が 26.0〜26.8 ms > 16.6 で `result=fail`、EXIT=1。
- 1% 移動 + 64 m 区画・バナナ 1万本 / 10万本は撮っていない（RenderSync の全走査が本数に比例して残る間は、増分も gt の差もそれで決まり、直した後に撮り直すため）。Game.log の ERROR は静止・1% 移動とも 0。
- Next: SCN-MEGA-FIX-RENDERSYNC-WALK（この項目の前に足した）。それが done になったら、この項目を todo に戻して全部の負荷を撮り直す。
- Notes: gt（24 ms）のうち Ecs の区間の外が約 11 ms ある（区間が無く内訳は不明）。撮り直しのときに見る。

### SCN-MEGA-FIX-RENDERSYNC-WALK 完了（2026-10-12、反復 15・run 20261011-211043）

- 変更:
  - 既定の差分の同期（`SyncToSceneViewBody` の `!m_bLegacyRenderSync` の側）から、根からの `SyncEntityRecursive` を外した。見るのは変わった行・通知・Remove の一覧と、変換のキャッシュを求め直す Text の行だけ。
  - Text は、変更ログ（dirty）と変換の版が進んだ Entity の行から一覧に積み（`AddRenderTextRow`）、`RefreshRenderTextRows` で求め直して dirty を落とし、同期した変換の版を記録する。旧の走査は毎フレーム求め直して前フレームの変換を今に揃えていたので、揃っていない行は次の同期でもう一度求め直す。走査に入らない部分木の行は求め直さない（旧の走査と同じ。再びアクティブにすると dirty で求め直す）。
  - MegaGeometry の走査（変わっていない行の照合）は外した。照合は `VerifyRenderSyncDelta` が検査のフレームで全行に対して行う。
  - 数を返す口 `World::GetLastRenderSyncVisitedRows()`: 既定の同期は公開した行・Remove・求め直した Text の行、旧の全走査はたどった Entity を数える。
  - 旧の全走査と検査 `ECS_RENDER_SYNC`・`ECS_RENDER_INVALIDATION` の照合はそのまま。
- 検証（`.harness/runs/20261011-211043/`）:
  - `verify-SCN-MEGA-FIX-RENDERSYNC-WALK-1.txt`: Debug の `WorldSyncDifferentialTest` のビルド EXIT=0。
  - `verify-SCN-MEGA-FIX-RENDERSYNC-WALK-2.txt`: ctest 1/1 Passed、EXIT=0。足した `TestDeltaSyncWalksNoRowsWhenStatic` が、静止のフレームでたどった行 0、Text の変更で 1、Text の追加（動かした Entity に後から足す）・親の移動・非アクティブの間・破棄の各場面でキャッシュが旧の走査と同じ値になること、旧の全走査では静止でも Entity をたどることを確かめる（検査 2 つを強制して違反 0）。Debug は NDEBUG なし。
  - `verify-SCN-MEGA-FIX-RENDERSYNC-WALK-3.txt`: 起動画面 default を RelWithDebInfo で `ECS_RENDER_SYNC`・`ECS_RENDER_INVALIDATION` 付きで撮り、どちらも pass=3 fail=0。SCN-MEGA-BASE-RWDI との比較は平均輝度差 0・PSNR 100 dB・不一致画素 5E-06（最大差 1）で `result=pass`、EXIT=0。PNG を開いて確認（天球・地面・球・岩・家）。Game.log の ERROR は 0。Core.lib（00:31）と Game.exe（00:33）は World.cpp の編集（00:30）より後。
  - 追加で `TextComponentBoardProxyTest` も pass（Text のグリフを集める経路）。
- Next: SCN-MEGA-MEASURE-CPU を todo に戻して全部の負荷を撮り直す（効果の数値はそこで確かめる）。
- Notes: `CollectTransientBoardProxies`（ApplicationProcessor が毎フレーム呼ぶ、Text のグリフ集め）も根から全 Entity をたどる。同期の段の外なので触っていない。撮り直しで gt の Ecs の区間の外（約 11 ms）を見るときの候補。

### SCN-MEGA-FIX-RENDERSYNC-WALK 差し戻しの対応（2026-10-12、反復 16・run 20261011-211043）

- 差し戻し: テストの Text に FontAtlas が無く、グリフの板が作られないため、追加・変更・削除の公開を確かめられていなかった。
- 変更（テストだけ）: `TestDeltaSyncWalksNoRowsWhenStatic` で、代役の RHI（`FakeDevice`）の上に既定の FontAtlas を作って Text に設定し、`CollectTransientBoardProxies` の結果を各場面で照合する。
  - 追加: 最初の同期の後に "A" のグリフ 1 枚、揃った後に Entity の位置 1 から並べた位置・寸法・テクスチャと一致。後から足した Text（"E"）も最初の同期でグリフ 1 枚、揃った後に位置 3 で一致。
  - 変更: "BC D" に変えた同期でたどった行 1、グリフ 3 枚（空白は送りだけ）が位置・寸法とも一致。
  - 親の移動: 今の変換は位置 5、前フレームの変換は 1 のまま、次の同期で 5 に揃う。非アクティブの間はグリフが出ず、再びアクティブにすると位置 7 で出る。
  - 旧の全走査との比較: 同じ状態で旧の全走査に切り替えて同期し、集めたグリフの板（Component・今と前フレームの位置・寸法）が既定の同期と一致。
  - 削除: 破棄の前に 2 つの Text のグリフがあり、回収後の同期で何も出ない。
  - 検査 2 つを強制して違反 0。Atlas を作れない構成（`NORVES_ENABLE_CORE_TEXT` が無効）では、グリフが出ないことだけを確かめる。
- 検証（`.harness/runs/20261011-211043/`）:
  - `verify-SCN-MEGA-FIX-RENDERSYNC-WALK-6.txt`: Debug の `WorldSyncDifferentialTest` のビルド EXIT=0（4 は Git Bash で `/m:1` が壊れて取り直し、4 の PowerShell 版も EXIT=0）。
  - `verify-SCN-MEGA-FIX-RENDERSYNC-WALK-7.txt`: ctest 1/1 Passed、EXIT=0。5 は前フレームの変換の期待を誤ってグリフごとにずらしていたための失敗（`BuildGlyphBoardProxies` は今の変換だけをずらす）。この失敗で Atlas が作られ照合が効いていることも確かめた。
  - `verify-SCN-MEGA-FIX-RENDERSYNC-WALK-8.txt`: 起動画面 default を RelWithDebInfo で検査 2 つ付きで撮り、どちらも pass=3 fail=0。SCN-MEGA-BASE-RWDI との比較は平均輝度差 0・PSNR 100 dB・不一致画素 3E-06（最大差 1）で `result=pass`、EXIT=0。PNG を開いて確認（天球・地面・球・岩・家）。Game.log の ERROR は 0。
- Next: SCN-MEGA-MEASURE-CPU を todo に戻して全部の負荷を撮り直す。
- Notes: グリフの板の前フレームの変換は、グリフごとの位置ずらしを含まない（Entity の前フレームの変換のまま）。速度のベクトルを使う段がグリフを扱うなら、ずれが出る。この項目では触っていない。
### SCN-MEGA-MEASURE-CPU 完了（2026-10-12、反復 17〜18・run 20261011-211043）

- 変更（計測の口だけ）: ゲームスレッドの Ecs の区間の外に区間を足した（`ApplicationProcessor.cpp`）。`GameThread.Input`・`GameThread.Simulation`（段を含む Tick 全体）・`GameThread.TransientBoards`（Text のグリフと粒子の板集め）・`GameThread.SceneQueryCheck`・`GameThread.RenderWorld.BeginFrame/Render/EndFrame`。
- 撮影（反復 17。RelWithDebInfo・1920×1080・300 フレーム。`.harness/runs/scene-capture/SCN-MEGA-MEASURE-CPU/<負荷>/metrics.json`。Game.exe（00:51）は区間を足した `ApplicationProcessor.cpp`（00:50）・`World.cpp`（00:30）より後で、今の HEAD のコードの値）:
  - 5万個の静止（default・close・aerial）、5万個の 1% 移動（default）、1% 移動 + 64 m 区画は 3万個（default）、バナナ 1万本 overview・10万本 horizon。
  - 1% 移動 + 64 m 区画の 5万個は、描画スレッドのバッファの確保の失敗で落ちた（VRAM の使用 15428 MB > 予算 12350 MB。親の注記どおり BVH-INDIRECT の前の既知の限界）。3万個で撮った。5万個の値は MEASURE-CULL で足す。

| 区間（median / p95 ms） | 5万個 静止 | 5万個 1% 移動 | 3万個 1% 移動 + 64 m 区画 | バナナ 1万本 overview | バナナ 10万本 horizon |
|---|---|---|---|---|---|
| 変わった行 / 伝播で処理した行（`ECS_TRANSFORM_COUNTS`） | 0 / 1 | 500 / 501 | 297 / 299 | 0 / 1 | 0 / 1 |
| 段の合計 `Ecs.Stage.*` | **0.180 / 0.258** | 2.054 / 2.577 | 1.083 / 1.376 | 0.096 / 0.164 | 0.098 / 0.201 |
| `Ecs.Sync.TransformPropagate` | 0.003 / 0.005 | 0.210 / 0.301 | 0.106 / 0.149 | 0.000 / 0.002 | 0.000 / 0.002 |
| `Ecs.Sys.RenderTransform` | 0.000 / 0.001 | 0.091 / 0.165 | 0.043 / 0.075 | 0.000 / 0.000 | 0.000 / 0.000 |
| `Ecs.Stage.RenderSync` | 0.017 / 0.025 | 1.175 / 1.500 | 0.592 / 0.778 | 0.011 / 0.019 | 0.011 / 0.022 |
| `Ecs.Sys.SceneQuery` | 0.009 / 0.012 | 1.231 / 1.546 | 0.596 / 0.781 | <0.005 | <0.005 |
| `Ecs.Sync.Cleanup` | 0.000 / 0.000 | 0.000 / 0.000 | 0.000 / 0.000 | 0.000 / 0.000 | 0.000 / 0.000 |
| `Ecs.Stage.PreSimulation`（移動のロジック） | 0.025 / 0.033 | 0.388 / 0.540 | 0.197 / 0.286 | 0.009 / 0.017 | 0.010 / 0.020 |
| `Ecs.Stage.LateTick` | 0.034 / 0.088 | 0.137 / 0.242 | 0.078 / 0.141 | 0.029 / 0.044 | 0.029 / 0.052 |
| `GameThread.TransientBoards`（Ecs の外） | **10.433 / 11.636** | 10.690 / 12.573 | 5.959 / 6.791 | <0.005 | <0.005 |
| `GameThread.Simulation`（段を含む） | 0.169 / 0.245 | 0.863 / 1.168 | 0.470 / 0.657 | 0.077 / 0.122 | 0.077 / 0.140 |
| `GameThread.RenderWorld.*` | 0.042 / 0.058 | 0.135 / 0.189 | 0.101 / 0.133 | 0.005 / 0.017 | 0.011 / 0.026 |
| gt | 10.97 / 11.93 | 14.44 / 16.64 | 8.11 / 9.29 | 0.177 / 0.292 | 0.190 / 0.313 |
| rt（※） | 138.0 / 145.6 | 131.8 / 146.6 | 79.8 / 111.8 | 23.3 / 26.2 | 225.4 / 274.2 |
| GPU | 64.9 / 71.3 | 57.0 / 62.4 | 32.0 / 64.8 | 8.6 / 10.0 | 67.7 / 71.6 |

- ※ rt は、計測の窓（CPU の窓の最初〜最後のフレーム番号の範囲）にある Frame 行のうち `RenderThreadMs > 0` の行を独立に集めた median / p95（反復 19 で保存済みの trace.csv から求め直した。標本は順に 302・302・300・289・296 件）。`CaptureScene.ps1` の rt は `RenderFrameMs > 0` の行に限って数えるため、描画スレッドの時間が別の行に記録されるバナナでは 2 件（1万本）・0 件（10万本）しか拾えず、21.7 ms・記録なしと偏っていた。5万個・3万個の値は 0.5 ms 以内で変わらない。
- 段・系・同期点のすべての区間（`cpu_scope_ms`・`cpu_scope_group_ms` の `Ecs.*`。5万個の静止は default 視点。括弧はその区間が出たスレッドの数。フレームあたりの合計で、出ないフレームは 0 として数える。バナナはゲームフレームが描画フレームより多いので、ゲームフレームあたりの値）:

| 区間（median / p95 ms・スレッド数） | 5万個 静止 | 5万個 1% 移動 | 3万個 1% 移動 + 64 m 区画 | バナナ 1万本 overview | バナナ 10万本 horizon |
|---|---|---|---|---|---|
| `Ecs.Stage.*` | 0.180 / 0.258（1） | 2.054 / 2.577（1） | 1.083 / 1.376（1） | 0.096 / 0.164（1） | 0.098 / 0.201（1） |
| `Ecs.Stage.FixedBegin` | 0.016 / 0.020（1） | 0.017 / 0.024（1） | 0.000 / 0.019（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Stage.FixedEnd` | 0.014 / 0.017（1） | 0.014 / 0.021（1） | 0.000 / 0.017（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Stage.FixedModulePost` | 0.004 / 0.005（1） | 0.006 / 0.009（1） | 0.000 / 0.005（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Stage.FixedModulePre` | 0.005 / 0.006（1） | 0.005 / 0.008（1） | 0.000 / 0.006（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Stage.FixedTick` | 0.010 / 0.013（1） | 0.011 / 0.017（1） | 0.000 / 0.012（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Stage.LateTick` | 0.034 / 0.088（1） | 0.137 / 0.242（1） | 0.078 / 0.141（1） | 0.029 / 0.044（1） | 0.029 / 0.052（1） |
| `Ecs.Stage.PostLateTick` | 0.007 / 0.010（1） | 0.008 / 0.012（1） | 0.008 / 0.010（1） | 0.008 / 0.011（1） | 0.007 / 0.012（1） |
| `Ecs.Stage.PostTick` | 0.004 / 0.006（1） | 0.004 / 0.007（1） | 0.004 / 0.006（1） | 0.004 / 0.005（1） | 0.004 / 0.007（1） |
| `Ecs.Stage.PostUpdate` | 0.017 / 0.057（1） | 0.017 / 0.044（1） | 0.021 / 0.053（1） | 0.015 / 0.050（1） | 0.015 / 0.067（1） |
| `Ecs.Stage.PreSimulation` | 0.025 / 0.033（1） | 0.388 / 0.540（1） | 0.197 / 0.286（1） | 0.009 / 0.017（1） | 0.010 / 0.020（1） |
| `Ecs.Stage.PreUpdate` | 0.010 / 0.013（1） | 0.010 / 0.015（1） | 0.010 / 0.013（1） | 0.007 / 0.010（1） | 0.007 / 0.012（1） |
| `Ecs.Stage.RenderSync` | 0.017 / 0.025（1） | 1.175 / 1.500（1） | 0.592 / 0.778（1） | 0.011 / 0.019（1） | 0.011 / 0.022（1） |
| `Ecs.Stage.Tick` | 0.016 / 0.021（1） | 0.226 / 0.327（1） | 0.120 / 0.169（1） | 0.013 / 0.019（1） | 0.013 / 0.023（1） |
| `Ecs.Sync.*` | 0.003 / 0.005（1） | 0.211 / 0.302（1） | 0.107 / 0.149（1） | 0.000 / 0.002（1） | 0.000 / 0.002（1） |
| `Ecs.Sync.Cleanup` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sync.FixedBegin` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sync.FixedEnd` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sync.FixedModulePost` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sync.FixedModulePre` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sync.FixedTick` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sync.LateTick` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sync.PostLateTick` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sync.PostTick` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sync.PostUpdate` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sync.PreSimulation` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sync.PreUpdate` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sync.RenderSync` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sync.Tick` | 0.000 / 0.000（1） | 0.000 / 0.001（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sync.TransformPropagate` | 0.003 / 0.005（1） | 0.210 / 0.301（1） | 0.106 / 0.149（1） | 0.000 / 0.002（1） | 0.000 / 0.002（1） |
| `Ecs.Sys.*` | 0.071 / 0.107（1） | 3.246 / 4.001（1） | 1.617 / 2.057（1） | 0.023 / 0.051（1） | 0.024 / 0.056（1） |
| `Ecs.Sys.FixedBegin.PrepareInterpolation` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.FixedBegin.UpdateTransforms` | 0.002 / 0.003（1） | 0.003 / 0.004（1） | 0.000 / 0.003（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.FixedEnd.CaptureInterpolation` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.FixedEnd.Cleanup` | 0.001 / 0.002（1） | 0.001 / 0.002（1） | 0.000 / 0.002（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.FixedEnd.UpdateTransforms` | 0.002 / 0.003（1） | 0.002 / 0.004（1） | 0.000 / 0.003（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.FixedModulePost.FixedTick` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.FixedModulePre.PreFixedTick` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.LateTick.Cleanup` | 0.001 / 0.002（1） | 0.002 / 0.003（1） | 0.002 / 0.002（1） | 0.001 / 0.002（1） | 0.001 / 0.003（1） |
| `Ecs.Sys.LateTick.DispatchCameraToPreRender` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.LateTick.DispatchPostPhysics` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.LateTick.MassWork` | 出ない | 出ない | 出ない | 0.000 / 0.001（1） | 0.000 / 0.001（1） |
| `Ecs.Sys.LateTick.ReportTickDup` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.LateTick.UpdateRenderTransforms` | 0.005 / 0.007（1） | 0.097 / 0.176（1） | 0.049 / 0.087（1） | 0.004 / 0.007（1） | 0.004 / 0.009（1） |
| `Ecs.Sys.LateTick.UpdateTransforms` | 0.002 / 0.003（1） | 0.002 / 0.003（1） | 0.002 / 0.003（1） | 0.002 / 0.003（1） | 0.002 / 0.005（1） |
| `Ecs.Sys.Physics.CharacterFixed` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.Physics.FixedPre` | 0.002 / 0.003（1） | 0.002 / 0.003（1） | 0.000 / 0.003（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.Physics.FixedStep` | 0.003 / 0.004（1） | 0.003 / 0.004（1） | 0.000 / 0.003（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.PostLateTick.HandlerLateUpdate` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.PostLateTick.ModuleLateTick` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.PostTick.Particles` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.PostUpdate.ModuleTick` | 0.001 / 0.001（1） | 0.001 / 0.001（1） | 0.000 / 0.001（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.PostUpdate.ScriptEnd` | 0.006 / 0.010（1） | 0.007 / 0.010（1） | 0.006 / 0.017（1） | 0.002 / 0.004（1） | 0.002 / 0.005（1） |
| `Ecs.Sys.PreSimulation.GameModeStateMachine` | 0.019 / 0.026（1） | 0.380 / 0.529（1） | 0.190 / 0.276（1） | 0.004 / 0.011（1） | 0.005 / 0.012（1） |
| `Ecs.Sys.PreUpdate.HandlerUpdate` | 0.001 / 0.002（1） | 0.001 / 0.002（1） | 0.001 / 0.002（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.PreUpdate.ScriptBegin` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.RenderSync.SyncToSceneView` | 0.012 / 0.018（1） | 1.163 / 1.482（1） | 0.585 / 0.765（1） | 0.006 / 0.012（1） | 0.006 / 0.014（1） |
| `Ecs.Sys.RenderTransform` | 0.000 / 0.001（1） | 0.091 / 0.165（1） | 0.043 / 0.075（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.SceneQuery` | 0.009 / 0.012（1） | 1.231 / 1.546（1） | 0.596 / 0.781（1） | 0.000 / 0.002（1） | 0.000 / 0.002（1） |
| `Ecs.Sys.Tick.Cleanup` | 0.002 / 0.003（1） | 0.002 / 0.003（1） | 0.002 / 0.003（1） | 0.001 / 0.002（1） | 0.001 / 0.003（1） |
| `Ecs.Sys.Tick.DispatchGroups` | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） | 0.000 / 0.000（1） |
| `Ecs.Sys.Tick.UpdateTransforms` | 0.002 / 0.003（1） | 0.211 / 0.308（1） | 0.107 / 0.153（1） | 0.002 / 0.003（1） | 0.002 / 0.005（1） |

  - 並列の系: すべての区間がスレッド 1（ワーカーに広がる系はこの負荷に無い）。`Ecs.Sync.*` の同期点は `TransformPropagate` 以外ほぼ 0。
  - 1% 移動で増える系は `Ecs.Sys.RenderSync.SyncToSceneView`・`Ecs.Sys.SceneQuery`・`Ecs.Sys.PreSimulation.GameModeStateMachine`（移動のロジック）・`Ecs.Sys.Tick.UpdateTransforms`・`Ecs.Sync.TransformPropagate`・`Ecs.Sys.LateTick.UpdateRenderTransforms`・`Ecs.Sys.RenderTransform`。他は本数・変化に依らず 0.02 ms 未満。
- 1% 移動での増分（静止からの差 ÷ 変わった行。補間の部分木は mega-stress では 0 行）:

| 区間 | 5万個（500 行） | 3万個 + 64 m 区画（297 行） | 増分の比（行の比 1.68） |
|---|---|---|---|
| `Ecs.Sync.TransformPropagate` | +0.207 ms・0.41 µs/行 | +0.103 ms・0.35 µs/行 | 2.0 |
| `Ecs.Sys.RenderTransform` | +0.091 ms・0.18 µs/行 | +0.043 ms・0.14 µs/行 | 2.1 |
| `Ecs.Stage.RenderSync` | +1.158 ms・2.3 µs/行 | +0.575 ms・1.9 µs/行 | 2.0 |
| `Ecs.Sys.SceneQuery` | +1.222 ms・2.4 µs/行 | +0.587 ms・2.0 µs/行 | 2.1 |
| `Ecs.Stage.PreSimulation` | +0.363 ms・0.73 µs/行 | +0.172 ms・0.58 µs/行 | 2.1 |

- 合否:
  - 5万個の静止でゲームスレッドの段の合計 ≤ 2 ms: **合格**（0.180 ms。close 0.18・aerial 0.182 も同じ）。
  - 各区間 ≤ 0.1 ms: 変換の伝播 0.003・描画の変換 0.000・RenderSync 0.017・SceneQuery 0.009・cleanup 0.000 で**合格**（FIX-RENDERSYNC-WALK で RenderSync は 13.24 → 0.017 ms）。
  - 1% 移動の増分が数に比例: **合格**。静止（行 0〜1）でどの区間も 0.02 ms 未満で、増分は変わった行の数で決まり、本数に比例する固定の重さは無い。1 行あたりは 3万個より 5万個が 15〜25% 高い（増分の比 2.0〜2.1 に対し行の比 1.68）。全体の行が増えて動かす行がばらけた分のキャッシュの外れと見る（負荷の条件も 64 m 区画の有無で違う）。
  - 10万本と 1万本の gt の差 ≤ 0.5 ms: **合格**（0.190 − 0.177 = 0.013 ms）。
  - 並列の系: この負荷で走る系は全部 GameThreadOnly で、Ecs の区間はすべてスレッド 1（ワーカーに出る系が無い）。
  - 届かないスコープ: gt の残りのほぼ全部が Ecs の外の `GameThread.TransientBoards`（5万個の静止で 10.43 ms、本数に比例）。SCN-MEGA-FIX-TRANSIENT-BOARDS として足した（反復 17）。他に新しい FIX は無い。
- 検証（`.harness/runs/20261011-211043/`）:
  - `verify-SCN-MEGA-MEASURE-CPU-4.txt`: 5万個の静止の metrics.json に対し、300 フレーム・gt の p95 ≤ 16.6 ms・区間 6 つ（`Ecs.Stage.*`・`Ecs.Sync.TransformPropagate`・`Ecs.Sys.RenderTransform`・`Ecs.Stage.RenderSync`・`Ecs.Sys.SceneQuery`・`Ecs.Sync.Cleanup`）が揃って `result=pass views=default,close,aerial`、EXIT=0（同じ確認の反復 17 の 2 も pass）。
  - `verify-SCN-MEGA-MEASURE-CPU-5.txt`: Debug の起動画面 default を撮って `CAPTURE_STARTUP_SCENE result=pass`、EXIT=0。`vulkan_validation` の error_count=0・vuid_count=0、Game.log の ERROR は 0。PNG を開いて確認（天球・地面・球・岩・家）。
  - 撮影の Game.log の ERROR は 5 つの負荷とも 0（落ちた 5万個 + 64 m 区画を除く）。
- Next: SCN-MEGA-FIX-TRANSIENT-BOARDS。
- Notes: 1% 移動の RenderSync と SceneQuery は 1 行あたり 2〜2.4 µs で、移動の CPU の大半（5万個の 1% で約 2.4 ms）。合格の条件（比例）は満たすので FIX は足さない。バナナの rt は窓の Frame 行から求め直した（上の ※）。10万本 horizon の rt は 225 ms で 1万本の 23 ms より大きいが、合否の条件（gt の差）の外で、描画側の費用（MEASURE-CULL などで見る）。

### SCN-MEGA-MEASURE-CPU 差し戻しの対応（2026-10-12、反復 19・run 20261011-211043）

- 評価者の指摘 2 点を、保存済みの計測から直した（撮り直していない）: (1) rt を計測の窓の Frame 行から独立に集め、表の rt を直した（バナナ 1万本 23.3 / 26.2 ms・10万本 225.4 / 274.2 ms）。(2) 段・系・同期点のすべての `Ecs.*` 区間の median・p95・スレッド数の表を足した（バナナの SceneQuery は 0.000 / 0.002 ms）。
- 検証: verify 1・2 を走らせ直して `verify-SCN-MEGA-MEASURE-CPU-6.txt`・`-7.txt` に保存した。6: 5万個の静止の metrics.json に対し `CHECK_CAPTURE_METRICS result=pass views=default,close,aerial`・EXIT=0。7: Debug の起動画面 default を撮って `CAPTURE_STARTUP_SCENE result=pass`・EXIT=0、`vulkan_validation` の error_count=0・vuid_count=0、Game.log の ERROR は 0、PNG を開いて確認（天球・地面・球・岩・家）。
- Next: SCN-MEGA-FIX-TRANSIENT-BOARDS。
- Notes: `CaptureScene.ps1` の rt の集計（`RenderFrameMs > 0` の行に限る）はバナナのように描画スレッドの時間が別の行に出る負荷で偏る。スクリプトは paths の外なので直していない。

### SCN-MEGA-FIX-TRANSIENT-BOARDS（2026-10-12、反復 20・run 20261011-211043）

- Done（実装のみ。タスクは BLOCKED）: `World::CollectTransientBoardProxies` は既定で Text の行の一覧 `m_TextBoardRows` だけを見る。行を得たとき（`BindComponentRow`・予約の再生）に足し、部分木の追加・親の付け替え・行の追加・死んだ行の発見で並べ直す。並べ直しは各 Text の根からの道（各段の Outer の Inner の中の位置。Component は同じ Entity の子より先）で旧の走査と同じ木の深さ優先の順にする。アクティブでない・破棄待ち・木の外の Text は出さない。旧の同期（`SetLegacyRenderSync(true)`）のときだけ根から全走査する（差分テストの旧との照合が順も含めて比べる）。
- 検証: verify 1（Debug ビルド）pass、verify 2（`WorldSyncDifferentialTest`）pass、関連の `TextComponentBoardProxyTest`・`ParticleFrameSnapshotTest` も pass。verify 3（起動画面の比較）pass（輝度差 0・PSNR 100 dB）。5万個の静止の撮影で `GameThread.TransientBoards` は median 0.000 ms・max 0.025 ms、gt の median は 10.97 → 0.174 ms（p95 0.301 ms）。
- verify 4 は `timing_missing=render_thread_ms_*` だけで fail（区間の閾値だけの検査は `verify-SCN-MEGA-FIX-TRANSIENT-BOARDS-4-scope-only.txt` で pass）。gt が下がりゲームスレッドが描画 1 フレームあたり約 800 フレーム回るため、`CaptureScene.ps1` が rt を拾う `RenderFrameMs > 0` の行に `RenderThreadMs` が載らない（窓の中で両方が載る行は 0 件。独立に集めた rt は median 135.6 ms で前回の 138.0 ms とほぼ同じ）。直す先は paths の外なので `blocked/SCN-MEGA-FIX-TRANSIENT-BOARDS.md` に選択肢を書いた。
- Next: blocked の判断（推奨は `CaptureScene.ps1` の rt を窓の Frame 行から独立に集める）の後、verify 4 だけを撮り直して done にする。
- Notes: 木の順の並べ直し（兄弟の位置が登録の順と違う場合）を直接突くテストは無い（差分テストの Text は登録の順と木の順が同じ）。

### SCN-ECS-RETIRE-LEGACY（2026-10-12、反復 21・run 20261011-211043）

- BLOCKED（コードは変えていない）: done-when に従って先に利用者を数えた。paths の外に利用者が残る。ComponentDataRegistry は `EntityHandle` の発行と解決（`RegisterEntity`・`ResolveEntity`）を持ち、`Game/Input/PickingController.cpp`・`Library/Modules/Physics/.../PhysicsModule.cpp`（`GetEntityHandle`）・`M8MinimalPhysicsSmoke.cpp`・`Test/Core/Engine/ApplicationFixedStepPipelineTest.cpp`・`Test/Core/Object/WorldTickGroupTest.cpp` が使う。旧の切替は `Library/Core/Private/Scene/SceneQuery.cpp:944`（`IsLegacyRenderSync`）・`Test/Core/Object/EcsHierarchyTest.cpp`・`SceneQueryRaycastTest.cpp` が使う。
- 決めたいこと・選択肢・推奨は `blocked/SCN-ECS-RETIRE-LEGACY.md`（推奨: 旧の切替を消す項目と、ハンドルの表を World へ移して ComponentDataRegistry を消す項目に分け、paths を広げる）。
- Next: 人の判断の後に再開。
- Notes: `WorldSyncDifferentialTest` は旧の全走査を照合の正解に使っているので、旧を消すときは手で決めた期待値との照合に書き換える必要がある。

### SCN-MEGA-FIX-TRANSIENT-BOARDS 再開（2026-10-12、反復 22・run 20261011-211043）

- Done: `Scripts/CaptureScene.ps1` の描画スレッドの時間（rt）を、CPU の窓（`RenderFrameMs > 0` の行で取る窓）の最初〜最後のフレーム番号の範囲の Frame 行のうち `RenderThreadMs > 0` の行から独立に集めるようにした。ゲームスレッド・CPU のフレーム時間とスコープの窓は今までどおり。グリフの板集めの実装は反復 20（b6a6a3c）のまま。
- 検証（`.harness/runs/20261011-211043/`）:
  - `verify-SCN-MEGA-FIX-TRANSIENT-BOARDS-1.txt`: Debug の `WorldSyncDifferentialTest` のビルド、EXIT=0。
  - `verify-SCN-MEGA-FIX-TRANSIENT-BOARDS-2.txt`: `WorldSyncDifferentialTest` 1/1 pass、EXIT=0。
  - `verify-SCN-MEGA-FIX-TRANSIENT-BOARDS-3.txt`: 起動画面 default の決定的比較 `result=pass`（輝度差 0・PSNR 100 dB・不一致の画素 5E-06）、EXIT=0。PNG を開いて確認（天球・地面・球・岩・家）。
  - 撮り直し `capture-SCN-MEGA-FIX-TRANSIENT-BOARDS-static.txt`（5万個の静止、RelWithDebInfo）: `result=pass`。gt median 0.174 ms・p95 0.267 ms、rt median 135.508 ms・p95 142.59 ms（反復 20 で手で集めた 135.6 / 141.7 ms と一致）、GPU median 64.98 ms。metrics.json の `GameThread.TransientBoards` は median 0・p95 0・max 0.022 ms。
  - `verify-SCN-MEGA-FIX-TRANSIENT-BOARDS-4.txt`: `--timing --min-frames 300 --max-scope-median-ms GameThread.TransientBoards=0.1` で `CHECK_CAPTURE_METRICS result=pass views=default`、EXIT=0。
- Next: 残りの todo の最優先。
- Notes: rt は描画スレッドの行から独立に集めるので、ゲームスレッドと描画スレッドの 1:1 の負荷では窓の行と同じ標本になり、値は変わらない。

### SCN-ECS-RETIRE-LEGACY-FLAGS（2026-10-12、反復 1・run 20261012-030203）

- Done: 旧の更新・同期の切替（`--ecs-legacy-transform`・`--ecs-legacy-sync`、`SetLegacyRenderSync`/`IsLegacyRenderSync`、`SetLegacyTransformPropagation`）、旧の再帰（`UpdateEntityTransformRecursive`・`SyncEntityRecursive`・`CollectTransientBoardProxiesRecursive` ほか）、全走査との照合（`EcsRenderSyncCompare`・`FinishRenderSyncFrame`・`NoteRenderScanChanged`・`CheckRenderRowUnchanged`）、SceneQuery の旧への切替を消した（実装は途中保存 eaad1e4f のまま。`--mega-scene-full-sync` は残す）。`WorldSyncDifferentialTest`・`EcsHierarchyTest`・`SceneQueryRaycastTest` は手で決めた期待値との照合（追加・値の変更・親の付け替え・非アクティブ・破棄・シーンの出入り）に替えた。
- 周回の失敗の切り分け: 変更前の 7c7e56f7 を同じビルドで周回しても同じ形で落ちた（`check-cycle-baseline-7c7e56f7.txt`。サンプル 7 件・結果行なし・Game.log がちょうど 10MB）。原因はこの変更ではなく、周回中のゲームスレッドが毎秒数万フレーム回り、120 フレームごとの `ECS_TRANSFORM_COUNTS`・`SCENEQUERY` の行（各約 1 万行・2MB 強）で Game.log が上限 10MB を越えて回り、スクリプトが結果の行を読めないこと。両方の行を「120 フレーム（回）以上かつ前回から 1 秒以上」で出すようにした（World.cpp・SceneQuery.cpp）。
- 開発ビルドの検査の扱い:

| 検査 | 扱い | 照合の中身 |
|---|---|---|
| ECS_TRANSFORM | 残す（旧の再帰を使わない全量の照合） | 階層のシステムの `EcsWorldTransform` を、根から局所の変換をたどって求め直した値と全行 bit まで照合 |
| ECS_RENDER_TRANSFORM | 残す（同上） | 描画の変換の列を全行求め直した値と照合（「旧の式」の言い回しを外しただけ） |
| ECS_RENDER_SYNC | 全走査との照合（`EcsRenderSyncCompare`）を消し、全量の照合を残す | `VerifyRenderSyncDelta`: 全 Entity・Component から作り直した proxy と SceneView の proxy・内容の hash を照合 |
| ECS_RENDER_INVALIDATION | 旧の全走査の中の作り直し照合（`CheckRenderRowUnchanged`）を消し、全量の照合を残す | `VerifyRenderSyncDelta` の内容の hash の照合（通知・変更ログに載らない変化の検出）と再試行の一覧の照合 |
| ECS_SCENEQUERY | 変わらず | 全量の Rebuild との照合 |

- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-RETIRE-LEGACY-FLAGS-1.txt`: Debug の 4 ターゲットのビルド EXIT=0、error・warning 0。
  - `-2.txt`: 15 件中 14 件 Passed（EcsHierarchyTest・WorldSyncDifferentialTest・SceneQueryRaycastTest ほか）。M9WorldAcceptanceTest だけ `reason=t0_capture_failed`（一時ディレクトリのテクスチャが `LooseReadFailed`）で EXIT=8。親の記録どおり変更の前後で同じ理由で落ちる既知の失敗で、この項目と無関係。
  - `-3.txt`: 3 シーンの混在の周回 3 周が `SCENE_CYCLE_SMOKE_RUN result=pass`、EXIT=0。Game.log を開いて確認: `SCENE_CYCLE_SMOKE result=pass cycles=3 scenes=3 changed=none unsettled=0`、サンプル 9 件、ERROR 0、Game.log 8.2MB（`ECS_TRANSFORM_COUNTS`・`SCENEQUERY` は各 71 行）。
- Next: 残りの todo の最優先（SCN-ECS-RETIRE-REGISTRY ほか）。
- Notes: 周回の Game.log は 8.2MB でまだ上限 10MB に近い。残りの大半は RenderingCoordinator の 120 フレームごとの `MEGA_SCENE_DELTA`・`PACKET_DELTA`（paths の外）。周回の数・シーンを増やすと同じ形で落ちうるので、同じ間引きを入れるか、周回中のフレームの速さ（毎秒数万フレーム）を見直す項目が要る。

### SCN-ECS-RETIRE-LEGACY-FLAGS 差し戻しの対応（2026-10-12、反復 2・run 20261012-030203）

- BLOCKED（コードは変えていない）: ランナーの再検証で verify 2 が M9WorldAcceptanceTest（`Reference` ラベルの GPU の受入れ）の `reason=t0_capture_failed` で落ちた。変更の前から同じ理由で落ち、出どころ（`Rendering3DTestRoutine.cpp`・`FrameCaptureReadbackHelper.cpp`）は paths の外。verify から外すのは検査の範囲の変更なので、`blocked/SCN-ECS-RETIRE-LEGACY-FLAGS.md` に選択肢を書いた（推奨: verify 2 に `-LE Reference` を足し、M9 の撮影失敗は別項目で直す）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-RETIRE-LEGACY-FLAGS-4.txt`: verify 1 の Debug の 4 ターゲットのビルド EXIT=0、error・warning 0。
  - `verify-SCN-ECS-RETIRE-LEGACY-FLAGS-5-excluding-M9.txt`: verify 2 から M9WorldAcceptanceTest だけを除いて 14/14 Passed、EXIT=0。
- Next: 人の判断の後に再開（推奨 A なら verify を取り直して done）。
- Notes: M9 の撮影失敗はログに失敗の状態（`FrameCaptureResultStatus`）が出ないので原因が未特定。直す項目ではまず状態をログに出す。

### SCN-ECS-RETIRE-REGISTRY（2026-10-12、反復 3・run 20261012-030203）

- Done: `EntityHandle` の発行と解決を World に移した。`World::GetEntityHandle(entity)` は ECS の行の番号と世代に World の識別子（`WorldId`。ECS の世界を作るたびに新しく取る）を付けて返し、`World::ResolveEntityHandle(handle)` は識別子・行の生存・`EcsObjectRef` の世代を照合して Entity を返す（表は World の ECS の行そのもので、World ごとの寿命）。`Entity::GetEntityHandle` は自分の World の発行を返す（`m_EntityHandle`・`SetEntityHandle` は消した）。`GEngine` のメンバの ComponentDataRegistry（`Engine/ComponentDataRegistry.*`、Publish*、`NORVES_ENABLE_COMPONENT_DATA_REGISTRY`）と `ComponentDataRegistryTest`・`ComponentDataBenchmark` を消した。
- 利用者: `PickingController` は `Engine::GetWorld().ResolveEntityHandle`、`ApplicationFixedStepPipelineTest` は World の発行と解決、`WorldTickGroupTest` は登録の解除の代わりに切り離した部分木のハンドルが無効になることを見る。`M8MinimalPhysicsSmoke` は Release でもハンドルが有効になったので条件コンパイルを外した。`PhysicsModule` は `slot.Owner->GetEntityHandle()`（所有者の World の発行）のままにした（テストの fixture は Engine の World でない World を使うので、Engine の World に固定しない）。
- テスト: `EcsLifetimeTest` に `TestEntityHandleIssueAndResolve` を足した（付け替えでハンドルが変わらない・世代の違うハンドルを拒む・破棄の予約の間は解決し同期点の除去の後は解決しない・行の番号の再利用で古いハンドルが新しい Entity を指さない・即時の除去の後・別の World の同じ行の番号と世代のハンドルを拒む）。
- スクリプト: `Scripts/Test-M8MinimalPhysics.ps1` の Game の起動 2 本に `--window-background` を足し、`-Configuration RelWithDebInfo` を受け付けるようにした（開発ビルドは Debug と同じく印まで照合）。スモークの印（ready・complete・failure）は `NORVES_BUILD_DEBUG` だけで出ていたので `NORVES_BUILD_DEVELOPMENT` に替えた（Release は変わらず終了コードだけ）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-RETIRE-REGISTRY-1.txt`: Debug の EntityTreeTest・ApplicationFixedStepPipelineTest のビルド EXIT=0、error 0。
  - `-2.txt`: WorldTickGroupTest・EcsLifetimeTest・EcsHierarchyTest・ApplicationFixedStepPipelineTest 4/4 Passed、EXIT=0。
  - `-3.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `-4.txt`: `M8_MINIMAL_PHYSICS_ACCEPTANCE result=pass configuration=RelWithDebInfo normal_exit=0 smoke_exit=0`、EXIT=0（ready の印 1 回・complete の `query_stable=1`（照会の当たりの Entity のハンドルの一致を含む）・failure の印なしをスクリプトが照合）。
- 確認（verify の外）: SceneQueryPhysicsFacadeTest・LegacyInputResetTest は pass。PhysicsBroadphaseQueryTest は Debug で 2 個目の fixture の `InstallAll` の assert で止まる既知の失敗（反復 SCN-ECS-TICK-PHYSICS の記録と同じ。この変更と無関係）。
- Next: 残りの todo の最優先。
- Notes: 段の最中に作った Entity は、作成の予約が同期点で再生されるまで行が無いのでハンドルは無効（旧の表は作った時点で発行していた）。`Library/Core/Public/Math/MatrixUtils.h` のコメントに ComponentDataRegistry の名が残る（paths の外）。

### SCN-ECS-RETIRE-LEGACY-FLAGS 再開（2026-10-12、反復 4・run 20261012-030203）

- Done: 人の判断（推奨 A: verify 2 に `-LE Reference`）で todo に戻ったので、コードは変えずに現在の HEAD（SCN-ECS-RETIRE-REGISTRY の後）で 3 本の verify を取り直し、done にした。開発ビルドの検査の扱いの表は反復 1 の節のとおり。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-RETIRE-LEGACY-FLAGS-6.txt`: Debug の 4 ターゲット（EntityTreeTest・SceneViewProxyLookupTest・WorldSyncDifferentialTest・SceneQueryRaycastTest）のビルド EXIT=0、error・warning 0。
  - `-7.txt`: `-LE Reference` 付きで 14/14 Passed（EcsHierarchyTest・WorldSyncDifferentialTest・SceneQueryRaycastTest ほか）、EXIT=0。
  - `-8.txt`: 3 シーンの混在の周回 3 周が `SCENE_CYCLE_SMOKE_RUN result=pass`、EXIT=0。Game.log を開いて確認: `SCENE_CYCLE_SMOKE result=pass cycles=3 scenes=3 changed=none unsettled=0`、サンプル 9 件、ERROR 0、Game.log 7.7MB。
- Next: 残りの todo の最優先（SCN-FIX-M9-T0-CAPTURE ほか）。
- Notes: Git Bash から verify 1 を打つと `/m:1` がパスに変換されて MSBuild が MSB1008 で落ちる。PowerShell から実行する。

### SCN-ECS-RETIRE-LEGACY-FLAGS 差し戻しの対応（2026-10-12、反復 5・run 20261012-030203）

- Done（評価者の NEEDS_WORK の 2 点）:
  1. 開発ビルドの検査の作り直しを、Entity の木（`GetRootEntities`・`GetChildEntities` の再帰・スタック）から ECS の行へ移した。`World::CollectEntityRowsByDepth` を足し、ECS の Entity の行（孤児を除く）を階層の列の Depth の順（親が子より先）に集め、行ごとに親の行の位置・根から届くか・「自分と祖先がすべて Active かつ破棄の予約でない」を親の行の結果から求める。`VerifyEntityTransforms`（再帰）と `VerifyRenderEntity`（再帰）を消し、1 行分の照合（`VerifyRowTransform`・`World::VerifyRenderRow`）を親が先の順に回して、求め直した値を行の位置ごとに覚えて子が読む形にした。`VerifyRenderSyncDelta` のスタックによる木の走査も同じ一覧に替えた。`SceneQuery::Rebuild(const World&)`（検査 ECS_SCENEQUERY の比較先）も同じ一覧から作るようにし、`CollectRecursive` を消した。
  2. `SceneQueryRaycastTest` の `VerifyAgainstRebuild` を正解にした照合をすべて外し、手で決めた期待値に替えた。`TestIncrementalUpdateMatchesExpected`（旧 `TestIncrementalUpdateMatchesRebuild`）は、各操作（追加・値の変更・親の動き・非アクティブと戻し・破棄・大量の追加での作り直し）の後に、Entry の集合（全体を覆う箱の照会で取り出して期待する Entity の集合と比べる）、狙った Entity への当たりと距離（中心から 10 離れた点から撃ち、距離 = 10 - √3。√3 は既定の ±1 の箱を包む球の半径）、外れた場所の非ヒットを確かめる。MegaGeometry の子・非アクティブな親への付け替え・World の切り替えの 3 テストも同じ道具で明示の期待値にした。
- 開発ビルドの検査の扱い（反復 1 の表を差し替える）:

| 検査 | 扱い | 照合の中身 |
|---|---|---|
| ECS_TRANSFORM | 残す（ECS の行から全量を作り直して比べる） | `CollectEntityRowsByDepth` の順に、根から局所の変換の列を合成した値と `EcsWorldTransform` を全行 bit まで照合し、`EffectiveActive` を親の行から求めた判定と照合。親が一覧の先に無い行は、親が孤児なら飛ばし（ECS_BIND の担当）、そうでなければ失敗 |
| ECS_RENDER_TRANSFORM | 残す（同上） | 同じ順に、親の行について求め直した描画の変換と補間の有無から自分の期待値を求め、`GetRenderWorldTransform`・列とタグの組と照合。破棄の予約と孤児の部分木は見ない |
| ECS_RENDER_SYNC | 残す（同上） | `VerifyRenderSyncDelta`: 同じ一覧で自分と祖先がすべて Active かつ破棄の予約でない行の Component から proxy を作り直し、SceneView の proxy・内容の hash と照合 |
| ECS_RENDER_INVALIDATION | 残す（同上） | `VerifyRenderSyncDelta` の内容の hash の照合（通知・変更ログに載らない変化の検出）と再試行の一覧の照合 |
| ECS_SCENEQUERY | 残す（同上） | 増分の Entry と照会の結果を、`Rebuild`（同じ一覧から作る）と照合 |

- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-RETIRE-LEGACY-FLAGS-9.txt`: Debug の 4 ターゲットのビルド EXIT=0、error・warning 0。
  - `-10.txt`: `-LE Reference` 付きで 14/14 Passed（EcsHierarchyTest・WorldRenderInterpolationTest・WorldSyncDifferentialTest・SceneQueryRaycastTest ほか）、EXIT=0。
  - `-11.txt`: 3 シーンの混在の周回 3 周が `SCENE_CYCLE_SMOKE_RUN result=pass`、EXIT=0。Game.log を開いて確認: `SCENE_CYCLE_SMOKE result=pass cycles=3 scenes=3 changed=none unsettled=0`、サンプル 9 件、ERROR 0、Game.log 8.2MB。
- Next: 残りの todo の最優先（SCN-FIX-M9-T0-CAPTURE ほか）。
- Notes: `World::SeedRenderSyncLog`（変更ログを取り始めるときの種まき）と `NotifyRenderRepublishAll` は木をたどるが、検査の比較先ではなく同期の側の一度きりの処理なので残した。

### SCN-FIX-M9-T0-CAPTURE（2026-10-12、反復 6・run 20261012-030203）— BLOCKED

- 原因: M9 の t0 の撮影は `SourceMissingTransferSrc`（フレーム 0、撮影元 PresentationColor）で失敗する。撮影元は 1280x720・RGBA16F・用途 `ShaderRead|RenderTarget` で `TransferSrc` が無い。これは `VignettePass` の出力 `VignetteOutput`（`ToneMappedColor` を上書きする）で、TemporalAA が効くと FXAA が止まり、拡大なしの Upscale が `ToneMappedColor` をそのまま `PresentationColor` に出すので、Vignette の出力が撮影元になる。他の後処理の出力はすべて `TransferSrc` を足しているが `VignetteOutput` だけ足していない。Rendering3DTest はカメラのグレーディングでビネットを有効にしている（SS-POST-TUNE 以降）。このブランチの変更（ランチャー・`--window-background`・ECS の同期）は原因ではない。
- Done（paths の内側）: `FrameCaptureTypes.h` に `GetFrameCaptureResultStatusName` を足し、`FrameCaptureReadbackHelper` の失敗の発行で `FRAME_CAPTURE_FAILED status=… request=… frame=… source_kind=…`、転送元の用途が無いときは `FRAME_CAPTURE_SOURCE width=… height=… format=… usage=…` をログに出す。`Rendering3DTestRoutine.cpp` の t0・陰性対照・t1 の撮影の失敗で `M9_WORLD_SMOKE capture_status=…` を出す。`Scripts/Test-M9World.ps1` の Game の起動 2 本に `--window-background` を足した。
- BLOCKED: 直すには paths の外の `Library/Core/Private/Rendering/VignettePass.cpp`（`VignetteOutput` の desc に `TransferSrc` を足す 1 行）が要る。`blocked/SCN-FIX-M9-T0-CAPTURE.md` に選択肢を書いた（推奨: paths に足して 1 行直す。描画の結果は変わらない）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-FIX-M9-T0-CAPTURE-1.txt`: Debug の Game のビルド EXIT=0、error 0（`-5-build.txt` は診断ログを足した後の再ビルドで EXIT=0）。
  - `-2.txt`・`-4.txt`: `ctest -R ^M9WorldAcceptanceTest$` は EXIT=8（未修正のため）。通常起動（`--window-background` 付き）は通り、M9 のスモークが t0 の撮影で落ちる。
  - `-3-gamelog.txt`: スモークの Game.log の抜粋（`FRAME_CAPTURE_SOURCE … format=9 usage=0x5`・`FRAME_CAPTURE_FAILED status=SourceMissingTransferSrc`・`capture_status=SourceMissingTransferSrc`）。
- Next: 人が paths を広げて todo に戻したら、VignettePass の 1 行を直して M9 を通す。

### SCN-ECS-SCOPE-METRICS-THREADS（2026-10-12、反復 7・run 20261012-030203）

- Done: `Scripts/CaptureScene.ps1` の `cpu_scope_ms`・`cpu_scope_group_ms` の median・p95・max を、その区間が出たフレーム（`frames_present`）だけで求めるようにした（出なかったフレームを 0 で埋めるのをやめた。範囲のフレーム数は `cpu_scope_frames` に残る）。区間ごとに出たスレッドの名前を `thread`（GameThread・RenderThread・Worker を , で連ねる）に書く。スレッドの名前はトレースの Frame の行を書く ThreadId（最も多いもの）を GameThread、GPU の行を書く ThreadId を RenderThread、それ以外を Worker とする。標準出力の `cpu_scope_median` の `t=` も名前にした。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-SCOPE-METRICS-THREADS-1.txt`: RelWithDebInfo のビルドと mega-stress 2000 個の撮影が `CAPTURE_SCENE result=pass`、EXIT=0。`RenderGraph.Execute=2.397/max=3.256/t=RenderThread`、`MegaGeometryPass.RecordFrameCommand=0.695/max=1.057/t=RenderThread`。
  - `-2.txt`: `RenderGraph.Execute` の median 2.397 ms・p95 2.876・max 3.256・`thread=RenderThread`・frames_present 122（範囲 9432 フレーム）、EXIT=0。
- Next: 残りの todo の最優先（MEASURE-UPLOAD・RT-PASSES・MEASURE-RT はこの値を使う）。
- Notes: この撮影では区間 70 種のうち GameThread 66・RenderThread 4 で、Worker に出た区間は無かった。median の意味が「範囲の全フレーム」から「出たフレーム」に変わったので、以前の撮影の `cpu_scope_ms` の median（毎フレーム出ない区間）とは直接比べられない。

### SCN-FIX-M9-T0-CAPTURE（2026-10-12、反復 8・run 20261012-030203）

- 原因は 3 段で、いずれもこのブランチの変更（ランチャー・`--window-background`・ECS の同期）ではなく、M9 の受入れが書かれた後に既定になった描画の設定による。
  1. t0 の撮影の失敗（`SourceMissingTransferSrc`）: TAA が効いて拡大なしのとき撮影元になる `VignetteOutput` だけ用途に `TransferSrc` が無かった（反復 6 の記録どおり）。
  2. 1 を直すと陰性対照で落ちた（`negative_control_pixel_delta`）: 何も変えない t0（フレーム 0）と陰性対照（フレーム 2）が全 921600 画素で違った。起動直後で組み立て途中（大きな球の生成・VT のタイルの到着）なうえ、既定の自動露出の順応・TAA のジッタ・RTGI の乱数と履歴・起動画面の揺れるスキンのパネル（壁時計で動く）が毎フレーム画面を変える。自動露出と TAA を止めても 676099 画素が違った。
  3. 2 を直すと最終の検査で落ちた（`pixel_or_skinned_submit_validation`、`changed_pixels=36405 negative_changed_pixels=0 gbuffer=0 shadow=1`）: M9 が見るスキンの GBuffer の描画数は GBuffer のラスタの経路で数えるが、既定のビジビリティバッファではスキンを VisibilityRaster が描き、GBuffer パスは描かない（`GBufferPass::ShouldSkipDraws`）ので 0 のまま。
- Done:
  - `VignettePass.cpp`: `VignetteOutput`（RenderGraph と旧経路の両方）の用途に `TransferSrc` を足した。用途の追加だけで描画は変わらない。
  - `Rendering3DTestRoutine.cpp`: M9 の実行のときだけ、手動露出・FXAA にし、RTGI を切る（通常の起動とランチャーから入る Rendering3DTest の見た目は変えない）。t0 の撮影を `IsStartupSceneAssembled`（組み立てと VT のタイルがそろう）まで待つ（Tick の残りは止めない）。陰性対照と最終の検査の失敗で、差の画素数・フレーム番号・描画数をログに出す。
  - `StartupSkinnedProbe.cpp`: M9 の実行のときは揺れるパネルの姿勢を進めない（大きな球の自転を M9 で止めているのと同じ扱い）。
  - `Scripts/Test-M9World.ps1`: M9 のスモークの起動に `--visibility-buffer=off`（装置が対応しないときに戻る GBuffer のラスタの経路）を足し、スキンの GBuffer の描画数の検査を契約どおり確かめる。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-FIX-M9-T0-CAPTURE-8.txt`: Debug の Game のビルド EXIT=0、error 0（`-6.txt` は VignettePass だけ直した時点のビルドで EXIT=0）。
  - `-9.txt`: `ctest -R ^M9WorldAcceptanceTest$` が `1/1 Passed`（94.27 s）、EXIT=0。`-7.txt` は VignettePass だけ直した時点の失敗（`negative_control_pixel_delta`）。
- Next: 残りの todo の最優先。
- Notes: 既定の経路（ビジビリティバッファ）でのスキンの描画は M9 で数えていない。VisibilityRaster の `SkinnedRecords` を `RenderingCoordinatorStatsSnapshot` へ出せば（`RenderingCoordinator.cpp` 側。この項目の paths の外）、M9 を既定の経路のまま検査できる。
- Notes: t0 は組み立てがそろってから撮るので、M9 のスモークの t0 はフレーム 61 前後になる（Debug。VT の落ち着きに 60 ティック）。

### SCN-MEGA-GPU-SCENE-PREP（2026-10-12、反復 9・run 20261012-030203）

- Done:
  - シェーダー: `Assets/Shaders/Common/MegaInstanceTable.glsl` を足し、インスタンスの表の行（`MegaInstanceRow` 144 B: world・previousWorld・メッシュの表の番号・最初のワークグループ・「見えた」印の先頭）とメッシュの表（`MegaMeshEntry` 64 B: LOD の球・クラスタ配列・材質の区間・頂点とインデックスの基点・BVH・ページの表の先頭）を宣言する。`LoadMegaInstance(index)` が 2 つの表から以前の `MegaInstance` と同じ値を組み立てる（平らな判定の二分探索だけ `LoadMegaInstanceFirstGroup`）。cull（`MegaGeometryCull.glsl`・`cluster_cull`・`cluster_bvh_cull`）・visbuffer（`visbuffer_mega.vert`・`visbuffer_sw_raster`・`VisibilityResolve.glsl`）・vsm_mega（`vsm_mega_cull`・`vsm_mega_chunks`）・`megageometry.vert` の直接の読みをすべて寄せた。メッシュの表の binding はどのセットでも 22。
  - C++: `GPUMegaInstance`（144 B）と `GPUMegaMesh`（64 B）に分け、`MegaGeometryPass::BuildSceneTables`（CPU で表を作る）・`UploadSceneTables`（フレームスロットへ書き、VSM の入力を作る）・`PrepareSceneTables`（共通の前段）を足した。前段は `MegaGeometryPass::Execute`（RenderGraph の順で、主のカリングと描画・ID のラスタと解決・VSM の影のどれよりも前）で表を書き、同じフレーム・同じ枠の記録はその表とスロットを使う。前段を通らない記録（同じフレームの 2 つ目のビューポート・コマンドを直接組んだ呼び出し）は従来どおり自分で作る。「見えた」ビットの先頭は表を作るときにクラスタ数を詰めて書く（`UpdateVisibilityLayout` の配置と同じで、2 パスでないフレームはシェーダーが読まない）。メッシュの表は描画・カリング・ID のラスタ（ソフトを含む）・解決・VSM のカリングと影の塊の記録の各セットへ binding 22 で結ぶ。
  - テスト: 表を手で組む `GeometryPageRequestVulkanTest`・`VirtualShadowMapVulkanTest`（ケース K を含む）・`VisibilityResolveVulkanTest` を、行とメッシュの表に分けて書く形にした。`RenderGraphCompileTest` のインスタンスの表のバイトの検査をメッシュの表と合わせて新しい並びにし、VSM のカリングと影の塊の記録の束縛の数（12・6）とメッシュの表の束縛を確かめるようにした。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-MEGA-GPU-SCENE-PREP-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0（warning は libwebsockets の `MSB8065` 1 件だけ）。
  - `-2.txt`: バナナ 1 万本 overview の `SCN-MEGA-BASE` との比較が `mean_luminance_difference=0`・`psnr_db=65.311`・`mismatched_pixel_ratio=0.000246` で `CAPTURE_SCENE result=pass`、EXIT=0。Game.log の ERROR 0。以前の項目の同じ比較（64.8〜69.2 dB・0.0002〜0.0003）の幅の中。
  - `-3.txt`（追加の確認）: `visibility-buffer=off`（GBuffer のラスタの経路。`megageometry.vert` と描画のセットの binding 22）で撮って ERROR 0、`SCN-MEGA-BASE` との比較 pass（47.2 dB。経路の違いの差）。
  - `-8.txt`: Debug の ctest 4 本のうち `GeometryPageRequestVulkanTest`・`VirtualShadowMapVulkanTest`・`RenderGraphCompileTest` が Passed。`VisibilityResolveVulkanTest` は「直接 dispatch の粗さの既定」の 1 件だけで落ちる（`-8-resolve-failures.txt`）。変更を stash して変更前（HEAD 0ed7bf4e）で再ビルドしても同じ 1 件で落ちる（`-6-baseline.txt`）ので、この変更によるものではない。
- Next: SCN-MEGA-GPU-SCENE-SCATTER（席ごとの device-local な表と散在書き込み。前段の `PrepareSceneTables` の位置で compute を記録する）。
- Notes: `VisibilityResolveVulkanTest` の直接 dispatch の粗さ（MegaGeometry の AO の既定、`VisFlatMaterialSurface`）の失敗は変更前からある。直す項目は TASKS に無い。
- Notes: 前段は `MegaGeometryPass::Execute` の中に置いた（RenderGraph の順で MegaGeometry が表の読み手のどのパスよりも前。CSM・点光源の影は GPU の表を読まない）。独立の RenderGraph のパスにするかは、散在書き込みの compute を入れる SCATTER で決める。

### SCN-MEGA-GPU-SCENE-PREP の差し戻しの対応（2026-10-12、反復 10・run 20261012-030203）

- Done: `cluster_cull.comp` の平らな判定の二分探索を `LoadMegaInstance(mid).clusterInfo.w` で読むようにし、行を直接読む `LoadMegaInstanceFirstGroup` を消した。これで cull・visbuffer・vsm_mega・`megageometry.vert` のインスタンスの読みは `LoadMegaInstance` だけになった（`grep LoadMegaInstanceFirstGroup` は 0 件）。`MegaGeometryPass.cpp` の前段へ移した DrawIndirectFirstInstance の警告文を日本語にした。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-MEGA-GPU-SCENE-PREP-9.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0（warning は libwebsockets の `MSB8065` 1 件だけ）。
  - `-10.txt`: バナナ 1 万本 overview の `SCN-MEGA-BASE` との比較が `mean_luminance_difference=0`・`psnr_db=68.642`・`mismatched_pixel_ratio=0.000239` で `CAPTURE_SCENE result=pass`、EXIT=0。`overview.Game.log` で `cluster_cull.comp` がソースからコンパイルされて読み込まれ、ERROR 0。
- Next: SCN-MEGA-GPU-SCENE-SCATTER。

### SCN-MEGA-GPU-SCENE-GROW（2026-10-12、反復 12・run 20261012-030203）

- Done:
  - `MegaGeometryPass::EnsureResidentTable` が常駐の表（席・描く順・影・メッシュ）を広げるとき、古い表の中身を新しい表の先頭へ `CopyBuffer` で写す記録を前段のコマンドへ積む（古い表 GenericRead→CopySource、新しい表 CopyDest→GenericRead）。古い表は従来どおり `m_RetiredBuffers` へ回す。表のバッファの用途に `TransferSrc`・`TransferDst` を足した。コピーしたときは写しの状態（席の行の状態 1・描く順と影の写しの行数・メッシュの写し）を引き継ぎ、行を送り直さない。コピーできなかった作り直し（初回）だけが従来の送り直しになる。`VRAM_LEDGER MegaGeometry_*` に `copied_bytes=` を足した。
  - メッシュの表が指す資源の寿命: `SceneTables::MeshOwners`（メッシュの表の行と同じ並びの `MegaMeshGPUData::RegionOwner`）を `BuildSceneTables` で集め、`UpdateResidentSceneTables` がフレームスロットの `MeshRegionOwners` へ写す。スロットは FrameUseRing で、その記録の提出の GPU の仕事が終わってから次に使われるので、頂点・インデックス・クラスタ・BVH（どれもメッシュの区画の中）は最後に使った提出が終わるまで空きへ戻らない（替えた古い持ち主の区画は GpuRetireQueue を通って戻る）。ページの表はフレームスロットごとのバッファで、外したページの区画は従来どおり GpuRetireQueue が提出の完了まで保つ。
  - 開発ビルドの検査: 表を広げてコピーした更新の後、常駐の 4 つの表（各先頭 16384 行まで）をスロットの読み戻しへコピーし、そのときの写しを覚えて、スロットを次に使うときに照合する（`MEGA_SCENE_GROW_CHECK ... mismatch=`）。食い違いは `MEGA_SCENE_UPLOAD` の `gpu_mismatch=` と `mismatch=` に足す。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-MEGA-GPU-SCENE-GROW-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `-2.txt`・`-2-log.txt`: mega-stress 5000 個・区画 64 m の撮影が `CAPTURE_SCENE result=pass`（mean_luminance 157.641。SCATTER の同じ撮影と同じ）、EXIT=0。描く順と影の表が 1024→2048→4096→8192 と GPU コピーで広がり（copied_bytes 147456・294912・589824 ほか）、`MEGA_SCENE_GROW_CHECK` 3 回とも `mismatch=0`、`MEGA_SCENE_UPLOAD` は `gpu_mismatch=0 mismatch=0`、区画の解放 2 回、ERROR 0。
  - `-3.txt`: mega-bananas・mega-stress の周回 3 周が `SCENE_CYCLE_SMOKE_RUN result=pass`、EXIT=0（Game.log の `MEGA_SCENE_UPLOAD` 12 件すべて `mismatch=0`、ERROR 0 を開いて確認。Game.log は後の補助の周回で上書き）。
  - `-4-slotgrow.txt`（補助）: 8150 個・区画の周期 0.2 秒で、区画の読み込み 4 回・解放 8 回の後も `MEGA_SCENE_GROW_CHECK` 2 回と `MEGA_SCENE_UPLOAD` が `mismatch=0`、ERROR 0（`-2-log.txt` の後半）。
  - `-5-slotgrow-cycle.txt`・`-6-slotgrow-log.txt`（補助）: mega-stress → mega-bananas の周回 3 周が pass、EXIT=0。席の表が 2048→16384 と GPU コピー（262144 B）で広がり、`MEGA_SCENE_GROW_CHECK` 4 回とも `mismatch=0`、ERROR 0。
- Next: 残りの todo の最優先。
- Notes: mega-stress は最初の更新で席がそろうので、ストリーミングだけでは席の表は広がらない（ECS が空いた席を使い回す）。席の表のコピーは、席の少ないシーンから多いシーンへ移る周回で確かめた。
- Notes: `GetReadyMegaMeshGPUData` が返すのはストアのエントリへの生のポインタで、GameThread の `ReleaseModel`（シーンを出るとき）と RenderThread の記録が重なると CPU 側で無効になりうる（GPU の区画は保たれる）。この項目では変えていない。
- Notes: 前の反復（SCATTER）は時間切れで「作業途中の保存」になり、`Assets/Shaders/mega_scene_scatter.comp` が追跡されていなかった。HEAD のコードが読むシェーダーなので、このコミットで追跡に加えた（中身は変えていない）。SCATTER の検証の記録は `verify-SCN-MEGA-GPU-SCENE-SCATTER-1〜5.txt`。

### SCN-MEGA-GPU-SCENE-SCATTER の再検証（2026-10-12、反復 13・run 20261012-030203）

- 反復 11 の実装（d44b9900。席ごと 128 B の device-local な席の表 `GPUMegaSlotRow`、変わった席だけを staging から `mega_scene_scatter.comp` で散在書き込み、席の表・描く順の表・メッシュの表の更新の条件を分け、ページの表は前段の `SyncPageTable` の版を主の描画・ID のラスタ・VSM が共有）を作り直さず、HEAD（GROW を含む）で verify を通し直した。
- Done:
  - `UpdateResidentSceneTables`: staging の容量を 2 の冪へ丸めるとき、2 GiB を超える大きさは 32 ビットで溢れて 64 KiB に戻っていたので、そのときは必要な大きさのまま取る。
  - シーンを出た後のメッシュの区画: GROW 以降、ランチャーへ戻っても前のシーンのメッシュの区画の持ち主（前のフレームの表の `m_SceneTables.MeshOwners` と組の `MeshRegionOwners`）が残り、ランチャーで `geometry_pool_used_bytes` が mega-stress 36274144・mega-bananas 988640 のままだった（SCATTER 時点は 0）。インスタンスの無いフレームで `ReleaseIdleMeshRegionOwners` が表の一覧を捨て、枠を選んで GPU の仕事が終わった組（`FrameUseRing::ForEachIdleInActiveSlot`。今のフレームに渡していない組だけ）の持ち主を手放す。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-MEGA-GPU-SCENE-SCATTER-14.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `-15.txt`: 起動画面 default の `SCN-MEGA-BASE-RWDI` との決定的な比較が `mean_luminance_difference=0`・`psnr_db=100`・`mismatched_pixel_ratio=1E-06` で `result=pass`、EXIT=0。PNG を開いて確認（家・球・岩・天球）。Game.log の ERROR 0。
  - `-16-fullsync.txt`: `--mega-scene-full-sync` の起動画面を `-15` の撮影と比べ、`mean_luminance_difference=0`・`psnr_db=100`・`mismatched_pixel_ratio=4E-06` で `result=pass`、EXIT=0。
  - `-17.txt`: mega-stress 5000 個（動く 1%・区画 64 m）の overview が `CAPTURE_SCENE result=pass`（mean_luminance 157.64）、EXIT=0。PNG を開いて確認。Game.log の ERROR 0、`MEGA_SCENE_UPLOAD` は最初 `rows=5000`、以後の窓は `rows=43`（動いた席だけ）・`mismatch=0`、`MEGA_SCENE_GROW_CHECK` 3 回とも `mismatch=0`。
  - `-18.txt`・`-18-Game.log`: mega-bananas・mega-stress の周回 3 周が `SCENE_CYCLE_SMOKE_RUN result=pass`、EXIT=0。ランチャーへ戻った後の `geometry_pool_used_bytes` は 6 回とも 0。`MEGA_SCENE_UPLOAD` 12 件すべて `mismatch=0`、インスタンスも常駐も変わらない更新は `bytes=0 rows=0 entries=0 ... page_bytes=0`（静止した mega-bananas の窓で 120 回中 113 回が 0）。ERROR 0。
- Next: 残りの todo の最優先（SCN-MEGA-MEASURE-UPLOAD で効果の数値を測る）。
- Notes: mega-stress の窓の最後の更新で `entries=4624` になるのは、区画の読み込みで描く順が並び替わったフレーム（窓の平均は 1 フレーム約 44 KB）。効果の数値は MEASURE-UPLOAD で見る。

### SCN-MEGA-GPU-SCENE-HISTORY（2026-10-12、反復 14・run 20261012-030203）

- Done:
  - `MegaGeometryMirror`: 差分が運ぶ前の変換（GameThread の直前のゲームのフレームの値）を使わず、`NoteDrawn` が毎回、最後に描いたフレームの変換（ObjectId ごと。無ければ今の変換）から決める。決め直すのは、最後に描いてから差分が当たった位置と、前に描いたとき前の変換と変換が違った位置（`m_Unsettled`）だけ。動いて止まった物体は、GameThread の揃える差分が破棄されても、次に描くフレームで 1 回だけ前の変換が今の変換に揃い、その席を変わった席として消費者へ知らせて版を進める（席の表の行が散在書き込みで直る）。TAA の有無に関わらない。フレーム限定の付け替えと戻し（`m_Rebased`・`SettleRebased`）は消した。
  - 全量の経路（`--mega-scene-full-sync`）も同じ規約にした: `RenderedObjectHistory` は描画が飛んだら TAA の有無に関わらず MegaGeometry の前の変換を最後に描いた変換にし、覚える鍵を ComponentId から ObjectId にした（散布の部品は 1 つの ComponentId が多数の物体を持つ）。GameThread の `ApplyPreviousObjectStates` の MegaGeometry の前の値も ObjectId で引く。
  - 開発ビルドの検査 `MEGA_HISTORY`: 16384 本以下は 4 回描くごと（それより多いと 60 回、262144 本を超える回は省く）に、1 つ前に描いたフレームの全部の proxy の変換を写し、次に描くときに全部の前の変換がその写しから全量で求めた値と一致するかを照合する。60 回描くごとに `MEGA_HISTORY draws= gap_draws= checked= instances= skipped= unsettled= mismatch=` を出し、食い違いは ERROR にする。
  - 検査 MEGA_SCENE_DELTA の決まった場面に「続けて描くフレームの前の変換が最後に描いた変換」「止まった印が破棄されたパケットにあり空の差分で描いても 1 回だけ揃い、席が変わった席に入る」「揃ったあとは動かさない」を足した。写しと GameThread の配列の照合は、前の変換を除いて比べる（`HashMegaGeometryContent` と同じ）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-MEGA-GPU-SCENE-HISTORY-6.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0（最終のコード）。
  - `-9.txt`: mega-stress 5000 個（動く 1%・区画 64 m）の overview が `CAPTURE_SCENE result=pass`（mean_luminance 157.641。SCATTER・GROW と同じ）、EXIT=0。PNG を開いて確認。`MEGA_HISTORY draws=120 gap_draws=0 checked=30 instances=142718 skipped=0 unsettled=45 mismatch=0`、Game.log の ERROR 0、`MEGA_SCENE_UPLOAD rows=43 ... gpu_mismatch=0 mismatch=0`。
  - `-8.txt`: 起動画面 default の `SCN-MEGA-BASE-RWDI` との決定的な比較が `mean_luminance_difference=0`・`psnr_db=100`・`mismatched_pixel_ratio=8E-06` で `result=pass`、EXIT=0。PNG を開いて確認（家・球・岩・天球）。
  - `-7-gap.txt`（補助）: 5 万個・`--engine-checks=MEGA_SCENE_DELTA` で `result=pass`、EXIT=0。`ENGINE_CHECK name=MEGA_SCENE_DELTA result=pass` 143 件・fail 0、`MEGA_HISTORY ... unsettled=497 mismatch=0`、ERROR 0。
- Next: 残りの todo の最優先。
- Notes: 撮影ではゲームと描画が 1:1 で進み `gap_draws=0`（パケットの破棄・描画の見送りが起きない）。飛んだフレームの扱いは検査 MEGA_SCENE_DELTA の決まった場面（描かなかった差分・止まった印の破棄・位置を移った物体・外れて戻った物体）で確かめた。描画を意図して飛ばす起動引数は無い。
- Notes: `-5-gap.txt` は前の変換まで含めて写しと GameThread の配列を比べていた決まった場面の照合が落ちた記録（前の変換を RenderThread が決めるようにした結果。照合から前の変換を除いて `-7` で pass）。
- Notes: GameThread の `SceneView::BeginMegaGeometrySync`（止まった物体の前の変換を揃える差分）は全量の経路のために残した。差分の同期では写しが前の変換を決め直すので、この差分は席の内容を変えない（送る量の削減は別の項目）。`WorldSyncDifferentialTest` の写しの 2 つの場面は新しい規約でも同じ値を期待している（Test は paths の外なので走らせていない）。

### SCN-MEGA-RT-SELECT（2026-10-12、反復 15・run 20261012-030203）

- Done:
  - `RayTracingSceneSubsystem` が RenderThread で MegaGeometry の席（SceneSlot）ごとの配列 `m_MegaSlots`（境界の球・メッシュ・使えるか。1 席 32 B で、変換は写しの proxy を読む）を持ち、写しが前に見た版から数えた変わった席（`CanPatchFrom`・`GetChangedSlots`）だけを直す。直せないとき（全部が変わった・世代や写しが違う）だけ作り直す。席を持たない proxy は作り直すときに写しを集める（変わると写しが全部が変わった扱いにする）。
  - TLAS に入れる規則: メインカメラの位置から境界の球の表面までの距離が 120 m 以内の MegaGeometry を、近い順（同じ距離は席の番号の小さい方）に、TLAS の合計（MegaGeometry 以外の描画のインスタンスを含む）が 65,536 本に収まるまで。入れる順は席の番号の昇順（決定的）。選んだ席だけ写しの proxy（前の変換は NoteDrawn が最後に描いたフレームから決めた値）からインスタンスを作り、メッシュの GPU データはフレームの中で同じメッシュを 1 回だけ引く。最初と 120 回ごとに `RT_SCENE instances= excluded= path= mega= candidates= out_of_range= over_limit= not_ready= radius_m=120 limit=65536 patched_slots= rebuilt=` を出す。
  - RT の入力の切替: 差分の同期では、連番のパストレーシングも含めて MegaGeometry を RenderThread の写しから足す（GameThread の `SceneView` の配列は読まない）。連番の前の値の持ち越しは、MegaGeometry を足した後に RenderThread の状態で `ApplyPathTracingSequenceCarry` を当てる（連番の1フレームの中ではどのパケットも同じ状態なので、描いたパケットの状態を前の値にする）。全量の同期（`--mega-scene-full-sync`）は GameThread の全量の配列のまま、同じ規則で選ぶ（`path=full`）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-MEGA-RT-SELECT-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `-2.txt`: 起動画面 default の `SCN-MEGA-BASE-RWDI` との決定的な比較が `mean_luminance_difference=0`・`psnr_db=100`・`mismatched_pixel_ratio=9E-06`（`indirect_lighting=rtgi`）で `result=pass`、EXIT=0。PNG を開いて確認（家・球・岩・天球・影）。Game.log の ERROR 0、`RT_SCENE instances=26 excluded=0 path=mirror mega=6 ... patched_slots=1 rebuilt=0`。
  - `-3.txt`: mega-stress 5000 個（動く 1%・区画 64 m）の overview が `CAPTURE_SCENE result=pass`（mean_luminance 157.614。前の項目は 157.641 で、遠くのインスタンスを TLAS から外した RT の影・GI の差）、EXIT=0。PNG を開いて確認。Game.log の ERROR 0、`RT_SCENE instances=728 excluded=4110 path=mirror mega=727 candidates=4837 out_of_range=4110 over_limit=0 not_ready=0 ... patched_slots=45 rebuilt=0`、`MEGA_HISTORY ... mismatch=0`、`MEGA_SCENE_UPLOAD ... mismatch=0`。
- Next: SCN-MEGA-RT-SKIP（TLAS の更新を、集合・変換・BLAS の版が変わったフレームだけにする）。
- Notes: 規則が外すのは MegaGeometry（席）だけ。描画コマンドから作る Mesh のインスタンスは境界を持たないので距離では外さず、上限の枠を先に使う（Mesh だけで 65,536 を超えても切らない）。
- Notes: 距離は境界の球の表面まで（大きな物体の中心が遠くても、表面が 120 m 以内なら入れる）。メインカメラが無いパケットは距離で外さず上限だけで選ぶ。
- Notes: `FramePacket::DefersMegaRayTracingToRenderThread`（連番を GameThread に残す判定）は使わなくなった。FramePacket.h は paths の外なので消していない。全量の同期の経路（`path=full`）は起動して確かめていない（verify に無い）。
- Notes: 連番の持ち越し（`MakeKeys`・`Find`）はインスタンス数の 2 乗で、上限の 65,536 本近くを連番で撮ると RenderThread が重い（以前は GameThread で全量に対して同じ計算をしていた）。

### SCN-MEGA-RT-SELECT 差し戻しの対応（2026-10-12、反復 16・run 20261012-030203）

- Done:
  - TLAS に入れる規則を、描画コマンドから作るメッシュのインスタンスにも当てる。メッシュの境界の球は、メッシュのローカルの AABB（`MeshResources::TryGetLocalBounds`。同じメッシュが続く間は使い回す）をインスタンスの変換で写した AABB を囲む球（AABB が分からないメッシュは並進と半径 0）。メッシュと MegaGeometry を同じ候補の列（番号はメッシュがパケットの並び、MegaGeometry がその後ろ）に入れて、120 m 以内を近い順に合計 65,536 本まで選び、外れたメッシュのインスタンスはパケットから除いて詰め、`customIndex` を振り直す。差分の同期は RenderThread（`AppendMegaGeometryInstancesFromMirror` に `MeshResources` を渡す）、全量の同期は GameThread（`AppendMegaGeometryInstances`）で選ぶ。`RT_SCENE` に `mesh=` を足し、`excluded`・`candidates` はメッシュを含む数にした。
  - 席ごとの配列 `MegaSlotEntry` が TLAS のインスタンスの入力（境界・3x4 の変換・前の変換・マスク・物体 ID・メッシュのハンドル）を持ち、変わった席（前の変換の決め直しも写しが変わった席として知らせる）だけ写しの proxy から作り直す。変わらない席は写しを読まず、席の入力とメッシュの GPU データ（頂点・インデックス・材質。GPU への書き込みの完了で変わるのでフレームごとに同じメッシュは 1 回だけ引く）からインスタンスを足す。席を持たない proxy も同じ入力の形で作り直すときだけ集める。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-MEGA-RT-SELECT-4.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `-5.txt`: 起動画面 default の `SCN-MEGA-BASE-RWDI` との決定的な比較が `mean_luminance_difference=0`・`psnr_db=100`・`mismatched_pixel_ratio=9E-06`（`indirect_lighting=rtgi`）で `result=pass`、EXIT=0。PNG を開いて確認（家・球・岩・天球・影）。Game.log の ERROR 0、`RT_SCENE instances=26 excluded=0 path=mirror mesh=20 mega=6 candidates=26 ... patched_slots=1 rebuilt=0`。
  - `-6.txt`: mega-stress 5000 個（動く 1%・区画 64 m）の overview が `CAPTURE_SCENE result=pass`（mean_luminance 157.614。前の反復と同じ）、EXIT=0。PNG を開いて確認。Game.log の ERROR 0、`RT_SCENE instances=728 excluded=4110 path=mirror mesh=1 mega=727 candidates=4838 out_of_range=4110 over_limit=0 not_ready=0 ... patched_slots=45 rebuilt=0`、`MEGA_HISTORY ... mismatch=0`、`MEGA_SCENE_UPLOAD ... mismatch=0`。
- Next: SCN-MEGA-RT-SKIP（TLAS の更新を、集合・変換・BLAS の版が変わったフレームだけにする）。
- Notes: 反復 15 の Notes の「規則が外すのは MegaGeometry だけ（Mesh は上限の枠を先に使う）」は廃止。今はメッシュも距離と上限で外す。
- Notes: `BuildFrameSnapshot` を MegaGeometry のリソース無しで呼ぶと規則を当てない（差分の同期では RenderThread が当てる。メッシュだけで呼ぶ既存のテストの挙動は変えていない）。
- Notes: 席の入力は 1 席あたり約 130 B（変換 2 つを持つため。反復 15 の 32 B から増えた）。
- Notes: メッシュの距離と上限を直接確かめるテスト（65,536 本を超えるメッシュ、120 m の外のメッシュ）は書いていない（verify に無い）。

### SCN-MEGA-RT-SKIP（2026-10-12、反復 17・run 20261012-030203）

- Done:
  - `RayTracingSceneSubsystem::BuildAccelerationStructures`: frame slot ごとの TLAS の記録（`TopLevelCacheEntry::Instances`）に、最後に Build / Update を記録したインスタンスの入力（選んだ集合と並び・3x4 の変換・マスク・customIndex・SBT の位置・カリングの印・BLAS の参照）を覚える。次にその slot で描くフレームの入力が全部一致すれば Update を記録せず、slot の TLAS をそのまま使う。変換はビットで比べ、BLAS は参照で比べる（記録が BLAS の参照を持つので、準備完了で増えた・交換で作り直した BLAS は別の参照になり一致しない）。カメラの移動で 120 m 内の集合が変わる・追加削除・並びの変化・BLAS の交換では入力が変わるので Update（数が違えば Build）を記録する。Update に渡す配列はコピーせず移して覚える。Build に失敗した slot は覚えた入力を捨てる。
  - `RT_SCENE update_skipped= updated= built= frames= instances=` を最初と 120 フレームごと、出していない残りを `Shutdown` で出す（破棄では出さない）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-MEGA-RT-SKIP-3.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0（最終のコード。`-1.txt` は終了時のログを足す前のビルドで EXIT=0）。
  - `-4.txt`: mega-bananas 1 万本の overview を `SCN-MEGA-BASE` と比べ、`mean_luminance_difference=0`・`psnr_db=65.362`・`mismatched_pixel_ratio=0.000239` で `result=pass`、EXIT=0。PNG を開いて確認（バナナ・山・影・床・空）。`-4-Game.log` の ERROR 0、`RT_SCENE update_skipped=84 updated=0 built=9 frames=93 instances=10001`（読み込みで集合が変わった間は記録し、静止した後は 3 つの slot とも省いた）。`-2.txt` は同じ比較の終了時のログを足す前の版（pass）。
- Next: 残りの todo の最優先（SCN-MEGA-MEASURE-UPLOAD で効果の数値を測る）。
- Notes: 比較は O(インスタンス数) で毎フレーム行う（選んだインスタンスの入力を作るのと同じ桁）。席の版から一致を決める方式にしなかったのは、描画コマンドから作るメッシュのインスタンスが版を持たないため。
- Notes: 動く物体のある mega-stress・カメラが動く場面で Update を記録することは、この項目の verify に無いので起動して確かめていない（入力が変わる限り一致しないので記録される）。

### SCN-MEGA-SHADOW-DIRTY（2026-10-12、反復 18・run 20261012-030203）

- Done:
  - VSM: MegaGeometry の動きの記録（`VirtualShadowMapPass.cpp` の投影物の収集）を、全インスタンスの鍵・署名を毎フレーム前フレームと比べる方式から、MegaGeometryPass が積んだ箱を `PlanInvalidation` で取り出す方式にした（`MegaGeometryPass::TakeShadowInvalidation`）。積む数は変わった席の数に比例する。動いた席は、前に影の表へ載せた境界と今の境界を合わせた箱 1 つ。スキニング・手続きメッシュの動きの記録（`CasterMotionTracker`）と、光源・クリップマップ側の無効化はそのまま。
  - MegaGeometryPass: 影の表へ載せたインスタンスの記録（境界の球・投影物か）を `m_Instances` と同じ並びで保つ。変わった席を直すとき（`PatchInstancesFromChangedSlots`）に、変換・メッシュ・影の設定・境界が変わった位置だけを覚え、次に表を作るときその位置だけ旧・新の境界を積む。作り直した（並びが変わった）後の最初の表だけ、前の並びを鍵で引いて全部を比べる（区画の出入り・全量の同期）。インスタンスが無くなったフレームは前の投影物の境界を積む。
  - 静止した席の通知: 表へ載せるメッシュごとに、区画（頂点・インデックス・クラスタのバッファと先頭）・クラスタ数・ページの範囲・境界と、ページを読み込むメッシュはページの範囲の常駐の区画の署名を覚え、変わったメッシュ（初めて載る・載らなくなって戻ったものを含む）を使う席の境界を積む。常駐の署名はページの表の版が変わったフレームだけ作り直す。これで、以前の署名に入っていた `PageTableVersion`（どのページが変わっても全部の MegaGeometry を描き直していた）を、常駐が変わったメッシュの席だけの描き直しにした。`MegaGeometryShadowCasterInputs` から `Motions`・`PageTableVersion` を外した。
  - 影の表の毎フレームの FNV（インスタンスごとの行列の署名）をやめた。前の並びの鍵の FNV は作り直した後の最初の表だけ。
  - CSM（ShadowMapPass）: MegaGeometry の投影物を `ShadowMapMegaCasterCache`（RenderThread の写しの席ごと）に保ち、写しが数えた変わった席だけを直す（直せない・全量の同期のときだけ作り直す）。CSM の深度範囲へ含める境界は、新しい `ShadowCasterRangeSet`（`Library/Core/Public/Rendering/ShadowCasterRanges.h`、実装は `DirectionalShadowLightMatrices.cpp`）の範囲（16 m の格子）ごとの AABB から作る（物が 1 つの範囲はその物の球そのもの、2 つ以上は AABB の中心から全部を含む半径）。変わった席の範囲だけ置き直し、外した・動いた範囲は引くときに残りの物から作り直す。描く一覧は CSM が MegaGeometry を描くフレーム（VSM が太陽を受け持たないとき）と点光源のキューブのときだけ席ごとの写しから作る。
  - VSM のパスが無効化を取り出せるよう、`VirtualShadowMapPass::SetMegaGeometryPass` を非 const のポインタにした（呼び出し側はどれも非 const を渡している）。
  - 開発用のログ `MEGA_SHADOW_DIRTY builds= rebuilds= dirty_slots= mesh_changes= residency_changes= boxes= invalidate_all= instances=`（表を作った 120 回ごと）。
- 検証（`.harness/runs/20261012-030203/`。最終のコードは `-6`〜`-8`）:
  - `verify-SCN-MEGA-SHADOW-DIRTY-6.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0、警告 0（`-1.txt` は内訳のログを足す前のビルドで EXIT=0）。
  - `-7.txt`: 起動画面 default の `SCN-MEGA-BASE-RWDI` との決定的な比較が `mean_luminance_difference=0`・`psnr_db=100`・`mismatched_pixel_ratio=8E-06` で `result=pass`、EXIT=0。PNG を開いて確認（家・球・岩・天球・影）。Game.log の ERROR 0。CSM は MegaGeometry 6 本を描いている（VSM が太陽を受け持たない構成。範囲の境界にしても画素が一致）。`MEGA_SHADOW_DIRTY builds=120 rebuilds=1 dirty_slots=12 mesh_changes=59 residency_changes=53 boxes=71`。`-2.txt` は内訳のログを足す前の同じ比較（pass、psnr 100）。
  - `-8.txt`: mega-stress 5000 個（動く 1%・区画 64 m）の overview が `CAPTURE_SCENE result=pass`（mean_luminance 157.614。RT-SELECT 以降と同じ）、EXIT=0。PNG を開いて確認。Game.log の ERROR 0、`CSM_MEGA_SKIPPED skipped=1`、`MEGA_HISTORY ... mismatch=0`、`MEGA_SCENE_UPLOAD ... mismatch=0`。読み込み中の窓は常駐の変化（`residency_changes=138`）で `boxes=58224`。`-3.txt` は内訳のログを足す前の同じ撮影（pass、157.614）。
  - `-4-long.txt`（補助）: 同じ負荷を約 600 フレーム（`-GpuTimingFrames 480`）。読み込み後の窓は `dirty_slots` 4997〜5714・`boxes` 5329〜6200（120 フレームで。1 フレーム約 45 席・約 50 箱。差は区画の出入りの作り直しで出入りした物）・`mesh_changes` 0〜16・`invalidate_all=0`、ERROR 0。
  - `-5-fullsync.txt`（補助）: `--mega-scene-full-sync` の起動画面が `SCN-MEGA-BASE-RWDI` と `psnr_db=100` で pass。毎フレーム作り直し（`rebuilds=119`）でも積む箱は差分の経路と同じ 71、ERROR 0。
- Next: 残りの todo の最優先（SCN-MEGA-MEASURE-UPLOAD で効果の数値を測る）。
- Notes: 範囲の格子は 16 m（`ShadowCasterRangeSet::RangeSizeMeters`）。物が 2 つ以上の範囲の球は物ごとの球より大きいので、CSM が MegaGeometry を多く描く構成では深度範囲が少し広がりうる（起動画面は画素が一致した）。
- Notes: 積む箱の上限は 4096（超えたら全ページ）。VSM は矩形が 256 を超えると全ページにするので、1 フレームに 256 を超える席が動く負荷（5 万個の 1% など）では全ページの描き直しになる。数値は MEASURE-UPLOAD で見る。
- Notes: VSM が投影物を集めなかったフレーム（深度・クリップマップが無い）は、他の投影物の記録と同じく積まれた MegaGeometry の無効化も捨てる。
- Notes: 表の作り直し（区画の出入り）のフレームは前の並びを鍵で引くので O(本数)。読み込み後の mega-stress では 120 フレームに 1〜2 回。
- Notes: `DirectionalShadowLightMatrices` の `CollectDirectionalShadowCasterBounds`・`FitDirectionalShadowMatrixSettingsToCasters` は MegaGeometry の proxy の配列を受ける形のまま（既存のテストが物ごとの球の値を確かめている。製品の経路は ShadowMapPass が範囲の境界を足す）。範囲の集合を直接確かめるテストは書いていない（verify に無い）。

### SCN-MEGA-MEASURE-UPLOAD（2026-10-12、反復 19・run 20261012-030203）

- 計測だけ（コードは変えていない）。RelWithDebInfo・1920×1080・300 フレーム。Game.exe（08:36）は HEAD 2a29301b のソースより後のビルド（verify 1 で up-to-date）。撮影は `.harness/runs/scene-capture/SCN-MEGA-MEASURE-UPLOAD/<負荷>/`（metrics.json・Game.log・trace.csv）、撮影の出力は `.harness/runs/20261012-030203/capture-SCN-MEGA-MEASURE-UPLOAD-<負荷>.txt`。
- 負荷: MEASURE-CPU と同じ 5万個の静止（default・close・aerial）・1% 移動、1% 移動 + 64 m 区画（5万個は前回と同じく VRAM 15,645 MB > 予算 12,307 MB でバッファの確保に失敗して落ちたので 3万個）、バナナ 3万本 overview。補助に 5万個の 0.2% 移動（矩形の上限 256 未満の側で比例を見るため）、カメラパスの再生（`camera-path-play`）をバナナ 3万本 overview と 10万本 horizon。
- 集計窓はどの値も metrics.json の `window_first_frame`〜`window_last_frame`（読み込みと 60 フレームの捨て期間の後の、描画 300 フレーム）。`RecordFrameCommand` は metrics.json の `MegaGeometryPass.RecordFrameCommand`（trace の RenderThread の区間を描画フレームごとに 1 個ずつ 300 個集め、median は中央値、p95 は昇順に並べた `int(0.95 × (n − 1))` 番目の値）。upload は `MEGA_SCENE_UPLOAD` の窓（120 回の更新ごとに 1 行）のうち集計窓と重なる窓の `window_bytes ÷ updates`（窓ごとの平均。括弧は瞬間値 `rows` の範囲）。VSM の無効化は `VSM_CACHE invalidated`（60 フレームごとの 1 フレームの値）の読み込み後の範囲。

| 値（median / p95） | 5万個 静止 | 5万個 1% 移動 | 5万個 0.2% 移動（補助） | 3万個 1% 移動 + 64 m 区画 | バナナ 3万本 overview | バナナ 3万本 パス再生 | バナナ 10万本 パス再生 |
|---|---|---|---|---|---|---|---|
| gt ms | 0.162 / 0.248 | 1.722 / 22.64 | 0.457 / 20.95 | 0.989 / 12.65 | 0.158 / 0.287 | 0.164 / 0.272 | 0.176 / 0.342 |
| rt ms | 56.0 / 59.0 | 60.4 / 63.8 | 57.5 / 60.5 | 42.1 / 57.3 | 41.4 / 46.8 | 45.0 / 50.6 | 120.2 / 144.8 |
| GPU ms | 42.2 / 43.7 | 46.0 / 47.3 | 43.3 / 44.7 | 31.2 / 34.8 | 18.5 / 20.3 | 22.1 / 24.9 | 50.7 / 60.0 |
| `MegaGeometryPass.RecordFrameCommand` ms | **0.960** / 1.138 | **0.898** / 1.122 | 0.896 / 1.115 | 0.619 / **6.294** | 0.565 / 0.715 | 0.564 / 0.700 | 1.659 / 2.132 |
| `MEGA_SCENE_UPLOAD` 窓平均 bytes / 更新（変わった行） | **0**（窓 120 回すべて 0） | 120,032（500） | 24,032（100） | 72,217・73,179・72,505（3 窓の平均 72,634。行は 297〜300） | 0 | 0 | 0 |
| `AccelerationStructureBuild`（GPU）ms | **0.000 / 0.000** | 0.025 / 0.026 | 0.024 / 0.025 | 0.022 / 0.103 | 0.000 / 0.000 | 0.000 / 0.000 | 0.789 / 1.365 |
| TLAS（`RT_SCENE`、120 フレームあたり） | 省略 120 | Update 120（890 本） | Update 120（890 本） | Update 105・Build 15 | 省略 120（30,001 本） | 省略 120（30,001 本、120 m 外 0） | Update 120（65,536 本、上限外 34,465） |
| VSM の無効化（ページ / 全ページ） | 0 / 743 | **741〜744 / 743（全部）** | 290〜343 / 743 | **679〜783（全部）** | 0 / 1,211 | 0 / 1,310 | 0 / 1,309 |
| `MEGA_SHADOW_DIRTY` 箱 / フレーム | 0 | 500 | 100 | 約 304 | 0 | 0 | 0 |
| `VirtualShadowMapPass`（GPU）ms | 0.203 / 0.780 | 3.042 / 4.938 | 0.944 / 2.084 | 2.223 / 4.294 | 0.376 / 0.927 | 0.743 / 1.554 | 1.940 / 4.748 |

- 静止の close・aerial: `RecordFrameCommand` 0.909 / 1.108・0.897 / 1.040 ms、upload 0、AS 0、TLAS は全部省略、無効化 0。gt 0.161 / 0.265・0.155 / 0.244、rt 46.7 / 49.3・55.1 / 58.1、GPU 33.3 / 34.3・42.1 / 43.8 ms。
- 合否:
  - 5万個で `RecordFrameCommand` ≤ 1 ms: median は**合格**（静止 0.90〜0.96・1% 移動 0.90 ms。前の節目では 1 回あたり約 16 ms）。p95 は 1.04〜1.14 ms で 1 ms をわずかに超え、3万個 + 64 m 区画では区画の出入りのフレームで跳ねる（300 回中 26 回が 3 ms 超・最大 8.4 ms、p95 6.3 ms）→ **SCN-MEGA-FIX-RECORD-COMMAND** を足した。
  - 静止で upload 0: **合格**（読み込み後の窓は 3 視点ともすべて `zero_updates=120`）。
  - 静止で `AccelerationStructureBuild` ≤ 0.05 ms: **合格**（0.000 ms。TLAS の Update は全フレーム省略）。
  - 1% 移動で upload が移動数に比例: **合格**。1 行 240 B（100 行 24,032 B・500 行 120,032 B の窓平均はどの窓も同じ値、`page_bytes=0`）。3万個 + 64 m 区画（約 300 行）は窓平均 72,217〜73,179 B / 更新で、瞬間値（297〜300 行で 71,312〜72,032 B）より 0.3〜1.6% 多い（窓の中に行の多い更新が混じる。同じ時間の `MEGA_SHADOW_DIRTY` で instances が 29,505〜30,000 と区画の出入りで変わるが、更新ごとの内訳はこのログでは分からない）。比例の判断は変わらない。
  - 1% 移動で VSM の無効化が移動数に比例: **不合格**。積む箱は移動数どおり（100・500）だが、矩形が上限 256 を超える 1%（500 席）・3万個の 1%（約 300 席）は全ページを描き直す（0.2% の 100 席は 290〜343 ページで部分）→ **SCN-MEGA-FIX-VSM-INVALIDATE** を足した。
  - カメラパスの再生中に TLAS の更新があるフレームの p95: バナナ 10万本 horizon（120 m 内の近い順 65,536 本の集合がカメラの移動で変わり、300 フレームすべて Update）で `AccelerationStructureBuild` 0.790 / **1.365 ms**、GPU のフレーム 50.7 / **60.0 ms**、rt 120.2 / **144.8 ms**。バナナ 3万本は全部が 120 m 内で集合が変わらず、パス再生中も Update 0（全部省略）。
  - 1% 移動の連続撮影（`move1-sequence/f120〜f124・f130.png`。Game を `--capture-sequence` で直接起動、EXIT=0）を開いた: 左端の岩（f123→f130 で右へ入る）と中央の橙の岩（左へ動く）は、どのフレームも輪郭がくっきりして二重像・尾を引く跡は無い。地面の影も一緒に動く。残像は増えていない。
  - 届かないもの: 上の 2 件を FIX として足した。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-MEGA-MEASURE-UPLOAD-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `-2.txt`: 5万個の静止の metrics.json に `CHECK_CAPTURE_METRICS result=pass views=default,close,aerial`、EXIT=0。
  - `-3.txt`: Debug の Game のビルド EXIT=0、error 0。
  - `-4.txt`: Debug の起動画面 default が `CAPTURE_STARTUP_SCENE result=pass`、EXIT=0。`vulkan_validation` の error_count=0・vuid_count=0、Game.log の ERROR 0。PNG を開いて確認（家・球・岩・天球・影）。
  - 撮影の Game.log の ERROR は全負荷 0（落ちた 5万個 + 64 m 区画を除く）。`MEGA_SCENE_UPLOAD ... mismatch=0`。
- Next: SCN-MEGA-FIX-VSM-INVALIDATE、SCN-MEGA-FIX-RECORD-COMMAND。
- Notes: 撮影中に見つけた前からの欠陥: mega-stress の既定の視点で、近くの岩が近平面で切れて内側（黒い面と半透明の膜）が下端中央に見える。静止・前の節目の撮影にも同じ量であり、移動とは無関係。SCN-MEGA-FIX-STRESS-NEAR-CLIP として足した。
- Notes: 動く負荷の gt の p95（1% 20〜23 ms）は、描画フレームの行のゲームスレッド時間だけが跳ねる（同じ窓の全ゲームフレーム 9,757 行では median 1.65・p95 2.28 ms）。Ecs の区間の p95 はどれも 1 ms 未満で、跳ねの内訳はこの計測では分からない（静止では跳ねない）。
- Notes: RenderThread の `MegaGeometryPass.BuildSceneTables` が静止でも 7.1 ms / フレーム（`PrepareSceneTables` 7.7 ms）で、rt の大きな部分。upload が 0 でも CPU で表を作る処理は本数に比例して残る。目標の外なので FIX は足していない（節目の判断で扱う）。
- Notes: 3万個 + 64 m 区画の 1 フレームの値は、区画の出入りの作り直し（`rebuilds=5`／120）で上下する（upload の最初の窓 250 KB / 更新など）。
- 反復 20（差し戻しへの対応、撮り直しなし）: 3万個 + 64 m 区画の upload を、瞬間値の範囲から集計窓（metrics.json の `window_first_frame` 60562〜`window_last_frame` 72313 = 08:54:41.2〜54.2）と重なる 3 窓の `window_bytes ÷ updates`（72,217.2・73,178.8・72,504.8 B）へ直した。`RecordFrameCommand` の値は metrics.json の算出方法（p95 は昇順の `int(0.95 × (n − 1))` 番目）へ揃えた（前の表は線形補間で、区画ありの p95 が 6.313 → 6.294 ms。他の列の差は 0.02 ms 未満で合否は同じ）。
  - 検証: `verify-SCN-MEGA-MEASURE-UPLOAD-5.txt`（RelWithDebInfo のビルド EXIT=0）・`-6.txt`（`CHECK_CAPTURE_METRICS result=pass views=default,close,aerial`、EXIT=0）・`-7.txt`（Debug のビルド EXIT=0）・`-8.txt`（`CAPTURE_STARTUP_SCENE result=pass`、EXIT=0。`vulkan_validation` error_count=0・vuid_count=0、Game.log の ERROR 0、PNG を開いて家・球・岩・影を確認）。

### SCN-MEGA-FIX-VSM-INVALIDATE（2026-10-12、反復 21・run 20261012-030203）

- 実装: 無効化の矩形・球を VsmParams（UBO、各 256 個）から storage buffer（`vsm_allocate.comp` の binding 10 `VsmInvalidations`。先頭から矩形 `cache.z` 個、続いて球 `cache.w` 個）へ移し、`VirtualShadowMap::MAX_INVALIDATION_RECTS` を 256 → 4096（MegaGeometry が積む箱の上限 `ShadowInvalidationMaxBoxes` と同じ）にした。バッファは `VirtualShadowMapPages` の `FrameUseRing<InvalidationUse>`（Record ごとに 1 本、128 KB、割り当ての全段階のディスクリプタが同じものを指す）。矩形は併合せず、動いた席の旧・新の境界のライト空間の矩形をそのまま渡す（多く無効にする方向の余白 1 mm は従来どおり）。4096 を超えたら全ページにする規則は保った。`GPUVsmParams` は 8,448 B → 256 B になり、割り当ての 12 段階ぶんの UBO の書き込みも減った。
- 結果（5万個・1920×1080・300 フレーム。撮影は `.harness/runs/scene-capture/SCN-MEGA-FIX-VSM-INVALIDATE/<move1|move0.2>/`）:

| 値 | 1% 移動（500 席）前 | 1% 移動（500 席）後 | 0.2% 移動（100 席）前 | 0.2% 移動（100 席）後 |
|---|---|---|---|---|
| `VSM_CACHE invalidated`（読み込み後の 10 回の範囲 / 平均） | 741〜744（全 743） | **299〜350 / 323.7**（全 742〜743） | 290〜343 | 290〜343 / 317.1 |
| `VirtualShadowMapPass`（GPU）median / p95 ms | 3.042 / 4.938 | **0.958 / 2.095** | 0.944 / 2.084 | 0.940 / 2.067 |
| GPU のフレーム median / p95 ms | 46.0 / 47.3 | 43.5 / 46.1 | 43.3 / 44.7 | 43.2 / 44.7 |

- 比: 移動数は 5 倍だが無効化のページは 1.02 倍（323.7 / 317.1）。動く席は番号のハッシュの小さい順に選ぶ（`MegaStressRoutine.cpp`）ので 100 席は 500 席の部分集合で、無効化のページの大半は両方に共通の、カメラの近く（影の範囲 80 m の内側の細かい段）にいる席が占める。増えた 400 席は大半が割り当て済みのページ（影の範囲の中だけ）の外を動くので、加わるページは少ない。無効化は動いた範囲（席の境界が覆う割り当て済みのページの和）に沿っており、全ページの描き直しは無くなった。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-MEGA-FIX-VSM-INVALIDATE-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0、警告 0。
  - `-2.txt`: 起動画面 default の `SCN-MEGA-BASE-RWDI` との決定的な比較が `mean_luminance_difference=0`・`psnr_db=100`・`mismatched_pixel_ratio=5E-06` で `result=pass`、EXIT=0。PNG を開いて確認（家・球・岩・天球・影）。Game.log の ERROR 0、`vsm_allocate.comp` のコンパイル成功、`VSM_PAGES ... point_requested=178`（点光源の球の経路も通る）。
  - `-3.txt`: mega-stress 5万個・1% 移動が `CAPTURE_SCENE result=pass`、EXIT=0。PNG を開いて確認（影は崩れていない。下端中央の近平面の切れは既知の SCN-MEGA-FIX-STRESS-NEAR-CLIP）。Game.log の ERROR 0、`MEGA_SHADOW_DIRTY ... invalidate_all=0`。
  - 補助: `capture-SCN-MEGA-FIX-VSM-INVALIDATE-move0.2.txt`（0.2% 移動、pass、ERROR 0）。`test-SCN-MEGA-FIX-VSM-INVALIDATE-vsm-vulkan.txt`: 既存の `RHITextureUpdateVulkanTest --test=VirtualShadowMapVulkanTest`（矩形・球の無効化の dirty の集合を CPU の参照と比べるキャッシュのケースを含む）が `RESULT=PASS`・`VUID_COUNT=0`、EXIT=0（ビルドは `-vsm-vulkan-build.txt`）。
- Next: SCN-MEGA-FIX-RECORD-COMMAND。
- Notes: 4096 を超える無効化（MegaGeometry の箱の上限 4096 に、MegaGeometry 以外の動いた投影物が加わるフレーム）は従来どおり全ページ。併合での上限の回避は入れていない。
- Notes: 256 を超える矩形・球の経路を直接確かめるテストは書いていない（verify に無い。既存の GPU テストは 256 以下の数）。

### SCN-MEGA-FIX-RECORD-COMMAND（2026-10-12、反復 23・run 20261012-030203）

- 内訳（先に `RecordFrameCommand` の中へ `MegaGeometryPass.Record.*` の区間を 15 個足して撮った。3万個・1% 移動・64 m 区画、RelWithDebInfo・1920×1080・300 フレーム。`.harness/runs/scene-capture/SCN-MEGA-FIX-RECORD-COMMAND/breakdown-before/`）:
  - 3 ms 超の 25 回（最大 8.38 ms）は 2 種類だけ。**区画の出入りのフレーム 12 回**: `UpdateVisibilityLayout`（見えたビットのバッファ）が 6.4〜7.7 ms。インスタンスの並びが変わると区画を詰め直すため、約 760 MB（`MEGA_CAPACITY visibility_bytes=752〜770 MB`、1 クラスタ 4 B × 約 1.9 億）のバッファを作り直し、0 で埋め、3 万個の区画を 1 個ずつコピーしていた。**その 8 フレーム後の 13 回**: `ReleaseStaleBuffers` が 4.5〜6.5 ms（手放した古い 760 MB のバッファの破棄）。
  - 1 ms をわずかに超える残りの 10 回（1.00〜1.35 ms）は、毎フレームの `UpdateVisibilityLayout`（配置の比較と 3 万個の写しで 0.35〜0.68 ms）が最大の部分。他の区間はカリングの記録 0.09・ID のラスタ 0.07・区間のディスクリプタ 0.07 ms（median）など小さい。
- 実装: 見えたビットの区画を席（`TableSlot`）ごとに持ち（`VisibilityRegion`）、席が同じクラスタ数で続く間は位置を動かさない。区画は表を作るとき（`BuildSceneTables` → `AssignVisibilityRegion`）に決めて `GPUMegaEntry::VisibleOffset` に書く。クラスタ数が替わった席は区画を手放し（クラスタ数ごとの空き区画へ）、空きから取るか末尾から切り出す。末尾が容量を超えるときは、前の表にも載らなかった席の区画を先に手放す。バッファは末尾が容量を超えたときだけ 1/8 の余裕を足して作り直し、古い中身を同じ位置へ 1 回のコピーで写す。記録（`UpdateVisibilityLayout`）は、直前のフレームに描かれていなかった区画・取り直した区画・持ち主が替わった区画だけを 0 で埋める（隣り合う区画はまとめて 1 回）。引き継ぎの条件（直前のフレームに描かれた・同じ鍵・コンポーネント・メッシュ・クラスタの塊）と、エポックの最初のフレームで全部を 0 から始める規則は変えていない。
- 区間は内訳を取った後に外した: `StatsManager::RecordScope` は全スレッド共通の mutex とトレースの書き込みを通るので、`RecordFrameCommand` の中の約 30 個 / フレームの記録の待ちが親の区間に乗る（5万個の静止で区間ありは median 2.36 ms、区間の合計は約 0.5 ms で、残りは区間の終わりと次の始まりの間の隙間。区間なしは 0.63 ms）。区間ありの撮影は `move1-cell64-30k-with-scopes/`・`static-50k-with-scopes/` に残した（区間ありでも区画ありの p95 は 0.825 ms、`Record.VisibilityLayout` は max 0.447 ms・`Record.ReleaseStale` は max 0）。
- 結果（区間なし、`.harness/runs/scene-capture/SCN-MEGA-FIX-RECORD-COMMAND/<負荷>/`。値は metrics.json の `MegaGeometryPass.RecordFrameCommand`、median / p95 / max ms）:

| 負荷 | 前（SCN-MEGA-MEASURE-UPLOAD） | 後 |
|---|---|---|
| 5万個 静止（`static-50k`） | 0.960 / 1.138 | **0.628 / 0.745** / 1.383 |
| 3万個 1% 移動 + 64 m 区画（`move1-cell64-30k`） | 0.619 / 6.294（3 ms 超 26 回、最大 8.4） | **0.487 / 0.625** / 1.042（1 ms 超 1 回、3 ms 超 0 回） |

  - 区画ありの撮影でも区画の出入りは起きている（`MEGA_SHADOW_DIRTY rebuilds=5・5・4`／120、`RT_SCENE built=13`／120）。見えたビットのバッファは窓の中で作り直していない（`MEGA_CAPACITY visibility_bytes=848,976,504` が 26 行とも同じ。前の 760 MB の詰めた大きさ + 1/8 の余裕と、区画の外の席の分）。
  - 付随: 区画ありの GPU のフレームは 31.2 / 34.8 → 28.5 / 31.1 ms、rt は 42.1 / 57.3 → 38.5 / 42.7 ms（760 MB の 0 埋めとコピーが無くなった）。5万個の静止は GPU 42.2 / 43.7 → 42.3 / 43.6 ms、rt 56.0 / 59.0 → 55.5 / 58.1 ms で変わらない。Game.log の ERROR は両方 0、`MEGA_SCENE_UPLOAD ... mismatch=0`。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-MEGA-FIX-RECORD-COMMAND-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `-2.txt`: 起動画面 default の `SCN-MEGA-BASE-RWDI` との決定的な比較が `mean_luminance_difference=0`・`psnr_db=100`・`mismatched_pixel_ratio=8E-06` で `result=pass`、EXIT=0。PNG を開いて確認（家・球・岩・天球・影）。
  - `-3.txt`: `CHECK_CAPTURE_METRICS result=pass views=default`（`--timing --min-frames 300 --max-scope-p95-ms MegaGeometryPass.RecordFrameCommand=1.0`）、EXIT=0。区画ありの PNG を開いて確認（岩・家・塔が欠けずに描かれている）。
  - 撮影の出力: `capture-SCN-MEGA-FIX-RECORD-COMMAND-<breakdown-before|static-50k|move1-cell64-30k>.txt`（どれも `CAPTURE_SCENE result=pass`）。
- Next: 残りの todo の最優先。
- Notes: 区画は席ごとなので、区画の外へ出たインスタンスの区画は、末尾が容量を超えるまで残る（前の表に載らなかった席だけを手放す）。見えたビットのバッファは、席の区画の和 + 1/8 までになる（今の負荷で 849 MB。前は詰めた大きさの 760 MB と、作り直しの 8 フレームの間の倍）。
- Notes: 席の区画の割り当て・手放し・回収を直接確かめるテストは書いていない（verify に無い）。
- Notes: `RecordFrameCommand` の中の区間は、トレースの記録の待ちが親に乗るので常設しない。内訳が要るときは同じ区間を一時的に足して撮り、比較は区間なしの撮影で行う。

### SCN-MEGA-FIX-VSM-INVALIDATE 再開（2026-10-12、反復 24・run 20261012-030203）

- 受入れの基準を「動いた境界が覆う割り当て済みのページの和との一致（多い側にだけずれる）」に改めたので、その和を数える照合を足した（無効化の実装は反復 21 のまま）。
- 実装: 開発ビルド（`NORVES_BUILD_DEVELOPMENT`）だけ、60 フレームごとに、太陽の段を矩形だけで無効にするフレームを選び、割り当ての段階 1（無効化）の直後のページの表の太陽の段（640 KB）を読み戻し先へ写す（`VirtualShadowMapPagesDispatch::PageTableSnapshot`。統計の読み戻しの枠ごとに 1 本）。読み戻したとき、同じフレームの動いた境界（`ChangedBounds`。MegaGeometry の席の旧・新の境界と、それ以外の動いた投影物）を余白なしでライト空間の矩形にし、段ごとにページの集合を作って、割り当て済みの欄と突き合わせる。`VSM_INVALIDATE_EXPECTED pages=<覆う割り当て済みのページ> invalidated=<同じフレームの統計> point_invalidated=<同> sun_dirty=<表の割り当て済みで dirty の欄> missed=<覆うのに dirty でない欄> extra=<dirty だが覆わない欄> bounds=<境界の数>` を出す。合格は `missed=0` かつ `invalidated − point_invalidated ≥ pages`。
- 結果（5万個・1920×1080・300 フレーム。`.harness/runs/scene-capture/SCN-MEGA-FIX-VSM-INVALIDATE/<move1|move0.2>/default.Game.log` の 10 回ずつ）:

| 負荷 | `pages`（照合の和） | `invalidated`（GPU） | missed / extra | 平均 | `VirtualShadowMapPass` GPU median / p95 ms |
|---|---|---|---|---|---|
| 1% 移動（500 席、bounds=500） | 341 300 329 318 318 335 308 332 292 346 | 同じ値（10 回とも一致） | 0 / 0 | 321.9 | 0.969 / 2.039 |
| 0.2% 移動（100 席、bounds=100） | 302 335 282 343 295 343 290 343 295 322 | 同じ値（10 回とも一致） | 0 / 0 | 315.0 | 0.938 / 2.063 |

  - 無効化は全ページ（743）より少なく（読み込み直後の 1 回の 743 を除く `VSM_CACHE invalidated` は 1% 移動で 300〜355）、動いた境界が覆う割り当て済みのページの和と同じフレームで一致した（少ない側・多い側のずれとも 0。矩形の余白 1 mm が加えるページは無い）。点光源の面は使っていない（point_invalidated=0）。
  - 起動画面でも照合が出る（`pages=21 invalidated=43 point_invalidated=22 sun_dirty=21 missed=0`。太陽の段の 21 が一致）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-MEGA-FIX-VSM-INVALIDATE-4.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `-5.txt`: 起動画面 default の `SCN-MEGA-BASE-RWDI` との決定的な比較が `mean_luminance_difference=0`・`psnr_db=100`・`mismatched_pixel_ratio=4E-06` で `result=pass`、EXIT=0。Game.log の ERROR 0。
  - `-6.txt`: mega-stress 5万個・1% 移動が `CAPTURE_SCENE result=pass`、EXIT=0。PNG を開いて確認（影は崩れていない。下端中央の近平面の切れは既知の SCN-MEGA-FIX-STRESS-NEAR-CLIP）。Game.log の ERROR 0、`MEGA_SHADOW_DIRTY ... invalidate_all=0`。
  - 補助: `capture-SCN-MEGA-FIX-VSM-INVALIDATE-move0.2-check.txt`（0.2% 移動、`CAPTURE_SCENE result=pass`、EXIT=0、ERROR 0）。
- Next: 残りの todo の最優先。
- Notes: 照合は 60 フレームに 1 回、RenderThread で境界の数 × 段 × 覆うページを回す（範囲 128 × 128 で切る）。Release では読み戻し先を作らず、照合もしない。

### SCN-MEGA-FIX-STRESS-NEAR-CLIP（2026-10-12、反復 25・run 20261012-030203）

- 原因: 近平面ではない。下端中央の真下（番号 158、x=-0.02・z=7.69、カメラから約 7.7 m）に置かれた崖のスキャン `namaqualand_cliff_01`（高さ 3 m）が、開いた背面をカメラへ向けていた。このメッシュは表（ローカル +Z と上）だけの殻で、背面と底が開いている（境界の辺 577 本。面積を掛けた法線の和の水平成分 / 表面積 = 0.476。`namaqualand_cliff_02` も 0.592。他の 12 種類は 0.035 未満）。番号の向き 340.5° では表がカメラの反対を向き、黒い面と膜は殻の裏面だった。
  - 確かめ方: 番号 158 だけ置かない一時の撮影で、下端中央の黒い画素（最大の成分 < 20）が 5,940 → 214 になり膜も消えた（一時の変更は戻した）。カメラから光線を飛ばすと、黒い画素（≤ 1）の 2,872 本の標本のうち 2,793 本が崖の裏面に当たる（`check-SCN-MEGA-FIX-STRESS-NEAR-CLIP-raycast.txt`）。
  - 回転の規約: World は行ベクトルの規約なので、Y 軸まわりの回転 θ はローカルの XZ の向き φ をワールドの φ − θ へ写す（前の黒い画素 150 本の標本で、φ − θ の仮定は 144 本が崖の裏面に当たり、φ + θ の仮定は 90 本が崖に当たらない）。
- 直し方（`MegaStressRoutine.cpp` だけ）: 読み込んだメッシュごとに、常駐の粗い段の三角形の面積を掛けた法線（頂点の法線の平均で向きを決める）の和を取り、水平成分 / 表面積 ≥ 0.2 を「横に開いた殻」とする（`MEGA_STRESS_OPEN_SIDE id ratio open front_yaw_deg` をログに出す）。開いた殻の動かない Entity は、表を原点の方（どの視点もフィールドの手前の原点付近から +Z を見る）へ向け、番号の向きを ±30° のばらつきに使う。動く Entity は `MakeMover` に番号の向きをそのまま渡し、置いた直後の `ApplyMover` がその向きで上書きするので、動きは変わらない。位置・種類・個数・視点の定義は変えていない。
- 結果（5万個・overview・1920×1080）: 下端中央の黒い画素（≤ 1）は 5,744 → 5,206 だが、中身が変わった。光線の判定で 2,603 本の標本のうち 2,600 本が崖の表（塊の間の影）に当たり、裏面は 3 本（境目の画素）。PNG を開いて確認: 崖は岩の塊として描かれ、膜と裏面の黒は消えた。画面全体の黒い画素（≤ 1）は 72,112 → 70,539（深い割れ目の影が 0 まで落ちるのはこのシーン全体の見え方で、崖が無い一時の撮影でも 65,695）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-MEGA-FIX-STRESS-NEAR-CLIP-3.txt`・`-5.txt`（コメントの直しの後）: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `-4.txt`: mega-stress 5万個 default が `CAPTURE_SCENE result=pass`（entities=50000）、EXIT=0。Game.log の ERROR 0。
  - `check-SCN-MEGA-FIX-STRESS-NEAR-CLIP-raycast.txt`: 前後の黒い画素の光線の判定（前: 裏面 2,793 / 2,872、後: 表 2,600 / 2,603）。
  - 補助: `capture-SCN-MEGA-FIX-STRESS-NEAR-CLIP-views.txt`（close・aerial、`CAPTURE_SCENE result=pass`、ERROR 0）。PNG を開いて確認: close の正面の崖（番号 158）が表を見せ、aerial は崩れていない。`-1.txt`・`-2.txt` は回転の向きを φ + θ と仮定した最初の版（表は向いたが、x が大きい所では逆を向く式）。
- Next: 残りの todo の最優先。
- Notes: 開いた殻の判定は粗い段の三角形だけで数える（崖 2 種で 0.48〜0.59、閉じた岩は 0.035 未満なので下限 0.2 に十分な隔たりがある）。動く崖は回るので、時刻によって開いた背面が見える（動く物の配置を変えない条件のため）。
- Notes: 根本の直しは資産（背面の無いスキャン）か MegaGeometry の両面描画で、どちらも paths の外。今の置き方の直しは、視点がフィールドの手前から +Z を見る間だけ効く。

### SCN-ECS-RT-PLAN（2026-10-12、反復 26・run 20261012-030203）

- 記録の前の計画（設計 §1.13）を RenderGraph に入れた。記録はまだ 1 本のリスト。
  - `PlanRecording`: 記録の前に、コンパイルした順に transient をすべて確保し、使う Texture の共有の layout を起点として読む（`ICommandList::GetTextureLayout`）。
  - 区間（`RGRecordSegment`）: `IRenderGraphPass::DeclaresAllLayoutChanges()`（既定 false）が true のパスは続く限り 1 つの「計画した区間」にまとめ、それ以外は Serial のパス 1 つの区間にする。Compile で作る（`GetRecordSegments()`）。
  - 区間の頭で全バリアの old/new を決める（`RGPlannedBarrier`。`GetPlannedBarriers()` は `GetCompiledBarriers()` と同じ並び）。規則は Vulkan の `TextureBarrier` と同じ（全体を覆うバリアだけが layout を進め、一部の UAV → 読み取りは General のまま）。計画した区間ではパスの最終の layout（attachment は render pass の finalState、それ以外は最終の状態が使う状態と違うときだけ）も反映してから次のパスのバリアを決める。
  - バリアは RHI の明示の old/new の `TextureBarrierExplicit` で張る（共有の layout を読まず書かない）。Serial の区間はバリアの直後に書き戻してから Execute を記録し（パスが記録の最中に共有の layout を読むため）、記録の後にそのパスが宣言した Texture の共有の layout を読んで確定してから次の区間を計画する。計画した区間は記録の間 `SetSharedTextureLayoutWrites(false)` で `EndRenderPass`・`TextureBarrier`・`GenerateMipmaps` などの共有の layout の書き込みを止め、区間の後に計画した最終の layout を `CommitTextureLayout` で書き戻す。
  - 保留のフレームコマンドの束: flush の位置は今と同じ（各パスのバリアの前・後とパスの後）。transient の確保が記録の前へ移っただけで、コマンドの並びは変わらない（確保は CPU の処理だけ）。
- RHI（`ICommandList`）: `TextureLayout`・`ResourceStateToTextureLayout`・`ExplicitTextureBarrierDesc` と、既定の実装を持つ仮想 4 つ（`TextureBarrierExplicit`・`GetTextureLayout`・`CommitTextureLayout`・`SetSharedTextureLayoutWrites`）。既定は状態だけの `TextureBarrier` に委ねるので、テストの偽のリストはそのまま動く。Vulkan は共有の layout の書き込みを `WriteSharedTextureLayout` の 1 か所に集めた。
- 今のグラフのバリアはすべて全体を覆うので、張る old/new と書き戻す layout は今の `TextureBarrier` と同じ（起動画面の決定論比較で一致）。どのパスもまだ `DeclaresAllLayoutChanges` を宣言しない。
- Execute の中で自分でバリアを張るパス（Rendering 層の `TextureBarrier`/`BufferBarrier` の直接の呼び出しと、保留のフレームコマンドへ積む `EnqueueTextureBarrier` の呼び手を辿った。数は呼び出しの箇所）:
  - 直接: MegaGeometryPass（55）、VirtualShadowMapPass（本体 8 + VirtualShadowMapRaster 24 + VirtualShadowMapPages 14）、VisibilityRasterPass・VisibilityDebugPass（21）、LightingPass（9 + DDGIProbePass 13 + RayTracingShadowPass 3）、ShadowProbePass（7）、MaterialTileClassifyPass（7）、HiZPyramidPass（4）、AutoExposurePass（4）、SkinningComputePass（1）、PathTracingPass（1）。
  - 保留のフレームコマンド経由（パスの後の flush で張られる）: MotionBlurPass（6）、DepthOfFieldPass（6）、TemporalAAPass（4）、ForwardPass（2）、VolumetricsPass（1）、NeuralMaterialDecodePass（1）、LightingPass（1）。
  - パスの外（RenderingCoordinator・RenderResources のフレームの頭と終わり）: TileUploader・VirtualTextureFeedbackRing・GeometryPageRequestRing（RenderResources）、FrameCaptureReadbackHelper、SceneRenderer（保留のフレームコマンドの実行）。
  - 上の一覧のパスは RT-PASSES で Parallel にできない（設計 §1.13）。一覧に無い RG のパス（GBuffer・ToneMapping・FXAA・Bloom・SSAO・SSR・Upscale・Composite・Presentation など）が候補。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-RT-PLAN-1.txt`: Debug の RenderGraphCompileTest のビルド build exit=0。
  - `-2.txt`: `RenderGraphParallelPlanTest` Passed（ctest exit=0）。4 件: 全 Serial（パスが宣言に無いバリアを張る）・全部を計画した 1 区間・計画 → Serial → 計画の混在で、計画した見込みの layout と old/new が逐次の実行（従来の `TextureBarrier` が共有の layout を読み書きする）と全バリアで一致し、書き戻した最終の layout も一致する。計画した区間は記録の最中に共有の layout を書かない。宣言に無いバリアを張るパスを計画の対象にすると食い違いを検出する（比較が空振りしない）。
  - `-3.txt`: RelWithDebInfo の起動画面 default の決定論比較が pass（mean_luminance の差 0、psnr 100 dB、不一致の画素 8E-06・最大差 1）。Game.log の ERROR 0。PNG を開いて確認（天球・地面・球・岩・家が基準どおり）。
  - 回帰: RenderGraphLifetimeTest・RenderGraphAttachmentStateTest・RenderGraphDumpTest・LegacyViewPassAdapterTest は Passed。
- Next: SCN-ECS-RT-RHI-SUBMIT。
- Notes: RenderGraphCompileTest（2843 行の `commandList.DispatchCount == 10`）と RenderGraphTextureUsageContractTest（950 行の `pass.Initialize`、assert の窓でタイムアウト）は、この反復の変更の前（スモーク）から落ちている。今回の変更の後も同じ箇所で落ちる。
- Notes: Serial の区間の確定は、そのパスが宣言した Texture だけを読む（全 Texture を毎パス読むと 1 フレームに数千回の dynamic_cast になるため）。宣言に無い Texture をパスが変えた場合、その Texture の見込みは次にそれを宣言する Serial のパスの記録の後まで古いまま（今は old/new が状態だけで決まるので張るバリアは変わらない）。
- Notes: Vulkan の記述子の書き込み（VulkanDescriptorSet の `ResolveDescriptorImageLayout`）も記録の最中に共有の layout（General かどうか）を読む。計画した区間ではその値が区間の頭のままになるので、RT-PASSES の監査では「記述子へ General と読み取りを切り替えるテクスチャを書かない」も条件に入れる。

### SCN-ECS-RT-PLAN 差し戻しの修正（2026-10-12、反復 27・run 20261012-030203）

- 差し戻し: 計画した区間の頭で流した、実行の前の保留のコマンドのバリアが、共有の layout の書き込みを止めた状態で記録され、最終の layout が書き戻されなかった（共有の layout が Undefined のまま残る）。
- 保留のフレームコマンドの区間ごとの束（設計 §1.13）:
  - 区間の頭（計画の前）に、それまでに積まれた保留のコマンドを、共有の layout を書く設定のまま 1 つの束として流す。流したコマンドがあれば、グラフの全 Texture の共有の layout を読み直し（`ConfirmAllTextureLayouts`）、それを起点に区間のバリアと最終の layout を計画する。GPU のコマンドの並びは今と同じ（前は最初のパスのバリアの前に流していた位置で、計画は CPU の処理だけ）。
  - Serial の区間の中で保留のコマンドを流したときは、宣言した Texture だけでなく全 Texture の layout を読み直して確定する（パスが積んだバリアが宣言に無い Texture を変えても次の区間の見込みが外れない）。保留のコマンドを流さなかった Serial の区間は、今までどおり宣言した Texture だけを読む。
  - 計画した区間のパスが、layout を宣言の外で変える保留のコマンド（`TextureBarrier`・`MegaGeometryPass`）を積んだら、契約違反として `NORVES_LOG_ERROR` を出す（計画に入らないため）。render pass のコマンドの最終の layout は宣言どおりなので計画で扱える。
  - `FlushPendingFrameCommands` は流したかどうかを返す。
- テスト: `RenderGraphParallelPlanTest` に `TestPreQueuedBarrierLayoutIsWrittenBack` を足した（指摘の再現: 共有の layout が Undefined の Texture を初期状態 ShaderResource で取り込み、計画したパスが読み、実行の前の保留のコマンドが Undefined → ShaderResource のバリアを張る）。既存の逐次比較（全バリアの old/new と最終の layout）に乗せ、最終の layout が ShaderReadOnly で逐次の実行と一致することを確かめる。表示文を日本語にした。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-RT-PLAN-1.txt`: Debug の RenderGraphCompileTest のビルド build exit=0（エラー 0）。
  - `-2.txt`: `RenderGraphParallelPlanTest` Passed（ctest exit=0）。直接の実行で 5 件すべて合格の行を確認。
  - `-3.txt`: RelWithDebInfo の起動画面 default の決定論比較 pass（mean_luminance の差 0、psnr 100 dB、不一致の画素 9E-06・最大差 1）。default.Game.log の ERROR 0（契約違反のログも出ない）。PNG を開いて確認（天球・地面・球・岩・家が基準どおり）。
  - 回帰: RenderGraphLifetimeTest・RenderGraphAttachmentStateTest・RenderGraphDumpTest・LegacyViewPassAdapterTest は Passed。
- Execute の中で自分でバリアを張るパスの一覧は反復 26 の節のとおり（変更なし）。
- Next: SCN-ECS-RT-RHI-SUBMIT。
- Notes: RT-RECORD で区間ごとに保留のコマンドを分けるときは、区間の頭の束（前の区間の後に積まれたもの）を前の区間のリストの末尾か、その区間のリストの先頭に置く。どちらでも束の後に layout を読み直す今の規則で足りる。

### SCN-ECS-RT-RHI-SUBMIT（2026-10-12、反復 28・run 20261012-030203）

- 複数のコマンドリストを 1 回で提出する経路（設計 §1.13）を RHI に入れた。描画の記録はまだ 0 番の 1 本だけ。
  - `ISwapChain::EndFrame(const CommandListPtr*, uint32_t)`: 並びの順に 1 回の `vkQueueSubmit` で提出し、全リストへ同じ提出番号を配る。既定の実装は 1 本だけを受け付けて単一の版へ渡す（テストの偽のスワップチェーンはそのまま動く）。Vulkan の単一の版は複数の版へ 1 本で渡す。
  - `ICommandList` の提出の状態（`CommandListSubmissionState`: Idle・Recording・Recorded・Pending・Aborted）と、`GetSubmissionState`・`GetLastSubmissionSerial`・`AbortSubmission`・`NotifySubmissionCompleted`・`SubmitBatchAndWait`（スワップチェーンを通さない道具・テスト用の束の提出。窓口のリストのフェンスで待つ）。既定の実装を持つので偽のリストはそのまま動く。
  - 束で提出できるのは Recorded のリストだけ。null・Vulkan でない・Recorded でない・重複を含む束は拒否し、拒否と提出の失敗（番号の枯渇・フェンスのリセット・キューの提出。例外を含む）では並びの全リストを中止する（`AbortSubmission`: コマンドバッファを初期状態へ戻し、GPU タイムスタンプのフレームと保留の AS の確定を捨て、次の記録の開始まで提出を拒む）。成功したら各リストの現在の slot の GPU タイムスタンプを同じ番号で確定する（`Detail::ExecuteCommandListBatchSubmissionSequence`）。
  - 同じ slot が完了するまで記録し直さない: 束で提出したリストは slot ごとに完了待ちの番号を持ち、`Begin`/`BeginRecording` はその slot が完了の通知を受けるまで例外を投げる。`VulkanSwapChain` は slot ごとに提出に含めたリスト（weak）を覚え、次にその slot のフェンスを待った `BeginFrame` と、破棄（WaitIdle の後）で完了を通知する。
  - RenderingCoordinator: 仕事の番号ごとのリスト `m_JobCommandLists`（0 番は `m_CommandList`）。フレームの頭で `PrepareJobCommandLists`（足りない番号のリストを RenderThread で作り、全リストを今の slot に合わせる。今の数は 1）、GPU タイムスタンプの完了の通知を全リストへ、提出は `SubmitJobCommandLists`（0 番と、このフレームで Recorded になった番号を番号の順に 1 回で）。記録の開始から提出までを `ScopedJobCommandListsAbort` で囲み、提出まで届かなかったフレーム（例外など）のリストを中止する（提出済みには何もしない）。
- paths の外: `RenderingCoordinator.h` に非公開のメンバ `m_JobCommandLists` の宣言を 1 つ足した（RenderingCoordinator.cpp の非公開の状態。World.h の前例と同じ扱い）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-RT-RHI-SUBMIT-1.txt`: Debug の RHITextureUpdateVulkanTest のビルド build exit=0（エラー 0）。
  - `-2.txt`: `RHIParallelRecordVulkanTest` Passed（ctest exit=0）。直接の実行（`check-SCN-ECS-RT-RHI-SUBMIT-direct.txt`）で `CASE two_threads_one_submit serial=1`・`CASE abort_then_rerecord serial=2`・`VUID_COUNT=0`・`RESULT=PASS`。内容: 2 つのスレッドで「render pass のクリア（書き込み）」と「明示の old/new のバリア → バッファへのコピー（読み出し）」を別々のリストへ記録（共有の layout は書かない）し、2 本を 1 回で提出すると読み出しが赤になり、2 本の提出番号が同じで完了が通知される。1 本を中止すると束は提出されず（読み出し先は前の値のまま）、残りの 1 本も中止され、中止したリストは単独でも提出できない。記録し直した束は新しい番号で提出され、新しい中身（青）だけを実行する。検証レイヤーを有効にしてエラー 0。
  - `-3.txt`: RelWithDebInfo の起動画面 default の決定論比較 pass（mean_luminance の差 0、psnr 100 dB、不一致の画素 9E-06・最大差 1）。default.Game.log の ERROR 0。PNG を開いて確認（天球・地面・球・岩・家が基準どおり）。
- Next: SCN-ECS-RT-RHI-TIMESTAMPS。
- Notes: `FixedArray` の既定の構築（スタックに置く大きさ）は要素を初期化しない（`= {}` でも同じ）。新しい slot ごとの番号は値で埋める構築にした。同じ形の `m_DirectFrameSlotSubmissionSerials`（GPU タイムスタンプの直接の提出の番号）は初期化されないまま（このタスクの範囲の外なので直していない）。
- Notes: RT-GRAPH の paths には RenderingCoordinator.cpp が無い。区間ごとのリストをグラフへ渡すには、`PrepareJobCommandLists` の数を区間の数にし、リストを ViewRenderContext へ載せる変更が Coordinator 側に要る。
- Notes: 1 つのリストは 1 つの提出の窓口（スワップチェーンか、1 つの SubmitBatchAndWait の窓口）で使う前提（完了待ちの番号は窓口ごとの番号で比べる）。

### SCN-ECS-RT-RHI-SUBMIT 差し戻しの修正（2026-10-12、反復 29・run 20261012-030203）

- 差し戻し: 直接の提出の経路で、中止の後の `Begin()` がフェンスの待機で止まり、中止したリストの `Submit()` がキューへ提出された。
- `VulkanCommandList`: フェンスのリセットを `Begin()` から `Submit()` の提出の直前（提出番号の確保の後、`vkQueueSubmit` の前）へ移した。記録だけして中止・未提出で終えてもフェンスは signaled のままなので、次の `Begin()` は待ち続けない。束の提出（`SubmitBatchAndWait`）は前から提出の直前でリセットしている。
- `Submit()` は Aborted（コマンドバッファを初期状態へ戻した）と Pending（束で提出して完了待ち）のリストを例外で拒否する。それ以外の状態は今までどおり（既存の呼び出し元を変えない）。
- テスト: `RHIParallelRecordVulkanTest` に直接の提出のケースを足した（`Begin → 記録 → End → AbortSubmission → Submit` が拒否され GPU で実行されない、その後の `Begin` が止まらずに記録し直した中身（白）が実行される）。CMake の説明の英語を日本語にした。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-RT-RHI-SUBMIT-1.txt`: Debug の RHITextureUpdateVulkanTest のビルド build exit=0（エラー 0）。
  - `-2.txt`: `RHIParallelRecordVulkanTest` Passed（ctest exit=0）。直接の実行（`check-SCN-ECS-RT-RHI-SUBMIT-direct-2.txt`）で `CASE two_threads_one_submit`・`CASE abort_then_rerecord`・`CASE direct_abort_then_rerecord rejected=1`・`VUID_COUNT=0`・`RESULT=PASS`。
  - `-3.txt`: RelWithDebInfo の起動画面 default の決定論比較 pass（mean_luminance の差 0、psnr 100 dB、不一致の画素 8E-06・最大差 1）。default.Game.log の ERROR 0。PNG を開いて確認（天球・地面・球・岩・家が基準どおり）。
  - 回帰（`check-SCN-ECS-RT-RHI-SUBMIT-regress-2.txt`）: RHIGPUTimestampVulkanTest・RHITextureUpdateVulkanTest・RHIImageLayoutVulkanValidationTest は Passed。
- Next: SCN-ECS-RT-RHI-TIMESTAMPS。
- Notes: 直接の提出で、フェンスのリセットの後に `vkQueueSubmit` が失敗するとフェンスは unsignaled のまま残り、次の `Begin()` が待ち続ける（前からの挙動。提出の失敗はデバイスの喪失と同じ扱いで、今回は直していない）。

### SCN-ECS-RT-RHI-TIMESTAMPS（2026-10-12、反復 30・run 20261012-030203）

- リストごとに取った GPU の時間を、同じフレームの同じ区間の名前で合わせる経路（設計 §1.13）を入れた。
  - `GPUTimestampResult` に区間の開始と終了の時刻（tick。有効なビットで切った値）・1 tick の長さ・有効なビット数を足した。Vulkan の解決で埋める（同じキューの時刻はリストをまたいで比べられる）。
  - `RHI::MergeGPUTimestampResultsAcrossCommandLists`（GPUTimestamp.h）: 提出の順に並べたリストごとの結果を合わせる。あるリストの (フレーム, 名前) は、前のリストから来た同じ (フレーム, 名前) のうち、このリストとまだ合わせていない最初のものと 1 つになる（同じリストの中の同名の区間は別々に残る）。合わせた区間は先頭のリストの開始から、最も遅い終了まで。後のリストの区間が先頭の開始より前に始まる・自分の開始より前に終わる・時刻の単位が違う、のどれかのときは無効にする。1 つのリストにしか無い区間（パス）はそのまま通す。
  - `VulkanCommandList`: `NotifyGPUTimestampFrameSlotCompleted` が完了にした slot をその場で読み出す（束の全リストの結果がフレームの頭で揃う。読めない分は次の通知か記録の開始で読み直す）。`SubmitBatchAndWait` は待った束の各リストへ時刻の完了も通知する（直接の `Submit(true)` と同じ）。
  - RenderingCoordinator: `PublishCompletedGPUTimestampResults` が仕事の番号の順に全リストの結果を集めて合わせ、最新の FrameGPU（合わせた値）をフレームの GPU 時間の統計（`m_LatestCompletedGPUTimeMs`・`SetGPUFrameTimeMs`）に使う。前は 0 番のリストの旧 API の値（`GetLastGPUTimestampDurationMs`）を読んでいた。今はリストが 1 本なので値は同じ。
- テスト: `RHIParallelRecordVulkanTest` に `cross_list_frame_gpu` を足した。書き込みと読み出しの 2 本のそれぞれに FrameGPU とパスの区間を開いて 1 回で提出し、合わせた FrameGPU の開始が先頭のリストの開始・終了が末尾のリストの終了で、先頭の開始 <= 末尾の開始 <= 末尾の終了・先頭の終了 <= 末尾の終了、4 つの区間（2 本の FrameGPU と 2 つのパス）の時間以上であることを確かめる。パスの区間は記録したリストの値のまま残る。後のリストが先頭より前に始まる並びを合わせると無効になる。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-RT-RHI-TIMESTAMPS-1.txt`: Debug の RHITextureUpdateVulkanTest のビルド build exit=0。
  - `-2.txt`: `RHIParallelRecordVulkanTest` Passed（ctest exit=0）。直接の実行（`check-SCN-ECS-RT-RHI-TIMESTAMPS-direct.txt`）で `CASE cross_list_frame_gpu frame_ms=0.005472 first_ms=0.002464 last_ms=0.000928`・`VUID_COUNT=0`・`RESULT=PASS`。
  - 回帰（`check-SCN-ECS-RT-RHI-TIMESTAMPS-regress.txt`）: RenderingCoordinatorGPUTimestampPublicationTest・RenderGraphGPUTimestampScopeTest・RHIGPUTimestampVulkanTest・RHITextureUpdateVulkanTest・RHIImageLayoutVulkanValidationTest は Passed。
- Next: SCN-ECS-RT-GRAPH。
- Notes: RT-GRAPH で区間ごとのリストを記録するときは、各リストの記録の頭で `BeginGPUTimestampFrame(フレーム番号)` と `FrameGPU` の区間を開き、末尾で閉じる（合わせる側は名前で 1 つにする）。0 番のリストは今の旧 API（`BeginGPUTimestamp`）のままでよい。
- Notes: 合わせる規則は (フレーム, 名前) だけで見るので、別々のリストに同じ名前のパス（例: 2 つのビューの同名のパス）が記録されると 1 つの区間に合わさる。区間ごとの記録でビューをまたぐなら、パスの区間の名前にビューを含めるか、FrameGPU だけを合わせる規則に絞る。

### SCN-ECS-RT-RHI-TIMESTAMPS 反復 31（2026-10-12、run 20261012-030203）

- 差し戻しの修正: Release・MinSizeRel は `NORVES_ENABLE_STATS=0` で `SupportsGPUTimestamps()` が常に false のため、`cross_list_frame_gpu` が無条件に失敗していた。計測ケースとその補助（`RunCrossListFrameGPUCase` ほか）を `#if NORVES_ENABLE_STATS` で囲み、提出・中止の検査は全構成で残した。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-RT-RHI-TIMESTAMPS-1.txt`: Debug のビルド EXIT=0。`-2.txt`: Debug の ctest 100% passed、EXIT=0。`-3.txt`（-V）: `CASE cross_list_frame_gpu frame_ms=0.006144 first_ms=0.002304 last_ms=0.001248`・`VUID_COUNT=0`・`RESULT=PASS`。
  - `-4.txt`: Release のビルド EXIT=0。`-5.txt`（Release の ctest -V）: 提出・中止の 3 ケースが出て計測ケースは出ず、`VUID_COUNT=0`・`RESULT=PASS`・EXIT=0。
- Next: SCN-ECS-RT-GRAPH。

### SCN-ECS-RT-GRAPH（2026-10-12、反復 32・run 20261012-030203）

- RenderGraph をパスの区間ごとのリストへ記録できるようにした（設計 §1.13）。どのパスもまだ Parallel を宣言しないので、今の描画は 1 本の記録のまま。
  - 記録の方針: `RGRecordPolicy`（Serial・Parallel）と `IRenderGraphPass::GetRecordPolicy()`（既定 Serial）。Parallel は `DeclaresAllLayoutChanges()` が true のパスだけが有効で、false なら `NORVES_LOG_ERROR` を出して Serial として扱う。
  - リストの区間（`RGRecordListSegment`・`GetRecordListSegments()`）: Compile で、グラフの頭と Parallel のパスで新しいリストを始め、Serial のパスは前のパスのリストへ続ける。Serial だけのグラフは 1 つの区間。
  - 記録: `ViewRenderContext::RecordListSource`（`IRenderGraphRecordListSource::AcquireRecordList`。Begin 済みのリストを提出の順に渡す口）がある時だけ、続く Parallel のパスごとにリストを受け取り、区間ごとの context（CommandList と PendingFrameCommands を区間のものにした写し）で `JobSystem::ParallelFor` から同時に記録する。バリアの範囲は記録の前に直列で決め、各区間のリストへ RT-PLAN の明示の old/new で張る。計画した区間では区間のリストの共有の layout の書き込みも止め、区間の後に全リストで戻してから書き戻す。区間の保留のコマンドは合流の後に RenderThread でその区間のリストのパスの後へ流し、パスの GPU の区間はその後に閉じる（Serial と同じ範囲）。続く Serial のパスは最後に受け取ったリストへ続け、実行の後の `context.CommandList` は最後のリストになる（呼び手は以後の記録をそこへ続ける）。リストを受け取れなかったパスは最後のリストへ Serial で記録する。
  - Serial の記録は前の反復の手順を `RecordSerialPass` へ移しただけで、バリア・保留のコマンドの flush・デバッグの印・GPU の区間の並びは同じ。
  - 各パスの Execute を、記録したスレッドの番号で `RG.Record.<パス>` の区間（カテゴリ RenderGraph）として残す。名前は `AddPass` の時のパスの名前で作り、グラフが持つ（計測が有効な構成だけ）。
- テスト: `RenderGraphParallelRecordTest`（RenderGraphCompileTest の束）。偽のリストと偽のリストの渡し口で、JobSystem を 2 つのワーカーで初期化して確かめる。
  - Serial だけのグラフ（計画できるパスと Serial の区間の混在、保留のコマンドあり）: リストの区間が 1 つ、リストを受け取らず、記録が渡し口なしの実行と同じ並び。
  - Parallel を返すが layout を宣言から決められないパスは同じ区間に残る。
  - Serial（計画）・Parallel・Parallel・Serial（計画）・Parallel・Serial（計画できない）の 6 パス: 区間が [0] [1] [2 3] [4 5]、各パスの記録先とバリアがそのパスの区間のリストに入りパスより前、区間ごとの保留のコマンドがその区間のリストのパスの後、受け取ったリストは 3 本で実行の後の記録先は最後のリスト、全リストの共有の layout の書き込みが戻っている。区間のリストを提出の順に並べると 1 本へ記録したときと同じ並び（old/new を含む）で、最終の layout も同じ。各パスの `RG.Record.<パス>` が 1 つずつ、そのパスを記録したスレッドの番号で残る。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-RT-GRAPH-1.txt`: Debug の RenderGraphCompileTest のビルド EXIT=0。
  - `-2.txt`: `RenderGraphParallelPlanTest`・`RenderGraphParallelRecordTest` が Passed（100%、EXIT=0）。
  - `-3.txt`: RelWithDebInfo の起動画面 default の決定論比較 pass（mean_luminance の差 0、psnr 100 dB、不一致の画素 8E-06・最大差 1）。default.Game.log の ERROR 0。PNG を開いて確認（天球・地面・球・岩・家が基準どおり）。
  - 回帰（`check-SCN-ECS-RT-GRAPH-regress.txt`）: RenderGraphGPUTimestampScopeTest・RenderGraphLifetimeTest・RenderGraphAttachmentStateTest・RenderGraphDumpTest・LegacyViewPassAdapterTest は Passed。
- Next: SCN-ECS-RT-PASSES。
- Notes: RT-PASSES で RenderingCoordinator をつなぐときは、`RecordListSource` の実装で仕事の番号ごとのリスト（`m_JobCommandLists`）を Begin して返し、リストの頭で `BeginGPUTimestampFrame` と FrameGPU の区間を開く。グラフの実行の後は `context.CommandList`（最後のリスト）へ以後の記録（次のビューのグラフ・合成・提示）を続け、提出は 0 番と受け取った順のリストを 1 回で行う。`render-parallel-record=off` は渡し口を null にすれば今の 1 本に戻る。
- Notes: Parallel のパスは区間ごとの context の写しで記録するので、Execute の中で ViewRenderContext のメンバ（例: `ActiveShadowMapSettings`）を書いて後のパスへ渡すパスは Parallel にできない。RT-PASSES の監査の条件に入れる。
- Notes: 区間の保留のコマンドの実行（`SceneRenderer::ExecuteFrameCommands`）は RenderThread で合流の後に行うので、スレッド安全の監査は要らない。ParallelFor の中で例外が出たときは、区間のリストの共有の layout の書き込みを戻さない（呼び手のフレームの中止でリストごと捨てる前提。Serial の例外と同じ扱い）。

### SCN-ECS-RT-PASSES（2026-10-12、反復 33・run 20261012-030203）

- 監査したパスに Parallel を宣言し、RenderingCoordinator から区間ごとのリストを渡して同時に記録するようにした（設計 §1.13）。`--render-parallel-record=off` で 1 本の記録に戻る。
- rt の上位: MEASURE-CPU の撮影にはパスごとの CPU の区間が無い（`RenderGraph.Execute` 49.8 ms・`MegaGeometryPass.RecordFrameCommand` 15.3 ms だけ）ので、同じ負荷（mega-stress 5万個・静止・RelWithDebInfo・120 フレーム）を今のビルドで 1 回撮り、RT-GRAPH の `RG.Record.<パス>` で並べた（`.harness/runs/scene-capture/SCN-ECS-RT-PASSES/audit-static/metrics.json`。rt の median 57.8 ms・`RenderGraph.Execute` 11.9 ms）。
- 監査の表（median / p95 ms は `RG.Record.<パス>`。条件は「Execute が記録だけ」「自分でバリアを張らない」「共有の CPU の状態を持たない（ViewRenderContext の書き換えと SharedResources を含む）」）:

| 順 | パス | median / p95 | 記録だけ | バリアなし | 共有の状態なし | 判定 |
|---|---|---|---|---|---|---|
| 1 | MegaGeometryPass | 7.711 / 9.132 | × 記録の前に表を作る（PrepareSceneTables 7.7 ms） | × 55 か所 | × RenderThread の写し・席の表 | Serial |
| 2 | VisibilityRasterPass | 0.694 / 1.001 | ○ | × 21 か所 | ○ | Serial |
| 3 | LightingPass | 0.417 / 0.462 | ○ | × 9 か所＋DDGI 13＋RT の影 3、保留へ 1 | ○ | Serial |
| 4 | ShadowMapPass | 0.387 / 0.434 | ○ | ○ | × `context.ActiveShadowMapSettings` を書いて後のパスへ渡す | Serial |
| 5 | VirtualShadowMapPass | 0.197 / 0.262 | ○ | × 8＋Raster 24＋Pages 14 | ○ | Serial |
| 6 | VisibilityResolvePass | 0.095 / 0.115 | ○ dispatch だけ（材質の既定・定数の 1x1 のテクスチャは準備段で作り、記録の最中は作らない（反復 34）） | ○（GBuffer は UnorderedAccess のまま） | ○ 書くのは自分のメンバ・ディスクリプタセット・定数のリング。読むラスタ・分類の結果は前の Serial のパスが書き終えたもの | **Parallel** |
| 7 | BloomPass | 0.026 / 0.035 | ○ | ○ | ○ | Serial（ミップの鎖の自前のテクスチャ（グラフの外）の layout を自分の render pass で変えるので、宣言どおりにだけ layout を変えるとは言えない） |
| 8 | MaterialTileClassifyPass | 0.018 / 0.033 | ○ | × 7 か所 | ○ | Serial |
| 9 | AutoExposurePass | 0.016 / 0.031 | ○ | × 4 か所 | ○ | Serial |
| 10 | SSAOPass | 0.008 / 0.011 | ○ 全画面 2 つを積むだけ | ○（出力 2 枚は render pass の finalState で宣言の ShaderResource へ） | ○ SharedResources は入力をグラフから読めない旧経路だけ | **Parallel** |
| 11 | SSRPass | 0.005 / 0.009 | — | — | — | Serial（前後が Serial で同時に記録する相手が無い。監査の対象外） |
| 12 | ToneMappingPass | 0.004 / 0.008 | ○（表示変換・見た目の 3D LUT の作成・転送は準備段で行い、記録の最中は用意済みを読むだけ（反復 34）） | ○（出力は finalState ShaderResource） | ○ 旧経路だけ SharedResources | **Parallel** |
| 13 | FXAAPass | 0.003 / 0.006 | ○ | ○ | ○ 旧経路だけ | **Parallel**（起動画面では宣言されない） |
| 14 | PresentationPass | 0.003 / 0.005 | ○ | ○ | × `context.bPresentationGraphPassHandled` を書く | Serial |
| 15 | GBufferPass | 0.003 / 0.004 | — | — | — | Serial（前後が Serial。監査の対象外） |
| 16 | VignettePass | 0.002 / 0.003 | ○ | ○ | ○ 旧経路だけ | **Parallel** |
| 17 | ForwardPass | 0.002 / 0.003 | ○ | × 2 か所＋保留へ 2 | ○ | Serial |
| — | SkyAtmospherePass | 0.000 | ○ | ○ | × context へ LUT を公開する | Serial |
| — | UpscalePass・DebugDrawPass | 0.000 | — | — | — | Serial（既定では何も記録しない（拡大なし・線なし）。リストを増やすだけ） |
| — | TemporalAA・MotionBlur・DepthOfField・Volumetrics・NeuralMaterialDecode・SkinningCompute | 0.000 | — | × 保留へ積むバリアか直接のバリア | — | Serial |

- Parallel の 5 つは、準備が整ったフレーム（初期化済み・パイプラインとディスクリプタセットが揃う・入力をグラフから読む（旧経路でない））だけ `DeclaresAllLayoutChanges()` と `GetRecordPolicy()` が true / Parallel を返す（最初のフレームは Serial。宣言の矛盾のエラーを出さない）。同時に記録される並びは [VisibilityResolve, SSAO] と [ToneMapping, Vignette, FXAA]。
- 共有のアロケータ: リスト（仕事の番号ごとに 1 本）と保留のフレームコマンド（区間ごと）は区間ごと、transient は記録の前に直列で確保（RT-PLAN）。Execute が触るディスクリプタセット（セットごとのプール）・定数のバッファ・VisibilityResolve の定数のリングはパスが持つ。テクスチャの作成・転送（`Texture::Update` は単発のコマンドに layout のバリアを記録して提出し GPU を待つ）と、render pass・framebuffer・パイプラインの作り直しは、Parallel の並びの記録の前に RenderThread で直列に呼ぶ準備段（`IRenderGraphPass::PrepareParallelRecord`）で済ませる（反復 34）。
- RenderGraph: 計画した区間で、パスがサンプルする Texture（ShaderResource の宣言）のパスの中の layout（バリアの後）を残し、記録の前に書き戻す（`CommitSampledLayouts`）。Vulkan の記述子の書き込みは共有の layout が General かどうかで image の layout を決め、計画した区間では共有の layout が区間の頭のままになるため。Parallel の並びはまとめて書き戻し、同じ Texture を General とそれ以外でサンプルするパスが並びにあれば同時に記録せず Serial で記録する（`RG_PARALLEL_SAMPLED_LAYOUT_CONFLICT` を 1 回出す）。計画した区間の Serial のパスはパスごとに書き戻す。Parallel のパスの組は、空でなく変わったときだけ `RG_PARALLEL_RECORD_PASSES` で出す。
- RenderingCoordinator: `JobRecordListSource`（仕事の番号 1 以降のリストを提出の順に作って渡す。記録を始め、トレース中は GPU タイムスタンプのフレームと FrameGPU の区間を開く。上限 32 本）。グラフの後の記録（保留のコマンド・撮影のコピー・VT の要求のバリア）は最後のリスト（`viewContext.CommandList`）へ続け、フレームの終わりに受け取ったリストの区間を閉じて End する。提出は既存の `SubmitJobCommandLists`（番号の順に 1 回）。リストの数は変わったときだけ `RENDER_PARALLEL_RECORD_LISTS` で出す。`NORVES_RENDER_PARALLEL_RECORD`（0 で off）を最初のフレームで読み、`RENDER_PARALLEL_RECORD enabled=` を出す。RenderFrameExecutor も、グラフの後の記録（保留のコマンド・直接の描画・合成）を Context の今のリストへ続ける。
- Game: `--render-parallel-record=on|off` を `NORVES_RENDER_PARALLEL_RECORD` へ渡す（それ以外の値はエラー）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-RT-PASSES-1.txt`（on）: RelWithDebInfo の起動画面 default の決定論比較 pass（mean_luminance の差 0、psnr 100 dB、不一致の画素 4E-06・最大差 1）。default.Game.log の ERROR 0。`RENDER_PARALLEL_RECORD enabled=1`、`RG_PARALLEL_RECORD_PASSES passes=VisibilityResolvePass,SSAOPass,ToneMappingPass,VignettePass`、`RENDER_PARALLEL_RECORD_LISTS count=4 frame=1`（フレーム 0 は VisibilityResolve だけで 1 本）。
  - `verify-SCN-ECS-RT-PASSES-2.txt`（off）: 同じ比較 pass（差 0、psnr 100 dB、不一致の画素 4E-06・最大差 1）。ERROR 0。`enabled=0` でリストは 0 本（全パスを 0 番へ記録）。
  - 両方の PNG を開いて確認（天球・地面・球・岩・家が基準どおり）。
  - 回帰（`check-SCN-ECS-RT-PASSES-regress.txt`）: Debug の RenderGraphParallelPlanTest・RenderGraphParallelRecordTest・RenderGraphLifetimeTest・RenderGraphAttachmentStateTest が Passed。
- Next: SCN-ECS-MEASURE-RT。
- Notes: Parallel にできた 5 つの記録の CPU は合わせて約 0.11 ms（5万個の静止）で、rt の上位 5 つ（合計約 9.4 ms）はどれもバリアか共有の状態で Serial のまま。MEASURE-RT で on の rt が off より悪化しないかを見る。上位を並列にするには、バリアをグラフの宣言へ移す・ShadowMapPass の設定を context でなくグラフの出力で渡すなどの別の項目が要る。
- Notes: 起動画面では FXAAPass が並びに入らない（宣言の条件を満たさないフレームか無効）。原因は確かめていない（Serial のまま正しく描かれる）。
- Notes: 別々のリストに同じ名前のパスが記録されると、GPU の時間を合わせる規則（フレームと名前）で 1 つに合わさる（RT-RHI-TIMESTAMPS の Notes）。今は Parallel のパスを持つグラフがビューごとに 1 つなので起きない。

### SCN-ECS-RT-PASSES 差し戻しの修正（2026-10-12、反復 34・run 20261012-030203）

- 差し戻し: ToneMappingPass（カメラの見た目の LUT のパスが変わったフレーム）と VisibilityResolvePass（未キャッシュの金属度・粗さ・AO の定数）が、Parallel の記録の最中に `Texture::Update`（単発のコマンドに layout のバリアを記録して提出し GPU を待つ）を呼ぶ。
- 修正: `IRenderGraphPass::PrepareParallelRecord(resources, context)`（既定は何もしない）を足し、RenderGraph が Parallel の並びを区間ごとのリストへ記録する前に、並びの各パスについて RenderThread で直列に呼ぶ（前の Serial のパスを記録した後・サンプルする layout の書き戻しの前）。
  - ToneMappingPass: 準備段で render pass・framebuffer・パイプラインの作り直し（`PrepareResources`）、表示変換の LUT（`PrepareColorLut`）、見た目の LUT（`PrepareLookLut`。演算子が ACES 2.0 LUT 以外のとき）を済ませ、演算子の番号と見た目の LUT を残す。同じフレームの Execute は残した値を読むだけで、LUT を作らない。準備段の無い Serial のフレームは今までどおり Execute で用意する（印は Execute の頭で消すので持ち越さない）。
  - VisibilityResolvePass: 準備段でラスタのパスが集めた材質の並び（Execute と同じ `MakeResolveMaterial`）から、既定のテクスチャ・サンプラーと定数の 1x1 のテクスチャを作る（`VisibilityResolve::PrepareMaterialTextures`。`RecordTiles` と同じ条件）。準備段を通ったフレームの Execute は `SetTextureCreationAllowed(false)` で作成を止め、足りないものは作れなかった扱い（定数は ERROR を 1 回出して既定へ、既定のテクスチャは記録しない）にする。
  - SSAOPass・VignettePass・FXAAPass: 準備段で attachment・render pass・framebuffer・パイプラインの作り直しを済ませる（Execute の同じ呼び出しは同じ大きさ・テクスチャなら何も作らない）。この 3 つの Execute にテクスチャの転送は無い。
- 監査の表の「記録だけ」と共有のアロケータの行を、上の準備段に合わせて直した。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-RT-PASSES-3.txt`（on）: ビルド pass、default の決定論比較 pass（mean_luminance の差 0、psnr 100 dB、不一致の画素 7E-06・最大差 1）、EXIT=0。default.Game.log の ERROR 0、`RENDER_PARALLEL_RECORD enabled=1`、`RG_PARALLEL_RECORD_PASSES passes=VisibilityResolvePass,SSAOPass,ToneMappingPass,VignettePass`、`RENDER_PARALLEL_RECORD_LISTS count=4 frame=1`。
  - `verify-SCN-ECS-RT-PASSES-4.txt`（off）: ビルド pass、同じ比較 pass（差 0、psnr 100 dB、不一致の画素 8E-06・最大差 1）、EXIT=0。ERROR 0、`enabled=0`。
  - 両方の PNG を開いて確認（天球・地面・球・岩・家が基準どおり）。
- Next: SCN-ECS-MEASURE-RT。
- Notes: 今回の差し戻しの経路（見た目の LUT の切り替え・新しい定数の材質の出現）を起動画面で踏む検証は無い（起動画面のカメラは LUT のパスを変えず、材質も固定）。Debug の RenderGraph の並列の記録のテストは今回走らせていない（既定の空の準備段を呼ぶだけの変更）。

### SCN-ECS-MEASURE-RT 節目の計測（2026-10-12、反復 35・親が表を書いた）

- 反復 35 は撮影と既定を off にする変更の後、壁時計（40 分）で止められた（表は親が `.harness/runs/scene-capture/SCN-ECS-MEASURE-RT/*/metrics.json` から書いた）。
- RelWithDebInfo・1920x1080・300 フレーム。単位 ms（median / p95）。

| 撮影 | GPU | rt | `RenderGraph.Execute` | `MegaGeometryPass.BuildSceneTables` | `RG.Record.LightingPass` |
|---|---|---|---|---|---|
| 5万個 close on | 30.44 / 31.09 | 45.01 / 48.42 | 11.36 / 13.75 | 7.18 / 8.51 | 0.42 / 0.47 |
| 5万個 close off | 30.50 / 31.25 | 45.06 / 48.29 | 11.48 / 13.70 | 7.18 / 8.28 | 0.43 / 0.49 |
| 5万個 close on（2 組目） | 30.48 / 31.56 | 45.02 / 49.41 | | | |
| 5万個 close off（2 組目） | 30.58 / 33.89 | 45.52 / 49.88 | | | |
| 5万個 close on（3 組目） | 34.23 / 43.55 | 51.23 / 62.91 | | | |
| 5万個 close off（3 組目） | 33.91 / 36.16 | 49.58 / 53.38 | | | |
| バナナ 1万本 overview on | 8.12 / 9.87 | 19.83 / 23.09 | 7.90 / 10.84 | 0.84 / 1.27 | 3.63 / 5.39 |
| バナナ 1万本 overview off | 8.21 / 9.89 | 20.03 / 23.80 | 8.16 / — | 0.83 / — | — |

- 判定: on の rt の p95（48.42）が off（48.29）を 0.13 ms 上回り、done-when の合格を外れたので既定を off にした（`--render-parallel-record=on` で有効）。差は組ごとの揺れ（off の p95 が 48.3〜53.4）より小さい。Debug の起動画面（on）の検証レイヤーのエラーは 0。
- 読み: Parallel にした 5 つの記録は合わせて約 0.03〜0.11 ms で、on と off の差は測れない大きさ。rt の内訳は 5万個で Execute 11.4 + Compile 0.6 + 区間外 約 33 ms（GPU 30.4）、バナナで Execute 7.9 + 区間外 約 12 ms（GPU 8.1）。`VulkanSwapChain.h` の `MAX_FRAMES_IN_FLIGHT = 1` で、記録と GPU が重ならずに足し算になっている。記録の道の上位は 5万個の `BuildSceneTables` 7.2 ms（毎フレーム全量の作り直し）とバナナの `RG.Record.LightingPass` 3.6 ms。
- Next: SCN-ECS-FIX-VISRESOLVE-CONST → SCN-ECS-RT-FRAME-SCOPES → SCN-ECS-FIX-LIGHTING-RECORD → SCN-MEGA-FIX-SCENE-TABLES-DELTA → SCN-ECS-FIF-AUDIT → （足した FIF の項目）→ SCN-ECS-FIF-SWITCH → SCN-ECS-MEASURE-FIF → SCN-ECS-RT-PARALLEL-WIDEN。

### SCN-ECS-FIX-VISRESOLVE-CONST（2026-10-12、反復 36・run 20261012-030203）

- 直し: `VisibilityResolve::PrepareMaterialTextures`（直列の準備段）の頭で作成を許可し、前のフレームの区間の記録が止めたままでも、途中のフレームに現れた未キャッシュの定数（金属度・粗さ・AO）の 1x1 のテクスチャを作る。`VisibilityResolvePass::Execute` は準備段を通ったフレームだけ作成を止め、どの戻り方でも許可へ戻す（止めるのは区間の記録の間だけ）。
- 試験: CPU の束 `RenderGraphCompileTest` の `RenderGraphTextureUsageContractTest` に `TestVisibilityResolveConstantTexturesMatchBetweenParallelAndSerialRecord` を足した（偽のデバイスで VisibilityResolve を初期化し、1 フレーム目の材質だけ → 2 フレーム目に定数 0.6・0.25・0.4 の材質を足す。並列の順（準備段 → 作成を止めて記録）と 1 本の順（記録の中で作る）で、2 フレーム目の各間接 dispatch が同じ枠に同じ濃さの定数のテクスチャを束ね、足した材質に 153・64・102 の 3 枚が付く）。修正を外すと 2 フレーム目の準備段で落ちることを確かめた。
- 同じ試験の束の既存の ShadowMap の検査が、ShaderManager が include の展開で原文を読むようになって（2026-09-23）から、空のシェーダーのディレクトリで落ちていた。束のシェーダーのディレクトリを `NORVES_SOURCE_ROOT/Assets/Shaders` にした。
- 検証（`.harness/runs/20261012-030203/`）:
  - `check-SCN-ECS-FIX-VISRESOLVE-CONST-cpu.txt`: Debug の RenderGraphTextureUsageContractTest（新しい検査を含む 14 件）・RenderGraphParallelPlanTest・RenderGraphParallelRecordTest が Passed。
  - `verify-SCN-ECS-FIX-VISRESOLVE-CONST-1.txt`: RelWithDebInfo の Game のビルド EXIT=0。
  - `verify-…-2.txt`（引数なし。SCN-ECS-MEASURE-RT で既定が off になったので off）・`verify-…-3.txt`（off）・`verify-…-4.txt`（`render-parallel-record=on` を足した）: default の決定論比較がどれも pass（mean_luminance の差 0、psnr 100 dB、不一致の画素 5E-06）、EXIT=0。3 本とも default.Game.log の ERROR 0。on は `RENDER_PARALLEL_RECORD enabled=1`・`RENDER_PARALLEL_RECORD_LISTS count=4 frame=1`。on・off の PNG を開いて確認（天球・地面・球・岩・家が基準どおり）。
- SCN-ECS-RT-PASSES の残りの指摘はこれで解け、status を done にした。
- Next: SCN-ECS-RT-FRAME-SCOPES（反復 35 の Next の並びどおり）。
- Notes: done-when の「on と off の起動画面の比較」は、既定が off になったため verify の 1 本目（引数なし）が off になる。on は `-GameArgs render-parallel-record=on` で別に撮った（`.harness/runs/startup-capture/SCN-ECS-FIX-VISRESOLVE-CONST-on`）。

### SCN-ECS-RT-FRAME-SCOPES（2026-10-12、反復 37・run 20261012-030203）

- RenderThread の 1 フレーム（rt の計測と同じ `RenderFrame` と `ReleasePacket`）を、重ならない直下の区間で切った（カテゴリ `RenderThread`）: `RT.ApplyPacket`（パケットの差分の適用・撮影の受付）→ `RT.WaitFrameFence`・`RT.AcquireImage`（`VulkanSwapChain::BeginFrame`）→ `RT.PrepareFrame`（完了の通知・退役・履歴・インスタンスの転送・リストの用意）→ `RT.Streaming`（VT とジオメトリのページ）→ `RT.BeginRecording`（記録の開始・タイルの転送・GPU の時間の取り込み）→ `RT.BuildAccelerationStructures` → `RT.RenderViews`（`RenderFrameExecutor::Execute`。`RenderGraph.Compile`・`RenderGraph.Execute` を含む）→ `RT.RecordTail`（overlay・表示のコピー・保留のコマンド・撮影のコピー・リストの終了）→ `RT.Submit`・`RT.Present`（`VulkanSwapChain::EndFrame`。提出の窓口の寿命は変えず、表示の前で区間だけ閉じる）→ `RT.FinishFrame`（統計・作り直しの確認）→ `RT.ReleasePacket`。`RT.WaitPacket`（パケットを受け取るまでの待ち）は rt の計測の外なので、直下の和に入れない。
- `RenderGraph.Execute` の直下: 既存の `RenderGraph.PlanRecording`・`RG.Record.*` に、`RG.SegmentBegin`（区間の頭の保留のコマンドの流しと layout の計画）・`RG.SegmentEnd`（区間の終わりの layout の書き戻し・確定）・`RG.Barriers`（パスの前の流し・バリア・layout の書き戻し・GPU の区間の開始）・`RG.FlushPending`（パスの後の保留のコマンドの流し・GPU の区間の終了）・`RG.TraceRecord`（Serial のパスの `RG.Record.*` をトレースへ書き込む費用）、Parallel の並びの `RG.PrepareParallel`・`RG.ParallelRecord`・`RG.ParallelFinish` を足した。
- 1 フレームに何十回も通る 5 つ（`RG.SegmentBegin`・`RG.SegmentEnd`・`RG.Barriers`・`RG.FlushPending`・`RG.TraceRecord`）は時間を足し込み、Execute の終わりに 1 件ずつ記録する（`Rendering/SequentialStatScope.h` の `AccumulatingStatTimer`。順に切る区間は同じヘッダの `SequentialStatScope`）。1 件ずつ記録した版は 1 フレームに約 100 件増え、トレースのミューテックスとファイルへの書き込み（ゲームスレッドと競る）で Execute が 11.4 → 13.6 ms に伸び、その費用がどの子にも入らず Execute の被覆が 77.9% だった。区間はすべて `NORVES_ENABLE_STATS` の中で、トレースが無効なら記録しない（Release では何もしない）。
- 表（mega-stress 5万個 close・RelWithDebInfo・1920x1080・300 フレーム。単位 ms）。CaptureScene の `cpu_scope_ms` はゲームスレッドのフレーム番号ごとの合計で、ゲームスレッドが描画 1 フレームの間に数百回回るため、1 フレームに何度も出る区間の median が描画 1 フレーム分にならない。そこでトレースから RenderThread の記録を `RT.WaitPacket` で描画 1 フレームずつに区切り、同じ窓（ゲームスレッドのフレーム 154468〜228176、299 フレーム）で名前ごとの合計の median・p95 を求めた（`.harness/runs/20261012-030203/check-SCN-ECS-RT-FRAME-SCOPES-breakdown.txt`）。

| RenderThread の直下 | median | p95 |
|---|---|---|
| `RT.ApplyPacket` | 0.00 | 0.00 |
| `RT.WaitFrameFence` | 30.04 | 31.40 |
| `RT.AcquireImage` | 0.01 | 0.01 |
| `RT.PrepareFrame` | 0.96 | 1.35 |
| `RT.Streaming` | 0.31 | 0.40 |
| `RT.BeginRecording` | 0.25 | 0.59 |
| `RT.BuildAccelerationStructures` | 0.59 | 0.74 |
| `RT.RenderViews` | 12.95 | 14.92 |
| `RT.RecordTail` | 0.02 | 0.04 |
| `RT.Submit` | 0.06 | 0.09 |
| `RT.Present` | 0.05 | 0.07 |
| `RT.FinishFrame` | 0.01 | 0.33 |
| `RT.ReleasePacket` | 0.03 | 0.03 |
| 直下の median の和 | 45.28 | — |
| （参考）フレームごとの直下の和 | 45.40 | 49.53 |
| rt（metrics.json） | 46.12 | 49.27 |
| （rt の外）`RT.WaitPacket` | 0.00 | 0.00 |

| `RT.RenderViews` の内訳 | median | p95 |
|---|---|---|
| `RenderGraph.Compile` | 0.64 | 1.23 |
| `RenderGraph.Execute` | 12.02 | 13.90 |

| `RenderGraph.Execute` の直下 | median | p95 |
|---|---|---|
| `RenderGraph.PlanRecording` | 0.01 | 0.01 |
| `RG.SegmentBegin` | 0.01 | 0.01 |
| `RG.Barriers` | 0.05 | 0.06 |
| `RG.FlushPending` | 0.17 | 0.21 |
| `RG.SegmentEnd` | 0.01 | 0.01 |
| `RG.TraceRecord` | 1.24 | 2.60 |
| `RG.Record.MegaGeometryPass` | 8.11 | 9.40 |
| `RG.Record.VisibilityRasterPass` | 0.73 | 1.08 |
| `RG.Record.LightingPass` | 0.44 | 0.52 |
| `RG.Record.ShadowMapPass` | 0.40 | 0.50 |
| `RG.Record.VirtualShadowMapPass` | 0.23 | 0.32 |
| `RG.Record.VisibilityResolvePass` | 0.10 | 0.14 |
| `RG.Record.BloomPass` | 0.03 | 0.04 |
| `RG.Record.AutoExposurePass` | 0.02 | 0.03 |
| `RG.Record.MaterialTileClassifyPass` | 0.02 | 0.04 |
| `RG.Record.SSAOPass` | 0.01 | 0.01 |
| `RG.Record.SSRPass` | 0.01 | 0.01 |
| `RG.Record.FXAAPass` | 0.00 | 0.01 |
| `RG.Record.PresentationPass` | 0.00 | 0.01 |
| `RG.Record.ToneMappingPass` | 0.00 | 0.01 |
| `RG.Record.VignettePass` | 0.00 | 0.01 |
| `RG.Record.DebugDrawPass` | 0.00 | 0.00 |
| `RG.Record.DepthOfFieldPass` | 0.00 | 0.00 |
| `RG.Record.ForwardPass` | 0.00 | 0.00 |
| `RG.Record.GBufferPass` | 0.00 | 0.00 |
| `RG.Record.MotionBlurPass` | 0.00 | 0.00 |
| `RG.Record.NeuralMaterialDecodePass` | 0.00 | 0.00 |
| `RG.Record.SkinningComputePass` | 0.00 | 0.00 |
| `RG.Record.SkyAtmospherePass` | 0.00 | 0.00 |
| `RG.Record.UpscalePass` | 0.00 | 0.00 |
| `RG.Record.VolumetricsPass` | 0.00 | 0.00 |
| 直下の median の和 | 11.59 | — |
| （参考）フレームごとの直下の和 | 11.93 | 13.77 |
| `RenderGraph.Execute` | 12.02 | 13.90 |

- 判定: RenderThread の直下の median の和 45.28 は rt の median 46.12 の 98.2%（描画 1 フレームごとの直下の和で見た被覆は median 98.9%・最小 95.3%）。`RenderGraph.Execute` の直下の median の和 11.59 は Execute の median 12.02 の 96.4%（フレームごとの被覆は median 99.6%・最小 92.1%）。どちらも 90% 以上。Parallel の区間は既定（off）のこの撮影には出ない。
- 読み: rt の 65% は `RT.WaitFrameFence`（30.0 ms。GPU の 30.5 ms とほぼ同じ）で、`MAX_FRAMES_IN_FLIGHT = 1` の見込みどおり、記録と GPU が重ならず足し算になっている。記録の道は `RT.RenderViews` 13.0 ms で、その 2/3 が `RG.Record.MegaGeometryPass`（中の `BuildSceneTables` 7.5 ms）。提出と表示は合わせて 0.1 ms。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-RT-FRAME-SCOPES-1.txt`: RelWithDebInfo の Game のビルド EXIT=0（C の警告・エラーなし）。
  - `verify-SCN-ECS-RT-FRAME-SCOPES-2.txt`: 撮影 `result=pass`、EXIT=0、300 フレーム（GPU median 30.51 / rt median 46.12・p95 49.27）。close.Game.log の ERROR 0。
  - `verify-SCN-ECS-RT-FRAME-SCOPES-3.txt`: `CHECK_CAPTURE_METRICS result=pass views=close`、EXIT=0（`RT.WaitFrameFence`・`RT.Submit`・`RT.Present` を要求）。
- Next: SCN-ECS-FIX-LIGHTING-RECORD（反復 35 の Next の並びどおり）。
- Notes: `RG.TraceRecord` の 1.2 ms（p95 2.6 ms）は計測そのものの費用で、区間 1 件の書き込みが平均 約 50 µs かかっている（ゲームスレッドが同じトレースへ 1 フレーム 0.2 ms の周期で書き続け、ミューテックスを競る）。以後の RenderThread の項目の判定で rt を比べるときは、記録の件数が変わると rt も動くことに注意する。描画 1 フレームごとの集計はまだ CaptureScene に無い（今回は使い捨ての集計で表を作った）。Release の構成のビルドは今回走らせていない（足した区間はすべて `NORVES_ENABLE_STATS` の中か、何もしない版を持つ）。

### SCN-ECS-RT-FRAME-SCOPES の差し戻しへの対応（2026-10-12、反復 38・run 20261012-030203）
- Done: 上の表の「残りの `RG.Record.*`（19 パス）」の 1 行を、19 区間の median・p95 の個別の行に置き換えた（値は `check-SCN-ECS-RT-FRAME-SCOPES-breakdown.txt` のまま。19 行の median の和は 0.09 で、直下の和 11.59 は変わらない）。コードは変えていないので撮影はやり直さず、反復 37 の撮影を使う。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-RT-FRAME-SCOPES-4.txt`: RelWithDebInfo の Game のビルド EXIT=0（エラー・C の警告なし）。
  - `verify-SCN-ECS-RT-FRAME-SCOPES-5.txt`: 反復 37 の撮影に対する `CHECK_CAPTURE_METRICS result=pass views=close`、EXIT=0（`RT.WaitFrameFence`・`RT.Submit`・`RT.Present` を要求）。
- Next: SCN-ECS-FIX-LIGHTING-RECORD。

### SCN-ECS-FIX-LIGHTING-RECORD（2026-10-12、反復 39・run 20261012-030203）

- 原因（バナナ 1万本 overview・RelWithDebInfo・300 フレーム。`LightingPass` の Execute の中を区間で切った撮影 `.harness/runs/scene-capture/SCN-ECS-FIX-LIGHTING-RECORD-diag*` のトレースから median）: `RG.Record.LightingPass` の 3.6 ms のうち 3.44 ms が RTGI の準備（`ExecuteRTGI`）。残りは VSM のパラメータ 0.08 ms、他の区間（入力の解決・照明の定数・DDGI・束縛・記述子の更新・描画の積み込み）は合わせて 0.03 ms。RTGI の中は、静止判定の署名 1.77 ms（レイトレーシングの instance 1万本 × 約 210 バイトを 1 バイトずつの FNV-1a で畳み込み、乗算が直列につながる）・instance の表の作成 1.35 ms（毎フレーム 1.1 MB の配列の確保・伸長と解放、頂点・インデックスのバッファの共有の参照を 2 万本複製、並べ替え、`std::isfinite`・`std::round` の呼び出し。RelWithDebInfo は `/Ob1` で inline の付かない関数は展開されない）・表の書き込み 0.13 ms・検証の走査（`IsComplete`・`HasShadowCasters`）0.07 ms・発光の表 0.04 ms。mega-stress（0.42 ms）との差は、バナナが RTGI の instance を 1万本持つこと。VSM の 0.08 ms は段の切り替えの距離（`VirtualShadowMapLevelDistanceThresholds`。段ごとに最大 80 回の二分法）を毎フレーム求め直していたもの。
- 直し（変わったときだけ行う）:
  - `TryBuildRTGIInstanceData` を 1 回の走査にまとめた: instance の表の行・BLAS の欠けと影を落とす物体の有無（`IsComplete`・`HasShadowCasters` と同じ条件）・発光の表・静止判定の署名を同じ走査で求める。表はメンバ（`m_RTGIInstances`）にフレームをまたいで持ち、行ごとに 8 バイトずつ比べて変わった行だけ書き換え、表のバッファへは変わったフレーム（とバッファを作り直したとき）だけ書く。スナップショットは CustomIndex の順なので、並んでいないときだけ並べ替える。頂点・インデックスのバッファは続けて同じなら重ねて持たない。有限の判定はビットで見て、走査で呼ぶ補助の関数に inline を付けた。
  - 静止判定の署名の instance の部分は、4 本の独立した列で 8 バイトずつ畳み込む（各列の 1 歩は全単射なので 1 語の変化は必ず署名を変える）。比べる値（変換を原点の位置と各軸の拡大率にそろえた表の行・材質の BaseColor・texture のハンドル）と、拡大率の 1/1024 の刻みは前と同じ。
  - `VirtualShadowMapLevelThresholdCache`（`VirtualShadowMapSample.h`）: 段の切り替えの距離を、設定・画角・画面の高さが変わったときだけ求め直す。`BuildVirtualShadowMapSampleParams` の末尾の省略できる引数で渡し、`LightingPass` が持つ（`ShadowProbePass` は従来どおり毎回求める）。
- 結果: `RG.Record.LightingPass` の median 3.63 → 0.373 ms、p95 5.39 → 0.454 ms（トレースの全 636 件）。rt の median 19.83 → 16.73 ms（SCN-ECS-MEASURE-RT の on との比較）。表の書き込みは静止したバナナの撮影の間 2 回だけ。撮影の平均の輝度は直す前の撮影と同じ 122.379。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIX-LIGHTING-RECORD-1.txt`: RelWithDebInfo の Game のビルド EXIT=0（エラー・C の警告なし）。
  - `verify-SCN-ECS-FIX-LIGHTING-RECORD-2.txt`: 撮影 `result=pass`、EXIT=0、300 フレーム（GPU median 8.02 ms、rt median 16.73 ms）。`RG.Record.LightingPass=0.373/max=0.626`。overview.Game.log の ERROR 0、`INDIRECT_LIGHTING source=rtgi`（RTGI は有効のまま）。PNG を開いて確認（床・バナナ・影・空が直す前と同じ）。
  - `verify-SCN-ECS-FIX-LIGHTING-RECORD-3.txt`: `CHECK_CAPTURE_METRICS result=pass views=overview`、EXIT=0（`RG.Record.LightingPass` の median ≤ 0.5）。
  - `verify-SCN-ECS-FIX-LIGHTING-RECORD-4.txt`: 起動画面 default の決定論比較 pass（mean_luminance の差 0、psnr 100 dB、不一致の画素 1.1E-05・最大差 1、`indirect_lighting=rtgi`）、EXIT=0。default.Game.log の ERROR 0。PNG を開いて確認（天球・地面・球・岩・家が基準どおり）。
  - `check-SCN-ECS-FIX-LIGHTING-RECORD-rtgi-test.txt`: Debug の `RTGIDiffuseIndirectVulkanTest`（静止の年齢の上限・instance や材質や露出の変化で静止が切れること・多ビューポートの組を含む）が Passed。
- Next: SCN-MEGA-FIX-SCENE-TABLES-DELTA（反復 35 の Next の並びどおり）。
- Notes: 原因を探すために足した区間（`Lighting.*`）は外した（区間 1 件のトレースへの書き込みが約 50〜90 µs かかり、`RG.Record.LightingPass` に入るため）。mega-stress 5万個 close は撮り直していない（RTGI の instance を持たない構成なので、変わるのは VSM の 0.08 ms の分だけの見込み）。同じ instance 1万本を毎フレーム走査する処理が `RT.BuildAccelerationStructures`（1.17 ms。TLAS の入力の照合）にも残っている。

### SCN-MEGA-FIX-SCENE-TABLES-DELTA（2026-10-12、反復 40・run 20261012-030203）

- 直し: `MegaGeometryPass` が表（`m_SceneTables`）をフレームをまたいで持ち続け、並びが変わらないフレームは直したインスタンスの行だけを書き換える（`PatchSceneTables`）。
  - 全部を作る（`ComposeSceneTables`）のは、インスタンスの作り直し（Setup の作り直し・手で足した・消した）・席の表の番号の決め直し・メッシュの区画の持ち主の手放し・表へ載せたメッシュの準備や中身（区画・アドレス・境界・BVH）の変化・載らなかったメッシュの準備完了・BVH の使える状態の変化・直したインスタンスのメッシュの差し替えのとき。確かめは表へ載せたメッシュごと（`SceneMeshSource`）と載らなかったメッシュごと（`SceneSkippedMesh`）で、インスタンスごとの引き直しをしない。
  - 差分は `PatchInstancesFromChangedSlots` が直した位置（`MarkSceneTableDirty`）の行の「見えた」区画の要求・描く順の行の区画の先頭・影の行と記録だけを直す。投影物かどうかが変わったときだけ影の判定のワークグループの先頭を数え直す。VSM の無効化は、直した行と、ページの常駐が変わったメッシュの行だけを積む（前と同じ条件）。
  - 「見えた」の区画の表の番号（`m_VisibilityBuildStamp`）は全部を作ったときだけ進める（回収の判定は「前の 2 回の全部の作り直しに載らなかった席」になる）。
  - 開発ビルドの検査 `CheckSceneTables`: 表を作った 120 回ごとに、何も変えずに全部を作り直した表（区画は今の割り当てを引くだけ）と、今の表の全部の行・合計・影の記録を比べ、`MEGA_TABLES mismatch=<通算の食い違った照合の数> checks=… full=… delta=… delta_rows=…` を出す。食い違えば `MEGA_TABLES_MISMATCH` を出し、次の表を全部作る。照合は `BuildSceneTables` の区間の外（`MegaGeometryPass.CheckSceneTables`。5万個で 1 回 約 8.3 ms）。
- 結果（mega-stress 5万個 close・RelWithDebInfo・300 フレーム）: `MegaGeometryPass.BuildSceneTables` の median 7.18 → 0.005 ms（静止）・0.141 ms（1% 移動。1 フレーム 500 行を差分で直す）。静止の rt の median 46.12 → 38.13 ms。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-MEGA-FIX-SCENE-TABLES-DELTA-1.txt`: RelWithDebInfo の Game のビルド EXIT=0（エラー・C の警告なし）。
  - `verify-…-2.txt`: 静止の撮影 `result=pass`、EXIT=0、300 フレーム（GPU median 30.46 / rt median 38.13・p95 41.55）。close.Game.log の ERROR 0、`MEGA_TABLES mismatch=0` が 5 回（checks=1〜5、2 回目以降は full=0 delta=120）。PNG を開いて確認（塔・岩・家・バナナ・影が描けている）。
  - `verify-…-3.txt`: `CHECK_CAPTURE_METRICS result=pass views=close`、EXIT=0（`BuildSceneTables` の median ≤ 0.5）。
  - `verify-…-4.txt`: 1% 移動の撮影 `result=pass`、EXIT=0。close.Game.log の ERROR 0、`MEGA_TABLES mismatch=0` が 5 回（delta_rows=60000 / 120 回）。
  - `verify-…-5.txt`: `CHECK_CAPTURE_METRICS result=pass views=close`、EXIT=0（`BuildSceneTables` の median ≤ 1.0）。
  - `verify-…-6.txt`: 起動画面 default の決定論比較 pass（mean_luminance の差 0、psnr 100 dB、不一致の画素 5E-06・最大差 1、`indirect_lighting=rtgi`）、EXIT=0。default.Game.log の ERROR 0・`MEGA_TABLES mismatch=0`。PNG を開いて確認（天球・地面・球・岩・家が基準どおり）。
- Next: SCN-ECS-FIF-AUDIT（反復 35 の Next の並びどおり）。
- Notes: 照合は撮影の 1% 移動で影の記録・投影物の数え直しを含めて通ったが、投影物かどうかの切り替え・メッシュの差し替え・ページの常駐の変化を狙って起こす試験は足していない（差分の経路の CPU の試験が無い）。`CheckSceneTables` の 1 回 8.3 ms は 120 フレームに 1 回 RenderThread に乗る（開発ビルドだけ。Release では呼ばない）。

### SCN-ECS-FIX-LIGHTING-RECORD の差し戻しへの対応（2026-10-12、反復 41・run 20261012-030203）

- 指摘: `ExecuteRTGI` の中で例外が出ると、両方の catch は履歴だけを無効化し、instance の表のアップロード済みの印（`m_bRTGIInstancesUploaded`）を残していた。CPU の表（`m_RTGIInstances`）を途中まで書き換えた後に例外が出ると、次のフレームの比較は変化なしと見て、GPU の表に古い行が残る。
- 直し: `ExecuteRTGI` の両方の catch の先頭で `m_bRTGIInstancesUploaded = false` にする。次に表を作れたフレームで表を必ず書き直す（CPU の表は差分の比較で今のスナップショットにそろうので、書き直せば GPU の表と一致する）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIX-LIGHTING-RECORD-5.txt`: RelWithDebInfo の Game のビルド EXIT=0（エラー・C の警告なし）。
  - `verify-SCN-ECS-FIX-LIGHTING-RECORD-6.txt`: 撮影 `result=pass`、EXIT=0、300 フレーム（GPU median 9.06 ms、rt median 16.61 ms）。`RG.Record.LightingPass=0.373/max=1.492`。overview.Game.log の ERROR 0、`INDIRECT_LIGHTING source=rtgi`。平均の輝度 122.379（反復 39 と同じ）。PNG を開いて確認（床・バナナ・影・空が描けている）。
  - `verify-SCN-ECS-FIX-LIGHTING-RECORD-7.txt`: `CHECK_CAPTURE_METRICS result=pass views=overview`、EXIT=0（`RG.Record.LightingPass` の median ≤ 0.5）。
  - `verify-SCN-ECS-FIX-LIGHTING-RECORD-8.txt`: 起動画面 default の決定論比較 pass（mean_luminance の差 0、psnr 100 dB、不一致の画素 1.4E-05・最大差 1、`indirect_lighting=rtgi`）、EXIT=0。default.Game.log の ERROR 0。PNG を開いて確認（天球・地面・球・岩・家が基準どおり）。
- Next: SCN-ECS-FIF-AUDIT（反復 40 の Next どおり）。
- Notes: 配列の確保の失敗を注入して例外の経路を実機で起こす試験は無い（直しはコードの経路で確かめた）。

### SCN-ECS-FIX-GT-PACING（2026-10-12、反復 42・run 20261012-030203）

- 直し: `RenderThread::NotifyNewFrame` が、前に渡したパケットを RenderThread が受け取る（`RenderLoop` が `m_CurrentPacket` を外す）まで `m_IdleCondition` で待ってから次のパケットを渡す。GameThread の先行は最大 1 フレーム（RenderThread が描くパケット・待っているパケット・書いているパケットの 3 枚で、パケットの数 3 と合う）。待ちは停止・終了の印（致命的な失敗を含む）・Running でない状態でも解ける。受け取った直後に `RenderLoop` が `m_IdleCondition` を起こす。待った時間は区間 `GT.WaitRenderThread`（GameThread）。
  - 開発ビルドの `--gt-pacing=on|off`（Game が環境変数 `NORVES_GT_PACING` に渡し、RenderThread が最初のパケットで 1 回だけ読む。`stage=gt_pacing enabled=<0|1>` をログへ出す）。off は前の動き（受け取られていないパケットを捨てて最新に差し替える）。
  - 渡したパケット・捨てたパケットの数を RenderThread が数え（`GetFramePacingStats`。`RenderWorld::GetFramePacingStats` から読む）、ApplicationProcessor が撮影の PNG の保存・`--exit-after-rendered-frames`・`--exit-after-frames` の終了の直前に `FRAME_PACING gt_frames=<n> rt_frames=<n> dropped=<n> pacing=<on|off>` を出す。
- 結果（mega-stress 5万個 close・RelWithDebInfo・300 フレーム）: `FRAME_PACING gt_frames=693 rt_frames=693 dropped=0`。窓の GameThread のフレーム数（`cpu_scope_frames`）87171 → 300。`RG.TraceRecord` の median 1.2 → 0.107 ms（p95 2.6 → 0.156 ms）でトレースのスレッドごとの溜めは不要だった。rt の median 38.13 → 35.66 ms、GPU の median 29.90 ms、`GT.WaitRenderThread` の median 35.14 ms。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIX-GT-PACING-1.txt`: RelWithDebInfo の Game のビルド EXIT=0（エラー・C の警告なし）。
  - `verify-SCN-ECS-FIX-GT-PACING-2.txt`: 撮影 `result=pass`、EXIT=0、300 フレーム。close.Game.log の ERROR 0、`stage=gt_pacing enabled=1`。PNG を開いて確認（塔・岩・家・バナナ・影・空が描けている）。
  - `verify-SCN-ECS-FIX-GT-PACING-3.txt`: `CHECK_CAPTURE_METRICS result=pass views=close`、EXIT=0（`GT.WaitRenderThread` を要求、`RG.TraceRecord` の median ≤ 0.2）。
  - `verify-SCN-ECS-FIX-GT-PACING-4.txt`: `FRAME_PACING` の照合 EXIT=0（`gt_frames=693 rt_frames=693 dropped=0`）。
  - `verify-SCN-ECS-FIX-GT-PACING-5.txt`: 起動画面 default の決定論比較 pass（mean_luminance の差 0、psnr 100 dB、不一致の画素 8E-06・最大差 1、`indirect_lighting=rtgi`）、EXIT=0。default.Game.log の ERROR 0。PNG を開いて確認（天球・地面・球・岩・家が基準どおり）。
  - `verify-SCN-ECS-FIX-GT-PACING-6.txt`: 周回 `-Cycles 2` は `SCENE_CYCLE_TOO_FEW`（複数のシーンでは 2 周目が基準なので 3 周以上が要る）で fail、EXIT=1。資源の差（`changed=none`）・落ち着かない周回は無し。
  - `verify-SCN-ECS-FIX-GT-PACING-7.txt`: 周回 `-Cycles 3`（rendering3dtest・mega-bananas・mega-stress）`SCENE_CYCLE_SMOKE result=pass changed=none unsettled=0`、EXIT=0。Game.log の ERROR 0。シーンの切り替えと終了で待ち合いが詰まらない。
  - `check-SCN-ECS-FIX-GT-PACING-off.txt`: `--gt-pacing=off`（mega-stress 5000 個・200 描画フレーム）で `FRAME_PACING gt_frames=134081 rt_frames=200 dropped=133879 pacing=off`、EXIT=0（前の動きに戻る）。
- Next: SCN-ECS-FIF-AUDIT。
- Notes: このタスクの verify の周回を `-Cycles 2` から `-Cycles 3` に直した（前の項目の複数のシーンの周回と同じ数。比べる周回が増えるので厳しくなる側）。SCN-ECS-FIF-SWITCH の verify にも同じ `-Cycles 2` が残っている。決定論の撮影（起動画面の比較）は 1 本のスレッドで描くので `FRAME_PACING gt_frames=0 rt_frames=0`。GameThread の Tick の dt は描画の間隔（mega-stress で約 35 ms）になる。固定の段は今のまま、周回と撮影で見た目・資源の差は出ていないが、dt に依存するゲームの処理を個別に確かめる試験は無い。窓を最小化したとき RenderThread はスワップチェーンの NotReady ですぐ返すので待ちは詰まらないが、GameThread は描画に合わせて回らなくなる（前と同じく全速で回る）。

### SCN-ECS-FIF-AUDIT（2026-10-12、反復 43・run 20261012-030203）

- 調べた範囲: `Library/Core/Private|Public/RHI`、`Library/Core/Private|Public/Rendering` の全パス（`->Update(` を持つ全ファイル）、`Library/Modules/ImGui`。コードは変えていない。
- 前提（確かめた事実）:
  - フレームの待ちは `VulkanSwapChain::BeginFrame` の `m_inFlightFences[m_currentFrame]` だけ（区間 `RT.WaitFrameFence`）。提出は `imageAvailable` しか待たない。
  - 使うキューはグラフィックスの 1 本だけ（compute・transfer のキューを使う所は無い）。同じキューの提出は提出の順に並ぶが、バリアが無ければ前の提出と重なって走りうる。バリアの第 1 の範囲は提出の順で前のコマンドを全部含む。
  - `ViewRenderContext::FrameIndex` は `m_currentFrame`。main のリスト（`m_CommandList`）は仕事の番号 0 番として `PrepareJobCommandLists` が `SetFrameIndex(frameIndex)` を呼ぶ（スロット 0 に固定ではない）。
  - `WaitIdle` は Resize・Canvas のビューの破棄・`RenderWorld::WaitForRender`・終了だけで、毎フレームの経路には無い。シーンの出入りは `WaitForRender` で両方のスロットを待つ。
  - `FRAME_PACKET_BUFFER_COUNT = 3`。パケットのスロットは GPU の完了を保証しない（反復 44 で訂正）: RenderThread は `RenderFrame`（提出まで）の直後に `ReleasePacket` → `FinishRead` でパケットを Empty へ戻し（`RenderThread.cpp:511`、`FramePacket.h:1302`）、`AcquireForRead` は最新の Ready を取るので、`--gt-pacing=off` や未消費のパケットの置き換えで同じスロットを続けて描くことがある。2 にすると 2 回目の `BeginFrame` は別の飛行中のスロットのフェンスしか待たないので、1 回目の提出が未完了のまま同じパケットのスロットの資源を書き換えうる。パケットのスロットで引いてよいのは CPU だけが読む資源（パケットの本体、ImGui の `ImDrawData` の写し）だけ。
  - RHI の資源のデストラクタ（`VulkanBuffer`・`VulkanTexture`・`VulkanFramebuffer`・`VulkanRenderPass`・`VulkanPipeline`・`VulkanDescriptorSet`・`VulkanSampler`・`VulkanAccelerationStructure`）はその場で `vkDestroy*` を呼ぶ（`VulkanTexture` の更新待ちの経路だけ例外）。今は前の提出が `BeginFrame` の待ちで終わっているので、記録の中で古いものを手放しても壊れない（反復 44 で追加）。
- 表（向き: C→G = CPU が書き GPU が読む、G→C = 読み戻し、G→G = GPU だけが書いて読み、フレームをまたいで 1 つで持つ）:

| 資源 | 持ち主 | 向き | 今の数え方 | 2 にしたとき | 直す項目 |
|---|---|---|---|---|---|
| フェンス・`imageAvailable`・提出の通し番号・通知待ちのリスト | `RHI/Vulkan/VulkanSwapChain.cpp` | — | スロットごと（MAX で作る） | 壊れない | — |
| `renderFinished` のセマフォ | `VulkanSwapChain.cpp:332,417` | G→表示 | スロットごと | 前の present が待つセマフォを次の提出が signal しうる | FIF-SWAPCHAIN（画像ごと） |
| 旧の `Present()` | `ISwapChain.h:107`、`VulkanSwapChain.cpp:132` | — | 呼び出し元なし | 呼ぶとフェンスを reset して提出せず止まる | FIF-SWAPCHAIN（消す） |
| コマンドバッファ・記録し直しの禁止の番号・リストの記述子プール | `VulkanCommandList.cpp:336-356,483-487,916-922` | C→G | スロットごと（`MAX_COMMAND_BUFFERS=3`） | 壊れない | — |
| 一時資源の lease（AS の構築の入力） | `VulkanCommandList.h:715,751` | C→G | スロットごと | 壊れない | — |
| GPU の時間の計測（query） | `VulkanCommandList.cpp:2353,2411,2487-2510` | G→C | スロットごと、完了の通知の後に読む | 壊れない | — |
| `TransientResourcePool`（RenderGraph の一時資源） | `RHI/TransientResourcePool.cpp:61-166` | G→G | スロット＋通し番号（同じスロットの前の通し番号だけ再利用） | 壊れない | — |
| `InstanceBufferRing` | `Rendering/InstanceBufferRing.cpp` | C→G | スロットごと、拡張は Upload の回数で遅延解放（1 フレーム 1 回） | 壊れない | — |
| `FrameUseRing` を使う資源（GBufferDebug・VSM の Pass／Pages の Mark・Allocate・Clear・Invalidation／Raster 2 つ・MaterialTileClassify・MegaGeometry の FrameSlot・ShadowProbe・Skinning・VisibilityMerge・VisibilityRaster の FrameSlot と Use・VisibilityResolve・VisibilitySwRaster） | 各パス | C→G | スロット＋通し番号（上限 4） | 壊れない | — |
| PathTracing の定数・instance の表、DDGI の定数・instance・結果、RayTracingShadow の定数・記述子・可視性、AutoExposure の定数 | `PathTracingPass.inl:715-745`、`DDGIProbePass.cpp:387-410`、`RayTracingShadowPass.cpp:177-214`、`AutoExposurePass.inl:488-530` | C→G | FrameIndex ごと | 壊れない | — |
| スキンのパレット・前のパレット | `SkinnedMeshGpuStore.cpp:100-177` | C→G | 変わったら作り直し（書き直さない） | 壊れない | — |
| MegaGeometry の散布の stage（`SceneStaging`）・区画の表・ページの表 | `MegaGeometryPass.cpp:2693,4299,4723` | C→G | FrameSlot（スロットごと） | 壊れない | — |
| MegaGeometry の常駐の表（slot・instance・shadow・mesh） | `MegaGeometryPass.cpp:4011-4171` | G→G（散布の compute が stage から書く） | 1 つ。拡張の古いバッファは 8 描画フレーム後に解放 | フレームの頭のバリアで守れる。8 > 2 で解放も足りる | FIF-FRAME-ORDER |
| ImGui の `ImDrawData` の写し（`m_SlotSnapshots`） | `Modules/ImGui/.../ImGuiOverlayPass.cpp:349-384` | CPU だけ（GT が書き RT が読む） | パケットのスロット（3） | 壊れない（パケットの状態で GT と RT が排他） | — |
| ImGui の頂点・インデックス・UBO・記述子（`m_VertexRing`・`m_IndexRing`・`m_UniformRing`・`m_SlotDescriptorSets`） | `Modules/ImGui/.../ImGuiOverlayPass.cpp:259-311,447-483` | C→G | パケットのスロット（`OverlayPacketSlotIndex`） | パケットのスロットは GPU の完了を保証しないので、前の提出が読む頂点・UBO を書き換えうる（反復 44 で訂正） | FIF-IMGUI（飛行中のスロットで引く） |
| TLAS のキャッシュ（`RayTracingSceneSubsystem` の `m_TopLevelCache[パケットのスロット]`、構造を in-place で Update） | `RenderingCoordinator.cpp:1719-1790` | G→G（Update は GPU が書く。入力の一致の判定は CPU だけ） | パケットのスロット | パケットのスロットは GPU の完了を保証しない。前の提出が読む TLAS を次のフレームの Update が書きうる（反復 44 で訂正） | FIF-FRAME-ORDER（フレームの頭のバリアで順序づけ、コメントを直す） |
| TLAS のインスタンスの表と scratch | `VulkanCommandList.cpp:1606-1700` | C→G | 呼び出しごとに lease（コマンドリストのスロット） | 壊れない | — |
| TLAS・BLAS を作り直したときの古い構造 | `RenderingCoordinator.cpp:1719-1790`（置き換え）、`VulkanAccelerationStructure.h` | — | 置き換えでその場で破棄 | 前の提出が使う構造を壊しうる | FIF-DEFERRED-DESTROY-MEMORY |
| 表示の Blit の記述子 `m_BlitDescriptorSet` | `RenderingCoordinator.cpp:5492-5494` | C→G | 1 組を毎フレーム束ね直す | 前のフレームが使う記述子を書き換える | FIF-FRAME-ORDER |
| 表示の合成の記述子 `m_CompositeAlphaOverDescriptorSet`（反復 44 で追加） | 持ち主 `RenderingCoordinator.cpp:3956-3979,5357`、書き換え `CompositePass.cpp:259-263` | C→G | 1 組を毎フレーム束ね直して `Update` | 前のフレームが使う記述子を書き換える | FIF-FRAME-ORDER（飛行中のスロットごとの組を渡す） |
| CompositePass が毎フレーム作る RenderPass・Framebuffer・Pipeline | `CompositePass.cpp:134-152,250-253` | — | 8 フレーム持ってから手放す | 8 > 2 で足りる | — |
| パスが描画先の替わりで作り直す Framebuffer・RenderPass・Pipeline・記述子・Sampler の古いもの（ToneMapping の `RenderPassSignature` など、`CreateFramebuffer`・`CreateDescriptorSet` を持つ約 40 ファイル）（反復 44 で追加） | 各パス、破棄は `VulkanFramebuffer.cpp:28`・`VulkanRenderPass.cpp:18`・`VulkanPipeline.cpp:99`・`VulkanDescriptorSet.cpp`・`VulkanSampler.cpp` | — | 置き換えでその場で `vkDestroy*` | 前の提出が使うものを壊しうる。RenderGraph の一時資源はスロットごとなので、2 にすると描画先が交互に替わり、毎フレーム作り直しうる | FIF-DEFERRED-DESTROY-PIPELINE（FIF-SWITCH で作り直しの数を見る） |
| パスが大きさの変化などで作り直す中間の Buffer・Texture の古いもの（反復 44 で追加） | 各パス、破棄は `VulkanBuffer.cpp:65-80`・`VulkanTexture.cpp:428` | — | 置き換えでその場で破棄（Resize は `WaitIdle` の後） | 前の提出が使うものを壊しうる | FIF-DEFERRED-DESTROY-MEMORY |
| overlay の framebuffer | `RenderingCoordinator.cpp:5442` | — | FrameIndex ごと | 壊れない | — |
| GPU→GPU の前のフレームの履歴（TAA の 2 枚、RTGI の履歴 2 組と denoise の出力、PathTracing の累積 2 枚〔ビューごと〕、VSM のページの状態〔Pool・PageTable・RequestBits・FreeList・DirtyList・Mega 系〕、HZB のピラミッド、AutoExposure のヒストグラムの作業バッファ、MegaGeometry の可視ビット・BVH のキュー・間接描画の配列、VisibilityMerge のキー） | 各パス | G→G | 1 つ（ping-pong は 2 枚） | 前の提出と重なって走りうる（今は BeginFrame の CPU 待ちが隠している） | FIF-FRAME-ORDER（フレームの頭の全体のバリア 1 本） |
| DynamicUniformAllocator の UBO と記述子（物体ごと。前の変換を含む） | `DynamicUniformAllocator.h`、使う所 `GBufferPass.cpp:746`・`ForwardPass.cpp:1262,1381`・`ShadowMapPass.cpp:1302-1857`・`DebugDrawPass.cpp:309` | C→G | 1 組、`Reset` で先頭へ戻す | 前のフレームが読む UBO・記述子を書き換える | FIF-DYNAMIC-UBO |
| LightingPass の照明の組（`m_ExecuteResourceSets[4]`） | `LightingPass.h:468-498`、`LightingPass.cpp:3301,4457,4715-4725,5183-5187` | C→G | 4 組を通し番号で 0 から使い直す（FrameIndex を見ない） | 同上 | FIF-LIGHTING |
| RTGI の定数・発光の表・instance の表・RTGI と denoise の記述子 | `LightingPass.cpp:3439-3616,4107-4187` | C→G | 1 つ（instance の表は変わった行だけ書き換え） | 同上。差分の書き換えはスロットごとの版が要る | FIF-LIGHTING |
| TAA・Bloom・ToneMapping・SSAO・MotionBlur の定数と記述子 | `TemporalAAPass.inl:404-421`、`BloomPass.cpp:699-938`、`ToneMappingPass.cpp:834-849`、`SSAOPass.cpp:862-887`、`MotionBlurPass.inl:344-362` | C→G | 1 組を Execute ごとに書き換え | 同上 | FIF-POST-A |
| SSR・DoF・Volumetrics・Vignette・FXAA・Upscale の定数と記述子、HZB の記述子 0（深度が替わったとき） | `SSRPass.cpp:725-757`、`DepthOfFieldPass.inl:330-339`、`VolumetricsPass.cpp:495-505`、`VignettePass.cpp:475-484`、`FXAAPass.cpp:493-504`、`UpscalePass.cpp:430`、`HiZPyramidPass.cpp:262-265` | C→G | 1 組 | 同上 | FIF-POST-B |
| PresentationPass・PresentationComposer の Blit の記述子、CanvasView の層の定数と記述子 | `PresentationPass.cpp:125,167`、`PresentationComposer.cpp:50`、`CanvasView.cpp:519,815-838` | C→G | 1 組 | 同上 | FIF-PRESENT |
| DDGI の照射・距離のアトラスと履歴 | `DDGIProbePass.h:66-89`、`DDGIProbePass.cpp:485-505,696` | G→G | FrameIndex ごと（履歴まで分かれる） | 履歴が 2 つに分かれ、2 フレームおきに積まれて交互に出る（ちらつき・収束が半分） | FIF-DDGI |
| VSM の統計・照明の VSM の統計・VT のフィードバック | `VirtualShadowMapPass.cpp:1125-1172`、`LightingPass.cpp:1615-1670`、`VirtualTextureFeedbackRing.h:50-52` | G→C | 枠ごと、完了の通し番号で読む | 壊れない | — |
| 転送のリング `TileUploader`（タイル・ミップテイル・ジオメトリのプールの区画への staging）（反復 44 で追加） | `TileUploader.h:7-60`、`RenderResources.cpp:1520-1560`（`BeginRetireFrame`・`CommitRetireFrame` が完了／提出の通し番号を渡す） | C→G | リングの区画を、それをコピーしたフレームの提出の通し番号が完了するまで再利用しない。重なるコピーは次のフレームへ送り、フレームをまたげば遷移のバリアが順序を作る | 壊れない | — |
| ジオメトリのページの要求 `GeometryPageRequestRing`（反復 44 で追加） | `GeometryPageRequestRing.h:7-110`、`RenderResources.cpp:1520-1560` | G→C | 3 枠、提出の通し番号が完了し、かつ書いてから `MinReadAgeFrames` 以上経った枠だけ読む | 壊れない。3 枠とも使用中のフレームは要求を取らずに進む（取りこぼしの数は `GetStats` に出る） | —（FIF-SWITCH で取れなかったフレームの数を見る） |
| 遅延解放 `GpuRetireQueue`（資源・sparse のページ・ジオメトリのプールの区画）（反復 44 で追加） | `GpuRetireQueue.h:7-80`、使う所 `GpuResourceStore.cpp`・`MegaGeometryResourceStore.cpp`・`MegaGeometryPass.cpp`・`SparsePagePool.h`・`GeometryPool.h`・`VirtualTextureStreamer.h`、`RenderResources.cpp:1520-1560` | — | 解放を頼んだ時点で最後に提出した通し番号（記録中ならそのフレームの通し番号）の完了まで生かす | 壊れない | — |
| 撮影の読み戻し | `FrameCaptureReadbackHelper.cpp:146-308`（`RenderingCoordinator.cpp:2466` で MAX から枠を作る） | G→C | スロットごと、同じスロットの BeginFrame の後に Map | 壊れない。読めるのが 2 フレーム後になる | —（FIF-SWITCH で撮影の終わりの待ちを確かめる） |
| AutoExposure のヒストグラムの読み戻し | `AutoExposurePass.inl:271-346,558-566` | G→C | FrameIndex ごと、同じ枠のフェンスの後に読む | 壊れない。露出の応答が 1 フレーム遅れる | —（FIF-SWITCH の起動画面の比較で差を見る） |
| ShadowProbe の統計 | `ShadowProbePass.cpp:432-440,602-608` | G→C | 4 枠を Execute の回数（`+2`）で読む | 1 フレームに 2 回以上 Execute すると未完了の提出を読む | FIF-READBACK-GATES |
| MegaGeometry の統計 | `MegaGeometryPass.cpp:2529-2538,2881` | G→C | 4 枠を描画フレームの数（`+2`）で読む | 2 では足りるが、飛行中 2 以下の前提に依存 | FIF-READBACK-GATES |
| device-local の `Update` の staging・BLAS の同期の構築 | `VulkanBuffer.cpp:167-194`、`VulkanTexture.cpp:1039-1172`、`VulkanDevice.cpp:1820-1955`、`RenderingCoordinator.cpp:1671-1708` | C→G | 呼び出しごとに staging、提出の後に `graphicsQueue.waitIdle()` | 正しいが、フレームの途中で呼ぶと前のフレームの GPU の完了まで止まり、重なりが消える | FIF-SYNC-SUBMIT-SCOPE（見えるようにする） |
| PersistentResourceCache | `RenderingCoordinator.cpp:2402`（`nullptr` を渡す） | — | 未接続 | 影響なし | — |
| FramePacket の本体 | `FramePacket.h:1188-1310`、`RenderThread.cpp:511`、`RenderingCoordinator.cpp:5750` | CPU だけ（GPU は直接読まない） | 3 枚、提出の直後に `FinishRead` で Empty へ戻す（GPU の完了は待たない。反復 44 で訂正。適用の確認は `RenderingCoordinator.cpp:3717-3729` で完了の通し番号に合わせる） | パケット自体は壊れない。`Clear` で手放す RHI 資源の参照が最後の参照なら、その場で破棄される | FIF-DEFERRED-DESTROY-*（破棄の側で守る） |

- 足した項目（SCN-ECS-FIF-SWITCH の前。反復 44 で IMGUI と DEFERRED-DESTROY の 2 つを足した。DEFERRED-DESTROY-PIPELINE は SWAPCHAIN と `VulkanSwapChain.cpp` を、DEFERRED-DESTROY-MEMORY は PIPELINE と `VulkanDevice` を共有するので、この順で 1 つずつ行う。他は書き込みパスが交わらない）: SCN-ECS-FIF-FRAME-ORDER → SCN-ECS-FIF-IMGUI → SCN-ECS-FIF-SWAPCHAIN → SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE → SCN-ECS-FIF-DEFERRED-DESTROY-MEMORY → SCN-ECS-FIF-DYNAMIC-UBO → SCN-ECS-FIF-LIGHTING → SCN-ECS-FIF-POST-A → SCN-ECS-FIF-POST-B → SCN-ECS-FIF-PRESENT → SCN-ECS-FIF-DDGI → SCN-ECS-FIF-READBACK-GATES → SCN-ECS-FIF-SYNC-SUBMIT-SCOPE。各項目は MAX_FRAMES_IN_FLIGHT=1 のまま、起動画面の比較が pass する形で書いた。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-AUDIT-1.txt`: `^## SCN-ECS-FIF-(?!AUDIT|SWITCH)` の見出しが TASKS.md にある、EXIT=0。
  - 反復の始めの RelWithDebInfo の Game のビルド EXIT=0（コードは変えていない）。
- Next: SCN-ECS-FIF-FRAME-ORDER。
- Notes: 2 にしたときの意味の変化（壊れはしない）: 撮影の読み戻しが 2 フレーム後に読める、AutoExposure の露出が 1 フレーム遅れる。FIF-SWITCH の起動画面の比較（決定論の撮影は 1 本のスレッドで描くので差が出ない見込み）と撮影の終わりの待ち（`--exit-after-rendered-frames` の前に最後の読み戻しが届くか）で確かめる。同期の検証（VK_LAYER の synchronization validation）を撮影の台本で有効にする口は無い。FIF-SWITCH の Debug の撮影は通常の検証レイヤーだけを見る。

### SCN-ECS-FIF-AUDIT 差し戻しの直し（2026-10-12、反復 44・run 20261012-030203）

- 評価（反復 43）の NEEDS_WORK の 3 点をコードで確かめ、上の表と前提と TASKS.md を直した。コードは変えていない。
  1. パケットのスロットは GPU の完了を保証しない: `RenderThread.cpp:511` は提出の直後に `ReleasePacket` を呼び、`FramePacket.h:1302` の `FinishRead` は GPU を待たずに Empty へ戻す。`AcquireForRead` は最新の Ready を取るので同じスロットを続けて描くことがある。前提の行・FramePacket の行・ImGui の行・TLAS の行を訂正し、ImGui の GPU 資源を飛行中のスロットで引く SCN-ECS-FIF-IMGUI を足した。TLAS の in-place の Update は G→G に分け直し、FIF-FRAME-ORDER のフレームの頭のバリアで順序づける形にした（パケットのスロットの条件 `GetMaxFramesInFlight() + 1 <= FRAME_PACKET_BUFFER_COUNT` は完了条件から外した）。
  2. `m_CompositeAlphaOverDescriptorSet`（`RenderingCoordinator.cpp:5357` が渡し、`CompositePass.cpp:259-263` が毎フレーム束ね直して `Update`）を表に足し、FIF-FRAME-ORDER の完了条件に飛行中のスロットごとの組を足した。CompositePass が毎フレーム作る RenderPass・Framebuffer・Pipeline は 8 フレーム持つので足りる。
  3. `TileUploader`・`GeometryPageRequestRing`・`GpuRetireQueue` を表に足した（3 つとも提出／完了の通し番号で再利用・読み戻し・解放を決めるので変更不要）。
- 洗い直しで見つけた漏れ: RHI の資源のデストラクタはその場で `vkDestroy*` を呼ぶ（`VulkanFramebuffer.cpp:28` など）。パスが描画先の替わりで作り直す framebuffer・記述子・pipeline、作り直す中間の texture・buffer、置き換える TLAS・BLAS の古いものを、2 にすると前の提出が使っている間に壊しうる。RenderGraph の一時資源はスロットごとなので、2 にすると描画先が交互に替わる。破棄を提出の完了まで遅らせる SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE と SCN-ECS-FIF-DEFERRED-DESTROY-MEMORY を足した。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-AUDIT-2.txt`: `^## SCN-ECS-FIF-(?!AUDIT|SWITCH)` の見出しが TASKS.md にある、EXIT=0（足した項目の見出し 14 件を同じファイルに並べた）。
- Next: SCN-ECS-FIF-FRAME-ORDER。
- Notes: 描画先が交互に替わることで framebuffer などを毎フレーム作り直す費用は、壊れはしない（DEFERRED-DESTROY の後）が CPU の時間を食いうる。FIF-SWITCH で `RHI.DeferredDestroy` の数を見て、毎フレーム増えるならパスの作り直しを描画先ごとのキャッシュにする項目を足す。

### SCN-ECS-FIF-FRAME-ORDER（2026-10-12、反復 45・run 20261012-030203）

- `ICommandList::GlobalMemoryBarrier` を足した（既定は何もしない。Vulkan は `VkMemoryBarrier` 1 本、src/dst とも ALL_COMMANDS、MEMORY_WRITE → MEMORY_READ|MEMORY_WRITE、render pass の中では例外）。`RenderingCoordinator` が仕事の番号 0 番（`m_CommandList`）の `BeginRecording` の直後、タイルのコピー（`RecordTileUploads`）と TLAS の Update（`BuildAccelerationStructures`）の記録より前に 1 回張る。
- Blit の記述子を `m_BlitDescriptorSets`、表示の合成の記述子を `m_CompositeAlphaOverDescriptorSets`（どちらも飛行中のスロットの数 = `GetMaxFramesInFlight()` 組）にし、フレームの `frameIndex`（`ViewRenderContext::FrameIndex` と同じ値）で引く。PresentationComposer・PresentationPass・予備の Blit・CompositePass はこのフレームの組だけを束ね直す。スワップチェーンの作り直しでは全組の encode の定数を束ね直す。
- TLAS のキャッシュの「スキップ」のコメントを、パケットのスロットは GPU の完了を保証せず、順序はフレームの頭のバリアで守る形に書き直した（条件は足していない）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-FRAME-ORDER-1.txt`: RelWithDebInfo の Game のビルド EXIT=0。
  - `verify-SCN-ECS-FIF-FRAME-ORDER-2.txt`: Debug の起動画面の撮影 result=pass、`metrics.json` の `vulkan_validation` は error_count=0・warning_count=0・vuid_count=0。
  - `verify-SCN-ECS-FIF-FRAME-ORDER-3.txt`: RelWithDebInfo の決定論の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=9E-06・within_limits=True、result=pass、EXIT=0。
- Next: SCN-ECS-FIF-IMGUI。
- Notes: メンバの改名に合わせて `Test/Core/Rendering/BoardComponentRoutingTest.cpp` の 1 行（合成の記述子が作られていないことの assert）を `.empty()` に追従させた（paths の外だが改名の機械的な追従）。このテストはビルドが通ることだけ確かめた（RelWithDebInfo、EXIT=0。実行はしていない）。

### SCN-FIX-M9-T0-CAPTURE（2026-10-12、反復 6・run 20261012-030203）— BLOCKED

- 原因: M9 の t0 の撮影は `SourceMissingTransferSrc`（フレーム 0、撮影元 PresentationColor）で失敗する。撮影元は 1280x720・RGBA16F・用途 `ShaderRead|RenderTarget` で `TransferSrc` が無い。これは `VignettePass` の出力 `VignetteOutput`（`ToneMappedColor` を上書きする）で、TemporalAA が効くと FXAA が止まり、拡大なしの Upscale が `ToneMappedColor` をそのまま `PresentationColor` に出すので、Vignette の出力が撮影元になる。他の後処理の出力はすべて `TransferSrc` を足しているが `VignetteOutput` だけ足していない。Rendering3DTest はカメラのグレーディングでビネットを有効にしている（SS-POST-TUNE 以降）。このブランチの変更（ランチャー・`--window-background`・ECS の同期）は原因ではない。
- Done（paths の内側）: `FrameCaptureTypes.h` に `GetFrameCaptureResultStatusName` を足し、`FrameCaptureReadbackHelper` の失敗の発行で `FRAME_CAPTURE_FAILED status=… request=… frame=… source_kind=…`、転送元の用途が無いときは `FRAME_CAPTURE_SOURCE width=… height=… format=… usage=…` をログに出す。`Rendering3DTestRoutine.cpp` の t0・陰性対照・t1 の撮影の失敗で `M9_WORLD_SMOKE capture_status=…` を出す。`Scripts/Test-M9World.ps1` の Game の起動 2 本に `--window-background` を足した。
- BLOCKED: 直すには paths の外の `Library/Core/Private/Rendering/VignettePass.cpp`（`VignetteOutput` の desc に `TransferSrc` を足す 1 行）が要る。`blocked/SCN-FIX-M9-T0-CAPTURE.md` に選択肢を書いた（推奨: paths に足して 1 行直す。描画の結果は変わらない）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-FIX-M9-T0-CAPTURE-1.txt`: Debug の Game のビルド EXIT=0、error 0（`-5-build.txt` は診断ログを足した後の再ビルドで EXIT=0）。
  - `-2.txt`・`-4.txt`: `ctest -R ^M9WorldAcceptanceTest$` は EXIT=8（未修正のため）。通常起動（`--window-background` 付き）は通り、M9 のスモークが t0 の撮影で落ちる。
  - `-3-gamelog.txt`: スモークの Game.log の抜粋（`FRAME_CAPTURE_SOURCE … format=9 usage=0x5`・`FRAME_CAPTURE_FAILED status=SourceMissingTransferSrc`・`capture_status=SourceMissingTransferSrc`）。
- Next: 人が paths を広げて todo に戻したら、VignettePass の 1 行を直して M9 を通す。

### SCN-ECS-FIF-IMGUI（2026-10-12、反復 46・run 20261012-030203）

- `ImGuiOverlayPass` の頂点・インデックス・UBO のリングのスロットと `m_SlotDescriptorSets` を、パケットのスロットでなく飛行中のスロット（`context.FrameIndex`）で引くようにした。組の数は `IN_FLIGHT_SET_COUNT`（= `FRAME_PACKET_BUFFER_COUNT` = 3）。`FrameIndex` がそれ以上なら描かずに警告を出す。CPU の写し（`m_SlotSnapshots`）はパケットのスロット（`OverlayPacketSlotIndex`）のまま。
- ヘッダとコメントの「スロット排他で安全」「同一スロットは回収済みになるまで再書込みされない」を、パケットのスロットは GPU の完了を保証せず、GPU 資源は `BeginFrame` のフェンスで前の利用の完了が保証される飛行中のスロットで引く、という決まりに書き直した。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-IMGUI-1.txt`: RelWithDebInfo の Game のビルド（`ImGuiOverlayPass.cpp` を再コンパイル）EXIT=0。
  - `verify-SCN-ECS-FIF-IMGUI-2.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=9E-06・within_limits=True、result=pass、EXIT=0。
  - `verify-SCN-ECS-FIF-IMGUI-3.txt`: ランチャー → rendering3dtest → ランチャーの周回 3 周 result=pass、EXIT=0。`Game.log` に ImGui の `Initialize(seam)` があり、新しい警告（`exceeds in-flight set count`）と ERROR は 0 件。
- Next: TASKS.md の次の未完（SCN-ECS-FIF 系の残り）。
- Notes: `ViewRenderContext::OverlayPacketSlotIndex` は CPU の写しの添字として引き続き使う（paths の外なので触っていない。そのコメントの「プール排他で安全」は CPU の写しについては正しい）。

### SCN-ECS-FIF-SWAPCHAIN（2026-10-12、反復 47・run 20261012-030203）

- `VulkanSwapChain` の `m_renderFinishedSemaphores` を飛行中のスロットごとからスワップチェーンの画像ごとにした。提出の signal と present の wait は `m_currentImageIndex` で引く。`CreateRenderFinishedSemaphores` が画像の数だけ作り（作成時と Resize の作り直しで呼ぶ）、`CleanupSwapChain` が破棄する。`imageAvailable` とフェンスはスロットごとのまま。`BeginFrame` の同期の揃いの検査はセマフォの数＝画像の数に、`EndFrame` の束の検査に画像の添字の範囲を足した。
- 呼び出し元の無い `ISwapChain::Present` と `VulkanSwapChain::Present` を消し、テストの偽物（`Test/Core/Rendering/SkinnedRenderPathContractTest.cpp` の `Present` の override）も合わせて消した（notes の指示による。paths の外の 1 行）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-SWAPCHAIN-1.txt`: RelWithDebInfo の Game のビルド（`VulkanSwapChain.cpp` を再コンパイル）EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-SWAPCHAIN-2.txt`: Debug の起動画面の撮影 result=pass、`vulkan_validation` は error_count=0・warning_count=0・vuid_count=0（`default.Validation.log` は 0 行）。
  - `verify-SCN-ECS-FIF-SWAPCHAIN-3.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=4E-06・within_limits=True、result=pass、EXIT=0。
- Next: TASKS.md の次の未完（SCN-ECS-FIF 系の残り）。
- Notes: 提出が拒まれたとき `imageAvailable` の signal が残る既存の問題（`EndFrame` の早期 return）は MAX に依らないので未対応のまま。Resize で画像の数が変わったあとの `m_currentImageIndex` の古い値は、`EndFrame` の範囲の検査で `InvalidCommandList` として拒む。

### SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE（2026-10-12、反復 48・run 20261012-030203）

- `VulkanDevice` に破棄の列を足した（ハンドルの種類・ハンドル・積んだ時点の「次に提出する通し番号」。`m_deferredDestroyMutex` で守り、どのスレッドから積んでもよい）。`VulkanFramebuffer`・`VulkanRenderPass`・`VulkanPipelineLayout`・`VulkanPipeline`・`VulkanDescriptorSetLayout`・`VulkanDescriptorPool`・`VulkanDescriptorSet`（pipeline layout）・`VulkanSampler` のデストラクタは `vkDestroy*` の代わりに `DeferDestroy` でハンドルを列へ移す。記述子セットは専用のプールと一緒にプールの破棄で解放される。
- `VulkanSwapChain` は作成の最後に `BeginDeferredDestroyTracking`、`EndFrame` の提出の後に `NotifyDeferredDestroySubmitted`、`BeginFrame` のフェンスの待ちの後に `NotifyDeferredDestroyCompleted`（完了の通し番号以下を破棄）、破棄の `WaitIdle` の後に `EndDeferredDestroyTracking`（列を全部空ける）を呼ぶ。`WaitIdleInternal` の成功（と device lost）の後と device の破棄の前も列を全部空ける。
- 統計: 開発ビルドで `RHI.DeferredDestroy reason=… RHI.DeferredDestroy.Enqueued/Destroyed/Pending/Immediate=…` をログに出す（完了の通知 600 回ごと・スワップチェーンの破棄・device の破棄）。`Stats.h` に数の口が無いので、ログの行と `GetDeferredDestroyStats()` で出す。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE-2.txt`: Debug の起動画面の撮影 result=pass、`vulkan_validation` は error_count=0・warning_count=0・vuid_count=0。終了のログで `Pending=0`（110 フレームで Enqueued=Destroyed=7949）。
  - `verify-SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE-3.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=8E-06・within_limits=True、result=pass、EXIT=0。
  - `verify-SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE-4.txt`: rendering3dtest・mega-bananas・mega-stress の周回 3 周 result=pass、EXIT=0。終了のログで `Pending=0`（Enqueued=Destroyed=42010）、ERROR 0 行。
- Next: TASKS.md の次の未完（SCN-ECS-FIF 系の残り）。
- Notes: 提出を追うスワップチェーンが無い間（作成前・破棄後、スワップチェーンを作らないテスト）はその場で破棄する（`Immediate` に数える。今回の 2 本とも 0）。通し番号はスワップチェーンのものなので、スワップチェーンが同時に 2 つある形には対応していない（`Screen` が 1 つだけ作る）。起動画面は 1 フレームあたり約 70 個を積んでいる（読み込み込み）。周回のランチャーの待機の区間では増えていない。FIF-SWITCH で描画先の交互の替わりによる作り直しが毎フレーム増えるかを見る。

### SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE 差し戻しの対応（2026-10-12、反復 49・run 20261012-030203）

- 差し戻し: `WaitIdleInternal` がキューロックを手放した後に破棄の列を全部空けていたため、その間に別のスレッドが提出して積んだハンドルを、提出の完了前に破棄しうる。
- 対応: `vkDeviceWaitIdle` と同じキューロック（`m_sparseBindMutex`）の下で、破棄してよい分を列から退避し、ロックを手放した後に退避した分だけを破棄する。退避するのは通し番号が「次に提出する通し番号」より小さい分（待ちの前に提出が済んだ分）だけで、記録中のフレームの分（まだ提出していない通し番号）は通常の完了通知まで残す。提出の通知は提出の後に来るので、次に提出する通し番号が実際の提出より先へ進むことはない。完了通知と共通の取り出しを `TakeCompletedDeferredDestroyEntriesLocked` にした。ロックの順は キュー → 列 で、列のロックを持ってキューのロックを取る経路は無い。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE-5.txt`: RelWithDebInfo の Game のビルド（`VulkanDevice.cpp` を再コンパイル）EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE-6.txt`: Debug の起動画面の撮影 result=pass、`vulkan_validation` は error_count=0・warning_count=0・vuid_count=0。終了のログで `Pending=0`（Enqueued=Destroyed=7949）。
  - `verify-SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE-7.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=1.4E-05・within_limits=True、result=pass、EXIT=0。
  - `verify-SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE-8.txt`: rendering3dtest・mega-bananas・mega-stress の周回 3 周 result=pass、EXIT=0。終了のログで `Pending=0`（Enqueued=Destroyed=42010）、ERROR 0 行。
- Next: TASKS.md の次の未完（SCN-ECS-FIF 系の残り）。
- Notes: done-when の「WaitIdle の後は列を全部空ける」は、提出が済んだ分を全部空ける、と読んだ（まだ提出していないフレームの分を空けると、記録中のコマンドバッファが参照するハンドルを壊しうるため）。全部空けるのはスワップチェーンの破棄（WaitIdle の後、追う提出が無くなる）と device の破棄の前。競合そのものを再現するテストは書いていない（verify が名指ししていないため）。

### SCN-ECS-FIF-DEFERRED-DESTROY-MEMORY（2026-10-12、反復 50・run 20261012-030203）

- `VulkanDevice` の破棄の列に Buffer・Image・ImageView・加速構造・メモリの種類を足した（`DeferDestroy(vk::Buffer/Image/ImageView/AccelerationStructureKHR)`・`DeferFree(vk::DeviceMemory, sizeBytes)`）。列は積んだ順に破棄するので、使う側から先に積む（view → image → memory、buffer → memory、構造 → 下のバッファ）。
- `VulkanBuffer` のデストラクタは unmap の後に VkBuffer とメモリを列へ移す（コンストラクタの失敗の後始末はまだ提出に使われていないのでその場で破棄のまま）。`VulkanTexture` のデストラクタは配列とミップの view・view・所有する image・メモリを列へ移す（所有しない image は移さない。`DeferVulkanTextureResourcesUntilDeviceIdle` の経路はそのまま）。`VulkanAccelerationStructure` のデストラクタは構造を列へ移し、下のバッファはメンバの破棄でその後に積まれる。TLAS のキャッシュの置き換えと、パスが作り直した中間の texture・buffer の古いものはこれで前の提出の完了まで生きる。
- `VulkanGPUResourceAllocator` は `CreateBuffer` の `BufferPtr` を持つだけなので、`FreeBuffer` で参照を落とすと `VulkanBuffer` のデストラクタ経由で同じ列に乗る（別の対応は不要）。
- 統計: `RHI.DeferredDestroy.PendingMemoryBytes`（列に残るメモリの量）と `MaxPendingMemoryBytes`（その最大）をログの行に足した。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-DEFERRED-DESTROY-MEMORY-1.txt`: RelWithDebInfo の Game のビルド（`VulkanDevice.cpp`・`VulkanTexture.cpp`・`VulkanBuffer.cpp` を再コンパイル）EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-DEFERRED-DESTROY-MEMORY-2.txt`: Debug の起動画面の撮影 result=pass、`vulkan_validation` は error_count=0・warning_count=0・vuid_count=0（`default.Validation.log` は 0 行）。終了のログで `Pending=0`・`PendingMemoryBytes=0`（Enqueued=Destroyed=14305）。
  - `verify-SCN-ECS-FIF-DEFERRED-DESTROY-MEMORY-3.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=5E-06・within_limits=True、result=pass、EXIT=0。
  - `verify-SCN-ECS-FIF-DEFERRED-DESTROY-MEMORY-4.txt`: rendering3dtest・mega-bananas・mega-stress の周回 3 周 result=pass、EXIT=0。周回の台帳（textures・texture_bytes）は反復 49 と同じ値で、2 周目以降も増えない。定期の統計の行はどれも `Pending=0`・`PendingMemoryBytes=0`、終了のログで `Pending=0`（Enqueued=Destroyed=55706）、ERROR 0 行。
- Next: TASKS.md の次の未完（SCN-ECS-FIF 系の残り。SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE は人が todo に戻した）。
- Notes: 列に残ったメモリの最大は周回の走行中で約 1.08 GB（シーンを出るときにまとめて手放した分が 1 フレームほど残る）。終了時の 5.2 GB はスワップチェーンの破棄の前に全部手放した分で、その後の `SwapChainDestroy` で全部空く。スワップチェーンの画像の view は列へ移るので、Resize ではスワップチェーンの破棄の後に view が破棄される（view は使われないので破棄の順は問わない）。sparse のメモリ（`VulkanSparseMemoryBlock`）の解放は paths の外なのでその場のまま。行末: `VulkanDevice.*`・`VulkanTexture.*` は HEAD で CRLF と LF が混ざっているので、変えた行だけを周りに合わせて直した。

### SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE 差し戻しの対応 2 周目（2026-10-12、反復 51・run 20261012-030203）

- 差し戻し: `WaitIdle` の後に、記録中のフレームが無いのに「次に提出する通し番号」で積んだ分が列に残る（スワップチェーンの作成直後に手放した Sampler など）。完了条件を「記録中のフレームが無ければ全部空ける、記録中ならその記録が提出される通し番号の分だけ残す」に直したもの。
- 対応: `VulkanDevice` に記録中の印（`m_bDeferredDestroyFrameRecording`。列のミューテックスで守る）を足した。`VulkanSwapChain::BeginFrame` は画像の取得に成功した後に `NotifyDeferredDestroyFrameRecordingBegan` を呼び、`EndFrame` は提出の成功で `NotifyDeferredDestroySubmitted`（通し番号を進めるのと同じロックの下で印を消す）、束の拒否・通し番号の枯渇・フェンスの reset の失敗・提出の失敗で `NotifyDeferredDestroyFrameRecordingAborted` を呼ぶ。`EndDeferredDestroyTracking` も印を消す。`WaitIdleInternal` はキューロックの下で、印が無ければ列を全部退避し、印があれば次に提出する通し番号より小さい分だけを退避する（反復 49 のキューロックの下での退避はそのまま）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE-9.txt`: RelWithDebInfo の Game のビルド（`VulkanDevice.cpp`・`VulkanSwapChain.cpp` を再コンパイル）EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE-10.txt`: Debug の起動画面の撮影 result=pass、`vulkan_validation` は error_count=0・warning_count=0・vuid_count=0（`default.Validation.log` は 0 バイト）。画像は天球・地面・球・岩・家が以前どおり。終了のログで `Pending=0`・`PendingMemoryBytes=0`（Enqueued=Destroyed=14273）。
  - `verify-SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE-11.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=1.3E-05・within_limits=True、result=pass、EXIT=0。
  - `verify-SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE-12.txt`: rendering3dtest・mega-bananas・mega-stress の周回 3 周 result=pass、EXIT=0。定期の統計の行はどれも `Pending=0`、終了のログで `Pending=0`（Enqueued=Destroyed=52471）、ERROR 0 行。
- Next: TASKS.md の次の未完（SCN-ECS-FIF 系の残り）。
- Notes: 記録中の印は「BeginFrame の成功から提出（または EndFrame の失敗）まで」。呼び出し側が BeginFrame の後に EndFrame を呼ばずにフレームを捨てた場合は、次の提出まで印が残る（WaitIdle の後に次の通し番号の分が次の提出の完了まで残るだけで、早く壊す側には倒れない）。競合や状態遷移を再現するテストは書いていない（verify が名指ししていないため）。

### SCN-ECS-FIF-FRAME-OWNER（2026-10-12、反復 52・run 20261012-030203）

- `Rendering/FrameResourceOwner.h`（ヘッダのみ）に、飛行中のスロットごとの「フレームの持ち主」`FrameResourceOwner` と、寿命を決める `FrameResourceOwners` を足した。段は 開始（`BeginFrame`: 完了した提出の分と、中止した・提出も中止もしないまま残った分を先に回収してから、このスロットの持ち主を新しい世代で記録中にする）→ 提出（`CommitSubmitted`: 提出の通し番号を結び付ける。0 は中止と同じ）または 中止（`AbortFrame`: 次の開始で回収）→ 回収（`Retire`: 結び付けた通し番号の完了で戻す。完了の通し番号が前より小さくなったら振り直しとして提出済みを全部戻す）。借りる口は `IFrameResourceAllocator` を持ち主に登録し、回収で `OnFrameResourcesReclaimed` が呼ばれる（中身のリング・記述子はまだ無い）。
- 世代は開始のたびに全スロットを通して増える。借りた物は `GetGeneration()` を持ち、`ValidateBorrowed` が開発ビルドで記録中のフレームの世代でなければ `FRAME_RESOURCE_STALE` を出して false を返す（最初の 8 回と、その後は 2 の冪の回だけログ）。
- `RenderingCoordinator` が `m_FrameResourceOwners` を持ち、スワップチェーンの `BeginFrame` の成功の後（`AdvanceCompletedRenderFrameSerial` の直後）に開始し、`ViewRenderContext::FrameResources` に渡す（全ビュー・ビューポート・overlay が同じ文脈を使う。並列の記録の区間は文脈を写すので同じ持ち主を指す）。提出の成功・失敗の分岐（本経路と、framebuffer の添字の範囲外の早期の提出の経路）で `CommitSubmitted` を呼び、`ReleaseInitializedResources` の `WaitIdle` の後に `Reset` する。
- 検査: `RenderGraphLifetimeTest` の束に `FrameResourceOwnerTest` を足した（同じフレームの 2 回目の Execute が前の分を戻さない、中止と提出も中止もしないまま次の開始が来た分の回収、2 スロットの回収、通し番号の振り直し、世代の拒否）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-FRAME-OWNER-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-FRAME-OWNER-2.txt`: Debug の `RenderGraphLifetimeTest` のビルド EXIT=0。
  - `verify-SCN-ECS-FIF-FRAME-OWNER-3.txt`: `ctest -R "^FrameResource"` で `FrameResourceOwnerTest` Passed（1/1）、EXIT=0。
  - `verify-SCN-ECS-FIF-FRAME-OWNER-4.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=9E-06・within_limits=True、result=pass、EXIT=0。`default.Game.log` に `FRAME_RESOURCE_*`（`FRAME_RESOURCE_OWNER_UNRETIRED` を含む）と ERROR は 0 件。
- Next: SCN-ECS-FIF-UPLOAD-ARENA。
- Notes: 持ち主はヘッダのみにした（`Library/Core/CMakeLists.txt` が paths の外のため。`FrameUseRing.h` と同じ形）。開始の時点でこのスロットの提出が回収されていなければ（スワップチェーンのフェンスの待ちの後なので本来は起きない）`FRAME_RESOURCE_OWNER_UNRETIRED` の警告を出して回収して進める。`Resize` の `WaitIdle` の後は振り直しの検出に任せ、`Reset` は呼んでいない。

### SCN-ECS-FIF-UPLOAD-ARENA（2026-10-12、反復 53・run 20261012-030203）

- `Rendering/FrameUploadArena.h`（ヘッダのみ）に、飛行中のスロットごとの CPU から書ける領域 `FrameUploadArena` を足した。常にマップしたままのページ（`CPUAccessible` = HOST_VISIBLE | HOST_COHERENT。定数・頂点・インデックス・ストレージのどれにも束縛できる使い道）の列で、1 フレームの中は前へ進むだけ、足りなければページを足す（ページより大きな切れ端はその大きさを 64 KiB 単位に丸めたページ）。返した切れ端は回収まで動かさず、前のページへ戻らないので上書きもしない。回収で先頭へ戻り、ページは使い回す。寿命は持ち主の段（回収）だけで決まり、Upload の回数は見ない。借りるのは排他の下で、並列の記録のワーカーから呼んでよい。
- 持ち主（`FrameResourceOwner`）に `AllocateUpload(size, usage)`・`AllocateUploadData(data, size, usage)`・`AllocateConstants<T>(const T&)` を足した。返す `FrameUploadAllocation` は `Buffer + Offset + Size`（記述子には `GetOffset32`/`GetSize32`）と `CPUAddress`・世代を持ち、`ValidateBorrowed(allocation, name)` で世代を確かめられる。書き込みはマップしたままのページへの memcpy で、`Buffer::Update` は通さない。記録中でない・大きさ 0・範囲の上限超え・ページを作れないときは無効の切れ端を返す（`FRAME_RESOURCE_UPLOAD_NOT_RECORDING`・`FRAME_RESOURCE_UPLOAD_TOO_LARGE`・`FRAME_RESOURCE_UPLOAD_PAGE_FAILED`）。
- 整列と範囲の上限は `FrameUploadLimits::FromCapabilities` がデバイスの能力から決める。`DeviceCapabilities` に `MaxUniformBufferRange`・`MinUniformBufferOffsetAlignment`・`MinStorageBufferOffsetAlignment` を足し、`VulkanDevice` が物理デバイスの limits から入れる（不明なら 256 と 16384、整列の下限は 16。頂点・インデックスは 16）。この環境では定数 64・ストレージ 16・定数の上限 65536。
- `RenderingCoordinator` は最初のフレームの開始の前に `DeviceFrameUploadPageSource` をデバイスで設定し（`FRAME_RESOURCE_UPLOAD_CONFIG` のログ）、`ReleaseInitializedResources` の `WaitIdle`・`Reset` の後にページを手放す。開発ビルドの `--frame-arena-initial-kb=<n>`（1〜1048576。`NORVES_FRAME_ARENA_INITIAL_KB` で描画側へ渡す）で 1 ページの大きさ（既定 1 MiB）を変えられる。
- 統計（開発ビルド）: `FrameResource.Upload reason=… FrameResource.Upload.Allocations/Bytes/PagesAdded/Failures`（最後に記録を終えたフレームの値）と `MaxFrameBytes`・`TotalPagesAdded`・`Pages`・`PageBytes` を 1 行で出す。出すのは最初のフレーム・600 フレームごと・ページを足したフレーム・失敗したフレーム・ページを手放すとき。
- 検査: `RenderGraphLifetimeTest` の束に `FrameResourceUploadArenaTest` を足した（使い道ごとの整列と範囲の上限、同じフレームの複数の借りが重ならず中身が残る・Update を通らずマップは 1 回、小さなページでページを足した後も前の切れ端が有効、大きな切れ端のページ、回収で先頭へ戻り同じページを使い回す・完了していないスロットは戻らない・中止は次の開始で戻る・前のフレームの切れ端の世代の拒否、記録中でないときの拒否、能力からの整列と上限）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-UPLOAD-ARENA-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-UPLOAD-ARENA-2.txt`: Debug の `RenderGraphLifetimeTest` のビルド EXIT=0。
  - `verify-SCN-ECS-FIF-UPLOAD-ARENA-3.txt`: `ctest -R "^FrameResource"` で `FrameResourceOwnerTest`・`FrameResourceUploadArenaTest` が Passed（2/2）、EXIT=0。
  - `verify-SCN-ECS-FIF-UPLOAD-ARENA-4.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=0・within_limits=True、result=pass、EXIT=0。`default.Game.log` に `FRAME_RESOURCE_UPLOAD_CONFIG page_bytes=1048576` と `FrameResource.Upload` の行（借りは 0）、ERROR は 0 件。
- Next: SCN-ECS-FIF-DESCRIPTOR-BORROW。
- Notes: まだ借りる側が無いので、起動画面では借りは 0・ページも 0（ページは最初の借りで作る）。`--frame-arena-initial-kb` でページを足す道を Game で通すのは、最初の利用者（SCN-ECS-FIF-DEBUGDRAW など）を移した後になる（今回は検査の小さなページで通した）。足したページは回収で手放さず使い回すので、一度だけ大きく借りたフレームの分はスロットに残る（縮める仕組みは無い）。持ち主・領域はヘッダのみ（`Library/Core/CMakeLists.txt` が paths の外のため）。ソースの `small` は Windows SDK のマクロと衝突するので使わない。

### SCN-ECS-FIF-UPLOAD-ARENA の差し戻しへの対応（2026-10-12、反復 54・run 20261012-030203）

- 差し戻し: 4 GiB を超える大きなページでは、定数・ストレージの切れ端のオフセットが uint32 を超える位置に置かれ、`GetOffset32` が切り詰めて別の範囲（先の頂点データ）を束縛してしまう。
- 対応: `FrameUploadArena::Allocate` で、定数・ストレージ（記述子の束縛が uint32 のオフセットと大きさを受け取る使い道）は、整列後のオフセットが `FrameUploadMaxDescriptorValue`（0xFFFFFFFF）を超える位置を飛ばして次のページ（無ければ新しいページの先頭）から返すようにした。大きさの上限も uint32 に収まる値に抑える（能力が 0 や uint32 超えのとき）。頂点・インデックスは uint64 のオフセットを受けるのでそのまま。前へ進むだけの決まりは変えない（飛ばしたページへは戻らない）。
- 検査: `FrameResourceUploadArenaTest` に、4 GiB を超える頂点の切れ端の後の定数・ストレージが別のページの uint32 に収まるオフセットで返り、`GetOffset32`/`GetSize32` が切り詰めないことを足した（中身を持たない大きさだけのページの偽物で、GPU のメモリは使わない）。進捗の表示を日本語にした。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-UPLOAD-ARENA-5.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-UPLOAD-ARENA-6.txt`: Debug の `RenderGraphLifetimeTest` のビルド EXIT=0。
  - `verify-SCN-ECS-FIF-UPLOAD-ARENA-7.txt`: `ctest -R "^FrameResource"` で `FrameResourceOwnerTest`・`FrameResourceUploadArenaTest` が Passed（2/2）、EXIT=0。
  - `verify-SCN-ECS-FIF-UPLOAD-ARENA-8.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=1.1E-05・within_limits=True、result=pass、EXIT=0。`default.Game.log` の ERROR は 0 件、画像は天球・地面・球・岩・家が以前どおり。
- Next: SCN-ECS-FIF-DESCRIPTOR-BORROW。
- Notes: この環境の `max_storage` は 4294967295（ログの `FRAME_RESOURCE_UPLOAD_CONFIG`）。飛ばした大きなページの残りはそのフレームでは使わない（次の回収で先頭へ戻る）。

### SCN-ECS-FIF-DESCRIPTOR-BORROW（2026-10-12、反復 55・run 20261012-030203）

- RHI に共有の layout とフレームごとの記述子の pool を足した。`IDevice::GetOrCreateDescriptorSetLayout(desc)` は束縛の形（binding の順にそろえた binding・Vulkan の型へ写した型・ステージ・要素数）のハッシュと並びの照合で、同じ形には 1 つの `IDescriptorSetLayout` を返す（RWBuffer と StructuredBuffer は同じ形）。`IDevice::CreateFrameDescriptorAllocator(capacity)` は `IFrameDescriptorAllocator`（型ごとの容量と最大セット数を持つ pool の列）を返す。Vulkan の実装（`VulkanFrameDescriptorAllocator`、`VulkanDescriptorSet.cpp`）は pool ごとの残りを自分で数え、入らなければ次の pool、無ければ pool を足す（容量を超える型を持つ layout はその layout が入る大きさの pool）。`Reset` は全部の pool を `vkResetDescriptorPool` で戻して使い回し、`Release` は破棄の列へ渡す。借りたセットは layout・pool・パイプラインレイアウトを作らない `VulkanDescriptorSet` で、pool の世代（Epoch）を持ち、回収の後は `IsExpired()` が true になって `Update` と `SetDescriptorSet` が拒む（`FRAME_RESOURCE_DESCRIPTOR_EXPIRED`）。確保と借りたセットの書き込みは pool ごとの排他の下。
- 持ち主（`FrameResourceOwner`）に `AllocateDescriptorSet(layout)` を足した。返す `FrameDescriptorAllocation` は `Set` と世代を持ち、`ValidateBorrowed(allocation, name)` で FRAME-OWNER の世代の検査を通す。回収で pool をまとめて戻す。`FrameResourceOwners::ConfigureDescriptors(device, capacity)` で全スロットに pool を作り、`ReleaseDescriptorPools` で手放す。`RenderingCoordinator` は upload の設定と同じ所で pool を作り（`FRAME_RESOURCE_DESCRIPTOR_CONFIG`）、`ReleaseInitializedResources` の `Reset` の後に手放す。共有の layout はデバイスの破棄の前（`DrainDeferredDestroyQueue` の前）に破棄の列へ渡す。既存の `CreateDescriptorSet` と Execute の中の共有（GBufferPass の材質ごと）はそのまま。
- 統計（開発ビルド）: `FrameResource.Descriptor reason=… FrameResource.Descriptor.Allocations/Writes/PoolsAdded/Failures/ExpiredRejects/AllocateUs/WriteUs/TotalPoolsAdded/Pools`（最後に記録を終えたフレームの値）。出すのは最初のフレーム・600 フレームごと・pool を足したフレーム・失敗や拒否のあったフレーム・手放すとき。
- 検査: `RHITextureUpdateVulkanTest` の束に `FrameResourceDescriptorVulkanTest` を足した（共有の layout、小さな容量で pool を足す道と容量を超える layout、完了していないスロットは戻らず完了で pool がまとめて戻り次のフレームは pool を足さない、前のフレームのセットの世代の拒否・IsExpired・書き込みの拒否、記録中でないときの拒否、検証レイヤーのエラー 0）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-DESCRIPTOR-BORROW-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-DESCRIPTOR-BORROW-2.txt`: Debug の `RHITextureUpdateVulkanTest` のビルド EXIT=0。
  - `verify-SCN-ECS-FIF-DESCRIPTOR-BORROW-3.txt`: `ctest -R "^FrameResourceDescriptor"` で `FrameResourceDescriptorVulkanTest` Passed（1/1）、EXIT=0（allocations=11・writes=10・pools_added=4、回収の後 pools_added=0、validation_errors=0）。
  - `verify-SCN-ECS-FIF-DESCRIPTOR-BORROW-4.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=2E-06・within_limits=True、result=pass、EXIT=0。`default.Game.log` に `FRAME_RESOURCE_DESCRIPTOR_CONFIG configured=1` と `FrameResource.Descriptor` の行（借りは 0・pool は 0）、ERROR は 0 件。画像は天球・地面・球・岩・家が以前どおり。
- Next: SCN-ECS-FIF-PARALLEL-ALLOC。
- Notes: まだ借りる側が無いので、起動画面では借りも pool も 0（pool は最初の借りで作る）。既定の容量は 1 pool に 256 セット・定数 512・sampled 512・sampler 256・storage buffer 512・storage image 256・結合 1024・加速構造 32（内部の値。最初の利用者を移したら統計で見直す）。借りたセットの対象（`VulkanDescriptorSet`）は借りるたびに作る（使い回しはしていない）。pool の排他は確保と書き込みで 1 つなので、並列の記録で競る分は SCN-ECS-FIF-PARALLEL-ALLOC で分ける。回収の後のセットの束縛の拒否（`SetDescriptorSet`）は検査で通していない（パイプラインが要るため。書き込みの拒否と `IsExpired` は通した）。

### SCN-ECS-FIF-DESCRIPTOR-BORROW の差し戻しへの対応（2026-10-12、反復 56・run 20261012-030203）

- 差し戻し: 共有の layout のキャッシュは遅らせる破棄の列（排他と列）より先に宣言されているので、デバイスの待機が失敗した終了の早期 return では列が先に破棄され、その後キャッシュに残った layout のデストラクタが `DeferDestroy` で破棄済みの排他を触る。
- 対応: `VulkanDescriptorSetLayout::DetachHandle`（ハンドルを破棄せずに手放す）を足し、`ReleaseSharedDescriptorLayouts(bDestroyHandles)` にした。待機失敗の道では他の資源と同じくハンドルを意図して残し、切り離してからキャッシュを空ける（`false`）。通常の終了は今までどおり破棄の列へ渡す（`true`）。デバイスより後まで持たれた layout も、切り離した後はデバイスに触らない。追加したエラーの説明文を日本語にした。
- 検査: `FrameResourceDescriptorVulkanTest` に待機失敗の終了を足した（キャッシュにだけ残る layout と持ち続ける layout を作り、待機の失敗を注入してデバイスを手放す。保護された終了の道を通り、持ち続けた layout のハンドルが切り離されていること）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-DESCRIPTOR-BORROW-5.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-DESCRIPTOR-BORROW-6.txt`: Debug の `RHITextureUpdateVulkanTest` のビルド EXIT=0。
  - `verify-SCN-ECS-FIF-DESCRIPTOR-BORROW-7.txt`: `ctest -R "^FrameResourceDescriptor"`（`-V` 付き）で `FrameResourceDescriptorVulkanTest` Passed（1/1）、待機失敗の終了も合格、validation_errors=0、EXIT=0。
  - `verify-SCN-ECS-FIF-DESCRIPTOR-BORROW-8.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=8E-06・within_limits=True、result=pass、EXIT=0。`default.Game.log` の ERROR は 0 件、画像は天球・地面・球・岩・家が以前どおり。
- Next: SCN-ECS-FIF-PARALLEL-ALLOC。
- Notes: 待機失敗の道で残すのは layout のハンドルだけ（デバイスと同じく意図した漏れ）。フレームの pool（`VulkanFrameDescriptorAllocator`）はデバイスより前に持ち主が手放す前提のまま（`RenderingCoordinator` の `ReleaseDescriptorPools`）。

### SCN-ECS-FIF-PARALLEL-ALLOC（2026-10-12、反復 57・run 20261012-030203）

- 借りる口を `FrameResourceLane`（`Rendering/FrameResourceOwner.h`）に分けた。口は CPU から書ける領域（`FrameUploadArena`）と記述子の pool を 1 組ずつ持ち、`AllocateUpload`・`AllocateUploadData`・`AllocateConstants`・`AllocateDescriptorSet`・`ValidateBorrowed` を持つ。持ち主（`FrameResourceOwner`）は口を継いで既定の口（番号 0）になり、区間の口は `AcquireLane`（RenderThread・記録中だけ。空いた口を使い回し、無ければ作る）と `ReleaseLane` で貸し借りする。口の裏のページと pool は持ち主に束ね、回収で全部の口を先頭へ戻し pool をまとめて戻す。回収の時点で返されていない口は `FRAME_RESOURCE_LANE_LEAKED` を出して返したものとして扱う。設定（`ConfigureUpload`・`ConfigureDescriptors`）・手放す処理・統計（`FrameResource.Upload`・`FrameResource.Descriptor` の 1 フレームの値とページ・pool の数）は全部の口を合計し、`FrameResource.Lanes`（作った区間の口の数）を足した。
- `ViewRenderContext::FrameResources` を `FrameResourceLane*` にした（直列の記録は持ち主自身を指す）。RenderGraph は Parallel の区間の文脈を写すとき、区間ごとに持ち主から口を借りて `FrameResources` に入れ、ワーカーの記録の後（途中で抜けるときも）返す（`ParallelFrameResourceLaneLease`）。持ち主が無い・記録中でないときは写した口のまま。
- ワーカーでページ・pool を足してよい条件を `FrameResourceLane` と `IRenderGraphPass.h` の準備段の約束に書いた（自分の区間の口だけ・デバイスでの作成だけで提出・待ち・バリアを伴わない・回収と手放すのは RenderThread）。
- 検査: `RenderGraphLifetimeTest` の束に `FrameResourceParallelAllocTest` を足した（8 区間のワーカーが同時に 400 回ずつ借り、小さなページでワーカーが同時にページを足す。直列の口と同じフレームの 2 回の区間の全部の切れ端が重ならず中身が残る、口をまたいで同じページを使わない、作成が同時に重なる、返した口の使い回し、統計の合計、回収の後のページの使い回しと前の世代の拒否。記録中でないときは口を貸さない、返し忘れの数え方、スロットごとに口が別）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-PARALLEL-ALLOC-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-PARALLEL-ALLOC-2.txt`: Debug の `RenderGraphLifetimeTest` のビルド EXIT=0。
  - `verify-SCN-ECS-FIF-PARALLEL-ALLOC-3.txt`: `ctest -R "^FrameResource"` で `FrameResourceDescriptorVulkanTest`・`FrameResourceOwnerTest`・`FrameResourceUploadArenaTest`・`FrameResourceParallelAllocTest` が Passed（4/4）、EXIT=0（ヘッダの変更を入れるため `RHITextureUpdateVulkanTest` も Debug で先にビルドした）。
  - `verify-SCN-ECS-FIF-PARALLEL-ALLOC-4.txt`: `--render-parallel-record=on` の Debug の起動画面の撮影 result=pass、`vulkan_validation` は error_count=0・warning_count=0・vuid_count=0（`default.Validation.log` は 0 バイト）。ログに `RG_PARALLEL_RECORD_PASSES` と `FrameResource.Lanes=2`、ERROR・`FRAME_RESOURCE_LANE_LEAKED` は 0 件。画像は天球・地面・球・岩・家が以前どおり。
  - `verify-SCN-ECS-FIF-PARALLEL-ALLOC-5.txt`: on の決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=9E-06・within_limits=True、result=pass、EXIT=0。
- Next: TASKS.md の次の未完（SCN-ECS-FIF 系の残り）。
- Notes: 排他つきの共有にはしていない（区間ごとに口を分けたので `FrameResource.LockWait` は測っていない。領域の排他は口ごとに残し、競らない）。区間の口の記述子の pool は口を作るときに `ConfigureDescriptors` と同じデバイス・容量で作る（pool そのものは最初の借りで作る）。まだ借りる側が無いので、Game では口の貸し借りだけが通り、借りは 0。口ごとの記述子の pool を複数のワーカーから同時に借りる検査（検証レイヤーのスレッドの検査つき）は書いていない（verify が名指ししていないため。GPU の束の `FrameResourceDescriptorVulkanTest` に足すのが候補）。

### SCN-ECS-FIF-REWRITE-GUARD（2026-10-12、反復 58・run 20261012-030203）

- 開発ビルドだけで動く「守られていない上書きの見張り」`RHI::FrameResourceWriteTracker`（`RHI/FrameResourceWriteTracker.h`、ヘッダのみ）を足し、`VulkanDevice` が 1 つ持つ（`IDevice::GetFrameResourceWriteTracker`。Release と他の描画バックエンドは nullptr）。資源ごとの状態（ID・世代・名前・書き込みの回数・前のフレームと今のフレームの参照の範囲・束縛したフレーム）は `VulkanBuffer`・`VulkanDescriptorSet` が持つ。説明は `Docs/Architecture/FrameResourceWriteGuard.md`。
  - 数える書き込み: `IBuffer::Update`、新しい `IBuffer::NoteExplicitWrite`（明示した書き込みの借り。`FrameUploadArena` は切れ端を返すたびに呼ぶ）、書き込みのある `IDescriptorSet::Update`。作成の直後の 1 回（大きさの変更は作り直しなので、その直後も）は数えない。任意の `Map` のポインタへの書き込みは捕まえられない（読み戻しの `Map` は書き込みとみなさない）ことを文書に書いた。
  - 参照: `VulkanCommandList` の記述子セットの束縛（セットが参照するバッファの範囲を含む）・頂点とインデックスの束縛・間接引数・`CopyBuffer` の両側・`FillBuffer`・`CopyBufferToTexture(Region)` の元。フレームの番号は `RenderingCoordinator` がフレームの持ち主の開始のたびに進める。
  - (2) 合否に使う: `FRAME_RESOURCE_UNPROTECTED resource=<名前> pass=<パス> kind=<buffer_previous_frame|buffer_recorded_same_frame|descriptor_bound_previous_frame|descriptor_bound_same_frame>`（名前・パス・種類の組ごとに初めての 1 回、id・gen・range・frame を付ける）と、`ReleaseInitializedResources` の `WaitIdle` の後に `FRAME_RESOURCE_UNPROTECTED_SUMMARY count=<組の数> resources= events= rewrites= frames=`。(1) 合否に使わない: `FRAME_RESOURCE_REWRITE resource= pass= frames= instances=`（名前とパスの組ごとに終わりに 1 回）。組ごとの資源の数は `FRAME_RESOURCE_UNPROTECTED_TOTAL`。
  - `pass=`: RenderGraph がパスの `Declare`・`PrepareParallelRecord`・`Execute` を、`RenderingCoordinator` が overlay のパスと Blit の記録を、その名前の区間（`RHI::FrameResourceWriteScope`、スレッドごと）で包む。パスの外は `RenderingCoordinator`。LightingPass の中から呼ぶ `DDGIProbePass::Execute` は自分の名前の区間を持つ。名前の末尾の番号は `#` にまとめる（`DynUBO_Slot12` → `DynUBO_Slot#`）。
  - 「守られている」の登録（名前ではなく作る所で）: フレームの持ち主の領域のページと借りた記述子セット、`FrameUseRing`（`Acquire` が今のパスの区間の残りで作る資源を登録。準備の段と記録の段で区間が分かれる MegaGeometry の `EnsureFrameSlot`・`EnsureBvhStageResources` は作る所を `FrameResourceProtectedCreationScope` で包む）、`DynamicBufferRing`（DebugDraw・ImGui のスロット）、`InstanceBufferRing`、飛行中のフレームの番号で引く枠（AutoExposure・PathTracing・RayTracingShadow・DDGI の `FindOrCreateFrameResources` など）、FIF-FRAME-ORDER の表示の Blit・合成の記述子の組。守られている資源は前のフレームとの重なりを見ず一覧にも載らないが、同じフレームの中の重なりは見る。
- (2) の一覧（起動画面 `default.Game.log` と、シーンの周回 rendering3dtest・mega-bananas・mega-stress × 3 の `scene-cycle-smoke/Game.log`。数は組ごとの資源の数。種類は全部 `*_previous_frame`、同じフレームの重なりは 0 件）:

| パス | 資源（`resource=`） | 種類 | 起動画面 | 周回 | 項目 |
|---|---|---|---|---|---|
| ShadowMapPass | `DynUBO_Slot#`（DynamicUniformAllocator の UBO） | buffer | 73 | 73 | DYNAMIC-UBO |
| ShadowMapPass | 同じ allocator の記述子（初期化の分 `RenderingCoordinator.DescriptorSet`、Grow の分 `ShadowMapPass.DescriptorSet`） | descriptor | 62 + 11 | 62 + 11 | DYNAMIC-UBO |
| LightingPass | `LightingParamsUBO`・`LightArraySSBO`・`LightingVsmSampleParams`・`LightingVsmPointSampleParams`・`LightingVsmSlices` | buffer | 各 1 | 各 1（LightArray 2） | LIGHTING |
| LightingPass | `RTGI.DiffuseIndirect.Parameters`・`.Emitters`・`.Instances` | buffer | 1・1・2 | 1・1・3 | LIGHTING |
| LightingPass | `LightingPass.DescriptorSet`（照明の組）・`RenderingCoordinator.DescriptorSet`（初期化で作る RTGI の記述子） | descriptor | 3・1 | 4・1 | LIGHTING |
| TemporalAAPass | `TemporalAAParams`・記述子（`RenderingCoordinator.DescriptorSet`：Resolve と Copy） | buffer・descriptor | 1・2 | 1・2 | POST-A |
| BloomPass | `BloomParamsUBO`・`BloomDownsampleParamsUBO`・`BloomUpsampleParamsUBO`・`BloomPass.DescriptorSet` | buffer・descriptor | 1・6・5・12 | 同じ | POST-A |
| ToneMappingPass | `ToneMappingParamsUBO`・`ToneMappingPass.DescriptorSet` | buffer・descriptor | 1・1 | 1・4 | POST-A |
| SSAOPass | `SSAOParamsUBO`・`SSAOBlurParamsUBO`・`SSAOPass.DescriptorSet` | buffer・descriptor | 1・1・2 | 同じ | POST-A |
| SSRPass | `SSRParamsUBO`・`SSRPass.DescriptorSet` | buffer・descriptor | 1・1 | 1・4 | POST-B |
| VolumetricsPass | `VolumetricsParams`・記述子（`RenderingCoordinator.DescriptorSet`） | buffer・descriptor | 1・1 | 1・1 | POST-B |
| VignettePass | `VignetteParamsUBO`・`VignettePass.DescriptorSet` | buffer・descriptor | 1・1 | 1・4 | POST-B |
| FXAAPass | `FXAAParamsUBO`・`FXAAPass.DescriptorSet` | buffer・descriptor | — | 1・4 | POST-B |

  - 組の数: 起動画面 count=30（resources=198、events=32886、172 フレーム）、周回 count=32（resources=215、events=242193、3109 フレーム）。(1) の一覧（`FRAME_RESOURCE_REWRITE`）は起動画面 30 行・周回 32 行で、(2) と同じ資源（守られていない資源はどれも毎フレーム書き換えている）。
  - 突き合わせ: 出たパスは全部 DYNAMIC-UBO・LIGHTING・POST-A・POST-B の対象にあり、漏れたパスは無い。起動画面・周回では動かないので出なかったもの: DebugDrawPass（DEBUGDRAW）、GBufferPass・ForwardPass（DYNAMIC-UBO）、MotionBlurPass（POST-A）、DepthOfFieldPass・UpscalePass・HiZPyramidPass（POST-B）、ImGuiOverlayPass・Canvas（PRESENT）。PresentationPass の Blit の記述子は FIF-FRAME-ORDER で飛行中のスロットごとの組になっていたので「守られている」と登録し、出なくなった（最初の撮影では `Unscoped.DescriptorSet` で出ていた）。DDGI の CPU が書くデータは FrameIndex ごとの枠なので「守られている」に入れた（DDGI の項目の本題の履歴の分かれは GPU→GPU で、この見張りでは見えない）。
  - TASKS.md の更新: PRESENT の verify の照合に、Canvas の書き込みが出る RenderGraph のパスの名前（`CanvasBoardPass`・`CanvasClearPass`・`CanvasInlineLayerPass`・`CanvasOwnRTLayerPass`・`CanvasLayerCompositePass`）を足した。DYNAMIC-UBO の notes に、ShadowMapPass の記述子が 2 つの名前で出ることを書いた。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-REWRITE-GUARD-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-REWRITE-GUARD-2.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=5E-06・within_limits=True、result=pass、EXIT=0。
  - `verify-SCN-ECS-FIF-REWRITE-GUARD-3.txt`: `FRAME_RESOURCE_UNPROTECTED_SUMMARY count=30 …` があり EXIT=0（SUMMARY の行を同じファイルに写した）。
  - `verify-SCN-ECS-FIF-REWRITE-GUARD-4.txt`: シーンの周回（rendering3dtest・mega-bananas・mega-stress × 3）`SCENE_CYCLE_SMOKE_RUN result=pass`、EXIT=0。Game.log の ERROR は 0 件。
  - Release で見張りが消えた形のビルド: `build-REWRITE-GUARD-release-core.txt`（Release の Core、error 0、EXIT=0。Blit・合成の記述子の登録を足す前の版）。
- Next: SCN-ECS-FIF-DEBUGDRAW。
- Notes: バッファの参照の範囲は書き換えを一度見たバッファだけ記録するので、重なりは 2 回目の書き換えから見つかる（作成の直後に 1 回だけ書く頂点などの記録を省くため）。記述子セットには名前が無いので、作ったパスの名前に `.DescriptorSet` を付けて出す（初期化で作ったものは `RenderingCoordinator.DescriptorSet` になる）。`FrameUseRing` の窓は「Acquire から今のパスの区間の終わりまで」なので、その間に作ったパスのメンバの資源も守られている扱いになる（同じフレームの中の重なりは見る）。同じフレームの重なり（`buffer_recorded_same_frame`・`descriptor_bound_same_frame`）は起動画面・周回では 0 件で、この 2 種類を起こす検査は書いていない（verify が名指ししていないため。`RenderGraphLifetimeTest` の束に足すのが候補）。性能を測る撮影（RelWithDebInfo）の数字には見張りの分が入る（束縛は原子変数の読み 1 回、書き換えを見たバッファの束縛とフレームで初めての記述子の束縛は資源ごとの排他）。テクスチャの書き換えと加速構造の構築の入力は見ない。

### SCN-ECS-FIF-REWRITE-GUARD の差し戻しへの対応（2026-10-12、反復 59・run 20261012-030203）

- 参照の記録: CPU から一度でも書いたバッファは、作成の直後の 1 回（数えない書き込み）の後から参照の範囲を記録する。「作成 → 初回の書き込み → コマンドが参照 → 同じ範囲へ再び書き込み」が最初の書き換えで見つかる（以前は 2 回目の書き換えまで参照を持たなかった）。バッファ全体の参照は、同じフレームの 2 回目から原子変数の読みだけで済ませる。
- 「守られている」の登録を Use に属する資源に限った: `FrameUseRing::Acquire` が開いていた「今のパスの区間の終わりまで」の窓（`MarkFrameUseProtectedCreation`）をやめ、Acquire で受け取った Use（枠）のメンバを作る所を `RHI::FrameResourceProtectedCreationScope` で包んだ（GBufferDebugPass・MaterialTileClassifyPass・ShadowProbePass・SkinningComputePass・VirtualShadowMapPages・VirtualShadowMapPass・VirtualShadowMapRaster・VisibilityRasterPass（合流・枠・検証表示）・VisibilityResolvePass・VisibilitySwRaster・MegaGeometryPass の枠の SceneStaging・SceneScatter の記述子・SceneCheckReadback・PageTable）。同じ区間で作るパスのメンバの資源は守られている扱いにならない。
- 報告の重複抑制: 資源ごとの抑制を種類だけでなく名前・パス・種類の組にした。同じ資源を別のパスで危険に書き換えると、そのパスの組も `FRAME_RESOURCE_UNPROTECTED` に出る。
- 一覧（1）: `FRAME_RESOURCE_REWRITE resource= pass= frames= id= gen=` を資源（ID と世代）ごとに 1 行にした（`pass` は書き換えたパス、2 つ以上なら `,` 区切り）。名前とパスの組ごとの集計は `FRAME_RESOURCE_REWRITE_TOTAL resource= pass= resources= max_frames=` で添える。`Docs/Architecture/FrameResourceWriteGuard.md` を合わせて直した。
- 結果: (2) の組は起動画面 count=30（resources=198、events=32982、172 フレーム）、周回 count=32（resources=215、events=242269、3109 フレーム）で、上の表と同じ組・同じ資源の数（リングの Use の資源は 1 つも出ていない）。(1) の一覧は起動画面 198 行（TOTAL 30 行）、周回 215 行。2 つ以上のパスから書き換えた資源は無かった。突き合わせの結論（漏れたパスは無い）は変わらない。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-REWRITE-GUARD-5.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-REWRITE-GUARD-6.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=1E-06・within_limits=True、result=pass、EXIT=0。
  - `verify-SCN-ECS-FIF-REWRITE-GUARD-7.txt`: `FRAME_RESOURCE_UNPROTECTED_SUMMARY count=30 …` があり EXIT=0。
  - `verify-SCN-ECS-FIF-REWRITE-GUARD-8.txt`: シーンの周回（rendering3dtest・mega-bananas・mega-stress × 3）`SCENE_CYCLE_SMOKE_RUN result=pass`、EXIT=0。Game.log の ERROR は 0 件。
- Next: SCN-ECS-FIF-DEBUGDRAW。
- Notes: 上の反復 58 の Notes のうち「重なりは 2 回目の書き換えから見つかる」「FrameUseRing の窓で作ったパスのメンバの資源も守られている扱い」はこの反復で無くなった。FrameUseRing を新しく使う所は Use のメンバを作る所を包む（包み忘れは一覧と `FRAME_RESOURCE_UNPROTECTED` に出る側に倒れる）。差し戻しの 4 件を起こす最小の再現の検査は書いていない（verify が名指ししていないため。`RenderGraphLifetimeTest` の束が候補）。

### SCN-ECS-FIF-DEBUGDRAW（2026-10-12、反復 60・run 20261012-030203）

- `DebugDrawPass` を、頂点・定数・記述子セットをフレームの持ち主から借りる最初の手本にした。`Execute` は `context.FrameResources` から頂点（`AllocateUploadData(..., FrameUploadUsage::Vertex)`）・カメラの定数（`AllocateConstants`）・記述子セット（`AllocateDescriptorSet`。レイアウトは `IDevice::GetOrCreateDescriptorSetLayout` で引いた共有の 1 つをメンバに持つ）を借り、定数は `BindConstantBuffer(0, Buffer, GetOffset32, GetSize32)` で束ねる。`DynamicBufferRing`・`DynamicUniformAllocator`・飛行中のフレームの番号（`FrameIndex`・`FRAME_PACKET_BUFFER_COUNT`）の扱いを DebugDrawPass から消した。
- 借りた頂点の切れ端のオフセットを `DebugDrawLineListPassCommand::VertexBufferOffset`（`FrameCommand::CreateDebugDrawLineList` の引数）で運び、`SceneRenderer` の `SetVertexBuffer` に渡す（以前は 0 に固定）。
- `Docs/Architecture/FrameResources.md` にパスを書く人向けの約束を書いた（フレームごとに書くデータは文脈から借りてメンバに持たない／GPU だけが使う資源はメンバに持ってよい／RHI の資源は手放せばよい／読み戻しは共通の口／Parallel でも同じ口／借り方の表と DebugDraw の形／`FRAME_RESOURCE_*` の読み方）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-DEBUGDRAW-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-DEBUGDRAW-2.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=5E-06・within_limits=True、result=pass、EXIT=0。画像は天球・地面・球・岩・家が以前どおり。
  - `verify-SCN-ECS-FIF-DEBUGDRAW-3.txt`: `FRAME_RESOURCE_UNPROTECTED_SUMMARY count=30` があり DebugDrawPass の組は無い、EXIT=0。ERROR は 0 件。
  - 起動画面ではデバッグの線が出ないので、`-DebugDrawTestLines`（大きな球を囲む箱）の決定論の撮影を変更前（`startup-capture/SCN-ECS-FIF-DEBUGDRAW-lines-before`、`debugdraw-lines-before.txt`）と変更後（`SCN-ECS-FIF-DEBUGDRAW-lines`、`verify-SCN-ECS-FIF-DEBUGDRAW-4-lines.txt`）で撮って比べた: mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=1E-05・within_limits=True、result=pass。PNG は黄色の箱が同じ位置に描かれ、球の裏の辺は以前どおり遮蔽されている。`FRAME_RESOURCE_UNPROTECTED_SUMMARY` は変更前 count=32（`DynUBO_Slot#` と `RenderingCoordinator.DescriptorSet` が pass=DebugDrawPass）→ 変更後 count=30 で DebugDrawPass の組・一覧は 0。借りは 1 フレームに切れ端 2（816 バイト = 頂点 672 + 定数 144）・記述子セット 1、ページ 1・pool 1 を最初のフレームで足した後は足していない。ERROR は 0 件。
- Next: SCN-ECS-FIF-DYNAMIC-UBO。
- Notes: `Docs/Architecture/FrameResourceWriteGuard.md` の「守られている」の表の `DynamicBufferRing` の行に「呼び出し側の DebugDraw・ImGui」と残っている（この項目の paths の外なので直していない。今の呼び出し側は ImGui だけ。`DynamicBufferRing.cpp` のコメントは直した）。DebugDraw の借りを並列の記録の区間で通す撮影はしていない（DebugDrawPass は直列のパスで、借りる口は同じ）。

### SCN-ECS-FIF-REWRITE-GUARD の 2 周目の差し戻しへの対応（2026-10-12、反復 61・run 20261012-030203）

- 一覧（1）の数え漏れ: 書き換えたフレームを数える基準を、作成の直後の 1 回（数えない書き込み）でも進んでいた `LastWriteFrame` から、数えない 1 回の後にだけ進める `LastRewriteFrame` に替えた。「フレーム 1 で作成の直後の書き込み → フレーム 1 で再び書き込み → フレーム 2 で再び書き込み」が `frames=2` で一覧に出る（以前は 1 と数えて出なかった）。`Docs/Architecture/FrameResourceWriteGuard.md` に `frames` の数え方を 1 行足した。合否に使う (2) の判定は変えていない。
- 結果: 起動画面 `FRAME_RESOURCE_UNPROTECTED_SUMMARY count=30 resources=198 events=32982 rewrites=198 frames=172`、一覧 198 行（TOTAL 30 行）。周回 count=32（resources=215、rewrites=215、3109 フレーム）、一覧 215 行。今の起動画面・周回では守られていない資源はどれも毎フレーム書き換えているので件数は前回と同じで、上の表と突き合わせの結論（漏れたパスは無い）は変わらない。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-REWRITE-GUARD-9.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-REWRITE-GUARD-10.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=3E-06・within_limits=True、result=pass、EXIT=0。
  - `verify-SCN-ECS-FIF-REWRITE-GUARD-11.txt`: `FRAME_RESOURCE_UNPROTECTED_SUMMARY count=30 …` があり EXIT=0。ERROR は 0 件。
  - `verify-SCN-ECS-FIF-REWRITE-GUARD-12.txt`: シーンの周回（rendering3dtest・mega-bananas・mega-stress × 3）`SCENE_CYCLE_SMOKE_RUN result=pass`、EXIT=0。Game.log の ERROR は 0 件。
- Next: SCN-ECS-FIF-DYNAMIC-UBO。
- Notes: 差し戻しの再現（同じフレームで作成の直後と 2 回目、次のフレームで 3 回目）を起こす検査は書いていない（verify が名指ししていないため。`RenderGraphLifetimeTest` の束が候補）。

### SCN-ECS-FIF-DYNAMIC-UBO（2026-10-12、反復 62・run 20261012-030203）

- `DynamicUniformAllocator` に借りる形を足し、`GBufferPass`・`ForwardPass`（透明・ワールドボード）・`ShadowMapPass`（CSM・点光源の影）を移した。`Initialize(device, uboSize, descriptorSetDesc)` は共有のレイアウトを `GetOrCreateDescriptorSetLayout` で引くだけで、`Allocate(context.FrameResources)` が定数の切れ端（`AllocateUpload(..., FrameUploadUsage::Constants)`）と記述子セット（`AllocateDescriptorSet`）を借り、切れ端をオフセット付きで binding 0 に結んで返す。定数は `Allocation::Write`（マップしたままのページへの写し）で書く。呼び出し側から `Reset`・`IBuffer::Update`・事前確保の数（`MAX_OBJECTS`・`SetGrowthLimit`・点光源の影の `PointShadowUniformSlot*`）を消した。飛行中の番号・通し番号は渡さない。`Allocate` は自分の状態を変えない（並列の記録の区間から区間の口を渡して呼べる）。
- フレームの持ち主が無い文脈（`FrameResources` が null。RenderingCoordinator を通さずにパスを実行する検査）とレイアウトを引けないデバイスでは、`Allocate` のたびに定数のバッファ（`DynUBO_Unowned`）と記述子セットを新しく作る（使い回さないので上書きは起きない）。
- 事前確保の形（4 引数の `Initialize`・`SetGrowthLimit`・`Reset`・引数なしの `Allocate`）は `RenderGraphCompileTest` の `TestDynamicUniformAllocatorGrowsToCoverShadowCasters` だけが使うので残した（paths の外。パスからは使わない）。
- 借りる形では数の上限が無くなった: 以前の GBuffer・Forward は 1 フレーム 256 で頭打ち（超えた描画を省いていた）、CSM は 65536 × 4、点光源の影は 32768 まで。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-DYNAMIC-UBO-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-DYNAMIC-UBO-2.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=9E-06・within_limits=True、result=pass、EXIT=0。PNG は天球・地面・球・岩・家と影が以前どおり。
  - `verify-SCN-ECS-FIF-DYNAMIC-UBO-3.txt`: mega-stress 5万個 close の撮影 result=pass、EXIT=0（GPU median 38.2 ms、rt median 44.4 ms）。close.Game.log の ERROR 0。`check-SCN-ECS-FIF-DYNAMIC-UBO-record-median.txt`（トレースの計測の窓 300 フレーム）: `RG.Record.ShadowMapPass` median 0.395 ms（表 0.40、-0.005）・`RG.Record.GBufferPass` 0.004（表 0.00）・`RG.Record.ForwardPass` 0.002（表 0.00）で、どれも +0.3 ms 以内。
  - `verify-SCN-ECS-FIF-DYNAMIC-UBO-4.txt`: `FRAME_RESOURCE_UNPROTECTED_SUMMARY count=27`（反復 61 の 30 から、ShadowMapPass の `DynUBO_Slot#`・`RenderingCoordinator.DescriptorSet`・`ShadowMapPass.DescriptorSet` の 3 組が消えた）、GBufferPass・ForwardPass・ShadowMapPass の組は 0、EXIT=0。ERROR 0。起動画面の借りは 1 フレームに定数 73・記述子セット 73（ページ・pool は最初のフレームで 1 つずつ足した後は足していない）。
- 既存の失敗（この変更の前の HEAD でも同じ assert で落ちる）: Debug の `RenderGraphCompileTest`（2843 行 `commandList.DispatchCount == 10`）と `SkinnedRenderPathContractTest`（1849 行 `packet.Stats.SkinnedGBufferRecordedDraws == 1`）。`SkinnedRenderPathContractTest` で失敗より前に走る `TestInitializedPassesExecuteThroughFrameCommandsAndSceneRenderer`（持ち主の無い文脈で GBuffer・Shadow の Execute が描画を積み、スキニングの束縛 8・9 を書く）は通った。`TestDynamicUniformAllocatorGrowsToCoverShadowCasters` は `RenderGraphCompileTest` の失敗より後なので走っていない。
- Next: TASKS.md の未完の次の項目。
- Notes: mega-stress 5万個 close では CSM の描画が無く（VSM が太陽の影を持つ）、借りは 1 フレーム 0 件なので、表の比較は「物体の多い描画で借りが増えすぎない」ことを直接には測っていない（起動画面の 73 件では記述子の借りと書き込みは合わせて 0.1 ms 未満）。`Docs/Architecture/FrameResourceWriteGuard.md` の守られていない組の一覧と `FrameResources.md` の手本の表に、この 3 つのパスが借りる形になったことは書いていない（paths の外）。持ち主の無い文脈と事前確保の形を確かめる検査は書いていない（verify が名指ししていないため。事前確保の形は検査と一緒に消すのが候補）。

### SCN-ECS-FIF-DYNAMIC-UBO の差し戻しへの対応（2026-10-12、反復 63・run 20261012-030203）

- 差し戻しの本題（物体の多い描画での記述子の借りと書き込みの CPU の時間）は、親が done-when から外し、節目の SCN-ECS-MEASURE-FIF で借りた数と時間を見る形にした（0343b256）。この反復は今の done-when と verify で閉じた。
- non-blocking の指摘: `DynamicUniformAllocator.cpp` の借りる形で足したログの説明文（Initialize の 2 件・Allocate の 1 件・持ち主の無い文脈で作るときの 2 件）を日本語にした。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-DYNAMIC-UBO-5.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-DYNAMIC-UBO-6.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=5E-06・within_limits=True、result=pass、EXIT=0。PNG は天球・地面・球・岩・家と影が以前どおり。
  - `verify-SCN-ECS-FIF-DYNAMIC-UBO-7.txt`: `FRAME_RESOURCE_UNPROTECTED_SUMMARY count=27 resources=52`、GBufferPass・ForwardPass・ShadowMapPass の組は 0 件、EXIT=0。Game.log の ERROR 0。
- 補助（節目の計測の材料。done-when の外）: 変更前のソース（c2822c93 の 5 ファイル）で、CSM が MegaGeometry を描く構成（mega-stress 5万個 close、`--shadow-probe`）を撮った（`scene-capture/SCN-ECS-FIF-DYNAMIC-UBO-csm-before`、`csm-before-SCN-ECS-FIF-DYNAMIC-UBO.txt`。result=pass、ERROR 0）。CSM の MegaGeometry の描画は 1 フレーム 49・202・605・2670（約 3,500 件の定数と記述子）、計測の窓（Frame 39806..40105、300 フレーム）で `RG.Record.ShadowMapPass` median 23.19 ms・p95 30.66 ms、`RG.Record.GBufferPass` 0.010、`RG.Record.ForwardPass` 0.003、GPU median 58.4 ms。変更後の同じ撮影は撮っていない（比べは節目へ移ったため）。
- Next: TASKS.md の未完の次の項目（SCN-ECS-FIF-LIGHTING）。
- Notes: 節目の SCN-ECS-MEASURE-FIF で記述子の借りの時間を見るときは、`--shadow-probe` 付きの mega-stress が借りの多い構成（1 フレーム約 3,500 件）として使える（`-GameArgs "stress-entities=50000,shadow-probe"`）。上の変更前の数字と同じ窓の定義（CaptureScene の timing_window）で比べられる。Git Bash から verify のビルドを走らせると `/m:1` がパスに変換されるので `MSYS_NO_PATHCONV=1` を付ける。

### SCN-ECS-FIF-LIGHTING（2026-10-13、反復 64・run 20261012-030203）

- `LightingPass` の照明の組（`m_ExecuteResourceSets` と現在の枠の 7 つのメンバ、組の取り替え・上限 4・通し番号の扱い）を消し、Execute ごとにフレームの持ち主から借りる形にした。借りるのは照明の定数（binding 4）・光源の配列（binding 5。光源が無いフレームは 0 で埋めた 1 つ分）・VSM のサンプル／点光源のサンプル（21・26）・スライスの表（25）・記述子セット（共有のレイアウトを `GetOrCreateDescriptorSetLayout` で引いてメンバに持つ）。初期化での照明のバッファと最初の記述子セットの作成もやめた。
- RTGI の定数（`m_RTGIComputeParametersBuffer`）・発光の表（`m_RTGIComputeEmitterBuffer`）・RTGI と denoise の記述子セットも dispatch ごとに借りる。メンバに残るのは共有のレイアウト・パイプライン・履歴・denoise の出力だけ。
- RTGI の instance の表の置き場は GPU だけが読むバッファ（CPU から見えない。`Update` を使わない）にし、`RecordRTGIInstanceUpload` が、表が GPU に届いたと確かめられていないときだけ、借りた切れ端から表全体を `CopyBuffer` で写す。コピーの前に `ShaderResource→CopyDest`（前の Execute・前のフレームの dispatch の読みとの依存）、後に `CopyDest→ShaderResource`（この後の dispatch の読み）のバリアを張る。同じフレームの 2 回目の Execute は、先に記録したコピーが使えるので写さない（表が変わればバリアを張って写し直す）。
- 「届いた」は提出の成功で確定する: コピーを記録したフレームの世代を持ち、次の dispatch で `FrameResourceLane::GetSubmissionOutcome` が `Submitted` なら確定、`Aborted`・`Unknown`（記録が残っていない）なら持ち越さずに写し直す。例外の catch では印と待ちの世代を両方落とす。持ち主の無い文脈は提出を確かめられないので毎回写す。
- フレームの持ち主に、終わったフレームの世代ごとの提出の結果（直近 16 個の `FrameSubmissionHistory`。提出・中止の段で書く）と `GetSubmissionOutcome` を足した。持ち主のページに `TransferSrc` を足した（借りた切れ端をコピー元にするため）。
- 光源の配列は借りた切れ端になったので、公開（`PhysicalLighting.PublishLighting`）にオフセット（`LightBufferOffset`）を足し、読む側（ForwardPass の binding 8・DDGIProbePass の binding 4・RTGI の binding 8）で束縛と大きさの確かめに通した。
- 持ち主の無い文脈（RenderingCoordinator を通さない検査）では、借りる代わりに使い回さない専用のバッファ・記述子セットを Execute ごとに作る（`DynamicUniformAllocator` と同じ扱い）。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-LIGHTING-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-LIGHTING-2.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、`indirect_lighting=rtgi`、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=4E-06・within_limits=True、result=pass、EXIT=0。PNG は天球・地面・球・岩・家と影が以前どおり。
  - `verify-SCN-ECS-FIF-LIGHTING-3.txt`: `FRAME_RESOURCE_UNPROTECTED_SUMMARY count=17 resources=39`（反復 63 の 27 から、LightingPass の 10 組〔照明の定数・光源の配列・VSM の 3 つ・照明の記述子セット・初期化で作った記述子セット・RTGI の定数・発光・instance の表〕が消えた）、LightingPass の行は 0、EXIT=0。Game.log の ERROR 0、`INDIRECT_LIGHTING source=rtgi`。借りは最後のフレームで切れ端 81・記述子セット 76（ページ・pool は最初のフレームで 1 つずつ足した後は足していない）、借りの失敗 0。
- Next: TASKS.md の未完の次の項目。
- Notes: Debug の `RTGIDiffuseIndirectVulkanTest` の複数ビューポートの場合は、消した組のメンバ（`m_LightDataBuffer` など）を読み、組の上限 4・次のフレームでの組の使い回しを確かめているので、この変更でコンパイルできない（Test/ は paths の外なので直していない）。借りる形の約束（Execute ごとに別の切れ端・セット、中身がそのビューポートの値、描画が束縛するセットが Execute のセット、上限なし）へ書き直す必要がある。DDGI の地図の引き方（`GetIrradianceAtlas(context.FrameIndex, …)`）と VSM の統計の読み戻し（通し番号の枠）は LightingPass に残っている（前者は DDGIProbePass の資源、後者は SCN-ECS-FIF-READBACK の対象）。提出の結果の確かめ・中止のフレームのコピーを持ち越さないことを起こす検査は書いていない（verify が名指ししていないため）。`git diff --numstat` と `--ignore-cr-at-eol` は LightingPass.cpp で 1 行違うが、`--minimal`・`--histogram` では一致する（既定の差分の並べ方の違いで、行末だけの書き換えは無い）。

### SCN-ECS-FIF-LIGHTING の差し戻しへの対応（2026-10-13、反復 65・run 20261012-030203）

- 持ち主の無い文脈の資源の寿命: RTGI（dispatch・denoise・instance の表のコピー）は、フレームの持ち主が無い文脈では記録の前に断るようにした（`ExecuteRTGI` の入口の条件に `context.FrameResources` を足し、`RecordRTGIInstanceUpload` も持ち主が無ければ記録しない）。コピー元の切れ端・RTGI と denoise の記述子セットは常に持ち主から借り、提出の完了まで持ち主が持つ。照明の全画面パスは持ち主の無い文脈でも描くので、その文脈で作るバッファと記述子セットは `m_UnownedBuffers`・`m_UnownedDescriptorSets` にパスを手放す（Shutdown）まで持つ（提出の完了を知る手段が無いため。本番の経路は常に借りるので入らない）。
- `RTGIDiffuseIndirectVulkanTest` を借りる形へ書き直した: 試験がフレームの持ち主（`FrameResourceOwners`。ページを作る口はデバイスで作りマップした先頭を覚える）を開始・提出・中止し、`context.FrameResources` に渡す。消した組のメンバの代わりに、LightingPass が直近の照明の Execute で束縛した借りた置き場（弱い参照・オフセット・大きさ）と記述子セット（生のポインタ）を `m_LastLightingExecute` に残し、試験がそこから読む。複数ビューポートの場合は、Execute ごとに別の切れ端・別のセットを借りること、描画が束縛するセットがその Execute のセットであること、中身がそのビューポートの値のままであること、同じフレームの 5 回目の Execute も描く（上限が無い）ことを確かめる（旧来の上限 4・次のフレームで同じ組を使い直す期待は消した）。
- non-blocking の指摘: Shutdown の英語のコメントと、光源の配列を用意できないときの英語のエラーの説明文を日本語にした。
- 検証（`.harness/runs/20261012-030203/`）:
  - `verify-SCN-ECS-FIF-LIGHTING-4.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0（行末の修復の後のビルド `verify-SCN-ECS-FIF-LIGHTING-7.txt` も EXIT=0）。
  - `verify-SCN-ECS-FIF-LIGHTING-5.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、`indirect_lighting=rtgi`、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=1.2E-05・within_limits=True、result=pass、EXIT=0。PNG は天球・地面・球・岩・家と影が以前どおり。
  - `verify-SCN-ECS-FIF-LIGHTING-6.txt`: `FRAME_RESOURCE_UNPROTECTED_SUMMARY count=17`、LightingPass の行 0、EXIT=0。Game.log の ERROR 0、`INDIRECT_LIGHTING source=rtgi`。
  - `check-SCN-ECS-FIF-LIGHTING-rtgi-test.txt`: Debug の `RTGIDiffuseIndirectVulkanTest`（検証レイヤー有効）EXIT=0、`lighting_execute_borrows_per_viewport=true bound_descriptor_sets_distinct=true borrowed_slices_keep_own_viewport_values=true no_execute_limit=true` ほか RTGI の全項目が true、検証レイヤーの報告 0。
- Next: TASKS.md の未完の次の項目。
- Notes: `DynamicUniformAllocator` の持ち主の無い文脈（`DynUBO_Unowned`）も、作った定数のバッファを記録の後に手放すので同じ寿命の問題を持ちうる（この項目の外。GBuffer・Shadow を直接呼ぶ検査は FrameCommand が記述子セット経由でバッファを持つ形なら守られるが、直接記録する経路は確かめていない）。

### SCN-FIX-SKINNED-PATH-TEST（2026-10-13、反復 1・run 20261013-010750）

- 原因: 517bdf0a で `DebugDrawPass::Initialize` が共有の記述子セットのレイアウトを `GetOrCreateDescriptorSetLayout` で引き、引けなければ初期化に失敗するようになった。試験の `FakeDevice` は共有のレイアウトの口（5004cd79 で足された）を持たず既定の nullptr を返すので、`TestCoordinatorPropagatesFramePacketStatsAndSubmissionSerials` で PostProcessStack の初期化が失敗し、`View::Render` がグラフを組む前に戻ってフレーム全体（GBuffer・影を含む）を記録しなくなっていた。c3b2294a 以降は `LightingPass::Initialize` も同じくレイアウトを要るので、DebugDrawPass だけ緩めても通らない。
- 判断: 期待が古いのではなく、試験の偽デバイスが今のデバイスの約束（遅延描画の経路は共有のレイアウトと飛行中のスロットごとの記述子の pool をデバイスから引く。Vulkan のデバイスは常に返す）を写していなかった。実装の退行ではないので実装は変えず、assert の期待（GBuffer 1・影はカスケード数）もそのまま。
- 直したこと: `SkinnedRenderPathContractTest.cpp` の `FakeDevice` に `GetOrCreateDescriptorSetLayout`（束縛の形を持つ偽のレイアウトを返す）と `CreateFrameDescriptorAllocator`（借りるたびに新しい偽のセットを返し Reset で戻す偽の pool）を足した。RenderingCoordinator を通す場合はフレームの持ち主から借りる形、通さない場合（`TestInitializedPassesExecuteThroughFrameCommandsAndSceneRenderer` など）は従来どおり持ち主の無い形で動く。
- 検証（`.harness/runs/20261013-010750/`）:
  - `verify-SCN-FIX-SKINNED-PATH-TEST-1.txt`: Debug の `SkinnedRenderPathContractTest` のビルド EXIT=0、error 0。
  - `verify-SCN-FIX-SKINNED-PATH-TEST-2.txt`: ctest（-V）で `SkinnedRenderPathContractTest passed`（main の最後の出力。1849 行の後ろの `TestCoordinatorDistinguishesRecoverableAndFatalBeginFrameStatuses`・`TestRuntimeCompilerAcceptsSkinnedShaders`・`TestVulkanBeginFrameFaultClassification`・`TestSubmissionSerialExhaustionNeverWrapsOrSubmits` まで走った）、100% tests passed、EXIT=0。
- Next: TASKS.md の未完の次の項目（SCN-FIX-RENDERGRAPH-COMPILE-TEST など）。
- Notes: 共有のレイアウトを返さないデバイスでは、デバッグ描画のような任意のポストプロセスの初期化失敗がビュー全体の描画を止める（`View::Render` が PostProcessStack の失敗で戻る）。本番の Vulkan では起きないので直していない。ほかの試験の偽デバイス（`RenderGraphCompileTest` など）が coordinator や View を通すなら、同じ口が要りうる。

### SCN-ECS-FIF-POST-A（2026-10-13、反復 1・run 20261013-230533）

- `TemporalAAPass`・`BloomPass`・`ToneMappingPass`・`SSAOPass`・`MotionBlurPass` の、毎フレーム `Update` で書き換えていたメンバの定数と記述子セットを消し、Execute（MotionBlur は Apply）ごとにフレームの持ち主から借りる形にした。メンバに残るのは共有の記述子セットのレイアウト（`GetOrCreateDescriptorSetLayout` で初期化に 1 回引く）・パイプライン・render pass・framebuffer・サンプラ・GPU だけが使う資源（TAA の履歴、Bloom の段のテクスチャ、SSAO の可視率）・初期化で 1 回だけ書く資源（Bloom のレンズの汚れ、ToneMapping の LUT）。
  - TAA: 解決と書き戻しの 2 つのセットを借り、1 つの定数の切れ端を両方の binding 4 に結ぶ（以前も 1 つのバッファを共有していた）。
  - Bloom: 合成のセットと定数に加え、段ごとの縮小・拡大の定数とセットも毎フレーム借りる（段の資源 `BloomMipLevel` はテクスチャと framebuffer だけになった）。
  - ToneMapping・SSAO: Parallel の区間の記録でも、文脈の `FrameResources`（区間ごとの口）から借りる。Parallel にできるかの判定（`DeclaresAllLayoutChanges`）はセットの代わりにレイアウトが揃っているかを見る。
  - MotionBlur: 3 つの段のセットを借り、1 つの定数の切れ端を 3 つの binding 4 に結ぶ。
- 借りられなかったフレームは、そのフレームだけ働かずに出力を整える（TAA と MotionBlur は SceneColor を変えずに ShaderResource へ移し、TAA は履歴を捨てる。Bloom・ToneMapping は空の render pass、SSAO は状態の移し替えのパスだけを積む）。
- 持ち主の無い文脈（RenderingCoordinator を通さない検査。ToneMapping の LUT・フィルムグレインの GPU 試験、MotionBlur の PT 参照の試験が `Apply`・`Execute` を直接呼ぶ）では、LightingPass と同じく使い回さない定数と記述子セットを作り、提出の完了を知る手段が無いのでパスを手放す（Shutdown）まで `m_UnownedBuffers`・`m_UnownedDescriptorSets` に持つ。本番の経路は常に借りる。
- 5 パスのコードに飛行中の番号・`FrameIndex`・`FrameUseRing` の扱いは無い（grep で 0 件）。
- 検証（`.harness/runs/20261013-230533/`）:
  - `baseline-build.txt`: 変更前の RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-POST-A-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-POST-A-2.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、`indirect_lighting=rtgi`、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=3E-06・within_limits=True、result=pass、EXIT=0。PNG は天球・地面・球・岩・家と影が以前どおり。
  - `verify-SCN-ECS-FIF-POST-A-3.txt`: EXIT=0。`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=6 resources=6`（反復 65 の 17 から、TemporalAAPass 2 組・BloomPass 4 組・ToneMappingPass 2 組・SSAOPass 3 組の 11 組が消えた）。残りは SSRPass・VolumetricsPass・VignettePass の 6 組（SCN-ECS-FIF-POST-B の対象）。Game.log の ERROR 0、借りの失敗 0（Upload.Failures=0・Descriptor.Failures=0）、ページ・pool は最初のフレームで 1 つずつ足した後は足していない。借りは最後のフレームで切れ端 97・記述子セット 93。
  - 起動画面で無効のパス: MotionBlurPass（起動画面では働かず、変更前の撮影にも組は出ていなかった）。ビルドが通ることと、コードの照合（メンバの定数・3 つのセットが無く、Apply が `BorrowDescriptorSet` と `BindFrameParams` で借りたセットを `EnqueueFullscreenPass` へ渡す）で確かめた。撮影は増やしていない。
- Next: TASKS.md の未完の次の項目（SCN-ECS-FIF-POST-B など）。
- Notes: 借りる口（持ち主から借り、持ち主の無い文脈では作ってパスを手放すまで持つ）を 5 パスと LightingPass がそれぞれ持っているので、共通の口（`FrameResourceLane` の隣など）にまとめるのが候補（この項目の paths の外）。持ち主の無い文脈・借りの失敗のフレームを起こす検査は書いていない（verify が名指ししていないため）。`Docs/Architecture/FrameResourceWriteGuard.md` の守られていない組の一覧に、この 5 パスが借りる形になったことは書いていない（paths の外）。編集で LF だけの行が混じっていた BloomPass・ToneMappingPass・SSAOPass（.h と .cpp）の行末が CRLF に揃ったので、変えていない行を元の行末へ戻した（`git diff --numstat` と `--ignore-cr-at-eol` が一致）。

### SCN-ECS-FIF-POST-B（2026-10-13、反復 1・run 20261013-232555）

- `SSRPass`・`DepthOfFieldPass`・`VolumetricsPass`・`VignettePass`・`FXAAPass`・`UpscalePass` の、毎フレーム書き換えていたメンバの定数と記述子セットを消し、Execute（DoF は Apply）ごとにフレームの持ち主から借りる形にした（SCN-ECS-FIF-POST-A と同じ形）。メンバに残るのは共有の記述子セットのレイアウト（`GetOrCreateDescriptorSetLayout` で初期化に 1 回引く）・パイプライン・render pass・framebuffer・サンプラ・SSR の 1×1 の 0 テクスチャ・DoF の中間テクスチャ。
  - SSR: 定数はシェーダの UBO の大きさ（`SSR_PARAMS_SIZE` = 320）で `AllocateUpload` から借り、パラメータ（308 バイト）の後ろを 0 で埋めて binding 4 に結ぶ（以前も 320 で結んでいた）。
  - DoF: 1 つの定数の切れ端を Gather と Resample の 2 つの借りたセットの binding（`ParamsBinding`）に結ぶ（以前も 1 つのバッファを共有していた）。
  - Upscale: 定数は無く、記述子セットだけを借りる。
- `HiZPyramid`: ミップ1 以降の記述子セットと全ミップの定数は作成時に 1 回だけ書くのでメンバのまま。深度を結ぶミップ0 のセットは作成時に作らず、`Build(commandList, depth, frameResources)` が持ち主から毎回借りる（`HiZPyramidPass` は `context.FrameResources` を渡す）。持ち主を渡さない呼び出し（`MegaGeometryPass` の HZB・HiZ の GPU 試験。引数の既定は null）では、深度かピラミッドが替わったときだけ使い回さないセットを新しく作り、前のセットは書き換えずに手放すまで持つ。
- 借りられなかったフレームは、そのフレームだけ働かずに出力を整える（SSR・Vignette・FXAA・Upscale は空の render pass、Volumetrics は空の render pass か状態の移し替え、DoF は SceneColor を変えずに ShaderResource へ移す、HiZ は作らない）。
- 持ち主の無い文脈（RenderingCoordinator を通さない検査）では、POST-A と同じく使い回さない定数と記述子セットを作り、パスを手放す（Shutdown）まで `m_UnownedBuffers`・`m_UnownedDescriptorSets` に持つ。
- 検証（証拠は `.harness/runs/20261013-232555/`）:
  - `verify-SCN-ECS-FIF-POST-B-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0、警告 0（最初のビルドで BOM の無かった `UpscalePass.cpp` に日本語を足して C4819 が出たので、BOM を付けて作り直した）。
  - `verify-SCN-ECS-FIF-POST-B-2.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、`indirect_lighting=rtgi`、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=8E-06・max_channel_difference=1・within_limits=True、result=pass、EXIT=0。PNG は天球・地面・球・岩・家と影が以前どおり。
  - `verify-SCN-ECS-FIF-POST-B-3.txt`: EXIT=0。`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0 resources=0 rewrites=0`（POST-A の 6 組〔SSRPass・VolumetricsPass・VignettePass〕が消え、起動画面の守られていない組は無くなった）。Game.log の ERROR 0、借りの失敗 0（Upload.Failures=0・Descriptor.Failures=0）、ページ・pool は最初のフレームで 1 つずつ足した後は足していない。借りは最後のフレームで切れ端 100・記述子セット 96（POST-A の 97・93 から、SSR・Volumetrics・Vignette の 1 つずつ）。
  - 起動画面で無効のパス: `DepthOfFieldPass`（借りが増えていないので Apply まで来ない）・`FXAAPass`（初期化はされるが借りが増えていないので Execute まで来ない）・`UpscalePass`（描画と表示の解像度が同じ）・`HiZPyramidPass`（借りが増えていない。起動画面の「HZB作成」は MegaGeometryPass の HZB で、持ち主を渡さない形を通り、画は一致）。ビルドが通ることと、コードの照合（対象のメンバ `m_ParamsBuffer`・`m_DescriptorSet`・`m_GatherDescriptorSet`・`m_ResampleDescriptorSet`・`m_DescriptorSets[0]` の参照が 0、飛行中の番号・`FrameUseRing` の扱いが 0、各パスが `AllocateConstants`／`AllocateUpload`／`AllocateDescriptorSet` で借りたセットを描画へ渡す）で確かめた。撮影は増やしていない。
- Next: TASKS.md の未完の次の項目。
- Notes: 借りる口（持ち主から借り、持ち主の無い文脈では作ってパスを手放すまで持つ）を持つパスが 11 になったので、共通の口にまとめるのが候補（この項目の paths の外）。`MegaGeometryPass` の HZB（`m_HiZ.Build`）にはまだ持ち主を渡していない（paths の外。今は深度が替わるときだけ新しいセットを作るので書き換えは起きない）。`Docs/Architecture/FrameResourceWriteGuard.md` の守られていない組の一覧は更新していない（paths の外）。持ち主の無い文脈・借りの失敗のフレームを起こす検査は書いていない（verify が名指ししていないため）。`SSRPass`・`FXAAPass`・`UpscalePass`（.h と .cpp）は元から LF だけの行が混じっていたので、変えていない行は元の行末のまま、変えた行と足した行を CRLF にした（`git diff --numstat` と `--ignore-cr-at-eol` が一致）。

### SCN-ECS-FIF-PRESENT（2026-10-13、反復 2・run 20261013-232555）

- 表示の Blit（`PresentationPass`・`PresentationComposer`）、Canvas（`CanvasView` の板と層の合成）、ImGui（`ImGuiOverlayPass`）の、毎フレーム書いていた記述子と定数をフレームの持ち主から借りる形にした。
  - 表示の Blit: 2 つが共有する `PreparePresentationBlitDescriptorSet`（`PresentationPass.h`）が、Blit のレイアウト（binding 0 入力・binding 1 表示の符号化の定数）のセットと符号化の定数を借りて書く。符号化の経路は render pass の色の形式（スワップチェーンの形式）から `GetPresentationEncodePath` で決める（RenderingCoordinator の作り方と同じ）。持ち主の無い文脈だけ、要求の `BlitDescriptorSet`（RenderingCoordinator の使い回しの組）を書き換える。
  - 遅らせる Blit: セットと定数は `PresentationPass::Execute`（グラフの記録の中）で借りて書き終え、`RecordDeferredBlit`（overlay の後）は同じフレームのセットを束縛するだけにした（別のフレームの呼び出しなら借り直す）。頂点・インデックスの束縛は見張りに「オフセットからバッファの終わりまで」と記録されるので、ImGui が借りた頂点を束縛した後に同じページへ定数を書き足すと、重ならない切れ端でも `FRAME_RESOURCE_UNPROTECTED ... pass=PresentationPass kind=buffer_recorded_same_frame` が出た（周回の最初の実行で 1 組）。書き込みを overlay の前へ寄せて 0 にした。
  - Canvas: 板のセットと層の合成のセット・不透明度の定数を借りる。共有のレイアウト（`m_BoardDescriptorSetLayout`・`m_LayerCompositeDescriptorSetLayout`）は `EnsureBoardSharedResources` で 1 回引く。保持の組（`RetainedBoardFrameResources`）に入れるのは持ち主の無い文脈で作ったセット・定数だけになった（render pass・framebuffer・パイプライン・テクスチャの保持は変えていない）。
  - ImGui: 飛行中のスロットごとのリング（`m_VertexRing`・`m_IndexRing`・`m_UniformRing`）と `m_SlotDescriptorSets`・`IN_FLIGHT_SET_COUNT` を消し、Execute ごとに頂点・インデックスの切れ端（`FrameUploadUsage::Vertex`・`Index`）へ全 ImDrawList を直接写し、UBO の切れ端とセットも借りる。Mesh2D の `VertexBufferOffsetBytes`・`IndexBufferOffsetBytes` に切れ端の基点を入れる。レイアウトは Initialize で 1 回引く。持ち主の無い文脈では描かずに警告する。CPU の写し（`m_SlotSnapshots`、パケットのスロット）はそのまま。
- 検証（証拠は `.harness/runs/20261013-232555/`）:
  - `baseline-build-iter2.txt`: 変更前の RelWithDebInfo の Game のビルド EXIT=0。
  - `verify-SCN-ECS-FIF-PRESENT-1〜3.txt`: 最初の実装（遅らせる Blit が RecordDeferredBlit で借りる形）のビルド・撮影・照合。どれも EXIT=0 だったが、周回で上の 1 組が出たので直して取り直した。
  - `verify-SCN-ECS-FIF-PRESENT-4.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0、警告 0。
  - `verify-SCN-ECS-FIF-PRESENT-5.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、`indirect_lighting=rtgi`、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=8E-06・max_channel_difference=1・within_limits=True、result=pass、EXIT=0。PNG は天球・地面・球・岩・家と影が以前どおり。
  - `verify-SCN-ECS-FIF-PRESENT-6.txt`: EXIT=0、`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0 resources=0 rewrites=0`。Game.log の ERROR 0、借りの失敗 0。借りは最後のフレームで切れ端 101・記述子セット 97（POST-B の 100・96 から、表示の Blit の定数とセットの 1 つずつ）。ページ・pool は最初のフレームで 1 つずつ足した後は足していない。
  - `check-SCN-ECS-FIF-PRESENT-cycle.txt`（done-when の「ランチャーと ImGui・Canvas が崩れない」の確かめ。起動画面の撮影は `--scene=rendering3dtest` で直接入るのでランチャー・ImGui・Canvas を通らない）: `RunSceneCycleSmoke.ps1 -Scenes rendering3dtest -Cycles 3 -Configuration RelWithDebInfo -GameArgs "enable-canvas-view,rendering3dtest-board-smoke-count=16,rendering3dtest-layer-composite-smoke"` で、ランチャー（ImGui）⇄ rendering3dtest（Canvas の板 16 枚・層 1 を OwnRT で不透明度 0.55）を 3 周、`SCENE_CYCLE_SMOKE_RUN result=pass`、EXIT=0。Game.log（`cycle-SCN-ECS-FIF-PRESENT/Game.log`）の ERROR 0、`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0`、借りの失敗 0、ImGui・Canvas の借りの失敗と描かない警告 0。借りはランチャーのフレームで切れ端 27（80,636 バイト、ImGui の頂点・インデックスを含む）、Canvas のある場面で記述子セット 106・切れ端 102。撮影は増やしていない（タスクの notes どおり）。
- Next: TASKS.md の未完の次の項目。
- Notes: RenderingCoordinator の `m_BlitDescriptorSets`（飛行中のスロットごとの組）は、持ち主の無い文脈の予備と、RenderingCoordinator 自身の Blit の予備の経路（`RecordDeferredBlit` が false のときに組を書き換えて Blit する）にだけ残っている（paths の外。予備の経路も借りる形にすれば組を消せる）。見張りが頂点・インデックスの束縛を「バッファの終わりまで」と数えるので、借りた頂点を束縛した後に同じフレームで同じページから借りて書くパスは、重ならなくても `buffer_recorded_same_frame` になる（`VulkanCommandList.cpp` の SetVertexBuffer・SetIndexBuffer。paths の外。束縛に大きさを渡せれば直る）。持ち主の無い文脈・借りの失敗のフレームを起こす検査は書いていない（verify が名指ししていないため）。`PresentationComposer.h`・`.cpp` は BOM が無かったので、日本語を足すのに BOM を付けた（行末は LF のまま）。`PresentationPass.cpp` の LF だけの 8 行は変えていない。

### SCN-ECS-FIF-RHI-OBJECT-CACHE（2026-10-14、反復 3・run 20261013-232555）

- デバイスが render pass・framebuffer・pipeline を鍵で持ち続けるようにした（`VulkanDevice::CreateRenderPass`・`CreateFramebuffer`・`CreateGraphicsPipeline`・`CreateComputePipeline`）。パスの側は今までどおり署名が変われば Create を呼ぶが、鍵に合う物があればそれが返り、実際には作らない。パスを足す人が描画先の入れ替わりを気にしなくてよい形にした。
  - render pass の鍵は記述子の全部（形式・load/store・初期と最後の layout・クリア値）。pipeline の鍵は記述子の全部（シェーダー・頂点の形・状態・render pass・記述子セットのレイアウト）で、シェーダーと render pass はアドレスで引く（持ち続ける pipeline が記述子の写しで生かすので、鍵がある間は使い回されない）。framebuffer の鍵は render pass と attachment の view の組（`VulkanTexture` に足した使い回さない ID `GetObjectCacheId` と depth の layer）と大きさ。Vulkan のテクスチャでない attachment は持ち続けずにその場で作る。
  - 見えなくなった鍵は、8 フレーム求められず、デバイスのほかに持ち手が無いときに手放す（`IDevice::AdvanceObjectCacheFrame`。RenderThread が提出の後に 1 回呼ぶ。破棄は遅らせる破棄の列で提出の完了まで遅れる）。終了は `ReleaseObjectCache`（RenderingCoordinator の WaitIdle の後と、デバイスの破棄の最初）。作るのはロックの外で、同時に作った分は捨てて先の物を返す。
  - 作成数は `IDevice::GetObjectCacheStats`。開発ビルドだけ、作ったフレームに `RHI_CREATE frame=<n> RHI.Create.Framebuffer=<n> RHI.Create.RenderPass=<n> RHI.Create.Pipeline=<n>`、終わりに `RHI_CREATE_SUMMARY`（累計の作成数・使い回した数・手放した数）をログへ出す。
  - 開発ビルドの `--rg-transient-slots=<1〜4>`（`NORVES_RG_TRANSIENT_SLOTS`）で、`TransientResourcePool` のスロットの数を飛行中のフレームの数より多くできる。多いときはスロットを BeginFrame の回数で回す（飛行中の数と同じなら今までどおりフレームの番号で引く）。起動時に `RG_TRANSIENT_SLOTS slots=<n> frames_in_flight=<n>` を出す。
  - GPU だけが書く一時の資源: `TransientResourcePool` のバッファは `CPUAccessible=false`（device-local）で作っていて、CPU から書くフレームの持ち主の領域とは別。変えていない。
  - GBufferPass: 署名が変わると pipeline まで捨てていたので、デバイスが返した render pass が前と同じなら pipeline を残すようにした（スロット 2 で毎フレーム「GBuffer pipelines created」のログが出ていた。作成はデバイスで使い回されて 0 件だった）。
- 検証（証拠は `.harness/runs/20261013-232555/`）:
  - `verify-SCN-ECS-FIF-RHI-OBJECT-CACHE-1.txt`: RelWithDebInfo の Game のビルド EXIT=0。
  - `verify-SCN-ECS-FIF-RHI-OBJECT-CACHE-2.txt`: 起動画面の比較 pass（PSNR 100 dB、輝度の差 0）、EXIT=0。ログ（`startup-capture/SCN-ECS-FIF-RHI-OBJECT-CACHE/default.Game.log`）は `RG_TRANSIENT_SLOTS slots=1`、`RHI_CREATE` はフレーム 0（framebuffer 68・render pass 18・pipeline 58）とフレーム 2（pipeline 2）だけで、決定的な区間の始まり（rendered=109）から撮影（frame=169）までの作成は 0。ERROR 0、`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0`。
  - `verify-SCN-ECS-FIF-RHI-OBJECT-CACHE-3.txt`: `--rg-transient-slots=2` の起動画面の比較 pass（PSNR 100 dB）、EXIT=0。ログ（`...-slots2/default.Game.log`）は `RG_TRANSIENT_SLOTS slots=2 frames_in_flight=1`、`RHI_CREATE` はフレーム 0・フレーム 1（もう 1 つのスロットの描画先の framebuffer 14）・フレーム 2（pipeline 2）だけで、落ち着いた後のフレームの作成は 0。`RHI_CREATE_SUMMARY` は framebuffer 82・render pass 18・pipeline 60 に対し、使い回しが framebuffer 2537・render pass 1705・pipeline 1866（持ち続けなければ毎フレーム作り直していた分）。ERROR 0、`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0`。
- Next: TASKS.md の未完の次の項目。
- Notes: 撮影では鍵を手放す道（`evicted`）は通っていない（起動画面では描画先の組が替わらないため。シーンの出入り・解像度の変更で通る）。手放すまでの 8 フレームは、持ち続ける framebuffer がパスの持つ描画先の texture を生かすので、解像度の変更やシーンを出た後に古い texture がその間残る。スロット 2 では pipeline の Create の呼び出しが毎フレーム約 11 回残っている（使い回しで作成は 0。パスが署名の変化で pipeline も作り直しに行く所。鍵を引く費用だけ）。`Debug/Stats.h`（paths の外）には足していないので、作成数はログの行で見る。持ち続ける物を起こす検査（同じ鍵で同じ物・texture が替われば別の物・手放し）は書いていない（verify が名指ししていないため）。`VulkanDevice.h`・`.cpp`・`IDevice.h`・`GBufferPass.cpp`・`RenderingCoordinator.cpp`・`TransientResourcePool.*` は行末の混在をそのまま保った（numstat の 2 通りが一致）。

### SCN-ECS-FIF-RHI-OBJECT-CACHE（2026-10-14、反復 1・run 20261014-003223、評価の差し戻しの直し）

- pipeline の鍵から render pass のアドレスを外し、render pass の互換の条件（attachment の数・形式、depth の有無と形式）を値で入れた（`VulkanDevice.cpp` の `AppendRenderPassCompatibilityKey`）。ImGui・Dummy のオーバーレイは非所有の参照で render pass を渡すので、アドレスで引くと render pass の破棄の後にアドレスが別の形式の render pass に再利用されたとき古い pipeline が返り得た。Vulkan では pipeline は互換の render pass ならどれとでも使え、作成に使った render pass より長く生きてよいので、値の鍵で安全に使い回せる。サンプル数は `VulkanRenderPass` が常に 1 で作るので鍵に入れていない（MSAA を足したら入れる）。
  - framebuffer の鍵は `GetRenderPass` が同じ物を返すようアドレスを残し、互換の条件の値も足した（アドレスが再利用されても互換でない framebuffer は返さない）。
  - 評価の non-blocking: `GBufferPass.cpp` の render pass を作れなかったときのログの説明文を日本語にした。
  - 評価の指摘 2（統計につなぐ）は親が完了条件をログに直したので対象外。
- 検証（`.harness/runs/20261014-003223/`）:
  - `verify-SCN-ECS-FIF-RHI-OBJECT-CACHE-1.txt`: RelWithDebInfo の Game のビルド EXIT=0。
  - `verify-SCN-ECS-FIF-RHI-OBJECT-CACHE-2.txt`: 起動画面の比較 pass（PSNR 100 dB、輝度の差 0）、EXIT=0。ログは `RG_TRANSIENT_SLOTS slots=1`、`RHI_CREATE` はフレーム 0（framebuffer 68・render pass 18・pipeline 58）とフレーム 2（pipeline 2）だけで、撮影（frame=169）までの落ち着いた後のフレームの作成は 0。ERROR 0、`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0`。
  - `verify-SCN-ECS-FIF-RHI-OBJECT-CACHE-3.txt`: `--rg-transient-slots=2` の起動画面の比較 pass（PSNR 100 dB）、EXIT=0。ログは `RG_TRANSIENT_SLOTS slots=2 frames_in_flight=1`、`RHI_CREATE` はフレーム 0・1（もう 1 つのスロットの framebuffer 14）・2（pipeline 2）だけで、落ち着いた後のフレームの作成は 0。`RHI_CREATE_SUMMARY` は使い回しが framebuffer 2537・render pass 1705・pipeline 1866。ERROR 0、`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0`。
- Next: TASKS.md の未完の次の項目。
- Notes: アドレスの再利用の再現（非所有の render pass を破棄して別形式で作り直す）は GPU 上では起こしていない（起動画面では render pass が替わらないため）。鍵が値になったことはコードで確かめた。`VulkanDevice.cpp`・`GBufferPass.cpp` は行末の混在をそのまま保った（numstat の 2 通りが一致）。

### SCN-ECS-FIF-DDGI（2026-10-14、反復 2・run 20261014-003223）

- `DDGIProbePass` の資源を、飛行中のフレームの番号 × ViewId × ViewportId ごとの組（`FrameResources`）から、ViewId × ViewportId ごとの組（`ViewResources`）へ変えた。組が持つのは GPU だけが読み書きする資源（レイの結果のバッファ、照射・距離のアトラスと履歴のピンポン）と状態（`*AtlasState`・`ResultState`・`bAtlasValid`・`AtlasProbeCount`・volume）だけ。
  - 履歴: 前回この組で更新できたアトラスが今の volume・プローブ数と合えば、現在と履歴を入れ替え（`std::swap` を保つ）、前回のアトラスを読みながらもう一方へ書く。飛行中のフレームの数に関わらず毎回 1 つ前の更新を読むので、2 にしても履歴が 2 つに分かれない。別の番号の組から前のフレームの物を探す処理（`FrameNumber + 1` の候補探し）は要らなくなったので消した。
  - CPU が書く定数 3 つ（レイの問い合わせ・bounce・アトラスの更新）・instance の表（`FrameUploadUsage::Storage`）・記述子セット 3 つは、Execute ごとに `context.FrameResources` から借りる。記述子セットのレイアウトは `GetOrCreateDescriptorSetLayout` で `EnsurePipeline` に 1 回引く。持ち主の無い文脈（`DDGIProbe*VulkanTest` が `Execute` を直接呼ぶ）では使い回さない定数・セットを作り、`Shutdown` まで `m_UnownedBuffers`・`m_UnownedDescriptorSets` に持つ。借りられなかったフレームはエラーを出して更新しない（パスは無効にしない）。
  - 履歴の無効化: Execute の始めに `bAtlasValid` を落とし、更新を終えたら立てる（以前どおり）。加えて、組が最後に命令を記録したフレームの世代を持ち、次の Execute でそのフレームが提出されなかった（`FrameSubmissionOutcome::Aborted`）と分かれば、履歴を捨てて状態を未定義から始め直す。
  - ビューの消滅と ID の使い回し: 8 フレームより長く Execute されない組は、次の Execute の始めに手放す（アトラス・結果のバッファの破棄はデバイスが提出の完了まで遅らせる）。同じ ID が後で別のビューに使われても古い履歴は読まない。
  - getter はビュー・ビューポートで引く `(viewId, viewportId)` にし、LightingPass（`m_DDGIProbePass.GetIrradianceAtlas` ほか 3 か所）は `context.FrameIndex` を渡さなくなった。試験の呼び出しの形 `(frameIndex, viewId, viewportId)` は frameIndex を見ない形で残した。`DDGIProbePass.cpp` に飛行中の番号・`FrameIndex`・守られた作成の区間（`FrameResourceProtectedCreationScope`）の扱いは無い。
  - 複数のビューポートで同じパスを Execute するとき: 組は ViewId × ViewportId で別になり、定数・セットは Execute ごとに別の切れ端・セットを借りる（コードの照合で確かめた。起動画面はビューポート 1 つ）。
- 検証（`.harness/runs/20261014-003223/`）:
  - `baseline-build-DDGI.txt`: 変更前の RelWithDebInfo の Game のビルド EXIT=0。
  - `verify-SCN-ECS-FIF-DDGI-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0・警告 0（DDGIProbePass.cpp・LightingPass.cpp を再コンパイル）。
  - `verify-SCN-ECS-FIF-DDGI-2.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、`indirect_lighting=rtgi`、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=7E-06・max_channel_difference=1・within_limits=True、result=pass、EXIT=0。PNG は天球・地面・球・岩・家と影が以前どおり。
  - `verify-SCN-ECS-FIF-DDGI-3.txt`: EXIT=0。`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0 resources=0 events=0 rewrites=0`、DDGIProbePass の行 0。Game.log の ERROR 0。
  - DDGI を有効にする構成: Game には DDGI の volume を設定する呼び出しが無く（`SetDDGIVolumeParameters` を呼ぶのは試験だけ）、起動の引数では有効にできない。試験 `DDGIProbeUpdateVulkanTest`（RelWithDebInfo、`check-SCN-ECS-FIF-DDGI-test-build.txt`・`check-SCN-ECS-FIF-DDGI-update-test.txt`）を走らせ、EXIT=0・VUID_COUNT=0。seed（フレーム番号 1）の後の bounce（フレーム番号 2）が seed のアトラスを履歴として 1 回読み、hysteresis=0.6 の混ぜ方で期待値と一致した（`single_probe_bounce`・`visibility` の各項目が通過）。履歴の更新は 1 フレーム 1 回、収束の混ぜ方は以前と同じ。撮影は増やしていない。
- Next: TASKS.md の未完の次の項目（SCN-ECS-FIF-READBACK など）。
- Notes: 提出しなかったフレームの後の履歴の捨て方・8 フレームでの組の手放し・2 つのビューポートの同時の Execute を起こす検査は書いていない（verify が名指ししていないため）。既定のアトラス（`m_DefaultIrradianceAtlas` の 1 回だけの遷移）は以前のまま（提出しなかったフレームで遷移した場合の扱いは変えていない）。`DDGIProbeRayQueryVulkanTest`・`DDGIProbeRadianceVulkanTest` は走らせていない（3 引数の getter を残したのでコンパイルの形は変わらない）。`DDGIProbePass.h`・`.cpp` は全行 CRLF・BOM 付きのまま、`LightingPass.cpp` は行末の混在を保った（numstat の 2 通りが一致）。

### SCN-ECS-FIF-READBACK（2026-10-14、反復 2・run 20261014-005646）

- フレームの持ち主に読み戻しの口を足した（`Rendering/FrameReadback.h`。ヘッダだけで実装し、`FrameResourceOwners` が 1 つ持つ）。パスは記録中に `FrameResourceLane::RequestReadback` で読み戻し先（ホストが読めて写像したままのバッファ）を借りる。
  - 1 つの要求で 2 つの形を扱う: GPU が直接書く（返したバッファを storage buffer として束縛し、書いた後に HostRead へのバリアを記録する。0 から数えるなら `bZeroOnHost` でホストが 0 を書くか、パスが GPU で FillBuffer する）と、他の資源からのコピー（`CopySource` を渡すと口がコピーとバリアを記録する）。
  - `CommitSubmitted` で要求へ実際の提出の通し番号を付け、`Retire`（開始の回収と同じ時点）でその完了の後に RenderThread で受け取り手へ渡す。提出しなかったフレーム（中止・放棄）の要求は取り消し（`FrameReadbackStatus::Canceled`）として次の開始で渡す。受け取りの呼び出しが終わるまで、その読み戻し先をほかの要求へ使い直さない。
  - 受け取り手は `FrameReadbackReceiver`（パスのメンバ）で登録する。要求が持つのは共有の状態（受け取り手のポインタ・世代）と要求したときの世代だけで、パスが消えると共有の状態のポインタが null になり、`CancelPending` で世代を進めると前の要求の結果は渡さずに捨てる（読み戻し先は GPU の完了を待ってから戻す）。結果には要求の番号・提出の通し番号・ビュー・ビューポートを付ける。
  - 上限は待っている要求 64・読み戻し先の合計 4 MiB。満杯なら借りられず、パスはそのフレームの統計を飛ばす。終了時は `FrameResourceOwners::Reset`（GPU を待った後）で提出済みの要求を全部渡し、残りを取り消す。RenderingCoordinator の終了では Reset がビューの Shutdown より前なので、パスの集計の行に最後のフレームまで入る。読み戻し先はその後 `ReleaseReadback` で手放す。
  - ログ `FrameResource.Readback reason=<periodic|dropped_full|release>` に `Requests`・`Delivered`・`Canceled`・`Orphaned`（受け取り手が消えた・世代を進めた）・`DroppedFull`・`Failures`・`Flushed`・`Pending`・`MaxPending`・`Buffers`・`BufferBytes` を出す（開発ビルドだけ。定期は持ち主の統計と同じ 600 フレームおき）。
- 移した使い手:
  - `ShadowProbePass`: 飛行中の 4 つの統計の枠と `ExecuteIndex + 2` の判定を消し、測るフレームごとに読み戻し先を借りる（GPU で 0 にする形は保った）。測定のやり直し（エポックの始まり）と Shutdown で `CancelPending`。
  - `MegaGeometryPass`: `StatsSlot`・`EnsureStatsSlots`・`RenderFrameCount + 2` の判定を消した。2 パスの記録で描く直前に借り、ログ（`MEGA_OCCLUSION`・`SW_RASTER_BIN`・`SW_RASTER_OVERSIZE`・`MEGA_CAPACITY`）は受け取り（`OnFrameReadback`）で出す。出す条件（最初の 1 回・相対フレームが 30 の倍数・`bStatsEveryFrame`）は変えていない。持ち主の口はコマンド（`MegaGeometryPassCommand::FrameResources`・ViewId・ViewportId）で渡す。
  - `LightingPass`（照明の VSM の統計）: 16 の枠と `CompletedRenderFrameSerial` の判定を消し、実行ごとに借りる（`bZeroOnHost`）。借りられないときは今までどおり読まない置き場へ束ねる。
- 検証（`.harness/runs/20261014-005646/`）:
  - `baseline-build-READBACK.txt`: 変更前の RelWithDebInfo の Game のビルド EXIT=0。
  - `verify-SCN-ECS-FIF-READBACK-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0・警告 0。
  - `verify-SCN-ECS-FIF-READBACK-2.txt`: MAX_FRAMES_IN_FLIGHT=1 のまま決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=8E-06・max_channel_difference=1、result=pass、EXIT=0。ログは ERROR 0、`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0`、`FrameResource.Readback reason=release Requests=339 Delivered=339 Canceled=0 Orphaned=0 DroppedFull=0 Failures=0 Flushed=2 MaxPending=2 Buffers=2`。`MEGA_OCCLUSION` は基準（SCN-MEGA-BASE-RWDI）と同じ frame=3・31・61・91・109・139 に出て、終了時の吐き出しで frame=169 も出た。`VSM_LIGHTING_STATS executes=171` は基準と同じ。
  - `verify-SCN-ECS-FIF-READBACK-3.txt`: mega-stress（5 万体、1920x1080、close）の撮影 result=pass、EXIT=0。ログは ERROR 0、`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0`、`Readback Requests=277 Delivered=277 DroppedFull=0`。`MEGA_OCCLUSION` は frame=5・33・63 と相対フレーム 0・30・60 に出て、30 フレームおき（前の mega-stress の撮影 SCN-ECS-FIF-DYNAMIC-UBO・SCN-ECS-FIX-GT-PACING も 30 フレームおき）。統計の 3 種の行は計 21 行。
  - `check-SCN-ECS-FIF-READBACK-probe.txt`（移した ShadowProbe の確かめ。verify では通らないので 1 回だけ）: `-ShadowProbe` の起動画面の撮影 EXIT=0。`SHADOW_PROBE method=csm frames=361 mean_texel_mm=20.702`・`method=vsm mean_texel_mm=11.209`・`SHADOW_PROBE_AGREE ratio=0.999358` は以前の測定（SCN-BANANAS-PERF-probe）と同じ値。`Readback Requests=2147 Delivered=2147 Flushed=3`。
- Next: TASKS.md の未完の次の項目。
- Notes: 他の資源からのコピーの形・満杯で飛ばす道・取り消し（中止したフレーム）・受け取り手が消えた後の捨て方は撮影では通っていない（どの撮影でも Canceled・Orphaned・DroppedFull が 0）。それらを起こす検査は書いていない（verify が名指ししていないため）。持ち主の無い手組みの文脈（`RenderGraphCompileTest` の照明の VSM の統計の検査。ctest で無効）では統計を取らなくなった。AutoExposure・VT のフィードバック・ページの要求の列などほかの読み戻しは移していない。`FrameReadback.h` は新規なので BOM + CRLF、ほかは行末の混在を保った（numstat の 2 通りが一致）。

### SCN-ECS-FIF-SYNC-SUBMIT-SCOPE（2026-10-14、反復 3・run 20261014-005646）

- `VulkanDevice::EndSingleTimeCommands`（単発の提出。提出の後に `graphicsQueue.waitIdle()` で待つ）を計装した。開発ビルド（`NORVES_ENABLE_STATS`）だけ数え、Release では何もしない。呼び出し側は変えていない。
  - 待ち（キューのロック＋`waitIdle`）を区間 `RHI.ImmediateSubmitWait`（呼んだスレッドの区間）で囲んだ。提出のロック（`GraphicsSubmitScope`）と待機のキューのロックの待ちを `lock_wait_ms`、`waitIdle` の時間を `gpu_wait_ms`・`max_gpu_wait_ms` に分けて集計する。
  - 呼び出し元: 入口でスタック（`CaptureStackBackTrace`）を取り、DbgHelp（`SymFromAddr`。実行ファイルの置き場だけを探し、シンボルサーバーへ行かない）で名前にする。RHI の中（`NorvesLib::RHI::`）と `std::` の関数を飛ばした最初の関数を `caller`、1 つ目の関数を `via`（`VulkanBuffer::Update`・`VulkanTexture::Update`・`VulkanAccelerationStructure::Build`）にする。名前はアドレスごとに覚え、解決はキューのロックの外で行う。
  - 描画フレーム: `AdvanceObjectCacheFrame`（RenderingCoordinator が毎フレームの EndFrame の後に呼ぶ）で番号を進め、呼んだスレッドを `render`、デバイスを作ったスレッドを `main` と呼ぶ（同じなら `main_render`、どちらでもなければ `other`）。呼び出し元・経由・スレッドごとに回数・フレーム数・最初と最後のフレーム・1 フレームの最大の回数を持つ。
  - ログ: ApplicationProcessor が `FRAME_PACING` と同じ時点（撮影の保存・`--exit-after-*` の終了の直前）に `LogVulkanImmediateSubmitSummary` を呼び、`IMMEDIATE_SUBMIT caller=<名前> thread=<名前> count=<n> max_per_frame=<n> via=... frames=... first_frame=... last_frame=... lock_wait_ms=... gpu_wait_ms=... max_gpu_wait_ms=...` を呼び出し元ごとに、最後に `IMMEDIATE_SUBMIT_SUMMARY` を出す。`IDevice.h`・`RenderWorld.h` は paths の外なので、関数は VulkanDevice.cpp に定義し ApplicationProcessor.cpp で宣言して呼ぶ（試験のフックと同じ形）。
- 検証（`.harness/runs/20261014-005646/`）:
  - `baseline-build-SYNC-SUBMIT-SCOPE.txt`: 変更前の RelWithDebInfo の Game のビルド EXIT=0。
  - `verify-SCN-ECS-FIF-SYNC-SUBMIT-SCOPE-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。警告 2 件（`VulkanDevice.h` の NOMINMAX の再定義、既存の文字列の C4566）は変更していない行（ヘッダの 9 行目、既存の書式の文字列）を指す。
  - `verify-SCN-ECS-FIF-SYNC-SUBMIT-SCOPE-2.txt`: MAX_FRAMES_IN_FLIGHT=1 のまま決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0・psnr_db=100・mismatched_pixel_ratio=8E-06・max_channel_difference=1、result=pass、EXIT=0。ログの ERROR 0、`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0`。
  - `verify-SCN-ECS-FIF-SYNC-SUBMIT-SCOPE-3.txt`: `IMMEDIATE_SUBMIT` の照合 EXIT=0。`IMMEDIATE_SUBMIT_SUMMARY count=258 callers=19 frame=171 lock_wait_ms=0.119 gpu_wait_ms=124.763`。
- 起動画面（決定論・1 本のスレッド）で分かったこと（MEASURE-FIF の材料）:
  - 毎フレーム: `SkyAtmospherePass::GenerateSunDiskTexture`（via `VulkanTexture::Update`）が count=171・frames=171・max_per_frame=1、待ちの合計 65.8 ms（最大 2.6 ms）。描画フレームごとに 1 回、キュー全体を待っている。
  - 立ち上がりの数フレーム: `RayTracingSceneSubsystem::BuildAccelerationStructures`（via `VulkanAccelerationStructure::Build`）が count=17・frames=4（フレーム 0〜6）・max_per_frame=11、待ちの合計 16.3 ms。
  - フレーム 0 だけ: 各パスの Initialize・LUT の生成（`CreateIblResources` 22 回、`LightingPass::Initialize` 10 回、`ForwardPass::Initialize` 10 回ほか）。
- Next: TASKS.md の未完の次の項目（SCN-ECS-MEASURE-FIF ほか）。
- Notes: `GenerateSunDiskTexture` の毎フレームの同期の提出は、直す項目が決まっていない（`SkyAtmospherePass` は paths の外）ので直していない。MEASURE-FIF で 2 フレームの時の実害を見てから決める。決定論の撮影は 1 本のスレッドで描くのでスレッドは全部 `main_render` で、`render`・`main`・`other` の振り分けは撮影では確かめていない。Release のビルドは走らせていない（追加はすべて `NORVES_ENABLE_STATS` の中）。初回の名前の解決で Game.pdb を読む（決定論の撮影の比較には影響なし）。`VulkanDevice.cpp`・`ApplicationProcessor.cpp` は行末の混在を保った（numstat の 2 通りが一致）。

### SCN-ECS-FIF-SWITCH（2026-10-14、反復 4〜5・run 20261014-005646）

- `VulkanSwapChain` の `MAX_FRAMES_IN_FLIGHT` を 2 にした。実際に使うスロットの数 `m_framesInFlight` を作成時に 1 回決め、同期オブジェクト・スロットの番号の回し方・`GetMaxFramesInFlight` はこの数を使う。開発ビルドだけ `--frames-in-flight=<1|2>`（Game が `NORVES_FRAMES_IN_FLIGHT` へ渡す）で 1 に戻せる。Release は常に 2。起動時に `FRAMES_IN_FLIGHT count=<n> max=2` を出す。
- シーンの出入り: 各シーンの終わり（`MegaBananasRoutine`・`MegaStressRoutine` の `Leave` の先頭、`Rendering3DTestRoutine::Leave` の後始末の開始の前）と ApplicationProcessor の終了は、資源を手放す前に無条件で `RenderWorld::WaitForRender`（RenderThread の待ちの後に `Device::WaitIdle`）を呼ぶので、両方のスロットの完了を待ってから解放する。Rendering3DTest は反復 5 まで F11 の部品があるときだけ待っていたので、反復 7 で無条件の待ちを足した（下の追記）。
- 一時の描画先のプール（`TransientResourcePool::EndFrame`）: 使った描画先を鍵ごとの並びの後ろへ足していたので、借りなかった物が前へ回り、同じ役目に返る texture がフレームごとにずれていた。2 フレームでは各スロットの 5 つの texture の巡回が 10 フレーム周期になり、framebuffer の鍵の手放し（8 フレーム）より長いため、Rendering3DTest（対話の起動）で LightingPass・ForwardPass の透明・VolumetricsPass・PresentationPass の overlay の framebuffer を毎フレーム 2〜4 個作り直していた（1 フレームでは周期 5 で手放しより短く、出ていなかった）。使った物を借りた順のまま先頭へ戻すようにし、同じ順に借りるフレームには同じ texture が返る。周回の `RHI_CREATE` の行は 818 → 26（各シーンへ入った直後だけ。1 フレームの周回と同じ形）。
- 検証（`.harness/runs/20261014-005646/`）:
  - `verify-SCN-ECS-FIF-SWITCH-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0（警告 3 件は変更していない既存の行と libwebsockets の設定）。
  - `verify-SCN-ECS-FIF-SWITCH-2.txt`: Debug の起動画面の撮影 result=pass、EXIT=0。`FRAMES_IN_FLIGHT count=2`、`vulkan_validation` は error_count=0・warning_count=0・vuid_count=0（`default.Validation.log` は 0 行）。Game.log の ERROR 0、`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0`、`FRAME_RESOURCE_STALE` 0。PNG は天球・地面・球・岩・家が以前どおり。
  - `verify-SCN-ECS-FIF-SWITCH-3.txt`: RelWithDebInfo の決定論の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0.0021・psnr_db=60.066・mismatched_pixel_ratio=0.148・max_channel_difference=13・within_limits=True、result=pass、EXIT=0。ログは `FRAMES_IN_FLIGHT count=2`・ERROR 0・`FRAME_RESOURCE_STALE` 0・`RHI_CREATE` はフレーム 0〜2 だけ・`Readback Requests=356 Delivered=356 Canceled=0 Flushed=4`。
  - `verify-SCN-ECS-FIF-SWITCH-5.txt`: `FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0` の照合 EXIT=0。
  - `verify-SCN-ECS-FIF-SWITCH-4.txt`（ログは `verify-SCN-ECS-FIF-SWITCH-4.Game.log`）: rendering3dtest・mega-bananas・mega-stress の 3 周 `SCENE_CYCLE_SMOKE_RUN result=pass`、EXIT=0。`FRAMES_IN_FLIGHT count=2`、`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0 frames=3121`、`FRAME_RESOURCE_STALE` 0・`FRAME_RESOURCE_OWNER_UNRETIRED` 0・ERROR 0。`RHI_CREATE` は各シーンへ入った直後と Rendering3DTest の入りの後の 3 フレームの束だけで、どの周回も同じ並び（落ち着いた後は 0）。
  - `verify-SCN-ECS-FIF-SWITCH-6.txt`: `--render-parallel-record=on,frame-arena-initial-kb=4` の決定論の撮影 result=pass（psnr_db=60.068・within_limits=True）、EXIT=0。`RENDER_PARALLEL_RECORD enabled=1`・記録のリスト 4・`FrameResource.Lanes=4`、`FrameResource.Upload.TotalPagesAdded=15`（1 以上）・`Descriptor.TotalPoolsAdded=6`・Failures 0、ERROR 0・`FRAME_RESOURCE_STALE` 0。
  - `verify-SCN-ECS-FIF-SWITCH-7.txt`: その撮影の `FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0` の照合 EXIT=0。
  - 比較の差（psnr 60 dB）は 2 フレームでの時間方向の履歴の進み方の違いによる。`--frames-in-flight=1` の撮影（`check-SCN-ECS-FIF-SWITCH-fif1.txt`）は psnr_db=100、2 フレームの撮影の繰り返し（`check-SCN-ECS-FIF-SWITCH-repeat.txt`）は互いに psnr_db=100 で、2 フレームでも決定論は保たれている。
- 同じフレームの 2 回目の Execute・中止・終了時の読み戻しの吐き出しの根拠: `FrameResourceOwnerTest`（SCN-ECS-FIF-FRAME-OWNER、`ctest -R "^FrameResource"` Passed。同じフレームの 2 回目の Execute が前の分を戻さない・中止と提出も中止もしないまま次の開始が来た分の回収・2 スロットの回収・通し番号の振り直し・世代の拒否）と `FrameResourceUploadArenaTest`（SCN-ECS-FIF-UPLOAD-ARENA。中止は次の開始で戻る・完了していないスロットは戻らない）。終了時の吐き出しは READBACK の撮影（`Flushed=2`）と今回の撮影（`Flushed=4`、Requests と Delivered が一致）で確かめた。読み戻しの取り消し（中止したフレーム）を起こす試験は無い（READBACK の Notes のとおり）。
- Next: SCN-ECS-MEASURE-FIF（2 フレームの効果の節目の計測）ほか TASKS.md の未完の項目。
- Notes: プールの並びを直す前の周回（毎フレームの作成あり）と直した後の周回は `.harness/runs/scene-cycle-smoke-diag/`、1 フレームの比較の周回は `.harness/runs/scene-cycle-smoke-fif1/` に残した（作成元の特定には一時の計装を使い、コミット前に外した）。`TransientResourcePool.cpp` は LF・BOM のまま（numstat の 2 通りが一致）。バッファのプール（`m_BufferPool`）は framebuffer の鍵に関わらないので並べ方を変えていない。

### SCN-ECS-MEASURE-FIF（2026-10-14、反復 6・run 20261014-005646、未完: mega-stress が 2 フレームで撮れない）

- 分け方: RenderThread の区間をトレース（`*.trace.csv` の Scope 行。時刻は区間の終わり）から描画フレームごとに切った。フレーム i は `RT.WaitPacket` の i 回目の始まりから i+1 回目の始まりまで。パケット待ち = `RT.WaitPacket` の長さ、GPU に依存する待ち = その間に終わった RenderThread の `RT.WaitFrameFence`・`RHI.ImmediateSubmitWait`・`RT.AcquireImage`・`RT.Present` の合計、CPU の作業 = 残り。窓は metrics.json の `window_first_frame`〜`window_last_frame`（300 フレーム）。各列の median・p95 はフレームごとの値から求めた（列の median の和ではない）。集計の出力は `check-SCN-ECS-MEASURE-FIF-*-split.txt`。
- バナナ 1 万本 overview（2 フレーム、`FRAMES_IN_FLIGHT count=2`。GPU のフレーム median 10.59 ms・p95 13.80 ms）:

| 列（ms） | median | p95 | max |
|---|---|---|---|
| RenderThread の 1 フレーム | 12.853 | 16.135 | 31.578 |
| CPU の作業 | 4.533 | 5.896 | 20.647 |
| GPU に依存する待ち（計） | 8.193 | 11.477 | 12.429 |
| └ `RT.WaitFrameFence` | 0.003 | 0.005 | 0.011 |
| └ `RHI.ImmediateSubmitWait` | 8.139 | 11.421 | 12.370 |
| └ `RT.AcquireImage` | 0.005 | 0.008 | 0.023 |
| └ `RT.Present` | 0.046 | 0.064 | 0.207 |
| パケット待ち | 0.000 | 0.000 | 0.003 |

- 答え: 残っている。`RT.WaitFrameFence` 以外の GPU に依存する待ちが p95 で 11.4 ms（1 フレームの 7 割）あり、窓の 300 フレームすべてに 1 回ずつ出る。呼び出し元は `IMMEDIATE_SUBMIT caller=SkyAtmospherePass::GenerateSunDiskTexture thread=render count=649 max_per_frame=1 via=VulkanTexture::Update frames=649 first_frame=0 last_frame=648 gpu_wait_ms=5571.137 max_gpu_wait_ms=23.572`（描画フレームの番号で毎フレーム 1 回。窓の 300 回の待ちは呼び出し 1 回あたり median 8.1 ms）。1×1 の太陽の円盤の texture を毎フレーム単発の提出で書き、`waitIdle` で前のフレームの GPU の完了まで止まるので、フェンスの待ちがこちらへ移っただけで CPU と GPU は重なっていない（RenderThread の 1 フレーム ≒ CPU の作業 + GPU の待ち）。ほかの呼び出し元は窓の外（初期化はフレーム 0、`RayTracingSceneSubsystem::BuildAccelerationStructures` はフレーム 0〜4、`CreateAndUploadCookedTexture` は main スレッドのフレーム 1 に 75 回）。→ `SCN-ECS-FIX-SUN-DISK-UPLOAD` を足した。`RT.AcquireImage`・`RT.Present` は p95 0.1 ms 未満で、直す項目は足していない。
- mega-stress 5 万個 close（2 フレーム）: 撮れない。2 回とも（`stress-try1`・`stress-try2`）、`MEGA_BVH bvh_instances=50000` の後に `RenderThread stopped after an unhandled exception`（`VulkanBuffer` の `allocateMemory` の失敗）で止まり、有効な描画フレームが 5 件だけで `CAPTURE_SCENE result=fail`、EXIT=1（`verify-SCN-ECS-MEASURE-FIF-1.txt`・`-1b.txt`）。`--frames-in-flight=1` では pass（`check-SCN-ECS-MEASURE-FIF-stress-fif1.txt`）で、そのときも `VRAM_BUDGET` の使用量の山は 12224 MB（予算 12186 MB）まで上がっている。→ `SCN-ECS-FIX-STRESS-VRAM-FIF2` を足した。
- 参考（問いの条件ではない）: mega-stress 5 万個 close の 1 フレームでは RenderThread の 1 フレーム median 40.197・p95 47.176、CPU の作業 5.227・9.515、`RT.WaitFrameFence` 34.450・39.725、`RHI.ImmediateSubmitWait` 0.216・0.621（同じ `GenerateSunDiskTexture` が毎フレーム出るが、フェンスで GPU が空になった後なので短い）、パケット待ち 0。
- 検証（`.harness/runs/20261014-005646/`）:
  - `verify-SCN-ECS-MEASURE-FIF-1.txt`・`-1b.txt`: mega-stress の撮影 EXIT=1（上のとおり）。verify 2（その CheckCaptureMetrics）は撮影が無いので走らせていない。
  - `verify-SCN-ECS-MEASURE-FIF-3.txt`: バナナの撮影 `CAPTURE_SCENE result=pass`、EXIT=0。
  - `verify-SCN-ECS-MEASURE-FIF-4.txt`: `CHECK_CAPTURE_METRICS result=pass views=overview`、EXIT=0。
- Next: SCN-ECS-FIX-STRESS-VRAM-FIF2 と SCN-ECS-FIX-SUN-DISK-UPLOAD を直した後に、この項目の両方の撮影をやり直して表を埋める（SCN-ECS-FIF-SYNC-SUBMIT-SCOPE の Notes の「直すかは MEASURE-FIF で見てから」はこの結果で決まった）。
- Notes: 提出・完了の通し番号はトレースに出ていないので、呼び出し元の特定は `IMMEDIATE_SUBMIT` の描画フレームごとの数（毎フレーム 1 回はこの呼び出し元だけ）と、窓の RenderThread の `RHI.ImmediateSubmitWait` が各フレーム 1 回であることの突き合わせで行った。集計のスクリプトは作業用の一時のもので、リポジトリには入れていない。

### SCN-ECS-FIF-SWITCH の評価の指摘への対応（2026-10-14、反復 7・run 20261014-005646）

- `Rendering3DTestRoutine::Leave` は、後始末（`CleanupM9WorldAcceptance` 以降）と Leave 直後の `GameModeScope::Cleanup` の前に待つ箇所が F11 の部品の後始末の中にしか無く、通常の `rendering3dtest → launcher` では待たずに資源を手放していた。退出のログ（`CAMERA_COMPONENT_SMOKE stage=exit`）の後、後始末の開始の前に無条件の `WaitForRender()` を足した（F11 の分岐の中の待ちは残した。2 回目は待つものが無いので直ちに返る）。
- 検証（`.harness/runs/20261014-005646/`。親の注記どおり、ビルドと起動画面の比較と周回だけ回した）:
  - `verify-SCN-ECS-FIF-SWITCH-8.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-ECS-FIF-SWITCH-9.txt`: 決定論の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0.0021・psnr_db=60.066・within_limits=True、result=pass、EXIT=0（反復 5 と同じ値）。ログは `FRAMES_IN_FLIGHT count=2`・`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0 frames=181`・`FRAME_RESOURCE_STALE` 0・ERROR 0。
  - `verify-SCN-ECS-FIF-SWITCH-10.txt`（ログは `verify-SCN-ECS-FIF-SWITCH-10.Game.log`）: rendering3dtest・mega-bananas・mega-stress の 3 周 `SCENE_CYCLE_SMOKE_RUN result=pass`、EXIT=0。`FRAMES_IN_FLIGHT count=2`、`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0 frames=3121`、`FRAME_RESOURCE_STALE` 0・`FRAME_RESOURCE_OWNER_UNRETIRED` 0・ERROR 0、`3Dレンダリングテスト終了` 3 回。`RHI_CREATE` の行の並びは反復 5 の周回と同じ形（各シーンへ入った直後と Rendering3DTest の入りの後の 3 フレームの束だけ）。
  - `verify-SCN-ECS-FIF-SWITCH-11.txt`: 撮影の `FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0` の照合 EXIT=0。
- Next: SCN-ECS-FIX-STRESS-VRAM-FIF2・SCN-ECS-FIX-SUN-DISK-UPLOAD → SCN-ECS-MEASURE-FIF の撮り直し。
- Notes: `Rendering3DTestRoutine.cpp` は行末の混在を保った（numstat の 2 通りが 4/0 で一致）。

### SCN-ECS-FIX-STRESS-VRAM-FIF2（2026-10-14、反復 8・run 20261014-005646）

- 溢れさせた分の特定（MEASURE-FIF の失敗の撮影 `stress-try1`・`stress-try2` と 1 フレームの `stress-fif1` の `VRAM_LEDGER`・`MEGA_BVH`・`VRAM_BUDGET` の並び）:
  - 2 フレームで増えた分: `VisibilityRasterPass` の記録の表（`VisBuffer_DrawRecords`）が飛行中のフレームの組（FrameUseRing の枠）ごとにあり、5 万個では 1 つ 2 GiB（command_slots 最大 26989696 × 64 B を 2 の冪へ切り上げ）。1 フレームでは 1 枚、2 フレームでは 2 枚になり、落ち着いた後の使用量が 2 GB 増える（1 フレーム 8453 MB に対し、保持だけ直した 2 フレームの撮影 `try/a1` は 10745 MB）。失敗の 2 回は、2 枚目の表が 2048 MB へ伸びる所（1 フレームでは `visbuffer_records mb=2048` が出るフレーム）の後に止まった。
  - 立ち上がりの山: `MegaGeometryPass` が倍々に伸ばす列（BVH の列 64 MB → 1 GiB → 2 GiB、visibility 39 MB → 677 MB → 1.36 GB、IndirectDraw・DrawInfo・ソフトウェアラスタの一覧）の古い物を `RetiredBufferFrames`（8 フレーム）持ち、さらに参照を落とした後に RHI の遅延破棄が提出の完了まで残すので、伸ばした古い物が全部同時に生きていた（合計 4 GB 前後）。1 フレームの山 12224 MB は落ち着いた後の 8453 MB にこの分が乗ったもの。
- 直したこと:
  - `VisibilityRasterPass`: 記録の表を組から外してパスに 1 つ（`m_RecordTable`・`m_RecordCapacity`・`m_RecordState`）にした。表は GPU だけが書き読みし（ホストの記録は組ごとの `RecordUpload` からのコピー）、同じキューで前のフレームの読み手（解決・分類・合流）の後に次のフレームの書き込みが並ぶことは、書く前のバリア（`GenericRead` → `CopyDest`/`UnorderedAccess`。`GenericRead` の段は頂点・フラグメント・計算・転送・間接引数を含む）が保証する（MegaGeometry の IndirectDraw などと同じ形）。伸ばすときの古い表は参照を落とし、GPU の完了までは RHI の遅延破棄が残す。Shutdown で表を手放す。
  - `MegaGeometryPass`: `RetiredBufferFrames` を 8 → 0 にした（手放したフレームの次のフレームの記録の終わりで参照を落とし、GPU の完了までの保持は RHI の遅延破棄に任せる。手放したフレームの古い中身のコピーの記録は残る）。
- `VRAM_BUDGET` の `heap_usage_mb`（1 秒おきの標本。立ち上がりの山と落ち着いた後）:

| 撮影 | 山 | 落ち着いた後 |
|---|---|---|
| 1 フレーム・変更前（MEASURE-FIF `stress-fif1`） | 12224 | 8453 |
| 2 フレーム・変更前（`stress-try1`・`try2`） | 止まった（2043 の後の標本なし） | — |
| 2 フレーム・保持だけ直した（`try/a1`、pass） | 15826 | 10745〜10751 |
| **2 フレーム・変更後（verify の撮影）** | **13778** | **8698〜8699** |
| **1 フレーム・変更後（`try/fif1`）** | **10061** | **8453** |

- 検証（`.harness/runs/20261014-005646/`）:
  - `baseline-build-STRESS-VRAM.txt`: 変更前の RelWithDebInfo の Game のビルド EXIT=0。
  - `verify-SCN-ECS-FIX-STRESS-VRAM-FIF2-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0（警告は libwebsockets の設定の MSB8065 だけ）。
  - `verify-SCN-ECS-FIX-STRESS-VRAM-FIF2-2.txt`: mega-stress 5 万個 close の撮影 `CAPTURE_SCENE result=pass`、EXIT=0。Game.log は `FRAMES_IN_FLIGHT count=2`・`[ERROR]` 0・`unhandled exception` 0・`FRAME_RESOURCE_STALE` 0・`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0`、`visbuffer_records` は 256 → 512 → 1024 → 2048 の 1 系列だけ。PNG は MEASURE-FIF の 1 フレームの撮影（`stress-fif1`）と見た目が同じ（mean_luminance 121.495 / 120.461）。
  - `verify-SCN-ECS-FIX-STRESS-VRAM-FIF2-3.txt`: `FRAMES_IN_FLIGHT count=2` かつ `unhandled exception` なしの照合 EXIT=0。
  - `check-SCN-ECS-FIX-STRESS-VRAM-FIF2-fif1.txt`: 変更後の `--frames-in-flight=1` の撮影（pass、ERROR 0）と保持だけ直した 2 フレームの撮影の `VRAM_BUDGET`。
- Next: SCN-ECS-FIX-SUN-DISK-UPLOAD → SCN-ECS-MEASURE-FIF の撮り直し。
- Notes: 2 フレームの立ち上がりの山（13.8 GB）は 1 フレーム（10.1 GB）より高い。伸ばした古い物は「手放したフレームの次のフレームの完了」まで残り、2 フレームではその完了が 1 フレーム遅いので、立ち上がりの数フレームの古い物が重なる。さらに下げるなら、大きな確保の前に遅延破棄の列が大きいとき前の提出の完了を待って先に破棄する（RHI。提出のロックの下で CreateBuffer を呼ぶ所があると待ちが自分を止めるので、ロックに頼らない待ち方が要る）か、列の大きさを読み込みの済んでいないメッシュも含めて先に見積もる形があるが、今回は入れていない。山の値は 1 秒おきの標本なので、本当の山はこれより高いことがある。記録の表を共有にした後の `VisibilityResolveVulkanTest` などの ctest は走らせていない（verify の名指しなし）。`MegaGeometryPass.cpp`（LF）・`VisibilityRasterPass.cpp`・`VisibilityRasterPass.h` は行末を保った（numstat の 2 通りが一致）。

### SCN-ECS-FIX-SUN-DISK-UPLOAD（2026-10-14、反復 9・run 20261014-005646）

- 直したこと（`SkyAtmospherePass`）:
  - 太陽の円盤の 1×1 の画素を、毎フレーム `VulkanTexture::Update`（同期の単発の提出）で書くのをやめ、Declare でフレームの持ち主から借りた領域（`AllocateUploadData`、Storage）へ書き、Execute でこのフレームの記録の中へコピーを積む形にした。グラフには円盤を `Write(CopyDest → 最後の状態 ShaderResource)` で宣言し、前のフレームの読み（fragment・compute・ray tracing）との順序はグラフが入れるバリアが守る。パスの中でコピーの後に `CopyDest → ShaderResource` へ戻す。
  - 借りたページから texture へ直接写すと、Vulkan の `CopyBufferToTexture` が元のバッファの参照の範囲を切れ端の始まりからページの終わりまで取るので、同じフレームで後のパスが同じページの後ろの切れ端へ書くのが `FRAME_RESOURCE_UNPROTECTED kind=buffer_recorded_same_frame` として 8 組出た（最初の試し）。借りた切れ端から GPU だけが使う 8 B の中継のバッファ（`SkyAtmosphere.SunDiskStaging`）へ大きさどおりに `CopyBuffer` してから texture へ写す形にして、0 組に戻した。中継のバッファは前のフレームのコピーの読みの後に書く（`CopySource → CopyDest`、書いた後に `CopyDest → CopySource`）。
  - 作った直後の texture（layout が未定義）と、フレームの持ち主の無い文脈（検査）だけは従来どおり同期で 1 回書く。
- 検証（`.harness/runs/20261014-005646/`）:
  - `baseline-build-SUN-DISK.txt`: 変更前の RelWithDebInfo の Game のビルド EXIT=0。
  - `verify-SCN-ECS-FIX-SUN-DISK-UPLOAD-1〜3.txt`: 最初の試し（中継のバッファなし）。ビルド・比較・照合はすべて EXIT=0 だが、ログに `FRAME_RESOURCE_UNPROTECTED_SUMMARY count=8` が出たので直した。
  - `verify-SCN-ECS-FIX-SUN-DISK-UPLOAD-4.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-ECS-FIX-SUN-DISK-UPLOAD-5.txt`: 決定論の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0.0021・psnr_db=60.066・within_limits=True、result=pass、EXIT=0（反復 7 と同じ値）。PNG は通常の起動画面。
  - `verify-SCN-ECS-FIX-SUN-DISK-UPLOAD-6.txt`: `IMMEDIATE_SUBMIT` の照合 EXIT=0。`SkyAtmospherePass::GenerateSunDiskTexture` は `count=1 frames=1 first_frame=0 last_frame=0`（変更前は毎フレーム）。19 の呼び出し元のうち 18 が `frames=1`、BLAS の構築だけ `frames=4`。撮影のログは `FRAMES_IN_FLIGHT count=2`・`[ERROR]` 0・`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0 frames=181`・`FRAME_RESOURCE_STALE` 0・`unhandled exception` 0。
- Next: SCN-ECS-MEASURE-FIF の撮り直し（`RHI.ImmediateSubmitWait` が窓から消えたかと、2 フレームの重なりの効果はそこで見る。この項目では性能を測っていない）。
- Notes: 根本は RHI の `CopyBufferToTexture`・`CopyBufferToTextureRegion` が元のバッファの参照の範囲を `~0ull`（ページの終わりまで）で記録すること。直すなら画素の大きさから範囲を出す形だが RHI は今回の paths の外なので、中継のバッファで避けた。同じ形で借りた領域から texture へ写すパスを足すときは同じ偽の検出が出る。`SkyAtmospherePass.cpp`・`SkyAtmospherePass.h` は CRLF を保った（numstat の 2 通りが一致）。PathTracing の ctest（`PathTracingVulkanTest` など、円盤を読む）は verify の名指しがないので走らせていない。

### SCN-ECS-MEASURE-FIF（2026-10-14、反復 10・run 20261014-005646、撮り直し）

- 分け方は反復 6 と同じ（RenderThread のトレースの区間を `RT.WaitPacket` の始まりで描画フレームに切り、窓は metrics.json の 300 フレーム。各列はフレームごとの値の median・p95）。集計の出力は `check-SCN-ECS-MEASURE-FIF-stress-split-r10.txt`・`check-SCN-ECS-MEASURE-FIF-bananas-split-r10.txt`。どちらも `FRAMES_IN_FLIGHT count=2`。

| 列（ms） | mega-stress 5 万個 close median | p95 | バナナ 1 万本 overview median | p95 |
|---|---|---|---|---|
| GPU のフレーム（metrics.json） | 37.226 | 44.204 | 11.817 | 14.930 |
| RenderThread の 1 フレーム | 37.310 | 45.530 | 12.078 | 16.034 |
| CPU の作業 | 5.927 | 11.966 | 5.384 | 7.998 |
| GPU に依存する待ち（計） | 31.035 | 37.416 | 6.355 | 10.003 |
| └ `RT.WaitFrameFence` | 30.961 | 37.348 | 6.275 | 9.951 |
| └ `RHI.ImmediateSubmitWait` | 0（窓で 0 回） | 0 | 0（窓で 0 回） | 0 |
| └ `RT.AcquireImage` | 0.010 | 0.015 | 0.010 | 0.015 |
| └ `RT.Present` | 0.063 | 0.103 | 0.056 | 0.099 |
| パケット待ち | 0.000 | 0.000 | 0.000 | 0.000 |

- 答え: 残っていない。`RT.WaitFrameFence` 以外の GPU に依存する待ちは、`RHI.ImmediateSubmitWait` が窓の 300 フレームで 0 回（RenderThread 以外のスレッドでも 0 回）、`RT.AcquireImage`・`RT.Present` は p95 0.1 ms 前後で、足す `SCN-ECS-FIX-*` は無い。反復 6 のバナナで毎フレーム 8.1 ms 出ていた `GenerateSunDiskTexture` の待ちは消えた（`IMMEDIATE_SUBMIT caller=SkyAtmospherePass::GenerateSunDiskTexture count=1 frames=1 first_frame=0`）。
- 重なり: 両シーンとも RenderThread の 1 フレームは GPU のフレームとほぼ同じで（37.3 / 37.2、12.1 / 11.8）、CPU の作業と GPU の時間の和（43 / 17）より短い。CPU の記録は GPU の処理と重なり、残りの待ちは GPU が律速のときの `RT.WaitFrameFence` だけ。バナナの RenderThread の 1 フレームは反復 6 の 12.853 → 12.078 ms、GPU に依存する待ちのうちフェンス以外は 8.2 ms → 0.07 ms。
- 窓の外の同期の提出（参考）: どの呼び出し元も初期化のフレームか立ち上がりだけ。mega-stress の `RayTracingSceneSubsystem::BuildAccelerationStructures` は描画フレーム 0〜10 に 15 回・待ちの最大 3673 ms（5 万個の BLAS/TLAS の初回の構築）、`CreateAndUploadCookedTexture` は main スレッドのフレーム 0〜1 に 74〜75 回。
- 検証（`.harness/runs/20261014-005646/`）:
  - `verify-SCN-ECS-MEASURE-FIF-5.txt`: mega-stress 5 万個 close の撮影（ビルド込み）`CAPTURE_SCENE result=pass`、EXIT=0。Game.log は `[ERROR]` 0・`unhandled exception` 0・`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0`。
  - `verify-SCN-ECS-MEASURE-FIF-6.txt`: `CHECK_CAPTURE_METRICS result=pass views=close`、EXIT=0。
  - `verify-SCN-ECS-MEASURE-FIF-7.txt`: バナナ 1 万本 overview の撮影（ビルド込み）`CAPTURE_SCENE result=pass`、EXIT=0。Game.log は `[ERROR]` 0・`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0`。
  - `verify-SCN-ECS-MEASURE-FIF-8.txt`: `CHECK_CAPTURE_METRICS result=pass views=overview`、EXIT=0。
- Next: TASKS.md の未完の次の項目（SCN-ECS-FIF-SYNC-SUBMIT-SCOPE の待っていた答えは「直す呼び出し元は無い」）。
- Notes: 記録の時間の上位（窓のフレームごとの合計の median。並列の記録の項目の材料）: mega-stress は `RG.Record.VisibilityRasterPass` 1.003・`MegaGeometryPass` 0.533・`ShadowMapPass` 0.406・`VirtualShadowMapPass` 0.221 ms、バナナは `VisibilityRasterPass` 0.587・`LightingPass` 0.460・`VirtualShadowMapPass` 0.268・`MegaGeometryPass` 0.147 ms。反復 6 のバナナの撮影は `.harness/runs/scene-capture/SCN-ECS-MEASURE-FIF/bananas-iter6` へ移した。集計のスクリプトは作業用の一時のもので、リポジトリには入れていない。フレームの時間の合否はここでは決めていない（SCN-MEGA-ACCEPT-1M）。

### SCN-ECS-DIAG-GPU-FIF2（2026-10-14、反復 11・run 20261014-005646、診断）

- 測り方: HEAD（dcad63b8。4fb8c816 とコードは同じ）の RelWithDebInfo を 1 回ビルドし、mega-stress 5 万個 close・1920×1080・`-GpuTimingFrames 300` を `--frames-in-flight=1`・2・1・2 の順に続けて 4 本撮った（`.harness/runs/scene-capture/SCN-ECS-DIAG-GPU-FIF2/fif<1|2>-<a|b>`）。各撮影の間 `nvidia-smi --query-gpu=timestamp,clocks.sm,clocks.mem,power.draw,temperature.gpu,clocks_throttle_reasons.active --format=csv -lms 500` を `<撮影>.nvsmi.csv` に記録し、窓（metrics.json の `window_first_frame`〜`window_last_frame` のトレースの時刻の範囲）に入った標本だけを集計した。GPU は RTX 4080（ドライバ 610.88、電力の上限 320 W）。集計の出力は `check-SCN-ECS-DIAG-GPU-FIF2-clock.txt`。パスの値は metrics.json の `gpu_pass_ms` の median（ms。`MegaGeometry` は `VisibilityRasterPass` と同じ値で、Cull・Draw などを含む）。

| 撮影（順に撮った） | 重ね | GPU のフレーム median | p95 | RenderThread の 1 フレーム median | `MegaGeometry` | `MegaGeometryCull1` | `MegaGeometryCull2` | `MegaGeometryDraw1` | `VisRasterSw1` | `LightingPass` | 窓の間の SM のクロック median（範囲） | 電力 median | 温度 max | throttle の理由 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `fif1-a` | 1 | 33.580 | 35.045 | 38.687 | 29.584 | 10.577 | 8.896 | 4.255 | 4.085 | 1.965 | 2760 MHz（2760〜2760、24 標本） | 145.9 W | 54 ℃ | 0x0（無し）24/24 |
| `fif2-a` | 2 | 33.328 | 35.262 | 33.288 | 29.797 | 11.249 | 9.418 | 4.291 | 3.872 | 1.683 | 2745 MHz（2745〜2760、20 標本） | 156.2 W | 56 ℃ | 0x0（無し）20/20 |
| `fif1-b` | 1 | 33.620 | 34.909 | 38.624 | 29.742 | 10.613 | 9.539 | 4.385 | 4.160 | 1.886 | 2745 MHz（2745〜2745、23 標本） | 148.1 W | 56 ℃ | 0x0（無し）23/23 |
| `fif2-b` | 2 | 34.395 | 36.404 | 34.362 | 30.577 | 11.009 | 9.883 | 4.500 | 4.192 | 1.719 | 2745 MHz（2745〜2745、22 標本） | 157.4 W | 58 ℃ | 0x0（無し）22/22 |

- 判断: 受け入れて記録する（`SCN-ECS-FIX-*` は足さない）。同じビルドで交互に撮ると、2 フレームの GPU のフレームの median は 1 フレームと 33.3〜34.4 ms 対 33.6 ms で、平均の差は +0.26 ms（+0.8%）、組の中の差は -0.25・+0.78 ms で、2 フレームの 2 本の間の揺れ（1.07 ms）より小さい。SM のクロックは 4 本とも 2745〜2760 MHz、メモリのクロックは 11201 MHz で同じ、throttle の理由は窓の全標本で 0x0（電力・温度の上限は効いていない）。2 フレームは GPU が休まないぶん電力が約 10 W 高い（146〜148 → 156〜157 W）が、上限の半分に届かない。
- SCN-ECS-MEASURE-FIF の 40.0 ms（`stress`、03:14）と 34.5 ms（`stress-fif1`、02:24）の差は、同じコードの 2 フレームが今回 33.3〜34.4 ms なので、2 フレームの重ねの費用ではない。そのときのクロックは記録が無く直接は確かめられないが、全パスが一様に遅い形と、同じコードで再現しないことから、撮影の時点の GPU の状態（クロック・ほかの GPU の負荷）によるものと見る。VRAM は溢れていない（`VRAM_BUDGET` の最後の行は `stress` 8731 MB／予算 12194 MB、今回の 2 フレーム 8703〜8705 MB／12218〜12227 MB、1 フレーム 8453 MB）。
- 2 フレームの重ねの効果: RenderThread の 1 フレームの median が 38.6 → 33.3〜34.4 ms（-4.3〜-5.3 ms）。GPU の時間を増やさずに CPU の記録が GPU と重なっている。
- 検証（`.harness/runs/20261014-005646/`）:
  - `verify-SCN-ECS-DIAG-GPU-FIF2-1.txt`: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`、EXIT=0。
  - `verify-SCN-ECS-DIAG-GPU-FIF2-2.txt`〜`-5.txt`: 撮影 `fif1-a`・`fif2-a`・`fif1-b`・`fif2-b` の `CAPTURE_SCENE result=pass`、EXIT=0。Game.log は 4 本とも `FRAMES_IN_FLIGHT count=` が指定どおり、`[ERROR]` 0・`unhandled exception` 0・`FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0`。
  - `verify-SCN-ECS-DIAG-GPU-FIF2-6.txt`: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-ECS-DIAG-GPU-FIF2/fif2-a --timing --min-frames 300` → `CHECK_CAPTURE_METRICS result=pass views=close`、EXIT=0。
- Next: TASKS.md の未完の次の項目（60 fps の判定は SCN-MEGA-ACCEPT-1M の実際の条件で行う）。
- Notes: 2 フレームでは `MegaGeometryCull1`＋`Cull2` が両組とも +0.7〜+1.2 ms（+4〜6%）で、`LightingPass` が -0.2〜-0.3 ms 速い。フレームの合計では揺れの中なので項目は足していないが、カリングを詰めるときの候補として残す。撮影は `-Build` を付けず、最初に 1 回ビルドしてから 4 本を同じ exe で撮った。集計のスクリプトは作業用の一時のもので、リポジトリには入れていない。

### SCN-ECS-RT-PARALLEL-WIDEN（2026-10-14、反復 1・run 20261014-033121）

- 共通の仕組み（RenderGraph・SceneRenderer）:
  - 並列の記録を既定 on にした（`NORVES_RENDER_PARALLEL_RECORD` が未設定・1 で on、0 のときだけ 1 本。`--render-parallel-record=off` で戻せる）。
  - 続く Parallel のパスが 1 つだけの並びは、リストを分けず今のリストへ直列で記録する。
  - 前のパスの記録の結果（CPU の状態）を読むパスは `IRenderGraphPass::ReadsRecordingOf(earlierPass)` を返し、RenderGraph はその前で同時に記録する並びを切る（前の並びを記録し終えてから準備段と記録を始める。GPU の依存は従来どおりグラフのバリアと提出の順）。
  - Execute が写しの context へ公開した値を後のパスへ戻す `FinishParallelRecord`（並びの後に RenderThread でグラフの順に呼ぶ）を足した。
  - 区間の保留のコマンド（影・GBuffer の描画など）を、ほかの区間と同時に流せる並びならワーカーで区間のリストへ流す。SceneRenderer の直前のパイプラインと統計を区間ごとの `SceneRendererRecordState` にし、統計は並びの後に足し込む。MegaGeometry の記録とスキンの描画を含む並びは従来どおり RenderThread で流す。
  - パスだけが持つ Texture の遷移の補助 `TransitionPassOwnedTexture`（状態を明示したバリアの後に共有の layout も書く。layout を宣言から決める区間のリストは共有の layout を書かないため）。
  - `IRenderGraphPass` の Parallel の約束を書き直した（バッファのバリアは可、Execute で禁止は単発の提出を伴う転送・context への公開・同時に書く共有の CPU の状態。資源とパイプラインの作成はデバイスが排他するので可）。
- 監査の表（記録の時間の上位は MEASURE-FIF の表。変更後の方針）:

| パス | 並列にできなかった理由 | 外し方 | 今の方針 |
|---|---|---|---|
| VisibilityRasterPass | Execute の中で MegaGeometry の記録（2 パスの遮蔽・HZB）を進め、材質の定数の 1x1 テクスチャを転送つきで作る。HZB のピラミッドを自分で遷移。後のパス（分類・解決・VSM）が Execute で作る表を読む | 定数のテクスチャは MegaGeometryPass の準備段で作る。ピラミッドは `TransitionPassOwnedTexture`（深度は ShaderResource のまま）。後のパスは `ReadsRecordingOf` で並びを分ける。初期化と framebuffer は準備段 | Parallel |
| MegaGeometryPass | Execute で表の準備（PrepareSceneTables）と記録をラスタへ移すかを決め、ラスタがそれを読む。移さないフレームは中で自分でバリアを張る記録を積む | 添付の解決・Setup・表の準備・移すかの判定を準備段（RenderThread）へ。Execute は添付の状態遷移を積むだけ | 記録をラスタへ移すフレームだけ Parallel |
| ShadowMapPass | Execute で PhysicalLighting（カスケード・クリップマップ・方向光の影）を公開。描画は保留のコマンドで RenderThread に直列で流れていた。スキンのパレットの記録は GBuffer・スキニングと共有 | 行列と公開を `PlanShadowFrame` に分け、準備段で公開。保留のコマンドはワーカーで流す。スキンの影の描画があるフレームは直列 | Parallel（スキンの影が無いフレーム） |
| GBufferPass | 材質の灰色テクスチャを転送つきで作る。保留のコマンドの直列の流し。スキンのパレット | 灰色テクスチャを準備段で作り、準備段を経たフレームの Execute は作らない。保留のコマンドはワーカーで流す。スキンの描画があるフレームは直列 | Parallel（スキンの描画が無いフレーム） |
| VirtualShadowMapPass | 点光源の並びを Execute で公開。MegaGeometry の影の入力・無効化と、スキニングの instance を読む | 公開を準備段へ。MegaGeometryPass・VisibilityRasterPass・SkinningComputePass の後で並びを分ける。パスの中のバリアはバッファだけ（layout を持たない） | Parallel |
| LightingPass | Execute で PhysicalLighting（照明・DDGI・RTGI・光線追跡の影など）を公開。空の IBL・raw252 の検証の像・DDGI の既定のアトラスを転送つきで作る。RTGI・DDGI・光線追跡の影の Texture を自分で遷移。RTGI の出力が dispatch の前に断る道で宣言の最終の状態（ShaderResource）にならない | 公開は `FinishParallelRecord` で戻す。転送つきの作成を準備段へ。パス専有の Texture は `TransitionPassOwnedTexture`。RTGI の出力は断る道でも ShaderResource へ移す | Parallel（GBuffer をグラフから読み、旧経路の登録をしないとき） |
| SkyAtmospherePass | Execute で SkyAtmosphere を公開 | 準備段で同じ値を公開 | Parallel |
| MaterialTileClassifyPass | ラスタの表を読み、解決がその結果を読む | ― | Serial のまま（前後のどちらとも同時に記録できない） |

- 起動画面の並列の並び（Debug・on）: `RG_PARALLEL_RECORD_PASSES passes=ShadowMapPass,GBufferPass,MegaGeometryPass,VisibilityRasterPass,VisibilityResolvePass,SSAOPass,SkyAtmospherePass,VirtualShadowMapPass,LightingPass,ToneMappingPass,VignettePass`、区間ごとのリストは 1 フレーム 10 本。
- 検証（`.harness/runs/20261014-033121/`）:
  - `verify-SCN-ECS-RT-PARALLEL-WIDEN-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-ECS-RT-PARALLEL-WIDEN-2.txt`: Debug の起動画面（on）`CAPTURE_STARTUP_SCENE result=pass`、EXIT=0。`vulkan_validation` は error_count=0・warning_count=0・vuid_count=0（`default.Validation.log` は 0 行。Threading の UNASSIGNED も数える）。Game.log は `RENDER_PARALLEL_RECORD enabled=1`、`[ERROR]` 0。画像は天球・地面・球・岩・家が以前どおり。
  - `verify-SCN-ECS-RT-PARALLEL-WIDEN-3.txt`: off の決定論の撮影を SCN-MEGA-BASE-RWDI と比較、psnr_db=60.066・within_limits=True、result=pass、EXIT=0（`enabled=0`）。
  - `verify-SCN-ECS-RT-PARALLEL-WIDEN-4.txt`: 既定（on）の決定論の撮影を同じ基準と比較、psnr_db=60.066・within_limits=True、result=pass、EXIT=0（`enabled=1`・リスト 10 本）。
- 補助（done-when の外）: Debug のバナナ 1 万本 overview（on）を撮った（`.harness/runs/scene-capture/SCN-ECS-RT-PARALLEL-WIDEN/bananas-debug-on`）。result=pass、検証レイヤーのログ 0 バイト、`[ERROR]` 0、ラスタ・MegaGeometry・VSM・Lighting を含む並びでリスト 11 本。
- Next: TASKS.md の未完の次の項目。
- Notes: 基準との psnr 60.066 は on と off で同じで、SCN-ECS-FIF-SWITCH の撮影（60.068）と同じ程度の、この変更より前からある差。on と off の性能の比べはしていない（SCN-MEGA-ACCEPT-1M）。スキンの描画があるフレームの ShadowMap・GBuffer は直列で、スキンの描画を同時に記録するには共有のパレットの記録（`SkinnedMeshGpuStore::PrepareDraw`・`MarkLastUse`）を準備段へ寄せる必要がある。NeuralMaterialDecode・SkinningCompute・SSR・Volumetrics・Forward などは Serial のまま（記録の時間の上位ではない）。準備段を経ずに記録するパス（1 本の記録・並びが 1 つ）の失敗の道（影・GBuffer の資源が揃わないフレーム）は、記録が無いまま宣言の最終の layout が書き戻される。

### SCN-MEGA-BVH-LEAF-SPLIT（2026-10-14、反復 2・run 20261014-033121）
- Done: BVH を持たずに渡されたメッシュ（v1.0・手続き・実行時に作るメッシュ）に、`MegaGeometryResourceStore::CreateMegaMesh` が葉 8 クラスタ以下のグループの BVH を作る。構築は新しいヘッダ `Rendering/MegaGeometry/MegaGeometryBvhBuilder.h`（`BuildClusterBVH`・`ValidateClusterBVH`）。LOD の段ごとに分け、段の中を中心の広がりが最も大きい軸で再帰的に二分して葉を作り（左に 8 の倍数を渡すので葉は最後の 1 つを除き 8 個で埋まる）、隣り合う節を 8 個ずつ束ねて上の段を組み、幅優先に並べる。葉は並べ替えの表（`ClusterOrder`）の連続した範囲で、クラスタの記録は並べ替えない（今の描画は不変）。結果はエントリの `BuiltBVH` に持つだけで GPU へは書かない。開発ビルドでは検査（`AnalyzeGroupBVH` の構造・表が全クラスタを 1 回ずつ持つ・葉の球が下のクラスタの球と親の球を、内部の節の球が子の球を包む・親の誤差の最大が下以上）を行い、`MEGA_BVH_BUILD mesh=… built=… nodes=… leaves=… levels=… violations=… build_ms=…` を出す。
- 検証（`.harness/runs/20261014-033121/`）:
  - `verify-SCN-MEGA-BVH-LEAF-SPLIT-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-MEGA-BVH-LEAF-SPLIT-2.txt`: 決定論の起動画面の撮影を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0.002・psnr_db=60.068・mismatched_pixel_ratio=0.148・within_limits=True、result=pass、EXIT=0。PNG は天球・地面・球・岩・家と影が以前どおり。Game.log の `[ERROR]` 0。
- 補助（verify の外）: 起動画面の 6 メッシュは全て焼き込み済みの BVH（v1.1）を持つので、上の撮影では構築が走らない（`MEGA_BVH_BUILD` 0 行）。大球を実行時に作る `-GameArgs rendering3dtest-big-sphere-source=runtime` で 1 本撮り（`aux-SCN-MEGA-BVH-LEAF-SPLIT-runtime-sphere.txt`、`.harness/runs/startup-capture/SCN-MEGA-BVH-LEAF-SPLIT-runtime-sphere`）、`MEGA_BVH_BUILD mesh="BigCobbleSphere" built=1 clusters=10912 nodes=1561 leaves=1364 levels=5 violations=0 build_ms=2.383`、`[ERROR]` 0、result=pass。
- Next: SCN-MEGA-FLAT-TO-BVH（`BuiltBVH` の `ClusterOrder` でクラスタを並べ直して GPU へ書き、BVH の経路へ流す。実行時の LOD 階層のクラスタの `ParentStart` は並べ替えに合わせて付け替えが要る）。
- Notes: 葉は焼き込みの v1.1 と違い、1 つのグループのメンバに限らない（LOD の段だけで分ける）。実行時の階層・v0 のクラスタは親の誤差が既定の最大値なので、節の LOD の判定では枝を切らない（保守的）。親の球は親のグループを持つクラスタ（`GroupId` が有効）だけ包む。試験の追加はしていない（ValidateClusterBVH の検査を開発ビルドの実行時に回す形）。

### SCN-MEGA-FLAT-TO-BVH（2026-10-14、反復 3・run 20261014-033121）
- Done: BVH を持たずに渡されたメッシュに読み込み時に作った BVH（BVH-LEAF-SPLIT）を、`MegaGeometryResourceStore::CreateMegaMesh` が区画の末尾へ書く（節の配列の直後に並べ替えの表 `ClusterOrder` を uint32 で続け、`GroupBVHBufferBytes` に含める）。クラスタの記録は並べ替えない。GPU へ書く葉には新しい印 `GPU_GROUP_BVH_NODE_FLAG_CLUSTER_ORDER`（2）を付け、`cluster_bvh_cull.comp` の葉のクラスタの判定は、印のある葉で `bvhInfo.xy + bvhInfo.z × 32` の表からクラスタの番号を引く（焼き込みの v1.1 の葉は従来どおり連続した範囲）。これで BVH を作れたメッシュはすべて `CanTraverseSceneBvh` を満たし、全インスタンスが BVH の経路を通る。開発ビルドの検査（`ValidateClusterBVH`）が違反を出した BVH は書かず、そのメッシュだけ平らな経路に残る。エントリに持っていた `BuiltBVH` は使い道が無くなったので消した。
- `--mega-flat-path`: `MegaGeometryPass` の構築時にプロセスのコマンドラインを見て（`--vsm-cache` と同じ読み方）、`bUseGroupBVH=false` で全インスタンスを平らな経路（`cluster_cull.comp`・FirstGroup の接頭和）へ戻す。環境変数 `NORVES_MEGA_BVH=0` も従来どおり。
- 検証（`.harness/runs/20261014-033121/`）:
  - `verify-SCN-MEGA-FLAT-TO-BVH-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-MEGA-FLAT-TO-BVH-2.txt`: 決定論の起動画面を SCN-MEGA-BASE-RWDI と比較、mean_luminance_difference=0.002・psnr_db=60.073・within_limits=True、result=pass、EXIT=0。`MEGA_BVH bvh_instances=6 flat_instances=0`、`[ERROR]` 0。PNG は天球・地面・球・岩・家と影が以前どおり。
  - `verify-SCN-MEGA-FLAT-TO-BVH-3.txt`: 旋回（30°/s、f60・f75・f90）を SCN-MEGA-ORBIT-OFF と比較、psnr_db=60.879・60.443・59.291、輝度の差は最大 0.0388、within_limits=True、result=pass、EXIT=0。
- 補助（verify の外）: 起動画面の 6 メッシュは焼き込みの BVH を持つので、新しい表を引く道を通すために大球を実行時に作る起動（`-GameArgs rendering3dtest-big-sphere-source=runtime`）で撮り比べた。`--mega-flat-path` 付き（`aux-SCN-MEGA-FLAT-TO-BVH-runtime-flat.txt`）は `bvh_instances=0 flat_instances=6`、既定（`aux-SCN-MEGA-FLAT-TO-BVH-runtime-bvh.txt`）は `bvh_instances=6 flat_instances=0`。どちらも `MEGA_OCCLUSION pass1=2280 pass2_tested=2287 pass2_drawn=0 occluded=7` で同じクラスタを描き、画像の比較は psnr_db=100・max_channel_difference=1、`[ERROR]` 0。
- Next: SCN-MEGA-BVH-INDIRECT。
- Notes: paths に MegaGeometryResourceStore.cpp/.h と MegaGeometryTypes.h を足した（BVH はストアの中にあり、GPU へ書くのはストアの区画の組み立て）。実行時の階層のクラスタは親の誤差が既定の最大値なので節の LOD では枝を切らず、節の判定は視錐台と遮蔽だけが効く（葉の判定は平らな経路と同じ `ProcessCluster`）。影（CSM・VSM）の判定は今までどおり BVH をたどらない。BVH の列は葉の数の分だけ大きくなる（実行時の大球を含む起動画面で leaf_capacity 12,858・queue 28,236。容量の固定は BVH-INDIRECT）。試験の追加はしていない。

### SCN-MEGA-BVH-INDIRECT（2026-10-14、反復 4・run 20261014-033121）
- Done: BVH のたどりの列を固定容量にし、段 1 以降と葉の段を GPU が書いた引数で間接 dispatch する（`ICommandList::DispatchIndirect`）。
  - 列（`MegaGeometry_BvhQueue`、96 MB）は節の列 2 本（各 4,194,304 要素。段 s は列 s & 1 を読み、次の段の列 (s + 1) & 1 へ書く）と葉の列（4,194,304 要素）。本数・メッシュによらず一定で、BVH のインスタンスが初めて現れた記録で 1 回だけ作る。
  - `cluster_bvh_cull.comp` は積む前に容量を確かめ、超えた要素は積まずに統計 12（`bvh_overflow`）へ数える。カウンタは積もうとした数で、読む側は min(数, 容量)。間接 dispatch の引数は、ワークグループの先頭の要素を積んだスレッドが min(数, 容量) から atomicMax で書く（カウンタのバッファの語 32 以降。段 s は 32 + 4s、葉は 32 + 64）。
  - 2 パス目の根の入力を別のバッファ（`MegaGeometry_BvhPass2Roots`、binding 14、4 MB）に持つ。1 パス目の前に頭だけを 0 にし（各パスの頭で 0 にするカウンタとは別）、1 パス目の段 0 が視錐台と LOD を通った根を積み、2 パス目の段 0 はそれを入力と間接 dispatch の引数として読む。容量（1,048,576）を超えたら 2 パス目は全部の根を判定し直す（統計 13 `bvh_pass2_fallback`）。
  - 列を作れなかったら（VRAM の不足の例外を受ける）、そのフレームの描画を見送り `MEGA_BVH_QUEUE_ALLOC_FAILED` を ERROR で 1 回出す。
  - `MEGA_CAPACITY` に `bvh_pass2_bytes`・`bvh_overflow`・`bvh_pass2_fallback`・`bvh_node_peak`・`bvh_leaf_peak`（積もうとした数の最大）を足した。`MEGA_BVH` の `leaf_capacity`・`queue_entries` は `scene_leaves`（全インスタンスの葉の合計）と固定の容量に置き換えた。定数バッファの予約 3 語を容量 3 つに使った。
- 検証（`.harness/runs/20261014-033121/`）:
  - `verify-SCN-MEGA-BVH-INDIRECT-1.txt`: RelWithDebInfo の Game のビルド EXIT=0。
  - `verify-SCN-MEGA-BVH-INDIRECT-2.txt`: バナナ 1 万本 overview を SCN-MEGA-BASE と比較、mean_luminance_difference=0.0001・psnr_db=70.53・within_limits=True、result=pass、EXIT=0。Game.log の `[ERROR]` 0、`MEGA_CAPACITY bvh_queue_bytes=100663296 bvh_overflow=0 bvh_pass2_fallback=0`（全行）、`MEGA_OCCLUSION pass1=364821 pass2_tested=367654 occluded=2833` は基準と同じ。PNG はバナナの散布と 3 つの山が以前どおり。
- 補助（verify の外、notes の確認）:
  - mega-stress 5 万個（`aux-SCN-MEGA-BVH-INDIRECT-stress50k.txt`、3 視点）: 落ちずに result=pass、`[ERROR]` 0、`bvh_queue_bytes=100663296`（1 万本と同じ）・`bvh_overflow=0`。`bvh_node_peak` は最大 3,623,747（aerial）、`bvh_leaf_peak` は最大 1,192,897。VRAM の使用は約 6.7 GB（以前は 14,975 MB で落ちた）。PNG に欠けは無い。
  - バナナ 10 万本 + mega-stress 5000 個の混在（`aux-SCN-MEGA-BVH-INDIRECT-mixed100k.txt`）: 落ちずに result=pass、`[ERROR]` 0、`bvh_overflow=0`、`bvh_node_peak=1538951`。
- Next: SCN-MEGA-INSTANCE-CULL。
- Notes: 容量の配分は測って決めた。最初の配分（節の列 2,097,152・葉の列 8,388,608）では 5 万個で節の列が溢れ（`bvh_overflow` 約 250 万、影だけ残って物が欠けた）、試しに容量を大きくした撮影（`.harness/runs/scene-capture/SCN-MEGA-BVH-INDIRECT/probe50k`）で節の列の 1 段の最大が約 349 万・葉の列が約 115 万と分かったので、総量 96 MB のまま節 4M・葉 4M に配った。aerial の節の列の使用は容量の 86%。mega-stress のメッシュの多くは節の LOD で枝を切らない（実行時の階層）ので、節の列の需要は視錐台の中の葉の数に比例する。100 万本・混在の容量の足りる・足りないは SCN-MEGA-ACCEPT-1M の `bvh_overflow`・`bvh_node_peak` で判断する。2 パス目の段 0 は 1 パス目の根の選別を引き継ぐので、2 パス目に判定する根は視錐台の中だけになった（性能は測っていない）。試験の追加はしていない。

### SCN-MEGA-INSTANCE-CULL（2026-10-14、反復 5・run 20261014-033121）
- Done: BVH の段 0 の前に、範囲とインスタンス（席）の専用の判定を置いた。
  - 範囲: BVH を持つインスタンスを描く順に 64 席ずつ区切った範囲の AABB を、常駐の表（`MegaGeometry_RangeBounds`、範囲ごとに vec4 × 2）に持つ。`UpdateResidentSceneTables` が、描く順の行の記録を送った範囲だけ（メッシュの表が変わったとき・表を作り直したときは全部、行が減ったときは末尾の範囲も）を記録し、`mega_scene_scatter.comp` の段 2（1 ワークグループ = 1 範囲）が段 1 の後に行の締まった球の AABB を合わせて書く。締まった球を持たない行のある範囲は印を付けて常に通す。
  - 範囲の段（`cluster_bvh_cull.comp`、`BVH_STAGE_RANGES`。1 パス目と 1 回の判定だけ）: 1 範囲 1 スレッドで、AABB を視錐台・距離で判定し、通った範囲を節の列 0 へ積み（カウンタ 0）、席の段の間接 dispatch の引数（1 範囲 = 1 ワークグループ）を書く。
  - 席の段（段 0）: 1 席 1 スレッドで、締まった球（メッシュの表に足した `boundsSphere` = `MegaMeshGPUData::TotalBounds` × 拡大率。無ければ根の節の球）で視錐台・距離・画素の大きさ・HZB（2 パス目だけ）を判定し、通った席だけ根の節の LOD を判定して、根の子を段 1 の列へ渡す（2 パス目の根の入力へも積む）。2 パス目は従来どおり 1 パス目が残した根の入力（溢れたら全部の席）を読む。段 1 以降の判定は変えていない。
  - 画素の大きさ: 球を包む AABB の 8 点を投影した矩形が画素の中心（k + 0.5）を 1 つも含まなければ落とす（余裕 1/32 画素）。描く範囲が描画先の全体のときだけ判定する。`NORVES_MEGA_INSTANCE_PIXEL_CULL=0` で切れる。
  - 距離: `MegaGeometryPassSettings::InstanceCullDistance`（既定 0 = 落とさない。`NORVES_MEGA_INSTANCE_CULL_DISTANCE` で替えられる）。
  - メッシュの表の行を 64 B → 80 B にした（`GPUMegaMesh::BoundsSphere`・`MegaMeshEntry::boundsSphere`）。カリングの定数に `BvhRangeCount`・`bBvhSeatsFromRanges`・`InstanceCullDistance`・`PixelCullWidth`・`PixelCullHeight` を足した。GPU 時間の名前に範囲の段 `MegaBvhC<パス>Range` を足した。`MEGA_BVH` に `instance_ranges` を足した（`dispatches_per_pass` は範囲の段を含む）。
- 検証（`.harness/runs/20261014-033121/`）:
  - `verify-SCN-MEGA-INSTANCE-CULL-3.txt`: 最終のコードの RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-MEGA-INSTANCE-CULL-4.txt`: 最終のコードのバナナ 1 万本 overview を SCN-MEGA-BASE と比較、mean_luminance_difference=0.0001・psnr_db=69.246・mismatched_pixel_ratio=0.000212・within_limits=True、result=pass、EXIT=0。Game.log の `[ERROR]` 0、`MEGA_BVH bvh_instances=10000 instance_ranges=157`、`bvh_overflow=0 bvh_pass2_fallback=0`、`MEGA_OCCLUSION pass1=364438 pass2_tested=367271 occluded=2833`（BVH-INDIRECT の 364,821・367,654 より少ない分は、根の節の球では視錐台にかかり締まった球では外れる席のクラスタ）。PNG はバナナの散布と 3 つの山が以前どおり。
  - `-1.txt`・`-2.txt` は画素の判定を切る環境変数を足す前の同じ検証（ビルド EXIT=0、psnr_db=65.552・pass）。
- 補助（verify の外、画素の大きさの判定が画像を変えないことの確認。`.harness/runs/scene-capture/SCN-MEGA-INSTANCE-CULL/`）:
  - 1920x1080 overview では画素の判定で落ちる席は無い（on/off で描いたクラスタ数が同じ）。同じ版を 2 回撮った差（地平線付近の 69 画素）が on/off の差（68 画素）と同じ程度で、基準との差はこの揺れ。
  - 192x108 horizon（`aux-...-horizon-tiny-*.txt`）では判定が働き、描いたクラスタが 78,641 → 78,347。off 同士の差は最大 2、on（2 回目・3 回目）と off の差は最大 5（画素数 11〜15）で揺れの程度。1 回目の on だけ 4 画素が最大 42 違ったが、同じ設定の 2・3 回目どうしは最大 2 で、1 回目の外れ値。余裕を 0.25 画素にすると何も落ちない（落ちる席は画素の中心の近くを通る細いものだけ）。
- Next: SCN-MEGA-BINNING-PREP。
- Notes: 範囲は描く順の連続した 64 行で、バナナの散布の席の順（Poisson disk の生成順。空間で並べていない）のままなので、範囲の AABB は広く、範囲の段で落ちる量は少ない見込み（1 万本 overview で 157 範囲）。範囲を空間で締めるには散布の席を空間の順（セル・Morton など）に並べる必要があり、Game の配置（`BananaLayout.cpp`）か席の割り当ての変更になる（このタスクの paths の外）。性能は測っていない（SCN-MEGA-ACCEPT-1M）。距離の判定は既定で切ってある（既定の見た目を変えないため）。HZB は今は 2 パス目の席の判定だけ（1 パス目は SCN-MEGA-TWOPASS-INSTANCES）。試験の追加はしていない。

### SCN-MEGA-INSTANCE-CULL（2026-10-14、反復 6・run 20261014-033121。評価の差し戻しへの対応）
- Done: 席と範囲の判定に使う締まった球が、実行時の LOD の簡略化した段の形を包むようにした。
  - 差し戻しの内容: 実行時の LOD の `TotalBounds` は最も細かい段のクラスタだけから作る（`LODHierarchyBuilder.cpp`）ので、簡略化で外へ動いた頂点を包まず、席の段が見える席を落としうる。
  - `mega_scene_scatter.comp` に段 3 を足した。メッシュの表を書き直したとき、段 1 の後・段 2 の前に、1 ワークグループ = 1 メッシュで全段のクラスタの球（クラスタ配列のデバイスアドレスから読む）を見て、球の中心はそのまま半径を max(|c_i − 中心| + r_i) × 1.0001 まで広げる（縮めない）。クラスタの球が有限でないメッシュは半径 0（締まった球が無い扱い。席は根の節の球、範囲は常に通す）。範囲の AABB（段 2）と席の段は、広げた同じ球を読む。
  - 球を GPU で直すのは、球を作る `LODHierarchyBuilder.cpp`・`MegaGeometryResourceStore.cpp` がこのタスクの paths の外で、クラスタの球は GPU の表にしか無いため。焼き込み済みの階層・cooked のメッシュの球も同じ段で全クラスタを包む値へ広がる。
  - 開発ビルドの常駐の表の検査（`MEGA_SCENE_GROW_CHECK`）は、メッシュの行の末尾の語（半径）を写しと比べないようにした（GPU が広げるため）。散在書き込みのディスクリプタセットを段ごと 3 → 4 にした。
- 検証（`.harness/runs/20261014-033121/`）:
  - `verify-SCN-MEGA-INSTANCE-CULL-8.txt`: 最終のコードの RelWithDebInfo の Game のビルド EXIT=0、error 0（`-5.txt` は同じ C++ でシェーダーを直す前のビルド、EXIT=0）。
  - `verify-SCN-MEGA-INSTANCE-CULL-7.txt`: バナナ 1 万本 overview を SCN-MEGA-BASE と比較、mean_luminance_difference=0・psnr_db=66.396・mismatched_pixel_ratio=0.000212・within_limits=True、result=pass、EXIT=0。`overview.Game.log` の `[ERROR]` 0、`bvh_overflow=0 bvh_pass2_fallback=0`、`MEGA_SCENE_GROW_CHECK mesh_rows=1 mismatch=0`、`MEGA_OCCLUSION pass1=364821 pass2_tested=367654`（前の反復の締まった球では 364,438。差の 383 クラスタは、LOD0 の球では外れていた席のクラスタで、球を広げて戻った。BVH-INDIRECT の値と同じ）。PNG はバナナの散布と 3 つの山が以前どおり。
  - `-6.txt` はシェーダーの `const` の付いた buffer_reference の変数でコンパイルに失敗した版（result=fail）。直した後の `-7.txt` が合格。
- Next: SCN-MEGA-BINNING-PREP。
- Notes: 影の表の球（`ResolveShadowBounds` が `TotalBounds` から作る）も実行時の LOD の段を包まない同じ根の問題を持つ（このタスクの前から。影の経路は paths の外）。根本は `LODHierarchyBuilder.cpp` で全段のクラスタを `TotalBounds` へ合わせることで、そうすれば段 3 は何も広げない。試験の追加はしていない。

### SCN-MEGA-BINNING-PREP（2026-10-14、反復 7・run 20261014-033121）
- Done: カリングが描くと決めたクラスタを、区間ごとのコマンドに加えて可視クラスタの全体の列へも積み、区画（パスの区間 × ハード/ソフト）の数えと接頭和を作る。描画は今までどおり区間ごとのコマンド。
  - 全体の列（`MegaGeometry_VisibleList`、binding 23、uvec4 × 4,194,304 = 64 MB）は全パス通しの固定容量。要素は (席, クラスタの番号, 区画, 区画の中の位置)。区画は `(sectionBase + 区間) × 2 + ソフトなら 1`。`EmitDrawCommand` が `AppendVisibleCluster` で、列の位置を先に取り、容量に入ったものだけ区画の数を数える（区画の数の和 = 列に入った要素の数）。区間の最大数（`MaxDrawCount`）によらず積む。ハード/ソフトは区間ごとのコマンドの経路と同じ画面上の大きさの判断（`IsSwRasterSmallCluster`）。
  - 数え（`MegaGeometry_BinCounters`、binding 24）: 頭 16 語（列へ積もうとした数・照合の間接 dispatch の引数・区画の数・区間の表の要素の数・ソフトの一覧の容量・照合の印）、区画の数、区画の先頭（接頭和）、旧の区間の接頭和。区間の数で大きさが変わり、足りないときだけ作り直す。フレームの頭で 0 にし、頭の語を書く（転送の書き込みが重ならないよう分けて埋める）。
  - 接頭和（`mega_bin_prefix.comp`、1 ワークグループ）を各パスの判定の後に走らせる。1 パス目の後は 2 パス目の区画が 0 なので、2 パス目の後に作り直しても 1 パス目の区画の先頭は変わらない（記録の番号を両パスを通じて保てる形）。
  - 新旧の照合（`mega_bin_check.comp`、開発ビルドで統計を読み戻す 2 パスのフレームの 2 パス目の後、間接 dispatch）: 新（全体の列）と旧（区間ごとのコマンドとソフトの一覧）を順番でなく集合で比べる。鍵は (パス, 区間, 席, メッシュ, クラスタ)。旧のコマンドはクラスタの番号を持たないので、クラスタはインデックスの範囲（メッシュの基点を引いた先頭・数・頂点の基点）で表し、新の要素はクラスタの記録から同じ範囲を引く（LOD の段はクラスタで決まる）。2 通りの hash の和と数、範囲の外の要素、区画の先頭 + 区画の中の位置の重なり（照合の印 `MegaGeometry_BinCheckMarks`、binding 25、16 MB）、区間ごとの数、ソフトの集合を比べ、ホストが `MEGA_BIN mismatch=<n> entries old sw_entries old_sw bins` を出す（0 でなければ ERROR）。
  - `MEGA_CAPACITY` に `visible_list_capacity`・`visible_list_bytes`・`visible_list_overflow`（積もうとした数 − 容量）・`visible_list_peak` を足した。統計のバッファを 16 → 32 語にし、`Common/MegaGeometryCull.glsl` の統計の配列を長さ無しにした（VSM の判定は短い範囲を結ぶため）。カリングのディスクリプタセットの形に binding 23〜25 を足し、各パスのセットは平らな判定が無くても結ぶ（接頭和・照合が同じセットを使う）。GPU 時間の名前に `MegaBin1`・`MegaBin2` を足した。
- 検証（`.harness/runs/20261014-033121/`）:
  - `verify-SCN-MEGA-BINNING-PREP-4.txt`: 最終のコードの RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-MEGA-BINNING-PREP-5.txt`: mega-stress 5000 個（動き 1%・区画 64 m）overview、result=pass、EXIT=0。`-5.stress.Game.log` の `[ERROR]` 0、`MEGA_BIN` 7 行すべて mismatch=0（例: `entries=350066 old=350066 sw_entries=343958 old_sw=343958 bins=56`。`MEGA_OCCLUSION pass1=350066 pass2_drawn=0`・`SW_RASTER_BIN pass1=343958 pass2=0` と一致）、`MEGA_CAPACITY` 全行 `visible_list_overflow=0`・`visible_list_peak=422444`・`draw_dropped=0`・`bvh_overflow=0`。
  - `verify-SCN-MEGA-BINNING-PREP-6.txt`: バナナ 1 万本 overview、result=pass、EXIT=0。`-6.bananas.Game.log` の `[ERROR]` 0、`MEGA_BIN` 10 行すべて mismatch=0（例: `entries=364821 old=364821 sw_entries=361769 old_sw=361769 bins=4`）、`visible_list_overflow=0`・`visible_list_peak=367658`。`MEGA_OCCLUSION pass1=364821 pass2_tested=367654 occluded=2833` は前の反復と同じで、PNG はバナナの散布と 3 つの山が以前どおり。
  - `-1.txt`〜`-2.txt` は行末を直す前の同じ C++ のビルドと mega-stress の撮影（どちらも pass、mismatch=0）。
- 補助（verify の外、照合が違いを見つけることの確認）: 全体の列の 5 番目の要素だけクラスタの番号を 1 ビット変えたシェーダーで撮った（`aux-SCN-MEGA-BINNING-PREP-fault.txt`、`.harness/runs/scene-capture/SCN-MEGA-BINNING-PREP/fault`）。数は一致したまま `MEGA_BIN mismatch=2 ... hash_mismatch=2` が ERROR で出た。シェーダーは元に戻した。
- Next: SCN-MEGA-BINNING-SWITCH。
- Notes: 撮影のスクリプトは Game.log の ERROR では落ちない（`-RequireChecks` の ENGINE_CHECK だけを見る）ので、`MEGA_BIN` の合否はログを開いて確かめた。照合を ENGINE_CHECK に載せるには、描画スレッドから `EngineChecks`（GameThread だけが読み書きする）へ渡す口が要る（このタスクの paths の外）。`GeometryPageRequestVulkanTest`（ctest、GPU）は `cluster_cull.comp` を自前のディスクリプタセットの形（binding 0〜13・22）で作るので、`cluster_cull.comp` が binding 23・24 を使うようになった分を足す必要がある（試験は paths の外で、verify にも無い）。全体の列の容量（約 419 万）は 5000 個の mega-stress で最大 42 万。100 万本・混在の足りる・足りないは SCN-MEGA-ACCEPT-1M の `visible_list_overflow`・`visible_list_peak` で判断する。性能は測っていない。試験の追加はしていない。

### SCN-MEGA-BINNING-PREP（2026-10-14、反復 1・run 20261014-061910、評価の 1 周目の差し戻しへの対応）
- Done: 全体の列を確保できなかったときも `MEGA_CAPACITY` の枠と溢れが正しく出るようにした。全体の列へ積もうとした数（数えのバッファの頭の語 0）を、照合の有無によらず統計を読み戻すフレームごとに `CopyBuffer` で統計の 16 へ写す（照合のシェーダー `mega_bin_check.comp` の同じ語への書き込みは外し、書き手を 1 つにした）。フレームの列の容量（確保できなければ 0）を `PendingStats::VisibleListCapacity` に持ち、照合の `listed` と `MEGA_CAPACITY` の `visible_list_capacity`・`visible_list_overflow`（積もうとした数 − 容量）・`visible_list_peak`（読み戻したフレームごと）をこれから出す。数えのバッファは写しの後に UnorderedAccess へ戻す（照合したフレームは IndirectArgument から）。
- 評価の指摘 1（`GeometryPageRequestVulkanTest` が束縛 23・24 を持たない）は、親の決定どおり直していない（ctest から外した写しの試験）。
- 検証（`.harness/runs/20261014-061910/`）:
  - `verify-SCN-MEGA-BINNING-PREP-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-MEGA-BINNING-PREP-2.txt`: mega-stress 5000 個（動き 1%・区画 64 m）overview、result=pass、EXIT=0。`-2.stress.Game.log` の `[ERROR]` 0、`MEGA_BIN` 7 行すべて mismatch=0（例: `entries=350066 old=350066 sw_entries=343958 old_sw=343958 bins=56`。`entries` は写した数から出すので、写しの経路が照合と一致することの確認にもなる）、`MEGA_CAPACITY` 7 行すべて `visible_list_capacity=4194304 visible_list_overflow=0`・`draw_dropped=0`・`bvh_overflow=0`、`visible_list_peak` は最大 422444（前の反復と同じ）。
  - `verify-SCN-MEGA-BINNING-PREP-3.txt`: バナナ 1 万本 overview、result=pass、EXIT=0。`-3.bananas.Game.log` の `[ERROR]` 0、`MEGA_BIN` 10 行すべて mismatch=0（例: `entries=364821 old=364821 sw_entries=361769 old_sw=361769 bins=4`）、`visible_list_overflow=0`・`visible_list_peak` は最大 367658、`MEGA_OCCLUSION pass1=364821 pass2_tested=367654 occluded=2833` は前の反復と同じ。PNG はバナナの散布と 3 つの山が以前どおり。
- Next: SCN-MEGA-BINNING-SWITCH。
- Notes: 列の確保の失敗そのものは実機で再現していない（64 MB の確保は通る）。確保できないときシェーダーは列の代わりに結んだ仮のバッファの長さ（8 要素）を容量として見るが、その 8 要素は使えないので、ログの容量は 0 として積もうとした数をそのまま溢れに出す。試験の追加はしていない。

### SCN-MEGA-BINNING-PREP（2026-10-14、反復 2・run 20261014-061910、評価の 2 周目の差し戻しへの対応）
- Done: 数えのバッファ（`MegaGeometry_BinCounters`）の作成の使用フラグに `TransferSrc` を足した。前の反復で足した、積もうとした数を統計へ写す `CopyBuffer` がこのバッファを転送元に使うのに、フラグが `StorageBuffer | IndirectBuffer | TransferDst` だけで `VUID-vkCmdCopyBuffer-srcBuffer-00118` に当たっていた（Vulkan の実装はフラグをそのまま写し、自動では足さない）。統計の写し先（借りた読み戻し先）は既に `FillBuffer` の転送先として使っている。
- 検証（`.harness/runs/20261014-061910/`）:
  - `verify-SCN-MEGA-BINNING-PREP-4.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0（`MegaGeometryPass.cpp` を再コンパイル）。
  - `verify-SCN-MEGA-BINNING-PREP-5.txt`: mega-stress 5000 個 overview、result=pass、EXIT=0。`-5.stress.Game.log` の `[ERROR]` 0、`MEGA_BIN` 7 行すべて mismatch=0（例: `entries=350066 old=350066 sw_entries=343958 old_sw=343958 bins=56`）、`MEGA_CAPACITY` 7 行すべて `visible_list_capacity=4194304 visible_list_overflow=0 draw_dropped=0 bvh_overflow=0`、`visible_list_peak` は最大 422444（前の反復と同じ）。
  - `verify-SCN-MEGA-BINNING-PREP-6.txt`: バナナ 1 万本 overview、result=pass、EXIT=0。`-6.bananas.Game.log` の `[ERROR]` 0、`MEGA_BIN` 10 行すべて mismatch=0、`visible_list_overflow=0`・`visible_list_peak` は最大 367658、`MEGA_OCCLUSION pass1=364821 pass2_tested=367654 occluded=2833` は前と同じ。PNG はバナナの散布と 3 つの山が以前どおり。
- Next: SCN-MEGA-BINNING-SWITCH。
- Notes: 検証レイヤーは撮影のスクリプトでは Debug の構成だけで有効になり、verify（RelWithDebInfo）では VUID を見ていない。Debug の Game のビルドと撮影はしていない。試験の追加はしていない。

### SCN-MEGA-BINNING-SWITCH（2026-10-14、反復 3・run 20261014-061910）
- Done: 描画・記録・ソフトの一覧を、可視クラスタの binning の結果で詰めて書く経路に切り替えた（既定）。`--mega-binning=off` で区間ごとのコマンドの旧経路へ戻る。
  - 判定（`EmitDrawCommand`）は詰めた経路では全体の列へ積むだけで、区間ごとのコマンド・描画情報・ソフトの一覧を書かない。ソフトの区画へ回すのはソフトのラスタが走るフレーム（`swRasterEnabled=2`）だけ。列の容量を超えたクラスタは `draw_dropped` にも数える。
  - 接頭和（`mega_bin_prefix.comp`）は詰めた経路で、このパスの区間ごとにハードの区画の数を区間のカウンタ（間接描画の数）へ、記録の区間の表（P の先頭・ハードとソフトの数の和。記録の枠で頭打ち）を区間のカウンタの後ろ（256 バイト境界）へ書き、このパスが列へ積んだ範囲と書き出しの間接 dispatch の引数を書く。旧の接頭和（照合用）は旧経路だけ。
  - 書き出し（新規 `mega_bin_scatter.comp`、カリングと同じセット、各パスの接頭和の後に間接 dispatch）: P = 区画の先頭 + 区画の中の位置。描画情報の P 番・記録用のコマンド（コマンドのバッファの全パスの範囲の後ろ、P 番）を書き、ハードの要素は区間のコマンドの範囲の先頭 + 区画の中の位置に firstInstance = P のコマンドを書き、ソフトの要素はソフトの一覧へ P を積む。記録の番号は両パスとも 1 + P で、ハード（`visbuffer_mega.vert` の 1 + firstInstance）とソフト（一覧の値 + 1）が同じ記録を引く。1 パス目の区画は 2 パス目の区画より前に並ぶので、2 パス目の接頭和・書き出しは 1 パス目の P を変えない。
  - 記録の計算（`visbuffer_records*.comp`）は `counts.z`（記録用のコマンドの先頭）・`counts.w`（詰めた経路の印）を足し、詰めた経路では記録の区間の表の数をそのまま使う。`VisibilityDrawPlan` に `RecordSlots`・`bCompactRecords`・`RecordCommandBase`・`RecordSectionBuffer/Offset/Bytes` を足し、`VisibilityRasterPass` は記録の枠を `RecordSlots`（詰めた経路は min(全パスのコマンドの数, 列の容量)）で取る。GBuffer の経路（sink 無し）も同じ区間のカウンタ（ハードの数）と詰めたコマンドを描く。
  - 数えの頭を 16 → 32 語にし、書き出しの引数・区間の数・記録の枠の数（0 なら旧経路）・記録用のコマンドの先頭・記録の区間の表の先頭・パスの範囲を足した。詰めた経路は区間ごとのコマンドを書かないので、新旧の照合（`MEGA_BIN`）は旧経路のフレームだけで走る。
- 検証（`.harness/runs/20261014-061910/`）:
  - `verify-SCN-MEGA-BINNING-SWITCH-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-MEGA-BINNING-SWITCH-2.txt`: 起動画面 default の決定的撮影の比較 result=pass（psnr 60.066 dB、輝度差 0.0021、EXIT=0）。`default.Game.log` の `[ERROR]` 0、`SW_RASTER_BIN pass1=1849 pass2=0 hw=823 overflow=0 zeroed=0`（1849 + 823 = `MEGA_OCCLUSION pass1=2672`）、`draw_dropped=0`・`visible_list_overflow=0`。PNG は家・球・岩・地面が以前どおり。
  - `verify-SCN-MEGA-BINNING-SWITCH-3.txt`: バナナ 1 万本 overview の比較 result=pass（psnr 67.526 dB、輝度差 0.0001、EXIT=0）。`overview.Game.log` の `[ERROR]` 0、`SW_RASTER_BIN` 10 行すべて `zeroed=0`（落ち着いた後は `pass1=361769 hw=3052 overflow=0 sw_groups=361769`。PREP の照合の `sw_entries=361769` と同じ振り分け）、`MEGA_CAPACITY draw_dropped=0 visible_list_overflow=0`。PNG はバナナの散布と 3 つの山が以前どおり。
- 補助（verify の外）: `--mega-binning=off` のバナナ 1 万本 overview（`aux-SCN-MEGA-BINNING-SWITCH-off.txt`、`.harness/runs/scene-capture/SCN-MEGA-BINNING-SWITCH/off`）も比較 pass（psnr 67.802 dB）、`[ERROR]` 0、旧経路の `zeroed=361769`、`MEGA_BIN` 全行 mismatch=0。
- Next: SCN-MEGA-PREV-HIZ。
- Notes: 撮影した 2 シーンは落ち着いた後に 2 パス目の描画が 0 で、1・2 パスの両方に描くフレームは無い（エポックの初めは 1 パス目が 0）。両パスにまたがる記録の番号の保持は、区画の並び（1 パス目が前）と、接頭和・書き出しが今のパスの区間だけを書く形で担保している。コマンドのバッファは記録用の範囲の分だけ大きくなり（2 の冪へ切り上げで バナナ 1 万本の `indirect_bytes` 168 → 336 MB、`draw_info_bytes` 67 → 134 MB）、記録の表は枠が 838 万 → 419 万に減る（約 537 → 268 MB）。性能は測っていない（SCN-MEGA-ACCEPT-1M で判断）。検証レイヤー（Debug）での撮影はしていない。試験の追加はしていない。

### SCN-MEGA-BINNING-SWITCH（2026-10-14、反復 4・run 20261014-061910、評価の 1 周目の差し戻しへの対応）
- Done: 記録の枠で頭打ちになったとき、間接描画の数が書かれていないコマンドを指す問題を直した。接頭和（`mega_bin_prefix.comp`）は区間のカウンタ（間接描画の数）へハードの区画の数をそのまま書いていたが、書き出し（`mega_bin_scatter.comp`）は P が記録の枠以上の要素を捨てる。区間のカウンタを min(区画の先頭 + ハードの数, 枠) − min(区画の先頭, 枠)（枠に入ったハードの数）にし、書き出しが書く範囲と揃えた。ハードの区画は区間の中でソフトの区画より前に並ぶので、枠に入ったハードの要素は区画の中の位置 0 から連続する。記録の区間の表の数・旧経路・`visbuffer_records*.comp` は変えていない（詰めた経路の記録は表の数を使う）。
- 検証（`.harness/runs/20261014-061910/`）:
  - `verify-SCN-MEGA-BINNING-SWITCH-5.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0（シェーダーは実行時にコンパイル）。`-4.txt` は Git Bash から呼んで `/m:1` がパスに変換され MSBuild が引数で落ちた版（EXIT=1、コードの失敗ではない）。
  - `verify-SCN-MEGA-BINNING-SWITCH-6.txt`: 起動画面 default の決定的撮影の比較 result=pass（psnr 60.066 dB、輝度差 0.0021、EXIT=0）。`default.Game.log` の `[ERROR]` 0、`SW_RASTER_BIN pass1=1849 pass2=0 hw=823 overflow=0 zeroed=0`、`draw_dropped=0`・`visible_list_overflow=0`。PNG は家・球・岩・地面が以前どおり。
  - `verify-SCN-MEGA-BINNING-SWITCH-7.txt`: バナナ 1 万本 overview の比較 result=pass（psnr 65.86 dB、輝度差 0、EXIT=0）。`overview.Game.log` の `[ERROR]` 0、`SW_RASTER_BIN` 10 行すべて `zeroed=0`（落ち着いた後 `pass1=361769 hw=3052 overflow=0`）、`MEGA_CAPACITY` 10 行すべて `draw_dropped=0 visible_list_overflow=0`。PNG はバナナの散布と 3 つの山が以前どおり。
- Next: SCN-MEGA-PREV-HIZ。
- Notes: 撮影した 2 シーンは記録の枠に届かないので、直した頭打ちの経路は GPU の実機では通っていない（評価者の CPU 上の再現の条件 `MaxDrawCount=2`・ハード `[5,1]` に当たる枠の不足は、詰めた経路の枠が min(全パスのコマンドの数, 列の容量) なので通常の撮影では起きない）。試験の追加はしていない。

### SCN-MEGA-PREV-HIZ（2026-10-14、反復 5・run 20261014-061910）
- Done: HZB を 2 枚（`m_HiZ[2]`）にして交互に作り、作った側に契約 `MegaHiZContract`（作った記録のフレームの数え・描いたゲームのフレームの番号・ジッタ込みの VP・元の深度の寸法・シーンの世代・インスタンスを全部作り直した回数・`CameraId`/`SourceCameraId`・ビュー/ビューポート）を持たせた。2 パスのフレームは、HZB を作る前に前のフレームの 1 枚の契約と今のフレームの値を `EvaluatePreviousHiZ` で比べ、`MegaPrevHiZStatus`（ready / epoch / missing / view / resize / stale / packet_gap / generation / resync / camera_cut）を決めて `GetPreviousHiZStatus()` で公開する。判定にはまだ使わない（2 パス目は今までどおり今のフレームの HZB で判定）。
  - VP は `PreviousMainCamera` を使わず、深度を描いたフレームの `CameraViewConstants::ViewProjectionMatrix`（ジッタ込み）を自分で保存する。
  - 全量の再同期は、`Setup` がインスタンスを作り直したとき（基準の適用・写しの版の取り逃がし・全量の同期）と `AddMegaMeshInstance`/`ClearMegaMeshInstances` で数える。シーンの世代は `Setup` が写し（`MegaGeometryMirror::GetGeneration`）から受け取る（全量の同期では 0）。パケットの飛びはゲームのフレームの番号が 2 以上進む・戻るとき（同じ番号の描き直しは飛びにしない）。カメラの切れ目は TAA と同じく `SourceCameraId`（と `CameraId`）が替わったとき。寸法が替わると 2 枚とも作り直す。
  - 開発ビルドは判定が変わったフレームと 300 回ごとに `MEGA_PREV_HIZ ready=<0|1> reason=<理由> frame epoch_frame render_frame prev_render_frame prev_frame size prev_size generation prev_generation resync prev_resync ready_frames not_ready_frames` を出す。
- 検証（`.harness/runs/20261014-061910/`）:
  - `verify-SCN-MEGA-PREV-HIZ-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-MEGA-PREV-HIZ-2.txt`: 起動画面 default の決定的撮影の比較 result=pass（psnr 60.066 dB、輝度差 0.0021、EXIT=0）。`default.Game.log` の `[ERROR]` 0。`MEGA_PREV_HIZ` は 4 行: 最初の 2 パスのフレームで `ready=0 reason=missing`、次のフレームから `ready=1`、エポックの先頭（`epoch_frame=0`）で `ready=0 reason=epoch`、`epoch_frame=1` から `ready=1`（`ready_frames=113 not_ready_frames=2`）。PNG は家・球・岩・地面が以前どおり。
- Next: SCN-MEGA-TWOPASS-INSTANCES。
- Notes: 撮影で実際に出た理由は missing・epoch だけで、resize・camera_cut・resync・packet_gap・generation・stale・view は実機の起動では通していない（コードの判定だけ）。全量の同期（`--mega-scene-full-sync`）は毎フレームインスタンスを作り直すので常に `reason=resync` になる。1 フレームに複数のビューポートで記録すると、交互の 2 枚を別のビューポートが使うので `reason=view` になる（ビューごとの HZB は持たない）。HZB の 2 枚目の分だけ GPU メモリが増える（1280×720 で R32 の半分の解像度の全ミップ、約 1.2 MB）。試験の追加はしていない。

### SCN-MEGA-TWOPASS-INSTANCES（2026-10-14、反復 6・run 20261014-061910）
- Done: 範囲とインスタンス（席）の 1 パス目を、契約が合うフレーム（`MEGA_PREV_HIZ ready=1`）だけ前のフレームの HZB で判定するようにした。HZB は今までどおり 1 パス目の深度（ソフトの合流の後）から作り、前の HZB の契約の評価を 1 パス目の前へ移した。
  - 範囲の段は、視錐台・距離を通った範囲の AABB を包む球を前の VP（ジッタ込み）で前の HZB に当て、隠れていれば範囲ごと 2 パス目へ回す。席の段は、遮蔽以外の判定と根の LOD を通った席を前の HZB に当て、見える席だけ 1 パス目でたどり（根の入力へも積む）、隠れた席はワークグループで印をまとめて範囲ごとに 1 要素として回す。回した範囲の列は根の入力と同じバッファの後ろの別の列（範囲・席 64 ビットの印、容量 65536 範囲）で、2 パス目は根の入力とは別の間接 dispatch で印の立った席を今のフレームの HZB で判定する。
  - 根の入力か回した範囲の列が溢れたら、2 パス目は両方の列を使わず全部の根を席の遮蔽の判定なしで判定し直す（溢れて積めなかった範囲の席を取りこぼさない。節・クラスタは今のフレームの HZB で判定を続ける）。
  - 2 パス目が 1 パス目のたどらなかったインスタンスにも届くので、「前のフレームで見えた」印から 1 パス目で描いたかを推せない。1 パス目が描いたクラスタへ 1 パス目の印（書く印の最上位ビットを反転した値）を書き、2 パス目はその印で重ねて描かないようにした。
  - `--mega-twopass=legacy` で旧経路（1 パス目は遮蔽の判定をせず、印から推す）へ戻る。開発ビルドは `MEGA_TWOPASS mode prev_hiz reason pass1_seats deferred_ranges deferred_seats defer_entries defer_overflow rescan pass2_tested pass2_passed` を出す。
- 検証（`.harness/runs/20261014-061910/`）:
  - `verify-SCN-MEGA-TWOPASS-INSTANCES-1.txt`: RelWithDebInfo の Game のビルド EXIT=0（警告は触っていない `VisibilityRasterPass.cpp` の既存の C4244 だけ）。シェーダーは `glslc` で cluster_bvh_cull・cluster_cull・vsm_mega_cull・mega_bin_*・vsm_mega_chunks のコンパイルも確かめた。
  - `verify-SCN-MEGA-TWOPASS-INSTANCES-2.txt`: 旋回（遮蔽 Off との一致）f60・f75・f90 すべて within_limits（psnr 60.881 / 60.429 / 59.279 dB、輝度差 0.0007 / 0.0072 / 0.039）、result=pass、EXIT=0。`default-orbit.Game.log` の `[ERROR]` 0、エポックの先頭は `prev_hiz=0 reason=epoch`、以降は `prev_hiz=1 pass1_seats=6`、`MEGA_CAPACITY` 全行 `draw_dropped=0 bvh_overflow=0 bvh_pass2_fallback=0 visible_list_overflow=0`。PNG は家・球・岩・地面が以前どおり。
  - `verify-SCN-MEGA-TWOPASS-INSTANCES-3.txt`: 起動画面 default の決定的撮影の比較 result=pass（psnr 60.066 dB、輝度差 0.0021、EXIT=0）。`default.Game.log` の `[ERROR]` 0、`MEGA_TWOPASS prev_hiz=1 pass1_seats=6`、`SW_RASTER_BIN pass1=1849 pass2=0 hw=823 zeroed=0`。PNG は以前どおり。
- 補助（verify の外。起動画面は 6 席とも見えていて回す経路を通らないため）:
  - バナナ 1 万本 overview と SCN-MEGA-BASE の比較 pass（psnr 66.223 dB、`aux-SCN-MEGA-TWOPASS-INSTANCES-bananas.txt`）。`pass1_seats=2611`、回した席 0、`MEGA_OCCLUSION` は前と同じ。
  - バナナ 1 万本の low・horizon を `--mega-twopass=legacy` で撮り（`aux-...-legacy.txt`）、既定の経路と比べて pass（low psnr 100 dB、horizon 59.936 dB、`aux-...-prev.txt`）。既定の経路は `deferred_seats=2`・`pass2_tested=2`・`pass2_passed=0`、`[ERROR]` 0。
  - 回した範囲の列を必ず溢れさせたシェーダー（一時的な変更。戻した）で同じ 2 視点を撮り、旧経路と比べて pass（low psnr 100 dB、horizon 61.549 dB、`aux-...-overflow.txt`）。`defer_overflow=2`/`1`・`rescan=1`、`[ERROR]` 0。
- Next: SCN-MEGA-TWOPASS-CLUSTERS。
- Notes: 撮影で範囲ごと回した範囲（`deferred_ranges`）は 0 で、範囲の段の前の HZB の判定は実機では通っていない（席の判定と同じ関数で、球は AABB を包む）。前の HZB は前のフレームの 1 パス目の深度から作ったもので、そのフレームの 2 パス目の描画を含まない（隠れる判定が少なめになる側）。溢れたときは溢れた範囲だけでなく全部の根を見直す（溢れた範囲を特定する印を持たないため）。1 パス目の印と「見えた」印の周回での衝突は反復 7 で直した（下の節）。性能は測っていない（SCN-MEGA-ACCEPT-1M で判断）。統計のバッファは 32 → 48 語。試験の追加はしていない。

### SCN-MEGA-TWOPASS-INSTANCES（2026-10-14、反復 7・run 20261014-061910。評価の差し戻しへの対応）
- Done: 「見えた」印と 1 パス目の印が周回で衝突し、見えるクラスタを取りこぼす件を直した。
  - 「見えた」印（読む印・書く印）を 1〜2^31−1 の 31 ビットで回し（`VisibleStampOf`）、1 パス目の印は書く印に最上位のビットを足した値にした。1 パス目の印は「見えた」印・0 のどれとも一致しない。
  - 書く印が 1 に戻るフレーム（周回の始まり）は `UpdateVisibilityLayout` が全部の区画を 0 で埋め、前の周回の「見えた」印・1 パス目の印を残さない。そのフレームが 2 パスでなく通らなくても、次のフレームは連続の条件を満たす区画が無いので使う区画は 0 から始まる。
  - 前の反復の Notes にあった「衝突時は 1 パス目に描くだけ」は誤りだったので消した（2 パス目が描画済みと誤認して描かない側だった）。
- 検証（`.harness/runs/20261014-061910/`）:
  - `verify-SCN-MEGA-TWOPASS-INSTANCES-4.txt`: RelWithDebInfo の Game のビルド EXIT=0。
  - `verify-SCN-MEGA-TWOPASS-INSTANCES-5.txt`: 旋回（遮蔽 Off との一致）f60・f75・f90 すべて within_limits（psnr 60.882 / 60.432 / 59.258 dB、輝度差 0.0007 / 0.0072 / 0.0391）、result=pass、EXIT=0。`default-orbit.Game.log` の `[ERROR]` 0、エポックの先頭は `prev_hiz=0 reason=epoch`。
  - `verify-SCN-MEGA-TWOPASS-INSTANCES-6.txt`: 起動画面 default の決定的撮影の比較 result=pass（psnr 60.066 dB、輝度差 0.0021、EXIT=0）。`default.Game.log` の `[ERROR]` 0、`prev_hiz=1 reason=ready pass1_seats=6`。PNG は家・球・岩・地面が以前どおり。
- Next: SCN-MEGA-TWOPASS-CLUSTERS。
- Notes: 周回の始まりは約 2^31 記録ごと（60fps で約 414 日）で、実機では通していない（印の値は Python で周回の前後を計算して確かめた）。ページの要求の印（`requestStamp`）も書く印を使うので 31 ビットで回るが、一致しても同じフレームの要求を 1 回省くだけ。試験の追加はしていない。

### SCN-MEGA-TWOPASS-CLUSTERS（2026-10-14、反復 8・run 20261014-061910）
- Done: 節・葉・クラスタの 1 パス目も前のフレームの HZB で判定し、隠れたものを 2 パス目の列へ回すようにして、クラスタごとの「見えた」ビットを消した。
  - 1 パス目: 節の段は視錐台・LOD を通った節が前の HZB に隠れていれば（インスタンス, 節）を節の列へ積んで枝をたどらない。葉の段は判定を通ったクラスタが前の HZB に隠れていれば描かずに（インスタンス, クラスタ）をクラスタの列へ積み、隠れていなければ描いて子のページの要求と自分のページの使用の印を出す（2 パス目はそのクラスタを判定しないので）。
  - 2 パス目: 回された範囲の席（前の項目のまま）・節（1 要素 1 スレッド、今の HZB で判定して子を段 1 の列へ）を段 0 の後に、回されたクラスタ（1 要素 1 スレッド）を葉の段の後に判定する。1 パス目がたどった席を 2 パス目が根から判定し直すこと（2 パス目の根の入力）はやめた。
  - 溢れ: 範囲・節・クラスタのどの列が溢れても、2 パス目は 3 つの列を使わず全部の根を席の遮蔽なしで見直す（TWOPASS-INSTANCES と同じ規則）。重ねて描かないよう、見直しは 1 パス目の道（範囲・席・節が前の HZB に隠れなかったか）を判定し直して列の要素の y の最上位ビットで運び、葉のクラスタで 1 パス目が描いたかを決める。
  - 平らな判定（`cluster_cull.comp`）は列を持たず、2 パス目も全クラスタを見て、1 パス目が描いたか（判定を通り前の HZB に隠れない）を判定し直す。前の HZB を BVH の無いシーンでも使う（`bvhInstanceCount > 0` の条件を外した）。
  - 前の HZB を使えないフレーム（エポックの先頭・カメラの切れ目など）は 1 パス目が遮蔽の判定をせずに判定を通った全部を描き、2 パス目へは何も回さない。
  - 消したもの: binding 5 の「見えた」ビットのバッファ、席ごとの区画（`AssignVisibilityRegion`・`PeekVisibilityRegion`・`UpdateVisibilityLayout`・`VisibilityRequest`・`VisibilityRegion`）、インスタンスの表の「見えた」印の先頭（`GPUMegaEntry::VisibleOffset`。GPU の行の語は予約で 0）、`--mega-twopass=legacy`（「見えた」ビットに頼る経路なので消えた）。ページの要求の重複を省く記録ごとの印は `PageRequestStamp`（`PageRequestStampOf`）として残した。
  - ユニフォームの並びは変えず、空いた語を `BvhDeferClusterCapacity`・`PageRequestStamp`・`BvhDeferNodeCapacity`・`PrevHiZMipCount` にした。binding 14 の頭は 32 語、節の列 2^21・クラスタの列 2^22 要素（`bvh_pass2_bytes` は約 51 MB。前の根の入力 1M 語は無くなった）。統計の 38〜41 に回した節・クラスタと溢れを足し、`MEGA_TWOPASS` に `deferred_nodes deferred_clusters node_overflow cluster_overflow` を出す（`mode=` は消した）。`MEGA_CAPACITY visibility_bytes` はスクリプトが読む鍵として残し 0。
- 検証（`.harness/runs/20261014-061910/`）:
  - `verify-SCN-MEGA-TWOPASS-CLUSTERS-1.txt`: RelWithDebInfo の Game のビルド EXIT=0。シェーダーは BOM を除いた写しで `glslc` により cluster_bvh_cull・cluster_cull・vsm_mega_cull・vsm_mega_chunks・mega_bin_*・mega_scene_scatter・visbuffer_sw_raster のコンパイルも確かめた。
  - `verify-SCN-MEGA-TWOPASS-CLUSTERS-2.txt`: バナナ 1 万本 overview と SCN-MEGA-BASE の比較 pass（psnr 69.297 dB、輝度差 0.0001、EXIT=0）。`overview.Game.log` の `[ERROR]` 0、エポックの先頭は `prev_hiz=0 reason=epoch`（1 パス目が 367658 クラスタを描く）、以降 `prev_hiz=1 pass1_seats=2611 deferred_nodes=1 deferred_clusters=2833`、`MEGA_OCCLUSION pass1=364821 pass2_tested=2833 pass2_drawn=0 occluded=2833`、`MEGA_CAPACITY draw_dropped=0 bvh_overflow=0 bvh_pass2_fallback=0 visible_list_overflow=0`。PNG はバナナの散布・山・影・床が以前どおり。
  - `verify-SCN-MEGA-TWOPASS-CLUSTERS-3.txt`: 下の溢れの確認の後、元の容量で再ビルド EXIT=0。
- 補助（verify の外）:
  - 旋回（遮蔽 Off との一致）f60・f75・f90 すべて within_limits（psnr 60.907 / 60.424 / 59.274 dB、`aux-SCN-MEGA-TWOPASS-CLUSTERS-orbit.txt`）、result=pass。`default-orbit.Game.log` の `[ERROR]` 0、溢れ 0、`deferred_clusters=6〜7`。
  - 節・クラスタの列の容量を 1 にした一時的なビルド（戻した）でバナナ overview を撮り SCN-MEGA-BASE と比べて pass（psnr 66.265 dB、`aux-SCN-MEGA-TWOPASS-CLUSTERS-overflow.txt`）。`cluster_overflow=2832 rescan=1 bvh_pass2_fallback=1`、`MEGA_OCCLUSION` は溢れないときと同じ（pass1=364821・pass2_tested=2833・occluded=2833。見直しで 1 パス目の描いた分を重ねて判定していない）、`[ERROR]` 0。
- Next: SCN-MEGA-VSM-RANGES。
- Notes: 前の HZB を使えないフレームは遮蔽なしで全部を描く（以前は前のフレームで見えたクラスタだけを 1 パス目が描き、残りを 2 パス目が今の HZB で選んだ）。100 万本でカメラの切れ目の 1 フレームが重くなりうる（SCN-MEGA-ACCEPT-1M で見る）。子のページの要求は 1 パス目が描いた時点でも出す（以前は 2 パス目の今の HZB で見えたときだけ）。撮影で節の列は 1 要素しか通らず、2 パス目の回した節の dispatch・節の溢れ（node_overflow）は実機ではほぼ通っていない（クラスタの溢れと同じ関数で見直しへ入る）。`GeometryPageRequestVulkanTest`（ctest から外した写しの試験）は `cluster_cull.comp` の束縛（binding 5）と引数が変わったが直していない。性能は測っていない（SCN-MEGA-ACCEPT-1M で判断）。試験の追加はしていない。

### SCN-MEGA-VSM-RANGES（2026-10-14、反復 9・run 20261014-061910）
- Done: VSM の組の絞り込み（`vsm_mega_cull_pairs.comp`）の前段に、太陽の段の範囲（AABB）と席の受け手の領域の判定を置いた。
  - 組の絞り込みの 1 ワークグループの 64 席は、描く順の表を 64 席ずつ区切った範囲（`BvhRangeSeats`）の 1 つに当たる。太陽の段では、候補の席の境界の球を包む AABB をワークグループで畳み込み、ライト空間の外接の箱が受け手の領域の外なら範囲の全席が組を作らない。通った範囲の席も、境界の球のライト空間の矩形（`SphereHasDirtyPage` が引くのと同じ矩形）で同じ判定をしてから従来の判定へ進む。点光源の面は判定しない（従来どおり Range で絞る）。境界を持たない投影物が範囲に 1 つでもあれば範囲では落とさない。
  - 受け手の領域（binding 22 の定数、`BuildMegaReceiverRegion`）: XY はカメラの XY（段 0 の中心。ページへスナップ済み）から「受け手距離 + 余白 + 段 0 の 1 ページ」+ その段のページの 1.5 倍の半径の円に掛かること。受け手距離は `MaxShadowDistance × VirtualShadowMapThresholdDistanceScale`（段の選び方のしきい値と同じ前提）、余白は最も粗い段の 1 ページ。太陽の向き（手前）へは制限せず（従来の深度の範囲のまま）、奥は「深度の原点 + 深度のスナップの刻みの半分 + 受け手距離 + 余白」より奥だけにある投影物を落とす。
  - dirty のページはどれも今のフレームに受け手の印付けで要求されたページ（`vsm_allocate.comp` の段階 2）なので、XY の判定は dirty のページの判定より多くは落とさない。深度の上限はカメラが深度の原点のスナップの枠の中にいる間（出れば太陽の段の全ページが無効になる）の受け手を覆うので、キャッシュに残るページに投影物の欠けを作らない。
- 検証（`.harness/runs/20261014-061910/`）:
  - `verify-SCN-MEGA-VSM-RANGES-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。シェーダーは BOM を除いた写しで `glslc` によりコンパイルを確かめた。
  - `verify-SCN-MEGA-VSM-RANGES-2.txt`: 起動画面 default の決定的撮影の比較 result=pass（psnr 60.068 dB、輝度差 0.0021、EXIT=0）。`default.Game.log` の `[ERROR]` 0、`VSM_MEGA_CULL overflow=0`。PNG は家・球・岩・地面・影が以前どおり。
  - `verify-SCN-MEGA-VSM-RANGES-3.txt`: バナナ 1 万本 horizon result=pass（EXIT=0）。`horizon.Game.log` の `[ERROR]` 0、`VSM_MEGA_CULL instances=8464 clusters=233124 overflow=0`（静止で以降は 0）。PNG はバナナと影が以前どおり。TWOPASS-INSTANCES の horizon（`prev`・`legacy`）と比べて psnr 60.449 / 60.136 dB（以前の 2 枚どうしは 59.936 dB）。
- 補助（verify の外）: 受け手距離を一時的に 5 m・余白 0 にしたビルド（戻した）で同じ horizon を撮り（`aux-SCN-MEGA-VSM-RANGES-radius5.txt`）、組が 8464 → 2555 に減り、中ほどから奥のバナナの影が消える（既定との psnr 43.864 dB、差は 594〜890 行）ことで、判定と定数の束縛が効くことを確かめた。元のコードで Game をビルドし直した。
- Next: SCN-MEGA-VSM-BVH-SUN。
- Notes: 範囲の AABB は MegaGeometryPass の範囲の表（`m_RangeBoundsBuffer`）を使わず、組の絞り込みの中で席の境界の球から畳み込んだ（表を VSM へ渡すには `MegaGeometryPass.h` と `VirtualShadowMapPass.cpp` を触るが、paths の外のため）。席ごとの影の表の読み込みは残るので、表を渡せば範囲で落ちた席の読み込みも省ける。カメラの深度はクリップマップに無いので、深度の原点からスナップの刻みの半分（`VirtualShadowMapClipmap.cpp` の `DepthSnapRatio` = 0.25 の半分を `ReceiverDepthSnapSlackRatio` に写した）の余裕を取った。視錐台の端の受け手の直線距離が `MaxShadowDistance` の 2 倍を超える画角（前方に対する横・縦のずれの比の二乗和が 3 超）では、その先の受け手が置くページの投影物を落としうる（段のしきい値と同じ前提）。撮影は静止で dirty のページが最初のフレームだけなので、範囲で落ちた数は統計に出していない。性能は測っていない（SCN-MEGA-ACCEPT-1M で判断）。試験の追加はしていない。

### SCN-MEGA-VSM-RANGES 差し戻しの修正（2026-10-14、反復 10・run 20261014-061910）
- 差し戻し: 受け手の領域の半径を `MaxShadowDistance × 2` に決めていたので、広い画角（縦 120° など）で前方の距離は影の距離の内側なのに直線距離が半径を超える受け手の投影物まで範囲・席で落としていた。
- Done: カメラからの距離で作る受け手の領域（binding 22 の定数 `GPUMegaReceiverRegion`・`BuildMegaReceiverRegion`・`RegionUniform`・席ごとの領域の判定）を消し、範囲の判定を dirty のページの階層で行うようにした。
  - 太陽の段で、64 席の範囲の AABB のライト空間の外接の箱が深度の範囲の外か、箱の XY を包む正方形（1 mm 広げる）が覆うページに dirty のページが 1 つも無ければ、範囲の全席が組を作らない。dirty のページは受け手距離の内側の受け手が今のフレームに要求したページで、太陽の向きへは深度の範囲まで延ばした柱になる。受け手の印付けが画角に応じて選んだページそのものを使うので、カメラの画角・縦横比に依らない。
  - dirty の階層をたどるのは範囲につき 1 スレッド（結果を共有メモリで配る）。箱は範囲のどの席の境界の球の矩形も含むので、範囲の判定は席の判定（`LevelOverlapsSphere`・`SphereHasDirtyPage`）より多くは落とさない。評価者の再現（受け手 (210,0,70)、投影物 (210,2,70)・半径 1）は投影物の矩形が受け手のページに掛かるので落ちない。
- 検証（`.harness/runs/20261014-061910/`）:
  - `verify-SCN-MEGA-VSM-RANGES-4.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。`vsm_mega_cull_pairs.comp` は BOM を除いた写しで `glslc` によりコンパイルを確かめた。
  - `verify-SCN-MEGA-VSM-RANGES-5.txt`: 起動画面 default の決定的撮影の比較 result=pass（psnr 60.068 dB、輝度差 0.0021、EXIT=0）。`default.Game.log` の `[ERROR]` 0、`VSM_MEGA_CULL overflow=0`。PNG は家・球・岩・地面・影が以前どおり。
  - `verify-SCN-MEGA-VSM-RANGES-6.txt`: バナナ 1 万本 horizon result=pass（EXIT=0）。`horizon.Game.log` の `[ERROR]` 0、`VSM_MEGA_CULL instances=9067 clusters=251084 overflow=0`（静止で以降は 0）。PNG はバナナと影が以前どおり。
- Next: SCN-MEGA-VSM-BVH-SUN。
- Notes: 前の反復の深度の上限（受け手の最も奥の深度より奥の投影物を落とす）は、受け手の深度の上限を画角なしに保証できないので外した（太陽の向きの奥は従来の深度の範囲まで）。画角を VSM の組の絞り込みへ渡すには `VirtualShadowMapPass.cpp`（paths の外）を触る。範囲の判定は席の判定より多く落とさないので組の数は範囲の判定が無いときと同じになり（9067。前の反復の 8464 はカメラの距離で余分に落としていた分）、効くのは範囲の 64 席の判定を 1 回にまとめる分だけ。範囲で落ちた数は統計に出していない。性能は測っていない（SCN-MEGA-ACCEPT-1M で判断）。試験の追加はしていない。

### SCN-MEGA-VSM-BVH-SUN（2026-10-14、反復 11・run 20261014-061910）
- Done: 太陽の段（正射影のクリップマップ）の MegaGeometry の投影物のクラスタの選択を、組ごとに全クラスタを見る形から、グループの BVH を段ごとの間接 dispatch でたどる形にした（新しい `vsm_mega_bvh.comp`）。点光源の面と BVH を持たないメッシュは平らな経路（`vsm_mega_cull.comp`）のまま。
  - 組の絞り込み（`vsm_mega_cull_pairs.comp`）は、太陽の段でメッシュの `bvhInfo` が BVH を持つ組を BVH の根として組の一覧の後ろから積む（数は頭の語 2）。組の一覧の頭を 12 語にし、`vsm_mega_cull_args.comp` が平らな経路の引数に加えて段 0 の引数（語 8〜10）を書く。絞り込みの束縛にインスタンスの表（1）とメッシュの表（22）を足した。
  - 段 0〜15（根を段 0。`GROUP_BVH_MAX_LEVELS` = 16 なので全部の節に届く。static_assert で結んだ）は 1 スレッド 1（組, 節）で、節の球を `LevelOverlapsSphere`・`SphereHasDirtyPage`（クラスタの判定と同じ関数）と正射影の LOD（節の親の誤差の最大 ÷ texel × 1.001 ≤ lodBias なら枝を切る）で判定し、葉は葉の列へ、内部の節は子を次の段の列へ積む。葉の段は 1 葉 8 スレッドで、平らな経路と同じ判定（新しい共通の `Common/VirtualShadowMapMegaSelect.glsl` の `SelectShadowCluster`）にかけて一覧へ書く。
  - 列の安全策は BVH-INDIRECT と同じ: 固定容量（節の列 2 本 × 4,194,304・葉の列 2,097,152、計 80 MiB。フレームをまたいで 1 つを共有）、積む前に容量を確かめ、カウンタは積もうとした数、読む側と引数は min(数, 容量)、段 1 以降と葉の段は前の段が書いた引数で間接 dispatch。溢れた要素は統計の語 10（`VSM_MEGA_CULL overflow`）へ数え、その節の球が覆う割り当て済みで dirty のページへ再描画の印を付ける。列を作れないとき・`--vsm-mega-flat` のときは全部の組を平らな経路で選ぶ（`VSM_MEGA_BVH enabled=…` を初期化で 1 行出す）。GPU の区間は `VsmCullBvh`。
- 検証（`.harness/runs/20261014-061910/`。最終のバイナリは 4〜6）:
  - `verify-SCN-MEGA-VSM-BVH-SUN-4.txt`: RelWithDebInfo の Game のビルド EXIT=0。新旧のシェーダーは BOM を除いた写しで `glslc` によりコンパイルを確かめた。
  - `verify-SCN-MEGA-VSM-BVH-SUN-5.txt`: 起動画面 default の決定的撮影の比較 result=pass（psnr 60.068 dB、輝度差 0.0021、EXIT=0）。`default.Game.log` の `[ERROR]` 0、`VSM_MEGA_BVH enabled=1`、`VSM_MEGA_CULL` は 3 行とも前の項目（VSM-RANGES）と同じ数（clusters=192・2168・4401、point_clusters=128・1972・3171）で overflow=0。PNG は家・球・岩・地面・影が以前どおり。
  - `verify-SCN-MEGA-VSM-BVH-SUN-6.txt`: バナナ 1 万本 horizon result=pass（EXIT=0）。`horizon.Game.log` の `[ERROR]` 0、`VSM_MEGA_CULL instances=9195 clusters=256204 overflow=0`（静止で以降は 0）。PNG はバナナと影が以前どおり。
- 補助（verify の外）: 最初の容量（節・葉とも 1,048,576）で撮った `verify-SCN-MEGA-VSM-BVH-SUN-1〜3.txt` は、3 の horizon で `overflow=47525`、溢れたページへの再描画の印で次のフレームも dirty が戻り `overflow=913817` が続いた。列を 8M にし、点光源の統計の語へ段ごとの積もうとした数の最大を書く一時の計測版（戻した）で同じ horizon を撮り（`aux-SCN-MEGA-VSM-BVH-SUN-probe8m.txt`）、節の列の 1 段の最大 1,193,951・葉の列 547,863 と、`clusters=226427` が同じ撮影の VSM-RANGES の horizon（226427）と一致することを確かめてから容量を決めた。
- Next: SCN-MEGA-VSM-BVH-POINT。
- Notes: 列が溢れ続ける負荷では、節の球が覆うページの再描画の印で毎フレーム dirty が戻り、収まるまで同じ量をたどり直す（取りこぼしは無いが重い）。100 万本の容量の足りる・足りないは SCN-MEGA-ACCEPT-1M の `VSM_MEGA_CULL overflow` で判断する。節の段の LOD の判定は焼き込みの BVH の葉の親の誤差の最大に頼る（実行時の階層は親の誤差が既定の最大値なので枝を切らない。主の経路と同じ）。段の数え・ピークの統計は出していない（`VirtualShadowMapPass.cpp` が paths の外）。`VirtualShadowMapVulkanTest` のケース J は平らな経路（合成メッシュは BVH のアドレスが 0）を通るが、verify に無いので走らせていない。性能は測っていない（SCN-MEGA-ACCEPT-1M で判断）。試験の追加はしていない。

### SCN-MEGA-VSM-BVH-POINT（2026-10-14、反復 12・run 20261014-061910）
- Done: 点光源の面（立方体の各面の透視のスライス）の MegaGeometry の投影物のクラスタの選択も、BVH を持つメッシュの組についてグループの BVH の段の間接 dispatch でたどるようにした。
  - 組の絞り込み（`vsm_mega_cull_pairs.comp`）は、太陽の段に限らずメッシュが BVH を持つ組を BVH の経路の根として一覧の後ろから積む。平らな経路（`vsm_mega_cull.comp`）に残るのは BVH を持たないメッシュと `--vsm-mega-flat` のときだけ。
  - `vsm_mega_bvh.comp` の節の段は、スライスの種類に応じて LOD の texel の値（`g_ShadowPerspective`・`g_ShadowAxisZ`・`g_ShadowTexelNdc`・`g_ShadowNearPlane`）を `SelectShadowCluster` と同じに入れる。点光源の面の節の LOD は `ProjectBakedErrorOrtho` が節の球の最も近い点の面の軸の距離で texel を求め、節の球は下のクラスタの親の球を包むので節の値は下の値以上（近い平面での頭打ちも単調）。球の判定（`LevelOverlapsSphere`・`SphereHasDirtyPage`）も包む球の透視像が包まれる球の像を含み、近い平面をまたげば面全体になるので、節で落ちる枝は葉でも落ちる。
  - ホスト（`VirtualShadowMapRaster.cpp/.h`）はコメントとログの文言だけ（列・段・束縛は太陽の段と共有のまま）。
- 検証（`.harness/runs/20261014-061910/`。最終のバイナリは 3）:
  - `verify-SCN-MEGA-VSM-BVH-POINT-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。変えたシェーダーは BOM を除いた写しで `glslc` によりコンパイルを確かめた。
  - `verify-SCN-MEGA-VSM-BVH-POINT-2.txt`: 夜の起動画面 default の決定的撮影 result=pass（EXIT=0）。`default-night.Game.log` の `[ERROR]` 0、`VSM_MEGA_BVH enabled=1`、`VSM_MEGA_CULL` は 3 行とも前の項目と同じ数（point_clusters=128・1972・3171）で overflow=0。PNG は点光源の照らす球・小球の列・岩の影が出ている。
  - `verify-SCN-MEGA-VSM-BVH-POINT-3.txt`: 下の計測版を戻した後の再ビルド EXIT=0。
- 補助（verify の外）:
  - `--vsm-mega-flat`（点光源の面は従来の平らな経路）で同じ夜の撮影をして比べ、psnr 100 dB・不一致の画素 0・輝度差 0（`aux-SCN-MEGA-VSM-BVH-POINT-flat.txt`。`VSM_MEGA_BVH enabled=0`、数も同じ）。点光源の影は変わらない。
  - 葉の段で点光源の面を捨てる一時の計測版（戻した）で撮り、point_clusters が 3 行とも 0 になり画像も変わる（`aux-SCN-MEGA-VSM-BVH-POINT-probe.txt`）ことで、点光源の面の組が全部 BVH の経路を通ったことを確かめた。
- Next: SCN-MEGA-LAYOUT-SEA。
- Notes: 点光源の面は範囲（64 席の AABB）の判定をしない（前の項目のまま。Range で絞る）。BVH の列の容量（節 4,194,304・葉 2,097,152）は太陽の段と点光源の面で共有するので、100 万本で点光源が多い場面の溢れは SCN-MEGA-ACCEPT-1M の `VSM_MEGA_CULL overflow` で判断する。性能は測っていない（SCN-MEGA-ACCEPT-1M で判断）。試験の追加はしていない。

### SCN-MEGA-FIX-BVH-QUEUE-MAIN（2026-10-14、反復 1・run 20261014-094847）— BLOCKED
- Done（途中）: `cluster_bvh_cull.comp` で、内部の節が子を列へ積む前に子ごとに視錐台と LOD（投影した MaxParentError）を判定し（`IsBvhChildCulled`）、通った子だけを詰めて積むようにした。`TestNode` は遮蔽だけを判定する（列の節は積んだ親が判定済み。2パス目へ回した節も1パス目に同じ親が積んだもの）。選ばれるクラスタは変わらない。
- 検証（`.harness/runs/20261014-094847/`）:
  - `verify-SCN-MEGA-FIX-BVH-QUEUE-MAIN-1.txt`: RelWithDebInfo の Game のビルド EXIT=0。シェーダーは BOM を除いた写しで `glslc` によりコンパイルを確かめた。
  - `verify-SCN-MEGA-FIX-BVH-QUEUE-MAIN-2.txt`: 100 万本 horizon・overview の撮影 result=pass。節の列の 1 段の最大は horizon 12,507,738 → 9,053,980、overview 12,987,174 → 7,941,959 に下がったが、葉の列は容量 4,194,304 で頭打ち、描くクラスタの求めた数 6.27M / 6.06M が描画の一覧の容量を超える（draw_dropped 1.2M / 1.3M）。PNG は地平線の手前の床の帯が残る。
  - `verify-SCN-MEGA-FIX-BVH-QUEUE-MAIN-3.txt`: `bvh_overflow=0` の検査 EXIT=1（horizon 4,859,676・overview 3,747,655）。
  - `verify-SCN-MEGA-FIX-BVH-QUEUE-MAIN-4.txt`: 1 万本 overview と SCN-MEGA-BASE の比較 fail（PSNR 16.7 dB）。SCN-MEGA-BASE は SCN-MEGA-LAYOUT-SEA の途中の保存で 1 万本の配置が変わる前の撮影で、変更前のビルドの撮影（`scene-capture/SCN-MEGA-LAYOUT-SEA-10k-rerun`）も平均輝度 122.08 で今回と同じ。
- 補助: 変更前の同じ配置の 1 万本と比べて pass（PSNR 60.3 dB、`aux-SCN-MEGA-FIX-BVH-QUEUE-MAIN-10k-vs-layout.txt`）。`NORVES_MEGA_LOD_ERROR_PX=4` の 100 万本 horizon は bvh_overflow=0・節と葉の列の最大 1.98M・描くクラスタ 3.57M で、地平線までバナナが描かれる（`aux-SCN-MEGA-FIX-BVH-QUEUE-MAIN-lod4/`）。各ログの `[ERROR]` 0。
- 止めた理由: 1 画素の閾値で選ばれるクラスタ自体が多い（見える 32.7 万本で 1 本あたり約 19）。バナナのクックの誤差が段ごとの足し算（`Tools/AssetCook/CookMeshDag.cpp:658`）と属性の項で大きく、最も粗い段（212 三角形）の誤差 0.173 m が半径 0.18 m とほぼ同じ。直す先（クック・シーンの閾値・容量）は paths の外か見た目の判断なので、選択肢を `blocked/SCN-MEGA-FIX-BVH-QUEUE-MAIN.md` に書いた。
- Next: 親の判断待ち（推奨はクックの誤差の直しとクックし直し。verify の 1 万本の比べる先も配置の後の撮影へ替える必要がある）。
- Notes: 性能は測っていない。試験の追加はしていない。焼き込みの BVH は空間だけで分けて LOD の段が混ざるので、節の列がまだ大きければ読み込み時に段ごとにまとめ直す余地がある。

### SCN-MEGA-FIX-COOK-LOD-ERROR（2026-10-14、反復 1・run 20261014-102529）
- Done: クックの LOD の誤差（`Tools/AssetCook/CookMeshDag.cpp`）を、段ごとの足し算・属性込みから、形のずれだけで測る「子の誤差の最大と簡略化の誤差の大きい方」にした。
  - 簡略化の誤差は、元の形（段 0）の頂点から簡略化後の面までの距離の最大と、前の段からのずれ（消えた頂点 → 新しい面、新しい三角形の重心 → 前の面）の大きい方。元の頂点は位置の代表ごとに段ごとちょうど 1 つのクラスタが受け持ち、簡略化のたびに最も近い三角形を持つ新しいクラスタへ受け渡す（`HandOffOriginalVertices`）。属性の重みは簡略化の選び方にだけ使う。
  - フォールバックの段（影・RT が使う）の切り口は、従来の誤差（属性込み・足し算）を `SelectionError` として別に持って選ぶ（DAG の形は変えていないので、フォールバックの三角形は従来と同じ）。`FallbackError` は切り口の形の誤差の最大。
  - 起動画面の大きな球（`generate: displaced-sphere`）は、根の三角形数の下限（新しい `rootMinTriangles`。球はフォールバックの下限 131072 を渡す）で根を止め、ラスタの段がフォールバックより粗くならないようにした（根 130,882 三角形・4 段）。
  - 17 件のモデルを `Scripts/CookAssets.ps1 -OnlyLogicalPath 'Models/*.gltf','Models/*.glb','Models/*.generated' -Force` で焼き直した（133 秒、`aux-cook-models3.txt`）。バナナの根（212 三角形）の誤差 0.173 m → 0.0183 m。
- 検証（`.harness/runs/20261014-102529/`）:
  - `verify-SCN-MEGA-FIX-COOK-LOD-ERROR-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - 再承認（`reapprove-SCN-MEGA-BASE-RWDI.txt`）: 旧基準（`startup-capture/SCN-MEGA-BASE-RWDI-before-cook-lod-error` に退避）と比べ default 50.80・near 45.32・low 52.10 dB（3 視点とも許容内）。PNG を開き、家・球・岩の形・穴なし・遠くの岩が粗くなっていないこと、球と近くの岩に黒い斑点が無いことを確かめて `SCN-MEGA-BASE-RWDI` を撮り直した。
  - `verify-SCN-MEGA-FIX-COOK-LOD-ERROR-2.txt`: 起動画面 default の決定的撮影の比較 result=pass（psnr 100 dB、EXIT=0）。`default.Game.log` の `[ERROR]` 0。
  - `verify-SCN-MEGA-FIX-COOK-LOD-ERROR-3.txt`: 1 万本 overview を `scene-capture/SCN-MEGA-BASE-COOK` に撮った result=pass（EXIT=0、`[ERROR]` 0）。PNG は近くのバナナの形が保たれ穴なし。同じ配置の変更前（`SCN-MEGA-LAYOUT-SEA-10k-rerun`）と比べ、描く一覧の山 682,543 → 238,486、BVH の節の山 1,082,437 → 142,081、葉の山 521,777 → 142,121（`MEGA_CAPACITY`）。
  - `AssetCookMeshSimplifySmoke` pass（切り口の穴・つまみ 0、`aux-simplify-smoke5.txt`）。
- 補助（verify の外。どれも採らなかった形）: 前の段からのずれの最大だけ（task の文のまま）だと、球の石の凹凸が段を重ねて均され、陰影がにじむ（PSNR 43.9 dB で fail、`aux-first-attempt-verify-2-max.txt`）。足し算に戻すと球は戻るがバナナの根は 0.084 m（`aux-exp-sum-*`）。元の形から測っても、球のラスタがフォールバックより粗くなると low 視点で黒い斑点（`SCN-MEGA-FIX-COOK-LOD-ERROR-rejected-reapproval`）、フォールバックを形の誤差で選ぶと近くの岩の窪みに黒い点（RTGI を切ると消える。`-rtgioff`）が出た。
- Next: SCN-MEGA-FIX-BVH-QUEUE-MAIN（100 万本の数はそこで見る）。
- Notes: `CookAssets.ps1` は Windows PowerShell 5 の `Get-Content` の既定の文字コードで `MegaStressMeshes.json`（BOM なし・日本語）を読み損ねるので、`$PSDefaultParameterValues['Get-Content:Encoding']='UTF8'` を付けて呼んだ（スクリプトは paths の外）。`Docs/Architecture/NVMESHv1LodGraph.md` の手順 3 の「メンバの誤差の最大 + 簡略化の誤差」は古い（paths の外）。クックは 93 秒 → 133 秒（元の頂点の受け渡しの距離計算の分）。golden（Indoor/Outdoor）はクック済みのメッシュを使わないので撮り直していない。試験の追加はしていない。

### SCN-MEGA-FIX-COOK-LOD-ERROR（2026-10-14、反復 2・run 20261014-102529）
- Done: 評価者の差し戻し（三角形が残らない簡略化の結果では属性込みの `simplified.ErrorAbsolute` を誤差に使っていた）を直した。`CookMeshDag.cpp` の候補の採用で空の出力を候補から外し（簡略化せず入力を残す処理へ回る）、誤差の計算の空の分岐を消した。出力が空なら「簡略化の出力に三角形が残りませんでした」で失敗にする（来ない前提の検査）。重複していたコメントも 1 つにまとめた。
- 検証（`.harness/runs/20261014-102529/`）:
  - AssetCook を作り直し（`aux-assetcook-build6.txt`、EXIT=0）、17 件のモデルを焼き直した（`aux-cook-models4.txt`、cooked=17、`[ERROR]` 0）。`build/CookedAssets/Cooked` の 173 件の `.nvpkg` の sha256 は焼き直しの前後で全件一致（`aux-recook-hash-diff.txt` は空）。現行の資産では空の出力の経路を通らないことの裏付け。
  - `verify-SCN-MEGA-FIX-COOK-LOD-ERROR-4.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-MEGA-FIX-COOK-LOD-ERROR-5.txt`: 起動画面 default の決定的撮影の比較 result=pass（psnr 100 dB、不一致の画素 0、EXIT=0）。PNG を開き、家・球・岩の形が保たれ穴が無いことを確かめた。資産が前回とバイト一致なので基準の撮り直しはしていない。
  - `verify-SCN-MEGA-FIX-COOK-LOD-ERROR-6.txt`: 1 万本 overview を `scene-capture/SCN-MEGA-BASE-COOK` に撮り直した result=pass（bananas=10000、EXIT=0、`[ERROR]` 0）。PNG は近くのバナナの形が保たれ穴なし。
- Next: SCN-MEGA-FIX-BVH-QUEUE-MAIN（100 万本の数はそこで見る）。
- Notes: 試験の追加はしていない（空の出力を作る入力の試験は計画外）。

### SCN-MEGA-FIX-BVH-QUEUE-MAIN（2026-10-14、反復 3・run 20261014-102529）
- Done: クックの LOD の誤差の直し（SCN-MEGA-FIX-COOK-LOD-ERROR）の後に同じ撮影で見直し、コードの追加の変更なしで合格を確かめた。主の描画の BVH のたどりは反復 1 の形（子を列へ積む前に視錐台と LOD で判定する。485b1950）のまま。選ぶクラスタが画面の細かさに見合う数になり、節の列の 1 段の最大は horizon 12,507,738 → 935,105、overview 12,987,174 → 915,269（容量 4,194,304 の約 22%）。焼き込みの BVH を LOD の段ごとにまとめ直す処理は要らなかった。列の容量の定数は変えていない。
- 検証（`.harness/runs/20261014-102529/`）:
  - `verify-SCN-MEGA-FIX-BVH-QUEUE-MAIN-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-MEGA-FIX-BVH-QUEUE-MAIN-2.txt`: 100 万本 horizon・overview の撮影 result=pass（bananas=1000000、EXIT=0）。両ログの `MEGA_CAPACITY` は全行 `bvh_overflow=0`・`draw_dropped=0`・`visible_list_overflow=0`（描く一覧の山 1.75M / 1.72M）、`[ERROR]` 0。PNG を開き、2 視点とも地平線までバナナが描かれ、床の帯が無いことを確かめた。
  - `verify-SCN-MEGA-FIX-BVH-QUEUE-MAIN-3.txt`: `bvh_overflow=0` の検査 EXIT=0。
  - `verify-SCN-MEGA-FIX-BVH-QUEUE-MAIN-4.txt`: 1 万本 overview と `SCN-MEGA-BASE-COOK` の比較 result=pass（psnr 57.8 dB、輝度差 0.0001、不一致の画素 0.08%、EXIT=0）。PNG は近くのバナナの形が保たれ穴なし。
- Next: SCN-MEGA-FIX-BVH-QUEUE-VSM。
- Notes: 100 万本の 2 視点で影の `VSM_MEGA_CULL overflow` が 230〜420 万あり、中ほどに影の暗い斑が出る。影の列の溢れは SCN-MEGA-FIX-BVH-QUEUE-VSM の範囲なので触っていない。性能は測っていない（SCN-MEGA-ACCEPT-1M で判断）。試験の追加はしていない。

### SCN-MEGA-FIX-BVH-QUEUE-VSM（2026-10-14、反復 4・run 20261014-102529）
- Done: 影の BVH のたどり（`vsm_mega_bvh.comp`）で、節の判定（段の範囲・深度の範囲・dirty のページ・LOD）を `IsShadowNodeCulled` にまとめ、根は段 0 が、子は親が列へ積む前に子ごとに判定して通った子だけを詰めて積む（主の経路の 485b1950 と同じ形）。影の LOD の許容 `LodThresholdTexels` の既定を 1 → 4 texel にした（`VirtualShadowMapRaster.h`）。
  - 調べ（一時の計測版。戻した）: 子の先の判定だけでは horizon の溢れ 2.85M → 1.29M。内訳は葉の列（容量 2,097,152）の溢れ約 96 万と出力の一覧（容量 1,572,864）の溢れで、節の列は溢れていない。一覧と葉の列を大きくした版で、1 texel の求めは初回 391 万 / 343 万（組 26.5 万・21.8 万）、ジオメトリのページの常駐が進んだ後は 550 万を超え、溢れの再描画の印で毎フレーム dirty が戻り続けた。全フレームの最大は 2 texel で 193 万 / 189 万（一覧を超える）、4 texel で 81 万 / 75 万。太陽は地面に置いた物を上から見るので、斜めに見る主のカメラより投影物が多くの texel を覆う。
  - 2 texel と 4 texel の撮影は全体で PSNR 約 40 dB・平均輝度が同じで、近くの影を拡大して並べても見分けがつかなかった（`aux-SCN-MEGA-FIX-BVH-QUEUE-VSM-probe-lod{2,4}.txt`、`scene-capture/SCN-MEGA-FIX-BVH-QUEUE-VSM-probe-lod{2,4}/`）。一覧の容量（VRAM）は変えていない。
  - `verify-SCN-MEGA-FIX-BVH-QUEUE-VSM-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。シェーダーは BOM を除いた写しで `glslc` によりコンパイルを確かめた。
  - `verify-SCN-MEGA-FIX-BVH-QUEUE-VSM-2.txt`: 100 万本 horizon・overview の撮影 result=pass（EXIT=0）。`VSM_MEGA_CULL` は horizon clusters=723,293・overview 657,472 で全行 overflow=0、以後は `VSM_CACHE` の cached だけ（静止で再描画なし）。`MEGA_CAPACITY` は全行 `bvh_overflow=0`・`draw_dropped=0`、`[ERROR]` 0。PNG を開き、地平線の手前まで遠くのバナナにも影が落ち、近くの地面の影が欠けていないことを確かめた。
  - `verify-SCN-MEGA-FIX-BVH-QUEUE-VSM-3.txt`: `VSM_MEGA_CULL overflow=0` の検査 EXIT=0。
  - `verify-SCN-MEGA-FIX-BVH-QUEUE-VSM-4.txt`: 起動画面 default の決定的撮影の比較 result=pass（psnr 57.68 dB、輝度差 0.0037、EXIT=0）。`VSM_MEGA_CULL` は全行 overflow=0。PNG は家・球・岩・地面・影が以前どおり。
- Next: TASKS.md の次の未完（SCN-MEGA-LAYOUT-SEA）。
- Notes: 影の LOD の許容 4 texel は内部の値の選択（2 は一覧を 25 万件ほど広げれば収まるが VRAM が約 135 MB 増える）。overview の中ほど（画面の中央のやや上）の暗い斑は、溢れ 0 になった今回も前の項目の撮影と同じ場所に出るので溢れ由来ではない（配置の疎密と見ている。配置は SCN-MEGA-LAYOUT-SEA で変わるので、その撮影で見直す）。性能は測っていない（SCN-MEGA-ACCEPT-1M で判断）。試験の追加はしていない。

### SCN-MEGA-FIX-BVH-QUEUE-VSM 再検証（2026-10-14、反復 5・run 20261014-102529）
- Done: 評価の差し戻し（再検証の証拠の不足）に対し、コードは変えずに verify 4 本を新しく回し直して証拠を残した。反復 4 の `verify-...-1〜4.txt` は残し、今回の出力は `verify-SCN-MEGA-FIX-BVH-QUEUE-VSM-5〜8.txt`（verify の 1〜4 番に対応）。
  - `-5.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `-6.txt`: 100 万本 horizon・overview の撮影 result=pass（bananas=1000000、EXIT=0）。撮影先は消してから撮り直した。`VSM_MEGA_CULL` は horizon clusters=723,293・overview 657,472 で全行 overflow=0、`[ERROR]` 0、`bvh_overflow` は全行 0。PNG を開き、近くの床にバナナの影が落ち、遠くの隙間も影で暗いことを確かめた。
  - `-7.txt`: `VSM_MEGA_CULL overflow=0` の検査 EXIT=0。
  - `-8.txt`: 起動画面 default の決定的撮影の比較 result=pass（psnr 57.68 dB、輝度差 0.0037、EXIT=0）。PNG は家・球・岩・地面・影が以前どおり。
- Next: TASKS.md の次の未完（SCN-MEGA-LAYOUT-SEA）。
- Notes: overview 中ほどの暗い斑は反復 4 の Notes どおり残る（溢れ由来ではない。SCN-MEGA-LAYOUT-SEA の撮影で見直す）。試験の追加はしていない。

### SCN-MEGA-LAYOUT-SEA（2026-10-14、反復 6・run 20261014-102529）
- Done: 主の描画（SCN-MEGA-FIX-BVH-QUEUE-MAIN）と影（SCN-MEGA-FIX-BVH-QUEUE-VSM）の BVH の列の直しの後に、配置のコードは変えずに同じ撮影を見直して閉じた。
  - `verify-SCN-MEGA-LAYOUT-SEA-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-MEGA-LAYOUT-SEA-2.txt`: 100 万本 horizon・overview の撮影 result=pass（bananas=1000000、EXIT=0）。撮影先は消してから撮り直した。`MEGA_BANANAS_LAYOUT radius=355.5 near_radius=32.9 mid_radius=81.8 near=141467 mid=174484 far=683269 near_density=41.6 mid_density=9.9 far_density=1.82 tiles=1640 batches=1 deficit=0 build_ms=1333.7 far_plane=1000`（far が半径を超える）。両ログとも `bvh_overflow=0`・`draw_dropped=0`・`VSM_MEGA_CULL overflow=0`、`[ERROR]` 0。
  - PNG を開いて確かめた: 地平線までバナナが描かれ、以前の約 50 m より先の床の色の帯は無い。格子の並び・区画の境は見えない。overview 中ほどの暗い斑は手前の 3 つの山（z≈2.3〜3.5）と近い輪の重なりの影で、画面上の位置が山の位置と合う（溢れ由来・格子由来ではない）。
  - 1 万本の小さな円盤: 半径は `sqrt(本数 / 単位円盤の本数)` で本数から決まり、内訳の比は同じ（1 万本で約 35.5 m）。verify に無いので撮っていない。
- Next: TASKS.md の次の未完。
- Notes: horizon の地平線のすぐ下に 2〜3 画素の淡い線が残る（行 542〜544、色 146,135,123）。海の縁（中心から約 370 m）から床の端（800 m）までの床が、目の高さ 1.7 m からは俯角 0.12〜0.25° で約 2 画素に潰れて見えるもの。消すには海を床の端まで広げる必要があり、遠の密度 2/m² で 400 万本を超えるので、100 万本の配置では既知の限界として残す。試験の追加はしていない。

### SCN-MEGA-LAYOUT-SEA（2026-10-14、反復 1・run 20261014-122019）
- Done: 反復 6 の差し戻し（地平線の直下の床の線）を直して閉じた。床を、海の中の円盤（半径 = 海の半径 × 0.86、遠の密度が平らに続く所の外の端）と、その外から床の端までの輪に分け、海を置き始めたら輪を遠くの海の見た目の色に寄せる（木の床のアルベドに倍率 0.99・0.93・0.52 を掛ける。撮影で地平線の直下の行の平均に合わせた値）。本数・配置は変えていない。分ける半径が 150 m 未満（1 万本の約 35.5 m の円盤）のときは前と同じ 1 枚の平面のままで、1 万本の見た目は変わらない。バナナを置けないときも輪は木の床のまま。
  - `verify-SCN-MEGA-LAYOUT-SEA-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-MEGA-LAYOUT-SEA-2.txt`: 100 万本 horizon・overview の撮影 result=pass（bananas=1000000、EXIT=0）。撮影先は消してから撮り直した。`MEGA_BANANAS_LAYOUT radius=355.5 near_radius=32.9 mid_radius=81.8 near=141467 mid=174484 far=683269 near_density=41.6 mid_density=9.9 far_density=1.82 tiles=1640 batches=1 deficit=0 far_plane=1000`。両ログとも `bvh_overflow=0`・`draw_dropped=0`・`overflow=0`、`[ERROR]` 0。
  - PNG を開いて確かめた: 前の灰色の線（horizon の y=542〜543 が RGB 146,135,123、overview の y=257〜260 が 143,133,121）は無くなり、同じ行は horizon 146,131,76・overview 144,129,75（中央の画素）。行の線形の平均は horizon の y=542〜543 が 145,129,75、すぐ下の y=544〜549 が 149,132,77、overview の y=257〜260 が 143,128,74、すぐ下の y=261〜266 が 145,129,78 で、海の縁と同じ色の続きに見える。床の端・格子の並び・区画の境は見えない。
  - 計算量: 配置が持つ格子は、海の外接正方形の区画の表（一辺 16 m、46×46）と処理中の区画の近傍の升だけで、床全体の格子は持たない（前の反復から変えていない）。
- Next: TASKS.md の次の未完。
- Notes: 輪の倍率は撮影 2 回の試し撮り（`scene-capture/SCN-MEGA-LAYOUT-SEA-tune1`・`-tune2`）で合わせた内部の値。青は倍率 0.2 で 0 に潰れたので 0.5 付近へ上げた。輪と円盤の境は原点が中心で、海の中心（0.2, 2.8）から最大 3 m ずれるが、境は遠の平らな所の中にあり見えない。試験の追加はしていない。

### SCN-MEGA-LAYOUT-SEA（2026-10-14、反復 2・run 20261014-122019）
- Done: 反復 1 の差し戻し（輪を海の色に寄せても、地平線の直下に滑らかな 2〜4 画素の帯が残り、下のバナナの濃淡と区別できる）を直して閉じた。床の外側の輪を床の正方形の端（800 m）まで延ばさず、海の縁がいちばん外へゆれた所（半径 × 1.04）の 6 m 先で終える。その先は空（観測点の高さ 100 m の空は地平線の少し下まで空の色が続く）が海の縁へつながる。本数・配置・輪の色は変えていない。1 万本（分ける半径が 150 m 未満）は前と同じ 1 枚の平面のまま。
  - `verify-SCN-MEGA-LAYOUT-SEA-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-MEGA-LAYOUT-SEA-2.txt`: 100 万本 horizon・overview の撮影 result=pass（bananas=1000000、EXIT=0）。撮影先は消してから撮り直した。`MEGA_BANANAS_LAYOUT radius=355.5 near_radius=32.9 mid_radius=81.8 near=141467 mid=174484 far=683269 near_density=41.6 mid_density=9.9 far_density=1.82 tiles=1640 batches=1 deficit=0 far_plane=1000`。両ログとも `bvh_overflow`・`draw_dropped`・`overflow` に 0 でない値なし、`[ERROR]` 0。
  - PNG を開いて確かめた: 前の帯の行（horizon の y=542〜543、overview の y=257〜260）は空の続きになり、行の輝度の横方向の標準偏差が horizon 1.5・overview 2.6〜2.7 で、すぐ上の空の行と同じ（中央の画素 horizon 110,112,121、overview 110,112,120）。その下の最初の行からバナナの濃淡（標準偏差 15〜50）が始まり、滑らかな帯は無い。床の端・格子の並び・区画の境は見えない。
- Next: TASKS.md の次の未完。
- Notes: 床の端の 6 m の余白は、海の中心のずれ（最大 2.8 m）と縁のバナナの長さを覆う内部の値（370 m 先の 6 m は 1 画素に満たない）。overview の地平線の手前に、遠の疎な所の木の床の隙間が灰色の斑として混ざるのは前の反復と同じ（バナナと混ざった濃淡で帯ではない）。試験の追加はしていない。

### SCN-MEGA-LAYOUT-MOUNDS（2026-10-14、反復 3・run 20261014-122019）
- Done: 100 万本級（本数 50 万以上・マスクなし）のときだけ、海の中に落とし込みで積んだ大きな塚を 4 つ置く（合計で全体の 1/10、1M では 10 万本）。番号 0 が検算用の塚（2 万本）、残り 3 つは各約 2.67 万本。塚は散布の Object の入力（山の直後）に並び、`BananaFieldBuilder::Step` の予算の中で積む（1 本ずつの記録は検算用の塚だけ）。1 万本（本数 50 万未満）は塚を置かず、配置・見た目は前のまま。
  - 落とし込み: 落とす位置を中心から正規分布で 1 つ引き（最大半径 2.4σ の外は引き直す。切り詰めると縁に壁ができる）、その周り 0.3 m の 8 候補のうち最も低く収まる所に、下の形へ初めて触れる高さ（2 mm 沈める）で置く。塚全体から候補を引く山のやり方だと、低い所から均一に埋まって頂の平らな円柱になった（最初の試し撮り）。σ は 260 本の山と相似な値の 1.3 倍（2 万本で 1.66 m・2.67 万本で 1.83 m）で、頂は 1.76〜1.98 m、斜面は約 32°。
  - 速さ: 塚は位置を 5 mm の升の中心へ、向きを 128 段・大きさを 8 段へ丸め、段ごとに全頂点（59,755）から作った足跡（升ごとの頂点の最低・最高の高さ）を使い回す。外部の検算と同じ升・同じ点で高さを数えるので、塚の中の隙間は全部 -2.0 mm。配置全体の時間は build_ms 3,809（前の海だけで約 1,334）。
  - 海は塚の周り（最大半径 + 候補の半径 + バナナ 2 本分）を空け、海の本数から塚の本数を引く（半径 355.5 → 337.2 m）。塚を足すと縁の区画で 2 本置けなくなった（deficit=2）ので、引く回数の上限までに位置が 1 つも無い区画だけ、密度の上限を標本の最大 × 1.25 に締めてもう一巡引く（置けた配置の乱数の並びは変えない）。
  - `MEGA_BANANAS_MOUND_GAP` を 3 つの山（kind=pile）と 4 つの塚（kind=mound）の全部で出す。`Scripts/VerifyBananaPile.py` に `--decimate N`（隙間を確かめるのを N 本に 1 本にし、下の形は全部のバナナの全頂点で積む）を足し、高さの格子を辞書から配列にした。
- 検証（`.harness/runs/20261014-122019/`）:
  - `verify-SCN-MEGA-LAYOUT-MOUNDS-1.txt`: RelWithDebInfo の Game のビルド EXIT=0、error 0。
  - `verify-SCN-MEGA-LAYOUT-MOUNDS-2.txt`: 100 万本 horizon の撮影 result=pass（bananas=1000000、EXIT=0）。撮影先は消してから撮り直した。ログ: `MEGA_BANANAS_PLACED count=1000000 pile=780 mound=100000 mound_sites=4 field=899220`、`MEGA_BANANAS_LAYOUT radius=337.2 ... deficit=0 build_ms=3808.6`。塚 4 つの `MEGA_BANANAS_MOUND_GAP` は max_gap_mm=-2.000（塚 5 は -1.999）・floating=0、山 3 つは max_gap_mm 0.172 / 0.043 / -0.952・floating=0。`MEGA_BANANAS_PILE_ITEM` は pile=3（検算用の塚）の 20,000 行だけ。`bvh_overflow`・`draw_dropped`・`visible_list_overflow`・影の `overflow` は全行 0、`[ERROR]` 0。
  - `verify-SCN-MEGA-LAYOUT-MOUNDS-3.txt`: `VerifyBananaPile.py --decimate 30` は検算用の塚の 20,000 本を全頂点で積み、667 本を確かめて floating=0・penetrating=0・supported=667、隙間は -2.0 mm、頂 1.7554 m（エンジンの値と一致）、EXIT=0（約 32 秒）。
  - PNG を開いて確かめた: 地平線の手前に 4 つの丸い塚（30〜75 m 先）が重ならずに並び、裾は海へなじむ。塚の部分を 4 倍に拡大しても、輪郭から離れた個体・浮いて見える個体は無い。地平線までの海・床の帯が無いことは前のまま。
- Next: TASKS.md の次の未完。
- Notes: 塚の姿勢の段数・候補の数と半径・σ の倍率・区画の締めた上限の余裕 1.25 は内部の値（試し撮り 2 回で決めた）。補助の比較（`aux-SCN-MEGA-LAYOUT-MOUNDS-10k-compare.txt`）: 1 万本 overview を `SCN-MEGA-BASE-COOK` と比べると PSNR 39.2 dB（下限 45）・平均輝度の差 0.0019 で fail。配置はログで同一（山の 1 本ずつの記録のハッシュが一致し、海の near=520・mid=1933・far=6767・stacked=678・max_lift=0.124 も一致）で、差はバナナの影の輪郭に沿った細い線だけ（明暗が半々）。基準は SCN-MEGA-FIX-BVH-QUEUE-VSM（影の LOD の許容 1 → 4 texel）より前の 11:12 に撮ったもので、その後の 1 万本の比較は撮っていない。この基準で比べる項目は、基準の撮り直しが要る見込み。試験の追加はしていない。

### SCN-MEGA-ACCEPT-1M（2026-10-14、反復 4・run 20261014-122019。1 回目の判定）
- 判定: 不合格（4 つの負荷のどれも GPU・gt・rt の median と p95 の 16.6 ms 以下を満たさない）。測った値と内訳は `Docs/RenderingValidation/SceneLauncherAcceptance.md` の「100万本と混在の負荷の受入れ」。既定の本数（1 万）は変えない。
  - 静止は GPU 15.1 / 15.6 ms で合格。rt は 36.9 / 40.0 ms で、TLAS の入力の選び直し（`RT.BuildAccelerationStructures` 22 ms。更新 0 回でも 100 万席の距離を毎フレーム判定）と、変わった行が 0 でも MegaGeometry の表を毎フレームたどる処理（7.1 ms）が大半。gt は rt を待つ時間がほぼ全部。
  - カメラパスは GPU 86 ms（LightingPass 70 ms）、1% 移動・ストリーミングは GPU 31〜33 ms・rt 49〜51 ms。ストリーミングの p95 の山は区画の出し入れごとの表の全量の作り直し。
- 直す先: `SCN-MEGA-FIX-RT-SELECT`・`TLAS-REBUILD`・`SCENE-TABLES-STEADY`・`MIXED-DEFICIT`・`VISIBLE-LIST`・`STREAM-TABLES-DELTA`・`STRESS-GPU` を TASKS.md に足した。100万本は過程で、目的はランチャーと各シーンの整備というユーザーの指示から、これらはシーンの項目（SCN-ACCEPT）の後に置き、SCN-MEGA-ACCEPT-1M はその後で測り直す。撮影で見つけた空の階調の段は `SCN-FIX-SKY-BANDING` として SCN-ACCEPT の前に置いた。
- Next: SCN-GEN-BUILDINGS-1（シーンの項目へ戻る）。

