# M9Skinned の fixture

このディレクトリの glTF は、スキニングとアニメーションの読み込みを確かめる小さな入力である。`fixture.bin` は試験が実行時に書く共有バッファで、リポジトリには含めない。

## Blender の書き出しの形

`NoSkeletonHint.gltf` と `BlenderArmature.gltf` は、Blender 5.2.2 の `io_scene_gltf2` の形を写した手書きの fixture である。`skins.py` の `__gather_skeleton` が `None` を返すため、`skin.skeleton` は書かれない。node は子が親より前に並び、`skin.joints` は根の bone から深さ優先で並ぶ。このため node の添字は子が 0、根が 1、関節の並びは `[1, 0]` になる。

bone 名は `Bone` と `Bone.001`、skin 名は Armature オブジェクト名の `Armature`、clip 名は `<Armature>Action` の規則に従う `ArmatureAction` である。clip は子の平行移動と根の回転を持ち、補間はいずれも `LINEAR` である。

- `NoSkeletonHint.gltf`: Armature の node を省き、根の関節と skinned mesh の `Triangle` をシーン直下に置く。旧 NVSKEL 0.2（profile1）と NVSKEL v1 の両経路で受ける入力である。
- `BlenderArmature.gltf`: 非関節の `Armature` の子に、根の関節と skinned mesh の `Triangle` を置く。Armature の TRS は Blender 側の X 軸まわり 90 度、一様 scale 0.5、位置 `(1, 2, 0)` を Y-up に変換したもので、glTF の位置は `[1, 0, -2]` になる。回転は `[0.7071067690849304, 0, 0, 0.7071067690849304]`（x, y, z, w 順）で、float32 の値を倍精度で表記する。profile1 は関節の親が非関節なので拒否する。`--rig-split`（NVSKEL v1、profile3）はこれを受け、Armature の変換を作者 frame に入れる。

バッファは Blender の実出力をコピーしたものではなく、`ValidU8Float.gltf` と同じ 416 バイトの配置を使う。`buffers`・13 個の `bufferViews`・13 個の `accessors` は値も配列順も共通である。mesh は material のない三角形 1 枚に絞っている。`asset.generator` は `NorvesLib hand-written fixture (Blender glTF exporter layout)` として手書きであることを示す。Armature の scale は、FBX 由来でよく見る 0.01 ではなく 0.5 である。

## Blender での書き出し

実物の書き出しは、リポジトリのルートから次のコマンドで作れる。

```sh
blender --background --factory-startup --python Test/Tools/BlenderExportMinimalRig.py -- /tmp/minimal-rig.gltf
```

スクリプトは空のシーンに 2 本の bone と三角形を作り、24 fps の 0・48 フレームにキーを打ち、変換を持つ Armature ごと Y-up の `GLTF_SEPARATE` で書き出す。出力先は任意の `.gltf` パスに置き換える。Blender が生成する `.bin` の配置や accessor の数は、試験用の共有バッファとは異なる。
