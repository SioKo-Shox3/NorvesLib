# Cook依存snapshot（G2-S6 / GR96）

CaptureCookDependencySnapshotは単体cook要求と非0のcooker revisionから、source・選択sidecar・glTFの全外部buffer/imageを読み、値所有のfile metadataとfingerprintを作る。出力cacheの採否、stamp永続化、既存rootの公開はまだ行わない。

## 印の範囲

- sourceの元locatorとcanonical file、全raw byteのsize/hash
- kind/logical/entry/type/format/variant、sidecar auto/required/disabled/override policy
- 選択sidecarのlocator、存在/不在、BOMや空白を含むraw byte
- 既存のcanonical骨格option encoderによる設定とalgorithm版
- glTF/GLB root bytes、全外部buffer（宣言長を超える余剰も含む）と外部imageのraw byte

bSkipIfUnchangedとPackagePath/ManifestPathは印に含めない。採否の制御flagや一時stage pathが入力内容の同一性を変えないためで、出力先・所有権・出力file検証はdecision層で別に必須とする。要求全体のcook可否を判定するAPIでもない。呼出元で有効な単体要求へ検査・正規化して使う。

FNV-1a64を既存packageと同じ非暗号学的な増分用hashとして用い、固定domain、LE整数、長さ付きUTF8を連結する。認証や敵対的改ざんへの暗号学的保証ではない。cooker revisionは挙動変更時に呼出側が更新する。runtime manifestのSourceHashやpackage writerを変更しない。

## 共通読込

LoadedImportSettings/LoadedImportSettingsDocumentは、実際に検査・parseした同じreadのRawSourceBytesを保持する。auto不在だけを既定値とし、required/explicit override不在、permission/read error、無効JSON/設定を拒否する。disabled時は隣接sidecarを読まず、作成や変更でも印は変わらない。

外部URIは既存BufferSet/ImageSourceのpercent復号と相対path検査を通す。ReadBufferFileWithPathが既存のcanonical component境界を共有し、そのreadに使ったpathを成功時だけ返す。標準ReadBufferFileはこの実装へ委譲し、既存の失敗規則を維持する。

Data URIとGLB BIN/imageはrootやbufferに既に含まれ、外部fileとして二重登録しない。全imagesを列挙し、unused画像も依存に含める。画像の完全decode・model意味検査・生成package検査の代用にはしない。

## 失敗と競合

成功時だけsnapshotを置換する。不在と読込失敗を混同せず、途中まで列挙した依存を返さない。これは複数fileのatomic source snapshotではなく、並行編集に対する完全な保護でもない。公開前に再採取して印が変わっていれば中止するのが後続decision/publisherの契約。

既存TrySkipModelCookは未移行。後続の共通Cook/Skip/Error決定を接続する際、batchがCookを決めた後のCookSingleAssetにはbSkipIfUnchanged=falseを渡し、古いcanonical-only cacheを二重判定させない。

## 実Windows検証

f96dc15517d2d03efd2a8e32a2e5ecb86d5665d3の[run37215038245](https://github.com/SioKo-Shox3/NorvesLib/actions/runs/37215038245)で16 CPU契約が成功。実file変更、sidecar空白・不在・exclusive lock、Unicode override、GLB、外部URIを検証した。単体7 CLIの79出力と診断5 literal、およびtexture v1の2 spec各2回・10出力の生byte互換も維持している。

この結果は依存採取の検証であり、保存済み出力の採否や既存rootへの増分公開の完了を意味しない。
