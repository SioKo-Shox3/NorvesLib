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
