# 関節対応のschema非依存な所有解決

GR84/GR85でrole展開後の具体的なsource/target名pairを扱うprivate基盤。Roadmap:4228/4296の指示どおり外部BoneMapはrole経由とするが、そのJSON/profile語彙をこの単位では定義しない。Requirementsの旧direct-pair v1案を既成の外部形式にせず、role要件自体をv2へ先送りもしない。既存の外部BoneMap reader/assetはまだ存在しない。

## 入力と解決

ResolveSkeletalJointMappingsはsource/targetの既存strict SkeletalJointIndex、UTF8名の借用pair列、明示root番号pair、必須source再利用policy、limits、outを受ける。名前は既存minor2 codecの厳密UTF8/NUL/非空規則とbyte完全一致で解決し、大小文字・Unicode正規化・namespace削除をしない。BVH token制約をtarget名へ持ち込まない。

- policyはRejectまたはAllowを必須指定。0のUnspecifiedと未知値は拒否
- targetは常に一意。同じpairの反復もDuplicateTarget
- source再利用はpolicyに従う。Allowでもcatalog自体の同名重複はstrict indexが拒否する
- root番号は既定UINT32_MAX。callerの明示pairがちょうど1回必要。root targetを別sourceへ割り当てるとRootConflict、pairがなければRootMissing
- この層は親配列を受けず、指定rootが階層上のrootかどうか、階層/rest/TopologyIdの互換性を証明しない。callerの後続入力検証の責務
- 狼/シ者の骨格共有やゲーム固有の必須関節を推測しない。必須role setは後続profileで定義する。全target対応を要求しない

既定limitsはcatalog片側1024関節、mapping1024件、入力名の総byte1MiB。これらはcook wireの128/256上限とは別。source/targetごとの個別名上限は構築済みindexが保持する値を使う。外側spanのstorage、count/max_size、root範囲、合計byteのchecked budgetを確保前に検査し、個別名のlookup失敗は元のIndexResultを残す。

## 所有と失敗

結果はSourceJointCount/TargetJointCount、入力順の{SourceIndex,TargetIndex,InputEntryIndex}、RootMappingIndex、元joint配列番号順のUnmappedSourceIndices/UnmappedTargetIndicesを所有する。文字列・index・入力spanの寿命には依存しない。catalogの名前/順序が変わったら再解決が必要で、結果は互換性IDや永続cacheではない。

成功直前のnothrow moveだけでoutを置換する。失敗/確保例外では既存outを保持。copyは独立所有、move元はdefault相当の空へ戻す。自己copy/moveは値を保持する。

エラーはStatus、Source/Target側、入力entry/競合元entry、元joint番号、lookup時のIndexResultを返す。構造/policy/catalog/limit/root前提を先に検査し、各entryはsource名→target名→target重複→source再利用→root矛盾の順に検査する。未測定のretarget誤差を0として返すことはない。

Unmapped sourceは後続でreportし、unmapped targetは後続retargetでbind localを維持してchannelを出さないための一覧。このresolver自体は姿勢を変更しない。

## 非対象と検証

JSON、role語彙/chain展開、axes/units、必須role policy、root motion、rest補正、clip生成、CLI、StageB作者時rest snapshotは非対象。次のconsumerがこれらを独立に検証する。

既存Asset bundleに元番号/入力順/Unicode/正規化差、policy両種と未指定、root不在/矛盾/範囲、未発見/不正名/重複、catalog両側/件数/総byte境界、空/不正span、入力/index破棄、copy/move/self代入、後半失敗out保持を追加する。実48CPUと不変のSampler4capture/旧89byte/managed gateを確認する。GCC構文検査と実Core runtime検証は区別し、確保故障注入は未実施。
