# glTF 材質の source 値

GltfMaterialSource は glTF 材質からPBR係数、5種のtexture参照、発光係数・strength、alpha、両面を読み取る。
この段階では画像を開かず、ARMの定数化・発光単位換算・cooked record生成は行わない。
全成功時だけ出力を置き換える。素材名と素材選択、資産名付き診断は呼出元が保持する。

## 値と既定

- baseColorFactorは4要素の線形値、既定は全て1、metallic/roughnessは既定1。各成分は有限0〜1
- emissiveFactorは有限0〜1の3要素、既定は全て0。KHR_materials_emissive_strengthのemissiveStrengthは有限・0以上、既定1
- normalScaleは有限の符号付き値、既定1。occlusionStrengthは有限0〜1、既定1
- alphaModeはOPAQUE/MASK/BLEND。alphaCutoffは有限0以上で1を超えてもよく、既定0.5。doubleSidedはboolean、既定false

定義は [glTF material schema](https://raw.githubusercontent.com/KhronosGroup/glTF/main/specification/2.0/schema/material.schema.json)、
[PBR schema](https://raw.githubusercontent.com/KhronosGroup/glTF/main/specification/2.0/schema/material.pbrMetallicRoughness.schema.json)、
[normal schema](https://raw.githubusercontent.com/KhronosGroup/glTF/main/specification/2.0/schema/material.normalTextureInfo.schema.json)、
[occlusion schema](https://raw.githubusercontent.com/KhronosGroup/glTF/main/specification/2.0/schema/material.occlusionTextureInfo.schema.json)、
[emissive strength schema](https://raw.githubusercontent.com/KhronosGroup/glTF/main/extensions/2.0/Khronos/KHR_materials_emissive_strength/schema/material.KHR_materials_emissive_strength.schema.json) に基づく。

値はdoubleで保持する。特に極小のemissiveFactorを換算前にfloatへ縮めると、大きなnits/unitで表現できる発光まで0に落ちる。
発光はfactorとstrengthにより決まり、emissiveTextureだけでは発光しない。換算指定の有無は後段のImportEmissionが検査する。
sourceで妥当でもcookedのfloat/放射輝度範囲に収まるとは限らず、後段でも検査が必要。

## 参照と解析範囲

textureInfo.indexは渡されたtextures配列の件数未満でなければならない。
[textureInfo schema](https://raw.githubusercontent.com/KhronosGroup/glTF/main/specification/2.0/schema/textureInfo.schema.json)の既定TEXCOORD_0だけを扱い、他のUV setは拒否する。
textureInfo.extensionsは空objectも含め、存在した時点で旧cookerと同様に拒否する。
元画像のindex/path/byte列の解決や転送色空間の処理は含まない。

読むfieldの重複、不正型・値域、キー内NUL、name内NULを拒否する。
未知のfieldとoptionalな未知材質拡張はfallbackのPBR値を使用する。未知拡張の値もobjectであることは検査する。
これはglTF全体の完全なschema検証ではない。extensionsRequiredの拒否/対応判定はdocument側で別途行う。
既存のdocument profileとcookerは変更していないため、このAPIの追加だけで必須拡張の受理範囲は広がらない。

## 検証境界

純source値の既定・範囲・参照index・double精度からの発光換算を通常/O2/ASan+UBSanで検証した（LSan除外）。
実JsonDocumentを使う読込・重複・不正入力・失敗保持の契約はMEMBER/CTest登録済みだが、Windows.h依存で未実行。
既存cook、manifest、runtime、GPUの動作は未変更であり、GR79の取り込み全体が完了したものではない。
