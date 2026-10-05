# SkeletalSamplerの共有化前基準

GR84のtarget bind処理を共有する前に、現行Samplerを実Coreのまま実行し、その結果を固定する。新しい数学実装によって期待値を生成しない。これは既存動作のcharacterizationであり、shear/反射の正しさ、作者時rest pose、retargetやGPU描画の受入れではない。

## 実行

既存SkeletalAnimationSamplingTestへ30caseの採取関数を追加した。既存assertはそのまま先に全て実行する。引数なしでも30caseの検査を行い、`--capture-pose-snapshot <path>` のときだけbit列を保存する。不正引数は2、assert/契約/保存失敗は非0。新exeは作らない。

Windows CIはReleaseの既存CPU集合に本testを追加し45件とする。さらに同じ実CoreのDebugとReleaseを別々にbuildし、各構成で2回direct実行する。DebugとReleaseの一致は要求せず、同じ構成の2回は全byte一致を要求する。最終のRelease CTestを後に実行し、LastTest.logには45件の結果を残す。

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

Python検査器は件数・順序・名前・既知bool・配列数・有限値・Clear・trailer/末尾を独立に検査し、合成byteによる拒否試験をnormal/-Oで実行する。合成byte試験はSamplerの数学検証とは扱わない。

## 出自と次の比較

基点は585d7004505af6cbaf917b2e466b9061aee4853c。Library全体のgit tree `59e5b593a248f555928a7f13e9939192fd263ce7` と一致する場合だけ基準を採取し、tracked sourceのdirty状態も拒否する。浅いcheckoutでも使えるようtree値を固定し、基点のfetchは要求しない。

構成別receiptにはcommit/tree/run/attempt、unchanged Library tree、fixture/main/CMake/検査器とruntimeのhash、実clの版/hash、x64構成、CMake compiler記録、生成vcxprojと構成別compile option/Windows SDK、Core.lib/exe/snapshotのhash、image/SDK/submodule状態を保存する。成功runのartifact API ID/digest/jobと実ZIPを別途照合してから受入れる。logs artifactが存在するだけでは成功としない。

次のbind抽出では同じfixture・構成・compiler/FP設定のsnapshotと全byte比較する。基準は自動更新しない。構成/環境差や不一致がある場合は原因を分離する。artifactの14日保持とは別に取得済みsnapshotとreceiptを保全する。現検査器は共有化前採取専用であり、抽出後はLibrary一致条件を黙って弱めず、固定基準との比較を別の手順として追加する。
