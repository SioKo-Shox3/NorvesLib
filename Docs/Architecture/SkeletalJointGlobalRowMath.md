# 関節global行列の共有評価

GR84の後続retarget検証に使うため、現Samplerのfloat・行ベクトルjoint-global FKだけをPrivate/Animation/SkeletalJointGlobalRowMath.hへ抽出する。double・列ベクトルのBvhEvaluateは変更しない。GR12公開PoseTypes、Resource事前計算、作者時rest snapshot、BoneMapはこの単位の対象外。

## 契約

BuildJointGlobalRowは関節番号、非throwでint32親番号を返す借用getter、local/global/visitStateのSpanを受ける。Samplerは既存jointsを参照するlambdaを渡し、親番号の別配列・std::function・helper内の確保を追加しない。

- local数を基準に3spanの数を一致させる。関節番号とuint32容量・nullptrを入口で検査
- spanは生存中の有効な領域を指すこと。local/global/scratchの重複は非対応。pass中のlocal/親は不変
- scratchはpass開始時に全0。1は処理中、2は同じ入力で完成済みのglobal。2の再訪はgetterを呼ばず成功する
- 親=-1ならrootとしてlocalをbitコピー。親>=0なら親を再帰評価してlocal*parentGlobal。親後置/複数rootに対応
- 親<-1、親範囲外、cycle/自己親、不正scratch、非有限な結果でfalse。singular/shear/反射自体は拒否しない
- falseではglobal/visitStateが途中まで更新され得る。bind helperのstrong-outとは異なり、元のFKと同じ作業領域契約。再評価前はscratchを全0へ戻す
- 再帰深さは親chain長。callerは資産で定めたjoint数上限を守る。helperのuint32容量検査を巨大入力の安全な再帰上限とは扱わない

結果は現Sampler内部のjointGlobals。JointModelMatricesやEntity所有のworld transformではない。Sampleの既存の親/IBM/clip検査、確保、外側の関節順ループ、false時Clear、Palette/JointModel/skin/boundsはそのまま使う。

## 検証

既存Asset bundleの独立試験でroot signed zeroのbit保持、非可換literal、親後置/逆順3段/配列並べ替え/分岐/複数root、cache再訪、cycle/親範囲/不正scratch/span、NaN/Inf/積overflow、有限singular/shear/反射、失敗後のscratchリセットを確認する。

実Samplerは既存30caseの凍結harnessを変更せず、Debug/Release各2回を成功run37373318284の各6095byteと比較する。compiler/SDK/生成option/source-build結合の既存比較器を維持し、実47CPU・旧89byte/managed gateを通す。host GCCは既存MatrixUtils.hのQuaternion scalar operator問題があるため、hostでこのhelperが実行できたとは扱わない。
