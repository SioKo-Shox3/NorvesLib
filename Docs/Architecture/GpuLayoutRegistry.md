# GPU の割り当て表

GPU に渡す構造体・GBuffer のチャネル・binding を追加する、または意味を変えるときは、先にこの表へ登録してから使う。決め打ちの値（`0.25` の閾値など）でチャネルに意味を足さない。GR27 以降の材質・シェーディングモデルもこの約束に従う。

GBuffer の空きチャネルの意味は `Library/Core/Public/Rendering/ShadingModelIds.h` と `Assets/Shaders/Common/GBufferEncoding.glsl` の定数表・関数が正本である。両者の定数を一致させ、`Assets/Shaders` 内の該当チャネルの読み書きは符号化・復号関数を通す。

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

書き手は `gbuffer.frag`、`megageometry.frag`、`foliage.frag`、`Common/VisibilityResolve.glsl`、`skinned_fur_gbuffer.frag`。それぞれ `GBufferEncodeNormal` / `GBufferEncodeMaterial` / `GBufferEncodeEmissive` を使う。通常のメッシュ・MegaGeometry・可視バッファの解決は材質の ID と `P0` / `P1` を書く。`foliage.frag` は `DefaultLit` と `P0 = P1 = 0` を書く。MegaGeometry のデバッグ表示や、可視バッファで材質を取得できない場合も既定値を書く。`Velocity` はクリップ座標の差から `(currentNdc - previousNdc) * 0.5` で求める。前フレームが無効なら `0`、有効でも前フレームのクリップ座標の w が閾値以下なら「履歴なし」の印を書く。ラスタ経路で現在の w が無効な場合や、通常の速度が NaN または長さの 2 乗が `1e6` 以上の場合も `0` とする。値・条件と読み手の扱いは 13 節に従う。

### 材質ごとの GBuffer プログラムのシェーダー契約

プログラムの FS は上の表の 5 枚へ同じ location・形式・意味で出力し、Normal・Material・Emissive は `Common/GBufferEncoding.glsl` の `GBufferEncodeNormal`・`GBufferEncodeMaterial`・`GBufferEncodeEmissive` を通す。標準 FS の本体は `Common/GBufferFragment.glsl` である。

VS と FS は標準の頂点入力と `set 0` の配置を共有する。ファーの殻の binding 3〜6 は 11 節のとおり読み替える。binding 0 は `MVPData`、1〜6 はアルベド・法線・金属度・粗さ・AO・高さのテクスチャ、7 は静的メッシュのインスタンス表、8〜10 はスキニングの現在行列・頂点・前行列、11 は VT フィードバックの storage buffer、12 は風の環境 UBO `VertexAnimationWindFrame`（頂点のみ）、13 は環境の UBO（14 節。ピクセルのみ）、14 は発光テクスチャ、15 は水たまりの UBO、16 は雨の遮蔽マップ、17 は水たまりの層（14〜17 はいずれもピクセルのみ。水たまりの配置は 16 節）である。風の UBO は GBuffer の 1 回の記録につき 1 つ確保し、各材質の記述子で共有する。標準 FS は GBuffer へ書く前に `ApplyWetSurfaceWithPuddle` を呼び、`DefaultLit` は返り値の水たまりの被覆を `P1` に書く。ファーの殻の FS は環境の濡れを適用しない。FS へ渡す location 0〜8 の値と型も標準の `gbuffer.vert` / `skinned_gbuffer.vert` / `Common/GBufferFragment.glsl` に合わせる。独自のスキン VS は `Common/SkinnedVertex.glsl` を include し、標準と同じ binding 8〜10 を読む。

組み込みのプログラム `Dissolve` は `GBufferProgramRegistry` の構築時に登録する。通常の VS は `gbuffer_dissolve.vert`、スキンの VS は `skinned_gbuffer_dissolve.vert`、FS は `gbuffer_dissolve.frag`。標準の location 0〜8 に加え、VS から FS へ location 9 の `vec3 fragRestPosition`（変形前の局所位置。スキンはバインド姿勢）と location 10 の `float fragDissolveProtect`（保護する骨の重み。静的メッシュは 0）を渡す。ディゾルブの閾値は `P0`（`ShadingParams.x`）で、インスタンス値を優先し、未指定なら材質値を使う。PaleFlat の消滅度と同じ `P0` の欄を使い、専用の閾値欄は増やさない。`P0` が閾値なので、ディゾルブの FS は濡れの感度に `P0` を使わず 1 とする。

### 材質値の優先と ID

`ShadingParams` は x = `P0`、y = `P1`、z = インスタンスの頂点アニメの種類（材質側は予約）、w = 被覆率の乗数（1 で不変）。各成分は `MeshComponent::SetShadingParams` / `SkinnedMeshComponent::SetShadingParams` のインスタンス値が 0 以上ならそれを使い、負なら `MaterialResourceData::ShadingParams` / `MegaMeshMaterial::ShadingParams` の材質値を使う。両方とも負なら `P0 = P1 = 0`、z = 0、w = 1。未指定の初期値は各成分 `-1` である。GLSL の解決は `Common/GBufferEncoding.glsl::ResolveShadingParams` に従う。スキニングのインスタンス値は CPU で材質値に重ねる。現行の MegaGeometry はインスタンス値を渡さず、材質値を使う。ただし `DefaultLit` の GBuffer の `P1` はこの入力値を使わず、書き手が求めた水たまりの被覆で置き換える。

`DefaultLit` の `P0` は濡れの感度であり、`Common/WetSurface.glsl::ResolveWetSurfaceResponse` がインスタンスの指定を材質より優先して解決する。両方が未指定（負または NaN）なら感度は 1、`DefaultLit` 以外も 1。これは GBuffer に書く `P0` の解決とは別で、両方未指定の `Material.a` は従来どおり 0 である。スキニングでは CPU がインスタンス値を材質側へ重ねた値を使う。

`InstanceData.shadingParams.z` は頂点アニメの種類を表す。生のインスタンス値が `0.5` 以上なら試験の正弦（種類 1）、それ以外（負の未指定値を含む）は恒等（種類 0）。`Common/VertexAnimation.glsl::VertexAnimationKind` が選び、`VertexAnimationParamsForFrame` がフレームの試験許可を適用する。頂点アニメは材質値を重ねる前の 4 成分を使い、材質側の z を種類の既定値にはしない。スキンも生の `ShadingParams` を別に渡し、MegaGeometry は `vec4(-1.0)` を使う。

### 風の頂点アニメの材質と環境

材質の `VertexAnimationParams` は `PackVertexAnimationMaterialConstants` で 2 つの `vec4` に詰める。`params0 = (mode, amplitude, heightStart, heightEnd)`、`params1 = (frequencyScale, branchFactor, phaseRandom, stiffness)`。`mode` は `None = 0`、`Grass = 1`、`Tree = 2`、`None` のとき 2 欄の全成分を 0 にする。

風の `VertexAnimationWindFrame` は `std140`、set 0 / binding 12、32 バイト。`windA = (風向 X, 風向 Z, baseSpeed, gustStrength)`、`windB = (gustFrequencyHz, gustScaleMeters, turbulence, 有効 1/0)`。`PackVertexAnimationWindConstants` が設定から詰め、風が無効なら 2 欄の全成分を 0 にする。

インスタンスの `shadingParams.z` は試験の正弦の種類であり、材質の `mode` とは別の入力。両方が有効なら `ApplyVertexAnimation` は試験の正弦を先に適用し、続けて風の変位を足す。スキニングの GBuffer は試験の正弦に限り適用し、材質の風は適用しない。

可視バッファが有効な場合、`RequiresGBufferOverlayDraw` は頂点アニメの材質を ID のラスタと材質解決から外し、解決後に GBuffer へ重ね描きする。

CPU の `ShadingModel` から GBuffer の ID への変換は `MaterialTypes.h::ToGBufferShadingModelId` だけが担う。`Fur`・`Eye`・`PaleFlat` を対応する ID に、それ以外を `DefaultLit` に写す。

### Normal.w の符号化

`packed = ((receiverFlags & 15u) << 4u) | (shadingModelId & 15u)`。下位 4 bit（bit 0〜3）が ID、上位 4 bit（bit 4〜7）が印。ビット列の再解釈ではなく、整数 `0〜255` を `float(packed)` として書く。これらの整数は RGBA16F に正確に格納できる。C++ は `GBufferContract::PackNormalW`、GLSL は `GBufferEncodeNormal` を使う。

復号は `GBufferDecodeNormal` が `uint(clamp(round(normalSample.w), 0.0, 2047.0))` を作り、ID と印を各マスクで取り出す。`2047` は復号関数のクランプ上限であり、bit 8 以上に追加の意味は登録されていない。返す法線 xyz はこの関数では正規化しない。ID だけ、印だけを読む場合も `GBufferDecodeShadingModelId` / `GBufferDecodeReceiverFlags` / `GBufferHasReceiverFlag` を使う。

| ID | C++ 定数（`GBufferContract`） | GLSL 定数 | 意味 | P0 | P1 |
|---|---|---|---|---|---|
| 0 | `ShadingModelDefaultLit` | `GBUFFER_SHADING_MODEL_DEFAULT_LIT` | 標準のライティング | 濡れの感度（`ResolveWetSurfaceResponse`） | 水たまりの被覆 0〜1（書き手が `ApplyWetSurfaceWithPuddle` の返り値を書く。水たまりが無ければ 0） |
| 1 | `ShadingModelFur` | `GBUFFER_SHADING_MODEL_FUR` | 毛皮 | 画素の濡れ度 `w = W × 領域の濡れやすさ`（UNORM8。決定 G6-S12） | 流れの角（書いた法線から作る Duff ら 2017 の正規直交基底の上の接線の角を `[0, 1)` で表す） |
| 2 | `ShadingModelEye` | `GBUFFER_SHADING_MODEL_EYE` | 目 | 湿り（鏡面の強さ） | 未使用 |
| 3 | `ShadingModelPaleFlat` | `GBUFFER_SHADING_MODEL_PALE_FLAT` | 淡い平坦な面 | 消滅度 | 未使用 |
| 4〜15 | 予約 | 予約 | 未割当 | 未割当 | 未割当 |

Fur の殻の高さは AO（`Material.b`）に反映する。ただし値は高さそのものではなく、`AoRoot` と `AoExponent` から求めた遮蔽値である。`Emissive.a` は半精度で分解能 `1/2048` ≒ 0.18 度。角の符号化と復号は `Common/FurStrand.glsl` の `FurEncodeFlowAngle`・`FurDecodeFlowTangent` を使う。

**決定 G6-S12（濡れの適用の範囲と符号化）:** 毛の濡れの見た目は Fur の画素（殻と殻 0 の皮膚）だけに掛け、Fur の `P0` には照明がつやを強めるために使う画素の `w` を書く。鼻・肉球・目など Fur でないスキンの slot と静的メッシュの濡れは、地面・草・岩の濡れ（GR53）の仕組みで行う。`DefaultLit` の `P0` は濡れの感度のままである。G7-S6 の (A) に従い、濡れは GBuffer へ書く前にアルベド・粗さ・法線へ適用済みにし、最終の `w` は書き戻さない。このため Fur でない slot へ毛の `W` を `P0` で渡すと意味がぶつかる。暗化・彩度・粗さの式は `Common/SurfaceWetness.glsl` に材質に依らない形で置き、GR53 も使える。

ID が 0 でない画素は `lighting.frag` が `Common/ShadingModels.glsl` の `ApplyShadingModelSurface`・`EvaluateShadingModelDirect`・`ShadingModelFinalize` で面・直接光・最終色を描き分け、ニューラル BRDF を使わず解析の式で評価する（検証表示を除く）。照明が P0・P1 のうち現在使う値は Fur の P0（濡れのつや）と P1（流れの角）、Eye の P0（鏡面の強さ）、DefaultLit の P1（水たまりの被覆。空由来の鏡面の遮りに使う）で、他は濡れ・消滅の段階で使う枠である。PaleFlat は `GBufferEncodeMaterialForShadingModel` で GBuffer に書く時点で粗さ 1・金属度 0 になり、SSR は粗さが終了値以上の画素を棄却するため改造しない（既定の終了値は 0.7）。半透明とパストレーサーは ID を読まない。
PaleFlat のアルベドは `Color`（リニア RGB）とし、直接光の係数は `mix(max(NdotL, 0) * shadow, mix(WrapMin, 1, clamp(NdotL * 0.5 + 0.5, 0, 1) * mix(1, shadow, ShadowStrength)), Flatness)`（shadow は 0〜1 に補正）とする。
環境光の拡散は `Color / PI * flatAmbientIrradiance` と AO・RTGI・DDGI・空の遮蔽を含む局所の拡散を `mix(1, AmbientLocalMix, Flatness)` の重みで混ぜる。`flatAmbientIrradiance` は IBL が有効なら `E_avg * ambientColor.w`（平均照度が無効なら 0）、IBL が無効なら `ambientColor.rgb * ambientColor.w * PI` とする。
直接・間接の鏡面と間接鏡面の反射率には、それぞれ `SpecularScale` を掛ける。

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

両者とも `set = 0, binding = 0` の `MVPData` に対応する。標準メッシュと MegaGeometry のワールド変換は UBO ではなく各経路のインスタンス表から、スキニングのワールド変換は別のバッファから読む。以下は `std140` の配置。各オフセットは `float` 配列の宣言順から積算し、頂点アニメの欄については C++ の `offsetof` の `static_assert` とも照合している。

### 頂点アニメの時刻（各 UBO 共通）

`vertexAnimTime`（C++ の一部は `VertexAnimTime`）は **16 バイト**の `float[4]` / `vec4`。x = 今の時刻 `t`（秒）、y = 前の時刻 `tPrev`（秒）、z = 試験の正弦の揺れの許可（1/0）、w = 0。以下の時刻欄と 8・9 節にも同じ契約を適用する。

組み立ては `Library/Core/Public/Rendering/VertexAnimation.h::MakeVertexAnimationTimeConstants`。`t = float(fmod(GameTotalTime, 4096.0))`（負なら 4096 を加える）、`tPrev = t - float(GameTotalTime - PreviousRenderedTotalTime)`。差は double で取ってから float にし、前の基準を最後に描いたフレームへ揃える。`tPrev` は独立に巻かず、巻き境界では負になることがある。

**時刻欄の注記（GR46）:** float の精度を保つため時刻は **4096 秒**で巻く。風・シェルなど周期の式の周波数は **1/4096 Hz の整数倍**へ量子化し、巻いた瞬間の不連続を防ぐ。`Common/VertexAnimation.glsl` もこの制約を記載する。

### GBuffer

C++ は `Library/Core/Private/Rendering/GBufferPass.cpp::GBufferPass::RecordGBufferDraws` 内の `PerObjectUBO`。`static_assert(sizeof(PerObjectUBO) == 576u)` と初期化時の `UBO_SIZE = 576` がある。頂点アニメのインスタンス値・時刻、鏡面の AA、頂点アニメの材質の 2 欄、視界の確保の欄は `offsetof` で 368・384・400・416・432・448 バイトを、ディゾルブの欄は 512・528・544・560 バイトを検査する。空きの末尾領域はない。

GLSL の `Assets/Shaders/gbuffer.vert` と `Assets/Shaders/skinned_gbuffer.vert` の `MVPData` は先頭 448 バイトを宣言する。`Assets/Shaders/Common/GBufferFragment.glsl`（`gbuffer.frag`・`gbuffer_alphaclip.frag`）は先頭 512 バイトを宣言する。ディゾルブの VS（`gbuffer_dissolve.vert`・`skinned_gbuffer_dissolve.vert`）と FS（`gbuffer_dissolve.frag`）は末尾の 64 バイトも含む全 576 バイトを宣言する（VS は視界の確保の 64 バイトを `vec4[4]` で読み飛ばす）。風の材質値を読むのは通常メッシュの頂点シェーダーだけである。`viewClearance` に値を入れるのは `bViewClearanceOccluder` の付いた材質だけで、ほかは全成分 0。読むのは切り抜きの変種（`gbuffer_alphaclip.frag`・`gbuffer_dissolve.frag`）だけで、通常の `gbuffer.frag` は読まない。

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
| 320 | `vtFeedbackParams: float[4]` | `vtFeedbackParams: vec4` | 法線の VT パラメータ / ORM の VT パラメータ / 高さの VT パラメータ / 鏡面のない材質（1/0） |
| 336 | `shadingParams: float[4]` | `shadingParams: vec4` | 材質の `P0` / `P1` / 予約 / 被覆率の乗数。スキニングはインスタンス値を重ねた値 |
| 352 | `shadingModel: float[4]` | `shadingModel: vec4` | x = GBuffer のシェーディングモデル ID、y = 切り抜きの閾値、z = 切り抜き方式、w = Hash の位相。ファーの殻での値は 11 節 |
| 368 | `vertexAnimInstanceParams: float[4]` | `vertexAnimInstanceParams: vec4`（頂点側のみ） | 16 バイト。スキンの描画の生の `ShadingParams`（負は未指定のまま）。静的メッシュでは全成分 -1 で、頂点側はインスタンス表を読む |
| 384 | `vertexAnimTime: float[4]` | `vertexAnimTime: vec4`（頂点側のみ） | 16 バイト。`t` / `tPrev` / 試験の揺れの許可（1/0）/ 0。巻きと量子化は上記の時刻欄の注記に従う |
| 400 | `specularAAParams: float[4]` | `specularAAParams: vec4`（断片側のみ） | 16 バイト。x = 鏡面の AA の強さ（`SpecularAAStrength`。0 で無効）、y = `NormalScale`、z = 材質のテクスチャの印、w = 予約の 0 |
| 416 | `vertexAnimMaterial0: float[4]` | `vertexAnimMaterial0: vec4`（頂点側のみ） | 16 バイト。材質の `mode` / `amplitude` / `heightStart` / `heightEnd` |
| 432 | `vertexAnimMaterial1: float[4]` | `vertexAnimMaterial1: vec4`（頂点側のみ） | 16 バイト。材質の `frequencyScale` / `branchFactor` / `phaseRandom` / `stiffness` |
| 448 | `viewClearance: ViewClearanceGpuData` | `viewClearance: ViewClearanceData`（断片側のみ） | 64 バイト（vec4 × 4）。遮蔽物のディザ。並びは下記の共通表 |
| 512 | `dissolveEdge: float[4]` | `dissolveEdge: vec4` | rgb = 縁の色、w = 縁の発光輝度（nits） |
| 528 | `dissolveSweep: float[4]` | `dissolveSweep: vec4` | xyz = 局所座標の掃引方向、w = 掃引の強さ（`DirectionBias`） |
| 544 | `dissolveParams: float[4]` | `dissolveParams: vec4` | 有効の印（1/0）/ `NoiseScale` / 縁の幅（`EdgeWidth`）/ ディザの幅（TAA 適用時は 0） |
| 560 | `dissolveProtectBones: uint32_t[4]` | `dissolveProtectBones: uvec4` | 保護する骨のビット集合 |

ファーの殻だけは先頭の `PerObjectUBO` 576 バイトに次の欄を続け、合計 **1104 バイト**とする。標準の材質は 576 バイトのまま。C++ の合成は `GBufferPass.cpp::GBufferPass::RecordGBufferDraws` 内の `FurPerObjectUBO`、確保は同ファイルの `m_FurUniformAllocator`。`FurShellDrawGpuParams` は `Library/Core/Public/Rendering/FurShellLod.h`、GLSL の全宣言は `Assets/Shaders/Common/FurStrand.glsl::MVPData` にあり、頂点・断片シェーダーが共通で使う。

| オフセット | 大きさ | C++ フィールド / 型 | GLSL フィールド / 型 | 意味 |
|---|---|---|---|---|
| 576 | 496 | `Material: FurMaterialGpuParams` | `fur: FurMaterialParams` | 材質の値。内訳は 11 節 |
| 1072 | 16 | `Draw.Force: float[4]` | `furDrawForce: vec4` | xyz = 世界空間の毛の力、w = 0。現状は xyz も 0 |
| 1088 | 16 | `Draw.Shell: float[4]` | `furShellParams: vec4` | x = 殻数、y = 最後の殻を残す割合、z = 連続の殻数、w = 皮膚と毛の粗さ |

両経路の `specularAAParams.z` は印を足し合わせた float で、1 は発光テクスチャ、2 は `NormalScale` の適用を表し、シェーダーは `uint(z + 0.5)` で戻す。発光テクスチャが有効かつ nits が正なら 1、`NormalScale != 1` なら 2 を立てる。印が 0 のときは y も 0 のままにし、既定の演算を保つ。

`vtFeedbackParams.w`（332 バイト）は `gbuffer.frag` が `> 0.5` を判定し、`Normal.w` の `DIFFUSE_ONLY` の印へ変換する。`Material.a` の意味には使わない。

VT パラメータの `0` は要求を書かない指定。整数を float に格納し、`Common/VirtualTextureFeedback.glsl::DecodeVirtualTextureFeedbackParam` で戻す。下位からタイル幅の log2（4 bit）、高さの log2（4 bit）、巡回位相（4 bit）、テクスチャ番号 + 1（12 bit）である。

### MegaGeometry

C++ は `Library/Core/Private/Rendering/MegaGeometryPass.cpp` の `SectionUniformData`。名称は per-object 相当でも、実際の共有単位は材質の区間。材質の風の頂点アニメは対象外で、静的な姿勢のままである。`TerrainBlendShading.h` の `TERRAIN_BLEND_PARAM_FLOATS = 40u` を含め、宣言された配列と `ViewClearanceGpuData` の合計は 160 float = **640 バイト**。`static_assert` は `VertexAnimTime` の位置 544・`SpecularAAParams` の位置 560・`ViewClearance` の位置 576・大きさ 640・上限 768 バイトを検査する。確保枠は `SectionUniformBufferBytes = 768`。更新・binding の範囲は `sizeof(SectionUniformData)` であり、確保枠の末尾 640〜767 バイト（128 バイト）は空きで、現在の UBO 宣言には含まれない。

GLSL は `Assets/Shaders/megageometry.vert` の `MVPData` が先頭 560 バイト、`Assets/Shaders/Common/MegaGeometryFragment.glsl` を通る `megageometry.frag`・`megageometry_clearance.frag` は全 640 バイトを宣言する。`ViewClearance` に値を入れるのは `bViewClearanceOccluder` の付いた材質の区間だけで、ほかは全成分 0。読むのは `megageometry_clearance.frag` だけで、通常の `megageometry.frag` は読まない。C++ の `EmissiveChromaticityAndLuminanceNits` に対応する GLSL 名は `emissiveColor`。

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
| 544 | `VertexAnimTime: float[4]` | `vertexAnimTime: vec4`（頂点側のみ） | 16 バイト。`t` / `tPrev` / 試験の揺れの許可（1/0）/ 0。巻きと量子化は上記の時刻欄の注記に従う |
| 560 | `SpecularAAParams: float[4]` | `specularAAParams: vec4`（断片側のみ） | 16 バイト。x = 鏡面の AA の強さ（`SpecularAAStrength`。0 で無効）、y = `NormalScale`、z = 材質のテクスチャの印、w = 予約の 0 |
| 576 | `ViewClearance: ViewClearanceGpuData` | `viewClearance: ViewClearanceData`（断片側のみ） | 64 バイト（vec4 × 4）。遮蔽物のディザ。並びは下記の共通表 |

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

### 視界の確保の共通データ

C++ は `Library/Core/Public/Rendering/ViewClearance.h::ViewClearanceGpuData`、GLSL は `Assets/Shaders/Common/ViewClearance.glsl::ViewClearanceData`。std140 の vec4 × 4、合計 **64 バイト**、整列は 16 バイト。C++ の `static_assert` が大きさ・整列・各フィールドの位置を検査する。次の位置は構造体の先頭からの相対オフセット。

| オフセット | 大きさ | C++ フィールド / 型 | GLSL フィールド / 型 | 意味（x / y / z / w 順） |
|---|---|---|---|---|
| 0 | 16 | `CameraRadius: float[4]` | `CameraRadius: vec4` | xyz = カメラのワールド位置（m）、w = カメラ側の半径（m） |
| 16 | 16 | `TargetRadius: float[4]` | `TargetRadius: vec4` | xyz = 注視点のワールド位置（m）、w = 注視点側の半径（m） |
| 32 | 16 | `Shape: float[4]` | `Shape: vec4` | 半径に対する切り抜きの縁の幅（`FeatherRatio`）/ 近接フェードの開始距離（m）/ 終了距離（m）/ 草の低背化の下限（`MinHeightScale`） |
| 48 | 16 | `Control: float[4]` | `Control: vec4` | x = `Strength`、y = 有効なら 1（それ以外は 0）、z = 遮蔽物のディザの位相（0〜63）、w = 予約の 0 |

`Control.z` はフレームの番号の下位 6 ビット（`FrameNumber % 64u`）。GBuffer・MegaGeometry の `bViewClearanceOccluder` の付いた材質だけが入れ、草・株は 0 のまま。遮蔽物の切り抜きは `ViewClearanceShouldDiscard` が読む。草・株の可視度と低背化は位相を使わない。

### スプラットの UBO と配列テクスチャ

スプラットの追加では上記の `SectionUniformData` を変更せず、別の `TerrainSplatBlock` に値を置く。`MegaGeometryPass.cpp` は `TERRAIN_SPLAT_PARAM_FLOATS = 48` 個の float を別の定数バッファとして確保し、`megageometry.frag` は `std140` の `terrainSplat[12]`（`vec4` × 12 = **192 バイト**）として読む。以下はスプラットと区間の発光テクスチャの `set 0` の束縛である。スプラットの定数は `TerrainBlendShading.h`、発光の定数は `MegaGeometryPass.cpp` にある。

| binding | C++ 定数 | GLSL 宣言（`megageometry.frag`） | 用途 |
|---|---|---|---|
| 18 | `TERRAIN_SPLAT_MEGA_ALBEDO_BINDING` | `sampler2DArray terrainSplatAlbedoArray` | 各層のアルベド |
| 19 | `TERRAIN_SPLAT_MEGA_NORMAL_BINDING` | `sampler2DArray terrainSplatNormalArray` | 各層の法線 |
| 20 | `TERRAIN_SPLAT_MEGA_ARM_BINDING` | `sampler2DArray terrainSplatArmArray` | 各層の AO・粗さ・金属度・高さ |
| 21 | `TERRAIN_SPLAT_MEGA_SPLAT_BINDING` | `sampler2DArray terrainSplatArray` | 層の重みの地図 |
| 23 | `TERRAIN_SPLAT_MEGA_PARAMS_BINDING` | `uniform TerrainSplatBlock` | 下表の `terrainSplat[12]` |
| 25 | `EmissiveTextureBindingIndex` | `sampler2D emissiveTexture` | 区間の発光テクスチャ |

値の正本は `TerrainBlendShading.h::PackTerrainSplatParams`、GLSL の対応名は `Common/TerrainLayerBlend.glsl::TerrainSplatParams` と `MakeTerrainSplatParams`。オフセットは `terrainSplat` の先頭からのバイト数で、各行の型は C++ の `float[4]`、GLSL の `vec4`。この並びは可視バッファの解決と RTGI でも共通である。

| オフセット | 配列要素 / 対応名 | x | y | z | w |
|---|---|---|---|---|---|
| 0 | `terrainSplat[0] / map` | 地図の最小 X | 地図の最小 Z | X の広さの逆数（1/m） | Z の広さの逆数（1/m） |
| 16 | `terrainSplat[1] / shape` | 層の数 | 崖の層の番号（なしは -1） | 高さ混ぜの深さ（1e-3 以上） | 地形の濡れ度（0〜1） |
| 32 | `terrainSplat[2] / cliff` | 崖へ移る傾斜の開始（rad） | 同終了（開始 + 1e-4 以上） | UV 回転の雑音の波長の逆数（1/m） | 回転角の最大（rad） |
| 48 | `terrainSplat[3] / flags` | 復号のビットマスクを float にした値 | 0 | 0 | 0 |
| 64 | `terrainSplat[4] / layer[0]` | 層 0 の UV 倍率 | 層 0 の濡れやすさ | 0 | 0 |
| 80 | `terrainSplat[5] / layer[1]` | 層 1 の UV 倍率 | 層 1 の濡れやすさ | 0 | 0 |
| 96 | `terrainSplat[6] / layer[2]` | 層 2 の UV 倍率 | 層 2 の濡れやすさ | 0 | 0 |
| 112 | `terrainSplat[7] / layer[3]` | 層 3 の UV 倍率 | 層 3 の濡れやすさ | 0 | 0 |
| 128 | `terrainSplat[8] / layer[4]` | 層 4 の UV 倍率 | 層 4 の濡れやすさ | 0 | 0 |
| 144 | `terrainSplat[9] / layer[5]` | 層 5 の UV 倍率 | 層 5 の濡れやすさ | 0 | 0 |
| 160 | `terrainSplat[10] / layer[6]` | 層 6 の UV 倍率 | 層 6 の濡れやすさ | 0 | 0 |
| 176 | `terrainSplat[11] / layer[7]` | 層 7 の UV 倍率 | 層 7 の濡れやすさ | 0 | 0 |

`flags.x` は `TERRAIN_SPLAT_FLAG_NORMAL_TWO_CHANNEL = 1`（bit 0）と `TERRAIN_SPLAT_FLAG_ALBEDO_SRGB_DECODE = 2`（bit 1）。`PackTerrainSplatParams` は層数を `MegaTerrainSplatMaxLayers = 8` に制限し、範囲外の崖の層を -1 とする。各層の UV 倍率は `1 / max(TileMeters, 1e-3)`、濡れやすさは 0〜1 に制限し、使わない層は 0。UV 回転の波長が正でなければ `cliff.z = 0`。濡れ度は有限なら 0〜1 に制限し、非有限なら 0 とする。

`MegaTerrainSplat::SplatArrayTexture` は RGBA8 線形の 3 層配列で、層 0・1 は `SPL0`・`SPL1` の重み、層 2（3 枚目）の R は `PDL0` の水たまりの凹み度 0〜1、GBA は 0。`TerrainComponent.cpp` が `GetPuddlePotential()` を層 2 の R へ写す。`Common/TerrainLayerBlend.glsl::TerrainSplatPuddlePotential` はこの R を読み、スプラットが無効・配列が 3 枚未満なら 0 とする。

`PackTerrainSplatParams` は呼び出し側の `bEnabled` または `splat.bEnabled` が偽、層数が 0、地図の幅または奥行きが 1e-3 以下の場合、48 個すべてを 0 にする。`TerrainSplatUv` は `(worldXZ - map.xy) * map.zw` で地図の座標を求める。

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
| 52〜55 | 208〜223 | `ShadingParams[0..3]` | `shadingParams: vec4` | インスタンスの `P0` / `P1` / 頂点アニメの種類（0.5 以上で試験の正弦）/ 被覆率の乗数。入力なしは全て -1 |

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
| `shadow.vert` | 0 / 7 | `world`, `shadingParams`（インスタンス表を使う分岐） |
| `point_shadow.vert` | 0 / 7 | `world`, `shadingParams`（インスタンス表を使う分岐） |
| `visbuffer_mesh.vert` | 0 / 1 | `world`, `shadingParams` |
| `Common/VisibilityResolve.glsl` | 0 / 5 | `world`, `previousWorld`, `objectColor.rgb`, `shadingParams` |
| `board2d.vert` | 0 / 7 | `world`, 板用の `normalRows`, `objectColor`, `customData.xy` |
| `world_board.vert` | 0 / 7 | `world[3]`, 板用の `normalRows`, `objectColor` |
| `impostor_gbuffer.vert` | 0 / 7 | `world[3]`, 板・アトラス用の `normalRows`, `objectColor`, `customData` |

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
| 0 | 29 | `sampler2DArray` | `skyOcclusionVisibility` | `BindTexture(29, ...)`。公開済み Visibility、またはゼロの既定配列。層 N+k は樹冠だけの組（N = 段の数） |
| 0 | 30 | `sampler2DArray` | `skyOcclusionAux` | `BindTexture(30, ...)`。公開済み Aux、またはゼロの既定配列 |
| 0 | 31 | `uniform PuddleSurfaceParamsBlock`（`std140`） | `u_PuddleSurface.params` | `BindConstantBuffer(LIGHTING_PUDDLE_SURFACE_BINDING, puddleSurfaceUpload.Buffer, ...)`。48 バイト（16 節） |
| 0 | 32 | `sampler2D` | `overheadHeightMap` | `BindTexture(LIGHTING_OVERHEAD_HEIGHT_MAP_BINDING, ...)`。有効な雨の遮蔽マップ、または既定黒 |
| 0 | 33 以降 | 未割当 | 空き | このレイアウトには宣言なし |

`LightingParams` の `shadingModelParams` はオフセット 944（32 バイト）で、C++ の `float[2][4]` と GLSL の `vec4[2]` に対応する。Fur・Eye の C++ の入口は `LightingPassSettings` の `ShadingModelLightingConstants`（`FurWrap`・`FurMinSpecularRoughness`・`EyeSpecularBoostPerP0` と Fur のローブ・透過・SSAO の係数）であり、既定値はすべて仮の値。転送時に非有限値を既定値へ戻し、負値を許さない欄を 0 以上、ローブのずらしを −1〜1、二つ目の指数の倍率を 0.01〜16、透過の指数を 1〜256、SSAO の割合を 0〜1 に補正する。

| 要素 | x | y | z | w |
|---|---|---|---|---|
| `shadingModelParams[0]` | Fur の wrap の量（0 で通常の Lambert） | Fur の鏡面に使う粗さの下限 | Eye の鏡面の強さの P0 あたりの増分（鏡面 × (1 + P0 × 値)） | Fur の濡れのつや `kGloss`（毛の 2 つのローブの指数と強さに `1 + kGloss × P0`。全体の値で `LightingPass.cpp` のローカル定数 1.0、仮） |
| `shadingModelParams[1]` | Fur の一つ目のローブのずらし | Fur の二つ目のローブのずらし | Fur の一つ目のローブの強さ | Fur の二つ目のローブの強さ（毛色を掛ける） |

PaleFlat は `paleFlatColorAndWrapMin`・`paleFlatShape`・`paleFlatAverageIrradiance` を使う。
末尾の `furShadingParams` はオフセット 1200（16 バイト）で、C++ の `float[4]` と GLSL の `vec4` に対応する。

| 欄 | x | y | z | w |
|---|---|---|---|---|
| `furShadingParams` | Fur の二つ目の指数の倍率 | 逆光の透過の指数 | 逆光の透過の強さ | Fur の画素に SSAO を掛ける割合 |

0〜32 に空き番号はない。25 は `#define VSM_SLICE_BINDING 25` と `Common/VirtualShadowMapSlice.glsl` の include により宣言される。24 も機能が無効な場合に転用できる空きではなく、C++ のレイアウトで常に確保されている。set 1 以降はこのシェーダーに宣言がない。

C++ の画像型は `CombinedImageSampler`、UBO は `ConstantBuffer`、読み取り専用 SSBO は `StructuredBuffer`、24 は `RWBuffer`。27・28 の履歴資源名は `Depth` / `Normal` だが、ここでは鏡面の蓄積値として使用する。`rtgiSpecularHit.gba` は当たった面が返す露出後の光の平均、`rtgiSpecularSky.a` は空へ抜けた割合である。

### RTGI のスプラット

こちらは上の `lighting.frag` とは別の、`Assets/Shaders/RTGI/DiffuseIndirect.comp` の `set 0` の配置である。binding 1 の `RTGIComputeParameters` は `std140` で、`foliageOcclusion` の後ろ、オフセット 256 に `terrainSplat[12]` を持つ。C++ の `LightingPass.cpp::RTGIComputeParameters` は `float terrainSplat[TERRAIN_SPLAT_PARAM_FLOATS]` を末尾に持ち、全体の **448 バイト**を `static_assert` で検査する。追加分の **192 バイト**の成分は 2 節の表と同じ。

| binding | C++ 定数（`TerrainBlendShading.h`） | GLSL 宣言（`DiffuseIndirect.comp`） | 用途 |
|---|---|---|---|
| 30 | `TERRAIN_SPLAT_RTGI_ALBEDO_BINDING` | `sampler2DArray terrainSplatAlbedoArray` | 各層のアルベド |
| 31 | `TERRAIN_SPLAT_RTGI_SPLAT_BINDING` | `sampler2DArray terrainSplatArray` | 層の重みの地図 |

### GPULightingParams の空の遮蔽

C++ は `LightingPassGpuTypes.h`、GLSL は `LightingParams` の対応欄。全体は **1216 バイト**。既存の `shadingModelParams` までの配置を保ち、その後ろに置く。公開が無いフレームと検証表示では、空の遮蔽の欄をすべてゼロにする。

| オフセット | C++ フィールド / 型 | GLSL 型 | 意味 |
|---|---|---|---|
| 976 | `skyOcclusionInfo: uint32_t[4]` | `uvec4` | 有効（1/0）/ 段の数 / 解像度 / 反射を辿る最大距離（m、float のビット列） |
| 992 | `skyOcclusionParams: float[4]` | `vec4` | Strength / DiffuseExponent / GroundHeightMeters / SpecularStrength |
| 1008 | `skyOcclusionBounce: float[4]` | `vec4` | rgb = OccluderBounceColor × OccluderBounceIntensity、w = 0 |
| 1024 | `skyOcclusionWindowOrigin: int32_t[4][4]` | `ivec4[4]` | 段ごとの xy = 窓の最小ワールドテクセル（X/Z）、zw = 0 |
| 1088 | `skyOcclusionTexelMeters: float[4]` | `vec4` | 段ごとのテクセル幅（m）。使わない段は 0 |

既定の配列は初期化時に作るゼロの DDGI 照度配列と共有する。現行 RHI は層数 1 を 2D ビューとして作るため、既定は配列ビューを持つ複数層である。無効時はテクスチャを参照しない。

### GPULightingParams の PaleFlat

`skyOcclusionTexelMeters` の後ろの差分 +48 バイトを G10-GR65 が使う。`GPULightingParams` / `LightingParams` の全体は **1216 バイト**。

| オフセット | C++ フィールド / 型 | GLSL 型 | 意味 |
|---|---|---|---|
| 1104 | `paleFlatColorAndWrapMin: float[4]` | `vec4` | rgb = `ConvertPaleFlatColorToLinear` で変換したリニア色、w = WrapMin |
| 1120 | `paleFlatShape: float[4]` | `vec4` | ShadowStrength / AmbientLocalMix / SpecularScale / Flatness |
| 1136 | `paleFlatAverageIrradiance: float[4]` | `vec4` | rgb = 選ばれた拡散照度の資源と組の全球の平均照度 E_avg（`ambientColor.w` を掛ける前）、w = 有効1・無効0。無効なら rgb も0 |

### 空の遮蔽クリップマップのチャネル

各画像は R×R の 2D 配列で、層 k が段 k。Visibility は max(2 × N, 2) 層（N = 段の数）で、層 k が全種別の組の段 k、層 N+k が樹冠だけの組の段 k。テクセル幅は `BaseTexelMeters × 4^k`。窓の最小ワールドテクセルを (ox, oz) とし、[ox, ox+R) × [oz, oz+R) を覆う。中心は ((wx+0.5)·s, (wz+0.5)·s)、画像の番地は (wx mod R, wz mod R) の正の剰余である。

| 画像 | 形式 | R | G | B | A |
|---|---|---|---|---|---|
| Occluders | RGBA32F | 地面高 G（絶対 m） | 樹冠の底 U（絶対 m） | 樹冠の頂 T（絶対 m。樹冠なしは G） | 樹冠の不透明度 κ |
| Visibility（層 k、全種別） | RGBA16F | b0 | cx | cy | cz |
| Visibility（層 N+k、樹冠だけ） | RGBA16F | b0 | cx | cy | cz |
| Aux | RGBA16F | G − GroundHeightMeters | U − G | T − G | κ |

樹冠だけの組は全種別と同じ歩みで樹冠の区間の光学的厚さだけを数え、不透明な樹冠は遮蔽率 1 とする。地面の下に入った向きは、その時点までの樹冠の厚さで遮蔽率を決めて歩みを止め、地面とその先の樹冠は数えない。

## 5. 表の更新手順

1. 変更する枠・型・オフセット・量子化・既定値を先に本表へ登録する。使用中・予約の枠を空きと見なさない。
2. ID・印・P0・P1 の変更は `ShadingModelIds.h` と `Common/GBufferEncoding.glsl` の定数表・関数を同時に直し、シェーダーと C++ の全書き手・読み手を同じ契約へ揃える。デカールでの値の保持と可視バッファへの書き出しも揃える。
3. UBO の変更は各パスの C++ 構造体・確保サイズ・binding の範囲と、頂点・断片の GLSL 宣言を揃える。既存のサイズの `static_assert` も更新する。`GPUSceneInstanceData` の変更は `MeshTypes.h`、詰める側の `DrawCommand.cpp`・`CanvasView.cpp`・`SceneView.cpp`、`Common/SceneInstanceData.glsl` と上記の読み手を揃える。可視バッファの材質表を変更するときは、7 節の C++・GLSL 宣言も揃える。 ファーの材質の定数を変えるときは `FurTypes.h` の `FurMaterialGpuParams`・`PackFurMaterialGpuParams`・`Common/FurMaterialParams.glsl` と 11 節を同時に直す。ファーの殻の UBO を変えるときは `GBufferPass.cpp` の合成構造体と確保サイズ・`FurShellLod.h`・`Common/FurStrand.glsl` を揃える。
4. lighting の binding の変更は `lighting.frag`（include 先を含む）と `CreateLightingDescriptorSetDesc`・`ExecuteWithInputs` を同時に直す。GLSL の宣言と C++ の種類・番号・束ねる資源を照合する。
5. 環境の UBO を変更するときは `EnvironmentGpuParams.h` の C++ 構造体と `static_assert`、`Common/WetSurface.glsl` の構造体、14 節に記載した全書き手の宣言・binding を揃える。
6. Velocity の「履歴なし」の印の値・判定の境・前フレームの w の閾値を変更するときは、`VelocityContract.h` と `Common/VelocityEncoding.glsl` を同時に直し、全書き手・読み手を 13 節の契約へ揃える。

## 6. 照合上の未確認事項と注意

- `megageometry.vert` の `emissiveColor` のコメントはカラー・強度と表記しているが、C++ の入力と `megageometry.frag` の演算は色度・輝度（nits）である。
- UBO の各フィールドのオフセットは配列宣言から積算した。頂点アニメ・鏡面の AA・視界の確保・ディゾルブの欄の `offsetof` 検査は `PerObjectUBO` の 368・384・400・416・432・448・512・528・544・560、`SectionUniformData` の 544・560、`GPURasterParams` の 112・128・176・192、`ShadowBaseUBO` の 208、`ShadowPerObjectUBO` の 224・240・256・272・288、`PointShadowFaceUBO` の 224・240・256・272・288・304、`ShadowAlphaClipUBO` と `PointShadowAlphaClipUBO` の 240・256・272 にある。VSM のディゾルブ表は `GPUVsmDissolveEntry` の 0・16・32・48 を検査する。`VisFrameConstants` と `ResolveParams` は大きさの検査だけで、フィールドごとの `offsetof` 検査はない。
- `SceneInstanceData.glsl` の `shadingParams.z` のコメントは予約のままだが、実際の頂点アニメの読み手は 1・3 節の種類として使う。`VisibilityBuffer.h` の `DrawRecord::LodPayload` のコメントも他の種類を 0 としているが、VSM の影の記録では 9 節の float ビット列を格納する。

## 7. 可視バッファの材質表

C++ は `Library/Core/Public/Rendering/VisibilityMaterialTable.h` の `VisibilityBuffer::MaterialEntry`。`alignas(16)` の 1 件は **160 バイト**で、同ファイルに大きさ・整列と `Header`（48）、`TexturesA`（64）、`TexturesD`（112）、`ShadingParams`（128）、`TexturesE`（144）のオフセットの `static_assert` がある。GLSL は `Assets/Shaders/Common/VisibilityResolve.glsl` の `VisMaterialEntry` で、`std430`、set 0 / binding 3 の `MaterialTable` から読む。

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
| 144 | `TexturesE: uint32_t[4]` | `texturesE: uvec4` | 発光テクスチャのハンドル下位・上位 / `NormalScale` の float ビット列 / 予約の 0 |

材質表の作成は `VisibilityMaterialTable.cpp::MakeMaterialEntry`。`Header[1]` には 1 節の変換後の ID を格納する。手続きメッシュのインスタンス値は解決時にインスタンス表から読み、スキニングのインスタンス値は表を作るときに材質値へ重ねる。

`Header[0]` の `MATERIAL_FLAG_EMISSIVE_TEXTURE = 1024`（bit 10）は有効な発光テクスチャと正の nits がある場合、`MATERIAL_FLAG_NORMAL_SCALE = 2048`（bit 11）は有限の `NormalScale != 1` の場合に立つ。印が立たない対応欄は 0 のままである。材質ごとのタイルの解決は両方を適用する。ただし地形・スプラットの法線には `NormalScale` を適用しない。直接 dispatch の解決は発光を定数の色度と nits だけから求め、テクスチャや `NormalScale` を適用しない。

`VisMaterialEntry` の宣言は `Common/VisibilityResolve.glsl` の 1 か所にあり、`visbuffer_resolve.comp`、`visbuffer_resolve_dump.comp`、`visbuffer_resolve_tiles.comp`、`visbuffer_resolve_tiles_dump.comp` が同ファイルを include する。

`VisibilityMaterialTable.cpp::MakeMaterialEntry` はスプラットの材質に `Header[0]` の `MATERIAL_FLAG_TERRAIN_SPLAT = 128`（bit 7）を立て、`Header[3]` に `HashTerrainSplat` の値を入れる。定数は `VisibilityMaterialTable.h` にあり、3 層の `MATERIAL_FLAG_TERRAIN_BLEND` とは別の印である。

### 材質ごとの解決の UBO とテクスチャ

`NORVES_VISRESOLVE_TILES` の `ResolveTileParams` は `tile`・`vt`・3 層の `terrain[10]` に続けて `terrainSplat[12]` を持つ。後者はオフセット 192 から **192 バイト**で、UBO 全体は従来の 192 バイトから **384 バイト**になった。`VisibilityResolvePass.cpp` の `TileParamsBytes` が 384 を検査し、`Common/VisibilityResolve.glsl` の `VIS_TILE_BINDING` は通常版が binding 11、dump 版が binding 12。`terrainSplat` の各成分は 2 節の表と同じ。

`tileParams.tile.z` の `TILE_FLAG_TERRAIN_SPLAT` / `VIS_TILE_FLAG_TERRAIN_SPLAT` は 32（bit 5）で、スプラットの配列と並びを使う印。C++ の定数は `VisibilityResolvePass.h`、GLSL の定数は `Common/VisibilityResolve.glsl` にある。材質表の `Header[0]` の bit 7 とは別のフィールドである。

| set | binding | C++ 定数 | GLSL 宣言（`VisibilityResolve.glsl`） | 用途 |
|---|---|---|---|---|
| 0 | 33 | `TERRAIN_SPLAT_VIS_ALBEDO_BINDING` | `sampler2DArray terrainSplatAlbedoTexture` | 各層のアルベド |
| 0 | 34 | `TERRAIN_SPLAT_VIS_NORMAL_BINDING` | `sampler2DArray terrainSplatNormalTexture` | 各層の法線 |
| 0 | 35 | `TERRAIN_SPLAT_VIS_ARM_BINDING` | `sampler2DArray terrainSplatArmTexture` | 各層の AO・粗さ・金属度・高さ |
| 0 | 36 | `TERRAIN_SPLAT_VIS_SPLAT_BINDING` | `sampler2DArray terrainSplatMapTexture` | 層の重みの地図 |
| 0 | 38 | `EmissiveTextureBindingIndex`（`VisibilityResolvePass.cpp`） | `sampler2D emissiveTexture` | 材質の発光テクスチャ |

33〜36 の定数は `TerrainBlendShading.h` にある。

## 8. 可視バッファのフレーム定数と頂点アニメの生値表

### ラスタの VisFrame

C++ は `VisibilityRasterPass.cpp::VisFrameConstants`（`alignas(16)`）、GLSL は `visbuffer_mesh.vert`・`visbuffer_mega.vert`・`visbuffer_skinned.vert`・`visbuffer_sw_raster.comp` の `VisFrame`。`std140`、set 0 / binding 0、合計 **144 バイト**。C++ の `static_assert` は大きさを検査し、確保サイズは `FrameUniformBytes = sizeof(VisFrameConstants)`。

| オフセット | 大きさ | C++ フィールド / 型 | GLSL フィールド / 型 | 意味 |
|---|---|---|---|---|
| 0 | 64 | `View: float[16]` | `view: mat4` | 現在のビュー行列 |
| 64 | 64 | `Projection: float[16]` | `projection: mat4` | 現在の投影行列 |
| 128 | 16 | `VertexAnimTime: float[4]` | `vertexAnimTime: vec4` | `t` / `tPrev` / 試験の揺れの許可（1/0）/ 0。2 節の時刻欄の注記（4096 秒の巻き・GR46 の周波数量子化）に従う |

### 解決の ResolveParams

C++ は `VisibilityResolvePass.h::VisibilityResolveGeometry::ResolveParams`（`alignas(16)`）、GLSL は `Common/VisibilityResolve.glsl::ResolveParams`。`std140`、set 0 / binding 0、合計 **272 バイト**。C++ の `static_assert` は大きさを検査する。

| オフセット | 大きさ | C++ フィールド / 型 | GLSL フィールド / 型 | 意味 |
|---|---|---|---|---|
| 0 | 64 | `InvView: float[16]` | `invView: mat4` | ビュー空間からワールドへの変換 |
| 64 | 64 | `InvProj: float[16]` | `invProj: mat4` | クリップ空間からビュー空間への変換 |
| 128 | 64 | `PreviousViewProj: float[16]` | `previousViewProj: mat4` | 前のカメラの投影 × ビュー |
| 192 | 16 | `CameraPosition: float[4]` | `cameraPosition: vec4` | xyz = カメラのワールド位置 |
| 208 | 16 | `Viewport: float[4]` | `viewport: vec4` | x / y / 幅 / 高さ（画素） |
| 224 | 16 | `Screen: uint32_t[4]` | `screen: uvec4` | 画面幅 / 画面高さ / フラグ / 材質表の件数 |
| 240 | 16 | `Frame: float[4]` | `frame: vec4` | プリエクスポージャ / MegaGeometry のデバッグ表示 / 幾何法線の印 / w = 鏡面の AA の強さ（製品の経路は `SpecularAAStrength`、0 で無効、`BuildParams` の既定は 0） |
| 256 | 16 | `VertexAnimTime: float[4]` | `vertexAnimTime: vec4` | `t` / `tPrev` / 試験の揺れの許可（1/0）/ 0。2 節の時刻欄の注記に従う |

### スキンの記録と生の ShadingParams の表

可視バッファの記録は `VisibilityBuffer.h::DrawRecord` と `Common/VisibilityBuffer.glsl::VisibilityDrawRecord`。`std430` の 1 件は **64 バイト**（C++ は大きさ・整列と 16・32・48 バイトの境界を `static_assert` で検査）。末尾の割り当ては次のとおりで、記録の大きさは増やさない。

| オフセット | 大きさ | C++ フィールド / 型 | GLSL 成分 | 可視バッファでの意味 |
|---|---|---|---|---|
| 48 | 8 | `PreviousVertexAddress: uint64_t` | `previous.xy` | 前のフレームに変形したスキン頂点のアドレス（下位 / 上位 32 bit） |
| 56 | 4 | `LodPayload: uint32_t` | `previous.z` | MegaGeometry の LOD の段。他の可視バッファの種類では 0 |
| 60 | 4 | `Reserved: uint32_t` | `previous.w` | スキンでは生の `ShadingParams` の表の添え字 |

表は `VisibilityRasterPass` が作る `VisBuffer_SkinnedVertexAnimParams`。C++ の `SkinnedVertexAnimParams::Values: float[4]`（位置 0、大きさ 16 バイト、`static_assert` あり）と GLSL の `visVertexAnimParams[]: vec4[]` が対応し、`std430` の要素間隔は **16 バイト**。描画ごとに生の `ShadingParams` を 1 件コピーし、同じ描画の塊で添え字を共有する。x / y / z / w は `P0` / `P1` / 頂点アニメの種類 / 被覆率の乗数で、負の未指定値も保持する。7 節の材質値を重ねた表とは別である。

| 読み手 | set / binding | 宣言・束ね |
|---|---|---|
| `visbuffer_skinned.vert` | 0 / 3 | `VIS_VERTEX_ANIM_PARAMS_TABLE_BINDING = 3`、`VisibilityRasterPass.cpp` の `SkinnedSet->BindStorageBuffer(3, ...)` |
| `Common/VisibilityResolve.glsl` | 0 / 32 | `VIS_VERTEX_ANIM_PARAMS_TABLE_BINDING = 32`、`VisibilityResolvePass.cpp` の `VertexAnimParamsTableBindingIndex` |

両者とも `Common/VisibilityBuffer.glsl::VisRecordVertexAnimParams` で読む。スキン以外の種類、または添え字が表の範囲外なら `vec4(-1.0)` を返す。解決では手続きメッシュは `InstanceData.shadingParams`、MegaGeometry は `vec4(-1.0)` を使う。MegaGeometry の種から位相を取り出す方法は 15 節に記す。

## 9. 影の頂点アニメ用の定数と記録

### VSM の VsmRasterParams

C++ は `VirtualShadowMapRaster.h::VirtualShadowMapRasterParams`（`VirtualShadowMapRaster.cpp` の `GPURasterParams` はその別名）、GLSL は `Common/VsmDrawVertex.glsl::VsmRasterParams`（`vsm_draw.vert` / `vsm_draw_alphaclip.vert` が共有）。`std140`、set 0 / binding 0、合計 **208 バイト**。`static_assert` が各フィールドの位置と大きさ 208（`vertexAnimTime` は 112、`worldField` は 128、風は 176・192）を検査する。

| オフセット | 大きさ | C++ フィールド / 型 | GLSL フィールド / 型 | 意味 |
|---|---|---|---|---|
| 0 | 16 | `lightRight: float[4]` | `lightRight: vec4` | ライトの右方向 |
| 16 | 16 | `lightUp: float[4]` | `lightUp: vec4` | ライトの上方向 |
| 32 | 16 | `lightDirection: float[4]` | `lightDirection: vec4` | ライトの方向 |
| 48 | 16 | `depth: float[4]` | `depth: vec4` | x = 深度の原点、y = 深度範囲の 2 倍の逆数 |
| 64 | 16 | `counts: uint32_t[4]` | `counts: uvec4` | 塊の数 / スライスの数 / インスタンス容量 / MegaGeometry のクラスタ記録容量 |
| 80 | 16 | `foliageWind: float[4]` | `foliageWind: vec4` | xy = 風向（XZ）、z = 風の時計、w = 共通の風の突風の周波数（Hz。0 なら従来の風） |
| 96 | 16 | `foliageWindParams: float[4]` | `foliageWindParams: vec4` | 風速 / 突風の大きさ / 突風の強さ / 乱流の強さ |
| 112 | 16 | `vertexAnimTime: float[4]` | `vertexAnimTime: vec4` | `t` / `tPrev` / 試験の揺れの許可（1/0）/ 0。影は x のみ使う。2 節の時刻欄の注記に従う |
| 128 | 48 | `worldField: WorldFieldSampleConstants` | `worldField: WorldFieldSampleConstants` | 株の影を曲げる世界フィールドの今・前の原点と範囲の逆数・曲げの係数（`WorldField/WorldFieldSample.glsl`）。株の塊だけが読む |
| 176 | 16 | `vertexAnimWindA: float[4]` | `vertexAnimWindA: vec4` | 風向 X / 風向 Z / `baseSpeed` / `gustStrength` |
| 192 | 16 | `vertexAnimWindB: float[4]` | `vertexAnimWindB: vec4` | `gustFrequencyHz` / `gustScaleMeters` / `turbulence` / 有効 1/0 |

`vsm_expand.comp` の同名 UBO 宣言は先頭 **112 バイト**（`foliageWindParams` まで）のままで、時刻欄を持たない。

### VSM の塊ごとの頂点アニメの表

`VirtualShadowMapPass.cpp::ChunkVertexAnimation` は `Params0` と `Params1` の 2 つの `float[4]` からなる **32 バイト**で、2 欄目の位置 16 と大きさを `static_assert` で検査する。描画時の set 0 / binding 8 は `std430` の読み取り専用 storage buffer `VsmChunkVertexAnim`（`Common/VsmDrawVertex.glsl`）で、塊 i の材質値は `chunkVertexAnim[2i]` と `[2i+1]`。各成分は 1 節と同じ。手続きメッシュに材質を対応させ、スキン・草の株には 0 を入れる。MegaGeometry はこの表を参照しない。

### VSM の切り抜きのディゾルブ表

切り抜きの描画の set 0 / binding 9 は `std430` の読み取り専用 storage buffer `VsmDissolveTable`（`Common/VsmDrawVertex.glsl`、`vsm_draw_alphaclip.vert`）。C++ の `VirtualShadowMapRaster.h::GPUVsmDissolveEntry` と GLSL の `VsmDissolveEntry` が対応し、1 件の大きさ・要素間隔は **64 バイト**。C++ の `static_assert` は大きさと各欄の位置を検査する。

| オフセット | C++ フィールド / 型 | GLSL フィールド / 型 | 意味（x / y / z / w 順） |
|---|---|---|---|
| 0 | `Sweep: float[4]` | `sweep: vec4` | xyz = 局所座標の掃引方向、w = 掃引の強さ |
| 16 | `Params: float[4]` | `params: vec4` | `NoiseScale` / 材質の閾値 / 種類（手続き 0、スキン 1）/ 有効の印（1/0） |
| 32 | `ProtectBones: uint32_t[4]` | `protectBones: uvec4` | 保護する骨のビット集合 |
| 48 | `Skin: uint32_t[4]` | `skin: uvec4` | 入力頂点のアドレスの下位 32 bit / 上位 32 bit / 予約 / 予約 |

`VirtualShadowMapPass.cpp::AppendDissolveCasterDraw` は塊の `Record.PreviousTransformIndex`（`record.source.z`、塊の先頭から 24 バイト）へ「表の番号 + 1」を書く。0 は表の指定なし。閾値は `uintBitsToFloat(record.previous.x)`（インスタンスの `P0`）が 0 以上なら 0〜1 に補正して優先し、負なら表の `params.y` を使う。

スキンは `SkinningComputeInstance::SourceVertexAddress` を `skin.xy` に詰め、スキニング前の入力頂点を読む。入力の `SkinnedMeshVertex` は 64 バイトで、変形後と同じ頂点番号からバインド姿勢の位置・骨の番号・重みを読み、保護率を求める。手続きメッシュは通常の局所頂点位置を使う。

### VSM の塊の記録

`VirtualShadowMapRaster.h::VsmShadowChunk` と `Common/VirtualShadowMapChunk.glsl::VsmShadowChunk` は `std430`、**144 バイト**。先頭 64 バイトが `DrawRecord` / `VisibilityDrawRecord`、64〜95 が境界とスライス集合、96〜143 がワールド変換の 3 行。大きさと境界・変換の位置は C++ の `static_assert` にある。

手続き・スキンの影の塊は前フレーム情報を読まないため、`VirtualShadowMapCasters.h::SetChunkVertexAnimationParams` が `record.previous` の 4 語へ生の値の **float ビット列**を格納する。数値を uint へ変換する意味ではない。

| 塊の先頭からの位置 | 大きさ | C++ の格納先 | GLSL 成分 | 影での意味 |
|---|---|---|---|---|
| 48 | 4 | `Record.PreviousVertexAddress` の下位 32 bit | `record.previous.x` | 生の `ShadingParams.x` の float ビット列 |
| 52 | 4 | `Record.PreviousVertexAddress` の上位 32 bit | `record.previous.y` | 生の `ShadingParams.y` の float ビット列 |
| 56 | 4 | `Record.LodPayload` | `record.previous.z` | 生の `ShadingParams.z`（頂点アニメの種類）の float ビット列 |
| 60 | 4 | `Record.Reserved` | `record.previous.w` | 不透明は生の `ShadingParams.w`、切り抜きは解決後の被覆率の float ビット列 |

値の指定がない場合は各成分 -1 のビット列。切り抜きの塊は `AppendMaskedCasterDraw` が `previous.w` を解決後の被覆率で上書きし、断片の切り抜きでも使う。`vsm_draw.vert` が `uintBitsToFloat(record.previous)` で復元する。MegaGeometry の塊の `record.previous` は 0 で、シェーダーはその値を読まず `vec4(-1.0)` を使う。可視バッファのスキン記録で `previous.w` を表の添え字にする 8 節とは、用途が異なる。

### 影の地図の ShadowMVP

C++ は `ShadowMapPass.cpp::ShadowPerObjectUBO`、GLSL は `shadow.vert::ShadowMVP`。`std140`、set 0 / binding 0、合計 **304 バイト**。`ShadowBaseUBO` の 208、`ShadowPerObjectUBO` の 224・240・256・272・288 と大きさを `static_assert` で検査し、確保サイズは `sizeof(ShadowPerObjectUBO)`。

| オフセット | 大きさ | C++ フィールド / 型 | GLSL フィールド / 型 | 意味 |
|---|---|---|---|---|
| 0 | 64 | `Base.lightView: float[16]` | `lightView: mat4` | ライトのビュー行列 |
| 64 | 64 | `Base.lightProjection: float[16]` | `lightProjection: mat4` | ライトの投影行列 |
| 128 | 64 | `Base.world: float[16]` | `world: mat4` | `worldSource.x = 1` の描画のワールド変換 |
| 192 | 16 | `Base.worldSource: float[4]` | `worldSource: vec4` | x = 1 なら UBO の world、それ以外はインスタンス表の world を使う |
| 208 | 16 | `Base.vertexAnimTime: float[4]` | `vertexAnimTime: vec4` | `t` / `tPrev` / 試験の揺れの許可（1/0）/ 0。影は x のみ使う。2 節の時刻欄の注記に従う |
| 224 | 16 | `reservedAlphaClip: float[4]` | `reservedAlphaClip: vec4` | 切り抜きの欄と位置を揃える予約 |
| 240 | 16 | `vertexAnimMaterial0: float[4]` | `vertexAnimMaterial0: vec4` | 材質の `params0` |
| 256 | 16 | `vertexAnimMaterial1: float[4]` | `vertexAnimMaterial1: vec4` | 材質の `params1` |
| 272 | 16 | `windA: float[4]` | `windA: vec4` | 風の `windA` |
| 288 | 16 | `windB: float[4]` | `windB: vec4` | 風の `windB` |

CSM の切り抜きは `ShadowAlphaClipUBO` / `ShadowAlphaClipMVP` の **288 バイト**で、先頭 224 バイトは `ShadowBaseUBO` と同じ。`shadow_alphaclip.vert`・`skinned_shadow_alphaclip.vert`・`shadow_alphaclip.frag` が全体を宣言する。224〜239 の `alphaClip` に続くディゾルブの欄は下表のとおりで、風の追加欄はない。大きさと各欄の位置・16 バイトの幅を C++ の `static_assert` で検査する。

| オフセット | 大きさ | C++ フィールド / 型 | GLSL フィールド / 型 | 意味（x / y / z / w 順） |
|---|---|---|---|---|
| 240 | 16 | `dissolveSweep: float[4]` | `dissolveSweep: vec4` | xyz = 局所座標の掃引方向、w = 掃引の強さ |
| 256 | 16 | `dissolveParams: float[4]` | `dissolveParams: vec4` | 有効の印（1/0）/ `NoiseScale` / 閾値の代わりの値 / 予約 |
| 272 | 16 | `dissolveProtectBones: uint32_t[4]` | `dissolveProtectBones: uvec4` | 保護する骨のビット集合 |

静的メッシュでインスタンス表を使う場合、`shadingParams.x` が 0 以上なら 0〜1 に補正して閾値に優先し、未指定なら `dissolveParams.z` を使う。スキンは CPU でインスタンスの `P0` を材質値へ重ねた閾値を `dissolveParams.z` に詰める。この閾値の優先順は点光源の切り抜きにも共通する。

### 点光源の PointShadowFace

C++ は `ShadowMapPass.cpp::PointShadowFaceUBO`、GLSL は `point_shadow.vert::PointShadowFace`。`std140`、set 0 / binding 0、合計 **320 バイト**。`PointShadowBaseUBO` は先頭 224 バイト、残りの各欄は 16 バイトで、各位置と大きさを `static_assert` で検査する。`point_shadow.frag` は先頭 144 バイトのみを宣言する。

| オフセット | 大きさ | C++ フィールド / 型 | GLSL フィールド / 型 | 意味 |
|---|---|---|---|---|
| 0 | 64 | `Base.lightView: float[16]` | `lightView: mat4` | 点光源の面のビュー行列 |
| 64 | 64 | `Base.lightProjection: float[16]` | `lightProjection: mat4` | 点光源の面の投影行列 |
| 128 | 16 | `Base.lightPositionAndInvRange: float[4]` | `lightPositionAndInvRange: vec4` | 光源の位置 xyz / 範囲の逆数 |
| 144 | 64 | `Base.world: float[16]` | `world: mat4` | `worldSource.x = 1` の描画のワールド変換 |
| 208 | 16 | `Base.worldSource: float[4]` | `worldSource: vec4` | x = 1 なら UBO の world、それ以外はインスタンス表の world |
| 224 | 16 | `reservedAlphaClip: float[4]` | `reservedAlphaClip: vec4` | 切り抜きの欄と位置を揃える予約 |
| 240 | 16 | `vertexAnimTime: float[4]` | `vertexAnimTime: vec4` | 今・前の時刻 / 試験の揺れの許可 / 0。影は今の時刻のみ使う |
| 256 | 16 | `vertexAnimMaterial0: float[4]` | `vertexAnimMaterial0: vec4` | 材質の `params0` |
| 272 | 16 | `vertexAnimMaterial1: float[4]` | `vertexAnimMaterial1: vec4` | 材質の `params1` |
| 288 | 16 | `windA: float[4]` | `windA: vec4` | 風の `windA` |
| 304 | 16 | `windB: float[4]` | `windB: vec4` | 風の `windB` |

点光源の切り抜きは `PointShadowAlphaClipUBO` / `PointShadowAlphaClipFace` の **288 バイト**で、先頭 224 バイトは `PointShadowBaseUBO` と同じ。`point_shadow_alphaclip.vert`・`skinned_point_shadow_alphaclip.vert`・`point_shadow_alphaclip.frag` が全体を宣言する。224〜239 に `alphaClip`、240 に `dissolveSweep`、256 に `dissolveParams`、272 に `dissolveProtectBones` を置く。各欄の型・成分・閾値の優先順は上の CSM の切り抜きと同じで、大きさと各欄の位置・16 バイトの幅を C++ の `static_assert` で検査する。風の追加欄はない。不透明で頂点アニメのある通常メッシュは面の中で材質ごとに UBO と記述子を共有し、アニメのない非スキンの描画は面ごとの記述子を共有する。切り抜き・スキンは別の記述子で扱う。

## 10. 草・低木の風の前時刻

`FoliagePass.cpp::FoliageUniform` / `Common/FoliageData.glsl::FoliageData` の UBO は **384 バイト**のまま（`static_assert` あり）。`vertexAnimTime` は追加していない。`std140` の `wind: float[4]` / `vec4` は位置 272、大きさ 16 バイトで、xy = 風向、z = 現在の風の時刻、w = 前の風の時刻。`wind[3]`（284 バイト）は両方の風の経路で `windTime - VertexAnimationPreviousDeltaSeconds(context.ExtensionFrame)` とし、最後に描いたフレームの時刻を基準にする。

## 11. ファーの材質の定数とマップ

`ShadingModel::Fur` のときだけ有効な `MaterialResourceData::Fur` を、`FurMaterialGpu.cpp` の `PackFurMaterialGpuParams(fur, wetness)` が std140 の定数にする。
RenderThread へは `SkinnedMeshProxy::FurSlots` の値の写しで渡し、ファーの値については材質の店を直接読まない。
スキンの殻描画はこの定数を `GBufferPass.cpp` の `FurPerObjectUBO` に組み込み、set 0 / binding 0 に束ねる。配置は 2 節。
各行は 16 バイトで、C++ の並びと std140 が一致する。全体は **496 バイト**（112 + 48 × 8）。

### FurMaterialGpuParams

C++ は `Library/Core/Public/Rendering/FurTypes.h::FurMaterialGpuParams`、GLSL は `Assets/Shaders/Common/FurMaterialParams.glsl::FurMaterialParams`。オフセットは構造体の先頭からのバイト数。

| オフセット | C++ フィールド / 型 | GLSL フィールド / 型 | 意味（x / y / z / w 順） |
|---|---|---|---|
| 0 | `Counts: uint32_t[4]` | `uvec4 counts` | x = 殻数（`LayerCount`）、y = 領域数（1〜8。表なしは 1 で既定の領域）、z = マップの印、w = 空き（0） |
| 16 | `Growth: float[4]` | `vec4 growth` | `MaxLengthMeters × LengthScale`（m）/ `StrandDensity` / `ClumpSize` / `ClumpStrength` |
| 32 | `Strand: float[4]` | `vec4 strand` | `RootRadius` / `TipRadius` / `ThicknessExponent` / `Tilt` |
| 48 | `Undercoat: float[4]` | `vec4 undercoat` | rgb = 下毛の線形色、a = `UndercoatAlbedoMix` |
| 64 | `Occlusion: float[4]` | `vec4 occlusion` | x = `AoRoot`、y = `AoExponent`、zw = 空き（0） |
| 80 | `WetGeometry: float[4]` | `vec4 wetGeometry` | `WetLengthCollapse` / `WetThicken` / `WetClump` / `WetSag`。各応答 × `W`。毛の FS は xyz に領域の濡れやすさ `s` を掛けて使い、垂れの w は頂点で `W` のまま使う |
| 96 | `WetShading: float[4]` | `vec4 wetShading` | `WetDarken` / `WetDesaturate` / `WetRoughness` / `WetGloss`。各応答 × `W`。毛の FS は xyz に領域の濡れやすさ `s` を掛けて使う。`WetGloss` は照明に届かず、現在は読まない |
| 112 + 48 i（i = 0〜7） | `Regions[i]: FurMaterialGpuRegion` | `FurMaterialRegion regions[FUR_MAX_REGION_COUNT]`（8 要素） | 領域 i |

領域 1 つは **48 バイト**。次のオフセットは各領域の先頭からの相対位置。

| オフセット | C++ フィールド / 型 | GLSL フィールド / 型 | 意味（x / y / z / w 順） |
|---|---|---|---|
| +0 | `Shading: float[4]` | `vec4 shading` | `RootDarkness` / `TipBrightness` / `LengthScale` / `ThicknessScale` |
| +16 | `Lobes: float[4]` | `vec4 lobes` | `PrimaryShift` / `SecondaryShift` / `PrimaryExponent` / `SecondaryExponent` |
| +32 | `Wet: float[4]` | `vec4 wet` | x = `WetSusceptibility`、yzw = 空き（0） |

濡れ度 `W` はインスタンスの `ShadingParams` の P0 を優先し、未指定なら材質値を使う（1 節の ID 1）。Pack は `[0, 1]` に収め（負・非有限は 0）、応答 × `W` だけを書き、生の濡れ度は書かない。
濡れの応答がすべて 0 なら `W` に依らず濡れの定数は同じ。FS は領域の `wet.x`（C++ の `Wet[0]`）を `s` として応答に掛け、GBuffer の `P0` に `W × s` を書く。頂点の垂れには `s` を掛けず、照明は材質の `WetGloss` の代わりに全体の `kGloss` を使う。
領域数が 0 の材質は 1 にして既定の領域を書き、使わない領域の枠も `FurRegion` の既定値で埋める。

### マップの印

`Counts.z`（C++ の `Counts[2]`）は有効なマップのハンドルに対応するビットマスク。立っていない枠は描画が既定の値を使う。

| ビット | 値 | C++ 定数 | GLSL 定数 | マップ |
|---|---|---|---|---|
| bit 0 | 1 | `FurMapMaskLength` | `FUR_MAP_MASK_LENGTH` | 長さ |
| bit 1 | 2 | `FurMapMaskFlow` | `FUR_MAP_MASK_FLOW` | 流れ |
| bit 2 | 4 | `FurMapMaskDensityClump` | `FUR_MAP_MASK_DENSITY_CLUMP` | 密度と束 |
| bit 3 | 8 | `FurMapMaskRegion` | `FUR_MAP_MASK_REGION` | 領域 |

### ファーのマップ

4 枚とも UV0 空間・線形・ミラーなしで扱う。C++ のハンドルは `FurMapTextures` に保持する。

| 名前 | 形式 | R | G | 読み方 |
|---|---|---|---|---|
| Length | R8 | 長さの割合 | 成分なし | 値 × `MaxLengthMeters` が実長。0 は毛なし |
| Flow | RG8 | UV 空間の方向の X の符号化値 | UV 空間の方向の Y の符号化値 | `v = 2 * rg - 1` が UV 空間の向き。ベクトルの長さは整い度で、ミップで縮む。正規化せず長さを強さとして読む |
| DensityClump | RG8 | 密度の倍率（0 は素肌） | 束の強さの倍率 | 各値を密度・束の強さに掛ける |
| Region | R8 | 領域 ID の符号化値 | 成分なし | `ID = round(r * 255)`。補間すると別の領域の値になるため `texelFetch` で読み、`FurRegionIndex` で表の外を最後の領域に寄せる |

### 殻の描画での束縛の読み替え

`GBufferPass.cpp` は `set 0` の通常材質用の枠をファーのマップとして読み替える。GR26 が材質ごとのテクスチャの枠を持つまでの暫定である。binding 1 はアルベドのまま。

| binding | 殻の描画で読むマップ | シェーダーの読み方 |
|---|---|---|
| 3 | Length | `lengthTexture` の R に領域の長さ倍率とセルの乱数を掛け、殻上の毛の残存を判定する |
| 4 | Flow | `flowTexture` の RG から接線を作って正規化する。中央付近は向きなしとして扱い、長さは整い度として使わない |
| 5 | DensityClump | `densityClumpTexture` の R だけをセルの密度に掛ける。G と束の定数は現状未使用 |
| 6 | Region | `regionTexture` の R を `texelFetch` で読む |

マップを解決できない場合は有効の印を落とし、断片シェーダーが Length = 1、Flow = `(0.5, 0.5)`、DensityClump = `(1, 1)`、Region = 0 として扱う。`shadingModel.y` は切り抜きの閾値 0.5、`.z` は TAA 適用時に Hash、それ以外は Test、`.w` は TAA の位相（TAA なしは 0）。殻 0 以外の細い毛と遠距離の被覆の判定に使う。

## 12. 世界フィールドの枯れ度の層

### 名前付き資源と座標

`RenderGraphResourceNames.h` の名前で描画グラフへ公開する。形式はいずれも `R8G8B8A8_UNORM`（RGBA8 UNORM）、用途は `ShaderRead | ShaderWrite`、解像度は固定の `WorldFieldDrynessResolution = 256` × 256。画素の R は枯れ度 [0, 1]、G・B・A は予約で 0。

| 資源名 | C++ 定数（`RenderGraphResourceNames`） | 既定の画素の大きさ | 既定の範囲 |
|---|---|---|---|
| `WorldField.DrynessNear` | `WorldFieldDrynessNear` | 0.25 m/画素 | 64 m × 64 m |
| `WorldField.DrynessFar` | `WorldFieldDrynessFar` | 2 m/画素 | 512 m × 512 m |

画素の大きさは `WorldFieldSettings::DrynessNearTexelMeters` / `DrynessFarTexelMeters` で変更でき、既定値は仮の値。各範囲は画素の大きさ × 256 で決まる。両層ともアクティブカメラの XZ 位置を `CalculateWorldFieldScroll` で画素格子にスナップし、原点を求める。主カメラがあるときは同じ ID のカメラだけを使い、主カメラがないときはアクティブカメラを使う。視線方向への前寄せは使わない。

履歴を使った蓄積はなく、通常は毎フレーム、現在の源から評価する。更新の準備ができず、記録済みの両層の座標が有効な場合は、最後に記録した画像と座標を再利用する。曲げの `WorldFieldSettings::bEnabled` が無効でも、枯れ度の `bDrynessEnabled` が有効なら枯れ度の層を扱う。

### 書き手の binding と定数

`Assets/Shaders/WorldField/WorldFieldDryness.comp` は set 0 の次の binding を使う。ワークグループは 8 × 8 × 1、dispatch は 32 × 32 × 2 で、z = 0 が近傍、z = 1 が遠景。

| set | binding | GLSL 宣言 | 用途 |
|---|---|---|---|
| 0 | 0 | `rgba8 writeonly image2D uDrynessNear` | 近傍の層への書き込み |
| 0 | 1 | `rgba8 writeonly image2D uDrynessFar` | 遠景の層への書き込み |
| 0 | 2 | `WorldFieldDrynessParams` | 層の原点・画素の大きさ・源の件数・解像度を持つ `std140` UBO |
| 0 | 3 | `WorldFieldDrynessSourceBlock` | 源の配列を持つ `std140` UBO |

C++ は `WorldFieldPass.h::WorldFieldDrynessParams`（`alignas(16)`）、GLSL は同名のブロック。合計 **32 バイト**で、C++ の `static_assert` が大きさ・整列・各フィールドの位置を検査する。

| オフセット | 大きさ | C++ フィールド / 型 | GLSL フィールド / 型 | 意味 |
|---|---|---|---|---|
| 0 | 8 | `NearOriginXZ: float[2]` | `NearOriginXZ: vec2` | 近傍の原点 XZ（m） |
| 8 | 4 | `NearTexelSize: float` | `NearTexelSize: float` | 近傍の画素の大きさ（m） |
| 12 | 4 | `SourceCount: uint32_t` | `SourceCount: uint` | 源の件数（0〜64） |
| 16 | 8 | `FarOriginXZ: float[2]` | `FarOriginXZ: vec2` | 遠景の原点 XZ（m） |
| 24 | 4 | `FarTexelSize: float` | `FarTexelSize: float` | 遠景の画素の大きさ（m） |
| 28 | 4 | `Resolution: uint32_t` | `Resolution: uint` | 各層の解像度（256） |

源の UBO は `WorldFieldPass.h::WorldFieldDrynessSourceBlock` と GLSL の同名ブロックで、`WorldFieldTypes.h::WorldFieldDrynessSource` / `WorldFieldDrynessSourceGpu` を 64 個並べた **4096 バイト**。1 個は `vec4` × 4 の **64 バイト**で、C++ の `static_assert` がブロックと要素の大きさ・要素の整列・各フィールドの位置を検査する。上限は `WorldFieldDrynessMaxSources = DrynessMap::DefaultGpuSourceLimit = 64`、GLSL は `WORLD_FIELD_DRYNESS_MAX_SOURCES = 64`。

| オフセット | 大きさ | C++ フィールド / 型 | GLSL フィールド / 成分 | 意味 |
|---|---|---|---|---|
| 0 | 8 | `CenterXZ: float[2]` | `CenterRadii.xy` | 源の中心 XZ（m） |
| 8 | 4 | `RadiusA: float` | `CenterRadii.z` | 内側の局所 X 半径（m） |
| 12 | 4 | `RadiusB: float` | `CenterRadii.w` | 内側の局所 Z 半径（m） |
| 16 | 4 | `CosYaw: float` | `RotationFalloffLevel.x` | 向きの余弦 |
| 20 | 4 | `SinYaw: float` | `RotationFalloffLevel.y` | 向きの正弦 |
| 24 | 4 | `Falloff: float` | `RotationFalloffLevel.z` | 外縁から 0 になるまでの幅（m） |
| 28 | 4 | `Level: float` | `RotationFalloffLevel.w` | 現在の枯れ度 [0, 1] |
| 32 | 16 | `Reserved0: float[4]` | `Reserved0: vec4` | 予約（0） |
| 48 | 16 | `Reserved1: float[4]` | `Reserved1: vec4` | 予約（0） |

### 消費側の定数と参照

C++ は `WorldFieldTypes.h::WorldFieldDrynessSampleConstants`（`alignas(16)`）、GLSL は `Assets/Shaders/Common/WorldField.glsl` の同名構造体。`std140` で合計 **32 バイト**。C++ の `static_assert` が大きさ・整列・各フィールドの位置を検査する。

| オフセット | 大きさ | C++ フィールド / 型 | GLSL フィールド / 型 | 意味 |
|---|---|---|---|---|
| 0 | 8 | `NearOriginXZ: float[2]` | `NearOriginXZ: vec2` | 近傍の原点 XZ（m） |
| 8 | 4 | `NearInvExtent: float` | `NearInvExtent: float` | 近傍の範囲（m）の逆数 |
| 12 | 4 | `bEnabled: uint32_t` | `bEnabled: uint` | 両層の座標定数が有効（1/0） |
| 16 | 8 | `FarOriginXZ: float[2]` | `FarOriginXZ: vec2` | 遠景の原点 XZ（m） |
| 24 | 4 | `FarInvExtent: float` | `FarInvExtent: float` | 遠景の範囲（m）の逆数 |
| 28 | 4 | `NearBlendMeters: float` | `NearBlendMeters: float` | 近傍の縁から遠景へ寄せる幅（m） |

`WorldFieldFrameState` は各層の原点・範囲・画素の大きさ・有効性を `DrynessNear` / `DrynessFar` に、混合幅を `DrynessNearBlendMeters` に持ち、`PhysicalLighting.PublishWorldField` で公開する。`BuildWorldFieldDrynessSampleConstants` は、両層の `bValid` が真で原点と範囲が有限、範囲が正であり、float 変換後の原点と範囲の逆数も有限なら、それらの値と混合幅を定数に写して有効にする。それ以外は全欄 0 を返す。

消費側は `Common/WorldField.glsl` の `SampleDryness(worldXZ)` だけを呼ぶ。include 前に `WORLD_FIELD_DRYNESS_NEAR_LAYER`、`WORLD_FIELD_DRYNESS_FAR_LAYER`（各 `sampler2D`）、`WORLD_FIELD_DRYNESS_CONSTANTS`（上記の定数）の 3 マクロを定義する。sampler は線形補間・`ClampToEdge` を使う。

無効時は 0。各層の UV は `(worldXZ - OriginXZ) * InvExtent` で、各軸の範囲は [0, 1)。近傍の外は遠景、両層の外は 0。近傍の内側では、縁からの距離を `NearBlendMeters` で割って [0, 1] に収めた重みで、遠景から近傍へ混ぜる。幅が正なら縁で遠景になり、幅の内側で連続に近傍へ寄る。幅が 0 の場合は近傍値をそのまま使う。幅は `WorldFieldSettings::DrynessNearBlendMeters` で変更でき、既定は 4 m。

### 源の受け渡し

`RenderWorld::SetWorldFieldDryness` は源を `RenderingCoordinator::SetWorldFieldDryness` へ渡す。coordinator は入力を無害化し、次の設定まで状態として保持して、`FramePacket::WorldFieldDrynessSources` へ値を写す。この配列は `ComputeSceneRevisionHash` に含めない。描画側は `ViewRenderContext::SnapshotWorldFieldDrynessSources` を通してパケットの配列を参照する。

## 13. Velocity の「履歴なし」の印

GBuffer の Velocity（RT4、`R16G16_FLOAT`）は通常の UV 差に加え、前フレームへ再投影できる履歴がない画素の印を格納する。正本は `Library/Core/Public/Rendering/VelocityContract.h`（`VelocityContract`）と `Assets/Shaders/Common/VelocityEncoding.glsl`。両者の定数を一致させ、シェーダーの符号化は `VelocityFromClip` / `VelocityNoHistory`、判定は `VelocityIsNoHistory` を通す。C++ の判定は `VelocityContract::IsNoHistory` を使う。

| 意味 | C++ 定数 | GLSL 定数 | 値 |
|---|---|---|---|
| 印の値（両成分） | `NoHistoryValue` | `VELOCITY_NO_HISTORY_VALUE` | `-32768.0` |
| 印の判定の境（両成分がこの値以下） | `NoHistoryDetectBelow` | `VELOCITY_NO_HISTORY_DETECT_BELOW` | `-16384.0` |
| 前フレームのクリップ座標の w の閾値 | `PreviousClipWMin` | `VELOCITY_PREVIOUS_CLIP_W_MIN` | `0.1` |
| 印の半精度ビット列（各成分） | `NoHistoryValueHalfBits` | `VelocityNoHistory()` の格納値 | `0xF800` |

前フレームが有効で、`previousClip.w <= VELOCITY_PREVIOUS_CLIP_W_MIN` なら印を書く。閾値の `0.1` は既定のカメラの `NearPlane` に合わせた仮の値で、カメラから動的に受け取る値ではない。正射影では w = `1` のため、この条件で印にならない。前フレームが無効なら `0` とする。ラスタ経路は `abs(fragCurrentClip.w) > 1e-6` も満たす場合だけ符号化し、それ以外は `0` とする。

印の条件に当たらない場合、`VelocityFromClip` は `(currentNdc - previousClip.xy / previousClip.w) * 0.5` を返す。ただし成分に NaN がある、または長さの 2 乗が `1e6` 以上なら `0` を返す。印の値は半精度で正確に表せ、通常の速度を無効化する処理より先に返す。

シェーダーのパスは `Assets/Shaders/` からの相対パス。

| 書き手 | 符号化の呼び出しと履歴の条件 |
|---|---|
| `Common/VisibilityResolve.glsl` | `VisComputeVelocity` が `RESOLVE_FLAG_PREVIOUS_VALID` を確認し、画素中心から求めた現在の NDC と前のクリップ座標を `VelocityFromClip` へ渡す |
| `Common/GBufferFragment.glsl` | `gbuffer.frag`・`gbuffer_alphaclip.frag` の本体。`mvp.frameParams.x > 0.5` と現在の w を確認して `VelocityFromClip` を呼ぶ |
| `megageometry.frag` | `ComputeVelocity` が `mvp.frameParams.x > 0.5` と現在の w を確認して `VelocityFromClip` を呼ぶ。デバッグ出力も同じ関数を使う |
| `foliage.frag` | `foliage.frameParams.x > 0.5` と現在の w を確認して `VelocityFromClip` を呼ぶ |
| `Grass/grass.frag` | `kGrassDrawFlagPreviousCamera` と現在の w を確認して `VelocityFromClip` を呼ぶ |

| 読み手 | 印の扱い |
|---|---|
| `temporal_aa.frag` | 動きに使う近傍の最も手前の画素か中心画素について、深度が `1` 未満かつ `VelocityIsNoHistory` が真なら履歴を捨て、今の色だけを使う。リアクティブも今の値を出す |
| `MotionBlurTileMax.frag` | `ShutterMotion` で印を判定し、動き `0` としてタイルの最大を求める |
| `MotionBlur.frag` | `ShutterMotion` で印を判定し、動き `0` として扱う |
| `RTGI/DiffuseIndirect.comp` | 再投影の 2 か所は印を知らず、`previousUV = currentUV - velocity`（もう一方は `uv - velocity`）を求める。印では `previousUV` が `[0, 1)` の外になるため履歴を捨てる |
| `gbuffer_debug.frag` | 印専用の判定はない。velocity 表示は `vec3(vec2(0.5) + velocity * debugParams.params.y, 0.5)`。通常の正の倍率では印の R・G が負となり、表示時のクランプで暗い色になる（B は `0.5`） |

新しい velocity の読み手を追加するときは、通常の速度として使う前に `VelocityIsNoHistory` を通し、履歴を捨てるか動き `0` とする扱いを登録する。

## 14. 環境の UBO（GPUEnvironmentParams）

C++ は `Library/Core/Public/Rendering/EnvironmentGpuParams.h::GPUEnvironmentParams`、GLSL は `Assets/Shaders/Common/WetSurface.glsl::EnvironmentGpuParams`。`std140`、16 バイト整列、合計 **48 バイト**。C++ の `static_assert` は大きさ・整列、`Wetness`・`GameTimeWrapped`・`CavityUniform`・`ExposureDefault`・`Discontinuity`・`Reserved` のオフセット、型の自明コピーと標準レイアウトを検査する。残りのオフセットは宣言順に 4 バイトずつ積算する。

| オフセット | C++ フィールド / 型 | GLSL の成分 | 意味 | 既定値 |
|---|---|---|---|---|
| 0 | `Wetness: float` | `weather.x` | 地表の濡れ。環境が無効なら 0 | 0 |
| 4 | `RainIntensity: float` | `weather.y` | 雨の強さ | 0 |
| 8 | `CloudCover: float` | `weather.z` | 雲量 | 0 |
| 12 | `GameTimeWrapped: float` | `weather.w` | 4096 秒で巻いたゲーム内時刻 | 0 |
| 16 | `CavityUniform: float` | `surface.x` | 一様の凹み | 0.5 |
| 20 | `ExposureDefault: float` | `surface.y` | 雨の遮蔽の既定値 | 1 |
| 24 | `Seed01: float` | `surface.z` | 種を 2^32 で割った値 | 0 |
| 28 | `Discontinuity: float` | `surface.w` | 不連続の印の下位 24 bit | 0 |
| 32〜47 | `Reserved: float[4]` | `reserved.xyzw` | 予約 | 全成分 0 |

`MakeDefaultEnvironmentGpuParams()` は濡れ 0・凹み 0.5・exposure 1 の既定値を返す。`PackEnvironmentGpuParams` は環境が無効ならその既定値を返す。有効なら入力を整え、雨量・濡れ・雲量を [0, 1] に収め、時刻を 0 以上にしてから `GameTimeWrapped = float(fmod(GameTimeSeconds, 4096))`、`Seed01 = float(double(Seed) / 2^32)`、`Discontinuity = float(DiscontinuityId & 0xFFFFFF)` として詰める。非有限の雨量・濡れ・雲量・時刻は既定値に戻す。

`RenderingCoordinator` は `packet->Scene.Environment` から `ViewRenderContext::EnvironmentGpu` を詰め、記録中のフレームの持ち主 `ViewRenderContext::FrameResources` からフレームに 1 回定数を借りて `ViewRenderContext::EnvironmentConstants` に置く。パスは `AcquireEnvironmentConstants` でその切れ端を引く。切れ端がなければ `FrameResources` から `EnvironmentGpu` を借り、持ち主もなければ無効な割り当てを返す。フレームごとに変わるこの切れ端を、材質の記述子のキャッシュの鍵に入れない。

GLSL は `NORVES_DECLARE_ENVIRONMENT_UBO(SET_INDEX, BINDING_INDEX)` をシェーダーで 1 回宣言し、`EnvironmentParamsBlock` の `u_Environment.params` から読む。各経路の宣言と C++ の定数は次のとおり。

| 経路 | set / binding | 段 | 読むシェーダー | C++ の定数 |
|---|---|---|---|---|
| `GBufferPass`（静的・インスタンス・スキン・切り抜き） | 0 / 13 | Pixel | `Common/GBufferFragment.glsl`（`gbuffer.frag`・`gbuffer_alphaclip.frag`） | `GBufferPass.cpp::EnvironmentBindingIndex` |
| 材質ごとの GBuffer プログラム | 0 / 13 | Pixel | 各プログラムの FS。`GBufferProgramRegistry.h` と `Common/GBufferFragment.glsl` の契約に従う | `GBufferPass.cpp::EnvironmentBindingIndex`（`GBufferPass` の set 0 の配置を共有） |
| MegaGeometry（材質・地形） | 0 / 24 | Pixel | `megageometry.frag` | `MegaGeometryPass.cpp::EnvironmentBindingIndex` |
| 可視バッファの解決（通常・dump・タイル） | 0 / 37 | Compute | `Common/VisibilityResolve.glsl`（`visbuffer_resolve*.comp` の 4 種） | `VisibilityResolvePass.cpp::EnvironmentBindingIndex` |
| `FoliagePass`（株） | 0 / 3 | Pixel | `foliage.frag` | `FoliagePass.cpp::kEnvironmentBinding` |
| 草のブレード | 0 / 6 | Pixel | `Grass/grass.frag` | `GrassPass.cpp::kGrassEnvironmentBinding` |
| デカール | 0 / 3 | Pixel | `decal.frag` | `DecalPass.cpp::kEnvironmentBinding` |
| 半透明の前方 | 0 / 19 | Pixel | `forward_transparent.frag` | `ForwardPass.cpp::EnvironmentBindingIndex`（15 は VT の要求バッファ、18 は空の放射輝度 LUT） |

通常メッシュ（材質ごとの GBuffer プログラムを含む）・MegaGeometry・可視バッファの解決は、書き込み直前のアルベド・粗さ・金属度・シェーディング法線に `ApplyWetSurfaceWithPuddle` を掛ける。`PuddleRainExposure` は画素のワールド位置を雨の遮蔽マップで調べ、屋根の下なら 0、雨に当たるなら 1 を返す。無効なマップや範囲外は開けていると扱う。環境の既定の当たり方にこの値を掛けた `exposure = surface.y * rainExposure` で濡れの当たり方を上書きし、濡れ、水たまりの順で適用する。

濡れはワールド空間の幾何法線（Y が上）と一様の凹み `surface.x` を使い、AO は凹みに使わない。水たまり用の凹み度は、地形のスプラットが有効な MegaGeometry・可視バッファのタイル解決では `TerrainSplatPuddlePotential` の PDL0、それ以外は `SamplePuddleFieldLayer` の水たまりの層から取る（16 節）。内部の `ApplyWetSurface` は `w = wetness * exposure * response` が 0 以下または NaN なら何も変えず戻る。最終の `w` は書き戻さず、`Material.a` は解決済みの `P0`、`Emissive.a` は DefaultLit なら返り値の水たまりの被覆、それ以外は解決済みの `P1` とし、ID と受け手の印も保つ。株と草の `P0` / `P1` は 0。`wetness = 0` では濡れと水たまりによる面の変更はなく、DefaultLit の被覆は 0 になる。

水たまりの式の正本は `Common/Puddle.glsl`。水位は `clamp((wetness - 0.3) / 0.7, 0, 1)`、閾値は `1 - 0.8 * 水位`。被覆は `smoothstep(閾値, 閾値 + 0.1, 凹み度)` に上向きの重み `smoothstep(0.90, 0.98, geomN.y)`・当たり方・0〜1 に収めた感度を掛ける。被覆 `c` に応じ、アルベドを `mix(1, 0.6, c)` 倍、粗さを `mix(0.1, 0.03, c)` へ重み `c` で補間、金属度を `1 - c` 倍、法線を幾何法線へ補間する。雨の強さ × 被覆を強度とする波紋を法線に足す。波紋は 0.35 m のセルを 2 層重ね、最大半径 0.15 m・波長 0.04 m・合計の傾き上限 0.14054、時刻は環境 UBO の `weather.w` を使う。

草・株・デカール・半透明の前方は環境の既定の当たり方 `surface.y` を使い、画素ごとの雨の遮蔽と水たまりを掛けない。草は `ApplyWetSurface`、ほかは `ApplyWetSurfaceFromEnvironment` を使う。

- `GBufferPass` と材質ごとのプログラムは、インスタンスの `P0`、材質の `P0`、`DefaultLit` かどうかを `ResolveWetSurfaceResponse` に渡す。MegaGeometry はインスタンスを未指定（-1）とし材質の `P0` を使う。半透明の前方もインスタンスを未指定（-1）とし、材質の `P0` を `TransparentForwardUBO::sceneColorParams.z` で渡す。材質がなければ -1 で感度 1 とし、ID を読まず `DefaultLit` として扱う。可視バッファの解決も材質があれば GBuffer と同じ優先で解決し、材質がなければ感度 1。幾何法線はラスタと同じ補間された頂点法線。MegaGeometry と可視バッファのデバッグ表示は濡らさない。
- 株は材質の `P0` を持たないため感度 1、幾何法線は画面微分から求めた面の法線（退化時は元の法線）。草も感度 1 で、環境の濡れを描画定数の `Surface.x` に詰め、頂点が LayerA の moisture により場所ごとに調整した `fragWetness` を使う。種の濡れ色・濡れ粗さを適用してから `ApplyWetSurface` を直接呼ぶ。
- デカールは感度 1、幾何法線を 0 として水膜を起こさず、濡れによるアルベドの暗化だけを書く。濡れによる粗さ・法線の変更は書かず、従来のデカール法線の合成は保つ。受け手の `P0` が 0 でもデカールだけは暗くなる。半透明の前方は上記の感度で照明前に濡れを適用する。

フレームの環境 UBO を借りられないとき、`GBufferPass`・MegaGeometry・可視バッファの解決・半透明の前方は、初期化時に `MakeDefaultEnvironmentGpuParams()` を 1 回書き、以後は書き換えない 48 バイトの常駐 UBO を結ぶ。`FoliagePass` と草は対象の描画を飛ばす。デカールは呼び手から環境の割り当てが渡されなければ、フレームの持ち主から既定値を借りる。

RTGI・パストレーサーの命中面には、この環境 UBO による濡れ処理を適用しない。RTGI はインスタンスの基本色、パストレーサーはインスタンスの色と材質テクスチャから命中面を作るため、ラスタとは方式が異なる。RTGI の地形スプラットでは例外として、既存の `TerrainApplyWetAlbedo` と `TerrainSplatWetness` による別方式の濡れ色を命中面へ反映する。

## 15. MegaGeometry のインスタンスの表（席の表と描く順の表）

両方とも `std430`、16 バイト整列で、1 行は **144 バイト**。C++ の `MegaGeometryPass.h` にある `GPUMegaSlotRow` と `GPUMegaInstance` の大きさは `static_assert` で検査する。色味と種は両方の行の位置 136・140 に揃え、`offsetof` の `static_assert` で互いの位置が一致することを検査する。

### 席の表

C++ は `MegaGeometryPass::GPUMegaSlotRow`、GLSL は `mega_scene_scatter.comp::SceneSlotRows` の `slotWords: uvec4[]`。席ごとに 9 個の `uvec4` を使う。通常の席の番号は `MegaGeometryProxy::SceneSlot` で、0 は空行用。席のないインスタンスや席が重複した場合はローカルの席を割り当てる。

| オフセット | 大きさ | C++ フィールド / 型 | GLSL の語・成分 | 意味 |
|---|---|---|---|---|
| 0 | 64 | `WorldMatrix: float[16]` | `slotWords[slot * 9 + 0..3]` | 現在のワールド変換 |
| 64 | 64 | `PreviousWorldMatrix: float[16]` | `slotWords[slot * 9 + 4..7]` | 前のフレームのワールド変換 |
| 128 | 4 | `Reserved0: uint32_t` | `slotWords[slot * 9 + 8].x` | 予約（0） |
| 132 | 4 | `Reserved1: uint32_t` | `slotWords[slot * 9 + 8].y` | 予約（0） |
| 136 | 4 | `Tint: uint32_t` | `slotWords[slot * 9 + 8].z` | インスタンスの色味 |
| 140 | 4 | `VariationSeed: uint32_t` | `slotWords[slot * 9 + 8].w` | 個体差の種 |

### 描く順の表

C++ は `MegaGeometryPass::GPUMegaInstance`、GLSL は `Common/MegaInstanceTable.glsl::MegaInstanceRow`。CPU はこの形を直接書かず、`mega_scene_scatter.comp` が席の表と描く順の記録から組み立てる。`MegaInstanceRowBuffer` の `megaInstanceRows[]` を `LoadMegaInstance` や `LoadMegaInstanceTint` で読む。ラスタの `megageometry.vert` と可視バッファ解決の `Common/VisibilityResolve.glsl` も読み手である。

| オフセット | 大きさ | C++ フィールド / 型 | GLSL フィールド / 成分 | 意味 |
|---|---|---|---|---|
| 0 | 64 | `WorldMatrix: float[16]` | `world: mat4` | 現在のワールド変換 |
| 64 | 64 | `PreviousWorldMatrix: float[16]` | `previousWorld: mat4` | 前のフレームのワールド変換 |
| 128 | 4 | `MeshIndex: uint32_t` | `info.x` | メッシュの表の番号 |
| 132 | 4 | `FirstGroup: uint32_t` | `info.y` | 最初のワークグループの通し番号 |
| 136 | 4 | `Tint: uint32_t` | `info.z` | インスタンスの色味 |
| 140 | 4 | `VariationSeed: uint32_t` | `info.w` | 個体差の種 |

### 記録の形と更新

`MegaGeometryPass::UpdateResidentSceneTables` が変更分を staging の記録に書き、`mega_scene_scatter.comp` が GPU の常駐表を更新する。段 0 の記録は 1 件あたり 10 個の `uvec4` で、先頭の `.x` が席の番号、続く 9 個が席の行。段 1 の描く順の記録は 1 件あたり 2 個の `uvec4` で、先頭が `(描く順の番号, 席の番号, メッシュの表の番号, 最初のワークグループ)`、次は予約。段 0 と段 1 の間にはバリアがあり、段 1 で GPU が変換・色味・種を席の行から描く順の行へ写す。

### 色味と種

色味は RGBA8 を詰めた `uint32_t` で、R が下位 8 bit、以降 G・B・A の順に並ぶ。`unpackUnorm4x8` と同じ並びで、A は現在使わない。既定の `0xFFFFFFFF` は白で、RGB が白（下位 24 bit がすべて 1）なら乗算せず、導入前とビット単位で同じ色を使う。白以外は `megageometry.vert` の `fragObjectColor`、または `Common/VisibilityResolve.glsl::VisObjectColor` で区間の材質の基本色に色味の RGB を掛け、その後アルベドのテクスチャと濡れを適用する。RTGI・パストレーサーの命中面にはこの色味を掛けない。

種は散布入力の `VariationSeed` で、入力が 0 の場合は `MegaGeometryScatterComponent::DefaultVariationSeed(Seed, 入力の番号)` の結果を使う。1 本ずつの `MegaGeometryComponent` は 0。既定の種の式は次のとおりで、各演算は `uint32_t` で行う。

```cpp
uint32_t h = scatterSeed ^ (inputIndex * 0x9E3779B9u);
h ^= h >> 16;
h *= 0x85EBCA6Bu;
h ^= h >> 13;
h *= 0xC2B2AE35u;
h ^= h >> 16;
return h;
```

`Common/MegaInstanceTable.glsl::MegaVariationPhase(seed)` は `(seed >> 8) / 2^24` を返す。範囲は [0, 1) で、頂点アニメの `instanceParams` に風の位相を渡すときに使う。現在の可視バッファ解決とラスタは、MegaGeometry の `instanceParams` に引き続き `vec4(-1.0)` を渡す（8 節）。

1 本の非表示は GPU の表に印を持たず、散布がその 1 本の proxy を `SceneView` から取り下げるため、描く順の表・影・RT のいずれにも入らない。

## 16. 水たまりの定数・束縛・世界フィールド

### PuddleSurfaceGpuParams

C++ は `Library/Core/Private/Rendering/PuddleSurfaceParams.h::PuddleSurfaceGpuParams`、GLSL は `Assets/Shaders/Common/Puddle.glsl` の同名構造体。`std140`、16 バイト整列、合計 **48 バイト**。C++ の `static_assert` が大きさ・整列・各欄の位置を検査する。GLSL は `NORVES_DECLARE_PUDDLE_SURFACE_UBO(SET_INDEX, BINDING_INDEX)` で `PuddleSurfaceParamsBlock` を宣言し、`u_PuddleSurface.params` から読む。

| オフセット | C++ フィールド / 型 | GLSL フィールド / 型 | 意味（x / y / z / w） |
|---|---|---|---|
| 0 | `OverheadWindow: float[4]` | `overheadWindow: vec4` | 雨の遮蔽マップの窓の原点 X / Z（m）/ 範囲（m）/ テクセル幅（m） |
| 16 | `OverheadDepth: float[4]` | `overheadDepth: vec4` | 上端 Y（m）/ 深度の範囲（m）/ 解像度 / 有効 1・無効 0 |
| 32 | `PuddleField: float[4]` | `puddleField: vec4` | 水たまりの層の原点 X / Z（m）/ 範囲の逆数（1/m）/ 有効 1・無効 0 |

`MakePuddleSurfaceGpuParams` は現在のフレーム・ビューに属する有効な雨の遮蔽マップの窓と深度を写し、それ以外は先頭 2 欄を 0 にする。水たまりの座標は `PhysicalLighting.WorldField.PuddleNear` が有効で、原点・正の範囲と変換結果が有限なときに写し、それ以外は末尾欄を 0 にする。`AcquirePuddleSurfaceConstants` はフレームの持ち主から定数を借りる。

### 書き手と照明の束縛

すべて set 0。各行の 3 番号は水たまりの UBO（`ConstantBuffer`）/ 雨の遮蔽マップ（`sampler2D`）/ 水たまりの層（`sampler2D`）の順。画像は C++ でテクスチャと sampler を同じ binding に束ねる。

| 経路・段 | UBO | 雨の遮蔽 | 水たまりの層 | GLSL / C++ の正本 |
|---|---|---|---|---|
| 通常メッシュ・材質ごとのプログラム・Pixel のみ | 15 | 16 | 17 | `Common/GBufferFragment.glsl` / `GBufferPass.cpp` |
| MegaGeometry・地形・Pixel | 26 | 27 | 28 | `Common/MegaGeometryFragment.glsl` / `MegaGeometryPass.cpp` |
| 可視バッファの解決（通常・タイル・各 dump）・Compute | 39 | 40 | 41 | `Common/VisibilityResolve.glsl` / `VisibilityResolvePass.cpp` |
| 照明・Pixel | 31 | 32 | 束縛なし | `lighting.frag` / `LightingPass.cpp` |

書き手の C++ の定数名は順に `PuddleSurfaceBindingIndex` / `OverheadHeightMapBindingIndex` / `PuddleFieldBindingIndex`。照明は `LIGHTING_PUDDLE_SURFACE_BINDING = 31` / `LIGHTING_OVERHEAD_HEIGHT_MAP_BINDING = 32`。照明は DefaultLit の P1 を 0〜1 に収め、空の鏡面の遮蔽へ `mix(1, OverheadHeightMapSkyVisibility(...), 被覆)` を掛けるため、水たまりの層そのものは読まない。

### WorldField.PuddleNear

`WorldFieldPass.cpp` が描画グラフ名 `WorldField.PuddleNear`（`RenderGraphResourceNames::WorldFieldPuddleNear`）で公開する。形式は **R8_UNORM**、解像度は `WorldFieldPuddleResolution = 256` × 256、`WorldFieldPuddleDefaultTexelMeters = 0.25` m/画素、範囲は 64 m × 64 m。R は水たまりの凹み度 0〜1。カメラの XZ に追従して原点を画素格子へ合わせ、源・原点・地形が同じなら記録済みの層を再利用する。

`RenderWorld::SetWorldFieldPuddles` は `WorldFieldTypes.h::WorldFieldPuddleSource` の配列を `RenderingCoordinator::SetWorldFieldPuddles` へ渡す。源は最大 64 個の手置きの楕円で、世界の中心 XZ・半径 A/B（m）、回転の cos/sin、縁の減衰幅 `Falloff`、凹み度 `Level` を持つ。`WorldFieldPuddle.comp` は地形スプラット配列の層 2 の R（PDL0）の写しと、各楕円の `Level * shape` の最大を取り、0〜1 に収めて層へ書く。履歴の蓄積ではない。

| set / binding | GLSL 宣言 | C++ の束縛 |
|---|---|---|
| 0 / 0 | `layout(r8) writeonly image2D uPuddleNear` | `BindStorageTexture(0, m_PuddleTexture)` |
| 0 / 1 | `std140 uniform WorldFieldPuddleParams` | `BindConstantBuffer(1, paramsAllocation.Buffer, ...)` |
| 0 / 2 | `sampler2DArray uTerrainSplat` | `BindTexture(2, terrainState.Texture または代替画像)` と `BindSampler(2, m_PuddleTerrainSampler)` |

`WorldFieldPuddleParams` は `WorldFieldPass.h` と GLSL に対応し、合計 **4144 バイト**。0: `OriginXZ`（vec2）、8: `TexelSize`（float）、12: `SourceCount`（uint）、16: `TerrainMap`（vec4。最小 XZ と広さの逆数）、32: `TerrainEnabled`（uint）、36・40: 地形の最大 X・Z（float）、44: 予約（uint）、48: `Sources[64]`。源は 1 個 **64 バイト**で、0: `CenterRadii`（中心 XZ・半径 A/B）、16: `RotationFalloffLevel`（cos/sin・Falloff・Level）、32・48: 予約の vec4。C++ の源は `WorldFieldPuddleSource`、GLSL は `WorldFieldPuddleSourceGpu`。ワークグループは 8 × 8 × 1、dispatch は 32 × 32 × 1。

`SamplePuddleFieldLayer` は `uv = (worldXZ - 原点XZ) * 範囲の逆数` で R を読み、無効時と [0, 1) の外では 0。縁から 2 m の幅で 0 へ減衰させる。水たまりの層は非地形のメッシュにも地形 PDL0 の写しを供給する。R8 の storage・sampled の対応と `bShaderStorageImageExtendedFormats` がすべてそろわなければ、この層は無効になる。
