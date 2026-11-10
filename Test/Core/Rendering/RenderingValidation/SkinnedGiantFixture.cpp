// 高密度の管状スキンメッシュと等間隔の関節鎖を CPU 上で生成する。
#include "RenderingValidation/SkinnedGiantFixture.h"

#include "Container/StringBuilder.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace NorvesLib::Test::RenderingValidation
{
    bool BuildSkinnedGiantFixture(const SkinnedGiantFixtureDesc& desc, SkinnedGiantFixtureData& out)
    {
        if (desc.TargetTriangleCount == 0u || desc.JointCount < 2u ||
            static_cast<uint64_t>(desc.JointCount) > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()) + 2u ||
            !std::isfinite(desc.Length) || desc.Length <= 0.0f ||
            !std::isfinite(desc.Radius) || desc.Radius <= 0.0f)
        {
            return false;
        }

        // 各格子の縦横が近い寸法となる分割数を選び、目標三角形数へ丸める。
        constexpr double pi = 3.14159265358979323846;
        const double targetQuads = static_cast<double>(desc.TargetTriangleCount) * 0.5;
        const double circumference = 2.0 * pi * static_cast<double>(desc.Radius);
        const double idealSegments = std::sqrt(targetQuads * circumference / static_cast<double>(desc.Length));
        const uint32_t maxCircumferenceSegments = std::max(8u, desc.TargetTriangleCount / 2u);
        const uint32_t circumferenceSegments = static_cast<uint32_t>(std::round(std::clamp(
            idealSegments, 8.0, static_cast<double>(maxCircumferenceSegments))));
        const uint32_t lengthSegments = static_cast<uint32_t>(
            std::max(1.0, std::round(targetQuads / static_cast<double>(circumferenceSegments))));

        // 継ぎ目の両側に UV が異なる頂点を持たせる。乗算は範囲確認まで 64 bit で行う。
        const uint64_t stride = static_cast<uint64_t>(circumferenceSegments) + 1u;
        const uint64_t vertexCount = (static_cast<uint64_t>(lengthSegments) + 1u) * stride;
        const uint64_t indexCount = static_cast<uint64_t>(lengthSegments) * circumferenceSegments * 6u;
        if (vertexCount > std::numeric_limits<uint32_t>::max() ||
            indexCount > std::numeric_limits<uint32_t>::max())
        {
            return false;
        }

        try
        {
            SkinnedGiantFixtureData result;
            if (vertexCount > result.Vertices.max_size() || indexCount > result.Indices.max_size() ||
                desc.JointCount > result.Joints.max_size())
            {
                return false;
            }
            result.Vertices.resize(static_cast<size_t>(vertexCount));
            result.Indices.resize(static_cast<size_t>(indexCount));
            result.Joints.reserve(desc.JointCount);

            // 配列は行優先で、平行移動は行 3 の Y 要素に置く。
            float previousJointY = 0.0f;
            for (uint32_t jointIndex = 0u; jointIndex < desc.JointCount; ++jointIndex)
            {
                const float jointY = static_cast<float>(static_cast<double>(desc.Length) * jointIndex /
                                                        static_cast<double>(desc.JointCount - 1u));
                if (jointIndex > 0u && jointY <= previousJointY)
                {
                    return false;
                }
                previousJointY = jointY;

                Core::Skeletal::SkeletalJoint joint;
                Core::Container::StringBuilder name;
                name.AppendFormat(TEXT("Joint%03u"), jointIndex);
                joint.Name = name.ToString();
                joint.ParentIndex = jointIndex == 0u ? -1 : static_cast<int32_t>(jointIndex - 1u);
                joint.InverseBindMatrix = {1.0f, 0.0f, 0.0f, 0.0f,
                                           0.0f, 1.0f, 0.0f, 0.0f,
                                           0.0f, 0.0f, 1.0f, 0.0f,
                                           0.0f, -jointY, 0.0f, 1.0f};
                result.Joints.push_back(std::move(joint));
            }

            // 最初の輪の三角関数を各輪で共有する。浮動小数点で隣接頂点が重なる寸法は拒否する。
            Core::Skeletal::SkeletalVertex* const vertices = result.Vertices.data();
            result.BindBounds = Math::AABB::CreateInvalid();
            for (uint32_t column = 0u; column < circumferenceSegments; ++column)
            {
                const double fraction = static_cast<double>(column) / circumferenceSegments;
                const double angle = 2.0 * pi * fraction;
                auto& vertex = vertices[column];
                vertex.Normal.X = static_cast<float>(std::cos(angle));
                vertex.Normal.Z = static_cast<float>(std::sin(angle));
                vertex.Position.X = desc.Radius * vertex.Normal.X;
                vertex.Position.Z = desc.Radius * vertex.Normal.Z;
                vertex.TexCoord.U = static_cast<float>(fraction);
                if (column > 0u && vertex.Position.X == vertices[column - 1u].Position.X &&
                    vertex.Position.Z == vertices[column - 1u].Position.Z)
                {
                    return false;
                }
                result.BindBounds.Expand(Math::Vector3(vertex.Position.X, 0.0f, vertex.Position.Z));
            }
            const auto& last = vertices[circumferenceSegments - 1u];
            if (last.Position.X == vertices[0].Position.X && last.Position.Z == vertices[0].Position.Z)
            {
                return false;
            }
            vertices[circumferenceSegments] = vertices[0];
            vertices[circumferenceSegments].TexCoord.U = 1.0f;
            result.BindBounds.Max.y = desc.Length;

            float previousRingY = 0.0f;
            for (uint32_t ring = 0u; ring <= lengthSegments; ++ring)
            {
                const double fraction = static_cast<double>(ring) / lengthSegments;
                const float y = static_cast<float>(static_cast<double>(desc.Length) * fraction);
                if (ring > 0u && y <= previousRingY)
                {
                    return false;
                }
                previousRingY = y;

                // 隣接する 2 関節の重みを smoothstep で補間し、合計を 1 にする。
                const double jointPosition = fraction * static_cast<double>(desc.JointCount - 1u);
                const uint32_t firstJoint = std::min(static_cast<uint32_t>(jointPosition), desc.JointCount - 2u);
                const double blend = jointPosition - static_cast<double>(firstJoint);
                const float secondWeight = static_cast<float>(blend * blend * (3.0 - 2.0 * blend));
                const size_t row = static_cast<size_t>(ring) * static_cast<size_t>(stride);
                for (uint32_t column = 0u; column <= circumferenceSegments; ++column)
                {
                    auto vertex = vertices[column];
                    vertex.Position.Y = y;
                    vertex.TexCoord.V = static_cast<float>(fraction);
                    vertex.JointIndices = {firstJoint, firstJoint + 1u, 0u, 0u};
                    vertex.JointWeights = {1.0f - secondWeight, secondWeight, 0.0f, 0.0f};
                    vertices[row + column] = vertex;
                }
            }

            // 既存の円柱と同じ外向きの頂点順で、各格子に 2 枚の三角形を張る。
            uint32_t* index = result.Indices.data();
            for (uint32_t ring = 0u; ring < lengthSegments; ++ring)
            {
                const uint32_t row = static_cast<uint32_t>(static_cast<uint64_t>(ring) * stride);
                const uint32_t nextRow = static_cast<uint32_t>(static_cast<uint64_t>(ring + 1u) * stride);
                for (uint32_t column = 0u; column < circumferenceSegments; ++column)
                {
                    *index++ = row + column;
                    *index++ = nextRow + column;
                    *index++ = row + column + 1u;
                    *index++ = row + column + 1u;
                    *index++ = nextRow + column;
                    *index++ = nextRow + column + 1u;
                }
            }

            out = std::move(result);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        catch (const std::length_error&)
        {
            return false;
        }
    }
} // namespace NorvesLib::Test::RenderingValidation
