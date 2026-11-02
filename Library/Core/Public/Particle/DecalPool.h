#pragma once

/**
 * @file DecalPool.h
 * @brief ゲーム側で追加順と寿命を管理する固定容量のデカールの置き場
 */

#include "Rendering/DecalTypes.h"

#include <cstdint>
#include <limits>

namespace NorvesLib::Core::Particle
{
    /// 枠と世代で、追い出されたデカールへの参照を区別する
    struct DecalHandle
    {
        uint32_t Index = std::numeric_limits<uint32_t>::max();
        uint32_t Generation = 0u;

        bool IsValid() const
        {
            return Index != std::numeric_limits<uint32_t>::max() && Generation != 0u;
        }

        bool operator==(const DecalHandle& other) const = default;
    };

    struct DecalDesc
    {
        Rendering::DecalInstance Instance;
        /// 寿命[s]。0 以下なら寿命なし
        float LifetimeSeconds = 0.0f;
        /// 寿命の終わりに薄れる区間[s]。0 以下なら薄れなし
        float FadeOutSeconds = 0.0f;
    };

    /// 単一のゲームスレッドから使う。満杯なら生きている最古の追加を追い出す
    class DecalPool
    {
    public:
        static constexpr uint32_t DefaultCapacity = 256u;

        explicit DecalPool(uint32_t capacity = DefaultCapacity);

        DecalHandle Add(const DecalDesc& desc);
        bool Remove(DecalHandle handle);
        bool IsAlive(DecalHandle handle) const;
        /// ワールド位置[m]と局所からワールドへの四元数（x, y, z, w）を更新する
        bool SetTransform(DecalHandle handle, const float position[3], const float rotation[4]);
        /// 呼び手の経過時間[s]で年齢を進める。負・非有限の値は 0 とする
        void Tick(float deltaSeconds);
        uint32_t GetCount() const;
        uint32_t GetCapacity() const;
        /// 生きているデカールを古い追加から並べ、寿命の薄れを反映した新しい一覧を返す
        Rendering::DecalScene BuildScene() const;

    private:
        struct DecalSlot
        {
            DecalDesc Desc;
            double AgeSeconds = 0.0;
            uint64_t AddOrder = 0u;
            uint32_t Generation = 1u;
            bool bActive = false;
        };

        Container::VariableArray<DecalSlot> m_Slots;
        Container::VariableArray<uint32_t> m_FreeIndices;
        uint64_t m_NextAddOrder = 0u;
    };
} // namespace NorvesLib::Core::Particle
