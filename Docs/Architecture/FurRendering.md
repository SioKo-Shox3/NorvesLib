# スキンのファーの殻描画

スキン済みの体の面を複数の殻として GBuffer に描き、各殻の断片を UV のセルごとに切り抜いて毛を表す。描画の選択は `GBufferPass.cpp`、距離による殻数は `FurShellLod.cpp`、頂点と断片は `skinned_fur_gbuffer.vert`・`skinned_fur_gbuffer.frag` が担う。GPU の構造体・GBuffer の成分・binding の詳細は [GPU の割り当て表](GpuLayoutRegistry.md) の 1・2・11 節を参照する。

## 殻の配置と速度

殻 0 は体の皮膚面。殻数を `N`、インスタンス番号を `i` とすると、`N > 1` では高さの割合 `h = i / (N - 1)`、`N = 1` では `h = 0` とする。スキニングと頂点アニメを適用した世界位置を `p`、変換後に正規化した世界法線を `n`、毛の力を `F`、`MaxLengthMeters * LengthScale` を `L` とすると、殻の位置は `p + normalize(n + F * h) * L * h`。向きがゼロに近い場合は `n` を使う。`F` は描画の毛の力 `furDrawForce`（現在は 0）に濡れの垂れ `(0, −1, 0) × kSag × W` を足したものである。垂れの応答が 0 または `W = 0` なら `F` は 0 で、殻は体の法線に沿って押し出される。

速度は、前フレームの骨・ワールド変換・時刻で求めた体の位置に、**現在と同じ**殻の変位を足して前のクリップ座標を作り、現在との差から求める近似である。前フレームの毛の向きや殻数の変化を再計算しない。カメラの履歴がない場合は速度を 0 にする。

## 描画の選択と順序

`SkinnedMeshComponent` が `ShadingModel::Fur` の材質を slot ごとに写し、`SkinnedDrawCommands` は slot と材質が一致したスキンの描画へ `Skinned.Fur` を付ける。`GBufferPass` はその写しのあるスキンだけを殻の対象にし、登録済みの `fur_shell` プログラムを選ぶ。複数の殻は同じ頂点・骨パレットを使う `DrawIndexedInstanced` で一度に描き、殻が 1 枚なら通常の `DrawIndexed` とする。

可視バッファの解決を使える場合、Fur は ID のラスタと材質解決から除き、解決後の GBuffer に重ねる。解決を使わない場合や使えず従来の経路に戻る場合は、標準の不透明物の後に重ねる。専用プログラムやパイプラインを使えないときは標準へ戻し、殻の写しを外して体の面だけを描く。標準のパイプラインも得られない場合は描画を飛ばす。影の描画は殻を増やさず体の面だけを使う。

## 距離と視錐台

カメラからスキンの世界境界の中心までの距離で `ComputeFurShellCountContinuous` を呼び、隣接する距離と殻数を線形補間した連続値 `Nf` を 1〜64 に収める。世界境界を使えないときはワールド変換の平行移動を距離の基準にする。負の距離は 0、非有限の距離は材質の `LayerCount` を使う。

材質に距離段がないときは次の仮の表を使い、それぞれの殻数を `LayerCount` で頭打ちにする。最初の距離より近くは最初の値、最後より遠くは最後の値を保つ。

| 距離（m） | 殻数 |
|---|---|
| 3 | 32 |
| 10 | 16 |
| 25 | 8 |
| 50 | 4 |
| 100 | 1 |

材質に段があるときは最大 4 段を使う。最初の距離が 0 より大きい場合だけ `(0 m, LayerCount)` を先頭に足し、その間も補間する。距離は昇順、殻数は減少または同数で、各殻数は材質の上限以下として検査される。

描く殻数は `N = ceil(Nf)`、最後の殻を残す割合は `N > 1` なら `Nf - (N - 1)`、`N = 1` なら 1。近い毛は最後の殻のセルを固定の乱数で間引き、遠い毛は被覆率にこの割合を掛ける。

スキンの局所境界から世界境界を求められたときは、各軸を `MaxLengthMeters * LengthScale * max(1, 使用中の領域の LengthScale)` だけ広げ、視錐台外なら殻ごと落とす。境界を作れない場合にこの拡張判定はできない。

## 毛の断片と GBuffer

Length の R、Flow の RG、DensityClump の RG、Region の R を読み、未解決のマップにはそれぞれ 1、`(0.5, 0.5)`、`(1, 1)`、0 を使う。Region は補間せず `texelFetch` で読み、ID を領域表の範囲に収める。Flow は `2 * rg - 1` で UV 空間の向きに戻し、余接フレームで世界空間の接線に変換して正規化する。向きが中央付近または退化した場合は余接フレームか法線から作った基底を使う。

セル座標は `UV * StrandDensity * DensityClump.r`。整数セルのハッシュから毛の中心と長さを決める。長さの割合の上限を `maxLength = Length.r * region.LengthScale` とし、各毛の長さを `maxLength * mix(0.6, 1.0, random)` とする。根元・毛先の半径と細りの指数から `t = clamp(h / strandLength, 0, 1)`、`radius = mix(TipRadius, RootRadius, pow(1 - t, ThicknessExponent)) * region.ThicknessScale` を求める。

殻 0 以外では、密度または `maxLength` が 0 以下なら破棄する。画素のセル座標の微分の最大が 0.7 以下なら毛の長さより高い殻と、セル内の円の外側を破棄する。円の直径の投影幅は最低 1 画素まで広げ、元の幅と広げた幅の比を被覆率として切り抜く。0.7 より大きい場合は近似した残存率 `clamp((maxLength - h) / (0.4 * maxLength), 0, 1)` と `min(1, PI * radius * radius)` を掛け、被覆率の期待値として切り抜く。TAA 適用時は画素と位相による Hash、それ以外は閾値 0.5 の Test を使う。

殻 0 は毛の切り抜きを通さず皮膚面として残す。アルベドには根元の暗さから毛先の明るさへの補正と、根元ほど強い下毛色の混合を適用する。AO は `mix(AoRoot, 1, pow(h, AoExponent))`、法線は体の法線から流れの接線へ `Tilt * h` で補間して正規化する。GBuffer には金属度 0、材質の粗さ（未指定は 0.5）、この AO、`P0` に材質と個体から解決した濡れ度、`P1` に書き込み法線を基準とする流れの角を書く。`P1` の符号化・復号は `Common/FurShading.glsl` の `FurEncodeFlowAngle`・`FurDecodeFlowTangent` が担う。C++ の参照実装 `Library/Core/Private/Rendering/FurGBufferEncoding.cpp` と同じ式に保つ。発光 RGB は 0 である。

マップは既存の `set 0` の binding 3〜6 を Length・Flow・DensityClump・Region に読み替える。これは GR26 が材質ごとのテクスチャ枠を持つまでの暫定である。

## 濡れ

画素の濡れは `w = W × s`。`W` はスキンのインスタンスの `P0` を優先し、指定がなければ材質の値を使って 0〜1 に収めた濡れ度。`s` は Region マップの ID から領域の表を引いた `WetSusceptibility` を 0〜1 に収めた値である。殻 0 の皮膚も含む Fur の画素だけに適用し、GBuffer の `P0` に画素の `w` を UNORM8 で書く。

`lum = 0.2126 R + 0.7152 G + 0.0722 B` とすると、色と粗さの式は次のとおり。アルベドの式は明るさの補正と下毛色の混合の後に適用し、粗さには材質の粗さ `furShellParams.w` を使う。式は `Common/SurfaceWetness.glsl` に材質に依らない形で置き、地面・草・岩の濡れにも使える。

```text
albedo' = mix(albedo, vec3(lum), kDesat × w) × mix(1, kDark, w)
roughness' = mix(roughness, kWetRough, w)
```

長さは `L' = L × mix(1, kShort, w)`、毛の半径は `r' = r × (1 + kThick × w)` とする。近い画素では毛先ほど束を寄せる。毛の中心を、一束の UV 幅 `ClumpSize` に相当する粗い格子（幅は少なくとも 1 セル）の塊の中心へ `kClump × w × G × h` の割合で寄せ、毛のセルの内側に収める。`G` は DensityClump マップの G で、マップがなければ 1、`h` は殻の高さの割合である。垂れは頂点で毛の力に `(0, −1, 0) × kSag × W` を足す。照明では GBuffer の `Albedo.a` から復号した `g = kGloss × w` を使い、Kajiya-Kay の 2 つのローブの指数と強さに `1 + g` を掛ける。

| 係数 | `FurParams` の欄との対応 |
|---|---|
| `kDark` | `1 − WetDarken` |
| `kDesat` | `WetDesaturate` |
| `kWetRough` | 粗さ × `(1 − WetRoughness)` |
| `kShort` | `1 − WetLengthCollapse` |
| `kThick` | `WetThicken` |
| `kClump` | `WetClump` |
| `kSag` | `WetSag` |
| `kGloss` | `WetGloss`（画素の `g = kGloss × w` を `Albedo.a` で照明へ渡す） |

`FurParams` の濡れの欄の既定値はすべて 0 で、各材質の応答を加えない。

`PackFurMaterialGpuParams` は濡れの各応答に `W` を掛けて `wetGeometry`・`wetShading` に詰める。各補間の項は `w` について一次なので、FS は掛け済みの係数と `s` を使って上の `w = W × s` の式を求める。垂れだけは頂点で `s` を読まず、`W` のまま使う。

材質の濡れの応答がすべて 0、または `W = 0` のとき、FS の色・粗さ・毛の切り抜きは濡れを入れる前とビット単位で同じになるよう、差の形で計算し `×1`・`+0` にする。照明は Fur の `P0` を読まず `Albedo.a` の `g` を使う。`WetGloss = 0` または `W = 0` なら `g = 0`・`A = 1.0` となり、照明の掛け算は `×1` で濡れを入れる前と同じになる。応答が 0 でも `W > 0` なら `P0` は `w` になるが、照明の結果には影響しない。

毛の FS は `wetShading.w = WetGloss × W` に領域の `s` を掛けて `g = kGloss × w` とし、Fur の画素の `Albedo.a` に `A = 1 − g × 252/255` を書く。照明は Fur の画素だけ `g = (1 − A) × 255/252` を復号する。`g` の分解能は `1/252`、`A` の下限は `3/255` で空の判定（`A < 0.01`）と重ならない。`g = 0` では `A = 1.0`・復号値 0・倍率 1 がいずれもちょうどになる。汎用の GBuffer・MegaGeometry の書き手は Fur の ID で `A ≥ 0.01` なら 1.0 にそろえ、テクスチャの alpha によるつやを防ぐ。空の印は保つ。

基準のアルベドは乾いたきれいな色を前提とする。AI 生成のアルベドに光や濡れが焼き込まれていると暗化が二重に掛かる。SSR は Fur を飛ばすため、濡れ毛のつやは直接光と IBL で出す。GBuffer の Albedo は 8 bit の線形なので、暗化した暗い毛色では階調が不足する可能性がある（`G6-GR35-WET-CHECK` の撮影で見る）。

合成のファントムの材質に持たせる値の出発点は次のとおりで、エンジンの既定には入れない。腹と顔の色 `#D8DAD4` は線形 RGB で約 `(0.687, 0.701, 0.659)`、線形の輝度で約 0.70。`w = 1` で約 0.43 になる `kDark ≒ 0.62` は、提案「色と明度のルール」（未定）の出発点である。

| 欄 | 値 | 意味 |
|---|---:|---|
| `WetDarken` | 0.38 | `kDark = 0.62` |
| `WetDesaturate` | 0.3 | 仮 |
| `WetRoughness` | 0.3 | 乾いた粗さ 0.5 → 濡れて 0.35 |
| `WetGloss` | 1.0 | 2 つのローブの指数と強さの倍率 `1 + w` |
| `WetLengthCollapse` | 0.3 | `kShort = 0.7` |
| `WetThicken` | 0.5 | 仮 |
| `WetClump` | 0.6 | 仮 |
| `WetSag` | 0.3 | 仮 |

ファントムの材質 `Assets/Materials/Fur/phantom.furmat.json` は `G6-GR91-FUR-MATERIAL-ASSET` が書式とともに置く。そのときにこれらの値を入れる。

## 照明

毛の向き `T` は、GBuffer の `P1` の流れの角と書き込み法線 `N` から `FurDecodeFlowTangent` で復元する。Duff らの基底は `z = 0` で符号が反転する継ぎ目を持つが、書き込みと読み出しで同じ法線を使うので一致する。以下、`V` は点からカメラ、`L` は点から光源への正規化した向き、`H = normalize(V + L)` はハーフベクトルである。

直接光の拡散は、`clamp((N·L + w) / (1 + w), 0, 1) × AO` を拡散 BRDF・光・影に掛ける。ここで `w = FurWrap`、AO は SSAO を重ねる前の GBuffer の材質 AO である。毛の AO は殻の自己遮蔽なので直接光にも掛ける。ほかの材質の直接光には AO を掛けない。

鏡面は Kajiya-Kay の 2 ローブで、`Ti = normalize(T + N × shift_i)`、`sinTH_i = sqrt(max(1 - (Ti·H)², 0))` とする。各ローブは `smoothstep(-1, 0, Ti·H) × sinTH_i^e_i × (e_i + 2) / (2π)`。1 つめは色なし、2 つめには毛色を掛け、それぞれの強さで加算してから光・`max(N·L, 0)`・影を掛ける。指数には下限を適用する前の GBuffer の粗さ `r` を使い、`α = r²`、`e1 = clamp(2 / max(α², 1e-6) - 2, 1, 2048)`、`e2 = max(e1 × FurSecondaryExponentScale, 1)` とする。`FurApplyWetLobes` は GBuffer の `Albedo.a` から復号した `g` を使い、2 つのローブの指数と強さに `1 + g` を掛ける。

逆光の透過は `pow(saturate(V·−L), p) × (1 - AO) × 毛色 × 光 × 強さ × 影` を拡散へ足す。AO は直接光と同じ材質 AO、`p = FurTransmissionPower` である。

環境光（IBL・DDGI・RTGI）は既存の式を使い、面の補正で粗さを少なくとも `FurMinSpecularRoughness` まで上げる。SSAO は殻の深度の段差で荒れるため、Fur の画素では材質 AO に `mix(1, SSAO, FurSsaoWeight)` を掛ける。SSR は Fur の画素を飛ばす。

照明定数の C++ の欄は `ShadingModelLightingConstants`、既定値はすべて仮で、`LightingPass::SetShadingModelLightingConstants` で差し替える。UBO の配置と転送時の値の補正は [GPU の割り当て表](GpuLayoutRegistry.md) を参照する。

| C++ の欄 | 既定値 | UBO の置き場 | 意味 |
|---|---:|---|---|
| `FurWrap` | 0.5 | `shadingModelParams[0].x` | 拡散のラップ量 |
| `FurMinSpecularRoughness` | 0.6 | `shadingModelParams[0].y` | 面の鏡面粗さの下限 |
| `EyeSpecularBoostPerP0` | 3.0 | `shadingModelParams[0].z` | Eye の P0 あたりの鏡面増分 |
| `FurPrimaryShift` | 0.1 | `shadingModelParams[1].x` | 1 つめのローブのずらし |
| `FurSecondaryShift` | -0.1 | `shadingModelParams[1].y` | 2 つめのローブのずらし |
| `FurPrimarySpecularStrength` | 0.05 | `shadingModelParams[1].z` | 1 つめのローブの強さ |
| `FurSecondarySpecularStrength` | 0.05 | `shadingModelParams[1].w` | 2 つめのローブの強さ |
| `FurSecondaryExponentScale` | 0.5 | `furShadingParams.x` | 2 つめの指数の倍率 |
| `FurTransmissionPower` | 8.0 | `furShadingParams.y` | 逆光の透過の指数 |
| `FurTransmissionStrength` | 1.0 | `furShadingParams.z` | 逆光の透過の強さ |
| `FurSsaoWeight` | 0.3 | `furShadingParams.w` | Fur の画素に SSAO を掛ける割合 |

`shadingModelParams[0].w` は予約（0）である。

## 既知の限界

- 可視バッファの解決後に重ねる物は、その可視判定に使う HZB の遮蔽物にならない。影も殻を含まない。
- 殻には雨など環境の濡れ（`Common/WetSurface.glsl`）を適用しない。濡れはインスタンスの `W` と材質の応答で掛ける。
- 濡れによる垂れ以外の毛の力は現在 0。風・重力・体の動きによる力は `G6-GR34-COAT-SCENE` の範囲である。速度の前フレームの位置には現在の `W` による変位を含む殻の変位を流用する。
- TAA のないビューでは切り抜きを Test（閾値 0.5）にするため、被覆率が 0.5 未満の細い毛や遠い毛は消える。Hash の時間的な平均は TAA を前提とする。
- 密度マップの R でセル自体の大きさが変わり、値が場所によって変化するとセルが歪む。DensityClump の G と束の幅は濡れの束でだけ使い、乾いた束の強さ `ClumpStrength` は使わない。Flow の長さも整い度としては使わない。
- 頂点の押し出しは全体の `L` に基づく。Length と領域の長さ倍率は断片の残存判定に使い、領域の倍率が 1 を超えても最外殻を `L` より外へ押し出さない。濡れによる長さの縮みも断片の破棄で行い、頂点の押し出しは縮めない。
- アルベドを含むテクスチャは通常の `texture` で読み、sparse の常駐確認・粗いミップへの退避・VT フィードバックを行わない。アルベドが VT の場合、未常駐領域の表示を保証できない。
- アルベドの alpha や材質の `BaseColor`・被覆率、法線・粗さ・発光の各テクスチャは殻シェーダーでは使わない。殻 0 を含め出力の `Albedo.a` には濡れのつや `g` を符号化する。
- 毛の FS（`skinned_fur_gbuffer.frag`）は材質の一つの粗さ（`furShellParams.w`）に濡れを適用し、領域表の `FurMaterialRegion::lobes` を書かないため、領域ごとのハイライトの違いはまだ出ない。
- 束の寄せは毛が自分のセルの中でしか判定されないため、中心をセルの内側に収める。束感は主に塊の境目の分け目として出る。遠い画素の被覆の期待値の分岐には束の寄せがない。
- 垂れには領域の濡れやすさを掛けない。
- 逆光の透過は `1 - AO` を掛けるため、AO が 1 に近い毛先ほど弱くなる。
- パストレーサーと RT にはスキンが入らないため、毛も対象外である（GR104）。

## 人が見るときの見どころ

今の各シーンには Fur の材質がまだない。毛のシーンは `G6-GR34-COAT-SCENE` が作る。Fur の材質を張ったスキンのキャラクターを用意した場合は、体の輪郭と毛の流れ、根元から毛先の色と遮蔽、動いたときの追従、カメラの距離による殻数の切り替わり、画面端での消え方を見る。TAA のあるビューとないビューでは、細い毛や遠い毛の残り方も見比べる。逆光で縁が明るくなるか、流れに沿って 2 本のハイライトの帯が出て 2 本目に毛色が付くか、SSAO の on/off で毛の暗さの荒れが少ないか、SSR を on にしても毛の画素に SSR の反射が乗らないかを見る。濡れ度を上げたとき、毛が暗く灰に寄り、毛先が束になって短く太く垂れ、ハイライトの帯が細く強くなるかを見る。`W = 0` で以前と同じかも見る（撮影・確かめは `G6-GR35-WET-CHECK`）。
