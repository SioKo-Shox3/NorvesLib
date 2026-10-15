#include "GameModes/Wasteland/WastelandFoliage.h"

#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Rendering/FoliageMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::Rendering;

namespace Game::GameModes::WastelandFoliage
{
    namespace
    {
        constexpr float kPi = 3.14159265358979f;

        // 一角の範囲: 谷床の (250, 1650) を中心とする 120 m 四方（傾斜の中央値 3.8°。DEM から測った）。
        // 視点 grass（Game/Scenes/SceneCatalog.h）はこの南南東の端から北北西を見る
        constexpr float kPatchCenterX = 250.0f;
        constexpr float kPatchCenterZ = 1650.0f;
        constexpr float kPatchHalfSize = 60.0f;
        // 視点の足元は空ける（目の前を株がふさがない）
        constexpr float kViewX = 266.0f;
        constexpr float kViewZ = 1692.0f;
        constexpr float kViewClearRadius = 2.5f;
        // 株を置く升目[m]。升目ごとに 1 株の候補を升目の中のでたらめな位置に置き、密度の斑で間引く。
        // 手本の写真（ground_01）の低木と草は 1.5〜3 m おきで、裸地の斑を挟む
        constexpr float kCellSize = 1.5f;
        // 置かない傾斜[度]（崩れた沢の岸・急な斜面）
        constexpr float kMaxSlopeDegrees = 28.0f;
        // 根元を地面へ埋める深さ[m]（地形の三角形の折れ目で浮かない）
        constexpr float kRootSink = 0.03f;
        // 風は方位 235°（南西）から北東へ吹く（地表の分類の地図の吹きだまりと同じ推定。Scripts/Terrain/TerrainMasks.py）。
        // エンジンの X は東・Z は南なので、吹いていく向きは (sin 55°, -cos 55°)
        constexpr float kWindDirectionX = 0.819f;
        constexpr float kWindDirectionZ = -0.574f;
        constexpr float kGustSpeed = 4.0f;
        constexpr float kGustScale = 14.0f;
        // grass_gust の風の時計のずらし[s]（草の揺れ 0.9 Hz で 1.3 周期、突風の塊は 5.6 m 流れる）
        constexpr float kGustViewTimeOffset = 1.4f;

        /// 位置から決まる乱数（0〜1）
        float Hash2(int32_t x, int32_t z, uint32_t salt)
        {
            uint32_t h = static_cast<uint32_t>(x) * 0x8DA6B343u ^ static_cast<uint32_t>(z) * 0xD8163841u ^ salt * 0xCB1AB31Fu;
            h ^= h >> 13u;
            h *= 0x5BD1E995u;
            h ^= h >> 15u;
            return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
        }

        /// 滑らかな値ノイズ（0〜1。scale[m] の斑）
        float ValueNoise(float x, float z, float scale, uint32_t salt)
        {
            const float fx = x / scale;
            const float fz = z / scale;
            const int32_t ix = static_cast<int32_t>(std::floor(fx));
            const int32_t iz = static_cast<int32_t>(std::floor(fz));
            const float tx = fx - static_cast<float>(ix);
            const float tz = fz - static_cast<float>(iz);
            const float ux = tx * tx * (3.0f - 2.0f * tx);
            const float uz = tz * tz * (3.0f - 2.0f * tz);
            const float a = Hash2(ix, iz, salt);
            const float b = Hash2(ix + 1, iz, salt);
            const float c = Hash2(ix, iz + 1, salt);
            const float d = Hash2(ix + 1, iz + 1, salt);
            return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uz;
        }

        /// 株の種類（束の並び）
        enum class PlantKind : uint32_t
        {
            GrassA,
            GrassB,
            BlackbrushA,
            BlackbrushB,
            MormonTea,
            Count
        };
        constexpr uint32_t kPlantKindCount = static_cast<uint32_t>(PlantKind::Count);

        /// sRGB（0〜255）を線形のアルベドへ
        float SrgbToLinear(float value)
        {
            const float c = value / 255.0f;
            return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }

        void SetColor(float (&destination)[3], float r, float g, float b)
        {
            destination[0] = SrgbToLinear(r);
            destination[1] = SrgbToLinear(g);
            destination[2] = SrgbToLinear(b);
        }

        /// 種類ごとのメッシュと色・風
        FoliageBatch MakeBatch(PlantKind kind)
        {
            FoliageBatch batch;
            switch (kind)
            {
            case PlantKind::GrassA:
            case PlantKind::GrassB:
            {
                FoliageMesh::GrassClumpDesc desc;
                desc.Seed = kind == PlantKind::GrassA ? 11u : 23u;
                desc.BladeCount = kind == PlantKind::GrassA ? 38u : 28u;
                desc.CulmCount = kind == PlantKind::GrassA ? 3u : 2u;
                desc.Height = kind == PlantKind::GrassA ? 0.3f : 0.24f;
                desc.BaseRadius = 0.05f;
                desc.BladeWidth = 0.004f;
                batch.Mesh = FoliageMesh::BuildGrassClump(desc);
                // 根元は前の年の灰褐色の枯れ葉、先は麦わら色〜灰色がかった黄土（写真の手前の草の株）
                SetColor(batch.StemColor, 118.0f, 104.0f, 86.0f);
                SetColor(batch.TipColorA, 182.0f, 152.0f, 104.0f);
                SetColor(batch.TipColorB, 152.0f, 138.0f, 114.0f);
                batch.Roughness = 0.85f;
                batch.WindStrength = 0.18f;
                batch.WindSwayFrequency = 0.9f;
                batch.MinPixelWidth = 0.7f;
                batch.MaxWidenRatio = 3.0f;
                batch.DebugName = kind == PlantKind::GrassA ? "grass_a" : "grass_b";
                break;
            }
            case PlantKind::BlackbrushA:
            case PlantKind::BlackbrushB:
            {
                FoliageMesh::ShrubDesc desc;
                desc.Seed = kind == PlantKind::BlackbrushA ? 101u : 137u;
                desc.Form = FoliageMesh::ShrubForm::Rounded;
                desc.Height = 0.6f;
                desc.CrownRadius = kind == PlantKind::BlackbrushA ? 0.5f : 0.42f;
                desc.StemCount = kind == PlantKind::BlackbrushA ? 13u : 11u;
                desc.BranchLevels = 4;
                desc.SpurLength = 0.07f;
                desc.StemRadius = 0.006f;
                desc.TwigRadius = 0.0012f;
                desc.LeavesPerTwig = 3;
                desc.LeafLength = 0.014f;
                desc.LeafWidth = 0.005f;
                batch.Mesh = FoliageMesh::BuildShrub(desc);
                // 枝は暗い灰褐色、葉は小さな灰緑（写真の丸い低木は遠目に暗い灰褐色の塊で、上の縁が灰色がかる）
                SetColor(batch.StemColor, 96.0f, 86.0f, 76.0f);
                SetColor(batch.TipColorA, 130.0f, 128.0f, 106.0f);
                SetColor(batch.TipColorB, 140.0f, 132.0f, 116.0f);
                batch.Roughness = 0.8f;
                batch.WindStrength = 0.035f;
                batch.WindSwayFrequency = 0.5f;
                batch.MinPixelWidth = 0.7f;
                batch.MaxWidenRatio = 2.5f;
                batch.DebugName = kind == PlantKind::BlackbrushA ? "blackbrush_a" : "blackbrush_b";
                break;
            }
            case PlantKind::MormonTea:
            default:
            {
                FoliageMesh::ShrubDesc desc;
                desc.Seed = 211u;
                desc.Form = FoliageMesh::ShrubForm::Upright;
                desc.Height = 0.7f;
                desc.CrownRadius = 0.35f;
                desc.StemCount = 18;
                desc.BranchLevels = 2;
                desc.StemRadius = 0.0035f;
                desc.TwigRadius = 0.0012f;
                desc.LeavesPerTwig = 0;
                batch.Mesh = FoliageMesh::BuildShrub(desc);
                // 根元は木質の褐色、茎は黄緑〜くすんだオリーブ
                SetColor(batch.StemColor, 112.0f, 96.0f, 72.0f);
                SetColor(batch.TipColorA, 148.0f, 146.0f, 102.0f);
                SetColor(batch.TipColorB, 130.0f, 132.0f, 100.0f);
                batch.Roughness = 0.75f;
                batch.WindStrength = 0.06f;
                batch.WindSwayFrequency = 0.6f;
                batch.MinPixelWidth = 0.7f;
                batch.MaxWidenRatio = 2.5f;
                batch.DebugName = "mormon_tea";
                break;
            }
            }
            return batch;
        }
    } // namespace

    FoliageScene BuildFoliage(const Terrain::TerrainHeightField &field)
    {
        VariableArray<FoliageInstance> instances[kPlantKindCount];
        const int32_t cells = static_cast<int32_t>(2.0f * kPatchHalfSize / kCellSize);
        const float minSlopeCos = std::cos(kMaxSlopeDegrees * kPi / 180.0f);
        uint32_t rejectedSlope = 0;
        for (int32_t cz = 0; cz < cells; ++cz)
        {
            for (int32_t cx = 0; cx < cells; ++cx)
            {
                const float x = kPatchCenterX - kPatchHalfSize + (static_cast<float>(cx) + Hash2(cx, cz, 1u)) * kCellSize;
                const float z = kPatchCenterZ - kPatchHalfSize + (static_cast<float>(cz) + Hash2(cx, cz, 2u)) * kCellSize;
                const float dx = x - kViewX;
                const float dz = z - kViewZ;
                if (dx * dx + dz * dz < kViewClearRadius * kViewClearRadius || !field.Contains(x, z))
                {
                    continue;
                }
                // 密度の斑: 数十 m の濃淡と数 m の群れ。裸地の斑を残す（全域に均等に撒かない）
                const float density = 0.55f * ValueNoise(x, z, 22.0f, 3u) + 0.45f * ValueNoise(x, z, 6.0f, 4u);
                if (Hash2(cx, cz, 5u) > std::clamp((density - 0.22f) * 1.6f, 0.0f, 0.92f))
                {
                    continue;
                }
                const Terrain::TerrainNormal normal = field.SampleNormal(x, z, 0.5f);
                if (!(normal.Y >= minSlopeCos))
                {
                    ++rejectedSlope;
                    continue;
                }
                // 種類: 草の株がおよそ半分、低木は群れの濃い所に多い。モルモンティーは 1 割ほど
                const float pick = Hash2(cx, cz, 6u);
                const float shrubShare = 0.3f + 0.25f * ValueNoise(x, z, 9.0f, 7u);
                PlantKind kind;
                if (pick < 0.08f)
                {
                    kind = PlantKind::MormonTea;
                }
                else if (pick < 0.08f + shrubShare)
                {
                    kind = Hash2(cx, cz, 8u) < 0.5f ? PlantKind::BlackbrushA : PlantKind::BlackbrushB;
                }
                else
                {
                    kind = Hash2(cx, cz, 9u) < 0.55f ? PlantKind::GrassA : PlantKind::GrassB;
                }
                const bool bShrub = kind == PlantKind::BlackbrushA || kind == PlantKind::BlackbrushB ||
                                    kind == PlantKind::MormonTea;

                FoliageInstance instance;
                instance.Position[0] = x;
                instance.Position[1] = field.SampleHeight(x, z) - kRootSink;
                instance.Position[2] = z;
                instance.Yaw = Hash2(cx, cz, 10u) * 2.0f * kPi;
                // 大きさ: 低木は高さ 0.35〜0.95 m（メッシュの 0.6〜0.7 m の倍率）、草は 0.7〜1.3 倍
                const float size = Hash2(cx, cz, 11u);
                const float heightScale = bShrub ? 0.6f + 0.75f * size * size + 0.15f * density : 0.7f + 0.6f * size;
                instance.HeightScale = heightScale;
                instance.WidthScale = heightScale * (0.85f + 0.35f * Hash2(cx, cz, 12u));
                instance.ColorVariation = Hash2(cx, cz, 13u);
                instance.WindPhase = Hash2(cx, cz, 14u);
                instances[static_cast<uint32_t>(kind)].push_back(instance);
            }
        }

        FoliageScene scene;
        scene.Wind.DirectionX = kWindDirectionX;
        scene.Wind.DirectionZ = kWindDirectionZ;
        scene.Wind.GustSpeed = kGustSpeed;
        scene.Wind.GustScale = kGustScale;
        uint32_t total = 0;
        for (uint32_t kindIndex = 0; kindIndex < kPlantKindCount; ++kindIndex)
        {
            FoliageBatch batch = MakeBatch(static_cast<PlantKind>(kindIndex));
            const uint32_t count = static_cast<uint32_t>(instances[kindIndex].size());
            LOG_INFO_F("WASTELAND_FOLIAGE_KIND id=%s instances=%u vertices=%zu triangles=%zu height=%.2f", batch.DebugName,
                       count, batch.Mesh ? batch.Mesh->Vertices.size() : 0u,
                       batch.Mesh ? batch.Mesh->Indices.size() / 3u : 0u,
                       batch.Mesh ? static_cast<double>(batch.Mesh->Height) : 0.0);
            if (count == 0 || !batch.Mesh)
            {
                continue;
            }
            batch.Instances = MakeShared<const VariableArray<FoliageInstance>>(std::move(instances[kindIndex]));
            scene.Batches.push_back(std::move(batch));
            total += count;
        }
        LOG_INFO_F("WASTELAND_FOLIAGE batches=%zu instances=%u rejected_slope=%u center=(%.1f,%.1f) half=%.1f",
                   scene.Batches.size(), total, rejectedSlope, static_cast<double>(kPatchCenterX),
                   static_cast<double>(kPatchCenterZ), static_cast<double>(kPatchHalfSize));
        return scene;
    }

    float WindTimeOffsetForView(const String &viewName)
    {
        return viewName == "grass_gust" ? kGustViewTimeOffset : 0.0f;
    }

} // namespace Game::GameModes::WastelandFoliage
