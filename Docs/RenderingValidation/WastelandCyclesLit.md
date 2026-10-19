# 実材質の地形をエンジンと Cycles で描き、照明を比べる

荒野の正解の絵（SCN-WLR-*）で、実際の地表の材質（SCN-WLR-MATERIAL-ALBEDO・NORMAL で照合した再現）の地形を、共通の照明
（一様な空と太陽）でエンジンと Blender の Cycles の両方で描き、露出前の HDR を領域ごとに比べた記録。比べるだけで、エンジンの
照明と材質は直していない。判定日: 2026-10-19。ブランチ `feature/scene-launcher`。タスク SCN-WLR-CYCLES-LIT-A。

## 道具

| 何 | どこ | 何をするか |
|---|---|---|
| `Scripts/Terrain/BlenderReference.py --lit` | Blender 5.2.2 をヘッドレスで起動 | 視点ごとに 2 段で描く。1 段目は画素ごとの材質（`TerrainAlbedo.osl` のアルベド・法線・粗さ・材質の AO と深度）を `<撮影>/material/` に、2 段目はその材質を当たりに使う Cycles の照明つきの描画を `<撮影>/cycles/` に書く。視点を分けて描けるよう、同じ台本・値の表・撮影なら前の視点の記録を残して足す |
| `Scripts/Terrain/ReferenceCheck.py --lit` | | 撮影・材質・Cycles の入力と標本の位置を検査し、領域ごとの表 `lit_check.json`、比較画像 `lit_<視点>.png`（エンジン｜Cycles）、領域と比の画像 `lit_regions_<視点>.png` を書く |

材質の再現と照合の基準は `WastelandMaterialAlbedo.md`・`WastelandMaterialNormal.md`、照明・Cycles の設定・領域・収束の定義は
`WastelandCyclesGray.md` と同じ（材質だけが灰色から実材質に変わる）。

## 条件

- エンジン: `CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Hdr -Aov -Views ref_cliff_01,ref_far_01,ref_ground_01
  -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod`（地形だけ・共通の照明・最細の LOD）。3 視点とも
  撮った時点でジオメトリのページの進行中 0・追い出し 0、描くクラスタの溢れ 0。
- Cycles: 地形はカメラから水平 4 km（影の届く範囲）の区画（ref_cliff_01 141 区画・1.21 億三角形、ref_far_01 148 区画・1.31 億、
  ref_ground_01 124 区画・0.87 億）。当たりの材質は OSL（アルベド・法線・粗さ）。材質の AO は Cycles に渡さない
  （経路の追跡が遮蔽を幾何から解く）。
- 標本の位置: 画素ごとの材質の深度とエンジンの深度から求めたずれが 0.05 画素を超えた視点は、カメラをずらして描き直した
  （ref_cliff_01 (+0.243, +0.027)・ref_far_01 (−0.107, −0.158) 画素。残りのずれは 3 視点とも 0.02 画素以下）。Cycles の描画は
  材質と同じずらしで描く。
- 比較画像: エンジンの撮影の `hdr.json` の表示変換（SCN-WLR-DISPLAY-TRANSFORM の `DisplayTransform.py`。同じプリエクスポージャ・
  トーンマップ・グレーディング・ビネット）を両方に掛けた。Cycles の空（透明の背景）は共通の照明の一様な空の放射輝度
  （RGB 4000・5000・6500）で埋める。

## 収束

`WastelandCyclesGray.md` と同じ基準（種を変えた 2 本の領域の平均の差 1 % 以下・画素の差の RMS 0.10 以下、1 本あたり 512〜2048
サンプル、1 視点 2400 秒）。

| 視点 | 1 本あたり | 止まった理由 | 描画 | 読み込み |
|---|---|---|---|---|
| ref_cliff_01 | 2048 | 上限 | 1453 秒 | 46 秒 |
| ref_far_01 | 512 | 収束 | 567 秒 | 50 秒 |
| ref_ground_01 | 512 | 収束 | 461 秒 | 34 秒 |

灰色の材質では ref_cliff_01 も 512 で収束したが、実材質では影の中の地面（2375 画素。平均の差 0.041・画素の RMS 1.98）と
影の崖（71400 画素。0.0005・0.121）が 2048 でも基準に入らない（未判定）。影の崖は平均の差が小さく、平均の値はほぼ決まっている。

## 結果（露出前の HDR、輝度 Y の線形の平均、cd/m²）

「AO の見込み」は、エンジンだけが掛ける材質の AO（`lighting.frag`: 直接光 × mix(1, AO, 0.3)、環境光 × AO）を Cycles の太陽の組・
空の組へ掛けた値に対するエンジンの比（原因の見立て用。太陽の組の照り返しにも直接光の係数を掛ける近似）。

| 視点 | 領域 | 画素 | 判定 | エンジン | Cycles | 比 | RGB の比 | Cycles の太陽 / 間接 | 材質の AO | AO の見込みとの比 |
|---|---|---|---|---|---|---|---|---|---|---|
| ref_cliff_01 | 日なたの地面 | 362760 | 判定 | 2295 | 2940 | 0.781 | 0.808・0.768・0.721 | 72 % / 2 % | 0.643 | 0.953 |
| ref_cliff_01 | 影の中の地面 | 2375 | 未判定（収束しない） | 1018 | 867 | 1.174 | 1.255・1.144・1.010 | 9 % / 7 % | 0.693 | 1.647 |
| ref_cliff_01 | 日なたの崖 | 145844 | 判定 | 5267 | 5529 | 0.953 | 0.926・0.969・0.989 | 89 % / 5 % | 0.862 | 1.000 |
| ref_cliff_01 | 影の崖 | 71400 | 未判定（収束しない） | 1271 | 718 | 1.771 | 1.553・1.905・1.940 | 18 % / 29 % | 0.943 | 1.864 |
| ref_cliff_01 | 遠景 | 0 | 未判定（画素なし） | | | | | | | |
| ref_far_01 | 日なたの地面 | 662004 | 判定 | 4506 | 5489 | 0.821 | 0.848・0.808・0.750 | 84 % / 1 % | 0.663 | 0.960 |
| ref_far_01 | 影の中の地面 | 0 | 未判定（画素なし） | | | | | | | |
| ref_far_01 | 日なたの崖 | 511 | 未判定（画素が足りない） | 1888 | 2852 | 0.662 | 0.643・0.675・0.682 | 78 % / 9 % | 0.626 | 0.797 |
| ref_far_01 | 影の崖 | 0 | 未判定（画素なし） | | | | | | | |
| ref_far_01 | 遠景 | 344162 | 判定 | 4170 | 4522 | 0.922 | 0.883・0.948・0.940 | 84 % / 9 % | 0.840 | 0.994 |
| ref_ground_01 | 日なたの地面 | 1210607 | 判定 | 4175 | 5154 | 0.810 | 0.840・0.794・0.742 | 83 % / 1 % | 0.622 | 0.965 |
| ref_ground_01 | 影の中の地面 | 0 | 未判定（画素なし） | | | | | | | |
| ref_ground_01 | 日なたの崖 | 2105 | 判定 | 3997 | 4675 | 0.855 | 0.849・0.860・0.827 | 84 % / 5 % | 0.799 | 0.933 |
| ref_ground_01 | 影の崖 | 39 | 未判定（画素が足りない） | 1073 | 973 | 1.103 | 0.980・1.186・1.212 | 24 % / 27 % | 0.795 | 1.330 |
| ref_ground_01 | 遠景 | 15795 | 判定 | 4051 | 4493 | 0.902 | 0.886・0.913・0.885 | 81 % / 5 % | 0.819 | 0.978 |

領域ごとのアルベドの平均は、エンジンの GBuffer と再現（画素ごとの材質）で全領域 2 % 以内（最大は ref_cliff_01 の影の中の地面の
赤 0.301 対 0.308）。材質の違いは比の元にならない。

比較画像（`lit_<視点>.png`）を開いて見たこと:

- 3 視点とも幾何・地層の筋・分類の斑が同じ位置に出る。
- 日なたの地面はエンジンが暗く、模様（砂と岩肌の斑・小石）の明暗が濃い。Cycles は砂の帯が明るく淡い。
- 崖はエンジンの陰（太陽に背を向けた面・溝の奥）が浅く、全体が黄色く平たく見える。Cycles は溝の奥が暗く赤い（ref_far_01・
  ref_cliff_01 の崖で目立つ）。

## 差の大きい所の原因の見立て

撮影の Game のログは 3 視点とも `RT_SCENE instances=0 excluded=198` で、灰色の比較（`WastelandCyclesGray.md`）と同じく
レイトレーシングのシーンに地形が入っていない（RTGI は遮蔽も照り返しも無い空を返し、GTAO も重ねない）。

1. **材質の AO（日なたの地面の −18〜−22 % の主因）**: エンジンは材質の AO（ORM の値。日なたの地面で平均 0.62〜0.66）を直接光に
   0.3 の重みで、環境光にそのまま掛ける（`lighting.frag` の `directAO`・`ddgiAmbientAO`）。Cycles は材質の AO を使わない。
   同じ AO を Cycles の太陽の組・空の組へ掛けると、比は 0.78〜0.82 から 0.95〜0.97 に寄り、灰色の材質の比（ref_cliff_01 0.953・
   ref_far_01 0.975）と同じ大きさの残りになる。青ほど比が低い（0.72〜0.75）のは、青い空の光に AO がそのまま掛かるため。
   平らな砂地の AO が 0.6 台と強いこと、直接光にも掛けることの両方が候補（AO の値の焼き方と掛け方は、ここでは確かめていない）。
2. **影の崖が明るい（+77 %）**: 灰色の比較の +57 % と同じ元（空の遮蔽と照り返しが無い）。材質の AO を見込んでも 1.86 で、
   AO では説明できない。実材質では Cycles の影の崖の照り返しの割合が 29 % と大きく（赤い岩の照り返し）、色の比が赤 1.55・
   緑青 1.9 と分かれる（エンジンの環境光は青い空だけ）。比較画像で崖の陰が浅く黄色く見えるのはこの分。
3. **残り（AO の見込みとの比 0.93〜0.97）**: 灰色の比較と同じく、照り返しの欠け（日なたの崖の Cycles の間接光 5 %）と、かすめる
   日差しで暗いこと（灰色の比較の 4）。ref_cliff_01 の日なたの崖は AO の見込みで 1.000 に揃う。
4. **遠景（0.90〜0.92）**: AO の見込みで 0.98〜0.99。赤の比が低い（0.88）のは遠景の赤い崖の照り返しの欠けと見ている。

## 未判定と限界

- 未判定: ref_cliff_01 の影の中の地面・影の崖（2048 サンプルで収束しない）と遠景（画素なし）、ref_far_01 の影の中の地面・
  影の崖（画素なし）と日なたの崖（511 画素）、ref_ground_01 の影の中の地面（画素なし）と影の崖（39 画素）。
- 「AO の見込み」は太陽の組の照り返しにも直接光の係数を掛ける近似で、合否には使わない。
- BC5・BC7 の圧縮と GPU の異方性の標本は、材質の照合（`WastelandMaterialAlbedo.md`・`WastelandMaterialNormal.md`）の範囲で
  再現している。Cycles はカメラから水平 4 km の外の区画を読まない（`WastelandCyclesGray.md` と同じ）。
- 比較画像の Cycles の空は一様な空で埋めており、エンジンの空（大気の散乱）とは違う。地形の部分だけを見る。

## 記録の場所

- 撮影: `.harness/runs/scene-capture/SCN-WLR-CYCLES-LIT/`（エンジンの HDR・AOV・ログ、`material/`（`<視点>.albedo.pfm`・
  `normal`・`roughness`・`occlusion`・`depth`、`inputs.json`）、`cycles/`（`<視点>.cycles.exr` に合成・太陽・空・直接・間接、
  `<視点>.aux.exr` に見え方・n·l・法線・α、照合の読む PFM、`<視点>.cycles.json` に設定と収束の経過、`inputs.json`）、
  `lit_check.json`、`lit_<視点>.png`、`lit_regions_<視点>.png`）。Cycles の EXR は SCN-WLR-TONEMAP-EVAL・SCN-WLR-GAP-LIST で使う。

## 手順

```
# エンジン（照合の読む撮影。3 視点で約 5 分）
powershell -Command "& ./Scripts/CaptureScene.ps1 -Scene wasteland -OutDir .harness/runs/scene-capture/SCN-WLR-CYCLES-LIT `
  -Views ref_cliff_01,ref_far_01,ref_ground_01 -Configuration RelWithDebInfo -Build -Width 1920 -Height 1080 -Hdr -Aov -LaunchPerView `
  -GameArgs @('--wasteland-common-light','--wasteland-hide=all','--wasteland-finest-lod')"
# Blender（1 視点ずつ。材質と Cycles で ref_cliff_01 約 40 分、ref_far_01 約 22 分、ref_ground_01 約 10 分）
python Scripts/Terrain/BlenderReference.py --lit --views ref_cliff_01 --camera-shift-hint 0.243,0.027 `
  --mesh .harness/runs/scene-capture/SCN-WLR-BLENDER-TERRAIN-mesh --capture .harness/runs/scene-capture/SCN-WLR-CYCLES-LIT
python Scripts/Terrain/BlenderReference.py --lit --views ref_far_01 `
  --mesh .harness/runs/scene-capture/SCN-WLR-BLENDER-TERRAIN-mesh --capture .harness/runs/scene-capture/SCN-WLR-CYCLES-LIT
python Scripts/Terrain/BlenderReference.py --lit --views ref_ground_01 `
  --mesh .harness/runs/scene-capture/SCN-WLR-BLENDER-TERRAIN-mesh --capture .harness/runs/scene-capture/SCN-WLR-CYCLES-LIT
# 照合
python Scripts/Terrain/ReferenceCheck.py --lit --views ref_cliff_01,ref_far_01,ref_ground_01 `
  --capture .harness/runs/scene-capture/SCN-WLR-CYCLES-LIT --verify
```

材質だけを描き直すときは `--material-only`、Cycles だけを描き直すときは `--skip-material` を足す。
