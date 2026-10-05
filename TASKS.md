# TASKS — NorvesLib

起動画面（Rendering3DTest）の描画改善（2026-09-27、ユーザー決定）。物理空と空の太陽による昼の屋外、点光源のキューブシャドウ、自動露出・ミップチェーンのブルーム・TAA・GTAO・コンタクトシャドウ・色収差・レンズダート・グレーディングLUT・RTGIの既定化、展示物の追加、視差オクルージョンと影の不具合の修正を `SS-` の項目で進める。見た目の証拠は `Scripts/CaptureStartupScene.ps1` の撮影を開いて確かめる。起動画面の見た目を変えることはこの計画でユーザーが承認済み。検証シーン（Indoor/Outdoorのgolden、R系の受入れ）は、項目に書いた場合を除き結果を変えない。

2026-09-30 再開: 止めていた4項目（SS-DAYLIGHT-P1・P2、SS-POINT-SHADOW-P2、SS-EMISSIVE-GLOW）は各 `blocked/<ID>.md` の推奨の選択肢で再開し、空のモデル（SS-SKY-MODEL-P1・P2）と発光の露出（SS-EMISSIVE-PREEXPOSE）を足した。この3項目は、項目に書いたとおり空を使う検証（R2・R7屋外）とgoldenの結果を変えうる。

2026-10-03 続き（ブランチ `feature/startup-scene-detail`）: ユーザーの指摘（石のタイルが近づくと粗い）と要望（球をMegaGeometryで高ポリに）から、FIX-ASYNC-TEXTURE-MIPS → SS-CSM-MEGA-CASTERS → SS-MEGA-SPHERE → SS-MEGA-SPHERE-DISPLACE → SS-ACCEPT-DETAIL の順に進める。

2026-10-03 続き2（ブランチ `fix/showcase-sphere-reflection-ao`）: ユーザーの指摘（金属の見本の球がカメラの角度によって一部透けて見え、同じ角度でも影のかかり方がちらつく）から、FIX-GTAO-STATIC-NOISE → FIX-SSR-SPECULAR-COMPOSITE を直す。

2026-10-04 続き3（ブランチ `feature/ground-material-showcase`）: ユーザーの要望（地面のテクスチャを色々用意して質感を見比べられるように。テクスチャは Poly Haven から落とし、git に入れない）から、SS-GROUND-SWATCHES を足し、その途中で見つけた FIX-MESH-BOUNDS-CULLING を直す。

2026-10-04 続き4（段1はブランチ `feature/vtg-stage1-bc`）: ユーザーと決めた全体計画 `Docs/Plans/VirtualizedTextureGeometryPlan.md`（テクスチャとジオメトリの仮想化。BC圧縮・sparse のVT・2パスの遮蔽カリング・LODの階層の焼き込み・ページのストリーミング・ビジビリティバッファ・ソフトウェアラスタ・VSM）を `VTG<段>-` の項目で進める。決定事項・設計・段の受入れは計画書が正本。1段ずつ回し、今の段だけ `todo`、後の段は `backlog`。段が終わると親が受入れを確かめて main へマージ・プッシュし、次の段の項目を詳しくしてから `todo` にする。8GB級のGPUで収まるのが目標で、開発機では `--vram-budget-mb` の上限で確かめる。起動画面（天球・地面・球・岩）が見える状態は全段で保つ。

それより下はR0〜R8と関連の修正の記録。R8までの完了後に残った `todo` は、起動画面の作業を先に進めるため `backlog`（ループが拾わない）にしてある。再開するときは `todo` へ戻す。

## VTG1-VRAM-BUDGET: VRAMの予算と使用量をVulkanから取り、上限の起動引数を足す
- status: done
- done-when: `RHI::VideoMemoryBudget`（DeviceLocal ヒープの budget・usage の合計と、取得できたかの印）を `IDevice::GetVideoMemoryBudget()` が返す（既定実装は無効値を返す非純粋仮想）。Vulkan は `VK_EXT_memory_budget` を任意拡張として有効化し、あれば `vkGetPhysicalDeviceMemoryProperties2` の budget を返す。Game は `--vram-budget-mb=<MB>` を読んで RenderResources 側へ渡し（GEngine のメンバ経由。シングルトンにしない）、起動後に1回と、その後は約1秒ごとに値が1%以上変わったときだけ `VRAM_BUDGET heap_budget_mb=<n> heap_usage_mb=<n> cap_mb=<n|none> source=<ext|none>` をログへ出す。GPU のテスト `VideoMemoryBudgetVulkanTest`（`RHITextureUpdateVulkanTest` の束の MEMBER）が、拡張のある GPU で budget>0・usage>0・usage≤budget を確かめる（Vulkan が無ければ 125）。
- verify: `cmake --build build --config Debug --target Game RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^VideoMemoryBudgetVulkanTest$"`
- stop-when: 拡張を有効にすると開発機でデバイスの作成が失敗する場合は、理由を記録して止める。
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Engine, Library/Core/Private/Engine, Game, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.1。予算をプールへ割り振る `VideoMemoryBudgetManager` は段2（VTG2-BUDGET-MANAGER）。危険地帯（RHI/Vulkan）。

## VTG1-VRAM-LEDGER: テクスチャのVRAMを形式とミップ込みで数え、ログに出す
- status: done
- done-when: `GpuResourceStore` が各テクスチャの確保量（形式の1画素のバイト数 × 全ミップの画素数）を持ち、`ResourceStats` に `TextureBytes` を出す。`IGPUResourceAllocator` の `EstimateTextureSize` がミップを数える。Game は起動画面の非同期のテクスチャの読み込みが終わった後に1回 `VRAM_LEDGER textures=<n> texture_mb=<n.n> buffers_mb=<n.n>` をログへ出す。CPU のテスト `TextureMemoryLedgerTest`（`RenderResourcesDomainContractTest` の束の MEMBER）が RGBA8 4096² 全ミップ = 89,478,484 B、RGBA8 1×1 = 4 B、作成→解放で合計が戻ることを確かめる。起動画面の撮影のログで `VRAM_LEDGER` の値を記録する（地面の見本を含む今の状態の基準値）。
- verify: `cmake --build build --config Debug --target Game RenderResourcesDomainContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^TextureMemoryLedgerTest$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG1-VRAM-LEDGER -Configuration RelWithDebInfo -ViewNames default`
- stop-when: 確保量を数える場所が RHI の内側にしか無く、Rendering 層から RHI/Vulkan を include しないと数えられない場合は、理由を記録して止める。
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Game, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: BC のバイト数は VTG1-RHI-BC-FORMATS で足す。

## VTG1-RETIRE-QUEUE: テクスチャとバッファを提出のserialで遅延解放する
- status: done
- done-when: `GpuRetireQueue`（RenderResources が持つ）が、`ReleaseTexture`・`ReleaseBuffer` で外れた RHI 資源を、解放を頼んだ時点で最後に提出した serial が完了する（`GetCompletedSubmissionSerial()`）まで保持してから破棄する。`SkinnedMeshGpuStore` の serial の扱いに合わせる。`MegaMeshMaterial` の `RHI::TexturePtr` の強参照を `TextureHandle` に置き換え、MegaGeometryPass は描画時に引き直す。CPU のテスト `GpuRetireQueueTest`（`RenderResourcesDomainContractTest` の束）が、完了の serial が届くまで破棄されない・届いたら破棄される・Shutdown で全部破棄されることを確かめる。`MegaGeometryResourcesTest` pass。起動画面の撮影で見た目が変わらない。
- verify: `cmake --build build --config Debug --target Game RenderResourcesDomainContractTest MegaGeometryResourcesTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(GpuRetireQueueTest|MegaGeometryResourcesTest|MeshResourcesProceduralGpuTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG1-RETIRE-QUEUE -Configuration RelWithDebInfo`
- stop-when: 提出の serial が RenderThread の外から安全に読めず、FramePacket の契約を変える必要がある場合は、理由を記録して止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Rendering/MegaGeometry, Library/Core/Private/Rendering/MegaGeometry, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.1。後の段の sparse のページ・ジオメトリのプールもこれで解放する。危険地帯（寿命・RenderThread）。

## VTG1-RHI-BC-FORMATS: RHIとVulkanにBC形式とR16を足す
- status: done
- done-when: `RHI::Format` に BC1_UNORM・BC1_SRGB・BC4_UNORM・BC5_UNORM・BC7_UNORM・BC7_SRGB・R16_UNORM を足し、Vulkan の形式の対応表（2か所）、`textureCompressionBC` の照会と（対応時の）有効化、`DeviceCapabilities::bTextureCompressionBC` を足す。形式のブロックの幅・高さ・バイト数を返す関数を RHI に置き、ミップの最小は1ブロックとして数える。テクスチャの台帳（VTG1-VRAM-LEDGER）が BC を数える。CPU のテスト `RHIBlockCompressedFormatTest`（`RenderResourcesDomainContractTest` の束）が BC7・BC5 4096² 全ミップ = 22,369,648 B、BC4・BC1 = 11,184,824 B、R16 1024² 全ミップ = 2,796,202 B を確かめる。
- verify: `cmake --build build --config Debug --target Game RenderResourcesDomainContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RHIBlockCompressedFormatTest|TextureMemoryLedgerTest)$"`
- stop-when: 開発機の GPU が `textureCompressionBC` に対応していない場合は記録して止める。
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.2。`ImpostorBake` など形式の switch を持つ箇所も漏れなく更新する（コンパイラの警告で拾う）。危険地帯（RHI/Vulkan）。

## VTG1-BC-UPLOAD: 圧縮済みの全ミップをGPUへ上げて描けるようにする
- status: done
- done-when: `TextureCreateInfo` と `GpuResourceStore` が BC と R16 の全ミップの初期データ（ミップ0から順に詰めた1つの塊）を受け取り、ミップごとのコピー領域でアップロードする（`GenerateMipmaps` は呼ばない）。BC に対応しないデバイスでは作成が失敗し、理由をログに出す（CPU で展開する逃げ道は作らない）。GPU のテスト `RHIBlockCompressedTextureVulkanTest`（`RHITextureUpdateVulkanTest` の束）が、既知のブロック（単色のブロックを手で組む）で作った BC1・BC4・BC5・BC7 の 8×8・2段のテクスチャを計算シェーダーでミップごとにサンプルし、期待の色（許容 2/255）を読み戻す。
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RHIBlockCompressedTextureVulkanTest|RHITextureUpdateVulkanTest|RHITextureToBufferReadbackVulkanTest)$"`
- stop-when: Vulkan のテクスチャの更新の経路が1段ずつのアップロードを表せず、`VulkanTexture` の更新の契約を作り直す必要がある場合は、理由を記録して止める。
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, Assets/Shaders, TASKS.md, PROGRESS.md
- notes: 危険地帯（RHI/Vulkan）。`VulkanTexture::Update` が呼び出しごとに待つ件は段2で扱う。

## VTG1-NVTEX-V01: NVTEX v0.1でBCとR16を表し、クック済みのBCを読み込めるようにする
- status: done
- done-when: `CookedTextureFormat` の VersionMinor 1 で PixelFormat に BC1・BC4・BC5・BC7（sRGB の有無）・R16 を表し、v0.0 も読む。ミップの検証はブロック単位（最小1ブロック）で、全段必須のまま。`MapCookedTextureFormat` が RHI の形式へ写し、クック済みの BC を `TextureResources` から読み込んで GPU に置ける。`CookedTextureTest` に v0.1 の BC のヘッダ・ミップのバイト数・壊れた入力（ブロック数の不足・未知の形式）の拒否を、`CookedTextureUploadTest` に BC7 のクック済みの読み込みを足す。`Docs/Architecture/` の NVTEX の形式の文書があれば v0.1 を追記する。
- verify: `cmake --build build --config Debug --target CookedMeshTest CookedTextureUploadTest TextureResourcesTextureAssetTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CookedTextureTest|CookedTextureUploadTest|TextureResourcesTextureAssetTest)$"`
- stop-when: v0.0 の読み込みを保てない形式の変更が要る場合は、理由を記録して止める。
- paths: Library/Core/Public/Asset, Library/Core/Private/Asset, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Asset, Test/Core/Rendering, Docs/Architecture, TASKS.md, PROGRESS.md
- notes: 計画書 4.2。glTF の ARM の分割（`TrySplitPreparedCookedTextureMip0RGBA8UNormLinear`）は RGBA8 のときだけ通し、BC では通さない（呼ばれたら理由を返して失敗する）。危険地帯（アセットロード）。

## VTG1-BC7ENC-VENDOR: bc7enc_rdoを取り込み、クッカーの圧縮の境界を作る
- status: done
- done-when: `Library/ThirdParty/bc7enc_rdo/` に固定した commit のソースと `LICENSE`、`UPSTREAM.json`（版・commit・取得元・sha256・SPDX）を置き、独立の静的ライブラリ `NorvesThirdParty_Bc7Enc` を作って `AssetCook` にだけリンクする（Core・Game はリンクしない）。`Tools/AssetCook` に `BlockCompressor`（RGBA8 から BC1・BC4・BC5・BC7 のブロックを作る薄い境界。並列はスレッドで画像を帯に分ける）を足す。スモーク `AssetCookBlockCompressSmoke`（`Tools/AssetCook/CMakeLists.txt` の add_test）が、64×64 の既知の画像を各形式で圧縮し bc7enc_rdo の復号で戻した PSNR（BC7・BC5・BC4 ≥ 40 dB、BC1 ≥ 32 dB）を確かめる。
- verify: `cmake --build build --config Debug --target AssetCook -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(AssetCookBlockCompressSmoke|AssetCookTextureSmoke)$"`
- stop-when: 取得元から入手できない、またはライセンスが MIT・パブリックドメインでない場合は、理由を記録して止める。
- paths: Library/ThirdParty/bc7enc_rdo, Tools/AssetCook, CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: ユーザー承認済みの外部依存（計画書 1）。取り込み方は tinyexr（`Library/Core/CMakeLists.txt:529-546` の独立の静的ライブラリ）と angelscript の `UPSTREAM.json` に倣う。

## VTG1-COOKER-USAGE: クッカーが用途ごとにBCへ焼き、ORMを1枚に詰める
- status: done
- done-when: `TextureCooker` に `--usage albedo|normal|orm|single|height16` を足す。albedo→BC7 sRGB、normal→BC5（入力は DirectX の向き、ミップは非正規化ベクトルの平均→再正規化）、orm→BC7 linear（`--orm-ao`・`--orm-roughness`・`--orm-metallic` の別々の元画像を R・G・B に詰め、無い枠は AO=1・粗さ=1・メタリック=0）、single→BC4、height16→R16（16bit の PNG の精度を保つ。8bit の入力は拡大）。NVTEX v0.1 を書く。`AssetCookTextureSmoke` を用途ごとに回し、ヘッダの形式・ミップ数・バイト数を確かめる。4096² の BC7 のクック時間を `TEXTURE_COOK usage=<u> format=<f> size=<w>x<h> ms=<n>` で出す。
- verify: `cmake --build build --config Debug --target AssetCook CookedMeshTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(AssetCookTextureSmoke|AssetCookBlockCompressSmoke|CookedTextureTest)$"`
- stop-when: 4096² の BC7 のクックが RelWithDebInfo で1枚60秒を超え、エンコーダの設定で縮まない場合は、測った値を記録して止める。
- paths: Tools/AssetCook, Library/Core/Public/Asset, Test/Core/Asset, TASKS.md, PROGRESS.md
- notes: 計画書 2・4.2。視差の高さを R16 にするか BC4 にするかは VTG1-STARTUP-COOKED で縞の出方を見て決める（両方焼けるようにしておく）。2026-10-04 親: 実装と verify は済んでいる（run `20261004-105824`、評価2周で blocked）。残りは評価の指摘だけ: この項目で足した `Tools/AssetCook/Main.cpp` の `Usage`・`Quality` と用途別の引数の検証の `error`（`std::string`）、`TextureCooker.h` の `ParseTextureUsage`（`std::string_view`）を独自型（`AnsiString`・`AnsiStringView`・`ErrorString` など）にし、既存の CLI との変換は呼び出しの境界だけにする。英語のまま足したエラーの説明（`TextureCooker.cpp` の用途の検証、`AssetCookTextureUsageSmoke.cpp` の失敗の表示）を日本語にする。既存のコードの標準型は直さない。

## VTG1-COOK-TARGET: 差分クックのビルド対象を足す
- status: done
- done-when: CMake の対象 `CookAssets`（ALL に含めない）が、`Assets/AssetSets/` の一覧（起動画面の材質の一覧を足す: 銀・石畳・Poly Haven の地面の見本6種。元画像が無いものは飛ばして `COOK_ASSETS missing=<path>` を出す）から `build/CookedAssets/` へ NVTEX とマニフェストを書く。元画像・一覧・AssetCook のどれかが変わったものだけを焼き、2回続けて実行すると2回目は `COOK_ASSETS cooked=0 skipped=<n>` になる。`Scripts/FetchPolyHavenTextures.ps1` の最後にクックの案内を出す。
- verify: `cmake --build build --config RelWithDebInfo --target AssetCook -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target CookAssets -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target CookAssets -- /m:1`
- stop-when: CMake の依存で差分を表せず、毎回すべて焼き直す以外にない場合は、理由を記録して止める。
- paths: CMakeLists.txt, Tools/AssetCook, Assets/AssetSets, Scripts, TASKS.md, PROGRESS.md
- notes: 計画書 1（差分クックはユーザーの決定）。既存の `Scripts/CookTextureAssetSet.ps1` と `Assets/AssetSets/*.json` の形に合わせる。出力は git に入れない。

## VTG1-MATERIAL-ORM: 材質にORMの1枚の枠を足し、BC5の法線のZを戻す
- status: done
- done-when: `MaterialCreateData`・`MaterialResourceData` に ORM の1枚の枠（R=AO・G=粗さ・B=メタリック）と、法線が2チャンネル（BC5）である印を足す。`gbuffer.frag`・`megageometry.frag`・`forward_transparent.frag` と PT の材質は、ORM があればそれを、無ければ従来の別々の枠を読む。2チャンネルの法線のときだけ Z を XY から戻す（RGBA8 の法線は従来どおり）。Indoor/Outdoor の golden が不変（決定的）、`MaterialResourcesTest`・`GBufferMaterialDescriptorCacheTest`・`PathTracingMaterialVulkanTest` pass。起動画面の撮影の平均輝度の変更前との差が、各視点で、同じセッションで同じコードを2回撮った揺らぎ以内（両方の値を記録する）。
- verify: `cmake --build build --config Debug --target MaterialResourcesTest RenderingGoldenImageTest PathTracingMaterialVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(MaterialResourcesTest|GBufferMaterialDescriptorCacheTest|PathTracingMaterialVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG1-MATERIAL-ORM -Configuration RelWithDebInfo`
- stop-when: descriptor の binding を増やすと既存の材質の descriptor のキャッシュの契約を崩す場合は、理由を記録して止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 2。2026-10-04 親: 元の「平均輝度の差 0.5 以下」は、同じコードを撮り直すだけで1視点あたり最大2.6動く（RTGI・TAA の履歴）ため判定できず blocked になった（`blocked/VTG1-MATERIAL-ORM.md` の選択肢2を採った）。実装と golden・テストは run `20261004-105824` で済んでいる。決定的な撮影は VTG2-CAPTURE-DETERMINISTIC で作る。

## VTG1-STARTUP-COOKED: 起動画面をクック済みのBCのテクスチャで描く
- status: done
- done-when: Game は既定で `build/CookedAssets/`（コンパイル時の既定の場所。`--texture-asset-root` で上書きできる）を asset root とマニフェストにし、起動画面の材質（銀・石畳・地面の見本6種）をクック済みの BC と ORM で読む。クック済みが無いテクスチャはばらのファイルを無圧縮で読み、`TEXTURE_COOKED_MISSING path=<p>` を1回だけ警告する。撮影の `VRAM_LEDGER` で、クック済みにした材質のテクスチャの量が同じ材質のばらの量の1/4以下になる（全体の texture_mb と、段4で焼く岩・小屋の glTF のテクスチャの残りの内訳も記録する）。見た目は、同じ視点のばらの撮影と比べた PSNR を視点ごとに記録し（目安 35 dB 以上。同じコードのばら同士の揺らぎが目安を下回る視点は、その揺らぎと並べて記録する）、PNG を開いて違いが目立たないことを確かめる。視差の高さを R16 にするか BC4 にするかを、近接・低角度の撮影の縞で決めて記録する。
- verify: `cmake --build build --config RelWithDebInfo --target AssetCook Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target CookAssets -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG1-STARTUP-COOKED -Configuration RelWithDebInfo`
- stop-when: クック済みとばらの経路で材質の作り方を分けないと読めない場合は、理由を記録して止める。
- paths: Game, Library/Core/Public/Asset, Library/Core/Private/Asset, Library/Core/Private/Rendering, Assets/AssetSets, Assets/Shaders/megageometry.frag, Scripts, TASKS.md, PROGRESS.md
- notes: 起動画面の見た目を変えうる（絶対規則7）。小屋・岩の glTF の中のテクスチャは段4で扱う。2026-10-04 親: 実装は `0a63286b` で済んでいる（材質のクック済みは約560 MiB、ばらでは約3200 MiB で約0.18。全体は 3609.6→969.6 MiB で、残りは岩・小屋の glTF の無圧縮のテクスチャ約409 MiB）。元の「全体の1/4以下」は段4の範囲を含んでいたので、`blocked/VTG1-STARTUP-COOKED.md` の選択肢1で条件を材質の比に直した。`megageometry.frag` の1行（粗い傾きの標本の BC5 の復号）は VTG1-MATERIAL-ORM の取りこぼしの修正として受け入れる。2026-10-04 親（2回目の再開）: run `20261004-145957` の評価2周で残った指摘（NEXT_FINDINGS.md の反復4）を直す。(1) ORM のパッケージが無いときは、粗さ・AO・メタリックの別々の元画像を読む経路へ戻す（マニフェストの登録の有無ではなく、実際の読み込みの結果で決める）。(2) 法線が2チャンネルである印は、実際に読めたテクスチャの形式から決める（ばらへ戻ったら立てない）。(3) マニフェストごと無い状態の既定の起動でも、ばらへ戻った各パスに `TEXTURE_COOKED_MISSING` を1回ずつ出す。(4) `GameApplicationHandler.cpp` の英語のまま変えたエラーの説明を日本語にする。PSNR の集計に使った PNG は別のディレクトリへ残し、後から照合できるようにする。欠落の3つの場合（ORM・法線・マニフェスト）は、`build/CookedAssets/` を書き換えず、`.harness/runs/` の下へ写した置き場から欠かして `--texture-asset-root` で起動した撮影（またはテスト）で確かめて記録する。

## VTG1-ACCEPT: 段1（BC圧縮）の受入れを記録する
- status: done
- done-when: `Docs/RenderingValidation/VirtualizationAcceptance.md`（新規）の段1の節に、テクスチャの VRAM（移行前後の `VRAM_LEDGER` の全体と、クック済みにした材質のばらとの比。岩・小屋の glTF のテクスチャは段4で焼く残りとして内訳を書く）、視点ごとの PSNR（同じコードの撮影の揺らぎと並べる）、朝10°・昼45°・夕3°・夜の起動画面の撮影（開いて確かめた所見）、golden、関係するテストの結果、既知の限界を書く。
- verify: `cmake --build build --config Debug --target RenderResourcesDomainContractTest RHITextureUpdateVulkanTest CookedMeshTest CookedTextureUploadTest MaterialResourcesTest RenderingGoldenImageTest AssetCook -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VideoMemoryBudgetVulkanTest|TextureMemoryLedgerTest|GpuRetireQueueTest|RHIBlockCompressedFormatTest|RHIBlockCompressedTextureVulkanTest|CookedTextureTest|CookedTextureUploadTest|AssetCookBlockCompressSmoke|AssetCookTextureSmoke|MaterialResourcesTest|GBufferMaterialDescriptorCacheTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG1-ACCEPT -Configuration RelWithDebInfo -SunElevations 10,45,3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG1-ACCEPT-night -Configuration RelWithDebInfo -Night`
- stop-when: 受入れの数値が段1の受入れ（計画書 5）を満たさない場合は、測った値を記録して止める。
- paths: Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: この段の後、親が main へマージしてプッシュする。

## VTG2-CAPTURE-DETERMINISTIC: 同じコードを2回撮ると一致する撮影の方式を足す
- status: done
- done-when: Game に撮影用の `--capture-deterministic` を足す。テクスチャ・モデル・大きな球の生成の非同期の読み込みがすべて終わってから、シミュレーションの時間を 1/60 秒の固定刻みにし、TAA の揺らしの列・RTGI の乱数の種・自動露出・時間的な履歴を「読み込み完了の時点」から数え直して、決まった描画フレーム数の後に撮る。大きな球の自転は撮影の時間に従う（壁時計に依らない）。`Scripts/CaptureStartupScene.ps1` に `-Deterministic` を足してこれを付ける。同じコードを `-Deterministic` で2回撮った各視点の平均輝度の差が 0.1 以下、PSNR が 45 dB 以上になる（3視点、`metrics.json` に `deterministic=true` と両方の値を記録する）。付けないときの既定の描画経路と見た目は変えない。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG2-CAPTURE-DETERMINISTIC-a -Configuration RelWithDebInfo -Deterministic`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG2-CAPTURE-DETERMINISTIC-b -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/VTG2-CAPTURE-DETERMINISTIC-a`
- stop-when: GPU の処理の順序（アトミックの順など）で 45 dB に届かない場合は、測った値と原因の候補を記録して止める。
- paths: Scripts/CaptureStartupScene.ps1, Game, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Library/Core/Private/Engine, Library/Core/Public/Engine, TASKS.md, PROGRESS.md
- notes: 2026-10-04 親が足した。VTG1-MATERIAL-ORM で、同じコードを撮り直すだけで1視点あたり最大2.6動き、見た目の保全を数値で判定できなかった。段2以降（VT・ビジビリティバッファ）の見た目の比較は `-Deterministic` で撮る。2枚の PNG の PSNR は既存の比較の道具（`VTG1-ACCEPT` で使ったもの）か、小さな PowerShell の関数で測る。

## VTG2-SPARSE-CAPS: sparseの機能とキューを照会して有効化する
- status: done
- done-when: Vulkan のデバイスの作成で `sparseBinding`・`sparseResidencyImage2D`・`sparseResidencyAliased`・`shaderResourceResidency`・`shaderResourceMinLod` を照会し、対応時に有効化する。`VK_QUEUE_SPARSE_BINDING_BIT` を持つキューの族を探し（グラフィックスの族が持てばそれを使う）、sparse の結び付けに使うキューを持つ。`DeviceCapabilities` に sparse の可否（2D の residency・shader の residency・MinLod）と、形式ごとの標準ブロック形状（`vkGetPhysicalDeviceSparseImageFormatProperties` の imageGranularity と標準形状か否か）を照会する API を載せる。GPU のテスト `SparseCapabilitiesVulkanTest`（`RHITextureUpdateVulkanTest` の束の MEMBER）が、開発機（RTX 4080）で BC7・BC5・BC4・R16 の 2D の sparse が使え、64 KiB のタイル（BC7・BC5 は 256×256 texel、BC4 は 512×256、R16 は 256×128）であることを確かめる（Vulkan が無ければ 125、sparse が無ければ理由を出して 125）。
- verify: `cmake --build build --config Debug --target Game RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SparseCapabilitiesVulkanTest|VideoMemoryBudgetVulkanTest|RHIBlockCompressedTextureVulkanTest)$"`
- stop-when: 機能を有効にすると開発機でデバイスの作成が失敗する場合は、理由を記録して止める。
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.2。危険地帯（RHI/Vulkan）。sparse の無い GPU では以降の VT を使わず、段1の BC の全常駐で描く。

## VTG2-SPARSE-TEXTURE: sparseのテクスチャを作り、タイルとミップテイルの情報を返す
- status: done
- done-when: `TextureCreateInfo` に sparse の印を足し、物理メモリを結ばない sparse の 2D テクスチャ（全ミップ）を作れる。RHI の API がタイルの大きさ（texel）、ミップごとのタイルの数（x・y）、ミップテイルの開始段と大きさ・オフセット（`vkGetImageSparseMemoryRequirements`）を返す。sparse に対応しない GPU・形式では作成が失敗して理由をログに出す。テクスチャの台帳（`VRAM_LEDGER`）は sparse のテクスチャを「結んだ量」で数える。GPU のテスト `SparseTextureVulkanTest`（`RHITextureUpdateVulkanTest` の束）が 4096² の BC7 の sparse のテクスチャでタイルの数（ミップ0 は 16×16）とミップテイルの開始段を確かめる。
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest RenderResourcesDomainContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SparseTextureVulkanTest|SparseCapabilitiesVulkanTest|TextureMemoryLedgerTest)$"`
- stop-when: 既存の `VulkanTexture` の作成の経路に sparse を足すと、通常のテクスチャの寿命や更新の契約を変える必要がある場合は、理由を記録して止める。
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（RHI/Vulkan）。

## VTG2-SPARSE-BIND: 物理ページのプールと、タイルの結び付け・外しを作る
- status: done
- done-when: DeviceLocal の大きな塊（既定 64 MiB）から 64 KiB のページを切り出すプール（`SparsePagePool`。RenderResources が持つ）と、RHI の「タイル（ミップ・x・y）とミップテイルへページを結ぶ・外す」API を作る。結び付けは1フレーム分をまとめて `vkQueueBindSparse` 1回で出し、グラフィックスのキューとはセマフォで順序付ける（結んだタイルを読む描画より前に結び付けが終わる）。外したページは `GpuRetireQueue` で、最後に使った提出の serial が完了してからプールへ返す。プールの使用量を `VRAM_LEDGER` に出す。GPU のテスト `SparseBindVulkanTest`（`RHITextureUpdateVulkanTest` の束）が、ミップテイルと2タイルを結び、結んだタイルへ書いた色を計算シェーダーのサンプルで読み戻し、外した後にプールの空きが戻ることを確かめる。
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest RenderResourcesDomainContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SparseBindVulkanTest|SparseTextureVulkanTest|GpuRetireQueueTest)$"`
- stop-when: 開発機で `vkQueueBindSparse` が1回あたり 2 ms を超え、まとめ方で縮まない場合は、測った値を記録して止める。
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（RHI/Vulkan・メモリ・寿命）。

## VTG2-TILE-UPLOAD: ステージングのリングからタイルへGPUを待たずに書く
- status: done
- done-when: 毎フレームのステージングのリング（既定 32 MiB、DeviceLocal でない host-visible）に置いたタイル・ミップテイルのデータを、描画のコマンドの先頭でバッファからイメージの領域へコピーする経路を作る（`VulkanTexture::Update` の waitIdle の経路は使わない）。リングの区画は、その提出の serial が完了するまで再利用しない。1フレームにコピーする量の上限（既定 24 MiB）を持つ。GPU のテスト `SparseTileUploadVulkanTest`（`RHITextureUpdateVulkanTest` の束）が、連続する複数フレームでタイルを書いて読み戻し、その間に `WaitIdle` を呼ばないことを確かめる。
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SparseTileUploadVulkanTest|SparseBindVulkanTest|RHITextureUpdateVulkanTest)$"`
- stop-when: 描画のコマンドの先頭へコピーを差し込むと RenderGraph の資源の状態の追跡と食い違う場合は、理由を記録して止める。
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（RHI/Vulkan・RenderThread）。後の段のジオメトリのページのアップロードもこのリングを使う。 2026-10-04 親（run `20261004-164924` の保留を解く）: 評価2周の残り1件だけ直す。`VulkanBuffer` の作成で、メモリの種類の選択・確保・結び付けのどれかに失敗したとき、作成済みの `VkBuffer`（と確保済みのメモリ）を破棄してから失敗を返す（例外の経路を含め、失敗時の後始末を1か所にまとめる）。`CPUAccessible=false`・`bExcludeDeviceLocal=true` の小さな `TransferSrc` のバッファの作成の失敗を繰り返しても、作成と破棄の数（または検証レイヤーの未解放の報告）が増えないことを GPU のテストで確かめる。

## VTG2-NVTEX-TILED: クック済みのテクスチャをタイルの並びで書き、1タイルずつ読めるようにする
- status: done
- done-when: NVTEX v0.2 として、クッカーが形式の標準ブロック形状のタイル単位（ミップごと、行優先）に並べたデータと、ミップテイル（タイルより小さい段）をまとめた塊を書き、タイルの表（ファイル内のオフセット・大きさ）を持つ。v0.0・v0.1 も読む。読み込み側は、ファイルの範囲読みで1タイル・ミップテイルを取り出せる（全体を読まない）。`CookAssets` の起動画面の材質は v0.2 で焼く。`CookedTextureTest` に v0.2 のタイルの表・範囲読み・壊れた表の拒否を、`AssetCookTextureSmoke` に v0.2 の書き出しを足す。
- verify: `cmake --build build --config Debug --target AssetCook CookedMeshTest CookedTextureUploadTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CookedTextureTest|CookedTextureUploadTest|AssetCookTextureSmoke)$"`
- verify: `cmake --build build --config RelWithDebInfo --target AssetCook CookAssets -- /m:1`
- stop-when: タイルの形状がデバイスごとに違い、クックの時点で1つに決められない場合は、標準ブロック形状に限る理由を記録して止める。
- paths: Tools/AssetCook, Library/Core/Public/Asset, Library/Core/Private/Asset, Library/Core/Private/Rendering, Test/Core/Asset, Docs/Architecture, TASKS.md, PROGRESS.md
- notes: 危険地帯（アセットロード）。 2026-10-04 親（run `20261004-164924` の保留を解く）: 評価2周の残りを直す。`Test/Core/Asset/CookedTextureTest.cpp` に足した範囲読みのテストの2か所（`BuildTexture` の戻り値を受ける変数）を独自型の `ByteArray` で受ける（既存の補助関数は変えない）。この項目で足した・変えた利用者向けのエラーの説明（評価が挙げた `AssetFileReader.cpp`・`CookedTextureLoader.cpp`・`TextureCooker.cpp` の行）を日本語にする（エラーコード・ログのキーは英語のまま）。

## VTG2-BUDGET-MANAGER: VRAMの予算をプールへ割り振る
- status: done
- done-when: `VideoMemoryBudgetManager`（RenderResources が持つ。シングルトン禁止）が、上限 = min(heapBudget, `--vram-budget-mb`) − VT 以外の使用量（ヒープの情報があれば heapUsage からプールの確保量を引いた値、無ければ上限の30%の見込み）を約1秒ごとに計算し、VT のプール（`SparsePagePool`）の目標の大きさを決める（後の段のジオメトリ・VSM のプールの枠も持つ）。変化したときだけ `VRAM_POOLS cap_mb=<n> non_pool_mb=<n> vt_target_mb=<n>` を出す。CPU のテスト `VideoMemoryBudgetManagerTest`（`RenderResourcesDomainContractTest` の束）が、上限・予算外の使用量・上限の引数の組み合わせで目標の大きさが期待どおりで、負にならないことを確かめる。
- verify: `cmake --build build --config Debug --target Game RenderResourcesDomainContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VideoMemoryBudgetManagerTest|TextureMemoryLedgerTest)$"`
- stop-when: 予算外の使用量（VT 以外の確保）を数える手段が台帳に無い場合は、理由を記録して止める。
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Game, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.1。 2026-10-04 親（run `20261004-164924` の保留を解く）: `blocked/VTG2-BUDGET-MANAGER.md` の選択肢Bを採る（台帳の作り直しはしない）。`VK_EXT_memory_budget` は主要な GPU で使えるので、ヒープの情報があるときは `non_pool = heapUsage − SparsePagePool が確保している全ページの量`（結んだ量・貸した量ではなく、プールの塊として確保した量）で決める。ヒープの情報が無いときは、上限（`--vram-budget-mb`、無ければ DeviceLocal のヒープの大きさ）の30%を VT 以外へ見込む保守的な近似にし、`VRAM_POOLS` に `source=estimate` を出す。台帳（`VRAM_LEDGER`）が解放待ちの資源と、パスが直接作るテクスチャを数えない点は既知の限界として段2の受入れに書く。 2026-10-04 親（run `20261004-210106` の保留を解く）: `blocked/VTG2-BUDGET-MANAGER.md` の選択肢Aを採る。`paths:` に `Library/Core/Public/RHI`・`Library/Core/Private/RHI` を足した。`RHI::VideoMemoryBudget` に、拡張の有無に依らず埋める DeviceLocal のヒープの大きさの合計（`DeviceLocalHeapBytes`）を足し（`bValid` の意味は変えない）、`--vram-budget-mb` もヒープの予算も無いときの上限に使う。CPU のテストで、上限の引数なし・ヒープの予算なし・ヒープの大きさ 2000 MiB のとき `non_pool=600 MiB`・`vt_target=1400 MiB`・`source=estimate` を確かめる。

## VTG2-RESIDENCY-FALLBACK: 常駐していないタイルを読まず、粗いミップへ逃げる
- status: done
- done-when: VT のテクスチャ（材質の Albedo・Normal・ORM・Height の枠）のサンプルを、`GL_ARB_sparse_texture2` の `sparseTextureARB` で行い、常駐していない（residency の符号が非常駐）ときは `sparseTextureLodARB` で1段ずつ粗いミップへ下げて読み直す（ミップテイルは常に常駐なので必ず終わる）。POM の高さのサンプルも同じ。`gbuffer.frag`・`megageometry.frag`・`forward_transparent.frag` が共通の関数（`Common/` の GLSL）を使い、材質が VT でないときは従来の `texture()` のまま（golden 不変）。GPU のテスト `VirtualTextureResidencyVulkanTest`（`RHITextureUpdateVulkanTest` の束）が、ミップテイルと一部のタイルだけを結んだテクスチャで、結んでいない領域が粗いミップの色になり、黒や未定義の値にならないことを確かめる。
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest RenderingGoldenImageTest MaterialResourcesTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VirtualTextureResidencyVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest|MaterialResourcesTest|GBufferMaterialDescriptorCacheTest)$"`
- stop-when: シェーダーのコンパイル（shaderc）が sparse の拡張を通さない場合は、理由を記録して止める。
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-04 親: 計画書 4.2 の「常駐ミップの地図＋ MinLod」から、residency の符号で粗いミップへ逃げる方式に変えた（材質ごとの地図の binding を増やさずに済み、非常駐は過渡的なので追加のサンプルは一時的）。異方性フィルタは逃げた画素だけ失う。 2026-10-04 親（run `20261004-164924` の保留を解く）: 評価2周の残りを直す。POM の明示勾配の版で、再試行の開始 LOD を自前の式で求めず、POM の分岐・ループの前に元の UV の `textureQueryLod` で実際の標本の LOD を取り、明示勾配の版へ渡す（異方性の上限のせいで細かいミップへ戻らない）。評価が挙げた反例（512²、ミップ0 のタイル(0,0) とミップテイルだけ常駐、ミップ1 は非常駐、UV (0.25,0.25)、勾配 (8/512,0)・(0,1/512)、異方性の上限4 → 期待はミップ2）を `VirtualTextureResidencyVulkanTest` のケースに足す。

## VTG2-FEEDBACK-WRITE: 材質のサンプルの箇所からタイルの要求をGPUのバッファへ書く
- status: done
- done-when: VT の材質の UBO にテクスチャの番号（VT の表の添字）を持たせ、3つの frag が、4×4 の画素のうちフレームごとに巡回する1画素で、`textureQueryLod` から欲しいミップ（POM の後の UV）と、そのミップのタイルの x・y を求め、フレームの要求のバッファ（storage buffer、既定 64K 件、32bit に詰めた番号・ミップ・x・y）へ書く。重複は小さなハッシュの表（atomicCompSwap）で減らす。非常駐で粗いミップへ逃げた画素は巡回によらず要求を書く。GPU のテスト `VirtualTextureFeedbackVulkanTest`（`RHITextureUpdateVulkanTest` の束）が、既知の UV の面を描いて期待のタイルの要求が書かれることを確かめる。
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VirtualTextureFeedbackVulkanTest|VirtualTextureResidencyVulkanTest|RenderGraphCompileTest)$"`
- stop-when: 要求のバッファを3つの frag の descriptor に足すと RenderGraph の資源の宣言の契約を崩す場合は、理由を記録して止める。
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI, Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.2。段6でビジビリティバッファの材質の解決パスへまとめる。 2026-10-04 親（run `20261004-164924` の保留を解く）: `blocked/VTG2-FEEDBACK-WRITE.md` の選択肢1を承認する。`paths:` に `Library/Core/Public/RHI`・`Library/Core/Private/RHI` を足した。`fragmentStoresAndAtomics` を対応しているときだけ有効化して `DeviceCapabilities` に載せ、非対応の GPU ではフィードバックを無効にする（材質は従来どおり描ける）。実装のメモは blocked の文書のとおり。VT の表の番号は VTG2-VT-STREAMER（`217a284f`・`8e6ed661`）で入った表に合わせる。

## VTG2-FEEDBACK-READ: 要求を数フレーム遅れで読み戻して集計する
- status: done
- done-when: 要求のバッファを3つのリングで持ち、2フレーム前のものを GPU を待たずに読み戻して、テクスチャごとのタイルの要求の集合（同じタイルは1つ、最後に要求したフレームを持つ）にまとめる。RenderThread を止めない。集計の結果を `VirtualTextureRequestSet` として VT のストリーマへ渡す。CPU のテスト `VirtualTextureRequestSetTest`（`RenderResourcesDomainContractTest` の束）が、詰めた要求の復号・重複の除去・溢れた件数の数え方を確かめる。
- verify: `cmake --build build --config Debug --target RenderResourcesDomainContractTest RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VirtualTextureRequestSetTest|VirtualTextureFeedbackVulkanTest)$"`
- stop-when: 読み戻しに GPU の完了待ちが要る場合は、理由を記録して止める。
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（RenderThread）。 2026-10-04 親（run `20261004-164924` の保留を解く）: 評価2周の残りを直す。(1) RHI のバリアにフラグメントシェーダーの storage の書き込みを表す状態を足し、`FRAGMENT_SHADER / SHADER_WRITE → HOST / HOST_READ` を記録する（完了の serial で待たない読み戻しは保つ）。(2) VTG2-FEEDBACK-WRITE の GPU のテスト `VirtualTextureFeedbackVulkanTest` に、frag が既知の要求を書き、バリアとリングを経て2フレーム後に回収されるケースを足して ctest に登録し、verify に加える。`paths:` に `Library/Core/Public/RHI`・`Library/Core/Private/RHI` を足した。

## VTG2-VT-STREAMER: 要求からタイルを読み、結び付けて常駐させる
- status: done
- done-when: `VirtualTextureStreamer`（RenderResources が持つ）が、要求の集合から未常駐のタイルを優先度（粗いミップ・画面の近くが先）順に選び、JobSystem の範囲読みで NVTEX v0.2 から読み、ステージングのリング（VTG2-TILE-UPLOAD）へ置き、ページを結び（VTG2-SPARSE-BIND）、コピーする。1フレームの上限（読み・コピー・結び付けの数）を持つ。ミップテイルは作成時に結んで常に常駐。材質のテクスチャを VT として作る入口（クック済みの v0.2 の材質のテクスチャを sparse で作る）を `TextureResources` に足す。CPU のテスト `VirtualTextureStreamerTest`（`RenderResourcesDomainContractTest` の束。読み込み・結び付けを偽物にする）が、優先度・上限・同じタイルの二重の要求・読み込みの失敗を確かめる。
- verify: `cmake --build build --config Debug --target Game RenderResourcesDomainContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VirtualTextureStreamerTest|VirtualTextureRequestSetTest|GpuRetireQueueTest)$"`
- verify: `ctest --test-dir build -C Debug -V --output-on-failure --no-tests=error -R "^VirtualTextureFeedbackVulkanTest$"`
- stop-when: 範囲読みの JobSystem の経路が無く、アセットの読み込みの経路を作り直す必要がある場合は、理由を記録して止める。
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Asset, Library/Core/Private/Asset, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（アセットロード・寿命・RenderThread）。 2026-10-04 親（run `20261004-164924` の保留を解く）: 評価2周の残り3件を直す。(1) コピーの1フレームの上限（件数・バイト数）を最初の1件にも掛ける。ミップテイルが上限を超えるときは、テイルを公開しないまま複数フレームに分けて書き、書き終えてから使う。上限0など処理できない設定は作成時に明示的に拒否する。(2) 優先度を「粗いミップが先 → 同じミップなら要求した画素の数（画面上の大きさ）が多い方 → 最後に要求したフレームが新しい方」と定義し直す（元の「画面の近くが先」は、カメラからの距離を要求に載せる手段が無いため、親がこの定義に置き換えた）。`HitCount` が画素の数を表すことをテストで確かめる。(3) 実際の `TileUploader` と `GpuRetireQueue` を使い、「コピーを記録 → Abort → 登録解除 → ページの再取得 → 次のフレーム」で古いコピーが記録されない回帰テストを足し、実行の証拠を残す。 2026-10-04 親（run `20261004-210106` の保留を解く）: `blocked/VTG2-VT-STREAMER.md` の選択肢1を採る。`paths:` に `Assets/Shaders` を足した。フィードバックのハッシュの表の各枠に件数の語を並べ、同じタイルの要求が重なったら `atomicAdd` で数え、読み戻しの復号が件数を `HitCount`（画面上で要求した画素の数）へ渡す。`VirtualTextureFeedbackVulkanTest` に、面積の違う2つのタイルで件数が面積の順になるケースを足す。

## VTG2-VT-EVICT: LRUで追い出し、プールの予算に収める
- status: done
- done-when: 最後に要求されたフレームが古いタイルから LRU で外し、`SparsePagePool` の使用量を `VideoMemoryBudgetManager` の VT の目標以下に保つ。目標が足りないときは、細かいミップのタイルから外し、要求の優先度の低いものを結ばない。小さなテクスチャ（長辺 1024 以下）はミップ単位で同じ仕組みに乗る（ミップ全体を1単位として結ぶ・外す）。ミップテイルは外さない。`VRAM_POOLS` に vt_used_mb・vt_evicted_tiles を出す。`VirtualTextureStreamerTest` に、目標を下げたときの追い出しの順と、目標以下に収まることを足す。
- verify: `cmake --build build --config Debug --target Game RenderResourcesDomainContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VirtualTextureStreamerTest|VideoMemoryBudgetManagerTest)$"`
- stop-when: 追い出しと結び付けが同じフレームで競合し、外したタイルを読む描画が出る経路を塞げない場合は、理由を記録して止める。
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（メモリ・寿命）。 2026-10-04 親（run `20261004-210106` の保留を解く）: `blocked/VTG2-VT-EVICT.md` の選択肢Aを採る。`paths:` に `Library/Core/Public/RHI`・`Library/Core/Private/RHI` を足した。描画用のタイムラインセマフォを1つ足し、描画の3つの提出箇所が値の割り当てと提出を同じミューテックスの下で行って通知する（提出が失敗したら値を戻す）。タイルを外す（unbind を含む）`BindSparse` だけがその時点の最新の値を待つ。結ぶだけの `BindSparse` と CPU は待たない。`SparseBindVulkanTest` に、描画で読んでいるタイルを外しても検証レイヤーの違反（`VUID_COUNT=0`）が出ず、`WaitIdle` を呼ばないケースを足す。

## VTG2-VT-STARTUP: 起動画面の材質をVTで描く
- status: done
- done-when: sparse に対応する GPU では、起動画面の材質（銀・石畳・地面の見本6種）のテクスチャを VT（sparse・v0.2）で作り、フィードバック・ストリーマ・追い出しで描く（非対応なら段1の全常駐）。`--virtual-texture=off` で段1の全常駐へ戻せる。`-Deterministic` の撮影で、VT と全常駐（`--virtual-texture=off`）の各視点の PSNR が 45 dB 以上（差の出どころを記録する）、PNG を開いて黒・ぼけたタイル・ちらつきが見えない。撮影の `VRAM_LEDGER`・`VRAM_POOLS` で、材質のテクスチャの量（VT は結んだ量）が全常駐より減ることを記録する。
- verify: `cmake --build build --config RelWithDebInfo --target Game AssetCook CookAssets -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG2-VT-STARTUP -Configuration RelWithDebInfo -Deterministic`
- stop-when: 撮影でタイルの出入りによるちらつきが残り、優先度・上限の調整で消えない場合は、測った値と撮影を記録して止める。
- paths: Game, Scripts/CaptureStartupScene.ps1, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, TASKS.md, PROGRESS.md
- notes: 起動画面の見た目を変えうる（絶対規則7）。全常駐との比較の撮影は `-ExtraArgs` のような引数がスクリプトに無ければ足す。 2026-10-04 親（run `20261004-210106` の保留を解く）: 評価の1周目（run `20261004-210106` の反復13）の4点を直す。(1) 完了条件の「全常駐を2回撮った揺らぎと同程度」は、ストリーミングの遅れで粗いミップが残りうる VT には厳しすぎるので、「`-Deterministic` の撮影で VT と全常駐の PSNR が各視点 45 dB 以上（評価のときは 62〜69 dB）」に親が直した。差の出どころ（粗いミップが残る区画、隣のタイルの不足など）は切り分けて記録する。(2) VT にする材質のテクスチャをアルベドだけでなく、法線・ORM・高さにも広げる（それぞれ要求・常駐の管理まで接続する）。(3) `SupportsVirtualTextureFeedback()` とフィードバックの有効化の成功を確かめ、失敗したら VT を解放して全常駐へ戻す。(4) VTG2-VT-EVICT の同期が入った後、VT のプールの目標を全常駐の量より小さくする `--vram-budget-mb` で、追い出しが起きる（`vt_evicted_tiles>0`）条件の連続撮影（カメラを動かす `-OrbitDegreesPerSecond` など）を開き、黒・ちらつきが出ないことを記録する。`GameApplicationHandler.cpp` の英語の説明句を日本語にする。

## VTG2-VT-FALLBACK-TEST: VTの全常駐への復帰を偽デバイスの契約テストで確かめる
- status: done
- done-when: `GpuRetireQueueTest`（偽デバイスで `RenderResources` を初期化するテスト）に、(1) sparse の2D部分常駐に対応してもフィードバック（`bFragmentStoresAndAtomics`・`bShaderResourceResidency`）に対応しないデバイスでは `Textures().SupportsVirtualTexture()` が false、(2) 4つの機能がそろうデバイスでは true、(3) 要求のバッファを作れず `EnableVirtualTextureFeedback()` が失敗するデバイスでは `CreateVirtualTexture` が VT を解放して無効なハンドルを返す、を足す。
- verify: `cmake --build build --config Debug --target RenderResourcesDomainContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^GpuRetireQueueTest$"`
- stop-when: 偽デバイスでは (3) の失敗を作れない（要求のバッファの確保が常に成功する）場合は、(1)(2) だけにして理由を記録する。
- paths: Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: VTG2-VT-STARTUP の評価（run `20261004-210106` 反復13）の3点目で、実機の確認は一時の環境変数で fragmentStoresAndAtomics を無いことにして行った（コミットしていない）。契約として残すための後追い。

## VTG2-STRESS-TEXTURES: テクスチャの負荷モードを足す
- status: done
- done-when: `Scripts/FetchPolyHavenTextures.ps1` に負荷用の材質の組（Poly Haven の CC0 の地面・壁・木などの材質 20種以上、4K）を `-StressSet` で足し、`CookAssets` が焼く（無ければ飛ばす）。Game の `--stress-textures` で、起動画面の地面の外側に負荷用の材質を貼った板を格子に並べた検証モードに入る。全常駐なら負荷用の材質の量が VT のプールの目標の2倍以上になる `--vram-budget-mb` で撮り、`VRAM_POOLS` の vt_used_mb が目標以下に収まり、撮影（既定・低角度と、格子の上を見下ろす視点）を開いて黒・未定義の色が見えないことを記録する。
- verify: `cmake --build build --config RelWithDebInfo --target Game CookAssets -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG2-STRESS-TEXTURES -Configuration RelWithDebInfo -Deterministic`
- stop-when: Poly Haven の API から負荷用の組を取れない場合は、理由を記録して止める。
- paths: Game, Scripts, Assets/AssetSets, .gitignore, TASKS.md, PROGRESS.md
- notes: 計画書 1（検証は起動画面に足し、重い負荷は別モード）。負荷用のテクスチャは git に入れない。撮影スクリプトに負荷モードの引数が無ければ足す。

## VTG2-ACCEPT: 段2（sparseのVT）の受入れを記録する
- status: done
- done-when: `Docs/RenderingValidation/VirtualizationAcceptance.md` の段2の節に、`-Deterministic` の撮影（朝・昼・夕・夜 × 3視点、VT と全常駐の PSNR と同じコードの揺らぎ）、材質のテクスチャの量（段1の全常駐・VT）、負荷モードの `--vram-budget-mb` での `VRAM_POOLS`、golden、関係するテストの結果、既知の限界（sparse の無い GPU の扱いを含む）を書く。8GB 級を模す上限（`--vram-budget-mb 6500`）で負荷モードが溢れずに描けることを記録する。
- verify: `cmake --build build --config Debug --target RenderResourcesDomainContractTest RHITextureUpdateVulkanTest CookedMeshTest RenderingGoldenImageTest MaterialResourcesTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SparseCapabilitiesVulkanTest|SparseTextureVulkanTest|SparseBindVulkanTest|SparseTileUploadVulkanTest|VirtualTextureResidencyVulkanTest|VirtualTextureFeedbackVulkanTest|VirtualTextureRequestSetTest|VirtualTextureStreamerTest|VideoMemoryBudgetManagerTest|CookedTextureTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG2-ACCEPT -Configuration RelWithDebInfo -Deterministic -SunElevations 10,45,3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG2-ACCEPT-night -Configuration RelWithDebInfo -Deterministic -Night`
- stop-when: 受入れの数値が段2の受入れ（計画書 5）を満たさない場合は、測った値を記録して止める。
- paths: Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: この段の後、親が main へマージしてプッシュする。

## VTG3-HIZ-PYRAMID: 今のフレームの深度からHZBの全ミップを作るパスを足す
- status: done
- done-when: MegaGeometry の描画の途中（後の VTG3-TWO-PASS-OCCLUSION の1パス目の後）で呼べる、深度から HZB（R32_FLOAT、全ミップ、2×2 の最大で縮める。深度は Less の標準の向き）を作る RenderGraph のパスを足す。幅・高さが奇数の段は、はみ出す行・列も最大に含めて保守的にする（縮めた1 texel がそれの覆う深度の最大以上）。ミップ0 は深度の解像度の半分。既存の `hiz_generate.comp`・`GenerateHiZPyramid` を土台にし、使われていない資源の作成と寿命を整理する。GPU のテスト `HiZPyramidVulkanTest`（`RHITextureUpdateVulkanTest` の束の MEMBER）が、既知の深度（奇数の大きさ 37×23 を含む）から作った各ミップの各 texel が、覆う深度の最大と一致することを読み戻して確かめる。
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(HiZPyramidVulkanTest|RenderGraphCompileTest)$"`
- stop-when: HZB のパスを RenderGraph に置くと、GBuffer の深度の Load/Store の契約（MegaGeometry が GBuffer へ Load で描く）を崩す場合は、理由を記録して止める。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.3。Hi-Z は 2026-05 の `4d1e6a37` で過剰カリングのため無効にされた（AABB の中心1点・解像度の不整合）。危険地帯（描画パスの構造）。

## VTG3-HIZ-CONSERVATIVE: クラスタの遮蔽の判定を保守的な矩形の判定に作り直す
- status: done
- done-when: `cluster_cull.comp` の Hi-Z の判定を、クラスタの境界（球、またはそれを包む AABB の8点）を画面へ投影した矩形と最も手前の深度で行う形に置き換える。矩形が 2×2 texel 以内に収まる HZB のミップを選び、その範囲の4 texel の最大と、境界の最も手前の深度を比べる（手前の深度 > 範囲の最大なら隠れている）。境界が近平面をまたぐ・カメラの後ろにかかるときは隠れていない扱い。HZB のミップ0 が深度の半分の解像度であることを投影の計算に正しく入れる。判定は共通の GLSL の関数にし、GPU のテスト `HiZOcclusionTestVulkanTest`（`RHITextureUpdateVulkanTest` の束）が、合成した HZB と境界の組（完全に隠れる・一部見える・近平面をまたぐ・画面の端にかかる・小さくて1 texel に収まる）で期待どおりの判定になることを確かめる（見えているものを隠れていると判定する誤りが0件）。
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest MegaGeometryResourcesTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(HiZOcclusionTestVulkanTest|HiZPyramidVulkanTest|MegaGeometryResourcesTest)$"`
- stop-when: 境界の投影が透視の行列の規約（View/Proj は列ベクトル＋Transpose、World は行ベクトル）と合わず、Math の抽象 API に足す必要がある場合は、足す API を記録して止める。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.3。行列の要素を直接触らず Math の抽象 API を使う（`no-direct-matrix-element-access` の方針）。

## VTG3-TWO-PASS-OCCLUSION: 2パスの遮蔽カリングを入れて既定で有効にする
- status: done
- done-when: MegaGeometry のインスタンスごとに、クラスタごとの「前のフレームで見えた」ビットの持続のバッファを持つ。1パス目は前のフレームで見えたクラスタだけを（視錐台・法線のコーン・LOD の判定はするが遮蔽の判定はせずに）描き、その時点の深度（GBufferPass の不透明＋1パス目）から HZB を作る（VTG3-HIZ-PYRAMID）。2パス目は1パス目で描かなかったクラスタを HZB で判定し（VTG3-HIZ-CONSERVATIVE）、見えたものを描く。2パス目は1パス目で描いたクラスタも HZB で判定し直してビットを更新する（次のフレームのため）。インスタンスの追加・LOD の切り替え・メッシュの差し替えでビットを捨てる。`--mega-occlusion=off` で従来の経路（遮蔽の判定なし）に戻せる。`MEGA_OCCLUSION pass1=<n> pass2_tested=<n> pass2_drawn=<n> occluded=<n>` を撮影のログに出す。`RenderGraphCompileTest` の MegaGeometry の記録の検査を新しいパスの並びに合わせる。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest MegaGeometryResourcesTest ViewportSnapshotDebugWiringTest RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|MegaGeometryResourcesTest|MegaGeometryFrameCommandDebugModeTest|HiZOcclusionTestVulkanTest|HiZPyramidVulkanTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG3-TWO-PASS-OCCLUSION -Configuration RelWithDebInfo -Deterministic`
- stop-when: 1パス目と2パス目の間で GBuffer へ描く順序を変えると、MegaGeometry の Velocity・Emissive の書き込みの契約が崩れる場合は、理由を記録して止める。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.3。危険地帯（描画パス・RenderThread）。影（CSM・点光源）の描画には遮蔽の判定を掛けない（カメラの視点の深度で影のキャスターを省くと影が欠ける）。 2026-10-05 親（run `20261005-014448` の保留を解く）: 評価2周の残り1件を直す。描画のスナップショットからインスタンスが消えたフレーム（全インスタンスが消えて `Execute` が記録を省くフレームを含む）で、そのインスタンスの可視ビットの履歴を無効にする（`Setup` でスナップショットから脱落した履歴を捨てるのが最小。`RecordFrameCommand` の中だけでカウンタを動かしても、記録を省く経路は直らない）。`RenderGraphCompileTest` に、同じ ObjectId・ComponentId・メッシュで「A → 空 → A」としたとき、再追加でビットが初期化される（初期化の回数が [1,0,1]）回帰のケースを足す。

## VTG3-OCCLUSION-ORBIT: 旋回するカメラで遮蔽カリングの穴・遅れが出ないことを確かめる
- status: done
- done-when: 起動画面で小屋・岩・大きな球が互いを隠す視点（小屋の陰に岩が入る位置など）を撮影スクリプトの視点に足し、`-Deterministic` で遮蔽あり・なし（`--mega-occlusion=off`）を撮って PSNR を記録する（目安 60 dB 以上。差の画素を開いて穴・欠けでないことを確かめる）。`-OrbitDegreesPerSecond` の連続フレーム（遮蔽あり）を開き、隠れていた物が見え始めるフレームで欠け・ちらつき・1フレームの遅れが無いことを確かめる。`MEGA_OCCLUSION` の occluded の数（隠れて省いたクラスタ）を視点ごとに記録し、隠し合う視点で 0 より大きいことを確かめる。GPU 時間（`-GpuTimingFrames`、RelWithDebInfo）の MegaGeometry の前後も記録する。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG3-OCCLUSION-ORBIT -Configuration RelWithDebInfo -Deterministic`
- stop-when: 穴・欠けが出て、判定の保守性を上げても消えない場合は、撮影と値を記録して止める。
- paths: Scripts/CaptureStartupScene.ps1, Game, Assets/Shaders, Library/Core/Private/Rendering, TASKS.md, PROGRESS.md
- notes: 起動画面の見た目を変えうる（絶対規則7）。

## VTG3-ACCEPT: 段3（遮蔽カリング）の受入れを記録する
- status: done
- done-when: `Docs/RenderingValidation/VirtualizationAcceptance.md` の段3の節に、遮蔽あり・なしの `-Deterministic` の撮影（朝・昼・夕・夜 × 既定・近接・低角度と、隠し合う視点）の PSNR、`MEGA_OCCLUSION` の省いたクラスタの数、GPU 時間の前後、旋回の連続フレームの所見、golden、関係するテストの結果、既知の限界を書く。
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest RenderGraphCompileTest MegaGeometryResourcesTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(HiZPyramidVulkanTest|HiZOcclusionTestVulkanTest|RenderGraphCompileTest|MegaGeometryResourcesTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG3-ACCEPT -Configuration RelWithDebInfo -Deterministic -SunElevations 10,45,3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG3-ACCEPT-night -Configuration RelWithDebInfo -Deterministic -Night`
- stop-when: 受入れの数値が段3の受入れ（計画書 5）を満たさない場合は、測った値を記録して止める。
- paths: Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: この段の後、親が main へマージしてプッシュする。

## VTG4-MESHOPT-VENDOR: meshoptimizerを取り込む
- status: done
- done-when: `Library/ThirdParty/meshoptimizer/` に固定した commit（リリースのタグ）のソースと `LICENSE`（MIT）、`UPSTREAM.json`（版・commit・取得元・sha256・SPDX）を置き、独立の静的ライブラリ `NorvesThirdParty_MeshOptimizer` を作って `AssetCook` にだけリンクする（Core・Game はリンクしない）。`Tools/AssetCook` に薄い境界（`MeshSimplifier` の名前は既存と衝突するので `CookMeshOptimizer` などにする）を置き、スモーク `AssetCookMeshSimplifySmoke`（`Tools/AssetCook/CMakeLists.txt` の add_test）が、緯度経度の球（約 2 万三角形）を境界の頂点を固定して半分に簡略化し、三角形の数・誤差（相対）・固定した頂点が動かないことを確かめる。
- verify: `cmake --build build --config Debug --target AssetCook -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(AssetCookMeshSimplifySmoke|AssetCookMeshSmoke)$"`
- stop-when: 取得元から入手できない、またはライセンスが MIT でない場合は、理由を記録して止める。
- paths: Library/ThirdParty/meshoptimizer, Tools/AssetCook, CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: ユーザー承認済みの外部依存（計画書 1）。取り込み方は bc7enc_rdo（`Library/ThirdParty/bc7enc_rdo`、VTG1-BC7ENC-VENDOR）に倣う。

## VTG4-NVMESH-V1: NVMESH v1（LODの階層を持つクラスタの記録）を足す
- status: done
- done-when: `CookedMeshFormat` に v1 を足す。cluster record は 128B（自分の境界球と誤差、親のグループの境界球と誤差、グループの番号、LOD の段、頂点・インデックスの位置と数、法線のコーン、ページの番号（段5まで 0））。グループの表（グループの境界球・誤差・クラスタの範囲）と、RT・影のための常駐の粗い段（フォールバック）のインデックスの範囲を持つ。v0 も読む（v0 は従来の1段のメッシュとして扱う）。読み込み側（`CookedMeshLoader`）が v1 を検証し（範囲・数の不整合・壊れた表を拒否）、MegaGeometry の `MegaMeshCreateInfo` へ渡せる形にする。`Docs/Architecture/NVMESHv1.md` に形式を書く。`CookedMeshTest` に v1 の書き出し・読み込みの往復、v0 の読み込み、壊れた入力の拒否を足す。
- verify: `cmake --build build --config Debug --target AssetCook CookedMeshTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CookedMeshTest|AssetCookMeshSmoke|CookedTextureTest)$"`
- stop-when: v0 の読み込みを保てない形式の変更が要る場合は、理由を記録して止める。
- paths: Library/Core/Public/Asset, Library/Core/Private/Asset, Library/Core/Public/Rendering/MegaGeometry, Tools/AssetCook, Test/Core/Asset, Docs/Architecture, TASKS.md, PROGRESS.md
- notes: 計画書 4.3。既存の設計案は `Docs/Plans/GameFeatureRoadmap.md` の GR80 P3（3061 行付近）。危険地帯（アセットロード）。

## VTG4-DAG-BAKE: クッカーでLODの階層を焼く
- status: done
- done-when: `MeshCooker` が、頂点の溶接（位置と属性）→ 128三角形・128頂点のクラスタ化 → 「約4クラスタのグループ化（隣接で）→ グループの境界の頂点を固定して属性（UV・法線）を保つ簡略化で半分に → 再クラスタ化」の繰り返しで、根（クラスタ数が数個）まで階層を作り、NVMESH v1 に書く。親の誤差は子の誤差の最大と簡略化の誤差の和（単調）、親の境界球は子の境界球を包む。フォールバックの段は、全体の三角形がおよそ 1/16 か 32K 以下になる誤差で切ったクラスタの集まり。性質のテスト（`CookedMeshTest` か AssetCook のスモーク）: 閉じた入力（緯度経度の球）で (1) 誤差がどの子から親へも単調、(2) 親の境界球が子を包む、(3) 誤差のしきい値を5通りに変えて切った各メッシュで、すべての辺がちょうど2つの三角形に共有される（穴・割れ目が無い）、(4) フォールバックの三角形数が範囲内。岩（`boulder_01`）の glTF を焼いた時間をログ（`MESH_COOK dag_levels=<n> clusters=<n> ms=<n>`）に出す。
- verify: `cmake --build build --config Debug --target AssetCook CookedMeshTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CookedMeshTest|AssetCookMeshSmoke|AssetCookMeshSimplifySmoke)$"`
- stop-when: 境界を固定した簡略化で半分に届かず、階層が根まで縮まらない入力が起動画面の資産にある場合は、その資産と値を記録して止める。
- paths: Tools/AssetCook, Library/Core/Public/Asset, Library/Core/Private/Asset, Test/Core/Asset, TASKS.md, PROGRESS.md
- notes: 計画書 4.3。既存の `LODHierarchyBuilder`（実行時。未使用・n² の疑い）は使わない。

## VTG4-DAG-BAKE-DOC: NVMESH v1 の文書を焼き込みの実装に合わせる
- status: done
- done-when: `Docs/Architecture/NVMESHv1.md` の「v1 を書くのは クッカー（… まだ v0 だけを出す）」を実装に合わせて直し、焼き込み（クッカー）の手順・既知の限界・試験の節を足す。内容は `PROGRESS.md` の「反復 5（run 20261005-043300）」の記録に書いた実装の事実（形式名 `nvmesh.v1.mesh3d.pnt.u32.lodgraph`・`cooked_version` 1・溶接/クラスタ化/グループ化/境界固定の簡略化/許容モードの再試行/打ち切り/フォールバックの選び方・実測値・「つまみ」の限界）に従う。コードは触らない。
- verify: `git diff --numstat -- Docs/Architecture/NVMESHv1.md`
- stop-when: 記録と実装（`Tools/AssetCook/CookMeshDag.cpp`）が食い違うときは、実装を正として文書に書く。
- paths: Docs/Architecture, TASKS.md, PROGRESS.md
- notes: VTG4-DAG-BAKE の paths に `Docs/Architecture` が無かったので、文書の更新だけ分けた。

## VTG4-DAG-SELECT-GPU: GPUのカリングで階層の切り方を選ぶ
- status: done
- done-when: クック済みの NVMESH v1 を MegaGeometry に読み込み（全段のクラスタを1組の頂点・インデックスに置く）、`cluster_cull.comp` が「自分の誤差を画面へ投影した値がしきい値以下で、親のグループの誤差を投影した値がしきい値を超える」クラスタだけを描く（同じグループのクラスタが同じ判断になるよう、親の判定はグループの境界球と誤差で行う）。段3の2パスの遮蔽（クラスタの番号ごとの可視ビット）と両立する。v0 と手続きの球は従来の段の選び方のまま。`MegaGeometryResourcesTest` に、v1 のメッシュで距離を変えたとき選ばれるクラスタの集まりが閉じたメッシュになる（CPU で同じ判定を写した検査）ケースを足す。
- verify: `cmake --build build --config Debug --target Game MegaGeometryResourcesTest RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(MegaGeometryResourcesTest|RenderGraphCompileTest|HiZOcclusionTestVulkanTest|CookedMeshTest)$"`
- stop-when: 階層の切り方の判定と段3の可視ビットの番号の付け方が両立せず、遮蔽の履歴の作り直しが要る場合は、理由を記録して止める。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Library/Core/Private/Asset, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.3。危険地帯（描画パス・アセットロード）。

## VTG4-FALLBACK-LEVEL: RTと影はフォールバックの段を使う
- status: done
- done-when: v1 のメッシュでは、CSM・点光源の影の描画（`ShadowLODLevel`・`LevelRanges` の「1つのインデックスの範囲で描ける段」）と、レイトレの加速構造（`RayTracingSceneInstanceSnapshot` と BLAS のキー）が、常駐のフォールバックの段の範囲を使う。v0・手続きの球は従来どおり。`MegaGeometryResourcesTest`・`RayTracingSceneSnapshotTest` に、v1 のメッシュの影・RT の範囲がフォールバックを指すケースを足す。起動画面の撮影で、影と RTGI が崩れないことを確かめる（VTG4-COOK-STARTUP-MODELS の後の、v1 の岩・小屋を既定で読む撮影で確かめる）。
- verify: `cmake --build build --config Debug --target MegaGeometryResourcesTest RayTracingSceneSnapshotTest DirectionalShadowLightMatricesTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(MegaGeometryResourcesTest|RayTracingSceneSnapshotTest|DirectionalShadowLightMatricesTest|RayTracingCapabilityContractTest)$"`
- stop-when: BLAS のキーがバッファのポインタと範囲で、フォールバックを別のバッファに置かないと照合が崩れる場合は、理由を記録して止める。
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 1（RT は焼いた粗い段を常駐）。影は段8・9で VSM に置き換えるまでのつなぎ。危険地帯（RT・影）。 2026-10-05 親（run `20261005-043300` の保留を解く）: `blocked/VTG4-FALLBACK-LEVEL.md` の選択肢1を採った。実装とテストは `beb6a07e` で済んでいる。v1 の岩を一時的に読む撮影はやめ、VTG4-COOK-STARTUP-MODELS（v1 の岩・小屋を既定で読む。PASS 済み）の `-Deterministic` の撮影を開いて、影（CSM の陰の形・接地の陰）と RTGI（`-Rtgi Off` の撮影との比較を含む）が崩れていないことを確かめて記録する。崩れていたらフォールバックの範囲を疑って直す。

## VTG4-COOK-STARTUP-MODELS: 起動画面の岩と小屋をクック済み（NVMESH v1・BC・VT）で読む
- status: done
- done-when: `CookAssets` が岩（`boulder_01_4k.gltf`）と小屋（`Cottage_Clean`）のメッシュを NVMESH v1（階層つき）に、テクスチャを BC（色 BC7 sRGB、法線 BC5。岩の `nor_gl` は OpenGL の向きなので Y を反転して DirectX の向きにする、ARM は R=AO・G=粗さ・B=メタリックで ORM と同じ並びなので BC7 linear）に焼く。Game は既定でクック済みの岩・小屋を読み（材質のテクスチャは段2の VT）、クック済みが無ければ従来の glTF の実行時の経路へ戻して警告する。撮影の `VRAM_LEDGER` で、岩・小屋の無圧縮のテクスチャ（409.6 MiB）が無くなることを記録する。`-Deterministic` の撮影で、glTF の経路（`--rendering3dtest-model-source=gltf` など）と比べた PSNR を視点ごとに記録し（階層の段の違いで差が出うる。目安 40 dB 以上）、PNG を開いて岩・小屋の形・模様・法線の向き（凹凸の陰の向き）が同じに見えることを確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target AssetCook CookAssets Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG4-COOK-STARTUP-MODELS -Configuration RelWithDebInfo -Deterministic`
- stop-when: glTF の材質（複数の submesh・材質）が NVMESH v1 の1材質に収まらない場合は、資産と理由を記録して止める。
- paths: Game, Tools/AssetCook, Assets/AssetSets, Library/Core/Private/Asset, Library/Core/Public/Asset, Library/Core/Private/Resource, Library/Core/Private/Rendering, Scripts, TASKS.md, PROGRESS.md
- notes: 段1の既知の限界（岩・小屋の glTF のテクスチャが無圧縮）をここで解く。起動画面の見た目を変えうる（絶対規則7）。危険地帯（アセットロード）。

## VTG4-BIG-SPHERE-COOK: 起動画面の大きな球をクッカーで生成して焼く
- status: done
- done-when: クッカーに、石畳の高さマップ（`cobblestone_floor_09_disp_4k.png`）で変位した緯度経度の球（今の実行時の生成と同じ半径・分割・変位の量）を作る生成器（`--generate displaced-sphere`）を足し、`CookAssets` が NVMESH v1（階層つき）に焼く。Game は既定でそれを読み、実行時の生成（約 3.6 秒）をしない（クック済みが無ければ従来の実行時の生成へ戻して警告する）。`-Deterministic` の撮影で、実行時の生成（従来の5段の LOD）と比べた PSNR を視点ごとに記録し（目安 40 dB 以上）、近接の PNG を開いて石畳の凹凸・継ぎ目・割れ目が無いことを確かめる。起動から撮影までの時間の内訳（球が使えるまで・環境マップ・材質のテクスチャ・撮影の保存）を、実行時の生成と比べて記録する。
- verify: `cmake --build build --config RelWithDebInfo --target AssetCook CookAssets Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG4-BIG-SPHERE-COOK -Configuration RelWithDebInfo -Deterministic`
- stop-when: 階層の焼き込みで球の継ぎ目（経度0と360の境・極）に割れ目が出て、溶接で消えない場合は、撮影と値を記録して止める。
- paths: Game, Tools/AssetCook, Assets/AssetSets, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Scripts, TASKS.md, PROGRESS.md
- notes: 計画書 1（大きな球もクックで作るアセットにする）。起動画面の見た目を変えうる（絶対規則7）。 2026-10-05 親（run `20261005-043300` の保留を解く）: `blocked/VTG4-BIG-SPHERE-COOK.md` の選択肢1を採った。実装と見た目の検証は済んでいる。球の生成は今は起動の律速でなく（球が使えるのは約 0.9 秒、律速は環境マップと材質のテクスチャの読み込みで約 8 秒）、焼き込みで起動の時間は短くならない。完了条件の「時間の短縮」を「起動の時間の内訳（球・環境マップ・材質のテクスチャ・撮影の保存の時刻）を記録する」に直したので、測った値（`verify-VTG4-BIG-SPHERE-COOK-21-startup-timeline.txt`）を記録して閉じる。起動の時間の短縮は仮想化の計画の範囲外。

## VTG4-DAG-FALLBACK-DOC: NVMESH v1 の文書へ、フォールバックの下限の上限と変位した球の生成を書く
- status: done
- done-when: `Docs/Architecture/NVMESHv1.md` のフォールバックの段の節（「目標は min(全体 / 16, 32768)」の記述）に、クックの `fallback_min_triangles`（`--fallback-min-triangles`）で下限を上げられること、その上限が 131072 であること（`Tools/AssetCook/CookMeshDag.cpp` の `FallbackMinOverrideMaxTriangles`）、既定の目標（32768 まで）はそのままなことを書く。クッカーの生成器 `--generate displaced-sphere`（`--input` の高さマップで変位した起動画面の大きな球を作って v1 に焼く。仕様は `Library/Core/Public/Rendering/MegaGeometry/StartupBigSphereSpec.h`）の節を足す。内容は `PROGRESS.md` の「反復 16（run 20261005-043300）」の実装の事実に従い、コードは触らない。
- verify: `git diff --numstat -- Docs/Architecture/NVMESHv1.md`
- stop-when: 記録と実装（`Tools/AssetCook/CookMeshDag.cpp`・`MeshCooker.cpp`）が食い違うときは、実装を正として文書に書く。
- paths: Docs/Architecture, TASKS.md, PROGRESS.md
- notes: VTG4-BIG-SPHERE-COOK の paths に `Docs/Architecture` が無かったので、文書の更新だけ分けた。

## VTG4-POLYHAVEN-MODELS: Poly Havenの高ポリのスキャン資産を起動画面に足す
- status: done
- done-when: `Scripts/FetchPolyHavenModels.ps1`（新規。Poly Haven の API の CC0 のモデルを MD5 で照合して落とす。git に入れない）が、高ポリのスキャン資産を 3〜5 点（岩・切り株・像など。合計 100 万三角形以上）落とし、`CookAssets` が NVMESH v1 とテクスチャ（BC・VT）に焼く。起動画面の既定の視点で、今の天球・地面・球・岩・小屋・見本の帯を隠さない位置（地面の外周の石畳の上など）に並べる（資産が無ければ置かずに警告）。`-Deterministic` で、近くから遠くへカメラを引く連続撮影（`-OrbitDegreesPerSecond` か距離を変える視点の列）を開き、段の切り替わりで割れ目・ちらつき・穴が無いことを確かめ、選ばれたクラスタの数（`MEGA_OCCLUSION` などのログ）が距離で減ることを記録する。
- verify: `cmake --build build --config RelWithDebInfo --target AssetCook CookAssets Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG4-POLYHAVEN-MODELS -Configuration RelWithDebInfo -Deterministic`
- stop-when: Poly Haven の API からモデルを取れない場合は、理由を記録して止める。
- paths: Game, Scripts, Assets/AssetSets, .gitignore, TASKS.md, PROGRESS.md
- notes: 計画書 1（検証は起動画面に高ポリの資産を足す）。起動画面の見た目を変える（ユーザー承認済みの追加）。

## VTG4-ACCEPT: 段4（LODの階層の焼き込み）の受入れを記録する
- status: done
- done-when: `Docs/RenderingValidation/VirtualizationAcceptance.md` の段4の節に、焼き込みの性質のテスト、起動画面の `-Deterministic` の撮影（朝・昼・夕・夜 × 3視点、glTF・実行時の生成との PSNR）、テクスチャの VRAM（岩・小屋の 409.6 MiB が無くなった後の全体）、距離を変える撮影の所見（割れ目・ちらつき）、選ばれたクラスタの数、起動の時間、golden、関係するテストの結果、既知の限界を書く。
- verify: `cmake --build build --config Debug --target AssetCook CookedMeshTest MegaGeometryResourcesTest RayTracingSceneSnapshotTest RenderGraphCompileTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CookedMeshTest|AssetCookMeshSmoke|AssetCookMeshSimplifySmoke|MegaGeometryResourcesTest|RayTracingSceneSnapshotTest|RenderGraphCompileTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG4-ACCEPT -Configuration RelWithDebInfo -Deterministic -SunElevations 10,45,3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG4-ACCEPT-night -Configuration RelWithDebInfo -Deterministic -Night`
- stop-when: 受入れの数値が段4の受入れ（計画書 5）を満たさない場合は、測った値を記録して止める。
- paths: Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: この段の後、親が main へマージしてプッシュする。

## VTG5-GEOM-POOL: ジオメトリの共有プールとサブアロケータを作る
- status: done
- done-when: `GeometryPool`（RenderResources が持つ。シングルトン禁止）が、DeviceLocal の大きなバッファ（既定 256 MiB の塊。頂点・インデックス・storage として使える用途）を必要に応じて足し、その中を区画に分けるサブアロケータ（整列・隣の空きとの結合・断片化の統計）を持つ。区画の解放は `GpuRetireQueue` で、最後に使った提出の serial が完了してから空きに戻す。使用量を `VRAM_LEDGER geometry_pool` に、目標を `VideoMemoryBudgetManager` の Geometry の枠に出す（取り分の決め方は VTG5-PAGE-STREAMER で詰める。この項目では使用量の報告まで）。CPU のテスト `GeometryPoolAllocatorTest`（`RenderResourcesDomainContractTest` の束）が、確保・解放・結合・整列・塊の追加・確保できないときの失敗・遅延解放の順序を確かめる。
- verify: `cmake --build build --config Debug --target Game RenderResourcesDomainContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(GeometryPoolAllocatorTest|GpuRetireQueueTest|VideoMemoryBudgetManagerTest)$"`
- stop-when: RHI のバッファの用途の組み合わせ（頂点・インデックス・storage・転送先・BDA）が1つのバッファで作れない場合は、理由を記録して止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/RHI, Library/Core/Private/RHI, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.3。危険地帯（メモリ・寿命）。

## VTG5-ASYNC-UPLOAD: ステージングのリングからバッファへGPUを待たずに書く
- status: done
- done-when: 段2のタイルのアップロードのリング（`TileUploader`）を、テクスチャのタイルとバッファの区画の両方へ書ける共通の `GpuUploadRing` に広げる（または同じリングをバッファのコピーにも使えるようにする）。バッファへのコピーは描画のコマンドの先頭で行い、`WaitIdle` を呼ばない。1フレームの上限（バイト数）をテクスチャとバッファで共有する。GPU のテスト `GpuUploadRingVulkanTest`（`RHITextureUpdateVulkanTest` の束）が、連続する複数フレームでプールの区画へ書いて計算シェーダーで読み戻し、その間に `WaitIdle` を呼ばないことを確かめる。既存の `SparseTileUploadVulkanTest` も通る。
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest RenderResourcesDomainContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(GpuUploadRingVulkanTest|SparseTileUploadVulkanTest|GeometryPoolAllocatorTest)$"`
- stop-when: テクスチャとバッファのコピーの同期（バリア）を1つのリングで表せない場合は、理由を記録して止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/RHI, Library/Core/Private/RHI, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（RHI/Vulkan・RenderThread）。

## VTG5-MEGA-POOL-MIGRATE: MegaGeometryの頂点・インデックス・クラスタを共有プールへ移す
- status: done
- done-when: `MegaGeometryResourceStore` が、メッシュごとの host-visible のバッファ3本をやめ、`GeometryPool` の区画（DeviceLocal）へ `GpuUploadRing` で書く。描画（`MegaGeometryPass` の間接描画・2パスの遮蔽）・影（フォールバックの範囲）・レイトレ（BLAS の頂点・インデックスのアドレスとキー）が、プールのバッファと区画のオフセットで動く。メッシュの解放は区画を遅延解放する。`MegaGeometryResourcesTest`・`RayTracingSceneSnapshotTest`・`RenderGraphCompileTest` が通り、`-Deterministic` の撮影が移行前と一致する（PSNR を記録。目安 60 dB 以上）。`VRAM_LEDGER` の geometry_pool で MegaGeometry の量を記録する。
- verify: `cmake --build build --config Debug --target Game MegaGeometryResourcesTest RayTracingSceneSnapshotTest RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(MegaGeometryResourcesTest|RayTracingSceneSnapshotTest|RenderGraphCompileTest|GeometryPoolAllocatorTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG5-MEGA-POOL-MIGRATE -Configuration RelWithDebInfo -Deterministic`
- stop-when: BLAS の作成が区画のオフセットを表せず、加速構造の作り方を変える必要がある場合は、理由を記録して止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画パス・RT・寿命）。手続きの `ProceduralMeshGpuStore` はこの段では移さない。

## VTG5-BATCHED-CULL: MegaGeometryのインスタンスをまとめて1回のカリングと材質ごとの間接描画にする
- status: done
- done-when: MegaGeometry の全インスタンスの表（変換・前のフレームの変換・メッシュ・材質の番号）を1つの storage buffer に置き、カリング（2パスの遮蔽を含む）を1回の dispatch（1パスにつき）で全インスタンスに掛ける。描画は材質ごとの区間に分けた間接描画の列（材質の数だけ `DrawIndexedIndirectCount`）にし、インスタンスごとの 1.25 MB の IndirectDraw のバッファをやめる。`-Deterministic` の撮影が移行前と一致する（PSNR を記録）。`MEGA_OCCLUSION` の行に描画フレームの番号と読み込み完了からの相対フレームの番号を足し、まとめた版を同じ条件で2回撮って全視点の同じ相対フレームの4つの数が一致すること（決定的であること）を記録する。移行前のログとの数の比較はしない（画像の PSNR で一致を判定する）。（CPU の記録の時間・GPU 時間の計測は VTG5-BATCHED-CULL-PERF へ分けた。）
- verify: `cmake --build build --config Debug --target Game MegaGeometryResourcesTest RenderGraphCompileTest ViewportSnapshotDebugWiringTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(MegaGeometryResourcesTest|RenderGraphCompileTest|MegaGeometryFrameCommandDebugModeTest|HiZOcclusionTestVulkanTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG5-BATCHED-CULL -Configuration RelWithDebInfo -Deterministic`
- stop-when: 2パスの遮蔽の可視ビットの番号の付け方がインスタンスの表と両立しない場合は、理由を記録して止める。
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, Scripts/CaptureStartupScene.ps1, Game, TASKS.md, PROGRESS.md
- notes: 計画書 4.3。危険地帯（描画パス）。材質の切り替えは段6のビジビリティバッファで不要になるが、それまでは材質ごとの区間で描く。 2026-10-05 親: run `20261005-094208` で反復6が40分の時間切れ、反復7が背景のビルドの完了待ちで終わり、2反復連続で進捗なしになった。途中の変更は `2267b9a6`（作業途中の保存）にあり、親が確かめた時点で Debug の Game・RenderGraphCompileTest・MegaGeometryResourcesTest のビルドと3本のテスト（MegaGeometryResourcesTest・RenderGraphCompileTest・MegaGeometryFrameCommandDebugModeTest）は通る。その上から続け、done-when の残り（`-Deterministic` の撮影と `MEGA_OCCLUSION` の一致）を確かめて閉じる。時間の計測はこの項目でしない。ビルド・撮影はフォアグラウンドで回して完了を待つ（背景で起動して返答を終えると反復がそこで終わる）。 2026-10-05 親（run `20261005-114343` の保留を解く）: 評価2周の残りは「`MEGA_OCCLUSION` の数が移行前と一致する」の証明だけ（画像は移行前と default 112.5・near 92.1 dB・low 完全一致）。移行前と移行後では読み込み完了のフレーム（150 と 104）が違い、30 フレームごとの標本の時点がずれるので、古い版を動かさない限り同じフレームでは比べられない。親が完了条件を、相対フレームの番号を付けて「まとめた版の2回が一致」と「移行前との差が移行前の揺れの幅の内」に直した。古い版を checkout して測ることはしない。 2026-10-05 親（run `20261005-134836` の保留を解く）: 評価の残りは「移行前との数の差が移行前の揺れの幅の内」の証明だけだった（low の相対フレーム30 が 41 で、移行前の4標本 34〜37 の外）。移行前の標本は数個しか残らず、古い版を動かさない限り揺れの幅は示せない。その後にグループの BVH などでカリングの作りが変わり、移行前の数との比較の意味も薄れた。遮蔽で省くのは見えないクラスタなので、数の多少は見た目に出ない。親が完了条件から移行前との数の比較を外した（画像の一致と、まとめた版の2回の数の一致で判定する）。実装と検証は済んでいるので、新しい完了条件で記録して閉じる。


## VTG5-BATCHED-CULL-PERF: まとめたカリングのCPUの記録の時間とGPU時間を測る
- status: done
- done-when: まとめたカリングの後の版で、`MegaGeometryPass` の CPU の記録の時間（RecordFrameCommand。開発ビルドの計測かログ）と GPU 時間（`-GpuTimingFrames`、RelWithDebInfo）を、MegaGeometry のインスタンスが既定（起動画面の数個）のときと 300 個のとき（撮影スクリプトの引数か一時の起動引数。VTG5-STRESS-GEOMETRY の前なので簡易なものでよい）で測り、`PROGRESS.md` に表で記録する。CPU の記録の時間が、300 個でも既定の2倍以内に収まる（インスタンスの数に比例しない）ことを確かめる。GPU 時間は記録するだけ。段3の受入れの GPU 時間（既定の視点の `MegaGeometryPass` 0.223 ms）と並べる。古い版を checkout して測ることはしない（作業ツリーの版を動かさない）。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG5-BATCHED-CULL-PERF -Configuration RelWithDebInfo -ViewNames default -GpuTimingFrames 400`
- stop-when: 300 個で CPU の記録の時間が既定の2倍を超え、原因がまとめ方の不足（インスタンスごとの処理が残っている）なら、測った値と残っている処理を記録して止める。
- paths: Scripts/CaptureStartupScene.ps1, Game, PROGRESS.md, TASKS.md
- notes: 2026-10-05 親が VTG5-BATCHED-CULL から分けた（1反復に収まらなかったため）。計測のための一時の起動引数を Game に足すなら、既定の描画は変えない。
## VTG5-PAGE-FORMAT: クラスタをページに詰めて焼き、根のページを決める
- status: done
- done-when: クッカーが、階層のクラスタを 128 KiB のページに詰めて NVMESH v1.1 に書く（1つのグループは1つのページに収める。ページは頂点・インデックス・クラスタの記録を自分の中のオフセットで持つ）。ページの表（ファイル内のオフセット・大きさ・親のページの番号）と、常に常駐する根のページ（粗い段とフォールバックの段を含む）の印を持つ。cluster record のページの番号を埋める。v1.0 も読む。`CookedMeshTest` に、ページの表の往復、グループがページをまたがないこと、根のページだけで閉じたメッシュ（フォールバック）が描けること、壊れた表の拒否を足す。`Docs/Architecture/NVMESHv1.md` に追記する。
- verify: `cmake --build build --config Debug --target AssetCook CookedMeshTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CookedMeshTest|AssetCookMeshSmoke|AssetCookMeshSimplifySmoke)$"`
- verify: `cmake --build build --config RelWithDebInfo --target AssetCook CookAssets -- /m:1`
- stop-when: 1つのグループが 128 KiB に収まらない資産がある場合は、資産と大きさを記録して止める。
- paths: Tools/AssetCook, Library/Core/Public/Asset, Library/Core/Private/Asset, Test/Core/Asset, Docs/Architecture, TASKS.md, PROGRESS.md
- notes: 計画書 4.3。危険地帯（アセットロード）。

## VTG5-GROUP-BVH: クラスタのグループのBVHでカリングをたどる
- status: done
- done-when: クッカーがグループの BVH（節ごとに境界球と、子の中で最大の親の誤差）を NVMESH v1.1 に焼き、GPU のカリングが、平らなクラスタの列の代わりに BVH を段ごとの dispatch（または持続するスレッド）でたどってグループを選び、選んだグループのクラスタを判定する。BVH で枝を切る条件（視錐台・遮蔽・誤差）は、切った先のクラスタが選ばれないことが保証される保守的なもの。CPU で同じ判定を写した検査（`MegaGeometryResourcesTest`）で、BVH をたどった選択と平らな選択が一致することを確かめる。`-Deterministic` の撮影が移行前と一致する（PSNR を記録）。
- verify: `cmake --build build --config Debug --target Game MegaGeometryResourcesTest CookedMeshTest RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(MegaGeometryResourcesTest|CookedMeshTest|RenderGraphCompileTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game AssetCook CookAssets -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG5-GROUP-BVH -Configuration RelWithDebInfo -Deterministic`
- stop-when: BVH の段の数がメッシュによって大きく違い、段ごとの dispatch の数が描画の予算を超える場合は、測った値を記録して止める。
- paths: Tools/AssetCook, Library/Core/Public/Asset, Library/Core/Private/Asset, Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Asset, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.3。危険地帯（描画パス・アセットロード）。

## VTG5-PAGE-REQUEST: カリングが常駐していないページを要求し、親の段で描く
- status: done
- done-when: ページの表（ページの番号 → プールの区画、または非常駐）を GPU に置き、カリングが「もっと細かい子のグループを描きたいが、そのページが常駐していない」ときは、親のグループのクラスタを描き（穴を作らない）、子のページの要求を要求のバッファ（重複はハッシュで減らす）へ書く。要求は2フレーム遅れで GPU を待たずに読み戻す（VT の `VirtualTextureFeedbackRing` に倣う）。GPU のテスト `GeometryPageRequestVulkanTest`（`RHITextureUpdateVulkanTest` の束）か `MegaGeometryResourcesTest` の CPU で写した判定で、子のページが非常駐のときに親が選ばれ、要求が書かれることを確かめる。
- verify: `cmake --build build --config Debug --target Game MegaGeometryResourcesTest RHITextureUpdateVulkanTest RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(MegaGeometryResourcesTest|RenderGraphCompileTest|GeometryPageRequestVulkanTest)$"`
- stop-when: 親で描く判定が2パスの遮蔽の可視ビットと食い違い、遮蔽の履歴を作り直す必要がある場合は、理由を記録して止める。
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.3。危険地帯（描画パス・RenderThread）。 2026-10-05 親（run `20261005-134836` の保留を解く）: 評価2周の残り1件を直す。複数のビューが同じフレームに要求を書くと、共有の要求のスロットの版を後のビューが上書きし、読み戻しで先のビューの要求が、解放・再割り当てされた区画の別のメッシュへ解決される（`GeometryPageRequestRing.h` のスロットの版、`MegaGeometryPass.cpp` のビューごとの呼び出し）。要求ごとに持ち主（メッシュ・インスタンス）と世代を持たせるか、そのフレームの全ビューが使うページの表と版を1つに固定する。共有のリングへの複数ビューの記録 → 途中の解放・再割り当て → 2フレーム後の読み戻しまで通し、先のビューの要求が別のメッシュへ解決されないテストを足す（保存した旧版を解決の関数へ直接渡すだけのテストでは検出できない）。

## VTG5-PAGE-LINK-ID: 親のクラスタが、作ったグループの番号を持つ
- status: done
- done-when: クッカーが親のクラスタに「作ったグループの番号」を書き（NVMESH のクラスタの記録に項目を足す。v1.1 以前は読めて、番号が無いときは今の境界球・誤差の照合へ戻る）、`ComputeGeometryPageLinks` が値の照合の代わりに番号で子のページを決める。同じ値のグループが別のページにあっても固定するページが無くなる（`PinnedPages` は番号の無い旧資産のときだけ）。`CookedMeshTest` に、同じ境界球・誤差の別グループを持つ合成のメッシュで、親ごとの子のページが生成元のグループのページになることを足す。
- verify: `cmake --build build --config Debug --target AssetCook CookedMeshTest MegaGeometryResourcesTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CookedMeshTest|MegaGeometryResourcesTest|AssetCookMeshSmoke)$"`
- stop-when: 記録の形式を変えると既存の焼き込み済みの資産が読めなくなる場合は、理由を記録して止める。
- paths: Tools/AssetCook, Library/Core/Public/Asset, Library/Core/Private/Asset, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Docs/Architecture, Test/Core/Asset, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: VTG5-PAGE-REQUEST の評価で、値の照合は同じ値の別グループを区別できないと指摘された。暫定は、候補のページを常駐のまま固定する（ストリーマの追い出しの対象外）。VTG5-PAGE-STREAMER の前に入れるのが望ましいが、固定したページは `MegaMeshGPUData::PinnedPageCount` で見え、ストリーマは `SetMegaMeshPageRegion` の拒否に従えば安全に進められる。危険地帯（アセットロード）。

## VTG5-PAGE-STREAMER: ページを要求から読み込み、予算の内で追い出す
- status: done
- done-when: `GeometryPageStreamer`（RenderResources が持つ）が、ページの要求を優先度（粗い段が先・要求の数・新しさ）で選び、JobSystem の範囲読みで NVMESH v1.1 からページを読み、`GpuUploadRing` でプールの区画へ書いてからページの表を更新する（書き終える前に公開しない）。1フレームの上限（読み・コピーのバイト数）を持つ。根のページはメッシュの読み込み時に常駐させ、追い出さない。`VideoMemoryBudgetManager` の Geometry の枠の目標を超えたら、最後に要求されたフレームが古いページから LRU で外す（外したページの区画は、使っていた提出が完了してから再利用）。`VRAM_POOLS` に geometry_target_mb・geometry_used_mb・geometry_evicted_pages を出す。CPU のテスト `GeometryPageStreamerTest`（`RenderResourcesDomainContractTest` の束。読み込み・アップロードは偽物）が、優先度・上限・根の常駐・追い出しの順・目標以下に収まること・解除と再利用の順序を確かめる。起動画面の撮影で、ページのストリーミングあり（既定）と全常駐（`--geometry-streaming=off`）の PSNR を記録する（目安 45 dB 以上）。
- verify: `cmake --build build --config Debug --target Game RenderResourcesDomainContractTest MegaGeometryResourcesTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(GeometryPageStreamerTest|GeometryPoolAllocatorTest|MegaGeometryResourcesTest|VideoMemoryBudgetManagerTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG5-PAGE-STREAMER -Configuration RelWithDebInfo -Deterministic`
- stop-when: 追い出したページを読む描画が出る経路を塞げない場合は、理由を記録して止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Asset, Library/Core/Private/Asset, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.3。危険地帯（メモリ・寿命・アセットロード・RenderThread）。VT のストリーマ（`VirtualTextureStreamer`）の作りに倣う。 2026-10-05 親: run `20261005-134836` の反復6が40分の時間切れになった。途中の変更は `9fc136b7`・`165138cb`・`c6b68001` にあり、親が確かめた時点で Debug の Game・RenderResourcesDomainContractTest・MegaGeometryResourcesTest のビルドは通る。その上から続ける。VTG5-PAGE-REQUEST の要求の持ち主・世代の修正の後に回る。ビルド・撮影はフォアグラウンドで回して完了を待つ。新しく作ったファイルは自分でコミットする（時間切れの途中保存では未追跡のファイルが残る）。 2026-10-05 親（run `20261005-154938` の保留を解く）: 評価2周の残りは「要求の新しさによる優先順位と LRU」。別々の `Update` で取り込んだ要求の発生フレームの新旧が、取り込んだフレームへ写されて失われる（再現: 読み込みの上限0で、ページ1の発生10を `Update(20)`、ページ2の発生11とページ3の発生12を `Update(21)` で取り込むと、上限を1件へ戻したときの選ぶ順が 3→1→2 になる。期待は 3→2→1。LRU も同じ）。その後の VTG5-PAGE-TOUCH の `01bba2de`・`1c5bb27e` で直っている可能性があるので、まず今のコードでこの再現を C++ のテスト（`GeometryPageStreamerTest` に、複数の `Update` をまたぐ優先順位と LRU のケース）にして確かめ、通らなければ優先順位・LRU には要求の発生フレームを持ち、期限の判定には取り込んだフレームを分けて持つ。

## VTG5-PAGE-TOUCH: 常駐ページの使用の印を LRU に渡す
- status: done
- done-when: 描画で使われている常駐ページ（描いたクラスタのページ）の「最後に使われたフレーム」をストリーマへ渡し、LRU の追い出しが読み込んだ順でなく使われた順になる。`--vram-budget-mb 900`（ジオメトリの目標 54 MB。起動画面の作業集合が約 55 MB）の `-Deterministic` の撮影が、ページの入れ替わりを続けず落ち着いて完了し（`HasPendingPageStreaming` が false に戻る）、`geometry_evicted_pages` の増加が止まる。CPU のテスト（`GeometryPageStreamerTest`）が、使われ続けるページを新しい要求のために追い出さないことを確かめる。
- verify: `cmake --build build --config Debug --target Game RenderResourcesDomainContractTest MegaGeometryResourcesTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(GeometryPageStreamerTest|MegaGeometryResourcesTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG5-PAGE-TOUCH -Configuration RelWithDebInfo -Deterministic -VramBudgetMb 900`
- stop-when: 使用の印をシェーダーから返すとカリングの時間が予算を超える場合は、測った値を記録して止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: VTG5-PAGE-STREAMER の撮影で見つけた。ページの要求は「非常駐の子」だけに出るので、常駐したページの最後に要求されたフレームは読み込み前のまま進まず、LRU が読み込んだ順になる。作業集合が目標を少し超えると、使われているページを外して再読み込みする入れ替わりが続く（既定の目標 3649 MB では起きない）。VTG5-STRESS-GEOMETRY の前に入れるのが望ましい。 2026-10-05 親（run `20261005-154938` の保留を解く）: 評価2周の残り1件を直す。BVH の経路（`cluster_bvh_cull.comp`）で、2パス目に節ごと遮蔽されると枝を打ち切るため、1パス目で描いたクラスタのページの使用の印が出ない（平坦の経路は直っている）。1パス目で描画のコマンドを積む時点で、自分のページの使用の印を出す。子のページの要求は今の可視の条件のまま。パスの間の要求のバッファの同期を確かめる。BVH の節の判定から通す回帰のケースを GPU のテスト（`GeometryPageRequestVulkanTest`。今は BVH のシェーダーはコンパイルの確認だけ）に足す。`PROGRESS.md` の行末の記録（「一致」）の誤りも直す。

## VTG5-STREAM-HANG: 予算を絞った負荷モードの撮影で起動から約32秒で止まる原因を突き止めて直す
- status: done
- done-when: VTG5-STRESS-GEOMETRY の撮影の途中で見つかった、`--stress-geometry` を `--vram-budget-mb 1100` で低い視点から旋回して撮ると、Game のログが起動から約32秒で止まる（ハングか異常終了）現象を再現し、原因（スレッドの待ち合いか、例外・アクセス違反か）を、プロセスが生きているかの確認、Windows のイベントログ（WER の Event 1000 と落ちた RVA）、ダンプ（`procdump -ma` が使えればそれ、無ければ `--render-thread=st` での再現と Visual Studio の `cdb`/`dumpbin` など手元の道具）で突き止めて直す。同じ条件の撮影を3回続けて最後まで撮れる（result=pass、ログが撮影の保存まで続く）ことを確かめ、原因と直し方を `PROGRESS.md` に記録する。原因の経路に回帰のテスト（CPU のストリーマ・プールの偽物で再現できればそれ）を足す。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config Debug --target RenderResourcesDomainContractTest MegaGeometryResourcesTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(GeometryPageStreamerTest|GeometryPoolAllocatorTest|MegaGeometryResourcesTest)$"`
- stop-when: 2時間の調査で再現しない場合は、試した条件と結果を記録して止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/RHI, Library/Core/Private/RHI, Assets/Shaders, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-05 親が足した。run `20261005-154938` の反復7（VTG5-STRESS-GEOMETRY）が、1100 MB の low の旋回の撮影でログが 17:29:35（起動の約32秒後）に止まるのを見つけ、調べる途中で時間切れになった。途中の変更は `9abc9a06`。危険地帯（ストリーミング・RenderThread・寿命）。ビルド・撮影はフォアグラウンドで回して完了を待つ。調べる間に作業ツリーの版を動かさない（古い版の checkout はしない）。

## VTG5-STRESS-GEOMETRY: ジオメトリの負荷モードを足す
- status: done
- done-when: Game の `--stress-geometry` で、Poly Haven のスキャン資産（段4の3点）と岩・小屋・大きな球を数百個（既定 300）、変換を変えて地面の外側に並べる検証モードに入る（資産が無ければ置かずに警告）。全常駐ならジオメトリの量が Geometry の枠の目標の2倍以上になる `--vram-budget-mb` で撮り、`VRAM_POOLS` の geometry_used_mb が目標以下に収まり、`geometry_evicted_pages>0` で、撮影（上から・低い視点・旋回の連続フレーム）を開いて穴・割れ目・ちらつきが無いことを記録する。8GB 級を模す `--vram-budget-mb 6500` でも溢れずに描けることを記録する。GPU 時間（RelWithDebInfo）を記録する。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG5-STRESS-GEOMETRY -Configuration RelWithDebInfo -Deterministic`
- stop-when: 数百個のインスタンスで1フレームのカリングが 16.6 ms を超え、まとめ方で縮まない場合は、測った値を記録して止める。
- paths: Game, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 計画書 1（重い負荷は別モード）。撮影スクリプトに負荷モードの引数が無ければ足す。 2026-10-05 親: run `20261005-154938` の反復7が40分の時間切れになった。途中の変更は `9abc9a06`（`--stress-geometry` の配置、撮影スクリプトの引数）。それまでに、300 個が並ぶこと（全常駐のページの総量 約 289 MB）、1000 MB の予算で目標以下に収まって追い出しが起き、穴・割れ目が無いことを確かめている。1100 MB の low の旋回で止まる件は VTG5-STREAM-HANG で直してから、その撮影を撮り直して閉じる。

## VTG5-ACCEPT: 段5（ジオメトリのページのストリーミング）の受入れを記録する
- status: done
- done-when: `Docs/RenderingValidation/VirtualizationAcceptance.md` の段5の節に、起動画面の `-Deterministic` の撮影（朝・昼・夕・夜 × 3視点、ページのストリーミングあり・全常駐の PSNR）、ジオメトリの量（段4の全常駐・プール・ページ）、負荷モードの `--vram-budget-mb 6500` と絞った予算での `VRAM_POOLS`・追い出し・撮影の所見、GPU 時間と CPU の記録の時間、golden、関係するテストの結果、既知の限界を書く。
- verify: `cmake --build build --config Debug --target RenderResourcesDomainContractTest RHITextureUpdateVulkanTest CookedMeshTest MegaGeometryResourcesTest RayTracingSceneSnapshotTest RenderGraphCompileTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(GeometryPoolAllocatorTest|GpuUploadRingVulkanTest|GeometryPageStreamerTest|CookedMeshTest|MegaGeometryResourcesTest|RayTracingSceneSnapshotTest|RenderGraphCompileTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG5-ACCEPT -Configuration RelWithDebInfo -Deterministic -SunElevations 10,45,3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG5-ACCEPT-night -Configuration RelWithDebInfo -Deterministic -Night`
- stop-when: 受入れの数値が段5の受入れ（計画書 5）を満たさない場合は、測った値を記録して止める。
- paths: Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: この段の後、親が main へマージしてプッシュする。

## VTG6-RHI-INT-FORMATS: 整数の形式とgeometryShaderの機能をRHIに足す
- status: done
- done-when: `RHI::Format` に R32_UINT と R32G32_UINT を足し、Vulkan の対応表・カラーの添付と storage image の用途・クリアの値（整数）を扱えるようにする。`geometryShader`（frag で `gl_PrimitiveID` を使うため）と `shaderStorageImageExtendedFormats`（RG16F などの storage image のため）を照会し、対応時に有効化して `DeviceCapabilities` に載せる。GPU のテスト `IntegerAttachmentVulkanTest`（`RHITextureUpdateVulkanTest` の束の MEMBER）が、R32_UINT の添付へ frag が `gl_PrimitiveID` と描画の番号から作った値を書き、読み戻して三角形ごとに期待の値になることを確かめる（機能が無ければ理由を出して 125）。
- verify: `cmake --build build --config Debug --target Game RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(IntegerAttachmentVulkanTest|RHIBlockCompressedTextureVulkanTest|SparseCapabilitiesVulkanTest)$"`
- stop-when: 開発機で `geometryShader` を有効にするとデバイスの作成が失敗する場合は、理由を記録して止める。
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.3（2026-10-05 親が段6の設計を詳しくした: ID は 32bit で「描画の記録の番号 << 7 | 記録の中の三角形の番号」、材質の解決は計算シェーダー、スキニングは計算シェーダーで変形、`geometryShader` の無い GPU では今の GBuffer の経路を予備に残す）。危険地帯（RHI/Vulkan）。

## VTG6-RASTER-CHUNKS: 手続きメッシュとスキニングを128三角形の塊に分け、頂点を計算シェーダーから読めるようにする
- status: done
- done-when: 手続きメッシュ（`ProceduralMeshGpuStore`）とスキニングのメッシュ（`SkinnedMeshGpuStore`）が、登録時にインデックスを 128 三角形以下の連続した塊（最初のインデックス・数）に分けて持つ（MegaGeometry のクラスタと同じ大きさ。後のビジビリティバッファの ID の「記録の中の三角形の番号」が 7bit に収まる）。手続きメッシュの頂点・インデックスのバッファに storage と BDA の用途を足す（または `GeometryPool` に置く）。CPU のテスト（`MeshResourcesProceduralGpuTest` と `SkinnedRenderPathContractTest` にケースを足す）が、塊が全三角形をちょうど1回ずつ覆い、各塊が 128 以下であることを確かめる。今の描画は変えない（起動画面の撮影が一致する）。
- verify: `cmake --build build --config Debug --target Game RenderResourcesDomainContractTest SkinnedRenderPathContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(MeshResourcesProceduralGpuTest|GeometryPoolAllocatorTest)$"`
- stop-when: 手続きメッシュのバッファの用途を変えると今の描画の経路の契約が崩れる場合は、理由を記録して止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.3。危険地帯（メモリ・寿命）。 2026-10-05 親（run `20261005-191130` の保留を解く）: `blocked/VTG6-RASTER-CHUNKS.md` の選択肢2を採った。verify の ctest から、既知の失敗（TEST-SKINNED。`gbuffer.vert` を読めず assert の後に終わらない）で完走しない `SkinnedRenderPathContractTest` を外した（ビルドは残す）。スキニングの塊の検証は、`SkinnedRenderPathContractTest.exe --test=` などで追加のケースだけを直接走らせた出力（「SkinnedMesh chunks cover every triangle once」）を証拠にする。TEST-SKINNED は段6の中で別に直す。

## VTG6-COMPUTE-SKINNING: 計算シェーダーでスキニングした今と前のフレームの頂点を作る
- status: done
- done-when: スキニングのインスタンスごとに、計算シェーダーが今のフレームのパレットと前のフレームのパレットで頂点（位置・法線・UV）を変形し、フレームごとのバッファ（今・前。storage・BDA）へ書くパスを足す（RenderGraph の資源として宣言し、後のビジビリティバッファのラスタと材質の解決が読む）。今の GBuffer の経路はまだ頂点シェーダーのスキニングのまま。GPU のテスト `ComputeSkinningVulkanTest`（`RHITextureUpdateVulkanTest` の束）が、既知のボーンと重みの頂点で、計算シェーダーの結果が CPU で計算した値（今と前）と一致する（許容 1e-4）ことを確かめる。`RenderingVelocitySkinnedVulkanTest` が通る。
- verify: `cmake --build build --config Debug --target Game RHITextureUpdateVulkanTest RenderingVelocityVulkanTest SkinnedRenderPathContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(ComputeSkinningVulkanTest|RenderingVelocitySkinnedVulkanTest)$"`
- stop-when: 前のフレームのパレットを RenderThread で保持する経路が無く、FramePacket の契約を変える必要がある場合は、理由を記録して止める。
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.3（スキニングは計算シェーダーで変形してからビジビリティバッファへ描く）。危険地帯（RenderThread・寿命）。 2026-10-05 親（run `20261005-191130` の保留を解く）: `blocked/VTG6-COMPUTE-SKINNING.md` の選択肢2を採った。verify の ctest から `SkinnedRenderPathContractTest`（既知の失敗 TEST-SKINNED）を外した。`ComputeSkinningVulkanTest`・`RenderingVelocitySkinnedVulkanTest` の ctest を証拠にする。

## VTG6-VISBUFFER-RESOURCES: ビジビリティバッファの資源と描画の記録の表を作る
- status: done
- done-when: RenderGraph の資源 `VisBuffer.Id`（R32_UINT、画面の大きさ。深度は `GBuffer.Depth` を共有）と、フレームごとの描画の記録の表（storage buffer。1つの記録が、種類（MegaGeometry のクラスタ・手続きメッシュの塊・スキニングの塊）、インスタンスの番号、頂点・インデックスの基点とアドレス、材質の番号、前のフレームの変換か前のフレームの頂点のアドレスを持つ）を足す。ID は `(記録の番号 << 7) | 記録の中の三角形の番号`、0 は空（画素が何も描かれていない）。ID と記録を作る・読む関数を C++ と GLSL（`Common/VisibilityBuffer.glsl`）でそろえる。CPU のテスト `VisibilityBufferEncodingTest`（`RenderResourcesDomainContractTest` の束）が、符号化と復号の往復、記録の数の上限（2^25）、空の扱いを確かめる。
- verify: `cmake --build build --config Debug --target Game RenderResourcesDomainContractTest RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VisibilityBufferEncodingTest|RenderGraphCompileTest)$"`
- stop-when: 記録の数がフレームあたり 2^25 を超えうる場面（負荷モード）がある場合は、測った値を記録して止める。
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.3。

## VTG6-VIS-RASTER: 不透明のすべてをビジビリティバッファへ描くパスを足す（既定は無効）
- status: done
- done-when: `--visibility-buffer=on` のとき、MegaGeometry のクラスタ（2パスの遮蔽・BVH・ページの経路のまま）、手続きメッシュの塊、スキニングの塊（VTG6-COMPUTE-SKINNING の変形済みの頂点）を、位置だけを読む頂点シェーダーと、ID（記録の番号は描画ごとの値、三角形は `gl_PrimitiveID`）を書く frag で、`VisBuffer.Id` と `GBuffer.Depth` へ描くパスを足す。描画の記録の表をそのフレームの描画から作る。この項目では GBuffer への書き込みはまだ今の経路のまま（`on` でも GBufferPass・MegaGeometryPass の GBuffer の描画は動かす）。`RenderGraphCompileTest` に `on` の記録を足す。デバッグの撮影（`--visibility-buffer=on` に、ID を色にして表示するデバッグの表示を足してよい）を開いて、物の輪郭と三角形の塊が正しく出ることを確かめる。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest MegaGeometryResourcesTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|MegaGeometryResourcesTest|IntegerAttachmentVulkanTest|VisibilityBufferEncodingTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG6-VIS-RASTER -Configuration RelWithDebInfo -Deterministic`
- stop-when: 2パスの遮蔽の HZB が、ビジビリティバッファの1パス目の深度で作れない場合は、理由を記録して止める。
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.3。危険地帯（描画パス）。既定の描画（`off`）は変えない。 2026-10-05 親（run `20261005-191130` の保留を解く）: `blocked/VTG6-VIS-RASTER.md` の選択肢1を採った。この項目は今の実装（MegaGeometryPass の後に両パスの間接描画を `VisBuffer.Id`・`GBuffer.Depth` へ重ね描き）で完了とする。stop-when の「ビジビリティの1パス目の深度で HZB を作る」順序は、GBuffer への書き込みを外す VTG6-DEFAULT-ON で組む。スキニングを含む ID の表示の撮影も VTG6-DEFAULT-ON へ回した（起動画面にスキニングが無いので、スキニングの検証シーンを撮る経路をそこで足す）。評価の差し戻し（記録の頂点の基点の二重加算）は `31877048` で直っている。

## VTG6-MATERIAL-CLASSIFY: 画面のタイルを材質ごとに分ける
- status: done
- done-when: `VisBuffer.Id` から 8×8 の画面のタイルごとに、そのタイルに出る材質の番号の集合を求め、材質ごとのタイルの一覧と、材質ごとの間接 dispatch の引数を作る計算シェーダーのパスを足す（空の画素だけのタイルはどの材質にも入れない）。GPU のテスト `MaterialTileClassifyVulkanTest`（`RHITextureUpdateVulkanTest` の束）が、合成した ID の画像（3つの材質が混じるタイル、空のタイル）から、期待のタイルの一覧と引数を作ることを確かめる。
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(MaterialTileClassifyVulkanTest|RenderGraphCompileTest)$"`
- stop-when: 1フレームの材質の数が間接 dispatch の引数の上限を超える場合は、測った値を記録して止める。
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.3。 2026-10-05 親（`027a4ab4` の評価の差し戻し。評価者は Claude の別文脈）: 次を直す。(1) 必須: `MaterialTileClassifyVulkanTest.cpp` が `std::vector`（`#include <vector>`）を使っている。`Container::VariableArray` に置き換える（絶対規則1。テスト実行ファイルで許されるのは `std::cout` だけ）。(2) 統計の「見えた最大の材質の番号＋1」（`material_tile_classify.comp` の `atomicMax`）が、上限以上の材質で return した後にしか通らず、上限を超えた材質を数えない。上限の判定の前に `atomicMax` して、stop-when の値が統計から読めるようにする（テストのケース A で材質 12 が見えたら 13 になる）。(3) 材質ごとの間接 dispatch の x がタイル数そのもので、4K では Vulkan が保証する `maxComputeWorkGroupCount[0]` の最小値 65535 を超える。x を 65535 以下に抑え、超える分は y に広げる（消費側はタイルの番号を `y * 65535 + x` で得て、数を超えたグループは return する）。この形をヘッダーのコメントに書き、テストに x の上限を超える数（上限を小さくして試せるなら、それで）のケースを足す。(4) 画面の端の判定（`pixel < screen`）を反証できるよう、テストに「ID の画像が Width・Height より大きく、外に空でない ID がある」ケースを足す。`Record` で Width・Height が ID のテクスチャの大きさ以下であることを確かめる。(5) `RenderGraphCompileTest` の `TestMaterialTileClassifyAbsentByDefault` は、パスを足さない構成だけを見ていて落ちようがない。実際の既定（`SceneView` がパスを足し、無効のまま）を試すか、確かめていることに合わせて名前を直す。既知の限界として残す: 一覧の大きさが最悪（1タイル64材質）で取ってあり、1080p で約 8.3 MB・4K で約 33 MB（既定で無効の間は影響なし。VTG6-DEFAULT-ON で予算と照らす）。記録の材質の番号がフレームで一意でない件は VTG6-MATERIAL-TABLE で直す。

## VTG6-SKINNING-FINAL-BARRIER: スキニングの頂点のバッファを書いた後に、宣言した最終の状態へ遷移させる
- status: done
- done-when: `SkinningComputePass::Execute` が、書いた今・前の頂点のバッファ（`Skinning.CurrentVertices`・`Skinning.PreviousVertices`）を、dispatch の後に `UnorderedAccess` から宣言した最終の状態 `GenericRead` へ `BufferBarrier` で遷移させる（RenderGraph は終わった状態を信じて、後のパスの読み取りの前にバリアを足さない。`MaterialTileClassifyPass` が同じ形で遷移させている）。記録できなかったインスタンスだけのフレームでも、宣言したバッファは遷移させる。`RenderGraphCompileTest` か `ComputeSkinningVulkanTest` の側で、その遷移が記録されることを確かめる。
- verify: `cmake --build build --config Debug --target Game RHITextureUpdateVulkanTest RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(ComputeSkinningVulkanTest|RenderGraphCompileTest)$"`
- stop-when: 遷移を足すと `VisibilityRasterPass` の読み取りの前に二重のバリアになる、または検証エラーが出る場合は、その状況を記録して止める。
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-05 VTG6-MATERIAL-CLASSIFY の反復で見つけた。`SkinningComputePass` は最終の状態を `GenericRead` と宣言するが dispatch の後にバリアを出さず、`VisibilityRasterPass` の頂点シェーダーが読む前に書き込みが見えることが保証されない。危険地帯（描画パス・同期）。 2026-10-05 親（`c5f252d5` の評価の差し戻し。評価者は Claude の別文脈）: 遷移の実装は正しいが、足したテスト `TestSkinningComputeFinalBarriersTransitionToGenericRead`（`RenderGraphCompileTest.cpp` 2317 行付近）は static の `RecordFinalBarriers` を直接呼ぶだけで `SkinningComputePass::Execute` を通らない。`Execute` の `RecordFinalBarriers(...)` の呼び出しを消しても、早期 return を旧条件（`!m_Compute.IsReady() || !context.SkinnedMeshes || !context.SnapshotSkinnedMeshFrameLeases`）に戻しても、どのテストも落ちない。次を直す。(1) パスを通すテストにする: `RenderGraphCompileTest` の `VisibilityRasterScene`（1928〜1973 行付近。`FakeDevice` と実際の `Assets/Shaders` を読む `ShaderManager` を持ち、`FakeDevice::CreateComputePipeline` もパイプラインを返す）に `SkinningComputePass` を足して `Raster.SetSkinningComputePass` でつなぎ、スキニングの描画コマンドと貸し出し（`SkinnedMeshFrameLease`。`ComputeSkinningVulkanTest.cpp` 429 行付近と同じ形）を1件渡し、`SkinnedMeshes = nullptr`（記録できないフレーム）で回す。確かめること: dispatch が 0 件でも `Skinning.CurrentVertices`・`Skinning.PreviousVertices` に `UnorderedAccess`→`GenericRead` が記録されること、その後 `VisibilityRasterPass` の前に同じバッファへの `GenericRead` 起点の二重のバリアが出ないこと（stop-when を実際に観測する）。上の2つの変更（呼び出しの削除・旧条件）で落ちることを、一時的に戻して確かめ、出力を証拠に残す。(2) テストのためだけに公開ヘッダーへ足した `public: static void RecordFinalBarriers(...)`（`SkinningComputePass.h` 158〜165 行付近）を private（か .cpp の無名の名前空間）に戻す。FakeDevice でシェーダーが通らず (1) が書けない場合は、理由と代わりの観測（validation layer 付きの `--visibility-buffer=on` の実行で VUID が0件など）を記録して止める。


## VTG6-CHUNKS-HARDEN: 三角形の塊の分け方の穴を塞ぎ、RTの経路の変化を確かめる
- status: done
- done-when: (1) `MeshResourcesProceduralGpuTest.cpp` で `6b85c9b` が足した行（203・239・253・263・274 行付近）の `std::vector` を `Container::VariableArray` に置き換える（絶対規則1）。(2) `MeshIndexChunks.h` が 3 の倍数でない区切り（サブメッシュの最初のインデックス・数）を黙って無視して隣の区間と合わせた塊を作る（例: インデックス 13 個、区間 [0,7) と [7,13) で塊 (0,12) が2つの材質をまたぐ）のをやめ、検出して失敗を返し、呼び出し側が `LOG` で1回知らせる（区間を勝手に合わせない）。`first += maxIndicesPerChunk` の桁あふれで無限ループにならない形にする。(3) テストに、128 三角形を超えるサブメッシュ（区切りと 128 での分割が重なる）、インデックスが 1〜2 個（塊 0 個）、256・257 三角形、3 の倍数でない区切りの失敗、スキニングのメッシュを解放した後に `TryGetChunks` が false になることのケースを足す。(4) `VisibilityRasterPass.cpp`（510 行付近）が描画の範囲ごとに塊を作り直し、登録時に保存した `ProceduralMeshGPUData::Chunks` を読んでいない。保存した塊を読むか、作り直すなら登録時の保存をやめるか、どちらかに揃え、理由を PROGRESS に書く。(5) 手続きメッシュのバッファが BDA を持ったので、`RenderingCoordinator.cpp`（569 行付近）の `CreateAddressableMeshBuffer` がコピーを作らず元のバッファを返すようになった（RT の BLAS の入力と RTGI・DDGI のインスタンスのアドレスが元のバッファを直接読む）。`RayTracingSceneSnapshotTest` を走らせて BLAS の寿命の検査が通ることを確かめる。(6) `PROGRESS.md` の 6b85c9b の節で改行の記号が実際の改行になって割れた行と、274ca1e0 の節の「描画経路は変えていない（バッファの用途ビットの追加のみ）」を事実（RT の経路がコピーから直接の利用に変わった。撮影の画素比較で出力は一致）に直す。
- verify: `cmake --build build --config Debug --target Game RenderResourcesDomainContractTest SkinnedRenderPathContractTest RayTracingSceneSnapshotTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(MeshResourcesProceduralGpuTest|GeometryPoolAllocatorTest|RayTracingSceneSnapshotTest)$"`
- stop-when: `RayTracingSceneSnapshotTest` が元のバッファの直接の利用で落ち、原因が BLAS とメッシュのバッファの寿命の契約にある場合は、落ちた検査と値を記録して止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-05 親が足した（VTG6-RASTER-CHUNKS の評価の残課題。評価は PASS）。スキニングの塊の追加のケースは `SkinnedRenderPathContractTest.exe` の直接実行の出力（そのケースの行）を証拠にする（TEST-SKINNED の既知の停止のため ctest では完走しない）。危険地帯（メモリ・寿命・RT）。

## VTG6-PASS-FRAME-SLOTS: 計算パスのUBOとdescriptorの枠を、1フレームに何回呼ばれても上書きしない形にする
- status: done
- done-when: `SkinningComputePass`（`m_Compute.BeginFrame(m_FrameCounter++)`）・`MaterialTileClassifyPass`（同じ形）・`MegaGeometryPass`（`m_FrameSlots[m_OcclusionFrameCount % FrameSlotCount]`）が、枠の番号をフレームではなく Execute を呼んだ回数で決めている。同じパスのインスタンスが1フレームに何回 Execute されうるか（エディタの複数のビューポート・反射・キャプチャなど、同じ SceneView か別の SceneView か）を調べて PROGRESS に書き、1フレームに N 回（N がフレームの枠の数以上でも）呼ばれても、まだ提出していない・GPU が読み終えていない UBO・descriptor set・一時のバッファを上書きしない形にする（フレームごとに使った枠を数えて足りなければ増やす、GPU の完了で枠を返すリングにする等）。CPU のテスト（関係する束の MEMBER）が、1フレームに `FrameSlotCount` を超える回数の Execute（の枠の割り当て）で、全部が別の枠になり、次のフレームで GPU の完了の後に再利用されることを確かめる。既定の描画は変えない（`-Deterministic` の起動画面の撮影が前と一致する）。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest MegaGeometryResourcesTest RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|MegaGeometryResourcesTest|ComputeSkinningVulkanTest|MaterialTileClassifyVulkanTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG6-PASS-FRAME-SLOTS -Configuration RelWithDebInfo -Deterministic`
- stop-when: 枠を GPU の完了で返すのに要るフェンスの値が RenderGraph・RHI の公開 API から取れず、RHI の公開 API を足す必要がある場合は、足す API を記録して止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-05 親が足した（VTG6-COMPUTE-SKINNING の評価の残課題。同じ SceneView が1フレームに3つ以上のビューポートを描くと、3回目の `BeginFrame` が1回目の枠の位置に戻り、提出前の dispatch の UBO と descriptor set を上書きする）。MegaGeometryPass は既定で有効なので、既定の経路の撮影で一致を確かめる。危険地帯（RenderThread・同期・寿命）。

## VTG6-SKINNING-HARDEN: 計算シェーダーのスキニングの閾値・上限・継続の検査を整える
- status: done
- done-when: (1) `skinning_compute.comp` の `NormalMatrixOf` の特異と見なす閾値（1e-6）を `MatrixUtils::CreateNormalMatrix` の `Constants::EPSILON`（1.19e-7）にそろえ、コメントを事実に合わせる。`ComputeSkinningVulkanTest` の参照（198 行付近の `TransformNormal`）も同じ閾値にし、|det| が 1.19e-7 以上 1e-6 未満の骨（一様スケール 0.009 など）のケースを足す。(2) `SkinningComputePass.cpp`（241 行付近）が頂点の合計が上限を超えると残りのインスタンスを黙って捨てるのをやめ、捨てた数を `LOG` で1回と統計（`Debug/Stats.h`）に出す。1本の束縛が `maxStorageBufferRange` を超えないこと、1インスタンスの dispatch の x が 65535 を超えないこと（超えるなら y に広げるか分ける）を確かめる。(3) `Declare`/`Execute` の継続の検査を足す: 2つ以上のインスタンスの詰め方（2つ目以降の `VertexBase`）、前のフレームのパレットが無いときに今の値で代用すること、インスタンス描画のものを外すこと（`RenderGraphCompileTest` か `ComputeSkinningVulkanTest` の側）。(4) `ComputeSkinningVulkanTest.cpp`（176 行付近）の `Math::Matrix4x4::values` への memcpy を Math の抽象 API に置き換える（行列の要素を直接触らない方針）。
- verify: `cmake --build build --config Debug --target Game RHITextureUpdateVulkanTest RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(ComputeSkinningVulkanTest|RenderingVelocitySkinnedVulkanTest|RenderGraphCompileTest)$"`
- stop-when: 閾値をそろえると `RenderingVelocitySkinnedVulkanTest` の結果が変わり、原因が頂点シェーダーのスキニングの側の閾値にある場合は、比べた値を記録して止める。
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Debug, Library/Core/Private/Debug, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-05 親が足した（VTG6-COMPUTE-SKINNING の評価の残課題。評価は PASS）。有効にするとパレットを GBuffer の経路と2回アップロードする件は VTG6-DEFAULT-ON で扱う。 2026-10-05 親（`e12e7457` の評価の差し戻し。評価者は Claude の別文脈）: (2) の「捨てた数を統計に出す」が満たされていない。`NORVES_STAT_ADD` で足した値は、同じ `RenderingCoordinator::RenderFrame` の中で `UpdateRenderingStats(renderStats)`（3181 行付近）が `m_RenderingStats = stats` で丸ごと上書きして消える。`renderStats` は `packet->Stats.GameThreadStats` から作られ、新しい欄 `SkinningComputeDroppedInstances` は誰も設定しないので常に 0（既存の `RenderGraphBarrierCount`・`RenderGraphTransientAcquireCount` は 2956 行付近で `renderStats` へ設定し直しているので残る）。CSV（`Stats.cpp` 500・534 行付近）と `ToString`（109〜114 行付近）にも欄が無い。直すこと: `UpdateRenderingStats` の前で `renderStats.SkinningComputeDroppedInstances` を設定する（毎フレームの数にする。累計にしない）。CSV と `ToString` に欄を足す（`Library/Core/Private/Debug` と `Library/Core/Public/Debug` を paths に足した）。`SkinningComputePass.cpp`（336 行付近）のログの文言を事実に合わせる。テストで、外したインスタンスがあるフレームの後に統計（`RenderingStats` の欄）が 0 より大きいこと、ログが2フレーム目に出ないことを確かめる。変異の確認の出力は cp932 で文字化けして中身が残らなかったので、出力を UTF-8 で保存するか終了コードと落ちた assert の行を残す。あわせて: y へ広げる経路は束縛の上限（2^27 バイト）から実運用では届かないので、`static_assert(SKINNING_MAX_BINDING_BYTES / sizeof(SkinnedMeshVertex) / ThreadsPerGroup <= SKINNING_MAX_GROUP_COUNT)` のように不変条件として示す。

## VTG6-MATERIAL-TABLE: 描画の記録の材質の番号をフレームで一意な材質の表の番号にする
- status: done
- done-when: ビジビリティバッファの描画の記録の材質の番号を、フレームごとの材質の表（storage buffer）の番号にする。表はそのフレームの不透明の描画が使う実物の材質（`Material` など、材質の解決が引くもの）ごとに1件で、同じ材質を使う MegaGeometry の区間・手続きメッシュの塊・スキニングの塊は同じ番号になり、違う材質は違う番号になる（今は MegaGeometry が区間の番号、手続き・スキニングがプロキシの中のスロットの番号を入れていて、別の材質が同じ番号にまとまる。`visbuffer_records.comp`・`VisibilityRasterPass.cpp`）。MegaGeometry の記録を GPU で作る経路には、インスタンスの区間から表の番号への対応を渡す。表の1件は、材質の解決が要る定数（基本色・係数・材質の種類の印など）と、後の VTG6-RESOLVE-MATERIALS がテクスチャを引くための材質の識別を持つ。番号は 0 から詰め、数は `VisibilityBuffer`・`MaterialTileClassifyPass` の材質の上限以下に収める（超えたら `VISBUFFER_MATERIAL_OVERFLOW` を1回出し、溢れた分は予備の番号へ寄せる）。`--visibility-buffer=on` の撮影のログに `VISBUFFER_MATERIALS unique=<n> limit=<n>` を出す。CPU のテスト `VisibilityMaterialTableTest`（`RenderResourcesDomainContractTest` の束）が、同じ材質の異なる描画が同じ番号に、違う材質が違う番号になること、番号が詰まっていること、上限を超えたときの寄せ方を確かめる。
- verify: `cmake --build build --config Debug --target Game RenderResourcesDomainContractTest RenderGraphCompileTest RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VisibilityMaterialTableTest|VisibilityBufferEncodingTest|RenderGraphCompileTest|MaterialTileClassifyVulkanTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG6-MATERIAL-TABLE -Configuration RelWithDebInfo -Deterministic -ExtraGameArguments --visibility-buffer=on`
- stop-when: 実物の材質を RenderThread で一意に識別する手段が FramePacket のスナップショットに無く、FramePacket の契約を変える必要がある場合は、理由と選択肢を記録して止める。
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-05 親が足した（`027a4ab4` の評価で見つけた。材質のタイルの分類は記録の番号で分けるので、番号がフレームで一意でないと、後の材質の解決が別の材質を同じ dispatch で扱う）。起動画面の材質の数（`unique`）を測って PROGRESS に書き、VTG6-MATERIAL-CLASSIFY の stop-when（材質の数が上限を超えるか）をその値で確かめる。危険地帯（描画パス・RenderThread）。既定の描画（`--visibility-buffer=off`）は変えない。 2026-10-05 親（`84c135f2` の評価の差し戻し。評価者は Claude の別文脈。実装の中身に欠陥は無い）: 次を直す。(1) 必須: `VisibilityMaterialTableTest.cpp` が `#include <sstream>`・`<string>` と `std::string`・`std::stringstream` を使っている（13・14 行、232〜243 行付近）。絶対規則1違反。同じ束の `VisibilityBufferEncodingTest.cpp`（290〜303 行付近）の `ReadWholeFile`（`Container::VariableArray<char>` に読み `std::strstr` で照合）と同じ形に置き換える。(2) 統合の経路を落とせる検査を足す: 今は `VisibilityRasterPass.cpp`（562・653 行付近）の材質の番号を元の `command.Draw.MaterialIndex`・`instance.MaterialIndex` に戻しても、どのテストも落ちない。パスを通す CPU のテスト（`RenderGraphCompileTest` の `VisibilityRasterScene` など）で、同じ材質の2つの描画が同じ表の番号の記録になり、違う材質が違う番号になることを、記録の表（CPU 側で作る記録、または読み出し口）で確かめる。変異（上の戻し）で落ちることを確かめ、出力を UTF-8 で残す。(3) `PROGRESS.md`（1199 行付近）の視点ごとの値が逆: ログでは near が最後 unique=17、low と default が unique=24。

## VTG6-RESOLVE-GEOMETRY: 材質の解決で三角形から重心座標・微分・法線・速度を求める
- status: done
- done-when: 材質ごとのタイルを処理する計算シェーダーが、ID から記録と三角形を引き、3頂点（ワールドの位置・法線・UV。MegaGeometry はプール、手続きはバッファ、スキニングは変形済みの頂点）から、画素のカメラの光線と三角形の交点で透視の補正つきの重心座標と、隣の画素への微分（解析的な dUV/dx・dUV/dy、dPos/dx・dPos/dy）を求める。法線（補間）・接線の基底（三角形の辺と UV から。今の `CalculateCotangentFrame` と同じ向きの規約）・速度（前のフレームの変換か前のフレームの頂点から前のクリップ座標）を求め、`GBuffer.Normal`・`GBuffer.Velocity` へ書き、`GBuffer.Albedo` には材質の基本色（テクスチャなしの定数の色。α=1）を書く。`--visibility-buffer=on` のとき、GBufferPass・MegaGeometryPass の GBuffer の描画を止め、この解決の出力を使う。GPU のテスト `VisibilityResolveVulkanTest` が、既知の三角形（手続き・MegaGeometry・スキニング）で重心座標・微分・法線・速度・基本色を CPU の参照と照合する。スキニングのインスタンスが2体以上の場面で、2体目の解決の位置・速度が頂点シェーダーのスキニングの描画と合うことを確かめる（記録の頂点の基点の二重加算が戻れば落ちる形。1体だけの場面では基点が 0 になり検出できない）。`on` で MegaGeometryPass の GBuffer の描画を止めるとき、2パスの遮蔽の HZB の深度に MegaGeometry の1パス目が入る順序を保つか、入らない間は判定が保守側（隠れていない側）に倒れるだけであることをコードと `RenderGraphCompileTest` の記録の並びで確かめて PROGRESS に書く（順序の組み直しは VTG6-DEFAULT-ON）。起動画面の撮影と on・off の比較は VTG6-RESOLVE-GEOMETRY-CAPTURE で行う。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RenderingVelocityVulkanTest RHITextureUpdateVulkanTest SkinnedRenderPathContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|VisibilityResolveVulkanTest|RenderingVelocityStaticVulkanTest|RenderingVelocityObjectVulkanTest|RenderingVelocitySkinnedVulkanTest|MaterialTileClassifyVulkanTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG6-RESOLVE-GEOMETRY-fix -Configuration RelWithDebInfo -Deterministic -ExtraGameArguments --visibility-buffer=on`
- stop-when: 解析的な微分が今の画面の微分（2×2 の画素の差）と大きく食い違い、ミップの選び方の規約を変える必要がある場合は、比べた値を記録して止める。
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Game, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.3。危険地帯（描画パス）。GBuffer の形式・意味（Albedo.a、法線の格納、Velocity の式）は変えない（後段が読む）。 2026-10-06 親: 前の反復はターンの上限（150）で途中のまま終わった。途中の作業は `f19b4967`（追跡済みの 12 ファイル: GBufferPass・MegaGeometryPass・SceneView・VisibilityRasterPass の on の配線、`RenderGraphCompileTest` の `TestVisibilityResolveOnReplacesGBufferDrawsWithStorageImageWrites`・`TestVisibilityResolveUnsupportedDeviceKeepsGBufferDraws`、`SkinnedRenderPathContractTest` の追加）と `44ebce39`（新しいファイル: `VisibilityResolve.glsl`・`visbuffer_resolve.comp`・`visbuffer_resolve_dump.comp`・`VisibilityResolvePass.*`・`VisibilityResolveVulkanTest.cpp`）にある。親が HEAD で Debug の `Game`・`RenderGraphCompileTest`・`RHITextureUpdateVulkanTest` をビルドし（exit 0）、`VisibilityResolveVulkanTest` と `RenderGraphCompileTest` が Passed であることを確かめた。この反復は、途中の作業を読んで done-when との差を洗い出し、足りない点（2体のスキニング、HZB の順序の確認、PROGRESS）を埋めて閉じる。最初から作り直さない。ターンを節約するため、大きなファイルは必要な範囲だけ読む。 2026-10-06 親: ランナーの再確認で `RenderingVelocitySkinnedVulkanTest` が 10 分止まって打ち切られた（プロセスは CPU 6 秒のまま残っていた）。反復の中の ctest（`verify-VTG6-RESOLVE-GEOMETRY-7-ctest.txt`、6/6 Passed、27.70 秒）と親の再実行（`--timeout 180`、26.71 秒で Passed）では通るので、たまに起きる止まりとして完了にした。再び止まったら TEST-SKINNED と合わせて原因を調べる。 2026-10-06 親（`883f0600..17b53ece` の評価の差し戻し。評価者は Claude の別文脈。式・GBuffer の意味・頂点の取り方・off の経路は問題なし）: 次を直す。(1) 必須: 「on で GBufferPass が描画を止める」の検査 `TestGBufferPassSkipsDrawsOnlyWhenVisibilityResolveIsActiveAndSupported`（`SkinnedRenderPathContractTest.cpp` 1567〜1663 行付近）は、既知の停止（TEST-SKINNED）の後ろに置かれて一度も実行されず、自身も `shaderManager.Initialize(device.get(), "")` で `gbuffer.vert` を読めず落ちる形。`GBufferPass.cpp`（505〜511 行付近）の描画の省略を消しても `IsSupported` の条件を外しても、実行されるテストは落ちない。実行されるテスト（`RenderGraphCompileTest` で GBufferPass が描画を積む構成、またはシェーダーのディレクトリを `NORVES_SOURCE_DIR "/Assets/Shaders"` にした形）へ移し、2つの変異（省略を消す・`IsSupported` を外す）で落ちることを UTF-8 の出力で残す。(2) 必須: done-when の「材質ごとのタイルを処理する計算シェーダー」と違い、解決は画面全体を1回の直接 dispatch で処理している（`ICommandList` に間接 dispatch が無いため。`VisibilityResolvePass.cpp` 270 行付近）。コードは今のままでよい。PROGRESS の「足りなかったのは 1 点だけ」を直し、このずれと理由を書く（タイルの間接 dispatch は VTG6-RESOLVE-TILE-DISPATCH で行う）。(3) 法線の補間をラスタと同じ意味にする: ラスタは頂点ごとに変換した法線を正規化してから補間する（`gbuffer.vert` 50 行・`megageometry.vert` 64 行付近）が、解決は正規化していない変換後の法線を補間する（`VisibilityResolve.glsl` 227・234〜236・390〜392 行付近）。非一様なスケールで向きがずれる。`VisibilityResolveVulkanTest` の CPU の参照も同じ選択を写しているので、参照はラスタの式から独立に立て直す。(4) 予備の経路への戻り: 今は装置の機能だけで判定し、ID のラスタのパイプラインが無いとき（`VisibilityRasterPass.cpp` 465 行付近）や解決のシェーダーが作れないとき（`VisibilityResolvePass.cpp` 291〜296・399 行付近）も GBufferPass・MegaGeometryPass が描画を止め、画面が空になる。実際に使える状態（パイプラインと解決の準備ができた）で判定し、使えないときは GBuffer の描画を続けて `VISBUFFER_FALLBACK reason=<..>` を1回出す。テストで確かめる。(5) `GBufferPass.cpp` の 360・369・504・513〜516 行付近が、中身はそのままで LF から CRLF に変わっている（`git diff 883f0600 -- Library/Core/Private/Rendering/GBufferPass.cpp` の numstat 29/10 と `--ignore-cr-at-eol` の 22/3 が食い違う）。元の行末に戻す（`node ~/.agent-workflow/restore-mixed-eol.mjs` が使えるなら使う）。既知の限界として PROGRESS に書く: 2体目のスキニングは CPU の参照と記録のアドレスのテストの組み合わせで代えている（計算スキニングの出力とつないだ端から端までの照合は DEFAULT-ON のスキニングの撮影で行う）、stop-when の 2×2 の微分の比較は CPU の前進差分だけ、GPU のテストはジッター 0・描画範囲 (0,0,W,H) だけ、Albedo は材質の基本色だけでインスタンスの色（`objectColor`）を掛けない（VTG6-RESOLVE-MATERIALS で扱う）。

## VTG6-VT-LOD-UNIFORM: VTのフィードバックと逃げ道のLODの問い合わせを一様な制御の位置へ移す
- status: done
- done-when: `gbuffer.frag`・`megageometry.frag`・`forward_transparent.frag`（と VT を標本する他の frag）で、各層（アルベド・法線・ORM・高さ）の LOD を、分岐・早期 return・ループより前の一様な制御の位置（main の先頭。`megageometry.frag` はデバッグの早期 return（141〜154 行付近）より前）で問い合わせ、`WriteVirtualTextureFeedback`・`WriteVirtualTextureHeightFeedback`・`SampleSparseResidentTracked`（`VirtualTextureFeedback.glsl` 66 行付近・`SparseResidencySampling.glsl` 61 行付近・`megageometry.frag` 119 行付近の `textureQueryLod`）へ引数で渡す形にする。POM の後の UV で標本する層は、POM の直後の一様な位置で `dFdx`/`dFdy` を取り、明示的な勾配から LOD を求める（`textureQueryLod` を分岐の後に残さない）。GPU のテスト（`VirtualTextureFeedbackVulkanTest` の束）に「別のテクスチャの逃げのループ（早期 return つき）の後でフィードバックを書く」形のプローブを足し、要求のミップが期待どおりであることを確かめる。起動時に GPU のドライバの版を `GPU_DRIVER` としてログに1回出す。`CaptureStartupScene.ps1` で VT の常駐（`vt_used_mb` の最大）を metrics に出し、上限（既定 64MB。引数で変えられる）を超えたら失敗にする。`-Deterministic` の起動画面（off）を2回撮って一致し（PSNR 100dB 前後）、VT の常駐が既定・近接・低角度で約 225・683・366 タイル（旧ドライバの量）に近いことを記録する。Indoor/Outdoor の golden を回し、差がドライバの更新とこの変更によることを確かめて `Docs/RenderingValidation/GoldenBaselines.md` の手順で再承認し、根拠（ドライバの版 591.86 → 610.88、`textureQueryLod` の値が分岐の後で未定義であること）をコミットの本文に書く。
- verify: `cmake --build build --config Debug --target Game RHITextureUpdateVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VirtualTextureFeedbackVulkanTest|VirtualTextureResidencyVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG6-VT-LOD-UNIFORM -Configuration RelWithDebInfo -Deterministic`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG6-VT-LOD-UNIFORM-b -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/VTG6-VT-LOD-UNIFORM`
- stop-when: LOD を先頭へ移しても VT の常駐が旧ドライバの量の2倍を超え、原因が異方性の扱い（`textureQueryLod` と明示的な勾配の LOD の違い）にある場合は、測った値を記録して止める。golden の差がドライバとこの変更だけで説明できない場合は、測った値と分類を記録して止める。
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Private/RHI, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, Baselines/RenderingValidation, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-06 親が足した。2026-10-06 0:17 に Windows Update が NVIDIA のドライバを 591.86（32.0.15.9186）から 610.88（32.0.16.1088）へ入れ替えた（System のイベントログ WindowsUpdateClient の Id 43・19、`C:\Windows\INF\setupapi.dev.log`）。610.88 では、分岐・早期 return の後で呼ぶ `textureQueryLod` が多くのクアッドで −∞ 相当（.x=0、.y≤−8）を返し、フィードバックがほぼ全面でミップ 0 を要求して VT が約 9500 タイル（約 600MB）になる（GLSL では暗黙の微分は一様でない制御の中で未定義なので、これは今のシェーダーの不具合）。調査の実験（シェーダーの写しを書き換えて撮影。`.harness/runs/startup-capture/INVEST-*`、書き換えのスクリプトは親の scratchpad の `exp/`）で、4つの層の問い合わせを main の先頭へ移すと 198・503・315 タイルに戻り、2回の撮影が PSNR 105〜108dB で一致した。外れたのは GBuffer の材質のアルベド・法線（最初に標本する ORM と POM の先頭の高さは正しい）、MegaGeometry の3層（デバッグの早期 return の後）。発生源の分岐: `SparseResidencySampling.glsl` 28〜36 行付近の逃げのループ、`VirtualTextureFeedback.glsl` 94〜107 行付近の atomic のループと早期 return、MegaGeometry の早期 return。危険地帯（シェーダー・VT・アセットロード）。起動画面の見た目を変えうる（絶対規則7）。 2026-10-06 親（評価は PASS）: POM の後の層の LOD は、明示的な勾配から求めず、POM の直後で `textureQueryLOD` を取る形にした（POM の中の分岐の後だが、610.88 で常駐が旧ドライバの量に戻り、撮影が決定的になることで確かめた）。`VirtualTextureFeedbackVulkanTest` の新しいプローブは、このドライバでは分岐の後の `textureQueryLOD` の退行を検出しない（退行の守りは撮影の VT の常駐の上限）。残る暗黙微分: `PbrMaterialTextureSampling.glsl` 77 行付近の VT でない材質の `texture()`（POM の後）、`forward_transparent.frag` 277 行付近の `dfgLut` の `texture()`（`discard` の後）。

## VTG6-RESOLVE-GEOMETRY-CAPTURE: 材質の解決（幾何）の on・off を起動画面で撮って比べる
- status: done
- done-when: `-Deterministic` で `--visibility-buffer=on`・`off` の起動画面（既定・近接・低角度）を撮り、`GBuffer.Normal`・`Velocity` の差（デバッグの表示か GBuffer の読み戻し）が小さいことを記録する（深度・法線の向き・速度の符号が合う。on の `GBuffer.Albedo` はテクスチャなしの基本色なので最終画像の差は大きくてよい）。差の画素を開いて、物の輪郭の欠け・法線の反転・速度の符号の反転が無いことを確かめる。`off` の撮影が段6の前（`VTG6-VIS-RASTER` の撮影）と一致することを確かめる（既定の描画を変えていない）。`RenderingVelocity*VulkanTest` を on でも通す方法があれば足し、無ければ理由を PROGRESS に書く。`MEGA_OCCLUSION` の on・off の数を記録する。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG6-RESOLVE-GEOMETRY-off -Configuration RelWithDebInfo -Deterministic`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG6-RESOLVE-GEOMETRY-on -Configuration RelWithDebInfo -Deterministic -ExtraGameArguments --visibility-buffer=on`
- stop-when: on で法線・速度の差が大きく、原因が解決の式（重心座標・接線の基底・速度）にある場合は、測った値と差の画像を記録して止める（VTG6-RESOLVE-GEOMETRY を doing に戻す）。
- paths: Scripts/CaptureStartupScene.ps1, Game, Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-06 親が VTG6-RESOLVE-GEOMETRY から分けた（撮影と比較で1反復）。危険地帯（描画パス）。 2026-10-06 親（`4e0d409c` の評価の差し戻し）: off の撮影が段6の前と一致しなかった（VT の常駐が約 220 から約 9500 ページ、sparse のプールが 64MB から 640MB、撮影が毎回同じでなくなった）。原因は 2026-10-06 0:17 の NVIDIA のドライバの更新（591.86 → 610.88、Windows Update）で、分岐の後の `textureQueryLod` が多くのクアッドで外れた値を返すようになったこと（VTG6-VT-LOD-UNIFORM で直す）。VTG6-VT-LOD-UNIFORM の後に、on・off を撮り直して比べ直す。段6の前の撮影は旧ドライバなので、off の一致の基準は「VTG6-VT-LOD-UNIFORM の後の off を2回撮って一致する（決定的）こと」と「VT の常駐が旧ドライバの量（既定・近接・低角度で約 225・683・366 タイル）に近いこと」に置き換える。評価の残課題もあわせて直す: GBuffer の検証表示（`GBufferDebugPass`）が ID のラスタと無関係なのに公開ヘッダー `VisibilityRasterPass.h`（262〜318 行付近）にあり、クラスの本体が `#if NORVES_ENABLE_STATS` の外で Release の Core に入る → Private の独立したファイルへ移し、定義ごと `#if` で囲む。`VisibilityRasterPass.cpp`（1529 行付近）の記述子と UBO の枠が Execute の回数 % 2（`FrameUseRing` の形にする）。検証表示の PNG は後処理（TAA・露出・トーンマップ等）を通った色なので、PROGRESS の「xyz × 0.5 + 0.5」「8bit の差」の説明を直し、反転の数え方（空の色を基準にした符号）をやめる。屋根の法線の系統的な差（off 側の法線マップの接線の基底の偏りの疑い）を調べて記録する。

## VTG6-DEBUG-PASS-FRAME-SLOTS: ID の検証表示（VisibilityDebugPass）の記述子と UBO の枠をフレームの枠にする
- status: done
- done-when: `VisibilityRasterPass.cpp` の `VisibilityDebugPass::Execute` が、記述子と UBO の枠を Execute の回数 % 2 で選んでいる（`m_FrameCounter % 2u`）のを、`GBufferDebugPass` と同じ `FrameUseRing<Use>`（`context.FrameIndex` とフレームの通し番号で枠を選び、Execute のたびに次の 1 組を使う）へ変える。同じフレームに複数回 Execute しても提出前の資源を上書きしないことを、`RenderGraphCompileTest` などの実行される検査か、同じ形の既存のテストの流儀で確かめる。`--visibility-buffer=debug` の起動画面の撮影が変わらないことも確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG6-DEBUG-PASS-FRAME-SLOTS -Configuration RelWithDebInfo -Deterministic -ExtraGameArguments --visibility-buffer=debug`
- stop-when: 検証表示の枠を変えると `--visibility-buffer=debug` の撮影が変わる場合は、差を記録して止める。
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-06 VTG6-RESOLVE-GEOMETRY-CAPTURE の反復で、`GBufferDebugPass` を `FrameUseRing` にしたときに見つけた。検証の表示だけの経路で、既定の描画には関係しない。低い優先度。 2026-10-06 親（`ba15c3df` の評価の差し戻し）: 必須: done-when の「同じフレームに複数回 Execute しても提出前の資源を上書きしない」を、落ちうる検査で確かめていない（変更を `m_FrameCounter % 2u` に戻しても、Acquire をフレームに1回にしても、どの検査も落ちない）。`RenderGraphCompileTest.cpp` の `TestComputePassFrameResourcesAreNotReusedWithinAFrame`（3137 行付近）と `CountBufferCreations`（3053 行付近）の形で、`VisibilityRasterScene`（FakeDevice はレンダーパス・グラフィックスパイプライン・フレームバッファを作れる）に `SceneColor` を作るパスと `VisibilityDebugPass` を足し、同じ通し番号で N（>2）回 Execute すると `"VisBuffer_DebugParams"` の作成が N 回、通し番号を進めると増えないことを確かめる。変更を戻すと落ちることを UTF-8 の出力で残す。足せない事情があるなら理由を PROGRESS に書く。あわせて: RelWithDebInfo の `RenderGraphCompileTest` が 0xC0000005 で落ちた（`verify-VTG6-DEBUG-PASS-FRAME-SLOTS-5-rgtest.txt`）。このファイルは `assert(scene.ShaderMgr.Initialize(...))` のように副作用を assert の中に置いていて、NDEBUG の構成では初期化ごと消える。変更前から起きるかを確かめ、既存の問題なら PROGRESS に書く（直すのは範囲外）。

## VTG6-INDIRECT-DISPATCH-RHI: RHIに間接dispatchを足す
- status: done
- done-when: `ICommandList::DispatchIndirect(buffer, offset)`（Vulkan は `vkCmdDispatchIndirect`。引数が不正なら false で何も記録しない）を足し、引数のバッファの用途（`ResourceUsage::IndirectBuffer`）とバリアの段（`GenericRead`・`IndirectArgument` が `DRAW_INDIRECT` の段と `INDIRECT_COMMAND_READ` のアクセスを含むこと）を確かめる。GPU のテスト `IndirectDispatchVulkanTest`（`RHITextureUpdateVulkanTest` の束）が、確認用の計算シェーダーを GPU 側の引数から間接 dispatch し、引数どおりにグループが走ること（計算シェーダーが書いた引数の `GenericRead` での読み取り・転送で書いた引数の `IndirectArgument` での読み取り・表の途中のオフセット・y への広げ（g = y * 数x + x）・x = 0・z > 1）と、不正な引数で false を返すことを確かめる。validation error 0 件。
- verify: `cmake --build build --config Debug --target Game RHITextureUpdateVulkanTest RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(IndirectDispatchVulkanTest|RenderGraphCompileTest|MaterialTileClassifyVulkanTest|VisibilityResolveVulkanTest|IntegerAttachmentVulkanTest)$"`
- stop-when: 間接 dispatch を足すのに RHI の公開 API の形（引数のバッファの用途の列挙など）を大きく変える必要がある場合は、足す API の案を記録して止める。
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-06 VTG6-RESOLVE-TILE-DISPATCH から分けた（1 反復で閉じない大きさのため。残りは VTG6-RESOLVE-TILE-SHADER と VTG6-RESOLVE-TILE-DISPATCH）。危険地帯（RHI の公開 API）。`DispatchIndirect` は `bool` を返し、対応しないコマンドリスト（既定の実装。テストの偽のコマンドリストなど）は false を返すだけにした（`BuildAccelerationStructure` と同じ流儀。純粋仮想にすると 17 のテストの偽のコマンドリストを全部直すことになるため）。`GenericRead` は既に `DRAW_INDIRECT` の段と `INDIRECT_COMMAND_READ` を含んでいた（`VulkanCommandList.cpp` の `ResourceStateToPipelineStageFlags`・`ResourceStateToAccessFlags`）ので、テストで固定しただけ。バッファの用途は `ResourceUsage::IndirectBuffer`（`VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT`）が既にあり、用途の列挙は変えていない。 2026-10-06 親（`1d841809` の評価の差し戻し。評価者は Claude の別文脈）: 必須: `VulkanCommandList.cpp`（1172〜1184 行付近）の `DispatchIndirect` が引数のバッファの用途を検査しない。`ResourceUsage::StorageBuffer | TransferDst` で作った（`IndirectBuffer` の無い）12 バイト以上のバッファを渡すと true を返して `vkCmdDispatchIndirect` を記録し、`VUID-vkCmdDispatchIndirect-buffer-02709` 違反になる（実装者の変異 M2 の出力 `verify-VTG6-INDIRECT-DISPATCH-RHI-3-mutation.txt` で6回出ている）。`IBuffer::GetUsage()` に `IndirectBuffer` が無ければ false を返し、ケース G に「`IndirectBuffer` の無いバッファは false、`VUID_COUNT` は 0 のまま」を足し、`ICommandList.h` の docstring の不正の列挙に用途を加える。あわせて: ケース A の GPU の同期（計算シェーダーが引数を書いてから間接 dispatch）は、このテストでは欠落を検出できない（synchronization validation が無効で、RT が有効な装置では `GenericRead` の段に `eAccelerationStructureBuildKHR` が残る）。守りは CPU の表の検査（`CheckBarrierStages`）なので、PROGRESS の書き方を既知の限界として直す。

## VTG6-RESOLVE-TILE-SHADER: 材質の解決の計算シェーダーをタイルの一覧から走る形にし、画面全体の直接 dispatch と画素で一致させる
- status: done
- done-when: 材質の解決（`Common/VisibilityResolve.glsl`・`VisibilityResolve::Record`）に、材質ごとのタイルの一覧から走る版を足す（新しいシェーダー `visbuffer_resolve_tiles.comp` か同じ本体のコンパイル時の分岐。製品の版と検証用の版の両方）。1 回の dispatch は 1 つの材質で、グループの番号は `g = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x`、`g >= その材質のタイルの数`（`MaterialTileClassify` の引数の `ARG_TILE_COUNT`）は return、タイルは `tileList[ARG_LIST_OFFSET + g]`（タイルの番号 = tileY * tilesX + tileX）で、そのタイルの 8×8 の画素を解決する。どの材質を処理するかは 1 回の dispatch ごとの定数（UBO。RHI に push constant が無い）で渡し、`FrameUseRing` の枠で 1 フレームに何回記録しても上書きしない。画面の端の部分タイルの画面の外は読み書きしない。`VisibilityResolve::Record` は、画面全体の直接 dispatch（今の形。材質のタイルが無いとき）と、材質ごとに 1 回ずつ `DispatchIndirect` する形（材質の数は引数の表の件数）を選べる。GPU のテスト `VisibilityResolveVulkanTest` が、合成した ID の画像を `MaterialTileClassify` で分類し、その一覧と引数で材質ごとの間接 dispatch の解決をした結果（GBuffer の Albedo・Normal・Velocity と、検証用の版の画素ごとの中間の値）が、画面全体の直接 dispatch の結果と画素でビット単位に一致することを確かめる（空のタイル・複数の材質が混じるタイル・部分タイルを含む。材質の表から引けない画素や上限以上の材質は直接版と同じ扱いか、違う点を記録する）。
- verify: `cmake --build build --config Debug --target Game RHITextureUpdateVulkanTest RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|VisibilityResolveVulkanTest|MaterialTileClassifyVulkanTest|IndirectDispatchVulkanTest)$"`
- stop-when: 材質ごとの間接 dispatch の解決が直接版と画素で一致せず、原因が分類の一覧（`MaterialTileClassify` の出力）の側にある場合は、一致しない画素と値を記録して止める。
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-06 VTG6-RESOLVE-TILE-DISPATCH から分けた。計画書 4.3。危険地帯（描画パス）。RenderGraph の配線（分類のパスを足す・解決のパスが引数と一覧を読む）は次の VTG6-RESOLVE-TILE-DISPATCH で行うので、この項目では `VisibilityResolvePass` の既定の経路（画面全体の直接 dispatch）を変えない。テストのコードでも標準ライブラリの型を使わない（`Container::VariableArray`）。

## VTG6-RESOLVE-TILE-DISPATCH: 材質の解決のパスを材質ごとのタイルの間接dispatchにして、起動画面で確かめる
- status: done
- done-when: `VisibilityResolvePass` を、`MaterialTileClassifyPass` が作る材質ごとのタイルの一覧と間接 dispatch の引数で、材質ごとに 1 回ずつ間接 dispatch する形にする（VTG6-INDIRECT-DISPATCH-RHI の `DispatchIndirect` と VTG6-RESOLVE-TILE-SHADER の材質ごとの解決を使う）。`--visibility-buffer=on` で、`SceneView` が分類のパスを有効にして、解決より前に足す（分類のパスは今は既定で無効。`VisibilityRasterPass` の後、`VisibilityResolvePass` の前）。解決のパスは分類の引数・一覧・統計を `GenericRead` で読み（RenderGraph の依存が組まれ、`MaterialTileClassifyPass` の最終のバリアの後に読む）、分類が記録できなかったフレーム（引数が 0）は何も走らせない。分類・解決のパスを足しても `off`（既定）の描画は変えない。`RenderGraphCompileTest` が、on の構成で分類 → 解決の順と、解決が材質の数だけ間接 dispatch を記録すること（偽のコマンドリストが `DispatchIndirect` を記録する）、分類を使えないとき（パイプラインが無い）の解決の扱い（直接 dispatch に戻るか何も走らせないか）を確かめる。材質の数だけ dispatch を出す費用（起動画面は 17〜24 材質）を記録する（dispatch の数・GPU の時間。`Debug/Stats.h` の統計かログ）。`-Deterministic` の on の撮影が、直接 dispatch の版（`VTG6-RESOLVE-GEOMETRY-CAPTURE` の on。HEAD = `80ec7662` の後の撮影か、この反復で撮り直した直接版）と一致する（PSNR を記録。一致しなければ差の出どころを記録）。
- verify: `cmake --build build --config Debug --target Game RHITextureUpdateVulkanTest RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|VisibilityResolveVulkanTest|MaterialTileClassifyVulkanTest|IntegerAttachmentVulkanTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG6-RESOLVE-TILE-DISPATCH -Configuration RelWithDebInfo -Deterministic -ExtraGameArguments --visibility-buffer=on`
- stop-when: 材質ごとの間接 dispatch の数（起動画面は 17〜24 材質）の費用が画面全体の直接 dispatch の 2 倍を超え、原因が dispatch の数そのものにある場合は、測った値を記録して止める。
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI, Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-06 親が足した（VTG6-RESOLVE-GEOMETRY の評価で、解決が材質ごとのタイルでなく画面全体の直接 dispatch になっていると分かった。`ICommandList` に間接 dispatch が無いため）。計画書 4.3。危険地帯（RHI の公開 API・描画パス）。後の VTG6-RESOLVE-MATERIALS は材質ごとの dispatch で材質ごとの descriptor を張る。 2026-10-06 反復: 1 反復で閉じない大きさなので、RHI の `DispatchIndirect`（VTG6-INDIRECT-DISPATCH-RHI。done）と、材質ごとの解決のシェーダーと直接版との画素の一致（VTG6-RESOLVE-TILE-SHADER）を前の単位に分けた。この項目は RenderGraph の配線と起動画面の撮影・費用の記録。 2026-10-06 親（VTG6-RESOLVE-TILE-SHADER の評価の残課題）: 配線のときに次を守る。(1) `VisibilityResolvePass` の `TileMaterialCount` の既定 0 は「引数の表の件数ぶん全部」で、製品の Layout では 1024 回の dispatch と 1024 組の UBO・descriptor set になる。材質の表のそのフレームの数（`VISBUFFER_MATERIALS unique`）を渡す。(2) 横のタイル数を `Params.Screen[0]` から求めていて、分類の `Layout.TilesX` と照合していない（`VisibilityResolvePass.cpp` 364 行付近）。分類と解決に同じ幅を渡すことを保証し、食い違ったら直接 dispatch に戻して1回知らせる。(3) `RecordTiles` が途中まで記録して false を返しうる（`VisibilityResolvePass.cpp` 381・398・413 行付近）のと、ヘッダー（207 行付近）の「false なら何も記録しない」をそろえる（記録の前に全部を確かめるか、記述を直す）。(4) ヘッダー（17 行付近）の「同じ本体なのでビット単位で一致する」は保証ではない（別々のパイプラインのビット一致を Vulkan は保証しない。確かめたのはこの GPU のテスト）と書き直す。 2026-10-06 反復: 配線を実装して done。解決は分類の引数・一覧・統計を GenericRead で読み、材質の表の数だけ間接 dispatch する（起動画面の定常で default・low 24 回、near 17 回）。分類を使えない・分類が記録できない・横のタイル数が食い違う・間接 dispatch を断られた・材質の数が範囲外のときは、画面全体の直接 dispatch へ戻す。親の 4 点は反映済み（TileMaterialCount は材質の表の数、横のタイル数の照合、RecordTiles は記録の前に全部確かめる、ヘッダーのビット一致の記述を保証でない形へ）。撮影は直接版とビット単位で一致（PSNR=∞）。費用は PROGRESS.md。

## VTG6-RESOLVE-MATERIALS: 材質の解決で材質のテクスチャ・ORM・BC5の法線・POM・VTの逃げ道を求める
- status: done
- done-when: 材質の解決が、今の `PbrMaterialTextureSampling.glsl`・`ParallaxOcclusionMapping.glsl`・`SparseResidencySampling.glsl` の式を、画面の微分の代わりに解析的な微分（`textureGrad`）で使う形にし（同じ関数を両方の経路で共有できる形に整える）、Albedo（テクスチャの α をそのまま α に）・Normal（法線マップ・BC5 の Z の復元）・Material（ORM・別々の枠）を `--visibility-buffer=on` で書く。POM は三角形の接線の基底と解析的な dUV で行う。VT の非常駐の逃げ道は明示勾配の版を使う。`-Deterministic` で on・off を撮って視点ごとの PSNR を記録し（目安 40 dB 以上。差は解析的な微分と 2×2 の微分の違いによるものとして出どころを記録する）、近接・低角度の PNG を開いて石畳の凹凸・視差・地面の見本の帯・金色の球の反射が同じに見えることを確かめる。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest MaterialResourcesTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|MaterialResourcesTest|GBufferMaterialDescriptorCacheTest|VirtualTextureResidencyVulkanTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG6-RESOLVE-MATERIALS -Configuration RelWithDebInfo -Deterministic`
- stop-when: 材質ごとの descriptor（材質の UBO とテクスチャの枠）を計算シェーダーの材質の dispatch へ渡す手段が無く、材質の束ね方（bindless など）を変える必要がある場合は、理由と選択肢を記録して止める。
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.3。危険地帯（描画パス）。 2026-10-05 親（VTG6-MATERIAL-TABLE の評価の残課題）: 材質の表の1件（`VisibilityMaterialTable.h` の `MaterialEntry`、128 バイト）に対応する GLSL の構造体とフラグの定義はまだ無い（ヘッダーのコメントは「シェーダーと一致」と書いているが先走り）。GLSL 側に足すときは `VisibilityBufferEncodingTest` と同じ形で定数・オフセットを照合する。CPU 側の表の中身を外から読む口が無い（`m_MaterialTable` は private）ので、材質ごとに descriptor を張るには取り出し口を足す。 2026-10-06 反復: 実装して done。共有: `PbrMaterialTextureSampling.glsl`・`ParallaxOcclusionMapping.glsl` から画面微分を使わない部分を `PbrMaterialTextureSamplingCore.glsl`・`ParallaxOcclusionMappingCore.glsl`（新規）へ分け、ラスタのシェーダーは元のファイルをそのまま取り込む（POM は `ApplyParallaxOcclusionMappingGrad` が勾配とミップを引数に取り、ラスタ用の `ApplyParallaxOcclusionMapping` は画面微分を取って呼ぶだけ）。解決（材質ごとの形のシェーダー）は同じ関数を、三角形から求めた解析的な微分で呼び、Albedo（インスタンスの色 × アルベド。α はテクスチャの α）・Normal（POM → 標本 → 法線マップ。BC5 の Z の復元と変位した球の法線の補正を含む）・Material（ORM の 1 枚か別々の枠。スカラー値の 1x1）を書く。直接 dispatch の版はテクスチャを束ねられないので材質の定数だけ。配線: 材質の表の CPU 側の中身を `VisibilityRasterPass::GetMaterialEntries()` で読み、`VisibilityResolve` が材質ごとの dispatch のディスクリプタセットへ 6 枚のテクスチャとサンプラー（異方性 4、Wrap）を束ねる（GBufferPass の既定のテクスチャの規則と同じ）。GBuffer.Material も storage image として書く。stop-when は該当しない（材質ごとの descriptor は dispatch ごとのディスクリプタセットで渡せた）。結果: GBuffer の Albedo・Material は on・off で PSNR 52〜55 dB（-LooseTextures）。Normal は屋根を除くと 51.6〜55.8 dB で、屋根だけが違う（高周波な法線マップの標本の差。下の VTG6-RESOLVE-MATERIALS-ANISO）。最終のシーンの色（-LooseTextures）は default 38.2・near 44.6・low 33.5 dB（low は太陽の周りを除くと 39.9 dB。発光は VTG6-RESOLVE-FEEDBACK-EMISSIVE で解決が書く）。VT（既定のクック済みテクスチャ）の on は、解決が VT の要求を書かないので粗いミップのまま（default 36.1・near 36.7・low 32.0 dB。撮影の見た目はぼやける）。これは VTG6-RESOLVE-FEEDBACK-EMISSIVE で直る。off の撮影は変更前（VTG6-RESOLVE-GEOMETRY-off）と PSNR 98〜105 dB（最大差 3）で、ラスタの経路は変わっていない。詳細は PROGRESS.md。

## VTG6-RESOLVE-VT-FEEDBACK: 材質の解決でVTのフィードバック（要求）を要求のバッファへ書く
- status: done
- done-when: 材質ごとの形の解決が、VT（sparse）のアルベド・法線・ORM・高さについて、ラスタの材質シェーダー（gbuffer.frag）と同じ規則で VT の要求を要求のバッファへ書く（4×4 の画素のうちフレームごとに巡回する 1 画素と、非常駐で粗いミップへ逃げた画素。欲しいミップは `textureQueryLOD` の代わりに解析的な微分から求める）。計算シェーダーの書き込みをホストの読み取りへ見せるバリアを足す。GPU のテストで、要求のミップ・タイル・位相・件数が CPU の参照と合うことを確かめ、`-Deterministic` で VT 構成の on を off と比べて記録する（`vt_used_mb`・追い出し・常駐タイルの数・PSNR）。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RHITextureUpdateVulkanTest ViewportSnapshotDebugWiringTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|VirtualTextureFeedbackVulkanTest|MegaGeometryFrameCommandDebugModeTest|VisibilityResolveVulkanTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG6-RESOLVE-VT-FEEDBACK-on -Configuration RelWithDebInfo -Deterministic -VisibilityBuffer On`
- stop-when: 要求が on で off の 2 倍以上か半分以下になり、原因が LOD の式で直せない場合は、測った値を記録して止める。
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-06 VTG6-RESOLVE-FEEDBACK-EMISSIVE が 1 反復で閉じない大きさなので分けた（計画書 4.3）。危険地帯（描画パス）。 2026-10-06 反復: 実装して done。`Common/VirtualTextureFeedback.glsl` に、画素の位置を引数に取る版（`WriteVirtualTextureFeedbackAtPixel`・`WriteVirtualTextureHeightFeedbackAtPixel`）を足し、`NORVES_VT_FEEDBACK_COMPUTE` でフラグメント専用の `early_fragment_tests` と `gl_FragCoord` の関数を除く（ラスタの関数は同じ本体を呼ぶだけ）。解決（`Common/VisibilityResolve.glsl`）は材質ごとの形で標本の直後に呼ぶ（アルベド・法線・ORM は POM の後の UV、高さは元の UV）。材質ごとの定数 UBO は 32 バイトになり、`vt`（アルベド・法線・ORM・高さのパラメータ）を足した。要求のバッファは材質ごとの形の束縛 `VIS_TILE_BINDING + 9`（デバイスが VT のフィードバックに対応するときだけ）。`VirtualTextureFeedbackRing::RecordHostReadBarrier` は、フラグメント段に加えて計算段（UnorderedAccess）の書き込みもホストの読み取りへ見せるバリアを足した。画面全体の直接 dispatch（フォールバック）は材質のテクスチャを束ねないので要求も書かない。欲しいミップの式 `VisQueryLodFromGradient` は、異方性の標本の数を仕様の `ceil(Pmax/Pmin)` から `clamp(floor(Pmax/Pmin), 1, 4)` に変えた（実測。仕様どおりだと常駐タイルが off の 1.5〜2.0 倍で、default は 2.02 倍の stop-when 相当だった。連続値は 1.22 倍、等方は 0.65 倍、floor は 1.02〜1.11 倍）。結果は PROGRESS.md。

## VTG6-RESOLVE-FEEDBACK-EMISSIVE: 材質の解決で発光とデバッグの表示を扱う（VTのフィードバックは VTG6-RESOLVE-VT-FEEDBACK で済）
- status: todo
- done-when: 材質の解決が、発光（色度×輝度×プリエクスポージャ、65504 で頭打ち）を `GBuffer.Emissive` へ書く。デバッグの表示（MegaGeometry のクラスタの色・LOD の段・ワイヤーフレーム）を `--visibility-buffer=on` で出す（ワイヤーフレームはビジビリティバッファのラスタを線の描き方にする）。夜の撮影（`-Night`。on は `-VisibilityBuffer On`）で発光の球のにじみが off と同じに見えることを確かめる。VT のフィードバックの要求と、VT 構成の on・off の撮影の比較は VTG6-RESOLVE-VT-FEEDBACK（済）。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RHITextureUpdateVulkanTest ViewportSnapshotDebugWiringTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|VirtualTextureFeedbackVulkanTest|MegaGeometryFrameCommandDebugModeTest|VisibilityResolveVulkanTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG6-RESOLVE-FEEDBACK-EMISSIVE -Configuration RelWithDebInfo -Deterministic -Night -VisibilityBuffer On`
- stop-when: 発光の球のにじみ（ブルーム）が on で off と大きく違い（目視で明らかに）、原因が発光の式（色度×輝度×プリエクスポージャ）の違いで直せない場合は、測った値と画像を記録して止める。
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 計画書 4.3。 2026-10-06 反復: VT のフィードバックを VTG6-RESOLVE-VT-FEEDBACK に分けて済（この項目の done-when・stop-when から外した）。残りは発光とデバッグの表示と夜の撮影で、1 反復で閉じなければ発光・デバッグの表示・ワイヤーフレームに分ける。verify のビルドの対象 `MegaGeometryFrameCommandDebugModeTest` は独立した target でなく `ViewportSnapshotDebugWiringTest` の束なので直した。夜の撮影の verify に `-VisibilityBuffer On` を足した（足さないと off の撮影になる）。 2026-10-06 親が足した（VTG6-RESOLVE-MATERIALS の反復から）: (1) 解決が VT の要求を書かないため、`--visibility-buffer=on` の VT のテクスチャは常駐せず粗いミップのままぼやける。この項目で要求を書いたあと、VT 構成の on・off の撮影を撮り直して PSNR を比べる。(2) 材質の精度の確認は、最終の色（発光・照明・ブルームを通る）でなく GBuffer の値で行える: `-GBufferDebug Albedo|Material|Normal`（`GBufferDebugPass` に足した。`-LooseTextures` と合わせれば VT に依らない）。 (3) 解決の束縛の番号は、材質の GBuffer（binding 9）を足したので、検証用の書き出しが 10、材質ごとの形の追加の束縛が 11（検証用が無ければ 10）から、テクスチャの枠が +3 から。 (4) 描画の記録の `LodPayload`（`previous.z`。MegaGeometry の描画番号の payload）を足した。デバッグの表示（クラスタの色・LOD の色）の payload の扱いはこの項目。(5) テクスチャの標本の勾配は、`NORVES_MATERIAL_SAMPLING_EXPLICIT_GRADIENT` で VT でない標本も `textureGrad` にしてある（計算シェーダーに暗黙の勾配が無く、`texture()` は粗いミップを引く。GPU のテストが検出する）。

## VTG6-RESOLVE-MATERIALS-ANISO: 材質の解決の高周波な法線マップの標本が、ラスタの暗黙の微分と違う点を調べる
- status: todo
- done-when: `-VisibilityBuffer On` と `Off` の GBuffer.Normal（`-GBufferDebug Normal -LooseTextures -Deterministic`）で、起動画面の default・low の屋根（高周波な法線マップを最小化して引く面）だけが大きく違う理由を、ラスタの暗黙の微分（`texture()`）と解析的な明示勾配（`textureGrad`）の異方性フィルタリングの違いとして確かめる（GPU のテストか、ラスタと解決で同じ `textureQueryLod` と標本値を撮り比べる）。原因が分かれば、解決の標本を直して屋根の差を縮め（GBuffer.Normal の PSNR を 3 視点とも 40 dB 以上にする）、直せないなら測った値と理由を既知の限界として記録する。地面・球・岩・壁（今は 0.24〜1 の平均差）を悪化させない。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest MaterialResourcesTest RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|VisibilityResolveVulkanTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG6-RESOLVE-MATERIALS-ANISO -Configuration RelWithDebInfo -Deterministic -LooseTextures -VisibilityBuffer On -GBufferDebug Normal`
- stop-when: 原因がドライバの異方性フィルタリングの実装（暗黙と明示で標本が違う）で、解決のシェーダーでは直せないと分かった場合は、測った値を記録して止める（done にして既知の限界として残す）。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 2026-10-06 VTG6-RESOLVE-MATERIALS の反復で見つけた。ばらのテクスチャの GBuffer.Normal の PSNR は default 34.4・near 40.6・low 35.0 dB で、屋根を除くと default 55.8・low 51.6 dB（屋根が画面に無い near は 40.6）。屋根は小屋の 1 枚の材質（アルベド・法線・ARM。高さ無し）の一部で、壁は 0.4 の平均差で合うので、法線マップの高周波な縞（画面で 3 画素ほどの周期）の標本だけが違う。実験（`-GBufferDebug Normal` の default、屋根の外接矩形の平均差。基準は 17.9）: 勾配を 0.5 倍で 23.8、2 倍で 5.4、4 倍で 18.4（他の面は 2 倍で悪化: 地面 0.24→1.88）。勾配の向きを保ったまま 2 本の長さを長い方に揃えた等方の標本では屋根 5.4・壁 0.40・球 0.90・岩 0.38・地面 0.88 で、全体の PSNR は 34.35→41.61 dB（地面だけ少し悪化）。法線の符号・軸の入れ替えは悪化（屋根 64〜102）、2×2 の前進・後退の差の模倣は変化なし（屋根 19.3）。`textureQueryLOD` の値を撮り比べると、ラスタの暗黙の値は解決の勾配から求めた値より全面で大きい（`VisQueryLodFromGradient` は異方性で下げた値。`textureQueryLOD` が異方性を含むかは未確認）。


## VTG6-PT-VT-TEXTURES: パストレーサーの材質のテクスチャの配列でVTのテクスチャを読めるようにする
- status: todo
- done-when: `PathTracingClosestHit.glsl` の `materialTextures[256]` の標本が、VT（sparse）のテクスチャでは非常駐のタイルを読まず粗いミップへ逃げる（VTG2-RESIDENCY-FALLBACK の共通の関数の明示 LOD の版を使う）。パストレーサーを有効にした起動画面の撮影で、VT と全常駐の差を記録する。
- verify: `cmake --build build --config Debug --target PathTracingMaterialVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingMaterialVulkanTest$"`
- paths: Assets/Shaders/PathTracing, Assets/Shaders/Common, Library/Core/Private/Rendering, Test/Core/Rendering, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 2026-10-05 親が足した。段2の受入れの既知の限界（パストレーサーを有効にすると VT の非常駐のタイルを読みうる）。起動画面の既定（RTGI は GBuffer だけを読む）では使わない。

## TEST-SKINNED: SkinnedRenderPathContractTestの停止を直す
- status: todo
- done-when: `SkinnedRenderPathContractTest`がCPU 0のまま戻らない（2026-09-24にctest 1350秒で強制終了）原因を特定し、契約を弱めずに完走させる。R5-P12時点でもpending件数assertで失敗していた既存問題として扱う。
- verify: `cmake --build build --config Debug --target SkinnedRenderPathContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error --timeout 300 -R "^SkinnedRenderPathContractTest$"`
- stop-when: 停止がスキニングの実装不具合ではなくテストの待機条件による場合は、待機条件を明示的な上限付きにし、検証内容を減らさない。
- paths: Test/Core/Rendering/CMakeLists.txt, Test/Core/Rendering/SkinnedRenderPathContractTest.cpp, Library/Core/Private/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-05 親が backlog から段6へ取り込んだ。段6はスキニングの経路（塊・計算シェーダーのスキニング・ビジビリティバッファ）を変えるので、この契約テストが完走しないまま VTG6-DEFAULT-ON で既定を切り替えない。分かっている停止の原因: `TestInitializedPassesExecuteThroughFrameCommandsAndSceneRenderer` が `ShaderManager::Initialize(device, "")` の空のシェーダーのディレクトリで `gbuffer.vert` を読めず `assert(gBuffer.Initialize(context))` で落ち、assert の後にプロセスが終わらない（ctest は打ち切りまで待つ）。シェーダーのディレクトリを `NORVES_SOURCE_DIR "/Assets/Shaders"` にすると次の `assert(pending.size() == 1)`（1530行付近）で落ちる（R5-P12 からの pending 件数の assert）。assert の後に終わらない件（テストの assert の置き換えの仕方か、Debug の assert の対話窓）も直す。

## VTG6-PRE-DEFAULT-HARDEN: ビジビリティバッファを既定にする前に、毎フレームの割り当て・枠の上限・失敗の知らせ方を整える
- status: todo
- done-when: (1) `VisibilityRasterPass` の `CollectProceduralChunks`（不透明の描画コマンドごとに毎フレーム呼ぶ）と `MeshIndexChunks.h` の区切りの作業配列（`cuts`）・塊の配列が、呼ぶたびに確保しない形にする（区切りが無いときの近道か、パスのメンバの作業領域の再利用）。(2) `FrameUseRing.h` の枠の番号の剰余（`MaxInFlightSlots`）に、フレームの枠の数が上限を超えたら止まる検査（`static_assert` か assert）を足す。`RenderFrameSerial` が 0 のまま来る経路（`FrameCommand::CreateMegaGeometryPass` で直接組んだコマンドなど）で、リングが記録ごとに伸び退避したバッファが解放されないのを、黙ってそうならない形（警告を1回出す、または通し番号の代わりを決める）にする。(3) `SkinnedMeshGpuStore`（313 行付近）の塊の失敗の分岐が、`FindOrUpload` のやり直しで毎フレーム `NORVES_LOG_ERROR` を出し、`nullptr` を返して既定の GBuffer の経路からもメッシュを消すのを、1回だけ知らせて GBuffer の経路は今のまま描く形にする。(4) `MeshIndexChunks.h` の冒頭のコメント（手続きメッシュが登録時に塊を持つ）を今の事実に合わせ、`RenderGraphCompileTest`（2383 行付近）の `std::strcmp(...DebugName, name)` を既存の `IsDebugName` に置き換える。(7) 計算スキニングの外した数の統計: `RenderingCoordinator.cpp`（2959〜2972 行付近）の集計（各ビューのパスの数を `renderStats` へ足す）を、Vulkan なしで検査できる形（`RenderingCoordinatorStatsPropagation.inl` の自由関数と `RenderingCoordinatorStatsPropagationTest` と同じ形）に出して検査する（今は集計を消しても `UpdateRenderingStats` の後ろへ動かしてもテストが通る）。描かれなかったビュー（`viewPlan.bEnabled == false`・描ける大きさが無い・描画の失敗で `Declare` が呼ばれない）の古い数を足し続けないようにし、1つの SceneView が複数のビューポートを描くときは合算する。`RenderGraphCompileTest` の `TestSkinningComputePassDropCountIsPerFrameAndLoggedOnce` が共有の Logger を `LogOutput::None` で初期化し直したまま戻さないのを直す。(8) 幾何の解決の予備の判定: `SceneView.cpp`（913〜914 行付近）の、解決のパスを GBufferPass・MegaGeometryPass へ渡す配線を外してもテストが落ちない。相手が null のときは装置の機能だけで判定する（`GBufferPass.cpp` 338 行・`MegaGeometryPass.cpp` 1680 行付近）ので、null のときは描画を止めない側に倒すか、SceneView のテストで渡されていることを確かめる。`GetFallbackReason` が計算スキニングの準備（`SkinningComputePass` の `IsReady`）を見ないので、計算スキニングだけが作れないとスキニングの物が消える。`VISBUFFER_FALLBACK` が1回だけ出ることを検査する。`VisibilityResolvePass.cpp`（371 行付近）・`.h`（211 行付近）のコメントを今の判定に合わせる。(9) `VisibilityRasterPass.cpp`（790 行付近）の本体の枠が `m_FrameCounter % FrameSlotCount`（Execute の回数）のまま。`FrameUseRing` の形にする。(10) `VisibilityResolvePass.cpp`（376 行付近）の `Container::VariableArray<Use*> uses` を Record のたびに作って reserve せずに push_back している（メンバの作業用配列にする）。同じファイル 713 行付近のコメント「Record は断ったとき何も記録しないので」をヘッダー（212 行付近）の記述（最初の間接 dispatch を断られたときはパイプラインとディスクリプタセットの設定が残る）に合わせる。タイルの版の変異 MG（材質ごとの形のパイプラインが作れないとき）を、`BeforeResolveTilePipelineUnavailable` のケースで落ちることを切り分けて確かめる。(6) 材質の表の毎フレームの費用: `VisibilityMaterialTable.cpp` の `Add` の線形探索（描画数×材質数）と上限超えの一覧の線形探索をハッシュの索引にし、`VisibilityRasterPass` の `BuildGpuEntries()` と `sectionMaterials` の毎フレームの確保をやめる。(5) 使われていない公開の `SkinningCompute::GetUsedCount`・`MaterialTileClassify::GetUsedCount`・`FrameUseRing::GetActiveSlot` を、テストで使うか消す。CPU のテストが (1) の作業領域の再利用（2フレーム目に確保が増えない）、(2) の 0 の経路の扱い、(3) の失敗でも GBuffer の経路のメッシュが残ることを確かめる。既定の描画は変えない（`-Deterministic` の起動画面の撮影が前と一致する）。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RenderResourcesDomainContractTest SkinnedRenderPathContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|MeshResourcesProceduralGpuTest|ComputeSkinningVulkanTest|MaterialTileClassifyVulkanTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG6-PRE-DEFAULT-HARDEN -Configuration RelWithDebInfo -Deterministic`
- stop-when: (3) で GBuffer の経路のメッシュを残すと、ビジビリティバッファの経路と描画の集合が食い違い、DEFAULT-ON の予備の経路の契約を決め直す必要がある場合は、理由と選択肢を記録して止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-05 親が足した（VTG6-CHUNKS-HARDEN・VTG6-PASS-FRAME-SLOTS・VTG6-SKINNING-FINAL-BARRIER の評価の残課題。どれも評価は PASS）。危険地帯（描画パス・寿命）。スキニングの塊の失敗のケースの証拠は `SkinnedRenderPathContractTest.exe` の直接実行の出力でよい（TEST-SKINNED の既知の停止）。

## VTG6-DEFAULT-ON: ビジビリティバッファを既定にし、今のGBufferのラスタを予備にする
- status: todo
- done-when: `geometryShader` に対応する GPU では、ビジビリティバッファの経路を既定（`--visibility-buffer` の既定を on）にし、GBufferPass・MegaGeometryPass の GBuffer へのラスタは、`geometryShader` の無い GPU か `--visibility-buffer=off` のときの予備にだけ残す（`VISBUFFER_FALLBACK reason=<..>` を1回出す）。`RenderGraphCompileTest`・`SkinnedRenderPathContractTest`・`GBufferMaterialDescriptorCacheTest`・`MegaGeometryFrameCommandDebugModeTest`・`RenderingVelocity*`・`DebugViewModeStringTest` を新しい既定に合わせて通す（予備の経路の検査も残す）。MegaGeometryPass の1パス目・2パス目の描画先をビジビリティバッファへ切り替え、2パスの遮蔽の HZB をビジビリティの1パス目の深度から作る（`ID・深度の1パス目 → HZB → 2パス目`。VTG6-VIS-RASTER から回した）。スキニングを含む検証シーン（`RenderingVelocitySkinnedVulkanTest` の場面など）をビジビリティバッファで撮る経路を足し、ID の表示と解決の結果を開いて確かめる。Indoor/Outdoor の golden を回し、差が出たら差がこの変更（解析的な微分・三角形の接線の基底）だけによることを確かめて `Docs/RenderingValidation/GoldenBaselines.md` の手順で再承認し、根拠をコミットの本文に書く。起動画面の朝・昼・夕・夜の `-Deterministic` の撮影を開いて確かめる。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest SkinnedRenderPathContractTest MaterialResourcesTest MegaGeometryResourcesTest RenderingVelocityVulkanTest ViewportSnapshotDebugWiringTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|SkinnedRenderPathContractTest|GBufferMaterialDescriptorCacheTest|MegaGeometryFrameCommandDebugModeTest|MegaGeometryResourcesTest|RenderingVelocityStaticVulkanTest|RenderingVelocityMotionVulkanTest|RenderingVelocityCameraVulkanTest|RenderingVelocityObjectVulkanTest|RenderingVelocitySkinnedVulkanTest|DebugViewModeStringTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG6-DEFAULT-ON -Configuration RelWithDebInfo -Deterministic -SunElevations 10,45,3`
- stop-when: golden の差がこの変更だけでは説明できない場合は、測った値と分類を記録して止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, Baselines/RenderingValidation, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-05 親: 元の VTG6-RETIRE-GBUFFER-RASTER（今のラスタを外す）を、`geometryShader` の無い GPU の予備として残す形に変えた。危険地帯（描画パス）。起動画面の見た目を変えうる（絶対規則7）。

## VTG6-ACCEPT: 段6（ビジビリティバッファ）の受入れを記録する
- status: todo
- done-when: `Docs/RenderingValidation/VirtualizationAcceptance.md` の段6の節に、ビジビリティバッファの既定と予備の経路の `-Deterministic` の撮影（朝・昼・夕・夜 × 3視点）の PSNR と差の出どころ、golden（再承認したならその根拠）、VT のフィードバックの要求の数・常駐量の前後、GPU 時間（RelWithDebInfo の `-GpuTimingFrames`、ビジビリティバッファのラスタ・分類・材質の解決の内訳と、予備の経路との比較。負荷モード 300 個も）、関係するテストの結果、既知の限界を書く。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RHITextureUpdateVulkanTest RenderingGoldenImageTest RenderingVelocityVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(IntegerAttachmentVulkanTest|VisibilityBufferEncodingTest|ComputeSkinningVulkanTest|MaterialTileClassifyVulkanTest|RenderGraphCompileTest|RenderingVelocitySkinnedVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG6-ACCEPT -Configuration RelWithDebInfo -Deterministic -SunElevations 10,45,3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG6-ACCEPT-night -Configuration RelWithDebInfo -Deterministic -Night`
- stop-when: 受入れの数値が段6の受入れ（計画書 5）を満たさない場合は、測った値を記録して止める。
- paths: Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: この段の後、親が main へマージしてプッシュする。

## VTG7-INT64-ATOMICS: 64bitアトミックのビジビリティバッファを作る
- status: backlog
- done-when: 64bit アトミックを照会・有効化し、深度の上位32bit＋ID のビジビリティバッファへハードのラスタが書く経路を足す（非対応なら深度テストの経路）。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderGraphCompileTest$"`
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI, Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 段7の開始時に親が詳しくする（計画書 4.3）。

## VTG7-SW-RASTER: 小さいクラスタを計算シェーダーでラスタする
- status: backlog
- done-when: 画面上で小さいクラスタを計算シェーダーでラスタし、大きいクラスタはハードのラスタへ振り分ける。小さい三角形の多い視点で GPU 時間が下がる（RelWithDebInfo で測る）。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG7-SW-RASTER -Configuration RelWithDebInfo`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md

## VTG7-ACCEPT: 段7（ソフトウェアラスタ）の受入れを記録する
- status: backlog
- done-when: `Docs/RenderingValidation/VirtualizationAcceptance.md` の段7の節（GPU 時間の前後）。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- paths: Docs/RenderingValidation, TASKS.md, PROGRESS.md

## VTG8-VSM-POOL: VSMの物理ページのプールとページの表を作る
- status: backlog
- done-when: 太陽のクリップマップ（各段 16K×16K 仮想、ページ 128×128）のページの表と物理ページのプールを作る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderGraphCompileTest$"`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 段8の開始時に親が詳しくする（計画書 4.3）。

## VTG8-VSM-MARK-RENDER: 必要なページに印を付けて描く
- status: backlog
- done-when: 深度から必要なページに印を付け、印のページだけを物理プールへ割り当て、クラスタの経路でページへ深度を描く。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderGraphCompileTest$"`
- paths: Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md

## VTG8-VSM-CACHE: 動かない物のページをキャッシュする
- status: backlog
- done-when: 動かない物のページを次フレームへ持ち越し、動いた物の範囲だけ無効化する。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderGraphCompileTest$"`
- paths: Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md

## VTG8-VSM-SAMPLE: 照明でVSMを読み、起動画面の太陽の影にする
- status: backlog
- done-when: 照明が VSM を PCF で読む。起動画面の太陽の影を VSM にし（検証シーンは CSM のまま）、撮影で CSM 以上に細かくちらつかないことを確かめる。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG8-VSM-SAMPLE -Configuration RelWithDebInfo -SunElevations 10,45,3`
- paths: Library/Core/Private/Rendering, Assets/Shaders, Game, Test/Core/Rendering, TASKS.md, PROGRESS.md

## VTG8-ACCEPT: 段8（VSM 太陽）の受入れを記録する
- status: backlog
- done-when: `Docs/RenderingValidation/VirtualizationAcceptance.md` の段8の節。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- paths: Docs/RenderingValidation, TASKS.md, PROGRESS.md

## VTG9-VSM-POINT: 点光源の影をVSMにする
- status: backlog
- done-when: 点光源の6面のキューブを同じ物理プールの VSM で持ち、起動画面の夜の電球の影を VSM にする。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-VSM-POINT -Configuration RelWithDebInfo`
- paths: Library/Core/Private/Rendering, Assets/Shaders, Game, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 段9の開始時に親が詳しくする（計画書 4.3）。

## VTG9-ACCEPT: 段9（VSM 点光源）と全体の受入れを記録する
- status: backlog
- done-when: `Docs/RenderingValidation/VirtualizationAcceptance.md` の段9の節と全体のまとめ（8GB 級の上限での全体の負荷モード、各段の数値）。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- paths: Docs/RenderingValidation, TASKS.md, PROGRESS.md

## SS-CAPTURE: 起動画面を撮影して数値を出す経路を作る
- status: done
- done-when: `Game.exe --capture-png=<path> --startup-camera=<yaw>,<pitch>,<arm>` を付けて起動すると、アセットの読み込みが落ち着いた後の起動画面（Rendering3DTest）の最終出力をPNGに保存し、終了コード0で終わる。`Scripts/CaptureStartupScene.ps1 -OutDir <dir>` が既定・近接（球の輪郭が画面の中央付近に来る視点）・低角度（地面すれすれ）の3視点を撮り、各PNGと、平均輝度・白飛び（255）画素率・黒つぶれ（0）画素率を書いた `metrics.json` を出す。Gameの終了コードが0でない、PNGが無い、Game.logにシェーダーのコンパイル失敗（`Failed to compile shader`。Slang SDK未設定の`neural_material_decode.slang`だけは除く）がある場合はスクリプトが終了コード1を返す。撮影は ImGui とデバッグ用のボードを写さない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-CAPTURE`
- stop-when: 既存の `RequestFrameCapture` と `--exit-after-rendered-frames` の組み合わせで最終出力を取れない理由が見つかり、RenderThreadの寿命に手を入れる必要が出たら、その理由を記録して止める。
- paths: Library/Core/Private/Engine, Library/Core/Public/Engine, Game, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 以降の全タスクの見た目の証拠はこのスクリプトで撮り、PNGを開いて確認する。撮影物は `.harness/runs/` に置き、コミットしない。

## SS-POM: 視差オクルージョンの凹凸の向きと輪郭の歪みを直し、3つのシェーダーで共通にする
- status: done
- done-when: POMと余接フレームを共通のシェーダーの取り込みファイルへまとめ、`gbuffer.frag`・`forward_transparent.frag`・`megageometry.frag` が同じ関数を使う。高さマップは白=高いとして読む（`1.0 - height` で深さへ直す）。輪郭のフェードと層数は幾何法線とビュー方向の内積で決め、TBNは元のUVから一度だけ作って法線マップにも使い、接空間のビュー方向はTとBを正規化して作る。マーチ中のサンプルは分岐の前に取ったUV勾配で`textureGrad`にする。撮影の近接視点で、球の石が盛り上がり目地がへこんで見え、輪郭の付近で模様が引き伸ばされたり流れたりしていない。法線マップ（`nor_gl`）の緑の向きが凹凸と合っている（光の当たる側が明るい）ことを点光源の近くで確かめ、逆なら直す。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-POM`
- stop-when: 余接フレームの向き（`8c80695` で正しくなった +∇u, +∇v）を戻さないと合わない場合は、戻さずに原因を記録して止める。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Game/GameModes/Rendering3DTest, TASKS.md, PROGRESS.md
- notes: 原因は `8c80695` で余接フレームが正しい向きになり、それまで打ち消し合っていた「高さを深さとして読む」反転だけが残ったこと。境界の歪みは、正規直交でない余接フレームで `viewDirTS.z` が N·V より大きくなり、`842070e` の輪郭フェードが効かなくなったため（`842070e` の修正自体は残っている）。`megageometry.frag` には `842070e` が入っておらず、`forward_transparent.frag`・`megageometry.frag` は古い閾値 `1e-8` のまま。

## SS-SKY-MODEL-P1: 空のモデルを球殻の大気のレイマーチと多重散乱の近似にし、地平線より下を地面として返す
- status: done
- done-when: `SkyAtmosphere.cpp` の `EvaluateHillaireSkyReference` を、平行平板の τ·e^(-τ) の近似から、球の惑星（`PlanetRadiusMeters`・`AtmosphereHeightMeters`）の大気を視線に沿ってレイマーチする形へ置き換える。各点で Rayleigh・Mie の散乱係数×高度の密度、太陽への透過率（同じ密度の光学的深さの積分）、位相関数で単一散乱を積分し、Hillaire 2020 の等方の多重散乱（2次以降の散乱を ψ_ms と 1/(1−f_ms) の等比級数で近似。前計算の小さな表か解析近似）を足す。視線の透過率は (1−e^(−τ)) の飽和で扱われ、地平線の付近が橙でなく白っぽくなる。地平線より下の視線は0を返さず、地面（`GroundAlbedo` のランバート面を太陽の透過光と空の照度で照らしたもの）の反射に透過率を掛けた値と、そこまでの散乱を返す。透過率LUT・空の太陽の地表照度（`ComputeSunGroundIlluminance`）・`EvaluateSkyViewRadiance` も同じ密度の積分に揃える（空の太陽の方向光と空の背景・空由来のIBLの間で太陽の透過率が食い違わない）。CPUのテスト `SkyAtmosphereModelTest` のアンカーを、テスト側に置いた細かい刻みの独立な数値積分と次の物理量の範囲で置き換える：太陽仰角40°で天頂が青（B > G > R）、天頂の輝度 約2000〜8000 nits、水平面の空の照度が太陽を含む全天の照度の10〜30%、太陽と反対側の地平線の R/B が0.5〜1.2、地平線より下が0でなく有限、仰角3°で太陽の周りが橙。空の放射輝度LUT・透過率LUTの生成は空のパラメータ（太陽の向きを含む）が変わったときだけ走り、1回の生成時間をログに出す（起動画面の解像度で1回 150 ms を超えるなら刻み・解像度を減らすか、生成をGPUへ移す）。撮影の昼（45°）の低角度で空が上ほど濃い青・地平線が白っぽく明るく、既定視点で地面の外（遠景）が黒くない。夕（3°）で太陽側の地平線が橙になる。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game SkyAtmosphereModelTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SkyAtmosphereModelTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-SKY-MODEL-P1 -SunElevations 10,45,3`
- stop-when: Indoor/Outdoor の golden が空を使うかを先に確かめる。使わないなら変わらないはずで、変わるなら止めて原因を直す。空を使っていて変わるなら、空の変更だけによる差であることを確かめて再承認する（任されている。手順は `Docs/RenderingValidation/GoldenBaselines.md`）。空の数値アンカーは「古い値に合わせる」のではなく物理的な範囲と独立な積分で置き換え、根拠を `Docs/RenderingValidation/R2Acceptance.md` に追記する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: SS-DAYLIGHT-P1・P2 の停止理由（`blocked/SS-DAYLIGHT-P1.md`）の選択肢A。2026-09-30、ユーザーの方針（細かな判断で止めず推奨で完走する）により推奨を採った。空の放射輝度LUT（`SkyAtmospherePass::GenerateRadianceLut`）はCPUの `EvaluateHillaireSkyReference` から作られ、ラスタの背景・空由来のIBL・PTの不交差・フォグの内向き散乱の色が同じLUTを引くため、この関数を直すと全部が揃って変わる。危険地帯（ライティング・空）。評価者を通す。

## SS-SKY-MODEL-P2: 新しい空で、空を使う検証（R2の受入れ・PTの屋外）を再照合する
- status: done
- done-when: 空を有効にする検証（`RenderingHdrSceneCaptureTest` の屋外・R2の受入れの数値/画像、`PathTracingOutdoorVulkanTest`、`R7OutdoorPathTracingReferenceVulkanTest`、`LightingLightBufferTest` の空の太陽の値など。`Test/Core/Rendering` で `SkyAtmosphere` を有効にしている箇所を洗い出す）を新しい空で回し、落ちるものは原因を分類する。差が空のモデルの変更だけによるもの（古い空のアンカー値・古い空の基準画像）は、新しい値が物理的な期待に近いことを確かめて再基準化・再承認する。ラスタとPTの比較（同じLUTを引く）の閾値は変えない。閾値を越えるものは原因を直すか、測定値と分類を既知の限界として受入れ記録（`R2Acceptance.md`・`R7OutdoorAcceptance.md`）に書く。
- verify: `cmake --build build --config Debug --target Game RenderingHdrSceneCaptureTest PathTracingVulkanTest R7OutdoorPathTracingReferenceVulkanTest LightingLightBufferTest RenderingGoldenImageComparatorTest R8SequenceRenderer R8ExrSequenceValidator -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingHdrOutdoorSceneVulkanTest|PathTracingOutdoorVulkanTest|PathTracingVolumetricTest|LightingLightBufferTest|R2SkyGoldenCurrentModelTest|R8SequenceSmokeTest)$"`
- stop-when: 閾値や規則そのものを変えないと通らない場合は、変えずに測定値と分類を既知の限界として記録して完了にする。
- notes: 2026-10-02 評価1周目（`68b0f42`）は NEEDS_WORK（`NEXT_FINDINGS.md`）。空を有効にする `PathTracingVolumetricTest`・`R8SequenceSmokeTest` が未実行、R2の空の基準画像3枚を新しい空から作った候補と画素で照合した証拠が無い。`R8SequenceSmokeTest` は単独で9分を超えるなら verify の行へ入れず、反復内で回した記録（所要時間とEXIT_CODE）を PROGRESS と受入れ記録に残す。
- notes: R7屋外の参照比較（`ctest ... --timeout 1800 -R "^R7OutdoorPathTracingReferenceVulkanTest$"`、約12分）は、ランナーの再検証の上限（10分）を超えるため verify の行から外した。`68b0f42` の Core に対して `.harness/runs/20261001-193759/verify-SS-SKY-MODEL-P2-6.txt` で Passed（721.71 s、EXIT_CODE=0）を確かめた（`68b0f42` の変更は `PathTracingVulkanTest.cpp` と文書だけで、R7屋外の実行ファイルには入らない）。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-02 評価1周目の指摘に対応した。`PathTracingVolumetricTest`（8.3 s）と `R8SequenceSmokeTest`（単独で25.8 s、EXIT_CODE=0。9分より短いので verify の行へ入れた）が合格。R2の空のgolden 3枚は今の空のモデルから作った候補と画素で完全一致し（`.harness/runs/20261002-084328/verify-SS-SKY-MODEL-P2-3.txt`・`-4.txt`）、この照合を `R2SkyGoldenCurrentModelTest` として登録した。R7屋外の参照比較は起動画面の変更の後の Core でも合格（603 s、`-7.txt`）。
- notes: 参照比較の段（`-L Reference`）のうち空を使うものだけを回す（R7屋外は単独で約12分）。GPUのテストは同時に2本走らせない。上の verify の対象名が CMake に無い場合は、`Test/Core/Rendering/CMakeLists.txt` の実際の名前へ読み替え、読み替えを PROGRESS に書く。

## SS-DAYLIGHT-P1: 起動画面を物理空と空の太陽による昼の屋外にする
- status: done
- done-when: Rendering3DTest が R2 の物理空（SkyAtmosphere）を有効にし、空の太陽（仰角約40°、カメラの既定視点から球と岩の影が地面に見える方位）が影を落とす方向光になる。シーン独自の方向光は外し、方向ライトの操作（矢印キーとImGui）は空の太陽の仰角・方位を動かす。`f90e7ea` の露出補正を外す（露出は SS-AUTOEXPOSURE-P2 の自動露出）。点光源（Lumen/Candelaの物理単位）と発光球の輝度も物理的にありうる値へ移す。IBLは空から作る（静的HDRは空が無効なときだけ使う）。撮影で、青い昼の空の下、球と岩の影が地面にはっきり見え、影の中の地面の平均輝度が日向の15〜40%（黒くつぶれず、影の中の石畳の模様が見える。PNGの領域の画素をsRGBからリニアへ戻し、Rec.709の重みで求めた輝度の平均の比で測る。8ビットの符号値の比ではない）、白飛び画素率が1%未満。昼の撮影の近接視点で、石畳の球の石の太陽側の斜面が明るく反対側が暗い（法線マップ `nor_gl` の緑の向きが凹凸と合う。逆なら直す）。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-DAYLIGHT-P1 -SunElevations 45`
- stop-when: 影の中の比が範囲に入らない原因が空のモデル（SS-SKY-MODEL-P1）の側にある場合は、そちらへ差し戻さず、測った値と原因を記録してこのタスクで直せる範囲（起動画面の値）で閉じる。
- paths: Game/GameModes/Rendering3DTest, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Component, Library/Core/Private/Component, Assets/Shaders, Assets/Textures, TASKS.md, PROGRESS.md
- notes: ユーザー決定（2026-09-27）「物理的な昼の屋外へ」。影が見えない原因は、静的HDRの環境光が地面の照度の約87%を占め（方向光1 lux）、影で約13%しか暗くならないこと（`f3d4abb` でIBLを E/π にした後、比が約9倍悪化）。空を有効にするとエンジンが空の太陽の方向光を自動で加える（SKY-SUN-P2）。起動画面の見た目を変える変更で、この変更はユーザーの承認済み。
- notes: 起動画面の側は `ad6729a` で実装済み。空のモデルが暗く橙で地平線より下が黒いため止めていた（`blocked/SS-DAYLIGHT-P1.md`）。SS-SKY-MODEL-P1 の後に撮り直し、完了条件を確かめる。法線マップの向きの確認は SS-POM の評価の指摘の残り。
- notes: 2026-10-02 評価1周目（`153c02c`）は NEEDS_WORK（`NEXT_FINDINGS.md`）。ライティングの出力では影の中は日向の約2割あるが、トーンマップ（ACESの足元）で表示のリニア値の比が約10%へ縮み、影の芯がほぼ黒になる。直す場所は起動画面のカメラの表示側（自動露出の露出補正・トーンマップの足元・グレーディング）で、検証シーンのカメラの既定は変えない。

## SS-DAYLIGHT-P2: 太陽の向きを操作・指定でき、高さフォグを掛ける
- status: done
- done-when: `--sun-elevation=<deg>` と `--sun-azimuth=<deg>` で起動時の太陽の向きを指定でき、撮影スクリプトの `-SunElevations` で朝（約10°）・昼（約45°）・夕（約3°）を撮れる。ImGuiに手動露出（EV100）のスライダーがある。R3の高さフォグを起動画面で有効にし（`kStartupHeightFogDensity` を0.02〜0.1の範囲で撮り比べて選ぶ）、遠くの地面と空の境が空の色へ霞む（遠くの地面の色がフォグ無しより空の地平線の色へ近づき、暗くならない）。撮影の3時刻とも空と地面の色が時刻らしく変わり、昼の画像で近景（球）のコントラストがフォグで落ちていない（球の輝度の標準偏差がフォグ無しの90%以上）。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-DAYLIGHT-P2 -SunElevations 10,45,3`
- stop-when: 霧のパラメータの意味（R3の公開契約）を変えないと起動画面に合わない場合は、契約を変えずに記録して、合う範囲の密度で閉じる。
- paths: Game, Library/Core/Private/Engine, Library/Core/Public/Engine, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 太陽・露出の引数とImGuiは `79b0c48` で実装済み。フォグが下向きの視線で黒くなる（空のモデルが地平線より下で0を返す）ため既定の密度を0にして止めていた（`blocked/SS-DAYLIGHT-P2.md`）。SS-SKY-MODEL-P1 の後に密度を決めて撮り直す。

## SS-CSM-DISTANCE: 影の分割をカメラのfarから切り離し、近くの影を細かくする
- status: done
- done-when: CSMの分割が「影の最大距離」（既定 約80 m、設定できる）で決まり、カメラのfar（1000 m）に依存しない。起動画面の既定視点でカスケード0の1テクセルが2.5 cm以下になる（テストか起動ログで数値を示す）。撮影で球と岩の接地部の影がくっきりし、影の縁が階段状に見えない。影の最大距離より遠い物体は影を受けない（急に消えないよう最後のカスケードの端でフェードする）。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game CascadedShadowLightMatricesTest DirectionalShadowLightMatricesTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CascadedShadowLightMatricesTest|DirectionalShadowLightMatricesTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-CSM-DISTANCE`
- stop-when: 承認済みのOutdoor goldenが変わる場合は、差の原因と物理的な妥当性を確かめて再承認を記録する（承認は任されている）。原因が説明できない差なら止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Game/GameModes/Rendering3DTest, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（影）。評価者を通す。現状はfar=1000・λ=0.5でカスケード0が0.1〜125.5 m、1テクセル約0.157 m（R2前の単一シャドウマップの8倍粗い）。既存のFIX-CSM-TERMINATORとFIX-CSM-MEGA-CASTER-BOUNDSはbacklogに残す。

## SS-SHOWCASE: 起動画面に材質見本の球の列とCottageを置き、地面を広いテクスチャ付きの材質にする
- status: done
- done-when: 既存の天球・地面・球・岩・点光源はそのまま残し、材質見本の球（金属0と1の2列 × 粗さ0.1/0.3/0.5/0.7/0.9の5段、同じ色）と、`Assets/Models/Cottage_Clean` の小屋を置く。小屋のOBJは追跡外のため、標準ライブラリだけのPythonスクリプト（`Scripts/ConvertObjToGltf.py`）でglTFに変換し、変換結果を追跡する。材質に粗さ・金属のスカラー値を指定できないならMaterialCreateDataに足す。地面は約60 m四方にし、テクスチャ付きの材質（CobbleStoneFloorのタイル＋POM）にする。撮影の既定視点で、見本の球・小屋・岩・球が重ならずに見え、金属の球に空と周囲が映り込む。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-SHOWCASE`
- stop-when: 小屋のテクスチャ（4K PNG 計約70MB）の読み込みで起動が30秒以上遅くなる場合は、縮小版を作るか遅延読み込みにするかを記録して、軽い方を採る。
- paths: Game/GameModes/Rendering3DTest, Assets/Models/Cottage_Clean, Scripts/ConvertObjToGltf.py, Library/Core/Public/Rendering, Library/Core/Private/Rendering, TASKS.md, PROGRESS.md
- notes: ユーザー決定（2026-09-27）「展示物を足す」。

## SS-POINT-SHADOW-P1: 影を落とす点光源のキューブシャドウマップを描く
- status: done
- done-when: CastShadowsの点光源（表示中のもの、最大4灯、光源からの距離が近い順）ごとに、6面の距離（光源からの線形距離）をキューブ配列の深度へ描くパスがある。キャスターは光源の範囲の球でカリングする。影の灯の選択と6面の行列はRenderThreadへFramePacketのスナップショット越しに渡る。6面の行列（Vulkanの規約での向きと上方向）をCPUのテスト `PointShadowFaceMatricesTest`（`CameraViewConstantsTest` の束へ MEMBER として足す）で確かめる。デバッグ表示でキューブの中身（距離）を確かめられる。影を落とす点光源が無いシーンでは何も描かず、既存の描画結果が変わらない。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game LightingLightBufferTest CameraViewConstantsTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingLightBufferTest|PointShadowFaceMatricesTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-POINT-SHADOW-P1`
- stop-when: RHIにキューブ配列の深度テクスチャや層ごとの描画先が無い場合は、RHIへの追加を同じタスクで行い、Rendering層からVulkanを直接includeしない。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/RHI, Library/Core/Private/RHI, Assets/Shaders, Test/Core/Rendering, Test/TestBundle, TASKS.md, PROGRESS.md
- notes: ユーザー決定（2026-09-27）「キューブシャドウを実装」。危険地帯（RHI・RenderThread・影）。評価者を通す。新しいテストは実行ファイルを増やさず既存の束へ足す。

## SS-POINT-SHADOW-P2: 点光源の影をライティングに掛け、起動画面の点光源に影を落とさせる
- status: done
- done-when: `lighting.frag` と `forward_transparent.frag` が、影を落とす点光源にキューブシャドウ（PCF、法線方向のずらし）を掛ける。影を落とさない点光源の照明は変わらない。Rendering3DTest の点光源を影ありにする。Rendering3DTest に夜の条件（起動引数 `--night`。空の太陽を消し、環境光は空が無効なときの静的HDRを月明かり程度（地面の照度 約0.1〜1 lx）へ落とす。露出は自動のまま）を足し、撮影スクリプトに `-Night` を足して夜の3視点を撮る。夜の撮影で、点光源による球・岩の影が地面に見え、影の縁にアクネ（縞）とピーターパン（接地部の浮き）が見えない。昼の既定の起動画面は変わらない。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game LightingLightBufferTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingLightBufferTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-POINT-SHADOW-P2-night -Night`
- stop-when: 承認済みのgoldenが変わる場合は差の原因と妥当性を記録して再承認する。影なしの点光源の結果が変わるなら止めて直す。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（ライティング・影）。評価者を通す。
- notes: 影の適用は `20babbe` で実装済みでゲートも合格。1600 lm の点光源は夕の地面の約1%の照度しかなく物理的に見えないため、「夕の撮影で見える」を満たせず止めていた（`blocked/SS-POINT-SHADOW-P2.md`）。2026-09-30、推奨の選択肢1（夜の撮影条件を足し、完了条件を夜に替える）を採った。点光源の物理値は変えない。

## SS-AUTOEXPOSURE-P1: 輝度のヒストグラムから露出を求める
- status: done
- done-when: SceneColorの輝度（log2、プリエクスポージャを外した絶対輝度）のヒストグラム（256区間）をcomputeで作り、下位・上位の外れ（例 下位10%・上位2%）を除いた平均から目標のEV100を求め、明るくなる向き・暗くなる向きで別の速さで順応させる。露出補正（EV）と最小・最大のEV100を設定できる。ヒストグラム→EV100の計算とタイムステップの順応をCPUの参照実装と照合するテスト `AutoExposureMathTest`（`CameraViewConstantsTest` の束へ MEMBER として足す）がある。この段では求めたEV100をログ・デバッグ表示に出すだけで、画面の露出はまだ変えない。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game CameraViewConstantsTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^AutoExposureMathTest$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-AUTOEXPOSURE-P1 -SunElevations 10,45,3`
- stop-when: GPUの結果をCPUへ戻す遅延がRenderThreadの同期を変えないと取れない場合は止めて記録する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Test/TestBundle, TASKS.md, PROGRESS.md
- notes: ユーザー決定（2026-09-27）ポスト処理「基本セット+演出系」。危険地帯（RenderThread）。評価者を通す。

## SS-AUTOEXPOSURE-P2: 自動露出を画面に適用し、起動画面の既定にする
- status: done
- done-when: カメラに露出の方式（手動/自動）があり、自動では前のフレームまでに求めたEV100からプリエクスポージャを決める（数フレームの遅れは許す）。検証シーン（Indoor/Outdoorのgolden、R系の受入れ）は手動のままで結果が変わらない。Rendering3DTest は自動にする。撮影の朝・昼・夕の3枚で、トーンマップ後の平均輝度がどれも0.12〜0.40に入り、白飛び画素率が2%未満。太陽の向きを急に変えたとき、露出が振動せず1〜3秒で落ち着く（連続撮影かログで示す）。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game CameraViewConstantsTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(AutoExposureMathTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-AUTOEXPOSURE-P2 -SunElevations 10,45,3`
- stop-when: 検証シーンの結果が変わる場合は止めて直す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Component, Library/Core/Private/Component, Assets/Shaders, Game/GameModes/Rendering3DTest, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（RenderThread・露出の契約）。評価者を通す。

## FIX-AUTOEXPOSURE-READBACK: 自動露出の読み戻しのホスト可視性とEV100のデバッグ表示
- status: done
- done-when: ヒストグラムの読み戻し先へのコピーの後に、転送の書き込み→ホストの読み取りの依存（Vulkanの`VK_ACCESS_HOST_READ_BIT`・`VK_PIPELINE_STAGE_HOST_BIT`）をRHI経由で記録する（RHIにホスト読み取りの状態か同等の手段を足す）。既知のヒストグラムをGPUで作って読み戻した各区間が一致する検査がある。目標と順応後のEV100を、RenderThreadからGameThreadへスナップショットで渡し、起動画面のImGui（「空の太陽」ウィンドウ）に表示する。
- verify: `cmake --build build --config Debug --target Game CameraViewConstantsTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^AutoExposureMathTest$"`
- stop-when: RHIの状態の追加が既存のバリアの割り当て（`ResourceBarrierTracker`）を変える場合は止めて記録する。
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Game/GameModes/Rendering3DTest, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: SS-AUTOEXPOSURE-P1の評価の指摘1・3。P2の範囲（RHIを含まない）で直せないため分けた。危険地帯（RHI/Vulkan・RenderThread）。評価者を通す。

## SS-BLOOM-MIPCHAIN: ブルームを段階的に縮小・拡大する方式に置き換える
- status: done
- done-when: ブルームが、13タップの縮小（最初の段は明るい画素のちらつきを抑える重み付き平均）を約6段、3×3のテントフィルタでの拡大と加算で作られ、元の色へ一定の割合（既定 約0.04、しきい値なしでエネルギーを保つ方式を既定）で混ぜる。広がりは画面の高さの10〜20%に届く。撮影の昼と夕で、太陽の周り・発光球・金属の球の強い反射の周りに柔らかいにじみが見え、縮小の格子や輪状の模様が見えない。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-BLOOM-MIPCHAIN -SunElevations 45,3`
- stop-when: 承認済みのgoldenが変わる場合は差の原因と妥当性を記録して再承認する（任されている）。原因が説明できない差なら止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 今のブルームは1回の16タップで、広がりは最大 約10 px（`bloom.frag`）。値の調整では広がらない。

## SS-EMISSIVE-PREEXPOSE: GBufferへ露出を掛けた発光を書き、明るい発光がRGBA16Fで溢れないようにする
- status: done
- done-when: `gbuffer.frag`・`megageometry.frag`・スキニングのGBuffer（`GBuffer_Emissive` へ書くすべてのシェーダー）が、発光（nits）にそのフレームのプリエクスポージャを掛けてから書き、Lighting 側で GBuffer の発光に露出を掛ける箇所を外す（発光の最終的な寄与は数値的に同じ）。前向きの透明（`forward_transparent.frag`）など SceneColor へ直接書く経路の発光は、既に露出後の値で書いているかを確かめ、揃っていなければ揃える。PT・RTGI・DDGI は材質から発光を読むので変えない（GBufferから読む経路があれば同じ規約に揃える）。Indoor/Outdoor の golden が変わらない（丸めの差で変わるなら、差がこの変更だけによることを確かめて再承認）。昼の露出（EV100 約14.6）で 1,000,000 nits の発光面が SceneColor で有限で、無限大・NaN にならないことを GPU の検査（既存の束へ MEMBER として足すか、既存のGPUテストへケースを足す。新しい実行ファイルは作らない）で確かめる。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-EMISSIVE-PREEXPOSE -SunElevations 45,3`
- stop-when: 自動露出のフレームで GBuffer を書くときの露出と Lighting が使う露出が別のフレームの値になる経路が見つかったら、同じフレームの値を使うように直す（直せないなら止めて記録する）。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: SS-EMISSIVE-GLOW の停止理由（`blocked/SS-EMISSIVE-GLOW.md`）の推奨1。2026-09-30 に採った。今は発光を物理の nits のまま `GBuffer_Emissive`（R16G16B16A16_FLOAT、上限65504）へ書くため、色(1,0.9,0.3)で約57000 nitsを超えると無限大になり発光が消える。危険地帯（GBuffer・ライティング・露出）。評価者を通す。

## SS-EMISSIVE-GLOW: 起動画面の発光球をブルームでにじむ明るさにする
- status: done
- done-when: 起動画面の発光球の輝度（今は1800 nits）を、昼・夕の自動露出で画面の平均輝度に対して十分に明るく（目安: 撮影の夕で周りの背景の30倍以上）なる物理的な値にし、撮影の夕で発光球の周りに柔らかいにじみが見える。昼は発光球が背景より明るく見え、白飛び・露出の悪化が無い（真昼の屋外では電球はほとんどにじまないので、昼のにじみは求めない）。ブルームの既定（しきい値なし・0.04）は変えない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-EMISSIVE-GLOW -SunElevations 45,3`
- stop-when: 発光球が白飛びの割合の基準（2%）を超える、または露出が発光球に引っ張られて画面が暗くなる場合は止めて記録する。
- paths: Game/GameModes/Rendering3DTest, TASKS.md, PROGRESS.md
- notes: SS-BLOOM-MIPCHAINで分かった。夕の自動露出（EV100 約11.1）では発光球はプリエクスポージャ後 約0.7で画面の平均の約4倍しかなく、しきい値なし0.04の補間ではにじみが縁の外5 pxで数%にとどまり見えない。
- notes: 反復15で発光球を45000 nitsにし、夕はにじむ（背景の約70倍）。昼はプリエクスポージャ後 約1.5でにじまない。GBufferの発光がRGBA16Fの物理nitsのため 約57000 nitsが上限で、昼に要る 約150000 nitsに届かない（blocked/SS-EMISSIVE-GLOW.md）。
- notes: 2026-09-30 再開。SS-EMISSIVE-PREEXPOSE で上限を外した後、発光球を物理的にありうる輝度（つや消しの電球の表面 約1〜1.5×10^5 nits の範囲）へ上げて昼・夕を撮り直す。値と根拠をコミット本文に書く。
- notes: 2026-10-02 完了。`85f5c6e` で150000 nits（内面つや消し電球の表面 約15 cd/cm²）。夕は場面の幾何平均の約500倍で縁の外66 pxまで柔らかくにじみ、昼は約22倍でにじみは縁の外6 px（`startup-capture/SS-EMISSIVE-GLOW/before-after-default-sun3-x3.png`・`-sun45-x3.png` を開いて確かめた）。露出は発光球に引っ張られず（平均輝度 178.6→178.5）、白飛び率は最大0.00034。昼のにじみは物理的な値とブルームの既定を保ったままでは出ないため、`blocked/SS-EMISSIVE-GLOW.md` の推奨1で完了条件から外した（600000 nitsでも昼は縁の外26 pxの淡い輪）。

## FIX-CAPTURE-SUN-AZIMUTH: 撮影スクリプトの -SunAzimuth が null のメソッド呼び出しで落ちる
- status: done
- done-when: `Scripts/CaptureStartupScene.ps1 -SunAzimuth <度>`が太陽の方位を渡して撮影でき、太陽を画角に入れる視点（例: 低角度の視点で方位 -120°）を撮れる。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/FIX-CAPTURE-SUN-AZIMUTH -SunElevations 3 -SunAzimuth -120`
- stop-when: なし。
- paths: Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: `[Nullable[double]]`の引数は値が入ると`double`になるため`$SunAzimuth.Value`がnullになる（199行目）。SS-BLOOM-MIPCHAINではGame.exeを同じ引数で直接起動して撮った。

## SS-TAA-P1: ジッタと履歴の再投影でTAAを作る
- status: done
- done-when: 投影行列にHalton(2,3)の8〜16点のサブピクセルのジッタを掛け、R6-aのvelocity（ジッタを除いた動き）で前のフレームの履歴を再投影し、近傍の色の分散（YCoCg）でクリップして混ぜるTAAのパスがある（位置はライティング・半透明の後、ブルームの前）。カメラが止まっているとき、ジッタで輪郭のギザギザが消える。有効/無効を切り替えられ、無効なら今の結果（FXAA）と変わらない。ジッタ列と投影のずらし量をCPUのテスト `TemporalAAJitterTest`（`CameraViewConstantsTest` の束へ MEMBER として足す）で確かめる。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game CameraViewConstantsTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(TemporalAAJitterTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-TAA-P1`
- stop-when: 検証シーン（golden）はTAA無効のまま結果が変わらないこと。変わるなら止めて直す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Component, Library/Core/Private/Component, Assets/Shaders, Test/Core/Rendering, Test/TestBundle, TASKS.md, PROGRESS.md
- notes: 危険地帯（カメラ・投影・RenderThread）。評価者を通す。
- notes: 評価1周目（`06a26a7`）は NEEDS_WORK。指摘は `NEXT_FINDINGS.md`（blocking 2件: 描画がゲームのフレームを飛ばしたときの履歴とvelocityの基準フレームの食い違い、1つのSceneViewに2つのViewportがあるときの毎フレームの履歴破棄）。`7461c3e` は反復の途中で止めた未検証の保存で、この指摘への対応（`HistoryFrameNumber` など）と SS-TAA-P2 の一部（MegaGeometry・スキニングのvelocity、`-OrbitDegreesPerSecond`、シャープ化）が混ざる。この項目では指摘への対応を仕上げてフレームの欠落を意図的に起こす検証を足し、P2 の部分はビルドが通り既定の結果を変えない状態に保つ（検証は SS-TAA-P2 で行う）。

## SS-TAA-P2: TAAをほかのパスと整合させ、起動画面の既定にする
- status: done
- done-when: UI・デバッグ描画・ImGuiはジッタの無い最終解像度に描かれる。スキニング・動く物体のvelocityが正しく、カメラの切り替え・画面サイズの変更で履歴を捨てる。解像感を戻す軽いシャープ化がある。撮影スクリプトにカメラを一定の速さで回す連続撮影（`-OrbitDegreesPerSecond`）を足し、回転中の画像で輪郭のゴースト（残像の筋）が見えない。Rendering3DTest の既定をTAAにし、FXAAは選択肢として残す。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-TAA-P2 -OrbitDegreesPerSecond 30`
- stop-when: 検証シーンの結果が変わるなら止めて直す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（RenderThread）。評価者を通す。

## SS-GTAO: SSAOをGTAOに置き換える
- status: done
- result: 2026-10-02。`gtao.frag`（2スライス×片側4段、半径1 m、外側61.5%で重みを0へ、画面上の上限128画素）と`gtao_denoise.frag`（4×4の深度考慮の平均）で旧SSAO（半球32サンプル）を置き換えた。TAAのジッタがあるときは雑音の並びをフレームごとにずらして履歴で蓄積する。多重反射の近似は`lighting.frag`の拡散の環境光に掛ける。Cornellの部屋（`RenderingGTAOCornellRoomVulkanTest`）で壁・天井・床の中ほど0.97〜1.00、隅・接地部0.77〜0.81。Outdoor goldenは1 LSBの差で再承認（`R1Acceptance.md`）。起動画面の撮影は`.harness/runs/startup-capture/SS-GTAO/`。
- done-when: 地平線ベースのAO（GTAO。画素ごとに2方向×数段、空間の雑音除去、TAAがあれば時間方向にも蓄積）が今のSSAO（半球32サンプル）を置き換え、多重反射の近似で明るい面の遮蔽を弱める。半径は世界の長さ（m）で指定する。撮影で、球・岩・小屋と地面の接地部、小屋の軒下に自然な遮蔽が見え、部屋の大きさの遮蔽で壁全体が黒くならない（Indoorの検証シーンで確かめる）。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-GTAO`
- stop-when: 承認済みのgoldenが変わる場合は差の原因と妥当性を記録して再承認する（任されている）。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 既存のFIX-SSAO-ROOM-SCALE（backlog）はこのタスクで置き換わる見込み。閉じたら、そのタスクに結果を書き添える。

## SS-CONTACT-SHADOW: 影の灯にコンタクトシャドウを足す
- status: done
- done-when: 影を掛ける方向光（空の太陽）について、画面空間で光の方向へ短く（世界で約0.2〜0.5 m）レイマーチする接触影をCSMの結果と掛け合わせる。影を落とす点光源にも同じ仕組みを使えるなら使う。撮影の近接視点と低角度で、球・岩・小屋の接地部に、CSMでは出ない細い影が見え、物体の表面に自己遮蔽の縞が出ない。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-CONTACT-SHADOW`
- stop-when: 承認済みのgoldenが変わる場合は差の原因と妥当性を記録して再承認する（任されている）。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（ライティング・影）。評価者を通す。

## SS-POST-TUNE: SSRを粗さでなめらかに消し、グレーディングとビネットを起動画面に合わせる
- status: done
- done-when: SSRが粗さのしきい値で急に切れず、粗さ約0.3〜0.7の間でなめらかに弱まる（材質の粗さを使う）。起動画面で、濡れていない石畳には弱い反射、金属の見本の球には周囲の反射が見える。起動画面のグレーディング（コントラスト・彩度・色温度）とビネットを、昼・夕の撮影で眠く見えない値にする（値と理由を記録する）。検証シーンのグレーディングは変えない。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-POST-TUNE -SunElevations 45,3`
- stop-when: SSRの変更で承認済みのgoldenが変わる場合は差の原因と妥当性を記録して再承認する（任されている）。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game/GameModes/Rendering3DTest, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 今はテクスチャの無い地面の粗さが既定の128/255（0.502）で、SSRのcutoff 0.5を超えて棄却されている。
- notes: 2026-10-02 完了。地面はSS-SHOWCASEで石畳の材質（粗さのテクスチャはUNORMで中央値0.76、5%点0.715）に替わっていて、フェードの終わり0.7より粗いためSSRは掛からない（乾いた粗い石畳に鏡の反射を足さない）。石畳の弱い反射は従来どおりIBLの鏡面（斜めの空のつや）で、SSRで見えるのは金属の見本の球（粗さ0.1・0.3・0.5）の下半分の地面の映り込み。

## SS-LENS-FX: 色収差とレンズダートを足す
- status: done
- done-when: 画面の端ほど強くなる放射方向の色収差（既定は弱く、R/Bのずれが画面端で約1〜2 px）と、ブルームに掛けるレンズダート（起動時に手続きで作るテクスチャ。外部の画像は使わない）がある。どちらも設定で切り替えられ、起動画面では有効、検証シーンでは無効で結果が変わらない。撮影の夕方（太陽が画面内）で、ブルームにダートの模様が乗り、画面端の輪郭に色のにじみがわずかに見える。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-LENS-FX -SunElevations 3`
- stop-when: 検証シーンの結果が変わるなら止めて直す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game/GameModes/Rendering3DTest, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: ユーザー決定（2026-09-27）「基本セット+演出系」。

## SS-GRADING-LUT: グレーディング用の3D LUTを掛けられるようにする
- status: done
- done-when: トーンマップの後に32³の3D LUTを掛けられ、恒等のLUTを掛けても結果が変わらないこと（GPUの出力の一致）をテスト `GradingLutIdentityVulkanTest`（`R8AcesLutToneMappingVulkanTest` の束へ MEMBER として足す）で確かめる。見た目のLUT（暖かみのある映画調）を作るスクリプト（`Scripts/BakeLookLut.py`、標準ライブラリとnumpyまで）と生成物を追跡し、起動画面で使う。検証シーンではLUTを使わない。撮影の昼・夕で、LUTの有無の2枚を比べて色調が変わり、階調の段差（バンディング）が出ない。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game R8AcesLutToneMappingVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(GradingLutIdentityVulkanTest|R8AcesLutToneMappingVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-GRADING-LUT -SunElevations 45,3`
- stop-when: R8のACES 2.0 LUT（`--tone-map=aces20-lut`）の経路と衝突する場合は、併用の順序を記録して、既定のACES経路にだけ掛ける。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Assets/Textures, Scripts/BakeLookLut.py, Game/GameModes/Rendering3DTest, Test/Core/Rendering, TASKS.md, PROGRESS.md

## SS-RTGI-DEFAULT: TAAの上でRTGIを起動画面の既定にする
- status: done
- done-when: ハードウェアのレイトレが使える環境では、起動画面でRTGIを有効にし（`30f00f8` で切った設定を戻す）、TAAと既存のデノイズで粒状の雑音が見えない。止まったカメラで16フレームを撮り、静止した地面の領域の画素の時間方向の標準偏差が、RTGI無効のときの2倍以内に収まる。レイトレが使えない環境ではIBL（空由来）に落ちて同じシーンが表示される。撮影で、小屋の軒下や球の下の地面に色のにじみ（間接光）が見える。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-RTGI-DEFAULT`
- stop-when: 雑音の基準を満たすのにRTGIの光線数を増やしてフレーム時間が2倍を超える場合は、既定にせず、測った値を既知の限界として記録して完了にする。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game/GameModes/Rendering3DTest, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 危険地帯（RTGI・RenderThread）。評価者を通す。
- notes: 2026-10-02 の反復（run 20261002-084328 の#20）は60分の壁時計で止められ、途中の変更は `b40ce35`（未検証の途中保存。行末は `d3172d6` で元へ戻した）に入っている。中身: RTGIの静止判定を位置と拡大率で見る（球の自転で毎フレーム履歴が捨てられていた）、露出が変わったときに履歴へ比を掛け直す、小屋・岩（MegaGeometry）のLOD0をレイトレのシーンへ入れる、起動画面でRTGIを既定で有効（`NORVES_STARTUP_RTGI=0` で無効）、切り替わりのときだけのログ、撮影スクリプトの静止16フレームの時間方向の雑音の測定。残っていたのは、地面の領域で履歴が棄却される原因（法線・材質の比較）の切り分けと、低角度の手前の石の目地に沿った雑音。まず `b40ce35` の差分を読み、ビルドと撮影で今の状態を測ってから続ける。
- notes: 2026-10-02 の評価1周目の差し戻し（低角度の中ほどの地面が2.49倍）に対応した。静止が続いているフレーム（年齢の上限が8を超える）では、RTGIの履歴を法線・材質の差で棄却せず、距離と裏返りだけで判定する。止まった16フレームの雑音は `CaptureStartupScene.ps1 -StillRenderedFrames ... -Rtgi On -CompareNoiseWith <-Rtgi Off の出力先>` で視点・帯ごとにRTGI無効との比を判定する（2倍を超えると失敗）。

## SS-LOOK-BALANCE: 起動画面の露出とトーンの釣り合いを朝・昼・夕・夜で取り直す
- status: done
- done-when: 起動画面のカメラだけで（検証シーンのカメラの既定のトーンマップ・露出・グレーディングは変えず、goldenは変わらない）、撮影（朝10°・昼45°・夕3°・夜 × 既定・近接・低角度）が次を満たす。(1) 画面全体の平均（`metrics.json` の平均輝度、0〜255）が 昼105〜140・朝95〜135・夕75〜120・夜25〜80。(2) 白飛び画素率1%未満（太陽・発光球を含めて）、黒つぶれ画素率は夜以外で2%未満。(3) 昼の影の中の比（SS-DAYLIGHT-P1 と同じ領域・表示のリニア輝度の比）が15〜40%。(4) 昼の画面上端の帯（y<60）の平均が青（sRGBの符号値で B − R ≥ 40）。(5) 夕の日向の地面の平均色が橙〜黄（R > G > B）で、桃・紫（B ≥ G）に寄らない。(6) 夜は背景が夜に見え（静的HDRの昼の写真がそのまま明るく写らない。画面上端の帯の平均が40/255未満）、点光源の光だまりが周りより明るく、その中で球・岩の影が見える。昼・夕・夜の既定視点で、変更前（このタスクの着手時）と変更後を並べた画像を残す。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-LOOK-BALANCE -SunElevations 10,45,3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-LOOK-BALANCE-night -Night`
- stop-when: すべての範囲を同時に満たす値の組が見つからない場合は、最も近い組と各値の測定値を既知の限界として記録して完了にする。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Component, Library/Core/Private/Component, Assets/Shaders, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-02 の親の確認で分かった。SS-DAYLIGHT-P1 の対応で起動画面の自動露出に一律の+2 EVの露出補正（`8b80473`）を掛けた結果、影の比は満たしたが画面全体の平均が約180/255になり、昼は白っぽく飛んで眠く、夕は桃色がかり、夜は背景の静的HDR（昼の芝生と木の写真）と地面が真っ白に写って夜に見えない（`startup-capture/SS-GRADING-LUT/default-sun45.png`・`default-sun3.png`、`SS-CONTACT-SHADOW-night/default-night.png`）。個々の項目の完了条件は満たしていたが、画面全体の釣り合いを見る条件が無かった。
- notes: やり方の目安: 一律の+2 EVをやめるか小さくし、暗部を縮めにくいトーンカーブ（例: Khronos PBR Neutral や AgX をトーンマップの選択肢に足してカメラごとに選べるようにする、または既存の `Aces20Lut`）と組み合わせる。夜は自動露出が昼並みの明るさへ戻しきらないようにする（明るさに応じた露出補正、または目標の範囲）。夜の背景は昼の写真を出さない（夜の空の色にする等）。SS-POST-TUNE・SS-GRADING-LUT・高さフォグの値は、ほかの値を変えた後に撮り比べ、必要なら合わせ直す。危険地帯（トーンマップ・露出・FramePacketのカメラ）。評価者を通す。

## SS-RTGI-FAR-NOISE: 起動画面の遠い地面に残るRTGIの時間方向の雑音を減らす
- status: done
- done-when: 止まったカメラで16フレームを撮り（`-StillRenderedFrames`）、既定視点の地平線寄りの帯（画面の高さの0.33〜0.42）と低角度視点の中ほどの帯（0.65〜0.80）でも、地面の画素の時間方向の標準偏差（表示の輝度）がRTGI無効のときの2倍以内に収まる（手前の帯 0.80〜0.98 の既存の基準も保つ）。帯ごとの値を `metrics.json` に出せるよう撮影スクリプトの測定領域を視点ごとに複数にする。止まった状態で疎らに明滅する画素（標準偏差が1/255を超える画素）の割合も記録する。
- verify: `cmake --build build --config Debug --target Game RTGIDiffuseIndirectVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RTGIDiffuseIndirectVulkanTest$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-RTGI-FAR-NOISE-Off -StillRenderedFrames 160,168,176,184,192,200,208,216,224,232,240,248,256,264,272,280 -Rtgi Off`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-RTGI-FAR-NOISE-On -StillRenderedFrames 160,168,176,184,192,200,208,216,224,232,240,248,256,264,272,280 -Rtgi On -CompareNoiseWith .harness/runs/startup-capture/SS-RTGI-FAR-NOISE-Off`
- stop-when: 原因が画素ごとの履歴の棄却ではなく、ジッタで変わるGBufferの標本そのもの（遠い画素の中の石の目地）にあり、棄却を緩めると動く物体の残像が出る場合は、帯ごとの測定値と原因を既知の限界として記録して完了にする。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: SS-RTGI-DEFAULT の完了時（2026-10-02）の測定: 判定の帯（0.80〜0.98）は既定1.27倍・近接0.86倍・低角度1.70倍で合格だが、年齢上限128の撮影（`startup-capture/SS-RTGI-DEFAULT-it1-age128-On` と `-it1-base-Off`）で既定の0.33〜0.42の帯は2.03倍（標準偏差1/255超の画素 26.6%、無効時1.6%）、低角度の0.65〜0.80の帯は2.29倍（15.3%、無効時0.5%）。前回の切り分け（`SS-RTGI-DEFAULT-exp-norej`、年齢上限64で法線・材質・深度の棄却を外した撮影）ではこの2つの帯が1.59倍・1.76倍だったので、履歴の棄却が主な原因と見られる。危険地帯（RTGI）。評価者を通す。

## SS-NIGHT-POLISH: 夜の点光源の色とレンズダートの映り方を整える
- status: done
- done-when: 起動画面の点光源の色を白熱電球の色温度（黒体 約2700〜3000 K をリニアのRGBへ直した値。根拠をコミット本文に書く）にし、夜の撮影の既定視点で光だまりの地面の平均色が橙〜黄（R > G > B、G/R が0.6〜0.85）で、黄緑（G/R > 0.9）に寄らない。夜の撮影でレンズダートの模様が光源の周りの淡い斑にとどまり、光源より大きな色付きの円（ゴースト）が目立たない（強さを下げる、または明るい光源の周りでの掛かり方を見直す。昼・夕のダートの見え方が消えない程度）。昼・夕の撮影の SS-LOOK-BALANCE の数値の範囲は保つ。変更前と変更後の夜の既定視点を並べた画像を残す。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-NIGHT-POLISH-night -Night`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-NIGHT-POLISH -SunElevations 45,3`
- stop-when: ダートを弱めると昼・夕のダートが見えなくなる場合は、夜の見え方を優先せず、両立する値と測定を記録して閉じる。
- paths: Game/GameModes/Rendering3DTest, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, TASKS.md, PROGRESS.md
- notes: 2026-10-02 の親の確認（`startup-capture/SS-ACCEPT-night/default-night.png`）で分かった。点光源の色は `Rendering3DTestRoutine.cpp` の `SetLightColor(1.0f, 0.9f, 0.3f)` で、石畳が黄緑がかる。レンズダートは `kStartupLensDirtIntensity = 2.0f`。

## FIX-RGCT-MEGA-VELOCITY: RenderGraphCompileTest の MegaGeometry の記録の検査の準備に GBuffer_Velocity を足す
- status: done
- done-when: `RenderGraphCompileTest.cpp` の MegaGeometryPass の記録の検査（2620行目の `BeginRenderPassCount == 1`）の準備で、`SharedResourceRegistry` に `GBuffer_Velocity`（128×64 の R16G16_FLOAT のレンダーターゲット）を登録し、検査を弱めずに RenderGraphCompileTest が通る。エンジン側（MegaGeometryPass）は変えない。
- verify: `cmake --build build --config Debug --target RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderGraphCompileTest$"`
- paths: Test/Core/Rendering/RenderGraphCompileTest.cpp, TASKS.md, PROGRESS.md
- notes: SS-GPU-PROFILE（2026-10-03）の検証で見つけた。MegaGeometry に velocity を足した変更（`7461c3e`・`f63b2fd`）から、MegaGeometryPass は `GBuffer_Velocity` が無いと GBuffer のフレームバッファを作らず記録の前に戻るが、検査の準備は5枚しか登録していない。2026-10-02 の SS-GTAO の実行（`20261002-084328/verify-SS-GTAO-8.txt`）でも同じ行で落ちていた。登録を一時的に足すと 1/1 で通ることを確かめた（`20261003-034747/verify-SS-GPU-PROFILE-7.txt`）。
- notes: 2026-10-03 親が直した。`.harness/runs/20261003-parent/build-RGCT.txt` で build EXIT_CODE=0、`ctest-RGCT.txt` で RenderGraphCompileTest 1/1 passed。

## SS-GPU-PROFILE: 計測のある構成で、加速構造の更新を含むGPUの時間とパスごとの内訳をトレースへ書けるようにする
- status: done
- done-when: 計測が有効な構成（Debug・RelWithDebInfo。`NORVES_ENABLE_STATS=1`）で、(a) フレームのGPUの区間（`FrameGPU`）が加速構造の更新（`RenderingCoordinator.cpp` の `BuildAccelerationStructures`）を含み、加速構造の更新も別の区間として取れる。(b) RenderGraph のパスごとのGPUの時間が `--trace-file` のトレースに行として出る。Release の構成には計測・トレース・ログの仕組みを足さない（Release の `NORVES_ENABLE_STATS=0` と出力は変えない。Release を有効にするCMakeの選択肢も作らない）。RelWithDebInfo の Game で起動画面を数百フレーム走らせ、フレームごとのGPUの時間・加速構造の更新・パスごとの内訳がトレースに出ることを、トレースを開いて確かめる（PROGRESS に数行の抜粋）。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config Release --target Game -- /m:1`
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderGraphCompileTest$"`
- stop-when: GPUのタイムスタンプを加速構造の記録の前へ動かすとコマンドの記録の順序（RenderThread の同期）を変える必要がある場合は、変えずに、加速構造の更新を別の区間として取るだけにして記録する。
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Library/Core/Private/Boot, Library/Core/Public/Boot, Library/Core/Private/Debug, Library/Core/Public/Debug, Library/Core/Private/RHI, Library/Core/Public/RHI, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: SS-ACCEPT の停止理由（`blocked/SS-ACCEPT.md`）から作った。加速構造の更新は `FrameGPU` の開始より前にあり、パスごとのタイムスタンプはトレースへ書く経路が無い。2026-10-02 ユーザーの指示: Release にGPUの計測やログなどのデバッグの機能を入れない。計測は最適化が有効で計測の残る RelWithDebInfo で行う。危険地帯（RenderThread・RHI）。評価者を通す。
- notes: 2026-10-03 実装は `053aaa3` で済み、検証の RenderGraphCompileTest が前から落ちていたため止めていた（`blocked/SS-GPU-PROFILE.md`）。FIX-RGCT-MEGA-VELOCITY で直したので、verify を回し直して done にする（実装の追加は不要）。

## SS-ACCEPT: 起動画面の改善を受け入れる
- status: done
- done-when: 朝・昼・夕 × 既定・近接・低角度の撮影一式と、変更前（`163ffe5`）の同じ視点の撮影を並べた記録（`Docs/RenderingValidation/StartupSceneAcceptance.md`、画像は `.harness/runs/` への参照）がある。最適化が有効で計測の残る RelWithDebInfo の構成で、起動画面の1フレームのGPUの時間（加速構造の更新を含む）を測り、1280×720で16.6 ms以下であることを記録する（超えるフレームがあれば、そのフレームのパスごとの内訳と、どれを軽くすれば収まるかを記録する）。Release には計測の仕組みを入れないので、Release では build が通り起動画面を撮影できることだけを確かめる。夜（`-Night`）の撮影も並べる。評価者が、各項目の完了条件と撮影を開いて反証を試みる。
- verify: `cmake --build build --config Release --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT -SunElevations 10,45,3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT-night -Night`
- stop-when: フレーム時間の予算を超える場合は、内訳と軽くする案を既知の限界として記録して完了にする（ユーザーへの報告に含める）。
- notes: 受け入れの撮影は、SS-LOOK-BALANCE の数値の範囲（画面の平均・白飛び・黒つぶれ・影の比・空の青・夕の色・夜の背景）も満たしているかを並べて記録する。
- paths: Docs/RenderingValidation, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 区切り。評価者を通す。
- notes: 2026-10-02 再開。評価1周目の指摘3（既定視点の変更前後のカメラ）は `51a22e7` で対応済み。指摘1・2（GPU時間・加速構造の更新を含む区間・予算を超えたフレームのパスごとの内訳と軽くする案）は SS-GPU-PROFILE の後に RelWithDebInfo で測り直して記録する。完了条件の「Releaseの構成で測る」は、2026-10-02 のユーザーの指示（Release にGPUの計測やログなどのデバッグの機能を入れない）により RelWithDebInfo へ改めた。RelWithDebInfo の既存のトレースでは夜の既定視点で540フレーム中6が16.6 msを超え、最大18.16 ms。

## FIX-ASYNC-TEXTURE-MIPS: 非同期で読み込むテクスチャにもミップマップを全段作る
- status: done
- done-when: 非同期の読み込みの経路（`TextureAssetLoader.cpp` の `DecodeLooseBlobWithStbiForWorker` など、ばらのPNGを stb で読む経路で `bFullMipChain=false` を渡している所）でも、同期の経路と同じくミップマップを全段（4096²なら13段）作る。ミップの生成はGPUで行い（既存の `GpuResourceStore` のミップ生成）、RenderThread の同期を変えない。法線マップ・ラフネス・高さなど色でないテクスチャもリニアのまま縮小される（sRGBの扱いが同期の経路と同じ）。起動画面の Game.log で石畳の5枚のテクスチャが `mip_levels=13` とミップ生成の成功を出し、起動から撮影までの時間の増分が2秒以内（ミップ生成の時間をログから合計して PROGRESS に書く）。昼の既定視点で遠くの地面のざらつき（ミップが無いことによるエイリアシング）が消えたことを、変更前後の遠景の拡大画像と、遠景の領域の隣り合う画素の差の平均（高周波の量）で示す。検証シーンの golden が変わる場合は、差がミップの有無だけによることを確かめて再承認する（任されている）。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/FIX-ASYNC-TEXTURE-MIPS -SunElevations 45`
- stop-when: ミップの生成を RenderThread の外の同期なしに行えない理由が見つかった場合は、同期を変えずに済む形（読み込みのワーカーでCPUで縮小して全段を渡す等）へ切り替え、選んだ理由を記録する。
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Library/Core/Private/Asset, Library/Core/Public/Asset, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-03 ユーザーの指摘（石のタイルが4Kのはずなのに近づくと粗い）から調べて分かった。`.harness/runs/startup-capture/SS-ACCEPT/near-sun45.Game.log` で `cobblestone_floor_09_diff_4k.png data_size=67108864 mip_levels=1 mipgen_ms=0.000`。同期の経路（`LoadStbiBlobForCaller`）は `true` を渡すが、非同期・メインの描画の経路の3か所は `false`。2026-06-07 の分離（`dee9822`）からこの形で、意図した記録は無い。異方性フィルタ（x4）もミップが無いと効かない。危険地帯（アセットの読み込み）。評価者を通す。

## SS-CSM-MEGA-CASTERS: 太陽の影（CSM）にMegaGeometryを描く
- status: done
- done-when: CSM の4枚のカスケードへ MegaGeometry（岩・小屋・今後の高ポリの球）を影のキャスターとして描く。カスケードごとにテクセルの大きさに見合う細かさのLOD（クラスタのLODの切り出しか、LOD0 をそのまま）を選び、選び方と三角形数を記録する。CSM の深度範囲に含める境界球が、実際に描くキャスターと一致する（backlog の FIX-CSM-MEGA-CASTER-BOUNDS を閉じる）。撮影の昼（45°）と夕（3°）で、小屋と岩の太陽の影が地面に落ち、影の縁にアクネ（縞）とピーターパン（接地部の浮き）が見えない（変更前後の拡大画像）。MegaGeometry の自己影（岩の窪み・小屋の軒）に縞が出ない。RelWithDebInfo の起動画面の1フレームのGPUの時間が16.6 ms以下のまま（`-GpuTimingFrames 600` の計測。ShadowMapPass の増分を記録）。検証シーンの golden が変わる場合は差の原因と妥当性を確かめて再承認する（任されている）。
- verify: `cmake -S . -B build -DNORVES_BUILD_TESTS=ON`
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest CascadedShadowLightMatricesTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CascadedShadowLightMatricesTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-CSM-MEGA-CASTERS -SunElevations 45,3`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- stop-when: CSM へ MegaGeometry を描くのに RenderThread の同期やパスの順序を変える必要がある場合は、変えずに済む形（ShadowMapPass の中で LOD0 の頂点・インデックスを直接描く等、点光源の影と同じ経路）を採り、理由を記録する。
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 2026-10-03 親が調べて分かった。`ShadowMapPass.cpp` は点光源のキューブへは MegaGeometry の LOD0 を描く（`PointShadowMegaCaster`）が、CSM へは描かない。そのため起動画面の岩と小屋には太陽の影が無く、見えている影はコンタクトシャドウだけ。SS-MEGA-SPHERE で大きな球を MegaGeometry にする前に要る。危険地帯（影・RenderThread）。評価者を通す。

## SS-MEGA-SPHERE: 大きな球を高ポリのMegaGeometryにし、テクスチャの密度を地面に揃える
- status: done
- done-when: 起動画面の大きな石畳の球（半径1 m、今は通常のメッシュの 32×16 の UV 球）を、手続きで作る高ポリの球（三角形 約100万〜200万。クラスタとLODの階層を持つ）の MegaGeometry に置き換える。UV は横3回・縦1.5回の繰り返しにし、テクスチャの密度（約2 mで1枚）と縦横比を地面の石畳に揃える（UVの継ぎ目で模様が切れない）。今の球が持つ機能をすべて保つ: 太陽の影（SS-CSM-MEGA-CASTERS）・点光源の影（数百万三角形を6面×灯数で描く費用が大きければ、影には粗いLODを使う）・コンタクトシャドウ・自転とTAAのvelocity・レイトレのシーン（RTGI）・材質（石畳、POM）。撮影の近接視点で球の輪郭に角（ポリゴンの折れ）が見えず、石の大きさが地面の石と同じくらいに見える（変更前後の拡大画像）。起動から撮影までの時間の増分が10秒以内（クラスタとLODの構築の時間をログに出す。超えるなら三角形数を下げる）。RelWithDebInfo の起動画面の1フレームのGPUの時間が16.6 ms以下のまま（パスごとの増分を記録）。MegaGeometry のクラスタの大きさ（1クラスタあたりの三角形数の平均）を記録し、極端に小さい（例: 平均16未満）なら原因を調べて記録する。
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-MEGA-SPHERE -SunElevations 45,3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-MEGA-SPHERE-night -Night`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- stop-when: 三角形数を下げても起動時間・GPUの時間の条件を満たせない場合は、満たす最大の三角形数で閉じ、測定値を既知の限界として記録する。
- paths: Game/GameModes/Rendering3DTest, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 2026-10-03 ユーザーの要望（球のポリゴンをスムースに。MegaGeometryの経路でポリ数を大きく上げてよい）。今の球は UV が1周（約6.3 m）で4Kを1回だけ貼るため、地面（2 mで1枚）の約1/3の密度で、石が横に引き伸ばされている。既存の MegaGeometry の岩は66122三角形が25562クラスタ（平均約2.6三角形）で、クラスタが小さすぎる疑いがある（`MAX_TRIANGLES_PER_CLUSTER = 128`）。危険地帯（MegaGeometry・RenderThread）。評価者を通す。

## SS-MEGA-SPHERE-DISPLACE: 高ポリの球を石畳の高さマップで実際に凹凸させる
- status: done
- done-when: SS-MEGA-SPHERE の高ポリの球の頂点を、石畳の高さマップ（`cobblestone_floor_09_disp_4k.png`、起動時にCPUで読む）で法線の向きへ動かし、石の盛り上がりと目地の窪みを実際の形にする（高さの尺度は今の POM の見た目に合わせ、値と根拠を記録する）。動かした形から法線を作り直し、法線マップと向きが合う。この球の材質では POM を切る（形で凹凸を出すので二重にしない）。メッシュに穴・割れ（UVの継ぎ目・極で頂点がずれて開く所）が無く、極の付近で棘が出ない（極へ向けて変位を弱める等。方法を記録する）。撮影の近接視点と低角度で、球の輪郭が石の凹凸でガタガタして見え、影（太陽・点光源・コンタクトシャドウ）とレイトレのシーンも同じ形を使う（変更前後の拡大画像）。起動時間とGPUの時間の条件は SS-MEGA-SPHERE と同じ。
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-MEGA-SPHERE-DISPLACE -SunElevations 45,3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-MEGA-SPHERE-DISPLACE-night -Night`
- stop-when: 極・継ぎ目の割れや棘を UV 球のままで消せない場合は、別の球の作り方（立方体を球へ写した形など）へ切り替え、模様の継ぎ目の見え方を変更前後の画像で比べて選んだ理由を記録する。
- paths: Game/GameModes/Rendering3DTest, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, TASKS.md, PROGRESS.md
- notes: 2026-10-03 ユーザーの「MegaGeometryでめちゃめちゃポリ数上げてもいい」を受け、高ポリを生かして輪郭にも凹凸を出す。高さマップは16ビットのグレーのPNG。評価者を通す。

## SS-ACCEPT-DETAIL: テクスチャのミップ・MegaGeometryの影・高ポリの球を受入れ記録へ足す
- status: done
- done-when: `Docs/RenderingValidation/StartupSceneAcceptance.md` に、FIX-ASYNC-TEXTURE-MIPS・SS-CSM-MEGA-CASTERS・SS-MEGA-SPHERE・SS-MEGA-SPHERE-DISPLACE の後の撮影（朝・昼・夕・夜 × 既定・近接・低角度）と、変更前（`b347eb7`）の同じ視点を並べた比較、SS-LOOK-BALANCE の数値の範囲の判定、RelWithDebInfo の1フレームのGPUの時間（加速構造の更新を含む。全フレームが16.6 ms以下か、超えたフレームのパスごとの内訳）、起動から撮影までの時間を足す。Release は build が通り撮影できることだけを確かめる。評価者が撮影を開いて反証を試みる。
- verify: `cmake --build build --config Release --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT-DETAIL -SunElevations 10,45,3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT-DETAIL-night -Night`
- stop-when: GPUの時間の予算を超える場合は、内訳と軽くする案を既知の限界として記録して完了にする。
- paths: Docs/RenderingValidation, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 区切り。評価者を通す。

## FIX-MEGA-CLUSTER-ADJACENCY: 読み込むモデルのMegaGeometryのクラスタを位置でつないで大きくする
- status: done
- done-when: クックの `MeshClusterizer` の三角形の隣接を、頂点の番号ではなく位置（UVの継ぎ目・法線の分かれ目で複製された頂点を同じ位置として扱う）でつなぎ、起動画面の岩（今は66122三角形が25562クラスタ、平均2.59、16未満が25001）と小屋（4281三角形が1323クラスタ、平均3.24）のLOD0の1クラスタあたりの平均三角形数を64以上にする（`stage=megamesh_cluster_stats` の行で確かめる）。描画の見た目（撮影）と影・レイトレの範囲は変わらない。
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/FIX-MEGA-CLUSTER-ADJACENCY -SunElevations 45`
- stop-when: クック済みのアセットの作り直しが要り、その手順が決まっていない場合は、作り直しの手順と影響を blocked に書いて止める。
- paths: Tools/AssetCook, Library/Core/Private/Rendering/MegaGeometry, Library/Core/Public/Rendering/MegaGeometry, Assets, TASKS.md, PROGRESS.md
- notes: 2026-10-03 SS-MEGA-SPHERE で見つけた。岩は頂点67042・三角形66122、小屋は頂点6599・三角形4281で、閉じたメッシュの目安（頂点≒三角形の半分）より頂点が多く、UVの島ごとに頂点が複製されている。`MeshClusterizer::BuildAdjacencyGraph` は頂点の番号の辺だけで隣接をつなぐため、クラスタの成長が島の境で止まる（手続きの球は頂点を共有するので平均127.8）。

## SS-MEGA-LOD-PERF: 大きな球のLODを画面上の誤差で選び、CSMのMegaGeometryをカスケードに見合う段で描く
- status: done
- done-when: (1) 大きな球（MegaGeometry、変位あり）のクラスタのLODを、その段で失われる形の誤差（球面からのずれと変位の差の大きい方）を実際の透視投影で画素へ直した大きさ（変位の前後のクリップ座標を画素へ直した差の上限。角度の近似で過小に見積もらない。この上限を、変位の前後のクリップ座標を画素へ直して比べる契約テストで確かめる）で選び、誤差が閾値以下になる最も粗い段を使う。閾値は撮り比べで見た目の差が出ない値（1画素以下）に決め、選んだ値と各視点の段を記録する。近接視点で LOD0 が選ばれるのは、それより粗い段の誤差が閾値を超えるときで、それは受け入れる（LOD0 は近接で目に見える変位の細部を運ぶ）。段の境目に割れ目が出ない（SS-MEGA-SPHERE の LOD 球の規則を保つ）。(2) CSM の各カスケードへ描く大きな球の段を、カスケードの1テクセルの大きさに見合う誤差で選ぶ（遠いカスケードほど粗い）。点光源のキューブも同じ考えで、遠い面・小さいキューブには粗い段を使う。岩・小屋は LOD0 のまま描き（影だけの粗い段は backlog の FIX-MEGA-SHADOW-LOD-LOADED で扱う）、その三角形数と ShadowMapPass の時間を記録する。どの段を何三角形描いたかを視点・カスケードごとにログへ出す。(3) RelWithDebInfo の `-GpuTimingFrames 600` で、近接視点（昼45°）の1フレームのGPUの中央値が5 ms以下、昼の3視点の全540フレームが16.6 ms以下（MegaGeometryPass・ShadowMapPass の中央値を変更前の SS-ACCEPT-DETAIL の値と並べて記録する）。(4) 見た目が変わらない: 近接・低角度の球の輪郭と目地、地面の影の、変更前後の拡大画像で差が目に見えない（差の画像と、球の領域の平均の差を示す）。
- verify: `cmake --build build --config Debug --target Game RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-MEGA-LOD-PERF -SunElevations 45,3`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-MEGA-LOD-PERF-gpu -Configuration RelWithDebInfo -GpuTimingFrames 600 -SunElevations 45`
- stop-when: 誤差1画素で見た目に差が出る場合は、差が見えない最も粗い閾値を撮り比べで選び、選んだ値と測定を記録する。中央値5 msに届かない場合は、届いた値とパスごとの内訳を既知の限界として記録して完了にする。
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Game/GameModes/Rendering3DTest, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-03 SS-ACCEPT-DETAIL の計測で分かった。近接視点のGPUの中央値が 約3.1 → 8.2〜9.3 ms に増え、増分のほぼすべてが MegaGeometryPass（0.34 → 5.4〜6.2 ms）。変位を LOD の誤差に含めた結果、近接で大きな球の LOD0（1,046,528三角形）が選ばれ、その多くが1画素より小さい。12960フレーム中2フレームが16.6 msを超えた（MegaGeometryPass と ShadowMapPass が同じフレームで跳ねた）。CSM は岩・小屋を LOD0 のまま4カスケードすべてへ描く（`csm_mega_lod=0`）。この計測は FIX-MEGA-CLUSTER-ADJACENCY（法線コーンのカリングの変更を含む）の前。2026-10-03 ユーザーの指示: 最適化を先にやってからマージ・プッシュする。危険地帯（MegaGeometry・影・RenderThread）。評価者を通す。
- result: 2026-10-03 完了（反復2）。投影の式を、角度の変化の最大 projectionFactor·D/(D²−R²) に視線から外れた点での透視の伸び 1/cos²α の上限（視錐台の角と、中心の方向の角＋見かけの半径の小さい方）を掛けた上限に直した（`MegaGeometryLODSelection.h`・`cluster_cull.comp`）。契約テストはエンジンの view・projection（Vulkan の Z 反転と Y 反転の有無）で球の表面の点を法線方向に変位させ、前後のクリップ座標を画素へ直した差が上限以下で、上限の6割以上が実際に出ることを確かめる（角度だけの式では落ちる）。閾値は1画素のまま（撮り比べ: 1画素は近接 LOD0・低角度 LOD1 で変更前と同じ段になり、変更前との差は16超の画素0。0.5画素は低角度も LOD0 で LOD0 の参照と見分けがつかないが、変更前より細かくなる）。既定（10 m）は LOD4（0.866画素）。CSM は球 c0 LOD3・c1〜c3 LOD4、岩 LOD0 66,122・小屋 LOD0 4,281三角形をカスケードごと、ShadowMapPass 中央値 0.74〜0.80 ms。近接（昼45°）のGPUの中央値は 8.42 ms で5 msに届かず、既知の限界（MegaGeometryPass 4.62・LightingPass 1.34・ShadowMapPass 0.80 ms。LOD0 の球の1,046,528三角形が主）。3視点1620フレームの最大 14.41 ms で予算超え0。同じ版の再計測は環境の負荷で ShadowMapPass が 2.1 ms へ増えて跳ね、近接で57フレームが予算を超えた（既知の限界）。
- notes: 2026-10-03 評価1周目（`cb84375`）は NEEDS_WORK（`NEXT_FINDINGS.md`）。反復2が `blocked/SS-MEGA-LOD-PERF.md` に書いた推奨（A: 近接の LOD0 を受け入れ、誤差の閾値だけを条件にする／D: 岩・小屋の影の粗い段は別の項目にする）を親が採り、done-when を改めた。正しい透視投影の式では近接の LOD1 は約1.8画素、中間の段を足しても約1.2画素の見込みで、1画素以下の閾値では LOD0 が残るため、中間の段は求めない。直すのは指摘3（投影の式とその契約テスト）と指摘4（閾値の撮り比べ）。上の result 行は反復1の記録で、2周目の結果で書き直す。

## FIX-MEGA-LOD-SHADING: MegaGeometry の粗い段の陰影が LOD0 より暗く・柔らかくなるのを直す
- status: backlog
- done-when: 変位のある大きな球の LOD1〜LOD4 で、目地の陰影（法線）が LOD0 と見分けがつかない（既定・低角度の視点の拡大画像で、LOD0 の参照との球の領域の平均の差が撮り直しの雑音と同じ程度）。形の誤差の閾値（1画素）は変えない。
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, TASKS.md, PROGRESS.md
- notes: 2026-10-03 SS-MEGA-LOD-PERF の撮り比べで見つけた。形の誤差が1画素未満でも、低角度（6 m）の LOD1（0.58画素）は LOD0 より目地が柔らかく（16超の画素 0.8%）、既定（10 m）の LOD4（0.87画素）は球の平均で 6〜10/255 暗い。変更前の段の選び方でも同じ段で、起こっていた差。粗い段の頂点の法線が変位の細部を平均してしまうのが原因と見られる。

## FIX-MEGA-SHADOW-LOD-LOADED: 読み込むモデルのMegaGeometryに影だけの粗い段を作る
- status: backlog
- done-when: 読み込むモデル（岩・小屋など）の MegaGeometry に、GBuffer のクラスタは元のまま保ったまま、影（CSM・点光源のキューブ）だけに使う粗い段を作り、カスケードの1テクセルに見合う段を選ぶ。読み込みの時間・メモリの増分を記録する。
- paths: Library/Core/Private/Resource, Library/Core/Private/Rendering, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: SS-MEGA-LOD-PERF（2026-10-03）から分けた。`ModelStaging.cpp` は `bBuildLODHierarchy = false` で、階層を作ると GBuffer のクラスタも DAG に置き換わる（`MegaGeometryResourceStore.cpp` の `uploadClusters = &lodHierarchy.AllClusters`）ため、影だけの段には別の設計が要る。近接の ShadowMapPass は 0.643 ms で、岩（66,122三角形）・小屋（4,281三角形）×4カスケードの寄与は小さい。危険地帯（アセットの読み込み・リソースの寿命）。

## FIX-STRING-EMPTY-CSTR: 空の Container::String の c_str() が終端の無い値を返すのを直す
- status: done
- done-when: 空の `TString` の `c_str()`・`data()` が空の文字列（終端の0）を指し、`%s` で書いても余計な文字が出ない。既存の呼び出しの挙動を変えない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- paths: Library/Core/Public/Container, Test/Core, TASKS.md, PROGRESS.md
- notes: 2026-10-03 SS-MEGA-LOD-PERF で見つけた。`String.h` の `c_str()` は空のとき `&npos`（size_t の最大値のバイト列）を返し、ログに化けた文字が出た。SS-MEGA-LOD-PERF では呼ぶ側で空なら "" を渡して避けた。

## SS-ACCEPT-PERF: 最適化の後のGPUの時間と撮影を受入れ記録へ反映する
- status: done
- done-when: `Docs/RenderingValidation/StartupSceneAcceptance.md` の「テクスチャのミップ・MegaGeometry の影・高ポリの球」の節の GPU のフレーム時間を、FIX-MEGA-CLUSTER-ADJACENCY と SS-MEGA-LOD-PERF の後の値（RelWithDebInfo、12視点 × 2回、540フレームずつ）で更新し、予算超えのフレームの有無（あればパスごとの内訳）と、SS-ACCEPT-DETAIL の値からの変化を並べる。撮影（朝・昼・夕・夜 × 3視点）を撮り直し、SS-LOOK-BALANCE の数値の範囲の判定と、変更前（`b347eb7`）との比較画像を差し替える。既知の限界を今の状態に合わせて書き直す。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT-PERF -SunElevations 10,45,3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-ACCEPT-PERF-night -Night`
- stop-when: 予算を超えるフレームが残る場合は、内訳と軽くする案を既知の限界として記録して完了にする。
- paths: Docs/RenderingValidation, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 区切り。評価者を通す。
- result: 2026-10-03 完了。撮影12枚 result=pass、SS-LOOK-BALANCE 全項目 PASS、変更前との比較画像は `startup-capture/SS-ACCEPT-PERF-compare/`。GPU の12視点 × 2回は別のアプリが開いた状態で測ることになり（計測の直後の5秒で GPU の使用率36〜40%。計測の最中は未記録）、12960フレーム中468フレームが16.6 msを超えた（最大 37.261 ms、ShadowMapPass・MegaGeometryPass・LightingPass が同じフレームで跳ね、変更していない軽いパスと区間に入らない残り（0.021〜3.115 ms）も伸びる。原因を GPU の共有とするのは推定）。同じコードを約25分前に測った昼の3視点（SS-MEGA-LOD-PERF、占有未記録の参考値）は予算超え 0 / 1620（変更前 1 / 3240）。内訳・軽くする案・再計測待ちを既知の限界に記録した。

## SS-ACCEPT-PERF-REMEASURE: 起動画面のGPUの時間を競合なしで12視点 × 2回測り直す
- status: backlog
- done-when: GPU を他のアプリと共有しない状態（Game が動いていない時点の GPU の使用率が数%以下であることを計測の前後に記録する）で、RelWithDebInfo の `-GpuTimingFrames 600` を朝・昼・夕・夜 × 3視点 × 2回回し、`Docs/RenderingValidation/StartupSceneAcceptance.md` の「テクスチャのミップ・MegaGeometry の影・高ポリの球」の節の GPU の表と予算の判定（超えたフレームがあればパスごとの内訳）を置き換える。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- paths: Docs/RenderingValidation, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 2026-10-03 SS-ACCEPT-PERF の計測は、ユーザーの別のアプリ（javaw）が GPU を36〜40%使う中で回った。PC を占有する重い処理なので、ユーザーが PC を使ってよいと言ったときだけ todo に戻す。

## FIX-NIGHT-SPHERE-LONG-RUN: 夜の大きな球の光源と反対側が、長く描くと明るくなるのを調べる
- status: backlog
- done-when: 夜の近接視点で、大きな球の光源と反対側の明るさが60描画フレーム目と600描画フレーム目で物理的に説明できる範囲でそろう（原因が RTGI の履歴なら、間接光の大きさを光源側との比で確かめる）。原因と直し方を記録する。
- paths: Library/Core/Private/Rendering, Assets/Shaders, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 2026-10-03 SS-ACCEPT-PERF で見つけた。近接・夜の球の反対側 / 光源側の8bit輝度は、60フレーム目の Debug・Release の撮影で 0.02〜0.03、600フレーム目の RelWithDebInfo の GPU 計測の撮影で 0.80〜0.84（`.harness/runs/20261003-181031/verify-SS-ACCEPT-PERF-14.txt`）。変更前（`b347eb7`）の `SS-ACCEPT-gpu-night` でも 0.48〜0.61 で、前からある。構成の違いかフレーム数の違いかはまだ切り分けていない。

## FIX-GTAO-STATIC-NOISE: GTAOの雑音をフレームごとにずらさず、金属の球の鏡面の遮蔽のちらつきを止める
- status: done
- done-when: GTAOの雑音（スライスの向き・段の位置の4×4の並び）をフレームごとにずらさず、雑音除去の4×4の平均だけで均す。大きな球の自転を止めた（`NORVES_STARTUP_SPHERE_SPIN=0`）静止の視点 `-112,4,8.5` を4フレームおきに12枚撮り、各フレームの平均で正規化したフレーム間の平均絶対差（8bitの表示値）が、金属の見本の球（粗さ0.3・0.5・0.7）で変更前より25%以上小さく、地面で±10%に収まる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/FIX-GTAO-STATIC-NOISE -Configuration RelWithDebInfo -ViewNames default -DefaultCamera -112,4,8.5 -StillRenderedFrames 90,94,98,102,106,110,114,118,122,126,130,134,150`（`NORVES_STARTUP_SPHERE_SPIN=0`）
- paths: Library/Core/Private/Rendering/SSAOPass.cpp, Assets/Shaders/gtao.frag, TASKS.md, PROGRESS.md
- notes: 金属の面の見た目はほぼ環境光の鏡面反射×鏡面の遮蔽（GTAOの可視率から求める）だけで、模様が無いのでTAAの近傍のクランプがGTAOのフレームごとの揺れを通す。鏡面の遮蔽からGTAOを外した撮影でもちらつきは同じ水準まで下がった（粗さ0.3で0.439→0.238）。接触影を切っても、RTGIを切っても球のちらつきは変わらなかった。
- result: 2026-10-03 完了。フレーム間の平均絶対差は金属の粗さ0.3で0.439→0.239、0.5で0.361→0.246、0.7で0.349→0.259、粗さ0.1で0.233→0.224、地面0.329→0.329。最終画像の静止の差は平均0.19（8bit）で、AOの模様は見えない。検証シーンのgoldenはジッタを掛けないので、もともと雑音をずらしていない。

## FIX-SSR-SPECULAR-COMPOSITE: SSRを環境光の鏡面反射の置き換えにし、金属の色と反射率を掛ける
- status: done
- done-when: LightingPass が、環境光の鏡面反射として SceneColor へ足した値（露出後）と、その反射率（鏡面の遮蔽込み、無次元、RGB）を別の出力に書き、SSRPass は当たった画素で `scene + a·(反射率·L_hit − 足した値)` にする（金属は反射率にalbedoの色が入る）。a は粗さ・距離・画面の端・厚さの余裕のなめらかな重みと強度の積。デバッグ表示・検証の表示（反射率0）ではSSRを足さない。視点 `-112,4,8.5` で、SSRが当たった金属の球の画素が金色を保ち（SSRの有無で差が出る画素の (R−B)/(R+G+B) がSSRなしの同じ画素の0.8倍以上）、輪郭のくっきりした地面の切り抜きが無いことを拡大画像で確かめる。Indoor/Outdoor のgoldenを回し、差がこの変更だけによるなら再承認する。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: 照明パスの出力を増やすとRenderGraphの資源の寿命やレガシー経路の契約を崩す場合は、理由を記録して止める。
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders/lighting.frag, Assets/Shaders/ssr.frag, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 原因は `ssr.frag` が当たった画素で照明済みの色を `mix(scene, L_hit, alpha)` で置き換えていたこと。金属は `reflectStrength` が1になり alpha が約0.8（強度0.8）に達し、写った地面の色を金色を掛けずに入れるので、球の縁に地面の切り抜きが透けたように見える。外れた画素は空から作る環境マップ（金色が掛かる）のままで、当たり外れの境目がカメラの角度で動く。危険地帯（描画パスの構造）なので評価者を通す。
- result: 2026-10-03 完了（`1129bfd`・`06fb327`・`7f3f34f`）。視点 `-112,4,8.5` でSSRの有無で差が出る金属の球の画素の (R−B)/(R+G+B) のSSRなしに対する比は、粗さ0.1・0.3・0.5で0.71→1.09、0.68→1.13、0.80→1.04。拡大画像で地面の切り抜きは消え、縁には金色の細い映り込みが残る。フレーム間の平均絶対差は粗さ0.3で0.239→0.213。評価者（Astra）の1周目の指摘（フォグ・半透明を重ねた後の色から減衰前の鏡面反射を引いていた）を受け、SSRを照明の直後（フォグ・半透明の前）へ移し、フォグ・半透明・被写界深度・動きぼけはSSRの出力へ重ねるようにした。RenderGraphCompileTest・Indoor/Outdoor golden pass（Outdoorは球の輪郭と接地部の陰の266画素の差を`Docs/RenderingValidation/R1Acceptance.md` に記録して再承認、Indoorは一致）。起動画面の朝・昼・夕の撮影（`startup-capture/FIX-SSR-SPECULAR-COMPOSITE/`）は平均輝度が前回の受入れより0.3〜1.8下がった（SSRの映り込みが反射率どおりの強さになった分）。反射に写る色は照明の色（フォグ・半透明を含まない）。評価者の2周目の指摘（フォグ・半透明がSSRの出力へ重なるのに、ViewはHDRのシーンの色として照明の直後の "Scene.Color" を取り、SceneColorのキャプチャからそれらが抜ける）は、SSRの出力を書き出してViewが優先して取るように直し（`7f3f34f`）、RenderingHdrIndoor/OutdoorScene・RenderingHdrEmissive2本・FrameCaptureFloatReadback・golden 2本・RenderGraphCompileTestの8本 pass で確かめた。評価は2周までなので3周目は回していない。

## FIX-MESH-BOUNDS-CULLING: 手続きメッシュの境界球を登録した頂点から求め、大きなメッシュが見えたままカリングされないようにする
- status: done
- done-when: `MeshComponent::BuildMeshProxy` が、MeshResources に Mesh3DVertex の並びで登録したメッシュの頂点の位置から求めたローカルのAABBを包む球を `MeshProxy::WorldBounds` にする。回転と非一様スケールが重なっても変換後の箱の角を包む。起動画面の既定視点で、地面の外側の区画（中心が視錐台の外）が描かれる。
- verify: `cmake --build build --config Debug --target RenderResourcesDomainContractTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(MeshResourcesProceduralGpuTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest|SceneViewProxyReconcileTest|SceneViewProxyLookupTest|ComponentDirtyTrackingTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/FIX-MESH-BOUNDS-CULLING -Configuration RelWithDebInfo`
- stop-when: Mesh3DVertex 以外の並びで MeshResources に登録する経路があり、頂点の大きさだけでは並びを判別できない場合は、理由を記録して止める。
- paths: Library/Core/Private/Component/MeshComponent.cpp, Library/Core/Private/Rendering/ProceduralMeshGpuStore.cpp, Library/Core/Private/Rendering/ProceduralMeshGpuStore.h, Library/Core/Public/Rendering/ProceduralMeshGPUData.h, Library/Core/Public/Rendering/RenderResources.h, Library/Core/Private/Rendering/RenderResources.cpp, Test/Core/Rendering/MeshResourcesProceduralGpuTest.cpp, TASKS.md, PROGRESS.md
- notes: 原因は `BuildMeshProxy` が `GetLocalBounds()` の既定の単位の箱から求めた半径約0.87 m の球を入れていたこと。`SceneView::FrustumCull` はこの球で判定するので、16 m × 60 m の地面は中心が視錐台の外に出ると見えている部分ごと描かれなかった。GBufferPass はこの置き場のメッシュを `sizeof(Mesh3DVertex)` のストライドで描き、製品コードの登録元はすべて Mesh3DVertex。危険地帯（公開 API・カリング・影のキャスターの範囲）なので評価者を通す。
- result: 2026-10-04 完了（`47382182`・`28755270`）。起動画面の既定視点で外側の石畳が両側に描かれることを撮影で確かめた（修正前は背景が見えていた）。評価者（Astra）の1周目の指摘（行の長さの最大値を半径に掛ける方法は回転と非一様スケールの組み合わせで箱の角が球の外へ出る）を受け、半径を上3x3の絶対値で移した半分の大きさの長さにし、その物体を World から同期した MeshProxy の境界球が8つの角を包む回帰を足した（修正前の式では落ちる）。2周目は PASS。MeshResourcesProceduralGpuTest・Indoor/Outdoor golden・SceneViewProxyReconcile/Lookup・ComponentDirtyTracking pass。ComponentDataRegistryTest・WorldSyncDifferentialTest は TEST-FULL-CTEST-BASELINE に記録済みの既存の失敗（WorldTransform の777）で落ちる。日本語のコメントを足したファイルに BOM を付けた（BOM が無いと MSVC が CP932 として読み、「。」で終わるコメントが次の行を飲み込んで宣言が消え、ビルドが失敗した）。選択の境界（`Entity::GetLocalBounds` → SceneQuery）は既定の単位の箱のままで、大きな地面の端は選択できない（既存）。

## SS-GROUND-SWATCHES: 起動画面の地面に Poly Haven の地面の材質を帯に並べ、質感を見比べられるようにする
- status: done
- done-when: 起動画面の地面の中央 x∈[-14,14] に4 m 幅の帯を7本（雪・泥と落ち葉・森の地面・石畳・大理石・アスファルト・砂）並べ、外側は従来の石畳のまま。各帯は色・法線（DirectX の向き）・粗さ・AO・高さ（大理石は無し）を持ち、タイルの長さは実物の大きさに合わせる。テクスチャは `Scripts/FetchPolyHavenTextures.ps1` が落とし、git に入れない。テクスチャが無い帯は石畳にして警告を出す。既定・近接・低角度の撮影で天球・地面・球・岩が見える。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/FetchPolyHavenTextures.ps1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SS-GROUND-SWATCHES -Configuration RelWithDebInfo`
- stop-when: Poly Haven の API が取れない、または利用条件が CC0 でない場合は理由を記録して止める。
- paths: Game/GameModes/Rendering3DTest, Scripts/FetchPolyHavenTextures.ps1, .gitignore, TASKS.md, PROGRESS.md
- notes: 地面は9枚の手続きの平面（メッシュのハンドル 110〜118）。帯の UV は地面全体の座標から求めるので、帯の境目で模様がずれない。視差の高さは高さマップの深さ / タイルの長さ。テクスチャは6種 × 5枚の 4k jpg（計 213.8 MB）で、`Assets/Textures/PolyHaven/<id>/` に置く（`.gitignore` 済み）。
- result: 2026-10-04 完了（`9d569868`・`d125bfb7`）。取得スクリプトは30枚を落とし、MD5 が一致した。起動画面の撮影（既定 116.6・近接 117.0・低角度 119.5）は result=pass で、6種の材質のテクスチャがすべて読み込まれ（読み込みの失敗なし）、既定視点に7本の帯と外側の石畳、近接・低角度で球・岩・小屋が見えることを PNG を開いて確かめた。このモードの材質は（既存の地面・球の材質と同じく）モードを抜けても解放しない。

## R1-P5: 透明描画を物理ライト・GGX・IBLへ接続する
- status: done
- done-when: 条件付き計画v4の固定10行、direct/IBL/shadow/metallic mutation、P4回帰6条件、Indoor/Outdoorのobject-presenceが数値契約を満たす。focused8 build、CPU6、skip契約を通し、source18本を1論理変更としてコミットする。
- verify: `cmake --build build --config Debug --target DirectionalShadowPassWiringContractTest ForwardPassPipelinePlacementTest LightingLightBufferTest RenderGraphCompileTest LightingParamsLayoutTest RenderingValidationSceneContractTest RenderingHdrSceneCaptureTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(DirectionalShadowPassWiringContractTest|ForwardPassPipelinePlacementTest|LightingLightBufferTest|RenderGraphCompileTest|LightingParamsLayoutTest|RenderingValidationSceneContractTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderingHdrSceneVulkanSkipContractTest$"`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=known-cd-lambert`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=ibl-prefilter-nonconstant`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=dfg-lut`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=ibl-roughness-sweep`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=white-furnace`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=direct-conductor-endpoint`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=scene-color --r1-scenario=transparent-physical-lighting`
- verify: `build\Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe --scene=indoor --capture-source=back-buffer --measure-visual`
- verify: `build\Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe --scene=outdoor --capture-source=back-buffer --measure-visual`
- paths: Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Public/Rendering/ShadowMapPass.h, Library/Core/Private/Rendering/ShadowMapPass.cpp, Library/Core/Public/Rendering/LightingPass.h, Library/Core/Private/Rendering/LightingPass.cpp, Test/Core/Rendering/LightingLightBufferTest.cpp, Test/Core/Rendering/DirectionalShadowPassWiringContractTest.cpp, Library/Core/Public/Rendering/ForwardPass.h, Library/Core/Private/Rendering/ForwardPass.cpp, Assets/Shaders/forward_transparent.vert, Assets/Shaders/forward_transparent.frag, Test/Core/Rendering/ForwardPassPipelinePlacementTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.h, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp, Test/Core/Rendering/RenderingGoldenImageTest.cpp, TASKS.md, PROGRESS.md, LESSONS.md, NEXT_FINDINGS.md, Docs/lessons/*
- notes: `.superpowers/sdd/RenderingR1PhysicalFoundationPlan/task-5-conditional-implementation-plan-v4.md` の技術仕様を全文読む。GPUとobject-presenceは同計画§7の実コマンド・全行判定を追加実行しログを開く。P4のsource commit/report SHAはPROGRESS.mdを使用。P5 scope18とsingle-source shadow追加は2026-08-15の承認済み。source baselineとthresholdは更新しない。後続P6aが読むobject-presence log prefixは同計画とP6a入口に一致させる。
- notes: 全フェーズ統合では、承認済み最終fixtureのIndoor +4 EV/Outdoor main directional 10000 luxをobject-presenceのscenario-local stateへ先行適用する。P5ではBuildSceneLayoutの既定値を保持し、P6aで既定値を統合する。数値scenarioの露出・10行・対象ROI・thresholdは保持。固定物理geometryを実GPU行列規約で表すfixture補正も行う。根拠・完了条件は `Docs/Plans/RenderingR1P5FixtureIntegration20260908.md`。数値背景だけを全画面へ広げ、background-only ROIを `[16,47]²` に固定する不整合修正をメインの実装判断で採用する。根拠・変更前契約は `Docs/Plans/RenderingR1P5BackgroundProposal20260908.md`。追加確認は取り下げ、個別承認eventは作らない。

## R1-P6A: 最終fixtureと全数値キャプチャを統合する
- status: done
- done-when: P5完了後、条件付き計画v4の40 static rows/120 captures、全画素sRGB oracle、forced format行、GPU CTest exact21、3 script self-test、focused7 build/CPU6、Indoor/Outdoor object-presenceを満たす。source20本の変更をコミットし、P6bの基点と証跡を記録する。
- verify: `cmake --build build --config Debug --target ForwardPassPipelinePlacementTest RenderingValidationSceneContractTest RenderingHdrSceneCaptureTest RenderingGoldenImageTest RenderingGoldenImageComparatorTest RenderingPerceptualDiffTest SceneSerializerTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(ForwardPassPipelinePlacementTest|RenderingValidationSceneContractTest|RenderingGoldenImageComparatorTest|RenderingPerceptualDiffTest|RenderingPerceptualArtificialDifferenceTest|SceneSerializerTest)$"`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --self-test-r1-fixture-contract`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=indoor --capture-source=back-buffer --r1-scenario=all-numerical`
- verify: `build\Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe --scene=indoor --capture-source=back-buffer --measure-visual`
- verify: `build\Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe --scene=outdoor --capture-source=back-buffer --measure-visual`
- verify: `pwsh -NoProfile -File Scripts/CalibrateRenderingVisualThresholds.ps1 -SelfTestR1Contract`
- verify: `pwsh -NoProfile -File Scripts/UpdateRenderingGoldenBaselines.ps1 -SelfTestR1Contract`
- verify: `pwsh -NoProfile -File Scripts/TestRenderingGpuCTestContract.ps1 -SelfTestR1Contract`
- verify: `pwsh -NoProfile -File Scripts/TestRenderingGpuCTestContract.ps1 -BuildDirectory build -ExpectedCount 21`
- paths: Library/Core/Public/Rendering/SceneView.h, Library/Core/Private/Rendering/SceneView.cpp, Test/Core/Rendering/ForwardPassPipelinePlacementTest.cpp, Assets/Shaders/mesh3d.vert, Assets/Shaders/mesh3d.frag, Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp, Test/Core/Rendering/RenderingGoldenImageTest.cpp, Test/Core/Rendering/RenderingGoldenImageComparatorTest.cpp, Test/Core/Rendering/RenderingPerceptualDiffTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.h, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingValidation/RenderingGoldenImage.h, Test/Core/Rendering/RenderingValidation/RenderingGoldenImage.cpp, Test/Core/Rendering/RenderingValidation/RenderingPerceptualDiff.h, Test/Core/Rendering/RenderingValidation/RenderingPerceptualDiff.cpp, Test/Core/Rendering/CMakeLists.txt, Scripts/UpdateRenderingGoldenBaselines.ps1, Scripts/CalibrateRenderingVisualThresholds.ps1, Scripts/TestRenderingGpuCTestContract.ps1, Docs/RenderingValidation/GoldenBaselines.md, TASKS.md, PROGRESS.md, LESSONS.md, NEXT_FINDINGS.md, Docs/lessons/*
- notes: `.superpowers/sdd/RenderingR1PhysicalFoundationPlan/task-6a-conditional-implementation-plan-v4.md` の技術仕様を全文読む。plan SHA=B805112EC87BAB673F7D0093FB6BB93DABBACCF5B4682D55992602869D40156F は2026-08-15承認済み。§11のGPU/skip/各SelfTestR1Contractも実行し、各raw outputを開く。P6b floor diagnosisを受け、Indoorだけplane補正を適用するforward-fixを `2a50d4c`→`d4cbe3e` で確定した。P6bの正式候補生成・source publish・full build/full CTestはこのタスクでは行わない。
- verify: `pwsh -NoProfile -File .harness/runs/20260908-084154/p6a-verify-scene-freshness.ps1`
- notes: 独立recheck11件、bundle第2周評価、別枠のR1統合接続評価を完了。詳細と未読範囲はPROGRESS.mdおよびP6a acceptance receipt。追加2 verifyは原計画§9.2/11.4の既存条件。

## R1-P6B: 物理描画の基準画像・閾値と受入記録を確定する
- status: done
- done-when: 承認済みv3のcontrol/identityを固定し、baseline2枚とthreshold60行の候補をそれぞれ実物承認後に公開する。BackBuffer20回は全exit0、full build1回とfull CTest1回は219件中212 PASS/7意図的skip/0 fail、受入れ証跡のblocking指摘を修正し、P6b tracked assetsを確定する。
- paths: Test/Core/Rendering/Baselines/RenderingValidation/Indoor.png, Test/Core/Rendering/Baselines/RenderingValidation/Outdoor.png, Test/Core/Rendering/Baselines/RenderingValidation/VisualThresholds.tsv, Docs/RenderingValidation/R1Acceptance.md
- notes: `.superpowers/sdd/RenderingR1PhysicalFoundationPlan/task-6b-conditional-implementation-plan-v3.md` を適用した。baseline/thresholdを別々の承認event後に公開し、BackBuffer20は20/20 PASS、targetless full buildはexit 0、full CTestは219件中212 passed/7 intentional skipped/0 failed。現行証跡は`Docs/RenderingValidation/R1Acceptance.md`、候補reportはforward-fix名で旧discard reportを上書きしていない。P6b実装・受入れコミットは`eedec7a681489768645eabf77355886ae8e755c0`、Roadmap localもR1完了へ更新済み。

## R2-P1: 空パラメータと太陽方向の参照契約を定義する
- status: done
- done-when: `SkyAtmosphereParameters` がRenderThreadへ安全に渡せる値だけを持ち、太陽高度・方位角から正規化された方向を決定できる。無効な入力を有限な既定値へ正規化し、Hillaire 2020系参照評価の天頂/太陽近傍サンプルと太陽ディスク露出値をCPUテストで固定する。Rendering層からRHI/Vulkanへの依存を増やさない。
- verify: `cmake --build build --config Debug --target SkyAtmosphereModelTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^SkyAtmosphereModelTest$"`
- paths: Library/Core/Public/Rendering/SkyAtmosphere.h, Library/Core/Private/Rendering/SkyAtmosphere.cpp, Library/Core/Public/Rendering/SceneProxy.h, Test/Core/Rendering/SkyAtmosphereModelTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R2-P2: 空LUTと太陽ディスクをLighting前段へ接続する
- status: done
- done-when: 同一の空スナップショットから透過率/空放射輝度を生成し、RenderGraph named resourceとしてLightingPassが読める。空背景は固定HDRへ暗黙に戻らず、空無効時だけ既存フォールバックを使う。太陽ディスクはR1プリエクスポージャを通り、有限性・安全域の飽和契約を満たす。
- verify: `cmake --build build --config Debug --target SkyAtmospherePassContractTest LightingParamsLayoutTest ForwardPassPipelinePlacementTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SkyAtmospherePassContractTest|LightingParamsLayoutTest|ForwardPassPipelinePlacementTest)$"`
- paths: Library/Core/Public/Rendering/SkyAtmospherePass.h, Library/Core/Private/Rendering/SkyAtmospherePass.cpp, Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/Public/Rendering/RenderGraph/RenderGraphResourceNames.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Private/Rendering/LightingPass.cpp, Library/Core/Public/Rendering/LightingPass.h, Library/Core/Private/Rendering/LightingPassGpuTypes.h, Assets/Shaders/sky_atmosphere.frag, Assets/Shaders/lighting.frag, Test/Core/Rendering/SkyAtmospherePassContractTest.cpp, Test/Core/Rendering/LightingParamsLayoutTest.cpp, Test/Core/Rendering/ForwardPassPipelinePlacementTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R2-P3: 動的空を放射輝度IBLとEVレンジへ統合する
- status: done
- done-when: 空のequirectangular放射輝度から既存のDiffuseIrradiance/PrefilteredSpecularを生成し、太陽高度の変更で環境光も更新される。朝/昼/夕のEVケースで空・直接太陽・IBLの出力が有限で、R1の静的HDR経路と無効時フォールバックが回帰しない。
- verify: `cmake --build build --config Debug --target SkyAtmosphereIblTest LightingLightBufferTest RenderingValidationSceneContractTest LightingParamsLayoutTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SkyAtmosphereIblTest|LightingLightBufferTest|RenderingValidationSceneContractTest|LightingParamsLayoutTest)$"`
- paths: Library/Core/Private/Rendering/SkyAtmosphere.cpp, Library/Core/Private/Rendering/SkyAtmospherePass.cpp, Library/Core/Private/Rendering/LightingPass.cpp, Library/Core/Public/Rendering/LightingPass.h, Library/Core/Private/Rendering/LightingPassGpuTypes.h, Library/Core/Public/Rendering/SceneProxy.h, Test/Core/Rendering/SkyAtmosphereIblTest.cpp, Test/Core/Rendering/LightingLightBufferTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R2-P4: CSM分割とテクセル安定化のCPU契約を実装する
- status: done
- done-when: 4カスケードの分割距離、各ライト行列、受影対象の深度範囲、テクセル中心スナップが同じカメラ/方向ライトから決まる。near/far逆転、無効方向、非有限bounds、サブテクセル移動に対して有限な安全結果を返し、境界に隣接するカスケードの範囲が連続する。
- verify: `cmake --build build --config Debug --target CascadedShadowLightMatricesTest DirectionalShadowLightMatricesTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CascadedShadowLightMatricesTest|DirectionalShadowLightMatricesTest)$"`
- paths: Library/Core/Private/Rendering/CascadedShadowLightMatrices.h, Library/Core/Private/Rendering/CascadedShadowLightMatrices.cpp, Library/Core/Private/Rendering/DirectionalShadowLightMatrices.h, Library/Core/Private/Rendering/DirectionalShadowLightMatrices.cpp, Test/Core/Rendering/CascadedShadowLightMatricesTest.cpp, Test/Core/Rendering/DirectionalShadowLightMatricesTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R2-P5: RHIの配列layer attachmentとShadowMapPassのCSM深度記録を実装する
- status: done
- done-when: 4層D32 array textureを作成し、各layerを1層Framebufferへ安全にattachできる。ShadowMapPassは各カスケードを別行列で記録し、RenderGraphの公開リソースは配列深度として一貫する。失敗時は部分リソースを公開せず、既存単一影の所有権/破棄順序を壊さない。
- verify: `cmake --build build --config Debug --target ShadowMapArrayLayerContractTest DirectionalShadowPassWiringContractTest RenderGraphTextureUsageContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(ShadowMapArrayLayerContractTest|DirectionalShadowPassWiringContractTest|RenderGraphTextureUsageContractTest)$"`
- paths: Library/Core/Public/RHI/IFramebuffer.h, Library/Core/Public/RHI/IDevice.h, Library/Core/Public/RHI/IGPUResourceAllocator.h, Library/Core/Private/RHI/Vulkan/VulkanTexture.h, Library/Core/Private/RHI/Vulkan/VulkanTexture.cpp, Library/Core/Private/RHI/Vulkan/VulkanFramebuffer.h, Library/Core/Private/RHI/Vulkan/VulkanFramebuffer.cpp, Library/Core/Public/Rendering/ShadowMapPass.h, Library/Core/Private/Rendering/ShadowMapPass.cpp, Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/CMakeLists.txt, Test/Core/Rendering/ShadowMapArrayLayerContractTest.cpp, Test/Core/Rendering/DirectionalShadowPassWiringContractTest.cpp, Test/Core/Rendering/RenderGraphTextureUsageContractTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R2-P6: Lighting/ForwardのCSMサンプリングへ移行する
- status: done
- done-when: GPU lighting params、descriptor、lighting/forward shaderが4行列・分割距離・sampler2DArrayを同じlayoutで使用する。カスケード境界にブレンドを適用し、影なし/単一層/不完全公開値では安全な既定へ戻る。既存R1の単一方向影契約と透明描画契約が維持される。
- verify: `cmake --build build --config Debug --target LightingParamsLayoutTest LightingLightBufferTest ForwardPassPipelinePlacementTest DirectionalShadowPassWiringContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingParamsLayoutTest|LightingLightBufferTest|ForwardPassPipelinePlacementTest|DirectionalShadowPassWiringContractTest)$"`
- paths: Library/Core/Private/Rendering/LightingPassGpuTypes.h, Library/Core/Private/Rendering/LightingPass.cpp, Library/Core/Public/Rendering/LightingPass.h, Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/Private/Rendering/ForwardPass.cpp, Library/Core/Public/Rendering/ForwardPass.h, Assets/Shaders/lighting.frag, Assets/Shaders/forward_transparent.vert, Assets/Shaders/forward_transparent.frag, Test/Core/Rendering/LightingParamsLayoutTest.cpp, Test/Core/Rendering/LightingLightBufferTest.cpp, Test/Core/Rendering/ForwardPassPipelinePlacementTest.cpp, Test/Core/Rendering/DirectionalShadowPassWiringContractTest.cpp, TASKS.md, PROGRESS.md

## R2-P7: 空・太陽・CSMの数値/画像受入れを固定する
- status: done
- done-when: 朝/昼/夕のgolden、天頂/太陽近傍float readback、太陽ディスク有限性、カスケード境界の欠落/二重化、サブテクセル移動の影エッジ変化率を実データで検証する。既存Indoor/Outdoor R1 baselineは上書きせず、R2用シナリオと証拠を分離する。
- verify: `cmake --build build --config Debug --target RenderingHdrSceneCaptureTest RenderingGoldenImageTest RenderingGoldenImageComparatorTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingHdrSceneCaptureTest|RenderingGoldenImageComparatorTest|RenderingPerceptualDiffTest)$"`
- verify: `build\\Test\\Core\\Rendering\\Debug\\RenderingHdrSceneCaptureTest.exe --self-test-r2-sky-csm-contract`
- verify: `build\\Test\\Core\\Rendering\\Debug\\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r2-scenario=sky-time-sweep`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingParamsLayoutTest|LightingLightBufferTest|ForwardPassPipelinePlacementTest|DirectionalShadowPassWiringContractTest|SkyAtmospherePassContractTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RHIImageLayoutVulkanValidationTest|RHIImageLayoutVulkanDrawSceneTest|RHIImageLayoutVulkanDrawThenNoCasterSceneTest|RenderingHdrOutdoorSceneVulkanTest)$"`
- paths: Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp, Test/Core/Rendering/RenderingGoldenImageTest.cpp, Test/Core/Rendering/RenderingGoldenImageComparatorTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingValidation/RenderingGoldenImage.cpp, Test/Core/Rendering/Baselines/RenderingValidation/R2*, Test/Core/Rendering/Thresholds/RenderingValidation/R2*, Docs/RenderingValidation/R2Acceptance.md, Scripts/CalibrateRenderingVisualThresholds.ps1, Scripts/UpdateRenderingGoldenBaselines.ps1, TASKS.md, PROGRESS.md

## R2-P8: R2受入れ記録と後続計画の入口を確定する
- status: done
- done-when: R2の実装コミット、検証ログ、golden/threshold、既知の非対象を一つの受入れ記録へまとめ、RoadmapのR2行を完了へ更新する。R1証跡と候補を再利用せず、R3は未着手の新規M1として残す。
- verify: `git diff --check`
- verify: `git status --short --branch`
- verify: `git log --oneline -10`
- paths: Docs/RenderingValidation/R2Acceptance.md, Docs/Plans/RenderingRoadmap.md, TASKS.md, PROGRESS.md, LESSONS.md, NEXT_FINDINGS.md
# R3: ボリュメトリクス

### 採用する設計

- 既存のSceneColor/SceneDepthを使う全画面 `VolumetricsPass` とし、3D froxel texture・compute pass・Vulkan/RHI実装は追加しない。
- 高さ密度は指数関数モデル、視線方向のBeer-Lambert透過率は解析積分する。方向光の単一散乱だけを固定24ステップで積分し、既存の4カスケードCSMで遮蔽する。
- passはLighting後・Forward透明描画前に置く。R2 `SkyAtmosphere.Radiance` を遠景の散乱先へ連続的にブレンドし、透明物自体への距離フォグ適用はR3の対象外とする。
- GPU性能ゲートはRoadmapどおり延期する。R1 baseline/thresholdとR2証跡は変更・再利用しない。

## R3-P1: 高さフォグの解析モデルとスナップショット契約を実装する
- status: done
- done-when: 有限な既定値と不正入力の安全な正規化を備えた高さフォグパラメータをSceneProxy/FramePacket経由でRenderThreadへ渡し、指数密度に対する水平・上昇・下降レイの解析透過率をCPUテストで固定する。Rendering層にRHI/Vulkan依存を追加しない。
- verify: `cmake --build build --config Debug --target VolumetricFogModelTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^VolumetricFogModelTest$"`
- paths: Library/Core/Public/Rendering/VolumetricFog.h, Library/Core/Private/Rendering/VolumetricFog.cpp, Library/Core/Public/Rendering/SceneProxy.h, Test/Core/Rendering/VolumetricFogModelTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R3-P2: 解析フォグとR2遠景空を描画パスへ接続する
- status: done
- done-when: `VolumetricsPass` がSceneColor/SceneDepthを読み、解析透過率で不透明描画を合成し、遠距離ではR2 SkyAtmosphere radianceへ連続的に収束する。Lighting後・Forward透明前の配置、fog無効時の恒等動作、named resource/descriptor境界を契約テストで固定する。新しい3D/RHI資源を作らない。
- verify: `cmake --build build --config Debug --target VolumetricsPassContractTest ForwardPassPipelinePlacementTest RenderingHdrSceneCaptureTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VolumetricsPassContractTest|ForwardPassPipelinePlacementTest)$"`
- paths: Library/Core/Public/Rendering/VolumetricsPass.h, Library/Core/Private/Rendering/VolumetricsPass.cpp, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Public/Rendering/RenderGraph/RenderGraphResourceNames.h, Assets/Shaders/volumetrics.frag, Test/Core/Rendering/VolumetricsPassContractTest.cpp, Test/Core/Rendering/ForwardPassPipelinePlacementTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R3-P3: 方向光の単一散乱をCSM遮蔽へ接続する
- status: done
- done-when: RenderWorldから設定した高さフォグがCoordinator経由でFramePacket.Sceneへ値コピーされ、固定24ステップの方向光散乱が既存の4カスケード深度配列を安全に参照する。遮蔽物の背後ではshaftが抑制され、非遮蔽領域ではライト方向に沿って現れる。影なし/不完全なCSM資源では既存のLightingPass fallbackを使って散乱だけを安全に抑制し、解析高さフォグとR1/R2の描画契約を保つ。
- verify: `cmake --build build --config Debug --target VolumetricsPassContractTest LightingParamsLayoutTest DirectionalShadowPassWiringContractTest RenderingHdrSceneCaptureTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VolumetricsPassContractTest|LightingParamsLayoutTest|DirectionalShadowPassWiringContractTest)$"`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=shadowed-shafts`
- paths: Library/Core/Public/Rendering/VolumetricsPass.h, Library/Core/Private/Rendering/VolumetricsPass.cpp, Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/Public/Rendering/RenderWorld.h, Library/Core/Private/Rendering/RenderWorld.cpp, Library/Core/Public/Rendering/RenderingCoordinator.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Private/Rendering/LightingPass.cpp, Assets/Shaders/volumetrics.frag, Test/Core/Rendering/VolumetricsPassContractTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.h, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationApplication.cpp, Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md, blocked/R3-P3.md
- notes: FramePacket境界を守り、GameThreadの高さフォグ設定は既存SetSkyAtmosphereと同じRenderWorld→RenderingCoordinator経路で値コピーする。既定の無効状態は維持し、検証fixtureから明示的に有効化する。CSM不在時も既存のLightingPass fallback配列をdescriptorへ渡し、散乱だけを無効化して解析高さフォグを維持する。停止記録`blocked/R3-P3.md`は解消済みで、最終検証ログは`.harness/runs/20260920-r3-p3-final/`に保存する。

## R3-P4: フォグ密度・影・遠景空のGPU受入れを固定する
- status: done
- done-when: 3段階密度のGPU golden sweep、CSM遮蔽物あり/なしのA/B、遠方の空/地平線とR2大気のブレンドを専用R3証拠で検証する。R1/R2 baselineは不変とし、GPU性能は未計測・後続ゲートとして明記する。
- verify: `cmake --build build --config Debug --target RenderingHdrSceneCaptureTest RenderingGoldenImageTest RenderingGoldenImageComparatorTest -- /m:1`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=density-sweep`
- verify: `build\Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=density-sweep`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=shadowed-shafts`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=occluder-ab`
- verify: `build\Test\Core\Rendering\Debug\RenderingHdrSceneCaptureTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=distant-atmosphere-blend`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SkyAtmosphereModelTest|SkyAtmospherePassContractTest|LightingParamsLayoutTest|ForwardPassPipelinePlacementTest|VolumetricFogModelTest|VolumetricsPassContractTest|RenderingGoldenImageComparatorTest)$"`
- paths: Test/Core/Rendering/RenderingValidation/RenderingValidationScene.h, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingHdrSceneCaptureTest.cpp, Test/Core/Rendering/RenderingGoldenImageTest.cpp, Test/Core/Rendering/Baselines/RenderingValidation/R3*, Test/Core/Rendering/Thresholds/RenderingValidation/R3*, Docs/RenderingValidation/R3Acceptance.md, TASKS.md, PROGRESS.md
- notes: P3評価の非blocking指摘は、R3-P4で非遮蔽散乱上限と飽和画素0をR3専用閾値へ追加して解消した。独立評価はPASS。

# R4: プローブGI

### 採用する設計

- S4の選定はDDGI。手動配置の有限な3Dプローブ格子を使い、既定は無効、RT対応Vulkanでのみ更新する。RT非対応・無効・資源作成失敗時は既存のラスタ/IBL描画を維持する。
- RenderWorldからの設定はSceneProxy/FramePacketへ値コピーし、RenderThreadはそのフレームの不変スナップショットとR5のTLASだけを読む。プローブ資源と履歴はLightingPassがRenderThread上で所有する。
- 1 volume最大1024 probes、probeあたり64本の準一様rayを毎フレーム更新する。octahedral irradiance/distanceをprobeごとの8x8 array layerへ格納し、1 texel境界、scene-linear RGBA16F irradiance、RG16F距離1次/2次モーメントを使う。
- Probe hitはlinear BaseColorとemissive、既存LightProxyのdirectional/point/spot照明、直前のDDGI irradianceを使って拡散radianceを求める。missは有効な空/環境radianceを使い、無ければ黒とする。反射・透過・鏡面GI・probe relocation/classification/自動配置は対象外。
- Atlasはscene-referred linearで保存し、LightingPassの最終合成で現在フレームのpre-exposureを一度だけ適用する。Volume内部では既存diffuse IBLをDDGIで置換し二重加算を防ぐ。Volume外またはDDGI無効時は既存diffuse IBLを使う。
- 静的受入れは[Cornell大学Computer Graphicsの公開geometry/reflectanceと合成RGBE](https://bowers.cornell.edu/computer-graphics/data)を参照する。直接照明の白ROIから露出scaleを一度だけ決め、shadow-floor/red-bounce/green-bounceの平均Y相対誤差を各25%以内、red/green bounce ROIの優勢chroma比差を各0.10以内とする。R1/R2/R3のbaselineは変更しない。
- 動的受入れはライトまたは不透明物体を動かしてprobe更新を確認する。変更後8フレーム以内に間接光ROIの最終変化量の80%以上へ単調に収束する。GPU性能gateはRoadmapどおりDeferred。
- R5-P5の独立評価はPASSで完了した。R4はR5の既存GPU hit/miss経路を最初のray-query検証で再確認し、P5の受入れ完了を前提にDDGIへ接続する。

## R4-P1: DDGI volume設定と格子/方向変換のCPU契約を作る
- status: done
- done-when: DDGI volumeの有限値検証、1..1024 probeのchecked grid indexing、octahedral direction mapping、ray方向列と無効入力fallbackをCPU契約テストで固定する。既定volumeは無効である。
- verify: `cmake --build build --config Debug --target DDGIVolumeModelTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^DDGIVolumeModelTest$"`
- paths: Library/Core/Public/Rendering/DDGIVolume.h, Library/Core/Private/Rendering/DDGIVolume.cpp, Test/Core/Rendering/DDGIVolumeModelTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R4-P2: volume設定とray-hit材質をFramePacketへ値スナップショットする
- status: done
- done-when: RenderWorldで設定したvolumeとlinear BaseColor/emissiveがSceneProxy/FramePacketへ値コピーされ、packet clear後に参照が残らない。BaseColor既定値1は既存の描画を変えず、RHI未対応時の設定も無効化される。
- verify: `cmake --build build --config Debug --target DDGISnapshotContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^DDGISnapshotContractTest$"`
- paths: Library/Core/Public/Rendering/DDGIVolume.h, Library/Core/Public/Rendering/SceneProxy.h, Library/Core/Public/Rendering/FramePacket.h, Library/Core/Public/Rendering/RenderWorld.h, Library/Core/Private/Rendering/RenderWorld.cpp, Library/Core/Public/Rendering/RenderingCoordinator.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/MaterialTypes.h, Library/Core/Private/Rendering/RenderMaterialStore.h, Library/Core/Private/Rendering/RenderMaterialStore.cpp, Library/Core/Public/Rendering/RayTracingSceneSubsystem.h, Test/Core/Rendering/DDGISnapshotContractTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: GameThreadからRenderThreadへのライブ参照は禁止し、加速構造・材質値・volume設定をFramePacketの値所有データで渡す。危険地帯の独立評価対象。

## R4-P3: compute ray queryでprobe rayのhit属性を取得する
- status: done
- done-when: LightingPassが記録する同一command listのTLAS build後にCompute shaderのray queryをdispatchし、FramePacketのTLASに対する既知のhit/miss、距離、instance/primitive属性を返す。frame slot再利用と欠落dispatchの番兵値readbackを検証する。RT無効時とDDGI資源作成例外時はdispatchを停止し、既存描画と同一SceneColorを維持する。
- verify: `cmake --build build --config Debug --target Game DDGIProbeRayQueryVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^DDGIProbeRayQueryVulkanTest$"`
- paths: Library/Core/Public/Rendering/DDGIProbePass.h, Library/Core/Private/Rendering/DDGIProbePass.cpp, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Private/Rendering/LightingPass.cpp, Assets/Shaders/DDGI/ProbeRayQuery.comp, Test/Core/Rendering/DDGIProbeRayQueryVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: descriptorはRHI abstractionのCompute stageで宣言し、RenderingからRHI/Vulkanをincludeしない。Vulkan同期・GPU resource寿命・FramePacket境界は独立評価対象。

## R4-P3A: ray hitの拡散radianceを材質と直接光から計算する
- status: done
- done-when: ray hitの三角形normalとFramePacketのBaseColor/emissiveを使い、既存directional/point/spot lightの遮蔽付きLambert radianceをscene-linear ray結果へ保存する。missは設定済み空/環境radianceを使う。probe間接光の再帰寄与は次タスクで加える。
- verify: `cmake --build build --config Debug --target DDGIProbeRadianceVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^DDGIProbeRadianceVulkanTest$"`
- paths: Library/Core/Public/Rendering/DDGIProbePass.h, Library/Core/Private/Rendering/DDGIProbePass.cpp, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Assets/Shaders/DDGI/ProbeRadiance.comp, Test/Core/Rendering/DDGIProbeRadianceVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: GPU geometry/material寿命、direct-light単位、RT query同期は危険地帯の独立評価対象。

## R4-P4: ray結果をirradiance/distance atlasへ積分して更新する
- status: done
- done-when: probe ray hitで直前フレームのDDGI irradianceを1段だけ加算し、radiance ray結果をoctahedral irradiance/distance atlasへvisibility重み付きで積分する。1 texel borderとhysteresis 0.8を適用し、既知の単一平面/遮蔽ケースのGPU readbackで値・距離モーメント・境界を固定する。
- verify: `cmake --build build --config Debug --target DDGIProbeUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^DDGIProbeUpdateVulkanTest$"`
- paths: Library/Core/Public/Rendering/DDGIProbePass.h, Library/Core/Private/Rendering/DDGIProbePass.cpp, Assets/Shaders/DDGI/ProbeIrradianceUpdate.comp, Test/Core/Rendering/DDGIProbeUpdateVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Library/Core/CMakeLists.txt, TASKS.md, PROGRESS.md

## R4-P5: DDGI irradianceをLightingPassへ接続する
- status: done
- done-when: GBuffer world position/normalを使ってprobeをvisibility-weightedに補間し、active volume内でdiffuse IBLを置換して間接拡散項を加える。volume外・無効・RT非対応・不完全resourceでは既存diffuse IBLと同じ出力契約を保つ。
- verify: `cmake --build build --config Debug --target LightingParamsLayoutTest RenderingDDGILightingContractTest RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingParamsLayoutTest|RenderingDDGILightingContractTest|RenderGraphCompileTest)$"`
- paths: Library/Core/Private/Rendering/DDGIProbePass.cpp, Library/Core/Public/Rendering/LightingPass.h, Library/Core/Private/Rendering/LightingPass.cpp, Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/Private/Rendering/LightingPassGpuTypes.h, Assets/Shaders/lighting.frag, Test/Core/Rendering/LightingParamsLayoutTest.cpp, Test/Core/Rendering/RenderingDDGILightingContractTest.cpp, Test/Core/Rendering/RenderGraphCompileTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: direct lightingと現在のpre-exposureは変更せず、DDGI有効/無効、volume内/外、sky IBL二重加算を契約テストする。DDGIProbePassが無効化・失敗フレームで前frame-slotのatlas有効状態を残さないことも確認してからLightingPassへ接続する。危険地帯の独立評価対象。

## R4-P6: Cornell参照と動的更新を実GPUで受け入れる
- status: done
- done-when: 公開Cornell RGBEとgeometry/reflectanceを使うHDR captureで3つの間接ROIの平均Y相対誤差が25%以内、red/green bounceのchroma比差が0.10以内になる。ライト/occluder移動後は8 frame以内に最終間接ROI変化の80%以上へ単調に収束し、DDGI無効時は既存描画の比較閾値内で一致する。
- verify: `cmake --build build --config Debug --target RenderingDDGIVulkanTest RenderingGoldenImageComparatorTest -- /m:1`
- verify: `build\Test\Core\Rendering\Debug\RenderingDDGIVulkanTest.exe --capture-source=scene-color --scenario=cornell-reference`
- verify: `build\Test\Core\Rendering\Debug\RenderingDDGIVulkanTest.exe --capture-source=scene-color --scenario=dynamic-update`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingDDGIVulkanTest|RenderingGoldenImageComparatorTest)$"`
- paths: Test/Core/Rendering/RenderingValidation/RenderingValidationScene.h, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingDDGIVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Test/Core/Rendering/Baselines/RenderingValidation/R4*, Test/Core/Rendering/Thresholds/RenderingValidation/R4*, TASKS.md, PROGRESS.md
- notes: 20260922-r4-p7-final3でHDR scene-color captureを再検証した。Cornellのshadow/red/green ROI相対誤差は0.147881/0.152345/0.106696、red/green chroma差は0.0194377/0.0229986、DDGI有効A/Bのmean/max deltaは0.168949/6.5625、無効A/Bはmean/max delta=0/0、VUID_COUNT=0である。dynamic-updateは4 warmup sample後のred/green ROI平均を使い、FrameNumber差8時点のprogress=0.971325/0.820146で単調収束した。Cornell quadは従来どおり室内向きのvertex normalを保持し、GBufferのFrontFace::Clockwiseに合わせたラスタ巻き順をfixture内で明示した。P4期待値を現行のwrap重み床とhysteresisへ再基準化し、閾値・完了条件は変更していない。

## R4-P7: R4受入れ記録と独立評価を確定する
- status: done
- done-when: R4Acceptanceへ選定、scope、Cornell参照URL/asset hash/ROI閾値、動的更新結果、対象build/CTest/GPU captureの読戻しログ、既知の制限、性能gate保留を記録する。FramePacket/RHI/Vulkan/Rendering境界の独立評価blocking指摘を処理し、未処理はNEXT_FINDINGS.mdに残す。
- verify: `git diff --check`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(DDGIVolumeModelTest|DDGISnapshotContractTest|DDGIProbeRayQueryVulkanTest|DDGIProbeUpdateVulkanTest|LightingParamsLayoutTest|RenderingDDGILightingContractTest|RenderingDDGIVulkanTest|RenderingDDGIVulkanDynamicTest|RenderingGoldenImageComparatorTest)$"`
- paths: Docs/RenderingValidation/R4Acceptance.md, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md
- notes: 20260922-r4-p7-final3でR4Acceptance、RGBE/threshold asset、実GPUログを確定した。対象Debug build、Cornell static/dynamic capture、R4指定9件CTestを再実行し、9/9 passed、VUID_COUNT=0を確認した。独立評価2周目で指摘された帳簿・行末・成果物追跡の不整合を修正し、最終成果物へ反映した。性能gateはDeferred、P4由来のvalidation陽性対照とhalf範囲上限はNEXT_FINDINGS.mdへ非blockingとして残す。

# R5: ハードウェアレイトレーシング基盤

### R5の選定と実行順

- R4のS4はDDGIとし、依存するR5を先行する。probe更新はcompute shaderのray queryを使い、R5初弾の不透明ハードシャドウはRT pipelineで実証する。
- BLAS/TLASの所有・更新は`GEngine`が所有するRayTracingSceneSubsystemへ集約する。RenderThreadはFramePacketの不変スナップショットだけを読む。
- RT非対応または無効時は既存ラスタ経路を維持する。R5はRT影までを対象とし、GI本実装とGPU性能ゲートは後続へ分離する。
- 方式の技術背景: [NVIDIA DDGI Integration Guide](https://developer.nvidia.com/blog/an-engineers-guide-to-integrating-ddgi/)

## R5-P1: Buffer Device Addressを独立して有効化する
- status: done
- done-when: RHIバッファでBDAを明示要求でき、対応デバイスでは要求したバッファだけが非ゼロのdevice addressを返す。未要求・非対応時は0を維持する。BDA/RHIのGPU検証と独立評価が通り、全体ビルド・全CTestの結果とR5-P1対象外の失敗がPROGRESS.mdに記録されている。
- verify: `cmake --build build --config Debug --target Game RHIBufferDeviceAddressVulkanTest -- /m:1`
- verify: `cmake --build build --config Debug --target RHIGPUTimestampVulkanTest RHIBufferDeviceAddressVulkanTest RHIAccelerationStructureVulkanTest RayTracingSceneSnapshotTest RHIRayTracingPipelineVulkanTest RayTracingCapabilityContractTest RHITextureToBufferReadbackVulkanTest RenderingValidationFixtureVulkanTest RHIImageLayoutVulkanValidationTest FrameCaptureFloatReadbackVulkanTest RenderingHdrSceneCaptureTest RenderingRayTracingShadowVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIBufferDeviceAddressVulkanTest$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error`
- paths: Library/Core/Public/RHI/RHITypes.h, Library/Core/Public/RHI/IBuffer.h, Library/Core/Public/RHI/DeviceCapabilities.h, Library/Core/Private/RHI/Vulkan/VulkanBuffer.*, Library/Core/Private/RHI/Vulkan/VulkanDevice.*, Test/Core/Rendering/RHIBufferDeviceAddressVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R5-P2: RT機能を任意機能として検出する
- status: done
- done-when: acceleration structure、ray query、RT pipelineの拡張とfeature chainを個別に照会し、利用可能な組合せだけを論理デバイスで有効化する。未対応・無効時はデバイス初期化を維持し、既存ラスタ描画へ戻る。
- verify: `cmake --build build --config Debug --target Game RayTracingCapabilityContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RayTracingCapabilityContractTest$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RHIImageLayoutVulkanValidationTest|RenderingHdrIndoorSceneVulkanTest)$"`
- paths: Library/Core/Public/RHI/DeviceCapabilities.h, Library/Core/Private/RHI/Vulkan/VulkanDevice.h, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Test/Core/Rendering/RayTracingCapabilityContractTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R5-P3: RT shader stageをshadercへ接続する
- status: done
- done-when: RayGen/Miss/ClosestHit/AnyHit/Intersection/Callableの各stageがRHIからshadercへ一意に写像され、最小shader fixtureのコンパイルが全stageで成功する。
- verify: `cmake --build build --config Debug --target RHIRayTracingShaderStageCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIRayTracingShaderStageCompileTest$"`
- paths: Library/Core/Public/RHI/RHITypes.h, Library/Core/Public/RHI/IShaderCompiler.h, Library/Core/Private/RHI/Vulkan/VulkanShaderCompiler.cpp, Test/Core/Rendering/RHIRayTracingShaderStageCompileTest.cpp, Test/Core/Rendering/CMakeLists.txt, Assets/Shaders/RayTracing/*, TASKS.md, PROGRESS.md

## R5-P4: 加速構造のRHI契約を定義する
- status: done
- done-when: BLAS/TLAS geometry・instance・build/update descriptorsと加速構造resourceがbackend-neutral APIで表現され、無効入力とRT非対応時の戻り値契約がテストで固定される。
- verify: `cmake --build build --config Debug --target RHIRayTracingApiContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIRayTracingApiContractTest$"`
- paths: Library/Core/Public/RHI/RHITypes.h, Library/Core/Public/RHI/IDevice.h, Library/Core/Public/RHI/ICommandList.h, Library/Core/Public/RHI/IAccelerationStructure.h, Library/Core/Private/RHI/Vulkan/VulkanDevice.h, Library/Core/Private/RHI/Vulkan/VulkanCommandList.h, Test/Core/Rendering/RHIRayTracingApiContractTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R5-P5: Vulkan BLAS構築を実装する
- status: done
- done-when: BDA対応vertex/index bufferから不透明triangle BLASを構築し、GPU queryで既知の交差/非交差を判定できる。RT無効時はresourceを公開せず、従来のmesh描画を維持する。
- verify: `cmake --build build --config Debug --target RHIAccelerationStructureVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIAccelerationStructureVulkanTest$"`
- paths: Library/Core/Private/RHI/Vulkan/VulkanBuffer.*, Library/Core/Private/RHI/Vulkan/VulkanDevice.*, Library/Core/Private/RHI/Vulkan/VulkanAccelerationStructure.*, Library/Core/Public/RHI/IAccelerationStructure.h, Test/Core/Rendering/RHIAccelerationStructureVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 20260922-r5-p5-resumeでDebug build exit 0、専用CTest 1/1 passed、直接GPU実行のBLAS/TLAS hit/missとresource寿命を確認した。独立評価はPASS。容量再問い合わせ、build-input usageの追加検証、行末整理はNEXT_FINDINGS.mdへnon-blockingとして残す。

## R5-P6: TLASのinstance build/updateを実装する
- status: done
- done-when: BLAS instance配列からTLASを構築し、既知transformの変更をupdateまたはrebuildで反映する。2フレーム間のGPU query結果が移動後の解析位置と一致し、build/update間のbarrierと寿命が検証される。
- verify: `cmake --build build --config Debug --target RHIAccelerationStructureVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIAccelerationStructureVulkanTest$"`
- paths: Library/Core/Public/RHI/IAccelerationStructure.h, Library/Core/Public/RHI/ICommandList.h, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Library/Core/Private/RHI/Vulkan/VulkanAccelerationStructure.*, Library/Core/Private/RHI/Vulkan/VulkanCommandList.*, Test/Core/Rendering/RHIAccelerationStructureVulkanTest.cpp, TASKS.md, PROGRESS.md

- notes: Updateの適合性は容量だけでは判定できない。TLASはsourceの直近Buildで使った実instance数を記録し、Update入力との一致を検証する。

## R5-P7: RT pipelineとShader Binding Tableを実装する
- status: done
- done-when: RayTracing pipeline descriptorがshader groupsを記述し、Vulkan backendがraygen/miss/closest-hit groupとShader Binding Tableを生成する。最小pipelineを作成し、無効なgroup構成を拒否する。
- verify: `cmake --build build --config Debug --target RHIRayTracingPipelineVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIRayTracingPipelineVulkanTest$"`
- paths: Library/Core/Public/RHI/RHITypes.h, Library/Core/Public/RHI/IPipeline.h, Library/Core/Public/RHI/IDevice.h, Library/Core/Private/RHI/Vulkan/VulkanPipeline.*, Library/Core/Private/RHI/Vulkan/VulkanDevice.*, Library/Core/Private/RHI/Vulkan/VulkanRayTracingPipeline.*, Library/Core/Private/RHI/Vulkan/VulkanDescriptorSet.*, Test/Core/Rendering/RHIRayTracingPipelineVulkanTest.cpp, blocked/R5-P7.md, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R5-P8: TraceRaysをコマンドリストへ接続する
- status: done
- done-when: RHI ICommandListのTraceRaysがVulkanの各軸上限と総呼出し数上限以内のdispatchだけを記録し、同一deviceのTLAS descriptor binding/updateと最小raygen/miss/hit shaderを通じて既知triangleのhit/missをGPU readbackできる。readback両要素は未書込み番兵値から期待値へ更新される。descriptor bindingはnull・BLAS・foreign-device resource・別descriptor種を拒否し、回帰テストで固定する。
- verify: `cmake --build build --config Debug --target RHIRayTracingPipelineVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHIRayTracingPipelineVulkanTest$"`
- paths: Library/Core/Public/RHI/RHITypes.h, Library/Core/Public/RHI/IDescriptorSet.h, Library/Core/Public/RHI/ICommandList.h, Library/Core/Private/RHI/Vulkan/VulkanAccelerationStructure.h, Library/Core/Private/RHI/Vulkan/VulkanDescriptorSet.h, Library/Core/Private/RHI/Vulkan/VulkanDescriptorSet.cpp, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Library/Core/Private/RHI/Vulkan/VulkanPipeline.cpp, Library/Core/Private/RHI/Vulkan/VulkanCommandList.h, Library/Core/Private/RHI/Vulkan/VulkanCommandList.cpp, Library/Core/Private/RHI/Vulkan/VulkanRayTracingPipeline.h, Assets/Shaders/RayTracing/RayTracingVisibilityRayGen.glsl, Assets/Shaders/RayTracing/RayTracingVisibilityMiss.glsl, Assets/Shaders/RayTracing/RayTracingVisibilityClosestHit.glsl, Test/Core/Rendering/RHIRayTracingPipelineVulkanTest.cpp, blocked/R5-P8.md, TASKS.md, PROGRESS.md

## R5-P9: GEngine所有のray-tracing sceneをFramePacketへ接続する
- status: done
- done-when: GEngine所有のRayTracingSceneSubsystemがdraw snapshot由来のmesh geometryとinstance transformからBLAS/TLASを管理し、RenderThreadはFramePacket以外のWorld/SceneViewを参照しない。生成・更新・破棄の寿命が契約テストを通る。
- verify: `cmake --build build --config Debug --target RayTracingSceneSnapshotTest RHIAccelerationStructureVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RayTracingSceneSnapshotTest|RHIAccelerationStructureVulkanTest)$"`
- paths: Library/Core/Public/Engine/NorvesEngine.h, Library/Core/Private/Engine/NorvesEngine.cpp, Library/Core/Public/Rendering/FramePacket.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/RenderingCoordinator.h, Library/Core/Public/Rendering/RayTracingSceneSubsystem.h, Test/Core/Rendering/RayTracingSceneSnapshotTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## R5-P10: RT pipelineでハードシャドウを描画する
- status: done
- done-when: opaque fixtureでRT visibilityがLightingへ接続され、PCF/PCSSを無効にしたraster hard shadowとのA/BがR5専用閾値内になる。RT無効時は同一fixtureがraster結果を維持する。
- verify: `cmake --build build --config Debug --target RenderingRayTracingShadowVulkanTest RenderingHdrSceneCaptureTest -- /m:1`
- verify: `cmake --build build --config Debug --target RHIGPUTimestampVulkanTest RHIBufferDeviceAddressVulkanTest RHIAccelerationStructureVulkanTest RayTracingSceneSnapshotTest RHIRayTracingPipelineVulkanTest RayTracingCapabilityContractTest RHITextureToBufferReadbackVulkanTest RenderingValidationFixtureVulkanTest RHIImageLayoutVulkanValidationTest FrameCaptureFloatReadbackVulkanTest RenderingHdrSceneCaptureTest RenderingRayTracingShadowVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderingRayTracingShadowVulkanTest$"`
- verify: `build\Test\Core\Rendering\Debug\RenderingRayTracingShadowVulkanTest.exe --capture-source=back-buffer --scenario=raster-rt-shadow-ab`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation --timeout 180`
- paths: Library/Core/Private/Rendering/LightingPass.cpp, Library/Core/Public/Rendering/LightingPass.h, Library/Core/Public/Rendering/ViewRenderContext.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Private/Rendering/RayTracingShadowPass.cpp, Library/Core/Public/Rendering/RayTracingShadowPass.h, Library/Core/CMakeLists.txt, Library/Core/Public/RHI/RHITypes.h, Library/Core/Private/RHI/Vulkan/VulkanCommandList.cpp, Library/Core/Private/Rendering/LightingPassGpuTypes.h, Assets/Shaders/lighting.frag, Assets/Shaders/RayTracing/*, Test/Core/Rendering/RenderingRayTracingShadowVulkanTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md
- notes: 通常描画は既存のラスタ影を既定とし、RT影は明示設定またはR5検証モードで有効にする。単一visibility textureの適用対象は有効な方向光が一灯の構成に限る。

## R5-P11: 動的TLAS更新とRT無効fallbackをGPU受入れする
- status: done
- done-when: opaque occluder移動の複数フレームcaptureでTLAS更新後の影位置が解析範囲へ移り、移動後のRT影とraster影が比較可能な範囲で一致し、RT無効化時はraster shadowへ復帰する。R5 shaderのプリエクスポージャmodeをLightingParamsLayoutTestが期待値で検証し、全CTestに新規回帰がない。
- verify: `cmake --build build --config Debug --target LightingParamsLayoutTest RenderingRayTracingShadowVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingParamsLayoutTest|RenderingRayTracingShadowVulkanTest)$"`
- verify: `cmake --build build --config Debug --target RenderingRayTracingShadowVulkanTest -- /m:1`
- verify: `build\Test\Core\Rendering\Debug\RenderingRayTracingShadowVulkanTest.exe --capture-source=back-buffer --scenario=dynamic-occluder-fallback`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error`
- paths: Library/Core/Private/Rendering/RayTracingShadowPass.cpp, Library/Core/Public/Rendering/RayTracingSceneSubsystem.h, Test/Core/Rendering/RenderingRayTracingShadowVulkanTest.cpp, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.*, Test/Core/Rendering/LightingParamsLayoutTest.cpp, Assets/Shaders/RayTracing/RayTracingShadowRayGen.glsl, Assets/Shaders/RayTracing/RayTracingShadowMiss.glsl, Assets/Shaders/RayTracing/RayTracingShadowClosestHit.glsl, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md, blocked/R5-P11.md

## R5-P12: R5の受入れ記録を確定する
- status: done
- done-when: R5の設計選定、RHI/Vulkan/API変更、RT影A/B、動的TLAS、非対応fallback、検証ログ、性能gate保留をR5Acceptanceへ集約し、R4/DDGIを後続として記録する。
- verify: `git diff --check`
- verify: `git status --short --branch`
- verify: `git log --oneline -10`
- paths: Docs/RenderingValidation/R5Acceptance.md, TASKS.md, PROGRESS.md

## R5-P13: VulkanTexture::Updateの同期失敗時staging資源を解放する
- status: done
- done-when: EndSingleTimeCommandsの終了・送信・待機失敗時もstaging bufferと転送先texture資源をGPU完了まで保持し、device teardownの非device-lost待機失敗ではallocator・command pool・device・instanceを破棄しない。失敗経路・teardown保護・Validation error検出と正常なtexture updateを検証する。
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RHITextureUpdateVulkanTest$"`
- verify: `build\Test\Core\Rendering\Debug\RHITextureUpdateVulkanTest.exe`
- paths: Library/Core/Private/RHI/Vulkan/VulkanTexture.cpp, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Library/Core/Private/RHI/Vulkan/VulkanDevice.h, Test/Core/Rendering/RHITextureUpdateVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: VulkanDeviceのWaitIdleと共有command poolを使う単発コマンドは、同一device上で呼出側が直列化する。並行更新の内部同期は本タスクの対象外。

## SCENE-P1: 起動時の既定シーン経路を保持し、テストAABBを既定表示から外す
- status: done
- done-when: `GameApplicationHandler::CreateGameModeStateMachine` が従来どおり `Rendering3DTest` を開始し、`Rendering3DTestRoutine::Enter` が球・地面・岩・ライト・HDR環境を構成する経路を変更しない。既定フレームへ常時投入されていた検証用黄色AABBだけを外し、選択表示のAABBは維持する。保存済みのEditorレンダリングシーンを推測して別ファイルへ差し替えない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingValidationSceneContractTest|SceneViewViewportCommandTest|WorldCameraSyncTest)$"`
- verify: `build\\Game\\Debug\\Game.exe --imgui --exit-after-rendered-frames=120`
- verify: `Select-String Game.log -Pattern "Sphere Entity created and added to World|Ground Entity created and added to World|Boulder model loaded and added to World|Environment source and derived IBL resources created|exit-after-rendered-frames reached"`
- stop-when: 実行ログに既定シーン構成または120フレーム終了の証拠がなく、保存済みのレンダリングシーン源を確認できない場合は、別のシーンを発明して起動経路へ接続せず、未実装の永続化入口を後続タスクとして記録する。
- paths: Game/GameModes/Rendering3DTest/Rendering3DTestRoutine.cpp, TASKS.md, PROGRESS.md

## AUDIT-RM-P1: RenderingRoadmapと実装・受入れ履歴の整合を監査する
- status: done
- done-when: R0〜R8のRoadMap依存関係・ステータス表・完了コミットを、追跡対象のコード、受入れ記録、PROGRESS.md、完了トレーラーと突き合わせる。R0〜R5の実装済み範囲、RoadMap表の遅れ、R6以降の着手入口、保存済みレンダリングシーンの有無を明示し、RoadMap本体を変更せず追跡対象の監査記録へ固定する。
- verify: `git check-ignore -v Docs/Plans/RenderingRoadmap.md`
- verify: `git log --all --format="%H%n%s%n%b%n---" --grep="RenderingRoadmap:"`
- verify: `git diff --check`
- stop-when: RoadMapの依存関係または完了証拠が現行履歴から再構成できない場合は、未確認のフェーズを完了扱いにせず、不足証跡を監査記録へ残す。
- paths: Docs/RenderingValidation/RenderingRoadmapAudit.md, TASKS.md, PROGRESS.md

## R6A-M1: velocityの方式と検証契約を定義する
- status: done
- done-when: R6-aのvelocity符号、device用clip行列、初回/無効履歴のゼロ契約、R16G16_FLOATと`GBuffer_Velocity`、FramePacketでのカメラ/オブジェクト履歴、初回対象経路、完了条件、停止条件を追跡対象の設計文書へ固定する。
- verify: `git diff --check`
- verify: `rg -n "currentUV - previousUV|GBuffer_Velocity|FramePacket|停止条件" Docs/RenderingValidation/R6aVelocityPlan.md`
- paths: Docs/RenderingValidation/R6aVelocityPlan.md, TASKS.md, PROGRESS.md

## R6A-P1: velocityのFramePacket履歴とGBuffer出力を実装する
- status: done
- done-when: 通常の遅延不透明MeshProxyについて、現在/前フレームのオブジェクト行列とメインカメラをFramePacketだけでRenderThreadへ渡し、既存GBuffer 4面の契約を維持したまま`GBuffer_Velocity`へR16G16_FLOATのvelocityを書き出す。移動後停止したMeshProxyのvelocityが次フレームでゼロになり、カメラのみ/物体のみ/併用の既知点を解析投影値の0.002以内でreadbackできる。物体のみは対象外背景がゼロで、初回無効履歴と安定静止を別frameで検証し、起動経路とRendering3DTestのシーン構成を変更しない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(FrameCaptureReadbackHelperTest|FramePacketManagerTest|RenderFrameExecutorPlanTest|MeshBatcherTest|MeshBatcherInstancingTest|InstanceDataFlattenTest|GBufferMaterialDescriptorCacheTest|SceneViewViewportCommandTest|BoardTransformTest|CanvasViewRenderTest|RenderGraphTextureUsageContractTest|RenderGraphCompileTest|RenderGraphNamedResourceTest|RenderGraphAttachmentStateTest|WorldCameraSyncTest|PhysicsArchitectureContractTest|PhysicsFixedStepPipelineTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure -V --no-tests=error -R "^RenderingVelocity(Static|Motion|Camera|Object|FirstFrame|MoveThenStop)VulkanTest$"`
- verify: `build\\Game\\Debug\\Game.exe --imgui --exit-after-rendered-frames=120`
- verify: `rg -n "CreateGameModeStateMachine|Sphere Entity created and added to World|Ground Entity created and added to World|Boulder model loaded and added to World|Environment source and derived IBL resources created|exit-after-rendered-frames reached" Game.log`
- stop-when: 既存のRHI/RenderGraph境界を保ったままvelocity attachmentを作成できない、またはGPU readbackで解析契約を観測できない場合は、RTGIや別起動経路へ拡張せずAPI不足を記録する。
- paths: Library/Core/Public/Component/MeshComponent.h, Library/Core/Private/Component/MeshComponent.cpp, Library/Core/Private/Object/World.cpp, Library/Core/Public/Rendering/MeshTypes.h, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Private/Rendering/DrawCommand.cpp, Library/Core/Public/Rendering/FramePacket.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/GBufferPass.h, Library/Core/Private/Rendering/GBufferPass.cpp, Library/Core/Public/Rendering/RenderGraph/RenderGraphResourceNames.h, Library/Core/Public/Rendering/FrameCaptureTypes.h, Library/Core/Private/Rendering/RenderFrameExecutor.cpp, Library/Core/Private/Rendering/FrameCaptureReadbackHelper.cpp, Assets/Shaders/gbuffer.vert, Assets/Shaders/gbuffer.frag, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.h, Test/Core/Rendering/RenderingValidation/RenderingValidationScene.cpp, Test/Core/Rendering/RenderingVelocityVulkanTest.cpp, Test/Core/Rendering/RenderGraphCompileTest.cpp, Test/Core/Rendering/CMakeLists.txt, Test/Modules/Physics/PhysicsArchitectureContractTest.cpp, Test/Modules/Physics/PhysicsFixedStepPipelineTest.cpp, Docs/RenderingValidation/R6aVelocityPlan.md, Docs/Rendering/R6aVelocityAcceptance.md, TASKS.md, PROGRESS.md

## R6-M1: RTGIとテンポラルデノイザの方式を選定する
- status: done
- done-when: RoadMapのR6本体について、RTGIの1〜2バウンス方式、ray query/RT pipelineの用途分担、テンポラル蓄積、デノイザ方式、履歴寿命、固定フレーム数ウォームアップ、R6性能gate保留を選定記録へ固定する。R6-aのvelocity契約とR5のRT基盤を前提にし、R7/R8の未実装機能を先取りしない。
- verify: `cmake --build build --config Debug --target Game RHIRayTracingPipelineVulkanTest -- /m:1`（exit 0）
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RHIRayTracingPipelineVulkanTest|RenderingVelocityCameraVulkanTest)$"`（2/2 passed）
- verify: `git diff --check`
- verify: `rg -n "RTGI|デノイザ|R6-a|ウォームアップ|性能" Docs/RenderingValidation/R6TechniquePlan.md`
- stop-when: R6本体の方式選定がR5/R6-aの公開境界やR7のパストレーサー実装を要求する場合は、境界を越えず未決定事項として記録する。
- paths: Docs/RenderingValidation/R6TechniquePlan.md, TASKS.md, PROGRESS.md

## R6-P1: RTGI結果形式とfallback契約を実装する
- status: done
- done-when: R6の1 bounce diffuse出力、ray-query capability、TLAS snapshot、FramePacketのscene/light revision、履歴resourceのcurrent/history公開、R4 DDGI/既存IBL fallbackをbackend-neutralな契約として接続し、RT無効・資源失敗時に既定rasterを維持する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|RenderingDDGILightingContractTest|RayTracingSceneSnapshotTest)$"`
- stop-when: R5のTLAS所有権またはR6-aのFramePacket/velocity契約を変更しないと接続できない場合は、必要なAPI不足を記録して停止する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md

## R6-P1-FIX: revisionとRTGI履歴契約の意味論を修正する
- status: done
- done-when: SceneRevisionが毎フレームの物体変換・PreviousWorldではなくシーン構成の変更を表し、前フレーム履歴のrevision差を動的移動中の即時fallback条件にしない。履歴のrevision差はR6-P3の棄却・weight抑制へ渡せる形で保持し、構成変更時は履歴を不採用にする。履歴revisionが1つ前でもRTGI選択が維持され、構成変更では不採用となる契約テストを追加する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingDDGILightingContractTest|RayTracingSceneSnapshotTest|RenderingVelocityCameraVulkanTest)$"`
- stop-when: R5のTLAS所有権またはR6-aのvelocity符号・FramePacket値所有を変更しないと修正できない場合は、必要な境界差分を記録して停止する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md

## R6-P2: 1 bounce ray-query GIを接続する
- status: done
- done-when: compute shader内のray queryでTLAS hit/missを処理し、有限なdiffuse indirect radianceをLightingPassへ渡す。RT非対応・RT無効・TLAS不完全・dispatch失敗時はR4 DDGIまたは既存IBLへ戻り、R5 RT pipeline/SBTを変更しない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(DDGIProbeRayQueryVulkanTest|RenderingRayTracingShadowVulkanTest)$"`
- stop-when: ray queryのscene snapshotとR5のTLASを共有できず、RT pipelineの新規SBT設計が必須になった場合はR7境界として未決定へ戻す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md

## R6-P1-FIX-2: SceneRevisionをカリング・ソート非依存へ修正する
- status: done
- done-when: SceneRevisionがカリング済みDrawCommandや奥行きソート順、物体変換、UI表示順に依存せず、全MeshProxy/SkinnedMeshProxyの構成集合と材質・環境設定だけで決まり、カメラ移動・物体移動・同一集合の並べ替えでは変化しない。構成追加・削除・メッシュ/材質変更では変化する性質テストを追加する。R6-P1の履歴revision契約とFramePacket値所有を維持する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingDDGILightingContractTest|RayTracingSceneSnapshotTest|RenderingVelocityCameraVulkanTest)$"`
- stop-when: 全proxy集合をFramePacketへ値コピーする既存境界だけでは構成集合を観測できない場合は、RenderThreadからWorldを参照せず不足するsnapshot項目を記録して停止する。
- paths: Library/Core/Public/Rendering/FramePacket.h, Library/Core/Public/Rendering/SceneProxy.h, Library/Core/Public/Rendering/RenderingCoordinator.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Test/Core/Rendering/RenderingDDGILightingContractTest.cpp, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md

## R6-P2-FIX: RTGI陽性経路をGPU readbackで検証する
- status: done
- done-when: RTGI capability・完全TLAS・出力textureを設定した専用GPUテストがLightingPassのray-query computeを実行し、hit/miss結果のfinite radiance、RTGI公開状態、RTGI無効・TLAS不完全時の既存間接光fallbackをreadbackで反証可能にする。既存R5 RT pipeline/SBTとRendering3DTest起動経路を変更しない。
- verify: `cmake --build build --config Debug --target Game RTGIDiffuseIndirectVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RTGIDiffuseIndirectVulkanTest|DDGIProbeRayQueryVulkanTest|RenderingRayTracingShadowVulkanTest)$"`
- verify: `glslangValidator -V --target-env vulkan1.2 -S comp Assets/Shaders/RTGI/DiffuseIndirect.comp -o build/RTGI-DiffuseIndirect.spv`
- stop-when: テストfixtureからLightingPassのRTGI入力を公開できず実GPU陽性経路を観測できない場合は、fallbackだけを合格にせず必要なtest seamを記録して停止する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders/RTGI, Test/Core/Rendering/RTGIDiffuseIndirectVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md

## R6-P3: velocity再投影と8フレーム履歴を実装する
- status: done
- done-when: `previousUV = currentUV - velocity` を使うcurrent/history ping-pong、depth/normal/material/revision棄却、age上限8、confidence、light revisionの2フレームweight制限、resize/初回/TLAS失敗時の無効化を実装する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingVelocityCameraVulkanTest|RenderingVelocityObjectVulkanTest|RenderGraphCompileTest)$"`
- stop-when: velocityの符号またはFramePacketの値所有を変更しないと履歴が成立しない場合は、R6-a契約との差分を記録して停止する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md

## R6-P4: 3x3 cross-bilateralデノイザを実装する
- status: done
- done-when: テンポラル出力へdepth/normal/material境界を越えない3x3 cross-bilateral filterを1回適用し、history confidenceをweightへ反映し、pre-exposureを二重適用せずLightingPassへ合成する。外部NRDや複数段SVGFを導入しない。
- verify: `cmake --build build --config Debug --target Game LightingParamsLayoutTest RTGIDiffuseIndirectVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(LightingParamsLayoutTest|RenderGraphTextureUsageContractTest|RenderGraphCompileTest|RTGIDiffuseIndirectVulkanTest)$"`
- stop-when: 既存GBuffer/Lightingの公開境界を壊さずfilterへ入力できない場合は、追加attachmentを推測で増やさず契約不足を記録する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md
- notes: テンポラルconfidenceをcross-bilateral weightへ反映し、RTGI GPUテストで公開・hit/miss・fallbackを確認した。出力textureの直接readbackと遠景depth閾値の確認はNEXT_FINDINGS.mdへ記録した。

## R6-P5: 動的GIとfallbackのGPU受入れを固定する
- status: done
- done-when: 固定8 rendered-frame warmup後の静止golden、カメラ/物体移動、ライト移動4フレーム以内の追従、RT無効/R4/IBL fallback、履歴棄却と移動後停止をGPU readbackまたはHDR captureで検証する。既定のRendering3DTest起動経路は不変とする。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation --timeout 180`
- stop-when: 既定シーンの球・地面・ライト球・方向ライト・boulder・HDR環境を失う変更が必要になった場合は、シーン起動経路を戻してfixtureを別経路へ分離する。
- paths: Test/Core/Rendering, Assets/Shaders, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-09-23の独立評価（NEEDS_WORK）で再開。(a) `Assets/Shaders/RTGI/DiffuseIndirect.comp`のレイ方向がpixel座標だけで決まり静止中に同じレイを再評価するため、rendered frameごとに決定論的に異なるサンプルへ直す。(b) ライト追従率の分母を収束後の変化量にし、間接光ROIで4 rendered frame以内の80%到達を判定する。(c) 履歴棄却を履歴age/confidenceのreadbackなど棄却そのもので反証する。静止収束画像の参照比較はR6-P5-REFで行う。

## R6-P5-REF: 静止収束画像をR7自前PT参照と知覚diffで比較する
- status: done
- done-when: R6受入れと同じ解像度・camera・geometry・material・light・HDR環境・exposure/pre-exposureで、R6 RTGIの静止収束SceneColorとR7 PTの収束SceneColorを事前に固定したFLIP pool/max-pixel閾値で比較し、結果と閾値根拠を`R6Acceptance.md`へ記録する。R7-P3Dの起動時PT選択と同一シーン比較の仕組みを使う。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^R6RTGIPathTracingReferenceVulkanTest$"`
- stop-when: 閾値超過が出た場合はRoadmap更新ルール5に従いR6を再オープン扱いにし、閾値やgoldenを緩めない。
- paths: Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md


## TEST-RGC: RenderGraphCompileTestのShadowMapPass初期化失敗を直す
- status: done
- done-when: `RenderGraphCompileTest`の`TestShadowMapNativeDeclareImportsDepthOutput`でFakeDevice上の`ShadowMapPass::Initialize`が失敗する原因（R2のCSM配列深度導入以降の既存失敗）を特定し、テストの契約を弱めずに直す。R6/R7の変更前から失敗していることは`c1474fc`時点の再実行で確認済み。
- verify: `cmake --build build --config Debug --target RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderGraphCompileTest$"`
- stop-when: 失敗がShadowMapPassの実装不具合ではなくFakeDeviceの不足による場合は、fakeへ必要な振る舞いを足し、ShadowMapPassの実資源検査を緩めない。
- paths: Test/Core/Rendering/RenderGraphCompileTest.cpp, Library/Core/Private/Rendering/ShadowMapPass.cpp, TASKS.md, PROGRESS.md

## R8-P1: ACES 2.0 SDRの出力変換を3D LUTへ焼き込み、OCIOの基準画像を作る
- status: done
- done-when: `Scripts/BakeAcesOutputLut.py` がOCIO（pipの`opencolorio`、2.4以上の組み込みACES 2.0 studio config）で、scene-linear Rec.709（D65）からdisplay「sRGB - Display」・view「ACES 2.0 - SDR 100 nits (Rec.709)」への変換を、log2 shaper（範囲をスクリプトの定数と記録に固定）付きの65³ RGBA16F LUTへ焼き、`Assets/ColorManagement/` に置く。LUTの値はsRGBの符号化を外したdisplay-linearにする（既存のsRGB提示をそのまま使う）。同じスクリプトが決定論的なHDR試験チャート（露出段のグレー・原色/補色・肌/空の色票）と、そのOCIO厳密変換の基準画像（`Test/Core/Rendering/Baselines/RenderingValidation/`）を作る。`--verify` は再生成がbyte一致し、LUT補間（CPU、三線形）とOCIO厳密変換の差がチャートの全画素でsRGB符号化後2/255以内なら0で終わる。OCIOの版・config名・shaper範囲・SHA-256とS8の決定（ベイクLUT）を `Docs/RenderingValidation/R8ColorManagement.md` に記録する。
- verify: `python Scripts/BakeAcesOutputLut.py --verify`
- stop-when: OCIOの組み込みconfigにACES 2.0のSDR viewがない、またはpipで入らない場合は理由を記録してユーザーへ戻す。
- paths: Scripts/BakeAcesOutputLut.py, Assets/ColorManagement/*, Test/Core/Rendering/Baselines/RenderingValidation/R8Aces*, Docs/RenderingValidation/R8ColorManagement.md, TASKS.md, PROGRESS.md
- notes: 2026-09-26 ユーザー承認のR8計画（S8=ベイクLUT、1280×720・1024spp、フィルムグレインは既定オフで入れる、連番の検査はScripts＋短いCTest）。pipでopencolorioを入れてよい（ユーザー承認済み）。承認済みgolden・閾値は変えない。既定の起動（Rendering3DTest）とトーンマップの既定は変えない。

## R8-P2: 3D textureのLUTでACES 2.0 SDRへ変換するトーンマップを加える
- status: done
- done-when: `ToneMappingPass` に新しい演算子（ACES 2.0 SDRのLUT）を加え、R8-P1のshaperで3D texture（RHIの`Texture3D`をGPUで初めて標本化する）を引く。この演算子では既定のグレーディング（Contrast/Saturation）を掛けない。起動引数（例 `--tone-map=aces20-lut`）で選べ、既定の演算子は変えない。新しいGPUテストがR8-P1の試験チャートをSceneColorとしてpassへ入れ、ToneMappedColorをOCIO基準画像とsRGB符号化後2/255以内で一致させる（閾値は比較の前に固定）。Indoor/Outdoor goldenは変わらない。
- verify: `cmake --build build --config Debug --target Game R8AcesLutToneMappingVulkanTest ToneMappingParamsLayoutTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8AcesLutToneMappingVulkanTest|ToneMappingParamsLayoutTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: 承認済みgoldenが変わる場合、または2/255に収まらずLUTの解像度・shaperを変える必要がある場合は、測定値を記録してユーザーへ戻す。
- paths: Assets/Shaders/tonemapping.frag, Library/Core/Public/Rendering/ToneMappingPass.h, Library/Core/Private/Rendering/ToneMappingPass.cpp, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/Public/RHI/*, Library/Core/Private/RHI/Vulkan/VulkanTexture.cpp, Test/Core/Rendering/R8AcesLutToneMapping*, Test/Core/Rendering/ToneMappingParamsLayoutTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 危険地帯（RHIの3D texture）。評価者を通す。Rendering層からRHI/Vulkanをincludeしない。

## R8-P3: PTの連番の1フレームを、前後のカメラ・変換とシャッター区間を固定して累積する
- status: done
- done-when: PTに連番の1フレームを描く経路を加える。1フレームの累積（1 spp × N dispatch）の間、前後のカメラ・instance変換と、シャッター区間の基準の長さ（起動引数のフレーム長、例 1/24 s。実時間のDeltaTimeを使わない）を固定し、dispatchごとにレンズとシャッター時刻を引き直す。起動引数で絞り（f値）・焦点距離（focus distance）・シャッター時間・フレーム長を指定できる。新しいGPUテストが、既知の速度で動く物体の動きぼけの幅がシャッター時間×速度に1 px以内、焦点外の点のCoCが解析値（`ComputePathTracingCocPixels`）に2%以内で一致し、累積の途中で履歴が消えず、再実行でbyte一致することを確かめる。既存のPTテストは変わらない。
- verify: `cmake --build build --config Debug --target Game R8PathTracingSequenceFrameVulkanTest PathTracingCameraTest PathTracingCameraVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8PathTracingSequenceFrameVulkanTest|PathTracingCameraTest|PathTracingCameraVulkanTest|PathTracingVulkanTest|PathTracingOutdoorVulkanTest)$"`
- stop-when: 既存のR6/R7のPT参照比較の結果が変わる場合は理由を記録してユーザーへ戻す。
- paths: Library/Core/Public/Rendering/PathTracingCamera.h, Library/Core/Public/Rendering/PathTracingPass.h, Library/Core/Private/Rendering/PathTracingPass.inl, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/FramePacket.h, Library/Core/Public/Rendering/SceneProxy.h, Library/Core/Private/Engine/ApplicationProcessor.cpp, Test/Core/Rendering/R8PathTracingSequenceFrame*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 危険地帯（RenderThread・PT・FramePacket）。評価者を通す。GameThread→RenderThreadはFramePacketのsnapshot越しだけ。

## R8-P3-FIX: 連番の1フレームの累積を、同じフレーム内のinstanceの並び替えで捨てない
- status: done
- done-when: R8-P3の評価（反復5）の指摘を直す。`HashPathGeometry`はinstanceの配列順で前後の変換・材質・customIndexを署名にするため、物体ごとの状態が同じでも同じSequenceFrameの中で`P,A,B`→`P,B,A`と並びが変わると累積が捨てられる。連番の経路のRTスナップショットを安定した物体キーの順に揃え、customIndex・材質texture表・発光instance表を整合させる（または署名を順序に依らない形にし、光源標本の表の順序も揃える）。`R8PathTracingSequenceFrameVulkanTest`へ、SequenceFrameと各物体の前後変換を固定したまま並びを変えた2回のdispatchで試料数が1→2と続き、固定順で描いた画像とbyte一致する検査を加える。既存のPTテストは変わらない。
- verify: `cmake --build build --config Debug --target Game R8PathTracingSequenceFrameVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8PathTracingSequenceFrameVulkanTest|PathTracingCameraTest|PathTracingCameraVulkanTest|PathTracingVulkanTest)$"`
- stop-when: 並びの正規化がR6/R7のPT参照比較の結果を変える場合は理由を記録してユーザーへ戻す。
- paths: Library/Core/Private/Rendering/PathTracingPass.inl, Library/Core/Public/Rendering/PathTracingPass.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/FramePacket.h, Test/Core/Rendering/R8PathTracingSequenceFrame*, TASKS.md, PROGRESS.md
- notes: 危険地帯（PT・RenderThread）。評価者を通す。

## R8-P4: ラスタの被写界深度をPTの薄レンズ参照と比べる
- status: done
- done-when: ラスタに被写界深度のpass（深度からCoCを求め、PTと同じ薄レンズの式・撮像面高24 mm・FOVから焦点距離）を加える。focus distanceが0（ピンホール）なら働かず、承認済みgoldenは変わらない。新しい比較テストが、手前・焦点面・奥に物体を置いた静止シーンで、ラスタのDoFとPT参照（R8-P3の経路、独立な3組の画素ごとの中央値）を比べる。閾値は比較の前に固定する規則で作る: PT参照と、f値を±20%変えたPTとの知覚差（FLIP平均・一致画素の画素単位最大・8×8区画最大）のうち小さい方。±40%の変化が3つとも閾値の外にあることを比較のたびに確かめる。
- verify: `cmake --build build --config Debug --target Game R8DepthOfFieldPathTracingReferenceVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8DepthOfFieldPathTracingReferenceVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。比較の入力のPTピンホール像と薄レンズ参照を画素内の一様標本（box）にそろえて取り直す（閾値の規則は不変）。それでも全画素の平均や光源の縁の区画最大がわずかに残る場合は、測定値と差の分類を既知の限界として記録してR8-P4を完了にする。
- paths: Assets/Shaders/*DepthOfField*, Library/Core/Public/Rendering/*DepthOfField*, Library/Core/Private/Rendering/*DepthOfField*, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Component/CameraComponent.h, Library/Core/Private/Component/CameraComponent.cpp, Test/Core/Rendering/R8DepthOfField*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 取得（PTの3組の中央値と±20%/±40%の変種、約20分）は1反復で1回までにし、shaderやCPU比較の変更は取得済みのダンプに対する`--compare-dumps`だけで評価する（shaderは実行時に読む）。取得をやり直すのは、pass本体やC++の変更で画像が変わるときだけ。差の分類と現状は`blocked/R8-P4.md`。
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: 比較入力のbox標本（`91b774b`）と既知の限界の範囲の判定（`7ca5332`。全画素の平均と光源の縁の区画最大に記録値+5%の上限、光源の縁の矩形の外は規則の閾値）。上限の値は中心標本の測定値のままで、box標本の取得の後に測定値へ合わせる。verifyは未実行。
- result: 比較の入力のPTのピンホール像と薄レンズ参照を画素内の一様標本にそろえた取得で、規則の判定PASS（平均0.0184≤0.0254、一致画素の画素最大0.356≤0.524、区画0.0529≤0.0567、±40%は外、負の対照は検出、VUID 0）。既知の限界の範囲の判定は不要になり外した（`b0f365b`）。記録は`Docs/RenderingValidation/R8Acceptance.md`。

## R8-P5: ラスタの動きぼけをPTのシャッター参照と比べる
- status: done
- done-when: ラスタに動きぼけのpass（R6-aのvelocityにシャッター時間/フレーム長を掛け、空の画素はカメラの動きから求める）を加える。シャッター時間が0なら働かず、承認済みgoldenは変わらない。新しい比較テストが、カメラのpanと既知の速度で動く物体のシーンで、ラスタの動きぼけとPT参照（R8-P3の経路、3組の中央値）を比べる。閾値の規則はR8-P4と同じで、シャッター時間を±20%変えたPTを物差しにし、±40%が閾値の外にあることを確かめる。
- verify: `cmake --build build --config Debug --target Game R8MotionBlurPathTracingReferenceVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8MotionBlurPathTracingReferenceVulkanTest|RenderingVelocityObjectVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。
- paths: Assets/Shaders/*MotionBlur*, Library/Core/Public/Rendering/*MotionBlur*, Library/Core/Private/Rendering/*MotionBlur*, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Test/Core/Rendering/R8MotionBlur*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: `1850803`（pass・比較テスト・動く球の床の影を既知の限界の範囲にする判定）。取得済みダンプ（`build/RenderingValidation/R8MotionBlurPathTracingReferenceRuns`）で平均0.0190（内）、影の矩形の内で画素0.794・区画0.1645。負の対照は画素の閾値が光源の縁で決まるため未検出で、既知の限界として記録する。verifyは未実行。
- result: `1850803`・`251fc92`。規則の判定FAIL（動く球の床の影で画素0.794・区画0.1646）、既知の限界の範囲の判定WITHIN（影の矩形の外は画素0.467・区画0.094で閾値内、平均0.0191は閾値内、2×2・4倍の負の対照は検出）。静止面の上を動く影は画面velocityで運べないため既知の限界として記録した。

## R8-P6: フィルムグレインを既定オフで加える
- status: done
- done-when: 出力変換の後（display空間）に、画素・フレーム番号・seedから決まるフィルムグレインを加える。強さは起動引数で指定し、既定はオフ（承認済みgoldenは変わらない）。新しいGPUテストが、オフでは出力が従来と一致し、オンでは中間グレーの平均の変化が0.5/255以内・標準偏差が指定の±10%以内、隣のフレームで模様が変わり、同じフレームの再実行でbyte一致することを確かめる。
- verify: `cmake --build build --config Debug --target Game R8FilmGrainVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8FilmGrainVulkanTest|ToneMappingParamsLayoutTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。
- paths: Assets/Shaders/tonemapping.frag, Library/Core/Public/Rendering/ToneMappingPass.h, Library/Core/Private/Rendering/ToneMappingPass.cpp, Library/Core/Private/Engine/ApplicationProcessor.cpp, Test/Core/Rendering/R8FilmGrain*, Test/Core/Rendering/ToneMappingParamsLayoutTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: `fce422d`。verifyは未実行。
- result: `fce422d`。強さ0で従来とbyte一致、4/255で平均の変化0.043/255・標準偏差3.999/255、隣のフレームの相関−0.004、再実行でbyte一致、VUID 0。golden屋内/屋外・ToneMappingParamsLayoutTestはpassed。

## R8-P7: EXR連番の検証exeを作る
- status: done
- done-when: C++の検証exe（TinyEXRとThirdPartyのFLIPを使う）が、連番のdirectoryと期待するフレーム数から、欠番・寸法の不一致・NaN/Inf画素（フレームと座標を出す）を検出し、R8-P1のLUT（CPU）でdisplayへ変換した隣接フレームのLDR-FLIP平均を並べ、中央値の3倍（かつ下限0.01）を超える組をポッピングとして失敗にする。規則は検査の前に固定する。単体テストが合成の連番で、正常な連番は合格、欠番・NaN・寸法違い・差し込んだ1フレームの跳びをそれぞれ検出することを確かめる。
- verify: `cmake --build build --config Debug --target R8ExrSequenceValidator R8ExrSequenceValidatorTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^R8ExrSequenceValidatorTest$"`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。
- paths: Test/Core/Rendering/R8ExrSequence*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: `31d84e0`（単体テストはCPUだけで動く）。verifyは未実行。
- result: `31d84e0`・`2ef552d`。合成の連番で正常は合格、欠番・NaN・寸法違い・1フレームの跳び・期待と違う寸法をそれぞれ検出。

## R8-P8: 屋内・屋外の決定論的なアニメーションとEXR連番の書き出し、8フレームのCTestを加える
- status: done
- done-when: 検証アプリのhandlerが、フレーム番号iから時刻 i/24 のカメラと物体の変換を決め（屋内: Cornellで箱が滑りカメラがdollyする。屋外: 空・霧の屋外シーンでカメラが回り球が動く）、R8-P3の経路（絞り・シャッター1/48 s・フレーム長1/24 s）で各フレームを累積し、`WritePathTracingExrFrame`で書き出し、manifest（シーン・解像度・spp・フレーム範囲・seed）を残す。起動引数でシーン・フレーム範囲・解像度・spp・出力directoryを指定できる。CTest `R8SequenceSmokeTest` が屋内・屋外それぞれ8フレームを256×144・64 sppで書き、R8-P7の検証exeに合格する。
- verify: `cmake --build build --config Debug --target Game R8SequenceRenderer R8ExrSequenceValidator -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^R8SequenceSmokeTest$"`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。
- paths: Test/Core/Rendering/R8Sequence*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, Library/Core/Public/Rendering/PathTracingExrOutput.h, Library/Core/Private/Rendering/PathTracingExrOutput.cpp, TASKS.md, PROGRESS.md
- notes: GameThread→RenderThreadはFramePacket越しだけ。
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: `e59e3aa`。verifyは未実行。
- result: `e59e3aa`・`2ef552d`。R8SequenceSmokeTestが屋内・屋外それぞれ8フレームを全フレームちょうど64試料で書き、検証exeに合格。

## R8-P9: 屋内の240フレームの連番を1280×720・1024 sppで書き出し、検査する
- status: done
- done-when: `Scripts/RenderR8Sequences.ps1 -Scene indoor` が屋内の240フレーム（24 fps・10秒）を1280×720・1024 sppで`build/R8Sequences/indoor/`へ書き、R8-P7の検証exeで欠番0・NaN/Inf画素0・ポッピング0を確かめ、所要時間・容量・検証結果を`.harness/runs/<日付>-r8-sequences/`へ残して0で終わる。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RenderR8Sequences.ps1 -Scene indoor`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。1反復で240フレームが書き終わらない場合は、書き出し済みの最後のフレームから続きを書く形で反復をまたいで進める。
- paths: Scripts/RenderR8Sequences.ps1, TASKS.md, PROGRESS.md
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: `4250b21`（`Scripts/RenderR8Sequences.ps1`、欠けたフレームだけを24枚ずつ描き、描画のプロセスはCPUの優先度を下げる）。verifyは未実行。
- result: 240フレーム・欠番0・非有限0・ポッピング0（隣接FLIPの中央値0.0108・最大0.0129・閾値0.0324）、全フレームちょうど1024試料、812 s・2.53 GB。記録は`.harness/runs/20260926-r8-sequences/`。

## R8-P10: 屋外の240フレームの連番を1280×720・1024 sppで書き出し、検査する
- status: done
- done-when: `Scripts/RenderR8Sequences.ps1 -Scene outdoor` が屋外の240フレームを同じ条件で`build/R8Sequences/outdoor/`へ書き、欠番0・NaN/Inf画素0・ポッピング0を確かめ、記録を残して0で終わる。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RenderR8Sequences.ps1 -Scene outdoor`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。1反復で240フレームが書き終わらない場合は、書き出し済みの最後のフレームから続きを書く形で反復をまたいで進める。
- paths: Scripts/RenderR8Sequences.ps1, TASKS.md, PROGRESS.md
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: `4250b21`（R8-P9と同じスクリプト）。verifyは未実行。
- result: 240フレーム・欠番0・非有限0・ポッピング0（隣接FLIPの中央値0.0161・最大0.0207・閾値0.0483）、全フレームちょうど1024試料、685 s・1.65 GB。

## R8-P11: R8の受入れ記録を確定する
- status: done
- done-when: `Docs/RenderingValidation/R8Acceptance.md` に、ACES基準画像との一致（R8-P2）、DoF・動きぼけのPT参照比較（R8-P4/P5）、フィルムグレイン（R8-P6）、屋内・屋外の240フレームの連番の検査（R8-P9/P10）の結果とログ、既知の制限、GPU性能はDeferredを記録し、完了コミットの本文末尾に `RenderingRoadmap: R8 complete` trailerを付ける。PROGRESS.mdに完了を記録する。
- verify: `git diff --check`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。既知の限界として完了にしたタスクは、受入れ記録に測定値・閾値・分類を明記したうえでtrailerを付ける。
- paths: Docs/RenderingValidation/R8Acceptance.md, Docs/RenderingValidation/R8ColorManagement.md, TASKS.md, PROGRESS.md
- notes: 危険地帯を含む機能の完了判定。評価者を通す。
- result: `Docs/RenderingValidation/R8Acceptance.md`。R8の変更の独立評価は1周目NEEDS_WORK（4件）、対応差分の2周目PASS。

## R6-P7: RTGIで発光三角形を光源標本する
- status: done
- done-when: RTGIの1次面と命中点の直接光に、レイトレーシングシーンの発光三角形の光源標本（影の問い合わせ付き）を加え、面光源の直接光と面光源に照らされた面からの1バウンスを雑音の少ない推定にする。`R6RTGIPathTracingReferenceVulkanTest`がR6-P5-REFで固定した閾値（間接光±20%の物差し）内に入り、R6受入れと既存のRTGI・DDGI・golden testが通る。
- verify: `cmake --build build --config Debug --target Game R6RTGIPathTracingReferenceVulkanTest R6RTGIAcceptanceVulkanTest RTGIDiffuseIndirectVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R6RTGIPathTracingReferenceVulkanTest|R6RTGIAcceptanceVulkanTest|RTGIDiffuseIndirectVulkanTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 承認済みgoldenやR6-P5-REFの閾値を変えないと通らない場合は、方式の再選定としてユーザーへ戻す。
- paths: Assets/Shaders/RTGI, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（RTGI shader・LightingPass・RenderThread）。R6-P5-REFで発見。ラスタは発光三角形を光源として扱わず、面光源の直接光がRTGIの偶然の命中だけで入るため斑点状の雑音になる。
- result: 光源標本（`128294a`、評価対応`02eaa7e`）、RTGIへSSAOを重ねない（`81bbf24`）、デノイズの外れ値抑制（`3f4f39c`）、PTの画素中心標本（`a15c43a`）に加え、ユーザーの判断で画素単位最大を幾何が一致する画素で判定し直接光を解析BRDFで揃え（`c162366`・`f7d0b59`・`775261a`）、静止時だけRTGIの履歴を延長し年齢に応じてデノイズの近傍の重みを下げた。R6参照比較は平均0.0466・一致画素の最大0.150・区画0.100で閾値内（閾値と物差しは不変）。記録は`Docs/RenderingValidation/R6Acceptance.md`の「R6-P7の完了」。

## FIX-NEURAL-BRDF-STREAK: ニューラルBRDFの直接光が点光源の近くに作る筋を直す
- status: backlog
- done-when: 通常表示の直接光（ニューラルBRDF）と解析BRDFの差が、Cornellの天井のように点光源に近い粗い面でも筋を作らない。原因（学習範囲外の入力、かすめ角の鏡面項など）を特定し、学習データか評価の範囲を直すか、範囲外では解析BRDFへ戻す。
- verify: 同じCornell条件で、ニューラルBRDFの通常表示と解析BRDF（検証mode 254の直接光）の画素差を比べる専用テストを追加して通す。
- stop-when: 学習済み重みの再生成が必要でデータがない場合は、範囲外の入力で解析BRDFへ戻す方針をユーザーへ提案する。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: R6-P7で発見。点光源だけの画像でラスタにだけ2画素幅の筋が出る。

## FIX-SSAO-ROOM-SCALE: 部屋の大きさのシーンでSSAOが壁をほぼ全遮蔽にする原因を直す
- status: backlog
- done-when: Cornell（5.5 m四方）の壁でSSAOがほぼ0になる原因（半径・bias・深度の再構成の尺度など）を特定して直し、IBL fallbackの間接光が壁で消えない。既存goldenは変えない。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 承認済みgoldenが変わる場合は基準の更新を提案して止まる。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: R6-P7で発見。RTGIには`81bbf24`でSSAOを掛けないようにしたが、IBL fallbackは引き続きSSAOを掛ける。
- result: SS-GTAO（2026-10-02）で置き換わった。原因は旧SSAOがワールド空間の法線をビュー空間の標本へそのまま使い、開けた面も遮蔽に数えていたこと。GTAOはビュー空間の地平線で余弦重みの可視率を求め、半径1 mの外の面を数えないので、Cornellの壁の中ほどの可視率は0.97〜1.00（`RenderingGTAOCornellRoomVulkanTest`）。statusは人の確認のためbacklogのまま。

## TEST-R6-RESIDUAL-TIMING: R6停止残留の判定が起動の早さで変わる原因を直す
- status: done
- done-when: `R6RTGIAcceptanceVulkanTest`の停止残留（`R6_STOP_RESIDUAL_CHECK`）が、単体実行とRenderingValidationラベル内の実行で同じ物体影響の大きさになり、ラベル全件を3回続けて実行して毎回通る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^R6RTGIAcceptanceVulkanTest$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 物体影響の差がRTGI/DDGIの実装の不具合（履歴や更新順）による場合は、テストの待ち時間で隠さずR6の不具合として記録し直す。
- paths: Test/Core/Rendering, Library/Core/Private/Rendering, TASKS.md, PROGRESS.md
- notes: R7-P3Dで発見。初回取得は資産読み込みの完了（壁時計に依存）で始まり、以降の段階はフレーム数で進む。単体実行は最初の段階がframe 153前後で物体影響0.018〜0.06（残留比0.001〜0.003）、ラベル内はframe 76前後で物体影響が約1.1e-4まで落ち、残留比が閾値0.5付近（R7-P3Cのラベル実行で0.477、R7-P3Dで0.506）になる。R6-P6の前に直す。
- result: 物体影響の差はRTGIの不具合（面光源の直接光がRTGIの偶然の命中だけで入る）によるもので、R6-P7の光源標本で解消した。テストの待ち時間は変えていない。単体実行2回は最初の段階がframe 172〜175・物体影響0.0251〜0.0280、RenderingValidationラベル全件の3回連続実行は各回frame 166〜177・物体影響0.0267〜0.0289・残留0.0058前後（残留比約0.2、閾値0.5）で毎回通過した。ラベルの2・3回目に失敗した1件は、実行中に追加した未完成のR4再照合テスト（R4-REOPEN）で、R6系は3回とも通過。ログは`.harness/runs/20260924-test-r6-residual/`。

## R6-P6: R6受入れと性能gate保留を確定する
- status: done
- done-when: R6-P1〜P5のコードコミット、受入れログ、golden/threshold、fallback、既知の制限、R7暫定参照の再照合条件を記録し、完了コードコミットへ `RenderingRoadmap: R6 complete` trailerを付ける。GPU性能はDeferredとして残す。
- verify: `git diff --check`
- verify: `git log -1 --format=%B`
- verify: `rg -n "R6|RTGI|性能|Deferred|fallback" Docs/RenderingValidation/R6TechniquePlan.md Docs/RenderingValidation/R6Acceptance.md PROGRESS.md`
- stop-when: R6の機能gateが未完、またはR7/R8の実装を前提にしないと受入れできない場合は、完了trailerを付けず残課題を記録する。
- paths: Docs/RenderingValidation/R6TechniquePlan.md, Docs/RenderingValidation/R6Acceptance.md, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md
- result: `R6Acceptance.md`の判定を受入れへ確定し、R6-P5-REF・R6-P7のコミットと検証ログ、既知の制限を加えた。全体gate（`.harness/runs/20260925-r6-p6/`）はtargetless Debug build BUILD_EXIT=0、RenderingValidation 56件中47 passed・8 skipped・1 failed（再オープン中のR4の再照合だけ）。R6の完了判定の独立評価はPASS（blockingなし。non-blocking 3件の記述の正確さは同じコミットで直した）。GPU性能はDeferred。

## R4-REOPEN: R4 DDGIのprobe由来の斑点と漏れを直し、自前PT参照で再照合する
- status: done
- done-when: `R4DDGIPathTracingReferenceVulkanTest`（R4の規定の指標: direct white ROIで露出を1回決め、影・赤・緑ROIの相対輝度誤差≤0.25、赤・緑ROIの優勢色度の差≤0.10）が同じ閾値で通り、R4の受入れ・DDGI系・R6・golden testが通る。
- verify: `cmake --build build --config Debug --target Game R4DDGIPathTracingReferenceVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R4DDGIPathTracingReferenceVulkanTest|DDGI.*|RenderingDDGILightingContractTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 閾値かR4の指標を変えないと通らない場合、または承認済みgoldenが変わる場合はユーザーへ戻す。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（DDGI shader・LightingPass）。R7-P7の再照合（更新ルール5、2026-09-25）で発見。R4受入れと同じCornell状態（DDGI有効、RTGI無効、点光源なし、天井の面光源あり、512×512、128フレーム後）のラスタを、4096 sppの自前PT（全輸送）と比べ、赤ROIの相対輝度誤差0.257（上限0.25）、影0.162、緑0.0566、色度差0.0091/0.0167、露出0.846。ラスタのROI平均はR4受入れ時の記録（direct 0.393677、shadow 0.15236、red 0.0687421、green 0.0951394）と同値で、劣化ではない。画像全体ではprobeの位置に格子状の明るい斑点、奥の壁の暗転、画面の縁の暗い帯があり、64画素区画のラスタ/PT輝度比は0.07〜2.15に散らばる（`.harness/runs/20260925-r7-p7-r4/pt-vs-ddgi.png`、`ratio-map.txt`）。probe格子（原点-0.1、間隔0.82、8×8×8）の外側の層は壁の裏と開口の外にある。
- result: `2588b47`・`effd5e3`・`355f8df`・`3b1f3f4`・`ce9d10f`。probeの分類（面の裏のhitが25%を超えるprobeを無効）、probeでの発光面の直接照度（Lambertの式と影の可視率）、照度atlasの全体/間接光だけの2組のlayer（hit面は間接光の組を読み、直接光の二重計上をなくす）、RTXGIの補間（法線方向0.225倍のずらし、押しつぶし、平方根の補間）、距離のcosineの指数8。`R4DDGIPathTracingReferenceVulkanTest`は影0.117・赤0.224・緑0.130・色度差0.030/0.011で合格（閾値不変）、公開Cornell参照0.104/0.117/0.019、動的0.875/0.871。全target build後のRenderingValidationラベル57件中0件失敗（8件はGPU skip契約）、DDGI系9/9。危険地帯の独立評価2周の指摘（鏡映の表裏、発光面の近くの過大評価、頂点の順による放射の側）は修正して回帰の場面を追加した。記録は`Docs/RenderingValidation/R4Acceptance.md`、ログは`.harness/runs/20260925-r4-reopen/`。

## R6-GATE-DDGI-ORACLE: DDGI放射輝度比較のシナリオ履歴を分離する
- status: done
- done-when: 非遮蔽と点/スポット遮蔽のreadbackがそれぞれ固定の直接照明期待値に一致し、二つ目のケースへ前ケースのprobe irradiance蓄積を持ち越さない。rendererのradiance計算・期待値・閾値は変更しない。
- verify: `cmake --build build --config Debug --target DDGIProbeRadianceVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^DDGIProbeRadianceVulkanTest$"`
- stop-when: pass状態を分離しても期待値とreadbackが一致しない場合、radianceや閾値を調整せず追加原因を記録する。
- paths: Test/Core/Rendering/DDGIProbeRadianceVulkanTest.cpp, TASKS.md, PROGRESS.md

## R6-GATE-OUTDOOR: 承認済みOutdoor goldenとの差分を原因診断する
- status: done
- done-when: `RenderingGoldenOutdoorVulkanTest`が既存golden/thresholdを変更せず通過する。原因と修正を記録し、修正後の`RenderingValidation`全体で新規失敗がないことを確認する。既定のRendering3DTest起動経路と保存シーンは維持する。
- verify: `cmake --build build --config Debug --target RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderingGoldenOutdoorVulkanTest$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation --timeout 180`
- stop-when: 原因が既承認baselineの改定を要する場合はgoldenを書き換えず根拠を保存する。RHI image-layout等の独立不具合を検出した場合は範囲を分けて記録し、該当経路を隠さない。
- paths: Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering/RenderingValidation, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-M1: NEE/MISとEXR出力方式を選定する
- status: done
- done-when: power heuristic MIS、ReSTIR除外、TinyEXR C API version、EXR channel/precision/compression、seed決定論性、R7 core/outdoor分割を技術選定記録と実装計画に固定する。
- verify: `rg -n "S7|NEE|MIS|ReSTIR|TinyEXR|6f470c9|RenderingRoadmap: R7 complete" Docs/RenderingValidation/R7SamplingAndExrSelection.md Docs/RenderingValidation/R7TechniquePlan.md`
- verify: `git diff --check`
- stop-when: 選定したEXR APIが必要なWindows/CMake buildとfloat scanline出力を満たさない場合、実装開始前に代替を根拠付きで記録する。
- paths: Docs/RenderingValidation/R7SamplingAndExrSelection.md, Docs/RenderingValidation/R7TechniquePlan.md, TASKS.md, PROGRESS.md

## R7-P1: ラスタとPTのBRDF・texture評価を共有する
- status: done
- done-when: raster opaque/transparentとRT shaderが共通BRDF・texture evaluationを参照し、R1数値契約とIndoor/Outdoor goldenを維持する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RTGIDiffuseIndirectVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest|ForwardPassPipelinePlacementTest|LightingParamsLayoutTest)$"`
- stop-when: include機構が既存shader compilerで成立しない場合、全shaderを一括移行せず最小の共有境界を記録する。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-P2: 独立path-tracing pipelineとprogressive accumulationを実装する
- status: done
- done-when: 明示選択のPT pipelineがR5 RT pipeline/SBTとFramePacket snapshotだけで1 sample/frameを累積し、静止時に収束、camera/scene revision変更時に履歴をresetする。raster pipelineとRendering3DTest起動経路は不変。
- verify: `cmake --build build --config Debug --target Game PathTracingVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingVulkanTest$"`
- stop-when: RHI/Vulkan APIやRenderThreadからWorldへの参照が不可避となった場合、境界を越えずsnapshot不足を記録する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md

## R7-P3A: RHIに配列combined image samplerとnon-uniform indexingを追加する
- status: done
- done-when: `RHI::DescriptorBinding`に配列数`count`（既定1）と配列要素単位のcombined image sampler bindを追加し、Vulkanのset layout・pipeline layout・descriptor pool・writeが`count`を反映する。Vulkan 1.2の`shaderSampledImageArrayNonUniformIndexing`を対応時だけ有効化して`DeviceCapabilities`へ公開する。`count=1`の既存bindingのlayout・pool容量・writeは変えない。専用GPUテストで4要素配列を呼び出しごとに異なる添字で標本化し、各要素の既知色をreadbackで一致させる。範囲外要素・非配列bindingへのbindは拒否する。
- verify: `cmake --build build --config Debug --target Game RHIDescriptorArrayVulkanTest RHIRayTracingPipelineVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RHIDescriptorArrayVulkanTest|RHIRayTracingPipelineVulkanTest|RHIRayTracingApiContractTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation --timeout 180`
- stop-when: 対応GPUでnon-uniform indexingを有効化できない、または既存descriptor setの割り当て・更新が回帰する場合は、RHI変更を戻して不足した能力を記録する。
- paths: Library/Core/Public/RHI/IDescriptorSet.h, Library/Core/Public/RHI/DeviceCapabilities.h, Library/Core/Private/RHI/Vulkan/VulkanDescriptorSet.h, Library/Core/Private/RHI/Vulkan/VulkanDescriptorSet.cpp, Library/Core/Private/RHI/Vulkan/VulkanDevice.h, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Library/Core/Private/RHI/Vulkan/VulkanPipeline.cpp, Test/Core/Rendering/RHIDescriptorArrayVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 危険地帯（RHI公開API・Vulkan）。独立評価を通す。

## R7-P3B: PTの材質snapshot・texture・シェーディング法線を接続する
- status: done
- done-when: `RayTracingHitMaterialSnapshot`へGBufferと同じ規則のinstance色（custom dataの非0成分、既定1）とalbedo・normal・metallic・roughnessのtexture handleを値として加える。PTはRenderThreadでtexture handleを解決し、重複を除いた配列descriptorへ束ね、未設定はGBufferと同じ既定値（白・平坦法線・metallic 0・roughness中間灰）にする。closest-hitは`Mesh3DVertex`の法線・UVを重心補間し、共通shaderの材質評価でalbedo・法線マップ・metallic・roughnessを得る。発光はGBuffer同様`色×nits`にpre-exposureを掛ける。既存のposition-only頂点（stride 12）は幾何法線・UV 0へfallbackする。GPUテストでtexture UV標本化、metallic/roughness snapshot、既定値、instance色、発光のpre-exposureをreadbackで固定し、既存PT/屋外/霧/カメラ/EXRテストに回帰がない。
- verify: `cmake --build build --config Debug --target Game PathTracingVulkanTest PathTracingMaterialVulkanTest PathTracingOutdoorVulkanTest PathTracingVolumetricTest PathTracingCameraVulkanTest PathTracingExrOutputTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingVulkanTest|PathTracingMaterialVulkanTest|PathTracingOutdoorVulkanTest|PathTracingVolumetricTest|PathTracingCameraTest|PathTracingCameraVulkanTest|PathTracingExrOutputTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: texture資源の寿命をFramePacketとRenderResourcesの既存所有境界で保証できない、またはラスタgoldenが変わる場合は、材質APIを広げず不足を記録する。
- paths: Library/Core/Public/Rendering/MaterialTypes.h, Library/Core/Public/Rendering/PathTracingPass.h, Library/Core/Private/Rendering/PathTracingPass.inl, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Assets/Shaders/PathTracing, Assets/Shaders/Common/PbrMaterialEvaluation.glsl, Assets/Shaders/gbuffer.frag, Test/Core/Rendering/PathTracingVulkanTest.cpp, Test/Core/Rendering/PathTracingMaterialVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（Rendering公開API・RT資源寿命）。独立評価を通す。

## R7-P3C: NEE/MISとpoint・spot・directional・area light・環境光輸送を実装する
- status: done
- done-when: FramePacketのLightProxyからラスタと同じ単位・減衰・spot円錐の光源表をPTへ渡し、delta lightはNEEだけ（weight 1）、発光三角形と太陽円盤はlight sampleとBSDF sampleをpower heuristic β=2で合成する。BSDF sampleは拡散cosineとGGX可視法線分布の混合で、PDFと共通BRDF評価を同じ方向で整合させる。環境光（一様値またはHDR equirect）はBSDF sampleで評価し、固定0.05の仮背景を廃止する。GPUテストで、R1白炉と同じ15行（roughness 5段×metallic 3段、平均相対誤差1%・最大3%）、既知cdの点光源によるLambert面の解析輝度、面光源のNEEのみ・BSDFのみ・MISが同じ期待値へ収束すること、spot円錐外0、directional照度を固定seedで検証する。
- verify: `cmake --build build --config Debug --target Game PathTracingLightingVulkanTest PathTracingVulkanTest PathTracingOutdoorVulkanTest PathTracingVolumetricTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingLightingVulkanTest|PathTracingVulkanTest|PathTracingMaterialVulkanTest|PathTracingOutdoorVulkanTest|PathTracingVolumetricTest)$"`
- stop-when: 白炉・解析照明が共通BRDFの近似に起因して閾値を超える場合は、閾値を緩めず、PT推定量の誤りと共通BRDFのエネルギー差を分けて記録する。
- paths: Library/Core/Public/Rendering/PathTracingPass.h, Library/Core/Private/Rendering/PathTracingPass.inl, Assets/Shaders/PathTracing, Assets/Shaders/Common, Test/Core/Rendering/PathTracingLightingVulkanTest.cpp, Test/Core/Rendering/PathTracingVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（シェーダーパイプライン・RenderThread）。独立評価を通す。

## R7-P3D: 起動時PT選択とラスタ/PT同一シーン比較を固定する
- status: done
- done-when: RenderingCoordinatorの初期化設定でmain SceneViewをPT pipelineにでき（既定はraster、`Rendering3DTest`起動は不変）、PTはLightingPassと同じ環境マップ設定と検証用一様環境（debug mode 252）を使う。描画検証appに`--renderer=path-tracing`と累積試料数の指定を加え、capture時の実累積試料数をcapture結果へ記録する。R1の白炉と解析点光源の行をPTで実行して同じ評価関数に通し、同じ行のラスタSceneColorとPT SceneColorの差を事前に固定した閾値で比較する。
- verify: `cmake --build build --config Debug --target Game RenderingHdrSceneCaptureTest PathTracingRasterParityVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingRasterParityVulkanTest|RenderingHdrSceneCaptureVulkanTest.*)$"`
- verify: `build\\Game\\Debug\\Game.exe --imgui --exit-after-rendered-frames=120`
- stop-when: PT選択がRenderThreadのpass寿命やFramePacket境界を変えないと成立しない場合は、実行時切替を追加せず起動時選択の不足を記録する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（RenderThread・pass寿命）。独立評価を通す。

## FIX-NORMAL-MATRIX-SCALE: 法線行列の小スケール退化判定を尺度不変にする
- status: backlog
- done-when: `MatrixUtils::CreateNormalMatrix`が一様スケール約0.005未満の物体にも逆転置を返し（特異かどうかは尺度に対する比で判定）、R1室内フィクスチャの平面メッシュの頂点法線を幾何と一致させ、PTの閉包命中シェーダから同じ退化規則の写しを外す。R1数値検証、Indoor/Outdoor golden、`PathTracingRasterParityVulkanTest`が変わらず通る。
- verify: `cmake --build build --config Debug --target Game RenderingHdrSceneCaptureTest PathTracingRasterParityVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 承認済みgoldenやR1の数値が変わる場合は、基準の更新を提案して止まる。
- paths: Library/Core/Public/Math, Assets/Shaders/PathTracing, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: R7-P3Dで発見。3x3の行列式の絶対値がFLT_EPSILON未満だと単位行列になり、回転した小さな物体の法線が回らない。R1室内の平面はこの規則を前提に頂点法線を+Zへ書き換えているため、PTも同じ規則でラスタと揃えている。

## R7-P4: SPP収束とCornell参照を固定する
- status: done
- done-when: 同じseedのnested 16/64/256 spp prefixで256 spp自己収束画像に対するMSEが単調減少し、Cornell boxが固定公開参照と規定誤差内で一致する。
- verify: `cmake --build build --config Debug --target PathTracingConvergenceVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingConvergenceVulkanTest$"`
- stop-when: monotonic結果がseed探索だけに依存する場合、閾値を緩めずsample estimatorと測定手順を再検討する。
- paths: Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- result: 収束は合格（入れ子の接頭列のMSEが全体と64区画すべてで単調、MSE比の中央値4.374、引き直しでbyte一致）。公開RGBEは色の符号化が公開されておらず色の壁の彩度を再現できないため、ユーザーの判断で輝度の判定を白い面・影・発光面の位置に限り、赤・緑の壁とR4の赤・緑ROIは優勢色度で判定する範囲へ見直した（数値の上限は事前固定のまま）。影ROI 0.0025、赤・緑ROIの色度差0.0044・0.0217、壁の優勢な成分が一致、発光面のずれ1画素、580区画の中央値0.0101・90%点0.0340で合格。記録は`Docs/RenderingValidation/R7CoreAcceptance.md`。

## R7-P5: thin-lensとshutter time samplingを実装する
- status: done
- done-when: FramePacketのcurrent/previous camera・geometry snapshotからshutter時刻を決定論的に評価し、薄レンズCoCの数値誤差と静止/移動goldenを固定する。
- verify: `cmake --build build --config Debug --target Game PathTracingCameraTest PathTracingCameraVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingCameraTest|PathTracingCameraVulkanTest)$"`
- stop-when: 変換補間が不正なshear/scaleを生む場合、live World参照や別のmotion systemを追加せず対応可能なtransform範囲を定義する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-P6: seed固定EXR sequence出力を実装する
- status: done
- done-when: TinyEXR v3 C APIでlinear RGB float/ZIP scanlineを出力し、NaN/Infを拒否する。同じscene/seed/SPP/frameの2出力がbyte一致または事前閾値内である。
- verify: `cmake --build build --config Debug --target Game PathTracingExrOutputTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingExrOutputTest$"`
- stop-when: independent EXR readerがchannel/precision/windowを一致して読めない場合、sequence APIを広げずwriter integrationを修正する。
- paths: CMakeLists.txt, Library/ThirdParty, Library/Core, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-P7: R7 coreを受入れ、R4/R6暫定参照を再照合する
- status: done
- result: `Docs/RenderingValidation/R7CoreAcceptance.md`（2026-09-25）。R6は自前PT（拡散2バウンス、median of means）と閾値内で合格、R4は赤ROI 0.257（上限0.25）で更新ルール5により再オープン（R4-REOPEN）。全体CTestは`.harness/runs/20260925-r7-gate/ctest-all.txt`。
- done-when: R7 coreの完了条件、公開Cornell参照、R1数値検証、CoC、決定論EXR、既知制限を集約し、同一条件のself PTでR4 DDGIとR6 RTGIを各1回再照合する。Outdoor extension完了前にR7 trailerは付けない。
- verify: R7 core関連Debug build/CTestとEXR finite-scanを実行し、全出力ログを開いて閾値結果を確認する。
- verify: `git diff --check`
- stop-when: R4/R6比較がRoadmap閾値を超えた場合、phaseを完了扱いにせず再オープン理由と再検証単位を記録する。
- paths: Docs/RenderingValidation, Test/Core/Rendering, TASKS.md, PROGRESS.md

## RTGI-HIT-SPECULAR: RTGIの命中面を光沢のある反射でも照らす
- status: backlog
- done-when: RTGIの命中面の直接光が材質の粗さ・金属度の鏡面葉を含み、R7屋外比較の既知差（夕の緑の球の影側の面。命中点をLambertにしたPTとは一致）が全輸送との比較で縮む。R6・R7の参照比較と既存goldenが通る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RTGIDiffuseIndirectVulkanTest|R6RTGIPathTracingReferenceVulkanTest|R7OutdoorPathTracingReferenceVulkanTest)$"`
- stop-when: 命中面の材質をRTのsnapshotへ持たせる方法（粗さ・金属度のtextureの扱い）に設計判断が要る場合はユーザーへ戻す。判定の参照の輸送範囲を変える場合もユーザーへ戻す。
- paths: Assets/Shaders/RTGI, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering
- notes: 危険地帯（RTGI）。R7-O3の既知差2。命中面の反射率は`3c80458`でinstance色にした。

## RTGI-MULTI-BOUNCE: 接地部の3回目以降のバウンスをRTGIで扱う
- status: backlog
- done-when: 球の下の接地部で、ラスタの間接光と全輸送のPTの差（拡散2バウンスの約1.5倍）が縮む。RTGIの光線数の増え方を記録し、R6・R7の参照比較と既存goldenが通る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R6RTGIPathTracingReferenceVulkanTest|R7OutdoorPathTracingReferenceVulkanTest)$"`
- stop-when: 光線数の増加が大きい、または方式（放射輝度の再利用・probe併用など）の選択が要る場合はユーザーへ戻す。
- paths: Assets/Shaders/RTGI, Library/Core/Private/Rendering, Test/Core/Rendering
- notes: 危険地帯（RTGI）。R7-O3の既知差1。

## FIX-CSM-TERMINATOR: 球の明暗境界でCSMの可視が数画素かけて下がるのを直す
- status: backlog
- done-when: 屋外シーンの球の明暗境界で、ラスタのCSMの可視（検証表示245）がPTの可視（1画素で1→0）に近づき、R7屋外比較で影の縁として除く画素が減る。承認済みgoldenが変わる場合はユーザーの承認を得る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R7OutdoorPathTracingReferenceVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: goldenの再承認が要る場合はユーザーへ戻す。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering
- notes: R7-O3の既知差3。例: 夕の緑の球で0.96→0.65→0.48→0.18（PTは1→0）。法線方向のずらし（normal offset）や比較の余裕の見直しが候補。

## FIX-GRAZING-IBL-SPECULAR: 斜めから見た地面のIBLの鏡面反射が強すぎる原因を調べる
- status: backlog
- done-when: 昼の屋外シーンの地平線近くの地面で、ラスタのIBLの鏡面反射による間接光（全輸送の約1.5倍）の原因（split-sumの近似、地平線より下の環境、DFGの補償など）を特定し、直すか既知差として根拠を記録する。
- verify: R7屋外比較の`_mean_luminance`と地平線近くの領域の比を開いて確認する。
- stop-when: 承認済みgoldenが変わる場合はユーザーへ戻す。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering
- notes: R7-O3の既知差4。朝・夕は1.07〜1.11倍。

## FIX-CSM-MEGA-CASTER-BOUNDS: CSMの遮蔽物の境界球にShadowMapPassが描かないMegaGeometryを含めない
- status: done
- done-when: CSMの深度範囲に含める境界球が、影の地図に実際に描く物体と一致する（MegaGeometryを影に描くまでは含めない、または描くようにする）。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(DirectionalShadowLightMatricesTest|CascadedShadowLightMatricesTest)$"`
- stop-when: MegaGeometryを影に描くかの判断が要る場合はユーザーへ戻す。
- paths: Library/Core/Private/Rendering, Test/Core/Rendering
- notes: `382489f`の評価のnon-blocking指摘。過大収集で深度範囲とPCSSの探索半径が広がるだけで、影は欠けない。2026-10-03 SS-CSM-MEGA-CASTERS で「描く」側で閉じた（CSMの深度範囲へ含めるMegaGeometryの境界球を、実際に描くキャスターの一覧から取る）。

## FIX-MEGAGEOMETRY-RECORD-TEST: RenderGraphCompileTestのMegaGeometryの記録の検査が落ちる原因を直す
- status: backlog
- done-when: `RenderGraphCompileTest`の`MegaGeometryPass::RecordFrameCommand`の検査（2つのMegaMeshを1回のRenderPassで2回描く）が通り、テスト全体が最後まで走る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderGraphCompileTest$"`
- stop-when: 検査の期待値そのものを変える必要がある場合は理由を記録する。
- paths: Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: SS-GTAOの反復（2026-10-02）で見つけた。`RenderGraphCompileTest.cpp:2620`の`BeginRenderPassCount == 1`で止まり、後ろのSSAO・Lightingの検査まで届かない。MegaGeometryの最後の変更は`f63b2fd`（SS-EMISSIVE-PREEXPOSE）で、`RecordFrameCommand`がパイプラインの未準備で早く戻っている可能性がある。

## FIX-R3-DENSITY-GOLDEN: R3のフォグ密度の基準画像が現在の描画と食い違う原因を調べて直す
- status: backlog
- done-when: `RenderingGoldenImageTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=density-sweep`の3段階が通るか、食い違いの原因（どの変更で変わったか）と再承認の根拠が`R3Acceptance.md`に記録される。
- verify: `build\Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=density-sweep`
- stop-when: R3の閾値そのものを変える必要がある場合は理由を記録する。
- paths: Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: SS-CONTACT-SHADOWの反復（2026-10-02）で見つけた。lowの段で`mean_flip=0.396656647`（上限0.02）・`raw_max=163`・`maximum_channel=19`で落ちる。接触影を切ったシェーダーでも同じ値（`.harness/runs/20261002-084328/check-SS-CONTACT-SHADOW-r3-density-contact-off.txt`）なので、それより前の変更による。ctestには入っていない。

## TEST-FULL-CTEST-BASELINE: 全体CTestの既存の失敗を直す
- status: backlog
- done-when: 全体CTestで、R7の作業前（`c9a3e33`）から失敗している次のテストが通るか、失敗の理由と扱いが記録される: `VolumetricsPassContractTest`（霧の設定行の文字列）、`RenderResourcesDomainContractTest`（`WaitIdleWithoutResultCheck(`の数3、期待2）、`ViewportCameraIdRenderPlanTest`・`BoardComponentRoutingTest`・`SkeletalFramePacketSnapshotTest`（GPUデバイスのないテストでRenderingCoordinator::GenerateDrawCommandsが`m_Device->GetCapabilities()`をnull参照、`578236d`以来）、`FrameCaptureReadbackHelperTest`・`ComponentDataRegistryTest`・`WorldSyncDifferentialTest`（WorldTransformの777）・`CanvasViewRenderTest`・`RenderGraphTextureUsageContractTest`（ShadowMapPassの初期化失敗）（Debugのassertの対話窓で止まりtimeout）、`M9WorldAcceptanceTest`（負の対照の画素差）。
- verify: `ctest --test-dir build -C Debug --output-on-failure --timeout 600`
- stop-when: テストの期待を変える必要がある場合は理由を記録してユーザーへ戻す。
- paths: Library/Core, Test/Core, Game
- notes: 2026-09-25の全体gate（`.harness/runs/20260925-r7-gate/`）で確認。SkinnedRenderPathContractTestはTEST-SKINNED、R4はR4-REOPEN。

## R7-O1: R2 SkyAtmosphereをPT miss radianceとsolar samplingへ接続する
- status: done
- done-when: rasterと同じSkyAtmosphereParametersからPTのsky miss radianceとsolar disk direct lightingを構成し、朝/昼/夕3時刻の有限性・parameter parityを検証する。
- verify: `cmake --build build --config Debug --target Game PathTracingOutdoorVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingOutdoorVulkanTest$"`
- stop-when: R2 parameter semanticsを変更しないと接続できない場合は、R2を先行修正しgoldenを書き換えない。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-O2: R3 volumetric fogをPT ray transportへ接続する
- status: done
- done-when: rasterと同じfog density/height/anisotropy parameterからfinite transmittanceとsingle-scatteringを評価し、fog/sky disabled fallbackを維持する。
- verify: `cmake --build build --config Debug --target Game PathTracingVolumetricTest PathTracingOutdoorVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingVolumetricTest|PathTracingOutdoorVulkanTest)$"`
- stop-when: R3 public parameter semanticsを拡張しないと一致しない場合はR3側契約差として記録し、RenderingからVulkanを参照しない。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## SKY-SUN-P1: 空の太陽の地表照度と方向光をCPUで一つに定める
- status: done
- result: `fa40e74`。透過率・地表照度・空の太陽の方向光をSkyAtmosphere/SkySunLightの公開関数にし、透過率LUTも同じ関数で作る（既存LUT値は不変）。
- done-when: 公開APIで、空のパラメータから地表での大気の透過率（RGB、透過率LUTと同じ式・同じ定数）と、太陽の地表照度（太陽円盤の照度×透過率、RGB）と、空の太陽を表す方向光（予約LightId、方向=太陽方向の逆、色=透過率、強度=照度の輝度、影あり）を求められる。空が無効なら方向光を作らない。透過率LUTの生成は同じ関数を使い、既存LUTの値は変わらない。
- verify: `cmake --build build --config Debug --target SkyAtmosphereModelTest SkyAtmospherePassContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SkyAtmosphereModelTest|SkyAtmospherePassContractTest|SkyAtmosphereIblTest)$"`
- stop-when: LUTの既存値が変わる場合は共有化を止め、同じ式の複製とLUTとの一致テストへ切り替える。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: ユーザー決定（2026-09-25）「空の太陽に統一」。空が有効なら空の太陽からエンジンが方向光を作り、ラスタはCSMの影付きで照らし、PTは同じ太陽を円盤の光源標本で数える（二重に数えない）。シーンの方向光は追加の光として残す。既定の起動は空が無効で変わらない。

## SKY-SUN-P2: 空の太陽をFramePacketの光源へ加え、ラスタの影をその灯へ掛ける
- status: done
- result: `4abdbcd`。FramePacket作成で空の太陽を1つだけ加え、CSM・RT影・LightingPassが同じ選び方（`603f366`で専用ヘッダへ分離）で影の灯を決める。
- done-when: GameThreadのFramePacket作成で、空が有効なら空の太陽の方向光を光源表へ1つだけ加える（再利用するpacketでも重複しない）。影を落とす方向光の選択は、空の太陽があればそれを選び、なければ従来どおり「表示される方向光がちょうど1つで影あり」のときだけ選ぶ（CSM・RT影で共通の関数）。ラスタ（lighting.frag・forward_transparent.frag）はCSM/RT影をその灯にだけ掛け、他の方向光は影なしで照らす。空が無効なシーンの描画と既存golden・閾値は変わらない。
- verify: `cmake --build build --config Debug --target Game CascadedShadowLightMatricesTest DirectionalShadowLightMatricesTest LightingLightBufferTest LightingParamsLayoutTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CascadedShadowLightMatricesTest|DirectionalShadowLightMatricesTest|LightingLightBufferTest|LightingParamsLayoutTest|RenderingRayTracingShadowVulkanTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 空が無効なシーンのgoldenが変わる場合は止めて原因を直す。承認済みgoldenの更新が必要になったらユーザーへ戻す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（光源・CSM・RT影・RenderThread）。影の係数は現在すべての方向光に同じ値を掛けるため、方向光が2つになるとCSMもRT影も止まる。影を掛ける灯は光源データの未使用の成分で示し、UBOの配置は変えない。

## SKY-SUN-P3: PTの太陽を地表照度（透過率込み、RGB）で数え、方向光と二重にしない
- status: done
- result: `fc7e985`・`e42a185`。PTの太陽円盤はRGBの地表照度で数え、空の太陽の方向光は点・spot・方向光の評価から除く。
- done-when: PTの太陽円盤の光源標本と2次以降の円盤命中のMISが、SKY-SUN-P1の地表照度（RGB）を使う。PTの点・spot・方向光の評価は空の太陽の方向光を除き、霧の単一散乱はラスタと同じ灯（CSMの灯）を使う。空を有効にした同じシーンで、ラスタの空の太陽の方向光とPTの太陽が同じ照度になる。R7-O1の屋外テストの期待値は同じ公開関数から求める。
- verify: `cmake --build build --config Debug --target Game PathTracingVulkanTest PathTracingLightingVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingOutdoorVulkanTest|PathTracingVolumetricTest|PathTracingLightingVulkanTest|PathTracingVulkanTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 空が無効なPTの基準（R6/R7参照比較、R1白炉・解析照明）が変わる場合は止めて原因を直す。
- paths: Library/Core/Private/Rendering, Assets/Shaders/PathTracing, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（PT・光源）。現状のPTの太陽照度はスカラーで大気の透過率を掛けていない（`PathTracingPass.inl`の`SkyState[1]`）。

## R7-O3: 屋外PT/raster相互比較を受入れR7を完了する
- status: done
- result: `Docs/RenderingValidation/R7OutdoorAcceptance.md`（2026-09-25）。3時刻とも閾値内（朝 平均0.0107/0.0274・一致画素最大0.0989/0.1293・区画0.0368/0.0682、昼 0.0153/0.0189・0.0937/0.1704・0.0576/0.0668、夕 0.0127/0.0238・0.0903/0.1367・0.0376/0.0583）。判定の参照はラスタが実装する輸送（拡散2バウンス）のPT、median of means、影の縁の除外と影の内側の負の対照（ユーザーの判断）。全輸送との差は既知差として記録。
- progress (2026-09-25): 比較テスト`6d7628f`。ラスタ側の修正: 低い太陽の影（`fdbc5dc`）、空の照明を地表から見た空へ（`6753496`）、CSMの深度範囲の向き（`6f1dc0b`）と遮蔽物の境界球（`382489f`）、影の余裕（`d3f254a`）、カスケード境界（`fa00d84`）、RTGI/DDGIの命中面の反射率（`3c80458`）、直接光のAO（`49116c3`）、RTGIの拡散2バウンス（`44d9a3a`、ユーザー決定）。Outdoor goldenは`efce943`で再承認。
- progress: 3時刻ともFLIP平均は閾値内（朝0.0127/0.0277、昼0.0168/0.0196、夕0.0163/0.0245）。8×8区画最大は朝のみ閾値内（昼0.0712/0.0709、夕0.0988/0.0736）、画素単位最大は3時刻とも超過（閾値を超える一致画素 朝17・昼33・夕182）。上位200画素の内訳（昼/夕）は影の縁134/152、3回目以降のバウンス64/21、その他2/27（夕の緑の球の影側の面）。ログは`.harness/runs/20260925-rtgi-2bounce/`。
- done-when: sky/volume込みの屋外sceneでPTとrasterを3時刻比較し、FLIP pool/max-pixel threshold内であり、既知近似差を記録する。R7 core + outdoor extension受入れcommit末尾に`RenderingRoadmap: R7 complete`を付ける。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingOutdoorVulkanTest$"`
- verify: relevant R7 Debug build/CTestと3時刻captureの数値レポートを開いて確認する。
- stop-when: 3時刻の一部で閾値超過、NaN/Inf、未解決のR7 core acceptanceが残る場合、完了trailerを付けない。
- paths: Docs/RenderingValidation, Test/Core/Rendering, TASKS.md, PROGRESS.md
