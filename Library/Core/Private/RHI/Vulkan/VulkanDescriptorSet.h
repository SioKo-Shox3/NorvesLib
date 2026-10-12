#pragma once

#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include "Container/Containers.h"
#include "Thread/Atomic.h"
#include "Thread/Mutex.h"

namespace NorvesLib::RHI::Vulkan
{
    // 明示的なusing宣言（グローバル名前空間から参照）
    using ::NorvesLib::Core::Container::MakeShared;
    using ::NorvesLib::Core::Container::TSharedPtr;
    using ::NorvesLib::Core::Container::TWeakPtr;
    using ::NorvesLib::Core::Container::UnorderedMap;
    using ::NorvesLib::Core::Container::VariableArray;

    class VulkanDevice;
    class VulkanBuffer;
    class VulkanTexture;
    class VulkanSampler;
    class VulkanAccelerationStructure;

    /** @brief 1 bindingあたりの配列要素数の上限。device limitより十分小さく保つ。 */
    constexpr uint32_t MaxDescriptorBindingArrayCount = 1024u;

    /**
     * @brief RHIのbinding配列数を受理できるか判定する
     *
     * 配列（count>=2）はCombinedImageSamplerだけを対象にし、他の型は従来どおり1要素に限る。
     */
    inline bool IsSupportedDescriptorBindingCount(const DescriptorBinding &binding)
    {
        if (binding.count == 1u)
        {
            return true;
        }
        return binding.count > 1u &&
               binding.count <= MaxDescriptorBindingArrayCount &&
               binding.type == ResourceBindType::CombinedImageSampler;
    }

    /**
     * @brief Vulkanディスクリプタセットレイアウト (vulkan.hpp使用)
     */
    class VulkanDescriptorSetLayout
    {
    public:
        /**
         * @brief コンストラクタ
         * @param device Vulkanデバイス
         * @param bindings バインディング記述子のリスト
         */
        VulkanDescriptorSetLayout(
            TSharedPtr<VulkanDevice> device,
            const VariableArray<DescriptorBindingDesc> &bindings);

        /**
         * @brief デストラクタ
         */
        ~VulkanDescriptorSetLayout();

        vk::DescriptorSetLayout GetVkDescriptorSetLayout() const { return m_layout; }
        const VariableArray<DescriptorBindingDesc> &GetBindings() const { return m_bindings; }
        const VariableArray<vk::DescriptorSetLayoutBinding> &GetVkBindings() const { return m_vkBindings; }
        vk::DescriptorType ToVkDescriptorType(DescriptorType type) const;

        /**
         * @brief レイアウトのハンドルを今すぐ破棄の列へ渡す（デバイスの終了の前。この後のデストラクタは何もしない）
         */
        void ReleaseHandle();

        /**
         * @brief レイアウトのハンドルを破棄せずに手放す（デバイスの待機を確かめられない終了で、ハンドルを意図して残すとき。
         *        この後のデストラクタはデバイスに触らない）
         */
        void DetachHandle() noexcept { m_layout = nullptr; }

    private:
        TSharedPtr<VulkanDevice> m_device;
        VariableArray<DescriptorBindingDesc> m_bindings;
        VariableArray<vk::DescriptorSetLayoutBinding> m_vkBindings;
        vk::DescriptorSetLayout m_layout;

        vk::ShaderStageFlags ToVkShaderStageFlags(ShaderStage stage) const;
    };

    /**
     * @brief Vulkanディスクリプタプール (vulkan.hpp使用)
     */
    class VulkanDescriptorPool
    {
    public:
        /**
         * @brief コンストラクタ
         * @param device Vulkanデバイス
         * @param maxSets 最大セット数
         */
        VulkanDescriptorPool(TSharedPtr<VulkanDevice> device, uint32_t maxSets = 100);

        /**
         * @brief レイアウトの配列要素数を満たす容量でプールを作成する
         *
         * 型ごとの容量は従来の既定値と「全bindingの要素数合計×maxSets」の大きい方にする。
         * 配列を含まないレイアウトでは従来の容量と一致する。
         * @param device Vulkanデバイス
         * @param maxSets 最大セット数
         * @param bindings このプールから割り当てるレイアウトのbinding
         */
        VulkanDescriptorPool(TSharedPtr<VulkanDevice> device,
                             uint32_t maxSets,
                             const VariableArray<DescriptorBindingDesc> &bindings);

        /**
         * @brief デストラクタ
         */
        ~VulkanDescriptorPool();

        vk::DescriptorPool GetVkDescriptorPool() const { return m_pool; }
        void Reset();

    private:
        void CreatePool(uint32_t maxSets,
                        const VariableArray<DescriptorBindingDesc> *bindings);

        TSharedPtr<VulkanDevice> m_device;
        vk::DescriptorPool m_pool;
    };

    /** @brief フレームごとの pool が数える記述子の型（pool の容量と、レイアウトが求める数の添字） */
    enum class FrameDescriptorPoolType : uint32_t
    {
        UniformBuffer,
        SampledImage,
        Sampler,
        StorageBuffer,
        StorageImage,
        CombinedImageSampler,
        AccelerationStructure,
        Count
    };

    inline constexpr uint32_t FrameDescriptorPoolTypeCount = static_cast<uint32_t>(FrameDescriptorPoolType::Count);

    /**
     * @brief 共有のディスクリプタセットレイアウト（IDevice::GetOrCreateDescriptorSetLayout が返す）
     *
     * 同じ束縛の形にはデバイスの中で 1 つだけ作る。形の照合に使う並び（binding の順にそろえた
     * binding・型・ステージ・要素数）と、フレームごとの pool から借りるときに要る型ごとの数を持つ。
     */
    class VulkanSharedDescriptorSetLayout : public IDescriptorSetLayout
    {
    public:
        VulkanSharedDescriptorSetLayout(const DescriptorSetDesc &desc,
                                        TSharedPtr<VulkanDescriptorSetLayout> layout,
                                        VariableArray<uint32_t> signature);

        const DescriptorSetDesc &GetDesc() const override { return m_desc; }
        const TSharedPtr<VulkanDescriptorSetLayout> &GetLayout() const { return m_layout; }
        const VariableArray<uint32_t> &GetSignature() const { return m_signature; }

        /** @brief このレイアウトのセット 1 つが pool から取る、型ごとの記述子の数 */
        uint32_t GetRequiredCount(FrameDescriptorPoolType type) const
        {
            return m_requiredCounts[static_cast<uint32_t>(type)];
        }

    private:
        DescriptorSetDesc m_desc;
        TSharedPtr<VulkanDescriptorSetLayout> m_layout;
        VariableArray<uint32_t> m_signature;
        uint32_t m_requiredCounts[FrameDescriptorPoolTypeCount] = {};
    };

    /**
     * @brief フレームごとの pool と、そこから借りたセットが共有する状態
     *
     * 世代（Epoch）は pool を回収・手放すたびに増える。借りたセットは借りたときの世代を持ち、
     * 今の世代と違えば回収された後として書き込みも束縛も拒む。Mutex は pool からの確保・回収と、
     * 借りたセットの書き込みを直列にする（Vulkan は同じ pool の確保・書き込み・reset に外部の同期を求める）。
     */
    struct VulkanFrameDescriptorPoolState
    {
        Thread::Mutex Mutex;
        Thread::Atomic<uint64_t> Epoch{1};
        /** 前の回収から今までの統計（Mutex の下で読み書きする） */
        FrameDescriptorStats Stats;
    };

    /**
     * @brief Vulkanディスクリプタセット実装クラス (vulkan.hpp使用)
     */
    class VulkanDescriptorSet : public IDescriptorSet
    {
    public:
        /**
         * @brief VulkanDescriptorSetのコンストラクタ
         * @param device Vulkanデバイス
         * @param desc ディスクリプタセット記述子
         * @param layout ディスクリプタセットレイアウト
         * @param pool ディスクリプタプール
         */
        VulkanDescriptorSet(
            TSharedPtr<VulkanDevice> device,
            const DescriptorSetDesc &desc,
            TSharedPtr<VulkanDescriptorSetLayout> layout,
            TSharedPtr<VulkanDescriptorPool> pool);

        /**
         * @brief フレームごとの pool から借りたセットを包む（レイアウト・pool・パイプラインレイアウトは作らない）
         * @param device Vulkanデバイス
         * @param layout 共有のレイアウト
         * @param descriptorSet pool から確保したセット（pool の回収で一緒に戻る）
         * @param borrowState pool と共有する状態
         * @param borrowEpoch 借りたときの pool の世代
         */
        VulkanDescriptorSet(
            TSharedPtr<VulkanDevice> device,
            TSharedPtr<VulkanDescriptorSetLayout> layout,
            vk::DescriptorSet descriptorSet,
            TSharedPtr<VulkanFrameDescriptorPoolState> borrowState,
            uint64_t borrowEpoch);

        /**
         * @brief デストラクタ
         */
        ~VulkanDescriptorSet() override;

        // IDescriptorSetインターフェース実装
        void BindConstantBuffer(uint32_t binding, BufferPtr buffer, uint32_t offset, uint32_t size) override;
        void BindTexture(uint32_t binding, TexturePtr texture) override;
        void BindSampler(uint32_t binding, SamplerPtr sampler) override;
        void BindStorageBuffer(uint32_t binding, BufferPtr buffer, uint32_t offset, uint32_t size) override;
        bool BindAccelerationStructure(uint32_t binding,
                                       AccelerationStructurePtr accelerationStructure) override;
        bool BindTextureArrayElement(uint32_t binding,
                                     uint32_t arrayElement,
                                     TexturePtr texture,
                                     SamplerPtr sampler) override;
        void BindStorageTexture(uint32_t binding, TexturePtr texture) override;
        void BindStorageTexture(uint32_t binding, TexturePtr texture, uint32_t mipLevel) override;
        void Update() override;
        bool IsExpired() const override;

        // Vulkan固有のメソッド (vulkan.hpp型)
        vk::DescriptorSet GetVkDescriptorSet() const { return m_descriptorSet; }
        vk::DescriptorSetLayout GetVkDescriptorSetLayout() const;
        vk::PipelineLayout GetVkPipelineLayout() const;

        /** @brief フレームごとの pool から借りたセットか */
        bool IsBorrowed() const { return static_cast<bool>(m_borrowState); }

    private:
        TSharedPtr<VulkanDevice> m_device;
        DescriptorSetDesc m_desc;
        TSharedPtr<VulkanDescriptorSetLayout> m_layout;
        TSharedPtr<VulkanDescriptorPool> m_pool;
        // 借りたセットだけ持つ（専用のセットは空）
        TSharedPtr<VulkanFrameDescriptorPoolState> m_borrowState;
        uint64_t m_borrowEpoch = 0;

        vk::DescriptorSet m_descriptorSet;
        vk::PipelineLayout m_pipelineLayout;

        bool m_bNeedsUpdate = false;

        struct BindingInfo
        {
            enum class ResourceType
            {
                Buffer,
                Texture,
                Sampler,
                AccelerationStructure
            };

            ResourceType type;
            BufferPtr buffer;
            TexturePtr texture;
            SamplerPtr sampler;
            AccelerationStructurePtr accelerationStructure;
            uint64_t offset = 0;
            uint64_t range = VK_WHOLE_SIZE;
            int32_t mipLevel = -1; // -1 = 全ミップ（デフォルト）、0+ = 特定ミップ
        };

        UnorderedMap<uint32_t, BindingInfo> m_bindings;

        /** @brief 配列bindingの要素。キーは (binding << 32) | 要素番号。 */
        UnorderedMap<uint64_t, BindingInfo> m_arrayElementBindings;

        void CreatePipelineLayout();
        vk::DescriptorType GetVkDescriptorType(uint32_t binding) const;
        uint32_t GetDescriptorCount(uint32_t binding) const;
        /** @brief 束縛を記述子へ書き込み、書いた記述子の数を返す */
        uint32_t WriteBindings();
    };

    /**
     * @brief フレームごとにまとめた記述子の pool（IFrameDescriptorAllocator の Vulkan 実装）
     *
     * 型ごとの容量を持つ pool の列から前へ進みながら確保し、残りの容量が足りなければ次の pool、
     * 無ければ pool を足す（残りは自分で数えるので、確保の失敗に頼らない）。Reset で全部の pool を
     * vkResetDescriptorPool で戻し、先頭から使い回す。
     */
    class VulkanFrameDescriptorAllocator : public IFrameDescriptorAllocator
    {
    public:
        VulkanFrameDescriptorAllocator(TSharedPtr<VulkanDevice> device, const FrameDescriptorPoolCapacity &capacity);
        ~VulkanFrameDescriptorAllocator() override;

        DescriptorSetPtr Allocate(const DescriptorSetLayoutPtr &layout) override;
        void Reset() override;
        void Release() override;
        FrameDescriptorStats GetFrameStats() const override;
        uint32_t GetPoolCount() const override;
        uint64_t GetTotalPoolsAdded() const override;

    private:
        struct PoolEntry
        {
            vk::DescriptorPool Pool;
            uint32_t Capacity[FrameDescriptorPoolTypeCount] = {};
            uint32_t Remaining[FrameDescriptorPoolTypeCount] = {};
            uint32_t MaxSets = 0;
            uint32_t RemainingSets = 0;
        };

        static bool Fits(const PoolEntry &pool, const VulkanSharedDescriptorSetLayout &layout);
        bool AddPoolLocked(const VulkanSharedDescriptorSetLayout &layout);
        void ReleasePoolsLocked();

        TSharedPtr<VulkanDevice> m_device;
        FrameDescriptorPoolCapacity m_capacity;
        TSharedPtr<VulkanFrameDescriptorPoolState> m_state;
        VariableArray<PoolEntry> m_pools;
        size_t m_currentPool = 0;
        uint64_t m_totalPoolsAdded = 0;
    };

} // namespace NorvesLib::RHI::Vulkan
