# JSONのUTF8入口とnative文字幅

GR78/GR79の元材質名をJSONから共通resolverへ渡す前提として、JsonDocument::TryParseUtf8 を設ける。glTF/GLB、sidecar、AssetCookのinspect/fingerprint/static cookは、bytesをTCHARへ単純拡幅せずこの入口を使う。path用の文字列変換は変更しない。

- byte表現は厳密UTF8、nativeの1byteはUTF8、2byteはUTF16、4byteはUnicode scalar
- 不正UTF8、過長、孤立surrogate、Unicode範囲外は置換せず拒否
- native TryParse の入力も同じscalar規則で検査する
- JSON字句はnative code unitをcharへ切り詰めない。数字はASCII 0..9、空白はspace/tab/CR/LFのみ
- 数値tokenは構文確認済みASCIIをAnsiStringへ移してfrom_charsする。数値の範囲外拒否は維持
- BOMは既存のcontainer/sidecar入口で処理し、新API自体では自動削除しない
- UnicodeとしてのNULは認めるが、JSON source内のliteral NULは構文エラー。escapeされたU+0000はASTに保持し、材質名などの識別子側で拒否
- 失敗時は既存TryParse同様にoutDocumentをResetする。成功時のみ新documentへ置換。正規化・大文字小文字変更は行わない

UnicodeText helperは失敗前のprefixについてcallbackを呼ぶことがあるため、TryParseUtf8は常に局所の文字列を組み立てる。入力からborrowしたviewを出力ASTに持ち越さない。

## 検証

純helperは全Unicode scalarのUTF8/16/32往復、不正UTF8/16/32、終端overflow、JSON ASCII字句を通常/O2/ASan+UBSanで検証する。実JsonDocumentはraw/escape/nativeの骨・狼emoji・アクセント文字・下位byteがquote/改行になる文字、数値、NUL、失敗Resetをnative bundleへ登録する。

C++の実行文字コードに依存しないよう、raw UTF8 fixtureはu8 literalを使う。Linuxのpure helperやMEMBER wrapper compileだけでWindows Core/実JSON成功としない。現在の通常Windows構成はANSI TCHARであり、UTF16/32 helper成功をCore全体のUNICODEビルド成功とは扱わない。

TStringの再確保はNUL終端copyを行うため、JSON文字列ごとに次のunescaped quoteまでのsource code-unit数を事前計算して確保する。これによりescapeされたNUL後の長いsuffixを再確保で失わず、各文字列がファイル残り全体の容量を持つ二次的なメモリ増加も避ける。長いNUL入り値・keyと後続node成長を回帰試験に含める。汎用TStringのコピー契約自体はこの変更の範囲外。
