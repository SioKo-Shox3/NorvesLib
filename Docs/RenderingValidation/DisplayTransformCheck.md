# 表示変換の再現と照合

エンジンの表示変換（露出・トーンマップ・グレーディング・ビネット・sRGB の出力）を `Scripts/Terrain/DisplayTransform.py` に再現し、
エンジンの撮影と照らした記録。荒野の正解の絵の比較（SCN-WLR-*）では、エンジンの HDR も Cycles の EXR もこの再現だけで表示の値へ写す
（Blender の AgX・Filmic は使わない）。

## 再現の範囲

| 段 | エンジン | 再現 |
|---|---|---|
| 露出 | ライティングでシーンの色へプリエクスポージャを掛ける（RGBA16F） | 放射輝度 × `pre_exposure` を半精度へ丸める |
| トーンマップ | `tonemapping.frag` の 6 つの演算子。ACES 2.0 SDR は `Aces20SdrRec709.lut3d`（65³、log2 の shaper）の三線形補間 | 同じ式。LUT は同じ shaper の座標で、補間の重みを副テクセルの精度 8 bit（1/256）へ丸める |
| グレーディング | カラーフィルター・明度、式 0（View の設定）か式 1（カメラの差し替え）、ACES 2.0 LUT には掛けない | 同じ式と順 |
| ビネット | トーンマップの中のビネットと `VignettePass` | 画素の中心の UV で同じ式 |
| 中間の保存 | ToneMappedColor・VignetteOutput は RGBA16F | 各段の後に半精度へ丸める |
| 出力 | sRGB の符号化（ハードウェアの sRGB 形式か `blit.frag`）と 8 bit への丸め | 同じ符号化と最近接への丸め |

ブルーム（レンズダートを含む）と FXAA は空間の処理なので再現に入れない。見た目の 3D LUT・フィルムグレイン・色収差が掛かる撮影は
`UnsupportedDisplay` で拒む。

撮影の `<視点>.hdr.json` の `post` に、ポストのパスがそのフレームで実際に使った値（演算子・グレーディングの式と値・2 つのビネット・
ブルームと FXAA の有無・色の階段の入力か・sRGB の符号化の経路）が入り、再現はこの値を使う。

## 照合の撮影

開発ビルドの Game の `--display-check=scene|ramp` はブルームと FXAA のパスを外す。`ramp` はさらにトーンマップの入力を色の階段
（出力を横 12 × 縦 7 の区画に分け、列 i の明るさ 2^(i−8)、行は R・G・B・C・M・Y・灰）にする（`ToneMappingPass::SetDisplayRampInput`）。

検査は `python Scripts/Terrain/ReferenceCheck.py --display --capture .harness/runs/scene-capture/SCN-WLR-DISPLAY-TRANSFORM --verify`。

| 組 | 撮影 | 基準 | 結果（2026-10-16、RelWithDebInfo、1920×1080） |
|---|---|---|---|
| `ramp-neutral` | 荒野 ref_cliff_01、`display-check=ramp`（NeutralLinear・グレーディングの式 1・差し替えのビネット） | 区画の縁 2 画素の内側の全画素が 1 段以内 | 全画素 1 段以内、完全一致 91.8 % |
| `ramp-aces` | sandbox overview、`tone-map=aces20-lut,display-check=ramp`（ACES 2.0 LUT・View の既定のビネット） | 同上 | 全画素 1 段以内、完全一致 91.0 % |
| `scene` | 荒野の基準の 8 視点、`display-check=scene -Hdr` | 視点ごとに画素の 99 % 以上が 1 段以内 | 8 視点とも全画素 1 段以内（最大 1 段）、完全一致 82.8〜86.4 % |

2026-10-22 から昼の荒野の曲線は View の既定の演算子（ACES Filmic）・中立のグレーディング・露出の補正 −1 EV になった
（`WastelandLookCamera.md`）。組の約束を次に改めた（`ReferenceCheck.py` の `DISPLAY_SETS`）:

- `ramp-neutral`: NeutralLinear の差し替えを撮れるのは夜の集落だけになったので、`night-village` の overview を `display-check=ramp -Hdr` で撮る
  （NeutralLinear・グレーディングの式 1 の夜の値・差し替えのビネット）。約束の演算子は 5 のまま。
- `scene`: 約束の演算子を 1（ACES Filmic）にした。`--tone-map` を付けずに撮る（付けると昼の荒野も View の演算子に従う）。
- `ramp-aces` は変えない。

検査は `python Scripts/Terrain/ReferenceCheck.py --display --capture .harness/runs/scene-capture/SCN-WL-LOOK-CAMERA/display-sets --verify`。

| 組 | 撮影 | 結果（2026-10-22、RelWithDebInfo、1920×1080、58dfac7a の Game.exe） |
|---|---|---|
| `ramp-neutral` | 夜の集落 overview、`display-check=ramp` | 全画素 1 段以内、完全一致 83.9 % |
| `ramp-aces` | sandbox overview、`tone-map=aces20-lut,display-check=ramp` | 全画素 1 段以内、完全一致 91.0 % |
| `scene` | 荒野の基準の 8 視点、`display-check=scene -Hdr`（ACES Filmic） | 8 視点とも全画素 1 段以内（最大 1 段）、完全一致 82.9〜86.0 % |

値を一つずらすと照合は落ちる（荒野 ref_cliff_01 のブルームなしの撮影で、1 段以内の画素がコントラスト 1.35→1.30 で 38 %、
彩度 1.2→1.15 で 61 %、ビネットを外すと 39 %、露出 +1/12 段で 0.03 %）。荒野の色温度 0.05 は 8 bit で 1 段未満の差しか生まず
1 段の照合では見分けられないが、0 にすると完全一致が 86.4 % から 51.0 % に落ち、R・B の平均の差が ±0.25〜0.27 段に開く。
