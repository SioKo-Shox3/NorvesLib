#pragma once

#include "Math/Vector3.h"
#include "Container/Span.h"
#include "Math/PolylinePath.h"
#include <cstdint>

/// 動物の操舵を計算する。長さ 0、NaN、無限大、極端な有限値を含む入力でも、
/// すべての関数の数値出力は有限となる。同じ入力と状態からは同じ結果を返す。
namespace NorvesLib::Modules::AI::Steering
{
    enum class SteeringPlane : uint8_t
    {
        Ground, // 高さを除き、XZ 平面で操舵する。
        Flight
    };

    struct SteeringKinematics
    {
        Math::Vector3 Position;
        Math::Vector3 Velocity;
    };

    struct SteeringLimits
    {
        float MaxSpeed = 4.0f;        // 速さの上限（m/s）
        float MaxAcceleration = 8.0f; // 加速度の上限（m/s²）
        float TurnRateBySpeed[4] = {6.0f, 4.0f, 3.0f, 2.0f}; // 速度比 0、1/3、2/3、1 の旋回率（rad/s）
    };

    struct ArriveParams
    {
        float StopRadius = 0.1f;
        float BrakingFactor = 0.8f;
    };

    struct PursueParams
    {
        float MaxPredictionTime = 1.5f;
    };

    enum class KeepDistanceMode : uint8_t
    {
        Hold,
        Approach,
        Retreat
    };

    struct KeepDistanceState
    {
        KeepDistanceMode Mode = KeepDistanceMode::Hold;
        bool bInitialized = false;
    };

    struct KeepDistanceParams
    {
        float MinDistance = 6.0f;
        float MaxDistance = 10.0f;
        float HysteresisFraction = 0.25f;
    };

    struct WanderState
    {
        uint32_t Rng = 0;
        float Angle = 0.0f;
        bool bInitialized = false;
    };

    struct WanderParams
    {
        float CircleDistance = 2.0f;
        float CircleRadius = 1.0f;
        float JitterRate = 3.0f;
        float SpeedFraction = 0.5f;
    };

    /// 全成分が有限かを返す。
    bool IsFiniteVector(const Math::Vector3& v);
    /// 非有限の成分を含むベクトルは零に戻す。
    Math::Vector3 SanitizeVector(const Math::Vector3& v);
    /// 成分を検査し、地上なら高さを除く。
    Math::Vector3 ToPlane(const Math::Vector3& v, SteeringPlane plane);
    /// XZ 平面での長さを返す。float の表現範囲を超えた長さは最大有限値に丸める。
    float PlanarLength(const Math::Vector3& v);
    /// 長さが 1e-6 未満または非有限なら、成分を検査した代替ベクトルを返す。
    Math::Vector3 SafeNormalize(const Math::Vector3& v, const Math::Vector3& fallback);
    /// 向きを保って長さを上限に収める。負または非有限の上限は零とする。
    Math::Vector3 ClampLength(const Math::Vector3& v, float maxLength);
    /// 非有限または負の上限を項目ごとの既定値に戻す。
    SteeringLimits SanitizeLimits(const SteeringLimits& limits);
    /// 速度比に対応する旋回率を隣り合うキーから線形補間する。
    float EvaluateTurnRate(const SteeringLimits& limits, float speed);

    /// 目標へ最大速で向かい、距離が 1e-4 m 未満なら停止する。
    Math::Vector3 Seek(const SteeringKinematics& self, const Math::Vector3& target,
                       const SteeringLimits& limits, SteeringPlane plane);
    /// 脅威から最大速で離れる。重なった場合は現在の進行方向か +Z に逃げる。
    Math::Vector3 Flee(const SteeringKinematics& self, const Math::Vector3& threat,
                       const SteeringLimits& limits, SteeringPlane plane);
    /// 停止半径までの残り距離と加速度から制動できる速さを求める。
    Math::Vector3 Arrive(const SteeringKinematics& self, const Math::Vector3& target,
                         const SteeringLimits& limits, const ArriveParams& params, SteeringPlane plane);
    /// 目標の等速移動を予測し、その位置へ到着するための速度を返す。
    Math::Vector3 Pursue(const SteeringKinematics& self, const SteeringKinematics& target,
                         const SteeringLimits& limits, const PursueParams& pursueParams,
                         const ArriveParams& arriveParams, SteeringPlane plane);
    /// 要求された上下端で水平距離を判定し、帯を保つ間は目標の速度に合わせる。
    /// 帯の中央へ戻るときは、停止半径をヒステリシスの余白を除いた帯の内側に収める。
    Math::Vector3 KeepDistance(const SteeringKinematics& self, const SteeringKinematics& target,
                               const SteeringLimits& limits, const KeepDistanceParams& params,
                               KeepDistanceState& state, SteeringPlane plane);
    /// 個体ごとの乱数列を初期化する。零の種には固定の非零値を使う。
    void InitializeWander(WanderState& state, uint32_t seed);
    /// 進行方向の前方に置いた円を使って徘徊する。正の有限な時間刻みでのみ乱数と角度を進める。
    Math::Vector3 Wander(const SteeringKinematics& self, const SteeringLimits& limits, const WanderParams& params,
                         WanderState& state, float dt, SteeringPlane plane);
    /// 射影して速さを上限に収めた現在速度から、1 歩の速度変化を MaxAcceleration × dt 以下に制限する。
    /// 現在と希望の水平速度がともに 1e-3 m/s を超えるとき、1 歩の水平方向の変化は旋回率 × dt 以下となる。
    Math::Vector3 ApplySteeringLimits(const Math::Vector3& currentVelocity, const Math::Vector3& desiredVelocity,
                                      const SteeringLimits& limits, float dt, SteeringPlane plane);

    struct PathFollowState
    {
        float S = 0.0f; // 始点から進んだ弧長（m）
        bool bHasHint = false;
    };

    struct PathFollowParams
    {
        float LookAhead = 1.5f;
        float MaxJump = 4.0f;
        float CornerLookAhead = 3.0f;
        float MinCornerSpeedScale = 0.3f;
        ArriveParams Arrive;
        float CornerTolerance = 0.2f; // 途中の頂点から先読みの弦までの許容距離（m）
        float MinLookAhead = 0.25f;   // 角で先読みを縮めるときの下限（m）
    };

    /// 折れ線の先を目指し、角で減速する。空なら進捗を保ち、零長・非有限の入力でも有限の速度を返す。
    /// 終点まで、進捗と目標の間の各頂点から先読みの弦までの距離を CornerTolerance 以内に収める。
    /// 半径付き Raycast で各線分の周囲に半径分の空きが確保された経路なら、許容距離を半径未満に取ると弦は閉塞セルに入らない。
    Math::Vector3 PathFollow(const SteeringKinematics& self, const Math::PolylinePath& path,
                             const SteeringLimits& limits, const PathFollowParams& params,
                             PathFollowState& state, SteeringPlane plane);
    /// 空の経路、または残りの弧長と終点までの水平距離がともに停止半径以内なら完了とする。
    bool IsPathFollowFinished(const SteeringKinematics& self, const Math::PolylinePath& path,
                              const PathFollowState& state, const ArriveParams& arrive);

    constexpr uint32_t kTrailCapacity = 64;
    constexpr float kTrailSampleInterval = 0.25f;

    struct TrailRing
    {
        Math::Vector3 Points[kTrailCapacity];
        uint32_t Head = 0; // 次に書き込む位置
        uint32_t Count = 0;
        float SinceLastSample = 0.0f;
    };

    struct FollowTrailParams
    {
        float FollowDistance = 2.0f;
        ArriveParams Arrive;
    };

    /// 先頭の個体の位置を一定間隔で最大一点記録する。非有限の位置は無視し、不正な時間刻みは零とする。
    void RecordTrail(TrailRing& ring, const Math::Vector3& leaderPosition, float dt);
    /// 最新点から指定の弧長だけさかのぼった記録点へ向かう。空・零長・非有限の入力でも有限の速度を返す。
    Math::Vector3 FollowTrail(const SteeringKinematics& self, const TrailRing& ring, const SteeringLimits& limits,
                              const FollowTrailParams& params, SteeringPlane plane);

    /// 水平半径内の有限な近傍から離れる。重なった個体は、有効な ID の組で決まる軸に沿って互いに逆向きに離れる。
    /// 近傍位置に対応する ID が揃わない場合や、どちらかの ID が零または両者が同じ場合は、近傍の添字で向きを決める。
    Math::Vector3 Separation(const SteeringKinematics& self, Core::Container::Span<const Math::Vector3> neighborPositions,
                             float radius, const SteeringLimits& limits, SteeringPlane plane,
                             Core::Container::Span<const uint64_t> neighborIds = {}, uint64_t selfId = 0);
    /// 有限な近傍速度の平均を速さの上限に収める。有効な近傍が空なら零を返す。
    Math::Vector3 Alignment(const SteeringKinematics& self, Core::Container::Span<const Math::Vector3> neighborVelocities,
                            const SteeringLimits& limits, SteeringPlane plane);
    /// 有限な近傍位置の重心へ向かう。有効な近傍が空なら零を返す。
    Math::Vector3 Cohesion(const SteeringKinematics& self, Core::Container::Span<const Math::Vector3> neighborPositions,
                           const SteeringLimits& limits, const ArriveParams& arrive, SteeringPlane plane);

    struct WeightedVelocity
    {
        Math::Vector3 Velocity;
        float Weight = 1.0f;
    };

    /// 希望速度の重み付き和を操舵面と速さの上限に収める。非有限の入力と負の重みの寄与は零とする。
    Math::Vector3 CombineWeighted(Core::Container::Span<const WeightedVelocity> items, const SteeringLimits& limits,
                                  SteeringPlane plane);

    constexpr uint32_t kContextDirectionCount = 16;

    struct ContextMaps
    {
        float Interest[kContextDirectionCount] = {};
        float Danger[kContextDirectionCount] = {};
    };

    struct ContextResolveParams
    {
        float DangerTolerance = 0.05f;
        float MinInterest = 1e-4f;
        bool bKeepExactWhenClear = true;
    };

    struct ContextResolveResult
    {
        Math::Vector3 Velocity;
        uint32_t Direction = 0;
        float Danger = 0.0f;
        bool bFallbackToOpen = false;
    };

    /// 添字を 16 で折り返し、+Z を起点に +X 側へ回る XZ 平面の単位方向を返す。
    Math::Vector3 ContextDirection(uint32_t index);
    /// 希望速度の水平成分から、最大速に対する速度比と各方向の正の内積の積を興味に重ねる。
    void AddInterest(ContextMaps& maps, const Math::Vector3& desiredVelocity, float maxSpeed);
    /// 与えられた先頭 16 方向まで、空き距離が先読み距離に足りない割合を危険に重ねる。
    /// NaN は閉塞、正の無限大は開放とし、先読み距離が非有限または正でなければ地図を保つ。
    void AddDangerFromClearance(ContextMaps& maps, Core::Container::Span<const float> clearance, float lookDistance);
    /// 半径内の近傍へ向かう方向に、距離と内積に応じた危険を重ねる。水平距離が 1e-4 m 未満の近傍は除く。
    void AddDangerFromNeighbors(ContextMaps& maps, const Math::Vector3& selfPosition,
                                Core::Container::Span<const Math::Vector3> neighborPositions, float radius);

    /// 非有限の興味を 0、非有限の危険を 1 として地図を [0, 1] に収め、希望水平速を maxSpeed で制限する。
    /// 最小危険値から DangerTolerance 以内を候補とする。全方向の危険が 1 なら方向 0・危険 1 で停止し、
    /// 希望水平速が 1e-4 m/s 以下でも停止する。候補の中で興味が最大の方向を選び、同点では小さい添字を優先する。
    /// その興味が MinInterest 以下なら、危険が最小の方向（同点では最小添字）を選び、bFallbackToOpen を立てる。
    /// bKeepExactWhenClear が真で、最小危険値が DangerTolerance 以下、希望方位を挟む両方向が候補であり、
    /// bFallbackToOpen が偽なら、希望の向きと制限後の速さを保つ。それ以外は選んだ向きで速さに (1 − 危険) を掛ける。
    /// 返す速度は有限で、Y 成分は常に 0 とする。
    ContextResolveResult ResolveContext(const ContextMaps& maps, const Math::Vector3& desiredVelocity, float maxSpeed,
                                        const ContextResolveParams& params);
}
