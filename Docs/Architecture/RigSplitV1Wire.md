# Skeleton / SkinnedMesh v1 と cold-load の契約

GR82 Stage B2。ClipBank schema 1 の外枠と author-rest 規則を引き継ぐ。既存0.xとClipBankの保存bytesを変更しない。以下は分離v1専用であり、三資産のRegistry公開・async cache・ファイル公開transactionを定めるものではない。

## 共通外枠

`NVSKELv1`、256 byte header、32 byte directory、LE、16 byte section alignment。headerは `ClipBankV1Wire.md` と同じ位置でroleだけが異なる。role1=Skeleton、role2=SkinnedMesh、role3=既存ClipBank。profile=1、topology algorithm=1。payload hashはdirectoryを含むbytes[256,end)のFNV-1a64。

bit0=required。未知optionalも範囲・積・alignment・padding・重なり・FourCC重複・hashを先に検査する。他roleの既知tableはoptionalを装っても拒否する。未知requiredは拒否。writerは必須tableと対応する任意tableを正準順で出し、読み飛ばした未知optionalのbytesを再保存しない。ContentHashは解析した元blob全体のFNVであり、再encodeした別blobのhashへ自動で更新されない。

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
- SUBMは既存0.2の64 byte record。baseVertex=0、既存有限bounds/slot/range検査を行う。LOD表が無ければ全indexを、表があればLOD0のindexだけを順序付きで隙間・重複なく所有する。
- MSLT: offset0にname offset u64、8にlength u32、12はzero、16にMATS index u32、20–31はzero。完全UTF8 slot名は一意。profile1はMSLT/MATS同件数を必須とし、writerは生成slot順にMATSを並べる。parserは件数と参照を検査する
- MATSは `CookedMaterialFormat.h` の128 byte codec。4つのtexture参照は同STRSへ向ける。全係数、alpha mode、double-sided、ARM、発光を保持し、CPU所有値ではoffsetを所有pathへ置換する

論理pathは非空の相対ASCIIで、空segment、dot/dot-dot、backslash、colon、percent、query/fragmentを拒否する。native source locatorのUnicodeとjoint/clip/slotのUTF8は別契約。

Meshの束縛は完全topologyとSkeleton blob/rest/ROOT hashの全てを検査する。ClipBankのrest overrideでMeshの世代pinは解除されない。同じSkeletonに別Mesh IBMを持たせることはできる。

## role2 の任意節 LODS・PART（GR63）

`LODS` と `PART` は両方あるか両方ない。いずれもflags=0の任意節であり、writerは表があるときだけ必須10節の `MATS` の後に `LODS`、`PART` の順で置く。数値はすべてリトルエンディアン。表の無い資産と、LODを頼まないcookの出力は従来とバイト一致する。旧いローダは2節を未知の任意節として読み飛ばす。ただし旧いrole2ローダはSUBMにINDX全体の所有を要求するため、LOD1以降の索引を含む資産は受け入れない。

- `LODS`: 16 byte record、1〜8件。offset 0は三角形数 `TriangleCount` u32、4は最大誤差 `MaxError` f32、8〜15は0。誤差は頂点座標と同じオブジェクト空間の単位。
- `PART`: 528 byte record、1〜256件。offset 0はサブメッシュ番号 `SubMeshIndex` u32、4は関節数 `JointCount` u32（1〜16）、8〜15は0。offset 16 + k×8（k=0〜7）はLOD kの `FirstIndex` u32と `IndexCount` u32で、存在するLOD数以降の枠はすべて0。offset 80 + j×28（j=0〜15）は `JointIndex` u32、`Min` f32×3、`Max` f32×3で、関節数以降の枠はすべて0。

INDXは `[LOD0の全部位][LOD1の全部位]…` の順で、各LODの中はサブメッシュ順、その中は部位順とする。部位はサブメッシュの中を分け、描画の単位はサブメッシュ×部位×LOD。頂点バッファは全LODで共有し、LODの頂点はLOD0が参照する頂点の部分集合、部位の境界の頂点は全LODで同じとする。

表がある場合、SUBMがINDXの `[0, 3×T0)`（LOD0）だけを所有し、LOD1以降はPARTの範囲が所有する。表が無い場合は従来どおりSUBMがINDX全体を所有する。各関節のAABBは、関節の重みが0より大きい部位の頂点をメッシュ空間のバインド姿勢で包む。実行時には各箱を関節のスキン行列で動かし、部位の全関節の箱の和で部位を包む。

表と関連データは次を検査する。`T_k` はLOD kの `TriangleCount`、範囲は半開区間とする。

1. LODSとPARTは両方空なら有効で、片方だけ空は無効。
2. LODは1〜8件、部位は1〜256件、部位×LODは2048件以下。積はu64で計算する。
3. 各 `T_k` は1以上、`MaxError` は有限かつ0以上。全LODの `T_k` の和×3がINDX全体の件数と一致する。
4. SUBMの `IndexCount` の合計は3×T0と一致する。SUBM自体の順序・材質・範囲は既存の `ValidateSkeletalSubmeshData` がLOD0の索引に対して検査する。
5. 各部位の `SubMeshIndex` はSUBMの範囲内で、記録順に非減少。すべてのサブメッシュに1部位以上ある。
6. 各LODについて、部位の範囲は記録順にそのLODの開始から終端まで隙間なく続く。開始は `3×Σ_{i<k} T_i`、終端は開始に `3×T_k` を足した値。各 `IndexCount` は0を含む3の倍数。
7. LOD0では各サブメッシュの最初の部位の開始が `IndexStart` に、最後の部位の終端が `IndexStart + IndexCount` に一致する。
8. LOD数以降の `LodRanges` は `FirstIndex` と `IndexCount` がともに0。
9. 各部位の関節AABBは1〜16件。関節番号は関節数未満の狭義昇順で、各軸の `Min` と `Max` は有限かつ `Min ≤ Max`。
10. INDX全体の各索引は頂点数未満。LOD1以降の索引はLOD0が参照する頂点だけを使い、頂点数のフラグ列で検査する。

読み取り時はさらに2節の有無、件数、予約byte、未使用枠と関節数を検査し、成功時だけ所有表を置き換える。

## role2 の任意節 SURF（GR60）

`SURF` は flags=0 の任意節で、表があるときだけ `MATS` の後、LOD 表があれば `PART` の後に置く。数値はリトルエンディアン。記録は 16 バイト、件数は 1〜1048576 件。

- offset 0 の u32: 頂点番号 v0 を bit 0〜20、v1 の下位 11 bit を bit 21〜31 に格納する。
- offset 4 の u32: v1 の上位 10 bit を bit 0〜9、頂点番号 v2 を bit 10〜30 に格納する。bit 31 は予約で 0。
- offset 8 の u32: 重心座標 b1 を bit 0〜15、b2 を bit 16〜31 に unorm16 で格納する。b0 = 1 − b1 − b2。
- offset 12 の u32: アルベドの RGBA8。R が最下位バイトで、基本色テクスチャと同じ sRGB の符号化のままとする。

3 頂点の番号がそれぞれ `VERT` の件数未満であり、整数の b1 + b2 が 65535 以下で、予約 bit が 0 で、件数が範囲内であることを検査する。

各記録は LOD0 の三角形から、面積に比例して決定的に選んだ点を表す。位置はバインド姿勢・メッシュ空間で `p = b0·P[v0] + b1·P[v1] + b2·P[v2]`。アルベドは基本色テクスチャの UV 位置の texel に基本色の係数を bake したものと同じ RGBA8 値である。

表の無い資産と、表面標本を頼まない cook の出力は従来とバイト一致する。旧いローダは `SURF` を未知の任意節として読み飛ばす。

## Resource と姿勢

SkeletonResource/SkinnedMeshResourceにはvalidated値をLoad前に設定する専用入口を用意する。split modeに入った後は旧setterを拒否し、Unload後もlegacyへ戻さない。新Sampler枝だけがSkeletonのrest/FKとMeshのIBM/Mを読む。旧0.x/B1枝はそのまま残す。

palette = IBM * jointGlobal * ROOT * inverse(M)。JointModelMatricesはIBMを含まない。SamplerのM引数はcomponent overrideを許すので保存Mとの一致を強制しない。cold-loadでは保存Mを使用する。

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

明示profile2の静的親と必須作者frameは `RigStaticRootFrame128.md` を参照する。本書のprofile1契約と既定値は維持する。

## クリップ解析の任意節（GR82）

明示解析を選んだprofile 2/3のClipBankだけ、末尾へ `ANLY`（optional、flags=0）を追加する。解析なしの7/8節の出力は不変。レコードはCLIP順に96B、件数はclip件数と一致する。offset 0=rootの正準joint番号u32、4=候補loop(bit0)/採用loop(bit1)、8から8個のf64（loop誤差・秒長・実効source fps・X移動・Z移動・平面移動距離m・平均速度m/s・総yaw rad）。72〜95は0。

loopは端点の姿勢差からの候補で、明示Loop/Onceを優先する。移動とyawは作者rest・階層・ROOTを合成し、+Y上/+Z前のモデル空間を固定間隔で標本化した要約。既定30Hz、最大4096標本。接地判定や移動の適用はしない。姿勢距離の長さは明示値、未指定ならrest joint位置の対角（退化時1m）で正規化する。fps補正は作者fpsとsource fpsの両方が必須で、キー時刻を `timeScale * authoredFps / sourceFps` 倍する。fpsを推測しない。

role2の任意節LODS（LOD）・PART（部位）・SURF（表面標本、GR60で実装）は上記の書式を使う。後続の任意節名を予約する: SOCK（ソケット）、MARK（イベント/位相）、CMPR（圧縮）。CLIPとROOTは既存の必須節名であり再利用しない。未実装の任意節は従来通り読み飛ばし、必須節は拒否する。

## 明示ファイルCLI

`AssetCook --rig-split --input <GLB/glTF> --out <新規directory> --logical Models/Dog` はprofile 3/256関節でSkeleton・SkinMesh・ClipBankと参照textureをcookし、`manifest.json` とpackage群を新しいdirectoryへまとめて公開する。既存directoryは空でも置換しない。更新/増分はGR96の責務。

既定でクリップ解析を保存する。`--no-analysis` で省略可能。`--root-joint <名前>`、`--loop auto|true|false`、`--authored-fps` と `--source-fps` の組、`--time-scale` を指定できる。既存の影響縮約/CUBICSPLINE/morph引数とsidecar指定も受ける。検証はnative pathで実出力の全bytesを読み直し、3資産のparse/束縛を行ってからno-replace renameする。描画や実物DCCの検証は含まない。
