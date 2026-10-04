# NVSKEL 0.0 / 0.1 / 0.2 wire契約

## 状態と版の境界

現行のreader/writerは0.0/0.1の契約を維持している。0.2はここで統一する仕様と純検証部品の段階であり、
本書の追加だけではreader/writer/decoderの受理範囲を切り替えない。
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
