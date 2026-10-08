#pragma once
#include "Math/Transform.h"
namespace NorvesLib::Core::Scene
{
    // 物理姿勢へ書き戻さない、固定stepの前/現姿勢。Worldが捕捉と評価の順序を管理する。
    class RenderTransformHistory
    {
      public:
        void Clear()
        {
            m_bValid = false;
        }
        bool IsValid() const
        {
            return m_bValid;
        }
        bool Reset(const Math::Transform& transform);
        // step外変更を検出したら旧位置を横切らず、履歴を現在姿勢へ切り替える。
        bool Prepare(const Math::Transform& simulation);
        bool Capture(const Math::Transform& simulation);
        bool Evaluate(float alpha, Math::Transform& out) const;
        const Math::Transform& GetCurrent() const
        {
            return m_Current;
        }

      private:
        Math::Transform m_Previous, m_Current;
        bool m_bValid = false;
    };
} // namespace NorvesLib::Core::Scene
