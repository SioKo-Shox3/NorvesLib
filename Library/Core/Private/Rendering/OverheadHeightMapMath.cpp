// 雨の遮蔽マップの格子、正射影、描き直しと遮蔽物の形を CPU で計算する。
#include "OverheadHeightMapMath.h"

#include "Math/MatrixUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;

        struct OccluderCandidate
        {
            SkyOccluderProxy Proxy;
            double DistanceSquared;
            size_t InputOrder;
        };

        struct DomeVertex
        {
            double X;
            double Y;
            double Z;
        };

        DomeVertex MakeDomeVertex(uint32_t ring, uint32_t azimuth)
        {
            const double theta = ring * (kPi / 2.0) / 4.0;
            const double phi = (azimuth % 8u) * (2.0 * kPi) / 8.0;
            return {std::sin(theta) * std::cos(phi),
                ring == 4u ? 0.0 : std::cos(theta), std::sin(theta) * std::sin(phi)};
        }

        void AccumulateTriangleHeight(
            const DomeVertex& a, const DomeVertex& b, const DomeVertex& c,
            double x, double z, bool& bFound, double& top)
        {
            const double denominator = (b.Z - c.Z) * (a.X - c.X) +
                (c.X - b.X) * (a.Z - c.Z);
            if (denominator == 0.0)
            {
                return;
            }
            const double wa = ((b.Z - c.Z) * (x - c.X) +
                (c.X - b.X) * (z - c.Z)) / denominator;
            const double wb = ((c.Z - a.Z) * (x - c.X) +
                (a.X - c.X) * (z - c.Z)) / denominator;
            const double wc = 1.0 - wa - wb;
            // 共有辺での丸め誤差だけを許容し、三角形間の隙間を防ぐ。
            constexpr double tolerance = 1.0e-7;
            if (wa >= -tolerance && wb >= -tolerance && wc >= -tolerance)
            {
                const double height = wa * a.Y + wb * b.Y + wc * c.Y;
                if (!bFound || height > top)
                {
                    top = height;
                }
                bFound = true;
            }
        }
    } // namespace

    float OverheadHeightMapWindow::GetOriginX() const
    {
        return OriginTexelX * TexelMeters;
    }

    float OverheadHeightMapWindow::GetOriginZ() const
    {
        return OriginTexelZ * TexelMeters;
    }

    OverheadHeightMapWindow ComputeOverheadHeightMapWindow(
        const OverheadHeightMapSettings& settings, float cameraX, float cameraY, float cameraZ)
    {
        OverheadHeightMapWindow window;
        window.Resolution = settings.Resolution;
        window.CoverageMeters = settings.CoverageMeters;
        window.TexelMeters = settings.CoverageMeters / settings.Resolution;
        const double texel = window.TexelMeters;
        const double halfResolution = settings.Resolution / 2u;
        window.OriginTexelX = static_cast<int32_t>(std::floor(cameraX / texel) - halfResolution);
        window.OriginTexelZ = static_cast<int32_t>(std::floor(cameraZ / texel) - halfResolution);
        window.TopY = static_cast<float>(std::ceil(
            (static_cast<double>(cameraY) + settings.TopAboveCameraMeters) /
            settings.TopSnapMeters) * settings.TopSnapMeters);
        window.RangeMeters = settings.RangeMeters;
        return window;
    }

    float OverheadDepthFromHeight(const OverheadHeightMapWindow& window, float y)
    {
        return (window.TopY - y) / window.RangeMeters;
    }

    float OverheadRoofYFromDepth(const OverheadHeightMapWindow& window, float depth)
    {
        return window.TopY - depth * window.RangeMeters;
    }

    bool WorldToOverheadTexel(
        const OverheadHeightMapWindow& window, float x, float z, uint32_t& i, uint32_t& j)
    {
        const double u = (static_cast<double>(x) - window.GetOriginX()) / window.CoverageMeters;
        const double v = (static_cast<double>(z) - window.GetOriginZ()) / window.CoverageMeters;
        if (!(u >= 0.0 && u < 1.0 && v >= 0.0 && v < 1.0) || window.Resolution == 0u)
        {
            return false;
        }
        i = static_cast<uint32_t>(std::floor(u * window.Resolution));
        j = static_cast<uint32_t>(std::floor(v * window.Resolution));
        return true;
    }

    void OverheadTexelCenterToWorld(
        const OverheadHeightMapWindow& window, uint32_t i, uint32_t j, float& x, float& z)
    {
        x = window.GetOriginX() + (static_cast<float>(i) + 0.5f) * window.TexelMeters;
        z = window.GetOriginZ() + (static_cast<float>(j) + 0.5f) * window.TexelMeters;
    }

    bool SampleOverheadRoofY(
        const OverheadHeightMapWindow& window, Container::Span<const float> depths,
        float x, float z, float& outRoofY)
    {
        uint32_t i = 0u;
        uint32_t j = 0u;
        if (depths.size() < static_cast<size_t>(window.Resolution) * window.Resolution ||
            !WorldToOverheadTexel(window, x, z, i, j))
        {
            return false;
        }
        const float depth = depths[static_cast<size_t>(j) * window.Resolution + i];
        if (!std::isfinite(depth) || depth >= 1.0f)
        {
            return false;
        }
        outRoofY = OverheadRoofYFromDepth(window, depth);
        return true;
    }

    Math::Matrix4x4 BuildOverheadHeightMapViewProjection(const OverheadHeightMapWindow& window)
    {
        const float centerX = window.GetOriginX() + window.CoverageMeters * 0.5f;
        const float centerZ = window.GetOriginZ() + window.CoverageMeters * 0.5f;
        const Math::Matrix4x4 view = Math::MatrixUtils::CreateLookAt(
            Math::Vector3(centerX, window.TopY, centerZ),
            Math::Vector3(centerX, window.TopY - 1.0f, centerZ), Math::Vector3(0.0f, 0.0f, -1.0f));
        const Math::Matrix4x4 projection = Math::MatrixUtils::CreateOrthographic(
            window.CoverageMeters, window.CoverageMeters, 0.0f, window.RangeMeters);
        // ビューの上は -Z、奥行きは上向きなので反転し、画像の行を +Z、深度を下向きにする。
        return projection * Math::MatrixUtils::CreateScale(1.0f, -1.0f, -1.0f) * view;
    }

    OverheadHeightMapGpuParams MakeOverheadHeightMapGpuParams(
        const OverheadHeightMapWindow& window, uint32_t boxCount, uint32_t ellipsoidCount)
    {
        OverheadHeightMapGpuParams params;
        Math::MatrixUtils::TransposeToShaderData(
            BuildOverheadHeightMapViewProjection(window), params.ViewProjection);
        params.Window[0] = window.GetOriginX();
        params.Window[1] = window.GetOriginZ();
        params.Window[2] = window.CoverageMeters;
        params.Window[3] = window.TexelMeters;
        params.Depth[0] = window.TopY;
        params.Depth[1] = window.RangeMeters;
        params.Counts[0] = boxCount;
        params.Counts[1] = ellipsoidCount;
        return params;
    }

    OverheadHeightMapRedrawDecision DecideOverheadHeightMapRedraw(
        const OverheadHeightMapSettings& settings, const OverheadHeightMapRedrawState& state,
        const OverheadHeightMapWindow& candidate, double nowSeconds, bool bOccludersChanged)
    {
        if (!state.bHasContent)
        {
            return {true, candidate};
        }
        const int64_t deltaX = static_cast<int64_t>(candidate.OriginTexelX) - state.Window.OriginTexelX;
        const int64_t deltaZ = static_cast<int64_t>(candidate.OriginTexelZ) - state.Window.OriginTexelZ;
        const bool bMoved = std::abs(deltaX) >= settings.RedrawThresholdTexels ||
            std::abs(deltaZ) >= settings.RedrawThresholdTexels || candidate.TopY != state.Window.TopY;
        if ((!bMoved && !bOccludersChanged) ||
            (nowSeconds >= state.LastRedrawSeconds &&
                nowSeconds - state.LastRedrawSeconds < settings.MinRedrawIntervalSeconds))
        {
            return {false, state.Window};
        }
        return {true, bMoved ? candidate : state.Window};
    }

    bool MakeOverheadBoxFromLocalBounds(
        const float localMin[3], const float localMax[3],
        const Math::Matrix4x4& worldRowVector, SkyOccluderProxy& out)
    {
        for (uint32_t axis = 0u; axis < 3u; ++axis)
        {
            if (!std::isfinite(localMin[axis]) || !std::isfinite(localMax[axis]) ||
                localMax[axis] <= localMin[axis])
            {
                return false;
            }
        }
        Math::Vector3 corners[8];
        for (uint32_t index = 0u; index < 8u; ++index)
        {
            corners[index] = Math::MatrixUtils::TransformPointRowVector(worldRowVector, Math::Vector3(
                (index & 1u) ? localMax[0] : localMin[0],
                (index & 2u) ? localMax[1] : localMin[1],
                (index & 4u) ? localMax[2] : localMin[2]));
            if (!std::isfinite(corners[index].x) || !std::isfinite(corners[index].y) ||
                !std::isfinite(corners[index].z))
            {
                return false;
            }
        }
        const double axisX = static_cast<double>(corners[1].x) - corners[0].x;
        const double axisZ = static_cast<double>(corners[1].z) - corners[0].z;
        SkyOccluderProxy result;
        result.Yaw = axisX != 0.0 || axisZ != 0.0 ? static_cast<float>(std::atan2(-axisZ, axisX)) : 0.0f;
        const double cosine = std::cos(static_cast<double>(result.Yaw));
        const double sine = std::sin(static_cast<double>(result.Yaw));
        double minimum[3] = {std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
        double maximum[3] = {std::numeric_limits<double>::lowest(),
            std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest()};
        for (const Math::Vector3& corner : corners)
        {
            const double local[3] = {cosine * corner.x - sine * corner.z, corner.y,
                sine * corner.x + cosine * corner.z};
            for (uint32_t axis = 0u; axis < 3u; ++axis)
            {
                minimum[axis] = std::min(minimum[axis], local[axis]);
                maximum[axis] = std::max(maximum[axis], local[axis]);
            }
        }
        const double centerX = (minimum[0] + maximum[0]) * 0.5;
        const double centerZ = (minimum[2] + maximum[2]) * 0.5;
        result.CenterX = static_cast<float>(cosine * centerX + sine * centerZ);
        result.CenterY = static_cast<float>((minimum[1] + maximum[1]) * 0.5);
        result.CenterZ = static_cast<float>(-sine * centerX + cosine * centerZ);
        result.HalfExtentX = static_cast<float>((maximum[0] - minimum[0]) * 0.5);
        result.HalfExtentY = static_cast<float>((maximum[1] - minimum[1]) * 0.5);
        result.HalfExtentZ = static_cast<float>((maximum[2] - minimum[2]) * 0.5);
        SetSkyOccluderShape(result, SkyOccluderShape::Box);
        SetSkyOccluderBlocksRain(result, true);
        if (!IsValidSkyOccluderProxy(result))
        {
            return false;
        }
        out = result;
        return true;
    }

    bool MakeOverheadEllipsoidFromSphere(
        float cx, float cy, float cz, float radius, SkyOccluderProxy& out)
    {
        SkyOccluderProxy result;
        result.CenterX = cx;
        result.CenterY = cy;
        result.CenterZ = cz;
        result.HalfExtentX = radius;
        result.HalfExtentY = radius;
        result.HalfExtentZ = radius;
        SetSkyOccluderShape(result, SkyOccluderShape::Ellipsoid);
        SetSkyOccluderBlocksRain(result, true);
        if (!IsValidSkyOccluderProxy(result))
        {
            return false;
        }
        out = result;
        return true;
    }

    uint32_t CollectOverheadOccluders(
        const OverheadHeightMapSettings& settings, const OverheadHeightMapWindow& window,
        Container::Span<const SkyOccluderProxy> skyOccluders,
        Container::Span<const SkyOccluderProxy> derived,
        Container::VariableArray<SkyOccluderProxy>& out, bool& outTruncated)
    {
        Container::VariableArray<OccluderCandidate> candidates;
        const double minimumX = window.GetOriginX();
        const double minimumZ = window.GetOriginZ();
        const double maximumX = minimumX + window.CoverageMeters;
        const double maximumZ = minimumZ + window.CoverageMeters;
        const double centerX = (minimumX + maximumX) * 0.5;
        const double centerZ = (minimumZ + maximumZ) * 0.5;
        const auto append = [&](const SkyOccluderProxy& proxy)
        {
            const double dx = proxy.CenterX - std::clamp(static_cast<double>(proxy.CenterX), minimumX, maximumX);
            const double dz = proxy.CenterZ - std::clamp(static_cast<double>(proxy.CenterZ), minimumZ, maximumZ);
            const double radiusSquared = static_cast<double>(proxy.HalfExtentX) * proxy.HalfExtentX +
                static_cast<double>(proxy.HalfExtentZ) * proxy.HalfExtentZ;
            if (dx * dx + dz * dz <= radiusSquared)
            {
                const double centerDx = proxy.CenterX - centerX;
                const double centerDz = proxy.CenterZ - centerZ;
                candidates.push_back({proxy, centerDx * centerDx + centerDz * centerDz, candidates.size()});
            }
        };
        for (const SkyOccluderProxy& proxy : skyOccluders)
        {
            if (IsValidSkyOccluderProxy(proxy) && DoesSkyOccluderBlockRain(proxy))
            {
                append(SanitizeSkyOccluderProxy(proxy));
            }
        }
        for (const SkyOccluderProxy& proxy : derived)
        {
            append(proxy);
        }
        outTruncated = candidates.size() > settings.MaxOccluders;
        if (outTruncated)
        {
            std::sort(candidates.begin(), candidates.end(), [](const OccluderCandidate& lhs, const OccluderCandidate& rhs)
            {
                return lhs.DistanceSquared < rhs.DistanceSquared ||
                    (lhs.DistanceSquared == rhs.DistanceSquared && lhs.InputOrder < rhs.InputOrder);
            });
            candidates.resize(settings.MaxOccluders);
            std::sort(candidates.begin(), candidates.end(), [](const OccluderCandidate& lhs, const OccluderCandidate& rhs)
            {
                return lhs.InputOrder < rhs.InputOrder;
            });
        }
        out.clear();
        out.reserve(candidates.size());
        for (const OccluderCandidate& candidate : candidates)
        {
            if (GetSkyOccluderShape(candidate.Proxy) == SkyOccluderShape::Box)
            {
                out.push_back(candidate.Proxy);
            }
        }
        const uint32_t boxCount = static_cast<uint32_t>(out.size());
        for (const OccluderCandidate& candidate : candidates)
        {
            if (GetSkyOccluderShape(candidate.Proxy) == SkyOccluderShape::Ellipsoid)
            {
                out.push_back(candidate.Proxy);
            }
        }
        return boxCount;
    }

    uint64_t HashOverheadOccluders(Container::Span<const SkyOccluderProxy> occluders)
    {
        uint64_t hash = 14695981039346656037ull;
        const auto* bytes = reinterpret_cast<const unsigned char*>(occluders.data());
        for (size_t index = 0u; index < occluders.size_bytes(); ++index)
        {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    bool EvaluateOverheadRoofTopY(
        const SkyOccluderProxy& proxy, float x, float z, float& outTopY)
    {
        if (!IsValidSkyOccluderProxy(proxy) || !std::isfinite(x) || !std::isfinite(z))
        {
            return false;
        }
        const double cosine = std::cos(static_cast<double>(proxy.Yaw));
        const double sine = std::sin(static_cast<double>(proxy.Yaw));
        const double dx = static_cast<double>(x) - proxy.CenterX;
        const double dz = static_cast<double>(z) - proxy.CenterZ;
        const double lx = cosine * dx - sine * dz;
        const double lz = sine * dx + cosine * dz;
        if (GetSkyOccluderShape(proxy) == SkyOccluderShape::Box)
        {
            if (std::abs(lx) > proxy.HalfExtentX || std::abs(lz) > proxy.HalfExtentZ)
            {
                return false;
            }
            outTopY = proxy.CenterY + proxy.HalfExtentY;
            return true;
        }
        const double normalizedX = lx / proxy.HalfExtentX;
        const double normalizedZ = lz / proxy.HalfExtentZ;
        bool bFound = false;
        double top = 0.0;
        const DomeVertex apex = {0.0, 1.0, 0.0};
        for (uint32_t azimuth = 0u; azimuth < 8u; ++azimuth)
        {
            AccumulateTriangleHeight(apex, MakeDomeVertex(1u, azimuth), MakeDomeVertex(1u, azimuth + 1u),
                normalizedX, normalizedZ, bFound, top);
            for (uint32_t ring = 1u; ring < 4u; ++ring)
            {
                const DomeVertex a = MakeDomeVertex(ring, azimuth);
                const DomeVertex b = MakeDomeVertex(ring + 1u, azimuth);
                const DomeVertex c = MakeDomeVertex(ring + 1u, azimuth + 1u);
                const DomeVertex d = MakeDomeVertex(ring, azimuth + 1u);
                AccumulateTriangleHeight(a, b, c, normalizedX, normalizedZ, bFound, top);
                AccumulateTriangleHeight(a, c, d, normalizedX, normalizedZ, bFound, top);
            }
        }
        if (!bFound)
        {
            return false;
        }
        outTopY = static_cast<float>(proxy.CenterY + proxy.HalfExtentY * top);
        return true;
    }
} // namespace NorvesLib::Core::Rendering
