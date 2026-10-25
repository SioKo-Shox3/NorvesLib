#include "GameModes/Wasteland/WastelandPlacementExport.h"

#include "Core/Public/FileStream/FileStream.h"
#include "Core/Public/Logging/LogMacros.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Container;
using NorvesLib::Core::Rendering::FoliageBatch;
using NorvesLib::Core::Rendering::FoliageInstance;
using NorvesLib::Core::Rendering::FoliageMeshData;

namespace Game::GameModes::WastelandPlacementExport
{
    namespace
    {
        /// 物の種類（JSON の type）
        constexpr const char *kTypeTalus = "talus";
        constexpr const char *kTypePlant = "plant";
        constexpr const char *kTypeGravel = "gravel";
        /// 根元の円の半径を引く、メッシュの下の帯の割合
        constexpr float kRootBand = 0.05f;

        void AppendFormat(VariableArray<char> &text, const char *pFormat, ...)
        {
            char buffer[512];
            va_list args;
            va_start(args, pFormat);
            const int written = std::vsnprintf(buffer, sizeof(buffer), pFormat, args);
            va_end(args);
            if (written > 0)
            {
                text.insert(text.end(), buffer, buffer + std::min<size_t>(static_cast<size_t>(written), sizeof(buffer) - 1));
            }
        }

        /// 草・石のメッシュの根元の円の半径（ローカル）。草と低木は下の帯の頂点の水平の最大の距離、石は地面の線の円として
        /// 水平の最大の距離の半分
        float RootRadius(const FoliageMeshData &mesh, bool bGravel)
        {
            float minY = 0.0f;
            float maxY = 0.0f;
            bool bFirst = true;
            for (const Rendering::FoliageVertex &vertex : mesh.Vertices)
            {
                minY = bFirst ? vertex.Position[1] : std::min(minY, vertex.Position[1]);
                maxY = bFirst ? vertex.Position[1] : std::max(maxY, vertex.Position[1]);
                bFirst = false;
            }
            const float bandTop = minY + (maxY - minY) * kRootBand;
            float radius = 0.0f;
            for (const Rendering::FoliageVertex &vertex : mesh.Vertices)
            {
                if (!bGravel && vertex.Position[1] > bandTop)
                {
                    continue;
                }
                radius = std::max(radius, std::sqrt(vertex.Position[0] * vertex.Position[0] +
                                                    vertex.Position[2] * vertex.Position[2]));
            }
            return bGravel ? radius * 0.5f : radius;
        }

        /// 書き出すパス。作業ディレクトリは実行ファイルのディレクトリになるので、相対パスはリポジトリの根を基準にする
        String ResolvePath(const String &path)
        {
            const bool bAbsolute = (path.size() >= 2 && path[1] == ':') || (!path.empty() && (path[0] == '/' || path[0] == '\\'));
#if defined(NORVES_SOURCE_ASSET_DIR)
            if (!bAbsolute)
            {
                return String(NORVES_SOURCE_ASSET_DIR) + "/../" + path;
            }
#endif
            (void)bAbsolute;
            return path;
        }

        /// 草と石の物 1 つの行
        void AppendScatterObject(VariableArray<char> &text, uint32_t meshIndex, const FoliageInstance &instance, float rootY,
                                 bool &bFirstObject)
        {
            // foliage.vert の Yaw の回転（ワールドで Y 軸まわり）を、崖錐の岩と同じ「共役を掛ける」規約の四元数にする
            const float halfYaw = 0.5f * instance.Yaw;
            AppendFormat(text, "%s\n[%u,%.4f,%.4f,%.4f,0,%.7f,0,%.7f,%.5f,%.5f,%.5f,%.4f]", bFirstObject ? "" : ",", meshIndex,
                         static_cast<double>(instance.Position[0]), static_cast<double>(instance.Position[1]),
                         static_cast<double>(instance.Position[2]), static_cast<double>(-std::sin(halfYaw)),
                         static_cast<double>(std::cos(halfYaw)), static_cast<double>(instance.WidthScale),
                         static_cast<double>(instance.HeightScale), static_cast<double>(instance.WidthScale),
                         static_cast<double>(rootY));
            bFirstObject = false;
        }

        void AppendScatterMesh(VariableArray<char> &text, const FoliageBatch &batch, const char *pType, bool bGravel)
        {
            const float height = batch.Mesh ? batch.Mesh->Height : 0.0f;
            const float radius = batch.Mesh ? RootRadius(*batch.Mesh, bGravel) : 0.0f;
            AppendFormat(text, ",\n{\"name\":\"%s\",\"type\":\"%s\",\"height\":%.5f,\"root_radius\":%.5f,\"vertices\":%zu}",
                         batch.DebugName, pType, static_cast<double>(height), static_cast<double>(radius),
                         batch.Mesh ? batch.Mesh->Vertices.size() : 0u);
        }
    } // namespace

    bool Write(const String &path, const Rendering::Terrain::TerrainHeightField &field, const TalusInput &talus,
               WastelandFoliage::FoliageScatter &foliage, const WastelandGravel::GravelScatter &gravel)
    {
        VariableArray<char> text;
        text.reserve(64u << 20u);
        const Rendering::Terrain::TerrainGridLayout layout = field.GetGridLayout();
        AppendFormat(text,
                     "{\n\"version\":1,\n\"description\":\"WastelandPlacementExport.h\",\n\"rotation\":\"conjugate\",\n"
                     "\"grid\":{\"first_x\":%.3f,\"first_z\":%.3f,\"columns\":%u,\"rows\":%u,\"spacing\":%.4f},\n"
                     "\"sampling\":{\"lattice_m\":%.1f,\"max_per_cell\":%zu},\n\"meshes\":[",
                     layout.FirstX, layout.FirstZ, layout.Columns, layout.Rows, layout.Spacing,
                     static_cast<double>(kSampleLatticeMeters), kMaxScatterPerCell);

        // 形の表: 崖錐の岩 2・草と低木 5・石 3 段 × 4
        for (uint32_t mesh = 0; mesh < WastelandTalus::kTalusMeshCount; ++mesh)
        {
            const WastelandTalus::TalusMeshShape &shape = talus.Shapes[mesh];
            AppendFormat(text, "%s\n{\"name\":\"%s\",\"type\":\"%s\",\"available\":%s,\"points\":[", mesh == 0 ? "" : ",",
                         talus.MeshNames[mesh].c_str(), kTypeTalus, shape.bAvailable ? "true" : "false");
            for (size_t index = 0; index < shape.Points.size(); ++index)
            {
                const NorvesLib::Math::Vector3 &point = shape.Points[index];
                AppendFormat(text, "%s[%.5f,%.5f,%.5f]", index == 0 ? "" : ",", static_cast<double>(point.x),
                             static_cast<double>(point.y), static_cast<double>(point.z));
            }
            AppendFormat(text, "]}");
        }
        const uint32_t plantBase = WastelandTalus::kTalusMeshCount;
        for (uint32_t kind = 0; kind < WastelandFoliage::kPlantKindCount; ++kind)
        {
            AppendScatterMesh(text, foliage.GetTemplate(kind), kTypePlant, false);
        }
        const uint32_t gravelBase = plantBase + WastelandFoliage::kPlantKindCount;
        for (uint32_t tier = 0; tier < WastelandGravel::kTierCount; ++tier)
        {
            for (uint32_t kind = 0; kind < WastelandGravel::kKindsPerTier; ++kind)
            {
                AppendScatterMesh(text, gravel.GetTemplate(tier, kind), kTypeGravel, true);
            }
        }
        AppendFormat(text, "\n],\n\"objects\":[");

        bool bFirstObject = true;
        for (const Component::MegaScatterInstance &instance : talus.Instances)
        {
            const uint32_t meshIndex = instance.MeshIndex < WastelandTalus::kTalusMeshCount ? instance.MeshIndex : 0u;
            AppendFormat(text, "%s\n[%u,%.4f,%.4f,%.4f,%.7f,%.7f,%.7f,%.7f,%.5f,%.5f,%.5f,%.4f]", bFirstObject ? "" : ",",
                         meshIndex, static_cast<double>(instance.Position.x), static_cast<double>(instance.Position.y),
                         static_cast<double>(instance.Position.z), static_cast<double>(instance.Rotation.x),
                         static_cast<double>(instance.Rotation.y), static_cast<double>(instance.Rotation.z),
                         static_cast<double>(instance.Rotation.w), static_cast<double>(instance.Scale.x),
                         static_cast<double>(instance.Scale.y), static_cast<double>(instance.Scale.z),
                         static_cast<double>(instance.Position.y));
            bFirstObject = false;
        }

        // 草と石: 全域の格子の点ごとに、その点を含む区画を段ごとに作る
        size_t latticePoints = 0;
        size_t plants = 0;
        size_t stones = 0;
        size_t thinnedCells = 0;
        const double lastX = layout.FirstX + layout.Spacing * static_cast<double>(layout.Columns > 0 ? layout.Columns - 1 : 0);
        const double lastZ = layout.FirstZ + layout.Spacing * static_cast<double>(layout.Rows > 0 ? layout.Rows - 1 : 0);
        const double firstX = std::ceil(layout.FirstX / kSampleLatticeMeters) * kSampleLatticeMeters;
        const double firstZ = std::ceil(layout.FirstZ / kSampleLatticeMeters) * kSampleLatticeMeters;
        for (double z = firstZ; z <= lastZ; z += kSampleLatticeMeters)
        {
            for (double x = firstX; x <= lastX; x += kSampleLatticeMeters)
            {
                const float px = static_cast<float>(x);
                const float pz = static_cast<float>(z);
                ++latticePoints;
                VariableArray<FoliageInstance> plantCell[WastelandFoliage::kPlantKindCount];
                foliage.CollectCellAt(px, pz, plantCell);
                for (uint32_t kind = 0; kind < WastelandFoliage::kPlantKindCount; ++kind)
                {
                    for (const FoliageInstance &instance : plantCell[kind])
                    {
                        AppendScatterObject(text, plantBase + kind, instance, instance.Position[1], bFirstObject);
                        ++plants;
                    }
                }
                for (uint32_t tier = 0; tier < WastelandGravel::kTierCount; ++tier)
                {
                    VariableArray<FoliageInstance> stoneCell[WastelandGravel::kKindsPerTier];
                    gravel.CollectCellAt(tier, px, pz, stoneCell);
                    size_t cellCount = 0;
                    for (const VariableArray<FoliageInstance> &instances : stoneCell)
                    {
                        cellCount += instances.size();
                    }
                    const size_t stride = std::max<size_t>(1, (cellCount + kMaxScatterPerCell - 1) / kMaxScatterPerCell);
                    thinnedCells += stride > 1 ? 1u : 0u;
                    size_t ordinal = 0;
                    for (uint32_t kind = 0; kind < WastelandGravel::kKindsPerTier; ++kind)
                    {
                        for (const FoliageInstance &instance : stoneCell[kind])
                        {
                            if ((ordinal++ % stride) != 0)
                            {
                                continue;
                            }
                            const float groundY = field.SampleHeight(instance.Position[0], instance.Position[2]);
                            AppendScatterObject(text, gravelBase + tier * WastelandGravel::kKindsPerTier + kind, instance,
                                                groundY, bFirstObject);
                            ++stones;
                        }
                    }
                }
            }
        }
        AppendFormat(text, "\n]\n}\n");

        const String resolvedPath = ResolvePath(path);
        NorvesLib::FileStream::FileStreamUniquePtr stream = NorvesLib::FileStream::FileStream::CreateUnique(
            resolvedPath, NorvesLib::FileStream::FileMode::Write, NorvesLib::FileStream::FileAccess::Write,
            NorvesLib::FileStream::FileShare::None);
        if (!stream || stream->Write(text.data(), text.size()) != text.size())
        {
            LOG_ERROR_F("WASTELAND_PLACEMENT_EXPORT_FAILED path=%s 配置を書けませんでした", resolvedPath.c_str());
            return false;
        }
        stream->Flush();
        LOG_INFO_F("WASTELAND_PLACEMENT_EXPORT path=%s talus=%zu plants=%zu stones=%zu lattice_points=%zu lattice_m=%.0f "
                   "thinned_cells=%zu bytes=%zu",
                   resolvedPath.c_str(), talus.Instances.size(), plants, stones, latticePoints,
                   static_cast<double>(kSampleLatticeMeters), thinnedCells, text.size());
        return true;
    }
} // namespace Game::GameModes::WastelandPlacementExport
