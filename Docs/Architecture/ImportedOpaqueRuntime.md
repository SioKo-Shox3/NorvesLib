# GR79 / GR27: 取り込んだ材質のruntime接続

静的な明示NVMESH v1はR8分割＋scalar 1×1 texture方式を使い、v0の従来経路も残す。スキンの取り込み材質はSkinnedMaterialBuilderが材質スロットごとに構築する。

## 対応範囲

- 静的なNVMESH v1は1 submesh / 1 material / LOD0、DefaultLitに対応し、BaseColor RGBA、canonical emissive色/nits、最終metallic/roughness/AO scalarをMegaMeshMaterialへ渡す。スキンはSkinnedMaterialBuilderから材質へ渡す
- 静的とスキンの両方で正のnitsの発光テクスチャ（sRGB）と任意の有限のnormalScaleを受理する。発光はテクスチャのRGB × EmissiveColor（Y=1の色度）× nitsで、テクスチャがなければ色度 × nits。normalScaleは復号した接空間法線のXYに掛けて正規化する
- これらの値はMegaGeometry、通常とスキンのGBuffer、可視バッファの材質ごとのタイルの解決で使う。直接dispatchの解決は発光の定数のみを使い、発光テクスチャとnormalScaleは適用しない
- nits=0の発光画像はEmissivePathがあっても読込・uploadしない。正のnitsでは保存RGB × nitsとruntimeのY再正規化後のRGB × nitsの両方を65504未満に制限する（CookedMaterialV1.md）。cook時に発光factor × strengthが非0ならemissiveNitsPerUnitは必須で、未設定を拒否する
- OpaqueのBaseColor.aは仕様どおり描画で無視する。CPU値は保持する
- GR27の範囲として、静的な材質はMaskと両面を受理して材質へ渡すが、MegaGeometryはMaskの切り抜きを行わない。Blendは共通の検査で拒否し、スキンはさらにMaskと両面を拒否する
- 新形式のmanifest format/cooked_versionと実blobの版は一致必須。sync/worker共通で検査する

## 画像とARM

v1画像はcooked RGBA8・1 layerのみ受理し、albedoと正のnitsのemissiveはsRGB、normal/ARMはlinearを検査する。loose fallbackで異なる見た目へ黙って代用しない。
現shaderはalbedo画像のalphaをGBufferへ流すため、Opaque albedoは全mipのalpha=255に限定する。これはglTFの制限ではなく、現在の背景判定への漏れを避ける保守的profileである。

ARMは元資産1枚から使用maskのR/G/Bだけを所有する。mask=0ならARMを読み込まない。使用channelのcook済み全mipをR8 textureへ直接uploadし、使用しないchannel・alpha配列を作らない。mip0からの再生成に依存しない。v0の既存分割・fallback・再生成経路は変えない。
派生R8はshared pointer取得後に一時的なresource registry登録を解放し、model材質へ所有を移す。モデル解放・途中失敗で匿名handleを蓄積しない。未対応値と全画像をCPUで検査してからuploadを始める。device側の失敗時にモデルを成功扱いしないが、標準RHI Update(void)のdriver内部エラー検出やGPU実行の保証を新たに与えるものではない。

## Scalar binding

textureがあるchannelは焼込済み値をそのまま使い、scalarを二重乗算しない。textureがなければ最終scalarを1×1 linear RGBA8 textureへ量子化する。metallic/roughnessが負なら従来の黒/白、AO=1は既定白を使う。
quantizeはround(clamp(v,0,1)×255)。GPU入力の最大量子化差は0.5/255で、CPU floatのbit保持とは分ける。pass所有cacheは同じlevelを共有し、Shutdown/device変更で解放する。作成失敗や非有限scalarは既定値で隠さず、その描画を止める。

## 残件

RGB factor、R/G/Bの写像、scalar量子化、sRGB、定数発光nitsを実Vulkan readback/PNGで確認し、既定Indoor/Outdoor goldenと実物の犬の見た目を確認する作業が残る。静的の複数材質・Blend、スキンのMask・Blend・両面とpacked GPU化はこのprofile外。
