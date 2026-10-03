# G2取り込み基盤の契約

## 確定した選定

2026-10-03 UTCに作者承認。

- GR77のGLB解析は既存JsonDocumentとstbを活用し、コンテナ・バッファ・埋め込み画像の不足処理をCoreの共有部品へ追加する。新しい外部glTF parserは導入しない
- GR78の設定はソース隣の<ソース名>.import.jsonを一次の置き場にし、cookとloose直接読込で同じ変換を使う。欠如時は恒等で既存出力を維持する
- BVH/FBXの採否、形式のStage A/B、材質既定、制約緩和等は別の未決事項。今回の2選定から包括承認を推定しない

## GR77: コンテナ層

Resource/GltfContainer.hのParseContainerは、独自Spanだけを使う無割当・I/Oなしの構造解析である。入力をコピーせず、Json/Binを入力内のviewとして返す。使用中は入力の寿命・アドレス・内容を維持する。JsonDocumentや文字列所有型への変換は後続層が行う。

- SuccessはGLB v2のコンテナ構造が有効であることを表す
- NotGlbはmagicがGLBではない入力。UTF-8 BOMを先頭から除いた全入力をJsonへ返す。拡張子は使わない。空や不正なテキストもこの段階ではNotGlbで、JSON層が拒否する
- その他の失敗は出力viewを空に戻し、途中まで解析したJSON/BINを公開しない
- GLB headerのversionは2、宣言全長と実長を厳密一致させる。8byte chunk headerとpayloadを残長の減算で検証し、非整列アドレスにもバイト単位のlittle-endian読取りで対応する
- chunkLengthは4の倍数。JSONは1つで必ず第1、BINは0または1つで存在時は第2。未知chunkは構造を検証して無視する。JSON→未知→BINは拒否し、JSON→BIN→未知やJSON→未知は許可する
- HasBinでBINなしと長さ0のBINを区別する。JSON/BINの末尾paddingは保持する
- JSON文法/UTF-8/asset.version、JSON内のbuffersとBINの対応、byteLengthと0〜3byte paddingの照合、data URI/base64、画像形式はこの関数の責務ではない。構造上有効でも、不正JSONや空JSONを資産として受け入れたことにはならない

根拠：[Khronos glTF 2.0 §4.4 Binary glTF](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#binary-gltf-layout)、[§3.6.1.2 GLB-stored Buffer](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#glb-stored-buffer)。

## 検証範囲

GltfContainerTestは既存CookedMeshTest束のMEMBERとし、新しい実行ファイルをCMakeへ増やさない。同じTest.cppと実GltfContainer.cppをLinuxで直接compileして、借用pointer/length、既知chunkの順序/重複、未知chunk、切断/巨大length、非整列アドレス、失敗時clear、4,000固定seed破損入力での境界と非変更を確認する。

この部品だけではcooker/loose/skeletalの既存入口は変わらない。3経路への配線、JSON/buffer/image解決、GLBからの実資産取り込みは後続タスクで検証する。Windows/Core全体・CMake bundle実行の合格とは区別する。

## GR77: 厳密Base64

Text/Base64.hは独自Spanだけを使う無割当の下層部品。GetBase64DecodedSizeで全入力を検証し、DecodeBase64は容量と入出力spanの非交差も確認した後に書き込む。サイズと状態はBase64DecodeOutcomeの値で返し、サイズ参照と入出力bytesのaliasを作らない。失敗時は出力bytesを変更せずSize=0、成功時も必要長以後の出力領域を保持する。空入力は0byte成功とし、bufferの最小長は上位層で決める。

標準alphabet、4文字単位、最後だけの1〜2個のpaddingを受け入れ、空白・非alphabet・base64url・省略paddingを拒否する。末尾の未使用bitが非0の非canonical表現も拒否する。これは[RFC 4648 §3–5](https://www.rfc-editor.org/rfc/rfc4648.html)がdecoderに許している厳密な実装方針であり、RFCが全decoderに非canonical拒否を強制すると解釈しない。

data URIのscheme/media type/parameters、percent-decoding、JSONの文字列型はこのprimitiveに渡す前の上位層が扱う。2つの固定prefixだけの判定を汎用[RFC 2397](https://www.rfc-editor.org/rfc/rfc2397.html)準拠のURI parserと呼ばない。

Base64DecodeTestは既存CookedMeshTest束のMEMBER。RFC例、1byte全256通り、2byte全65,536通り、固定seedの3byte4,096件、異常padding/容量/alias/失敗時非変更を同じ実装へ直接リンクして検証する。data URIやGLBの消費経路への接続完了を意味しない。
