# 明示されたdouble座標基底の変換

GR84のsource側のdouble・列ベクトル値を変換するprivate helper。既存AssetImport::SignedAxisとBvh::Vector3d/Matrix3d/RigidTransformdを再利用する。target bind/FKのfloat行ベクトル処理とは混ぜない。

## 基底と単位

BuildSkeletalCoordinateConversionはup・forward・source handedness・positionScaleを必須引数で受ける。canonicalは+Y上/+Z前/right-handed（Requirements:8944–46、既存ImportTransformの規約と一致）。これは変換先基底の宣言であって、モデルの実際の正面やBVHファイルの軸を観測・推定した結果ではない。Autoや暗黙のBuild引数はない。default変換値は未設定として拒否し、内部index/sign/scaleはprivateに保持する。

source up/forwardの単位ベクトルをu/fとして、Aの行は右手sourceで[u×f;u;f]、左手sourceで[−(u×f);u;f]。Aはsigned permutationであり、A^-1=A^T、det(A)=+1または−1。同じunsigned軸をup/forwardへ使う設定（反対符号も含む）と未知enumを拒否する。

- translation: t'=positionScale*A*t
- matrix: M'=A*M*A^T

実装はindex/sign置換であり、疎行列の0成分との積や重複した行列積/逆行列関数を導入しない。positionScaleは有限・正で、translationだけへ1回適用する。反射Aそのものをquaternionとして扱わない。

## 有効値と失敗

行列APIは全てのfinite 3x3を代数的に変換する。singular/shear/scale/reflectionも許し、SO3誤差閾値やorthonormalizationを加えない。RigidTransform wrapperも回転の妥当性を新たに保証しない。後続quaternion/retarget consumerで必要な回転検証を行う。

IEEE binary64・FE_TONEAREST・gradual underflowを対応環境とする。fast-math等のcompile設定、非nearest丸め（x86では標準fenvとMXCSRの両方を検査）、FTZ/DAZ相当の動作はUnsupportedFloatEnvironmentで拒否し、callerの設定は変更しない。Build後に環境が変わり得るため各変換でも検査する。IsValidは構築済み設定を表し、現在のFP環境で実行できる保証ではない。通常の非trapping算術を前提とする。

全成分の非finite入力、translation積のoverflow、非zero入力がscale後にzeroへ消えるunderflowを拒否する。表現可能なsubnormalは保持する。matrixの符号/置換は有限doubleをoverflowさせず、scaleには依存しない。

Buildと各変換はcandidateで完了してからoutへ反映する。失敗時out保持、入力/outのalias対応。Transform wrapperはrotationを変換できてもtranslationが失敗したら両方とも公開しない。固定inline値だけで処理し、helper内の確保やallocator stubはない。

## 境界

root OFFSETの減算/target bindの加算/root_scale、nonroot位置channelの無視とreport、fps/Quaternion補間/rest補正、JSON/role語彙、CLI、StageB作者時rest snapshotは非対象。generic BvhEvaluateはそのまま維持する。import profileがnonroot位置を無視するときは、後続adapterで位置参照もOFFSETへ戻す必要がある。

## 検証

独立literalでidentity/proper/improper基底、非可換Rz90、一般3x3、translation、parent-child FK covarianceを検証する。全48signed up/forward/handedness組合せでup/forward写像・determinant・直交・一意性・往復を確認。未知enum/軸衝突/不正scale、全成分NaN/Inf、DBL_MAX、subnormal境界、非nearestの3modeとx86のFTZ/DAZとMXCSRだけを変更した丸め、aliasとlate failureのbit保持も試験する。

実helper cppと値型だけのtestをLinuxで通常/O2-NDEBUG/ASan・UBSan（LSan除外）実行する。BvhDocument/動的コンテナを作らず、Core/Windows shim/allocator stub/BvhEvaluate.cppはリンクしない。このhost試験は全CoreやGPUの受入れではない。実Windows49CPU、凍結Sampler4capture、旧89byte/managed gateは別の受入れ条件。
