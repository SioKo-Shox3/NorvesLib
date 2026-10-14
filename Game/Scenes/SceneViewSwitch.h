#pragma once

// ---------------------------------------------------------------------------
// 実行中に名前付き視点を切り替える依頼の窓口
//
// 1 回の起動で複数の視点を撮る撮影（--capture-views。Debug/MultiViewCapture.h）が依頼を出し、シーンの GameMode が
// Tick で受けて、カメラ（と視点ごとの画角・太陽など）を SceneCatalog.h の名前付き視点へ置き直す。
// 持ち主は GameApplicationHandler で、GameMode には非所有のポインタで渡す（無いなら nullptr）。
// GameThread だけが読み書きする。
//   撮影役 : Request で依頼し、GetState が Applied（置き直した）か Failed（置けない）になるのを待つ
//   GameMode : Tick の最初に IsRequested を見て、置き直したら Complete(true)、置けなければ Complete(false)
// ---------------------------------------------------------------------------

#include "Core/Public/Container/String.h"

#include <cstdint>

namespace Game::Scenes
{

    class SceneViewSwitch
    {
    public:
        enum class State : uint8_t
        {
            /// 依頼が無い
            Idle,
            /// 依頼を出し、GameMode が受けるのを待っている
            Requested,
            /// GameMode が視点を置き直した
            Applied,
            /// GameMode が視点を置けなかった（未知の視点など）
            Failed,
        };

        /// 視点 viewName への切り替えを依頼する（"default" は先頭の視点の別名）
        void Request(const NorvesLib::Core::Container::String &viewName)
        {
            m_ViewName = viewName;
            m_State = State::Requested;
        }

        /// 受けていない依頼があるか
        [[nodiscard]] bool IsRequested() const
        {
            return m_State == State::Requested;
        }

        /// 依頼された視点の名前
        [[nodiscard]] const NorvesLib::Core::Container::String &GetViewName() const
        {
            return m_ViewName;
        }

        /// GameMode が依頼を処理した結果を返す
        void Complete(bool bApplied)
        {
            m_State = bApplied ? State::Applied : State::Failed;
        }

        [[nodiscard]] State GetState() const
        {
            return m_State;
        }

    private:
        NorvesLib::Core::Container::String m_ViewName;
        State m_State = State::Idle;
    };

} // namespace Game::Scenes
