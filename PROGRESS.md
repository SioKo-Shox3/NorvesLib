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

- 画: 昼・夜の PNG を開いた。昼は、手前の地面に複製した岩の群れと小屋、その奥にテクスチャの板（24 枚）、さらに奥にジオメトリの格子（小屋・岩・球 300 個）が並び、穴・欠け・板の抜けは無い。夜は電球が手前の岩の群れと地面の板を照らし、岩の間の影に欠けは見えない。板・ジオメトリの格子は電球の光の範囲の外で暗い。板は遠景（板 1 枚が横 40 px ほど）で、テクスチャの解像度の崩れはこの視点では判定できない（近景の確認は段2の `-StressTextures`）。
- Notes: (1) 8GB 級の上限（6500 MB）では VT・ジオメトリの使用量が目標よりずっと小さく（8 / 29〜34 MB）、追い出しは起きなかった。この視点で要るタイル・ページが少ないため。予算を縛るのは段2・段5の絞った上限（`VTG2-STRESS-TEXTURES`・段5）で確かめ済み。(2) `heap_usage` 約 1.9 GB のうち `non_pool_mb` が約 1.15 GB、VSM のプールが 427 MB。(3) 夜のジオメトリの格子は光源の外なので、点光源の影の負荷は地面の近くの複製 300 個が受け持つ（`point_requested` 14）。(4) 昼の `VSM_PAGES` の要求は最大 31（うち点光源 14）。
- 検証: ビルド（`verify-VTG9-STRESS-ALL-5.txt`、EXIT=0）。昼・夜の撮影（`verify-VTG9-STRESS-ALL-6.txt`・`-7.txt`、result=pass、EXIT=0）。変更前の昼の挙動（併用がジオメトリの手前の行だけを映し、複製が効かない）は `try0-day.txt`・`VTG9-STRESS-ALL-day-try0/` に残した。
- Next: TASKS.md の次の `todo`。
