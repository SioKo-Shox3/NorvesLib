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

## G5 地形の表示と衝突（2026-10-08）

HeightFieldを描画と物理で共有し、通常メッシュへの変換、LOD間引きとskirt、ray/球・箱・capsule overlap/capsule sweepを追加した。Static/Kinematic地形を既存SceneQuery経由でキャラクターとカメラから参照する。--terrain-smokeは中央の平地から丘へ歩ける明示起動のシーン。既定起動は維持する。

関連ソースの構文確認と地形メッシュ・ray・capsule sweepのローカルスモークが成功した。CIは起動していない。G5は層材質・cook・草の実装を継続する。
