# SkeletalSamplerの共有化前基準

この文書は、GR84のtarget bind処理を共有する前に実CoreのSamplerから採取した基準データを記録する。既存動作の記録であり、shear/反射の正しさ、作者時rest pose、retargetやGPU描画の受入れではない。

## 固定case

1. identity bind
2. 非一様scale＋回転のbind
3. 同bindへTranslationだけ上書き
4〜6. mesh/IBM非identity、linearの時刻clamp前・中間・後
7〜9. Stepのkey直前・一致・以後
10. 非可換な親子の移動・回転・scale
11. 空mesh
12〜17. NaN時刻・親cycle・重複key時刻・特異IBM・特異mesh・FK overflowの拒否
18〜19. 親が後ろにある有効階層・複数root
20〜21. 空/範囲外channel・重複channel
22〜26. shear・反射・極大bind・clipで上書きした極大bind・極大IBM/clipの旧結果観察
27〜29. 非有限IBM/mesh・空骨格の拒否
30. 失敗群の後の成功

全caseで同じposeを再利用し、呼び出し前にPalette・JointModel・bounds・flagを汚す。falseなら2配列empty、bounds全6成分0、flag=falseを検査する。22〜26の成功boolは事前に断定せず、旧実装の結果を固定する。観察caseにも有限出力とfalse時Clearの検査を適用する。

## bit列

全整数はuint32 little-endian、floatはbit_castした32bit列。struct paddingやpointer/名前hashを含めない。先頭はmagic `0x4250534e`、schema=1、case数=30。各caseはID、ASCII名のbyte長＋byte、成功bool、Paletteの数＋16floatずつ、JointModelの数＋16floatずつ、vertex数＋各SkinVertex位置/法線6float、bounds有無＋Min/Max6float。末尾は `0x454e4442`。失敗時のSkinVertexはempty paletteによる既存fallbackも記録する。

## 出自

基点は585d7004505af6cbaf917b2e466b9061aee4853c。採取時のLibrary全体のgit treeは `59e5b593a248f555928a7f13e9939192fd263ce7`。
