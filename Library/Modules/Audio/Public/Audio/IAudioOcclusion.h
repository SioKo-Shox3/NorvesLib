// 声の遮蔽を追跡・更新する公開インターフェースと診断値を定義する。
#pragma once

#include "Audio/AudioTypes.h"
#include "Math/Vector3.h"
#include "Scene/AcousticTransmission.h"

#include <cstdint>

namespace NorvesLib::Modules::Audio
{
    // 遮蔽の更新器の計数。
    struct AudioOcclusionDiagnostics
    {
        uint32_t TrackedVoiceCount = 0;      // 追跡中の声の数。
        uint32_t EvaluatedLastFrame = 0;     // 直近の Tick で評価した声の数。
        uint64_t EvaluationCount = 0;        // 評価器を呼んだ回数の累計。
        uint64_t EvaluationFailureCount = 0; // 評価器が失敗を返した回数の累計。
        uint64_t AppliedCount = 0;           // SetVoiceAcoustics を呼んだ回数の累計。
        uint64_t PrunedVoiceCount = 0;       // 破棄・停止で自動的に外した声の累計。
    };

    // 呼び手が位置を与えた声の遮蔽を評価器で求め、予算つきで声の音量とローパスへ当てる口。GameThread 専用。
    class IAudioOcclusion
    {
    public:
        virtual ~IAudioOcclusion() = default;
        // 声を追跡に載せる。同じハンドルを再び渡すと位置・分類・優先度を更新する。優先度は大きいほど先に評価する。
        virtual AudioResult TrackVoiceAcoustics(VoiceHandle handle, const Math::Vector3& sourcePosition,
                                                Core::Scene::ESoundClass soundClass, uint8_t priority) = 0;
        // 追跡中の声の音源の位置を更新する。
        virtual AudioResult SetVoiceSourcePosition(VoiceHandle handle, const Math::Vector3& sourcePosition) = 0;
        // 声を追跡から外す。以後この声のバックエンドには触れない。
        virtual AudioResult UntrackVoice(VoiceHandle handle) = 0;
        // 明示したリスナー位置は、次に 1 声以上を評価する更新で使い、消費する。評価する声が無い更新では保留する。
        // 明示がない更新ではモジュールが解決したリスナー（GR73）の位置を使う。どちらもない間は評価しない。
        virtual void SetListenerPosition(const Math::Vector3& position) = 0;
        // まだ使っていない明示の位置を取り消す。
        virtual void ClearListenerPosition() = 0;
        // 追跡中の声に今当てている値を返す。まだ評価していない・追跡していないなら false。
        [[nodiscard]] virtual bool TryGetVoiceAcoustics(VoiceHandle handle, VoiceAcousticParams& outCurrent) const = 0;
        [[nodiscard]] virtual AudioOcclusionDiagnostics GetDiagnostics() const = 0;
    };
} // namespace NorvesLib::Modules::Audio
