# bind行列算術の共有

GR84向けに、現SamplerのIBM→bindGlobal→bindLocalとTRS分解だけをPrivate/Animation/SkeletalBindRowMath.hへまとめる。Resource/名前索引/公開PoseTypes/事前計算cacheを導入しない。作者時rest snapshotとは別の処理。

- bindGlobal = inverse(IBM) * meshNodeGlobalRow
- bindLocal = childGlobal * inverse(parentGlobal)。rootはchildGlobalをそのまま使う
- determinantが非有限またはabs(det)<既存EPSILON、逆行列/積が非有限ならfalse
- Try関数は成功直前に代入し、失敗時out保持。outが入力と同じ行列でもよい
- TRS分解は既存のrow translation、row長のscale、row除算からのrotationとNormalizeをそのまま使う。shear/反射の厳密復元や途中TRSの有限性は保証しない

Samplerの階層/clip検査、時間clamp、channel絶対上書き、手動row scaleを含むCompose、再帰FK、Palette/JointModel/SkinVertex/bounds、Clearは維持する。特に極大bindから一時的に非有限TRSが出てもclipのScale/Rotationで回復できる旧入力を追加拒否しない。TryInverseMatrixの失敗時out保持は新helperの保証であり、旧Samplerが使っていた一時変数の失敗時値は観測結果に含まれない。

## 検証

独立helper試験は既存CookedMeshTest bundleへ追加し、新exeは作らない。非可換literal、root/parent、入力/出力alias、特異/非有限/積overflow/determinant境界と失敗out保持、非一様scale/反射/途中infinite scaleを検査する。

Sampler側は成功run37373318284のDebug/Release各6095byteを固定fixtureへ保存する。旧fixture cpp、main、Rendering CMake、採取時構造検査器はbyte不変。別の比較器SkeletalSamplerCompare.pyでreceiptの固定SHA、構成別binaryのSHA/size/構造、2回反復、基準全byte、実cl hash/version、x64、Vulkan/Windows SDK、生成Core/Sampler compile option、source個別override不在、CMakeのsource/build root、projectの実source Include、構成別exe/Core.libの期待位置を照合する。Debug対Releaseの一致は要求しない。cl/Core/exe hashはCI実計算receiptであり、これらbinary自体をartifactへ保存したとは扱わない。

基準取り用SkeletalSamplerBaseline.pyの旧Library一致条件は弱めず残す。新workflowでは構成別比較器を呼ぶ。凍結したC++ marker中のno_refactorは旧採取harnessの識別子として残る文字列で、共有化後Libraryが旧byteのままであるという主張には使わない。比較receiptのpurposeはpost_refactor_frozen_byte_comparisonとして区別する。

Linux GCCでは既存MatrixUtils.hの未使用Normalize<Quaternion>がscalar operator欠落で実体化され、helper試験のcompileへ到達できなかった。Windows代替shimや既存Math修正では回避せず、実Core/MSVCと固定bit列を受入れゲートとする。これはhelperのruntime試験がhostで成功したという意味ではない。
