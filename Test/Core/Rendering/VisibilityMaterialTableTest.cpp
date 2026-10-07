// ビジビリティバッファの材質の表の契約テスト（GPU を使わない）。
// 記録の材質の番号がフレームで一意な表の番号になること:
// 同じ材質の手続きメッシュ・スキニングの描画が同じ番号に、
// 違う材質が違う番号になること、MegaGeometry
// の区間の材質は値が同じでも別の件（標本の規則を選ぶ印）になること、 番号が 0
// から詰まっていること、上限を超えたときに予備の番号へ寄せること、表の件のテクスチャの枠が用途どおりの位置から
// 読めること、MegaGeometry
// の記録を作る計算シェーダーが区間の番号ではなく表の番号を書くことを確かめる。
#include "Rendering/VisibilityMaterialTable.h"
#include "Rendering/VisibilityResolveMaterialBuild.h"
#include "Rendering/MaterialTypes.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>

namespace NorvesLib
{
namespace
{

using namespace Core::Rendering;
namespace VB = Core::Rendering::VisibilityBuffer;
namespace Container = Core::Container;

int g_failures = 0;

void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "VisibilityMaterialTableTest 失敗: " << message << std::endl;
        ++g_failures;
    }
}

// 材質の解決が引く実物の材質（手続きメッシュ・スキニングの描画が持つもの）。seed ごとに値が違う
MaterialResourceData MakeResourceMaterial(uint32_t seed)
{
    MaterialResourceData data;
    data.BaseColor[0] = 0.1f * static_cast<float>(seed + 1);
    data.AlbedoTexture.Id = 100 + seed;
    data.NormalTexture.Id = 200 + seed;
    data.Metallic = 0.25f;
    data.Roughness = 0.5f;
    return data;
}

// MegaGeometry の区間の材質（値で持つ）。MakeResourceMaterial と同じ seed なら、同じ値になるようにそろえる
MegaGeometry::MegaMeshMaterial MakeMegaMaterialLike(const MaterialResourceData& data)
{
    MegaGeometry::MegaMeshMaterial material;
    for (uint32_t channel = 0; channel < 4; ++channel)
    {
        material.BaseColor[channel] = data.BaseColor[channel];
    }
    for (uint32_t channel = 0; channel < 3; ++channel)
    {
        material.EmissiveColor[channel] = data.EmissiveColor[channel];
    }
    material.EmissiveLuminanceNits = data.EmissiveLuminanceNits;
    material.AlbedoTexture = data.AlbedoTexture;
    material.NormalTexture = data.NormalTexture;
    material.HeightScale = data.HeightScale;
    return material;
}

void TestSameMaterialSharesIndexAcrossDrawKinds()
{
    VB::MaterialTable table;

    // 手続きメッシュの描画 2 つ（別の塊・別のインスタンスでも、同じ材質）とスキニングの描画が同じ材質を使う
    MaterialResourceData shared = MakeResourceMaterial(0);
    // MegaGeometry の材質は金属度・粗さのスカラーを持たないので、同じ値にそろえる
    shared.Metallic = -1.0f;
    shared.Roughness = -1.0f;

    const uint32_t procedural0 = table.Add(VB::MakeMaterialEntry(&shared));
    const uint32_t procedural1 = table.Add(VB::MakeMaterialEntry(&shared));
    const uint32_t skinned = table.Add(VB::MakeMaterialEntry(&shared));
    Expect(procedural0 == procedural1 && procedural1 == skinned, "同じ材質を使う手続き・スキニングの描画は同じ番号になる");
    Expect(table.GetUniqueCount() == 1, "同じ材質だけなら表は 1 件");
    Expect(table.BuildGpuEntries().size() == 1, "GPU へ上げる表も 1 件");

    // 別の MaterialHandle でも値が同じなら同じ材質（実物の値で引くため）。値が違えば別の材質
    const MaterialResourceData sameValue = shared;
    Expect(table.Add(VB::MakeMaterialEntry(&sameValue)) == procedural0, "値が同じ材質は同じ番号");
    const MaterialResourceData other = MakeResourceMaterial(1);
    Expect(table.Add(VB::MakeMaterialEntry(&other)) != procedural0, "値の違う材質は別の番号");
}

        void TestImportedScalarValues()
        {
            MegaGeometry::MegaMeshMaterial material;
            material.Metallic = 0.25f;
            material.Roughness = 0.75f;
            material.OcclusionStrength = 0.6f;
            const auto entry = VB::MakeMaterialEntry(material);
            float ao = 0;
            std::memcpy(&ao, &entry.TexturesD[2], sizeof(ao));
            Expect(entry.Scalars[0] == 0.25f && entry.Scalars[1] == 0.75f && ao == 0.6f,
                   "取り込んだ金属度・粗さ・AO の定数を保持する");
            VB::MaterialTable table;
        material.MetallicTexture.Id = 987;
        material.RoughnessTexture.Id = 988;
        const auto resolved = VisibilityResolveGeometry::MakeResolveMaterial(nullptr, VB::MakeMaterialEntry(material), {});
        Expect(resolved.MetallicConstant == 0.25f && resolved.RoughnessConstant == 0.75f && resolved.AOConstant == 0.6f,
               "解決できないハンドルではラスタと同じ材質定数へ戻る");
            const auto first = table.Add(entry);
            material.OcclusionStrength = 1;
            Expect(first != table.Add(VB::MakeMaterialEntry(material)), "AO の異なる材質を同一視しない");
        }

void TestMegaGeometryMaterialIsSeparateEntry()
{
    // MegaGeometry の区間の材質は、MegaGeometryPass のラスタ（等方の Linear のサンプラー・粗さの既定は白）と同じ規則で
    // 解決する印を持つ。値が手続き・スキニングの材質と同じでも、標本の規則が違うので別の件になる
    MaterialResourceData shared = MakeResourceMaterial(0);
    shared.Metallic = -1.0f;
    shared.Roughness = -1.0f;
    const MegaGeometry::MegaMeshMaterial megaMaterial = MakeMegaMaterialLike(shared);

    const VB::MaterialEntry megaEntry = VB::MakeMaterialEntry(megaMaterial);
    const VB::MaterialEntry proceduralEntry = VB::MakeMaterialEntry(&shared);
    Expect((megaEntry.Header[0] & VB::MATERIAL_FLAG_MEGA_GEOMETRY) != 0u, "MegaGeometry の材質の件には印が立つ");
    Expect((proceduralEntry.Header[0] & VB::MATERIAL_FLAG_MEGA_GEOMETRY) == 0u, "手続き・スキニングの材質の件には印が立たない");
    Expect((VB::MakeMaterialEntry(static_cast<const MaterialResourceData*>(nullptr)).Header[0] & VB::MATERIAL_FLAG_MEGA_GEOMETRY) == 0u,
           "解決できない材質（既定の件）には印が立たない");
    Expect(VB::MATERIAL_FLAG_MEGA_GEOMETRY != VB::MATERIAL_FLAG_NORMAL_TWO_CHANNEL &&
               VB::MATERIAL_FLAG_MEGA_GEOMETRY != VB::MATERIAL_FLAG_HAS_HEIGHT,
           "印は他の旗と重ならない");

    VB::MaterialTable table;
    const uint32_t mega0 = table.Add(megaEntry);
    const uint32_t procedural = table.Add(proceduralEntry);
    const uint32_t mega1 = table.Add(VB::MakeMaterialEntry(megaMaterial));
    Expect(mega0 != procedural, "値が同じでも MegaGeometry の材質は手続き・スキニングの材質と別の番号");
    Expect(mega0 == mega1, "MegaGeometry の材質同士は値が同じなら同じ番号");
    Expect(table.GetUniqueCount() == 2, "MegaGeometry の材質と手続きの材質で表は 2 件");

    // 法線・高さの旗は印と共存する（旗のビットを取り違えない）
    MegaGeometry::MegaMeshMaterial full = megaMaterial;
    full.bNormalTwoChannel = true;
    full.bHasHeightMap = true;
    const uint32_t fullFlags = VB::MakeMaterialEntry(full).Header[0];
    Expect(fullFlags == (VB::MATERIAL_FLAG_NORMAL_TWO_CHANNEL | VB::MATERIAL_FLAG_HAS_HEIGHT | VB::MATERIAL_FLAG_MEGA_GEOMETRY),
           "法線・高さの旗と MegaGeometry の印が同じ語に共存する");
}

// 表の件のテクスチャの枠（TexturesA〜D）から、用途ごとのハンドルを取り違えずに読めること。材質の解決は、読んだハンドルから
// 材質ごとの dispatch のテクスチャ（アルベド・法線・金属度・粗さ・AO・ORM・高さ）を引く。枠をずらすと別の用途のテクスチャを張る
void TestTextureSlotsRoundTrip()
{
    // 上位の語にも値が入る 64bit のハンドルにして、下位・上位の取り違えも検出する
    const uint64_t high = 0x1234ull << 32;
    auto check = [high](const VB::MaterialEntry& entry, const char* label) {
        const VB::MaterialTextureHandles handles = VB::ReadMaterialTextureHandles(entry);
        const bool bOk = handles.Albedo.Id == (high | 11ull) && handles.Normal.Id == (high | 22ull) &&
                         handles.Metallic.Id == (high | 33ull) && handles.Roughness.Id == (high | 44ull) &&
                         handles.AO.Id == (high | 55ull) && handles.ORM.Id == (high | 66ull) && handles.Height.Id == (high | 77ull);
        if (!bOk)
        {
            std::cerr << "VisibilityMaterialTableTest " << label << ": アルベド=" << handles.Albedo.Id << " 法線=" << handles.Normal.Id
                      << " 金属度=" << handles.Metallic.Id << " 粗さ=" << handles.Roughness.Id << " AO=" << handles.AO.Id
                      << " ORM=" << handles.ORM.Id << " 高さ=" << handles.Height.Id << std::endl;
        }
        Expect(bOk, label);
    };

    MaterialResourceData data;
    data.AlbedoTexture.Id = high | 11ull;
    data.NormalTexture.Id = high | 22ull;
    data.MetallicTexture.Id = high | 33ull;
    data.RoughnessTexture.Id = high | 44ull;
    data.AOTexture.Id = high | 55ull;
    data.ORMTexture.Id = high | 66ull;
    data.HeightTexture.Id = high | 77ull;
            check(VB::MakeMaterialEntry(&data), "手続き・スキニングの材質の表の件から、用"
                                                "途ごとのテクスチャのハンドルを読める");

    MegaGeometry::MegaMeshMaterial mega;
    mega.AlbedoTexture = data.AlbedoTexture;
    mega.NormalTexture = data.NormalTexture;
    mega.MetallicTexture = data.MetallicTexture;
    mega.RoughnessTexture = data.RoughnessTexture;
    mega.AOTexture = data.AOTexture;
    mega.ORMTexture = data.ORMTexture;
    mega.HeightTexture = data.HeightTexture;
            check(VB::MakeMaterialEntry(mega), "MegaGeometry "
                                               "の材質の表の件から、用途ごとのテクスチャのハンドルを読める");

    // 指定の無い枠は無効なハンドルのまま（隣の枠のハンドルが漏れない）
    MaterialResourceData onlyRoughness;
    onlyRoughness.RoughnessTexture.Id = high | 44ull;
    const VB::MaterialTextureHandles sparse = VB::ReadMaterialTextureHandles(VB::MakeMaterialEntry(&onlyRoughness));
    Expect(sparse.Roughness.Id == (high | 44ull) && !sparse.Albedo.IsValid() && !sparse.Normal.IsValid() && !sparse.Metallic.IsValid() &&
               !sparse.AO.IsValid() && !sparse.ORM.IsValid() && !sparse.Height.IsValid(),
           "粗さだけ指定した件は、粗さの枠だけが有効で、他の枠は無効のまま");
}

void TestDifferentMaterialsGetDifferentIndices()
{
    const MaterialResourceData baseline = MakeResourceMaterial(0);
    VB::MaterialTable table;
    const uint32_t baselineIndex = table.Add(VB::MakeMaterialEntry(&baseline));

    // 材質の解決が引く値を 1 つずつ変えると、そのたびに別の材質になる
    uint32_t expectedUnique = 1;
    const auto expectDistinct = [&](const MaterialResourceData& variant, const char* what)
    {
        const uint32_t index = table.Add(VB::MakeMaterialEntry(&variant));
        ++expectedUnique;
        Expect(index != baselineIndex, what);
        Expect(table.GetUniqueCount() == expectedUnique, what);
    };

    MaterialResourceData variant = baseline;
    variant.BaseColor[3] = 0.5f;
    expectDistinct(variant, "基本色が違えば別の材質");
    variant = baseline;
    variant.EmissiveLuminanceNits = 10.0f;
    expectDistinct(variant, "発光が違えば別の材質");
    variant = baseline;
    variant.Roughness = 0.9f;
    expectDistinct(variant, "粗さのスカラーが違えば別の材質");
    variant = baseline;
    variant.bNormalTwoChannel = true;
    expectDistinct(variant, "法線の形式が違えば別の材質");
    variant = baseline;
    variant.Shading = ShadingModel::ClearCoat;
    expectDistinct(variant, "材質の種類が違えば別の材質");
    variant = baseline;
    variant.AlbedoTexture.Id = baseline.AlbedoTexture.Id + 1;
    expectDistinct(variant, "アルベドのテクスチャが違えば別の材質");
    // 64bit のハンドルは上位の語だけが違っても別の材質（識別を切り詰めない）
    variant = baseline;
    variant.NormalTexture.Id = baseline.NormalTexture.Id | (1ull << 40);
    expectDistinct(variant, "テクスチャのハンドルの上位の語だけが違っても別の材質");
    variant = baseline;
    variant.HeightTexture.Id = 9;
    expectDistinct(variant, "高さのテクスチャがあれば別の材質");

    // 材質を解決できない描画（null）は、すべて既定の 1 件にまとまる
    const uint32_t unresolved0 = table.Add(VB::MakeMaterialEntry(static_cast<const MaterialResourceData*>(nullptr)));
    const uint32_t unresolved1 = table.Add(VB::MakeMaterialEntry(static_cast<const MaterialResourceData*>(nullptr)));
    Expect(unresolved0 == unresolved1 && unresolved0 != baselineIndex, "解決できない材質は既定の 1 件にまとまる");
}

void TestIndicesAreDenseInFirstSeenOrder()
{
    VB::MaterialTable table;
    constexpr uint32_t Count = 40;
    for (uint32_t pass = 0; pass < 3; ++pass)
    {
        // 同じ材質を何度足しても、最初に見た順の番号のまま
        for (uint32_t seed = 0; seed < Count; ++seed)
        {
            const MaterialResourceData data = MakeResourceMaterial(seed);
            Expect(table.Add(VB::MakeMaterialEntry(&data)) == seed, "番号は最初に見た順に 0 から詰まる");
        }
    }
    Expect(table.GetUniqueCount() == Count && !table.HasOverflow(), "上限以下なら溢れない");

    const Container::VariableArray<VB::MaterialEntry> entries = table.BuildGpuEntries();
    Expect(entries.size() == Count, "表の件数は材質の数と同じ（隙間が無い）");
    for (uint32_t seed = 0; seed < Count && seed < entries.size(); ++seed)
    {
        const MaterialResourceData data = MakeResourceMaterial(seed);
        const VB::MaterialEntry expected = VB::MakeMaterialEntry(&data);
        Expect(std::memcmp(&entries[seed], &expected, sizeof(VB::MaterialEntry)) == 0,
               "表の添え字の件が、その番号で足した材質の値");
    }

    table.Clear();
    Expect(table.GetUniqueCount() == 0 && table.BuildGpuEntries().empty(), "Clear で空に戻る");
    const MaterialResourceData first = MakeResourceMaterial(7);
    Expect(table.Add(VB::MakeMaterialEntry(&first)) == 0, "Clear の後は 0 から詰め直す");
}

void TestOverflowFoldsIntoReservedIndex()
{
    constexpr uint32_t Limit = 4;
    VB::MaterialTable table(Limit);
    Expect(table.GetLimit() == Limit && table.GetFallbackIndex() == Limit - 1, "予備の番号は上限 - 1");

    const MaterialResourceData a = MakeResourceMaterial(0);
    const MaterialResourceData b = MakeResourceMaterial(1);
    const MaterialResourceData c = MakeResourceMaterial(2);
    const MaterialResourceData d = MakeResourceMaterial(3);
    const MaterialResourceData e = MakeResourceMaterial(4);

    Expect(table.Add(VB::MakeMaterialEntry(&a)) == 0, "a は 0 番");
    Expect(table.Add(VB::MakeMaterialEntry(&b)) == 1, "b は 1 番");
    Expect(table.Add(VB::MakeMaterialEntry(&c)) == 2, "c は 2 番");
    Expect(!table.HasOverflow(), "上限 - 1 件までは溢れない");

    // 4 件目からは予備の番号へ寄せる（通常の材質は予備の番号を使わない）
    Expect(table.Add(VB::MakeMaterialEntry(&d)) == Limit - 1, "溢れた d は予備の番号");
    Expect(table.Add(VB::MakeMaterialEntry(&e)) == Limit - 1, "溢れた e も予備の番号");
    Expect(table.Add(VB::MakeMaterialEntry(&d)) == Limit - 1, "溢れた材質を再び足しても予備の番号で、数は増えない");
    Expect(table.HasOverflow() && table.GetOverflowedCount() == 2, "溢れた材質は値の違う 2 件");
    Expect(table.GetUniqueCount() == 5, "値の違う材質の数は上限を超えても数える");

    // 溢れる前に入った材質の番号は変わらない
    Expect(table.Add(VB::MakeMaterialEntry(&a)) == 0 && table.Add(VB::MakeMaterialEntry(&c)) == 2,
           "入っていた材質の番号は溢れても変わらない");

    const Container::VariableArray<VB::MaterialEntry> entries = table.BuildGpuEntries();
    Expect(entries.size() == Limit, "溢れたときの表は予備の番号までの件数");
    if (entries.size() == Limit)
    {
        const VB::MaterialEntry fallback = VB::MakeMaterialEntry(static_cast<const MaterialResourceData*>(nullptr));
        Expect(std::memcmp(&entries[Limit - 1], &fallback, sizeof(VB::MaterialEntry)) == 0,
               "予備の番号の件は既定の材質の値");
        const VB::MaterialEntry expectedC = VB::MakeMaterialEntry(&c);
        Expect(std::memcmp(&entries[2], &expectedC, sizeof(VB::MaterialEntry)) == 0, "入っていた材質の件は値のまま");
    }

    // 上限が 1 のときは、すべてが予備の番号（0 番）になる。上限 0 は 1 に丸める
    VB::MaterialTable single(1);
    Expect(single.Add(VB::MakeMaterialEntry(&a)) == 0 && single.Add(VB::MakeMaterialEntry(&b)) == 0,
           "上限 1 ではすべて 0 番へ寄せる");
    Expect(single.GetUniqueCount() == 2 && single.GetOverflowedCount() == 2, "上限 1 では通常の材質が入らない");
    VB::MaterialTable zero(0);
    Expect(zero.GetLimit() == 1, "上限 0 は 1 に丸める");

    // 既定の上限は材質のタイルの分類の材質の数と同じで、番号が範囲に収まる
    VB::MaterialTable defaults;
    Expect(defaults.GetLimit() == MaterialTiles::DEFAULT_MAX_MATERIALS, "既定の上限は材質のタイルの分類と同じ");
    for (uint32_t seed = 0; seed < VB::MATERIAL_LIMIT + 5; ++seed)
    {
        const MaterialResourceData data = MakeResourceMaterial(seed);
        const uint32_t index = defaults.Add(VB::MakeMaterialEntry(&data));
        Expect(index < VB::MATERIAL_LIMIT, "番号は上限未満");
    }
    Expect(defaults.GetUniqueCount() == VB::MATERIAL_LIMIT + 5 && defaults.GetOverflowedCount() == 6,
           "既定の上限を超えた分を数える");
}

void TestLargeTableKeepsIndicesAcrossIndexGrowth()
{
    // 件数が多く、索引の枠が何度も倍になる（探索が線形の並びでなくハッシュの索引でも、番号は最初に見た順のまま）。
    // 上限を超えた分は予備の番号へ寄り、溢れた件も索引に載って再び足しても数が増えない
    constexpr uint32_t Limit = 300;
    constexpr uint32_t Count = 1000;
    VB::MaterialTable table(Limit);
    const size_t initialSlots = table.GetIndexSlotCount();
    for (uint32_t pass = 0; pass < 3; ++pass)
    {
        for (uint32_t seed = 0; seed < Count; ++seed)
        {
            const MaterialResourceData data = MakeResourceMaterial(seed);
            const uint32_t expected = seed + 1u < Limit ? seed : Limit - 1u;
            const uint32_t index = table.Add(VB::MakeMaterialEntry(&data));
            if (index != expected)
            {
                Expect(false, "多数の材質でも、番号は最初に見た順で、溢れた材質は予備の番号");
                return;
            }
        }
    }
    Expect(table.GetUniqueCount() == Count && table.GetOverflowedCount() == Count - (Limit - 1u),
           "再び足しても値の違う材質の数は増えない");
    Expect(table.GetIndexSlotCount() > initialSlots && table.GetIndexSlotCount() >= static_cast<size_t>(Count) * 2u,
           "索引の枠は件数の 2 倍以上に広がる");
    Expect((table.GetIndexSlotCount() & (table.GetIndexSlotCount() - 1u)) == 0u, "索引の枠の数は 2 の冪");

    // 1 つのバイトだけが違う件も別の材質として区別される（ハッシュが同じ枠に当たっても、バイト列で見分ける）
    VB::MaterialTable close(Limit);
    VB::MaterialEntry base = VB::MakeMaterialEntry(static_cast<const MaterialResourceData*>(nullptr));
    for (uint32_t variant = 0; variant < 200; ++variant)
    {
        VB::MaterialEntry entry = base;
        entry.Header[3] = variant;
        Expect(close.Add(entry) == variant, "予約の語だけが違う材質も別の番号");
    }
    for (uint32_t variant = 0; variant < 200; ++variant)
    {
        VB::MaterialEntry entry = base;
        entry.Header[3] = variant;
        Expect(close.Add(entry) == variant, "再び足すと同じ番号");
    }
}

void TestTableReusesCapacityAcrossFrames()
{
    // フレームごとに Clear して同じ材質を足し直しても、件の配列・索引・GPU へ上げる並びの容量は 2 フレーム目から増えない
    constexpr uint32_t Limit = 200;
    constexpr uint32_t Count = 300;
    VB::MaterialTable table(Limit);
    Container::VariableArray<VB::MaterialEntry> gpuEntries;

    const auto fillFrame = [&]() -> void
    {
        table.Clear();
        for (uint32_t seed = 0; seed < Count; ++seed)
        {
            const MaterialResourceData data = MakeResourceMaterial(seed);
            table.Add(VB::MakeMaterialEntry(&data));
        }
        table.BuildGpuEntriesInto(gpuEntries);
    };

    fillFrame();
    const size_t entryCapacity = table.GetEntryCapacity();
    const size_t slotCount = table.GetIndexSlotCount();
    const size_t gpuCapacity = gpuEntries.capacity();
    Expect(entryCapacity > 0 && slotCount > 0 && gpuCapacity >= Limit, "最初のフレームで容量を取る");
    for (uint32_t frame = 0; frame < 3; ++frame)
    {
        table.Clear();
        Expect(table.GetEntryCapacity() == entryCapacity && table.GetIndexSlotCount() == slotCount,
               "Clear は件の配列と索引の容量を残す");
        fillFrame();
        Expect(table.GetEntryCapacity() == entryCapacity && table.GetIndexSlotCount() == slotCount &&
                   gpuEntries.capacity() == gpuCapacity,
               "次のフレームでは容量を増やさない（確保し直さない）");
    }

    // 詰めた並びは BuildGpuEntries と同じで、Clear の後の番号は 0 から詰まり直す
    const Container::VariableArray<VB::MaterialEntry> fresh = table.BuildGpuEntries();
    Expect(fresh.size() == gpuEntries.size() &&
               std::memcmp(fresh.data(), gpuEntries.data(), fresh.size() * sizeof(VB::MaterialEntry)) == 0,
           "BuildGpuEntriesInto は BuildGpuEntries と同じ並び");
    table.Clear();
    Expect(table.GetUniqueCount() == 0 && !table.HasOverflow(), "Clear は溢れも空にする");
    const MaterialResourceData first = MakeResourceMaterial(250);
    Expect(table.Add(VB::MakeMaterialEntry(&first)) == 0, "Clear の後は、前のフレームで溢れていた材質も 0 から詰める");
    table.BuildGpuEntriesInto(gpuEntries);
    Expect(gpuEntries.size() == 1, "置き換えるので前のフレームの件が残らない");
}

bool ReadWholeFile(const char* path, Container::VariableArray<char>& outText)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        return false;
    }
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);
    outText.resize(static_cast<size_t>(size) + 1);
    file.read(outText.data(), size);
    outText[static_cast<size_t>(size)] = '\0';
    return true;
}

void TestRecordShaderWritesTableIndex()
{
    // MegaGeometry の記録を GPU で作る計算が、区間の番号ではなく、区間から表の番号への対応（binding 7）を引いて書く
    Container::VariableArray<char> text;
    if (!ReadWholeFile(NORVES_SOURCE_ROOT "/Assets/Shaders/visbuffer_records.comp", text))
    {
        Expect(false, "visbuffer_records.comp を読めること");
        return;
    }
    const char* source = text.data();
    Expect(std::strstr(source, "binding = 7") != nullptr && std::strstr(source, "sectionMaterials[") != nullptr,
           "区間から表の番号への対応を binding 7 で受け取る");
    const char* headerPos = std::strstr(source, "record.header = uvec4(");
    const char* usePos = headerPos != nullptr ? std::strstr(headerPos, "sectionMaterials[sectionIndex]") : nullptr;
    const char* endPos = headerPos != nullptr ? std::strchr(headerPos, ';') : nullptr;
    Expect(usePos != nullptr && endPos != nullptr && usePos < endPos, "記録の材質の番号に、表の番号を書く");
}

int RunTest()
{
    static_assert(sizeof(VB::MaterialEntry) == 128, "材質の表の 1 件は 128 バイト");
    static_assert(VB::MATERIAL_LIMIT <= MaterialTiles::DEFAULT_MAX_MATERIALS, "上限は材質のタイルの分類の材質の数以下");

    TestSameMaterialSharesIndexAcrossDrawKinds();
            TestImportedScalarValues();
    TestMegaGeometryMaterialIsSeparateEntry();
    TestTextureSlotsRoundTrip();
    TestDifferentMaterialsGetDifferentIndices();
    TestIndicesAreDenseInFirstSeenOrder();
    TestOverflowFoldsIntoReservedIndex();
    TestLargeTableKeepsIndicesAcrossIndexGrowth();
    TestTableReusesCapacityAcrossFrames();
    TestRecordShaderWritesTableIndex();

    if (g_failures != 0)
    {
        return 1;
    }

    std::cout << "VisibilityMaterialTableTest 成功" << std::endl;
    return 0;
}

} // namespace
} // namespace NorvesLib

int main()
{
    return NorvesLib::RunTest();
}
