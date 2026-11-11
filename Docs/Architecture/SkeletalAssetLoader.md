# 統合cooked骨格資産のCPUローダ（GR83-P1）

現在のNVSKEL 0.0〜0.2を対象とするprivateなcold-load核。任意の論理パスからmesh/skeleton/全clipを所有するbundleへ接続する。非同期queue、cache、Registry登録、M9/Game配線、GPU uploadはこの入口に含まない。

## 二つの境界

LoadCookedSkeletalForWorkerはimmutable AssetSystem snapshotと所有論理パスを受け、default variantのModelを解決する。FailOnCookedFailureだけではmanifest欠落のloose読みが残るので、UsedCooked・Skl0・既存format・CookedVersionを明示検査する。外側entry hashを通った内側blobを既存parserへ渡し、metadataがある数量だけを実解析と比較する。既定では loose を成功扱いせず、明示の許可があるときだけ glTF/GLB の骨格資産を読み込む。static mesh/未知formatを成功扱いしない。

CookedSkeletalCpuAssetの所有stateはloaderだけが生成する。解析表の可変参照は公開せず、default/moved-fromは空。SourceBlobと全配列を保持し、snapshotや一時入力が消えてもCPU結果は有効。loader失敗では以前のCPU結果を保持する。

AssembleCookedSkeletalAssetは初期化済みRegistryと事前設定owner ThreadIdを要求する。現在threadを自動的にownerへ設定しない。owner不一致・未設定・Registry未初期化・空CPUを生成前に拒否する。OS main threadや製品GameThreadを自動認識する仕組みではなく、製品のowner設定と完了delegateの接続は後続で行う。

const CPU結果を保持して再試行できるよう、組立候補へ解析配列をcopyする。CreateResourceがID発行とInitializeを行い、候補のmesh/skeleton/全clip/aggregateがLoad成功したときだけoutを置換する。CreateTransient/Registerは使わず、途中失敗時に無関係なRegistry GCを走らせない。候補の強参照破棄で配列・leaseを解放する。Registry pool/path/count/既存handleは保持するが、失敗で消費したIDの欠番やIdentityPoolのintern文字列まで巻き戻すとは称さない。Registryを再初期化してIDを再利用する間、生きたbundleを持ち越さない。

同じpathの再呼出でも新しいbundleを作る。cacheや共有骨格の完成を意味しない。全clip順・値・submesh/slot・mesh transformを保持し、GetClip(name)の空/不在/重複拒否を維持する。clip0 fallbackや自動renameを行わない。旧単数APIも変更しない。

## loose の読み込み(明示の許可)

骨格資産は既定で cooked だけを読み込む。許可は `SkeletalAssetSession::SetAllowLooseSkeletal(bool)` で設定し、`IsLooseSkeletalAllowed()` で参照する。`BindSnapshot` 成功後の変更は `AlreadyBound` で拒否する。
許可はセッションから `SkeletalRuntimeLimits::bAllowLoose` を経て、`LoadAsync` が `CookedSkeletalLoadPlan::bAllowLoose`（既定 false）へ写す。
Game の入口は開発ビルド（`NORVES_BUILD_DEVELOPMENT`）だけの `--allow-loose-skeletal`。Release では受け付けず、有効時は `SKELETAL_ASSETS allow_loose=1` を記録する。
許可があっても、manifest 欠落・variant 欠落で resolve が `Source=Loose` を返したときだけ、worker で glTF/GLB を `Skeletal::DecodeSkeletalGltf` に渡し、cooked と同じ CPU 結果を組む。
resolve は `FailOnCookedFailure` を維持し、cooked の読み・parse・entry・hash の失敗を loose に代えない。許可なしの loose は `source=none` の解決失敗になる。
`AssetLoadProfile` の `skeletal_asset_resolve`・`skeletal_cooked_parse`・`skeletal_resource_create` は loose 読み込み時に `source=loose` を記録し、組立の段は CPU 結果の source を引き継ぐ。Resource 生成は GameThread（owner）の組立だけで行う。
分割の 3 資産（`LoadRigSplitAsync`・`RigSplitAssetLoader`）は loose を持たず、許可があっても cooked だけを読む。
理由は 2 つ。論理パス（`Models/Rig.nvskel`・`Models/Mesh.nvskel`・`Animations/*.nvclip`）がソースの glTF の名前と対応せず、静的モデルの loose と同じ規則では辿れない。1 つの glTF から 3 資産を切り出す処理（`RigSplitCook`・材質の計画・clip bank）は `Tools/AssetCook` にあり、Core から使えない。
許可は分割の要求へ渡さない。`SkeletalAssetRuntime.cpp` は `bAllowLoose` を単一骨格の計画（`!splitRequest`）にだけ写し、`RigSplitAssetLoader.cpp` は `FailOnCookedFailure` と `UsedCooked` の検査で cooked に固定している。
`Scripts/SummarizeAssetLoadProfile.ps1 -RequireCompleteCookedSkeletal` は上の 3 段すべてに `source=cooked`・`success=1` を求める。`-ExpectedCookedSkeletalLogicalPath` は任意で指定でき、モデル版のスイッチとは併用できない。

## 検証と制限

独立MEMBER試験は旧minorと0.2複数clip、論理パス正規化、外側hashと有効外側内の破損payload、metadata、owner不一致、未初期化Registry、作成途中の拒否/例外/Load失敗、候補weak参照・lease寿命と実Samplerを検証する。元CPU結果・出力・既存登録資源の保持を区別して確認する。

AssetLoadProfileのskeletal_asset_resolve / skeletal_cooked_parse / skeletal_resource_createはsource・status・成功/失敗を分ける。Releaseは既存設定でlogging無効のため、ログ配送の試験はDebug実行で検証する。これはCPU-only試験でありGPUの画像受入れではない。

128関節、現在のmesh必須・単一root・wireの旧minor契約はparserのまま。作者時rest snapshot、安全な別骨格束縛、3資産split、256関節、auto C、root motion、async/cache/delegate配線、M9移行は別の作業。オオカミとシ者の骨格構成は未定のまま扱う。
