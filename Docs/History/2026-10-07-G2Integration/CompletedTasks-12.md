## G2-GR79-GLTF-MATERIAL-SOURCE: glTF材質の係数と参照を読み取る
- status: done
- done-when: glTF標準PBR/normal/AO/emissive/alpha/両面とKHR emissiveStrengthをdouble精度のsource値へ読み、重複/型/値/texture範囲/UV・texture拡張を検査して失敗時出力保持。既存cook経路の受理はまだ変更しない。
- verify: 純source値域/default/texture/発光factor×strengthを通常/O2/sanitizerとMEMBERで検証。実JsonDocumentの読込/失敗保持をnative登録しWindows未実行を区別する。公式glTF schemaのnormalScale符号・cutoff>1・strength既定1/最小0と整合。
- stop-when: texture有無で発光判定、normalScale負やcutoff>1を誤拒否、source doubleを換算前にfloatへ縮める、optional拡張のfallbackと必須拡張の受理を混同、wire/cook/描画へ接続済みとする。

## G2-GR79-DOUBLE-SIDED-AUTO: 両面autoの意味を閉鎖性判定待ちとして保持する
- status: done
- done-when: GR79(6)のdoubleSided=autoは単なるsource復帰ではなく閉鎖性に応じた判断を後段で行うmodeとして保持する。private enum FromSourceをAutoへ訂正しJSON/既定/説明/テストを整合。geometry判定はこの値層で捏造せず未接続を明示する。
- verify: enum値/canonical67B/hashのbyte不変、親forceから素材autoへ戻ること、未判定のautoをsource値確定と扱わないこと。
- stop-when: autoを無条件にsourceと同義にする、境界辺しきい値を値parserに埋める、未実装topologyを実装済みとする。

## G2-GR79-ARM-PIXELS: RGBA8からARM判定と焼込を生成する
- status: done
- done-when: linear RGBA8のAO/MR画像から実histogramを作り、profile済みchannel policyとfactorで畳み込みを判定。全定数なら画像なし、textureが残る場合だけ同寸法を要求して係数焼込RGBAを生成。入力/出力保持とalias拒否を保証。
- verify: 合成AI犬の外れ値/metallic ignore・texture override、factor1同byte/factor焼込、source無し既定・実測区別、異寸法のactive/folded、overflow/不正/出力不足/aliasを通常/O2/sanitizerとMEMBERで反証。
- stop-when: absent textureを実測扱い、画像alphaの不必要な変更、異寸法を黙って誤結合、失敗時部分出力、画像IO/実物/GPU/cook接続まで済みとする。

## G2-MATERIAL-SELECTION-SHARED: 材質識別の共通関数を実装する
- status: done
- done-when: 作者09:00承認通り一意元名/番号+元名を共通resolverへ集約、未一致/同targetへの二重指定拒否、同名警告情報を返す。SourceMaterialとGeneratedSlotのcatalogを区別し、GR79/GR78/GR32へ接続可能な単一の照合規則を固定する。
- verify: 無名1材質/Material_0・同名・番号/名前両指定・逆primitive順・生成名衝突・期待名不一致・invalidUTF8/NUL・失敗保持。旧GR32番号はslot番号のまま。
- stop-when: 生成slot名を元名に復元扱い、別関数へ解決ルールを複製、同名swapを検出可能と主張、未一致を無視して成功。

## G2-JSON-UNICODE-INPUT: UTF8入口とnative文字幅を整合する
- status: done
- done-when: JSON bytes入口を厳密UTF8→nativeの共通APIへ接続し、lexerのchar縮約/数値tokenのwide型不整合を修正。ANSI/UTF16/UTF32のraw/escape/native同名性を契約化し、material resolverで推測修復しない。
- verify: 骨+狼emoji/Latin、構文文字と同じ下位byteのUnicode、invalidUTF8/surrogate/NUL、ASCII/数値互換を純helperと実native登録で検証。Corewideの実compile/実行未確認を明示。
- stop-when: byte拡幅だけや符号修正だけでdecode済みとする、path用ToCoreStringをJSON修正で一括変更、native実行未実施を合格扱いする。

## G2-S6-WINDOWS-CLI-CI: Windowsで実CLIの分割前基準を採取する
- status: done
- done-when: feature限定の標準Windows runnerで公式Vulkan SDKと実Core/AssetCook/CookedMeshTestをビルドし、固定7smokeの実exe出力・manifest・ログを保存。before baselineを実成功確認する。--asset-set比較とMain分割後比較は後続gateとして残す。
- verify: 最小権限workflow、固定SDK/debug component、実run結果と各caseログ・snapshotを開いて確認。GPU検証と混同しない。
- stop-when: mainへ変更、課金/権限を拡大、失敗を成功扱い、Mainを基準採取前に分割。
- notes: 2026-10-04 10:02 UTC 作者がWindows CI新設・公式Vulkan SDKライセンス同意/導入を承認。既存workflow/run/checkは確認できなかった。

## G2-GR32-MATERIAL-SELECTION-ADAPTER: slot名検索を共通resolverへ接続する
- status: done
- done-when: SkinnedMeshComponentのslot名検索が厳密UTF8変換後ResolveMaterialSelectionを呼び、独立したcallback照合を廃止。番号API/世代fallback/Default/一意空slot名は保持し、重複/NUL/不正Unicodeを拒否する。
- verify: 純adapterの行順とslotindex/Default/重複/空名/NUL/不正UTF8を3mode、native componentの既存fallback+NUL入力拒否。実Core実行はWindowsCI結果と区別。
- stop-when: 元材質番号と生成slot番号を混同、NUL前方一致、GR79/GR78接続まで済みとする。

## G2-S6-NATIVE-TEST-COMPILE: 実Windowsで判明した2件のテスト不備を直す
- status: done
- done-when: CookedMeshV1TestのAnsiStringView/literal不正比較と、CookedSkeletalAssetTestのWindows nearマクロ衝突を修正し、テストの値域/厳しさを維持する。
- verify: 差分の意味不変・名前衝突回帰、Windows CIでCookedMeshTestビルドと既存CLI smokeを再検証。Core/AssetCookはrun37195379624でビルド成功、テスト/CLI実行は未到達。
- stop-when: assertionを削除/弱化して通す、エンジン実装の挙動を巻き込む、未再実行のnative成功を主張。

## G2-S6-SMOKE-ENVIRONMENT: Windows PowerShell検証の子環境を整える
- status: done
- done-when: PowerShell7→Python→CMake→Windows PowerShellでPSModulePathを継承して標準Utilityを見失う経路を、子process環境だけの除去で避ける。親環境・smoke command/fixture/byte条件は保持し、実Glb以降を再検証する。
- verify: 大小文字の異なる環境key/他変数/親保持/Skeletal指定の単体試験、実Windows CLI7case。基準未採取なのでdriver recipe修正は今回を含めて固定する。
- stop-when: OS/ユーザーの恒久環境を変更、manifest比較を弱化、実再試験前に環境原因の確定/全smoke合格を主張。

## G2-MATERIAL-IMPORT-PLAN: sidecarと元材質の解決結果を値所有する
- status: done
- done-when: 幾何設定52Bを維持した新document/loaderが資産layerと素材selector+layer+surfaceを厳密解析。glTF元index/name catalogを所有し、全selectorを共通関数1回で解決、ARM/発光設定とSurfaceNameを値所有planへ届ける。旧loader/v0受理は拡張せず、実cook接続は次の垂直slice。
- verify: raw無名/Material_0/入替/同名警告情報/未一致/二重target/Unicode/JSON寿命後所有/失敗保持、旧幾何APIの拒否互換、sidecarfile入口。native testをCIで実行する。
- stop-when: SurfaceNameをreportだけでruntime対応とする、未対応cookに設定を捨てて渡す、resolver照合を複製、元indexを生成slotへ置換。

## G2-S6-SKELETAL-FIXTURE-URI: 骨格GLB試験の置換対象URIを明示する
- status: done
- done-when: ChangeBufferUriが期待する外部URIを引数で受け、ExtraZeroInfluencesのextra_zero.binだけを正しく除去する。従来fixture.binの既定とちょうど1件の置換assertを維持する。
- verify: 既存JSON/binaryは無変更、抽出後JSONとbuffer長を確認。実Skeletal unit/CLIを再実行し、失敗時出力保持やInfluenceLimitExceededの期待を変更しない。
- stop-when: URIの不一致を黙殺、JSON全体を正規化、エンジン判定やassertを弱化、実全7smoke前に基準採取完了とする。

## G2-S6-MAIN-SPLIT: 単体cookを再利用できる境界へMainを分割する
- status: done
- done-when: d1307c32の実Windows基準（run37200047966）を固定し、CLI外殻と再利用可能な単体cookを分ける。新機能/型体系の全面変更を混ぜず、分割後の実7smoke package/manifest79件が全byte一致する。
- verify: 同じWindows checkout byte/driver/fixture、既存10CPUと単体cook境界1契約、7smoke、保存済みbeforeとafterのraw比較。recipe31項目のうち11はWindowsCRLFであることに注意する。
- stop-when: 条件を弱化/正規化して差を消す、比較未実施で分割完了、baseline期限切れを黙認、asset-set実装を同じ差分へ混ぜる。

## G2-S6-TEXTURE-BASELINE: origin/mainのtexture spec v1出力を固定する
- status: done
- done-when: 固定main b8c5df1のCookTextureAssetSet.ps1とSilverTextures/SilverGltfTexturesを、固定before AssetCook.exeと明示Windows PowerShell5.1で実行し8package+2manifestの生byteを保存する。
- verify: 11入力のGit/checkout hash、exe hash、PowerShell実version/hash、全10file一覧/size/hash、終了コードと各5/3asset。2回の独立出力が全byte一致。
- stop-when: PS serializerを推測/正規化、基準の実行失敗を成功扱い、withdrawn対象で代用、texture基準だけでmodel依存印まで完了とする。

## G2-S6-TEXTURE-SPEC: texture spec v1を所有するC++要求へ解析する
- status: done
- done-when: 固定2specとv1必須field/default variant/Assets prefix/順序を保持した値所有型へ解析する。整数version、重複/未一致型/unsafe出力pathを拒否し、失敗時の出力を保持する。未知fieldとusageは旧仕様どおり無視。
- verify: 実JsonDocumentで2spec相当・寿命・default/override・case-insensitive重複・traversal・version literal・NUL/type不正を検証。既存11CPU/7CLI/79byte一致を回帰。
- stop-when: runtime cook/増分/serializerを未接続なのに--asset-set完了とする、独自型でない公開所有containerを増やす。

## G2-S6-TEXTURE-SPEC-FIXTURE: JSON派生fixtureをbyte安全に構築する
- status: done
- done-when: TString::replaceの終端NULによるsuffix先頭破損を試験入力へ持ち込まないよう、prefix/replacement/suffixをappendする。失敗時は全fixture byteを記録する。parserの検査条件は維持。
- verify: 全Changed needleはBaseに1回だけ、置換後JSONを独立確認し、実Windows12CPU/7CLI/79byte一致を再実行。
- stop-when: malformed fixtureをparserが通すよう変更、試験をskip、汎用TString修正を同じ差分に混ぜる。

## G2-S6-TEXTURE-BATCH: native --asset-setでv1を新規rootへcookする
- status: done
- done-when: C++サービスとCLIがv1を全件cookし、PS5.1集約manifestをspec順に生成。存在しないRuntimeRootだけへ同volume no-replace renameで公開し、固定Silver2specの10fileと全byte一致する。
- verify: 既存12CPU/7CLI/79byte一致、2spec各2回10byte一致、PS5.1 ASCII escape実probe、late画像不正/既存root/出力prefix衝突/公開先競合で不変。
- stop-when: 既存root上書きやmanifest-lastを原子的と呼ぶ、genericrenameで競合先を置換、増分/モデルbatch/production caller移行を完了扱い。

## G2-S6-DEPENDENCY-SNAPSHOT: cook入力の依存fileと生byte印を共通化する
- status: done
- done-when: 単体要求からsource/glTF外部buffer・image/選択sidecarのlocator・presence・全raw byteを値所有metadataへ集め、正規化optionとcooker revisionを含む増分用fingerprintを生成する。sidecar/URIの既存解決を共有し、runtime SourceHashやcooked bytesを変えない。
- verify: raw/texture/modelGLTF/GLB、buffer余剰byte/image変更・欠落復帰、percentURI、sidecar空白変更・auto不在/required/disabled/override移動、revision/option変更、失敗時出力保持。既存13CPU/7CLI79byte/native2spec10byteを回帰。
- stop-when: これだけでSkip/既存root公開を完了扱い、hashだけで出力を信頼、disabled sidecarを読む、不在とpermission errorを混同、競合編集のatomic snapshotを主張。

## G2-S6-OUTPUT-PACKAGE: 増分照合の共通package検証を作る
- status: done
- done-when: callerが渡すpackage bytesと期待manifest参照を照合し、V1単一entry・payload hash・kind/formatの実parse・骨格metadataを検証する。成功時に全packageのsize/hashを返す。ファイルpath解決・所有権・Skip決定は含めない。
- verify: 実単体cookのraw/custom FourCC/texture4形式/audio/static+派生画像/skeletalを受理し、不正table/payload/format/metadata/複数entryを拒否する。失敗時出力保持、padding差の全体hash検出、既存16CPU/79+10byte gateを維持。
- stop-when: 型parseをpayload hashだけで代用、v1を現v0として受理、source freshness/安全path/ownershipを検証したと主張する。

## G2-S6-CACHE-DECISION: 保存出力recordと共通増分判定を作る
- status: done
- done-when: 正規化した現要求と依存snapshotから独立に出力一覧を導き、値所有record・現在manifestの自分の参照・全package印を照合してCook/Skip/Errorを返す。成功cook後のrecord採取は前後依存の一致と全出力検証を必須にする。
- verify: 全kindの実cook→record→Skip、派生画像一覧、入力/sidecar/出力の変更、無関係manifest変更、失敗時出力保持、Windows alias/reparse/危険path/旧record path非採用、既存79+10byte gate。
- stop-when: recordに書かれた任意pathを開く、別keyのfileやsourceを上書き可能な要求をCookにする、全manifest hashをentry cache identityにする、永続化/既存root公開を完了扱い。

## G2-S6-MODEL-NATIVE-PATH: modelのfile locatorをnative pathで共有する
- status: done
- done-when: model fingerprint/cookの外部URIとsidecar探索が同じnative filesystem pathを使い、narrow変換で失われるUnicode source/overrideを黙って別fileへ変えない。既存ASCII入出力のbyte互換を保つ。
- verify: Windowsの日本語・非BMP source/base/overrideと外部buffer・image、cacheと実cookの一致、失敗保持、既存79+10byte gate。
- stop-when: ANSI文字化けを許容、runtime asset logical pathの規約を同時に変更、実Windows未検証のままUTF対応と宣言する。

## G2-S6-COOK-STYLE: 新規cook検証コードの書式を揃える
- status: done
- done-when: 新しい出力検証・共通増分判断とその試験をAllman/制御文brace/bool命名へ揃え、headerのusing namespaceを明示修飾へ置換する。既存単体cooker本体は新設adapter範囲以外を整形しない。
- verify: 明示した識別子・名前修飾の変更後はbrace以外のC++ token一致、文字列literal不変、BOM/CRLF/numstat、既存19CPU/79+10byte gate。
- stop-when: cook/hash/path判断式やserialized文字列を変える、既存全fileの無関係な整形、実検証なしの互換宣言。

## G2-S6-STATE-CODEC: cookの所有recordを厳密な保存形式にする
- status: done
- done-when: owner/root/manifestの現在bindingに結び付いた値所有stateを無損失で解析/直列化し、未知/重複field・危険path・所有衝突・過大入力を拒否する。保存値からfileを開かず、増分判断は既存C++へ渡す。
- verify: 実cook recordの往復とSkip、uint64境界、寿命/失敗保持、全階層schema/所有alias拒否、Windows CPUと79+10byte gate。
- stop-when: stateを所有権の認証と扱う、record欠落を既存fileの上書き許可にする、runtime manifestを変更、file公開とtransactionを未検証で接続する。

## G2-S6-STATE-FILE: 所有stateを安全に新規保存して読み戻す
- status: done
- done-when: caller指定のRuntimeRoot外の同volume stateを排他新規保存し、Missing/Loaded/Errorを区別して読む。既存stateを置換せず、自分のtemp以外を採用/削除しない。
- verify: cook→capture→save→load→共通Skip、write/flush/verify/renameの失敗、既存file/directory/junction/競合、out保持、Windowsと既存byte gate。
- stop-when: locked journalなしでproduction rootとstateを別々に公開、unknown ownershipの自動採用、atomic reader/powerloss耐久性を保証する。

## G2-S6-STAGED-OUTPUT-PLAN: 出力一覧とstage捕捉の境界を共有する
- status: done
- done-when: 共通Prepare/BuildInventory由来の値所有planを公開し、final→stageの変更を出力rootだけへ限定する。始点snapshot・要求意味・fragment全件を照合して共通record採取へ渡す。
- verify: 全kind/派生画像一覧、実stage cook/捕捉、始点後のsource/外部file/sidecar変化とout保持、要求/fragmentの差し替え拒否、read-only計画、Windows CPUと79+10byte/16拒否 gate。
- stop-when: 別の命名/増分authorityを作る、新snapshotで途中変更を黙認する、stage locatorを保存stateへ入れる、lock/journalなしでproduction公開や既存root採用を始める。

## G2-S6-OUTPUT-SET-GUARD: asset集合を跨ぐ出力と入力の衝突を拒否する
- status: done
- done-when: 検証済みplan集合のprimary/派生key・物理path・prefixを、他assetの全依存/spec/control locatorと照合する。書込/所有取得を行わず、単体命名・増分判断を複製しない。
- verify: primary/派生/variantの衝突、case/short alias/hardlink/prefix罠、他asset入力/外部file/不在sidecarとの衝突、無関係file保持、規模上限/計算量、Windowsと既存byte gate。
- stop-when: unknown所有fileを採用する、出力命名を独自実装する、衝突確認なしでstage作成/公開をproductionへ接続する。

## G2-S6-OWNER-ID: 独立したtupleからowner識別子を導出する
- status: done
- done-when: callerが独立に確定したspec/final-root/manifest identityから、versioned length-delimited UTF8 tupleとSHA-256で安定した128bit識別子を導出する。SourceRootや保存stateからownerを採用しない。
- verify: 独立算出の固定vector、field境界/Unicode/上限/不正入力/失敗保持、state codecとの照合、Windows CPUと79+10byte gate。
- stop-when: filesystem alias解決やroot作成を暗黙に行う、識別子を認証/既存root上書き許可と扱う、未確定のresolverをproductionへ接続する。

## G2-S6-OWNER-RESOLVER: owner identityとfile locatorを独立に解決する
- status: done
- done-when: existing specとexisting root/既存直親下の不在rootから物理UTF8 owner identityを導出し、ASCII drive-form保存bindingとnative I/O locatorを分けて返す。read-onlyで失敗時outを保持する。
- verify: 既存/不在から作成後の再照合、Unicode・case・8.3・SUBST、同名spec置換、危険raw名/type/reparse拒否・shareなしspecの属性観測、独立state保存読戻し、Windowsと79+10byte gate。conditional試験の未実行を明記する。
- stop-when: 不在名の将来canonicalを無条件に保証する、保存ownerを採用する、lexical binding差を黙認する、lock/journalなしでroot/stateをproduction公開する。

## G2-S6-DESTINATION-LOCK: destination volume単位で協調writerを排他する
- status: done
- done-when: FINALのcanonical volume GUIDだけでGlobal mutexを共有し、同期callbackの取得/再観測/解除を同一threadで閉じる。Busyは即時、abandonedを明示し、全取得で後段journal検査が必要とする。
- verify: 実thread/process排他、同volume親子/sibling/case/作成前後、callback失敗/例外/再入、実process終了のabandonedとobject再作成、private故障注入、無書込、Windowsと79+10byte gate。cross-session実測は環境がある場合だけ。
- stop-when: owner/spec/stateごとにlockを分ける、aliasや親子rootで同volume排他を回避する、永続crash印/transaction/認証を保証する、ACL/privilege変更やlockfile採用を始める。

## G2-S6-MANAGED-UPDATE-INVENTORY: 旧stateと新planの更新対象対応を値所有する
- status: done
- done-when: 独立ExpectedBinding・state scope・旧state・FINAL plan集合のkey/package対応を値として検査し、package/manifest/stateのbefore-image要件へ写す。初期profileは所有inventory固定、file I/O/Skip/publish許可は行わない。
- verify: 全kind実recordの対応、順序差・値所有、binding/追加除去rename/primary派生/上限/失敗保持、世代上限で変更不可の明示、Windowsと79+10byte gate。
- stop-when: 値の対応だけでfilesystem所有を証明する、未知fileを採用する、独自cook/cache判定を作る、journal discovery/locked再照合/実before-imageなしで公開へ使う。

## G2-S6-MANAGED-STORE-OBSERVATION: 固定管理領域と物理祖先の中断状態を観測する
- status: done
- done-when: volume lock内でownerを再解決し、物理workspace直下と全祖先の固定store名を列挙する。厳密header/indexと全active root IDを照合し、pending・入れ子・未知/置換storeを停止理由にする。成功は読み取り専用の値観測に限定する。
- verify: 実Windowsでcase差/alias/SUBST/Unicode物理親、祖先active/pending、逆順unknown root、同階層と非関連store、壊れたclaim/保存path罠/型/reparse/上限/失敗保持・無変更、既存79+10byte gate。
- stop-when: 保存絶対pathをI/Oへ渡す、観測だけでroot所有や公開を許す、未知既存store/rootを採用する、store初期化/復旧/ACL変更を同時に実装する。

## G2-S6-MANAGED-STORE-INITIALIZATION: 新規管理領域を完全な状態でno-replace公開する
- status: done
- done-when: 既存volume lock内で共通観測を再利用し、StoreMissing/不在rootだけにfresh stageを作る。CNG StoreId・実directory IDのheaderとgeneration1空indexをwrite/flush/readback/parseし、同handleのno-replace directory renameと公開後再観測を行う。
- verify: 既存store/未知root/pending/alias拒否、stage衝突・未知child保持、各write/close/rename/公開後故障、実child process中断、ID維持、Created/PublishedButError/out保持、Windows31CPUと79+10byte gate。
- stop-when: 公開後の失敗でstoreを削除する、未確認orphanを名前だけで掃除/採用する、copy/replaceへfallbackする、runtime root/state/package/journalを同時公開する、ACL/privilege変更を要求する。

## G2-S6-MANAGED-TRANSACTION-INTENT: 相対slotの更新記録を厳密な値契約にする
- status: done
- done-when: bootstrap/updateのimmutable intentを、独立store anchor・固定control side-documentの正確なbytes/IDs・共通state/index codec・manifest readerで構築/往復する。slotは型から導出し、保存absolute pathはI/Oへ渡さない。値の成功は公開許可ではない。
- verify: 実cook/captureとnative before/after image、固定key/package対応、Cook/Skipの値整合、世代/上限/unknown/duplicate/slot escape/同byte別ID/入力寿命/失敗保持、既存Windows・79+10byte gate。
- stop-when: 永続file lineageやwhole-root update交換を導入する、unlistedを変更する、updateで親directoryを再作成する、beforeを再serializeしたJSONで代用する、source不在復旧でsourceを読む、値codecをwrite capabilityとみなす。

## G2-S6-MANAGED-BOOTSTRAP-EXECUTOR: 不在runtimeの公開とpending復旧を接続する
- status: done
- done-when: 初期profileは既存texture spec v1。controller自身が実cook/captureから既知stageとintentを作り、root/state/indexを同volume lock内で公開する。receiptはpending内で最後にcommitし、完全commitまたは完全rollbackを照合した後だけpendingを退役する。source/spec不在で固定pendingを安全に復旧できる。
- verify: 実Windowsのnative IDとexact before bytes、各rename/receipt/rollback/退役境界の実process終了、ordinary/abandoned再開、同byte別ID/未知entry/親置換/side doc破損/no-replace競合/alias、既存32CPU・79+10byte・5診断。
- stop-when: 保存absolute locatorをI/Oへ戻す、独立bindingを導出できると偽る、既存root採用/whole-root update交換/恒久file lineageを行う、未知orphanをprefixで掃除する、ordinary観測を弱める、powerloss/atomic reader保証が必要になる。

## G2-S6-EMPTY-STRING: 空文字のC文字列契約を修復する
- status: done
- done-when: 未確保とムーブ元のTStringのdata/c_strが正しい文字型の静的ゼロ終端を返し、非空/割当/所有権/APIを変えない。
- verify: 5文字型・既存alias・default/null/empty/clear/shrink/move/reuse、printf診断、Windows34CPU・17中断復旧・89byte互換。Busy診断をescapeなしstrict UTF8で読む。
- stop-when: iterator/operator[]の別契約やTStringViewを同時変更する、テストだけで不正なc_strを隠す。

## G2-S6-MANAGED-UPDATE-EXECUTOR: 既存rootの固定inventoryを増分更新し復旧する
- status: done
- done-when: 共通DecideCookCacheだけでNoChange/Cookを決め、既存claim/root/Skip/unlistedを保持する。変更package・manifest・state・indexのexact before/afterを固定pendingで分類し、receipt最後のcommit/条件付きrollbackとsource不在復旧を接続する。
- verify: 共通controller抽出後のbootstrap契約不変、変更なし世代上限、manifest-only更新、欠落/破損package、親置換/別ID/unknown slot拒否、28更新境界とabandonedの実中断、既存34CPU/17中断/89byte/5診断。
- stop-when: 所有inventoryを追加除去する、親directoryを再作成する、root全体を交換/走査してunlistedを採用する、beforeを再serializeする、source不在復旧でsourceを読む、CLI統合やspec v2を同時に始める。

## G2-S6-MANAGED-TEXTURE-CLI: texture v1の通常コマンドを管理更新へ接続する
- status: done
- done-when: --asset-setを共通initializer/bootstrap/updateの薄いadapterへ置換し、旧独立publication fallbackを実行しない。--recover --runtime-rootは親workspaceの固定pendingだけを明示復旧し、新cookを自動継続しない。
- verify: Created/NoChange/Updated、未所有root/既存16拒否、manifest/inventory/owner変更、spec/source不在の明示復旧と引数拒否、既存35CPU/17+29中断/79+10byte/5診断、有限管理metadataの別inventory/原ID記録。
- stop-when: 通常コマンドで兄弟pendingを黙って復旧する、未所有rootを採用する、診断substringでStoreMissingを推測する、比較対象の一括ignoreを追加する、spec v2/ModelCookCache移行まで同時に拡張する。

## G2-GR79-MESH-V1-WRITER-COOK: 明示NVMESH v1の材質writerとcookを接続する
- status: done
- done-when: 既存1primitive/1material profileで明示nvmesh.v1を出力し、共有128B材質、係数/alpha/両面/発光、role別texture、承認済みARM判定/焼込/全定数時省略、解決済み設定hashを実cook/fingerprintへ接続する。v0とruntime v1拒否は維持する。
- verify: glTF/GLB実fixtureの反復byte/reader往復、AI既定/texture override/factor/別AO-MR/全定数、発光換算必須と診断、selector/sidecar拒否、設定差cache invalidation/版不整合miss、既存36CPU/17+29中断/79+10byte/managed CLI gate。
- stop-when: DoubleSided autoをsource値へ黙って置換する、v0 hash/既定を変える、描画受入れを偽る、spec v2/可変inventory/骨格Stage B/多primitive runtimeまで同時に広げる。

- result: code5de8b9f / tree7fb17894 / run37315777388 attempt1 job111782027052。37CPU、新v1実fixture、17/29中断、79+10byte、25+15CLI、metadata12、Python12+4+7 normal/-O、MSVCx64/CNG、3ZIPを独立検証し親も再実行PASS。初回run37313863062のC2678を修正。runtime/GPU・model asset-setは範囲外。

## G2-GR79-MATERIAL-STAGING: packed ARMと全材質値の所有CPU adapterを作る
- status: done
- done-when: cooked v1の全係数/alpha/sidedness/ARM mask/4論理pathをCPU stagingに欠落なく所有する。wire DefaultLitをruntime enumへ明示写像する。係数の再乗算/ARM分割/nits再変換をせず、未接続材質のFinalizeと製品v1ロードは明示拒否を維持する。
- verify: 新native CPU試験でmask8通り/alpha3通り/全float bit/Unicode path/元blob解放/不正値・参照・版・path時out保持を検証。CookedMeshV1Testのv1拒否とv0空表互換、既存37CPU/凍結79+10byte/managed復旧gateを維持。
- stop-when: shader/descriptor/影/透明/複数材質runtimeまで広げる、CPU合格をGPU合格と扱う、既定v0材質を変える。

- result: codec669322b/treea7956787/run37321891650 attempt1 job111802776013で実MSVCx64・38CPU/新旧marker・17/29中断・79+10byte・25+15CLI/metadata12・Python12+4+7 normal/-O・3ZIP/CNGを独立受入。親readonly再実行exit0。Unicodeは手組みdataからの有限保証、GPU未受入れ/製品v1拒否は維持。

## G2-GR79-OPAQUE-RUNTIME: 1材質の不透明v1を既存描画経路へ接続する
- status: done
- done-when: 1submesh/1material/LOD0の対応subsetを判定し、v1材質値をMegaMeshへ渡す。ARMは使用maskのR8だけ生成、scalarは既存同等の1x1 binding、shader/UBO/露出は不変。Opaqueのfactor alphaは保持して無視し、画像alphaの背景漏れを保守的に拒否する。Nits0のemissive参照はuploadを省略して通す。未対応normalScale/alpha/両面/正発光texture等はGPU作成前に理由付き拒否。v0互換維持。
- verify: pure profile/texture検査/selected split/1x1 cache、FakeDeviceで値とupload選択/未対応時GPU作成ゼロ、v0とmodel sync/async/cache回帰。既存38CPU/79+10byte/managed復旧を維持。実GPU画像受入れはこのCPU接続taskと区別し未実行なら残件にする。
- stop-when: shader/alpha-cull/透明/複数材質描画/packed GPU化まで拡大する、未対応設定を落とす、GPU合格を偽る。

- result: code17ce1e04/tree d3680dce/run37336768659 attempt1 job111853407017。実MSVCx64・41CPU/新旧3marker・17/29中断・79+10byte・25+15CLI/metadata12・Python12+4+7 normal/-O・3ZIP/CNGを独立受入し親も再実行exit0。先行2runのinclude阻害を修正し失敗証拠保持。GPU描画は未受入れ。旧v0/loose匿名texture所有の漏れは次の小修正へ残す。

## G2-GR79-LEGACY-TEXTURE-OWNERSHIP: 旧静的モデルの匿名texture所有を閉じる
- status: done
- done-when: CreateTextureFromPixelsの匿名handleをptr取得後にregistryから解放してmodelへ移管し、CreateTexture(data)のupload例外でも作成済み登録を残さない。prepared/cookedの名前付きcache、byte/mip/upload/shader/既定表示は変更しない。
- verify: 旧経路のalbedo/normal/ARM5roleの解放、後半texture作成失敗、upload例外、geometry失敗でregistryが基線へ戻りweakが失効する。public CreateTextureの通常戻り/空dataのhandle所有は維持。既存41CPU/79+10byte/managed復旧gate。実GPU画像の合格とは別。
- stop-when: upload失敗時の戻り値仕様や広いcache設計まで変更する、v0の見た目やbyteを変える。

- result: codea0faaa01/tree9215c4ed/run37343440486 attempt1 job111875985370。実MSVCx64・41CPU/新旧4marker・17/29中断・79+10byte・25+15CLI/metadata12・Python12+4+7 normal/-O・3ZIP/CNGを受入し親もreadonly再実行exit0。5role/例外/途中失敗/通常caller/named cacheの有限検証。GPU/cross-sessionは未実行。

## G2-GR79-MULTI-PRIMITIVE-COOK: 複数primitiveと材質を明示v1へcookする
- status: done
- done-when: 1 glTF mesh内のN primitiveを局所index検査後に連結し、primitive境界を越えずcluster化する。参照元材質を決定的に共有しimplicit defaultと番号0を区別、128B材質と派生画像inventoryをcook/fingerprint/cacheで一致させる。v0と単primitive v1の既存出力、N>1 runtime拒否は維持する。
- verify: 2材質/材質再利用/逆順参照/default/8超primitive、後半不正index/属性と失敗時out保持、2種類ARMと共有画像/色空間衝突/反復byte、後半設定・画像変更と破損packageによるcache miss、readerの全range/材質対応、既存41CPU/79+10byte/managed復旧gate。
- stop-when: 複数mesh/node変換/スキン/LOD階層/描画N>1/モデルasset-setまで拡張する、unsupported材質設定を落とす、GPU受入れと扱う。

- result: code02ec0e14/treee9377b27/run37350305381 attempt1 job111899190790。実Windows41CPU/新multi＋旧4marker、17/29中断、79+10byte、25+15CLI/metadata12、Python12+4+7 normal/-O、MSVCx64/CNG/3ZIPを受入。親readonly再実行exit0/全stderr空。9pathsの差分と旧304証拠fileを保全。N>1 runtime/GPU/cross-sessionは未受入れ。

## G2-GR84-BVH-DECODE: BVHの生データを厳密に解析し所有する
- status: done
- done-when: 1ROOTの階層・親番号・OFFSET・CHANNELS宣言順・End Site・frame-majorの有限double値とFrame Timeを独立所有する。UTF8/数値/重複/行幅/末尾/明示上限を厳密検査し、失敗/確保例外では既存outを保持する。I/O・回転変換・リターゲット・fps推定・NVSKEL出力は行わない。
- verify: 手書きliteralによる6回転順/位置混在/JOINT位置/静止関節/分岐/End Site有無/UTF8/BOM/CRLF/tab/指数の所有と値順、全拒否status・frame幅/数・名前・各limit境界と超過・不正UTF8・末尾・出力保持。実Windows42CPUと既存89byte/managed復旧gate、host構文検査は別記録。
- stop-when: 実ライセンス制限付きBVHをfixtureへ入れる、Blender実装をコピーする、Euler補間/軸・単位推定/RootMotion/CLI/StageBを同時実装する、raw解析をGR84全体完了や行列一致と扱う。

- result: code9c62d3ba/tree4d6dc722/run37356979599 attempt1 job111921766216。実Windows42CPU/旧5＋新BVH marker、17/29中断、79+10byte、25+15CLI/metadata12、Python12+4+7 normal/-O、MSVCx64/CNG/3ZIPを受入。親readonly再実行exit0/全stderr空。旧538証拠file保全。raw解析のみの受入で、matrix/retarget/Blender/CLI/StageB/allocator故障注入は未受入れ。

## G2-GR84-BVH-EVALUATE: 明示された位置規約でBVHのlocal/world姿勢を評価する
- status: done
- done-when: degreeの宣言順回転をdoubleの列ベクトル行列へ合成し、親FKとEnd Site位置を返す。完全XYZ位置は必須enumでOFFSET加算/絶対local置換を選び、位置なしはOFFSETを使う。sourceを変更せず出力を所有、失敗時out保持。Sampler/retarget/軸・単位/fps/CLIを触らない。
- verify: 6回転順の解析行列を1e-9比較、両位置規約のroot/child/End Site literal、非零OFFSET/静止/分岐/複数frame/度の周期性、rawで読めても非対応なpartial/交錯/rotation→position、壊れた親/範囲/enum/finite/overflowで拒否とout保持。実Windows43CPUと既存89byte/managed復旧gate。
- stop-when: 入力の位置規約を自動推測する、Blender互換/実物照合済みと謳う、既存行ベクトルSamplerや骨格bindを同時変更する、retarget/RootMotion/補間/StageBまで広げる。

- result: code6fbbbe8b/tree7d5cab88/run37362515658 attempt1 job111940266049。実Windows43CPU/旧6＋新FK marker、17/29中断、79+10byte、25+15CLI/metadata12、Python12+4+7 normal/-O、MSVCx64/CNG/3ZIPを受入。親readonly再実行exit0/全stderr空、旧572証拠file保全。合成6順1e-9・2位置規約source FK/End Siteまで。target retarget/Sampler/実Blender/軸単位fps/RootMotion/CLI/StageB/GPU/確保故障注入は未受入れ。

## G2-GR84-JOINT-NAME-INDEX: 関節名を厳密なUTF8所有索引で解決する
- status: done
- done-when: Private/Animationの独立値型で非空/厳密UTF8/NULなし/重複なしのjoint名を所有し、完全byte一致で元配列番号を返す。native SkeletalJoint名は既存minor2 NameCodecで変換する。明示count/個別/総byte上限、構築/検索失敗時out保持、copy/moveの所有を保証する。既存Identity索引とSamplerは変えない。
- verify: 入力順の異なる名前/日本語/非BMP/UTF8-16-32-native境界、case/prefix/colon/正規化違い、空/NUL/不正/重複/unknown/各limit/不正span、元入力破棄とcopy/move、失敗時出力保持。実Windows44CPUと既存89byte/managed復旧gate。
- stop-when: runtimeの重複先頭勝ちを変更する、BVH token制約をtarget名へ強制する、名前解決を階層/rest互換とみなす、BoneMap JSON/retarget/Sampler共有/StageBを同時実装する。

- result: code585d7004/tree47a06216/run37368810439 attempt1 job111960454749。実Windows44CPU/旧7＋新index marker、17/29中断、79+10byte、25+15CLI/metadata12、Python12+4+7 normal/-O、MSVCx64/CNG/3ZIPを受入。親readonly再実行exit0/全stderr空、旧604証拠file保全、最終641file inventory一致。厳密名前対応だけの受入で階層/rest/retarget/Sampler共有/CLI/StageB/GPU/実Blender/確保故障注入は未受入れ。

## G2-GR84-SAMPLER-BASELINE: 共有化前の実Sampler出力を構成別に固定する
- status: done
- done-when: runtimeを変更せず既存SkeletalAnimationSamplingTestを実CoreのDebug/Releaseで実行し、固定fixtureの結果を構成別の明示little-endian snapshotと出自記録へ保存する。既存assertを全て実行し、失敗時Clear全体、同構成で反復byte一致、固定case数/順序/完了を確認する。
- verify: bind非一様scale/回転、partial clip、時刻clamp/linear/step、非可換階層、空mesh、既知拒否群、親が後ろ/複数root、shear/反射/極大bind上書きの旧結果を記録。実Windows45CPU・Debug/Release direct capture・旧89byte/managed gate。runtimeの旧commit同一byteとfixture/toolchain/config/hashを記録。
- stop-when: baselineを作るためにruntimeを直す、未知の入力の成功boolを数学的期待で捏造する、Debug対Release同一byteを要求する、captureだけでtarget bind/retarget/Blender/GPUの受入れとする。

- result: code3cd8c479/tree48531542/run37373318284 attempt1 job111975595995。実Windows45CPU/旧8＋Sampler marker、Debug/Release各2回の30case採取、同構成repeat byte、旧89byte/managed全gate/3ZIPを受入。親readonly再実行exit0/全stderr空、旧642証拠保全、最終667file一致。snapshot各6095byte SHAa5b0a90496e9064c2808692706b69f8a11936fea9af3031f25eb909ab33e62d4。case22/23/25成功・24/26拒否は両構成で一致。cl/Core/exe hashはCI実計算receiptで、binary自体の親再hashは未実施。refactor/retarget/GPU/Blenderの受入れではない。

## G2-GR84-BIND-ROW-MATH: bind行列の導出だけを共有し旧Samplerのbit列を保つ
- status: done
- done-when: private plain mathでIBMからbindGlobal、親からbindLocal、既存TRS分解を一元化しSamplerが共有関数を使う。公開Resource/PoseTypes・clip/FK/Compose/Palette/SkinVertex/Clearは変えない。基準run37373318284の固定fixtureと同compiler/config/FP条件でDebug/Release各snapshotが完全byte一致する。
- verify: private helperのroot/parent/非可換/特異/非有限/overflow/alias/失敗out保持と旧TRS振舞い、実46CPU、構成別2回の旧6095byte基準比較、固定fixture hash/compiler/生成option・旧89byte/managed gate。基準は自動更新しない。
- stop-when: shear/反射/途中TRS nonfiniteを新規拒否する、前後不一致を閾値へ緩める、world FK/Resource事前計算/GR12型/作者rest snapshot/retarget/StageBまで同時変更する。

- result: code430a6cbc/tree5239cf56/run37379203280 attempt1 job111996430902。実Windows46CPU/旧marker＋bind marker、Debug/Release各2回の旧6095byte完全一致、旧89byte/managed全gate/3ZIPを受入。親readonly再実行exit0/最終782file inventory一致、旧668証拠保全。検証script更新時の3失敗attemptは保存し、条件を維持した有限差分で修復・一括再照合。cl/Core/Sampler.exeの実byte非保存、CI hash receiptのみの限界は維持。joint-global共有/retarget/GPU/Blenderは未完。

## G2-GR84-JOINT-GLOBAL-ROW-MATH: 親子の行ベクトルFKを確保なしで共有する
- status: done
- done-when: 現Samplerのjoint-global評価をprivate helperへ移し、parentを借用getter、local/global/scratchをSpanで受ける。追加所有配列/確保なしで親順任意・local*parent・visitState・有限検査を保つ。Sampleの公開false/Clearと固定2構成6095byteを維持する。
- verify: rootのsigned zeroを含むbitコピー、非可換literal、親後置/逆順chain/分岐/複数root、cycle/親範囲/無効span、NaN/Inf/overflow、有限singular/shear/反射、cache再訪/失敗後scratchリセット。実47CPUとDebug/Release旧byte各2回比較・旧89byte/managed全gate。
- stop-when: source BVHのdouble列FKへ統合する、helper失敗時の内部出力不変を約束する、毎回の親配列抽出を追加する、clip/Compose/Palette/Resource/GR12/StageB/retargetへ拡張する。

- result: code0225e17f/tree62fe3239/run37387162803 attempt1 job112023133952。実Windows47CPU/旧marker＋FK marker、4実captureが旧6095byteと完全一致、旧89byte/managed/CLI/metadata/Python/MSVC/CNG/3ZIP全gateを受入。親readonly再実行exit0/最終774file inventory一致。旧受入783fileと初回失敗650file保全、累積9paths/runtime3/修正commit2pathsを区別。public Zero定数は別件保留、host runtime/retarget/GPU/Blenderは未受入れ。

## G2-GR84-JOINT-MAPPING-RESOLVE: schema非依存で関節対応を所有解決する
- status: done
- done-when: strict UTF8索引2個と名前pairから元joint番号を解決し、入力順pair/明示root pair位置/元番号順unmappedを所有する。source再利用は必須policyで選び、target重複とroot不在/矛盾を拒否する。成功時だけout置換、失敗/確保例外はout保持。JSON/role語彙を固定しない。
- verify: 元番号/順序/UTF8、未発見/不正名/重複、source policy両種/未指定、root不在/矛盾/範囲、明示catalog/mapping/name-byte上限・不正span、入力/index破棄・copy/move/self代入・失敗out保持。実48CPU/固定4capture/旧89byte・managed全gate。
- stop-when: 旧direct BoneMap v1 JSONを外部契約化する、role経由要件をv2へ先送りする、rootやゲーム必須関節を推測する、名前対応を階層/rest互換と扱う、座標変換/retarget/CLI/StageBを同時実装する。

- result: code75ccc722/tree05afdd4e/run37392452079 attempt1 job112040470502。実Windows48CPU/新mapping＋旧11marker、4実capture旧6095byte一致、旧89byte/managed/CLI/metadata/Python/MSVC/CNG/3ZIP全gateを受入。親readonly再実行exit0/最終918file inventory一致、旧受入775file/失敗650file保全。準備checkerの失敗記録は保持。有限のschema非依存対応解決だけを受入れ、JSON/role語彙/階層rest/retarget/CLI/StageB/GPU/Blender/確保故障注入は未受入れ。

## G2-GR84-COORDINATE-CONVERSION: 明示された軸と単位をdoubleで変換する
- status: done
- done-when: 既存SignedAxisと必須handedness/positive scaleからcanonical +Y上/+Z前/right-handedへのsigned permutationを構築し、t'=sAt、M'=AMA^Tをdoubleのindex/sign置換で評価する。default値は未設定、非finite/overflow/非zeroのzero underflowを拒否しout保持、aliasを許可する。
- verify: 独立proper/improper literal、全48basisのup/forward/determinant/roundtrip、finite非回転行列、行列のscale非依存、軸衝突/未知enum/不正scale/非finite/overflow/subnormal、alias/late failureのatomic。実helperをstubなしhost通常/O2-NDEBUG/ASan-UBSan、実49CPU/固定4capture/旧89byte・managed gate。
- stop-when: modelの正面/軸/単位を推測する、反射をquaternionとして扱う、非回転有限行列を正規化/拒否する、root OFFSET処理/nonroot無視/fps/rest/JSON/CLI/StageBを同時実装する。

- result: code67cb7620/tree75d8b4f9/run37398784660 attempt1 job112060978186。実Windows49CPU/新coordinate＋旧12marker、4実capture旧6095byte一致、旧89byte/managed/CLI/metadata/Python/MSVC/CNG/3ZIP全gateを受入。親readonly再実行exit0/最終1320payload inventory一致、旧受入919file/失敗650file保全。実double helperのhost通常/O2-NDEBUG/ASan-UBSan成功とfast-math拒否も別途確認。有限の明示座標変換だけを受入れ、SO3/rest補正/root処理/retarget/JSON/CLI/StageB/GPU/Blenderは未受入れ。

## G2-GR84-ROTATION-FRAME: 明示補正で1frameの回転を実Samplerへ渡す
- status: done
- done-when: canonical source world回転・既存joint対応・明示proper CからW=C D C^T Bを評価し、実際に生成した親worldで絶対local回転へ戻す。未写像はbind local、複数rootは維持。heading保持/全target平行移動scale保持を必須policyとし、既存float Samplerと同じCompose/共役正規化/FKで生成値を検査した後だけ公開する。
- verify: private純粋kernelの実host通常/O2-NDEBUG/ASan-UBSan、非可換C/D/B・180度・parent/mapping順・未写像親子・forest・source再利用・root/cycle/不正span/late failure。native正uniform非unit scale/nonidentity mesh/IBM、実BVH FK→明示座標→1key clip→実Samplerの回転/末端literal、非uniform/shear/反射/非finite拒否。凍結4capture/旧6095byteと旧全gateを維持。
- stop-when: Cの推測/align_bones・heading補正・root位置・時間補間・loop・JSON/role語彙・CLI/StageBを同時実装する、非uniform/shearのworld回転の意味を黙って決める、旧Samplerへ新しい拒否を加える、float実現値を確認せずdouble結果だけで受入れる。

- result: codec3f7530d/treed4783ad5/run37405962755 attempt1 job112083481089。初回e467b378のC4716を2か所の明示return 0で直し、実Windows51CPU/新2＋旧13marker、4実capture旧6095byte一致、旧89byte/managed/CLI/metadata/Python/MSVC/CNG/3ZIP全gateを受入。親readonly再実行exit0/最終1999payload inventory一致、旧coordinate1321file/失敗rotation1765file/失敗FK650file保全。latest3pathsと累積18paths/11Libraryを区別し、productionは初回候補から不変。明示C・heading/T/scale保持・正uniform targetの1frame回転と実Sampler接続だけを受入れ、自動C/role/clip時間列/root/CLI/StageB/GPU/Blenderは未完。

## G2-GR84-BVH-CLIP-IMPORT: BVHの全sampleを再生可能な回転clipへ接続する
- status: done
- done-when: 明示axes/handedness/scale・C・root対応・source位置規約・HeaderFrameTime/OverrideFpsから、全frame/全mapped targetのLINEAR回転clipと所有reportを生成する。元document全体を検証してから位置を計算しない専用source回転planでFKし、既存native1frame検証を共用する。timeは独立double計算→検査付きfloat、Durationは最終格納timeそのもの、隣接quaternionは半球連続。clip/reportは最後に一括公開。
- verify: 位置のみsource/静止関節、全元値と名前・階層・channel検証、未使用の巨大position/OFFSETでも回転不変、root規約別の有限/計算不能diagnostics、明示時間・underflow/overflow/float衝突、符号境界、実Sampler全key/選択midpoint、nonidentity mesh/scale2/forest、所有/late failure atomic、checked keys/bytes/work予算。新testはbundle改名入口でも明示return。旧51CPU/4固定capture/旧全gateを維持。
- stop-when: role経由を省いた外部JSONを作る、C/軸/fps/正面を推測する、sourceの無視する位置を評価して回転まで失敗させる、key検証を全補間曲線の保証と扱う、rootMotion/loop/resampling/CLI/cooker/StageBを同時完成とする。

- result: code6e514704/tree7d9b9ac6/run37418209451 attempt1 job112121471665。実Windows53CPU・旧15＋新2marker・4固定capture6095byte一致・旧89出力・17/29 child退出・25+15CLI・12metadata・Python全gate・3ZIP・archived PE/CNG importを確認。親readonly再実行exit0、240payload inventory一致。環境消失後の現在runと固定参照による独立再受入であり、失われた旧累積証拠の保全・再現は主張しない。有限のsample保持回転clip/reportと実key評価を受入。自動C/role/rootMotion/resampling/CLI/cook/StageB/GPU/Blenderと補間全域は未完。

## G2-GR84-BVH-COOK-BRIDGE: 型付きBVH要求をNVSKEL packageと実再生へ接続する
- status: done
- done-when: 明示Add/Replace-by-name、BVH bytes・具体名pair・root・C・設定から既存NVSKEL0.2 writerへ接続する。専用target decodeだけanimation欠落/空配列を許し、mesh morph検査を漏らさない。旧入口の1本以上契約と旧hash/byteは保持。clipと所有reportは成功時だけ公開し、実package/manifest→AssetSystem→resolved blob parser→Resource/Samplerまで合成fixtureで通す。
- verify: Add初回/既存保持/重複拒否、Replace一意名/順序変更/無関係clip保持、null/不正animation拒否、0clip Morph Drop/Reject/scale、明示time/Cと全key/選択midpoint、失敗時旧out保持、決定的hashと入力変更失効、package破損拒否。新bundle入口は明示return 0。既存53CPU/4凍結capture/旧89出力/CLI/managed全gateを維持する。
- stop-when: 外部direct-pair JSONやCLIを追加する、従来fingerprintでBVH変更をskipする、writerの128関節/単一root/mesh必須を緩める、旧decodeを0clip許容へ変える、StageBの作者rest検査やGPU/実物の見た目まで完成扱いする。

- result: codeafdd794/tree6e8b044e/run37425986849 attempt1 job112145637479。初回c81e178の新test失敗後、positive BVH2件の改行と失敗診断のみを修正。実54CPU/18marker/4固定capture/旧89出力/25+15CLI/12metadata/17+29child退出/Python全gate/3ZIP/PE-CNGを受入。親readonly再実行exit0・stderr空・387payload inventory一致。latest2paths/Library0、累積12paths/Library2を区別。失敗runは別332payload folderで保持。型付き名前Add/Replace・0clip target・NVSKEL/package/AssetSystem/実SamplerのCPU接続だけを受入れ、外部role/CLI/file依存cache/自動C/StageB/GPU/実物品質は未完。

## G2-GR84-ROLE-PROFILE: ロール経由の設定bytesを明示補正cookへ接続する
- status: done
- done-when: version付き自己完結Profile JSON bytesのquadruped_v1語彙、source_roles/target_roles（joint+C）、root必須、任意required_roles、明示軸/手系/単位/位置/timeを所有解析し、正準role順の具体名pairへ展開して既存typed BVH cookへ接続する。source-only名も実source存在を検査し報告する。名前/配列順を推測・正規化せず、単一roleは1、chainは同長。旧経路不変、raw Profile/算法版/limitsを新hashへ追加し、以前のoutを失敗時に保つ。
- verify: 異なる骨名をroleで接続し実cook→package/Resource/Sampler、非可換C、未写像保持、0clip/Add/Replace、語彙/型/未知/重複/UTF8/必須/root/chain/alias/不正C拒否、JSON byte/depth/token/name/展開予算、入力破棄と失敗保持、object順変更で同一payload・raw変更でhash失効。既存54CPU/固定capture/旧出力/CLI/managed gateを維持。
- stop-when: direct-pair外部JSONを復活する、欠落Cや軸/fpsを推測する、ゲーム用骨格共有や必須4脚を決定済み扱いする、file/CLIを依存snapshot/fingerprintなしに追加する、明示Cの検証を自動rest補正の完成とする。

- result: code22220d56/treeff4a232b/run37435870572 attempt1 job112177403023。初回961d46ddのResourceId未登録fixtureをRegistry経由へ直し、production不変。実54CPU/旧18＋新role marker・4固定capture・旧89出力・25+15CLI・12metadata・17/29child退出・Python全gate・3ZIP/PE-CNGを受入。親readonly再実行exit0・stderr空・358payload inventory一致。latest2paths/Library0と累積10paths/Library3を区別、旧failed339payloadを別保全。role bytes→明示C→cook/package/実SamplerまでのCPU接続を受入れ、chain ordinal>0実cookは未網羅、file/CLI/cache・自動補正・StageB/GPU/実物品質は未完。

## G2-GR84-ROLE-FILE-CLI: BVHとProfileの単体CLIを安全なfile観測へ接続する
- status: done
- done-when: 新mode専用のwide Windows argv→strict UTF8→native locatorで--bvh/--role-profile/--clip-operation/--clip-nameを解析し、既存target import指定と共に単体Skl0 package/manifestを作る。新要求は常時Cookで旧skipを使わず、正しいtyped SourceHashを共通inventory/captureへ接続。source/sidecar/全glTF外部buffer・image/BVH/Profileをsnapshotへ含め、出力alias guardと最終write直前再採取を行う。package/manifest/所有reportは全て構築後に公開し、実出力を再検証する。旧modeのargv/診断/出力bytesは維持。
- verify: 実AssetCook.exeのUnicode（非BMP含む）4locator、空/重複/未知/混在/skip拒否、0clip/Add/Replace、BVH/Profileだけの変更・欠落・read failure・予算、hash/共通Forced判断/record、依存・出力alias/hardlink/reparse/prefix、公開直前入力変更時ゼロwrite、公開開始後失敗の明示Error、単体manifestの別key保護。新CLI smokeは旧frozen inventoryへ混ぜず別に記録。旧54CPU/4capture/89出力/25+15CLI/managed gateを維持。
- stop-when: narrow argvをUTF8と推測する、新要求をglTF-only fingerprint/skipへ落とす、SchemaVersionを全体更新して旧stateを壊す、入力変化確認をcook後のcaptureだけで代用する、複数file transaction/敵対的ABA/電源断回復を保証済みとする、managed texture CLIをmodel/BVH対応済みと説明する。

- result: code3c0fbe71/treeb7eb83a1/run37447482776 attempt1 job112215690482。CPU54/旧19＋新file marker、実Windows58CLI（成功12/拒否46）GetACP1252、4Unicode入力locator＋外部glTF、125file専用artifactの全case/独立raw Profile hash/最終NVPK1411byte・NVSKEL1235byteを確認。旧89出力/25+15CLI/12metadata/17+29child退出/4固定6095byte capture/Python全gate/PE-CNG＋SHELL32を維持。親readonly再実行exit0・stderr空・387payload inventory一致。latest29paths/Library0。常時Cook・共通全依存/同一read照合・write直前再観測・実出力recordまで受入。2file transaction/同時writer・自動C/StageB/GPU/Blenderは未受入。中間出力保持とrepeatはnative CIの検査証拠、未archive binaryはhash attestationと区別する。

## G2-GR83-COOKED-LOADER-CORE: 統合骨格資産を所有CPU解析から安全に組み立てる
- status: done
- done-when: 任意論理pathとimmutable AssetSystemからUsedCooked/Model/Skl0/既存formatを明示検査し、現在のNVSKEL全clipを所有解析する。事前指定owner threadと初期化Registryで、CreateResourceによる未登録mesh/skeleton/全clip/aggregateを完成時だけ返す。失敗時の旧out/Registry pool・path・handle保持、名前引きと実Samplerを確認する。
- verify: 旧minorと0.2複数clip/順序・submesh/slot/transform保持、空/重複名の既存曖昧拒否、cooked限定・format/外側hash/有効外側内の破損payload拒否、owner不一致/default/未初期化Registry拒否、作成途中の候補破棄と非0 ID・所有寿命/lease、3段階profileログ。独立MEMBERを追加し、旧54CPU/固定capture/旧CLI/GR84実CLIを維持する。
- stop-when: GR84の回帰を新変更で隠す、旧parser/writer/128profileを緩める、clip0 fallback、自動Cや作者rest/split/256を混ぜる、未登録bundleをasync/cache/GC済みと扱う、M9/Game/GPU配線まで完了とする。CPU owner-thread契約と未実装の製品GameThread接続を区別する。

- result: codee29a2d33/tree7005c3d9/run37458778661 attempt1 job112252798210。初回768e247aのtest C2678と、f78b9ec7の合成manifest必須項目漏れをtest/PROGRESSだけで修正し、productionは初回から不変。Release CPU55/旧20＋loader marker、Debugのloader/Profile markerと3stage成功/失敗sink配送を確認。旧54source・4固定6095byte capture・89出力・25+15CLI・12metadata・17/29child退出・GR84別58process/125fileartifact・MSVC/PE-CNG/SHELL32を維持。12checker normal/-Oのreceipt/stdout一致・stderr空、親readonly replay exit0・470payload inventory一致。latest2paths/Library0と累積11paths/Library4を区別する。2失敗runは別々の有限archiveで保全。受入れは統合cooked-only所有CPU解析と明示owner上の未登録全clip組立まで。製品GameThread/Registry一括公開/async/cache/M9/StageB/GPUは未接続。

## G2-GR83-SKELETAL-BUNDLE-PUBLICATION: 骨格bundleをRegistryへ一括公開する
- status: done
- done-when: P1組立を呼ぶmove-only prepared値にRegistry session/ownerと完成bundleを所有し、4型shadow poolを経由して子＋aggregateを全部またはゼロで登録する。子はIDのみ、cache pathはaggregateのみ。成功までoutと既存slot/世代/path/handle/count/recordsを保持する。正規key＋元URI・実child handlesを検査するcache Acquireを用意する。
- verify: 3clipでresource+6/path+1/各typed handle/既存memory和、空/既存pool/free-list/欠損型placeholder各段の拒否・例外と再試行、読取側の全-or-zero観測、session再初期化/owner/default/invalid/key衝突拒否、同key同一参照、外部clip保持と2sweep GC/stale handle、lease保持。有限clone予算とO(既存pool)費用を明示。旧Registry/Metadata/SkeletalResourceLifetime/SkeletalAssetResource/P1と既存CLI・固定captureを検証する。
- stop-when: public Registerの例外安全を仮定する、GCで失敗rollbackする、子handle/会計を黙って捨てる、commit列で確保/callback/Unloadする、snapshot cloneを高スループット実装と称する、async配送/製品loop/M9/StageB/GPUへ広げる。Registry寿命・Initialize/Shutdownの排他はcaller契約とし任意並行破棄を保証しない。

- result: code177c1b69/tree780f3603/run37474221718 attempt1 job112305303905。初回8e913414の新試験filesystem include不足をtest/PROGRESSだけで修正しproduction不変。Release CPU60/22marker、Debugのpublication・loader/Profile・既存Registry4memberを確認。旧55cppは不変、共有fixtureは14関数とMagicのinlineだけ。4固定6095byte capture・旧89出力・25+15CLI・12metadata・17/29child退出・GR84別58process/125file・MSVC/PE-CNG/SHELL32を維持。14checker normal/-Oのreceipt/stdout一致・stderr空、親readonly replay exit0・541payload inventory一致。初回失敗421payloadは別保持。preparedのsession/owner、4型shadow一括公開、typed child handle、厳密cache取得とGC/leaseのCPU契約を受入。既存4memberのassert契約はDebugで検証しRelease件数だけを根拠にしない。clone累積O(K²)、RSS/時間/汎用Register例外安全/任意並行破棄/async/製品loop/M9/StageB/GPUは対象外。

## G2-GR83-FINITE-SUBMIT-SAFETY: 非同期ロードの有限ジョブ投入を例外時にも計上整合させる
- status: done
- done-when: submission専用の未計上ticketでOnCompleteを先に登録し、enqueue成功後に同じadmission gate内でarmする。同期完了・重複submit・遅い失敗handlerで別仕事のcountを減らさず、登録/queueの確保例外でDrainを残さない。公開bool、closed Cancel、persistent、世代と既存schedulerを維持する。
- verify: simple/global fallback/localの境界例外と再試行、他task保持中の遅いCancel、同Taskの成功/失敗重複・terminal同期・arm前後、Stop/Drain fenceと再初期化世代、weak寿命、既存JobSystemShutdownTestをDebug/Releaseで実行。ModelAsyncLoadQueue/ModelResourcesAssetRuntime/AssetRuntimeSnapshotReloadの関連回帰を含める。境界注入と実allocator OOMの全域検証を区別する。
- stop-when: push後にallocating/throwing callbackを置く、例外cleanupで以前受理した同Taskを取消す、一般worker/handler例外・Shutdown/Initialize全体OOM・scheduler再設計を含める、persistentへ逃げる、GR83 async/製品loop/GPUまで完了とする。

- result: code6a2d9815/tree4e4ec3c5/run37483939116 attempt1 job112339002242。Release61/旧22＋finite marker、Debugのfinite常時検査とmodel非同期3束を確認。旧60cppとloader/publication/Registry/Profile、4固定6095byte capture・旧89出力・25+15CLI・12metadata・17/29child退出・GR84別58process/125file・PE-CNG/SHELL32を維持。16checker normal/-Oのreceipt/stdout一致・stderr空、親readonly replay exit0・629payload一致。7paths/Library2。未arm ticketの準備例外・同期/重複・close/fence・旧世代の有限計上を受入。境界注入は実allocator全OOMの証明ではなく、一般worker/handler例外、全handler復帰、consumer配送、製品asyncは未受入。関連modelのassert契約はDebug限定で実証する。

## G2-GR83-SKELETAL-EVENT-RUNTIME: 骨格の非同期読込から一括公開とdelegate配送まで接続する
- status: done
- done-when: explicit ownerと借用JobSystem/RegistryでBindするinstanceから、実finite TaskのP1所有CPU解析、事前確保intrusive ready、owner P2a一括公開、遅延delegateを一つにつなぐ。同snapshot/正規pathのgroup合流とRegistry session/domain/generation cacheを持ち、永続strong cacheを作らない。
- verify: 実simple/work-stealing＋3clip fixture、所有/typed failureとcache/GC、wrong owner、有限Flushと再入追加の次Flush配送、submit拒否/例外/earlyterminal、Cancel/Closeの準備/worker/ready/assembly/precommit/通知各窓、TaskWaitとhandoff ackの差、callback例外/DrainDeferred、session/domain/snapshot/上限/weak解放。新試験はDebug/Releaseの常時検査、旧61と関連Debug/CLI/固定captureを維持する。
- stop-when: worker/eventでResource/consumerを扱う、全Pending polling、State lock下のSubmit/Cancel/consumer、handoffやCloseに新確保必須、precommitとの取消競合窓、TaskWaitだけでDrain完了、自己callback内同期破棄、架空ResourceId/token推測、製品loop/M9/StageB/GPUまで完成とする。

- known-limit: 既存AssetManifestのJSON文字列はASCII限定。新runtimeのUTF-8構文検証は非ASCII論理pathのcooked成功を意味しない。現在の正常fixtureはASCIIで、合法UTF-8未登録pathはtyped拒否として検証する。manifest UTF-8化は別変更。

