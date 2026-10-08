#include "Animation/SkeletalJointIndex.h"
#include "Resource/SkeletalGltfData.h"

namespace NorvesLib::Core::Animation
{
    SkeletalJointIndexResult BuildSkeletalJointIndexFromJoints(Container::Span<const Skeletal::SkeletalJoint> joints,
                                                               const SkeletalJointIndexLimits& limits,
                                                               SkeletalJointIndex& out)
    {
        using Status = SkeletalJointIndexStatus;
        if (joints.empty() ||
            !Asset::SkeletalNameDetail::ValidStorage(joints.data(), joints.size(), sizeof(Skeletal::SkeletalJoint),
                                                     alignof(Skeletal::SkeletalJoint)))
        {
            return {Status::InvalidInput};
        }
        Container::VariableArray<Container::VariableArray<uint8_t>> encoded;
        Container::VariableArray<SkeletalJointNameView> views;
        if (joints.size() > limits.MaxJoints || joints.size() > UINT32_MAX || joints.size() > encoded.max_size() ||
            joints.size() > views.max_size())
        {
            return {Status::LimitExceeded};
        }
        using Char = Container::String::value_type;
        const size_t maxBytes = Container::VariableArray<uint8_t>{}.max_size();
        size_t total = 0;
        // 全名のbyte上限を確保より先に検査し、code-unit数をbyte数として扱わない。
        for (size_t i = 0; i < joints.size(); ++i)
        {
            const auto& name = joints[i].Name;
            if (name.empty())
            {
                return {Status::EmptyName, i};
            }
            const auto measured = Asset::MeasureSkeletalNameEncoding<Char>(2, {name.data(), name.size()});
            if (!measured.Succeeded())
            {
                return {measured.Status == Asset::SkeletalNameStatus::NameTooLong ? Status::LimitExceeded
                                                                                  : Status::InvalidName,
                        i, SIZE_MAX, measured.Status};
            }
            if (measured.ByteCount > limits.MaxNameBytes || measured.ByteCount > UINT32_MAX - total ||
                measured.ByteCount > SIZE_MAX - total || total > limits.MaxTotalBytes ||
                measured.ByteCount > limits.MaxTotalBytes - total || total > maxBytes ||
                measured.ByteCount > maxBytes - total)
            {
                return {Status::LimitExceeded, i};
            }
            total += measured.ByteCount;
        }
        encoded.resize(joints.size());
        views.reserve(joints.size());
        for (size_t i = 0; i < joints.size(); ++i)
        {
            const auto& name = joints[i].Name;
            const auto measured = Asset::MeasureSkeletalNameEncoding<Char>(2, {name.data(), name.size()});
            if (!measured.Succeeded())
            {
                return {Status::InvalidName, i, SIZE_MAX, measured.Status};
            }
            encoded[i].resize(measured.ByteCount);
            const auto written = Asset::EncodeSkeletalWireName<Char>(2, {name.data(), name.size()}, encoded[i]);
            if (!written.Succeeded())
            {
                return {Status::InvalidName, i, SIZE_MAX, written.Status};
            }
            views.push_back({encoded[i]});
        }
        // 借用viewはこのcallだけ。戻る前に共通builderが別poolへ所有する。
        return BuildSkeletalJointIndex(views, limits, out);
    }
} // namespace NorvesLib::Core::Animation
