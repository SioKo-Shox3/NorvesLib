# 明示 NVMESH v1 の材質 cook

G2 / GR79。形式名 `nvmesh.v1.mesh3d.pnt.u32.clustered` を明示した単体 cook に限定する。
出力 profile は 1 glTF mesh / N primitive・参照材質 / LOD0。材質128B、cluster128Bを共有codec/readerで検査する。
旧 v0 と単primitive v1のwire・hashは維持する。複数submeshのruntimeは明示拒否する。単材質の対応subsetはImportedOpaqueRuntime.mdを参照する。

## 材質と画像

- sidecarの共有材質selectorを使い、存在しない指定・名前と番号の重複指定を拒否する。同名材質は元資産のrenameを促す警告を出す
- SurfaceNameは解決後に `surface_requires_GR81` で拒否する。未接続の設定を捨てて成功させない
- AOは既定ignore。AI profileを明示した資産ではmetallicもignore。必要な材質だけtextureへ上書きできる
- ARMのautoは1〜99パーセンタイル幅を使う既存共通処理。残るchannelにはfactorを焼き込み、全channelが定数なら派生画像を出さない
- retained ARMではroughness/metallicのscalarを未指定の負値、AOを1として二重乗算を防ぐ。定数channelには最終scalarを記録する
- 色画像と発光画像はsRGB、法線とARMはlinear。ARMはraw RGBA8としてtexture cookerに渡し、PNG bytesに偽装しない
- 発光factor×strengthが非ゼロならemissiveNitsPerUnit必須。資産・材質・設定名付きで拒否する。textureだけでfactorゼロの材質は通す
- asset-set spec v2のtop-level emissiveNitsPerUnitは正の有限numberのみ受理し、不正値は設定名付きで拒否する。spec.Emissionから単体requestのAssetSetEmission、ImportSettingsFileOptionsを経て共有resolverへ渡す。優先順位は素材 > 資産sidecar > asset-setで、上位の不正値を下位へfallbackしない。import設定の正本はsidecarであり、asset-setの換算は限定した例外

## Auto両面判定

暫定の数値方針は、完全一致の位置溶接後に「境界edge / unique edge <= 0.001」、非多様体・同方向のedge対・溶接後の退化三角形がない場合をほぼ閉じたmeshとする。+0/-0は同位置。近接位置のepsilon溶接はしない。
閉じたmeshは片面、それ以外は両面。材質境界を開口と誤認しないよう全primitiveを合わせて一度判定し、各auto材質へ適用する。force_true / force_falseを材質ごとに優先する。
方針とアルゴリズム版はv1 hashに含む。最終の見た目の受入れをこのCPU判定で代替しない。

## 依存と互換

cookとfingerprintは材質解析を共有する。必要な外部画像は所有してcookし、GLB内のbytesだけを入力blob存続中に借用する。ARMが定数化して省略されても、判定に読んだ元画像はsource hashへ含める。
JSONの順序ではなく解決済みの設定のcanonical（67B）を材質のsource hashへ含める。共通増分の依存snapshotはsidecarの生bytesとglTF外部参照に加え、AssetSetEmission.Presentのときだけasset-set.emissiveNitsPerUnitと換算値を記録する。cache判定のfingerprintにも同じ換算を渡す。
出力前に共通の依存・集合guardを使い、入力、sidecar、外部画像への出力aliasを拒否する。出力の版とmanifestのcooked_versionは厳密に合わせる。
可変inventoryのmanaged update、spec v2、並列jobs、runtime描画接続はこの単体cook変更に含めない。

## 発光換算とcacheの契約

- asset-set換算による保存値を、資産sidecar・素材の明示指定が上書きする。上位の不正値や不正なasset-set値を拒否し、換算未設定時は資産名・材質名・設定名を診断する。strength=0またはfactor=0の非発光材質は受理する。
- 換算の差をfingerprintに反映し、cookのhashと一致させる。換算の追加・変更・削除はcacheを失効させ、他の依存も変わらず同じ換算ならskipを維持する。
- 発光sourceのv0 cook互換を維持する。


後続のruntime対応subsetはImportedOpaqueRuntime.mdに記載する。実物の資産の撮影・見た目の受入れは未実施であり、この文書のcook単体受入れと描画接続/GPU画像の受入れは別である。

## 複数primitiveの所有範囲

primitiveの元順序をsubmesh順に保ち、局所indexを検査してから頂点列へbaseVertexを加えて連結する。importのscale/fit/pivotは全meshへ一度適用する。cluster化は各primitiveの局所頂点/局所indexで行い、出力indexだけglobalへ戻す。clusterは材質境界を越えない。VertexOffsetは0、VertexCountはglobal上限を表す。材質表は参照元番号の昇順で共有し、暗黙defaultは番号0と区別した末尾slotにする。骨格の8slot制限は静的meshへ適用しない。

画像のtool内IDは64bitでありNVMESH wireのフィールドではない。source画像は元番号、既存単primitiveの派生ARMはUINT32_MAXと旧pathを保つ。multi派生ARMはUINT32_MAX+1+元材質番号、暗黙defaultは2^33とし、論理pathを `.mat<番号>.arm.rgba8` / `.matdefault.arm.rgba8` に分ける。共有source画像はID・path・format・payload・bytesが一致する場合だけ統合し、材質をまたぐsRGB/linear衝突は拒否する。cook/fingerprintは統合後のID順inventoryを共有する。
