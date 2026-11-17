// CPU の Rendering/GpuParticle/GpuParticleTypes.h と Particle/VfxSimulationMath.h と同じ並び・式。
// 並びや式を変えるときは CPU と GLSL の両方を同時に変える。
#ifndef NORVES_PARTICLE_COMMON_GLSL
#define NORVES_PARTICLE_COMMON_GLSL

// GpuParticleFrame.h の kGpuParticlePoolCapacity と同じ値。
const uint kGpuParticlePoolCapacity = 262144u;
// GpuParticleDraw.h の kGpuParticleDrawBucketCount と同じ値。
const uint kGpuParticleDrawBucketCount = 4u;

// 粒が生存している旗。
const uint kGpuParticleFlagAlive = 1u << 0u;
// 粒が衝突面に留まっている旗。
const uint kGpuParticleFlagStuck = 1u << 1u;
// 粒が有効化されている旗。
const uint kGpuParticleFlagActive = 1u << 2u;
// 子エミッタが無いことを示す配列添字。
const uint kGpuParticleInvalidEmitter = 0xFFFFFFFFu;
// 初期位置の形を使う旗。
const uint kGpuEmitterFlagShape = 1u << 0u;
// 抗力を使う旗。
const uint kGpuEmitterFlagDrag = 1u << 1u;
// 風を使う旗。
const uint kGpuEmitterFlagWind = 1u << 2u;
// 乱れを使う旗。
const uint kGpuEmitterFlagTurbulence = 1u << 3u;
// 粒の有効化を使う旗。
const uint kGpuEmitterFlagActivation = 1u << 4u;
// 平面衝突を使う旗。
const uint kGpuEmitterFlagCollisionPlane = 1u << 5u;
// 高さ場衝突を使う旗。
const uint kGpuEmitterFlagCollisionHeightField = 1u << 6u;
// 深度衝突を使う旗。
const uint kGpuEmitterFlagCollisionDepth = 1u << 7u;
// 大きさの寿命曲線を使う旗。
const uint kGpuEmitterFlagCurveSize = 1u << 8u;
// 色の寿命曲線を使う旗。
const uint kGpuEmitterFlagCurveColor = 1u << 9u;
// 不透明度の寿命曲線を使う旗。
const uint kGpuEmitterFlagCurveOpacity = 1u << 10u;
// 伸びの寿命曲線を使う旗。
const uint kGpuEmitterFlagCurveStretch = 1u << 11u;
// カメラの箱での折り返しを使う旗。
const uint kGpuEmitterFlagWrapInCameraBox = 1u << 12u;
// 時間倍率の影響を受けない時間を使う旗。
const uint kGpuEmitterFlagUnscaledTime = 1u << 13u;
// Flags 内の VfxSpawnShape の開始ビット。
const uint kGpuEmitterShapeShift = 16u;
// シフト前の VfxSpawnShape のマスク。
const uint kGpuEmitterShapeMask = 0x7u;
// スキンの表面から放出する形。
const uint kGpuEmitterShapeSkinSurface = 5u;
// 骨のバインド位置から放出する形。
const uint kGpuEmitterShapeBone = 6u;
// 表面の標本を一様に受け入れる方式。
const uint kGpuSkinAcceptUniform = 0u;
// ディゾルブの境目で標本を受け入れる方式。
const uint kGpuSkinAcceptDissolveEdge = 1u;
// 指定した骨の組で標本を受け入れる方式。
const uint kGpuSkinAcceptBoneMask = 2u;
// 生成スレッドごとの標本の試行上限。
const uint kGpuSkinMaxAttempts = 8u;
// このフレームにスキンの源が無い印。
const uint kGpuSkinSourceNone = 0xFFFFFFFFu;
// 単独の骨の指定が無い印。
const uint kGpuSkinNoBone = 0xFFFFFFFFu;
// 骨の組から一様に選ぶ乱数の番号。
const uint kGpuSkinRandomDrawBonePick = 8u;
// 試行 k の標本は 16+2k、受け入れは 17+2k（k < 8、最大 31）。
const uint kGpuSkinRandomDrawAttemptBase = 16u;
// RenderFlags 内の VfxGpuSpriteOrientation の開始ビット。
const uint kGpuEmitterRenderOrientationShift = 0u;
// シフト前の VfxGpuSpriteOrientation のマスク。
const uint kGpuEmitterRenderOrientationMask = 0x3u;
// RenderFlags 内の VfxGpuSpriteBlend の開始ビット。
const uint kGpuEmitterRenderBlendShift = 2u;
// シフト前の VfxGpuSpriteBlend のマスク。
const uint kGpuEmitterRenderBlendMask = 0x1u;
// RenderFlags 内の VfxGpuSpritePass の開始ビット。
const uint kGpuEmitterRenderPassShift = 3u;
// シフト前の VfxGpuSpritePass のマスク。
const uint kGpuEmitterRenderPassMask = 0x1u;
// RenderFlags 内の VfxGpuSpriteLighting の開始ビット。
const uint kGpuEmitterRenderLightingShift = 4u;
// シフト前の VfxGpuSpriteLighting のマスク。
const uint kGpuEmitterRenderLightingMask = 0x1u;
// RenderFlags 内の 幅 1px 未満の補償 の開始ビット。
const uint kGpuEmitterRenderSubPixelCompensationShift = 5u;
// シフト前の 幅 1px 未満の補償 のマスク。
const uint kGpuEmitterRenderSubPixelCompensationMask = 0x1u;
// RenderFlags 内のスロットの順に描く設定の開始ビット。
const uint kGpuEmitterRenderSlotOrderShift = 6u;
// シフト前のスロットの順に描く設定のマスク。
const uint kGpuEmitterRenderSlotOrderMask = 0x1u;
// CurveKeyCounts 内の Size のキー数の開始ビット。
const uint kGpuCurveKeyCountSizeShift = 0u;
// CurveKeyCounts 内の Color のキー数の開始ビット。
const uint kGpuCurveKeyCountColorShift = 8u;
// CurveKeyCounts 内の Opacity のキー数の開始ビット。
const uint kGpuCurveKeyCountOpacityShift = 16u;
// CurveKeyCounts 内の Stretch のキー数の開始ビット。
const uint kGpuCurveKeyCountStretchShift = 24u;
// シフト前の曲線キー数のマスク。0 は曲線なし。
const uint kGpuCurveKeyCountMask = 0xFFu;

// 粒の位置・年齢、速度・寿命、通し番号・エミッタのスロット・状態の旗。
// meta.w は命中の法線（GpuParticlePackNormal。0 は法線なし）。
struct GpuParticle
{
    vec4 posAge;
    vec4 velLife;
    uvec4 meta;
};

// response は VfxCollisionResponse。childEmitter はエミッタの配列添字で、無しは kGpuParticleInvalidEmitter。
struct GpuCollisionParams
{
    uint response;
    float restitution;
    uint childEmitter;
    float childProbability;
};

// std430 で 304 バイト。CPU と同じ順に 16 バイト行を並べる。
struct GpuEmitterParams
{
    uvec4 poolCapacitySeedFlags;                  // 0: プール開始・容量・乱数種・モジュールの旗。
    uvec4 serialSpawnCurveOffsetCounts;          // 16: 最初の通し番号・生成数・キーの開始添字・各 Target のキー数。
    vec4 positionLifetime;                      // 32: xyz 位置・w 寿命。
    vec4 radiusConeAngleLengthDepthThickness;   // 48: 半径・円錐半頂角（ラジアン）・円錐の長さ・深度の厚み。
    vec4 boxHalfExtentsNearFadeDistance;         // 64: xyz 箱の半径・w 近距離フェード。CameraBox は（半幅, 高さ/2, 半幅）。
    vec4 velocityMinDrag;                       // 80: xyz 最小速度・w 抗力。
    vec4 velocityMaxWindCoupling;               // 96: xyz 最大速度・w 風へ寄る速さ。
    vec4 accelerationWindScale;                 // 112: xyz 加速度・w 風の倍率。
    vec4 color;                                 // 128: 線形 RGBA 色。
    vec4 sizeTurbulenceStrengthFrequency;       // 144: xy 大きさ・z 乱れの強さ・w 乱れの周波数。
    vec4 activationFractionRampGroundKillY;     // 160: 有効割合・境目の幅・地面の高さ・消す高さ。
    GpuCollisionParams planeCollision;          // 176: 平面の衝突応答。
    GpuCollisionParams heightFieldCollision;    // 192: 高さ場の衝突応答。
    GpuCollisionParams depthCollision;          // 208: 深度の衝突応答。
    uint renderFlags;                          // 224: bit 0〜1 向き・2 合成・3 描画段階・4 照明・5 幅 1px 未満の補償・6 スロットの順に描く設定。
    float softDistance;                        // 228: ソフトパーティクルの距離。
    float fogInfluence;                        // 232: 霧の影響。
    float streakShutterSeconds;                // 236: 伸びのシャッター時間。
    vec4 curveColor;                            // 240: 色の寿命曲線が値 1 で到達する RGBA 色。
    vec3 skinBindPosition;                      // 256: Bone のバインド姿勢の位置（メッシュ空間）。
    uint skinBoneIndex;                         // 268: 骨の番号。kGpuSkinNoBone なら組から一様に選ぶ。
    float skinNormalSpeed;                      // 272: 法線の向きの初速。
    float skinVelocityInheritance;              // 276: 体の速度を継承する係数。
    uint skinAcceptMode;                        // 280: 標本を受け入れる方式。
    uint skinSourceIndex;                       // 284: このフレームの源の行。無しは kGpuSkinSourceNone。
    uvec4 skinBoneMask;                         // 288: 受け入れと骨の選択に使う 128 bit の組。
};
// キー数は Size・Color・Opacity・Stretch を下位から各 8 ビット（0 は曲線なし）。
// 別バッファのキーは開始添字から同じ Target 順に連続して置く。

// スキンの源の行。std430 で 96 バイト。RenderThread のパスが毎フレーム作る。
struct GpuSkinSource
{
    uvec2 vertexAddress; // 0: SkinnedMeshVertex（64 B）の列の BDA（下位・上位 32 bit）。
    uvec2 sampleAddress; // 8: 表面の標本（16 B）の列の BDA（下位・上位 32 bit）。
    uint sampleCount; // 16: 表面の標本の数。
    uint vertexCount; // 20: 頂点の数。
    uint boneCount; // 24: パレットに含まれる骨の数。
    uint paletteOffset; // 28: mat4 の列で今の world・worldNormal・（位置・法線）×boneCount が始まる番号。
    uint previousPaletteOffset; // 32: mat4 の列で前の world・位置×boneCount が始まる番号。
    float frameDeltaSeconds; // 36: 前のパレットからの時間差（秒）。
    uint dissolveEnabled; // 40: ディゾルブの材質かどうか。
    float dissolveThreshold; // 44: インスタンスの P0。
    vec3 dissolveDirection; // 48: ディゾルブの方向。
    float dissolveDirectionBias; // 60: ディゾルブの方向のバイアス。
    float dissolveNoiseScale; // 64: ディゾルブのノイズの倍率。
    float dissolveEdgeWidth; // 68: ディゾルブの境目の幅。
    uint reserved0; // 72: 予約欄。
    uint reserved1; // 76: 予約欄。
    uvec4 dissolveProtectBones; // 80: ディゾルブから保護する骨の組（128 bit）。
};

// 正規化した年齢と値の寿命曲線のキー。
struct GpuCurveKey
{
    float time;
    float value;
};

// 生成要求。outputOffset は生成数の排他的 prefix sum。
struct GpuSpawnJob
{
    uint emitterIndex;
    uint count;
    uint firstSerial;
    uint outputOffset;
};

// VelocityX の乱数を引く順序。
const uint kVfxRandomDrawVelocityX = 0u;
// VelocityY の乱数を引く順序。
const uint kVfxRandomDrawVelocityY = 1u;
// VelocityZ の乱数を引く順序。
const uint kVfxRandomDrawVelocityZ = 2u;
// ShapeA の乱数を引く順序。
const uint kVfxRandomDrawShapeA = 3u;
// ShapeB の乱数を引く順序。
const uint kVfxRandomDrawShapeB = 4u;
// ShapeC の乱数を引く順序。
const uint kVfxRandomDrawShapeC = 5u;
// ChildSpawn の乱数を引く順序。
const uint kVfxRandomDrawChildSpawn = 6u;
// Activation の乱数を引く順序。
const uint kVfxRandomDrawActivation = 7u;

// 32 ビット値に PCG ハッシュを適用する。
uint VfxPcgHash(uint value)
{
    uint state = value * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// シード、通し番号、乱数を引く順序からハッシュを得る。
uint VfxCounterHash(uint seed, uint serial, uint draw)
{
    return VfxPcgHash(draw ^ VfxPcgHash(serial ^ VfxPcgHash(seed)));
}

// ハッシュの上位 24 ビットを [0, 1) に変換する。
float VfxHashToUnitFloat(uint hash)
{
    return float(hash >> 8u) / 16777216.0;
}

// シード、通し番号、乱数を引く順序から [0, 1) の値を得る。
float VfxCounterUnitFloat(uint seed, uint serial, uint draw)
{
    return VfxHashToUnitFloat(VfxCounterHash(seed, serial, draw));
}

// 粒の通し番号の有効化のハッシュ [0, 1)。
float VfxActivationHash(uint seed, uint serial)
{
    return VfxCounterUnitFloat(seed, serial, kVfxRandomDrawActivation);
}

// hash < fraction の粒の重みを ramp で制限し、無効な粒には 0 を返す。
float VfxActivationWeight(float fraction, float ramp, float hash)
{
    if (!(hash < fraction))
    {
        return 0.0;
    }
    if (!(ramp > 0.0))
    {
        return 1.0;
    }

    float weight = (fraction - hash) / ramp;
    return weight < 0.0 ? 0.0 : (weight > 1.0 ? 1.0 : weight);
}

// 格子点と軸の [0, 1) のハッシュ。符号付き座標は uint にして C++ と同じビット列で混ぜる。
float VfxLatticeHash(int ix, int iy, int iz, uint axis, uint seed)
{
    uint h = VfxPcgHash(uint(ix) ^ VfxPcgHash(uint(iy) ^
        VfxPcgHash(uint(iz) ^ VfxPcgHash(seed ^ axis))));
    return VfxHashToUnitFloat(h);
}

// 格子点ハッシュを x、y、z の順に線形補間する。無効な入力や int32 範囲外の格子にはゼロを返す。
vec3 VfxTurbulenceAcceleration(vec3 position, float frequency, float strength, uint seed)
{
    if (frequency <= 0.0 || strength == 0.0 || isnan(frequency) || isinf(frequency) ||
        isnan(strength) || isinf(strength) || any(isnan(position)) || any(isinf(position)))
    {
        return vec3(0.0);
    }

    vec3 q = position * frequency;
    if (any(isnan(q)) || any(isinf(q)))
    {
        return vec3(0.0);
    }

    vec3 cell = floor(q);
    const float gridLimit = 2147483648.0;
    if (any(lessThan(cell, vec3(-gridLimit))) || any(greaterThanEqual(cell, vec3(gridLimit))))
    {
        return vec3(0.0);
    }

    float fractionX = q.x - cell.x;
    float fractionY = q.y - cell.y;
    float fractionZ = q.z - cell.z;
    int ix = int(cell.x);
    int iy = int(cell.y);
    int iz = int(cell.z);
    // 隣の格子点は符号付き整数のオーバーフローを避けて求める。
    int nextX = int(uint(ix) + 1u);
    int nextY = int(uint(iy) + 1u);
    int nextZ = int(uint(iz) + 1u);

    vec3 acceleration = vec3(0.0);
    for (uint axis = 0u; axis < 3u; ++axis)
    {
        float h000 = VfxLatticeHash(ix, iy, iz, axis, seed);
        float h100 = VfxLatticeHash(nextX, iy, iz, axis, seed);
        float h010 = VfxLatticeHash(ix, nextY, iz, axis, seed);
        float h110 = VfxLatticeHash(nextX, nextY, iz, axis, seed);
        float h001 = VfxLatticeHash(ix, iy, nextZ, axis, seed);
        float h101 = VfxLatticeHash(nextX, iy, nextZ, axis, seed);
        float h011 = VfxLatticeHash(ix, nextY, nextZ, axis, seed);
        float h111 = VfxLatticeHash(nextX, nextY, nextZ, axis, seed);

        precise float x00 = h000 + (h100 - h000) * fractionX;
        precise float x10 = h010 + (h110 - h010) * fractionX;
        precise float x01 = h001 + (h101 - h001) * fractionX;
        precise float x11 = h011 + (h111 - h011) * fractionX;
        precise float y0 = x00 + (x10 - x00) * fractionY;
        precise float y1 = x01 + (x11 - x01) * fractionY;
        precise float noise = y0 + (y1 - y0) * fractionZ;
        precise float component = strength * (2.0 * noise - 1.0);
        acceleration[axis] = component;
    }

    return acceleration;
}

// 命中の法線が格納されていることを示す旗。bit 30 は使わない。
const uint kGpuParticleNormalValidBit = 1u << 31u;

// 法線を八面体に写像し、x・y を各 15 bit の unorm に詰める。無効な法線は 0 を返す。
uint GpuParticlePackNormal(vec3 n)
{
    if (any(isnan(n)) || any(isinf(n)))
    {
        return 0u;
    }
    float maximumComponent = max(max(abs(n.x), abs(n.y)), abs(n.z));
    if (maximumComponent <= 1.0e-8)
    {
        return 0u;
    }
    // 先に最大成分で割り、有限で大きな入力でも和のオーバーフローを避ける。
    n /= maximumComponent;
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    vec2 octahedral = n.xy;
    if (n.z < 0.0)
    {
        // 軸上の 0 は正側に折り返し、負の Z 軸も四隅へ写す。
        vec2 direction = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        octahedral = (vec2(1.0) - abs(octahedral.yx)) * direction;
    }
    vec2 unorm = clamp(octahedral * 0.5 + 0.5, vec2(0.0), vec2(1.0));
    uvec2 quantized = uvec2(floor(unorm * 32767.0 + 0.5));
    return kGpuParticleNormalValidBit | quantized.x | (quantized.y << 15u);
}

// 格納した法線を復元して正規化する。法線なしは上向きを返し、戻り値を false にする。
bool GpuParticleUnpackNormal(uint packed, out vec3 n)
{
    if ((packed & kGpuParticleNormalValidBit) == 0u)
    {
        n = vec3(0.0, 1.0, 0.0);
        return false;
    }
    vec2 octahedral = vec2(float(packed & 0x7FFFu), float((packed >> 15u) & 0x7FFFu)) /
        32767.0 * 2.0 - 1.0;
    n = vec3(octahedral, 1.0 - abs(octahedral.x) - abs(octahedral.y));
    if (n.z < 0.0)
    {
        vec2 direction = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (vec2(1.0) - abs(n.yx)) * direction;
    }
    n = normalize(n);
    return true;
}

// 通し番号からリングのスロットを返す。capacity は 1 以上が前提で、0 なら poolOffset を返す。
uint GpuParticleRingSlot(uint poolOffset, uint capacity, uint serial)
{
    return capacity == 0u ? poolOffset : poolOffset + serial % capacity;
}

#endif // NORVES_PARTICLE_COMMON_GLSL
