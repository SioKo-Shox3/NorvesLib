# R8 S8 色管理：ACES 2.0 SDRのベイクLUT

日付: 2026-09-26 ／ 状態: 決定済み

## 決定（S8）

表示変換はOCIOの厳密変換をオフラインで3D LUTへ焼き込み、ランタイムはそのLUTを引く（ベイクLUT）。エンジンにOCIOを
組み込まず、GPUでは3D textureの三線形補間だけを使う。LUTの値はsRGBの符号化を外したdisplay-linearにし、既存のsRGB提示
（sRGB形式への書き込み）をそのまま使う。既定のトーンマップと既定の起動（Rendering3DTest）は変えない。

## 変換の固定値

| 項目 | 値 |
|---|---|
| OCIO | 2.5.2（pipの`opencolorio`、要件は2.4以上） |
| config | 組み込み `studio-config-v4.0.0_aces-v2.0_ocio-v2.5` |
| 入力 | `Linear Rec.709 (sRGB)`（scene-linear Rec.709、D65） |
| display / view | `sRGB - Display` / `ACES 2.0 - SDR 100 nits (Rec.709)` |
| 表示の符号化 | `DISPLAY - CIE-XYZ-D65_to_sRGB - MIRROR NEGS`（区分的sRGB）。LUTではこれを外す |
| OCIOの処理 | `DisplayViewTransform` → `getOptimizedCPUProcessor(OPTIMIZATION_LOSSLESS)`、float32 |
| LUT | 65³、RGBA16F、R成分が最も速く変わる順（x=R, y=G, z=B）、Aは1 |
| shaper | 各成分 `u = log2(x / 2^-8 + 1) / log2(2^8 / 2^-8 + 1)`、`u` は[0,1]へ飽和。範囲は[0, 256] |
| 格子点の入力 | `x_i = 2^-8 * (2^(u_i * log2(2^16 + 1)) - 1)`、`u_i = i / 64` |

0は格子の端に一致し、2^-8より十分明るい値ではlog2の等間隔（約0.25段/格子）になる。256より明るい成分は256として扱う
（ACES 2.0 SDRは中性色でおよそ128で白へ飽和する）。

## 生成物とSHA-256

`python Scripts/BakeAcesOutputLut.py` で生成し、`--verify` で再生成のbyte一致と補間誤差を検査する。

| ファイル | 内容 | SHA-256 |
|---|---|---|
| `Assets/ColorManagement/Aces20SdrRec709.lut3d` | 32 byteの見出し（`NLUT3D01`、size、channels、format=1、予約、shaperOffset、shaperMax）＋LUT本体 | `616DC53D5E23CDC146E989AE468C2844891F7440476228443D95EB2A81325F24` |
| `Test/Core/Rendering/Baselines/RenderingValidation/R8AcesChart.rgba16f` | HDR試験チャート（`NRGBA16F`、256×240、scene-linear、half） | `54D5E5510095B0275909F32DCD1CF99B47CA3771862580A89114B714A471E1EF` |
| `Test/Core/Rendering/Baselines/RenderingValidation/R8AcesReference.rgba32f` | チャートのOCIO厳密変換（`NRGBA32F`、display-linear、float32） | `E87179F2D22BA48E3031CB93C119E8821D8F569D50C6810C00AEB9B89970E2A0` |
| `Test/Core/Rendering/Baselines/RenderingValidation/R8AcesReference.png` | 基準画像の確認用プレビュー（sRGB 8 bit） | `E01A199CE5F9DFF77032A636F55184F2BB218FADE3185B580AF563C2728897CF` |

画像の見出しは magic(8)・width(u32)・height(u32) で、上の行からRGBAを並べる（すべてlittle-endian）。

## 試験チャート

16×16画素の区画を横16個並べた15行（256×240）。SceneColorがRGBA16Fなので、値はhalfへ丸め、基準は丸めた値から求める。

- グレー: 18%×2^-8〜2^7の1段刻み、0と2^-14〜2^7の1.5段刻み
- 原色・補色・中間色相（16色、最大成分=1）を18%×2^{-3,0,2,4,6}の5露出
- ColorChecker系の記憶色（肌・空・葉など16色、linear sRGBの近似値）を×0.25・×1・×4の3露出
- 明るい空の色（0.5, 0.8, 1.6）×2^-2〜2^5.5
- 連続変化: logグレーの傾斜（2^-14〜2^9）と、彩度1の色相の一巡を0.18・1・8の3明るさ

## 合格条件と実測

条件: 再生成がbyte一致し、LUTのCPU三線形補間とOCIO厳密変換の差が、チャートの全画素で[0,1]へ飽和させた区分的sRGB符号化後の
値で2/255以内。

実測（`--verify`、2026-09-26）: 4ファイルともbyte一致。61,440画素の最大1.577/255、平均0.135/255。最大は色相一巡の明るさ8の
黄（scene (8.0, 7.03, 0.0)）。

参考（合否に使わない）:
- 補間の重みを1/256へ丸めた場合（GPUの固定小数の重みの近似）のチャート最大は1.592/255。
- 固定の種（20260926）の無作為な色20万点（露出2^-10〜2^7、最大成分で正規化した一様な色）では最大5.819/255、99.9パーセンタイル
  1.399/255、2/255超は67点。超過は高彩度の色域境界付近に限られ、65³の三線形補間では検討したどのshaperでも残る。

## shaperの選定

チャートでの最大誤差（sRGB符号化後、/255）を候補ごとに測り、2/255以内に収まったものを採用した。

| shaper | 範囲 | チャート最大 | 2/255超の画素 |
|---|---|---|---|
| `log2(max(x, 2^min))` | 2^-14〜2^8 | 5.40 | 784 |
| 同 | 2^-12〜2^8 | 3.50 | 576 |
| 同 | 2^-10〜2^8 | 3.94 | 96 |
| 同 | 2^-10〜2^7 | 3.10 | 528 |
| `log2(x/c + 1)` | c=2^-10、〜2^7 | 3.00 | 528 |
| 同 | c=2^-8、〜2^7 | 2.56 | 256 |
| 同 | **c=2^-8、〜2^8** | **1.58** | **0** |
| 同 | c=2^-6、〜2^7 | 2.61 | 48 |
| 同 | c=2^-7、〜2^8 | 2.88 | 320 |
| 同 | c=2^-9、〜2^8 | 3.35 | 592 |

誤差は高彩度の色域境界付近の急な変化で決まり、範囲に対して単調ではない。採用値の余裕は0.42/255で、GPUでの一致はR8-P2で
同じ閾値により確かめる。

## 既知の制限

- LUTは[0, 256]の外の成分と負の成分を端へ寄せる。Rec.709の外（負の成分）を含むSceneColorは厳密変換と一致しない。
- 再生成のbyte一致はOCIOの版とCPU処理系に依存する。OCIOを更新したら生成し直してSHA-256とこの記録を更新する。
