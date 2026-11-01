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

この部品だけではcooker/loose/skeletalの既存入口は変わらない。3経路への配線、JSON/buffer/image解決、GLBからの実資産取り込みは後続タスクで検証する。WindowsではDebugのGltfContainerTestを含む既存束のCPU試験が合格した（2026-11-02、GR77: native受入れ）。Core全体や表示の受入れとは区別する。

## GR77: 厳密Base64

Text/Base64.hは独自Spanだけを使う無割当の下層部品。GetBase64DecodedSizeで全入力を検証し、DecodeBase64は容量と入出力spanの非交差も確認した後に書き込む。サイズと状態はBase64DecodeOutcomeの値で返し、サイズ参照と入出力bytesのaliasを作らない。失敗時は出力bytesを変更せずSize=0、成功時も必要長以後の出力領域を保持する。空入力は0byte成功とし、bufferの最小長は上位層で決める。

標準alphabet、4文字単位、最後だけの1〜2個のpaddingを受け入れ、空白・非alphabet・base64url・省略paddingを拒否する。末尾の未使用bitが非0の非canonical表現も拒否する。これは[RFC 4648 §3–5](https://www.rfc-editor.org/rfc/rfc4648.html)がdecoderに許している厳密な実装方針であり、RFCが全decoderに非canonical拒否を強制すると解釈しない。

data URIのscheme/media type/parameters、percent-decoding、JSONの文字列型はこのprimitiveに渡す前の上位層が扱う。2つの固定prefixだけの判定を汎用[RFC 2397](https://www.rfc-editor.org/rfc/rfc2397.html)準拠のURI parserと呼ばない。

Base64DecodeTestは既存CookedMeshTest束のMEMBER。RFC例、1byte全256通り、2byte全65,536通り、固定seedの3byte4,096件、異常padding/容量/alias/失敗時非変更を同じ実装へ直接リンクして検証する。data URIやGLBの消費経路への接続完了を意味しない。

## GR77: data URIとBINの意味境界

Resource/GltfBufferSource.hは、JSONやファイルI/Oへ依存しない意味検証を提供する。

- ParseDataUriはscheme/media typeの大小文字を区別せず、application/octet-stream・application/gltf-buffer・image/png・image/jpegを分類する。type/subtypeのpercent表記も扱う。MIME parametersは文法を検証して許可し、最後のliteral base64 flagを要求する。省略MIMEのtext/plain、対応外MIME、非base64はこのglTF用profileでは受け入れない
- parameterはASCII token属性と、tokenまたはpercent表記を使ったprintable ASCII値を扱う。raw文字はRFC2396のurlcharにも制限し、#や^等はpercent表記が必要。payloadのraw文字にも同じURI文字制限を適用する。未知のparameterを変換設定として解釈しない。汎用の全media type/全URI実装ではない
- 戻りviewはURIのpayloadを借用し、percent復号後の必要長を持つ。成功はURI header/escapeの検証だけ。呼出側がpercent復号→厳密Base64検証/復号を必ず行う。壊れたBase64をURI構造の成功だけで受理しない
- GetPercentDecodedSize/DecodePercentBytesは%HHをbyteへ戻すだけで、+を空白へ変換しない。空やNULを含むbyte値も機械的に扱う。文字コード、ファイルパス、NUL可否は利用層の責務であり、この成功を安全なパスとみなさない。容量・入出力span交差を検証し、失敗時出力非変更、サイズは値返却
- BindGlbBufferはuriのないbuffers[0]専用。GLB/BINの存在、index0、正の宣言長、BINの4byte整列長、宣言長との差0〜3、余剰byteが0であることを検証し、宣言範囲だけを借用viewで返す。失敗viewは空。JSONの型/uri未定義は呼出側が先に確認する
- BufferSetはapplication系、ImageSourceはimage系を採用する。generic URI分類で成功しただけでは、その用途で使えるMIMEと判断しない

GltfBufferSourceTestは対応MIME/parameter/percentの復号連携、全256byteのpercent変換、異常値/容量/交差、BINの0〜3byte padding/宣言長/index/欠落と4,000固定seed変異を検証する。まだ実ファイルのbuffer所有や3消費経路への接続は行わない。

## GR77: BufferSetの所有と外部ファイル境界

BufferSetはJSONから独立したBufferRequest列を受け取り、外部ファイル/data URIは独自VariableArrayで所有、GLB BINは元ファイルへ借用する。所有配列自身を指すSpanをメンバに保存せず、取得のたびにviewを作る。copyは所有bytesを複製し、GLBだけは同じ入力へ借用する。copy代入は候補copy→noexcept swapとし、内側の確保例外で旧metadataとbytesが分離しない。move/copy/再解決後も、呼出側はGLBの寿命とアドレスを維持する。

- GetBytesはaccessor用の宣言byteLength範囲。GetSourceBytesは外部ファイルの余剰byteを含む全量、data URIは復号全量、BINは宣言範囲。旧.gltf hashではBOMを含む元source全体と各外部ファイル全量をJSON順で使い、宣言範囲だけでhashしない
- data URIはapplication/octet-streamかapplication/gltf-bufferに限定し、画像MIMEをbufferとして受け入れない。percent→厳密Base64の後、宣言長以上のbytesがあることを確認する
- 外部URIはpercent復号後の正規相対ASCII pathを使用する。NUL/control/non-ASCII、絶対path、backslash、colon、空/./../末尾dot・spaceのsegmentを拒否する。raw query/fragmentもファイル名とみなさない。ASCII範囲は既存cookerと同じ制約、末尾dot/spaceはWindowsの別名解釈を避けるための追加拒否
- reader callbackは同期・呼出中の借用。入力/request/container/outSetを変更せず、実filesystemのsource directory境界を守る責務を持つ。任意のcustom callbackが安全なファイルアクセスを自動的に保証されるわけではない
- 標準ReadBufferFileはSourceFileのparentと候補をweakly_canonicalし、path component単位で内側か確認する。文字列prefixだけで判断しない。通常ファイルだけを読み、directory/device/FIFO等を対象にしない。source directory外のsymlinkを拒否する
- filesystemが読込中に悪意をもって差し替えられない通常のローカル資産運用を前提とする。canonical確認とopenをOSレベルで不可分にしたsandboxではない
- 候補を組み立てた後にswapして公開する。失敗/例外は出力setを空にし、allocation/reader例外は再送出する。旧setから取得したviewはReset/再解決/破棄後に利用しない。読込元GLBを出力set自身の破棄対象storageへ置かない

実BufferSet/FileReader/Test sourceはg++のsyntax/Werror確認とMEMBER object compileが可能。所有/コピー/例外/実ファイル・symlinkの試験は既存CookedMeshTest束へ登録するが、実allocatorを含むLinux実行は未確認。Linuxの実allocator接続のcompile試行では既存MemoryOverrides.hのutility不足、MemorySystem.cppではWindows.h依存で停止した。WindowsのDebugではGltfBufferSetTestを実行し合格した（2026-11-02、GR77: native受入れ）。代替allocatorは作らない。GltfBufferUriTestは実BufferSet.cppから相対path検証だけをsection GCで直接リンクし、通常/O2/ASan・UBSanで実行する。純predicateの成功を所有/ファイルI/O試験の成功と扱わない。

JSONからのdescriptor構築とcooker/loose/skeletalへの接続は後続。looseの旧accessor計算にはunchecked加算/乗算があるため、共通bufferの宣言境界を導入する際に別途修正する。

## GR77: JSON buffer記述の接続

ResolveJsonBuffersは既存JsonDocumentのroot.buffersだけを読み、BufferSetへ渡す。rootはobject、buffersは非空array、各要素はobjectを要求し、buffers/byteLength/uriの既知field重複を拒否する。未知fieldやextrasを取り込み設定として解釈せず、この関数だけでglTF全体を検証したとは扱わない。

byteLengthはnumber型の有限・正の整数で、2^53−1とsize_t上限以内に限定する。型と範囲の確認後だけcastする。uriは未定義の場合だけBIN対象で、null/数値を未定義とみなさない。uri文字列は既存cookerと同じASCII profileを使い、percent復号とpath/data URIの判断はBufferSetへ渡す。

URIの所有配列とrequest配列を先に確保し、URIの確保後だけ借用Spanを作る。Resolve呼出しが終わるまでそのstorageを変更しない。JSON documentは解決後に破棄でき、残るsetは外部/data URI bytesを所有し、元GLBだけを借用する。失敗/例外ではsetを空にし、allocation/reader例外を伝播する。

ParseBufferByteLengthの実pure関数は通常/O2-NDEBUG/ASan・UBSanで確認し、GltfBufferNumberTestを既存束へ追加する。GltfBufferJsonTestは型/欠落/重複/uri空・null/3source混在/JSON破棄後の寿命を確認する。WindowsのDebugで実行し合格した（2026-11-02、GR77: native受入れ）。LinuxではJsonDocumentのWindows.h依存によりcompile/実行未確認。numeric helperの成功でJSON統合の合格を代用しない。

## GR77: 静的cookerのbuffer接続

CookGltfToNvmeshは共有containerでJSONとBINを分離し、JSON部分だけのNUL検査とJsonDocument解析を行う。ResolveJsonBuffers/BufferSetで外部・data URI・GLB BINを解決し、既存のaccessor範囲/整列検証とmesh抽出へ宣言長のviewを渡す。GLB BINはCook呼出の元sourceを借用し、複製しない。完成NVMESHは従来どおり自身のbytesを所有する。

- source hashは元source全量をBOM込み・LE64長prefix付きでhashし、外部bufferだけをJSON順に全実bytesと長prefixで追記する。外部bufferの宣言長を超える余剰も保持する。GLB/data URIは元sourceに既に含まれるため二重に加えない。GLBに追加の外部bufferがある場合はその外部bytesを追記する
- 従来の一mesh/一primitive・mesh-local頂点・cluster・material path・既定値・NVMESH形式を保持する。外部imageは参照pathを作るだけで、この段階で画像ファイルを新たに読まない。embedded imageのpackage生成、looseとskeletalの接続はまだ含まない
- data URI bufferは対応application MIMEの正規Base64を受理する。旧smokeのAA==は宣言長に足りないため、非対応ではなく短いbufferとして失敗する。重複field・非正整数・危険な外部pathなど共有resolverの拒否規則が適用される
- StaticGltfBufferCookTestを既存CookedMeshTest束に追加し、AssetCookLibをリンクする。102byte三角形の外部/GLB/data URIのpayload一致、BOM/外部余剰/混在GLBのhash、percent path、短さ/不正padding/壊れたGLBと失敗時出力保持を検査する

Linuxでの実cookerと新試験のcompileは既存String.hのWindows.h依存で停止した。WindowsのVisual Studio構成ではDebugでStaticGltfBufferCookTest・AssetCookMeshSmoke・AssetCookGlbSmokeを実行し合格した（2026-11-02、GR77: native受入れ）。GLB containerとbuffer意味helperの通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）/MEMBER回帰は実行成功。fixture JSON/既知binaryの文法・整合性照合と、skeletal/material/cluster部のsource不変確認は統合実行と区別する。

## GR77: 共有画像source

GltfImageSourceは画像記述の解決とbytesの所在を扱い、stbでの画像decodeやファイルI/Oは行わない。file URIはpercent後の相対ASCII pathを所有して返す。data URIはPNG/JPEGの正規Base64を復号して所有し、bufferViewはbuffer index/offset/lengthだけを保存する。GetBytesには解決時と同じ内容のBufferSetを渡し、その宣言範囲内のviewを得る。GLB BIN全体も画像領域もこの段階で複製しない。

- images/uri/bufferView/mimeTypeおよび参照するbufferViewsの既知field重複・型を検証する。uriとbufferViewはどちらか一方だけ。bufferViewにはPNG/JPEG mimeTypeが必須で、offset/lengthは安全な整数と減算式で範囲検査する。画像viewのbyteStride指定は値・型にかかわらず禁止する
- data URIはimage/pngまたはimage/jpegに限定し、宣言mimeTypeとの矛盾・空復号・壊れたpercent/Base64を拒否する。外部uriのmimeType省略はUnknownのまま後続の画像読込へ委ねる。file名だけからformatを保証せず、外部fileをこの部品で実読込しない
- MIMEやbytesの解決成功は画像内容のdecode成功ではない。PNG/JPEGとしての実内容・寸法は後続の画像decoderが検証する。WebP/KTX2等の宣言MIMEは受理しない
- 自己所有storageへのSpanを保持しない。data URI/file pathのcopyは独立所有、copy代入は候補copy→swap。bufferViewのcopyは所在だけを複写し、同じBufferSet内容とそのsource寿命を呼出側が維持する
- Resolve失敗/例外は出力を空にし、確保例外は伝播する。bufferViewのGetBytesも再度範囲確認し、BufferSetが空になっていた場合は空viewを返す

GltfImageRangeTestは実inline helperの範囲/空/null/size_t境界/借用を通常・O2-NDEBUG・ASan/UBSan（LeakSanitizer除外）で実行する。GltfImageSourceTestは既存束で記述/所有copy-move/data URI/借用view/MIME/失敗clearを検査する。WindowsのDebugで実行し合格した（2026-11-02、GR77: native受入れ）。Linuxの実JsonDocument/所有配列の統合実行はWindows.h依存で未検証。cooker/stagingへの画像接続やtexture package出力はこの部品には含まない。

## GR77: 静的cookerの埋込み画像結果

MeshCookResult.EmbeddedImagesは参照された埋込み画像をimageIndex順に返す。論理pathはモデルの論理path全体に.img<imageIndex>.png（JPEGは.jpg）を付け、同じpathをNVMESH材質へ格納する。Albedoはnvtex.v0.rgba8.srgb、Normal/ARMはnvtex.v0.rgba8.linear。同じimageIndexと互換formatは1件へ集約してRolesを合成し、sRGB/linearの衝突は明示エラーにする。画像bytesのsource hashは元画像bytesのFNV-1a64。既存の色空間・material既定・頂点/cluster/wire形式は変えない。

- 元GLBのBIN内画像だけはsourceBytesへ借用する。呼出側はEmbeddedImagesを利用する間、元GLBの寿命とアドレスを維持する。成功時の結果置換が借用元を解放しないよう、GLB入力はoutResult自身の所有storageに置かない。外部buffer/data URI由来の画像は、cookerローカルBufferSet/ImageSourceが破棄されても有効なよう画像部分だけを結果へ所有コピーする
- MeshEmbeddedImageの所有copyは独立bytes、借用copyは同じ元GLBへの参照。copy代入は候補copy→swap。SetBytesは空/nullや所有storageを解放して自己借用する操作を拒否し、所有切替時の確保失敗では既存bytesを維持する
- 外部画像URIは従来どおり材質pathだけを保存し、画像fileの実読込やEmbeddedImagesへの追加はしない。occlusion/emissive等の新しい材質処理は追加しない
- StaticGltfBufferCookTestに1px PNGを加え、外部参照とのNVMESH bytes一致、3roleのpath/format/hash、GLB借用、外部buffer/data URI所有、copy/move、同format共有、衝突時出力保持を登録する。画像bytes変更はmodel source hashへ反映し、画像内容のdecodeは後続texture cookerへ委ねる

Mainのmodel単独出力ガードは、後述のmodel+N texture一括出力へ置き換えた。WindowsのDebugでStaticGltfBufferCookTest・AssetCookGlbSmokeを実行し合格した（2026-11-02、GR77: native受入れ）。Linuxの実cooker/新画像fixtureはWindows.h依存でcompile/実行未検証。実物の画像decodeとpackage出力はnative受入れ節に記す。

## GR77: modelとtextureの一括package出力

MainはEmbeddedImagesがあるときだけ専用の一括経路を使う。全画像をPNG/JPEGのheaderで制限して既存TextureCookerへ渡し、model1件と参照画像ごとの単一entry NVPKGを準備する。modelは指定--out、textureはその同じdirectoryに<model package filename>.img<N>.nvpkgを作る。NVPKG多entry形式は導入しない。材質はpathだけを持つため、textureのvariantはTextureAssetResolverが使うdefaultに固定する。同じlogical modelの複数variantもこの画像pathを共有するため、variantごとに異なる画像を独立保持する仕様ではない。別の画像を独立させる場合は別logical model pathを使う。外部画像だけの従来モデル経路はそのまま維持する。

- 変換が全て成功してからpackageをメモリ上で解析し、entry/type/hash/全payloadとParseCookedMesh/ParseCookedTextureを検証する。画像変換失敗ではpackage/manifestを書かない
- 元source/manifestと出力群のaliasをcanonical path、既存fileのequivalent、Windowsでは大文字小文字を区別しないordinal比較で拒否する。安定したローカルfilesystemを前提とし、競合するsymlink差替えに対するOS sandboxではない
- BuildMergedManifestJsonはincoming reference列を受け取り、logicalPath/kind/variantの重複を拒否する。同keyだけを更新し、それ以外のentryと既存の骨格metadataを保持する。audioも1件の列として同じ処理を使う。formatをkeyに加えない。保持する別keyが使うbacking packageと今回の出力pathが重なる場合は、同payloadの可能性があっても書込み前に拒否する
- package群を書き、候補manifestからAssetSystemでincoming全件をSuccessCookedとして解決し、bytes一致を確認した後、manifestを1回だけ書く。Core配列用のbinary/text writerはflush結果も確認する
- 複数fileのatomic transactionではない。途中のI/O失敗時には既に書かれたpackageが残り、既存packageが置換されている可能性もある。manifest書込み自体のI/O失敗もロールバックしない。この限界を「失敗時すべて無変更」とは表現しない

AssetCookGlbSmokeはCMake3.14/既存PowerShell方式で、小GLBのmodel+texture3、role format/default variant、全新規entryの実package解決（Mainの自己検証）、既存骨格metadata保持、再cookのmanifest bytes一致、画像差替えhash、不正画像/重複manifestの拒否、Windows未作成pathのcase alias拒否、実audio packageへの暗黙texture出力衝突で既存package/manifestを保持する回帰を登録する。保持用の骨格entryはmetadata保存だけのfixtureで、実package解決の対象にはしない。

実GLB3fixtureは共有container/BIN/rangeとstbi_load_from_memoryへ直接通し、正常2件の1px RGBA値と異常1件のdecode拒否を確認した。LinuxでのMain/CMake/PowerShell/CLI smokeはWindows.h依存およびCMake・PowerShell不在により未検証。Windows PowerShell 5.1とVisual Studio構成のDebugではAssetCookGlbSmokeを実行し合格した（2026-11-02、GR77: native受入れ）。fixture試験と実物GLBの受入れは区別する。

## GR77: 骨格の共有buffer接続

SkeletalGltfDecodeは元byte列の入口を持ち、共有containerでGLB/JSONを分離してJSON部分だけを検査する。既存のprimitive/skin/animation/accessor確認順を保った共通内部処理でResolveJsonBuffersを呼び、accessorはBufferSetの宣言範囲を参照する。128関節上限、単一clip、関節/mesh node変換、頂点重み・単位・NVSKEL形式は変えない。Armature親や複数clip等のGR82契約を先取りしない。

- CookGltfToNvskelは元bytes入口とBufferSetを用い、共有source hashでBOMを含む元sourceと外部buffer全量だけをhashする。GLB/data URIを二重に加えない
- AnalyzeSkeletalはprofiling付きReadBinaryFileから同じ入口へ渡す。decodedの頂点/関節/clipは自身の配列へ所有し、元GLBの破棄後も有効。hash用outSourceBuffersを求める場合だけ、そのBIN viewより元GLBを長く保持する
- 旧String入口も残す。旧outSourceBuffers配列が必要なときは、外部/data URIの全source bytesを独立所有コピーで返す。宣言長を超える外部file余剰も保持する。String入口はGLBコンテナを受けない
- 標準共有readerの相対ASCII URI/percent/canonical containmentを骨格にも適用する。以前の単純なdirectory結合では読めた入力directory外へのURIは受理しない
- 各入口は正常失敗でsource出力を空にする。元bytesをoutSourceBuffers自身に所有させない。旧StringとSpanのoverloadがあるため、空入力の試験は型を明示する

CookedSkeletalAssetTestのM9 fixtureからGLB/data URIを組み、decoded値、NVSKEL全bytes、SourceHash、BIN pointer、BOM/外部余剰、旧String出力、不正GLB/欠落BIN/短いdata URIと出力clearを登録する。試験専用directoryは排他的な一意名で作り、並列試験の既存directoryを削除しない。WindowsのDebugでCookedSkeletalAssetTestをUTF-8 code pageで実行し合格した（2026-11-02、GR77: native受入れ）。Linuxの実骨格decoder/cooker/analyzer/native試験はWindows.h依存でcompile/実行未検証。実物dog_riggedの拒否はnative受入れ節に記す。

## GR77: loose accessorの宣言範囲

GltfAccessorRangeはactual buffer長・宣言buffer長・bufferView offset/length・accessor offset/count/element/strideから、範囲が成立するときだけ開始位置と必要byte数を返す純粋なhelper。加算/乗算する前に減算/除算で境界を証明し、0 count・0 element・短いstrideも拒否する。形式ごとの整列/type/stride規則を統合するものではない。

GLTFAnalyzerは4つの使用accessorをこのhelperへ通し、実fileに余剰があっても宣言buffer/view外を読まない。安全性を確認した不変のmetadataだけで、その後のpointerとstrideを計算する。既存の頂点値・index winding・cluster・材質既定は変えない。private ModelStagingのCPU入口を追加し、既存の内部staging処理をGPU生成なしで呼べるようにする。この入口は候補を完成させてから出力へ移す。

GltfAccessorRangeTestは小範囲の全列挙とsize_t極値、対応compilerではunsigned128の独立oracle（2万件、うち1万件は有効な広い範囲）を実行する。Linuxの通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）で各5,438,750件とMEMBER compileが成功。追跡glTF6fileの使用attribute/index24layoutも実helperに通過した。12件は実file長、fixtureの12件は宣言長をactual長としたmetadata確認で、後者は実I/O成功を意味しない。

GltfAccessorStagingTestは小三角形の成功と短いview/短い宣言buffer/巨大offset/0countを既存束へ登録し、失敗時に旧stagingを保持することを検査する。WindowsのDebugでGltfAccessorStagingTestを実行し合格した（2026-11-02、GR77: native受入れ）。LinuxのGLTFAnalyzer/native試験はWindows.h依存でcompile/実行未検証。現在のJSON数値getterや形式全体の厳格化・static parser統合まで済んだとは扱わない。

## GR77: 画像bytesのCPU staging

ModelStagingはfile読込とstbi_load_from_memoryを分離し、StageStandardTextureBytes/StageArmTextureBytesからPNG/JPEG bytesを所有CPU stagingへ変換できる。新bytes入口とAssetCookの埋込み画像は同じsignature probeを使う。probeは形式の候補を判定するだけで、切れたPNG/JPEGの完全性はdecoderが検証する。既存file入口の対応形式は狭めない。

- inputが空/nullまたはINT_MAXを超える場合はstbへ渡さない。正のdimensionとpixel byte数のsize_t範囲も確認する
- stbが返すpixelsは非copyのscope ownerで解放し、エンジン所有配列への確保/copyやログ中に例外があっても放置しない。返すPixelDataは独立所有で、stb pointerや入力の借用ではない
- 標準textureは従来どおりRGBA8_UNORM。ARMはR/G/BをAO/Roughness/MetallicのR8_UNORM3枚へ分ける。既定色空間やチャンネル写像を変えない
- bytes入口は成功した候補だけを出力へ移し、falseでは旧出力を保持する。ARMの3出力が同じobjectをaliasする呼出しは拒否する。GPU resource生成やアップロードは行わない

GltfImageRangeTestへPNG/JPEG signatureと切断/null判定を追加し、同じ実headerを通常/O2-NDEBUG/ASan・UBSan（LeakSanitizer除外）とMEMBERで確認する。ImageBytesStagingTestは小PNGのfile/bytes pixel一致、ARM分解、format、異常input/長さ/alias拒否と旧出力保持を既存束へ登録する。WindowsのDebugでImageBytesStagingTestを実行し合格した（2026-11-02、GR77: native受入れ）。Linuxの実ModelStaging/native試験はWindows.h依存でcompile/実行未検証であり、signatureや単独stbの確認と区別する。

## 必須glTF拡張の共通判定（GR77）

静的cooker・loose静的staging・骨格decoderはGltfDocumentProfileの同じ判定を使う。
現在対応する必須拡張の許可リストは空。extensionsRequired省略/空配列は許可し、
重複field・配列以外・文字列以外の要素と非空配列は拒否する。Draco、meshopt、
mesh_quantization、texture_transformも未対応なので、必須指定を黙って無視しない。
optionalなextensionsUsedの宣言だけでは拒否しない。これは全glTF schema検証ではない。

静的cookerの通常診断を保持し、骨格は既存InvalidDocumentへ写像する。
判定はbuffer読込やgeometry抽出より前。GltfDocumentProfileTestに共通判定と
cooker/骨格入口、GltfAccessorStagingTestに正常geometryへ必須拡張を付けた拒否を登録。
Linux環境では実JsonDocument/consumerがWindows.hへ依存するため、登録試験のnative実行は未確認。WindowsのDebugでGltfDocumentProfileTest・GltfAccessorStagingTestを実行し合格した（2026-11-02、GR77: native受入れ）。

### loose頂点indexの境界（GR77）

accessorのbyte範囲が正しくても、復号したindex値が頂点数以上なら不正である。
loose静的ロードは全index値を検査してからwinding変換・MeshClusterizerへ渡す。
空/nullのindex列と頂点数0も拒否し、CPU stagingの候補が失敗した場合は既存出力を保持する。
純粋検査は通常・最適化・ASan/UBSan（LeakSanitizer除外）で確認した。
実stagingのindex=3/65535回帰はWindowsのDebugでGltfAccessorStagingTestを実行し合格した（2026-11-02、GR77: native受入れ）。Linuxのnative実行はWindows.h依存により未確認。

## loose静的モデルのGLB・埋込み画像接続（GR77）

GLTFAnalyzerのstatic stagingは元fileを保持して共有Container/ResolveJsonBuffersを使う。
JSONだけを文字列化し、GLBのBINは元fileから借用する。旧独立buffer parser/loaderは削除した。
geometryのaccessor/view解析、時計回りへの変換、最初のmesh/primitive、材質既定は維持する。
buffer URIは共有resolverの相対ASCII/canonical境界に従う。

参照された画像だけImageSourceで解決する。bufferView/data URI画像はPNG/JPEGを検査して
CPU stagingがpixelsを所有し、BuildModelStagingを抜けた後は元file/bufferに依存しない。
Albedo/Normalは既存loose経路同様RGBA8_UNORM、ARMはRGBをAO/Roughness/MetallicのR8に分離する。
元画像3枚ならstagingは5枚となる。埋込みのRequestPathは空に保ち、finalizerのmanifest検索へ回さない。
外部画像の論理RequestPathと解決済みfallbackは従来同様で、相対requestの画像はfinalizeまで遅延する。
参照された不正image/不正PNG/JPEGは失敗し、黙って既定画像へ置換しない。
既存の材質欠如/材質index範囲外時のfallbackは維持する。

GltfLooseSourceTestを既存束へ登録し、外部絶対pathの実pixels/相対pathの遅延、
GLB、data URI buffer、data URI image、外部bufferViewのgeometryと5画像、
不正container/image/view/必須拡張、失敗時の出力保持を検査する。
生成した1200byte fixtureの実Container/range/stbは通常・最適化・ASan/UBSanで確認した。
WindowsのDebugでGltfLooseSourceTestを実行し合格した（2026-11-02、GR77: native受入れ）。Linuxの実JsonDocument/GLTFAnalyzer/staging試験はWindows.h依存でcompile停止し未実行。
実物GLBのcooked受入れはnative受入れ節に記す。loose経路の実物GLB読込とGPU描画は未実行。

### 埋込み画像のMIMEとsignature一致（GR77）

MeshCookerの結果生成前とloose画像解決はMatchesEmbeddedImageMimeの同じ判定を通す。
PNG/JPEGの宣言と実signatureが逆の場合は拒否し、拡張子だけ誤った画像資産を作らない。
未知signatureのCLI診断は従来値を維持し、宣言とsignatureの矛盾は別の明示エラーにする。
外部画像は従来どおり参照を保持し、そのdecoder対応形式を狭めない。
signature一致は画像全体の完全性の証明ではなく、最後のdecode検証はstbが担当する。

純粋判定は通常・最適化・ASan/UBSan（LeakSanitizer除外）で実行し、MEMBERはcompileを確認。
PNGをJPEGと宣言したGLBをCLI smokeへ足し、失敗時に既存manifest/modelが変わらないことを登録した。
cookerとlooseの不一致・出力保持はWindowsのDebugでStaticGltfBufferCookTest・GltfLooseSourceTest・AssetCookGlbSmokeを実行し合格した（2026-11-02、GR77: native受入れ）。Linuxのnative実行はWindows.h依存で未確認。

## GR77: native受入れ

### CPUの試験

2026-11-02、Windows PowerShell 5.1.22621・CMake Visual Studio構成。DebugのCookedMeshTest/AssetCookビルドはEXIT=0。CPUだけのctestはcode page 65001で19/19 Passed、既定932では18/19。CookedSkeletalAssetTestだけが警告文のUTF-8/cp932照合で失敗する既存のGR77範囲外の問題で、未修正。AssetCookMeshSmoke 24.4 s、AssetCookGlbSmoke 2.1 s、CookedSkeletalAssetTest 12.4 s、ほかは各0.1 s未満。AssetCookGlbSmokeのmanifest SHA-256計算は、PowerShell 7側のモジュール誤読込を避けるため.NETに変更済み。 この19件はfixtureを含むCPUの試験であり、実物GLBやGPU表示の受入れとは区別する。

### cooked

2026-11-02、実物GLBをAssetCookのCLIでcookした。各manifestは新しい空directoryから開始した。

| 入力 | 規模 | 構成 | 終了コード | 壁時計 | ピーク作業セット |
|---|---|---|---|---|---|
| wooden_shack.glb | 66,800,548 bytes・423,360頂点・596,648面・画像3枚 | RelWithDebInfo | 0 | 9.394 s | 720,613,376 bytes（687.23 MiB） |
| dog_trellis2_60k.glb | 33,033,908 bytes・71,473頂点・59,941面・画像3枚 | Debug | 0 / 0 | 25.337 / 16.200 s | 635,424,768 / 636,207,104 bytes |

ピーク作業セットはSystem.Diagnostics.Process.PeakWorkingSet64を100 msごとと終了時に読み、観測最大値を記録した。dogの時間はDebugでの受入れ記録であり、性能評価ではない。

wooden_shackはmodel 1件・texture 3件を出力し、成功出力に`textures=3 vertices=423360 indices=1789944`を確認した。SingleAssetCook.cppの全incoming entryの実package解決・payload照合を通過してから出る成功出力であり、CLIの自己検証が合格した。manifestの全cooked_packageが実在し、entry名/type、package内PayloadHash、payloadから独立に計算したFNV-1a64がmanifestのcooked_hashと一致した。

dogは代替入力を使わず、別directoryで2回cookした。manifestと全4 packageはバイト単位で一致した。両出力に共通のSHA-256は次のとおり。

| file | SHA-256（両出力一致） |
|---|---|
| manifest.json | `70f8fcac80b9095987ef8abaca5db1fbdcd515114a7043e4e26c0744ef3d4e42` |
| Cooked/model.nvpkg | `128ee0f65c687282a2096afdbee27a8898869141e5da33c8ee10c27a229477d2` |
| Cooked/model.nvpkg.img0.nvpkg | `1f9ffdea6d8488c45d36ff75f3545b6c013ff4368fff853992fca0febcbe2f9d` |
| Cooked/model.nvpkg.img1.nvpkg | `dfb2472f183df14c3d4bd03948d2c2a13ce7c8c4fabba0f7294eb8d1347ea236` |
| Cooked/model.nvpkg.img2.nvpkg | `a17c7c00d6e1397f49c48d06beb58360a164840690724efde5b3ae3c759d38cb` |

同じdogをJSON chunk・BIN chunk・bufferViewから無変換で切り出した画像3枚に分割し、buffers[0].uriとimagesのuriを書き換えた.gltfをDebugでcookした（終了コード0、1.301 s）。NVMESH entry payloadはGLB由来3,081,964 bytes、分割由来3,081,908 bytesで不一致。最初の差はpayload先頭からoffset 40（0x28、0xec / 0xb4）で、CookedMeshWireFormat.hのCookedMeshFormatV0::HeaderOffset::FileSizeに当たる。CookedMeshLoader.cppのParseCookedMeshV0（266行）がこの値を読む。

差の原因は材質の画像pathで、`Models/Generated/dog_trellis2_60k/dog_trellis2_60k.glb.img<N>.png`が外部画像の`Models/Generated/dog_trellis2_60k/image<N>.png`になる。文字列領域が189→132 bytes、8byte整列後の全長が56 bytes変わり、材質の文字列参照・FileSize・StringTableSize・頂点/index offset・PayloadHash・StringByteCountに反映される。submesh・cluster・頂点・indexの各領域はバイト単位で一致した。SourceHashはNVMESH v0に格納されず、manifestでは元sourceの構成変更により`25844229d9fac020`→`9a31bb23d0f0af6c`となる。

外部画像の.gltfはmodelだけを出力するため、切り出した3画像を同じformat・entry名で個別にtexture cookした（Debug、すべて終了コード0）。GLBの一括出力とtexture package全bytes・NVTEX payloadが3件とも一致し、SHA-256も上表のimg0/1/2と一致した。実測値・両出力のSHA-256・CLI全文は`build/GR77Accept/results.md`に記録した。

### 骨格

dog_rigged.glb（34,533,828 bytes・72,754頂点・skin 1・skin.skeleton欠落・animation 0）をDebugのnvskel形式でcookし、終了コード1となった。診断全文は次のとおり。

```text
import_report={"version":1,"kind":"skeletal","outcome":"failed","source_hash":null,"decode_status":8,"skin":{"policy":"strict","warn_dropped_weight":0.01,"fail_dropped_weight":0.25,"influence_scan":null}}
AssetCook error: skeletal glTF decode failed: status=8
```

decode_status/status=8はSkeletalGltfData.hのUnsupportedClipCount。SkeletalGltfDecode.cppの2773〜2780行で、JSON解析後のanimation 0を拒否している。ParseContainerは3097行、ResolveJsonBuffersは2818行にあり、今回の拒否はGLBコンテナ・buffer・BINの失敗ではなく、buffer解決前の既存のclip契約によるもの。skin.skeleton必須の検査は1651〜1655行にあるが、今回はそこまで到達しない。既存の契約による正しい拒否であり、この入力を通すのはG2-GR82-B4-STATIC-ROOT-FRAME128の範囲とする。

### 表示

mega-bananasを`-RequireAssets`で1枚撮り、GLB由来のバナナがテクスチャ付きで描かれるかの確認は未実行。今回の受入れではアプリを起動しないため、表示は後で確かめる。

### 未実行

- loose経路で実物GLBを読む受入れは、エンジンに実物をlooseで読む入口が無いため未実行。現状はfixtureを使うGltfLooseSourceTestで閉じている。
- mega-bananasの撮影とGPU表示は未実行。CPU試験・実packageの成功から表示の合格は推定しない。
