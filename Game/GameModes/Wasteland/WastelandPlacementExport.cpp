#include "GameModes/Wasteland/WastelandPlacementExport.h"

#include "Core/Public/Container/Span.h"
#include "Core/Public/FileStream/FileStream.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Text/JsonDocument.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>

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
        /// 全域の全部を書く石の段（ほかの段は格子の区画の標本）
        constexpr const char *kFullGravelTier = "cobble";
        /// 形の範囲の表（リポジトリの根から）
        constexpr const char *kRegionsRelativePath = "Scripts/Terrain/ShapeRegions.json";

        /// 形の範囲の四角（両端を含む）
        struct RegionBox
        {
            float MinX = 0.0f;
            float MaxX = 0.0f;
            float MinZ = 0.0f;
            float MaxZ = 0.0f;
        };

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

        /// リポジトリの根を基準にしたパス。作業ディレクトリは実行ファイルのディレクトリになるので、相対パスはリポジトリの根から
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

        /// path の最後の区切りの後ろ（ファイルの名前）
        String FileName(const String &path)
        {
            size_t start = 0;
            for (size_t index = 0; index < path.size(); ++index)
            {
                if (path[index] == '/' || path[index] == '\\')
                {
                    start = index + 1;
                }
            }
            return String(path.c_str() + start);
        }

        /// 物の行の置き場のパス（.json を除いた名前 + .objects.bin）
        String ObjectsPath(const String &path)
        {
            const size_t length = path.size();
            if (length >= 5 && std::strcmp(path.c_str() + length - 5, ".json") == 0)
            {
                return path.substr(0, length - 5) + ".objects.bin";
            }
            return path + ".objects.bin";
        }

        /// 形の範囲の表を読む。読めなければ理由を outError に入れて false
        bool LoadRegions(VariableArray<RegionBox> &outRegions, String &outError)
        {
            const String path = ResolvePath(String(kRegionsRelativePath));
            std::ifstream stream(path.c_str(), std::ios::binary | std::ios::ate);
            const std::streamoff size = stream ? static_cast<std::streamoff>(stream.tellg()) : 0;
            if (size <= 0)
            {
                outError = String("読めません: ") + path;
                return false;
            }
            VariableArray<uint8_t> bytes(static_cast<size_t>(size));
            stream.seekg(0, std::ios::beg);
            stream.read(reinterpret_cast<char *>(bytes.data()), size);
            size_t start = 0;
            if (bytes.size() >= 3 && bytes[0] == 0xEFu && bytes[1] == 0xBBu && bytes[2] == 0xBFu)
            {
                start = 3;
            }
            JsonDocument document;
            String parseError;
            if (!JsonDocument::TryParseUtf8(Span<const uint8_t>(bytes.data() + start, bytes.size() - start), document,
                                            &parseError))
            {
                outError = path + ": " + parseError;
                return false;
            }
            const JsonValue regions = document.GetRoot().FindMember("regions");
            if (!regions.IsArray() || regions.GetArraySize() == 0)
            {
                outError = path + ": regions の配列がありません";
                return false;
            }
            for (size_t index = 0; index < regions.GetArraySize(); ++index)
            {
                const JsonValue x = regions.GetArrayElement(index).FindMember("x");
                const JsonValue z = regions.GetArrayElement(index).FindMember("z");
                if (!x.IsArray() || x.GetArraySize() != 2 || !z.IsArray() || z.GetArraySize() != 2)
                {
                    outError = path + ": 範囲の x・z が 2 つの数の配列でありません";
                    return false;
                }
                RegionBox box;
                box.MinX = static_cast<float>(x.GetArrayElement(0).AsNumber());
                box.MaxX = static_cast<float>(x.GetArrayElement(1).AsNumber());
                box.MinZ = static_cast<float>(z.GetArrayElement(0).AsNumber());
                box.MaxZ = static_cast<float>(z.GetArrayElement(1).AsNumber());
                outRegions.push_back(box);
            }
            return true;
        }

        bool InsideAnyRegion(const VariableArray<RegionBox> &regions, float x, float z)
        {
            for (const RegionBox &box : regions)
            {
                if (x >= box.MinX && x <= box.MaxX && z >= box.MinZ && z <= box.MaxZ)
                {
                    return true;
                }
            }
            return false;
        }

        /// 物の行を足す
        void AppendRow(VariableArray<float> &rows, const float (&values)[kObjectColumns])
        {
            rows.insert(rows.end(), values, values + kObjectColumns);
        }

        /// 草と石の物 1 つの行
        void AppendScatterObject(VariableArray<float> &rows, uint32_t meshIndex, const FoliageInstance &instance, float rootY)
        {
            // foliage.vert の Yaw の回転（ワールドで Y 軸まわり）を、崖錐の岩と同じ「共役を掛ける」規約の四元数にする
            const float halfYaw = 0.5f * instance.Yaw;
            const float values[kObjectColumns] = {static_cast<float>(meshIndex), instance.Position[0], instance.Position[1],
                                                  instance.Position[2], 0.0f, -std::sin(halfYaw), 0.0f, std::cos(halfYaw),
                                                  instance.WidthScale, instance.HeightScale, instance.WidthScale, rootY};
            AppendRow(rows, values);
        }

        void AppendScatterMesh(VariableArray<char> &text, const FoliageBatch &batch, const char *pType, const char *pTier,
                               bool bGravel)
        {
            const float height = batch.Mesh ? batch.Mesh->Height : 0.0f;
            const float radius = batch.Mesh ? WastelandFoliage::ComputeRootRadius(*batch.Mesh, bGravel) : 0.0f;
            AppendFormat(text,
                         ",\n{\"name\":\"%s\",\"type\":\"%s\",\"tier\":\"%s\",\"height\":%.5f,\"root_radius\":%.5f,"
                         "\"vertices\":%zu}",
                         batch.DebugName, pType, pTier, static_cast<double>(height), static_cast<double>(radius),
                         batch.Mesh ? batch.Mesh->Vertices.size() : 0u);
        }

        /// 区画の番号の範囲（[first, last]）。DEM の格子の [lo, hi] に掛かる区画
        void CellRange(double lo, double hi, double cellSize, int32_t &outFirst, int32_t &outLast)
        {
            outFirst = static_cast<int32_t>(std::floor(lo / cellSize));
            outLast = static_cast<int32_t>(std::floor(hi / cellSize));
        }

        bool WriteBytes(const String &path, const void *pData, size_t size)
        {
            NorvesLib::FileStream::FileStreamUniquePtr stream = NorvesLib::FileStream::FileStream::CreateUnique(
                path, NorvesLib::FileStream::FileMode::Write, NorvesLib::FileStream::FileAccess::Write,
                NorvesLib::FileStream::FileShare::None);
            if (!stream || stream->Write(pData, size) != size)
            {
                return false;
            }
            stream->Flush();
            return true;
        }
    } // namespace

    bool Write(const String &path, const Rendering::Terrain::TerrainHeightField &field, const TalusInput &talus,
               WastelandFoliage::FoliageScatter &foliage, const WastelandGravel::GravelScatter &gravel)
    {
        VariableArray<RegionBox> regions;
        String regionError;
        if (!LoadRegions(regions, regionError))
        {
            LOG_ERROR_F("WASTELAND_PLACEMENT_EXPORT_FAILED 形の範囲を読めませんでした: %s", regionError.c_str());
            return false;
        }

        const Rendering::Terrain::TerrainGridLayout layout = field.GetGridLayout();
        const double firstX = layout.FirstX;
        const double firstZ = layout.FirstZ;
        const double lastX = layout.FirstX + layout.Spacing * static_cast<double>(layout.Columns > 0 ? layout.Columns - 1 : 0);
        const double lastZ = layout.FirstZ + layout.Spacing * static_cast<double>(layout.Rows > 0 ? layout.Rows - 1 : 0);
        const uint32_t plantBase = WastelandTalus::kTalusMeshCount;
        const uint32_t gravelBase = plantBase + WastelandFoliage::kPlantKindCount;

        VariableArray<float> rows;
        rows.reserve(static_cast<size_t>(8u) << 20u);

        // 崖錐の岩: 置いた全部
        for (const Component::MegaScatterInstance &instance : talus.Instances)
        {
            const uint32_t meshIndex = instance.MeshIndex < WastelandTalus::kTalusMeshCount ? instance.MeshIndex : 0u;
            const float values[kObjectColumns] = {static_cast<float>(meshIndex), instance.Position.x, instance.Position.y,
                                                  instance.Position.z, instance.Rotation.x, instance.Rotation.y,
                                                  instance.Rotation.z, instance.Rotation.w, instance.Scale.x,
                                                  instance.Scale.y, instance.Scale.z, instance.Position.y};
            AppendRow(rows, values);
        }

        // 草と低木: DEM の格子に掛かる全部の区画
        size_t plants = 0;
        size_t plantCells = 0;
        {
            const double cellSize = WastelandFoliage::FoliageScatter::GetCellSize();
            int32_t firstCellX = 0, lastCellX = 0, firstCellZ = 0, lastCellZ = 0;
            CellRange(firstX, lastX, cellSize, firstCellX, lastCellX);
            CellRange(firstZ, lastZ, cellSize, firstCellZ, lastCellZ);
            for (int32_t cellZ = firstCellZ; cellZ <= lastCellZ; ++cellZ)
            {
                for (int32_t cellX = firstCellX; cellX <= lastCellX; ++cellX)
                {
                    VariableArray<FoliageInstance> plantCell[WastelandFoliage::kPlantKindCount];
                    foliage.CollectCellAt(static_cast<float>((cellX + 0.5) * cellSize),
                                          static_cast<float>((cellZ + 0.5) * cellSize), plantCell);
                    ++plantCells;
                    for (uint32_t kind = 0; kind < WastelandFoliage::kPlantKindCount; ++kind)
                    {
                        for (const FoliageInstance &instance : plantCell[kind])
                        {
                            AppendScatterObject(rows, plantBase + kind, instance, instance.Position[1]);
                            ++plants;
                        }
                    }
                }
            }
        }

        // 段・種類ごとの石の地面の線の円の半径（ローカル）
        float gravelRootRadius[WastelandGravel::kTierCount][WastelandGravel::kKindsPerTier] = {};
        for (uint32_t tier = 0; tier < WastelandGravel::kTierCount; ++tier)
        {
            for (uint32_t kind = 0; kind < WastelandGravel::kKindsPerTier; ++kind)
            {
                const FoliageBatch &batch = gravel.GetTemplate(tier, kind);
                gravelRootRadius[tier][kind] = batch.Mesh ? WastelandFoliage::ComputeRootRadius(*batch.Mesh, true) : 0.0f;
            }
        }

        // 石と礫の区画 1 つを書く
        size_t stoneCounts[WastelandGravel::kTierCount] = {};
        size_t stoneCells[WastelandGravel::kTierCount] = {};
        auto appendGravelCell = [&](uint32_t tier, float x, float z)
        {
            VariableArray<FoliageInstance> stoneCell[WastelandGravel::kKindsPerTier];
            gravel.CollectCellAt(tier, x, z, stoneCell);
            ++stoneCells[tier];
            for (uint32_t kind = 0; kind < WastelandGravel::kKindsPerTier; ++kind)
            {
                for (const FoliageInstance &instance : stoneCell[kind])
                {
                    // 地面の線は置き方（WastelandGravel::StoneGroundLine）と同じ高さ
                    float groundY = 0.0f;
                    WastelandGravel::StoneGroundLine(field, instance.Position[0], instance.Position[2],
                                                     gravelRootRadius[tier][kind] * instance.WidthScale, instance.Yaw, groundY);
                    AppendScatterObject(rows, gravelBase + tier * WastelandGravel::kKindsPerTier + kind, instance, groundY);
                    ++stoneCounts[tier];
                }
            }
        };

        // 大きな石: 全部の区画。石と小礫: 格子の点を含む区画を丸ごと（形の範囲の中は 48 m おき、外は 192 m おき）
        size_t latticeInside = 0;
        size_t latticeOutside = 0;
        for (uint32_t tier = 0; tier < WastelandGravel::kTierCount; ++tier)
        {
            if (std::strcmp(WastelandGravel::GravelScatter::GetTierName(tier), kFullGravelTier) != 0)
            {
                continue;
            }
            const double cellSize = WastelandGravel::GravelScatter::GetCellSize(tier);
            int32_t firstCellX = 0, lastCellX = 0, firstCellZ = 0, lastCellZ = 0;
            CellRange(firstX, lastX, cellSize, firstCellX, lastCellX);
            CellRange(firstZ, lastZ, cellSize, firstCellZ, lastCellZ);
            for (int32_t cellZ = firstCellZ; cellZ <= lastCellZ; ++cellZ)
            {
                for (int32_t cellX = firstCellX; cellX <= lastCellX; ++cellX)
                {
                    appendGravelCell(tier, static_cast<float>((cellX + 0.5) * cellSize),
                                     static_cast<float>((cellZ + 0.5) * cellSize));
                }
            }
        }
        {
            const int32_t firstPointX = static_cast<int32_t>(std::ceil(firstX / kSampleLatticeMeters));
            const int32_t lastPointX = static_cast<int32_t>(std::floor(lastX / kSampleLatticeMeters));
            const int32_t firstPointZ = static_cast<int32_t>(std::ceil(firstZ / kSampleLatticeMeters));
            const int32_t lastPointZ = static_cast<int32_t>(std::floor(lastZ / kSampleLatticeMeters));
            const auto onCoarse = [](int32_t index)
            { return ((index % kSampleCoarseStride) + kSampleCoarseStride) % kSampleCoarseStride == 0; };
            for (int32_t pointZ = firstPointZ; pointZ <= lastPointZ; ++pointZ)
            {
                for (int32_t pointX = firstPointX; pointX <= lastPointX; ++pointX)
                {
                    const float x = static_cast<float>(pointX) * kSampleLatticeMeters;
                    const float z = static_cast<float>(pointZ) * kSampleLatticeMeters;
                    const bool bInside = InsideAnyRegion(regions, x, z);
                    if (!bInside && !(onCoarse(pointX) && onCoarse(pointZ)))
                    {
                        continue;
                    }
                    ++(bInside ? latticeInside : latticeOutside);
                    for (uint32_t tier = 0; tier < WastelandGravel::kTierCount; ++tier)
                    {
                        if (std::strcmp(WastelandGravel::GravelScatter::GetTierName(tier), kFullGravelTier) != 0)
                        {
                            appendGravelCell(tier, x, z);
                        }
                    }
                }
            }
        }

        const String resolvedPath = ResolvePath(path);
        const String objectsPath = ObjectsPath(resolvedPath);
        const size_t objectCount = rows.size() / kObjectColumns;

        VariableArray<char> text;
        text.reserve(4u << 20u);
        AppendFormat(text,
                     "{\n\"version\":2,\n\"description\":\"WastelandPlacementExport.h\",\n\"rotation\":\"conjugate\",\n"
                     "\"grid\":{\"first_x\":%.3f,\"first_z\":%.3f,\"columns\":%u,\"rows\":%u,\"spacing\":%.4f},\n",
                     layout.FirstX, layout.FirstZ, layout.Columns, layout.Rows, layout.Spacing);
        AppendFormat(text,
                     "\"sampling\":{\"full\":[\"talus\",\"plant\",\"%s\"],\"cell_sampled\":[",
                     kFullGravelTier);
        bool bFirstTier = true;
        for (uint32_t tier = 0; tier < WastelandGravel::kTierCount; ++tier)
        {
            const char *pName = WastelandGravel::GravelScatter::GetTierName(tier);
            if (std::strcmp(pName, kFullGravelTier) != 0)
            {
                AppendFormat(text, "%s\"%s\"", bFirstTier ? "" : ",", pName);
                bFirstTier = false;
            }
        }
        AppendFormat(text,
                     "],\"regions\":\"%s\",\"lattice_inside_m\":%.1f,\"lattice_outside_m\":%.1f,"
                     "\"lattice_points_inside\":%zu,\"lattice_points_outside\":%zu,\"cell_size_m\":{\"plant\":%.4f",
                     kRegionsRelativePath, static_cast<double>(kSampleLatticeMeters),
                     static_cast<double>(kSampleLatticeMeters * kSampleCoarseStride), latticeInside, latticeOutside,
                     static_cast<double>(WastelandFoliage::FoliageScatter::GetCellSize()));
        for (uint32_t tier = 0; tier < WastelandGravel::kTierCount; ++tier)
        {
            AppendFormat(text, ",\"%s\":%.4f", WastelandGravel::GravelScatter::GetTierName(tier),
                         static_cast<double>(WastelandGravel::GravelScatter::GetCellSize(tier)));
        }
        AppendFormat(text, "},\"cells\":{\"plant\":%zu", plantCells);
        for (uint32_t tier = 0; tier < WastelandGravel::kTierCount; ++tier)
        {
            AppendFormat(text, ",\"%s\":%zu", WastelandGravel::GravelScatter::GetTierName(tier), stoneCells[tier]);
        }
        AppendFormat(text, "}},\n\"objects_file\":\"%s\",\n\"object_count\":%zu,\n\"object_columns\":%zu,\n"
                           "\"object_format\":\"float32 little-endian, row-major\",\n\"meshes\":[",
                     FileName(objectsPath).c_str(), objectCount, kObjectColumns);

        // 形の表: 崖錐の岩 2・草と低木 5・石 3 段 × 4
        for (uint32_t mesh = 0; mesh < WastelandTalus::kTalusMeshCount; ++mesh)
        {
            const WastelandTalus::TalusMeshShape &shape = talus.Shapes[mesh];
            AppendFormat(text, "%s\n{\"name\":\"%s\",\"type\":\"%s\",\"tier\":\"talus\",\"available\":%s,\"points\":[",
                         mesh == 0 ? "" : ",", talus.MeshNames[mesh].c_str(), kTypeTalus, shape.bAvailable ? "true" : "false");
            for (size_t index = 0; index < shape.Points.size(); ++index)
            {
                const NorvesLib::Math::Vector3 &point = shape.Points[index];
                AppendFormat(text, "%s[%.5f,%.5f,%.5f]", index == 0 ? "" : ",", static_cast<double>(point.x),
                             static_cast<double>(point.y), static_cast<double>(point.z));
            }
            AppendFormat(text, "]}");
        }
        for (uint32_t kind = 0; kind < WastelandFoliage::kPlantKindCount; ++kind)
        {
            AppendScatterMesh(text, foliage.GetTemplate(kind), kTypePlant, kTypePlant, false);
        }
        for (uint32_t tier = 0; tier < WastelandGravel::kTierCount; ++tier)
        {
            for (uint32_t kind = 0; kind < WastelandGravel::kKindsPerTier; ++kind)
            {
                AppendScatterMesh(text, gravel.GetTemplate(tier, kind), kTypeGravel,
                                  WastelandGravel::GravelScatter::GetTierName(tier), true);
            }
        }
        AppendFormat(text, "\n]\n}\n");

        if (!WriteBytes(objectsPath, rows.data(), rows.size() * sizeof(float)) ||
            !WriteBytes(resolvedPath, text.data(), text.size()))
        {
            LOG_ERROR_F("WASTELAND_PLACEMENT_EXPORT_FAILED path=%s 配置を書けませんでした", resolvedPath.c_str());
            return false;
        }
        LOG_INFO_F("WASTELAND_PLACEMENT_EXPORT path=%s talus=%zu plants=%zu pebbles=%zu stones=%zu cobbles=%zu "
                   "plant_cells=%zu cobble_cells=%zu lattice_inside=%zu lattice_outside=%zu objects=%zu bytes=%zu",
                   resolvedPath.c_str(), talus.Instances.size(), plants, stoneCounts[0], stoneCounts[1], stoneCounts[2],
                   plantCells, stoneCells[2], latticeInside, latticeOutside, objectCount, rows.size() * sizeof(float));
        return true;
    }
} // namespace Game::GameModes::WastelandPlacementExport
