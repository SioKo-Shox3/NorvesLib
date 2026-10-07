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
