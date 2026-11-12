# Texture asset-set v1 の入力境界（G2-S6 / GR96）

TextureAssetSetSpecはJSONの寿命から独立した値所有型。既存CookTextureAssetSet.ps1のname/package_root/default_variant/texturesと、各textureのlogical_path/source_path/format/package_name/entry_name/variantを保持する。未知fieldとusageは旧wrapperどおり無視し、variantは省略時だけdefaultを使う。

版は整数tokenの1だけを許す。JsonValue::IsIntegerLiteralは小数点・指数の有無だけを記録し、既存AsNumberの変換や範囲は変えない。1.0/1e0/true/stringはspecの版として拒否する。

manifestに出るlogical/package/entry/variant/formatは既存単体CLIのASCII範囲を守る。pathの前後空白を除去し、区切りを/に統一、logicalとentryは大文字小文字が完全一致するAssets/だけを除去する。重複logical+variantとpackageはASCII大文字小文字を無視して拒否。元のtexture順序を変えず、indexをsortするためO(N log N)。

安全のため、出力pathは絶対path・traversal・空segment・ADSのcolon・末尾dot/spaceのWindows別名を拒否する。これは旧wrapperの曖昧な出力名をそのまま通さない明示的な制限。source_pathはUTF8値を保持する。repository-relative/drive/UNC等の解決、source存在、出力とのalias、symlink、実出力の保護は実行層の責任であり、parserだけで完了扱いにしない。

既知fieldの重複、型違い、NUL、空の必須文字列、空texturesを拒否する。成功時だけ出力specを置換する。cookサービス・CLI --asset-set・旧manifest serializer・増分印は後続の接続で実装する。

空白だけの必須値とpath前後の空白は[.NET Char.IsWhiteSpace](https://learn.microsoft.com/en-us/dotnet/api/system.char.iswhitespace)の集合で判定する。単独Assetsは旧PowerShell比較どおり大文字小文字を無視して拒否する一方、Assets/ prefixの除去は完全一致時だけ行う。

2026-10-04実受入: f3100edb564182d82d03851be1c1afed8bb1ce1e / run37207785735のWindowsでCore/tools build、12CPU、既存7CLI/79byte一致を確認。TextureAssetSetSpecTestは実2specの5+3row全所有fieldと入力破棄後の保持を検証する。試験fixtureの構築は3区間appendで行い、汎用String::replaceの別件不具合へ依存しない。

## spec v2 の generators

`AssetCook --asset-set` が受け付ける spec v2 では、トップレベルの任意欄 `generators` に cook 前の外部コマンドを宣言できる。省略または空配列は宣言なしを表す。spec v1 では `generators` は未知の欄として無視され、内容の検査も実行も行わない。

```json
"generators": [
  {
    "name": "make_texture",
    "command": ["Tools/TextureGenerator.exe", "--input", "Source/texture.src", "--output", "Generated/texture.ppm"],
    "inputs": ["Source/texture.src"],
    "outputs": ["Generated/texture.ppm"],
    "cwd": "."
  }
]
```

| キー | 必須・任意 | 値と上限 |
|---|---|---|
| `name` | 必須 | 前後に空白のない印字可能 ASCII の文字列、1〜128 byte。名前の重複は ASCII の大文字小文字を無視して拒否する |
| `command` | 必須 | 1〜256 個の文字列の配列。先頭はプログラム、以降は引数。各要素は UTF-8 で 1〜4096 byte、制御文字（0〜31、127）を拒否する。先頭は空白だけでも不可。引数の空白は保持する |
| `inputs` | 必須 | source root 相対の path の配列、0〜1024 個。実行時には各入力が通常ファイルであることを要する |
| `outputs` | 必須 | source root 相対の path の配列、1〜1024 個。全 generator の合計も1024個以下。ファイルまたはディレクトリを指定できる |
| `cwd` | 任意 | source root 相対のディレクトリ。省略または厳密に `"."` なら source root。明示した空文字列は不可 |

`generators` 自体は64要素以下。要素はオブジェクトで、上表以外のキーを拒否する。必須欄の欠落・既知欄の重複・型違いを拒否する。

`inputs`・`outputs`・通常の `cwd` は、前後の Unicode 空白を除き、`\` を `/` にそろえる。正規化後は印字可能 ASCII の相対 path に限り、絶対 path（先頭 `/`、drive・UNC を含む）、コロン、空の区間、`.`・`..` の区間、区間末尾の `.`・空白を拒否する。`Assets/` は取り除かない。`cwd` の `"."` の特例は空白除去より前に判定するため、`" . "` は拒否される。これらの path は `cwd` ではなく source root を基準にする。実際の path の解決と共有 validator による Windows 名等の検査、出力先の保護は実行層で行う。

拒否時の `TextureAssetSetResult.Field` は次のとおり。`TextureIndex` は通常 generator の0始まりの添字で、トップレベル・合計上限の失敗は `SIZE_MAX`、`generators.asset_source` は asset の添字を返す。

| 拒否する場合 | `Field` | エラー |
|---|---|---|
| `generators` の重複、配列以外、64要素超過、要素がオブジェクト以外 | `generators` | 順に `DuplicateField`、`InvalidType`、`InvalidValue`、`InvalidType` |
| 要素に未知のキーがある | `unsupported_generator_field` | `InvalidValue` |
| 名前の欠落・重複・型違い・空文字列、ASCII・長さ・前後空白の違反 | `generators.name` | `MissingField`・`DuplicateField`・`InvalidType`・`InvalidValue` |
| command の欠落・重複・型違い、空配列・個数超過、不正な引数 | `generators.command` | `MissingField`・`DuplicateField`・`InvalidType`・`InvalidValue` |
| inputs の欠落・重複・型違い、個数超過、不正な path | `generators.inputs` | `MissingField`・`DuplicateField`・`InvalidType`・`InvalidValue`・`UnsafePath` |
| outputs の欠落・重複・型違い、空配列・個数超過、不正な path | `generators.outputs` | `MissingField`・`DuplicateField`・`InvalidType`・`InvalidValue`・`UnsafePath` |
| cwd の重複・型違い、不正な path | `generators.cwd` | `DuplicateField`・`InvalidType`・`UnsafePath` |
| 名前が ASCII の大文字小文字を無視して重複する | `generators.duplicate_name` | `InvalidValue` |
| 全 generator の outputs の合計が1024個を超える | `generators.outputs` | `InvalidValue` |
| 同じ asset の source を2つ以上の generator の output が覆う | `generators.asset_source` | `InvalidValue` |
| 同じ generator 内または別 generator 間で outputs が同一または祖先・子孫になる | `generators.outputs_overlap` | `InvalidValue` |

JSON のメンバ名に NUL がある場合は、その欄を取得する際に `InvalidValue` となる。文字列は JSON から UTF-8 へ変換して検査する。outputs の一致・祖先判定は正規化した区間境界と ASCII の大文字小文字を無視した比較を使う。asset source を覆うかの判定は、`source_path` が上記の相対 path として正規化できる場合だけ行い、同一 path または output の子孫なら覆うと扱う。

検査の順は、spec の版・トップレベル欄・全 assets とその論理キー／package の重複、次に `generators` の配列と個数、宣言順の各要素（未知キー → name → command → inputs → outputs → cwd）、最後に全体の名前重複 → outputs 合計 → asset source の複数被覆 → outputs の重なりである。同じ入力が複数の条件に触れる場合は先の失敗を返す。したがって asset source の複数被覆は outputs の重なりより先に報告される。成功時だけ出力 spec を置換する。
