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

        // 索引の探索の開始位置（FNV の上位のビットを混ぜて、下位だけに偏らないようにする）
        size_t SlotStart(uint64_t hash)
        {
            return static_cast<size_t>(hash ^ (hash >> 32));
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
        // MegaGeometry の材質が持つ定数を、従来の未指定値も含めて保存する
        entry.Scalars[0] = material.Metallic;
        entry.Scalars[1] = material.Roughness;
        entry.Scalars[2] = material.HeightScale;
        entry.Scalars[3] = material.DisplacementUVSpacing;
        // MegaGeometry の定数 AO を予約語へ格納する。ハンドルの領域とは重ならない。
        std::memcpy(&entry.TexturesD[2], &material.OcclusionStrength, sizeof(float));
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
        m_Slots.resize(INITIAL_SLOT_COUNT, 0u);
    }

    void MaterialTable::Clear()
    {
        m_Entries.clear();
        m_EntryHashes.clear();
        m_Overflowed.clear();
        std::fill(m_Slots.begin(), m_Slots.end(), 0u);
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

    uint32_t MaterialTable::FindEncoded(uint64_t hash, const MaterialEntry& entry) const
    {
        // 枠は件数の 2 倍以上あるので、空きの枠に必ず行き当たる
        const size_t mask = m_Slots.size() - 1u;
        for (size_t slot = SlotStart(hash) & mask;; slot = (slot + 1u) & mask)
        {
            const uint32_t encoded = m_Slots[slot];
            if (encoded == 0u)
            {
                return 0u;
            }
            const bool bOverflowed = (encoded & OVERFLOW_BIT) != 0u;
            const size_t index = static_cast<size_t>((encoded & ~OVERFLOW_BIT) - 1u);
            const uint64_t storedHash = bOverflowed ? m_Overflowed[index].Hash : m_EntryHashes[index];
            const MaterialEntry& stored = bOverflowed ? m_Overflowed[index].Entry : m_Entries[index];
            if (storedHash == hash && SameBytes(stored, entry))
            {
                return encoded;
            }
        }
    }

    void MaterialTable::InsertEncoded(uint64_t hash, uint32_t encoded)
    {
        // 足した件はもう配列に入っているので、枠を広げるときの引き直しがその件も登録する
        if ((m_Entries.size() + m_Overflowed.size()) * 2u > m_Slots.size())
        {
            GrowSlots();
            return;
        }
        const size_t mask = m_Slots.size() - 1u;
        size_t slot = SlotStart(hash) & mask;
        while (m_Slots[slot] != 0u)
        {
            slot = (slot + 1u) & mask;
        }
        m_Slots[slot] = encoded;
    }

    void MaterialTable::GrowSlots()
    {
        m_Slots.assign(m_Slots.size() * 2u, 0u);
        const size_t mask = m_Slots.size() - 1u;
        const auto place = [&](uint64_t hash, uint32_t encoded) -> void
        {
            size_t slot = SlotStart(hash) & mask;
            while (m_Slots[slot] != 0u)
            {
                slot = (slot + 1u) & mask;
            }
            m_Slots[slot] = encoded;
        };
        for (size_t index = 0; index < m_Entries.size(); ++index)
        {
            place(m_EntryHashes[index], static_cast<uint32_t>(index + 1u));
        }
        for (size_t index = 0; index < m_Overflowed.size(); ++index)
        {
            place(m_Overflowed[index].Hash, static_cast<uint32_t>(index + 1u) | OVERFLOW_BIT);
        }
    }

    uint32_t MaterialTable::Add(const MaterialEntry& entry)
    {
        const uint64_t hash = HashEntry(entry);
        const uint32_t found = FindEncoded(hash, entry);
        if (found != 0u)
        {
            return (found & OVERFLOW_BIT) != 0u ? GetFallbackIndex() : found - 1u;
        }

        // 通常の材質は 0 から limit - 2 まで。最後の番号は溢れた材質の予備
        if (m_Entries.size() + 1u < m_Limit)
        {
            m_Entries.push_back(entry);
            m_EntryHashes.push_back(hash);
            InsertEncoded(hash, static_cast<uint32_t>(m_Entries.size()));
            return static_cast<uint32_t>(m_Entries.size() - 1u);
        }

        Stored stored;
        stored.Hash = hash;
        stored.Entry = entry;
        m_Overflowed.push_back(stored);
        InsertEncoded(hash, static_cast<uint32_t>(m_Overflowed.size()) | OVERFLOW_BIT);
        return GetFallbackIndex();
    }

    VariableArray<MaterialEntry> MaterialTable::BuildGpuEntries() const
    {
        VariableArray<MaterialEntry> result;
        BuildGpuEntriesInto(result);
        return result;
    }

    void MaterialTable::BuildGpuEntriesInto(VariableArray<MaterialEntry>& outEntries) const
    {
        outEntries.assign(m_Entries.begin(), m_Entries.end());
        if (HasOverflow())
        {
            // 予備の番号までを既定の材質で埋める（溢れた材質はこの値で解決する）
            outEntries.resize(m_Limit, MakeMaterialEntry(static_cast<const MaterialResourceData*>(nullptr)));
        }
    }

} // namespace NorvesLib::Core::Rendering::VisibilityBuffer
