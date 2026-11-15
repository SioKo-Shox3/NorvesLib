#include "Resource/GltfNodeTransform.h"
#include "Container/VariableArray.h"
#include "Text/JsonDocument.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Gltf
{
    namespace
    {
        constexpr double Identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        static_assert(sizeof(float) == 4);

        bool ReadIndex(const JsonValue& value, uint32_t& out) noexcept
        {
            if (!value.IsIntegerLiteral())
            {
                return false;
            }
            const double number = value.AsNumber();
            if (!std::isfinite(number) || number < 0.0 || number > static_cast<double>(UINT32_MAX))
            {
                return false;
            }
            out = static_cast<uint32_t>(number);
            return true;
        }

        NodeTransformResult ReadArray(const JsonValue& value, size_t count, double* out) noexcept
        {
            if (!value.IsArray() || value.GetArraySize() != count)
            {
                return NodeTransformResult::InvalidNode;
            }
            for (size_t index = 0; index < count; ++index)
            {
                const JsonValue element = value.GetArrayElement(index);
                if (!element.IsNumber())
                {
                    return NodeTransformResult::InvalidNode;
                }
                out[index] = element.AsNumber();
                if (!std::isfinite(out[index]))
                {
                    return NodeTransformResult::NonFiniteTransform;
                }
            }
            return NodeTransformResult::Success;
        }

        NodeTransformResult ReadLocalTransform(const JsonValue& node, double (&matrix)[16]) noexcept
        {
            if (node.HasMember("matrix"))
            {
                if (node.HasMember("translation") || node.HasMember("rotation") || node.HasMember("scale"))
                {
                    return NodeTransformResult::InvalidNode;
                }
                return ReadArray(node.FindMember("matrix"), 16, matrix);
            }

            double translation[3] = {0.0, 0.0, 0.0};
            double rotation[4] = {0.0, 0.0, 0.0, 1.0};
            double scale[3] = {1.0, 1.0, 1.0};
            const char* names[3] = {"translation", "rotation", "scale"};
            double* values[3] = {translation, rotation, scale};
            const size_t counts[3] = {3, 4, 3};
            for (size_t index = 0; index < 3; ++index)
            {
                if (node.HasMember(names[index]))
                {
                    const auto result = ReadArray(node.FindMember(names[index]), counts[index], values[index]);
                    if (result != NodeTransformResult::Success)
                    {
                        return result;
                    }
                }
            }

            // 最大成分で割ってから長さを求め、巨大値や非正規数でも正規化する。
            double maximum = 0.0;
            for (double value : rotation)
            {
                if (std::abs(value) > maximum)
                {
                    maximum = std::abs(value);
                }
            }
            if (maximum == 0.0)
            {
                return NodeTransformResult::InvalidNode;
            }
            double lengthSquared = 0.0;
            for (double& value : rotation)
            {
                value /= maximum;
                lengthSquared += value * value;
            }
            const double length = std::sqrt(lengthSquared);
            for (double& value : rotation)
            {
                value /= length;
            }
            const double x = rotation[0], y = rotation[1], z = rotation[2], w = rotation[3];
            std::memcpy(matrix, Identity, sizeof(Identity));
            matrix[0] = scale[0] * (1.0 - 2.0 * (y * y + z * z));
            matrix[1] = scale[0] * (2.0 * (x * y + z * w));
            matrix[2] = scale[0] * (2.0 * (x * z - y * w));
            matrix[4] = scale[1] * (2.0 * (x * y - z * w));
            matrix[5] = scale[1] * (1.0 - 2.0 * (x * x + z * z));
            matrix[6] = scale[1] * (2.0 * (y * z + x * w));
            matrix[8] = scale[2] * (2.0 * (x * z + y * w));
            matrix[9] = scale[2] * (2.0 * (y * z - x * w));
            matrix[10] = scale[2] * (1.0 - 2.0 * (x * x + y * y));
            matrix[12] = translation[0];
            matrix[13] = translation[1];
            matrix[14] = translation[2];
            for (double value : matrix)
            {
                if (!std::isfinite(value))
                {
                    return NodeTransformResult::NonFiniteTransform;
                }
            }
            return NodeTransformResult::Success;
        }

        bool Multiply(const double (&left)[16], const double (&right)[16], double (&out)[16]) noexcept
        {
            for (size_t column = 0; column < 4; ++column)
            {
                for (size_t row = 0; row < 4; ++row)
                {
                    double sum = 0.0;
                    for (size_t inner = 0; inner < 4; ++inner)
                    {
                        const double product = left[inner * 4 + row] * right[column * 4 + inner];
                        sum += product;
                        if (!std::isfinite(product) || !std::isfinite(sum))
                        {
                            return false;
                        }
                    }
                    out[column * 4 + row] = sum;
                }
            }
            return true;
        }

        // 余因子行列を行列式で割り、法線用の逆転置を求める。
        NodeTransformResult InvertLinear(const double (&matrix)[16], double (&normal)[3][3],
            double& determinant) noexcept
        {
            const double a = matrix[0], b = matrix[4], c = matrix[8];
            const double d = matrix[1], e = matrix[5], f = matrix[9];
            const double g = matrix[2], h = matrix[6], i = matrix[10];
            const double cofactors[3][3] = {
                {e * i - f * h, f * g - d * i, d * h - e * g},
                {c * h - b * i, a * i - c * g, b * g - a * h},
                {b * f - c * e, c * d - a * f, a * e - b * d}
            };
            determinant = a * cofactors[0][0] + b * cofactors[0][1] + c * cofactors[0][2];
            if (!std::isfinite(determinant))
            {
                return NodeTransformResult::NonFiniteTransform;
            }
            if (determinant == 0.0)
            {
                return NodeTransformResult::SingularTransform;
            }
            for (size_t row = 0; row < 3; ++row)
            {
                for (size_t column = 0; column < 3; ++column)
                {
                    normal[row][column] = cofactors[row][column] / determinant;
                    if (!std::isfinite(normal[row][column]))
                    {
                        return NodeTransformResult::SingularTransform;
                    }
                }
            }
            return NodeTransformResult::Success;
        }

        bool HasField(size_t offset, size_t size, size_t stride) noexcept
        {
            return offset <= stride && size <= stride - offset;
        }

        bool Overlaps(size_t left, size_t leftSize, size_t right, size_t rightSize) noexcept
        {
            return left <= right ? right - left < leftSize : left - right < rightSize;
        }

        bool AsFloat(double value, float& out) noexcept
        {
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
            {
                return false;
            }
            out = static_cast<float>(value);
            return std::isfinite(out);
        }

        bool EvaluateVertex(const uint8_t* vertex, const AssetImport::ImportVertexLayout& layout,
            const double (&matrix)[16], const double (&normalMatrix)[3][3], bool bKeepNormal,
            float (&position)[3], float (&normal)[3]) noexcept
        {
            float sourcePosition[3], sourceNormal[3];
            std::memcpy(sourcePosition, vertex + layout.PositionOffset, sizeof(sourcePosition));
            std::memcpy(sourceNormal, vertex + layout.NormalOffset, sizeof(sourceNormal));
            for (size_t row = 0; row < 3; ++row)
            {
                const double value = matrix[row] * sourcePosition[0] + matrix[4 + row] * sourcePosition[1] +
                    matrix[8 + row] * sourcePosition[2] + matrix[12 + row];
                if (!AsFloat(value, position[row]) || !std::isfinite(sourceNormal[row]))
                {
                    return false;
                }
            }
            if (bKeepNormal)
            {
                return true;
            }

            if (sourceNormal[0] == 0.0f && sourceNormal[1] == 0.0f && sourceNormal[2] == 0.0f)
            {
                normal[0] = normal[1] = normal[2] = 0.0f;
                return true;
            }
            double transformed[3] = {};
            for (size_t row = 0; row < 3; ++row)
            {
                for (size_t column = 0; column < 3; ++column)
                {
                    transformed[row] += normalMatrix[row][column] * sourceNormal[column];
                }
            }
            const double length = std::hypot(transformed[0], transformed[1], transformed[2]);
            if (!std::isfinite(length) || length == 0.0)
            {
                return false;
            }
            for (size_t row = 0; row < 3; ++row)
            {
                if (!AsFloat(transformed[row] / length, normal[row]))
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    const char* NodeTransformResultName(NodeTransformResult result) noexcept
    {
        switch (result)
        {
        case NodeTransformResult::Success: return "success";
        case NodeTransformResult::MultipleMeshNodes: return "mesh_node_multiple";
        case NodeTransformResult::SkinnedMeshNode: return "mesh_node_skinned";
        case NodeTransformResult::NonFiniteTransform: return "node_transform_non_finite";
        case NodeTransformResult::SingularTransform: return "node_transform_singular";
        case NodeTransformResult::InvalidNode: return "node_invalid";
        case NodeTransformResult::NonFiniteBakedVertex: return "baked_vertex_non_finite";
        case NodeTransformResult::InvalidLayout: return "vertex_layout_invalid";
        }
        return "node_invalid";
    }

    MeshNodeTransform ResolveMeshNodeTransform(const JsonValue& root, uint32_t meshIndex)
    {
        MeshNodeTransform result;
        if (!root.IsObject())
        {
            return result;
        }
        if (!root.HasMember("nodes"))
        {
            result.Result = NodeTransformResult::Success;
            return result;
        }
        const JsonValue nodes = root.FindMember("nodes");
        if (!nodes.IsArray() || nodes.GetArraySize() > UINT32_MAX)
        {
            return result;
        }
        const size_t count = nodes.GetArraySize();
        Container::VariableArray<uint32_t> parents(count, UINT32_MAX);
        for (size_t index = 0; index < count; ++index)
        {
            const JsonValue node = nodes.GetArrayElement(index);
            if (!node.IsObject())
            {
                return result;
            }
            if (!node.HasMember("children"))
            {
                continue;
            }
            const JsonValue children = node.FindMember("children");
            if (!children.IsArray())
            {
                return result;
            }
            for (size_t child = 0; child < children.GetArraySize(); ++child)
            {
                uint32_t childIndex = UINT32_MAX;
                if (!ReadIndex(children.GetArrayElement(child), childIndex) || childIndex >= count ||
                    parents[childIndex] != UINT32_MAX)
                {
                    return result;
                }
                parents[childIndex] = static_cast<uint32_t>(index);
            }
        }

        // 既定sceneに含まれない連結成分も含め、親の鎖の循環を拒否する。
        Container::VariableArray<uint8_t> states(count, 0);
        for (size_t index = 0; index < count; ++index)
        {
            uint32_t current = static_cast<uint32_t>(index);
            while (current != UINT32_MAX && states[current] == 0)
            {
                states[current] = 1;
                current = parents[current];
            }
            if (current != UINT32_MAX && states[current] == 1)
            {
                return result;
            }
            current = static_cast<uint32_t>(index);
            while (current != UINT32_MAX && states[current] == 1)
            {
                states[current] = 2;
                current = parents[current];
            }
        }
        for (uint8_t& state : states)
        {
            state = 0;
        }

        Container::VariableArray<uint32_t> pending;
        if (root.HasMember("scenes") || root.HasMember("scene"))
        {
            const JsonValue scenes = root.FindMember("scenes");
            uint32_t sceneIndex = 0;
            if (!scenes.IsArray() || scenes.GetArraySize() == 0 ||
                (root.HasMember("scene") && !ReadIndex(root.FindMember("scene"), sceneIndex)) ||
                sceneIndex >= scenes.GetArraySize())
            {
                return result;
            }
            const JsonValue scene = scenes.GetArrayElement(sceneIndex);
            if (!scene.IsObject())
            {
                return result;
            }
            if (scene.HasMember("nodes"))
            {
                const JsonValue roots = scene.FindMember("nodes");
                if (!roots.IsArray())
                {
                    return result;
                }
                for (size_t index = 0; index < roots.GetArraySize(); ++index)
                {
                    uint32_t rootIndex = UINT32_MAX;
                    if (!ReadIndex(roots.GetArrayElement(index), rootIndex) || rootIndex >= count ||
                        parents[rootIndex] != UINT32_MAX)
                    {
                        return result;
                    }
                    if (states[rootIndex] == 0)
                    {
                        states[rootIndex] = 1;
                        pending.push_back(rootIndex);
                    }
                }
            }
        }
        else
        {
            for (size_t index = 0; index < count; ++index)
            {
                if (parents[index] == UINT32_MAX)
                {
                    states[index] = 1;
                    pending.push_back(static_cast<uint32_t>(index));
                }
            }
        }

        uint32_t selected = UINT32_MAX;
        size_t matchCount = 0;
        while (!pending.empty())
        {
            const uint32_t current = pending.back();
            pending.pop_back();
            const JsonValue node = nodes.GetArrayElement(current);
            if (node.HasMember("mesh"))
            {
                uint32_t nodeMesh = UINT32_MAX;
                if (!ReadIndex(node.FindMember("mesh"), nodeMesh))
                {
                    return result;
                }
                if (nodeMesh == meshIndex)
                {
                    selected = current;
                    ++matchCount;
                }
            }
            const JsonValue children = node.FindMember("children");
            for (size_t index = 0; index < children.GetArraySize(); ++index)
            {
                uint32_t child = UINT32_MAX;
                if (!ReadIndex(children.GetArrayElement(index), child) || child >= count)
                {
                    return result;
                }
                if (states[child] == 0)
                {
                    states[child] = 1;
                    pending.push_back(child);
                }
            }
        }
        if (matchCount == 0)
        {
            result.Result = NodeTransformResult::Success;
            return result;
        }
        if (matchCount > 1)
        {
            result.Result = NodeTransformResult::MultipleMeshNodes;
            return result;
        }
        result.NodeIndex = selected;
        if (nodes.GetArrayElement(selected).HasMember("skin"))
        {
            result.Result = NodeTransformResult::SkinnedMeshNode;
            return result;
        }

        Container::VariableArray<uint32_t> ancestors;
        for (uint32_t current = selected; current != UINT32_MAX; current = parents[current])
        {
            if (ancestors.size() >= count)
            {
                result.Result = NodeTransformResult::InvalidNode;
                return result;
            }
            ancestors.push_back(current);
        }
        // 根から順に左結合で積を作り、途中のdoubleの丸め順序も一定にする。
        for (size_t index = ancestors.size(); index != 0; --index)
        {
            double local[16], product[16];
            result.Result = ReadLocalTransform(nodes.GetArrayElement(ancestors[index - 1]), local);
            if (result.Result != NodeTransformResult::Success)
            {
                return result;
            }
            if (!Multiply(result.Matrix, local, product))
            {
                result.Result = NodeTransformResult::NonFiniteTransform;
                return result;
            }
            std::memcpy(result.Matrix, product, sizeof(product));
        }
        double normal[3][3];
        result.Result = InvertLinear(result.Matrix, normal, result.Determinant);
        if (result.Result != NodeTransformResult::Success)
        {
            return result;
        }
        for (size_t index = 0; index < 16; ++index)
        {
            if (result.Matrix[index] != Identity[index])
            {
                result.bIdentity = false;
                break;
            }
        }
        return result;
    }

    NodeTransformResult BakeMeshNodeTransform(const MeshNodeTransform& transform,
        Container::Span<uint8_t> vertices, size_t vertexCount, const AssetImport::ImportVertexLayout& layout,
        Container::Span<uint32_t> indices) noexcept
    {
        if (transform.Result != NodeTransformResult::Success)
        {
            return transform.Result;
        }
        if (layout.Stride == 0 || !HasField(layout.PositionOffset, 12, layout.Stride) ||
            !HasField(layout.NormalOffset, 12, layout.Stride) ||
            Overlaps(layout.PositionOffset, 12, layout.NormalOffset, 12) ||
            vertexCount > vertices.size() / layout.Stride || indices.size() % 3 != 0)
        {
            return NodeTransformResult::InvalidLayout;
        }
        for (size_t index = 0; index < indices.size(); ++index)
        {
            if (indices[index] >= vertexCount)
            {
                return NodeTransformResult::InvalidLayout;
            }
        }
        if (transform.bIdentity)
        {
            return NodeTransformResult::Success;
        }
        bool bKeepNormal = true;
        for (size_t column = 0; column < 3; ++column)
        {
            for (size_t row = 0; row < 3; ++row)
            {
                bKeepNormal &= transform.Matrix[column * 4 + row] == Identity[column * 4 + row];
            }
        }
        double normalMatrix[3][3] = {};
        if (!bKeepNormal)
        {
            double determinant = 0.0;
            const auto inverseResult = InvertLinear(transform.Matrix, normalMatrix, determinant);
            if (inverseResult != NodeTransformResult::Success)
            {
                return inverseResult;
            }
        }
        // 全結果を先に検証し、どこかで失敗した場合は元のspanを保持する。
        for (size_t index = 0; index < vertexCount; ++index)
        {
            float position[3], normal[3];
            if (!EvaluateVertex(vertices.data() + index * layout.Stride, layout, transform.Matrix,
                    normalMatrix, bKeepNormal, position, normal))
            {
                return NodeTransformResult::NonFiniteBakedVertex;
            }
        }
        for (size_t index = 0; index < vertexCount; ++index)
        {
            uint8_t* vertex = vertices.data() + index * layout.Stride;
            float position[3], normal[3];
            (void)EvaluateVertex(vertex, layout, transform.Matrix, normalMatrix, bKeepNormal, position, normal);
            std::memcpy(vertex + layout.PositionOffset, position, sizeof(position));
            if (!bKeepNormal)
            {
                std::memcpy(vertex + layout.NormalOffset, normal, sizeof(normal));
            }
        }
        if (transform.Determinant < 0.0)
        {
            for (size_t index = 0; index < indices.size(); index += 3)
            {
                std::swap(indices[index + 1], indices[index + 2]);
            }
        }
        return NodeTransformResult::Success;
    }
} // namespace NorvesLib::Core::Gltf
