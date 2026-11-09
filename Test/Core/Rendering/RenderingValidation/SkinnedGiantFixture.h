// GPU 試験で使う高密度スキンメッシュの生成データを定義する。
#pragma once

#include "Math/GeometryTypes.h"
#include "Resource/SkeletalGltfData.h"

#include <cstdint>

namespace NorvesLib::Test::RenderingValidation
{
    struct SkinnedGiantFixtureDesc
    {
        uint32_t TargetTriangleCount = 1000000u;
        uint32_t JointCount = 128u;
        float Length = 20.0f;
        float Radius = 1.0f;
    };

    struct SkinnedGiantFixtureData
    {
        Core::Container::VariableArray<Core::Skeletal::SkeletalVertex> Vertices;
        Core::Container::VariableArray<uint32_t> Indices;
        Core::Container::VariableArray<Core::Skeletal::SkeletalJoint> Joints;
        Math::AABB BindBounds;
    };

    // 関節は 2 本以上必要。失敗した場合は out を変更しない。
    bool BuildSkinnedGiantFixture(const SkinnedGiantFixtureDesc& desc, SkinnedGiantFixtureData& out);
} // namespace NorvesLib::Test::RenderingValidation
