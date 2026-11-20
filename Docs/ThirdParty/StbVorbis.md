# stb_vorbis 1.24

## 出所とライセンス

[stb](https://github.com/nothings/stb) の commit
[`9f755b604db3492df2cbeb39bf84b81e3e3932e3`](https://github.com/nothings/stb/commit/9f755b604db3492df2cbeb39bf84b81e3e3932e3)
（2026-11-12 17:33:26 UTC）にある stb_vorbis v1.24 を使用する。
取り込んだ上流ファイルは `stb_vorbis.c` と `LICENSE` のみで、ほかのヘッダや tests は含めない。
原本と取り込み後のサイズ・SHA-256、変更の出所は `Library/ThirdParty/stb_vorbis/UPSTREAM.json` に記録する。

ライセンスは MIT または Unlicense の選択式（SPDX: `MIT OR Unlicense`）。
`LICENSE` を同梱しており、`stb_vorbis.c` の末尾にも同じ二択が記載されている。

## ビルド設定

`NorvesThirdParty_StbVorbis` を独立した静的ターゲットとして登録し、`NorvesAudioDecode` だけがリンクする。
`Core`・`Game` に直接含めない。利用側が `STB_VORBIS_HEADER_ONLY` を定義して同じファイルから宣言を読むときも設定が一致するよう、次の定義を PUBLIC とする。

- `STB_VORBIS_NO_STDIO`: ファイル入出力 API を外し、メモリからの入力に限定する。
- `STB_VORBIS_NO_PUSHDATA_API`: push API を外し、pull API を使う。
- `STB_VORBIS_NO_COMMENTS`: vendor・comment 情報の解析と保持を外す。2023 年の脆弱性の多くが comment 解析にあったため、攻撃面を減らす。

MSVC では第三者コードの警告を `/w` で抑える。

## 既知の脆弱性

次の状態は、この版のコードと取り込み時の変更に基づく。

| ID | CVE | 内容 | この版での状態 | 根拠 |
|---|---|---|---|---|
| ForAllSecure | CVE-2019-13217〜CVE-2019-13223 | 不正な入力による複数の問題 | 上流 v1.17 で修正済み | `stb_vorbis.c` の v1.17 の改版履歴 |
| GHSL-2023-165 | CVE-2023-45675 | vendor の終端 0 バイトの書き込み | 上流 v1.24 で修正済み | `start_decoder` が長さを検査し、`len+1` を確保してから終端を書く |
| GHSL-2023-166 | CVE-2023-45676 | `setup_malloc` の丸めの整数オーバーフロー | 上流 v1.24 で修正済み | `sz <= 0` または `sz > INT_MAX - 7` を検査してから丸める |
| GHSL-2023-167 | CVE-2023-45677 | `len == INT_MAX` のときの整数オーバーフロー | 上流 v1.24 で修正済み | `start_decoder` が vendor と各 comment の負の長さと `INT_MAX` を拒否する |
| GHSL-2023-168 | CVE-2023-45678 | submap の配列を 1 要素越えて書き込む | 上流 v1.24 で修正済み | `Mapping` の `submap_floor[16]` と `submap_residue[16]`（上流 commit `30e553a2`） |
| GHSL-2023-169 | CVE-2023-45679 | 未初期化ポインタの解放 | 上流 v1.24 で修正済み | `comment_list` の各要素を `memset` で 0 初期化する |
| GHSL-2023-170 | CVE-2023-45680 | `vorbis_deinit` の NULL 参照 | 上流 v1.24 で修正済み | 解放時に `if (p->comment_list)` で検査する |
| GHSL-2023-171 | CVE-2023-45681 | `comment_list_length` の掛け算の整数オーバーフロー | 上流 v1.24 で修正済み | 要素数を `INT_MAX / (int)sizeof(char*)` と比較する |
| GHSL-2023-172 | CVE-2023-45682 | 疎な codebook の負の添字による範囲外読み込み | 上流 v1.24 では未修正。この取り込みに修正を適用 | `DECODE` で `var >= 0` を検査してから `sorted_values[var]` を読む |

GHSL-2023-165〜171 に対する上流 PR #1553〜#1559 は 2024-07-04 に閉じられたが、
2026 年 10〜11 月の上流の修正により同等の検査が入っている。
これには `f5d198ce` のサイズのオーバーフロー検査と `CODEBOOK_LIMIT`、
`ee8dbb8d` のページ境界の assert のエラー化、`c662d880` の `set_file_offset` のオーバーフロー対策が含まれる。
`STB_VORBIS_NO_COMMENTS` は comment 関連の解析を無効にする追加の緩和でもある。

## 適用した修正

### GHSL-2023-172

GHSL-2023-172 では、疎な codebook の `sorted_entries` が 0 のとき `sorted_values` が NULL のまま、
`DECODE` が `sorted_values[-1]` を読もうとする。取り込み時に次の 1 行だけを変更した。

```diff
-   if (c->sparse) var = c->sorted_values[var];
+   if (c->sparse && var >= 0) var = c->sorted_values[var];
```

出所は上流の未取り込み・クローズ済み [PR #1560](https://github.com/nothings/stb/pull/1560) と、
同じ修正を持つ [sezero/stb の `stb_vorbis-sezero` ブランチ](https://github.com/sezero/stb/tree/stb_vorbis-sezero)。

### CRC 表の定数化

デコーダを開くたびに `crc32_init` が共有の CRC 表を書き換えていたため、別のスレッドでデコーダを開いたり、シーク中に表を読んだりすると競合していた。
同じ式で事前計算した表を読み取り専用にし、初期化処理を削除した。

```diff
-static uint32 crc_table[256];
-static void crc32_init(void)
-{
-   …
-}
+static const uint32 crc_table[256] = {
+   0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005,
+   …
+};
@@
-   crc32_init(); // always init it, to avoid multithread race conditions
```

### シークの実行時の検査

壊れた入力では、Debug ビルドは位置や範囲の `assert` で停止し、`assert` が無効なビルドは復号済みバッファの終端を越えて読み取り位置を進めることがあった。
`seek_to_sample_coarse` は最後のページのサンプル位置、探索中のページの順序、戻ったページに位置の分かる区切りがあることを検査する。
`stb_vorbis_seek_frame64` は現在位置を、`stb_vorbis_seek64` は復号の成否と移動量を検査し、失敗時は `VORBIS_seek_failed` を記録して 0 を返す。

```diff
-   assert(right.last_decoded_sample != -1);
+   if (right.last_decoded_sample == -1) goto error;
@@
-         assert(mid.page_start < right.page_start);
+         if (mid.page_start >= right.page_start) goto error;
@@
-   assert(end_pos >= 0);
+   if (end_pos < 0) goto error;
@@
-   assert(f->current_loc_valid);
-   assert(f->current_loc <= sample_number);
+   if (!f->current_loc_valid || f->current_loc > sample_number)
+      return error(f, VORBIS_seek_failed);
@@
-      (void)stb_vorbis_get_frame_float(f, &n, NULL);
-      assert(sample_number > frame_start);
-      assert(f->channel_buffer_start + (int) (sample_number-frame_start) <= f->channel_buffer_end);
+      if (!stb_vorbis_get_frame_float(f, &n, NULL)
+          || sample_number <= frame_start
+          || sample_number - frame_start > (int64) (f->channel_buffer_end - f->channel_buffer_start)) {
+         f->current_playback_loc_valid = FALSE;
+         return error(f, VORBIS_seek_failed);
+      }
       f->channel_buffer_start += (int) (sample_number - frame_start);
```

3 件のローカルパッチは、それぞれの修正が上流に取り込まれた時点で外す。

## 運用上の前提

書き換わる共有状態がないため、別々のデコーダインスタンスは別のスレッドで同時に使用できるが、同じインスタンスは同時に使用しない。
壊れた入力へのシークで上記の検査に失敗すると、`assert` で停止せずに 0 を返す。

stb は pull 復号時にページの CRC を検査しないため、ラッパの `NorvesAudioDecode` が stb に渡す前に全ページの境界と CRC を検査する。
先頭の BOS から単一の論理ストリームが連番で続き、EOS が入力末尾で終わる最後のページにのみ現れることを確かめる。この並びが崩れた入力や、EOS の granule が未設定の入力は開かない。

ランタイムの復号対象は、cook 時に全体を復号して検証し、ロード時に payload のハッシュを確かめたものに限る。
利用者が置いたファイルやネットワークからの信頼できない入力をデコーダに直接渡さない。
これらの検証とハッシュ確認は、この第三者ターゲット自体ではなく利用側の責務とする。

浮動小数点で復号するため、出力のビット一致を比較できるのは同じビルド内に限る。
ビルド構成やコンパイラをまたいでビット一致を要求しない。

## 更新手順

1. 更新先の commit から `stb_vorbis.c` と `LICENSE` の原本を取得する。
2. 上流の修正状況を確認し、`UPSTREAM.json` の `patches` の各修正を必要なら当て直す。
3. `UPSTREAM.json` の版・出所と、取り込み後の `size`・`sha256`、パッチ適用前の `upstreamSize`・`upstreamSha256` を更新する。
4. 上記の脆弱性の表と運用上の前提を見直す。
