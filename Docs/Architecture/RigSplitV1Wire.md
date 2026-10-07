# Skeleton / SkinnedMesh v1 と cold-load の契約

GR82 Stage B2。ClipBank schema 1 の外枠と author-rest 規則を引き継ぐ。既存0.xとClipBankの保存bytesを変更しない。以下は分離v1専用であり、三資産のRegistry公開・async cache・ファイル公開transactionを定めるものではない。

## 共通外枠

`NVSKELv1`、256 byte header、32 byte directory、LE、16 byte section alignment。headerは `ClipBankV1Wire.md` と同じ位置でroleだけが異なる。role1=Skeleton、role2=SkinnedMesh、role3=既存ClipBank。profile=1、topology algorithm=1。payload hashはdirectoryを含むbytes[256,end)のFNV-1a64。

bit0=required。未知optionalも範囲・積・alignment・padding・重なり・FourCC重複・hashを先に検査する。他roleの既知tableはoptionalを装っても拒否する。未知requiredは拒否。writerは必須tableだけを正準順で出し、読み飛ばしたoptionalのbytesを再保存しない。ContentHashは解析した元blob全体のFNVであり、再encodeした別blobのhashへ自動で更新されない。

profile1は直接TRS、正のscale、単一root、外部親なし、最大128 joint。ROOTは正規化されたIdentity bytesだけを受ける。restをSkeletonIdに含めず、完全な名前/親列も比較する。非恒等ROOT、Armature、256 joint、clip-only sourceは後続profileとする。

## role1: Skeleton

必須順は STRS(1), TJNT(24), RSET(48), ARST(48), ROOT(64)。括弧はstride。

- STRS/TJNT/RSET/ARSTのfield位置はClipBankと同じ
- TJNTはUTF8 unsigned byte名前順、完全名と正準parent。parent-after-childを許可し、cycle/forestは拒否
- RSETは1件、ARSTは全jointちょうど。作者label・解決済import scale・40 byte TRS列のrest hashを持つ
- ROOTはf32[16]を1件。profile1ではIdentity以外、NaN、負のzeroを拒否
- IBM、Mesh M、geometry、material、clipを含まない

新roleはSTRS実sizeに加え、aliasを重複計上した全参照所有byte合計を、topology/geometry/stringの所有候補確保前に検査する。writerと同じMaxStringBytesを使う。

所有Skeleton値は完全blob hash、payload hash、rest hash、ROOT hashを保持する。Meshとの共有はimmutable値の共有であり、Registry cacheの完成を意味しない。

## role2: SkinnedMesh

必須順は STRS(1), TJNT(24), SREF(64), VERT(64), INDX(4), IBMS(64), MNGT(64), SUBM(64), MSLT(32), MATS(128)。

- TJNTは完全な正準palette。初版はSkeletonの全jointを含み、subset圧縮しない
- SREF: offset0にSTRS path offset u64、8にpath length u32、12にprofile u32、16にSkeletonId u64、24にSkeleton完全blob hash u64、32にcurrent rest hash u64、40にROOT hash u64、48–63はzero
- VERT: position3f / normal3f / UV2f / joint4u32 / weight4f。zero-weightのslotも正準indexへ写し、全4 indexを検査する。weightは有限非負、和は1±1e-5
- IBMSはpalette順のf32[16]。MNGTはmesh node Mを1件。finite affineかつ既存行列逆算の有限domain内であること。IBM/MはMesh固有で、Skeletonへ格納しない
- SUBMは既存0.2の64 byte record。baseVertex=0、全indexを順序付きで隙間・重複なく所有し、既存有限bounds/slot/range検査を行う
- MSLT: offset0にname offset u64、8にlength u32、12はzero、16にMATS index u32、20–31はzero。完全UTF8 slot名は一意。profile1はMSLT/MATS同件数を必須とし、writerは生成slot順にMATSを並べる。parserは件数と参照を検査する
- MATSは `CookedMaterialFormat.h` の128 byte codec。4つのtexture参照は同STRSへ向ける。全係数、alpha mode、double-sided、ARM、発光を保持し、CPU所有値ではoffsetを所有pathへ置換する

論理pathは非空の相対ASCIIで、空segment、dot/dot-dot、backslash、colon、percent、query/fragmentを拒否する。native source locatorのUnicodeとjoint/clip/slotのUTF8は別契約。

Meshの束縛は完全topologyとSkeleton blob/rest/ROOT hashの全てを検査する。ClipBankのrest overrideでMeshの世代pinは解除されない。同じSkeletonに別Mesh IBMを持たせることはできる。

## Resource と姿勢

SkeletonResource/SkinnedMeshResourceにはvalidated値をLoad前に設定する専用入口を用意する。split modeに入った後は旧setterを拒否し、Unload後もlegacyへ戻さない。新Sampler枝だけがSkeletonのrest/FKとMeshのIBM/Mを読む。旧0.x/B1枝はそのまま残す。

palette = IBM * jointGlobal * ROOT * inverse(M)。JointModelMatricesはIBMを含まない。SamplerのM引数はcomponent overrideを許すので保存Mとの一致を強制しない。cold-load試験は保存Mを使用する。

完全MATSはCPUで保持する。split MeshのLoadはCPU所有の成立であり、render leaseは発行しない。既定Opaqueへ潰して描画しない。renderer material staging、GPUと実物の見た目は未受入。

## cook、同一入力、予算

新入口は `CookRigSplitV1NativePath`。full sidecarを1回読み、absentでもnon-nullのgeometry投影を渡す。同decodeでdocument、BufferSet、生成slot→source material index列を受け取り、名前から番号を推測しない。implicitはUINT64_MAXで実material0と区別する。材質selectorは既存共通resolverを1回通し、未解決・二重指定を拒否する。同名materialは元での改名を促す所有warningを返す。

同じ取得済みbufferからgeometry/material/hashを作る。画像も所有し、external fileはcanonical locatorが同じなら取得済みbytesを再利用する。geometryのexternal readerが成功した時点のlocatorをbuffer index付きでcaptureし、後から元URIを再解決して過去bytesのidentityを作り直さない。既にgeometry bufferとして取得したfileはその保存bytesを使う。GLB viewを成功結果へ借用のまま残さない。

画像はencoded、URI、寸法、最終RGBA、全mip予定byte、累積所有copyを確保前に有限化する。split専用stb実体のmalloc/realloc/freeにもper-call workspace上限を付け、旧decoderは変更しない。reallocの新旧同時生存も予算へ含める。全画像のoutput残量と返却copy残量をdecode前にclampし、ARM scratchとraw所有copyの2本も確保前に予約する。使用material名はslot copy前にUTF8実長を検査し、同名suffixも生成前に検査する。process全RSS、OS allocator overhead、任意入力のCPU時間を保証するものではない。texture副出力は所有cook planであり、texture package公開済みではない。

source_hashは長さ付きdomain分離でraw source、取得済みbuffer実bytes、sidecar raw bytes、正準material/settings hash、生成slot/source対応、GR86 policy、出力論理path/variantを含む。raw sidecar差も別入力として保守的に扱う。三roleには同じsource_hashを付け、cooked hashやSkeletonIdと混同しない。旧hash domainは変更しない。

## package / manifest / cold-load

3個の1-entry NVPKとfresh manifest候補を返す。既存manifest合成、書込み、増分skip、CLIは行わない。package名にはsource/cooked hashを含め、生成後の完全pathにも4096 byte上限を適用する。これはファイル群を同時にatomic snapshotした保証ではなく、各読込結果はhashで検査する。

- Skeleton: kind `skeleton`=5、Ske1、`nvskel.v1.skeleton`、version1
- Mesh: kind `model`=2、Skm1、`nvskel.v1.skinmesh.pnujiw.u32`、version1
- Bank: kind `animation`=6、Anm1、`nvskel.v1.clips`、version1

metadataは全roleでskeleton_id(16桁lower hex)/profile/joint_countを必須とする。Meshはvertex/index/submesh/material_slot/material数量、Bankはclip/snapshot/channel/sample数量も必須とし、未知/他roleの項目を拒否する。cold-loadで実payload数量と照合する。旧Modelの4数量metadata規則を新roleへ流用しない。

一つのshared const AssetSystem snapshot、Skeleton path、Mesh path、順序付きBank path(1–16)、variant、policy、limitsからcooked-onlyで読む。manifestを別名から変更しないことはcallerの責務。opened streamの実package sizeをresize前に検査し、per-fileと全読込の残量を渡す。旧入口の既定は無制限を保つ。

Bank順→内部clip順を維持し、全名重複は拒否。全bankのrest比較/数量検査成功後だけCPU結果を置換する。未登録CreateResourceで全child/aggregateのLoad成功後だけownerのoutを置換し、Registry公開/GC rollbackを使わない。ResourceIdの欠番は許容する。
