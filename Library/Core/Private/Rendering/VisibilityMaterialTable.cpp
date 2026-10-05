#include "Rendering/VisibilityMaterialTable.h"

#include <algorithm>
#include <cstring>

namespace NorvesLib::Core::Rendering::VisibilityBuffer
{
    using namespace Container;

    namespace
    {
        void StoreHandle(uint32_t* words, uint64_t id)
        {
            words[0] = static_cast<uint32_t>(id & 0xFFFFFFFFull);
            words[1] = static_cast<uint32_t>(id >> 32);
        }

        // ハンドルの 64bit を、下位・上位の 2 語から戻す
        TextureHandle LoadHandle(const uint32_t* words)
        {
            TextureHandle handle;
            handle.Id = static_cast<uint64_t>(words[0]) | (static_cast<uint64_t>(words[1]) << 32);
            return handle;
        }

        bool SameBytes(const MaterialEntry& a, const MaterialEntry& b)
        {
            return std::memcmp(&a, &b, sizeof(MaterialEntry)) == 0;
        }
    } // namespace

    MaterialEntry MakeMaterialEntry(const MaterialResourceData* data)
    {
        MaterialEntry entry;
        if (!data)
        {
            return entry;
        }

        for (uint32_t channel = 0; channel < 4; ++channel)
        {
            entry.BaseColor[channel] = data->BaseColor[channel];
        }
        for (uint32_t channel = 0; channel < 3; ++channel)
        {
            entry.Emissive[channel] = data->EmissiveColor[channel];
        }
        entry.Emissive[3] = data->EmissiveLuminanceNits;
        entry.Scalars[0] = data->Metallic;
        entry.Scalars[1] = data->Roughness;
        entry.Scalars[2] = data->HeightScale;
        entry.Scalars[3] = 0.0f;
        entry.Header[0] = (data->bNormalTwoChannel ? MATERIAL_FLAG_NORMAL_TWO_CHANNEL : 0u) |
                          (data->HeightTexture.IsValid() ? MATERIAL_FLAG_HAS_HEIGHT : 0u);
        entry.Header[1] = static_cast<uint32_t>(data->Shading);
        entry.Header[2] = static_cast<uint32_t>(data->Blend);
        StoreHandle(&entry.TexturesA[0], data->AlbedoTexture.Id);
        StoreHandle(&entry.TexturesA[2], data->NormalTexture.Id);
        StoreHandle(&entry.TexturesB[0], data->MetallicTexture.Id);
        StoreHandle(&entry.TexturesB[2], data->RoughnessTexture.Id);
        StoreHandle(&entry.TexturesC[0], data->AOTexture.Id);
        StoreHandle(&entry.TexturesC[2], data->ORMTexture.Id);
        StoreHandle(&entry.TexturesD[0], data->HeightTexture.Id);
        return entry;
    }

    MaterialEntry MakeMaterialEntry(const MegaGeometry::MegaMeshMaterial& material)
    {
        MaterialEntry entry;
        for (uint32_t channel = 0; channel < 4; ++channel)
        {
            entry.BaseColor[channel] = material.BaseColor[channel];
        }
        for (uint32_t channel = 0; channel < 3; ++channel)
        {
            entry.Emissive[channel] = material.EmissiveColor[channel];
        }
        entry.Emissive[3] = material.EmissiveLuminanceNits;
        // MegaGeometry の材質は金属度・粗さのスカラーを持たない（未指定）
        entry.Scalars[0] = -1.0f;
        entry.Scalars[1] = -1.0f;
        entry.Scalars[2] = material.HeightScale;
        entry.Scalars[3] = material.DisplacementUVSpacing;
        entry.Header[0] = (material.bNormalTwoChannel ? MATERIAL_FLAG_NORMAL_TWO_CHANNEL : 0u) |
                          (material.bHasHeightMap ? MATERIAL_FLAG_HAS_HEIGHT : 0u) | MATERIAL_FLAG_MEGA_GEOMETRY;
        entry.Header[1] = static_cast<uint32_t>(ShadingModel::DefaultLit);
        entry.Header[2] = static_cast<uint32_t>(BlendMode::Opaque);
        StoreHandle(&entry.TexturesA[0], material.AlbedoTexture.Id);
        StoreHandle(&entry.TexturesA[2], material.NormalTexture.Id);
        StoreHandle(&entry.TexturesB[0], material.MetallicTexture.Id);
        StoreHandle(&entry.TexturesB[2], material.RoughnessTexture.Id);
        StoreHandle(&entry.TexturesC[0], material.AOTexture.Id);
        StoreHandle(&entry.TexturesC[2], material.ORMTexture.Id);
        StoreHandle(&entry.TexturesD[0], material.HeightTexture.Id);
        return entry;
    }

    MaterialTextureHandles ReadMaterialTextureHandles(const MaterialEntry& entry)
    {
        MaterialTextureHandles handles;
        handles.Albedo = LoadHandle(&entry.TexturesA[0]);
        handles.Normal = LoadHandle(&entry.TexturesA[2]);
        handles.Metallic = LoadHandle(&entry.TexturesB[0]);
        handles.Roughness = LoadHandle(&entry.TexturesB[2]);
        handles.AO = LoadHandle(&entry.TexturesC[0]);
        handles.ORM = LoadHandle(&entry.TexturesC[2]);
        handles.Height = LoadHandle(&entry.TexturesD[0]);
        return handles;
    }

    MaterialTable::MaterialTable(uint32_t limit)
        : m_Limit(std::max(1u, limit))
    {
    }

    void MaterialTable::Clear()
    {
        m_Entries.clear();
        m_EntryHashes.clear();
        m_Overflowed.clear();
    }

    uint64_t MaterialTable::HashEntry(const MaterialEntry& entry)
    {
        // FNV-1a（バイト列全体。パディングが無いので同じ値なら同じハッシュになる）
        const auto* bytes = reinterpret_cast<const unsigned char*>(&entry);
        uint64_t hash = 14695981039346656037ull;
        for (size_t index = 0; index < sizeof(MaterialEntry); ++index)
        {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    uint32_t MaterialTable::Add(const MaterialEntry& entry)
    {
        const uint64_t hash = HashEntry(entry);
        for (size_t index = 0; index < m_Entries.size(); ++index)
        {
            if (m_EntryHashes[index] == hash && SameBytes(m_Entries[index], entry))
            {
                return static_cast<uint32_t>(index);
            }
        }
        for (const Stored& stored : m_Overflowed)
        {
            if (stored.Hash == hash && SameBytes(stored.Entry, entry))
            {
                return GetFallbackIndex();
            }
        }

        // 通常の材質は 0 から limit - 2 まで。最後の番号は溢れた材質の予備
        if (m_Entries.size() + 1u < m_Limit)
        {
            m_Entries.push_back(entry);
            m_EntryHashes.push_back(hash);
            return static_cast<uint32_t>(m_Entries.size() - 1u);
        }

        Stored stored;
        stored.Hash = hash;
        stored.Entry = entry;
        m_Overflowed.push_back(stored);
        return GetFallbackIndex();
    }

    VariableArray<MaterialEntry> MaterialTable::BuildGpuEntries() const
    {
        VariableArray<MaterialEntry> result = m_Entries;
        if (HasOverflow())
        {
            // 予備の番号までを既定の材質で埋める（溢れた材質はこの値で解決する）
            result.resize(m_Limit, MakeMaterialEntry(static_cast<const MaterialResourceData*>(nullptr)));
        }
        return result;
    }

} // namespace NorvesLib::Core::Rendering::VisibilityBuffer
