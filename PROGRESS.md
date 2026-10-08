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
