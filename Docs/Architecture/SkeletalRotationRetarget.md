# 1frame の回転 retarget（GR84）

## 対象

private な有限単位として、canonical source world 回転と明示の補正 C を現在の target bind に対応づける。
heading を保持し、全 target の平行移動と scale は bind local のままにする。
外部 BoneMap/role schema、C の作成、ルート移動、時刻、補間、loop、CLI、Stage B は扱わない。
この参照は現在の IBM/mesh から作る一時値であり、clip 作成時の rest snapshot や永続 cache ではない。

## 列規約の純粋 kernel

- source/target の親、source world 回転、target bind local/world 回転、解決済みの pair、pair ごとの C を借用する
- source 再利用は Reject/Allow を必須指定し、target の重複は拒否する
- 指定 root pair が実際の root 同士であり、pair 表に存在することを検査する。target を1本の木には限定しない
- mapped の目標 world は W = C D C^T B。local は生成した親 world の転置を左から掛ける。root では W が local
- unmapped は bind local を維持し、生成された親の world に従う。チャンネル用値は出力しない
- 入力配列や mapping の順序に依存しない。最大1024関節の有界走査を使い、kernel は確保も再帰もしない
- 入力の回転は有限、Gram 行列の単位行列からの最大成分差 1e-5 以下、det の1からの差 2e-5 以下を要求する
- その許容内だけ matrix→単位 quaternion→matrix で小さい数値誤差を除く。反射や明確な shear を投影して採用する処理ではない
- 最終 local は trace/最大対角分岐で double XYZW にし、正規化して float へ丸める。最大絶対成分を正にして単独 frame の符号を決める
- 時系列の半球連続化や補間はまだ無い。float 丸めが実エンジンで許容内かは native adapter が別途確認する

work は失敗時に部分更新され得る。out は全件成功後だけ入力 pair 順で置換する。
work/out と入力、および work と out の重なりは拒否する。有効な領域・呼出し中不変の入力は caller の責務。
戻り値は joint/entry/side を持つ。pure 成功だけでは bFloatRealizationChecked は true にならない。

## native の現在 bind 参照と実現値検査

借用 SkeletalJoint と meshGlobal から旧 Sampler と同じ逆行列・bind global/local・TRS 分解を使う。
Compose は unit scale の factory の後に9個の row 成分へ scale を掛ける旧本体を共有する。
列 quaternion を共役・正規化する入口も同じ式を共有する。Sampler に所有配列や新しい拒否は追加しない。

新しい consumer の初期 profile は、有限 affine で正の uniform scale を持つ bind local/global に限定する。
非 unit scale と非恒等 mesh に対応するが、非uniform retarget の一般解を受け入れたことにはしない。
row 長の最大と最小の差は最大長の1e-5以内、非対角の直交誤差も上記許容内を要求する。
affine の3つの perspective 成分は厳密0、m33 は1から1e-5以内。非uniform、shear、reflection、非finite などを区別して拒否する。
旧 Sampler がそれらを処理する挙動は変えない。

raw bind と、旧分解→旧 Compose→旧 FK で作った参照の回転差を全関節で検査する。
回転の差は正規化した quaternion の和/差から 4 atan2 を使って測り、暫定上限を0.05度とする。
生成した実 float XYZW を再び旧共役正規化→Compose→FKへ通し、全targetの world 回転と double 目標との差を測る。
local/global と、Sampler と同じ順の jointModel/palette がすべて有限な場合だけ out を公開する。
成功時の AngularErrorRadians はこの実現値の最大回転誤差、bFloatRealizationChecked は true。
失敗では out を保持し、数値差による失敗には該当 joint と差を返す。確保例外も out を変更しない。
頂点の skinning、bounds、GPU、実資産の見た目まで保証するものではない。

## FP 前提

既存の座標変換と同じ binary64/nearest/gradual-underflow 検査を private header へ共有した。
各 translation unit の fast-math 条件を検査できるよう内部 linkage とする。kernel の float 出力には IEEE binary32 も要求する。
caller の設定を変更しない。通常の非 trapping 算術を前提とし、演算が sticky exception flags を立てる可能性はある。

## 検証の分離

純粋 kernel は実ソースだけを host で実行する。native adapter/Resource/Sampler は Windows Core で検証する。
合成 BVH→既存 FK→明示 basis→回転対応→1key clip→実 Sampler を照合する。
非恒等 mesh と scale2 の fixture は、local 末端長を1/2にして比較する world 長を一致させる。
C≠I の fixture は明示補正式の独立 literal を比較する。補正後姿勢を補正前の source 骨方向と無条件には同一視しない。
未写像の中間親・別root、非可換回転、半回転、位置 overflow による late refusal を含める。
既存 Sampler の凍結 Debug/Release 6095byte と4採取、および旧 cook/managed gate は別途維持する。
Windows/実GPU/実Blender の未実行結果を host テストで代用しない。
