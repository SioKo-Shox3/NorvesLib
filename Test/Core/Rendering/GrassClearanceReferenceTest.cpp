// 草の切り抜きの内外、近接、低背化、無効時の一致と再現性を CPU の記録で確かめる。
#include "Rendering/CameraViewConstants.h"
#include "Rendering/GrassTileSelector.h"
#include "Math/MatrixUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;
namespace Math = NorvesLib::Math;

namespace
{
    constexpr uint32_t Resolution = 4;
    constexpr uint32_t WindowSize = 8;
    constexpr double MarginMeters = 0.01;
    int GFailureCount = 0;
    using Records = Container::VariableArray<GrassBladeRecord>;

    void Check(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            std::printf("失敗: %s\n", message);
            ++GFailureCount;
        }
    }

    GrassFieldRenderData MakeField()
    {
        GrassFieldRenderData field;
        field.ChunkSizeMeters = 32.0f;
        field.LayerAResolution = Resolution;
        field.LayerBResolution = Resolution;
        field.ContentRevision = 1;
        // 前方の三つの距離帯と後方を覆い、(0, 1) だけ密度をゼロにする。
        for (int32_t z = -1; z <= 3; ++z)
        {
            for (int32_t x = -1; x <= 0; ++x)
            {
                GrassFieldChunkRenderData chunk;
                chunk.ChunkX = x;
                chunk.ChunkZ = z;
                chunk.LayerAByteOffset = static_cast<uint32_t>(field.LayerAPixels.size());
                chunk.LayerBByteOffset = static_cast<uint32_t>(field.LayerBPixels.size());
                field.Chunks.push_back(chunk);
                for (uint32_t pixel = 0; pixel < Resolution * Resolution; ++pixel)
                {
                    field.LayerAPixels.push_back(x == 0 && z == 1 ? 0 : 255);
                    field.LayerAPixels.push_back(255);
                    field.LayerAPixels.push_back(128);
                    field.LayerAPixels.push_back(0);
                    field.LayerBPixels.push_back(128);
                    field.LayerBPixels.push_back(128);
                    field.LayerBPixels.push_back(128);
                    field.LayerBPixels.push_back(0);
                }
            }
        }
        GrassSpeciesGpuData species{};
        species.HeightMinMeters = 1.0f;
        species.HeightMaxMeters = 1.2f;
        species.BladeWidthMeters = 0.01f;
        species.BladesPerSquareMeter = 128.0f;
        field.Species.push_back(species);
        return field;
    }

    bool SameRecords(const Records& a, const Records& b)
    {
        return a.size() == b.size() && (a.empty() ||
            std::memcmp(a.data(), b.data(), a.size() * sizeof(GrassBladeRecord)) == 0);
    }

    bool SameCounts(const GrassGenerateCounts& a, const GrassGenerateCounts& b)
    {
        return std::memcmp(a.PerLod, b.PerLod, sizeof(a.PerLod)) == 0;
    }

    bool PositionLess(const GrassBladeRecord& a, const GrassBladeRecord& b)
    {
        if (a.PositionX != b.PositionX)
        {
            return a.PositionX < b.PositionX;
        }
        if (a.PositionY != b.PositionY)
        {
            return a.PositionY < b.PositionY;
        }
        return a.PositionZ < b.PositionZ;
    }

    bool SamePosition(const GrassBladeRecord& a, const GrassBladeRecord& b)
    {
        return std::memcmp(&a.PositionX, &b.PositionX, sizeof(float)) == 0 &&
            std::memcmp(&a.PositionY, &b.PositionY, sizeof(float)) == 0 &&
            std::memcmp(&a.PositionZ, &b.PositionZ, sizeof(float)) == 0;
    }

    const GrassBladeRecord* FindPosition(const Records& records, const GrassBladeRecord& record)
    {
        const auto* end = records.data() + records.size();
        const auto* found = std::lower_bound(records.data(), end, record, PositionLess);
        return found != end && SamePosition(*found, record) ? found : nullptr;
    }

    void TestClearance()
    {
        const GrassFieldRenderData field = MakeField();
        const GrassGenerateSettings settings;
        ViewClearanceParameters parameters = MakeDefaultViewClearanceParameters();
        parameters.bEnabled = true;
        parameters.Strength = 1.0f;
        parameters.bAffectGrass = true;
        parameters.CameraPosition = Math::Vector3(0.0f, 0.6f, 0.0f);
        parameters.TargetPosition = Math::Vector3(0.0f, 0.4f, 2.5f);
        CameraProxy camera;
        camera.PositionX = parameters.CameraPosition.x;
        camera.PositionY = parameters.CameraPosition.y;
        camera.PositionZ = parameters.CameraPosition.z;
        camera.ForwardX = 0.0f;
        camera.ForwardY = 0.0f;
        camera.ForwardZ = 1.0f;
        camera.UpX = 0.0f;
        camera.UpY = 1.0f;
        camera.UpZ = 0.0f;
        camera.FieldOfView = 90.0f;
        camera.NearPlane = 0.1f;
        camera.FarPlane = 200.0f;
        const auto view = CameraViewConstants::BuildForDevice(camera, 1.0f, nullptr);
        const auto planes = Math::MatrixUtils::ExtractClipSpaceFrustumPlanes(
            view.CullingViewProjectionMatrix, Math::ClipSpaceDepthRange::ZeroToOne);
        GrassTileSelectionInput selection;
        selection.CameraPosition = parameters.CameraPosition;
        selection.WindowSize = WindowSize;
        planes.CopyToShaderData(selection.FrustumPlanes);
        GrassTileSelector selector;
        Container::VariableArray<GrassTileInfo> tiles;
        const uint32_t tileCount = selector.Select(field, settings, selection, tiles);
        Check(tileCount > 0 && tileCount == tiles.size(), "選別したタイルがある");
        const GrassGenerateConstants baselineConstants = BuildGrassGenerateConstants(
            settings, parameters.CameraPosition, planes.Planes, Resolution, Resolution, field.ChunkSizeMeters);
        GrassGenerateConstants constants = baselineConstants;
        SetGrassViewClearance(constants, parameters);
        Records baseline;
        Records cleared;
        Records repeated;
        const auto baselineCounts = RunGrassGenerateReference(field, baselineConstants, tiles.data(), tileCount, &baseline);
        const auto clearedCounts = RunGrassGenerateReference(field, constants, tiles.data(), tileCount, &cleared);
        const auto repeatedCounts = RunGrassGenerateReference(field, constants, tiles.data(), tileCount, &repeated);
        Check(!baseline.empty() && !cleared.empty() && cleared.size() < baseline.size(), "切り抜きで記録が減り、残る草もある");
        Check(SameRecords(cleared, repeated) && SameCounts(clearedCounts, repeatedCounts), "同じ入力の記録と本数がビット一致する");
        uint64_t baselineTotal = 0;
        uint64_t clearedTotal = 0;
        for (uint32_t lod = 0; lod < kGrassLodCount; ++lod)
        {
            baselineTotal += baselineCounts.PerLod[lod];
            clearedTotal += clearedCounts.PerLod[lod];
            Check(baselineCounts.PerLod[lod] < settings.LodCapacity[lod], "記録の容量に余裕がある");
        }
        Check(baselineTotal == baseline.size() && clearedTotal == cleared.size(), "全 LOD の本数と記録数が一致する");

        ViewClearanceParameters disabled[] = {parameters, parameters, parameters};
        disabled[0].Strength = 0.0f;
        disabled[1].bEnabled = false;
        disabled[2].bAffectGrass = false;
        const char* disabledMessages[] = {
            "強さゼロでは記録と LOD ごとの本数がビット一致する",
            "無効では記録と LOD ごとの本数がビット一致する",
            "草への適用なしでは記録と LOD ごとの本数がビット一致する"
        };
        for (uint32_t index = 0; index < 3; ++index)
        {
            GrassGenerateConstants disabledConstants = baselineConstants;
            SetGrassViewClearance(disabledConstants, disabled[index]);
            Records disabledRecords;
            const auto disabledCounts = RunGrassGenerateReference(
                field, disabledConstants, tiles.data(), tileCount, &disabledRecords);
            Check(SameRecords(baseline, disabledRecords) && SameCounts(baselineCounts, disabledCounts), disabledMessages[index]);
        }
        if (baseline.empty() || cleared.empty())
        {
            return;
        }

        // 順序の再現性を確かめた後、位置で集合を照合する。
        std::sort(baseline.data(), baseline.data() + baseline.size(), PositionLess);
        std::sort(cleared.data(), cleared.data() + cleared.size(), PositionLess);
        for (size_t index = 1; index < baseline.size(); ++index)
        {
            Check(PositionLess(baseline[index - 1], baseline[index]), "切り抜き前の位置に重複がない");
        }
        for (size_t index = 0; index < cleared.size(); ++index)
        {
            Check(FindPosition(baseline, cleared[index]) != nullptr, "切り抜き後に新しい位置が増えない");
            if (index > 0)
            {
                Check(PositionLess(cleared[index - 1], cleared[index]), "切り抜き後の位置に重複がない");
            }
        }

        const double axisX = static_cast<double>(parameters.TargetPosition.x) - parameters.CameraPosition.x;
        const double axisY = static_cast<double>(parameters.TargetPosition.y) - parameters.CameraPosition.y;
        const double axisZ = static_cast<double>(parameters.TargetPosition.z) - parameters.CameraPosition.z;
        const double axisLengthSquared = axisX * axisX + axisY * axisY + axisZ * axisZ;
        const double tMargin = MarginMeters / std::sqrt(axisLengthSquared);
        uint32_t insideCount = 0;
        uint32_t outsideCount = 0;
        uint32_t behindCount = 0;
        uint32_t behindRemaining = 0;
        uint32_t nearCount = 0;
        uint32_t nearRemaining = 0;
        uint32_t featherCount = 0;
        uint32_t featherRemaining = 0;
        uint32_t shortenedCount = 0;
        for (const auto& original : baseline)
        {
            const auto originalBlade = UnpackGrassBlade(original.Packed,
                settings.HeightQuantMaxMeters, settings.WidthQuantMaxMeters);
            // 量子化した元の草丈の中心を使い、境界から 1 cm 離して分類する。
            const Math::Vector3 center(original.PositionX,
                original.PositionY + originalBlade.HeightMeters * 0.5f, original.PositionZ);
            const double dx = static_cast<double>(center.x) - parameters.CameraPosition.x;
            const double dy = static_cast<double>(center.y) - parameters.CameraPosition.y;
            const double dz = static_cast<double>(center.z) - parameters.CameraPosition.z;
            const double t = (dx * axisX + dy * axisY + dz * axisZ) / axisLengthSquared;
            const double segmentT = std::clamp(t, 0.0, 1.0);
            const double offsetX = dx - segmentT * axisX;
            const double offsetY = dy - segmentT * axisY;
            const double offsetZ = dz - segmentT * axisZ;
            const double distance = std::sqrt(offsetX * offsetX + offsetY * offsetY + offsetZ * offsetZ);
            const double cameraDistance = std::sqrt(dx * dx + dy * dy + dz * dz);
            const double radius = parameters.CameraRadius + (parameters.TargetRadius - parameters.CameraRadius) * segmentT;
            const double featherEnd = radius * (1.0 + parameters.FeatherRatio);
            const auto* remaining = FindPosition(cleared, original);
            const bool bSegmentInterior = t > tMargin && t < 1.0 - tMargin;
            if (bSegmentInterior && distance < radius - MarginMeters)
            {
                ++insideCount;
                Check(ComputeViewClearanceVisibility(parameters, center) == 0.0f, "内側の中心の可視度がゼロになる");
                Check(remaining == nullptr, "カプセルの内側には記録が残らない");
            }
            if (distance > featherEnd + MarginMeters && cameraDistance > parameters.NearFadeEnd + MarginMeters)
            {
                ++outsideCount;
                Check(remaining != nullptr, "外側の草がすべて同じ位置に残る");
                if (remaining != nullptr)
                {
                    const auto blade = UnpackGrassBlade(remaining->Packed,
                        settings.HeightQuantMaxMeters, settings.WidthQuantMaxMeters);
                    Check(blade.HeightMeters == originalBlade.HeightMeters, "外側に残った草丈は変わらない");
                }
            }
            if (t > 1.0 + tMargin && cameraDistance > parameters.NearFadeEnd + MarginMeters)
            {
                ++behindCount;
                behindRemaining += remaining != nullptr ? 1u : 0u;
                Check(remaining != nullptr, "注視点より奥の草が同じ位置に残る");
            }
            if (cameraDistance < parameters.NearFadeStart - MarginMeters)
            {
                ++nearCount;
                nearRemaining += remaining != nullptr ? 1u : 0u;
                Check(remaining == nullptr, "近接フェードの内側には記録が残らない");
            }
            if (bSegmentInterior && distance > radius + MarginMeters && distance < featherEnd - MarginMeters &&
                cameraDistance > parameters.NearFadeEnd + MarginMeters)
            {
                ++featherCount;
                if (remaining != nullptr)
                {
                    ++featherRemaining;
                    const auto blade = UnpackGrassBlade(remaining->Packed,
                        settings.HeightQuantMaxMeters, settings.WidthQuantMaxMeters);
                    Check(blade.HeightMeters <= originalBlade.HeightMeters, "縁に残った草丈は元の高さ以下になる");
                    shortenedCount += blade.HeightMeters < originalBlade.HeightMeters ? 1u : 0u;
                }
            }
        }
        Check(insideCount > 0, "切り抜き前にカプセルの内側の草がある");
        Check(outsideCount > 0, "切り抜き前にカプセルの外側の草がある");
        Check(behindCount > 0 && behindRemaining == behindCount, "注視点より奥の本数が一致する");
        Check(nearCount > 0 && nearRemaining == 0, "切り抜き前に近接の内側の草があり、切り抜き後はゼロになる");
        Check(featherCount > 0 && featherRemaining > 0 && shortenedCount > 0, "縁に草が残り、低背化を検査できる");
        std::printf("GrassClearanceReferenceTest: A=(0, 0.6, 0)、B=(0, 0.4, 2.5)、地面=0 m、草丈=1.0-1.2 m\n");
        std::printf("記録: 切り抜き前=%zu、切り抜き後=%zu、内側=%u、外側=%u、注視点より奥=%u（残存=%u）、近接内側=%u（残存=%u）、縁=%u（残存=%u、低背化=%u）\n",
            baseline.size(), cleared.size(), insideCount, outsideCount, behindCount, behindRemaining,
            nearCount, nearRemaining, featherCount, featherRemaining, shortenedCount);
    }
}

int main()
{
    TestClearance();
    if (GFailureCount != 0)
    {
        std::printf("GrassClearanceReferenceTest: 失敗 %d 件\n", GFailureCount);
        return 1;
    }
    std::printf("GrassClearanceReferenceTest: 合格\n");
    return 0;
}
