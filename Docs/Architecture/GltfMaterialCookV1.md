# 明示 NVMESH v1 の材質 cook

G2 / GR79。形式名 `nvmesh.v1.mesh3d.pnt.u32.clustered` を明示した単体 cook に限定する。
現在の出力 profile は 1 primitive / 1 material / LOD0。材質128B、cluster128Bを共有codec/readerで検査する。
旧 v0 の既定・wire・hash は変えない。runtime loaderのv1拒否もこの変更では解除しない。

## 材質と画像

- sidecarの共有材質selectorを使い、存在しない指定・名前と番号の重複指定を拒否する。同名材質は元資産のrenameを促す警告を出す
- SurfaceNameは解決後に `surface_requires_GR81` で拒否する。未接続の設定を捨てて成功させない
- AOは既定ignore。AI profileを明示した資産ではmetallicもignore。必要な材質だけtextureへ上書きできる
- ARMのautoは1〜99パーセンタイル幅を使う既存共通処理。残るchannelにはfactorを焼き込み、全channelが定数なら派生画像を出さない
- retained ARMではroughness/metallicのscalarを未指定の負値、AOを1として二重乗算を防ぐ。定数channelには最終scalarを記録する
- 色画像と発光画像はsRGB、法線とARMはlinear。ARMはraw RGBA8としてtexture cookerに渡し、PNG bytesに偽装しない
- 発光factor×strengthが非ゼロならemissiveNitsPerUnit必須。資産・材質・設定名付きで拒否する。textureだけでfactorゼロの材質は通す
- asset-set単位の換算値は単体requestにまだ存在しない。今回の共有resolverには不在として渡し、種別横断spec側の接続は後続に残す

## Auto両面判定

暫定の数値方針は、完全一致の位置溶接後に「境界edge / unique edge <= 0.001」、非多様体・同方向のedge対・溶接後の退化三角形がない場合をほぼ閉じたmeshとする。+0/-0は同位置。近接位置のepsilon溶接はしない。
閉じたmeshは片面、それ以外は両面。force_true / force_falseを優先する。
方針とアルゴリズム版はv1 hashに含む。最終の見た目の受入れをこのCPU判定で代替しない。

## 依存と互換

cookとfingerprintは材質解析を共有する。必要な外部画像は所有してcookし、GLB内のbytesだけを入力blob存続中に借用する。ARMが定数化して省略されても、判定に読んだ元画像はsource hashへ含める。
JSONの順序ではなく解決済みの設定をhashする。一方、共通増分の依存snapshotはsidecarの生bytesとglTF外部参照も記録する。
出力前に共通の依存・集合guardを使い、入力、sidecar、外部画像への出力aliasを拒否する。出力の版とmanifestのcooked_versionは厳密に合わせる。
可変inventoryのmanaged update、spec v2、並列jobs、runtime描画接続はこの単体cook変更に含めない。

## 検証

GltfMaterialCookV1Testはreleaseでも有効なCHECKで実glTF/GLB、PNG decode、既存encoded/raw texture byte一致、材質codec往復、ARM、発光、selector、閉鎖判定、単体package/manifest、cache、入力alias拒否を検証する。
既存の79 standalone出力と10 texture出力は凍結基準と別途byte比較する。managed CLI、中断復旧、CNGの既存gateも維持する。
実Windows CIの受入れ結果はPROGRESS.mdに記録する。実画像/GPU描画の受入れは後続。
