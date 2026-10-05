# BVHのdouble姿勢評価（G2 / GR84）

## 接続範囲

BvhDocumentの指定frameを、数学的な列ベクトルのlocal/world剛体変換へ評価するpure関数。行列はrow-majorに格納するが、点の変換は R×x+t、親子合成は worldR=parentR×localR、worldT=parentT+parentR×localT。既存Samplerの行ベクトル行列へ混ぜず、bind/retarget・軸/単位変換・fps・補間・RootMotion・CLIへは接続しない。

## 必須の位置規約

EvaluateBvhFrameのTranslationConvention引数には既定値とAutoを設けない。入力の来歴に従い、呼出側が必ず指定する。

- OffsetPlusChannels: 完全3位置channelがあるjointのlocalT=OFFSET+p
- AbsoluteLocalChannels: 完全3位置channelがあるjointのlocalT=p
- 位置channelがないjointは両方ともlocalT=OFFSET

root/nonrootに同じ規則を適用する。AbsoluteLocalは親座標系内の位置でありworld位置ではない。出力Poseには指定した規約を記録する。ゼロ初期化のUnspecifiedは無効値として拒否し、未評価PoseもUnspecifiedを保持する。

[Jeff LanderのBVH解説](https://research.cs.wisc.edu/graphics/Courses/cs-838-1999/Jeff/BVH.html)のOFFSET加算と、[Blender ARMATURE importer](https://github.com/blender/blender/blob/main/scripts/addons_core/io_anim_bvh/import_bvh.py#L526-L570)のp−OFFSETを使う処理は、同じ位置解釈とは断定できない。ここでは特定製品への互換を保証せず、二つの明示された数学規則をそれぞれ検証する。Blenderを起動した実測照合は未実施。

## 受理profileと数値

位置は0またはXYZ全3成分、回転は0またはXYZ全3成分。位置groupの順序は自由、回転groupは6順を受理する。両方ある場合は全位置の後に全回転を要求し、部分成分/交錯/回転の後の位置はUnsupportedChannelsで拒否する。raw parserが保存できる全データを、この評価器が受理するわけではない。

各軸は右手系の正回転。degreeの周期を360で除いてからradianへ変換し、宣言順に右積する。ZYXはRz×Ry×Rx、XYZはRx×Ry×Rz。End Siteのworld位置はworldT+worldR×EndSiteOffset。

入口で親の宣言順・channel範囲と一意性・frame積とcardinality・OFFSET/End Site・正の有限Frame Timeを検査する。名前と非選択frameの値は計算に使わず再検査しない。選択frameの値は有限必須で、有限入力からFKの加算がoverflowした場合もNonFiniteResultで拒否する。規約の不正enum値も拒否する。

出力は値を独立所有し、成功時だけnothrow moveで置換する。失敗・確保例外では旧outを保持する。確保故障注入はこの単位では行わない。

## 検証と残件

BvhEvaluateTestは6順の90度行列、単軸±90度/450度、45度、両位置規約の非零root/nonroot/末端/静止branch/複数frameをliteralで確認する。行列許容は1e-9。raw解析で受理されるが評価profile外のchannel列、親/範囲/enum/非有限/overflowでの拒否とout保持を固定する。CookedMeshTestのMEMBERで、既存Samplerは無変更。

これはBVH source座標での数値評価の受入れである。実Blender照合、target骨格への対応づけ/rest補正、既存Samplerとの照合、フレーム時間補正、root motion、loop、cook/CLI、NVSKEL Stage Bは別の残件として扱う。
