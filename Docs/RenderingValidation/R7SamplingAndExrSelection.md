# R7 S7 技術選定記録：サンプリング方式とEXR出力

日付: 2026-09-23 ／ 状態: 決定済み

## 課題定義

- 解く問題: R7パストレーサーで直接照明と材質サンプリングを公平に合成し、R8の映像連番にも使える標準EXRを決定論的に出力する。
- 制約: RHI/Vulkan境界をRendering層へ漏らさない。新しい大型依存を避け、既定のRendering3DTest起動経路とラスタ描画を変えない。作業色空間はR1確定のRec.709 linear/D65を維持する。
- 成功基準: Cornell box、解析照明、SPP収束、固定seed、EXR相互読出しで検証できること。R8の240-frame sequenceが同じ出力契約を利用できること。

## 候補

| 候補 | 概要 | 主な根拠 | 主な懸念 |
|---|---|---|---|
| NEE + MIS | 各bounceでlight samplingとBSDF samplingを行い、power heuristicで重み付けする | 【外部】多重サンプリングのPDFを明示する積分方式。area lightとBSDF経由のemissionを同じ推定量で扱える | 【推測】PDFの測度、選択確率、delta lightの扱いを揃えないと偏りが出る |
| ReSTIRを含む拡張 | 時間・空間再利用を加える | 【外部】大規模な動的直接照明の候補技法 | 【推測】履歴・再利用bias・検証面積を増やし、R7の基礎検証を遅らせる |
| TinyEXR v3.2.0 | C11 APIで単一part scanline EXRを出力する | 【外部】v3.2.0 release、C11 API、ZIP/PIZ等のcodec、scanline/tiled出力を公開している | 【外部】OpenEXR本体より新しいAPI系統であり、R7で実際の相互運用性を検査する必要がある |
| OpenEXR 3.x | 参照実装のC++ APIまたはOpenEXRCore C APIを使う | 【外部】公式のCMake targets、Windows build、scanline/chunk writerがある | 【外部】Imathと圧縮codec群を含む依存・構成範囲が大きい |
| EXR writerを導入しない | 独自形式か既存PNGのみを使う | 【推測】依存追加は避けられる | RoadmapのEXR sequence条件を満たさない |

## 証拠等級付き比較

- 【外部】TinyEXR v3.2.0は公式releaseであり、release commitは`6f470c9`。同プロジェクトREADMEはv3をC11 APIとして示し、ZIP/PIZ、scanline/tiled、multipart、custom attributesを列挙する。 [release](https://github.com/syoyo/tinyexr/releases/tag/v3.2.0) / [README](https://github.com/syoyo/tinyexr)
- 【外部】OpenEXR公式はCMake imported targetを推奨し、Windows buildとC APIのchunk書き込みを説明する。公式のビルド要件にはImathと複数の圧縮codec経路がある。 [installation](https://openexr.com/en/latest/install.html) / [C API](https://openexr.com/en/latest/OpenEXRCoreAPI.html)
- 【実測】本リポジトリはC++23のCMake projectで、Coreにstb系のsource integrationがあり、R5のRT pipeline/SBT/TraceRaysが既に存在する。EXR libraryはまだ導入されていない。
- 【推測】単一part・RGB float・scanline・ZIPに限ればTinyEXR v3は必要十分であり、Core private wrapperへ閉じ込めればAPI変更時も影響範囲を限定できる。
- 【外部】TinyEXRのv1 single-header APIはdeprecatedとREADMEに記載されるため採用せず、v3 C APIをcommit固定して使う。

## 決定

- サンプリング: NEE + power heuristic MIS（β=2）を必須とする。非delta lightはlight sampleとBSDF continuationの両PDFをMISで合成し、point/directional delta lightはlight sample側をweight 1とする。light選択PMFをPDFへ含める。ReSTIR等の再利用方式は対象外。
- 拡散・GGX材質は共有評価コードを使用し、point/directional/area emitterを扱う。経路は最大8 surface bounce、Russian rouletteは3 bounce以降とする。各pixel/sample/bounceから再現可能な乱数列を生成し、グローバル可変RNGに依存しない。
- EXR: TinyEXR v3.2.0のC11 APIをcommit `6f470c9`へ固定する。single-part scanline、RGB 32-bit float、linear Rec.709値、ZIP lossless compressionを初期出力契約とする。色変換、display OETF、ACES変換は加えず、R8の出力側処理へ分離する。
- TinyEXRの呼び出しはCore private writer wrapperに限定し、第三者型をpublic RHI/APIへ出さない。CMakeではC compilerを明示的に有効化し、third-party compile optionをNorvesLib全体へ伝播させない。

## プロトタイプと判定基準

- P6でTinyEXR v3.2.0をビルドし、有限値を含む既知RGB float scanlineを保存・再読込する。header/channel type/window/compressionと値を確認し、同じseedの2回出力をbyte比較する。
- EXR writerがMSVC/CMakeでビルドできない、または独立decoderでRGB float/ZIPが読めない場合はOpenEXRCoreを再評価する。golden差を通すために閾値を緩めない。

## 再評価トリガー

- TinyEXR v3.2.0のrequired RGB float scanline/ZIP contractが独立decoderで不一致となる。
- license、security advisory、Windows buildまたはC11 portabilityにblocking issueが判明する。
- multipart/deep/DWAA等、本スコープ外の形式がRoadmap変更で必須になる。
