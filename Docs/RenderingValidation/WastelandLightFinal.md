# 荒野の光の仕上げ（共通の照明の数値の確認と、既定の荒野の写真との並べ）

タスク SCN-WL-LIGHT-FINAL。判定日: 2026-10-22。ブランチ `feature/scene-launcher`。

1. 共通の照明（地形だけ・霞なし）: これまでの直しの仕様（`Scripts/Terrain/ExpectSpecs/` の rt-tlas-range・shadow-leak・
   shadow-pages・material-ao・rtgi-color）を、同じ Game.exe で撮り直した 1 つの置き場の撮影でまとめて判定し、後の直しが前の直しを
   壊していないかを確かめる。
2. 既定の荒野（前哨地・植物・礫・霞・雲あり）: Cycles の正解が無いので、6 視点を手本の写真と並べて所見を書く（数値の合否にしない）。
   歩きながらの連続撮影で、移動中の粒・残像・遮蔽の欠けを見る。

## 1. 共通の照明の仕様をまとめて通す

```
python Scripts/Terrain/ReferenceCheck.py --light-final --engine-root .harness/runs/scene-capture/SCN-WL-LIGHT-FINAL --verify
```

本体は `Scripts/Terrain/LightFinal.py`。置き場の組と、仕様ごとの比べ方:

| 組 | 起動引数（共通: `wasteland-common-light`・`wasteland-hide=all`・`wasteland-finest-lod`・`wasteland-aa=fxaa`） | 視点 |
|---|---|---|
| `gray/` | `wasteland-gray-material` | ref_cliff_01・ref_far_01 |
| `gray/engine-sun/`・`gray/engine-sky/` | 上に `common-light-case=sun`・`sky` | ref_cliff_01・ref_far_01 |
| `lit/`（照り返しの入を兼ねる） | なし | ref_cliff_01・ref_cliff_02・ref_ground_01・ref_far_01 |
| `bounce-off/` | `rtgi-bounce=off` | ref_cliff_01 |

| 仕様 | 比べ方 | 組 | 参照 |
|---|---|---|---|
| rt-tlas-range | `--gray` | gray | SCN-WLR-CYCLES-GRAY |
| shadow-leak | `--gray` | gray | SCN-WLR-CYCLES-GRAY |
| shadow-pages | `--lit` | lit | SCN-WLR-CYCLES-LIT |
| material-ao | `--lit` | lit | SCN-WLR-CYCLES-LIT |
| rtgi-color | `--bounce` | lit・bounce-off | SCN-WLR-CYCLES-LIT |

視点は仕様の行から取る。組の撮影が揃い、起動引数が組の条件どおりで、全部の組の Game.exe とエンジンの実装が同じで、
仕様ごとの比べ方（`--verify` の入力の照合を含む）が全部通れば合格。

### AA を FXAA に固定する理由

昼の荒野の既定の AA が TAA になった（SCN-WL-FAR-AA-MOTION）後に既定のまま撮ると、撮影の深度と HDR に TAA のジッタの端数が乗る。
`--lit` の材質の標本の位置の照合（0.1 画素以下）が ref_cliff_02 (−0.415, −0.244)・ref_far_01 (−0.153, −0.260)・
ref_ground_01 (−0.114, −0.286) 画素で落ちた。FXAA で撮り直すと (0.006, 0.032)・(0.022, −0.001)・(0.016, −0.007) 画素で、
仕様を書いたとき（既定が FXAA だったころ）の撮影と同じ値に戻った。仕様を書いた条件に揃えるため、組は `wasteland-aa=fxaa` で撮る。
`--wasteland-common-light` だけで撮るほかの比較（`--gray`・`--lit`・`--building`・`--plant` の単独の撮影）も同じずれを受ける。

### 参照の台本の同値

SCN-WLR-CYCLES-GRAY・LIT は BlenderReference.py の f2795eae（LIT は 5570cce8）の版で描いた。その後の建物・低木・建物の拡散の色・雲の
4 つの版（47aae7bf・fb7275f2・5e6c648b・af4cfd5f）は、どれも job の building・plant・cloud があるときだけ動く枝で、地形の
`--gray`・`--lit` の描画は変えていない（差分の削った行はすべて `if building`・`if plant` の中か、同じ式の分岐への付け替え）。
今の台本 af4cfd5f の同値の一覧（`ReferenceCheck.BLENDER_SCRIPT_EQUIVALENT_SHA256`）が雲の 1 つ前の版しか持っていなかったので、
前の版を全部足した。

### 結果（Game.exe 88925d4f…、コミット 0e235d6a で撮影）

| 仕様 | 行 | 値 | 範囲 | 閉じたときの値 | 合否 |
|---|---|---|---|---|---|
| rt-tlas-range | gray ref_cliff_01 影の崖 空の比 | 1.049 | 0.9〜1.1 | — | 合格 |
| rt-tlas-range | gray ref_far_01 遠景 空の比 | 1.044 | 0.9〜1.1 | — | 合格 |
| shadow-leak | gray ref_cliff_01 影の中の地面 見え方の食い違い | 0.0053 | ≤ 0.03 | 0.0197 | 合格 |
| shadow-leak | gray ref_cliff_01 影の崖 見え方の食い違い | 0.0125 | ≤ 0.05 | 0.0397 | 合格 |
| shadow-pages | lit ref_cliff_02 影の中の地面 見え方の食い違い | 0.0004 | ≤ 0.05 | — | 合格 |
| shadow-pages | lit ref_far_01 遠景 見え方の食い違い | 0.0302 | ≤ 0.05 | — | 合格 |
| material-ao | lit ref_ground_01 日なたの地面 比 | 0.983 | 0.92〜1.08 | — | 合格 |
| material-ao | lit ref_cliff_02 日なたの地面 比 | 0.943 | 0.92〜1.08 | — | 合格 |
| rtgi-color | ref_cliff_01 影の崖 色度の差 | **0.0201** | ≤ 0.02 | 0.0187 | **不合格** |
| rtgi-color | ref_cliff_01 影の中の地面 色度の差 | 0.0175 | ≤ 0.02 | 0.0081 | 合格 |

- rtgi-color の 2 行は、同じ Game.exe・同じ起動引数で撮り直しても同じ値（0.0201・0.0175）で、撮影の揺らぎではない。
  TAA のまま撮った組では 0.0192・0.0175（ジッタで標本が変わるだけ）。
- 照り返しの色度（エンジン → Cycles）: 影の崖 (r, g) = (0.797, 0.166) → (0.777, 0.177)、影の中の地面 (0.804, 0.153) →
  (0.787, 0.161)。閉じたとき（SCN-WL-RTGI-COLOR、10 月 20 日）のエンジンは影の崖 (0.796, 0.167)・影の中の地面 (0.795, 0.157)。
  エンジンの照り返しが Cycles より赤い向きは閉じたときと同じで、その後に影の中の地面で r が 0.009 赤くなった。
- 見立て（未確認）: SCN-WL-RTGI-AMOUNT が、光線の形の裏から当たって地面の下の空を拾う照り返し（青い）を読み飛ばすようにした。
  灰色ではこれで Cycles に近づいた（影の中の地面の色度の差 0.021 → 0.0037）が、実材質では青い分が消えて、当たりの色の近似
  （層の平均の色・当たりの鏡面は太陽だけ）が残す赤い偏りが表に出た。当たりの点で空の光を鏡面に返していないことは、
  Cycles の照り返しに色の付かない光が約 18 % 多く混ざる（SCN-WL-RTGI-COLOR の切り分け）向きと合う。
  どの変更で動いたかは、古いコミットの Game.exe を作り直さないと切り分けられない（作業ツリーでコミットを切り替えない）。

## 2. 既定の荒野を写真と並べる

撮影: `CaptureScene.ps1 -Scene wasteland -Hdr -Views ref_far_01,ref_far_02,ref_cliff_01,ref_cliff_02,ref_ground_01,ref_ground_02`
（`.harness/runs/scene-capture/SCN-WL-LIGHT-FINAL-views6/`）、並べた画は `python Scripts/Terrain/CompareReference.py --capture <撮影先>`
の `reference_<写真>.png`。値は線形の領域の平均の Y（写真の加工とホワイトバランスは分からないので、向きと大きさだけを見る）。

| 視点 | 空 写真 / 撮影 | 崖 写真 / 撮影 | 地面 写真 / 撮影 |
|---|---|---|---|
| far_01 | 0.830 / 0.650 | 0.240 / 0.461 | 0.136 / 0.373 |
| far_02 | 0.446 / 0.575 | 0.133 / 0.420 | 0.148 / 0.317 |
| cliff_01 | 0.527 / 0.313 | 0.140 / 0.222 | 0.137 / 0.141 |
| cliff_02 | 0.423 / 0.627 | 0.231 / 0.312 | 0.134 / 0.191 |
| ground_01 | 0.664 / 0.599 | 0.059 / 0.432 | 0.097 / 0.326 |
| ground_02 | 0.475 / 0.667 | 0.055 / 0.298 | 0.219 / 0.404 |

所見:

- 日なたの地面と崖の明るさ: 夕方の cliff_01 のほかは、撮影の地面が写真の 1.4〜3.4 倍、崖が 1.3〜3 倍明るい。地面は一様な明るい
  橙で、写真の地面の暗い赤茶と植生の暗い斑（低木の被覆）が無い。far_01・far_02・ground_01 は画面全体が明るく平たい。
- 谷と崖の影: far_01・far_02 は太陽が高く、崖の陰の面と日なたの面の差が写真より小さい（写真は崖の段の下の陰が濃い）。
  cliff_02 の台地の長い影と cliff_01 のビュートの陰は形も向きも写真と合い、影の中の地面は日なたより暗く灰がかる。
- 影の中の色: cliff_01・cliff_02 の影の中は灰褐色で、写真の影の中の青み（空の光）は弱い。共通の照明の rtgi-color の
  「照り返しが Cycles より赤い」と同じ向き。
- 空の青: cliff_01（写真 B 0.840、撮影 B 0.443）と ground_02 は写真の深い青に対し、撮影は白っぽく彩度の低い青。far_01 は写真の
  積雲が無く一面の薄い雲のベール（SCN-WLR-CLOUD-REF の被覆の差と同じ）。
- 遠景の霞: far_01・ground_01・ground_02 の 5 km 先の崖が桃色に白んで、写真より手前の崖との明るさの差が小さい。
- 前哨地の照り返し: 前哨地の視点（`outpost`）は手本の写真が無いので、並べずに撮影だけを見た。日の当たる日干し煉瓦の壁と
  石積みの小屋は地面と同じ暖かい色で、陰の面は日なたより一段暗い。陰の面の照り返しの暖かさは地面の照り返しと揃って見え、
  明らかな欠けは無い。前哨地の踏み固めた地面の灰褐色と砂の橙が、縁のくっきりした斑になって迷彩の模様のように見える
  （光でなく地表の塗り分け）。

所見のうち、空の白さ・遠景の桃色の霞・地面と崖が写真より明るく彩度が高いことは、既にある SCN-WL-SKY-AERIAL の差と同じで、
そこで扱う。影の中の色が赤い向きは共通の照明の rtgi-color の不合格と同じで、SCN-WL-RTGI-COLOR-RESIDUAL で扱う。

## 移動中の見え方

`ground` の視点から固定の台本 `Scripts/CameraScripts/wasteland-gravel-walk.txt` で前後へ歩き、既定の AA（TAA）で 360〜600
フレームを 12 フレームおきに撮った（`walk/`。荒野にカメラパスは無いので、カメラの台本の歩きで代える）。

- 6 枚を並べた画（`walk/sheet.png`）と、504・516・528 フレームの手前の拡大（`walk/crop_near.png`）を開いた。低木・礫・草は
  輪郭がくっきりしたままで、引きずる残像・二重像は無い。低木の足元の影と遮蔽は歩いても株に付いて動き、欠けや遅れて出る
  暗がりは見えない。地面と影の中にちらつく粒は目立たない。
- 空の雲は、明るさが平らな段になって等高線のように並ぶ階調の段がはっきり見える（ref_far_02 にも同じ段がある）。光の仕上げとは
  別の欠陥として SCN-WL-CLOUD-BANDING を足した。

## 撮影

| 撮影 | 置き場 |
|---|---|
| 共通の照明の組（FXAA） | `.harness/runs/scene-capture/SCN-WL-LIGHT-FINAL/` |
| 共通の照明の組（既定の TAA。材質の標本の位置で落ちた組） | `.harness/runs/scene-capture/SCN-WL-LIGHT-FINAL-taa/` |
| 照り返しの入・切の撮り直し | `.harness/runs/scene-capture/SCN-WL-LIGHT-FINAL-repeat/` |
| 既定の荒野の 6 視点・前哨地・歩き | `.harness/runs/scene-capture/SCN-WL-LIGHT-FINAL-views6/`（`extra/`・`walk/`） |
