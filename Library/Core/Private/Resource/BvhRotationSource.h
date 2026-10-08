#pragma once
// 元documentを検証してから回転だけを読む。元のValuesはplanの使用中、生存し不変でなければならない。
#include "BvhEvaluate.h"

namespace NorvesLib::Core::Bvh
{
    struct BvhRotationSourceLimits
    {
        uint32_t MaxJoints = 1024;
        uint32_t MaxDepth = 256;
        uint32_t MaxFrames = 1000000;
        size_t MaxValues = size_t{1} << 23;
        size_t MaxNameBytes = 512;
        size_t MaxTotalNameBytes = 1024u * 1024u;
    };
    enum class BvhRotationSourceStatus : uint8_t
    {
        Success,
        InvalidDocument,
        InvalidName,
        DuplicateName,
        UnsupportedRotationChannels,
        LimitExceeded,
        UnsupportedFloatEnvironment,
        FrameOutOfRange,
        InvalidOutput,
        NonFiniteRotation
    };
    struct BvhRotationSourceResult
    {
        BvhRotationSourceStatus Status = BvhRotationSourceStatus::InvalidDocument;
        size_t JointIndex = SIZE_MAX;
        uint32_t FrameIndex = UINT32_MAX;
        size_t ValueIndex = SIZE_MAX;
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == BvhRotationSourceStatus::Success;
        }
    };
    class BvhRotationSourcePlan
    {
      public:
        BvhRotationSourcePlan() = default;
        BvhRotationSourcePlan(const BvhRotationSourcePlan&) = delete;
        BvhRotationSourcePlan& operator=(const BvhRotationSourcePlan&) = delete;
        BvhRotationSourcePlan(BvhRotationSourcePlan&& other) noexcept;
        BvhRotationSourcePlan& operator=(BvhRotationSourcePlan&& other) noexcept;
        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_bValid;
        }
        [[nodiscard]] size_t GetJointCount() const noexcept
        {
            return m_Joints.size();
        }
        [[nodiscard]] uint32_t GetFrameCount() const noexcept
        {
            return m_FrameCount;
        }
        // indexはGetJointCount未満。ROOTはUINT32_MAXを返す。
        [[nodiscard]] uint32_t GetParent(size_t index) const noexcept
        {
            return m_Joints[index].Parent;
        }

      private:
        struct JointRotation
        {
            uint32_t Parent = UINT32_MAX;
            uint32_t Depth = 0;
            Container::FixedArray<uint32_t, 3> ValueIndices{0, 0, 0};
            Container::FixedArray<uint8_t, 3> Axes{0, 0, 0};
            uint8_t RotationCount = 0;
        };
        Container::VariableArray<JointRotation> m_Joints;
        Container::Span<const double> m_Values;
        uint32_t m_FrameCount = 0;
        uint32_t m_FrameWidth = 0;
        bool m_bValid = false;

      public:
        [[nodiscard]] static constexpr size_t GetJointStorageSize() noexcept
        {
            return sizeof(JointRotation);
        }

      private:
        void Swap(BvhRotationSourcePlan& other) noexcept;
        friend BvhRotationSourceResult PrepareBvhRotationSource(const BvhDocument&, const BvhRotationSourceLimits&,
                                                                BvhRotationSourcePlan&);
        friend BvhRotationSourceResult EvaluateBvhRotationFrame(const BvhRotationSourcePlan&, uint32_t,
                                                                Container::Span<Matrix3d>) noexcept;
    };
    // 全元値/名前/構造を検証する。positionのpartial/並びは無視できるが、rotationはXYZ一組か無しだけ。
    // 成功時だけoutを置換し、確保例外でも以前のplanを保持する。作者時restやsource所有cacheではない。
    [[nodiscard]] BvhRotationSourceResult PrepareBvhRotationSource(const BvhDocument& document,
                                                                   const BvhRotationSourceLimits& limits,
                                                                   BvhRotationSourcePlan& out);
    // 配列順は元joint順。local/worldを旧FKと同じ算術で計算し、位置演算は一切行わない。
    // outWorldは確保済みで、元Valuesとの重なりを拒否する。失敗時には部分更新され得る。
    [[nodiscard]] BvhRotationSourceResult EvaluateBvhRotationFrame(const BvhRotationSourcePlan& plan,
                                                                   uint32_t frameIndex,
                                                                   Container::Span<Matrix3d> outWorld) noexcept;
} // namespace NorvesLib::Core::Bvh
