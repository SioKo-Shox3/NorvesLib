# Texture asset-set v1 の入力境界（G2-S6 / GR96）

TextureAssetSetSpecはJSONの寿命から独立した値所有型。既存CookTextureAssetSet.ps1のname/package_root/default_variant/texturesと、各textureのlogical_path/source_path/format/package_name/entry_name/variantを保持する。未知fieldとusageは旧wrapperどおり無視し、variantは省略時だけdefaultを使う。

版は整数tokenの1だけを許す。JsonValue::IsIntegerLiteralは小数点・指数の有無だけを記録し、既存AsNumberの変換や範囲は変えない。1.0/1e0/true/stringはspecの版として拒否する。

manifestに出るlogical/package/entry/variant/formatは既存単体CLIのASCII範囲を守る。pathの前後空白を除去し、区切りを/に統一、logicalとentryは大文字小文字が完全一致するAssets/だけを除去する。重複logical+variantとpackageはASCII大文字小文字を無視して拒否。元のtexture順序を変えず、indexをsortするためO(N log N)。

安全のため、出力pathは絶対path・traversal・空segment・ADSのcolon・末尾dot/spaceのWindows別名を拒否する。これは旧wrapperの曖昧な出力名をそのまま通さない明示的な制限。source_pathはUTF8値を保持する。repository-relative/drive/UNC等の解決、source存在、出力とのalias、symlink、実出力の保護は実行層の責任であり、parserだけで完了扱いにしない。

既知fieldの重複、型違い、NUL、空の必須文字列、空texturesを拒否する。成功時だけ出力specを置換する。cookサービス・CLI --asset-set・旧manifest serializer・増分印は後続の接続で実装する。

空白だけの必須値とpath前後の空白は[.NET Char.IsWhiteSpace](https://learn.microsoft.com/en-us/dotnet/api/system.char.iswhitespace)の集合で判定する。単独Assetsは旧PowerShell比較どおり大文字小文字を無視して拒否する一方、Assets/ prefixの除去は完全一致時だけ行う。
