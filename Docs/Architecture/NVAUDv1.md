# NVAUD v0 / v1 音声形式

## 範囲と状態

本書は cook 済み音声 NVAUD の v0 と v1 を規定する。v0 は PCM16 専用、v1 は単一のヘッダで PCM16 と Vorbis を運ぶ。v1 の `Codec = 2` は Opus の予約値であり、現在のパーサは拒否する。

v1 で実装済みなのは形式とパーサである。v1 を書き出す cook（WAV の取り込み・Vorbis の cook）と復号は未実装である。

定数と読み込み動作の正本は [CookedAudioFormat.h](../../Library/Core/Public/Asset/CookedAudioFormat.h) と [CookedAudioLoader.cpp](../../Library/Core/Private/Asset/CookedAudioLoader.cpp) である。

## 形式の識別

| 版 | マジック | バイト列（16 進数） | `VersionMajor` | `VersionMinor` | manifest の `format` | パッケージの `entry_type` | `EntryType`（`uint32`） |
| --- | --- | --- | ---: | ---: | --- | --- | --- |
| v0 | `NVAUDv0\0` | `4e 56 41 55 44 76 30 00` | 0 | 0 | `nvaud.v0.pcm16` | `Aud0` | `0x30647541` |
| v1 | `NVAUDv1\0` | `4e 56 41 55 44 76 31 00` | 1 | 0 | `nvaud.v1.pcm16` / `nvaud.v1.vorbis` | `Aud1` | `0x31647541` |

両マジックは末尾の NUL を含めて正確に 8 バイトである。`EntryType` は FourCC の先頭文字を下位 8 ビットに置く値であり、ヘッダ内の欄ではなくパッケージのエントリ種別である。

整数のバイト順はすべて little-endian とし、`EndianMarker` は `0x01020304`（格納バイト列は `04 03 02 01`）に固定する。`GainDb` は IEEE-754 binary32 のビット列を little-endian で格納する。

形式の別はマジック・パッケージの entry type・manifest の `format` の 3 か所で識別する。音声パーサ自身が検査するのは渡された音声バイト列であり、entry type と manifest の照合は呼ぶ側の責務である。`ParseCookedAudioV0` は v1 の完全なファイルを `BadMagic` で拒否する。無効・空・短い入力はマジック検査より先に拒否する。

## バイトの配置

オフセットと大きさの単位はバイトで、オフセットはファイル先頭を基準とする。型は格納形式を示し、C++ の構造体のメモリ配置を直接保存する形式ではない。

### v0 ヘッダ

`CookedAudioFormatV0::HeaderSize = 68`。

| オフセット | 大きさ | 型 | 欄 | 値の決まり |
| ---: | ---: | --- | --- | --- |
| 0 | 8 | `uint8[8]` | `Magic` | `NVAUDv0\0`（末尾 NUL を含む） |
| 8 | 4 | `uint32` | `HeaderSize` | `68` 固定 |
| 12 | 2 | `uint16` | `VersionMajor` | `0` 固定 |
| 14 | 2 | `uint16` | `VersionMinor` | `0` 固定 |
| 16 | 4 | `uint32` | `EndianMarker` | `0x01020304` 固定 |
| 20 | 4 | `uint32` | `Reserved0` | `0` 必須 |
| 24 | 8 | `uint64` | `FileSize` | 入力全体のバイト数と一致 |
| 32 | 8 | `uint64` | `PayloadOffset` | `68` 固定 |
| 40 | 8 | `uint64` | `PayloadSize` | 正数、`BlockAlignment` の倍数、入力内に収まる |
| 48 | 8 | `uint64` | `PayloadHash` | payload の FNV-1a 64 |
| 56 | 4 | `uint32` | `SampleRate` | `44100` または `48000`（Hz） |
| 60 | 2 | `uint16` | `ChannelCount` | `1` または `2` |
| 62 | 2 | `uint16` | `BitsPerSample` | `16` 固定 |
| 64 | 2 | `uint16` | `BlockAlignment` | `ChannelCount * 2`（`2` または `4`） |
| 66 | 2 | `uint16` | `Reserved1` | `0` 必須 |

### v1 ヘッダ

`CookedAudioFormatV1::HeaderSize = 104`。

| オフセット | 大きさ | 型 | 欄 | 値の決まり |
| ---: | ---: | --- | --- | --- |
| 0 | 8 | `uint8[8]` | `Magic` | `NVAUDv1\0`（末尾 NUL を含む） |
| 8 | 4 | `uint32` | `HeaderSize` | `104` 固定 |
| 12 | 2 | `uint16` | `VersionMajor` | `1` 固定 |
| 14 | 2 | `uint16` | `VersionMinor` | `0` 固定 |
| 16 | 4 | `uint32` | `EndianMarker` | `0x01020304` 固定 |
| 20 | 2 | `uint16` | `Codec` | `0` = PCM16、`1` = Vorbis。`2` = Opus は予約・拒否、ほかも拒否 |
| 22 | 2 | `uint16` | `Flags` | bit0 = `HasLoop`。ほかのビットは `0` 必須（値は `0` または `1`） |
| 24 | 8 | `uint64` | `FileSize` | 入力全体のバイト数と一致 |
| 32 | 8 | `uint64` | `PayloadOffset` | `104` 固定 |
| 40 | 8 | `uint64` | `PayloadSize` | 正数、`FileSize - 104` と一致。PCM16 は `BlockAlignment` の倍数 |
| 48 | 8 | `uint64` | `PayloadHash` | payload の FNV-1a 64 |
| 56 | 8 | `uint64` | `TotalFrames` | 正数。PCM16 は `PayloadSize / BlockAlignment` と一致 |
| 64 | 8 | `uint64` | `LoopStartFrame` | ループ有効時は `LoopStartFrame < LoopEndFrame`、無効時は `0` |
| 72 | 8 | `uint64` | `LoopEndFrame` | ループ有効時は `LoopEndFrame <= TotalFrames`、無効時は `0` |
| 80 | 4 | `uint32` | `SampleRate` | `44100` または `48000`（Hz） |
| 84 | 2 | `uint16` | `ChannelCount` | `1` または `2` |
| 86 | 2 | `uint16` | `BitsPerSample` | PCM16 は `16`、Vorbis は `0` |
| 88 | 2 | `uint16` | `BlockAlignment` | PCM16 は `ChannelCount * 2`、Vorbis は `0` |
| 90 | 2 | `uint16` | `Category` | `0`〜`5`（後述） |
| 92 | 4 | `uint32` | `EncoderDelay` | PCM16 は `0` 必須。Vorbis は `uint32` の任意の値を受理 |
| 96 | 4 | `float32` | `GainDb` | 有限値（dB）。NaN・正負の無限大を拒否 |
| 100 | 4 | `uint32` | `Reserved` | `0` 必須 |

## v1 の欄と payload の意味

### 符号化方式とフレーム

`Codec` は `CookedAudioCodec` に対応する。`Pcm16 = 0` と `Vorbis = 1` を受理し、予約値 `Opus = 2` とそれ以外は `UnsupportedCodec` になる。`SampleRate` と `ChannelCount` の制限は両方式に共通である。

1 フレームは全チャンネルの同一時刻のサンプルをまとめた単位である。`TotalFrames` は再生できるフレーム数で、必ず正数とする。PCM16 では `TotalFrames = PayloadSize / BlockAlignment` が必要である。Vorbis ではパーサは復号しないため、符号化データからフレーム数を算出して照合する検査はない。

`EncoderDelay` は圧縮の頭で捨てるフレーム数を示す。PCM16 では `0` 必須である。Vorbis では現在のパーサは値を保持するだけで、`TotalFrames` との大小関係や符号化データとの整合性を検査しない。

### ループ・ゲイン・カテゴリ

`Flags` の bit0 は `FlagHasLoop = 0x0001` であり、`KnownFlagsMask` も `0x0001` である。未定義ビットはゼロ必須である。

`HasLoop` が有効なら、ループ範囲はフレーム単位の半開区間 `[LoopStartFrame, LoopEndFrame)` とする。終端は含まない。`0 <= LoopStartFrame < LoopEndFrame <= TotalFrames` を満たす必要がある。無効なら `LoopStartFrame` と `LoopEndFrame` は両方 `0` 必須である。

`GainDb` は dB 単位の有限値である。パーサは binary32 の指数ビットがすべて 1 の値を拒否する。有限値に追加の上限・下限は設けず、読み取ったゲインを結果に保持する。

| 値 | `CookedAudioCategory` | 用途 |
| ---: | --- | --- |
| 0 | `Unspecified` | 未指定 |
| 1 | `Sfx` | `sfx`（効果音） |
| 2 | `Ambience` | `ambience`（環境音） |
| 3 | `Footstep` | `footstep`（足音） |
| 4 | `Music` | `music`（音楽） |
| 5 | `Ui` | `ui`（UI 音） |

### payload とハッシュ

PCM16 の payload はチャンネルをインターリーブした符号付き `int16` の little-endian バイト列である。`BitsPerSample = 16`、`BlockAlignment = ChannelCount * 2` とし、payload はフレーム境界で終える。

Vorbis の payload は Ogg Vorbis ファイルのバイト列をそのまま格納する。`BitsPerSample = 0`、`BlockAlignment = 0` とする。現在のパーサは Ogg/Vorbis の内部構造・ヘッダとの一致・復号可能性を検査しないため、`Success` は復号成功を意味しない。

v1 の payload は `HeaderSize = 104` から始まり、ファイルの終わりでちょうど終わる。余剰バイトを許さない。v0 も `HeaderSize = 68` から始まるが、`PayloadSize <= FileSize - PayloadOffset` を検査するため、payload の後ろの余剰バイトを許す。その場合も `FileSize` は余剰を含む入力全体の大きさと一致する必要がある。

`PayloadHash` は両版とも `ComputeCookedAudioPayloadHash` による payload だけの FNV-1a 64 である。ヘッダと、v0 で許される payload 後の余剰は対象に含まない。

- 初期値（offset basis）: `14695981039346656037`。
- 素数（prime）: `1099511628211`。
- 各バイトを順に XOR し、素数を乗算する。演算は `uint64` で行い、結果は 64 ビットに収める。

## パーサの検査順

以下はコードの実行順である。最初に該当した失敗の状態を返し、以降の検査は行わない。`bytes.size()` は渡された `AssetBlob` のバイト数を表す。

### `ParseCookedAudioV0`

1. `SourceBlob.IsValid()` が偽 → `InvalidBlob`。
2. バイト列が空 → `EmptyBlob`。
3. `bytes.size() < 68` → `HeaderTooSmall`。
4. 先頭 8 バイトが v0 のマジックと不一致 → `BadMagic`。
5. `VersionMajor != 0` または `VersionMinor != 0` → `UnsupportedVersion`。
6. `EndianMarker != 0x01020304` → `EndianMismatch`。
7. `HeaderSize != 68` → `HeaderSizeMismatch`。
8. `Reserved0 != 0` または `Reserved1 != 0` → `ReservedFieldNonZero`。
9. `FileSize != bytes.size()` → `FileSizeMismatch`。
10. `SampleRate` が `44100` でも `48000` でもない → `UnsupportedSampleRate`。
11. `ChannelCount` が `1` でも `2` でもない → `UnsupportedChannelCount`。
12. `BitsPerSample != 16` → `UnsupportedBitsPerSample`。
13. `BlockAlignment != ChannelCount * 2` → `BlockAlignmentMismatch`。
14. `PayloadSize == 0` → `InvalidPayloadSize`。
15. `PayloadSize % BlockAlignment != 0` → `PayloadBlockMisaligned`。
16. `PayloadOffset != 68`、`PayloadOffset > bytes.size()`、または `PayloadSize > bytes.size() - PayloadOffset` → `PayloadOutOfRange`。
17. `PayloadHash` が payload の計算値と不一致 → `PayloadHashMismatch`。
18. すべて通過し、読み取り結果を構築 → `Success`。

### `ParseCookedAudioV1`

1. `SourceBlob.IsValid()` が偽 → `InvalidBlob`。
2. バイト列が空 → `EmptyBlob`。
3. `bytes.size() < 104` → `HeaderTooSmall`。
4. 先頭 8 バイトが v1 のマジックと不一致 → `BadMagic`。
5. `VersionMajor != 1` または `VersionMinor != 0` → `UnsupportedVersion`。
6. `EndianMarker != 0x01020304` → `EndianMismatch`。
7. `HeaderSize != 104` → `HeaderSizeMismatch`。
8. `Reserved != 0` または `Flags` に `KnownFlagsMask` 外のビットがある → `ReservedFieldNonZero`。
9. `FileSize != bytes.size()` → `FileSizeMismatch`。
10. `Codec` が `Pcm16 = 0` でも `Vorbis = 1` でもない（`Opus = 2` を含む） → `UnsupportedCodec`。
11. `SampleRate` が `44100` でも `48000` でもない → `UnsupportedSampleRate`。
12. `ChannelCount` が `1` でも `2` でもない → `UnsupportedChannelCount`。
13. PCM16 で `BitsPerSample != 16`、または Vorbis で `BitsPerSample != 0` → `UnsupportedBitsPerSample`。
14. PCM16 で `BlockAlignment != ChannelCount * 2`、または Vorbis で `BlockAlignment != 0` → `BlockAlignmentMismatch`。
15. `Category > 5` → `UnsupportedCategory`。
16. `GainDb` のビット列で `(gainBits & 0x7f800000u) == 0x7f800000u`（NaN または無限大） → `InvalidGain`。
17. `PayloadSize == 0` → `InvalidPayloadSize`。
18. PCM16 で `PayloadSize % BlockAlignment != 0` → `PayloadBlockMisaligned`。Vorbis はこの検査を行わない。
19. `PayloadOffset != 104`、`PayloadOffset > bytes.size()`、または `PayloadSize != bytes.size() - PayloadOffset` → `PayloadOutOfRange`。
20. `TotalFrames == 0`、または PCM16 で `PayloadSize / BlockAlignment != TotalFrames` もしくは `EncoderDelay != 0` → `FrameCountMismatch`。
21. `HasLoop` が有効で `LoopStartFrame >= LoopEndFrame` または `LoopEndFrame > TotalFrames`、もしくは無効でどちらかのループ欄が非ゼロ → `LoopRangeInvalid`。
22. `PayloadHash` が payload の計算値と不一致 → `PayloadHashMismatch`。
23. すべて通過し、読み取り結果を構築 → `Success`。

### `ParseCookedAudio` の振り分け

有効な `AssetBlob` に 8 バイト以上あり、先頭 8 バイトが v1 のマジックと完全一致した場合だけ `ParseCookedAudioV1` に渡す。それ以外は `ParseCookedAudioV0` に渡す。

無効・空・8 バイト未満・どちらでもないマジックの入力は v0 のパーサへ渡すため、その結果は v0 だけを呼んだときと同じである。8 バイト以上でも v1 のマジックと一致しなければ同じ扱いとなる。v1 のマジックを持つ短い入力（8〜103 バイト）は v1 側で `HeaderTooSmall` になる。振り分け後に失敗しても他方の版へ再試行しない。

## 状態の一覧

`CookedAudioParseStatus` の全列挙子を宣言順に示す。検査番号は上記の各版の番号に対応する。

| 状態 | v0 の検査 | v1 の検査 | 意味 |
| --- | --- | --- | --- |
| `Success` | 18 | 23 | すべての検査を通過 |
| `InvalidBlob` | 1 | 1 | 無効な入力 |
| `EmptyBlob` | 2 | 2 | 有効だが空の入力 |
| `HeaderTooSmall` | 3 | 3 | 各版のヘッダサイズ未満 |
| `BadMagic` | 4 | 4 | 各版のマジックと不一致 |
| `UnsupportedVersion` | 5 | 5 | メジャーまたはマイナーの版が不一致 |
| `EndianMismatch` | 6 | 6 | エンディアンマーカーが不一致 |
| `HeaderSizeMismatch` | 7 | 7 | ヘッダサイズ欄が不一致 |
| `FileSizeMismatch` | 9 | 9 | ファイルサイズ欄と入力の大きさが不一致 |
| `ReservedFieldNonZero` | 8 | 8 | 予約欄が非ゼロ。v1 は未定義のフラグビットも含む |
| `UnsupportedSampleRate` | 10 | 11 | 未対応のサンプルレート |
| `UnsupportedChannelCount` | 11 | 12 | 未対応のチャンネル数 |
| `UnsupportedBitsPerSample` | 12 | 13 | 符号化方式に対して量子化ビット数が不一致 |
| `BlockAlignmentMismatch` | 13 | 14 | 符号化方式に対してブロックサイズが不一致 |
| `InvalidPayloadSize` | 14 | 17 | payload の大きさがゼロ |
| `PayloadBlockMisaligned` | 15 | 18（PCM16 のみ） | PCM16 の payload がフレーム境界で終わらない |
| `PayloadOutOfRange` | 16 | 19 | 開始位置・範囲が不正。v1 は末尾の余剰も拒否 |
| `PayloadHashMismatch` | 17 | 22 | payload のハッシュが不一致 |
| `UnsupportedCodec` | 該当なし | 10 | v1 で追加。予約値を含む未対応の符号化方式 |
| `UnsupportedCategory` | 該当なし | 15 | v1 で追加。カテゴリが `0`〜`5` の範囲外 |
| `InvalidGain` | 該当なし | 16 | v1 で追加。ゲインが有限値でない |
| `LoopRangeInvalid` | 該当なし | 21 | v1 で追加。ループ範囲または無効時のゼロ条件が不正 |
| `FrameCountMismatch` | 該当なし | 20 | v1 で追加。総フレーム数がゼロ、PCM16 のフレーム数が不一致、または PCM16 の遅延が非ゼロ |

## 読み取り結果と API

`CookedAudioParseResult` は `Status` と `Audio`（`CookedAudioData`）を持つ。`Succeeded()` は `Status == Success` のときだけ真になる。成功した結果は元の `AssetBlob` を `SourceBlob` に保持し、payload の位置・大きさ・ハッシュと音声の各欄を公開する。

v0 に存在しない v1 の欄には、次の互換値を設定する。

| `CookedAudioData` の欄 | v0 を読んだ値 | v1 を読んだ値 |
| --- | --- | --- |
| `FormatVersion` | `0` | `1` |
| `Codec` | `CookedAudioCodec::Pcm16` | ヘッダの `Codec` |
| `FrameCount` | `PayloadSize / BlockAlignment` | ヘッダの `TotalFrames` |
| `TotalFrames` | `FrameCount` と同じ | ヘッダの `TotalFrames` |
| `HasLoop` | `false` | `Flags` の bit0 |
| `LoopStartFrame` | `0` | ヘッダの値 |
| `LoopEndFrame` | `0` | ヘッダの値 |
| `EncoderDelay` | `0` | ヘッダの値 |
| `GainDb` | `0.0f` | ヘッダの値 |
| `Category` | `CookedAudioCategory::Unspecified` | ヘッダの値 |

`FrameCount` は互換のために残しており、両版とも `TotalFrames` と同じ値になる。

`GetPcmBytes()` は `Codec == CookedAudioCodec::Pcm16` のときだけ payload の `Span<const uint8_t>` を返し、それ以外は空を返す。圧縮データを PCM として取り違えないための境界である。`GetEncodedBytes()` は `Codec` に関係なく payload を返す。両アクセサは `SourceBlob` が無効、または payload 範囲が入力外なら空を返す。

現在の [AudioClipLoader.cpp](../../Library/Modules/Audio/Private/Audio/AudioClipLoader.cpp) は PCM の形式欄（`SampleRate`・`ChannelCount`・`BitsPerSample`・`BlockAlignment`）から対応可否を判断して payload を扱う。Vorbis は `BitsPerSample = 0` のため `UnsupportedFormat` で拒否され、復号には進まない。

## manifest とパッケージ

| 版・符号化方式 | `kind` | `format` | `entry_type` |
| --- | --- | --- | --- |
| v0 PCM16 | `audio` | `nvaud.v0.pcm16` | `Aud0` |
| v1 PCM16 | `audio` | `nvaud.v1.pcm16` | `Aud1` |
| v1 Vorbis | `audio` | `nvaud.v1.vorbis` | `Aud1` |

音声の欄（サンプルレート・チャンネル数・フレーム数・ループ・ゲイン・カテゴリ・エンコーダ遅延など）は manifest の `metadata` に出さず、NVAUD ヘッダを正本にする。正本を 1 か所にまとめ、manifest とヘッダの食い違いを作らないためである。

現在の AssetCook のパッケージ検査は `nvaud.v0.pcm16` と `Aud0` の組だけを受け付け、v1 の書き出し対応には検査対象の拡張が必要である。

[CookOutputPackage.cpp](../../Tools/AssetCook/CookOutputPackage.cpp) の audio 分岐は上記の組を照合した後、`ParseCookedAudio` が成功し `BitsPerSample == 16` であることを確認する。この分岐はパース結果の `FormatVersion` 自体を比較しない。[SingleAssetCook.cpp](../../Tools/AssetCook/SingleAssetCook.cpp) の現在の audio 出力は `CookedAudioFormatV0::EntryType` を使い、参照にも `EntryTypeText = "Aud0"`、`CookedVersion = 0` を設定する。
