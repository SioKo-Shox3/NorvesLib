// JSON から表面の語彙を読み込み、名前と実行時の添字を対応付ける。
#pragma once

#include "Scene/SurfaceTypes.h"
#include "Container/StringView.h"
#include "Container/UnorderedSet.h"
#include "Container/VariableArray.h"
#include "Thread/Mutex.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Scene
{
    enum class ESurfaceRegistryError : uint8_t
    {
        None, ReadFailed, Empty, TooLarge, ParseError, InvalidRoot, UnsupportedVersion,
        UnknownKey, DuplicateKey, InvalidType, NonFiniteNumber, OutOfRange,
        InvalidName, DuplicateName, UnknownParent, CyclicParent, InvalidDefault, UnknownFlag, TooManySurfaces
    };

    struct SurfaceRegistryLoadResult
    {
        ESurfaceRegistryError Error = ESurfaceRegistryError::None;
        Container::String Detail;

        bool Succeeded() const { return Error == ESurfaceRegistryError::None; }
    };

    /**
     * @brief 表面の名前と記述を管理します。
     *
     * 読み込みは GameThread で行い、参照操作と同時に走らせないでください。
     * 参照操作どうしは同時に呼べます。返した参照は読み込み成功時または破棄時に失効します。
     * 物理の proxy などが保持する SurfaceId が再読み込みで別の表面を指さないよう、
     * 廃止した名前の添字も保持します。永続的な保存には添字ではなく名前を使ってください。
     */
    class SurfaceRegistry
    {
    public:
        static constexpr const char* DefaultSurfaceName = "Default";
        static constexpr uint32_t SupportedVersion = 1;
        static constexpr size_t MaximumTextBytes = 1024 * 1024;
        static constexpr size_t MaximumSurfaceCount = 1024;
        static constexpr size_t MaximumNameBytes = 64;

        SurfaceRegistry();

        SurfaceRegistryLoadResult LoadFromJson(const Container::String& json);
        SurfaceRegistryLoadResult LoadFromAssetFile(const Container::AnsiString& inputPath);

        SurfaceId FindByName(Container::StringView name) const;
        bool TryFindByName(Container::StringView name, SurfaceId& outId) const;
        const SurfaceDesc& GetDesc(SurfaceId id) const;
        const Container::String& GetName(SurfaceId id) const;

        /**
         * @brief 条件を満たす最初の表面を親へたどって返します。
         * 足音の組が Dirt にしかないとき、親が Dirt の Dirt_Mud は Dirt になります。
         */
        template <class Predicate>
        SurfaceId ResolveFallback(SurfaceId id, Predicate&& predicate) const
        {
            size_t index = id.Value;
            if (index >= m_Surfaces.size() || !m_Surfaces[index].bActive)
            {
                index = 0;
            }
            for (size_t visited = 0; visited < m_Surfaces.size(); ++visited)
            {
                if (predicate(m_Surfaces[index].Desc))
                {
                    return SurfaceId(static_cast<uint16_t>(index));
                }
                if (index == 0)
                {
                    return SurfaceId::Default();
                }
                index = m_Surfaces[index].Desc.Parent.Value;
                if (index >= m_Surfaces.size() || !m_Surfaces[index].bActive)
                {
                    index = 0;
                }
            }
            return SurfaceId::Default();
        }

        uint64_t GetRevision() const;
        size_t GetSurfaceCount() const;

    private:
        struct SurfaceEntry
        {
            SurfaceDesc Desc;
            bool bActive = false;
        };

        Container::VariableArray<SurfaceEntry> m_Surfaces;
        uint64_t m_Revision = 0;
        mutable Container::UnorderedSet<uint64_t> m_WarnedNames;
        mutable Thread::Mutex m_WarningMutex;
    };
} // namespace NorvesLib::Core::Scene
