#include "GameModes/Wasteland/WastelandFoliage.h"

#include "Core/Public/FileStream/FileStream.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Rendering/FoliageMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::Rendering;

namespace Game::GameModes::WastelandFoliage
{
    namespace
    {
        constexpr float kPi = 3.14159265358979f;

        // 株を置く升目[m]。世界の升目ごとに 1 株の候補を升目の中のでたらめな位置に置き、地図と密度の斑で間引く。
        // 手本の写真（ground_01）の低木と草は 1.5〜3 m おきで、裸地の斑を挟む
        constexpr float kCandidateSpacing = 1.5f;
        // 区画の一辺の升目の数と、区画の一辺[m]（区画ごとに描画が視錐台で外し、距離で株を間引く）
        constexpr int32_t kCandidatesPerCell = 16;
        constexpr float kCellSize = kCandidateSpacing * static_cast<float>(kCandidatesPerCell);
        // 区画を作る範囲（カメラのいる区画の四角から区画までの水平の最短の距離[m]）。作り直すのはカメラが区画を跨いだときだけなので、
        // カメラの点でなく区画の四角から測り、区画の中のどこへ動いても最も遠くまで残る低木の消える距離 120 m の内側を作り終えている。
        // 1 区画ぶん離れるまで捨てない（区画の境を行き来しても作り直さない）
        constexpr float kGenerateRadius = 128.0f;
        constexpr float kKeepRadius = kGenerateRadius + kCellSize;
        // 置かない傾斜[度]（崩れた沢の岸・急な斜面）
        constexpr float kMaxSlopeDegrees = 28.0f;
        // DEM の格子の縁から置かない幅[m]（株の根元の円が格子の外へはみ出さない）
        constexpr float kGridEdgeMargin = 1.0f;
        // 根元を地面へ埋める深さ[m]（地形の三角形の折れ目で浮かない）
        constexpr float kRootSink = 0.03f;
        // 区画の箱を株の根元の範囲から広げる量[m]（横は樹冠の半径と風のずれ、上は最も高い株の高さ）
        constexpr float kCellBoundsSideMargin = 1.5f;
        constexpr float kCellBoundsTopMargin = 1.6f;
        constexpr float kCellBoundsBottomMargin = 0.2f;
        // 風は方位 235°（南西）から北東へ吹く（地表の分類の地図の吹きだまりと同じ推定。Scripts/Terrain/TerrainMasks.py）。
        // エンジンの X は東・Z は南なので、吹いていく向きは (sin 55°, -cos 55°)
        constexpr float kWindDirectionX = 0.819f;
        constexpr float kWindDirectionZ = -0.574f;
        constexpr float kGustSpeed = 4.0f;
        constexpr float kGustScale = 14.0f;
        // grass_gust の風の時計のずらし[s]（草の揺れ 0.9 Hz で 1.3 周期、突風の塊は 5.6 m 流れる）
        constexpr float kGustViewTimeOffset = 1.4f;
        // 低木の太陽の影（VSM）: 遠景の方針で株の実体を描く 60 m まで落とす（Docs/Plans/WastelandRebuild.md の遠景の方針）。
        // ブラックブラッシュ 1 株は約 300 塊なので、株の影の塊の予算（約 1.6 万）に 60 m の内の数百株を収めるには遠い株を簡略にする。
        // 6 m からは細い枝先の角柱を省いて葉を 4 枚に 1 枚（2 倍の大きさ）に、15 m からは枝を省いて葉を 32 枚に 1 枚（5.7 倍）にする
        // （15 m の先の影の texel は 1 cm 前後で、株の揺れの数 cm は texel の数個なので影を揺らさない）
        constexpr float kShrubShadowDistance = 60.0f;
        constexpr float kShrubShadowMidDistance = 6.0f;
        constexpr float kShrubShadowFarDistance = 15.0f;

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

        float SmoothStep(float edge0, float edge1, float value)
        {
            const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        /// 地表の分類から決まる株の生えやすさ（0〜1。密度の斑の上限に掛ける）
        float VegetationSuitability(const Terrain::TerrainSurfaceWeights &weights)
        {
            // 地表（細粒）は 1、礫の舗石は 0.75（手本の ground_01 の礫の地面にも低木が並ぶ）、吹きだまりの砂は 0.5
            float suitability = weights.Fine + 0.75f * weights.Gravel + 0.5f * weights.Sand;
            // 沢底の流路（砂の重みが吹きだまりの上限 0.6 を超えて 1 へ近づく所）には生えない
            suitability *= 1.0f - SmoothStep(0.45f, 0.7f, weights.Sand);
            // 露岩・崖錐が混ざる所は早めに減らす（岩の上には生えない）
            suitability *= 1.0f - SmoothStep(0.15f, 0.45f, weights.Rock + weights.Talus);
            return suitability;
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
        static_assert(static_cast<uint32_t>(PlantKind::Count) == kPlantKindCount, "WastelandFoliage.h の種類の数と合わせる");

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

        /// 種類ごとのメッシュと色・風・間引きの距離
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
                // 葉は数十枚（写真の手前の枯れ草の株は細い葉の密な房で、外側の葉は先が垂れる）
                desc.BladeCount = kind == PlantKind::GrassA ? 90u : 70u;
                desc.CulmCount = kind == PlantKind::GrassA ? 6u : 4u;
                desc.Height = kind == PlantKind::GrassA ? 0.32f : 0.26f;
                desc.BaseRadius = 0.05f;
                desc.BladeWidth = 0.0035f;
                desc.Segments = 8;
                batch.Mesh = FoliageMesh::BuildGrassClump(desc);
                // 草は太陽の影を落とさない（接地は照明の接触影と AO に任せ、影の予算を低木に使う）
                batch.ShadowDistance = 0.0f;
                // 根元は前の年の灰褐色の枯れ葉、先は麦わら色〜灰色がかった黄土（写真の手前の草の株）
                SetColor(batch.StemColor, 132.0f, 114.0f, 90.0f);
                SetColor(batch.TipColorA, 182.0f, 152.0f, 104.0f);
                SetColor(batch.TipColorB, 152.0f, 138.0f, 114.0f);
                batch.Roughness = 0.85f;
                batch.WindStrength = 0.18f;
                batch.WindSwayFrequency = 0.9f;
                batch.MinPixelWidth = 0.7f;
                batch.MaxWidenRatio = 3.0f;
                // 高さ 0.2〜0.4 m の小さな株は、30 m から間引き始めて 80 m で消す（その先は 1 株が数画素で、地面の材質の斑に任せる）
                batch.FadeStartDistance = 30.0f;
                batch.FadeEndDistance = 80.0f;
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
                // 写真の低木は細い針金のような枝が根元から壺形に密に立つ塊で、太い骨格は見えない。
                // 根元の幹を細く多くし、枝先の細枝と葉で樹冠を埋める
                desc.StemCount = kind == PlantKind::BlackbrushA ? 30u : 26u;
                desc.BranchLevels = 4;
                desc.SpurLength = 0.07f;
                desc.StemRadius = 0.0035f;
                desc.TwigRadius = 0.0009f;
                desc.LeavesPerTwig = 5;
                desc.LeafLength = 0.016f;
                desc.LeafWidth = 0.005f;
                // 影の簡略のメッシュ（中景: 根元から 2 段の枝と 4 枚に 1 枚の葉、遠景: 32 枚に 1 枚の葉だけ）
                const FoliageMesh::ShrubShadowProxyDesc proxies[2] = {{1, 4u}, {-1, 32u}};
                TSharedPtr<FoliageMeshData> proxyMeshes[2];
                batch.Mesh = FoliageMesh::BuildShrub(desc, proxies, 2u, proxyMeshes);
                batch.ShadowDistance = kShrubShadowDistance;
                batch.ShadowLods.push_back(FoliageShadowLod{proxyMeshes[0], kShrubShadowMidDistance, false});
                batch.ShadowLods.push_back(FoliageShadowLod{proxyMeshes[1], kShrubShadowFarDistance, true});
                // 枝は暗い灰褐色、葉はそれより少し明るい褐色がかった灰色（写真の低木は地面より暗い褐色の塊。
                // 曇天の写真で樹冠の輝度は隣の地面の 0.35〜0.5 倍）
                SetColor(batch.StemColor, 106.0f, 88.0f, 74.0f);
                SetColor(batch.TipColorA, 156.0f, 132.0f, 108.0f);
                SetColor(batch.TipColorB, 146.0f, 128.0f, 110.0f);
                batch.Roughness = 0.8f;
                batch.WindStrength = 0.035f;
                batch.WindSwayFrequency = 0.5f;
                batch.MinPixelWidth = 0.7f;
                batch.MaxWidenRatio = 2.5f;
                // 高さ 0.4〜0.9 m の丸い株は遠くまで点として読める（写真では中景の低木の点が地平まで続く）。
                // 45 m から間引き始めて 120 m で消す
                batch.FadeStartDistance = 45.0f;
                batch.FadeEndDistance = 120.0f;
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
                // 細い茎だけの株は 1 株の塊が少ないので簡略にせず、遠い株の影を揺らさないだけにする
                batch.ShadowDistance = kShrubShadowDistance;
                batch.ShadowLods.push_back(FoliageShadowLod{batch.Mesh, kShrubShadowFarDistance, true});
                // 根元は木質の褐色、茎は黄緑〜くすんだオリーブ
                SetColor(batch.StemColor, 112.0f, 96.0f, 72.0f);
                SetColor(batch.TipColorA, 148.0f, 146.0f, 102.0f);
                SetColor(batch.TipColorB, 130.0f, 132.0f, 100.0f);
                batch.Roughness = 0.75f;
                batch.WindStrength = 0.06f;
                batch.WindSwayFrequency = 0.6f;
                batch.MinPixelWidth = 0.7f;
                batch.MaxWidenRatio = 2.5f;
                // 細い茎のほうき状の株は、ブラックブラッシュより少し手前で消す
                batch.FadeStartDistance = 40.0f;
                batch.FadeEndDistance = 100.0f;
                batch.DebugName = "mormon_tea";
                break;
            }
            }
            return batch;
        }

        /// 区画 (ax, az) の四角と区画 (bx, bz) の四角の間の水平の最短の距離[m]（隣り合う区画は 0）
        float DistanceBetweenCells(int32_t ax, int32_t az, int32_t bx, int32_t bz)
        {
            const float dx = static_cast<float>(std::max(std::abs(ax - bx) - 1, 0)) * kCellSize;
            const float dz = static_cast<float>(std::max(std::abs(az - bz) - 1, 0)) * kCellSize;
            return std::sqrt(dx * dx + dz * dz);
        }
    } // namespace

    float ComputeRootRadius(const FoliageMeshData &mesh, bool bGravel)
    {
        // 根元の円を引く、メッシュの下の帯の割合
        constexpr float kRootBand = 0.05f;
        float minY = 0.0f;
        float maxY = 0.0f;
        bool bFirst = true;
        for (const FoliageVertex &vertex : mesh.Vertices)
        {
            minY = bFirst ? vertex.Position[1] : std::min(minY, vertex.Position[1]);
            maxY = bFirst ? vertex.Position[1] : std::max(maxY, vertex.Position[1]);
            bFirst = false;
        }
        const float bandTop = minY + (maxY - minY) * kRootBand;
        float radius = 0.0f;
        for (const FoliageVertex &vertex : mesh.Vertices)
        {
            if (!bGravel && vertex.Position[1] > bandTop)
            {
                continue;
            }
            radius = std::max(radius, std::sqrt(vertex.Position[0] * vertex.Position[0] + vertex.Position[2] * vertex.Position[2]));
        }
        return bGravel ? radius * 0.5f : radius;
    }

    bool SamplePlacementGround(const Terrain::TerrainHeightField &field, const Terrain::TerrainPlacementGround *ground,
                               float x, float z, float &outY)
    {
        if (ground != nullptr)
        {
            return ground->SampleGround(field, x, z, outY);
        }
        outY = field.SampleHeight(x, z);
        return true;
    }

    bool SampleRootRing(const Terrain::TerrainHeightField &field, float x, float z, float radius, float yaw, float &outMinY,
                        float &outMaxY, const Terrain::TerrainPlacementGround *ground)
    {
        outMinY = 0.0f;
        outMaxY = 0.0f;
        for (uint32_t point = 0; point < kRootRingPoints; ++point)
        {
            const float angle = yaw + static_cast<float>(point) * (2.0f * kPi / static_cast<float>(kRootRingPoints));
            float height = 0.0f;
            if (!SamplePlacementGround(field, ground, x + radius * std::cos(angle), z + radius * std::sin(angle), height))
            {
                return false;
            }
            outMinY = point == 0u ? height : std::min(outMinY, height);
            outMaxY = point == 0u ? height : std::max(outMaxY, height);
        }
        return true;
    }

    void FoliageScatter::Initialize(TSharedPtr<const Terrain::TerrainHeightField> field,
                                    TSharedPtr<const Terrain::TerrainSurfaceMask> mask,
                                    TSharedPtr<const Terrain::TerrainPlacementGround> ground)
    {
        Reset();
        m_pField = std::move(field);
        m_pMask = std::move(mask);
        m_pGround = std::move(ground);
        for (uint32_t kindIndex = 0; kindIndex < kPlantKindCount; ++kindIndex)
        {
            m_Templates[kindIndex] = MakeBatch(static_cast<PlantKind>(kindIndex));
            const FoliageBatch &batch = m_Templates[kindIndex];
            m_RootRadius[kindIndex] = batch.Mesh ? ComputeRootRadius(*batch.Mesh, false) : 0.0f;
            LOG_INFO_F("WASTELAND_FOLIAGE_KIND id=%s vertices=%zu triangles=%zu height=%.2f fade=[%.0f,%.0f] shadow_distance=%.0f "
                       "shadow_lod_triangles=%zu/%zu",
                       batch.DebugName, batch.Mesh ? batch.Mesh->Vertices.size() : 0u,
                       batch.Mesh ? batch.Mesh->Indices.size() / 3u : 0u,
                       batch.Mesh ? static_cast<double>(batch.Mesh->Height) : 0.0,
                       static_cast<double>(batch.FadeStartDistance), static_cast<double>(batch.FadeEndDistance),
                       static_cast<double>(batch.ShadowDistance),
                       batch.ShadowLods.size() > 0u && batch.ShadowLods[0].Mesh ? batch.ShadowLods[0].Mesh->Indices.size() / 3u : 0u,
                       batch.ShadowLods.size() > 1u && batch.ShadowLods[1].Mesh ? batch.ShadowLods[1].Mesh->Indices.size() / 3u : 0u);
        }
        LOG_INFO_F("WASTELAND_FOLIAGE_SCATTER mask=%d cell=%.1f candidate=%.1f generate_radius=%.0f",
                   m_pMask ? 1 : 0, static_cast<double>(kCellSize), static_cast<double>(kCandidateSpacing),
                   static_cast<double>(kGenerateRadius));
    }

    void FoliageScatter::Reset()
    {
        m_pField.reset();
        m_pMask.reset();
        m_pGround.reset();
        for (FoliageBatch &batch : m_Templates)
        {
            batch = FoliageBatch{};
        }
        m_Cells.clear();
        m_ExclusionCircles.clear();
        m_ExclusionRects.clear();
        m_bHasCenter = false;
    }

    void FoliageScatter::SetExclusions(VariableArray<ExclusionCircle> circles, VariableArray<ExclusionRect> rects)
    {
        m_ExclusionCircles = std::move(circles);
        m_ExclusionRects = std::move(rects);
        m_Cells.clear();
        m_bHasCenter = false;
    }

    bool FoliageScatter::IsExcluded(float x, float z) const
    {
        for (const ExclusionCircle &circle : m_ExclusionCircles)
        {
            const float dx = x - circle.X;
            const float dz = z - circle.Z;
            if (dx * dx + dz * dz < circle.Radius * circle.Radius)
            {
                return true;
            }
        }
        for (const ExclusionRect &rect : m_ExclusionRects)
        {
            const float dx = x - rect.CenterX;
            const float dz = z - rect.CenterZ;
            const float along = dx * rect.AlongX + dz * rect.AlongZ;
            const float lateral = dx * rect.AlongZ - dz * rect.AlongX;
            if (std::fabs(along) < rect.HalfLength && std::fabs(lateral) < rect.HalfWidth)
            {
                return true;
            }
        }
        return false;
    }

    void FoliageScatter::BuildCell(Cell &cell, VariableArray<GroundRejection> *rejected)
    {
        const Terrain::TerrainHeightField &field = *m_pField;
        const float minSlopeCos = std::cos(kMaxSlopeDegrees * kPi / 180.0f);
        cell.MinRootY = 0.0f;
        cell.MaxRootY = 0.0f;
        bool bHasRoot = false;
        for (int32_t j = 0; j < kCandidatesPerCell; ++j)
        {
            for (int32_t i = 0; i < kCandidatesPerCell; ++i)
            {
                // 世界の升目の番号（区画の作り方やカメラの動きに関わらず、同じ升目は同じ株になる）
                const int32_t gx = cell.X * kCandidatesPerCell + i;
                const int32_t gz = cell.Z * kCandidatesPerCell + j;
                // 升目ごとに 2 つの候補: 1 つ目は低木か草の株、2 つ目は低木の間に生える草の株だけ
                // （手本の写真の中景に淡い草の株が低木と同じくらい多い）。乱数の塩は候補ごとに替える
                for (uint32_t slot = 0; slot < 2u; ++slot)
                {
                    const uint32_t salt = slot * 32u;
                    const float x = (static_cast<float>(gx) + Hash2(gx, gz, salt + 1u)) * kCandidateSpacing;
                    const float z = (static_cast<float>(gz) + Hash2(gx, gz, salt + 2u)) * kCandidateSpacing;
                    if (!field.Contains(x - kGridEdgeMargin, z - kGridEdgeMargin) ||
                        !field.Contains(x + kGridEdgeMargin, z + kGridEdgeMargin) || IsExcluded(x, z))
                    {
                        continue;
                    }
                    float suitability = 1.0f;
                    Terrain::TerrainSurfaceWeights weights;
                    if (m_pMask && m_pMask->Sample(x, z, weights))
                    {
                        suitability = VegetationSuitability(weights);
                    }
                    // 露岩・崖錐・沢底の流路（生えやすさがほぼ 0）は数えて外す
                    if (suitability < 0.02f)
                    {
                        ++m_MaskRejectedCandidates;
                        continue;
                    }
                    // 密度の斑: 数十 m の濃淡と数 m の群れ。裸地の斑を残す（全域に均等に撒かない）。
                    // 草だけの候補は、低木と別の 12 m・4 m の斑で草の濃い所と薄い所を作る
                    const float density = slot == 0u
                                              ? 0.55f * ValueNoise(x, z, 22.0f, 3u) + 0.45f * ValueNoise(x, z, 6.0f, 4u)
                                              : 0.6f * ValueNoise(x, z, 12.0f, 31u) + 0.4f * ValueNoise(x, z, 4.0f, 32u);
                    const float acceptance = slot == 0u ? std::clamp((density - 0.22f) * 1.6f, 0.0f, 0.92f)
                                                        : std::clamp((density - 0.3f) * 1.5f, 0.0f, 0.7f);
                    if (Hash2(gx, gz, salt + 5u) >= acceptance * suitability)
                    {
                        continue;
                    }
                    const Terrain::TerrainNormal normal = field.SampleNormal(x, z, 0.5f);
                    if (!(normal.Y >= minSlopeCos))
                    {
                        continue;
                    }
                    // 種類: 1 つ目の候補は草の株がおよそ半分、低木は群れの濃い所に多い。モルモンティーは 1 割ほど
                    const float pick = slot == 0u ? Hash2(gx, gz, 6u) : 1.0f;
                    const float shrubShare = 0.3f + 0.25f * ValueNoise(x, z, 9.0f, 7u);
                    PlantKind kind;
                    if (pick < 0.08f)
                    {
                        kind = PlantKind::MormonTea;
                    }
                    else if (pick < 0.08f + shrubShare)
                    {
                        kind = Hash2(gx, gz, 8u) < 0.5f ? PlantKind::BlackbrushA : PlantKind::BlackbrushB;
                    }
                    else
                    {
                        kind = Hash2(gx, gz, salt + 9u) < 0.55f ? PlantKind::GrassA : PlantKind::GrassB;
                    }
                    const bool bShrub = kind == PlantKind::BlackbrushA || kind == PlantKind::BlackbrushB ||
                                        kind == PlantKind::MormonTea;

                    FoliageInstance instance;
                    instance.Position[0] = x;
                    instance.Position[2] = z;
                    instance.Yaw = Hash2(gx, gz, salt + 10u) * 2.0f * kPi;
                    // 大きさ: 低木は高さ 0.35〜0.95 m（メッシュの 0.6〜0.7 m の倍率）、草は 0.7〜1.3 倍
                    const float size = Hash2(gx, gz, salt + 11u);
                    const float heightScale = bShrub ? 0.6f + 0.75f * size * size + 0.15f * density : 0.7f + 0.6f * size;
                    instance.HeightScale = heightScale;
                    instance.WidthScale = heightScale * (0.85f + 0.35f * Hash2(gx, gz, salt + 12u));
                    // 根元: 中心の高さから埋めた高さを、根元の円の最も低い点より浮かない所まで下げる（斜面で円の下手が浮かない）。
                    // 円の最も高い点が根元より埋もれの許しを超えて高い（根元の円の広い株が傾いた所にある）ときは、根元の枝が地面に
                    // 沿う低木 B を根元の細い低木 A に替え、それでも収まらなければ置かない
                    float rootY = 0.0f;
                    bool bGrounded = false;
                    for (uint32_t attempt = 0; attempt < 2u && !bGrounded; ++attempt)
                    {
                        if (attempt == 1u)
                        {
                            if (kind != PlantKind::BlackbrushB)
                            {
                                break;
                            }
                            kind = PlantKind::BlackbrushA;
                        }
                        float ringMinY = 0.0f;
                        float ringMaxY = 0.0f;
                        float centerY = 0.0f;
                        if (!SamplePlacementGround(field, m_pGround.get(), x, z, centerY) ||
                            !SampleRootRing(field, x, z, m_RootRadius[static_cast<uint32_t>(kind)] * instance.WidthScale,
                                            instance.Yaw, ringMinY, ringMaxY, m_pGround.get()))
                        {
                            ++cell.Unreliable[static_cast<uint32_t>(kind)];
                            if (rejected != nullptr)
                            {
                                rejected->push_back({x, z, static_cast<uint32_t>(kind)});
                            }
                            break;
                        }
                        rootY = std::min(centerY - kRootSink, ringMinY + kRootRingFloatAllowance);
                        bGrounded = rootY >= ringMaxY - kRootRingBurialAllowance;
                    }
                    if (!bGrounded)
                    {
                        continue;
                    }
                    instance.Position[1] = rootY;
                    instance.ColorVariation = Hash2(gx, gz, salt + 13u);
                    instance.WindPhase = Hash2(gx, gz, salt + 14u);
                    instance.FadeThreshold = Hash2(gx, gz, salt + 15u);
                    cell.Instances[static_cast<uint32_t>(kind)].push_back(instance);
                    cell.MinRootY = bHasRoot ? std::min(cell.MinRootY, instance.Position[1]) : instance.Position[1];
                    cell.MaxRootY = bHasRoot ? std::max(cell.MaxRootY, instance.Position[1]) : instance.Position[1];
                    bHasRoot = true;
                }
            }
        }
        // 描画は区画の先頭から、しきい値が残す割合を下回る個数だけを描くので、しきい値の小さい順に並べる
        for (VariableArray<FoliageInstance> &instances : cell.Instances)
        {
            std::sort(instances.begin(), instances.end(),
                      [](const FoliageInstance &a, const FoliageInstance &b) { return a.FadeThreshold < b.FadeThreshold; });
        }
    }

    float FoliageScatter::GetCellSize()
    {
        return kCellSize;
    }

    void FoliageScatter::CollectCellAt(float x, float z, VariableArray<FoliageInstance> (&outInstances)[kPlantKindCount],
                                       VariableArray<GroundRejection> *rejected)
    {
        if (!m_pField)
        {
            return;
        }
        // 地図で外した候補の数は Update のログのものなので、書き出しの区画では数えたまま戻す
        const uint32_t savedRejected = m_MaskRejectedCandidates;
        Cell cell;
        cell.X = static_cast<int32_t>(std::floor(x / kCellSize));
        cell.Z = static_cast<int32_t>(std::floor(z / kCellSize));
        BuildCell(cell, rejected);
        m_MaskRejectedCandidates = savedRejected;
        for (uint32_t kind = 0; kind < kPlantKindCount; ++kind)
        {
            outInstances[kind].insert(outInstances[kind].end(), cell.Instances[kind].begin(), cell.Instances[kind].end());
        }
    }

    bool FoliageScatter::Update(float cameraX, float cameraZ, FoliageScene &inOutScene)
    {
        if (!m_pField || !std::isfinite(cameraX) || !std::isfinite(cameraZ))
        {
            return false;
        }
        const int32_t centerX = static_cast<int32_t>(std::floor(cameraX / kCellSize));
        const int32_t centerZ = static_cast<int32_t>(std::floor(cameraZ / kCellSize));
        if (m_bHasCenter && centerX == m_CenterCellX && centerZ == m_CenterCellZ)
        {
            return false;
        }
        m_bHasCenter = true;
        m_CenterCellX = centerX;
        m_CenterCellZ = centerZ;

        // 離れた区画を捨て、近づいた区画を作る
        m_MaskRejectedCandidates = 0;
        const size_t before = m_Cells.size();
        m_Cells.erase(std::remove_if(m_Cells.begin(), m_Cells.end(),
                                     [&](const Cell &cell)
                                     { return DistanceBetweenCells(centerX, centerZ, cell.X, cell.Z) > kKeepRadius; }),
                      m_Cells.end());
        const size_t dropped = before - m_Cells.size();
        const int32_t reach = static_cast<int32_t>(std::ceil(kGenerateRadius / kCellSize)) + 1;
        uint32_t created = 0;
        for (int32_t cellZ = centerZ - reach; cellZ <= centerZ + reach; ++cellZ)
        {
            for (int32_t cellX = centerX - reach; cellX <= centerX + reach; ++cellX)
            {
                if (DistanceBetweenCells(centerX, centerZ, cellX, cellZ) > kGenerateRadius)
                {
                    continue;
                }
                const bool bExists = std::any_of(m_Cells.begin(), m_Cells.end(), [&](const Cell &cell)
                                                 { return cell.X == cellX && cell.Z == cellZ; });
                if (bExists)
                {
                    continue;
                }
                Cell cell;
                cell.X = cellX;
                cell.Z = cellZ;
                BuildCell(cell);
                m_Cells.push_back(std::move(cell));
                ++created;
            }
        }
        RebuildScene(inOutScene);

        size_t instances = 0;
        for (const FoliageBatch &batch : inOutScene.Batches)
        {
            instances += batch.Instances ? batch.Instances->size() : 0u;
        }
        uint64_t unreliable[kPlantKindCount] = {};
        for (const Cell &cell : m_Cells)
        {
            for (uint32_t kind = 0; kind < kPlantKindCount; ++kind)
            {
                unreliable[kind] += cell.Unreliable[kind];
            }
        }
        LOG_INFO_F("WASTELAND_FOLIAGE center_cell=(%d,%d) cells=%zu created=%u dropped=%zu batches=%zu instances=%zu "
                   "mask_rejected=%u unreliable=%llu/%llu/%llu/%llu/%llu",
                   centerX, centerZ, m_Cells.size(), created, dropped, inOutScene.Batches.size(), instances,
                   m_MaskRejectedCandidates, static_cast<unsigned long long>(unreliable[0]),
                   static_cast<unsigned long long>(unreliable[1]), static_cast<unsigned long long>(unreliable[2]),
                   static_cast<unsigned long long>(unreliable[3]), static_cast<unsigned long long>(unreliable[4]));
        return true;
    }

    void FoliageScatter::RebuildScene(FoliageScene &inOutScene) const
    {
        inOutScene.Batches.clear();
        for (uint32_t kindIndex = 0; kindIndex < kPlantKindCount; ++kindIndex)
        {
            const FoliageBatch &batchTemplate = m_Templates[kindIndex];
            if (!batchTemplate.Mesh)
            {
                continue;
            }
            auto instances = MakeShared<VariableArray<FoliageInstance>>();
            auto cells = MakeShared<VariableArray<FoliageCell>>();
            size_t total = 0;
            for (const Cell &cell : m_Cells)
            {
                total += cell.Instances[kindIndex].size();
            }
            instances->reserve(total);
            for (const Cell &cell : m_Cells)
            {
                const VariableArray<FoliageInstance> &source = cell.Instances[kindIndex];
                if (source.empty())
                {
                    continue;
                }
                FoliageCell range;
                range.BoundsMin[0] = static_cast<float>(cell.X) * kCellSize - kCellBoundsSideMargin;
                range.BoundsMin[1] = cell.MinRootY - kCellBoundsBottomMargin;
                range.BoundsMin[2] = static_cast<float>(cell.Z) * kCellSize - kCellBoundsSideMargin;
                range.BoundsMax[0] = static_cast<float>(cell.X + 1) * kCellSize + kCellBoundsSideMargin;
                range.BoundsMax[1] = cell.MaxRootY + kCellBoundsTopMargin;
                range.BoundsMax[2] = static_cast<float>(cell.Z + 1) * kCellSize + kCellBoundsSideMargin;
                range.FirstInstance = static_cast<uint32_t>(instances->size());
                range.InstanceCount = static_cast<uint32_t>(source.size());
                instances->insert(instances->end(), source.begin(), source.end());
                cells->push_back(range);
            }
            if (instances->empty())
            {
                continue;
            }
            FoliageBatch batch = batchTemplate;
            batch.Instances = TSharedPtr<const VariableArray<FoliageInstance>>(std::move(instances));
            batch.Cells = TSharedPtr<const VariableArray<FoliageCell>>(std::move(cells));
            inOutScene.Batches.push_back(std::move(batch));
        }
    }

    FoliageWindParameters MakeWind()
    {
        FoliageWindParameters wind;
        wind.DirectionX = kWindDirectionX;
        wind.DirectionZ = kWindDirectionZ;
        wind.GustSpeed = kGustSpeed;
        wind.GustScale = kGustScale;
        return wind;
    }

    float WindTimeOffsetForView(const String &viewName)
    {
        return viewName == "grass_gust" ? kGustViewTimeOffset : 0.0f;
    }

    FoliageBatch MakeReferencePlant(const Terrain::TerrainHeightField &field)
    {
        FoliageBatch batch = MakeBatch(PlantKind::BlackbrushA);
        batch.DebugName = "reference_blackbrush_a";
        // 揺らさない（風のずれは 0）・間引かない（消える距離が始まりの距離以下なら全部残す）
        batch.WindStrength = 0.0f;
        batch.FadeStartDistance = 0.0f;
        batch.FadeEndDistance = 0.0f;
        // 比べる Cycles の描画は全部の枝と葉で影を落とすので、どの距離でも本体のメッシュで影を描く
        batch.ShadowLods.clear();
        FoliageInstance instance;
        instance.Position[0] = kReferencePlantX;
        instance.Position[1] = field.SampleHeight(kReferencePlantX, kReferencePlantZ) - kRootSink;
        instance.Position[2] = kReferencePlantZ;
        // 大きさは撒く株の中ほど（高さ 0.6 m のメッシュの 1.2 倍）。色の揺らぎ・揺れの位相は決めた値
        instance.Yaw = 0.0f;
        instance.HeightScale = 1.2f;
        instance.WidthScale = 1.2f;
        instance.ColorVariation = 0.5f;
        instance.WindPhase = 0.25f;
        instance.FadeThreshold = 0.0f;
        auto instances = MakeShared<VariableArray<FoliageInstance>>();
        instances->push_back(instance);
        auto cells = MakeShared<VariableArray<FoliageCell>>();
        FoliageCell cell;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            cell.BoundsMin[axis] = instance.Position[axis] - kCellBoundsSideMargin;
            cell.BoundsMax[axis] = instance.Position[axis] + kCellBoundsSideMargin;
        }
        cell.BoundsMin[1] = instance.Position[1] - kCellBoundsBottomMargin;
        cell.BoundsMax[1] = instance.Position[1] + kCellBoundsTopMargin;
        cell.FirstInstance = 0;
        cell.InstanceCount = 1;
        cells->push_back(cell);
        batch.Instances = TSharedPtr<const VariableArray<FoliageInstance>>(std::move(instances));
        batch.Cells = TSharedPtr<const VariableArray<FoliageCell>>(std::move(cells));
        return batch;
    }

    bool ExportReferencePlant(const FoliageBatch &batch, const String &path, ReferencePlantExport &outRecord)
    {
        outRecord = ReferencePlantExport{};
        if (!batch.Mesh || !batch.Instances || batch.Instances->size() != 1u)
        {
            return false;
        }
        const FoliageMeshData &mesh = *batch.Mesh;
        const FoliageInstance &instance = (*batch.Instances)[0];
        outRecord.VertexCount = mesh.Vertices.size();
        outRecord.TriangleCount = mesh.Indices.size() / 3u;
        char header[640] = {};
        const int headerLength = std::snprintf(
            header, sizeof(header),
            "ply\nformat binary_little_endian 1.0\n"
            "comment NorvesLib wasteland reference plant source=%s\n"
            "comment space=engine_world units=m axes=+X_east,+Y_up,+Z_south color=linear_albedo\n"
            "element vertex %llu\nproperty float x\nproperty float y\nproperty float z\n"
            "property float nx\nproperty float ny\nproperty float nz\n"
            "property float red\nproperty float green\nproperty float blue\n"
            "element face %llu\nproperty list uchar int vertex_indices\nend_header\n",
            batch.DebugName, static_cast<unsigned long long>(outRecord.VertexCount),
            static_cast<unsigned long long>(outRecord.TriangleCount));
        if (headerLength <= 0 || headerLength >= static_cast<int>(sizeof(header)))
        {
            return false;
        }
        VariableArray<uint8_t> bytes;
        bytes.reserve(static_cast<size_t>(headerLength) + mesh.Vertices.size() * 36u + outRecord.TriangleCount * 13u);
        const auto append = [&bytes](const void *pData, size_t byteCount)
        {
            const uint8_t *begin = static_cast<const uint8_t *>(pData);
            bytes.insert(bytes.end(), begin, begin + byteCount);
        };
        append(header, static_cast<size_t>(headerLength));
        // foliage.vert と同じ置き方と色（風のずれは 0、距離の縮みは 1）
        const float c = std::cos(instance.Yaw);
        const float s = std::sin(instance.Yaw);
        const float scale[3] = {instance.WidthScale, instance.HeightScale, instance.WidthScale};
        float tipColor[3] = {};
        for (uint32_t channel = 0; channel < 3; ++channel)
        {
            tipColor[channel] = batch.TipColorA[channel] +
                                (batch.TipColorB[channel] - batch.TipColorA[channel]) * instance.ColorVariation;
        }
        const float brightnessPhase = instance.WindPhase * 7.31f + instance.ColorVariation * 3.17f;
        const float brightness = 0.88f + 0.24f * (brightnessPhase - std::floor(brightnessPhase));
        for (const FoliageVertex &vertex : mesh.Vertices)
        {
            const float local[3] = {vertex.Position[0] * scale[0], vertex.Position[1] * scale[1],
                                    vertex.Position[2] * scale[2]};
            const float position[3] = {instance.Position[0] + c * local[0] + s * local[2], instance.Position[1] + local[1],
                                       instance.Position[2] - s * local[0] + c * local[2]};
            const float normalLocal[3] = {vertex.Normal[0] / scale[0], vertex.Normal[1] / scale[1],
                                          vertex.Normal[2] / scale[2]};
            float normal[3] = {c * normalLocal[0] + s * normalLocal[2], normalLocal[1],
                               -s * normalLocal[0] + c * normalLocal[2]};
            const float length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
            for (float &component : normal)
            {
                component = length > 0.0f ? component / length : 0.0f;
            }
            float albedo[3] = {};
            for (uint32_t channel = 0; channel < 3; ++channel)
            {
                albedo[channel] =
                    (batch.StemColor[channel] + (tipColor[channel] - batch.StemColor[channel]) * vertex.Tint) * brightness;
            }
            const float values[9] = {position[0], position[1], position[2], normal[0], normal[1],
                                     normal[2],   albedo[0],   albedo[1],   albedo[2]};
            append(values, sizeof(values));
            const float dx = position[0] - instance.Position[0];
            const float dz = position[2] - instance.Position[2];
            outRecord.CrownRadius = std::max(outRecord.CrownRadius, std::sqrt(dx * dx + dz * dz));
            outRecord.Height = std::max(outRecord.Height, position[1] - instance.Position[1]);
        }
        for (size_t i = 0; i + 2 < mesh.Indices.size(); i += 3)
        {
            const uint8_t count = 3;
            const int32_t face[3] = {static_cast<int32_t>(mesh.Indices[i]), static_cast<int32_t>(mesh.Indices[i + 1]),
                                     static_cast<int32_t>(mesh.Indices[i + 2])};
            append(&count, sizeof(count));
            append(face, sizeof(face));
        }
        NorvesLib::FileStream::FileStreamUniquePtr stream = NorvesLib::FileStream::FileStream::CreateUnique(
            path, NorvesLib::FileStream::FileMode::Write, NorvesLib::FileStream::FileAccess::Write,
            NorvesLib::FileStream::FileShare::None);
        if (!stream || stream->Write(bytes.data(), bytes.size()) != bytes.size())
        {
            return false;
        }
        stream->Flush();
        return true;
    }

} // namespace Game::GameModes::WastelandFoliage
