# 骨格CUBICSPLINE焼込の区間境界

## 現段階の範囲

GR86の明示bake用の数学部品。decode/cook/CLIはまだCUBICSPLINEを拒否する。
SkeletalCubicBoundsはdouble入力を実数とみなしたHermite曲線を外向き区間で包み、
保存float端点を数学的に補間したLINEARとの全区間上界を返す。
実runtimeのfloat時刻/演算やlibmの誤差、sidecar変換後の再認証、キー列生成は別段階。
この部品だけでWindowsの実samplerや描画上の誤差を保証しない。

## HermiteとBezier

原区間長h>0、開始/終了値v0/v1、out0/in1 tangentに対し、Bezier制御点を
[v0, v0+h*out0/3, v1-h*in1/3, v1]とする。回転の原値/tangentを半球反転しない。
評価/分割はde Casteljau。分割パラメータには実際に保存するfloat時刻から計算した値を使い、
丸め前の0.5分割と混同しない。3/4成分、有限値、正durationのみ受理する。

参考: [glTF 2.0 Appendix C.5](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#interpolation-cubic)。

## vectorの上界

保存float端点A/Zからchordの三次制御点L=[A,(2A+Z)/3,(A+2Z)/3,Z]を作る。
E=max_i ||B_i-L_i||2は全パラメータの誤差上界。Bernstein係数が非負かつ総和1なので、
制御点差の凸包に曲線差が収まる。保存floatへの丸め差もLとの比較に含まれる。
移動の単位は入力座標の長さ、scaleは無次元。メートル化は呼出側の責務。
中点で相殺するS字も制御点差は残るため見落とさない。

## rotationの上界

A/Zは保存float quaternionの数学的な正規化。A・Z>0を区間で証明できない場合はUncertified。
r0=||B0||、r1=||B3||、rho(u)=(1-u)r0+ur1、R(u)=(1-u)A+uZとし、
Q=rho*Rを二次Bezierから三次へ次数上げする。E=max_i ||B_i-Q_i||2、
m=min(r0,r1)*sqrt((1+A・Z)/2)は||Q||の下界。E<mなら||原cubic||>=m-E>0。
正規化した原cubicとNLERPのSO(3)角度差は2*asin(E/m)<=4E/m。

理想SLERPとの時間パラメータ差も含め、追加上界にd^2（d=||A-Z||）を使う。
理由: quaternion球上角theta<pi/2に対し、SLERP曲線の2階微分normはtheta^2。
chordとのEuclid差はtheta^2/8以下。正規化後のSO(3)角度差は
2*asin(theta^2/8)<=theta^2/2<=d^2となる。
最終上界は4E/m+d^2 rad。鋭さより保守性を優先し、sin/acosの実装精度を境界計算に持ち込まない。
これはSLERP/NLERPのどちらにも使えるが、実runtimeのfloat正規化・半球判定誤差は別途必要。

## 数値契約と失敗

IEEE binary64、最近接丸め、subnormal有効、基本演算/sqrtのIEEE丸めを前提とする。
各演算の結果をnextafterで外へ広げ、入出力controlも区間として保持する。
fast-math/finite-only/再結合/reciprocal最適化の検出時、最近接以外、FTZ/DAZ検出時はUnsupportedArithmetic。
検出できないコンパイラの個別設定まで保証せず、利用buildは精密な浮動小数点設定が必須。
非有限/容量外数値、overflow、非ゼロが証明できないquaternionは成功にしない。
失敗時outは保持。Splitの左右出力同一objectは拒否し、入力と片側出力の共有は候補作成後の置換で扱う。

## runtime接続前に必要な検査

実SkeletalAnimationSamplerはduration<=float epsilonでalpha=0、回転はdot<0で半球反転、
dot>0.9995でNLERP、それ以外SLERPを使う。短区間のconstant分岐は別に認証するか拒否する。
float保存時刻の衝突・丸め、値のfloat演算誤差、libm精度予算、depth/sample上限を扱ってから接続する。
ApplySkeletalImportは現在animation抽出後にtranslationをscaleするため、fitを含む最終倍率を
前もって解決してメートル許容に合わせるか、最終値で再認証する必要がある。
保証するのはlocal TRS channel差であり、親scale/骨長/スキニング後のワールド頂点変位ではない。

## 検証oracleの精度

Linuxで拡張精度long doubleが使える場合は、独立Hermite参照値が包絡内にあることを直接検査する。
MSVCのようにlong doubleがbinary64と同精度の場合は、参照式側の丸めを考慮した比較に切り替える。
その版はULP級の厳密包絡検証とは呼ばず、既知の正確な値のassertと別に扱う。
参照式の誤差予算はテストだけにあり、実装の境界や採用許容には加えない。
