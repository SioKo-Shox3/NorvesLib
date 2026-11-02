#include "Resource/ImportTransform.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace NorvesLib::Core::AssetImport
{
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
    namespace
    {
        // 2^-1074を単位とする2の補数整数。binary64の全範囲と64ビット分の加算回数を保持する。
        struct ExactSum
        {
            static constexpr size_t LimbCount = 34;
            uint64_t Limbs[LimbCount] = {};

            void AddWord(size_t index, uint64_t word, bool bNegative)
            {
                while (word != 0 && index < LimbCount)
                {
                    const uint64_t previous = Limbs[index];
                    if (bNegative)
                    {
                        Limbs[index] -= word;
                        word = previous < word ? 1 : 0;
                    }
                    else
                    {
                        Limbs[index] += word;
                        word = Limbs[index] < previous ? 1 : 0;
                    }
                    ++index;
                }
            }

            bool Add(double value)
            {
                if (!std::isfinite(value))
                {
                    return false;
                }
                const uint64_t bits = std::bit_cast<uint64_t>(value);
                const uint64_t exponent = (bits >> 52) & 0x7ff;
                uint64_t significand = bits & ((uint64_t{1} << 52) - 1);
                const size_t shift = exponent == 0 ? 0 : static_cast<size_t>(exponent - 1);
                if (exponent != 0)
                {
                    significand |= uint64_t{1} << 52;
                }
                const size_t index = shift / 64;
                const size_t offset = shift % 64;
                const bool bNegative = (bits >> 63) != 0;
                AddWord(index, significand << offset, bNegative);
                if (offset != 0)
                {
                    AddWord(index + 1, significand >> (64 - offset), bNegative);
                }
                return true;
            }

            bool AddProduct(double left, double right)
            {
                const double product = left * right;
                if (!std::isfinite(product))
                {
                    return false;
                }
                // 積の丸めで失われた下位分も正確な和に加える。
                return Add(product) && Add(std::fma(left, right, -product));
            }

            bool IsZero() const
            {
                for (const uint64_t limb : Limbs)
                {
                    if (limb != 0)
                    {
                        return false;
                    }
                }
                return true;
            }

            bool ToDouble(double& out) const
            {
                const bool bNegative = (Limbs[LimbCount - 1] >> 63) != 0;
                uint64_t magnitude[LimbCount];
                uint64_t carry = bNegative ? 1 : 0;
                for (size_t index = 0; index < LimbCount; ++index)
                {
                    magnitude[index] = (bNegative ? ~Limbs[index] : Limbs[index]) + carry;
                    carry = carry != 0 && magnitude[index] == 0 ? 1 : 0;
                }
                size_t count = LimbCount;
                while (count != 0 && magnitude[count - 1] == 0)
                {
                    --count;
                }
                if (count == 0)
                {
                    out = 0.0;
                    return true;
                }
                size_t highest = (count - 1) * 64 + 63 - std::countl_zero(magnitude[count - 1]);
                const uint64_t sign = bNegative ? uint64_t{1} << 63 : 0;
                if (highest < 52)
                {
                    // 非正規数は累算器の単位と同じため丸めずに表現できる。
                    out = std::bit_cast<double>(sign | magnitude[0]);
                    return true;
                }
                if (highest > 2097)
                {
                    return false;
                }
                const size_t shift = highest - 52;
                const size_t index = shift / 64, offset = shift % 64;
                uint64_t significand = magnitude[index] >> offset;
                if (offset != 0 && index + 1 < LimbCount)
                {
                    significand |= magnitude[index + 1] << (64 - offset);
                }
                if (shift != 0)
                {
                    const size_t guardIndex = shift - 1;
                    const bool bGuard = ((magnitude[guardIndex / 64] >> (guardIndex % 64)) & 1) != 0;
                    bool bSticky = false;
                    for (size_t limb = 0; limb < guardIndex / 64; ++limb)
                    {
                        bSticky |= magnitude[limb] != 0;
                    }
                    const size_t remainder = guardIndex % 64;
                    if (remainder != 0)
                    {
                        bSticky |= (magnitude[guardIndex / 64] & ((uint64_t{1} << remainder) - 1)) != 0;
                    }
                    // 中間値は最下位の仮数が偶数になる方向に丸める。
                    if (bGuard && (bSticky || (significand & 1) != 0))
                    {
                        ++significand;
                        if (significand == (uint64_t{1} << 53))
                        {
                            significand >>= 1;
                            ++highest;
                        }
                    }
                }
                if (highest > 2097)
                {
                    return false;
                }
                const uint64_t exponent = static_cast<uint64_t>(highest - 51);
                const uint64_t fraction = significand & ((uint64_t{1} << 52) - 1);
                out = std::bit_cast<double>(sign | (exponent << 52) | fraction);
                return true;
            }
        };

        struct TransformPlan
        {
            double Rotation[3][3] = {};
            double Scale = 1.0;
            double Pivot[3] = {};
            double Offset[3] = {};
            bool bFlipU = false;
            bool bFlipV = false;
        };
        bool Intersects(size_t left, size_t leftSize, size_t right, size_t rightSize)
        {
            return left <= right ? right - left < leftSize : left - right < rightSize;
        }
        bool MemoryIntersects(const void* left, size_t leftSize, const void* right, size_t rightSize)
        {
            const auto a = reinterpret_cast<uintptr_t>(left);
            const auto b = reinterpret_cast<uintptr_t>(right);
            return a <= b ? b - a < leftSize : a - b < rightSize;
        }
        bool HasField(size_t offset, size_t fieldSize, size_t stride)
        {
            return offset <= stride && fieldSize <= stride - offset;
        }
        void ReadPosition(const uint8_t* vertex, size_t offset, double (&out)[3])
        {
            float position[3];
            std::memcpy(position, vertex + offset, sizeof(position));
            for (size_t axis = 0; axis < 3; ++axis)
            {
                out[axis] = position[axis];
            }
        }
        bool Finite3(const double (&value)[3])
        {
            return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
        }
        void Rotate(const TransformPlan& plan, const double (&value)[3], double (&out)[3])
        {
            for (size_t row = 0; row < 3; ++row)
            {
                out[row] = plan.Rotation[row][0] * value[0] +
                    plan.Rotation[row][1] * value[1] + plan.Rotation[row][2] * value[2];
            }
        }
        void ReadRotatedPosition(const uint8_t* vertex, size_t offset, const TransformPlan& plan, double (&out)[3])
        {
            double position[3];
            ReadPosition(vertex, offset, position);
            Rotate(plan, position, out);
        }
        bool SurfaceWeight(const double (&a)[3], const double (&b)[3], const double (&c)[3], double& out)
        {
            double cross[3];
            for (size_t axis = 0; axis < 3; ++axis)
            {
                const size_t y = (axis + 1) % 3, z = (axis + 2) % 3;
                ExactSum sum;
                if (!sum.Add(a[y] * b[z]) || !sum.Add(-a[z] * b[y]) ||
                    !sum.Add(b[y] * c[z]) || !sum.Add(-b[z] * c[y]) ||
                    !sum.Add(c[y] * a[z]) || !sum.Add(-c[z] * a[y]) || !sum.ToDouble(cross[axis]))
                {
                    return false;
                }
            }
            out = std::hypot(std::abs(cross[0]), std::abs(cross[1]), std::abs(cross[2]));
            return std::isfinite(out);
        }
        void BuildRotation(const ImportSettings& settings, TransformPlan& plan)
        {
            const auto up = static_cast<uint8_t>(settings.Up);
            const auto forward = static_cast<uint8_t>(settings.Forward);
            plan.Rotation[1][up / 2] = up % 2 == 0 ? 1.0 : -1.0;
            plan.Rotation[2][forward / 2] = forward % 2 == 0 ? 1.0 : -1.0;
            const auto& u = plan.Rotation[1];
            const auto& f = plan.Rotation[2];
            plan.Rotation[0][0] = u[1] * f[2] - u[2] * f[1];
            plan.Rotation[0][1] = u[2] * f[0] - u[0] * f[2];
            plan.Rotation[0][2] = u[0] * f[1] - u[1] * f[0];
            if (settings.bMirrorX)
            {
                for (double& value : plan.Rotation[0])
                {
                    value = -value;
                }
            }
        }
        bool AsFloat(double value, float& out, bool bRejectUnderflow = false)
        {
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
            {
                return false;
            }
            out = static_cast<float>(value);
            return !bRejectUnderflow || value == 0.0 || out != 0.0f;
        }
        TransformResult EvaluateVertex(const uint8_t* vertex, ImportVertexLayout layout,
                                       const TransformPlan& plan, float (&output)[8])
        {
            double position[3];
            ReadRotatedPosition(vertex, layout.PositionOffset, plan, position);
            for (size_t axis = 0; axis < 3; ++axis)
            {
                const double relative = position[axis] - plan.Pivot[axis];
                const double scaled = relative * plan.Scale;
                if ((relative != 0.0 && scaled == 0.0) ||
                    !AsFloat(scaled + plan.Offset[axis], output[axis], true))
                {
                    return TransformResult::Unrepresentable;
                }
            }
            double normal[3], rotatedNormal[3];
            ReadPosition(vertex, layout.NormalOffset, normal);
            if (!Finite3(normal))
            {
                return TransformResult::InvalidVertex;
            }
            Rotate(plan, normal, rotatedNormal);
            const double length = std::hypot(rotatedNormal[0], rotatedNormal[1], rotatedNormal[2]);
            if (!std::isfinite(length) || length == 0.0)
            {
                return TransformResult::InvalidVertex;
            }
            // 一様な正scaleでは逆転置のscale成分が正規化で消え、直交rotation/mirrorだけ残る。
            for (size_t axis = 0; axis < 3; ++axis)
            {
                if (!AsFloat(rotatedNormal[axis] / length, output[3 + axis]))
                {
                    return TransformResult::Unrepresentable;
                }
            }
            float uv[2];
            std::memcpy(uv, vertex + layout.TexCoordOffset, sizeof(uv));
            if (!AsFloat(plan.bFlipU ? 1.0 - static_cast<double>(uv[0]) : uv[0], output[6]) ||
                !AsFloat(plan.bFlipV ? 1.0 - static_cast<double>(uv[1]) : uv[1], output[7]))
            {
                return TransformResult::InvalidVertex;
            }
            return TransformResult::Success;
        }
    } // namespace

    bool SupportsSkeletalScaleImport(const ImportSettings& settings) noexcept
    {
        return ValidateSettings(settings) == SettingsResult::Success &&
            settings.Up == SignedAxis::PositiveY && settings.Forward == SignedAxis::PositiveZ &&
            !settings.bMirrorX && settings.Origin == OriginMode::Keep &&
            settings.Winding == WindingMode::Keep && !settings.bFlipU && !settings.bFlipV;
    }

    UniformImportScale ResolveUniformImportScale(const ImportSettings& settings,
        const double (&minimum)[3], const double (&maximum)[3]) noexcept
    {
        const auto validation = ValidateSettings(settings);
        if (validation != SettingsResult::Success)
        {
            return {validation == SettingsResult::UnsupportedFeature ? TransformResult::UnsupportedOrigin :
                TransformResult::InvalidSettings, 1.0};
        }
        double extents[3];
        for (size_t axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) || maximum[axis] < minimum[axis])
            {
                return {TransformResult::InvalidVertex, 1.0};
            }
            extents[axis] = maximum[axis] - minimum[axis];
            if (!std::isfinite(extents[axis]))
            {
                return {TransformResult::Unrepresentable, 1.0};
            }
        }
        double scale = settings.Scale;
        if (settings.Fit != FitAxis::None)
        {
            const double extent = settings.Fit == FitAxis::Up ? extents[1] :
                settings.Fit == FitAxis::Forward ? extents[2] : std::max({extents[0], extents[1], extents[2]});
            if (extent <= 0.0)
            {
                return {TransformResult::DegenerateFit, 1.0};
            }
            scale = settings.FitMeters / extent;
        }
        if (!std::isfinite(scale) || scale <= 0.0)
        {
            return {TransformResult::Unrepresentable, 1.0};
        }
        return {TransformResult::Success, scale};
    }

    bool TryScaleImportValue(float value, double scale, float& outValue) noexcept
    {
        if (!std::isfinite(value) || !std::isfinite(scale) || scale <= 0.0)
        {
            return false;
        }
        const double result = static_cast<double>(value) * scale;
        float candidate = 0.0f;
        if ((value != 0.0f && result == 0.0) || !AsFloat(result, candidate, true))
        {
            return false;
        }
        outValue = candidate;
        return true;
    }

    ImportTransformOutcome ApplyImportTransform(Container::Span<uint8_t> vertices, size_t vertexCount,
        ImportVertexLayout layout, Container::Span<uint32_t> indices, const ImportSettings& input) noexcept
    {
        // 入力設定が頂点storageとaliasしていても、書込passで設定が変化しないよう値で固定する。
        const ImportSettings settings = input;
        const auto fail = [](TransformResult result)
        {
            ImportTransformOutcome outcome;
            outcome.Result = result;
            return outcome;
        };
        const auto validation = ValidateSettings(settings);
        if (validation == SettingsResult::UnsupportedFeature)
        {
            return fail(TransformResult::UnsupportedOrigin);
        }
        if (validation != SettingsResult::Success)
        {
            return fail(TransformResult::InvalidSettings);
        }
        if (vertices.data() == nullptr || vertexCount == 0 || layout.Stride == 0 ||
            vertexCount > vertices.size() / layout.Stride ||
            !HasField(layout.PositionOffset, 12, layout.Stride) || !HasField(layout.NormalOffset, 12, layout.Stride) ||
            !HasField(layout.TexCoordOffset, 8, layout.Stride) ||
            Intersects(layout.PositionOffset, 12, layout.NormalOffset, 12) ||
            Intersects(layout.PositionOffset, 12, layout.TexCoordOffset, 8) ||
            Intersects(layout.NormalOffset, 12, layout.TexCoordOffset, 8))
        {
            return fail(TransformResult::InvalidLayout);
        }
        if (indices.data() == nullptr || indices.empty() || indices.size() % 3 != 0 ||
            indices.size() > std::numeric_limits<size_t>::max() / sizeof(uint32_t) ||
            MemoryIntersects(vertices.data(), vertices.size(), indices.data(), indices.size() * sizeof(uint32_t)))
        {
            return fail(TransformResult::InvalidIndices);
        }
        for (const auto index : indices)
        {
            if (index >= vertexCount)
            {
                return fail(TransformResult::InvalidIndices);
            }
        }
        TransformPlan plan;
        BuildRotation(settings, plan);
        plan.bFlipU = settings.bFlipU;
        plan.bFlipV = settings.bFlipV;
        double minimum[3], maximum[3];
        ReadRotatedPosition(vertices.data(), layout.PositionOffset, plan, minimum);
        if (!Finite3(minimum))
        {
            return fail(TransformResult::InvalidVertex);
        }
        for (size_t axis = 0; axis < 3; ++axis)
        {
            maximum[axis] = minimum[axis];
        }
        for (size_t index = 1; index < vertexCount; ++index)
        {
            double position[3];
            ReadRotatedPosition(vertices.data() + index * layout.Stride, layout.PositionOffset, plan, position);
            if (!Finite3(position))
            {
                return fail(TransformResult::InvalidVertex);
            }
            for (size_t axis = 0; axis < 3; ++axis)
            {
                minimum[axis] = std::min(minimum[axis], position[axis]);
                maximum[axis] = std::max(maximum[axis], position[axis]);
            }
        }
        const auto resolvedScale = ResolveUniformImportScale(settings, minimum, maximum);
        if (resolvedScale.Result != TransformResult::Success)
        {
            return fail(resolvedScale.Result);
        }
        plan.Scale = resolvedScale.Value;
        double surfaceArea = 0.0;
        if (settings.Origin == OriginMode::BoundsCenter || settings.Origin == OriginMode::BoundsBottomCenter)
        {
            for (size_t axis = 0; axis < 3; ++axis)
            {
                plan.Pivot[axis] = minimum[axis] / 2.0 + maximum[axis] / 2.0;
            }
            if (settings.Origin == OriginMode::BoundsBottomCenter)
            {
                plan.Pivot[1] = minimum[1];
            }
        }
        else if (settings.Origin == OriginMode::SurfaceCentroid)
        {
            ExactSum area, area3, moment[3];
            for (size_t triangle = 0; triangle < indices.size(); triangle += 3)
            {
                double position[3][3];
                for (size_t vertex = 0; vertex < 3; ++vertex)
                {
                    ReadRotatedPosition(vertices.data() + static_cast<size_t>(indices[triangle + vertex]) * layout.Stride,
                        layout.PositionOffset, plan, position[vertex]);
                }
                double weight = 0.0;
                if (!SurfaceWeight(position[0], position[1], position[2], weight))
                {
                    return fail(TransformResult::Unrepresentable);
                }
                if (weight == 0.0)
                {
                    continue;
                }
                if (!area3.AddProduct(weight, 3.0) || !area.Add(weight))
                {
                    return fail(TransformResult::Unrepresentable);
                }
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    for (size_t vertex = 0; vertex < 3; ++vertex)
                    {
                        if (!moment[axis].AddProduct(position[vertex][axis], weight))
                        {
                            return fail(TransformResult::Unrepresentable);
                        }
                    }
                }
            }
            if (area.IsZero())
            {
                return fail(TransformResult::DegenerateSurface);
            }
            double totalArea = 0.0, totalArea3 = 0.0;
            if (!area.ToDouble(totalArea) || !area3.ToDouble(totalArea3) || totalArea3 == 0.0)
            {
                return fail(TransformResult::Unrepresentable);
            }
            surfaceArea = totalArea / 2.0;
            for (size_t axis = 0; axis < 3; ++axis)
            {
                double totalMoment = 0.0;
                if (!moment[axis].ToDouble(totalMoment))
                {
                    return fail(TransformResult::Unrepresentable);
                }
                plan.Pivot[axis] = totalMoment / totalArea3;
                if (!std::isfinite(plan.Pivot[axis]))
                {
                    return fail(TransformResult::Unrepresentable);
                }
            }
        }
        else if (settings.Origin == OriginMode::Custom)
        {
            for (size_t axis = 0; axis < 3; ++axis)
            {
                plan.Offset[axis] = settings.OriginOffset[axis];
            }
        }
        // 全結果を先に反証してから書き込む。同一storageを不変入力として読むため追加確保は要らない。
        for (size_t index = 0; index < vertexCount; ++index)
        {
            float output[8];
            const auto result = EvaluateVertex(vertices.data() + index * layout.Stride, layout, plan, output);
            if (result != TransformResult::Success)
            {
                return fail(result);
            }
        }
        for (size_t index = 0; index < vertexCount; ++index)
        {
            uint8_t* vertex = vertices.data() + index * layout.Stride;
            float output[8] = {};
            (void)EvaluateVertex(vertex, layout, plan, output);
            std::memcpy(vertex + layout.PositionOffset, output, 12);
            std::memcpy(vertex + layout.NormalOffset, output + 3, 12);
            std::memcpy(vertex + layout.TexCoordOffset, output + 6, 8);
        }
        const bool bFlip = settings.Winding == WindingMode::Flip ||
            (settings.Winding == WindingMode::Auto && settings.bMirrorX);
        if (bFlip)
        {
            for (size_t triangle = 0; triangle < indices.size(); triangle += 3)
            {
                std::swap(indices[triangle + 1], indices[triangle + 2]);
            }
        }
        ImportTransformOutcome outcome;
        outcome.Result = TransformResult::Success;
        outcome.AppliedScale = plan.Scale;
        outcome.SurfaceArea = surfaceArea;
        for (size_t axis = 0; axis < 3; ++axis)
        {
            outcome.Pivot[axis] = plan.Pivot[axis];
        }
        outcome.bFlippedWinding = bFlip;
        return outcome;
    }
} // namespace NorvesLib::Core::AssetImport
