#include "SurfaceSampleTableBuilder.h"
#include "Asset/CookedSkinMeshV1.h"
#include "Resource/MaterialImportPolicy.h"
#include "TextureCooker.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace NorvesLib::Tools::AssetCook
{
    namespace C = Core::Container;
    namespace S = Core::Skeletal;
    namespace I = Core::AssetImport;
    namespace
    {
        constexpr uint64_t SurfaceSampleSeed()
        {
            uint64_t hash = 14695981039346656037ull;
            for (char value : "NorvesLib.SurfaceSamples.v1")
            {
                if (value != 0)
                {
                    hash = (hash ^ static_cast<uint8_t>(value)) * 1099511628211ull;
                }
            }
            return hash;
        }

        // SplitMix64 の上位 53 bit を使い、[0, 1) の一様乱数を返す。
        double Uniform(uint64_t& state)
        {
            uint64_t value = (state += 0x9e3779b97f4a7c15ull);
            value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
            value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
            value ^= value >> 31;
            return static_cast<double>(value >> 11) * 0x1.0p-53;
        }

        struct AlbedoTable
        {
            uint8_t Channels[3][256]{};
        };

        uint32_t TexelCoordinate(double uv, uint32_t size)
        {
            const double wrapped = uv - std::floor(uv);
            return std::min(static_cast<uint32_t>(std::floor(wrapped * size)), size - 1);
        }
    }

    bool BuildSurfaceSampleTable(const S::SkinMeshV1Data& mesh,
                                 C::Span<const SurfaceSampleAlbedoSource> slotAlbedo, uint32_t count,
                                 C::VariableArray<S::SurfaceSampleRecord>& out, C::AnsiString& error)
    {
        const auto fail = [&](const char* code)
        {
            error = code;
            return false;
        };
        if (count == 0 || count > S::MaximumSurfaceSampleCount)
        {
            return fail("surface_samples_count");
        }
        if (mesh.Indices.empty() || mesh.Indices.size() % 3 != 0)
        {
            return fail("surface_samples_triangles");
        }
        const size_t triangleCount = mesh.Indices.size() / 3;
        C::VariableArray<uint32_t> triangleSlots(triangleCount, UINT32_MAX);
        for (const auto& subMesh : mesh.SubMeshes)
        {
            if (subMesh.IndexStart % 3 != 0 || subMesh.IndexCount % 3 != 0 ||
                uint64_t(subMesh.IndexStart) + subMesh.IndexCount > mesh.Indices.size() ||
                subMesh.MaterialSlot >= slotAlbedo.size())
            {
                return fail("surface_samples_submesh");
            }
            const size_t end = (size_t(subMesh.IndexStart) + subMesh.IndexCount) / 3;
            for (size_t triangle = subMesh.IndexStart / 3; triangle < end; ++triangle)
            {
                if (triangleSlots[triangle] != UINT32_MAX)
                {
                    return fail("surface_samples_submesh");
                }
                triangleSlots[triangle] = subMesh.MaterialSlot;
            }
        }
        C::VariableArray<double> cumulative;
        cumulative.reserve(triangleCount);
        double totalArea = 0;
        for (size_t triangle = 0; triangle < triangleCount; ++triangle)
        {
            if (triangleSlots[triangle] == UINT32_MAX)
            {
                return fail("surface_samples_submesh");
            }
            for (size_t corner = 0; corner < 3; ++corner)
            {
                const uint32_t index = mesh.Indices[triangle * 3 + corner];
                if (index >= mesh.Vertices.size() || index > S::SurfaceSampleMaxVertexIndex)
                {
                    return fail("surface_samples_vertex");
                }
                const auto& vertex = mesh.Vertices[index];
                if (!std::isfinite(vertex.Position.X) || !std::isfinite(vertex.Position.Y) ||
                    !std::isfinite(vertex.Position.Z) || !std::isfinite(vertex.TexCoord.U) ||
                    !std::isfinite(vertex.TexCoord.V))
                {
                    return fail("surface_samples_vertex");
                }
            }
            const auto& p0 = mesh.Vertices[mesh.Indices[triangle * 3]].Position;
            const auto& p1 = mesh.Vertices[mesh.Indices[triangle * 3 + 1]].Position;
            const auto& p2 = mesh.Vertices[mesh.Indices[triangle * 3 + 2]].Position;
            const double ax = double(p1.X) - p0.X, ay = double(p1.Y) - p0.Y, az = double(p1.Z) - p0.Z;
            const double bx = double(p2.X) - p0.X, by = double(p2.Y) - p0.Y, bz = double(p2.Z) - p0.Z;
            const double cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
            totalArea += std::sqrt(cx * cx + cy * cy + cz * cz) * 0.5;
            cumulative.push_back(totalArea);
        }
        if (!std::isfinite(totalArea) || totalArea <= 0)
        {
            return fail("surface_samples_area");
        }
        C::VariableArray<AlbedoTable> tables(slotAlbedo.size());
        for (size_t slot = 0; slot < slotAlbedo.size(); ++slot)
        {
            const auto& source = slotAlbedo[slot];
            if (source.Image && (source.Image->Width == 0 || source.Image->Height == 0 ||
                source.Image->Pixels.size() % 4 != 0 ||
                uint64_t(source.Image->Width) * source.Image->Height != source.Image->Pixels.size() / 4))
            {
                return fail("surface_samples_image");
            }
            for (size_t channel = 0; channel < 3; ++channel)
            {
                for (unsigned value = 0; value < 256; ++value)
                {
                    auto& baked = tables[slot].Channels[channel][value];
                    baked = static_cast<uint8_t>(value);
                    if (source.Factor[channel] != 1.0f &&
                        I::BakeBaseColorByte(baked, source.Factor[channel], baked) != I::MaterialPolicyStatus::Success)
                    {
                        return fail("surface_samples_albedo");
                    }
                }
            }
        }
        C::VariableArray<S::SurfaceSampleRecord> samples;
        samples.reserve(count);
        uint64_t state = SurfaceSampleSeed();
        for (uint32_t i = 0; i < count; ++i)
        {
            const double u = (double(i) + Uniform(state)) / count;
            // 丸めで末端に届いた場合も、最後の正の面積の三角形を選ぶ。
            const double target = std::min(u * totalArea, std::nextafter(totalArea, 0.0));
            const size_t triangle = size_t(std::upper_bound(cumulative.begin(), cumulative.end(), target) - cumulative.begin());
            const uint32_t v0 = mesh.Indices[triangle * 3];
            const uint32_t v1 = mesh.Indices[triangle * 3 + 1];
            const uint32_t v2 = mesh.Indices[triangle * 3 + 2];
            const double r1 = Uniform(state), r2 = Uniform(state);
            const double s = std::sqrt(r1);
            const uint32_t b1q = static_cast<uint32_t>(std::lround(s * (1 - r2) * S::SurfaceSampleBarycentricOne));
            uint32_t b2q = static_cast<uint32_t>(std::lround(s * r2 * S::SurfaceSampleBarycentricOne));
            if (b1q + b2q > S::SurfaceSampleBarycentricOne)
            {
                b2q = S::SurfaceSampleBarycentricOne - b1q;
            }
            const double b0 = double(S::SurfaceSampleBarycentricOne - b1q - b2q) / S::SurfaceSampleBarycentricOne;
            const double b1 = double(b1q) / S::SurfaceSampleBarycentricOne;
            const double b2 = double(b2q) / S::SurfaceSampleBarycentricOne;
            const uint32_t slot = triangleSlots[triangle];
            const auto* image = slotAlbedo[slot].Image;
            const uint8_t white[4] = {255, 255, 255, 255};
            const uint8_t* texel = white;
            if (image)
            {
                const auto& uv0 = mesh.Vertices[v0].TexCoord;
                const auto& uv1 = mesh.Vertices[v1].TexCoord;
                const auto& uv2 = mesh.Vertices[v2].TexCoord;
                const double texU = b0 * uv0.U + b1 * uv1.U + b2 * uv2.U;
                const double texV = b0 * uv0.V + b1 * uv1.V + b2 * uv2.V;
                const uint32_t x = TexelCoordinate(texU, image->Width);
                const uint32_t y = TexelCoordinate(texV, image->Height);
                texel = image->Pixels.data() + (size_t(y) * image->Width + x) * 4;
            }
            uint32_t rgba8 = uint32_t(texel[3]) << 24;
            for (size_t channel = 0; channel < 3; ++channel)
            {
                rgba8 |= uint32_t(tables[slot].Channels[channel][texel[channel]]) << (channel * 8);
            }
            samples.push_back(S::PackSurfaceSampleRecord(v0, v1, v2, uint16_t(b1q), uint16_t(b2q), rgba8));
        }
        out = std::move(samples);
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
