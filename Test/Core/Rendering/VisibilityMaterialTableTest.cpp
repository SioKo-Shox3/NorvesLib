// ビジビリティバッファの材質の表の契約テスト（GPU を使わない）。
// 記録の材質の番号がフレームで一意な表の番号になること: 同じ材質の異なる描画（MegaGeometry の区間・手続きメッシュ・スキニング）が
// 同じ番号に、違う材質が違う番号になること、番号が 0 から詰まっていること、上限を超えたときに予備の番号へ寄せること、
// MegaGeometry の記録を作る計算シェーダーが区間の番号ではなく表の番号を書くことを確かめる。
#include "Rendering/MaterialTypes.h"
#include "Rendering/MegaGeometry/MegaGeometryTypes.h"
#include "Rendering/VisibilityMaterialTable.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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

    // 手続きメッシュの描画 2 つ（別の塊・別のインスタンスでも、同じ材質）、スキニングの描画、MegaGeometry の区間が同じ材質を使う
    MaterialResourceData shared = MakeResourceMaterial(0);
    // MegaGeometry の材質は金属度・粗さのスカラーを持たないので、同じ値にそろえる
    shared.Metallic = -1.0f;
    shared.Roughness = -1.0f;

    const uint32_t mega = table.Add(VB::MakeMaterialEntry(MakeMegaMaterialLike(shared)));
    const uint32_t procedural0 = table.Add(VB::MakeMaterialEntry(&shared));
    const uint32_t procedural1 = table.Add(VB::MakeMaterialEntry(&shared));
    const uint32_t skinned = table.Add(VB::MakeMaterialEntry(&shared));
    Expect(mega == procedural0 && procedural0 == procedural1 && procedural1 == skinned,
           "同じ材質を使う MegaGeometry・手続き・スキニングの描画は同じ番号になる");
    Expect(table.GetUniqueCount() == 1, "同じ材質だけなら表は 1 件");
    Expect(table.BuildGpuEntries().size() == 1, "GPU へ上げる表も 1 件");

    // 別の MaterialHandle でも値が同じなら同じ材質（実物の値で引くため）。値が違えば別の材質
    const MaterialResourceData sameValue = shared;
    Expect(table.Add(VB::MakeMaterialEntry(&sameValue)) == mega, "値が同じ材質は同じ番号");
    const MaterialResourceData other = MakeResourceMaterial(1);
    Expect(table.Add(VB::MakeMaterialEntry(&other)) != mega, "値の違う材質は別の番号");
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

std::string ReadShaderSource(const char* name)
{
    std::ifstream file(std::string(NORVES_SOURCE_ROOT) + "/Assets/Shaders/" + name, std::ios::binary);
    std::stringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

void TestRecordShaderWritesTableIndex()
{
    // MegaGeometry の記録を GPU で作る計算が、区間の番号ではなく、区間から表の番号への対応（binding 7）を引いて書く
    const std::string source = ReadShaderSource("visbuffer_records.comp");
    Expect(!source.empty(), "visbuffer_records.comp を読めること");
    Expect(source.find("binding = 7") != std::string::npos && source.find("sectionMaterials[") != std::string::npos,
           "区間から表の番号への対応を binding 7 で受け取る");
    const size_t headerPos = source.find("record.header = uvec4(");
    Expect(headerPos != std::string::npos &&
               source.find("sectionMaterials[sectionIndex]", headerPos) != std::string::npos &&
               source.find("sectionMaterials[sectionIndex]", headerPos) < source.find(";", headerPos),
           "記録の材質の番号に、表の番号を書く");
}

int RunTest()
{
    static_assert(sizeof(VB::MaterialEntry) == 128, "材質の表の 1 件は 128 バイト");
    static_assert(VB::MATERIAL_LIMIT <= MaterialTiles::DEFAULT_MAX_MATERIALS, "上限は材質のタイルの分類の材質の数以下");

    TestSameMaterialSharesIndexAcrossDrawKinds();
    TestDifferentMaterialsGetDifferentIndices();
    TestIndicesAreDenseInFirstSeenOrder();
    TestOverflowFoldsIntoReservedIndex();
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
