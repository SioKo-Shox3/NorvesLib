# GR79の所有CPU材質staging

cook済みNVMESH v1の材質をModelMaterialStagingのadapterで検査し、描画に渡す前の所有値へ変換する。
この段階は材質値の保存まで。製品ModelAssetLoaderのv1拒否を維持し、ImportedMaterialがあるModelStagingDataをFinalizeへ渡しても明示拒否する。

- Layoutで従来の分割ARM経路と未接続のpacked ARM v1を区別する
- RGBA baseColor、canonical emissive色とnits、metallic/roughness/AO、normalScale、alphaCutoffを再計算せず保持する
- negative scalar sentinel、負normalScale、1を超えるcutoffもwireの許容どおり保持する
- ARMの3bit mask、alpha3モード、両面を明示写像する。wire DefaultLit=0をruntime Unlit=0へ直接castしない
- Albedo/Normal/ARM/Emissiveの4論理pathはUTF-8の所有文字列。元blobの寿命に依存しない
- 共有codecで数値・flagsを検査し、公開CookedMeshDataを手組みしても参照の2つの表現・blob境界・正規化済み論理path・UTF-8を確認する
- 失敗時は出力を変更しない。CPU helperはtextureの読込/uploadやGPU作成を行わない

ImportedMaterialStagingTestは値のbit保持、mask8×alpha3×両面2、所有/コピー、拒否と出力保持を検証する。GltfMaterialCookV1Testでは実cook→reader→adapterの接続を確認する。
次段階でpacked ARMを1枚の全mipとしてuploadし、shaderがmaskに応じたchannel/scalarを読む。既存v0のR8分割互換は別に維持する。GPU描画・alpha/両面/影・透明経路の合格はこのCPU試験で代替しない。

範囲注意: 現在のNVMESH v1 readerはstring tableをASCII限定で検査する。UTF-8の所有保証は手組みCookedMeshDataからadapterまでで、Unicode cooked blobの製品ロード対応を意味しない。Finalizeはreleaseでも取得できるModelFinalizeStatusで未対応材質の早期拒否をresource失敗から区別する。
