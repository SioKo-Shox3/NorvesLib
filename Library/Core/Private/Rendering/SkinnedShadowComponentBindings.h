// 1つの点光源の1面だけで使うcomponent単位の記述子共有。RHIやallocatorに依存しない。
#pragma once
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    inline constexpr uint32_t SkinnedPointShadowComponentCapacity = 16;
    struct SkinnedShadowBindingKey
    {
        uint64_t ComponentId = 0;
        uint64_t Epoch = 0;
        uint64_t MeshId = 0;
        uint64_t Generation = 0;
        const void* Palette = nullptr;
        const void* Vertex = nullptr;
        bool operator==(const SkinnedShadowBindingKey&) const = default;
        bool IsValid() const
        {
            return MeshId != 0 && Generation != 0 && Palette && Vertex &&
                ((ComponentId == 0 && Epoch == 0) || (ComponentId != 0 && Epoch != 0));
        }
    };

    template<class Binding>
    class SkinnedShadowComponentBindings final
    {
    public:
        // createは新しいcomponentでだけ呼ぶ。失敗した枠も予約し、後続submeshで再試行しない。
        template<class Create>
        bool TryGet(const SkinnedShadowBindingKey& key, Create&& create, Binding& out)
        {
            out = {};
            if (!key.IsValid())
            {
                return false;
            }
            for (uint32_t index = 0; index < m_Count; ++index)
            {
                const auto& stored = m_Keys[index];
                const bool sameComponent = key.ComponentId != 0 ? stored.ComponentId == key.ComponentId
                    : stored.ComponentId == 0 && stored.Palette == key.Palette;
                if (sameComponent)
                {
                    if (!(stored == key))
                    {
                        return false;
                    }
                    out = m_Bindings[index];
                    return bool(out);
                }
            }
            if (m_Count == SkinnedPointShadowComponentCapacity)
            {
                return false;
            }
            const uint32_t index = m_Count++;
            m_Keys[index] = key;
            m_Bindings[index] = create();
            out = m_Bindings[index];
            return bool(out);
        }
        uint32_t Count() const { return m_Count; }
    private:
        SkinnedShadowBindingKey m_Keys[SkinnedPointShadowComponentCapacity]{};
        Binding m_Bindings[SkinnedPointShadowComponentCapacity]{};
        uint32_t m_Count = 0;
    };
}
