#include "Rendering/SharedResourceRegistry.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{

    // ========================================
    // テクスチャリソース
    // ========================================

    void SharedResourceRegistry::RegisterTexture(const Identity &name, RHI::ITexture *texture)
    {
        if (!name.IsValid())
        {
            NORVES_LOG_WARNING("SharedResourceRegistry", "無効なIdentity名でテクスチャを登録しようとしました");
            return;
        }
        m_Textures[name] = texture;
    }

    void SharedResourceRegistry::RegisterTexturePtr(const Identity &name, RHI::TexturePtr texture)
    {
        if (!name.IsValid())
        {
            NORVES_LOG_WARNING("SharedResourceRegistry", "無効なIdentity名でTexturePtrを登録しようとしました");
            return;
        }
        m_TexturePtrs[name] = texture;
        // 生ポインタ版にも同時登録（互換性のため）
        m_Textures[name] = texture.get();
    }

    RHI::ITexture *SharedResourceRegistry::GetTexture(const Identity &name) const
    {
        auto it = m_Textures.find(name);
        if (it != m_Textures.end())
        {
            return it->second;
        }
        return nullptr;
    }

    RHI::TexturePtr SharedResourceRegistry::GetTexturePtr(const Identity &name) const
    {
        auto it = m_TexturePtrs.find(name);
        if (it != m_TexturePtrs.end())
        {
            return it->second;
        }
        return nullptr;
    }

    bool SharedResourceRegistry::HasTexture(const Identity &name) const
    {
        return m_Textures.find(name) != m_Textures.end();
    }

    // ========================================
    // バッファリソース
    // ========================================

    void SharedResourceRegistry::RegisterBuffer(const Identity &name, RHI::IBuffer *buffer)
    {
        if (!name.IsValid())
        {
            NORVES_LOG_WARNING("SharedResourceRegistry", "無効なIdentity名でバッファを登録しようとしました");
            return;
        }
        m_Buffers[name] = buffer;
    }

    RHI::IBuffer *SharedResourceRegistry::GetBuffer(const Identity &name) const
    {
        auto it = m_Buffers.find(name);
        if (it != m_Buffers.end())
        {
            return it->second;
        }
        return nullptr;
    }

    // ========================================
    // フレーム管理
    // ========================================

    void SharedResourceRegistry::BeginFrame()
    {
        // フレームごとの一時リソースをクリア
        m_Textures.clear();
        m_TexturePtrs.clear();
        m_Buffers.clear();
    }

    void SharedResourceRegistry::Clear()
    {
        m_Textures.clear();
        m_TexturePtrs.clear();
        m_Buffers.clear();
    }

} // namespace NorvesLib::Core::Rendering
