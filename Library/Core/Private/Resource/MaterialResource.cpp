#include "Resource/MaterialResource.h"
#include "Object/Reflection.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core
{

    // ========================================
    // リフレクション実装
    // ========================================

    IMPLEMENT_CLASS(MaterialResource, Resource)

    // ========================================
    // コンストラクタ・デストラクタ
    // ========================================

    MaterialResource::MaterialResource()
        : Resource()
    {
    }

    MaterialResource::MaterialResource(const FieldInitializer *initializer)
        : Resource(initializer)
    {
    }

    MaterialResource::MaterialResource(const IUnknown *sourceObject)
        : Resource(sourceObject)
    {
    }

    MaterialResource::~MaterialResource()
    {
        Finalize();
    }

    // ========================================
    // ライフサイクル
    // ========================================

    void MaterialResource::Initialize()
    {
        Resource::Initialize();
    }

    void MaterialResource::Finalize()
    {
        Resource::Finalize();
    }

    // ========================================
    // Resource実装
    // ========================================

    bool MaterialResource::Load()
    {
        SetResourceState(ResourceState::Loaded);
        return true;
    }

    void MaterialResource::Unload()
    {
        m_AlbedoTexture = Rendering::TextureHandle::Invalid();
        m_NormalTexture = Rendering::TextureHandle::Invalid();
        m_MetallicTexture = Rendering::TextureHandle::Invalid();
        m_RoughnessTexture = Rendering::TextureHandle::Invalid();
        m_AOTexture = Rendering::TextureHandle::Invalid();

        m_EmissiveColor[0] = 0.0f;
        m_EmissiveColor[1] = 0.0f;
        m_EmissiveColor[2] = 0.0f;
        m_EmissiveStrength = 0.0f;

        SetResourceState(ResourceState::Unloaded);
    }

    size_t MaterialResource::GetMemorySize() const
    {
        // マテリアルリソース自体は小さい（テクスチャはハンドル参照のみ）
        return sizeof(MaterialResource);
    }

    // ========================================
    // エミッシブ
    // ========================================

    void MaterialResource::SetEmissiveColor(float r, float g, float b)
    {
        m_EmissiveColor[0] = r;
        m_EmissiveColor[1] = g;
        m_EmissiveColor[2] = b;
    }

    void MaterialResource::GetEmissiveColor(float &r, float &g, float &b) const
    {
        r = m_EmissiveColor[0];
        g = m_EmissiveColor[1];
        b = m_EmissiveColor[2];
    }

} // namespace NorvesLib::Core
