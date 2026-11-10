# GPU の割り当て表

GPU に渡す構造体・GBuffer のチャネル・binding を追加する、または意味を変えるときは、先にこの表へ登録してから使う。決め打ちの値（`0.25` の閾値など）でチャネルに意味を足さない。GR27 以降の材質・シェーディングモデルもこの約束に従う。

GBuffer の空きチャネルの意味は `Library/Core/Public/Rendering/ShadingModelIds.h` と `Assets/Shaders/Common/GBufferEncoding.glsl` の定数表・関数が正本である。`ShadingModelContractTest` は両者の一致と、`Assets/Shaders` 内の直接の読み書きが符号化・復号関数を通ることを確かめる契約を担う。

オフセットは先頭からのバイト数、範囲の両端は含む。`float` は 4 バイト、`vec4` は 16 バイト、`mat4` は 64 バイトとして、C++ の配列宣言と GLSL の並びを照合している。「空き」は当該経路で意味を割り当てていない成分であり、登録なしで転用してよいという意味ではない。

## 1. GBuffer

### 添付とチャネル

形式は `Library/Core/Public/Rendering/GBufferPass.h` の `GBufferPassSettings` の既定値。実際の生成は `Library/Core/Private/Rendering/GBufferPass.cpp` の `m_Settings.*Format` に従うため、設定を変更する場合も本表を更新する。カラーの番号はシェーダーの `location` に対応する。

| 添付 | 番号 | 既定の形式 | R | G | B | A |
|---|---|---|---|---|---|---|---|
| Albedo | 0 | `R8G8B8A8_UNORM` | 線形アルベド R | 線形アルベド G | 線形アルベド B | アルファ。空画素の判定にも使う |
| Normal | 1 | `R16G16B16A16_FLOAT` | ワールド法線 X | ワールド法線 Y | ワールド法線 Z | シェーディングモデル ID と受け手の印を詰めた整数 |
| Material | 2 | `R8G8B8A8_UNORM` | 金属度 | 粗さ | AO | 材質値 `P0` |
| Emissive | 3 | `R16G16B16A16_FLOAT` | プリエクスポージャ後の発光 R | 同 G | 同 B | 材質値 `P1` |
| Velocity | 4 | `R16G16_FLOAT` | 現在の UV − 前の UV の X | 同 Y | 成分なし | 成分なし |
| Depth | 深度 | `D32_FLOAT` | 深度（標本の `.r`） | 成分なし | 成分なし | 成分なし |

`Material.a` と `Emissive.a` は意味のない余白ではなく、登録済みの `P0` / `P1` の枠である。`P0` は UNORM 8 bit、`P1` は半精度 float の制約を受ける。

書き手は `gbuffer.frag`、`megageometry.frag`、`foliage.frag`、`Common/VisibilityResolve.glsl`。それぞれ `GBufferEncodeNormal` / `GBufferEncodeMaterial` / `GBufferEncodeEmissive` を使う。通常のメッシュ・MegaGeometry・可視バッファの解決は材質の ID と `P0` / `P1` を書く。`foliage.frag` は `DefaultLit` と `P0 = P1 = 0` を書く。MegaGeometry のデバッグ表示や、可視バッファで材質を取得できない場合も既定値を書く。`Velocity` はクリップ座標の差から `(currentNdc - previousNdc) * 0.5` で求め、履歴が使えない場合は `0` とする。

### 材質値の優先と ID

`ShadingParams` は x = `P0`、y = `P1`、z = 予約、w = 被覆率の乗数（1 で不変）。各成分は `MeshComponent::SetShadingParams` / `SkinnedMeshComponent::SetShadingParams` のインスタンス値が 0 以上ならそれを使い、負なら `MaterialResourceData::ShadingParams` / `MegaMeshMaterial::ShadingParams` の材質値を使う。両方とも負なら `P0 = P1 = 0`、z = 0、w = 1。未指定の初期値は各成分 `-1` である。GLSL の解決は `Common/GBufferEncoding.glsl::ResolveShadingParams` に従う。スキニングのインスタンス値は CPU で材質値に重ねる。現行の MegaGeometry はインスタンス値を渡さず、材質値を使う。

CPU の `ShadingModel` から GBuffer の ID への変換は `MaterialTypes.h::ToGBufferShadingModelId` だけが担う。`Fur`・`Eye`・`PaleFlat` を対応する ID に、それ以外を `DefaultLit` に写す。

### Normal.w の符号化

`packed = ((receiverFlags & 15u) << 4u) | (shadingModelId & 15u)`。下位 4 bit（bit 0〜3）が ID、上位 4 bit（bit 4〜7）が印。ビット列の再解釈ではなく、整数 `0〜255` を `float(packed)` として書く。これらの整数は RGBA16F に正確に格納できる。C++ は `GBufferContract::PackNormalW`、GLSL は `GBufferEncodeNormal` を使う。

復号は `GBufferDecodeNormal` が `uint(clamp(round(normalSample.w), 0.0, 2047.0))` を作り、ID と印を各マスクで取り出す。`2047` は復号関数のクランプ上限であり、bit 8 以上に追加の意味は登録されていない。返す法線 xyz はこの関数では正規化しない。ID だけ、印だけを読む場合も `GBufferDecodeShadingModelId` / `GBufferDecodeReceiverFlags` / `GBufferHasReceiverFlag` を使う。

| ID | C++ 定数（`GBufferContract`） | GLSL 定数 | 意味 |
|---|---|---|---|
| 0 | `ShadingModelDefaultLit` | `GBUFFER_SHADING_MODEL_DEFAULT_LIT` | 標準のライティング |
| 1 | `ShadingModelFur` | `GBUFFER_SHADING_MODEL_FUR` | 毛皮 |
| 2 | `ShadingModelEye` | `GBUFFER_SHADING_MODEL_EYE` | 目 |
| 3 | `ShadingModelPaleFlat` | `GBUFFER_SHADING_MODEL_PALE_FLAT` | 淡い平坦な面 |
| 4〜15 | 予約 | 予約 | 未割当 |

印の値はシフト前のビットマスク。複数を OR で併用できる。

| 値 | Normal.w 内のビット / 値 | C++ 定数 | GLSL 定数 | 意味 |
|---|---|---|---|---|
| 1 | bit 4 / 16 | `ReceiverFlagTerrain` | `GBUFFER_RECEIVER_FLAG_TERRAIN` | 地形の受け手 |
| 2 | bit 5 / 32 | `ReceiverFlagTightTerrainShadow` | `GBUFFER_RECEIVER_FLAG_TIGHT_TERRAIN_SHADOW` | 影を起伏に詰める地形。`TERRAIN` と併用 |
| 4 | bit 6 / 64 | `ReceiverFlagFoliageClump` | `GBUFFER_RECEIVER_FLAG_FOLIAGE_CLUMP` | 草・低木の株の受け手 |
| 8 | bit 7 / 128 | `ReceiverFlagDiffuseOnly` | `GBUFFER_RECEIVER_FLAG_DIFFUSE_ONLY` | 鏡面のない材質 |

`forward_transparent.frag` の半透明とパストレーサーはこの ID を読まず、`DefaultLit` として扱う。

### 空画素とデカール

`GBufferPass.cpp` のカラー添付のクリア値は全成分 `0`、深度は `1`。`lighting.frag` の通常の背景分岐は `albedoSample.a < 0.01` を空画素として空を描く。クリア済みの `Normal.w = 0` は復号すれば `DefaultLit`・印なしだが、それだけで面の存在を判定しない。空画素の法線 xyz もゼロであり、有効な面の単位法線として扱わない。

デカールは Normal への描画前に `DecalPass.cpp::RecordDecals` で Normal を別画像へコピーして印を読む。同じ画像を添付と標本に同時に使わないためであり、法線合成の A は重みとして出力しても書き込みマスクで元の `Normal.w` を保護する（`GBufferEncodeDecalNormalBlend`）。

## 2. GBuffer / MegaGeometry の UBO

両者とも `set = 0, binding = 0` の `MVPData` に対応する。標準メッシュと MegaGeometry のワールド変換は UBO ではなく各経路のインスタンス表から、スキニングのワールド変換は別のバッファから読む。以下の UBO のオフセットは、全メンバが `float` 配列である宣言順から積算した値である。フィールドごとの `offsetof` の `static_assert` は存在せず、大きさの検査と区別する。

### GBuffer

C++ は `Library/Core/Private/Rendering/GBufferPass.cpp::GBufferPass::Execute` 内の `PerObjectUBO`。`static_assert(sizeof(PerObjectUBO) == 368u)` と初期化時の `UBO_SIZE = 368` がある。空きの末尾領域はない。

GLSL は `Assets/Shaders/gbuffer.frag` の `MVPData` が全 368 バイト、`Assets/Shaders/gbuffer.vert` と `Assets/Shaders/skinned_gbuffer.vert` の同名宣言が先頭 320 バイト（`frameParams` まで）を持つ。

| オフセット | C++ フィールド / 型 | GLSL フィールド / 型 | 意味（ベクトルは x / y / z / w 順） |
|---|---|---|---|
| 0 | `view: float[16]` | `view: mat4` | 現在のビュー行列 |
| 64 | `projection: float[16]` | `projection: mat4` | 現在の投影行列 |
| 128 | `previousView: float[16]` | `previousView: mat4` | 前のビュー行列 |
| 192 | `previousProjection: float[16]` | `previousProjection: mat4` | 前の投影行列 |
| 256 | `cameraPosition: float[4]` | `cameraPosition: vec4` | xyz = カメラのワールド位置、w = 空き（シェーダー未使用） |
| 272 | `emissiveChromaticityAndLuminanceNits: float[4]` | `emissiveChromaticityAndLuminanceNits: vec4` | xyz = Y=1 の発光色度、w = 発光輝度（nits） |
| 288 | `pomParams: float[4]` | `pomParams: vec4` | 高さ倍率 / 高さマップあり（1/0）/ ORM を `metallicTexture` 枠に張る（1/0）/ 法線が 2 チャンネル（1/0） |
| 304 | `frameParams: float[4]` | `frameParams: vec4` | 前のカメラ履歴あり（1/0）/ 発光のプリエクスポージャ / sparse（VT）テクスチャあり（1/0）/ アルベドの VT フィードバックパラメータ |
| 320 | `vtFeedbackParams: float[4]` | `vtFeedbackParams: vec4`（断片側のみ） | 法線の VT パラメータ / ORM の VT パラメータ / 高さの VT パラメータ / 鏡面のない材質（1/0） |
| 336 | `shadingParams: float[4]` | `shadingParams: vec4`（断片側のみ） | 材質の `P0` / `P1` / 予約 / 被覆率の乗数。スキニングはインスタンス値を重ねた値 |
| 352 | `shadingModel: float[4]` | `shadingModel: vec4`（断片側のみ） | x = GBuffer のシェーディングモデル ID、yzw = 0 |

`vtFeedbackParams.w`（332 バイト）は `gbuffer.frag` が `> 0.5` を判定し、`Normal.w` の `DIFFUSE_ONLY` の印へ変換する。`Material.a` の意味には使わない。

VT パラメータの `0` は要求を書かない指定。整数を float に格納し、`Common/VirtualTextureFeedback.glsl::DecodeVirtualTextureFeedbackParam` で戻す。下位からタイル幅の log2（4 bit）、高さの log2（4 bit）、巡回位相（4 bit）、テクスチャ番号 + 1（12 bit）である。

### MegaGeometry

C++ は `Library/Core/Private/Rendering/MegaGeometryPass.cpp` の `SectionUniformData`。名称は per-object 相当でも、実際の共有単位は材質の区間。`TerrainBlendShading.h` の `TERRAIN_BLEND_PARAM_FLOATS = 40u` を含め、宣言された配列の合計は 136 float = **544 バイト**。検査は `static_assert(sizeof(SectionUniformData) <= 768u)`、確保枠は `SectionUniformBufferBytes = 768`。更新・binding の範囲は `sizeof(SectionUniformData)` であり、確保枠の末尾 544〜767 バイト（224 バイト）は空きで、現在の UBO 宣言には含まれない。

GLSL は `Assets/Shaders/megageometry.frag` の `MVPData` が全 544 バイト、`Assets/Shaders/megageometry.vert` は先頭 336 バイト（`frameParams` まで）。C++ の `EmissiveChromaticityAndLuminanceNits` に対応する GLSL 名は `emissiveColor`。

| オフセット | C++ フィールド / 型 | GLSL フィールド / 型 | 意味（x / y / z / w 順） |
|---|---|---|---|
| 0 | `View: float[16]` | `view: mat4` | 現在のビュー行列 |
| 64 | `Projection: float[16]` | `projection: mat4` | 現在の投影行列 |
| 128 | `CameraPosition: float[4]` | `cameraPosition: vec4` | xyz = カメラのワールド位置、w = 空き（シェーダー未使用） |
| 144 | `ObjectColor: float[4]` | `objectColor: vec4` | 材質の `BaseColor` RGBA。現シェーダーが使うのは rgb |
| 160 | `EmissiveChromaticityAndLuminanceNits: float[4]` | `emissiveColor: vec4` | xyz = Y=1 の発光色度、w = 発光輝度（nits） |
| 176 | `PomParams: float[4]` | `pomParams: vec4` | 高さ倍率 / 高さマップあり / デバッグ表示の番号 / デバッグ payload 対応（1/0） |
| 192 | `PreviousView: float[16]` | `previousView: mat4` | 前のビュー行列 |
| 256 | `PreviousProjection: float[16]` | `previousProjection: mat4` | 前の投影行列 |
| 320 | `FrameParams: float[4]` | `frameParams: vec4` | 前のカメラあり / 発光のプリエクスポージャ / 変位の頂点の UV 間隔（0 は変位なし）/ payload が LOD の段（1/0） |
| 336 | `MaterialParams: float[4]` | `materialParams: vec4` | ORM を `metallicTexture` 枠に張る / 2 チャンネル法線 / sparse（VT）テクスチャあり / アルベドの VT パラメータ |
| 352 | `VtFeedbackParams: float[4]` | `vtFeedbackParams: vec4` | 法線の VT パラメータ / ORM の VT パラメータ / 高さの VT パラメータ / GBuffer のシェーディングモデル ID |
| 368 | `TerrainParams: float[40]` | `terrainParams: vec4[10]` | 下表の地形 3 層のパラメータ |
| 528 | `ShadingParams: float[4]` | `shadingParams: vec4` | 材質の `P0` / `P1` / 予約 / 被覆率の乗数 |

MegaGeometry の `vtFeedbackParams.w` は `MegaMeshMaterial::Shading` から変換した ID であり、GBuffer の同名成分の `DIFFUSE_ONLY` 入力とは異なる。`ShadingParams` には `MegaMeshMaterial::ShadingParams` を写す。`megageometry.frag` は地形の場合に `TERRAIN` の印を書く。

地形の詳細は `Library/Core/Private/Rendering/TerrainBlendShading.h::PackTerrainBlendParams` と `Assets/Shaders/Common/TerrainLayerBlend.glsl::TerrainBlendParams` が対応する。各行の型は C++ `float[4]`、GLSL `vec4`。

| オフセット | 配列要素 / 対応名 | x | y | z | w |
|---|---|---|---|---|---|
| 368 | `terrainParams[0] / slope` | 土の傾斜開始（rad） | 土の傾斜終了（rad） | 岩の傾斜開始（rad） | 岩の傾斜終了（rad） |
| 384 | `terrainParams[1] / height` | 土への寄せの高さ開始 | 同終了 | 高さによる傾斜の偏り（rad） | 混ぜの深さ |
| 400 | `terrainParams[2] / noise` | 雑音の波長 0 の逆数 | 波長 1 の逆数 | 傾斜の雑音振幅（rad） | 砂の埋め高さ |
| 416 | `terrainParams[3] / layers` | 3 層混ぜ有効（1/0） | 土の UV 倍率 | 岩の UV 倍率 | 法線・アルベドの復号のビットマスク |
| 432 | `terrainParams[4] / macro` | 大きな模様の波長 0 の逆数 | 波長 1 の逆数 | 明るさの揺らぎ | 色味の揺らぎ |
| 448 | `terrainParams[5] / detail` | UV の段を変え始める距離 | 距離比の log2 | 最大の段 | テクスチャ 1 枚の広さ（m） |
| 464 | `terrainParams[6] / mask` | 地表の地図の最小 X | 最小 Z | X の広さの逆数 | Z の広さの逆数 |
| 480 | `terrainParams[7] / maskMix` | 礫を土へ回す割合 | 外縁の帯の幅（m） | 引く位置のずらし幅（m） | 確認用の色分け（1/0） |
| 496 | `terrainParams[8] / farMap` | 遠景の地図の最小 X | 最小 Z | X の広さの逆数 | Z の広さの逆数 |
| 512 | `terrainParams[9] / farMix` | 遠景の 1 画素の広さ（m） | 砂の法線の傾きの分散 | 土の同分散 | 岩の同分散 |

`layers.w` の印は `DIRT_NORMAL_TWO_CHANNEL = 1`、`ROCK_NORMAL_TWO_CHANNEL = 2`、`SAND_ALBEDO_SRGB_DECODE = 4`、`DIRT_ALBEDO_SRGB_DECODE = 8`、`ROCK_ALBEDO_SRGB_DECODE = 16`（接頭辞はいずれも `TERRAIN_BLEND_`）。`layers.x = 0` は 1 層、`mask.z = 0` / `farMap.z = 0` は対応する地図なし。

## 3. GPUSceneInstanceData

定義は `Library/Core/Public/Rendering/MeshTypes.h` の `GPUSceneInstanceData`。`alignas(16)`、56 float = **224 バイト**。大きさ・整列・各フィールドのオフセットは同ファイルの `static_assert` で固定されている。GLSL は `std430` の `InstanceData` として読む。

### 標準メッシュの 56 float

`Library/Core/Private/Rendering/DrawCommand.cpp::FillGPUSceneInstanceData` が詰める。行列の配列要素 `4*c+r` は GLSL の `[c][r]` に対応する。

| float の番号 | バイト位置 | C++ 成分 | GLSL 成分 / 型 | 割り当て |
|---|---|---|---|---|
| 0〜15 | 0〜63 | `World[0..15]` | `world: mat4` | 現在のワールド変換 |
| 16〜31 | 64〜127 | `PreviousWorld[0..15]` | `previousWorld: mat4` | 前のワールド変換 |
| 32〜34 | 128〜139 | `NormalMatrix[0..2]` | `normalRows[0].xyz: vec3` | 法線変換の行 0 |
| 35 | 140〜143 | `NormalMatrix[3]` | `normalRows[0].w: float` | 空き（0） |
| 36〜38 | 144〜155 | `NormalMatrix[4..6]` | `normalRows[1].xyz: vec3` | 法線変換の行 1 |
| 39 | 156〜159 | `NormalMatrix[7]` | `normalRows[1].w: float` | 空き（0） |
| 40〜42 | 160〜171 | `NormalMatrix[8..10]` | `normalRows[2].xyz: vec3` | 法線変換の行 2 |
| 43 | 172〜175 | `NormalMatrix[11]` | `normalRows[2].w: float` | 空き（0） |
| 44〜47 | 176〜191 | `ObjectColor[0..3]` | `objectColor: vec4` | オブジェクトカラー RGBA。入力 `customData[0..3]` の各成分が 0 ならその成分を 1 にする。入力なしは全て 1 |
| 48〜51 | 192〜207 | `CustomData[0..3]` | `customData: vec4` | 入力 `customData[0..3]` の原値。入力なしは全て 0。標準メッシュの現シェーダーでは未使用だが、CPU のカスタム値の保持先 |
| 52〜55 | 208〜223 | `ShadingParams[0..3]` | `shadingParams: vec4` | インスタンスの `P0` / `P1` / 予約 / 被覆率の乗数。入力なしは全て -1 |

法線の 3 行は `Math/MatrixUtils.h::CopyUpper3x3ToShaderData` が各行の w を 0 にする。シェーダーは各行 xyz に入力法線の x / y / z を掛けて足し、正規化する。

### 板・インポスターでの割り当て

同じ構造体を `CanvasView.cpp::FillBoardInstanceData` と `SceneView.cpp::FillWorldBoardInstanceData` も使う。標準メッシュで空いている成分も、この用途では使用済みである。

| float の番号 | バイト位置 | GLSL 成分 | 板・インポスターの意味 |
|---|---|---|---|
| 0〜15 | 0〜63 | `world` | 現在のワールド変換。2D 板は列 0・1 の xy を軸、列 3 の xy を原点として読む。ワールド板・インポスターは列 3 の xyz が原点 |
| 16〜31 | 64〜127 | `previousWorld` | 前のワールド変換を保持。板の現シェーダーは読まない |
| 32〜33 | 128〜135 | `normalRows[0].xy` | 幅・高さ。2D は px、ワールド板・インポスターはワールド単位 |
| 34〜35 | 136〜143 | `normalRows[0].zw` | ピボット X・Y |
| 36〜37 | 144〜151 | `normalRows[1].xy` | X・Y 反転（1/0） |
| 38〜39 | 152〜159 | `normalRows[1].zw` | UV 矩形の原点 X・Y |
| 40〜41 | 160〜167 | `normalRows[2].xy` | UV 矩形の幅・高さ。インポスターは読まない |
| 42〜43 | 168〜175 | `normalRows[2].zw` | インポスターのセル数 X・Y。2D 板では空き（0）、通常のワールド板は読まない |
| 44〜47 | 176〜191 | `objectColor` | Tint RGBA。不透明の板は A = 1 |
| 48 | 192〜195 | `customData.x` | 2D 板はビューポート幅。ワールド側はインポスターのセル解像度 |
| 49 | 196〜199 | `customData.y` | 2D 板はビューポート高さ。ワールド側はアトラス幅 |
| 50 | 200〜203 | `customData.z` | 2D 板では空き（0）。ワールド側はアトラス高さ |
| 51 | 204〜207 | `customData.w` | 2D 板では空き（0）。ワールド側は `LODSwitchDistance`。インポスターの頂点側が転送するが、現断片側では読まない |
| 52〜55 | 208〜223 | `shadingParams` | 板・インポスターのシェーダーは読まない。詰める側は全成分を -1 にする |

### 読むシェーダー

いずれも `Assets/Shaders/` 配下。`InstanceData` の宣言は `Common/SceneInstanceData.glsl` の 1 か所にあり、下記の読み手が include する。

| ファイル | set / binding | 実際に読む主なフィールド |
|---|---|---|
| `gbuffer.vert` | 0 / 7 | `world`, `previousWorld`, `normalRows[].xyz`, `objectColor.rgb`, `shadingParams` |
| `forward_transparent.vert` | 0 / 7 | `world`, `normalRows[].xyz`, `objectColor` |
| `shadow.vert` | 0 / 7 | `world`（インスタンス表を使う分岐） |
| `point_shadow.vert` | 0 / 7 | `world`（インスタンス表を使う分岐） |
| `visbuffer_mesh.vert` | 0 / 1 | `world` |
| `Common/VisibilityResolve.glsl` | 0 / 5 | `world`, `previousWorld`, `objectColor.rgb`, `shadingParams` |
| `board2d.vert` | 0 / 7 | `world`, 板用の `normalRows`, `objectColor`, `customData.xy` |
| `world_board.vert` | 0 / 7 | `world[3]`, 板用の `normalRows`, `objectColor` |
| `impostor.vert` | 0 / 7 | `world[3]`, 板・アトラス用の `normalRows`, `objectColor`, `customData` |

MegaGeometry の `Common/MegaInstanceTable.glsl` は別の構造体を読む。この 56 float の表と混同しない。

## 4. lighting.frag の binding

対象は `Assets/Shaders/lighting.frag` の descriptor set 0。C++ の配置定義は `Library/Core/Private/Rendering/LightingPass.cpp::CreateLightingDescriptorSetDesc`、実際の束ねは `LightingPass::ExecuteWithInputs` の `lightingDescriptorSet`。画像には同じ番号で `BindSampler` も行う。下表の C++ 呼び出しはいずれもこの変数に対するもの。

| set | binding | GLSL 型 | GLSL 名 | C++ 側で束ねる箇所・資源 |
|---|---|---|---|---|
| 0 | 0 | `sampler2D` | `gbufferAlbedo` | `BindTexture(0, albedoTexture)` |
| 0 | 1 | `sampler2D` | `gbufferNormal` | `BindTexture(1, normalTexture)` |
| 0 | 2 | `sampler2D` | `gbufferMaterial` | `BindTexture(2, materialTexture)` |
| 0 | 3 | `sampler2D` | `gbufferDepth` | `BindTexture(3, depthTexture)` |
| 0 | 4 | `uniform LightingParams`（`std140`） | `params` | `BindConstantBuffer(4, lightingParamsUpload.Buffer, ...)` |
| 0 | 5 | `readonly buffer LightBuffer`（`std430`, `LightData[]`） | `lightBuffer.lights` | `BindStorageBuffer(5, lightArrayUpload.Buffer, ...)` |
| 0 | 6 | `sampler2DArray` | `shadowMap` | `BindTexture(6, ...)`。公開済み CSM、または既定の配列 |
| 0 | 7 | `sampler2D` | `gbufferEmissive` | `BindTexture(7, emissiveTexture)`。未取得時は `albedoTexture` |
| 0 | 8 | `sampler2D` | `envMap` | `BindTexture(8, environmentTexture)`。空・静的 HDR・検証用・既定黒から選択 |
| 0 | 9 | `sampler2D` | `brdfLUT` | `BindTexture(9, m_BrdfLutTexture)` |
| 0 | 10 | `sampler2D` | `ssaoTexture` | `BindTexture(10, ssaoTexture)`。未取得時は `albedoTexture` |
| 0 | 11 | `readonly buffer NeuralBRDFWeights`（`float[]`） | `neuralBRDF.data` | `BindStorageBuffer(11, ...)`。重み、または既定の重みバッファ |
| 0 | 12 | `sampler2D` | `diffuseIrradiance` | `BindTexture(12, diffuseIrradianceTexture)` |
| 0 | 13 | `sampler2D` | `prefilteredSpecular` | `BindTexture(13, prefilteredSpecularTexture)` |
| 0 | 14 | `sampler2D` | `skySunDisk` | `BindTexture(14, ...)`。空の太陽ディスク、または既定黒 |
| 0 | 15 | `sampler2D` | `skyTransmittance` | `BindTexture(15, ...)`。空の透過率、または既定黒 |
| 0 | 16 | `sampler2D` | `rayTracingShadowVisibility` | `BindTexture(16, ...)`。公開済み RT 影、または既定黒 |
| 0 | 17 | `sampler2DArray` | `ddgiIrradianceAtlas` | `BindTexture(17, ...)`。DDGI 照度アトラス、または既定アトラス |
| 0 | 18 | `sampler2DArray` | `ddgiDistanceAtlas` | `BindTexture(18, ...)`。DDGI 距離アトラス、または既定アトラス |
| 0 | 19 | `sampler2D` | `rtgiDiffuseIndirect` | `BindTexture(19, ...)`。RTGI の拡散間接光、または既定黒 |
| 0 | 20 | `samplerCubeArray` | `pointShadowCubes` | `BindTexture(20, ...)`。点光源の影キューブ、または既定キューブ |
| 0 | 21 | `uniform VsmSampleBlock`（`std140`, `VsmSampleParams`） | `vsmBlock.vsm` | `BindConstantBuffer(21, vsmSampleUpload.Buffer, ...)` |
| 0 | 22 | `readonly buffer VsmPageTableBuffer`（`std430`, `uint[]`） | `vsmPageTable` | `BindStorageBuffer(22, boundVsmPageTable, ...)` |
| 0 | 23 | `readonly buffer VsmPoolBuffer`（`std430`, `uint[]`） | `vsmPool` | `BindStorageBuffer(23, boundVsmPool, ...)` |
| 0 | 24 | `buffer VsmLightingStatsBuffer`（`std430`, `uint[]`） | `vsmLightingStats` | `BindStorageBuffer(24, boundVsmStats, ...)`。`NORVES_VSM_STATS` 有効時だけ GLSL 宣言あり |
| 0 | 25 | `readonly buffer VsmSliceBuffer`（`std430`, `VsmSlice[]`） | `vsmSlices` | `BindStorageBuffer(25, vsmSliceUpload.Buffer, ...)` |
| 0 | 26 | `uniform VsmPointSampleBlock`（`std140`, `VsmPointSampleParams`） | `vsmPointBlock.point` | `BindConstantBuffer(26, vsmPointSampleUpload.Buffer, ...)` |
| 0 | 27 | `sampler2D` | `rtgiSpecularHit` | `BindTexture(27, ...)`。このフレームの RTGI 履歴 `Depth`、または既定黒 |
| 0 | 28 | `sampler2D` | `rtgiSpecularSky` | `BindTexture(28, ...)`。このフレームの RTGI 履歴 `Normal`、または既定黒 |
| 0 | 29 | `sampler2DArray` | `skyOcclusionVisibility` | `BindTexture(29, ...)`。公開済み Visibility、またはゼロの既定配列 |
| 0 | 30 | `sampler2DArray` | `skyOcclusionAux` | `BindTexture(30, ...)`。公開済み Aux、またはゼロの既定配列 |
| 0 | 31 以降 | 未割当 | 空き | このレイアウトには宣言なし |

0〜30 に空き番号はない。25 は `#define VSM_SLICE_BINDING 25` と `Common/VirtualShadowMapSlice.glsl` の include により宣言される。24 も機能が無効な場合に転用できる空きではなく、C++ のレイアウトで常に確保されている。set 1 以降はこのシェーダーに宣言がない。

C++ の画像型は `CombinedImageSampler`、UBO は `ConstantBuffer`、読み取り専用 SSBO は `StructuredBuffer`、24 は `RWBuffer`。27・28 の履歴資源名は `Depth` / `Normal` だが、ここでは鏡面の蓄積値として使用する。`rtgiSpecularHit.gba` は当たった面が返す露出後の光の平均、`rtgiSpecularSky.a` は空へ抜けた割合である。

### GPULightingParams の空の遮蔽

C++ は `LightingPassGpuTypes.h`、GLSL は `LightingParams` の末尾。全体は **1072 バイト**。既存の `depthLinearization` までの配置を保つ。公開が無いフレームと検証表示では、追加欄をすべてゼロにする。

| オフセット | C++ フィールド / 型 | GLSL 型 | 意味 |
|---|---|---|---|
| 944 | `skyOcclusionInfo: uint32_t[4]` | `uvec4` | 有効（1/0）/ 段の数 / 解像度 / 0 |
| 960 | `skyOcclusionParams: float[4]` | `vec4` | Strength / DiffuseExponent / GroundHeightMeters / 0 |
| 976 | `skyOcclusionBounce: float[4]` | `vec4` | rgb = OccluderBounceColor × OccluderBounceIntensity、w = 0 |
| 992 | `skyOcclusionWindowOrigin: int32_t[4][4]` | `ivec4[4]` | 段ごとの xy = 窓の最小ワールドテクセル（X/Z）、zw = 0 |
| 1056 | `skyOcclusionTexelMeters: float[4]` | `vec4` | 段ごとのテクセル幅（m）。使わない段は 0 |

既定の配列は初期化時に作るゼロの DDGI 照度配列と共有する。現行 RHI は層数 1 を 2D ビューとして作るため、既定は配列ビューを持つ複数層である。無効時はテクスチャを参照しない。

### 空の遮蔽クリップマップのチャネル

各画像は R×R の 2D 配列で、層 k が段 k。テクセル幅は `BaseTexelMeters × 4^k`。窓の最小ワールドテクセルを (ox, oz) とし、[ox, ox+R) × [oz, oz+R) を覆う。中心は ((wx+0.5)·s, (wz+0.5)·s)、画像の番地は (wx mod R, wz mod R) の正の剰余である。

| 画像 | 形式 | R | G | B | A |
|---|---|---|---|---|---|
| Occluders | RGBA32F | 地面高 G（絶対 m） | 樹冠の底 U（絶対 m） | 樹冠の頂 T（絶対 m。樹冠なしは G） | 樹冠の不透明度 κ |
| Visibility | RGBA16F | b0 | cx | cy | cz |
| Aux | RGBA16F | G − GroundHeightMeters | U − G | T − G | κ |

## 5. 表の更新手順

1. 変更する枠・型・オフセット・量子化・既定値を先に本表へ登録する。使用中・予約の枠を空きと見なさない。
2. ID・印・P0・P1 の変更は `ShadingModelIds.h` と `Common/GBufferEncoding.glsl` を同時に直し、全書き手・読み手を同じ関数の契約へ揃える。`ShadingModelContractTest` が定数表と直接の読み書きを検査する。デカールの保持は `DecalGBufferVulkanTest`、可視バッファの書き出しは `VisibilityResolveVulkanTest` の対象。
3. UBO の変更は各パスの C++ 構造体・確保サイズ・binding の範囲と、頂点・断片の GLSL 宣言を揃える。既存のサイズの `static_assert` も更新する。`GPUSceneInstanceData` の変更は `MeshTypes.h`、詰める側の `DrawCommand.cpp`・`CanvasView.cpp`・`SceneView.cpp`、`Common/SceneInstanceData.glsl` と上記の読み手を揃える。可視バッファの材質表を変更するときは、7 節の C++・GLSL 宣言も揃える。`InstanceDataFlattenTest` / `InstanceBufferRingTest` はこの構造体の値の転送・配置・容量を扱う。
4. lighting の binding の変更は `lighting.frag`（include 先を含む）と `CreateLightingDescriptorSetDesc`・`ExecuteWithInputs` を同時に直す。GLSL の宣言と C++ の種類・番号・束ねる資源を照合する。この一覧全体を専用に照合する試験の存在は未確認。

## 6. 照合上の未確認事項と注意

- `megageometry.vert` の `emissiveColor` のコメントはカラー・強度と表記しているが、C++ の入力と `megageometry.frag` の演算は色度・輝度（nits）である。
- `SectionUniformData` の 544 バイトと UBO 各フィールドのオフセットは配列宣言からの積算値。MegaGeometry のサイズ検査は 768 バイト以下という上限だけであり、544 バイトとの等値検査や UBO フィールドの `offsetof` 検査はない。

## 7. 可視バッファの材質表

C++ は `Library/Core/Public/Rendering/VisibilityMaterialTable.h` の `VisibilityBuffer::MaterialEntry`。`alignas(16)` の 1 件は **144 バイト**で、同ファイルに大きさ・整列と `Header`、`TexturesA`、`TexturesD`、`ShadingParams` のオフセットの `static_assert` がある。GLSL は `Assets/Shaders/Common/VisibilityResolve.glsl` の `VisMaterialEntry` で、`std430`、set 0 / binding 3 の `MaterialTable` から読む。

| オフセット | C++ フィールド / 型 | GLSL フィールド / 型 | 意味（x / y / z / w 順） |
|---|---|---|---|
| 0 | `BaseColor: float[4]` | `baseColor: vec4` | 基本色 RGBA |
| 16 | `Emissive: float[4]` | `emissive: vec4` | xyz = Y=1 の発光色度、w = 発光輝度（nits） |
| 32 | `Scalars: float[4]` | `scalars: vec4` | 金属度 / 粗さ / 高さの係数 / 変位した頂点の UV 間隔。金属度・粗さは負なら未指定 |
| 48 | `Header: uint32_t[4]` | `header: uvec4` | `MATERIAL_FLAG_*` / GBuffer のシェーディングモデル ID / `BlendMode` / 地形の混ぜ合わせの値の要約（なしなら 0） |
| 64 | `TexturesA: uint32_t[4]` | `texturesA: uvec4` | アルベドのハンドル下位・上位 / 法線のハンドル下位・上位 |
| 80 | `TexturesB: uint32_t[4]` | `texturesB: uvec4` | 金属度のハンドル下位・上位 / 粗さのハンドル下位・上位 |
| 96 | `TexturesC: uint32_t[4]` | `texturesC: uvec4` | AO のハンドル下位・上位 / ORM のハンドル下位・上位 |
| 112 | `TexturesD: uint32_t[4]` | `texturesD: uvec4` | 高さのハンドル下位・上位 / MegaGeometry の定数 AO の float ビット列 / 予約 |
| 128 | `ShadingParams: float[4]` | `shadingParams: vec4` | `P0` / `P1` / 予約 / 被覆率の乗数。初期値は全成分 -1 |

材質表の作成は `VisibilityMaterialTable.cpp::MakeMaterialEntry`。`Header[1]` には 1 節の変換後の ID を格納する。手続きメッシュのインスタンス値は解決時にインスタンス表から読み、スキニングのインスタンス値は表を作るときに材質値へ重ねる。

`VisMaterialEntry` の宣言は `Common/VisibilityResolve.glsl` の 1 か所にあり、`visbuffer_resolve.comp`、`visbuffer_resolve_dump.comp`、`visbuffer_resolve_tiles.comp`、`visbuffer_resolve_tiles_dump.comp` が同ファイルを include する。
