# 統合骨格資産のevent-driven CPU runtime

SkeletalAssetRuntimeは独立instanceであり、製品loop/GPU/M9を自動駆動しない。所有者がRegistry、JobSystem、immutable AssetSystemと明示owner threadを一度Bindする。

## ロードと通知

LoadAsyncはowner限定。論理UTF-8 pathを厳密検証・正規化し、同snapshot/pathの未配送groupへ合流する。group IDはinstance内で非再利用・非wrap。拒否はID0でcallback無し。実finite TaskはP1のCPU解析だけを行い、外側catchで非std例外やlogging例外も型付き失敗にする。

完了eventは事前確保requestのintrusive ready slotへ一度linkする。worker/Task cancel/登録callerからconsumerを呼ばず、Resourceも作らない。全Pendingの状態pollはしない。handoff終了counterはTaskのterminal stateとは別であり、requestの一時参照を解放した最後に減らす。

ownerのFlushCompletedでP2aの未登録組立→4型Registry一括公開を行い、delegateをlock外で呼ぶ。通常失敗もtyped completionを一度配送する。成功aggregateは全clipを保持し、消費者は強参照をcopyして寿命を延長する。runtimeはFinished後に永久strong cacheを持たない。

## batchと再入

maxLoadsはgroup配送pass数でありcallback数/時間ではない。0はFlush開始時にreadyだった全group。入口でPendingCallbacksとBatchCallbacksを分け、callbackから同group・選択済み別groupへ追加した購読も次Flushに回す。新規load/cachehitも同様。再入FlushはReentrant、active snapshot変更はBusy。

callback予約をState mutex内で行い、それを開始境界とする。予約後にCancel/Closeが来てもその1呼出は完走できる。callback例外はsubscriberごとに捕捉し、残りの正常配送とcleanupを進める。Delegate member targetは非所有なので、未開始通知の取消だけで実行中targetの破棄が安全になるとは限らない。

## 取消と終了

Cancelはgroupの未予約通知を抑止し、同じgroupである場合だけByKeyを外す。ByIdは配送/cleanupまで残す。Task取消はState lock外。assembly前後とcommit前で取消/閉鎖/snapshot/sessionを検査し、最後の検査とP2a commitはState→Registryの順にlockを保持する。取消が先なら登録0。commitが先ならbundle全部が公開済みで、後勝ち取消はそれをrollbackしない。

Closeは受付を閉じ、既存Owned listからTaskを一つずつpinしてlock外で取消す。追加vector確保やTask/consumer終了待ちはしない。ただしmutex獲得待ちはあり、lock-freeや時間上限ではない。Drainはclosedのみで、admission/terminal handoff/Flush/callback/cancel pin終了後に既存requestを一つずつlock外で破棄する。cleanup中の新Cancel/Closeは所有を取り直さず、複数Drainは一つのcleanup完了を待つ。

自身のcallback/active操作内DrainはDeferred。外側で改めてDrainedを確認する。callbackからruntime自身を同期破棄する運用は非対応。Registry/JobSystemの寿命とInitialize/Shutdown排他はDrainedまでcallerが保持する。DrainはJobSystem全体のqueue空や全TaskPtr物理破棄を保証しない。

## cache-domainと世代

Registry mutex内の専用counterでsession/ordinalを発行する。ResourceIdや架空Resourceは使わない。keyはsession:domain:snapshot-generation:default:pathの実文字列。in-flight索引はIdentityを使い、Identityの保持viewと入力key、既存Requestの完全keyをそれぞれ照合する。hashだけで合流せず、不一致は拒否する。同Registry内の別runtime・再生成・新sessionを混同しない。別runtime間の同snapshot共有は今回保証しない。

SetSnapshotはownerかつ完全idle限定。同pointerは無変更、別pointerは世代を非wrapで進める。旧資産の利用者参照は保持する。cache hitも遅延通知で、配送中だけaggregateをpinする。外部参照解放後は既存Registryの2sweep GCで回収し、次回は新ResourceId/handle世代になる。

## 有限性と検証範囲

group/subscriber/path/key/clip/Registry cloneメタデータに有限上限を置く。P2aのshadow copyは追加ごとO(既存pool)であり高スループット/RSS/時間上限を保証しない。Closeの既存list再走査も件数依存である。

試験は実simple/work-stealing Taskと3clip cooked fixture、実P1/P2a/Resource/Samplerを使い、常時有効CHECKと子process watchdogでDebug/Releaseを反証する。private hookのbad_allocは境界制御フローの証拠で、実allocator全OOMの証明ではない。一般Taskの任意worker/先行handler例外、OS同期障害、異常capture destructor、任意Registry並行破棄は契約外。製品GameThread配送とGPU/実キャラ品質は別途検証する。

反証ではSubmit返答より前にworker/handler完了を固定し、Closeしない正常経路でも受理確定後のready1・公開/通知各1を確認する。commit内で停止して別threadからState mutexを試行し取得不能、commit外では取得可能であることを確認する。これはnative同期操作と実装を結び付ける検査で、Registryの読取待機だけでState gateを代用しない。

既知の入口制約: 現AssetManifestのJSON文字列取得はASCII限定で、非ASCIIのlogical_pathを拒否する。runtimeはUTF-8の構文を厳密検証するが、非ASCII論理pathのcooked読込成功までは保証しない。現統合試験はASCII manifestで正常読込し、合法UTF-8の未登録pathはResolveRejectedとして一度通知する。GR84のUnicode物理locatorの検証とは別の境界である。manifestのUTF-8化は、旧拒否契約/lookup/cook互換を扱う独立変更として残す。

## slot ごとの材質の組み立て

3 資産の split の `SkinnedMeshResource` は、MATS の材質 record と 4 つの論理パス（albedo・normal・arm・emissive）を slot の名前と並べて保持する。`GetSlotMaterialSources` は `GetMaterialSlots` と同じ slot 順で返し、旧経路と `Unload` 後は空になる。`GetSplitMeshGeneration` は `SetSplitMesh` の成功と `Unload` ごとに進む資産の世代である。

`ResourceIO::SkinnedMaterialBuilder` は `RenderResources` を借用し、構築時に明示した owner thread（GameThread）だけで `Build`・`Bind`・`Collect`・`Clear` を受ける。破棄も owner thread で行い、`RenderResources` は builder より長く生かす。

`Build` は slot ごとに、record と 4 つの論理パスを受ける skin 用の `BuildImportedMaterialStaging` の多重定義を使い、静的と同じ `ValidateImportedOpaqueMaterial` で検査する。続いてスキンで表せない Mask・両面・AO texture を使う場合の恒等でない `occlusionStrength` を拒否する。

次に cooked の texture を解決して検査する。アルベドは sRGB で全 mip の alpha が不透明、normal と ARM は linear を要求し、単層 RGBA8 と完全な mip 列も検査する。ARM は使う channel だけを R8 に分ける。全 CPU 検査と texture の世代の再確認を終えてから GPU の texture を作り、`MaterialCreateData` に handle と材質値を揃えて `Materials().Create` に渡す。正の nits の発光画像は検査で拒否し、0 nits の発光画像は読み込まない。

失敗はその slot の状態と理由で返し、ほかの slot の作成を続ける。slot の状態は `Created`・`InvalidMaterial`・`Unsupported`・`TextureFailed`・`CreateFailed` で、1 つでも失敗すれば全体は `PartialFailure` になる。slot 0 の既定の材質には落とさない。

理由は `imported_opaque: ` に名前を続ける。静的と共通の材質検査には `layout`・`shading_model`・`invalid_material_values`・`logical_path`・`alphaMode_blend_unsupported`・`normalScale_requires_shader_support`・`emissiveTexture_requires_shader_support` がある。texture の解決・検査には `cooked_texture_required role=albedo|normal|arm`、`texture_profile`・`texture_mip_count`・`texture_mip_dimensions`・`texture_mip_bytes`・`texture_incomplete_mip_chain`・`opaque_albedo_alpha_requires_shader_support` などがあり、texture 検査の理由には ` role=albedo|normal|arm` が付く（`|` は候補の区切り）。

スキン側で追加する理由は `alphaMode_mask_unsupported`・`doubleSided_unsupported`・`occlusionStrength_requires_shader_support`・`texture_exception`・`material_create`。texture の作成時には `texture_generation_changed`・`texture_finalize role=albedo|normal`・`arm_channel_finalize`・`arm_channel_owner`・`arm_channel_pointer` も返す。

builder の項目は mesh の弱参照と世代、作った材質と ARM の R8 texture の所有を持つ。同じ mesh・同じ世代の 2 回目の `Build` は以前の結果を返し、作り直さない。アルベドと normal は texture のキャッシュの handle であり、builder は所有しない。

`Collect` は mesh が破棄された・読み込み済みでない・世代が変わった項目を離す。項目は材質を先に解放してから texture の所有を離す。`Clear` と builder の破棄では全項目を離す。

`Bind` は component の mesh と結果の資産 ID・世代を照合し、作れた slot だけ `SkinnedMeshComponent::SetSlotMaterial` する。split の mesh は描画 lease を出さないので、現在は `SetSlotMaterial` が失敗し `Rejected` になる。split の描画はこの境界の外である。

スキンの GBuffer は材質の BaseColor を読まないため、色は cook の焼き込みに頼る（[CookedMaterialV1.md](CookedMaterialV1.md)）。`MaterialCreateData` には staging の BaseColor をそのまま入れる。

取り込みの既定の `doubleSided` Auto は開いた形に両面を付けるため、その slot は `doubleSided_unsupported` で失敗する。スキンの資産は取り込み設定に `"material":{"doubleSided":"force_false"}` を置く。`materials` の試験もこの設定で cook する。

`SkeletalSplitRuntimeTest` の `materials` の事例は、2 材質の rig を cook して読み、FakeDevice の上で slot の数・名前・`MaterialCreateData` の値、MASK と texture の解決の失敗が該当 slot だけを失敗させること、資産の解放と `Collect` による材質の回収を確かめる。GPU の描画・実物の見た目は確かめていない。

## 確認用のシーン asset-preview

`asset-preview` は開発ビルド（Debug・RelWithDebInfo）だけの資産確認用シーンで、ランチャーのカード「資産の確認」から入る。Release には含まれない。直接入る場合は `--scene=asset-preview` を指定する。資産を指定しない場合は `ASSET_PREVIEW_EMPTY` を記録する。

### 起動引数と視点

資産の引数は次の `=` 付きの形で渡す。論理パスは元の glTF/GLB に対応する名前であり、cooked の物理ファイルや package のパスではない。

| 引数 | 動作 |
| --- | --- |
| `--asset-preview-model=<論理パス>` | 静的モデルを追加する。繰り返し指定でき、`RenderResources::MegaGeometry().LoadModelAsync`（内部は `ModelAssetRuntime`）で読む。 |
| `--asset-preview-skeletal=<論理パス>` | 骨格つきの資産を追加する。繰り返し指定でき、`SkeletalAssetSession` の runtime の `LoadAsync`（単一パスの NVSKEL の経路）で読む。 |
| `--asset-preview-clip=<クリップ名>` | 骨格つきの全資産で同名のクリップを再生する。指定しない場合、または該当クリップが見つからない場合は bind の姿勢にする。 |
| `--asset-preview-time=<秒>` | 指定クリップをこの時刻で止め、撮影の姿勢を固定する。省略時は先頭から繰り返し再生する。 |

骨格つきの資産は既定で cooked を使う。cooked が無い資産を glTF/GLB から直接読む場合は `--allow-loose-skeletal` も指定する。[SkeletalAssetLoader.md](SkeletalAssetLoader.md) の規則どおり、manifest・variant の欠落による loose の解決だけを許し、壊れた cooked から loose へは切り替えない。

視点は `--scene-view=<名前>` で指定する。自由飛行カメラで移動でき、`--capture-views=front,side,close,top` を使えば 1 回の起動で複数の視点を撮影できる。

| 視点 | 位置・向き |
| --- | --- |
| `front` | 既定。+Z 側から正面を見る。 |
| `side` | +X 側から横を見る。 |
| `close` | 並びの中央の資産へ寄る。 |
| `top` | 真上から見る。 |

次の指定は起動を止め、`ASSET_PREVIEW_ARGS_INVALID` を記録する。

- 未知の `--asset-preview-*`、空のパス、同じ種類での同じパスの重複。
- `--asset-preview-clip` または `--asset-preview-time` の 2 回目の指定。
- 数でない・負・有限でない・100000 秒を超える時刻。
- clip を指定しない time、skeletal を指定しない clip。

`--scene` で別のシーンへ直接入る際に asset-preview の引数があれば、`ASSET_PREVIEW_ARGS_IGNORED` の警告を記録する。

### 起動と撮影の例

以下はリポジトリのルートから実行する PowerShell の例。既存の開発ビルドと、静的モデルの cooked データ・対応する runtime manifest を用意しておく。静的モデルの cook と資産のルート・manifest の設定は [AssetCookWorkflow.md](AssetCookWorkflow.md) を参照する。`--allow-loose-skeletal` は骨格つきの資産への許可であり、静的モデルの cooked データの代わりにはならない。

直接の起動では、M9 の fixture の `Wave` を 0.5 秒で止め、静的モデルを 1 つ並べる。

```powershell
& ./build/Game/Debug/Game.exe `
  --scene=asset-preview `
  --allow-loose-skeletal `
  --asset-preview-skeletal=Models/M9Skinned/ValidU8Float.gltf `
  --asset-preview-clip=Wave `
  --asset-preview-time=0.5 `
  --asset-preview-model=Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.gltf
```

`Game/CMakeLists.txt` は実行ファイル `Game` を定義し、`AssetCookWorkflow.md` と `Scripts/CaptureScene.ps1` の実行先も `build/Game/Debug/Game.exe` になっている。骨格の論理パスとクリップ名は `Game/GameModes/Rendering3DTest/M9WorldAcceptance.h` の `SkeletalPath`・`ClipName` に対応する。

静的モデルの元資産は `Assets/Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.gltf`。例は [NVMESHv0.md](NVMESHv0.md) の manifest の `logical_path` と同じ表記を使う。`ModelAssetRuntime.cpp` の `TryBuildPlan`・`LoadModelAsync` は論理パスを正規化し、先頭の `Assets/` を除く。`Rendering3DTestRoutine.cpp` も `modelPath` を `MegaGeometry().LoadModelAsync` に渡すため、ここでもモデルの要求には元資産の論理名を使う。

同じ資産を 4 視点から撮る場合は、出力先を `-OutDir` に指定する。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 `
  -Scene asset-preview `
  -OutDir ./build/Captures/asset-preview `
  -Views front,side,close,top `
  -RequireAssets `
  -GameArgs allow-loose-skeletal,asset-preview-skeletal=Models/M9Skinned/ValidU8Float.gltf,asset-preview-clip=Wave,asset-preview-time=0.5,asset-preview-model=Models/Rendering3DTestSilverGltf/Rendering3DTestSilverGltf.gltf
```

`-GameArgs` は各引数の先頭の `--` を除き、カンマ区切りで渡す。この用途では値にカンマを含めない。`-Configuration` の既定は `Debug`。上の撮影ではスクリプトが `--capture-views` を付け、1 回の起動で各視点の PNG と `metrics.json`、ログ、シーンの報告を出力先へ保存する。

### 配置と材質

全要求が成功または欠けとして決着してから、静的モデル（指定順）→骨格つき（指定順）の順で +X 方向へ一列に並べる。隣り合う AABB の間を 0.5 m 空け、各資産の最下点を Y=0、Z の中心を 0、並び全体の X の中心を 0 にする。骨格つきの AABB は bind の姿勢の頂点にメッシュノードの変換を掛けて求める。

床は 1 m ごとの格子で、原点を通る線に色が付く。各資産の原点（Entity の位置）には、長さ約 0.3 m の軸の印を置く（+X は赤、+Y は緑、+Z は青）。

骨格つきの材質は `SkinnedMaterialBuilder` の `Build`・`Bind` で slot ごとに付ける。付かない slot は既定の材質で描き、`ASSET_PREVIEW_MATERIAL` を記録する。このシーンが使う単一パスの `LoadAsync` は split でない旧経路なので、`Build` が `NotSplitMesh` を返す資産も既定の材質になり、`status=not_split_mesh` を記録する。静的モデルの材質は cooked モデルのまま使う。

3 資産の split を読む `LoadRigSplitAsync` はこのシーンの引数では使わない。split の mesh の描画 lease は `G2-GR79-SKINNED-MATERIAL-BUILDER-P1` の範囲である。

### ログと欠けの報告

- 配置した資産ごとに `ASSET_PREVIEW_PLACED kind=model|skeletal path=… x=… y=… z=…` を記録する。
- 資産が欠けると `ASSET_PREVIEW_MISSING path=<論理パス> reason=<理由>` を記録する。クリップの欠けも報告し、資産自体は bind の姿勢で置く。
- 読み込み元の cooked・loose は GR83 の `AssetLoadProfile` のログ（`skeletal_asset_resolve` などの `source=`）で確認する。シーン独自の読み込み元のログは出さない。

欠けの理由は次のとおり。

| 理由 | 内容 |
| --- | --- |
| `admission_rejected` | 静的モデルの要求の受付を拒否した。 |
| `admission_rejected_<数値>` | 骨格つきの要求の受付を拒否した。数値は `SkeletalRuntimeStatus` の値。 |
| `load_failed` | 静的モデルの読み込みに失敗した。 |
| `skeletal_<failure>` | 骨格つきの読み込みに失敗した。`SkeletalRuntimeFailure` の snake_case（例: `skeletal_resolve_rejected`）。 |
| `skeletal_resource_load_failed` | 完了通知は届いたが、骨格つきの資産が無かった。 |
| `clip_not_found clip=<名前>` | 指定クリップが無い。資産は bind の姿勢で配置する。 |
| `invalid_bounds` | 配置に使う境界が不正。 |
| `skeletal_runtime_unavailable` | 骨格つきの runtime が使えない。 |

`--scene-report=<path>` の報告には、シーンの組み立て中に欠けごとに `missing_asset=<論理パス> reason=<理由>` の行を追記する。`Scripts/CaptureScene.ps1 -RequireAssets` はこの行を検出して失敗にする。報告による欠けの検出は Release でも有効だが、`asset-preview` 自体は開発ビルドだけで使える。

`SCENE_READY scene=asset-preview` は、全要求が決着して配置を終え、最初の数フレームを過ぎたときに 1 回だけ記録する。欠けがあっても出るため、資産が揃ったかどうかはシーンの報告と `-RequireAssets` で判定する。

### 既知の限界

- 静的モデルは cooked のメッシュ（NVMESH）に AABB が無いため、MegaMesh の境界球から AABB を作る。床より下には沈まないが、少し浮くことがある。
- 3 資産の split は未対応で、単一パスの骨格資産は旧経路の既定の材質で表示する。
- クリップで動いた分は配置の空きに含めないため、再生中に隣の資産と重なることがある。

実際の起動による確認は `G2-REAL-DOG-ASSETS-CHECK`・`G2-GR82-REAL-RIG-CHECK` で扱う。ホットリロード・UI の操作・比較の表示は GR101（確認ビューア）の範囲である。
