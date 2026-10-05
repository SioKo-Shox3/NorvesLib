# Cook ownerと実fileの対応付け

G2-S6 / GR96。ResolveCookOwnerBindingは読み取り専用であり、spec内容の解析、source読込、root作成、state採用、stage公開は行わない。

## 3つの表現

CookResolvedOwnerBindingは次を分ける。

- SpecLocator / FinalRuntimeRootLocator: raw検査を通ったcaller側のnative I/O locator
- Identity: handleから取得したvolume-GUID canonical名のUTF8表現と相対manifest。ComputeCookOwnerIdの入力
- ExpectedBinding: OwnerId、ASCII drive-formのlexical RuntimeRootIdentity、相対manifest。現schema 1の照合用

GUID canonical名をI/Oへ戻したり、既存のRuntimeRootIdentityへ代入したりしない。元のsaved stateをresolverへ渡さず、ownerはcallerの指定と観測から独立に導出する。成功時だけoutを一括置換し、失敗時は保持する。errorとrequest/out内の文字列のaliasは診断文字列も変更せず拒否する。

## 受理するprofile

- local-drive絶対locatorのみ。UNC/device/volume-GUID入力、relative、drive root自体、危険raw componentは拒否
- specは既存regular file。日本語/非BMPを含むnative locatorに対応
- FINAL rootのcaller locatorはASCII。既存non-reparse directory、または既存の直親directory下の不在leafだけ
- native locatorの32767 code unit/256 component制限、owner identityの4096 UTF8 byte制限を引き継ぐ
- specとrootが同じvolumeである必要はない。stateやstageの同volume要件は後段の責務
- reparse、type不一致、identity確認不能を拒否。内容を読む権利や排他所有を取得するものではない

FILE_READ_ATTRIBUTESの観測は、別handleがshareなしで内容を開いていても成功する場合がある。resolverの成功をcontent-read許可や公開可能の証拠にしてはならない。参照: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew

CookPathIdentityのprivate coreを共有し、既存のfile endpoint観測はdirectory leafを引き続き拒否する。新しいdirectory入口だけがdirectoryを受け入れ、不在時にimmediate parentを要求する。

## 不在rootは仮観測

不在rootはexisting parentのcanonical名へexact leafを足した候補であり、未使用の名前だったことは証明しない。作成・renameによってnormalized名が変化する場合があるため、後段は作成後に再解決し、元のcanonical identityと完全一致することを確かめる。相違時に新ownerや保存ownerへ黙って切り替えない。

NTFS/FATのfile-name tunnelingを含め、任意のfilesystem・作成手段でbefore/afterが必ず一致するという保証はしない。参照: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltgettunneledname

## aliasと移設

case-foldやUnicode正規化をhash前に行わない。既存8.3/SUBSTはGUID final nameを取得できた場合だけ受理し、未知の表記へfallbackしない。別hardlink名は別spec identity。同じ名前へのspec置換ではfile IDをhashしないためownerを維持する。

同じ物理rootのaliasでOwnerIdが一致しても、lexical RuntimeRootIdentityが違う旧stateはloadを拒否する。安全側の互換制限であり、自動移行は行わない。SourceRootはresolverの入力に含めず、解決済みsource locatorの変更は共通CookDependencySnapshotが検出する。

## 検証と後段

CookOwnerResolverTestは通常の新規leafの作成前後照合、spec同名置換、Unicode、8.3、独立state保存/読戻し、型・raw名・上限・失敗保持を扱う。SUBST、ASCII alias経由のUnicode物理親、case-sensitive directoryはrunner capabilityに依存する。receiptの各flagが0なら未実行であり合格実証として数えない。shareなしspecの属性観測結果は別flagで示す。

lock、journal、state配置、既存root採用、production公開は未接続。この観測はatomic namespace snapshotでも認証でもなく、協調writerのlockと公開前の再照合を置き換えない。
