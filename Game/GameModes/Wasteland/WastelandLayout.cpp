#include "GameModes/Wasteland/WastelandLayout.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace NorvesLib::Core::Container;
using NorvesLib::Core::Rendering::Terrain::TerrainHeightField;
using NorvesLib::Core::Rendering::Terrain::TerrainNormal;

namespace Game::GameModes::WastelandLayout
{

    namespace
    {
        constexpr float kPi = 3.14159265358979f;
        constexpr float kDegToRad = kPi / 180.0f;

        // Poly Haven のスキャン（Assets/AssetSets/WastelandModels.json が焼く）と生成した建物
        // （Assets/AssetSets/WastelandBuildingsModels.json が焼く。GLB 内蔵のテクスチャは img0 がアルベド・img1 が ORM・img2 が法線）
#define WASTELAND_POLYHAVEN(id)                                                                                         \
    {id, "Assets/Models/PolyHaven/" id "/" id "_4k.gltf", "Assets/Models/PolyHaven/" id "/cooked/" id "_albedo",        \
     "Assets/Models/PolyHaven/" id "/cooked/" id "_normal", "Assets/Models/PolyHaven/" id "/cooked/" id "_orm", false}
#define WASTELAND_GENERATED(id)                                                                                         \
    {id, "Assets/Models/Generated/" id "/" id ".glb", "Assets/Models/Generated/" id "/" id ".glb.img0.png",               \
     "Assets/Models/Generated/" id "/" id ".glb.img2.png", "Assets/Models/Generated/" id "/" id ".glb.img1.png", true}
        constexpr PropKindSpec kPropKindSpecs[kPropKindCount] = {
            WASTELAND_POLYHAVEN("namaqualand_boulder_02"),
            WASTELAND_POLYHAVEN("namaqualand_boulder_03"),
            WASTELAND_POLYHAVEN("namaqualand_boulder_04"),
            WASTELAND_POLYHAVEN("namaqualand_boulder_05"),
            WASTELAND_POLYHAVEN("namaqualand_cliff_01"),
            WASTELAND_POLYHAVEN("namaqualand_cliff_02"),
            WASTELAND_GENERATED("adobe_ruin"),
            WASTELAND_GENERATED("watchtower"),
            WASTELAND_GENERATED("stone_arch"),
            WASTELAND_GENERATED("windmill"),
            WASTELAND_GENERATED("water_tower"),
            // 木造の小屋の id のまま、石積みの小さな物置として生成した
            WASTELAND_GENERATED("wooden_shack"),
        };
#undef WASTELAND_POLYHAVEN
#undef WASTELAND_GENERATED

        // ---- 前哨地 ----
        // 道は原点を通り、北から東へ 15° 振った向き（ワールドの +Z から +X へ測った角）。両側に建物の表を道へ向けて並べる
        constexpr float kRoadAzimuthDegrees = 15.0f;
        // 岩を置かない道の幅・長さ
        constexpr float kRoadClearHalfWidth = 7.0f;
        constexpr float kRoadClearHalfLength = 55.0f;
        // 建物を地面に埋める深さ[m]（底の凹凸と、盆地の床のわずかな起伏で浮かないように）
        constexpr float kBuildingSink = 0.25f;
        // 建物の周りの、岩を置かない半径[m]
        constexpr float kBuildingClearRadius = 13.0f;

        /// 前哨地の建物 1 つ。道に沿う距離 s（南が負）・道からの横の距離 l（東が正）・表の向き
        struct BuildingSlot
        {
            PropKind Kind;
            float Along;
            float Lateral;
            /// 表を向けるワールドの向き。道へ向けるときは NaN（l の符号で決める）
            float FacingAzimuthDegrees;
            /// 向きのばらつき[度]
            float YawJitterDegrees;
        };
        constexpr float kFaceRoad = std::numeric_limits<float>::quiet_NaN();
        constexpr BuildingSlot kBuildingSlots[] = {
            // 南の入り口の見張り塔
            {PropKind::Watchtower, -30.0f, 9.0f, kFaceRoad, 3.0f},
            {PropKind::AdobeRuin, -14.0f, -9.5f, kFaceRoad, 4.0f},
            {PropKind::StoneShed, -11.0f, 8.5f, kFaceRoad, -5.0f},
            {PropKind::AdobeRuin, 3.0f, 10.0f, kFaceRoad, -6.0f},
            {PropKind::StoneShed, 6.0f, -8.5f, kFaceRoad, 7.0f},
            {PropKind::WaterTower, 17.0f, -14.0f, kFaceRoad, 0.0f},
            // 風車は羽根を南西（盆地を見下ろす視点と道の南の端）へ向ける
            {PropKind::Windmill, 24.0f, 16.0f, 205.0f, 0.0f},
            // 北の端の門。表を南へ向け、アーチが道をまたぐ
            {PropKind::StoneArch, 34.0f, 0.0f, kRoadAzimuthDegrees + 180.0f, 0.0f},
        };

        // ---- メサ・稜線の足元 ----
        // 原点から放射状にたどる本数と範囲[m]・刻み[m]
        constexpr uint32_t kFootRayCount = 56;
        constexpr float kFootSearchStart = 260.0f;
        constexpr float kFootSearchEnd = 780.0f;
        constexpr float kFootSearchStep = 2.0f;
        // 崖とみなす傾斜と、その先 kCliffRiseProbe[m] で kCliffMinRise[m] 以上上がること（砂丘の風下の面を除く）
        constexpr float kCliffSlopeDegrees = 38.0f;
        constexpr float kCliffRiseProbe = 20.0f;
        constexpr float kCliffMinRise = 8.0f;
        // 根元: 崖から傾斜がこれより緩む所まで戻る（最大 kFootBackMax[m]）
        constexpr float kFootSlopeDegrees = 24.0f;
        constexpr float kFootBackStep = 1.5f;
        constexpr float kFootBackMax = 18.0f;
        // 崖の岩の拡大率の範囲（cliff_01 は高さ約 5 m・幅 8 m、cliff_02 は高さ約 7 m・幅 20 m）
        // メサ（高さ 28〜62 m）の足元で埋もれないよう、高さ 11〜17 m 程度にする
        constexpr float kCliff01Scale[2] = {2.2f, 3.4f};
        constexpr float kCliff02Scale[2] = {1.6f, 2.4f};
        // 崖の岩を根元から崖の側へ押し込む距離（奥行きに対する割合）。背面の開いた側をメサへ埋める
        constexpr float kCliffPushIntoSlope = 0.35f;
        // 足元に添える大きな岩: 横へ ±kFootBoulderSpread[m]、盆地の側へ kFootBoulderInward[m] の範囲、拡大率
        constexpr float kFootBoulderSpread = 16.0f;
        constexpr float kFootBoulderInward[2] = {3.0f, 14.0f};
        constexpr float kFootBoulderScale[2] = {1.6f, 3.2f};

        // ---- 盆地 ----
        constexpr uint32_t kBasinBoulderCount = 26;
        constexpr float kBasinRadius[2] = {28.0f, 280.0f};
        constexpr float kBasinMaxSlopeDegrees = 30.0f;
        constexpr float kBasinBoulderScale[2] = {0.8f, 2.8f};
        // 視点の前を岩でふさがない（SceneCatalog.h の ground の位置）
        constexpr float kGroundViewX = -20.0f;
        constexpr float kGroundViewZ = -40.0f;
        constexpr float kGroundViewClearRadius = 25.0f;

        // 岩を埋める深さ（拡大後の高さに対する割合）と、足元の範囲のうち地形を引く割合（丸い底の縁は地面から浮くので内側で引く）
        constexpr float kRockSinkFraction = 0.2f;
        constexpr float kRockFootprintFraction = 0.6f;
        constexpr float kCliffSinkFraction = 0.12f;
        constexpr float kCliffFootprintFraction = 0.8f;

        // 整数のハッシュ（配置の乱数。番号だけで決まり、実行ごとに同じ）
        uint32_t Hash(uint32_t value)
        {
            value ^= value >> 16;
            value *= 0x7feb352du;
            value ^= value >> 15;
            value *= 0x846ca68bu;
            value ^= value >> 16;
            return value;
        }

        /// 系列 stream の index 番目の [0,1) の乱数
        float Random(uint32_t stream, uint32_t index)
        {
            return static_cast<float>(Hash(Hash(stream * 0x9e3779b9u) ^ (index * 0x85ebca6bu + 0x27d4eb2fu)) & 0xFFFFFFu) /
                   16777216.0f;
        }

        float Lerp(const float (&range)[2], float t)
        {
            return range[0] + (range[1] - range[0]) * t;
        }

        /// ローカルの (lx, lz) を Y 軸まわりに yaw 回したワールドの差分。World は行ベクトルの規約なので、
        /// ローカルの XZ の向き φ（+Z から +X へ測った角）はワールドの φ − yaw へ写る
        void RotateXZ(float lx, float lz, float yaw, float &outX, float &outZ)
        {
            const float c = std::cos(yaw);
            const float s = std::sin(yaw);
            outX = lx * c - lz * s;
            outZ = lz * c + lx * s;
        }

        /// 表（ローカルの向き frontYaw）をワールドの向き azimuth へ向ける回転
        float FacingYaw(float frontYaw, float azimuth)
        {
            return frontYaw - azimuth;
        }

        /// 足元の範囲（ローカルの XZ の箱を footprintFraction 倍した 3×3 点）で地形の最も低い高さを引き、
        /// sink だけ埋めて最下点を据えた Entity の y を返す
        float GroundedY(const TerrainHeightField &field, const PropMeshShape &shape, float x, float z, float yaw, float scale,
                        float footprintFraction, float sink)
        {
            const float centerX = 0.5f * (shape.MinX + shape.MaxX);
            const float centerZ = 0.5f * (shape.MinZ + shape.MaxZ);
            const float halfX = 0.5f * (shape.MaxX - shape.MinX) * footprintFraction;
            const float halfZ = 0.5f * (shape.MaxZ - shape.MinZ) * footprintFraction;
            float lowest = std::numeric_limits<float>::max();
            for (int fz = -1; fz <= 1; ++fz)
            {
                for (int fx = -1; fx <= 1; ++fx)
                {
                    float dx = 0.0f;
                    float dz = 0.0f;
                    RotateXZ((centerX + halfX * static_cast<float>(fx)) * scale,
                             (centerZ + halfZ * static_cast<float>(fz)) * scale, yaw, dx, dz);
                    lowest = std::min(lowest, field.SampleHeight(x + dx, z + dz));
                }
            }
            return lowest - sink - shape.MinY * scale;
        }

        /// メッシュの範囲の中心（XZ）がワールドの (targetX, targetZ) に来る Entity の位置
        void CenterAt(const PropMeshShape &shape, float targetX, float targetZ, float yaw, float scale, float &outX,
                      float &outZ)
        {
            float dx = 0.0f;
            float dz = 0.0f;
            RotateXZ(0.5f * (shape.MinX + shape.MaxX) * scale, 0.5f * (shape.MinZ + shape.MaxZ) * scale, yaw, dx, dz);
            outX = targetX - dx;
            outZ = targetZ - dz;
        }

        float SlopeCosine(const TerrainHeightField &field, float x, float z)
        {
            const TerrainNormal normal = field.SampleNormal(x, z, 1.0f);
            return normal.Y;
        }

        struct RoadFrame
        {
            float AlongX;
            float AlongZ;
            float LateralX;
            float LateralZ;
        };

        RoadFrame MakeRoadFrame()
        {
            const float azimuth = kRoadAzimuthDegrees * kDegToRad;
            // 道の向き（北北東）と、それを右（東）へ 90° 回した横の向き
            return RoadFrame{std::sin(azimuth), std::cos(azimuth), std::cos(azimuth), -std::sin(azimuth)};
        }

        /// 岩を置いてよい場所か（道・建物・視点の前を空け、急な斜面を避ける）
        bool IsClearForScatter(const TerrainHeightField &field, const RoadFrame &road,
                               const VariableArray<PropPlacement> &buildings, float x, float z)
        {
            const float along = x * road.AlongX + z * road.AlongZ;
            const float lateral = x * road.LateralX + z * road.LateralZ;
            if (std::fabs(lateral) < kRoadClearHalfWidth && std::fabs(along) < kRoadClearHalfLength)
            {
                return false;
            }
            for (const PropPlacement &building : buildings)
            {
                const float dx = x - building.X;
                const float dz = z - building.Z;
                if (dx * dx + dz * dz < kBuildingClearRadius * kBuildingClearRadius)
                {
                    return false;
                }
            }
            const float gx = x - kGroundViewX;
            const float gz = z - kGroundViewZ;
            if (gx * gx + gz * gz < kGroundViewClearRadius * kGroundViewClearRadius)
            {
                return false;
            }
            return SlopeCosine(field, x, z) >= std::cos(kBasinMaxSlopeDegrees * kDegToRad);
        }

        /// 岩 1 つを (x,z) に置く（範囲の中心をそこへ合わせ、足元の地形に埋める）
        PropPlacement MakeRock(const TerrainHeightField &field, const PropMeshShape &shape, PropKind kind, float x, float z,
                               float yaw, float scale, float footprintFraction, float sinkFraction)
        {
            PropPlacement placement;
            placement.Kind = kind;
            placement.Yaw = yaw;
            placement.Scale = scale;
            CenterAt(shape, x, z, yaw, scale, placement.X, placement.Z);
            const float height = (shape.MaxY - shape.MinY) * scale;
            placement.Y = GroundedY(field, shape, placement.X, placement.Z, yaw, scale, footprintFraction,
                                    height * sinkFraction);
            return placement;
        }

        /// 読めた種類のうち、candidates から乱数 t で 1 つ選ぶ。どれも無ければ Count
        template <size_t N>
        PropKind PickKind(const PropMeshShape (&shapes)[kPropKindCount], const PropKind (&candidates)[N], float t)
        {
            PropKind available[N] = {};
            size_t count = 0;
            for (PropKind kind : candidates)
            {
                if (shapes[static_cast<uint32_t>(kind)].bAvailable)
                {
                    available[count++] = kind;
                }
            }
            if (count == 0)
            {
                return PropKind::Count;
            }
            return available[std::min(count - 1, static_cast<size_t>(t * static_cast<float>(count)))];
        }

        void PlaceBuildings(const TerrainHeightField &field, const PropMeshShape (&shapes)[kPropKindCount],
                            const RoadFrame &road, VariableArray<PropPlacement> &out, LayoutStats &stats)
        {
            for (const BuildingSlot &slot : kBuildingSlots)
            {
                const PropMeshShape &shape = shapes[static_cast<uint32_t>(slot.Kind)];
                if (!shape.bAvailable)
                {
                    continue;
                }
                const float x = slot.Along * road.AlongX + slot.Lateral * road.LateralX;
                const float z = slot.Along * road.AlongZ + slot.Lateral * road.LateralZ;
                float azimuth = 0.0f;
                if (std::isnan(slot.FacingAzimuthDegrees))
                {
                    // 東側（l が正）の建物は西へ、西側は東へ、道の方を向く
                    const float sign = slot.Lateral >= 0.0f ? -1.0f : 1.0f;
                    azimuth = std::atan2(sign * road.LateralX, sign * road.LateralZ);
                }
                else
                {
                    azimuth = slot.FacingAzimuthDegrees * kDegToRad;
                }
                azimuth += slot.YawJitterDegrees * kDegToRad;
                PropPlacement placement;
                placement.Kind = slot.Kind;
                placement.Yaw = FacingYaw(shape.FrontYaw, azimuth);
                placement.Scale = 1.0f;
                CenterAt(shape, x, z, placement.Yaw, 1.0f, placement.X, placement.Z);
                placement.Y = GroundedY(field, shape, placement.X, placement.Z, placement.Yaw, 1.0f, 1.0f, kBuildingSink);
                out.push_back(placement);
                ++stats.Buildings;
            }
        }

        void PlaceFootRocks(const TerrainHeightField &field, const PropMeshShape (&shapes)[kPropKindCount],
                            VariableArray<PropPlacement> &out, LayoutStats &stats)
        {
            constexpr PropKind kCliffKinds[] = {PropKind::Cliff01, PropKind::Cliff02};
            constexpr PropKind kBigBoulderKinds[] = {PropKind::Boulder02, PropKind::Boulder03, PropKind::Boulder04};
            const float cliffCos = std::cos(kCliffSlopeDegrees * kDegToRad);
            const float footCos = std::cos(kFootSlopeDegrees * kDegToRad);
            for (uint32_t ray = 0; ray < kFootRayCount; ++ray)
            {
                ++stats.FootRays;
                const float step = 2.0f * kPi / static_cast<float>(kFootRayCount);
                const float angle = step * (static_cast<float>(ray) + 0.8f * (Random(11, ray) - 0.5f));
                const float dirX = std::sin(angle);
                const float dirZ = std::cos(angle);
                float hitRadius = -1.0f;
                for (float radius = kFootSearchStart; radius <= kFootSearchEnd; radius += kFootSearchStep)
                {
                    const float x = radius * dirX;
                    const float z = radius * dirZ;
                    if (SlopeCosine(field, x, z) >= cliffCos)
                    {
                        continue;
                    }
                    const float rise =
                        field.SampleHeight(x + kCliffRiseProbe * dirX, z + kCliffRiseProbe * dirZ) - field.SampleHeight(x, z);
                    if (rise >= kCliffMinRise)
                    {
                        hitRadius = radius;
                        break;
                    }
                }
                if (hitRadius < 0.0f)
                {
                    continue;
                }
                // 崖から盆地の側へ、傾斜が緩む所まで戻った所を根元にする
                float footRadius = hitRadius;
                for (float back = 0.0f; back < kFootBackMax; back += kFootBackStep)
                {
                    footRadius -= kFootBackStep;
                    if (SlopeCosine(field, footRadius * dirX, footRadius * dirZ) >= footCos)
                    {
                        break;
                    }
                }
                ++stats.FootHits;
                const float towardOrigin = std::atan2(-dirX, -dirZ);
                const float tangentX = dirZ;
                const float tangentZ = -dirX;

                // 崖の岩: 表を盆地へ向け、背面（奥行きの kCliffPushIntoSlope）を崖の側へ押し込む
                const PropKind cliffKind = PickKind(shapes, kCliffKinds, Random(12, ray));
                if (cliffKind != PropKind::Count)
                {
                    const PropMeshShape &shape = shapes[static_cast<uint32_t>(cliffKind)];
                    const float(&scaleRange)[2] = cliffKind == PropKind::Cliff01 ? kCliff01Scale : kCliff02Scale;
                    const float scale = Lerp(scaleRange, Random(13, ray));
                    const float yaw = FacingYaw(shape.FrontYaw, towardOrigin + (Random(14, ray) - 0.5f) * 30.0f * kDegToRad);
                    const float push = (shape.MaxZ - shape.MinZ) * scale * kCliffPushIntoSlope;
                    out.push_back(MakeRock(field, shape, cliffKind, (footRadius + push) * dirX, (footRadius + push) * dirZ, yaw,
                                           scale, kCliffFootprintFraction, kCliffSinkFraction));
                    ++stats.Cliffs;
                }

                // 足元に添える大きな岩（1〜2 個）
                const uint32_t boulderCount = Random(15, ray) < 0.5f ? 1u : 2u;
                for (uint32_t index = 0; index < boulderCount; ++index)
                {
                    const uint32_t key = ray * 4 + index;
                    const PropKind kind = PickKind(shapes, kBigBoulderKinds, Random(16, key));
                    if (kind == PropKind::Count)
                    {
                        break;
                    }
                    const float spread = (Random(17, key) * 2.0f - 1.0f) * kFootBoulderSpread;
                    const float inward = Lerp(kFootBoulderInward, Random(18, key));
                    const float x = (footRadius - inward) * dirX + spread * tangentX;
                    const float z = (footRadius - inward) * dirZ + spread * tangentZ;
                    const float scale = Lerp(kFootBoulderScale, Random(19, key));
                    const float yaw = Random(20, key) * 2.0f * kPi;
                    out.push_back(MakeRock(field, shapes[static_cast<uint32_t>(kind)], kind, x, z, yaw, scale,
                                           kRockFootprintFraction, kRockSinkFraction));
                    ++stats.FootBoulders;
                }
            }
        }

        void PlaceBasinScatter(const TerrainHeightField &field, const PropMeshShape (&shapes)[kPropKindCount],
                               const RoadFrame &road, const VariableArray<PropPlacement> &buildings,
                               VariableArray<PropPlacement> &out, LayoutStats &stats)
        {
            constexpr PropKind kBoulderKinds[] = {PropKind::Boulder02, PropKind::Boulder03, PropKind::Boulder04,
                                                  PropKind::Boulder05};
            uint32_t attempt = 0;
            while (stats.BasinBoulders < kBasinBoulderCount && attempt < kBasinBoulderCount * 8)
            {
                const uint32_t key = attempt++;
                // 面積が一様になるよう半径の 2 乗で引く
                const float t = Random(30, key);
                const float radius = std::sqrt(kBasinRadius[0] * kBasinRadius[0] +
                                               t * (kBasinRadius[1] * kBasinRadius[1] - kBasinRadius[0] * kBasinRadius[0]));
                const float angle = Random(31, key) * 2.0f * kPi;
                const float x = radius * std::sin(angle);
                const float z = radius * std::cos(angle);
                if (!IsClearForScatter(field, road, buildings, x, z))
                {
                    continue;
                }
                const PropKind kind = PickKind(shapes, kBoulderKinds, Random(32, key));
                if (kind == PropKind::Count)
                {
                    break;
                }
                // 小さめを多く、たまに大きな岩
                const float sizeT = Random(33, key);
                const float scale = Lerp(kBasinBoulderScale, sizeT * sizeT);
                out.push_back(MakeRock(field, shapes[static_cast<uint32_t>(kind)], kind, x, z, Random(34, key) * 2.0f * kPi,
                                       scale, kRockFootprintFraction, kRockSinkFraction));
                ++stats.BasinBoulders;
            }
        }
    } // namespace

    const PropKindSpec &GetPropKindSpec(PropKind kind)
    {
        return kPropKindSpecs[std::min(static_cast<uint32_t>(kind), kPropKindCount - 1)];
    }

    void BuildPlacements(const TerrainHeightField &field, const PropMeshShape (&shapes)[kPropKindCount],
                         VariableArray<PropPlacement> &outPlacements, LayoutStats &outStats)
    {
        outPlacements.clear();
        outStats = LayoutStats{};
        const RoadFrame road = MakeRoadFrame();
        PlaceBuildings(field, shapes, road, outPlacements, outStats);
        // 建物の周りを空けるため、ここまでの配置（建物）を写しておく
        const VariableArray<PropPlacement> buildings = outPlacements;
        PlaceFootRocks(field, shapes, outPlacements, outStats);
        PlaceBasinScatter(field, shapes, road, buildings, outPlacements, outStats);
    }

} // namespace Game::GameModes::WastelandLayout
