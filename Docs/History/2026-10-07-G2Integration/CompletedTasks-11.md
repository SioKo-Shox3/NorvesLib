- notes: P4D全探索比較前の既存不具合修正。旧float二次式は明確な球・円筒missにもSuccessを返す。最適化の結果を誤った全探索へ合わせない。距離/点の外部float表現自体の丸めは残る。

- notes: 追加試験のcompileでVector3に単項minusが無いことを確認。P4Aで補強した未実行facade試験にも同じ式が1件あったため、明示Vector3(-1,0,0)へ修正する。

## G1-GR03-CURVES: 入力と振動の最小曲線評価をMathへ共通化する
- status: done
- done-when: RoadmapのG1追加指示に従いMath/Curves.hへ区分線形とイージングの最小APIを置き、入力応答と振動が同じ評価実装を再利用する。既存既定値/JSON/端点/無効値契約を維持し後続GRへ拡張口を示す。
- verify: 曲線端点/区間/不正値/イージングと既存入力軸・振動のCPU回帰、独立レビュー。
- stop-when: 未承認の既定値変更、二重curve適用、非有限値、既存公開契約の破壊。
- notes: 原文Roadmapの段階横断注意G1/GR03・GR04。入力API/GR119・GR121の再利用規約も文書に明記する。

## G1-GR01-CONTRACT: 更新群とpauseと最終cameraの共有契約を明文化する
- status: done
- done-when: 更新群ごとのComponent割当、Bridge進行/非pause/将来bTickWhenPausedの3分類、新Component登録規約、G14 CameraDirector/G16 listenerの参照順を現コードと承認S3に合わせて文書化する。
- verify: TickStages/Processor/World/Module/Gameの実順序を照合し、未実装のbTickWhenPausedや将来hookを既存機能と誤記しない。独立文書レビュー。
- stop-when: IModule::LateTickより後のGame::OnLateUpdate確定を隠す、未承認の更新順/pausingの実装変更。
- notes: 原文Roadmapの段階横断注意G1/GR01。camera最終確定は承認S3=OnLateUpdateと実順序を正確に説明する。

## G1-GR08-CAPSULE-ACCEPTANCE: 後続移動処理向けのカプセル境界試験を固定する
- status: done
- done-when: 縦Capsuleの箱面/辺/角、Capsule同士/回転、開始重なりDistance0/印/押出しNormal、OverlapCapsuleのDepth/Normalを解析例で固定し契約を文書化する。
- verify: 実GeometrySweep/SeparationとPhysicsProxyQueryのCPU試験を通常/最適化/sanitizer/MEMBERで実行し独立レビュー。
- stop-when: 正常ケースを緩めて既存実装へ合わせる、未収束を確定hitと扱う、法線符号/深さ規約の曖昧化。
- notes: GameFeatureHandoffとRoadmapのG1/GR08追加完了条件。既存Sphere試験や中心通過乱数試験だけでは代替しない。

## G1-GR08-P4D: 統合クエリをrayと掃引AABBの候補訪問へ接続する
- status: done
- done-when: 既存query集約をP4C訪問へ接続し、Overlapの形状boundsとSweepの始終点を包むAABBで狭域判定を減らす。proxy検証/filter/数値安全域は除外前に確認し、順序/MaxHits/失敗時クリアを維持する。boundsを安全に構成できなければ全探索へ戻す。
- verify: 固定seedの混在proxyと7種を、独立sortによる全QueryProxy走査と比較し、各hit field/順序/MaxHitsをCPU検証。境界OBB/初期重なり/無効遠方proxy/候補内IterationLimit/巨大掃引fallbackと既存試験を通常/O2/sanitizer/MEMBERで実行し独立レビュー。
- stop-when: 確定hitの取りこぼし、検証失敗の隠蔽、候補内未収束をhitへ変換、出力残留、bounds overflowで不正除外、blocking未解消。
- paths: Library/Core/Public/Math/GeometryIntersection.h, Library/Core/Private/Math/GeometryIntersection.cpp, Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp, Test/Modules/Physics/PhysicsQueryPruningTest.cpp, Test/Modules/Physics/PhysicsRayPrecisionTest.cpp, Test/Modules/Physics/CMakeLists.txt, Docs/Architecture/PhysicsQueries.md, TASKS.md, PROGRESS.md
- notes: AABBで非交差を証明できる対象には狭域反復を行わないため、旧全探索がその対象でIterationLimitだったケースをNoHitと確定できる。候補内のIterationLimitは維持。旧APIの転送と新旧既定比較は続く別タスクで統一する。

- notes: 比較中、旧OBB rayのepsilon平行扱いによる明確な誤hitも実再現。物理private経路をdouble slab/厳密0判定と局所double法線へ修正し、共有Mathは変更しない。

- notes: 第1周でSweep OBBのfloat/double Gram検証差を発見。既存Mathのdouble検証をIsValidSweepBoxへ抽出してSweep本体とprecheckで共有する。Mathの受理条件・計算意味は変更しない。

## G1-GR08-P4E: 旧rayとoverlapを共通query経路へ転送する
- status: done
- done-when: 旧署名を維持してPhysicsModule/Broadphaseのray/overlapを共通queryへ転送し、旧hit型へ値変換する。旧Overlap法線はquery→対象の向きへ戻し、Point/Depth/UserData/順序と同距離rayの大handle優先を維持する。既定filterの新旧結果を固定seedで比較する。
- verify: productionで使うspan版旧型adapterを実Broadphase/Mathへ直接リンクし、混在shape・既定新旧4種・全field/順序・同距離・失敗clear・不十分bufferを通常/O2/sanitizer/MEMBERで検証。実Module入口試験も追加しcompile試行、独立レビュー。
- stop-when: 法線符号/Depth/識別値欠落、順序規則変更、未対応fake providerの既存入口破壊、エラーをsuccess扱い、旧公開署名変更、blocking未解消。
- paths: Library/Modules/Physics/Private/Physics/PhysicsBroadphase.h, Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp, Library/Modules/Physics/Private/Physics/PhysicsModule.cpp, Test/Modules/Physics/PhysicsLegacyQueryTest.cpp, Test/Modules/Physics/PhysicsBroadphaseQueryTest.cpp, Test/Modules/Physics/CMakeLists.txt, Docs/Architecture/PhysicsQueries.md, TASKS.md, PROGRESS.md
- notes: 新旧とも有効な正規直交OBB/共通数値安全域を条件とし、範囲外はInvalidArgumentへ統一。旧有限検査で通っても数学的に無効なOBBやfloat計算保証外の巨大形状は互換保証に含めない。直接BroadphaseのOverlap追記契約は維持、Module/Sceneの失敗出力は空。

- notes: 第1周でLayer0の旧無filter契約を確認。旧adapterだけ内部kernelのfilter適用を無効にし、新APIのbitmask条件は維持。4種類の旧hit/新NoHitを回帰へ追加する。

## G1-GR01-TRANSFORM-BOUNDARY: 群境界で子階層のworld姿勢を確定する
- status: done
- done-when: Roadmap G1リスクの段階境界確定に従い、Movement等で親が動いた後のDefault/Animation/PoseFinalize、Camera後のPreRenderから子world姿勢を同frameで読む。既存群順/収集/削除/Fixed規則を維持する。
- verify: 実WorldTickGroupTestへ親子階層の各群境界・同群の非暗黙確定・追加翌frame・Late境界の回帰を追加。可能なportable群/dispatch回帰と実統合compile試行、独立レビュー。Windows.h阻害は未実行と記録する。
- stop-when: callbackごとの全走査、同群順序/寿命の破壊、未承認scheduler変更、Windows統合の偽装。
- notes: G1最終照合で、現DispatchTickGroupsは前半5群を一括走査し変換確定をLate境界だけに行う点を発見。Entity::GetWorldTransformは親ありdirtyでcached値を返すため、先の群で変更した親への追従が不足する。

## G1-VALIDATION: 実装一覧とクラウド検証範囲を確定する
- status: done
- done-when: GR01/03/04/08の実装、実行済みportable試験、未実行統合/実機確認を区別する一覧をDocs/Architectureへ保存する。G1追加Public header29件の明示登録を揃える（監査でCurves.hのみ漏れ）。
- verify: 実ログ/command/sourceを照合し34件成功とcompile blocked1件を別記する。Core PUBLIC_HEADERSと追加headerを照合し重複を検出する。新たなWindows/CI合格を主張しない。
- stop-when: CPU純kernelをWorld/Engine統合の合格と扱う、未実行項目をpassにする、未承認のG2選定を確定する。

## G2-P0-COOK-LIB: 既存cookerを直接リンク可能な基盤へ分離する
- status: done
- done-when: GR77/GR78以降の共通基盤としてAudio/Mesh/Texture cookerのcpp/hを単一STATIC AssetCookLibへ所属させ、AssetCook実行ファイルはMain.cppのみを持ち同libをリンクする。公開headerとCore依存をconsumerへ伝搬し、二重compileしない。
- verify: target source/include/link関係と既存smoke登録のbyte不変を確認。cooker/Main.cpp内容が不変であることを照合。実AssetCook/CookedMeshTestとRaw/Texture/Mesh/Audio smokeは環境が対応する場合に実行し、できない場合は未実行と明記する。
- stop-when: 分離のために形式/CLI/platform実装/描画の変更が必要、新規test実行ファイル、Windows依存の代替stub。
- paths: Tools/AssetCook/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: G2実装開始は作者承認済み。G2-S1〜S8の推奨案の包括承認ではない。まず形式/既定値に依存しない分離を行う。

## G2-SELECT-S8: GLBの共有解析方針を確定する
- status: done
- done-when: 作者の選定を記録しGR77へ適用する。
- notes: 2026-10-03 UTCに既存JsonDocument/stbを活用するGLB共有処理を承認。新規外部glTF parserは追加しない。BVH/FBXは質問範囲外で、この承認に含めない。

## G2-SELECT-S2: 取り込み設定の正本を確定する
- status: done
- done-when: 作者の選定を記録しGR78/GR96へ適用する。
- notes: 2026-10-03 UTCにAを承認。ソース隣<ソース名>.import.jsonを正本にしてcook/looseで共用する。欠如時は恒等。イベント/ソケット等のruntime定義と取り込み変換は分離する。

## G2-GR86-LIMITS: 現行の関節上限を共有定数へ集約する
- status: done
- done-when: glTF decoder/cooker/cooked loaderの現行128関節上限を1つの公開定数へ揃える。値・拒否条件・エラー・format/vertex ABIを変更しない。
- verify: 対象3箇所の参照と旧128境界の意味不変を照合、公開headerのcompileとPUBLIC_HEADERS登録、可能な関連CPU回帰、実統合compile制約を明記する。
- stop-when: 未承認の256関節化、JOINTS_1拒否変更、format v1への先行変更、既存型/描画ABIの変更。
- notes: GR86前半の選定不要部分。G2-S4の縮約/焼き込み/Strict変更とStage Bの上限拡張は保留したまま進める。

## G2-GR77-CONTAINER: GLBの共有バイトコンテナを解析する
- status: done
- done-when: 共有のGltfContainerが借用SpanでGLB v2 header/JSON/BINを範囲安全に分解し、非GLB入力はBOMを除いたJSON viewとして区別する。未知chunkを無視し、既知chunk順/重複/長さ/切断を拒否する。BINのコピー/ファイルI/O/JSON意味解析は行わない。
- verify: 既存CookedMeshTest束へMEMBERを追加し、正常/未知chunk/切断/overflow/順序/再利用時出力clear/入力非変更と借用寿命契約を実parserの通常/O2-NDEBUG/sanitizer/MEMBER compileで確認する。
- stop-when: 入力越境/overflow、構造上無効なGLBを成功扱い、所有権を偽装、Windows stub導入、3経路への接続完了と誤認。
- paths: Library/Core/Private/Resource/GltfContainer.h, Library/Core/Private/Resource/GltfContainer.cpp, Library/Core/CMakeLists.txt, Test/Core/Asset/GltfContainerTest.cpp, Test/Core/Asset/CMakeLists.txt, Docs/Architecture/AssetImportPipeline.md, TASKS.md, PROGRESS.md
- notes: GR77最初の部品。buffers[0]/byteLength/padding、data URI/base64、画像、cooker/loose/骨格への接続は後続タスク。G2-S8のGLB部分だけを承認済みとして使う。

## G2-GR77-BASE64: 埋め込みdata URI向けの厳密base64を共有化する
- status: done
- done-when: Span入力/出力の無割当primitiveでRFC4648標準alphabet・padding・末尾未使用bitを検証し必要長と復号を提供する。容量/入出力重なりを拒否し、失敗時は出力bytes不変/長さ0。
- verify: RFC既知例、全1/2byte入力と固定seed3byte、非正規padding/無効文字/空白/切断、容量/alias/出力tail保持を通常/O2/sanitizer/MEMBERで実行する。
- stop-when: padding不正の黙認、部分書込み、入力越境/overflow、data URI/JSON/ファイル接続を完了扱いする。
- notes: GltfBufferSet/画像のdata URI解決が使う下層部品。URIの構文・percent decode・MIMEは別層で扱う。

## G2-GR77-BUFFER-SEMANTICS: data URIとGLB BINの意味境界を検証する
- status: done
- done-when: 共有の無割当helperでdata URIの対応MIME/base64形式/parameter/percent escapeを扱い、GLB buffer0の宣言長と0〜3byteのゼロpaddingを検証する。失敗時の出力非変更/空viewと借用寿命を明示する。
- verify: 既知data URI、大小文字/parameter/percent、壊れたescape、対応外MIME/encoding、BIN index/長さ/padding/欠落を実helperで通常/O2/sanitizer/MEMBER確認。ファイルI/O/JSON/ownedBufferSetは後続と区別する。
- stop-when: 非base64や対応外画像の黙認、percent後のNUL/経路を無条件に安全と扱う、出力部分書込み、BINコピー。
- notes: GR77のGltfBufferSet/画像が使う意味検証を先行し、cooker/loose/skeletalは後続で接続する。

## G2-GR77-BUFFER-SET: 外部と埋め込みbufferの所有と参照を共有化する
- status: done
- done-when: 所有する外部/data URI bytesと借用GLB BINを共通BufferSetで解決し、宣言範囲のaccessor viewと外部hash用の全source bytesを区別する。自己所有配列へのSpanを保存せずcopy/moveで参照を壊さない。JSON/消費経路接続は後続へ分ける。
- verify: descriptor・data URI・BIN・reader失敗・容量/宣言長・copy/move/再利用・出力契約の実testを既存束へ追加。実source syntax/型特性と可能なCPU helper回帰を確認し、実allocator/Windows依存の未実行を明記する。
- stop-when: BINコピー、旧.gltf hashのBOM/外部余剰byteの消失、借用寿命の隠蔽、read callbackによる境界迂回を安全と主張、偽allocator/Windows stub。
- notes: 3経路はまだ接続しない。外部URIはpercent後の相対path検証と実filesystem containmentを通す。data URIの受理追加はGR77の意図的仕様変更として旧拒否smokeを更新する。

## G2-GR77-BUFFER-JSON: JSONのbuffer記述を共有resolverへ変換する
- status: done
- done-when: 既存JsonDocumentのbuffersから型/正の安全整数byteLength/uriの有無/ASCII文字列/重複既知fieldを検証し、借用descriptorの寿命を保ってBufferSetへ渡す。3consumerのJSON読取りを共有できる入口を用意する。
- verify: 既存CookedMeshTest束へJSON記述の正常/欠落/型違い/重複/数値境界/BIN/data URIを追加。実型の宣言・呼出を照合し、Windows依存でcompileできない範囲と純helper回帰を区別する。
- stop-when: null uriを省略と誤認、doubleからsizeへの範囲外cast、移動したURI配列へのSpan、未承認の形式/既定値変更。

## G2-GR77-LOOSE-BOUNDS: loose accessorの範囲計算を安全化する
- status: done
- done-when: GLTFAnalyzerのunchecked startOffset/count*strideをcheckedな宣言buffer/view範囲検証へ置換し、GR77共有buffer接続前にoverflowによる越境を防ぐ。正常既存fixtureの属性値は維持する。
- verify: count/stride/offsetの整数境界を実純helperと既存staging試験へ追加し、旧正常値の不変と不正拒否を確認する。
- notes: buffer解決の共有化だけでは現在のValidateAccessorBoundsのunchecked加算/乗算は解消しない。loose接続の前提として別タスク化。

## CORE-MEMORY-HEADER-UTILITY: メモリAPIヘッダの標準includeを自己完結させる
- status: done
- done-when: MemoryOverrides.h単体でstd::forwardの宣言不足にならないよう必要includeを明示し、動作を変更しない。
- verify: 実header単体のsyntax確認。MemorySystem全体のWindows依存解消とは扱わない。
- notes: G2 BufferSetの実allocator接続compileで既存不足を検出。G2の取り込み実装とは別の小修正候補として保存。

## G2-GR77-COOK-BUFFERS: 静的cookerへ共有GLBとbufferを接続する
- status: done
- done-when: CookGltfToNvmeshが共有container/JSON buffer resolverを使い、GLB BINとdata URI bufferを読める。既存geometry/material/default/formatを維持し、旧.gltf hashはBOMと外部buffer全量を保存、新embedded内容を二重hashしない。
- verify: 既存静的cooker fixtureをJSON+external/GLB/data URIで比較する登録試験とsmoke更新を追加。payload一致/metadata/hash変化/不正の拒否を固定し、Windows統合未実行を明記する。
- stop-when: BINのコピー、既存material色空間/既定の変更、既存.gltf外部画像の新規実読込、自前parserへ未承認切替、3経路全完了の誤認。
- notes: embedded imageは次段の所有/借用とMain packagingで接続する。既存data_uri_buffer negativeは1byteで宣言長を満たさず、受理追加後も短さで失敗する。

## G2-GR77-IMAGE-SOURCE: 画像の外部参照と埋込みbytesを共有解決する
- status: done
- done-when: GltfImageSourceでimagesのfile URI/data URI/bufferView+mimeTypeを分類し、埋込みPNG/JPEGのbytesと寿命を明示する。URIとbufferViewの排他・既知field型/重複・buffer宣言範囲を検証し、外部画像はここでは読み込まない。
- verify: 既存束にfile/data URI/BIN view/外部buffer view・MIME/範囲/型/所有copy-moveの契約試験を追加し、実行可能helperとWindows依存未検証を分ける。
- stop-when: BIN全体コピー、所有bytesへの自己Span保存、壊れたdata URIや範囲外viewの受理、material色空間/format既定の変更。
- notes: MeshCookResultへの寿命移譲、Mainのmodel+texture出力/manifest一括更新、loose staging接続は後続タスク。GR77完了とは分ける。

## G2-GR77-COOK-IMAGES: 静的cookerの埋込み画像を結果へ保持する
- status: done
- done-when: GltfImageSourceからPNG/JPEGを材質roleごとに解決し、決定的な仮想pathとsrgb/linear formatをNVMESH参照とEmbeddedImagesへ保存する。GLB画像は元sourceへ借用し、cookerローカルBufferSet/data URIの画像は結果側が所有する。同一画像の互換roleは重複させず、色空間衝突は明示拒否する。
- verify: 外部imageの参照のみ経路維持、GLB/data URI/外部buffer内imageの寿命、決定path/format/重複と衝突、失敗出力不変を既存束へ登録。実行可能範囲とnative未検証を明記する。
- stop-when: BIN全体コピー、局所所有bytesへのdangling Span、既存外部画像の無条件実読込、material既定変更、Main出力まで完了と誤認する。
- notes: Mainのmodel+N texture package/manifest一括更新は別タスク。現行albedo/normal/ARMのみ接続し、occlusion/emissiveの描画仕様を勝手に増やさない。

## G2-GR77-PACKAGE-IMAGES: モデルと埋込みtextureを一括出力する
- status: done
- done-when: MainでEmbeddedImagesを既存texture cookerへ渡しmodel+N個の単一entry packageを作る。既存manifestを任意のAssetCookedReference群で統合し、無関係entry/骨格metadataを保持する。全候補の変換とpackage検証後にmanifestを1回更新し、model-only拒否guardを置換する。
- verify: 小GLBのmodel1+texture3、role format/path/hash、全AssetSystem解決、同key更新/無関係entry維持/重複拒否、失敗時manifest非更新を既存smoke方式へ追加。旧外部画像なし経路とaudioの回帰を固定し、native未検証を明記する。
- stop-when: 多entry NVPKG形式への変更、materialの未承認既定変更、未生成textureを参照するmodelだけを成功出力、formatをmanifest keyと誤認する。
- notes: GR111の多entry形式は導入しない。model外部画像の既存single出力は保持し、manifest keyはlogicalPath/kind/variant。texture参照のdefault variantを守る。

## G2-GR77-SKELETAL-BUFFERS: 骨格デコーダとcookerへ共有GLBを接続する
- status: done
- done-when: SkeletalGltfDecodeのbuffer読込/accessor参照を共有BufferSetへ接続し、元bytesのGLB/JSON入口をcookerとAnalyzeSkeletalから利用する。旧String入口も保持し、既存骨格契約/128上限/単一clip/変換/formatは変えない。hashは元sourceと全外部bufferだけを含み、BINを複製しない。
- verify: M9の既存fixtureをJSON外部/GLB/data URIで表し、頂点/関節/clipとNVSKEL payload/hashの比較、不正buffer拒否を既存束へ追加する。実native未検証と共有helper成功を区別する。
- stop-when: Armature親/clip複数/256関節への未承認拡張、NVSKEL形式変更、String互換APIの破壊、hash/BIN借用寿命の逸脱。
- notes: GR82の骨格契約変更やStage A/Bを先取りしない。画像/材質を骨格側へ新たに導入しない。

## G2-GR77-IMAGE-STAGING: 埋込み画像bytesをCPU texture stagingへ渡す
- status: done
- done-when: ModelStagingの画像decodeをfile読込とbytes読込へ分け、PNG/JPEG bytesから標準RGBA8とARMの3つのR8 stagingを生成できる。既存外部画像の既定format/チャンネル写像を維持し、input長/dimension境界とstb所有解放を守る。
- verify: 同じ小PNGのfile/bytes pixel一致、ARM分解、異常bytes/大きい長さの拒否、stagingの所有と失敗出力契約を既存束へ追加。native未検証と実stb/helper確認を区別する。
- stop-when: albedo既定色空間の変更、GPU生成、stb確保の例外時漏れ、借用pixelsの返却。
- notes: GLTFAnalyzerの画像参照接続は後続。空RequestPathとHasDataによるfinalize契約を保持する。

## G2-GR77-DOCUMENT-PROFILE: 必須glTF拡張の拒否規則を共有する
- status: done
- done-when: extensionsRequiredの型/重複と非対応必須拡張を共有判定し、静的cooker/loose/骨格で黙って無視しない。現時点の許可リストは空で、既存静的cookerの通常診断を維持する。
- verify: 無指定/空配列/型違い/重複/Draco/meshopt/quantization/texture_transformの拒否を既存束へ登録。native未検証を明記する。
- stop-when: 未実装拡張の受理、optional extensionsUsed全体の無差別拒否、骨格/材質の未承認仕様拡張。

## G2-GR77-LOOSE-SOURCES: loose静的モデルへGLBと埋込み画像を接続する
- status: done
- done-when: GLTFAnalyzerの静的stagingを共有container/BufferSet/ImageSourceへ接続し、GLB/data URIのgeometryとPNG/JPEG画像をCPU所有stagingにする。埋込み画像のRequestPathは空、既存外部画像の論理参照とRGBA8/ARM分解を保持する。
- verify: 小GLB・data URI・外部入力でgeometry/5staged textures（元画像3枚）を比較し、元source破棄後の所有と必須拡張拒否を確認する登録試験。Windows/GPU未検証と区別する。
- stop-when: BIN全体コピー、埋込み画像をcooked manifest参照へ回す、既存材質既定の変更、full static parser統合の先取り。

## G2-GR77-LOOSE-INDEX-RANGE: クラスタ生成前に頂点インデックスを検査する
- status: done
- done-when: loose geometryの全indexがvertices範囲内であることを、MeshClusterizerへ渡す前に検査する。不正入力は失敗し既存出力を保持する。
- verify: 3頂点に対するindex=3/65535の拒否と正常0/1/2の受理を既存staging試験へ登録。純粋な共通検査も境界と空/nullを検証する。
- stop-when: clusterizer全体改修、正常meshのwindingや材質変更、native未実行を合格扱いする。
- notes: GLTFAnalyzer::ExtractMeshDataはindex値を未検査で返し、MeshClusterizer::ComputeNormalCone/ComputeBoundingSphereがその値で頂点を参照する。LOOSE-SOURCES前に安全化する。

## G2-GR77-MIME-SIGNATURE: 埋込み画像の宣言MIMEと実形式の一致を検証する
- status: done
- done-when: PNG/JPEG宣言と実signatureの矛盾を、cook packagingとlooseの同じ規則で拒否する。外部画像の従来decoder形式は狭めない。
- verify: PNG宣言/JPEG signatureと逆の拒否、正常一致、失敗時出力保持を既存束へ登録する。
- stop-when: 新形式追加、native未実行を合格扱い、材質や色空間既定を変更。
- notes: 現行は対応signatureかどうかだけを判定する。GR77接続の非blocking残件として登録。

## G2-GR78-SETTINGS: 共有取り込み設定の値とJSONスキーマを定義する
- status: done
- done-when: Core privateにcook/loose共用のv1設定を置き、units/axes/origin/meshとmeta・将来予約blockを厳格解析する。未知/重複/非有限/矛盾を拒否し、出力は成功時だけ更新。固定順LE正規化bytesはmeta/JSON書式に依存しない。
- verify: 純粋な値検証/固定bytesを通常・最適化・sanitizerとMEMBERで検証。実JsonDocument試験は既存束へ登録し、Windows依存による未実行を明記する。
- stop-when: sidecar無しの現行挙動変更、未実装repair/lod/material/collision/clipの非空指定を受理、未承認材質既定やモデル形式の更新。
- notes: S2承認済み。CoreからToolsへ依存させないため解析もCore privateへ配置し、AssetCookLibとlooseが共有する。file探索・変換適用・hash連結・CLIは別反復。

## G2-GR78-TRANSFORM: 取り込み変換の共有演算を実装する
- status: done
- done-when: 軸正規化・一様scale/fit・mirrorX・keep/bounds中心/足元/custom原点・法線/UV/windingをdoubleで合成し、float表現可能性を検証して静的geometryへ適用する。骨格は一様scaleのみの制約を保持する。
- verify: 軸写像、fit、各原点、鏡像、法線、UV、極端値/縮退/失敗出力を検証する。
- stop-when: 骨格への未承認軸/原点変更、描画既定/形式変更、非有限やoverflowの黙認。

- notes: 2026-10-03作者承認によりsurface_centroidを未対応として明示拒否し、他変換を先行する縮小scopeで再開。表面重心は別保留へ分離。

## G2-GR78-SIDECAR: ソース隣の設定読込と変換・hashを接続する
- status: done
- done-when: <source>.import.jsonをcook/looseで読み、無しは既存bytes/hash不変、有りは共有変換と正規化設定hashを適用。骨格の位置/IBM/translation/mesh-nodeを同時scaleする。
- verify: sidecar無し回帰、有無/値変更/書式とmeta変更、失敗保持、static/skeletal/cook/looseの一貫性を既存束へ登録する。
- stop-when: cook/looseの設定解釈差、未承認形式更新、実native未実行の隠蔽。

## G2-GR78-CLI: 設定指定・require・skipと診断の入口を追加する
- status: done
- done-when: import-settings/no-sidecar/require-sidecar、hashとpackage実体一致を条件にしたskip、inspect診断を追加する。既存必須CLIとsidecar無し出力は維持する。
- verify: 設定優先順位/相互排他/失敗、skip時非更新、package欠如/破損時再cook、inspectをsmokeへ登録する。
- stop-when: GR96一括cook/依存追跡の先取り、誤ったskip、未実装オプションの受理。

## G2-GR78-SIDECAR-IO-HASH: 設定fileの読込とhash連結を独立実装する
- status: done
- done-when: source隣/明示override/無効/必須の設定file選択と安全なread・厳格parseを共有化し、sidecar無しの旧hashを保持、有りの正規化bytes+algorithm version連結を定義する。geometryへはまだ適用しない。
- verify: absent/invalid/required/override/conflict/出力保持をnative登録し、純hashの既知値/無し不変/値変更とバージョン変更を実行する。
- stop-when: 保留中ImportTransformを未確認でロードへ接続、file不在以外のI/O失敗を無し扱い、meta/書式差でhash変更。
- notes: GR78-TRANSFORMの独立確認保留中に先行できるSIDECARの部分作業。

## G2-GR78-STATIC-SIDECAR: 静的cookとlooseへ設定を適用する
- status: done
- done-when: 共有loaderと承認済み変換を静的meshのcluster/bounds前へ挿入し、無しなら旧bytes/hashを保ち、有りなら正規化設定hashを連結する。override/disabled/requiredを内部APIへ渡せ、採用設定を診断可能にする。
- verify: sidecar無し/disabledのpayload同値、scale/fit/origin/UV/windingのcook-loose同値、meta/書式hash不変と値変更、surface拒否と出力保持を既存束へ登録する。
- stop-when: surface_centroidの再有効化、骨格への無言適用、未実装オプションの黙認、native未実行を成功扱い。

## G2-GR78-SKELETAL-SIDECAR: 骨格へ一様scaleと設定hashを適用する
- status: done
- done-when: 頂点位置/IBMの平行移動/animation Translation/mesh-node平行移動を同じ正の一様scaleで変換し、cook/loose/legacyで共有する。軸・鏡像・原点やmesh変更は未対応として拒否する。
- verify: sidecar無し同値、scale前後のskinning結果、fit倍率、回転/scaleチャンネル不変、範囲外と拒否時出力保持を既存束へ登録する。
- stop-when: 未承認NVSKEL形式変更、骨格の軸/原点変換、native未実行を実skinning成功扱い。

## G2-GR78-OUTPUT-SIDECAR-GUARD: cook出力による設定file上書きを拒否する
- status: done
- done-when: 採用したsidecarとmodel package/manifest/embedded texture packageのpath・symlink・hardlink aliasを、書込前に拒否する。
- verify: 有効sidecarをpackage/manifest出力先にしたCLI回帰で失敗と既存設定bytes保持を登録し、通常出力を妨げないことを確認する。
- stop-when: 設定正本を出力で上書きする、途中書込後にaliasを判定する、TOCTOU完全防御と主張する。
- notes: 静的接続で追加された新規入力sidecarの保護。通常出力pathの既存挙動は維持する。

## G2-GR78-CLI-SETTINGS: CLIの設定選択を共有loaderへ接続する
- status: done
- done-when: --import-settings/--no-sidecar/--require-sidecarをstatic/skeletalへ渡し、相互排他/重複/空値/非model用途を明示拒否する。明示設定が自動探索より優先し、無効化は読込を省略する。
- verify: 実option parserの引数/値/順序/失敗保持とCLI設定優先順位/拒否/設定保護を登録する。
- stop-when: 未実装skip/inspectの受理、既存必須引数の緩和、native未実行をCLI成功扱い。

## G2-GR78-CLI-SKIP: 未変更modelのcookを入力hashで省略する
- status: done
- done-when: cook前に元入力/buffers/設定のhashを計算し、同じmanifest keyのsource_hash・要求format/entry/output先と実packageのcooked_hashを検証した場合だけskipped終了する。派生画像packageも欠損/破損なら省略しない。
- verify: preflight hashとcook hash一致、設定編集/無変更/meta変更、出力欠損/破損/要求先変更/画像欠損の回帰を登録する。
- stop-when: 本cookを実行してからskip扱い、hash一致だけでpackage実体未検査、GR96一括依存追跡への拡大。

## G2-GR78-CLI-INSPECT: cookしないmodel診断を追加する
- status: done
- done-when: --inspect <file>でbounds/軸長/頂点三角形数/位置溶接と成分/ゼロ法線/画像寸法・チャンネル統計/材質係数と設定叩き台を診断し、前方向の符号を勝手に決めない。
- verify: 小型既知fixtureの幾何/画像/材質値、無出力cookと入力非変更を確認する。本人実物は入手後に別受入れ。
- stop-when: 診断のためpackage生成/既存sidecar上書き、未知方向を確定として出す、未入手実物を合格扱い。

## G2-GR78-SOURCE-PREFLIGHT: cook前の入力hashと派生画像記述を共有化する
- status: done
- done-when: geometry変換/cluster/画像decode/package生成なしで元source/buffers/設定のcookと同じhashを返し、embedded画像の論理path/format/hashを所有metadataとして返す。設定loaderを本cookと共有し、失敗時出力保持。
- verify: glTF/GLB/data URIと設定有無/変更/meta/無効/必須、骨格、embedded画像metadata、外部buffer全量のhash同値をnative既存束へ登録する。
- stop-when: preflight成功をgeometryの妥当性保証とする、失敗の部分公開、画像bytesの寿命をcallerに漏らす。

## G2-GR78-INSPECT-GEOMETRY: 無変換の幾何診断kernelを実装する
- status: done
- done-when: bounds/軸長/最長軸・頂点/三角形数・数値完全一致の位置溶接数・頂点共有成分数・ゼロ法線を変更なしで計測する。符号付きゼロは同一、未参照頂点は孤立成分と明示する。
- verify: 既知の分離三角形/継ぎ目/孤立/縮退、非有限/不正index/workspace重複、微小値/float両極端、面順序の純実装試験。
- stop-when: epsilonを無言適用、source geometryを書換え、未検証kernelを診断CLIへ接続する。

## G2-GR78-INSPECT-IMAGE: 画像寸法とチャンネル統計を診断する
- status: done
- done-when: PNG/JPEGを既存stbで展開し、8/16bitの寸法/decoded channel数と整数min/max/meanを取得。色空間線形化はせず、最終pixel payloadの512MiB上限を事前検査する（stb内部総メモリの制限ではない）。失敗時結果保持。
- verify: 既知8bit RGBA/16bit gray、壊れたPNG/JPEG/未対応/巨大dimensions/aliasの実stb試験を3modeとMEMBERで確認する。
- stop-when: 16bit画像を無言で8bit化する、巨大decodeを上限なしで始める、source画像を編集する。

## G2-GR78-VALIDATION: 実装範囲と検証状態を整理する
- status: done
- done-when: GR78の承認済み実装とsurface保留、純実装の実行結果とnative/実物の未検証を分けて記録する。
- verify: 各task/commit/実log/CMake登録を照合し、次のG2選定を承認済み扱いにしない。
- stop-when: Windows/native/実物を未実行のままPASSとする、surfaceを実装済みへ含める。

## G2-SELECT-S4: GR86の縮約/焼込/Strict方針を確定する
- status: done
- done-when: 作者の回答を記録し、その範囲内で後続taskを作る。
- notes: 2026-10-03に推奨A（上位4本縮約、CUBICSPLINE焼込、morph明示drop、Strict余剰拒否、v1時256）を作者が承認。

## G2-SELECT-S1: NVSKELの版と骨格識別方針を確定する
- status: done
- done-when: 作者の回答を記録し、GR32/GR82の形式を同じ定義へ揃える。
- notes: 2026-10-03に推奨A（0.2へsubmesh/複数clip統一、材質仕様後v1、SkeletonIdにrest poseを含めず骨長差はGR85）を作者が承認。v1 clip作成時rest pose保持と束縛時の既定拒否/明示許可/差分報告を追加。企画の骨格共有は未定へ訂正。S7のmesh数条件は別未確定。

## G2-S1-S4-DECISIONS: 承認とrest pose安全契約を計画へ反映する
- status: done
- done-when: S4/S1 A採用、企画共有案未定への訂正、v1 clip作成時rest pose/束縛時比較/既定拒否/明示許可/差分報告を正本へ記録する。
- verify: roadmap/requirements/handoffの競合前提を照合し、他選定を承認済みにしない。
- stop-when: rest pose不一致をSkeletonId一致だけで許可する、未定の造形/共有案を採用済みへ戻す。

## G2-GR86-STRICT-INFLUENCES: 追加ウェイトセットの黙認を拒否する
- status: done
- done-when: StrictのJOINTS_n/WEIGHTS_n（n>=1）を専用InfluenceLimitExceededで拒否し、既存status値と通常4影響入力の挙動を保つ。
- verify: 属性名分類の純試験、JOINTS_1/WEIGHTS_1有無/値/片側/大きいn/不正名、raw/legacy/cookerの拒否と出力保持を登録する。
- stop-when: 未実装Reduce optionの受理、関節上限/頂点ABI/形式変更、既存status番号の変動。

## G2-GR86-REDUCE-KERNEL: 4影響への決定的な縮約を実装する
- status: done
- done-when: 全影響の有限/非負/関節範囲/総和を検査し、同一関節をまとめ、重み降順/同値joint番号順で4本を選び正規化する。脱落量・警告・許容超過を返し、失敗時vertex出力保持。
- verify: 5影響既知値、同一joint合算、入力順置換、同重みtie、微小重み、ゼロ/負/非有限/不正joint/総和、閾値境界、出力保持/aliasを純試験で検証する。
- stop-when: glTF正規化整数のraw総和検査を省く接続、未検証kernelのdecode適用、Strict/頂点ABI/形式の変更。

## G2-GR86-OPTIONS-HASH: 縮約指定と診断の型・hashを定義する
- status: done
- done-when: Strict既定/ReduceToFourの明示指定と警告・失敗閾値、走査範囲付きreportを独立型にし、Strictは既存hash不変・Reduceだけ正規化policyをsource hashへ連結可能にする。
- verify: 値域/未対応enum/Strict未使用閾値/符号付きゼロ/既知bytes/FNV状態/既定不変/閾値・algorithm変更を純試験で固定する。
- stop-when: 未実装Morph/Cubic optionの受理、未接続をCLI完了扱い、struct paddingでhashが変わる。

## G2-GR86-INFLUENCE-SETS: 複数joint/weightセットの記述を検証する
- status: done
- done-when: Reduce用にJOINTS_n/WEIGHTS_nを正準順へ集め、0始まり連続・同数pair・重複なし・accessor番号の型/範囲を検証して所有結果を返す。Strict gateは変更しない。
- verify: 順序違い/複数pair/欠損/非連続/重複/不正名/不正番号と失敗出力保持をpure/nativeに分けて検証する。
- stop-when: raw整数weight総和やjoint範囲検査を省くdecode接続、未検証のReduce受理、Strict既定の緩和。

## G2-GR86-REDUCE-DECODE: 明示Reduceを骨格decodeへ接続する
- status: done
- done-when: options指定時だけ全joint/weightセットの型/count/range/整数raw総和を検査し、共有kernelで4本へ縮約する。reportは正常prefixと失敗頂点の測定を区別し、raw/legacy/file入口で一致する。既定Strictは不変。
- verify: 5影響fixture、raw/GLB/legacy/fileの一致、閾値超過/負値/追加セット内の不正joint/UNORM総和/不正options、失敗Data/sourcebufferの非公開を登録する。
- stop-when: cooked wire/頂点ABI/128上限変更、未接続のcooker/CLI成功主張、捨てる影響の不正値黙認。

## G2-GR86-REDUCE-COOK: 縮約指定とhashを骨格cookへ接続する
- status: done
- done-when: 明示optionsをcook/preflightへ伝播し、同一source/settings/policy/algorithmから同じhashを得る。Strict hashとwireは不変、失敗out保持、縮約診断を返す。
- verify: Strict旧hash、Reduce cooked値/診断、preflight同値、warn/fail変更hash、閾値超過out保持をnativeへ登録。純hash回帰を実行する。
- stop-when: CLI未接続なのに利用可能と主張、形式/ABI/128上限変更、設定指定の黙殺。

## G2-GR86-REDUCE-CLI: 明示縮約と閾値をCLIから指定する
- status: done
- done-when: --skin-influences strict|reduceとwarn/fail閾値を厳密parseし、skeletal cook/skipの両方へ同じpolicyを伝播する。既定Strict/非骨格拒否/重複・不正・無意味指定拒否と脱落量診断を維持する。
- verify: pure parserの通常/O2/sanitizer/MEMBER、native CLI smokeで縮約成功・既定拒否・閾値失敗出力保持・policy変更でcache miss/同値でhitを登録する。
- stop-when: 未実装のCUBICSPLINE/morph/joint-policyを受理する、skipだけStrictのまま、native未実行を成功と主張する。

## G2-GR86-IMPORT-REPORT: 縮約・焼込・dropのJSON診断を共有形式へまとめる
- status: done
- done-when: 実装済みpolicyの測定と失敗を版付きImportReportへまとめ、GR84の将来拡張と区別する。CLI stderr警告と同じ測定を持つ。
- verify: JSON文法/数値/未完走査と失敗計測の分離/決定的出力、書込経路を足す場合は既存入力・出力保護。
- stop-when: 未実装のBVH/retargetを実装済みとして報告、既存出力保護の迂回。

## G2-GR86-CUBIC-KERNEL: CUBICSPLINEの評価と保守的誤差境界を実装する
- status: done
- done-when: double Hermite/Bezierの区間包絡・評価・分割と理想LINEAR近似の全区間誤差上界を定義する。float保存端点との差を含め、vector長さと回転角の単位を区別する。実samplerの数値丸め認証/短区間は次taskで扱い、このkernelだけでruntime保証としない。
- verify: 公式式の既知値、非単位時間、S字中点相殺、回転正規化/ゼロ/半球と実sampler条件、誤差境界を純kernelで反証する。
- stop-when: 中点標本だけで全区間保証とする、tangent符号の暗黙変更、誤差のメートル/角度混同、未接続decode受理。

## G2-GR86-CUBIC-RUNTIME: 実sampler条件で焼込誤差を認証する
- status: done
- done-when: 数学kernel上界に保存float時刻/値と実runtime数値条件を合わせ、短duration/半球/NLERPを扱う。保証対象・数値仮定・許容の下限を明記し、深さ/sample予算内で全区間が認証できた場合だけ所有LINEAR列を返す。
- verify: 中点相殺/巨大tangent/非単位dt/float時刻衝突/近ゼロquaternion/短duration/予算超過、最終キーと全区間境界。
- stop-when: 任意epsilonだけで形式的runtime保証とする、未認証区間を成功公開、元cubic符号変更、変換後メートル誤差を無視する。

## G2-GR86-CUBIC-POLICY: 焼込指定・許容・診断・hashを定義する
- status: done
- done-when: Reject既定/Bake明示、translationメートル/rotationラジアン/scale無次元の許容、depth/channel/asset sample予算と診断を定義。未使用指定を拒否し、Bake無しの既存Strict/Reduce canonical/hashを維持する。Bake時だけ全設定/algorithmをhashへ追加する。
- verify: pure validation/canonical/hash golden、既存Strict/Reduce無変更、数値下限/不正/上限/無意味指定の拒否。
- stop-when: 未接続decoderがBakeを黙って無視して成功する、既存hash回帰、未実装morph/256を受理。

## G2-GR86-CUBIC-DECODE: 明示焼込を骨格decodeへ接続する
- status: done
- done-when: CUBICSPLINE triplet/count/有限時刻を検査して共通bakerへ渡す。fitを含む最終translation倍率のメートル空間で認証し二重scaleしない。raw/legacy/file/cookを同じ経路にし、prefix診断と失敗Data/source非公開を維持する。既定Rejectの挙動を維持する。
- verify: Translation/Rotation/Scaleの既知曲線、glTF/GLB、source triplet不正・zeroq・容量/閾値超過、sidecar scale/fitと実helper/parse往復。native未実行は区別する。
- stop-when: 元cubicの符号変更、scale後の許容超過黙認、partial成功、native/GPU実行済みと偽る。

## G2-GR86-CUBIC-CLI: 焼込CLIとJSON診断を接続する
- status: done
- done-when: --cubicspline reject|bakeと許容指定をstrict parseし、cook/skipへ同じpolicyを渡す。焼込channels/keys/単位別上界/失敗をImportReportへ格納し、未走査はnullとする。
- verify: pure parser/JSON、nativeCLI成功・既定拒否・失敗保持・設定差cache鍵、診断単位。
- stop-when: 未実装morph/256指定を受理、cacheと本cookの設定不一致、未測定をゼロと偽る。

## G2-GR86-MORPH-POLICY: 明示dropの方針・数量診断・hashを定義する
- status: done
- done-when: Reject既定/Drop明示を公開optionsへ定義し、既存Strict/Reduce/Bakeのみのhashを維持する。Dropはalgorithmを含む別canonicalで区別し、未接続段階で指定を黙って無視しない。
- verify: enum/旧hash golden/Dropの全既存policy組合せと型単独includeを純試験で検査する。
- stop-when: 未接続Dropの成功黙認、既存cache鍵の予期しない変更、sparse/256の受理。

## G2-GR86-MORPH-DECODE: 明示dropを共通decodeとcookへ接続する
- status: done
- done-when: morph targets・初期weight・weight animationの除去を明示Dropだけに限定し、base geometryとTRSを維持する。対象の構造を検査し、除去数/走査状況を報告する。既定Reject/sparse拒否/失敗出力保持を維持する。
- verify: raw/legacy/file/GLB/cookの既定拒否/明示成功/数量/不正・sparse/他policy併用と出力保持を登録し、純部品の試験を実行する。
- stop-when: malformedをDropで成功扱い、baseへmorphを黙って焼込、未測定数を確定と偽る。

## G2-GR86-MORPH-CLI: 明示dropをCLIとJSONへ接続する
- status: done
- done-when: --morph reject|dropを厳密parseしcook/skipへ同じpolicyを渡す。除去数・警告・未走査nullをJSONへ格納し、既定は拒否する。
- verify: pure parser/JSONとnativeCLI既定拒否/明示成功/出力保持/設定差cache鍵を検査または登録する。
- stop-when: 指定無しの無言drop、sparse/256受理、native未実行を成功と主張。

## G2-GR86-VALIDATION: 前半の実装と検証範囲を固定する
- status: done
- done-when: Strict/Reduce/Bake/Dropの現行実装、純検証、native未実行、既知の入力制限、v1待ち256を分けて記録する。
- verify: 現行ソースから関連portable testsを再ビルド・実行し、native登録/ログ/公開branchと照合する。
- stop-when: Windows/GPU/実物assetの受入れを未実行のまま合格とする、GR86後半256まで完了と扱う。

## G2-SELECT-S3-MATERIAL-FORMAT: 共通材質レコードの載せ先を確定する
- status: done
- done-when: S3(a)のNVMESH v1/128B材質レコード/v0併読案と代案を作者へ確認し、GR79/GR32/GR82の共有仕様へ反映する。
- notes: S1のNVSKEL版承認はS3の材質選定承認を含まない。ARM既定とemissive nitsは別判断として保留する。

- decision: 2026-10-04作者承認。S3(a)はNVMESH v1/128B材質/v0併読、S7は1mesh/Nprimitiveの案A。ARM/emissive既定は別途保留。

## G2-SELECT-S7-MESH-PROFILE: 骨格mesh数の受理範囲を確定する
- status: done
- done-when: S7の1mesh/Nprimitiveまたは同skin・同transform複数mesh案を作者へ確認してGR32へ反映する。
- notes: S1承認だけでmesh/node契約は緩和しない。

- decision: 2026-10-04作者承認。S3(a)はNVMESH v1/128B材質/v0併読、S7は1mesh/Nprimitiveの案A。ARM/emissive既定は別途保留。

## G2-GR32-GR82-MIGRATION-INVENTORY: 承認済み形式移行の変更箇所を整理する
- status: done
- done-when: 現行0.0/0.1の単一primitive/clip契約、統一0.2への影響箇所、v1作成時restの現在の欠落箇所と必須試験を棚卸しする。未選定S3/S7の結論・wire詳細を先取りしない。
- verify: 実header/decoder/cooker/loader/resource/component/testと作者の決定記録を照合する。
- stop-when: 受理条件/API/wireを変更、比較空間や閾値を未決のまま固定、未実装0.2/v1を実装済みと表現する。

## G2-GR32-SUBMESH-CONTRACT: 素のsubmesh型とpacked範囲検証を定義する
- status: done
- done-when: Asset/Resource層でRenderingに依存しないSubMesh/MaterialSlot型を持ち、1..8の三角形範囲が全indexを昇順・隙間/重複なしで覆うこととslot境界を検査する。両表空は旧互換の1範囲/slot0へ解釈し、片側空は拒否する。まだdecoder/wire/描画の受理は変えない。
- verify: pure kernelの正常1/2/8範囲、欠落/重複/隙間/逆順/非三角形/u32境界/片側空/null入力/失敗結果、MEMBER compileと既存pure回帰。
- stop-when: 0.2を書き始める、複数meshを受理、Render型へ依存、旧形式の判定/頂点ABI/描画既定を変更。

## G2-GR32-V02-SCHEMA: 統一0.2の版別wire契約を固定する
- status: done
- done-when: 旧0.0/0.1の256B/hash/単一clip契約を保ち、0.2の320B、submesh64B、Name16B+予約48Bのslot64B、複数clipの所有範囲と128関節を同じschemaへ定義する。版別header/hash/節検証のpure部品を用意し、writer/loaderの受理はまだ切り替えない。
- verify: 固定byte/独立hash golden、版別定数/節境界/overflow/reserved/未対応版、旧hash不変、MEMBER/CTest登録。
- stop-when: 同じminor2の予約を後でtextureへ転用、0.2へ共有128B材質を暗黙追加、旧版判定の全面緩和、writerを先に切り替える。

## G2-GR32-PRIMITIVE-DECODE: 1mesh内の複数primitiveを所有表へ変換する
- status: done
- done-when: 1mesh/1skin/1mesh-nodeを維持して1..8primitiveを連結し、local index検証後にbaseVertexを加えた絶対indexとpacked submesh/一意slot名を返す。Reduce/Morph/Bake/scaleの資産集計を保ち、下流未接続時は黙って材質を落とさず拒否する。
- verify: 2/8primitiveの連結/巻き/材質省略とindex0の区別、範囲/9件/複数mesh拒否、Reduce全体prefix/Morph幅と総数/fitを登録する。
- stop-when: primitiveを跨いだindex越境を許す、scale二重適用、未接続cook/runtimeが成功して表を失う。

## G2-GR32-V02-TEXT: 0.2の名前byte列と参照境界を検証する
- status: done
- done-when: 0.0/0.1のprintable ASCIIを維持し、0.2のUTF-8/NUL拒否とbyte単位StringRefを共通部品で検査する。CoreのTCHARと保存byteの変換契約を明確にし、失敗出力保持を満たす。writer/readerはまだ切り替えない。
- verify: ASCII/多byte/境界codepoint/不正UTF-8/overlong/surrogate/NUL/範囲overflow/失敗保持の純試験とMEMBER/CTest登録。
- stop-when: 旧版で非ASCIIを受理、壊れた文字列を置換して成功、UTF-16単位数を保存byte長と混同する。

## G2-GR32-V02-READER: 版別の骨格表とclip所有を読み込む
- status: done
- done-when: 旧0.0/0.1を保持して0.2の320B/submesh/名前slot/版別文字列を所有dataへ読む。record値/reserved/padding/範囲/hashとclip/channel/sampleの一意所有を検査する。新表を未対応下流が捨てないよう明示ガードを置く。
- verify: 手書き0.2/複数clip/旧golden、重複/隙間/跨ぎ/不正flags/bounds/文字列、失敗Data非公開。
- stop-when: 旧clipCount拒否を緩和、同minorで表を読み分ける裏分岐、部分資産公開、未対応runtimeへ無言で流す。

## G2-GR32-V02-WRITER: 統一0.2をcookして旧cacheを再生成する
- status: done
- done-when: submesh/slot/複数clip共通schemaの0.2を生成し再parseで照合する。1primitiveも名前slotへ具体化し、旧空表は互換1件へ変換する。cacheはcurrent writer minorを検査し旧版のままskipしない。既存出力保護を保つ。
- verify: raw/GLB/cook/parseの表一致、旧版loadとcache再生成、slot/UTF-8/範囲、CLI metadata/失敗保持。
- stop-when: table/nameを失う、旧cacheを新writer済み扱い、Resource/描画の未接続を成功と偽る。

## G2-GR32-RESOURCE-TABLES: 所有資産と不変leaseへsubmesh表を保持する
- status: done
- done-when: SkinnedMeshResourceがsubmesh/slot名を所有・検証し、不変AssetLeaseへ世代単位で保持する。旧空表/3引数lease互換を保ち、未対応描画への新表の無言破棄はguardする。
- verify: 旧互換/1・2・8表/片側空/範囲/名前保持/世代とlease寿命/不正時の非公開をCPU契約へ登録する。native未実行は明記する。
- stop-when: RenderThreadがmutable Resourceを参照、表の無言破棄、旧frame leaseの内容を後から書換える。

## G2-GR32-COMPONENT-SLOTS: 材質slotの名前APIとframe snapshotを接続する
- status: done
- done-when: SkinnedMeshComponentがindex/名前でslot材質を設定し、従来SetMaterial/GetMaterialの互換を保つ。FramePacketへ値所有で渡し、欠落slot/古いresource世代の扱いを定義する。
- verify: index/名前/重複・不在/旧単一材質fallback/世代差し替え/描画thread越境のCPU契約。
- stop-when: mutable componentを描画threadが読む、曖昧な名前を無言で別slotへ適用、既存材質を破壊する。

## G2-GR32-COOK-METADATA: cook metadataへ表数量を記録する
- status: done
- done-when: 新cookのmanifest metadataへsubmesh_count/material_slot_countを保存し、旧manifestの省略を未知として保持する。既存項目/merge/cacheを壊さず、欠落を1件と捏造しない。
- verify: 1/2/8表の数量、旧省略/不正値、manifest再読込/merge、既存CLI metadata回帰を登録する。
- stop-when: 旧省略から件数を推測して表示、wireとmetadataが不一致、既存metadataをmergeで失う。

## G2-GR32-DRAW-RANGES: submesh別描画とcomponent単位palette共有を接続する
- status: done
- done-when: AppendSkinnedDrawCommandsとRecordSkinnedDrawCallが絶対index範囲/baseVertex0/slot/NoShadowを使う。途中guardを接続済み経路で解除し、palette共有は別taskへ分ける。
- verify: 計画指定の独立SkinnedSubmeshDrawContractTestへdraw数/範囲/影flag/palette共有を登録する。GPU受入れは別gateに残す。
- stop-when: submeshごとに重いskinning準備を複製、範囲外draw、既定描画/シェーダーABIを変更、GPU実行無しでGR32全体完了を宣言。

## G2-GR32-PALETTE-SHARING: component単位のpalette共有を接続する
- status: done
- done-when: GBuffer/CSM/点光源影/viewport/submeshで同componentの準備を共有し、別component・世代・フレームや異なるposeを混同しない。frame leaseとsubmitted serialの寿命を保つ。
- verify: 1component・1frameのSkinnedPalette作成1回、SkinnedPreviousPaletteはGBuffer対象だけ最大1回。影はprevious非束縛を維持し、両パス順序・複数submesh/viewport・世代/別pose・abort/寿命を独立draw契約で検査する。
- stop-when: 他component/古いframeのpaletteを流用、prepared tokenを寿命検査なしに使う、参照中またはGPU使用中のbufferを解放する。

## G2-GR79-MATERIAL-WIRE: 共有128B材質レコードの純codecを固定する
- status: done
- done-when: 承認済み128B配置/4StringRef/係数/flagsをRendering非依存の共通型とlittle-endian codecへ固定し、予約/参照境界/数値を検査する。wireのDefaultLit=0とruntime enumを混同しない。ARM方針やnits換算既定は決めず、現reader/writerへまだ接続しない。
- verify: 独立byte golden、flags/alpha/負の未指定係数、非有限/不正reserved/参照overflow/失敗保持、通常/O2/sanitizer、MEMBER/CTest登録。
- stop-when: ARM既定やemissive nits既定を採用したと扱う、未知flag/予約を黙って無視、native/GPU未実行を受入れ済みとする。

## G2-GR32-POINT-SHADOW-BUDGET: 点光源影のUBO消費をcomponent単位に共有する
- status: done
- done-when: 各light/faceで同componentの全submeshが同じUBO/descriptorを共有し、16componentの容量で途中の部位だけを落とさない。影にpreviousを束縛しない。
- verify: 8submesh×16component、17番目の全体省略、face分離、旧匿名fallback、binding10無しをCPU契約へ登録する。
- stop-when: material/submesh数でUBO枠が増える、faceを跨いだdescriptor上書き、GPU未検証を合格扱いする。

## G2-GR32-POSE-HISTORY-GENERATION: 前姿勢を資産世代へ束縛する
- status: done
- done-when: GameThreadとRenderedObjectHistoryの前姿勢がcomponentIdに加えて資産handle/generation一致を要求し、同bone数の別資産を混同しない。component単位で履歴を記録する。
- verify: 同bone数のreload・別資産・frame gap・複数submesh/viewportで前姿勢とfallbackを検査する。
- stop-when: mutable Resource参照、別世代poseの流用、既存velocity基準の変更。

## G2-GR82-A1-MULTI-CLIP-DECODE: Stage Aの複数clip取り込みを接続する
- status: done
- done-when: 新DecodeRigGltfのStage A入口で1mesh/1skin/1mesh-node・primitive8/joint128を維持しanimations1以上を取り込み、cookへ接続する。旧DecodeSkeletalGltf/AnalyzerのTwoClips/IntermediateNode拒否は維持。NVSKEL0.2は既存の統一定義を使う。
- verify: 3clip glTF/GLB→cook→parseで名前/duration/channel一致、3本目失敗でData/source buffers非公開、Cubic全clip予算/global prefix、Morph root数量1回+全clip weight channels、scale1回を検証する。pure report試験とnative未実行を区別する。
- stop-when: 旧strict入口を緩める、budgetをclipごとリセット、root morphをclip数倍、Stage BやM9まで対応済み扱いする。

## G2-GR82-A2-CLIP-RESOURCES: 複数clipの保持と一意名前引きを接続する
- status: done
- done-when: SkeletalAssetResourceで複数clipを保持しGetClipCount/GetClip(name)を追加。単数SetResources互換とGetAnimationClip先頭を維持。空/重複名は名前引きで曖昧拒否し資産自体を一律拒否しない。
- verify: 単数互換/配列所有/子Load/不正子/Unload/名前欠落重複/強参照寿命/メモリ計上をnative登録。
- stop-when: M9の複数clip guardを無断解除、制作alias規約を勝手に採用、Resource寿命破壊。

## G2-GR79-IMPORT-POLICY-CORE: ARM百分位と発光の必須設定を固定する
- status: done
- done-when: AI生成profileのAO/metallic ignore・roughness auto、素材override、1〜99百分位幅での定数判定、emissiveFactor×strengthの非0判定とnits換算明示必須を純関数にする。asset-set単位のfallbackを受けるが高優先の不正値を無言fallbackしない。
- verify: TRELLIS型B192〜255/Pixal型G249〜255外れ値、texture override/因子/正しいAO式、百分位rank境界、textureのみ/strength0/未設定/非有限/上限、設定優先順位を通常/O2/sanitizerで反証する。
- stop-when: 発光textureだけで未設定拒否、AI犬の金属をautoへ戻す、max-minを定数判定に使う、asset/material名付き診断やasset-set実配線を未実装のままGR79全体完了扱いする。

## G2-GR79-MESH-V1-WIRE: NVMESH v1の外枠とLOD0レコードを固定する
- status: done
- done-when: v0定数を不変の純headerへ分離し、v1.0 Magic/Header256/Material128/Cluster128を定義。外枠のversion/サイズ/予約/整列/packed節/hashとLOD0 clusterを純検証する。旧parser/cooker受理は変えない。
- verify: 独立byte golden、v0定数無変更、v1サイズ/未知版/予約/overflow/整列/padding/hash/LOD予約・範囲、失敗保持/alias拒否を通常/O2/sanitizerとMEMBERで検証。
- stop-when: 未接続のv1 runtimeを受理済み扱い、material係数を無言で捨てる、GR80用の拡張48Bを別用途へ転用、旧v0受理契約を破壊する。

## G2-GR79-MESH-V1-READER: v1材質と複数submeshを所有読込する
- status: done
- done-when: v0既存読込を維持し、v1の材質128/4参照/cluster128を意味検証して保持する。submeshが全index/clusterを一意分割し材質対応・実index/絶対上限・unique128を検査。v0は従来値へ昇格。runtimeは係数adapter未接続のv1とN>1を明示拒否し黙示的な情報欠落を防ぐ。
- verify: v0/v1/複数材質golden、係数bit/4path/寿命/不正所有とreserved/数値、pure partition通常/O2/sanitizer、native登録。既存v0手組みstaging互換を保つ。
- stop-when: v0の受理や見えを変更、v1材質をpathだけへ落とす、full readerとGPU受入れを混同、unique数をVertexCount fieldと混同。

## G2-RESOURCE-IO-NAMESPACE: 読込名前空間とResource基底クラスの衝突を解消する
- status: done
- done-when: Core::Resourceの基底クラスを維持し、GLTFAnalyzer/ModelStaging/loader群の名前空間だけをResourceIOへ分ける。全参照を更新し、同TUで骨格Resourceと共存する。wire/アルゴリズム/既定描画は変更しない。
- verify: 宣言/参照の列挙、旧namespace残存ゼロ・class参照不変、置換を戻したsourceの同一性、両include順のnative compile契約登録、実Windows未実行を区別する。
- stop-when: Resource基底クラス/反射名を変更、loader参照の取り残し、文字列置換で本体の挙動を変える、独立言語例をnative成功扱いする。

## G2-S6-CLI-BYTE-SMOKE: 単体CLI分割前後のバイト比較手順を固定する
- status: done
- done-when: 既存raw/texture/audio/model/GLB/import/骨格smokeを変更せず実行し、成功した全caseのpackage/manifestを正規化せず保存・比較するツールと反証テストを追加する。Main分割は実smokeと前後byte一致確認まで保留。
- verify: 比較器の一致/追加/欠落/byte差/JSON空白/recipe差/改竄/空出力/pathをPythonで検証。実AssetCookとWindows smokeは別の未実行gateとして記録する。
- stop-when: 比較器の単体成功をCLIの実成功扱い、JSON正規化で差を消す、既存出力を上書き、Mainを先に分割する。

## G2-GR79-MATERIAL-SETTINGS-VALUE: 材質設定の解決と正規形を固定する
- status: done
- done-when: profileと素材/資産/asset-setの設定を失敗保持で解決し、解決済み材質の独立canonical/hashを用意。幾何52Bと旧v0経路を変更しない。
- verify: AI既定と素材上書き/明示source復帰/不正下位値拒否/換算不在許可、canonical独立golden/bit/未使用値/-0、設定差hashを通常/O2/sanitizerとMEMBERで検証。
- stop-when: 未接続JSON/CLI/asset-set/素材識別子を実装済みとする、発光の有無を見る前に換算不在だけで拒否、旧hashを変更する。

## G2-GR79-MATERIAL-SETTINGS-JSON: 素材選択と独立した設定値JSONを厳密解析する
- status: done
- done-when: ARM mode/constant、profile、両面、alpha、換算値のJSON blockを重複/未知/型/値域の拒否と失敗保持で解析。素材selectorは作者回答待ち、既存sidecar APIは未接続のmaterialを引き続き拒否する。
- verify: ARM token通常/O2/sanitizer/MEMBER、実JsonDocumentの省略/空/正常/未知/重複/null/非ASCII/NUL/後段失敗をnative登録。旧sidecar受理条件とMainのbyte不変を確認。
- stop-when: parserの用意だけでsidecar/CLIに接続済みと言う、素材識別方式を先取り、警告だけで発光換算未設定を通す。

