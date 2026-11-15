# bind行列算術の共有

GR84向けに、現SamplerのIBM→bindGlobal→bindLocalとTRS分解だけをPrivate/Animation/SkeletalBindRowMath.hへまとめる。Resource/名前索引/公開PoseTypes/事前計算cacheを導入しない。作者時rest snapshotとは別の処理。

- bindGlobal = inverse(IBM) * meshNodeGlobalRow
- bindLocal = childGlobal * inverse(parentGlobal)。rootはchildGlobalをそのまま使う
- determinantが非有限またはabs(det)<既存EPSILON、逆行列/積が非有限ならfalse
- Try関数は成功直前に代入し、失敗時out保持。outが入力と同じ行列でもよい
- TRS分解は既存のrow translation、row長のscale、row除算からのrotationとNormalizeをそのまま使う。shear/反射の厳密復元や途中TRSの有限性は保証しない

Samplerの階層/clip検査、時間clamp、channel絶対上書き、手動row scaleを含むCompose、再帰FK、Palette/JointModel/SkinVertex/bounds、Clearは維持する。特に極大bindから一時的に非有限TRSが出てもclipのScale/Rotationで回復できる旧入力を追加拒否しない。TryInverseMatrixの失敗時out保持は新helperの保証であり、旧Samplerが使っていた一時変数の失敗時値は観測結果に含まれない。
