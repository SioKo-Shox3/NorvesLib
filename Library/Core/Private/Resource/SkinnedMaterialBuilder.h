#pragma once

#include "Container/PointerTypes.h"
#include "Container/String.h"
#include "Container/VariableArray.h"
#include "Rendering/MaterialTypes.h"
#include "Thread/Thread.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core
{
    class SkeletalAssetResource;
    class SkinnedMeshResource;

    namespace Component
    {
        class SkinnedMeshComponent;
    }

    namespace Rendering
    {
        class RenderResources;
    }
}

namespace NorvesLib::Core::ResourceIO
{
    enum class SkinnedMaterialSlotStatus : uint8_t
    {
        Created,          // 材質を作った。
        InvalidMaterial,  // 材質値か論理パスが staging の検査を通らない。
        Unsupported,      // スキンの描画で表せない値。
        TextureFailed,    // texture の解決・検査・作成に失敗した。
        CreateFailed      // 材質を作れなかった。
    };

    struct SkinnedMaterialSlotResult
    {
        uint32_t Slot = 0;
        Container::String Name;
        SkinnedMaterialSlotStatus Status = SkinnedMaterialSlotStatus::InvalidMaterial;
        Container::AnsiString Reason;
        Rendering::MaterialCreateData CreateData;
        Rendering::MaterialHandle Material;
    };

    enum class SkinnedMaterialBuildStatus : uint8_t
    {
        Success,         // 全 slot を作った。
        PartialFailure,  // 1 つ以上の slot が失敗した。
        WrongThread,     // owner 以外の thread から呼ばれた。
        InvalidAsset,    // asset か mesh が無い。
        NotSplitMesh,    // 3 資産の経路の mesh でない。
        NotLoaded        // mesh が読み込み済みでない。
    };

    struct SkinnedMaterialBuildResult
    {
        SkinnedMaterialBuildStatus Status = SkinnedMaterialBuildStatus::InvalidAsset;
        uint64_t MeshResourceId = 0;
        uint64_t Generation = 0;
        Container::VariableArray<SkinnedMaterialSlotResult> Slots;
    };

    enum class SkinnedMaterialBindStatus : uint8_t
    {
        Bound,       // SetSlotMaterial が成功した。
        NotCreated,  // slot の材質が無い。
        Rejected     // SetSlotMaterial が拒否した。
    };

    struct SkinnedMaterialBindResult
    {
        bool bAccepted = false;
        uint32_t BoundCount = 0;
        Container::VariableArray<SkinnedMaterialBindStatus> Slots;
    };

    class SkinnedMaterialBuilder final
    {
    public:
        // resources は借用し、この builder より長く生かす。owner は GameThread の id。
        SkinnedMaterialBuilder(Rendering::RenderResources& resources, Thread::Thread::ThreadId owner);
        // owner thread で破棄し、材質を解放してから texture の所有を離す。
        ~SkinnedMaterialBuilder();
        SkinnedMaterialBuilder(const SkinnedMaterialBuilder&) = delete;
        SkinnedMaterialBuilder& operator=(const SkinnedMaterialBuilder&) = delete;

        // owner 専用。同じ mesh・世代では以前の結果を返す。
        [[nodiscard]] Container::TSharedPtr<const SkinnedMaterialBuildResult> Build(
            const Container::TSharedPtr<SkeletalAssetResource>& asset);
        // owner 専用。作成できた slot だけに材質を付ける。
        [[nodiscard]] SkinnedMaterialBindResult Bind(Component::SkinnedMeshComponent& component,
                                                     const SkinnedMaterialBuildResult& result) const;
        // owner 専用。失効した mesh・世代の項目を離し、項目数を返す。
        size_t Collect();
        // owner 専用。全ての項目を離す。
        void Clear();
        [[nodiscard]] size_t GetEntryCount() const noexcept;

    private:
        struct Entry;
        [[nodiscard]] bool IsOwnerThread() const;

        Rendering::RenderResources& m_Resources;
        Thread::Thread::ThreadId m_Owner;
        Container::VariableArray<Container::TUniquePtr<Entry>> m_Entries;
    };
} // namespace NorvesLib::Core::ResourceIO
