# R7 PT EXR連番出力契約

`WritePathTracingExrFrame` は、GPUで累積済みの RGBA32F テクスチャを同期読戻しした後の画素を受け取る。RGBの値をそのまま単一partの scanline EXRへ書き、alpha、表示用OETF、露出変換は出力しない。作業値は linear Rec.709/D65 とする。EXRの `chromaticities` 属性は付けず、出力側で色変換もしない。

出力ファイル名は `<scene>_seed<8桁16進>_spp<6桁10進>_frame<6桁10進>.exr`。`scene` は英数字・`_`・`-` だけを受け入れる。seed、SPP、frame は呼出元が実際の描画条件を渡す。現在のPT shaderのseedは `0x6a09e667` に固定されている。SPPは `PathTracingPass::GetAccumulatedSampleCount()` と一致させる。1回の呼出しが1 frameを保存し、出力ディレクトリは事前に作成する。

全RGBA値の有限性と寸法・配列長を保存前に検査する。TinyEXR v3.2.0 C APIで B/G/R 各32-bit floatを ZIP の16 scanline blockとして保存し、同じディレクトリの一時ファイルを完成させてから `MoveFileExA` で最終名へ移す。失敗時は完成ファイルを公開しない。

TinyEXRは [v3.2.0 の commit `6f470c9ab24bf3992bc512ce07e8ecb00d9bf105`](https://github.com/syoyo/tinyexr/releases/tag/v3.2.0) の C11 ソースを固定した。MSVC では `/experimental:c11atomics` を指定する。付属の JPH SIMD ソースの `__builtin_clz` 1箇所だけ、MSVC向けに `_BitScanReverse` を使う。TinyEXRの型は Core の公開APIへ出さない。

`PathTracingExrOutputTest` は同一 scene・seed・32 SPP・frame の実GPU PT画像を2回保存してファイル全体の byte 一致を検査する。TinyEXRを呼ばない Python 標準ライブラリの読取器が、EXR header、RGB float channel、data/display window、ZIP block、全画素の有限性と既知の linear RGB 値を検査する。17行の画像で最終blockも確認する。NaN/Inf入力による既存ファイルの上書きも拒否する。OpenCV の OpenEXR decoderでも同じ3×17画像と32×32画像を別途読めることを確認した。scanlineの配置は [OpenEXR File Layout](https://openexr.com/en/latest/OpenEXRFileLayout.html) に従う。
