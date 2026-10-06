# BVH回転clipのNVSKEL cook接続（GR84）

## 型付き入口

CookGltfWithBvhToNvskelNativePath はglTF/GLBのmesh/rigと借用BVH bytes、具体的なsource/target名pair、明示root、pair順のC、軸・手系・位置規約・time設定を受け、NVSKEL0.2 bytesと所有reportを返す。
呼出中は全入力を不変に保つ。role展開の出力を受ける内部入口であり、外部direct-pair JSONやCLIを定義しない。

Addは指定clip名との一致が0件なら末尾へ追加する。Replaceは一致がちょうど1件のときその位置へ置換する。空名、暗黙の先頭選択、名前の正規化、置換時のrenameは受けない。無関係なclipの値と順序を保持し、無関係な重複名まで新たに資産全体の拒否条件にはしない。

## animation無しrig

専用DecodeBvhTargetRigGltfNativePathだけがanimations欠落と空配列を受ける。nullや不正型、壊れた既存animationは拒否する。旧DecodeRigGltfの1本以上、旧単clip入口の1本、writer/loaderの1本以上契約は維持する。
0本でもmesh/node側のMorph Drop検査を1回行い、検証後だけ報告を完了状態にする。偽のbind clipは作らない。明示scale/fitもgeometry/bindへ従来どおり適用する。

## 所有と互換

新規clipは既存sample保持importerで生成し、既存writerへ渡す。NVSKELを自己parseし、全clipの名前・duration・channel/path/interpolation・time/valueのfloat bitsを照合してから結果を公開する。失敗や確保例外では以前の結果を保持する。
追加したMaxNvskelBytesは最終NVSKEL配列の確保前に適用する出力byte予算であり、decodeや一時配列を含むRSS上限ではない。
128関節、単一root、mesh必須という現package profileは変わらない。in-memory importerの1024関節/forest対応とは別の境界。

旧BVH無し経路にはhash要素を追加しない。BVH付き経路では従来glTF/外部buffer/import/decode hashに、算法版・raw BVH・operation/名前/pair/root/C・全import設定とlimitsを、長さ付きbytes/明示little-endian scalarとして追加する。構造体paddingやlocale文字列は使わない。
この入口は毎回cookし、既存glTF-only fingerprintをBVH要求へ流用しない。BVH/Profileのファイル入口を追加する場合はdependency snapshot、fingerprint、公開前再採取と失効条件を同時に接続する。

## 検証範囲

合成fixtureの実package/manifestをAssetSystemで解決し、そのblobからparseしたmesh/rig/clipを実Resource/Samplerへ渡す。全stored keyと選択midpointのCPU接続を対象にする。
現在のtargetから生成して同じpackageへ保存するため、作者時rest snapshotによる別骨格への安全な再束縛を意味しない。自動C、role/Profile、root motion、再サンプル、GR83統合loader、Stage B、実Blender/GPUの品質はそれぞれ別の機能。
