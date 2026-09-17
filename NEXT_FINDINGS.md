# NEXT_FINDINGS — R2-P1評価

## R2-P1-001

- status: resolved
- source: R2-P1読み取り専用評価
- finding: CPU参照が太陽ディスクの放射輝度を立体角積分せず、そのまま散乱源へ掛けていたため、後続Hillaire 2020系LUTの照度単位と整合しなかった。
- fix: 可視太陽ディスク立体角を散乱源へ適用し、平行大気・単一散乱・float精度というP1参照の適用範囲と、球殻/多重散乱/地表反射を後続LUTへ委ねる契約をヘッダへ明記する。
- verify: `SkyAtmosphereModelTest` の再ビルド・CTest、差分の再評価。
- resolution: `76a0c15` で修正し、2周目評価はPASS。P1の参照範囲・太陽角半径・R1 EV規約を記録済み。

## R2-P2-001

- status: resolved
- source: R2-P2 1周目読み取り専用評価
- finding: `Assets/Shaders/sky_atmosphere.frag` に`#version`宣言がなく、エンジンのシェーダー読み込み時にコンパイルできない経路があった。
- fix: Vulkan向け`#version 450`を追加し、エンジンと同じBOM除去後入力を直接`glslc`へ渡す検証を追加した。
- verify: `SkyAtmospherePassContractTest`、対象3テストのCTest、sky/lighting両シェーダーのBOM除去後`glslc`コンパイル。
- resolution: `53a58dc`で修正し、2周目評価はPASS。

## R2-P2-002

- status: resolved
- source: R2-P2 1周目読み取り専用評価
- finding: 256x128放射輝度LUTのαマスクをテクセルサンプルとして太陽ディスク判定に使うと、太陽ディスクが解像度依存で欠落する可能性があった。
- fix: 空スナップショットから太陽方向と積分した角半径のcos値をLightingParamsへ渡し、`dot(rayDir, sunDirection)`による解析的マスクへ変更した。
- verify: `LightingParamsLayoutTest`のstd140サイズ/オフセット検査、`SkyAtmospherePassContractTest`、sky/lightingシェーダーのBOM除去後`glslc`コンパイル。
- resolution: `53a58dc`で修正し、2周目評価はPASS。
