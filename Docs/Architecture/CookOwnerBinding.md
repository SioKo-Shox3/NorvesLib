# Cook owner識別子の値導出

G2-S6 / GR96。ComputeCookOwnerIdは、callerが保存stateとは独立に確定した3つのidentityから安定した識別子を導出する。資産fileの読込、alias解決、rootの作成、所有権の取得、公開は行わない。

## 入力とwire

CookOwnerIdentityは次をUTF8の値として所有する。

1. CanonicalSpecLocator
2. CanonicalFinalRuntimeRootIdentity
3. ManifestName（安全でcanonicalなASCII相対名）

各fieldは非空・NULなし・正しいUTF8・4096byte以下。前2つはcanonical identityをcallerが確定済みであることを前提とする。値層は物理パスの存在・canonical性を証明しない。casefold、slash変換、Unicode正規化も加えない。errorと入力・出力、outと入力のaliasは認めない。errorのaliasでは値を保護するため診断文字列も更新せずfalseを返す。それ以外の失敗もoutを保持する。

hashへ渡す順序は以下で固定する。

- u32 little-endian: schema version = 1
- producer: NorvesLib.AssetCook
- CanonicalSpecLocator
- CanonicalFinalRuntimeRootIdentity
- ManifestName

各文字列はu32 little-endianのUTF8 byte数と、そのbyte列を連続させる。終端NUL、native struct、wchar_tの生byte、locale依存文字列は含めない。SHA-256 digestの先頭16byteをdigest順の32文字lowercase hexへ変換し、全zeroは予約値として拒否する。

Windows 10 / Server 2016以降のCNG BCryptHashとBCRYPT_SHA256_ALG_HANDLEを使う。AssetCookLibがBcrypt.libへリンクする。私製SHA実装や鍵・乱数・永続credentialを追加しない。非Windows実装は明示的に未対応を返す。

公式仕様: https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcrypthash

## 安定性と意味

spec内容・row順・timestamp・cooker revision・random stage名・generationはtupleへ含めない。SourceRootも含めない。同じspecとdestinationの入力選択として扱い、解決済みsource locatorと要求意味を含む既存CookDependencySnapshotによって再cookを判定する。

spec identity、FINAL root identity、manifestが変われば別ownerとなる。入力内容が変わっただけで旧stateを別ownerとして扱わない。識別子は認証・改ざん検出・既存rootの採用権限ではない。同じtupleなら誰でも同じIDを作れる。保存済みownerを期待値として逆採用しない。

## resolverとの境界

filesystemを観測する完全なbinding resolverは未接続。spec/rootの物理canonical identity、case-sensitive directory、不在rootから作成後へのidentityの維持を別途検証する必要がある。現CookOwnedStateはASCII drive-formのRuntimeRootIdentityを保存し、CookStateFileはcallerのRuntimeRootとその表記を照合する。物理volume-GUID identityをこのfieldへそのまま代入してはならない。

stateの配置、destination lock、journal、既存rootの更新、rootの移設はこの値層の成功では許可されない。

## 検証

CookOwnerBindingTestはPython hashlibで独立算出した固定vector、曖昧なfield連結、caseとUnicode正規形の区別、日本語/非BMP、各fieldの上限、不正UTF8/NUL、不正manifest、失敗保持、codecの独立expected binding照合を検査する。実Windows結果と既存79+10byte互換はPROGRESS.mdへ記録する。
