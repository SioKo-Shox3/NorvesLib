// スキンメッシュ表面のアンカー評価と関節ごとの突き立て位置探索を提供する。
#pragma once

#include "Container/Span.h"
#include "Container/VariableArray.h"
#include "Math/GeometryTypes.h"
#include <cstdint>

namespace NorvesLib::Core
{
    class SkinnedMeshResource;
}

namespace NorvesLib::Core::Animation
{
    // 実行時だけの記述であり、保存やリフレクションの対象にはしない。
    // U は頂点 1、V は頂点 2 の重み。三角形が退化した場合は関節の局所オフセットを使う。
    struct SurfaceAnchorDesc
    {
        uint32_t TriangleIndex = 0;
        float U = 0.0f;
        float V = 0.0f;
        uint64_t MeshRevision = 0;
        uint32_t FallbackJoint = 0;
        Math::Vector3 FallbackLocalOffset = Math::Vector3::Zero;
    };

    // 全てのベクトルはワールド空間。退化時は M = 関節モデル行列 * OwnerWorld とし、
    // M で局所オフセットを変換した位置、M の局所 Y 軸の行を正規化した法線、
    // 局所 X 軸の行を法線に直交化して正規化した接線を使う。
    struct SurfaceAnchorFrame
    {
        Math::Vector3 Position;
        Math::Vector3 Normal;
        Math::Vector3 Tangent;
        bool bValid = false;
        bool bFromFallbackJoint = false;
    };

    struct SurfaceAnchorPoseView
    {
        // 両行列列は Entity 空間で、OwnerWorld を右から掛ける。
        Container::Span<const Math::Matrix4x4> Palette;
        Container::Span<const Math::Matrix4x4> JointModelMatrices;
        Math::Matrix4x4 OwnerWorld = Math::Matrix4x4::Identity;
    };

    [[nodiscard]] SurfaceAnchorFrame EvaluateSurfaceAnchor(const SurfaceAnchorDesc& desc,
                                                           const SkinnedMeshResource& mesh,
                                                           const SurfaceAnchorPoseView& pose) noexcept;

    // 三角形を最大重みの関節へ一度だけ割り当てた CSR 表。
    // リビジョンだけを照合するため、構築元と同じメッシュに使い、インデックス変更時は再構築する。
    class JointTriangleBuckets
    {
    public:
        [[nodiscard]] bool Build(const SkinnedMeshResource& mesh, uint32_t jointCount);
        [[nodiscard]] Container::Span<const uint32_t> GetTriangles(uint32_t joint) const noexcept;
        [[nodiscard]] uint64_t GetMeshRevision() const noexcept;
        [[nodiscard]] uint32_t GetJointCount() const noexcept;
        [[nodiscard]] uint32_t GetUnassignedTriangleCount() const noexcept;
        [[nodiscard]] bool IsBuiltFor(const SkinnedMeshResource& mesh) const noexcept;

    private:
        Container::VariableArray<uint32_t> m_Offsets;
        Container::VariableArray<uint32_t> m_Triangles;
        uint64_t m_MeshRevision = 0;
        uint32_t m_JointCount = 0;
        uint32_t m_UnassignedTriangleCount = 0;
        bool m_bBuilt = false;
    };

    enum class StabResultKind : uint8_t
    {
        Hit,
        Snapped,
        Miss
    };

    enum class StabMissReason : uint8_t
    {
        None,
        InvalidInput,
        NoCandidates,
        BeyondSnapDistance
    };

    struct StabQuery
    {
        Math::Ray Ray; // ワールド空間。方向は単位ベクトル。
        float MaxDistance = 0.0f;
        float MaxSnapDistance = 0.0f;
        Container::Span<const uint32_t> CandidateJoints;
    };

    struct StabResult
    {
        StabResultKind Kind = StabResultKind::Miss;
        StabMissReason Reason = StabMissReason::None;
        uint32_t TriangleIndex = 0;
        float U = 0.0f;
        float V = 0.0f;
        float Distance = 0.0f; // Hit はレイ上の距離、Snapped は表面までの隙間。
        Math::Vector3 WorldPosition;
        uint32_t TrianglesTested = 0;
        uint64_t MeshRevision = 0;
    };

    // 退化した三角形は交差を試すが、評価時に関節へ戻るため返却候補から除外する。
    // 候補関節のバケットにスキニングできる三角形が無ければ NoCandidates を返す。
    // 退化した三角形だけを試した場合は BeyondSnapDistance を返す。
    [[nodiscard]] StabResult FindSurfacePoint(const StabQuery& query, const JointTriangleBuckets& buckets,
                                              const SkinnedMeshResource& mesh,
                                              const SurfaceAnchorPoseView& pose) noexcept;

    // 以下の既定値は仮の値であり、用途に合わせて調整する。
    struct SurfaceAnchorMotionSettings
    {
        float SmoothingTimeConstantSeconds = 0.08f;
        float MaxSpeed = 50.0f;          // m/s
        float MaxAcceleration = 2000.0f; // m/s²
        float MaxDeltaTime = 0.25f;      // 秒
    };

    struct SurfaceAnchorMotionSample
    {
        Math::Vector3 Velocity;
        Math::Vector3 TangentialVelocity;
        Math::Vector3 SmoothedVelocity;
        Math::Vector3 Acceleration;
        bool bHasVelocity = false;
        bool bHasAcceleration = false;
        bool bDiscontinuity = false;
    };

    // 位置 P の速度は v = (P_now - P_prev) / dt、接線速度は v - (v・n)n。
    // n は現在のフレームの単位法線。平滑は s = s_prev + α(v - s_prev)、
    // α = 1 - exp(-dt / τ)、τ <= 0 では α = 1。最初の速度は s の初期値にする。
    // 加速度は平滑速度の差分 a = (s - s_prev) / dt。速度は 2 フレーム目、
    // 加速度は 3 フレーム目から得られる。
    // 無効なフレーム、非有限の位置・法線、非有限・0 以下・MaxDeltaTime 超過の dt、
    // MaxSpeed 超過の速度、MaxAcceleration 超過の生速度の変化率、
    // 強制リセット、計算結果が float で表せない場合は不連続とし履歴を消す。
    // 無効なフレームと非有限の位置・法線は起点にせず、それ以外は現在の位置を新しい起点にする。
    struct SurfaceAnchorMotion
    {
        [[nodiscard]] SurfaceAnchorMotionSample Update(const SurfaceAnchorFrame& frame, float deltaSeconds,
                                                       const SurfaceAnchorMotionSettings& settings = {},
                                                       bool bForceReset = false) noexcept;
        void Reset() noexcept;

    private:
        Math::Vector3 m_PreviousPosition;
        Math::Vector3 m_PreviousVelocity;
        Math::Vector3 m_SmoothedVelocity;
        bool m_bHasPosition = false;
        bool m_bHasVelocity = false;
    };

} // namespace NorvesLib::Core::Animation
