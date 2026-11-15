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
