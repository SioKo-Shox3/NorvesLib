#pragma once

#include "Container/Span.h"
#include "Resource/SkeletalCubicBounds.h"
#include "Resource/SkeletalImportOptions.h"
#include <cstdint>

namespace NorvesLib::Core::Skeletal
{
    enum class CubicBakeKind
    {
        Vector3,
        Rotation
    };
    struct CubicBakeInputKey
    {
        float Time = 0;
        CubicFloatPoint Incoming;
        CubicFloatPoint Value;
        CubicFloatPoint Outgoing;
    };
    struct CubicBakeKey
    {
        float Time = 0;
        CubicFloatPoint Value;
    };
    struct CubicBakeOptions
    {
        CubicBakeKind Kind = CubicBakeKind::Vector3;
        double Tolerance = 0.001;
        double ValueScale = 1;
        uint32_t MaximumDepth = DefaultCubicMaximumDepth;
        uint32_t MaximumSamples = DefaultCubicMaximumSamplesPerChannel;
    };
    enum class CubicBakeStatus
    {
        Success, InvalidInput, InvalidOptions, UnsupportedArithmetic, InsufficientStorage,
        OverlappingStorage, ShortInterval, TimeCollision, DepthExceeded, SampleLimitExceeded,
        UnrepresentableValue, InvalidQuaternion, NumericBudgetExceeded, Uncertified
    };
    struct CubicBakeResult
    {
        CubicBakeStatus Status = CubicBakeStatus::InvalidInput;
        uint32_t SampleCount = 0;
        uint32_t MaximumDepthUsed = 0;
        double MaximumAcceptedErrorUpper = 0;
    };
    // toleranceはVector3ならValueScale適用後のL2長さ、RotationならSO(3)角度rad。
    // IEEE binary32/64最近接、gradual underflow、正確なsqrt、精密FPのsamplerを前提とする。
    // 回転は保存端点norm[.99,1.01]/dot下限>.9996を認証してNLERP枝だけを使う。
    // 入力/全workspace/全out/optionsは書換え領域と非重複。workspaceは途中変更可。
    // 成功時だけout先頭SampleCountを置換し、失敗時outは保持する。未認証prefixを公開しない。
    [[nodiscard]] CubicBakeResult BakeCubicChannel(Container::Span<const CubicBakeInputKey> input,
        const CubicBakeOptions& options, Container::Span<CubicBakeKey> workspace,
        Container::Span<CubicBakeKey> out) noexcept;
} // namespace NorvesLib::Core::Skeletal
