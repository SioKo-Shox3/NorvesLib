// ビジビリティバッファの ID の符号化・描画の記録の表の契約テスト（GPU を使わない）。
// ID = (記録の番号 << 7) | 三角形の番号 の往復、記録の数の上限（2^25）、空（0）の扱い、記録の表の追加・引き・容量、
// RenderGraph の資源の記述、GLSL（Common/VisibilityBuffer.glsl）の定数が C++ と一致していることを確かめる。
#include "Rendering/VisibilityBuffer.h"

#include <cstdint>
#include <cstdio>
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
        std::cerr << "VisibilityBufferEncodingTest 失敗: " << message << std::endl;
        ++g_failures;
    }
}

VB::DrawRecord MakeRecord(VB::RecordKind kind, uint32_t triangleCount, uint32_t seed)
{
    VB::DrawRecord record;
    record.Kind = static_cast<uint32_t>(kind);
    record.InstanceIndex = seed * 3 + 1;
    record.MaterialIndex = seed * 5 + 2;
    record.TriangleCount = triangleCount;
    record.FirstIndex = seed * 3;
    record.VertexBase = seed * 7;
    record.VertexAddress = 0x1000'0000'0000ull + seed;
    record.IndexAddress = 0x2000'0000'0000ull + seed;
    return record;
}

void TestRoundTripAndBitLayout()
{
    // 小さい記録の番号は全三角形を網羅し、往復と ID の並びを確かめる。
    bool roundTripOk = true;
    bool layoutOk = true;
    bool nonEmptyOk = true;
    for (uint32_t recordNumber = 1; recordNumber <= 2048; ++recordNumber)
    {
        for (uint32_t triangle = 0; triangle < VB::MAX_TRIANGLES_PER_RECORD; ++triangle)
        {
            uint32_t id = 0;
            if (!VB::TryEncode(recordNumber, triangle, id))
            {
                roundTripOk = false;
                continue;
            }
            nonEmptyOk = nonEmptyOk && !VB::IsEmpty(id);
            layoutOk = layoutOk && id == ((recordNumber << 7) | triangle) && (id >> 7) == recordNumber &&
                       (id & 127u) == triangle;
            uint32_t decodedRecord = 0;
            uint32_t decodedTriangle = 0;
            roundTripOk = roundTripOk && VB::TryDecode(id, decodedRecord, decodedTriangle) &&
                          decodedRecord == recordNumber && decodedTriangle == triangle;
        }
    }
    Expect(roundTripOk, "記録の番号 1..2048 × 三角形 0..127 の符号化と復号が往復しなければならない");
    Expect(layoutOk, "ID は (記録の番号 << 7) | 三角形の番号 でなければならない");
    Expect(nonEmptyOk, "符号化した ID は 0（空）にならない");

    // 大きい記録の番号の代表値（桁の境目・上限）。
    const uint32_t samples[] = {4096u, 65535u, 65536u, 1u << 20, (1u << 24) - 1, 1u << 24, VB::MAX_RECORD_COUNT - 1,
                                VB::MAX_RECORD_COUNT};
    bool sampleOk = true;
    for (uint32_t recordNumber : samples)
    {
        for (uint32_t triangle : {0u, 1u, 63u, 64u, 127u})
        {
            uint32_t id = 0;
            uint32_t decodedRecord = 0;
            uint32_t decodedTriangle = 0;
            sampleOk = sampleOk && VB::TryEncode(recordNumber, triangle, id) &&
                       VB::TryDecode(id, decodedRecord, decodedTriangle) && decodedRecord == recordNumber &&
                       decodedTriangle == triangle;
        }
    }
    Expect(sampleOk, "記録の番号の桁の境目と上限でも符号化と復号が往復しなければならない");

    // ID を 32bit 全域から刻みで取っても、復号した結果を符号化すると元の ID に戻る（0 と記録の番号 0 を除く）。
    bool idRoundTripOk = true;
    for (uint64_t value = 128; value <= 0xFFFFFFFFull; value += 65537)
    {
        const uint32_t id = static_cast<uint32_t>(value);
        uint32_t recordNumber = 0;
        uint32_t triangle = 0;
        uint32_t encoded = 0;
        idRoundTripOk = idRoundTripOk && VB::TryDecode(id, recordNumber, triangle) &&
                        VB::TryEncode(recordNumber, triangle, encoded) && encoded == id;
    }
    Expect(idRoundTripOk, "記録の番号が 1 以上の ID は、復号して符号化し直すと元に戻らなければならない");
}

void TestRecordLimit()
{
    Expect(VB::TRIANGLE_BITS == 7 && VB::MAX_TRIANGLES_PER_RECORD == 128, "三角形の番号は 7bit・最大 128 三角形");
    Expect(VB::RECORD_BITS == 25, "記録の番号は 25bit");
    Expect(VB::RECORD_SLOT_LIMIT == (1u << 25), "表の枠の上限は 2^25");
    Expect(VB::MAX_RECORD_COUNT == (1u << 25) - 1, "使える記録の数は 0 番を除いた 2^25 - 1");

    uint32_t id = 0xABCDu;
    Expect(!VB::TryEncode(0, 0, id) && id == 0xABCDu, "記録の番号 0（空の記録）は符号化できず、出力を触らない");
    Expect(!VB::TryEncode(VB::RECORD_SLOT_LIMIT, 0, id) && id == 0xABCDu, "記録の番号 2^25 は符号化できない");
    Expect(!VB::TryEncode(0xFFFFFFFFu, 0, id) && id == 0xABCDu, "記録の番号が 32bit の最大でも符号化できない");
    Expect(!VB::TryEncode(1, VB::MAX_TRIANGLES_PER_RECORD, id) && id == 0xABCDu, "三角形の番号 128 は符号化できない");
    Expect(!VB::TryEncode(1, 0xFFFFFFFFu, id) && id == 0xABCDu, "三角形の番号が範囲外なら符号化できない");

    Expect(VB::TryEncode(VB::MAX_RECORD_COUNT, VB::MAX_TRIANGLES_PER_RECORD - 1, id) && id == 0xFFFFFFFFu,
           "最後の記録の最後の三角形の ID は 0xFFFFFFFF");
    Expect(VB::TryEncode(1, 0, id) && id == 128u, "最初の記録の最初の三角形の ID は 128");

    Expect(VB::IsValidRecordNumber(1) && VB::IsValidRecordNumber(VB::MAX_RECORD_COUNT), "1 と上限は使える記録の番号");
    Expect(!VB::IsValidRecordNumber(0) && !VB::IsValidRecordNumber(VB::RECORD_SLOT_LIMIT),
           "0 と 2^25 は使えない記録の番号");
}

void TestEmptyId()
{
    Expect(VB::EMPTY_ID == 0 && VB::IsEmpty(0), "0 は空");
    Expect(!VB::IsEmpty(1) && !VB::IsEmpty(128) && !VB::IsEmpty(0xFFFFFFFFu), "0 以外は空でない");

    uint32_t recordNumber = 77;
    uint32_t triangle = 88;
    Expect(!VB::TryDecode(0, recordNumber, triangle) && recordNumber == 77 && triangle == 88,
           "空の ID は復号できず、出力を触らない");

    // 1..127 は記録の番号 0（空の記録）の三角形になり、符号化では作られない。表からは引けない。
    VB::RecordTable table;
    table.Add(MakeRecord(VB::RecordKind::ProceduralChunk, 4, 1));
    const VB::DrawRecord* record = nullptr;
    uint32_t resolvedTriangle = 0;
    bool anyResolved = table.TryResolve(0, record, resolvedTriangle);
    for (uint32_t id = 1; id < 128; ++id)
    {
        anyResolved = anyResolved || table.TryResolve(id, record, resolvedTriangle);
    }
    Expect(!anyResolved && record == nullptr, "空の ID と記録の番号 0 の ID は表から記録を引けない");
}

void TestRecordTable()
{
    Expect(sizeof(VB::DrawRecord) == 64, "記録は 64 バイト");

    VB::RecordTable table;
    Expect(table.SlotCount() == 1 && table.RecordCount() == 0, "Clear した表は空の記録（0 番）だけを持つ");
    Expect(table.SizeInBytes() == 64, "空の表は 1 枠ぶん 64 バイト");
    const VB::DrawRecord& nullRecord = table.Data()[0];
    Expect(nullRecord.Kind == 0 && nullRecord.TriangleCount == 0 && nullRecord.VertexAddress == 0,
           "0 番は何も持たない空の記録");

    const uint32_t first = table.Add(MakeRecord(VB::RecordKind::MegaGeometryCluster, 128, 1));
    const uint32_t second = table.Add(MakeRecord(VB::RecordKind::ProceduralChunk, 1, 2));
    VB::DrawRecord skinned = MakeRecord(VB::RecordKind::SkinnedChunk, 77, 3);
    skinned.PreviousVertexAddress = 0x3000'0000'0000ull;
    const uint32_t third = table.Add(skinned);
    Expect(first == 1 && second == 2 && third == 3, "記録の番号は 1 から順に割り当てる");
    Expect(table.RecordCount() == 3 && table.SlotCount() == 4 && table.SizeInBytes() == 4 * 64,
           "記録の数・枠の数・バイト数");

    uint32_t id = VB::EMPTY_ID;
    Expect(VB::TryEncode(third, 76, id), "最後の三角形の ID を作れる");
    const VB::DrawRecord* record = nullptr;
    uint32_t triangle = 0;
    Expect(table.TryResolve(id, record, triangle) && record != nullptr && triangle == 76, "ID から記録と三角形を引ける");
    Expect(record != nullptr && record->Kind == static_cast<uint32_t>(VB::RecordKind::SkinnedChunk) &&
               record->PreviousVertexAddress == 0x3000'0000'0000ull && record->TriangleCount == 77 &&
               record->VertexAddress == skinned.VertexAddress,
           "引いた記録は足した記録と同じ中身");
    Expect(record == &table.Data()[third], "引いた記録は表の該当の行");

    // 三角形の番号が記録の三角形数以上の ID・表の外の記録の番号は引けない。
    VB::TryEncode(second, 1, id);
    Expect(!table.TryResolve(id, record, triangle), "三角形数 1 の記録の三角形 1 は引けない");
    VB::TryEncode(4, 0, id);
    Expect(!table.TryResolve(id, record, triangle), "表に無い記録の番号は引けない");

    // 足したときの ID（AddAndEncode）。
    const uint32_t encoded = table.AddAndEncode(MakeRecord(VB::RecordKind::MegaGeometryCluster, 10, 4), 9);
    uint32_t decodedRecord = 0;
    uint32_t decodedTriangle = 0;
    Expect(VB::TryDecode(encoded, decodedRecord, decodedTriangle) && decodedRecord == 4 && decodedTriangle == 9,
           "AddAndEncode は足した記録の三角形の ID を返す");
    const uint32_t before = table.RecordCount();
    Expect(table.AddAndEncode(MakeRecord(VB::RecordKind::MegaGeometryCluster, 10, 5), 10) == VB::EMPTY_ID &&
               table.RecordCount() == before,
           "記録の三角形数以上の三角形は ID にならず、記録も足さない");

    // 使えない記録は断る。
    const uint32_t countBefore = table.RecordCount();
    Expect(table.Add(MakeRecord(VB::RecordKind::None, 4, 6)) == 0, "種類が無い記録は足せない");
    Expect(table.Add(MakeRecord(VB::RecordKind::ProceduralChunk, 0, 6)) == 0, "三角形が 0 の記録は足せない");
    Expect(table.Add(MakeRecord(VB::RecordKind::ProceduralChunk, 129, 6)) == 0, "三角形が 129 の記録は足せない");
    VB::DrawRecord badIndex = MakeRecord(VB::RecordKind::ProceduralChunk, 4, 6);
    badIndex.FirstIndex = 4;
    Expect(table.Add(badIndex) == 0, "最初のインデックスが 3 の倍数でない記録は足せない");
    Expect(table.RecordCount() == countBefore && table.RejectedCount() == 5, "断った記録は表に入らず、断った数を数える");

    table.Clear();
    Expect(table.RecordCount() == 0 && table.SlotCount() == 1 && table.OverflowCount() == 0 &&
               table.RejectedCount() == 0,
           "Clear は記録と数えた数を戻す");
}

void TestRecordTableCapacity()
{
    VB::RecordTable table(3);
    Expect(table.MaxRecords() == 3, "上限を小さくできる");
    bool added = true;
    for (uint32_t i = 0; i < 3; ++i)
    {
        added = added && table.Add(MakeRecord(VB::RecordKind::MegaGeometryCluster, 128, i)) == i + 1;
    }
    Expect(added, "上限までは足せる");
    Expect(table.Add(MakeRecord(VB::RecordKind::MegaGeometryCluster, 128, 9)) == 0 && table.OverflowCount() == 1 &&
               table.RecordCount() == 3,
           "上限を超えた記録は足せず（0）、溢れた回数を数える");
    Expect(table.AddAndEncode(MakeRecord(VB::RecordKind::MegaGeometryCluster, 128, 9), 0) == VB::EMPTY_ID &&
               table.OverflowCount() == 2,
           "上限を超えたら AddAndEncode も空の ID を返す");

    // 上限は 2^25 - 1（0 番の空の記録を引いた数）を超えられない。
    VB::RecordTable unbounded(0xFFFFFFFFu);
    Expect(unbounded.MaxRecords() == VB::MAX_RECORD_COUNT, "上限を大きく指定しても 2^25 - 1 に収める");
    VB::RecordTable zero(0);
    Expect(zero.Add(MakeRecord(VB::RecordKind::MegaGeometryCluster, 1, 1)) == 0 && zero.OverflowCount() == 1,
           "上限 0 の表は何も足せない");
}

void TestRenderGraphDescriptions()
{
    const RGTextureDesc idDesc = VB::MakeIdTextureDesc(1920, 1080);
    Expect(idDesc.Width == 1920 && idDesc.Height == 1080, "VisBuffer.Id は画面の大きさ");
    Expect(idDesc.Format == RHI::Format::R32_UINT, "VisBuffer.Id は R32_UINT");
    Expect((idDesc.Usage & RHI::ResourceUsage::RenderTarget) == RHI::ResourceUsage::RenderTarget &&
               (idDesc.Usage & RHI::ResourceUsage::ShaderRead) == RHI::ResourceUsage::ShaderRead,
           "VisBuffer.Id はカラー添付で書いてシェーダーが読める");

    const RGBufferDesc tableDesc = VB::MakeRecordTableBufferDesc(5);
    Expect(tableDesc.Size == 5 * 64, "記録の表のバッファは枠の数 × 64 バイト");
    Expect((tableDesc.Usage & RHI::ResourceUsage::StorageBuffer) == RHI::ResourceUsage::StorageBuffer &&
               (tableDesc.Usage & RHI::ResourceUsage::ShaderRead) == RHI::ResourceUsage::ShaderRead,
           "記録の表は storage buffer");
    Expect(VB::MakeRecordTableBufferDesc(0).Size == 64, "枠が 0 でも空の記録の 1 枠は確保する");
}

// ---- GLSL の定数の照合 ----

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

// `const uint NAME = 123u;` の形の1行から値を取り出す
bool FindGlslUint(const Container::VariableArray<char>& text, const char* name, uint64_t& outValue)
{
    char pattern[160];
    std::snprintf(pattern, sizeof(pattern), "const uint %s = ", name);
    const char* found = std::strstr(text.data(), pattern);
    if (found == nullptr)
    {
        return false;
    }
    const char* cursor = found + std::strlen(pattern);
    if (*cursor < '0' || *cursor > '9')
    {
        return false;
    }
    uint64_t value = 0;
    while (*cursor >= '0' && *cursor <= '9')
    {
        value = value * 10 + static_cast<uint64_t>(*cursor - '0');
        ++cursor;
    }
    if (*cursor != 'u' || cursor[1] != ';')
    {
        return false;
    }
    outValue = value;
    return true;
}

void ExpectGlslConstant(const Container::VariableArray<char>& text, const char* name, uint64_t expected)
{
    uint64_t actual = 0;
    if (!FindGlslUint(text, name, actual))
    {
        std::cerr << "VisibilityBufferEncodingTest 失敗: VisibilityBuffer.glsl に定数 " << name
                  << " が `const uint NAME = 値u;` の形で無い" << std::endl;
        ++g_failures;
        return;
    }
    if (actual != expected)
    {
        std::cerr << "VisibilityBufferEncodingTest 失敗: VisibilityBuffer.glsl の " << name << " = " << actual
                  << "（C++ は " << expected << "）" << std::endl;
        ++g_failures;
    }
}

void TestGlslConstantsMatchCpp()
{
    Container::VariableArray<char> text;
    if (!ReadWholeFile(NORVES_SOURCE_ROOT "/Assets/Shaders/Common/VisibilityBuffer.glsl", text))
    {
        Expect(false, "Assets/Shaders/Common/VisibilityBuffer.glsl を読めない");
        return;
    }

    ExpectGlslConstant(text, "VIS_TRIANGLE_BITS", VB::TRIANGLE_BITS);
    ExpectGlslConstant(text, "VIS_MAX_TRIANGLES_PER_RECORD", VB::MAX_TRIANGLES_PER_RECORD);
    ExpectGlslConstant(text, "VIS_RECORD_BITS", VB::RECORD_BITS);
    ExpectGlslConstant(text, "VIS_RECORD_SLOT_LIMIT", VB::RECORD_SLOT_LIMIT);
    ExpectGlslConstant(text, "VIS_EMPTY_ID", VB::EMPTY_ID);
    ExpectGlslConstant(text, "VIS_KIND_NONE", static_cast<uint32_t>(VB::RecordKind::None));
    ExpectGlslConstant(text, "VIS_KIND_MEGA_CLUSTER", static_cast<uint32_t>(VB::RecordKind::MegaGeometryCluster));
    ExpectGlslConstant(text, "VIS_KIND_PROCEDURAL_CHUNK", static_cast<uint32_t>(VB::RecordKind::ProceduralChunk));
    ExpectGlslConstant(text, "VIS_KIND_SKINNED_CHUNK", static_cast<uint32_t>(VB::RecordKind::SkinnedChunk));
    ExpectGlslConstant(text, "VIS_RECORD_FLAG_INDEX16", VB::RECORD_FLAG_INDEX16);
    ExpectGlslConstant(text, "VIS_NO_PREVIOUS_TRANSFORM", VB::NO_PREVIOUS_TRANSFORM);

    // 記録の構造体は 4 つの uvec4（64 バイト）
    const char* structStart = std::strstr(text.data(), "struct VisibilityDrawRecord");
    Expect(structStart != nullptr, "VisibilityDrawRecord の構造体がある");
    if (structStart != nullptr)
    {
        const char* structEnd = std::strstr(structStart, "};");
        uint32_t uvec4Count = 0;
        for (const char* cursor = std::strstr(structStart, "uvec4 "); cursor != nullptr && structEnd != nullptr &&
                                                                       cursor < structEnd;
             cursor = std::strstr(cursor + 1, "uvec4 "))
        {
            ++uvec4Count;
        }
        Expect(uvec4Count == sizeof(VB::DrawRecord) / 16, "GLSL の記録は 16 バイトの uvec4 が C++ の 64 バイトぶん並ぶ");
    }
}

int RunTest()
{
    TestRoundTripAndBitLayout();
    TestRecordLimit();
    TestEmptyId();
    TestRecordTable();
    TestRecordTableCapacity();
    TestRenderGraphDescriptions();
    TestGlslConstantsMatchCpp();

    if (g_failures != 0)
    {
        return 1;
    }

    std::cout << "VisibilityBufferEncodingTest 成功" << std::endl;
    return 0;
}

} // namespace
} // namespace NorvesLib

int main()
{
    return NorvesLib::RunTest();
}
