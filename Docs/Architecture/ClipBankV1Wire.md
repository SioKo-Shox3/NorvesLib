# NVSKEL v1 ClipBank の初期profile

GR82 Stage B1の事前契約。旧0.0/0.1/0.2形式を変更しない。初回は明示v1 APIでClipBankだけを生成し、三資産の論理パス・公開・async・共有は後続へ残す。

## 作者restと束縛

提供された作成元rigのnode local TRSを、アニメ適用前の作者snapshotとして保存する。IBM由来のbindやclipのt=0、別の現在rigから復元しない。古いactionを新restで再exportした入力から失われた履歴を復元できるとは扱わない。

名前は厳密UTF-8、非空、NULなし、完全一致。大小文字やUnicode正規化をしない。unsigned byte辞書順に並べた名前と、その順序に写した親番号を正準topologyとする。prefixはASCII `NVSKEL_TOPOLOGY`＋NULの16 bytes、LE u32 schema=1とjointCount、各jointのLE u32 nameByteCount・名前bytes・LE u32 parentIndex（root=-1）を連結する。SkeletonIdはこの列のFNV-1a64。rest/IBM/meshはIDに含めず、束縛時はIDだけでなく正準列全文も比較する。

全CLIPに作者snapshot参照が必須。同じsnapshotの共有は許可するが、現在Skeletonへの参照で代用しない。作者と現在の全jointでlocal translation差[m]、q/-q同値の最短角差[rad]、正scale各軸のabs(log(current/authored))を比較する。既定は1e-5m/1e-4rad/1e-5で、有限・非負の呼出しpolicyとして変更可能。いずれかが閾値を超えたら既定拒否。明示overrideはrest差だけを許可し、topologyや破損入力を免除せず、clipの絶対TRSを補正しない。各snapshot/各jointの差と超過、実際のoverride使用を所有reportで返す。

初回profile=1は直接TRS・正scale・単一root・外部親なし・128以下。正のnonuniform scaleは受ける。joint matrix、反射/特異scale、非joint親、256、clip-onlyは明示未対応。animation Scaleの全keyもXYZ正を要求する。rest/rotation keyは既存Samplerと同じfloat normが有限かつEPSILON超の範囲だけ受け、tiny/huge quaternionをIdentity/zeroへ黙って置換しない。保存値を無断正規化しない。glTF仕様全体を不正と呼ばない。v1の実Samplerは現在rigのrestを未アニメjointの既定に使い、旧APIはIBM/M由来の既定を保つ。

## wireの事前配置

全数値はlittle endian。構造体をmemcpyしない。headerは256 bytes、directory entryは32 bytes、sectionは16-byte aligned。未使用/reserved/paddingは0。初期writerは基本7 sectionの順序を固定し、minor 1ではCMPRを追加する。未知optional sectionを含むparser入力は完全な範囲/hash/重複検査の後でのみ読み飛ばす。

Header:
- 0: magic8=`NVSKELv1`
- 8: u32 headerSize=256
- 12/14: u16 major=1/minor=0または1（CMPRありは1）
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
- CHAN: 48 bytes。nameOffset u64/nameBytes u32/path u32/nameHash u64/interpolation u32/encoding u32（minor 0は0、minor 1は後述）/firstSample u32/sampleCount u32/reserved8
- SAMP: 32 bytes。time f32/value float4/reserved12。LINEAR/STEPの無圧縮のみ

TJNTは名前順の正準表。RSETのrest範囲は全jointを正準順に持つ。RSET/CLIP/CHANの所有範囲は重複や穴を許さず各表を尽くす（minor 1でCMPRに保存するCHANはSAMPの範囲を所有しない）。restHashはARSTのT/Q/Sの40 bytes×jointCountだけのFNV（paddingなし）。ラベルは作者入力の識別用で、出自の証明や暗号学的IDではない。CHANはnameHashだけでなく完全名をTJNTへ解決する。壊れたhash、重複joint/path、不正時刻・非有限値・不正snapshotを拒否する。

上限はjoint128/clip256/snapshot256/channel32768/sample1048576/name4096 bytes/STRS1MiB/wire64MiB。呼出し側はより小さく制限できる。section数/recordSize×count/offset+sizeを割当前に検査する。importにも別途source64MiB、全buffer実bytes64MiB、node1024/accessorとview各65536/buffer256/vertex1048576/index3145728/primitive属性128を設け、joint/clip/channelと累積LINEAR・STEP・Bake sampleを確保前に検査する。共有accessorはchannelごとの複製数を数え、data URIの実復号量とGLB借用分を先に予約し、外部fileは同じopened streamの実sizeを累積残量と比較してから確保する。これは全processのRSS/実行時間上限の保証ではない。writer自身の2回一致と、独立literal/oracleを別に確かめる。

## 量子化（CMPR、minor 1）

`WriteClipBankV1` は `ClipBankV1WriteOptions::bQuantize=true` のときだけCMPRを出し、minorを1にする。既定のfalseではCMPRを出さず、同じ入力に対して従来どおりminor 0のbytesを書く。AssetCookのrig-splitとリターゲットのcookには、現在 `--clip-quantize` がなく、この選択肢は接続されていない。minorを上げるのは、encoding≠0のCHANをminor 0の読み手が黙って読まないため。読み手はminor 0/1を受け、minor 0でCMPRまたはencoding≠0がある場合、minor 1でCMPRがない場合、minor 2以上を拒否する。

CMPR directoryはflags=1（required）、recordSize=4、count=byteSize/4。section先頭はversion u32=1、channelCount u32、dataHash u64の16 bytesで、続くCHAN順の32-byte entryを各チャンネルに1個持つ。channelCountはCHANのcountと一致する。dataHashは `[16 + 32*channelCount, byteSize)` のデータ域全体（末尾を含む詰め物も含む）のFNV-1a64で、16-byte headerとentry列は含まない。fileのpayloadHashはdirectoryと全sectionを含む。

各entryのオフセットは、0: timeForm u32、4: count u32、8: start f32、12: step f32、16: dataOffset u32、20: dataSize u32、24: reserved8=0。dataOffsetはCMPR先頭からのbyte位置。データ域のブロックはCHAN順に並び、各ブロックの開始とsection末尾を4-byte境界に揃え、隙間は0で埋める。ExplicitかつF32のチャンネルはSAMPに保存し、CMPRのdataSizeは0で、dataOffsetはその時点の境界を指す。それ以外はCMPRに保存し、対応するSAMPを消費しない。CHAN.firstSampleはそのチャンネルより前にSAMPに書いたキー数であり、CMPR保存のチャンネルでも現在のSAMP位置を示す。CHAN.sampleCountとentry.countは1以上の論理キー数で一致する。

時刻はExplicit（timeForm=0、start=step=0）かUniform（timeForm=1、有限なstart≧0・有限なstep>0・count≧2）。CMPR内のExplicitブロックは先頭にcount個のf32時刻を並べ、その後に値を置く。Uniformは時刻配列を持たず、i番目の時刻を `float(double(start) + double(i) * double(step))` で復元する。ExplicitかつF32の場合は時刻と値の両方をSAMPに置く。

値のencodingと、時刻配列の後に続くデータは次のとおり。nはcount、float4の成分順はX/Y/Z/W。

- F32=0: float4をn個。Explicitの場合はCMPRに値を置かず、SAMPの32-byte recordを使う。
- RotSmallest3_32=1: Rotation専用。各キーはLE u32で、bit 31〜30が省略成分の添字、bit 29〜20・19〜10・9〜0が残る3成分の10-bit符号。
- RotSmallest3_48=2: Rotation専用。各キーはLE 6 bytesで、bit 47〜46が省略成分の添字、bit 45は予約0、bit 44〜30・29〜15・14〜0が残る3成分の15-bit符号。
- TransQuant16=3: Translation専用。min XYZのf32×3、extent XYZのf32×3、続けてキーごとにXYZのLE u16×3。各軸は `float(double(min) + double(extent) * (code / 65535.0))` で復号し、Wは0とする。書き手はextent=0の軸に符号0を使う。
- Const=4: float4を1個だけ置き、全キーに同じ値を使う。どのpathにも使える。

smallest-threeは正規化した四元数の絶対値最大成分を省略し、最大が同値ならX/Y/Z/W順で先の成分を選ぶ。省略成分が負なら全成分の符号を反転する。残る成分は元のX/Y/Z/W順に詰め、b=10または15、M=2^b−1として `v=(2*code/M−1)/sqrt(2)` で復号する。省略成分は正の `sqrt(max(0, 1−sum(v*v)))` とし、全4成分を再正規化する。32 bitの書き手は各成分の量子化位置のfloor/ceilの8組を復号し、元との内積の絶対値が最大の組を選ぶ（同値なら先の組）。48 bitは各成分を最近整数に丸める。

書き手はまず全キーのfloat4がbit単位で同じならConstを選ぶ。それ以外のRotationは、各キーを32 bitで往復した回転角 `2*acos(abs(q・q'))` が1つでも0.1°を超えればチャンネル全体を48 bit、そうでなければ32 bitにする。TranslationはTransQuant16を選び、各軸の全キーの最小値をmin、`float(max−min)` をextentとする。min/extentまたは往復後のXYZが非有限になる場合はF32に戻す。各軸の符号は `round((value−min)/extent*65535)` を0〜65535に収める。ScaleはConst以外F32。時刻はキー2個以上で `step=float((double(last)−double(start))/(count−1))` を求め、有限・正のstepから復元した全時刻が有限、0〜duration内で厳密増加し、それぞれ元時刻から `step*1e-3` 以内ならUniform、それ以外はExplicitにする。したがってUniform化で時刻も変わり得る。

誤差の尺度を分ける。32 bit単体の固定種による単位四元数10万個の試験は、正規化した4次元角 `acos(abs(q・q'))` が0.1°以下かを検査する。その2倍の回転角の最大は約0.15°が目安で、全入力の上限を証明する値ではない。48 bitの成分の量子化刻みは32 bitの約1/32で、回転角の誤差もその程度に小さくなる目安だが、個々の誤差比を保証するものではない。TransQuant16はXYZの各成分について範囲/65535以下の誤差を試験する。smallest-threeはqと−qを同一視して元の符号や長さを保持しないが、Slerpは最短経路を取るため、符号の違い自体は通常は補間する回転を変えない。ただし両端の内積が0のときは最短経路が2つあり、符号で選択が変わり得る。量子化は作者のキー値を変えるため明示時だけ行い、ARSTを量子化しない規則は変えない。

読み手はCMPRを `SkeletalAnimationChannel` のキー列へ展開し、実行時の値の評価は既存のまま行う。Uniformでは `UniformStartSeconds`・`UniformStepSeconds` を印として残し、`ClipChannelSampler` が時刻から添字を求め、前後のキー時刻と比較して補正する。補正で決まらない場合は二分探索へ戻るため、通常の二分探索と同じキーを選ぶ。

展開先のキー列を確保する前に、directoryの重複・範囲・recordSize×count、payloadHash、CMPRのversion・channelCount・dataHash、entryとCHANのcount・encodingおよびRotation/Translation専用encodingのpath、ブロックの順序・サイズ・境界・詰め物・SAMP範囲を検査する。Uniformのstart/step、48 bitの予約bit、Translationの有限なminと非負・有限なextentも検査する。MaxSamplesはSAMPの実格納数だけでなく、CMPRで省略されたキーを含む全チャンネルの論理キー数とroot motionのキー数の合計で数え、既定1048576を超えたら拒否する。展開後も時刻の厳密増加・duration内・有限値など既存の検証を行う。

## 検証の分離

旧fixed sampler/cook/CLI bytesを再採取しない。新v1のrest既定分岐・topology・拒否/override・姿勢は別oracleで確認する。Game/GPU/DCC/実犬の品質はこのCPU codec/bindingの成功から推測しない。公開Resourceを生成する前に全bindingを完成させ、明示owner上の未登録候補を全成功時だけ公開する。
