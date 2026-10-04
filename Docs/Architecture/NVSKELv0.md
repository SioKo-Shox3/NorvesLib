# NVSKEL 0.0 / 0.1 / 0.2 wire契約

## 状態と版の境界

readerは旧0.0/0.1を維持して0.2の所有表/複数clip検査を接続済み。writerも統一0.2を生成し、
旧cacheは再生成対象になる。Resource/範囲drawへ接続し、palette共有とWindows/GPU受入れは未完。
0.2はGR32のsubmesh/名前slotとGR82 Stage Aの複数clipを同じ定義へ載せる。別のminor=2を作らない。

| 項目 | 0.0 | 0.1 | 0.2 |
|---|---|---|---|
| major/minor | 0/0 | 0/1 | 0/2 |
| header | 256B | 256B | 320B |
| mesh-node transform | byte192〜255は0、解釈時identity | 64B保存 | 同じ64B保存 |
| clip数 | 1 | 1 | 1以上、clipごとにchannelを所有 |
| submesh/slot | 暗黙の1範囲/slot0 | 同左 | それぞれ1〜8の表 |
| 関節上限 | 128 | 128 | 128 |
| hash | payloadのみ | transform64B→payload | transform64B→extension64B→payload |

全版little-endian、magicは8byteのNVSKELv0、EndianMarker=0x01020304。
entry_type=Skl0、format=nvskel.v0.skinned.pnujiw.u32は維持する。
全vertexは64B、joint80B、clip/channel/sampleは各32B。4影響の頂点ABIは変えない。

0.2の材質slotは名前だけの64B recordである。NVMESH v1/将来NVSKEL v1 MATSで共有する128B PBR材質とは別物。
slot予約48Bを、同じminor=2の途中で3 texture参照へ転用しない。
GR79のPBR情報を骨格へ格納するのはStage BのMATSへの接続で扱う。

## 先頭256B

オフセットと型は `Asset/CookedSkeletalWireFormat.h` のCookedSkeletalFormatV0を正とし、従来の配置を維持する。
主要な領域:

- 0〜39: magic/header size/version/endian/各record size
- 40: FileSize u64
- 48〜159: vertex/index/joint/clip/channel/sample/stringのOffset u64 + Size u64
- 160: PayloadHash u64
- 168〜191: vertex/index/joint/clip/channel/sampleのCount u32
- 192〜255: 0.0では0予約、0.1以降はmesh-node transform f32[16]

transformの保存順は従来のまま（平行移動成分は12/13/14）。この版追加で座標変換や巻き順を変更しない。
0.0/0.1のheader長・hash範囲・clipCount==1を、0.2の条件で全面的に置換しない。

## 0.2拡張header: 256〜319

| Offset | 型 | 内容 |
|---|---|---|
| 256 | u32 | SubmeshRecordSize=64 |
| 260 | u32 | MaterialSlotRecordSize=64 |
| 264 / 272 | u64 / u64 | SubmeshOffset / Size |
| 280 / 288 | u64 / u64 | MaterialSlotOffset / Size |
| 296 / 300 | u32 / u32 | SubmeshCount / MaterialSlotCount |
| 304 | u64 | ExtraVertexOffset=0 |
| 312 / 316 | u32 / u32 | ExtraVertexRecordSize=0 / ExtraVertexFlags=0 |

ExtraVertexは現profileで未対応。非0を黙って無視せず拒否する。将来の拡張時は明示した版/profileの移行が必要。
clip/channel/sampleのoffset/countは先頭256Bに既にあるため、複数clip専用の別headerを設けない。

## 節の配置

0.0/0.1: header → vertex → index → joint → clip → channel → sample → string

0.2: header → vertex → index → joint → clip → channel → sample → submesh → slot → string

- 各節の開始は16B整列。前節末尾を16Bへ切り上げた場所へ次節を置き、任意の穴や重なりを認めない
- record節のSizeはCount×RecordSizeに一致。indexはCount×4
- 最後のstring末尾はFileSizeに一致。stringは空でもよい
- 整列paddingは0で、hashへ含める。string参照のrange/版別の文字列制約とrecordの予約領域も別途検証する
- index総数は非0・3の倍数、すべてのrecord countはu32内。各clip/channelが非空なのでClips<=Channels<=Samples
- 節の乗算/加算/整列はoverflowを先に検査する。外部fileの宣言値を先にポインタへ変換しない

## submesh record: 64B

| Offset | 型 | 内容 |
|---|---|---|
| 0 / 4 | u32 / u32 | IndexOffset / IndexCount |
| 8 / 12 | u32 / u32 | VertexOffset=0 / VertexCount（0は未指定） |
| 16 / 20 | u32 / u32 | MaterialIndex / Flags |
| 24 / 36 | f32[3] / f32 | BoundsCenter / BoundsRadius |
| 40〜63 | u64[3] | Reserved=0 |

IndexOffset/Countはindex要素単位。全submeshは非空三角形範囲で[0,IndexCount)を昇順・隙間/重複なしで覆う。
indexは連結済み頂点の絶対番号であり、VertexOffset/baseVertexを再び加えない。
MaterialIndexはslot表内。Flagsはbit0=NoShadowのみ定義し、未知bitを拒否する。
Boundsは有限、半径非負。recordの検証・保持・描画接続を完了するまで、新recordの受理を有効化しない。

## material slot record: 64B

- 0: NameOffset u64（string節内のbyte offset）
- 8: NameLength u32（終端NULを含まない）
- 12: NameReserved u32=0
- 16〜63: Reserved 48B=0

空名も表現できる。0.0/0.1の既存名はprintable ASCII（0x20〜0x7e）制約を維持する。
0.2の名前は妥当なUTF-8で保存し、埋込NULを拒否する。長さは文字数でなくbyte数。
0.2 writer/readerの公開前に、この版別の文字列encode/decode/範囲検査を接続する。
名前の生成/衝突解決はglTFからslotを割り当てる段階で別途固定する。
名前だけを持つ表に、PBR係数やtexture参照を黙って詰めない。

## clip/channel/sampleの所有契約

0.2は複数clipを扱える定義とし、GR32の生成側がまだ1clipだけの期間もwireを変更しない。

- clipは非空のchannel連続範囲を一意に所有し、clip表順に全channelを隙間/重複なく分割する
- channelは非空のsample範囲を一意に所有し、channel表順に全sampleを隙間/重複なく分割する。他clip/channelのsampleへ混入しない
- 同じjoint/pathは別clipなら使用可能、同clip内では重複不可
- 時刻の有限性/非負/単調増加と補間型を検査し、durationはそのclipだけの最大sample時刻と照合する
- clip名の空/重複と名前選択の扱いは、Stage Aのdecode/resource契約へ揃える
- 古い0.0/0.1は複数clipを引き続き拒否する

## 純検証部品と接続順

`CookedSkeletalWireFormat.h` は従来の定数/hashを内容不変で分離し、0.2の追加定数/hashを定義する。
`CookedSkeletalWireValidation` は版別header、count、packed節、拡張header、raw hashの入力長を検査する。

これは完全なNVSKEL parserではない。magicの実byte、padding、transform/recordの値、文字列、
submeshのpacked index範囲、clip/channel/sampleの所有は、reader接続時に合わせて検査する。
低レベルhash関数は有効な64B transform/64B extensionとpayload範囲を前提とし、単体では入力長を検証しない。

接続は、schema/純部品 → Nprimitive decode → 0.2 reader/writerとcache移行 → Resource/描画 → Stage Aの複数clip生成・選択の順に進める。
0.2 readerを初めて公開する時点で複数clipの所有契約まで検証し、後から同minorの意味を緩めない。
未接続の下流が表を捨てて成功する状態は作らず、段階移行中は明示拒否する。
writer切替後のcache判定はwriter minorを確認し、通常loadで許される旧0.0/0.1を最新cook済みとして残さない。

## 範囲外

v1/256関節、128B PBR材質の骨格MATS、作成時rest snapshot、イベント/マーカー/ソケットはStage B以降。
S1で承認されたrest安全契約を不要にしたり、SkeletonId一致だけで互換としたりする仕様ではない。

## 名前codecの境界

CookedSkeletalNameCodecはminor0/1のprintable ASCIIとminor2のUTF-8を区別する。
UTF-8は[RFC 3629](https://www.rfc-editor.org/rfc/rfc3629)のscalar範囲/最短形を検査し、
孤立continuation、途中切れ、overlong、surrogate、U+10FFFF超過を拒否する。資産名の追加条件として埋込NULも拒否する。
置換文字への自動置換、大小文字変換、Unicode正規化、BOM除去は行わない。空名は表現できる。

- 1byte CharはUTF-8 byte列、2byteはUTF-16 code unit、4byteはUnicode scalar列として扱う
- MeasureのByteCountは保存byte数。CodeUnitCountはencode元またはdecode先のChar単位数であり、名前の文字数ではない
- ResolveSkeletalWireNameはstring節内のoffset/lengthを差分で検査し、成功時だけ借用viewを返す。lengthはu32内
- encode/decodeは全入力と容量を先行検査し、失敗時はout全体を保持する。終端NULは書かない
- 入力とout全領域（未使用末尾を含む）の重複、pointer/サイズのoverflow、不正alignmentを拒否する
- 借用viewは元stringTableの寿命に従う。loaderは必要なCore Stringへ変換して所有する

純試験ではUTF-8/UTF-16/UTF-32とホストwchar_tを照合し、NULとsurrogateを除く全1,112,063 scalarを往復する。
保存byte列4,382,591BのFNV値をPython標準UTF-8で独立生成した固定値と照合する。
このcodec追加だけでは実reader/writerを切り替えず、Windows/Core全体やWindows TCHAR経路の実行済みを意味しない。

## 0.2 readerの接続状態

ParseCookedSkeletalは旧0.0/0.1と統一0.2を版別に読み、VersionMinorを結果へ保持する。
0.2のsubmeshは範囲/slotに加えてVertexCount、NoShadow、boundsも所有dataへ保持する。
VertexOffsetは0必須で、VertexCountが非0の場合は各絶対indexが[0,VertexCount)内であることも検査する。
0.2だけjoint/clip/channel/sampleの予約byteも0必須とし、旧版の受理条件を変えない。
clip名/slot名の空や重複はwireでは表現できる。名前選択・一意名生成はStage A/authoring側の責務とし、readerで勝手に改名しない。

0.2の複数clipはchannel/sampleを表順に一意所有し、joint/path重複とdurationをclip単位で検査する。
エラー時は部分的なSkeletalやSourceBlobを公開しない。
M9は表をResourceへ渡して範囲drawへ接続する。複数clipの選択は未対応なので明示拒否する。
範囲drawのコード接続後もpalette共有/GPU受入れは未完で、reader/writerの受理だけを描画検証済みと扱わない。

検証は純wire/submesh recordの通常・最適化・sanitizer試験と、手書き0.2のnative回帰登録を分ける。
native回帰には2clip/2submesh/UTF-8、旧golden、範囲/予約/padding/hash/clip跨ぎと失敗出力を含めるが、
このLinux環境ではWindows.h依存により実readerを含むCore全体の試験は未実行。

## 0.2 writerとcache移行

raw decoderは1primitiveでも明示submesh/slotを作り、material名を保持する。
writerは両表空の旧所有dataを全index/slot0（Default名）へ具体化する。片側空は拒否する。
320B/64B表/UTF-8を生成し、padding/予約byteを0にしてtransform→extension→payloadでhashを計算する。
再parse後に表数量・index/slot/flags/頂点範囲・joint/clip/slot名を照合し、外側のcook結果は全成功時だけ置き換える。

submesh boundsは最終scale適用済みの参照頂点から計算する。floatに丸めたAABB中心から最大距離を取り、
正の半径はfloatの1ULP外側へ丸める。非有限/範囲外index/float半径overflowは拒否する。
これは静止頂点位置のboundsであり、animation全poseを包む保証や、そのままskinning cullingへ使える保証ではない。

cacheは通常parse成功に加えてminor=2を要求する。source hashやmanifestのcooked_version=0が一致していても、
minor0/1 payloadを最新出力としてskipしない。通常loadでは旧版を引き続き読める。
GR82 Stage AのDecodeRigGltf/cookは複数clipをこの0.2へ保存する。旧DecodeSkeletalGltf/Analyzerはraw TwoClipsの拒否を維持する。

純bounds/layoutの通常・最適化・sanitizerと名前/wire回帰を実行する。
raw/GLB→cook→parseの表/Unicode保持・失敗出力保持、旧0.0/0.1 cache missと0.2 hitはnative回帰へ登録する。
Windows依存のwriter/loader/cache/CLI統合は未実行で、GPU描画受入れも未完。

名前をJSONの\u escapeで渡す経路では、high/low surrogateを一つのscalarへ結合する。
孤立surrogate・途中切れ・不正な組合せはJsonDocumentで拒否し、有効な非BMP文字は4byte UTF-8になる。
この修正は共通JSON parserにも適用する。純試験は全scalarのescape読取・UTF-8黄金hashとUTF-16/32出力を照合し、
JsonDocument本体とAssetCookでのescape名の往復はnative回帰へ登録する（この環境では未実行）。

## manifestの表数量metadata

新しい骨格cookはmetadataにsubmesh_count/material_slot_countを保存する。数量は生成payloadを再parseした所有表から取得する。
2項目は組で任意とし、存在する場合は1〜8・三角形index総数との整合を検査する。
旧manifestの省略はbHasSubmeshCounts=false/数量0の「未知」として保持し、1件と推測して表示しない。
他資産の追記mergeでも、既知の数量は保持し、旧省略は省略のまま残す。
source hashや通常loadの互換は変更しない。既存0.2 cacheのmanifestに数量がない場合も、未知として扱い勝手に補完しない。
純数量条件と既存layoutを検査し、manifest解析/1・2・8primitive cook/CLI/merge回帰を登録する。
Windows依存のnative/CLI/CMake/PowerShell実行はこの環境では未実行。
