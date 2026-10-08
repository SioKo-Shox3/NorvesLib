# GR79 共有材質record v1

## 状態

NVMESH v1とNVSKEL v1 MATSで共有する128B recordを、Rendering非依存の純型/codecとして定義する。
現在のNVMESH v0 reader/writerとNVSKEL 0.2の名前slot表へはまだ接続しない。
この追加ではARMの既定方針、emissiveNitsPerUnit、v1 containerの受理やGPU描画を変更しない。

## Wire配置

little-endian、recordは128B。C++構造体のpaddingをmemcpyしない。

| offset | 内容 |
|---|---|
| 0 / 16 / 32 / 48 | Albedo / Normal / Arm / EmissiveのStringRef（u64 offset、u32 byte length、u32 reserved=0） |
| 64 | BaseColor f32×4（線形RGBA） |
| 80 / 92 | EmissiveColor f32×3（Y=1）、EmissiveNits f32 |
| 96 / 100 | Metallic / Roughness f32。負は未指定 |
| 104 / 108 / 112 | OcclusionStrength / NormalScale / AlphaCutoff f32 |
| 116 | Flags u32 |
| 120 / 124 | ShadingModelId u32 / Reserved u32=0 |

Flagsはbit0=DoubleSided、bit1–2=AlphaMode（0 Opaque / 1 Mask / 2 Blend、3拒否）、
bit3=ArmUseAO、bit4=ArmUseRoughness、bit5=ArmUseMetallic。それ以外のbitを拒否する。
ARM参照が空なのにArmUseを立てたrecordは拒否する。文字列を持つが使用bitを持たないrecordは低レベルcodecでは保持する。
不要な参照の除去はcookのcanonical化で行う。

初期profileのShadingModelIdは0=DefaultLitだけを受理する。
これはRendering::ShadingModelの列挙値ではない。現runtimeはUnlit=0/DefaultLit=1なので、変換時に明示写像する。
未知IDをstatic_castして別のshaderへ渡さない。

## 数値と参照の検査

- BaseColorとOcclusionStrengthは有限の0〜1
- Metallic/Roughnessは有限、指定時は0〜1、負の値は未指定。glTF入力の負を認める規則ではない
- NormalScaleは有限。符号付きの値を保持する
- AlphaCutoffは有限の非負。1を超える値も保持する
- 発光0 nitsはRGBも0。正のnitsは非負RGBかつY=0.2126R+0.7152G+0.0722Bが1±1e-4
- 発光は保存RGB×nitsと、runtimeでY再正規化したRGB/Y×nitsの両方が65504未満。Yの許容誤差内でも再正規化後に上限へ達する値は拒否し、保存値自体は変更しない
- StringRefはstring節内のbyte境界を差分で検査し、offset+lengthのoverflowを避ける
- StringRef/recordの予約領域は0必須。string内容・論理path・texture format/色空間はcontainer/cook接続側が別途検査する

readは128Bちょうど、writeは出力の先頭128Bのみを使用する。全入力検証後だけoutを変更し、失敗時は出力全体を保持する。
入出力の領域重複は拒否する。floatは有効値のbit列を保存し、暗黙のclamp/色変換/正規化を行わない。
構造体の中立な初期値は、import時のARMやnits換算の既定方針を採用した意味ではない。

## glTF規約との対応

BaseColor/metallic/roughnessの入力範囲は[公式PBR schema](https://raw.githubusercontent.com/KhronosGroup/glTF/main/specification/2.0/schema/material.pbrMetallicRoughness.schema.json)に従う。
[公式material schema](https://raw.githubusercontent.com/KhronosGroup/glTF/main/specification/2.0/schema/material.schema.json)はAlphaCutoffの1超過も定義しており、MASKでは全体が透明になる。
[normal texture schema](https://raw.githubusercontent.com/KhronosGroup/glTF/main/specification/2.0/schema/material.normalTextureInfo.schema.json)のscaleには非負制限がないため、wireで負を勝手に拒否しない。

後続のARM cookでは、AO strengthをtextureへ単純乗算しない。
[occlusion schema](https://raw.githubusercontent.com/KhronosGroup/glTF/main/specification/2.0/schema/material.occlusionTextureInfo.schema.json)の式は1 + strength × (texture − 1)。
Metallic/Roughnessのfactor×textureとは別であり、ignore/constant/autoの既定方針が決まることとは独立した仕様条件である。

## 検証と残作業

CookedMaterialFormatTestに独立Python struct.pack由来の128B golden、値/flags/予約/参照境界、
負の未指定値・1超過cutoff・負normalScale・NaN/Inf・発光の正規化/上限・失敗保持・alias拒否を登録する。
通常/O2-NDEBUG/ASan・UBSan（LSan除外）と実MEMBER wrapperを検証し、CTestにも登録する。

NVMESH v1 container/cluster128B、v0昇格、glTF→値の写像、ARM処理、runtime材質への変換、NVSKEL v1への接続は残る。
この純codecの合格は、Windows/Core全体・既定画面・実物GLB・GPUの受入れを意味しない。
