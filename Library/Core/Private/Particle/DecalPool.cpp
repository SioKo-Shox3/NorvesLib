/**
 * @file DecalPool.cpp
 * @brief デカールの固定枠の再利用、最古の追い出し、寿命と薄れの処理
 */

#include "Particle/DecalPool.h"

#include "Logging/LogMacros.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Particle
{
    namespace
    {
        template <size_t Count>
        bool IsFinite(const float (&values)[Count])
        {
            for (const float value : values)
            {
                if (!std::isfinite(value))
                {
                    return false;
                }
            }
            return true;
        }

        bool NormalizeTransform(Rendering::DecalInstance& instance, const float* position, const float* rotation)
        {
            if (position == nullptr || rotation == nullptr)
            {
                return false;
            }

            double lengthSquared = 0.0;
            for (uint32_t component = 0u; component < 4u; ++component)
            {
                if (!std::isfinite(rotation[component]))
                {
                    return false;
                }
                lengthSquared += static_cast<double>(rotation[component]) * rotation[component];
            }
            for (uint32_t component = 0u; component < 3u; ++component)
            {
                if (!std::isfinite(position[component]))
                {
                    return false;
                }
            }
            if (lengthSquared == 0.0)
            {
                return false;
            }

            const double inverseLength = 1.0 / std::sqrt(lengthSquared);
            for (uint32_t component = 0u; component < 4u; ++component)
            {
                instance.Rotation[component] = static_cast<float>(rotation[component] * inverseLength);
            }
            for (uint32_t component = 0u; component < 3u; ++component)
            {
                instance.Position[component] = position[component];
            }
            return true;
        }
    } // namespace

    DecalPool::DecalPool(uint32_t capacity)
    {
        capacity = capacity == 0u ? 1u : capacity;
        m_Slots.resize(capacity);
        m_FreeIndices.reserve(capacity);
        for (uint32_t index = capacity; index > 0u; --index)
        {
            m_FreeIndices.push_back(index - 1u);
        }
    }

    DecalHandle DecalPool::Add(const DecalDesc& desc)
    {
        DecalDesc normalized = desc;
        const Rendering::DecalInstance& instance = desc.Instance;
        if (!IsFinite(instance.HalfExtents) || !IsFinite(instance.Color) || !IsFinite(instance.LocalNormal) ||
            !std::isfinite(instance.NormalOpacity) || !std::isfinite(instance.EdgeFade) ||
            !std::isfinite(desc.LifetimeSeconds) || !std::isfinite(desc.FadeOutSeconds) ||
            instance.HalfExtents[0] <= 0.0f || instance.HalfExtents[1] <= 0.0f || instance.HalfExtents[2] <= 0.0f ||
            !NormalizeTransform(normalized.Instance, instance.Position, instance.Rotation))
        {
            NORVES_LOG_WARNING("DecalPool", "デカールの値が非有限、箱の半分の大きさが非正、または四元数がゼロのため追加できません");
            return {};
        }

        if (m_FreeIndices.empty())
        {
            uint32_t oldestIndex = 0u;
            for (uint32_t index = 1u; index < GetCapacity(); ++index)
            {
                if (m_Slots[index].AddOrder < m_Slots[oldestIndex].AddOrder)
                {
                    oldestIndex = index;
                }
            }
            Remove({oldestIndex, m_Slots[oldestIndex].Generation});
        }

        const uint32_t index = m_FreeIndices.back();
        m_FreeIndices.pop_back();
        DecalSlot& slot = m_Slots[index];
        slot.Desc = normalized;
        slot.AgeSeconds = 0.0;
        slot.AddOrder = m_NextAddOrder++;
        slot.bActive = true;
        return {index, slot.Generation};
    }

    bool DecalPool::Remove(DecalHandle handle)
    {
        if (!IsAlive(handle))
        {
            return false;
        }

        DecalSlot& slot = m_Slots[handle.Index];
        slot.bActive = false;
        ++slot.Generation;
        if (slot.Generation == 0u)
        {
            slot.Generation = 1u;
        }
        m_FreeIndices.push_back(handle.Index);
        return true;
    }

    bool DecalPool::IsAlive(DecalHandle handle) const
    {
        return handle.IsValid() && handle.Index < GetCapacity() && m_Slots[handle.Index].bActive &&
               m_Slots[handle.Index].Generation == handle.Generation;
    }

    bool DecalPool::SetTransform(DecalHandle handle, const float position[3], const float rotation[4])
    {
        return IsAlive(handle) && NormalizeTransform(m_Slots[handle.Index].Desc.Instance, position, rotation);
    }

    void DecalPool::Tick(float deltaSeconds)
    {
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0f)
        {
            deltaSeconds = 0.0f;
        }

        for (uint32_t index = 0u; index < GetCapacity(); ++index)
        {
            DecalSlot& slot = m_Slots[index];
            if (!slot.bActive)
            {
                continue;
            }
            slot.AgeSeconds += static_cast<double>(deltaSeconds);
            if (slot.Desc.LifetimeSeconds > 0.0f && slot.AgeSeconds >= slot.Desc.LifetimeSeconds)
            {
                Remove({index, slot.Generation});
            }
        }
    }

    uint32_t DecalPool::GetCount() const
    {
        return GetCapacity() - static_cast<uint32_t>(m_FreeIndices.size());
    }

    uint32_t DecalPool::GetCapacity() const
    {
        return static_cast<uint32_t>(m_Slots.size());
    }

    Rendering::DecalScene DecalPool::BuildScene() const
    {
        if (GetCount() == 0u)
        {
            return {};
        }

        Container::VariableArray<uint32_t> orderedIndices;
        orderedIndices.reserve(GetCount());
        for (uint32_t index = 0u; index < GetCapacity(); ++index)
        {
            if (m_Slots[index].bActive)
            {
                orderedIndices.push_back(index);
            }
        }
        std::sort(orderedIndices.begin(), orderedIndices.end(), [this](uint32_t left, uint32_t right)
        {
            return m_Slots[left].AddOrder < m_Slots[right].AddOrder;
        });

        auto decals = Container::MakeShared<Container::VariableArray<Rendering::DecalInstance>>();
        decals->reserve(GetCount());
        for (const uint32_t index : orderedIndices)
        {
            const DecalSlot& slot = m_Slots[index];
            Rendering::DecalInstance instance = slot.Desc.Instance;
            if (slot.Desc.LifetimeSeconds > 0.0f && slot.Desc.FadeOutSeconds > 0.0f)
            {
                const double remaining = static_cast<double>(slot.Desc.LifetimeSeconds) - slot.AgeSeconds;
                const float fade = static_cast<float>(std::clamp(remaining / slot.Desc.FadeOutSeconds, 0.0, 1.0));
                instance.Color[3] *= fade;
                instance.NormalOpacity *= fade;
            }
            decals->push_back(instance);
        }
        return {decals};
    }
} // namespace NorvesLib::Core::Particle
