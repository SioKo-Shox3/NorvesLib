# GR79: 不透明・1材質のruntime接続

GR79本文のR8分割＋scalar 1×1 texture方式を使う。shader、descriptor/UBO配置、露出、BRDFは変更しない。v0の従来経路を残し、明示NVMESH v1だけ対応profileへ接続する。

## 対応範囲

- 1 submesh / 1 material / LOD0、DefaultLit、Opaque、片面、normalScale=1
- BaseColor RGBA、canonical emissive色/nits、最終metallic/roughness/AO scalarを保持してMegaMeshMaterialへ渡す
- OpaqueのBaseColor.aは仕様どおり描画で無視する。CPU値は保持する
- EmissivePathがあってもnits=0かつ色0なら寄与がないため読込・uploadを省略する
- 正nitsのemissiveTexture、Mask/Blend、両面、非恒等normalScaleは明示拒否。alpha/cull/影はGR27等へ残す
- 新形式のmanifest format/cooked_versionと実blobの版は一致必須。sync/worker共通で検査する

## 画像とARM

v1画像はcooked RGBA8・1 layerのみ受理し、albedoはsRGB、normal/ARMはlinearを検査する。loose fallbackで異なる見た目へ黙って代用しない。
現shaderはalbedo画像のalphaをGBufferへ流すため、Opaque albedoは全mipのalpha=255に限定する。これはglTFの制限ではなく、現在の背景判定への漏れを避ける保守的profileである。

ARMは元資産1枚から使用maskのR/G/Bだけを所有する。mask=0ならARMを読み込まない。使用channelのcook済み全mipをR8 textureへ直接uploadし、使用しないchannel・alpha配列を作らない。mip0からの再生成に依存しない。v0の既存分割・fallback・再生成経路は変えない。
派生R8はshared pointer取得後に一時的なresource registry登録を解放し、model材質へ所有を移す。モデル解放・途中失敗で匿名handleを蓄積しない。未対応値と全画像をCPUで検査してからuploadを始める。device側の失敗時にモデルを成功扱いしないが、標準RHI Update(void)のdriver内部エラー検出やGPU実行の保証を新たに与えるものではない。

## Scalar binding

textureがあるchannelは焼込済み値をそのまま使い、scalarを二重乗算しない。textureがなければ最終scalarを1×1 linear RGBA8 textureへ量子化する。metallic/roughnessが負なら従来の黒/白、AO=1は既定白を使う。
quantizeはround(clamp(v,0,1)×255)。GPU入力の最大量子化差は0.5/255で、CPU floatのbit保持とは分ける。pass所有cacheは同じlevelを共有し、Shutdown/device変更で解放する。作成失敗や非有限scalarは既定値で隠さず、その描画を止める。

## 検証と残件

ImportedOpaqueRuntimeTestはpure検査、mask8通りのR8全mip、1×1cache、FakeDeviceのupload選択、未対応時resource作成ゼロ、モデル解放/2本目作成失敗/Update例外/geometry失敗の寿命、manifest版不一致、sync/workerロードを検証する。MegaGeometryResourcesTestとModelResourcesAssetRuntimeTestの既存回帰も実Windows CPU gateへ加える。
これは実GPU描画の合格ではない。RGB factor、R/G/Bの写像、scalar量子化、sRGB、定数発光nitsを実Vulkan readback/PNGで確認し、既定Indoor/Outdoor goldenと実物の犬の見た目を確認する作業が残る。複数材質・透明・両面・正発光texture・normalScaleの拡張とpacked GPU化はこのprofile外。
