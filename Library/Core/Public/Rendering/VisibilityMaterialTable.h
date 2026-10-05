#pragma once

#include "Container/Containers.h"
#include "Rendering/MaterialTileClassifyPass.h"
#include "Rendering/MaterialTypes.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Rendering::VisibilityBuffer
{
    /**
     * @brief フレームごとの材質の表に入れられる材質の数（予備の番号を含む）
     *
     * 材質のタイルの分類（MaterialTileClassifyPass）が分けられる材質の数と同じ。表の番号がこれ以上の材質は
     * どのタイルにも入れないので、表もこの数に収める。
     */
    constexpr uint32_t MATERIAL_LIMIT = MaterialTiles::DEFAULT_MAX_MATERIALS;

    /** @brief MaterialEntry::Flags のビット（visbuffer_records.comp・材質の解決のシェーダーと一致） */
    constexpr uint32_t MATERIAL_FLAG_NORMAL_TWO_CHANNEL = 1u;
    constexpr uint32_t MATERIAL_FLAG_HAS_HEIGHT = 2u;
    /**
     * @brief MegaGeometry の区間の材質（MegaGeometryPass のラスタと同じ標本の規則で解決する印）
     *
     * MegaGeometryPass は等方の Linear のサンプラー（maxAnisotropy 指定なし）で標本し、粗さのテクスチャが無いときは白
     * （粗さ 1）を張る。手続き・スキニングの GBufferPass は異方性 4 のサンプラーと中間灰（粗さ 128/255）。材質の解決は、
     * この印のある件には等方のサンプラー・等方の LOD の式・白の既定の粗さを使う。
     */
    constexpr uint32_t MATERIAL_FLAG_MEGA_GEOMETRY = 4u;

    /**
     * @brief 材質の表の 1 件（storage buffer の 1 要素。128 バイト）
     *
     * 材質の解決が引く定数（基本色・発光・金属度と粗さのスカラー・高さの係数・種類の印）と、
     * テクスチャを引き直すための識別（テクスチャのハンドルの 64bit を下位・上位の 2 語で）を持つ。
     * 同じ値の材質は 1 件にまとまる。MegaGeometry の材質は MATERIAL_FLAG_MEGA_GEOMETRY の印を持つので、値が同じでも
     * 手続き・スキニングの材質とは別の件になる（標本の規則が違うため）。
     * パディングを持たない並びなので、まとめるかどうかはバイト列の一致で決める。
     */
    struct alignas(16) MaterialEntry
    {
        float BaseColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        /** @brief 発光の色度（xyz）と輝度（nits。w） */
        float Emissive[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        /** @brief x = 金属度のスカラー（負は未指定）、y = 粗さのスカラー（負は未指定）、z = 高さの係数、w = 変位した頂点の間隔 */
        float Scalars[4] = {-1.0f, -1.0f, 0.05f, 0.0f};
        /** @brief x = MATERIAL_FLAG_*、y = ShadingModel、z = BlendMode、w = 予約 */
        uint32_t Header[4] = {0u, 0u, 0u, 0u};
        /** @brief アルベド・法線（xy が下位・上位、zw が法線） */
        uint32_t TexturesA[4] = {0u, 0u, 0u, 0u};
        /** @brief 金属度・粗さ */
        uint32_t TexturesB[4] = {0u, 0u, 0u, 0u};
        /** @brief AO・ORM */
        uint32_t TexturesC[4] = {0u, 0u, 0u, 0u};
        /** @brief 高さ（xy）、残りは予約 */
        uint32_t TexturesD[4] = {0u, 0u, 0u, 0u};
    };

    static_assert(sizeof(MaterialEntry) == 128, "visbuffer_records.comp などの MaterialEntry と一致しなければならない");
    static_assert(alignof(MaterialEntry) == 16, "storage buffer の要素の配置は 16 バイト");
    static_assert(offsetof(MaterialEntry, Header) == 48 && offsetof(MaterialEntry, TexturesA) == 64 &&
                      offsetof(MaterialEntry, TexturesD) == 112,
                  "uvec4 の並びと一致しなければならない");

    /** @brief 手続き・スキニングの描画が持つ材質（MaterialResourceData。null なら材質の解決と同じ既定の値）から表の 1 件を作る */
    MaterialEntry MakeMaterialEntry(const MaterialResourceData* data);

    /** @brief MegaGeometry の区間の材質（値で持つ）から表の 1 件を作る（MATERIAL_FLAG_MEGA_GEOMETRY の印が立つ） */
    MaterialEntry MakeMaterialEntry(const MegaGeometry::MegaMeshMaterial& material);

    /** @brief 表の 1 件が持つテクスチャのハンドル（MakeMaterialEntry が詰めた枠を、同じ位置から読み出したもの） */
    struct MaterialTextureHandles
    {
        TextureHandle Albedo;
        TextureHandle Normal;
        TextureHandle Metallic;
        TextureHandle Roughness;
        TextureHandle AO;
        TextureHandle ORM;
        TextureHandle Height;
    };

    /** @brief 表の 1 件のテクスチャの枠（TexturesA〜D）から、各用途のハンドルを読み出す */
    MaterialTextureHandles ReadMaterialTextureHandles(const MaterialEntry& entry);

    /**
     * @brief フレームで一意な材質の表（CPU 側の積み上げ）
     *
     * Add が材質を渡された順に 0 から詰めた番号へ置く。同じ値の材質は同じ番号、違う材質は違う番号になる。
     * 材質の数が上限（limit）を超えるときは、表の最後の番号（limit - 1）を予備の番号にして、溢れた材質を寄せる。
     * 通常の材質が使える番号は 0 以上 limit - 1 未満。予備の番号の件は既定の材質の値で、溢れたときだけ表に入る。
     */
    class MaterialTable
    {
    public:
        explicit MaterialTable(uint32_t limit = MATERIAL_LIMIT);

        /** @brief 空にする（上限は変えない） */
        void Clear();

        /** @brief 材質を足して表の番号を返す。上限を超えて入らない材質は予備の番号を返す */
        uint32_t Add(const MaterialEntry& entry);

        /** @brief 表の件数の上限（予備の番号を含む） */
        uint32_t GetLimit() const { return m_Limit; }
        /** @brief 予備の番号（上限 - 1） */
        uint32_t GetFallbackIndex() const { return m_Limit - 1u; }
        /** @brief これまでに足した、値の違う材質の数（上限を超えても数える） */
        uint32_t GetUniqueCount() const { return static_cast<uint32_t>(m_Entries.size() + m_Overflowed.size()); }
        /** @brief 予備の番号へ寄せた、値の違う材質の数 */
        uint32_t GetOverflowedCount() const { return static_cast<uint32_t>(m_Overflowed.size()); }
        bool HasOverflow() const { return !m_Overflowed.empty(); }

        /**
         * @brief GPU へ上げる表（番号 0 から詰めた並び。溢れたときは予備の番号までを既定の値で埋めて返す）
         *
         * 返す件数は、溢れていなければ足した材質の数、溢れていれば上限。
         */
        Container::VariableArray<MaterialEntry> BuildGpuEntries() const;

    private:
        struct Stored
        {
            uint64_t Hash = 0;
            MaterialEntry Entry;
        };

        static uint64_t HashEntry(const MaterialEntry& entry);

        uint32_t m_Limit = MATERIAL_LIMIT;
        Container::VariableArray<MaterialEntry> m_Entries;
        Container::VariableArray<uint64_t> m_EntryHashes;
        Container::VariableArray<Stored> m_Overflowed;
    };

} // namespace NorvesLib::Core::Rendering::VisibilityBuffer
