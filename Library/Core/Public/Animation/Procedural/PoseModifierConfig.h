#pragma once

#include "Animation/PoseModifier.h"
#include "Text/JsonDocument.h"

// 骨格単位の実行時の姿勢後処理設定ファイル（*.posemod.json）を読み込む。
namespace NorvesLib::Core::Animation::Procedural
{
    namespace PoseModifierDefaultOrder
    {
        inline constexpr int32_t Lean = 100;
        inline constexpr int32_t Spine = 110;
        inline constexpr int32_t Look = 200;
        inline constexpr int32_t FootIK = 500;
        inline constexpr int32_t SpringBone = 700;
        inline constexpr int32_t FollowThrough = 710;
    } // namespace PoseModifierDefaultOrder

    [[nodiscard]] bool TryGetDefaultPoseModifierOrder(Identity name, int32_t& out);

    enum class PoseModifierConfigError : uint8_t
    {
        None, InvalidJson, UnsupportedVersion, MissingKey, UnknownKey, DuplicateKey,
        TypeMismatch, OutOfRange, UnknownType, UnknownJoint, UnknownParam, InvalidCurve,
        NoSkeleton, Busy, FileReadFailed, CreateFailed
    };
    struct PoseModifierConfigReport
    {
        PoseModifierConfigError Error = PoseModifierConfigError::None;
        Container::String Path;
    };
    struct PoseModifierCreateArgs
    {
        const SkeletonResource& Skeleton;
        // 呼び出し中だけ有効な借用値。省略時は無効。保持する設定は作り手がコピーする。
        JsonValue Settings;
    };
    using PoseModifierFactory = bool (*)(const PoseModifierCreateArgs&, Container::TSharedPtr<IPoseModifier>& out,
                                        PoseModifierConfigReport& report);
    struct PoseModifierTypeEntry
    {
        const char* Name;
        int32_t DefaultOrder;
        PoseModifierFactory Create;
    };
    struct PoseModifierTypeTable
    {
        Container::Span<const PoseModifierTypeEntry> Entries;
    };
    PoseModifierTypeTable GetBuiltinPoseModifierTypes();

    // 補助関数の失敗パスは渡された値からの相対位置。既存の報告があれば保持する。
    [[nodiscard]] bool ResolvePoseModifierJoint(JsonValue name, const SkeletonResource& skeleton,
                                               int32_t& outIndex, PoseModifierConfigReport& report);
    [[nodiscard]] bool CheckPoseModifierKeys(JsonValue object, Container::Span<const char* const> allowed,
                                            PoseModifierConfigReport& report);
    // 全件成功したときだけ out を記載順の modifier 群で置き換える。
    // 作り手の報告パスは settings からの相対位置として扱う。
    [[nodiscard]] bool ParsePoseModifierConfig(const Container::String& json, const SkeletonResource& skeleton,
                                              const AnimParamSet* params, const PoseModifierTypeTable& types,
                                              Container::VariableArray<Container::TSharedPtr<IPoseModifier>>& out,
                                              PoseModifierConfigReport& report);
} // namespace NorvesLib::Core::Animation::Procedural
