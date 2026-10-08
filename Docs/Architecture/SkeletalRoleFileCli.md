# BVH / Role Profile 単体CLI

GR84の明示ロール設定を実AssetCook.exeへ接続する。旧GLB単体cook、texture asset-set、inspectの引数解析とhashは維持する。

## 使い方

```text
AssetCook --input rig.glb --out walk.nvpkg --manifest walk.json --logical Actors/Animal --kind model --entry animal.nvskel --entry-type Skl0 --format nvskel.v0.skinned.pnujiw.u32 --variant default --bvh walk.bvh --role-profile animal.roles.json --clip-operation add --clip-name Walk
```

新4引数はすべて必須。既存9つの単体指定も明示する。値付き引数は`--name value` / `--name=value`を受ける。重複、空値、未知引数、他modeとの混在、`--skip-if-unchanged`を拒否する。Addは入力glTFに同名clipがない場合の追加、Replaceは入力内の一意な同名clipの置換であり、既存出力packageの編集ではない。animation欠落・空配列のrigにもAddできる。

Profileの語彙・必須root・明示C・軸/手系・位置規約・時間は[SkeletalRoleProfile.md](SkeletalRoleProfile.md)に従う。CLIでfpsやCを重複指定しない。既存のskin/cubic/morph引数、import-settings/no-sidecar/require-sidecarを使える。no-sidecarとoverride/requiredの併用は拒否する。

Windowsではこのmodeだけ元のwide command lineを取得し、厳密UTF16→UTF8→native pathへ変換する。source/BVH/Profile/明示sidecarのUnicode・非BMP locatorを扱い、旧narrow argvをUTF8と推測しない。出力path/論理名には共通単体出力guardのASCII・local drive制約が残る。

## 観測・hash・公開

SingleAssetCookRequestは追加locator/name/operation/limitsを値所有する。Profileは既定1MiB、BVHは既定64MiBの長さをread前に検査する。これらは2追加fileの上限であり、旧glTF readerの全入力や工程全体のRSS上限ではない。

共通dependency snapshotへBVH/Profileのroleを末尾追加し、root・選択sidecar（不在予約を含む）・全外部buffer/image・新2fileを採取する。新modeのsettings/limitsはtag付きで印へ追加し、旧SchemaVersion 1の符号化と旧要求のhash算術を変えない。inventoryのSourceHashは型付きRole Profile cookと同じ共通helperで計算する。

新modeは常時Cook。共通DecideCookCacheへallowSkip=trueを渡してもForcedでCookする。旧glTF-only stampを誤採用せず、新しいpersistent stampも保存しない。

adapterへ渡す同一readのroot/BVH/Profile bytesは、開始snapshotのrole別size/raw hashとcook前に照合する。package、manifest、有限JSON reportをメモリで構築・検証した後、最初のwrite直前とmanifest write直前に入力・inventory・alias guardを再観測する。case/canonical/hardlink/reparse/prefix保護は全依存を含む既存共通guardを使う。既存manifestは同じ単体identityの1行に限り、SourceHashの更新を許す。壊れたmanifest、別key、manifestのない既存packageを勝手に採用しない。

write後は実package/manifest/AssetSystem検証と共通CookOutputRecordの採取を行う。結果とreportは成功時だけ呼出元へ移す。既存出力への最初のwriteより前の拒否は出力を変更しない。

これは安定filesystemでの変更検出であり、複数入力の原子的snapshot、同時writer、敵対的ABA、package+manifestの2file transaction、電源断復旧を保証しない。write開始後のI/O失敗・変更では部分出力が残り得るためErrorとし、成功reportを出さない。managed controllerはtexture専用のままであり、このCLIをmanaged model対応とは扱わない。

## 診断と検証範囲

成功はexit 0、stdoutの`AssetCook cooked role-profile`とstderrの`role_profile_report=`に続くversion 1 JSON。失敗はexit 1と`AssetCook error:`、成功reportはなし。reportはraw Profile hash/版、正準role/ordinal、source-only role、選択clip、sample/time/丸め、回転誤差、半球反転、root位置計算不能、無視した非root位置を含む。全sampleの格納keyを検証し、連続補間全域・root motion生成を完了と表示しない。

専用実CLI smokeはWindows CreateProcessWを通るUnicode入力（GLBとASCII外部URIを持つglTF）、GetACPの記録、Add/Replace、引数拒否、依存file/壊れた設定、成功reportのJSONとUTF8、元出力保持を検証する。新出力inventoryは旧25 CLI・79 standalone出力・10 texture出力の固定比較と分離する。CPU fixtureは型付きhash一致、Forced、要求copy、依存変更/削除・alias・公開直前probe、実Resource/Samplerを検証する。

実Windows CIは候補commitごとに別途確認する。実Blender資産、GPU画像品質、auto C、root motion、resampling、Stage Bはこの境界の受入れ対象外。
