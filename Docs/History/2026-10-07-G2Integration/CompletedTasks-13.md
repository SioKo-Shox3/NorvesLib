- result: code2d69f465/treeb7a6beea/run37502233582 attempt1 job112401887301。初回61f325feの新manifest fixture失敗をtest/docsだけで修正、production不変。Release62/24marker、runtime両構成の28 child process・36 subcase marker、従来Debug/旧61cpp・4固定6095byte capture・旧89出力・25+15CLI・12metadata・17/29child退出・GR84別58process/125file・PE-CNG/SHELL32を確認。18checker normal/-Oのreceipt/stdout一致・stderr空、親readonly replay exit0・672payload一致。latest4paths/Library0、累積15paths/Library9。初回failed545payloadは別保持。実worker→ready/ack→明示owner一括公開→遅延delegate、合流/cache/session/domain/snapshot、取消/close/drain/弱参照のCPU契約を受入。非ASCII論理pathのcooked成功・一般OOM/Task例外・製品loop/M9/StageB/GPUは未受入。

## G2-GR83-OWNER-LIFECYCLE-M9-DELEGATE: 実owner lifecycleとM9消費者を骨格runtimeへ接続する
- status: done
- done-when: Core::GEngineの通常メンバとして骨格sessionを所有し、実ApplicationProcessorのBegin/owner更新/Close/Drain/終了から駆動する。Registryの所有/借用を区別し、M9の逐次CreateTransient組立を弱参照delegateと一回completion eventからのowner attachへ置換する。Waveを明示選択し、通常Rendering3DTestを既定のまま保つ。
- verify: 実production helperのCPU lifecycle/受理済snapshot/早期・遅延通知/一回attach/Leave・failed Enter/再初期化、pause中配送とconsumer待ち、borrowed Registry保持・owned空終了、明示clip選択/古いmembership拒否/既存先頭互換。実App call siteをGameビルドとsource接続検査で確認し、描画/XAudio2/実キャラ未実行を分離する。旧CPU/Debug/cook/CLIを維持する。
- stop-when: 未使用NorvesEngine::Updateだけへ配線する、2つのGEngine/rendererを統合する、3系統hot reload transactionへ拡大する、Task状態pollやrawcallback target、borrowed Registry全体のshutdown、旧handle全session無効を保証する、GPU/実物品質をCPU検証から推測する。callback内同期Shutdownは依存解体前に外側へ延期する。

- result: code a082707c / tree 825dcb67 / run 37519489315 attempt 1 / job 112460910508。Release 63、owner の Debug/Release 計20 child process・20 case marker、実App source 8件×normal/-O、M9 ContractOnly All、Game両構成のcompile/linkを確認。Game execution=not_run。旧62 cppとruntime/finite/Registry/loader/Profile、固定4×6095 byte、旧89出力・25+15 CLI・12 metadata・17/29 child退出・GR84の58 process/125 file・PE-CNG/SHELL32を維持。20 checkerのnormal/-O receipt/stdout一致・stderr空、親readonly replay exit0・711 payload一致。初回run 37516423509のC2664は新fixture3行だけを修正し、失敗548 payloadは別保持。latest 2 paths/Library 0、累積26 paths/Library 9。実owner接続とCPU helperを受入、Game起動・GPU・XAudio2・既定撮影・実キャラ・Stage Bは未受入。

## G2-GR82-B1-CLIPBANK-REST-BIND-POSE: 作者rest付きv1 ClipBankを安全に束縛して実姿勢まで評価する
- status: done
- done-when: 新しい明示rig importで作者のlocal TRSを保持し、v1 ClipBankの必須snapshotとして保存・parseする。名前順の正準topology IDと完全な名前/親照合、現在rigとの全joint rest差検査を経て、明示ownerの未登録Resourceと実Samplerまで接続する。既定は許容超過を拒否し、明示override時だけ差量を所有reportで返して通す。
- verify: 独立wire literal/hash/oracle、作者source破棄後のsnapshot所有、joint順置換/Unicode/親違い/hash衝突、全joint Translationと未アニメjointのT/R/nonuniform scale差、q/-q、境界とoverrideのnegative control、壊れたsection/範囲/有限予算、owner/失敗時outとRegistry保持、実Resource/FK/skin結果。旧0.x reader/writer/cook/CLIと固定Sampler出力、既存GR83 owner/runtimeを維持し、新v1は別期待値で認証する。
- policy: topologyは厳密UTF8 unsigned byte名前順＋正準親indexのFNV-1a64、restを含めず全文も比較。全clipに作者snapshot参照を必須化。暫定許容はT 1e-5m、R 1e-4rad、各軸abs(log scale比) 1e-5で呼出し側から設定可能。初回profileは直接TRS・正scale・単一root・外部親なし・1mesh/1skin・128以下。旧入口は無変更。
- stop-when: 旧cookedから失われた作者restを推測する、IDだけで束縛する、overrideで構造破損も通す、旧goldenを再生成する、helperだけでDoneとする、三path公開/async/cache・Armature/256・GPU/DCCまでこの一件へ混ぜる。骨格共有/ゲーム固有rigは未定のまま。

- result: code 7b423fb1 / tree c10b19de / run 37537055880 attempt 1 / job 112520629199。Release65と新2membersのdirect Debug、codec/binding各3 case markerを両構成で確認。独立literalの992byte bank×2・rest-binding JSON×2・oracle8件×normal/-O、owner20/runtime28 child、旧63 cpp・固定4×6095byte・旧cook/CLI/PE証拠を保持。22 checkerのreceipt/stdout一致・stderr空、親readonly replay exit0・788 payload/448 source/21原API/6 ZIP一致。初回small macro衝突はtest3識別子だけ修正し、失敗599 payloadは別保持。latest2 paths/Library0、累積31 paths/Library19。作者rig→v1保存/所有parse→全名/topology/rest guard→明示override/差report→未登録owner Resource/実Samplerを受入。3資産cold-load/publication/runtime・Armature/256・GPU/DCC/Game実行は未受入。混在Line/Step/Bakeの専用累積境界試験は未網羅として残す。

## G2-GR82-B2-SPLIT-WIRE-COLD-LOAD: 骨格とメッシュを分離保存して同snapshotから姿勢評価へつなぐ
- status: done
- done-when: 明示v1 library cookでSkeleton/SkinnedMesh/既存ClipBankの三roleを保存し、role別1-entry NVPKとfresh manifest計画を所有する。同immutable AssetSystem snapshotからSkeleton・Mesh・順序付きBank群を全て読んで検証・束縛し、明示ownerの未登録bundleから名前指定の実Sampler/FK/SkinVertexを評価する。IBMとMはMeshに持ち、共有可能なSkeletonはtopology/rest/ROOTだけを持つ。
- verify: 独立wire literal/oracleと破損・上限拒否、same-read設定/buffer/material対応、全MATS/slot/texture参照のCPU読戻し、SREFの完全topologyと内容世代pin、複数bank順/重複名/全作者rest比較、実package cold-load/一snapshot/所有/失敗原子性/明示owner、共有Skeletonと異なるMesh IBM、実姿勢と旧Sampler全bytes。新試験はDebug/Release常時検査。旧65 cpp・B1 992byte/差report・GR83 owner/runtime・旧cook/CLIを維持する。
- policy: role1 STRS/TJNT/RSET/ARST/ROOT、role2 STRS/TJNT/SREF/VERT/INDX/IBMS/MNGT/SUBM/MSLT/MATS。profile1はsingle root・直接TRS・128以下・ROOT Identity。B1 role3のschema/bytesは変更しない。論理参照は既存manifest同様ASCII、joint/clip/slot名はUTF8。parse/read/image予算は確保前に検査する。
- stop-when: SkeletonにMeshのIBMを置く、IDだけで束縛する、rest overrideでMesh世代pinを無視する、素材を暗黙Opaqueへ潰す、snapshotをfilesystem transactionと扱う、新CLI/既存manifest合成/Registry公開/async cache・Armature/256/GPUまで同時に完了扱いする。実描画は未対応のまま明示拒否しCPU責務を検証する。

- result: code ef4cac08 / tree e5a2740d / run37553853287 attempt1 / job112575390064。Release69・旧65 cpp不変、新4 direct Debug・12case各構成、独立Skeleton704/Mesh1360byte各2・pose2・oracle8×normal/-Oを確認。B1/owner20/runtime28・旧4×6095byte・89出力/CLI/GR84/PEを維持。12比較器×2と親readonly replayがexit0、827payload・480source・18原API・6ZIP一致。初回run37550413059の新oracle巻き順誤りだけをPython/文書で修正し、production/旧test/goldenは不変。failed710は別保持。latest2paths/Library0、累積57paths/Library37。三資産cooked-only読込・全MATSのCPU所有・未登録owner/実姿勢まで受入。Registry/runtime/新CLI・Armature/256・GPU/DCCは未受入。限定probeのpositive control・2snapshot/既登録pool/Load内部失敗等の追加観測は残す。

## G2-GR82-B3-SPLIT-BUNDLE-PUBLICATION-RUNTIME: 分離資産を既存の一括公開と非同期delegateへ接続する
- status: done
- done-when: B2の同snapshot/順序付きBank検証を唯一のworker読込として使い、既存GR83 runtimeのready/handoff・owner組立・4型shadow公開・一回遅延delegateへ接続する。要求のpolicy/limits/全manifest内容と世代を完全identityに含め、immutable receiptと実child内容をcache取得時にも検査する。
- verify: override後のstrict拒否、順序/内容/variant/limits/世代差、全clip本文変更のcache拒否、3clipで登録+6/path+1、既存poolと全準備失敗の全部-or-zero、取消/Close/Drain/再入/混在legacy、実Session owner/pause外配送と名前指定CPU pose。新Debug/Release常時検査、B2/B1全wire/poseと旧69/旧Sampler/cook/CLI/実Game buildを維持する。
- policy: 新runtime/queue/sessionを作らず既存共有核へmodeを追加。split keyは有限256KiB、旧key4096/path2048は保持。Bank最大16、profile128/Identity ROOT/正scaleは変更しない。receiptとCPU所有はaggregate寿命に従う。別bundleのSkeleton wrapper/IDは独立とする。
- stop-when: workerでResourceを作る、hashだけで一致扱いする、override結果だけをkeyにする、GCで公開失敗を戻す、State→Registry最終gateを分断する、新CLI/ファイル群transaction/Armature/256/GPUへ広げる。描画lease無しのCPU成功と描画成功を分ける。

- result: code55831fe8/treeb83204ae/run37564096257 attempt1/job112607753170。Release71・旧69 cpp/順序不変、新2 direct Debug、publication5case各構成、runtime8scenario×2scheduler＝16child各構成/計32を確認。同一snapshotの実worker→owner四型一括公開→遅延delegate→名前指定CPU姿勢、strict/override分離・全clip改変とcallback内再要求拒否、取消/Close/Drain、実Session pause helperを受入。13比較器×normal/-O計26回と親自身readonly replay exit0/stderr0、1095payload/488source/82原API/6ZIP一致。B1/B2独立wire/pose、固定4×6095byteと旧cook89/CLI/GR84/PEを保持。native失敗・再CIなし。比較器の定数alias漏れとinventory自己出力差は元失敗を保持して補正し、source/native/原API/ZIPを変更しない。26paths/Library18。Gameはbuild-only、GPU/新CLI/Armature/clip-only/256とcross-bundle Skeleton ID共有は未受入。

## G2-GR82: 分離資産の取り込みをロードマップの完了条件まで接続する
- status: done
- done-when: Skeleton / SkinnedMesh / ClipBankの分離、作者rest検査、Armature親、clip専用glTF、ループ/ルート移動要約、拡張節の予約、明示cook入口が接続される。
- verify: 変更箇所のCPU検査を中心に実施し、CIはロードマップの区切りで必要な場合のみ。Windows実行・GPU/DCC実物の未検証は区別する。
- notes: 分離保存・束縛・非同期公開は接続済み。静的親frameとclip専用入力のC++実行は未検証。GR86の256関節、要約とcook入口の残りを続ける。

- G2-GR82継続（GR86関連）: 明示profile 3の256関節経路を実装。129/256/257のケースを既存テストに追加、C++実行は未検証。次はクリップ要約と明示cook入口。

- G2-GR82継続: クリップ解析・時間補正・ANLY保存と将来節の予約を接続。残りは明示file/CLI経路と実資産受入れ。追加C++ケースは実行未検証。

- G2-GR82継続: `--rig-split` の明示ファイルcookとtexture同梱・新規directory公開を接続。実CLI smokeは用意済み・未実行。GR96の混在資産セット/増分/レポートへ続く。G2完了後にマージしG3を開始する（作者指示）。

- acceptance 2026-10-07: c1690c / CI run 37652726746 全項目成功。上記の未検証記述は実装時点の履歴。GPU/DCC/実資産の見た目は別途。

## G2-GR96: 種別横断の一括cookと取り込みレポート
- status: done
- done-when: spec v1互換を保ち、v2のraw/texture/audio/skeletal/clipを同一プロセスでcookし、入力・sidecar・外部file・設定・revisionによる増分判断、集約manifest、最小統計レポート、予算exit 2、jobs 1/4の出力一致が揃う。詳細異常検出は要件GR96の実装順に従って後段へ置く。
- verify: 既存TextureManagedCliTest/RigSplitCookTest等の関連CPUケースと実CLIをまとめて確認。個々の変更ではCIを起動しない。未実行を合格にしない。
- current: mixed batch、分離rig/clip、出力増減、予算/統計、jobsを接続。C++実行は未検証。増分追加は既存package親directory内に限る。--forceで全件を再cookする。--prune指定時だけspec外の資産をmanifestから外し、fileは残して自動再採用しない。
- G2残件: GR84の自動rest補正、向き/ルート分離、出力fpsへの補間、周期検出/切り出し、glTF/BVHの共通対応づけとv1出力を続ける。GR82のANLY要約だけでGR84を完了としない。

- acceptance 2026-10-07: c1690c / CI run 37652726746 全項目成功。上記の未検証記述は実装時点の履歴。GPU/DCC/実資産の見た目は別途。

## G2-GR84: BVHとglTFの共通変換・周期処理・独立bankを接続する
- status: done
- done-when: 作者rest対応、向き/平面ルート分離、出力fps、auto/none/range周期処理、v1作者rest付きbank、CLIと資産セットの依存追跡を接続する。G3の再生時適用は含めない。
- verify: 既存SkeletalBvhClipImportTest、RigStaticRootFrameWireTest、RigSplitCookTestと実AssetCookRigSplitSmokeを区切りでまとめる。毎変更CIはしない。
- current: 共通処理、RMTN、Role Profile拡張、CLI/asset-set接続と反証ケースを実装。native実行は未検証。

- acceptance 2026-10-07: c1690c / CI run 37652726746 全項目成功。上記の未検証記述は実装時点の履歴。GPU/DCC/実資産の見た目は別途。

