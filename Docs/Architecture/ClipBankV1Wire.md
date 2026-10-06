# NVSKEL v1 ClipBank の初期profile

GR82 Stage B1の事前契約。旧0.0/0.1/0.2形式を変更しない。初回は明示v1 APIでClipBankだけを生成し、三資産の論理パス・公開・async・共有は後続へ残す。

## 作者restと束縛

提供された作成元rigのnode local TRSを、アニメ適用前の作者snapshotとして保存する。IBM由来のbindやclipのt=0、別の現在rigから復元しない。古いactionを新restで再exportした入力から失われた履歴を復元できるとは扱わない。

名前は厳密UTF-8、非空、NULなし、完全一致。大小文字やUnicode正規化をしない。unsigned byte辞書順に並べた名前と、その順序に写した親番号を正準topologyとする。prefixはASCII `NVSKEL_TOPOLOGY`＋NULの16 bytes、LE u32 schema=1とjointCount、各jointのLE u32 nameByteCount・名前bytes・LE u32 parentIndex（root=-1）を連結する。SkeletonIdはこの列のFNV-1a64。rest/IBM/meshはIDに含めず、束縛時はIDだけでなく正準列全文も比較する。

全CLIPに作者snapshot参照が必須。同じsnapshotの共有は許可するが、現在Skeletonへの参照で代用しない。作者と現在の全jointでlocal translation差[m]、q/-q同値の最短角差[rad]、正scale各軸のabs(log(current/authored))を比較する。既定は1e-5m/1e-4rad/1e-5で、有限・非負の呼出しpolicyとして変更可能。いずれかが閾値を超えたら既定拒否。明示overrideはrest差だけを許可し、topologyや破損入力を免除せず、clipの絶対TRSを補正しない。各snapshot/各jointの差と超過、実際のoverride使用を所有reportで返す。

初回profile=1は直接TRS・正scale・単一root・外部親なし・128以下。正のnonuniform scaleは受ける。joint matrix、反射/特異scale、非joint親、256、clip-onlyは明示未対応。animation Scaleの全keyもXYZ正を要求する。rest/rotation keyは既存Samplerと同じfloat normが有限かつEPSILON超の範囲だけ受け、tiny/huge quaternionをIdentity/zeroへ黙って置換しない。保存値を無断正規化しない。glTF仕様全体を不正と呼ばない。v1の実Samplerは現在rigのrestを未アニメjointの既定に使い、旧APIはIBM/M由来の既定を保つ。

## wireの事前配置

全数値はlittle endian。構造体をmemcpyしない。headerは256 bytes、directory entryは32 bytes、sectionは16-byte aligned。未使用/reserved/paddingは0。初期writerは7 sectionの順序を固定し、未知optional sectionを含むparser入力は完全な範囲/hash/重複検査の後でのみ読み飛ばす。

Header:
- 0: magic8=`NVSKELv1`
- 8: u32 headerSize=256
- 12/14: u16 major=1/minor=0
- 16: u32 endian=0x01020304
- 20: u32 role=3（ClipBank。1 Skeleton/2 SkinnedMeshは未対応拒否）
- 24: u32 flags=0
- 28: u32 sectionCount（7〜16）
- 32: u64 directoryOffset=256
- 40: u64 fileSize
- 48: u64 payloadHash（directoryを含むbytes[256,end)のFNV-1a64）
- 56: u64 SkeletonId
- 64: u32 profile=1
- 68: u32 topologyAlgorithm=1
- 72〜255: reserved=0

Directory entry:
- 0: FourCC
- 4: u32 flags（bit0=required、それ以外0）
- 8: u64 offset
- 16: u64 byteSize
- 24: u32 recordSize
- 28: u32 count

Required sections:
- STRS: recordSize=1。終端NULなしのUTF-8 bytes。各参照はoffset/lengthで境界を検証
- TJNT: 24 bytes。nameOffset u64/nameBytes u32/canonicalParent i32/nameHash u64
- RSET: 48 bytes。firstRest u32/restCount u32/labelOffset u64/labelBytes u32/profile u32=1/restHash u64/resolvedImportScale f64/reserved8
- ARST: 48 bytes。T float3、列規約Q float4、S float3、reserved8。値を量子化/正規化して保存差を隠さない
- CLIP: 48 bytes。nameOffset u64/nameBytes u32/snapshotIndex u32/duration f32/firstChannel u32/channelCount u32/flags u32=0/nameHash u64/reserved8
- CHAN: 48 bytes。nameOffset u64/nameBytes u32/path u32/nameHash u64/interpolation u32/encoding u32=0/firstSample u32/sampleCount u32/reserved8
- SAMP: 32 bytes。time f32/value float4/reserved12。LINEAR/STEPの無圧縮のみ

TJNTは名前順の正準表。RSETのrest範囲は全jointを正準順に持つ。RSET/CLIP/CHANの所有範囲は重複や穴を許さず各表を尽くす。restHashはARSTのT/Q/Sの40 bytes×jointCountだけのFNV（paddingなし）。ラベルは作者入力の識別用で、出自の証明や暗号学的IDではない。CHANはnameHashだけでなく完全名をTJNTへ解決する。壊れたhash、重複joint/path、不正時刻・非有限値・不正snapshotを拒否する。

上限はjoint128/clip256/snapshot256/channel32768/sample1048576/name4096 bytes/STRS1MiB/wire64MiB。呼出し側はより小さく制限できる。section数/recordSize×count/offset+sizeを割当前に検査する。importにも別途source64MiB、全buffer実bytes64MiB、node1024/accessorとview各65536/buffer256/vertex1048576/index3145728/primitive属性128を設け、joint/clip/channelと累積LINEAR・STEP・Bake sampleを確保前に検査する。共有accessorはchannelごとの複製数を数え、data URIの実復号量とGLB借用分を先に予約し、外部fileは同じopened streamの実sizeを累積残量と比較してから確保する。これは全processのRSS/実行時間上限の保証ではない。writer自身の2回一致と、独立literal/oracleを別に確かめる。

## 検証の分離

旧fixed sampler/cook/CLI bytesを再採取しない。新v1のrest既定分岐・topology・拒否/override・姿勢は別oracleで確認する。Game/GPU/DCC/実犬の品質はこのCPU codec/bindingの成功から推測しない。公開Resourceを生成する前に全bindingを完成させ、明示owner上の未登録候補を全成功時だけ公開する。
