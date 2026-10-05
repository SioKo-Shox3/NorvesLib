// 材質ごとのタイルの分類（material_tile_classify.comp / MaterialTileClassify）の GPU テスト。
// 合成した VisBuffer.Id の画像（36x20。右端と下端が 8 に満たない部分タイルを含む 5x3 のタイル）と描画の記録の表から、
// 材質ごとのタイルの一覧と間接 dispatch の引数・統計が期待どおりに作られることを確かめる。
//   ケース A（上限 8 材質、一覧は十分）: 3 つの材質が混じるタイル・同じ材質が別の記録で出るタイル・空のタイル・
//     1 画素だけのタイル・8 材質すべてが出るタイル・上限以上の材質が出るタイル・引けない ID だけのタイル・部分タイル。
//     CPU の独立した参照（記録の表の引き）と、手で導いた一覧の両方と照合する。
//   ケース B（一覧が足りない）: 一覧の大きさを 15 にして、引数が一覧の中へ収まるよう切り詰められ、落とした数が統計に出て、
//     一覧の外へ書かないこと。
//   ケース C（全部が空）: どの材質にもタイルが入らず、統計が 0 のこと。
//   RecordClear: 引数と統計を 0 にできること。
//   どのケースも Vulkan の validation error が 0 件。
// Vulkan デバイスが無い環境では 125（スキップ）を返す。
#include "Rendering/MaterialTileClassifyPass.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/VisibilityBuffer.h"

#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace NorvesLib::RHI::Vulkan
{
void BeginVulkanValidationErrorCaptureForTesting() noexcept;
void EndVulkanValidationErrorCaptureForTesting() noexcept;
uint32_t GetVulkanValidationErrorCaptureHitCountForTesting() noexcept;
}

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;

    constexpr const char* TestName = "MaterialTileClassifyVulkanTest";
    constexpr int GpuTestSkipReturnCode = 125;
    constexpr uint32_t ImageWidth = 36;
    constexpr uint32_t ImageHeight = 20;
    constexpr uint32_t TestMaxMaterials = 8;
    constexpr uint32_t GuardWord = 0xDEADBEEFu;
    // 出力の末尾に置く見張りの語の数
    constexpr uint32_t GuardWords = 16;

    int g_failures = 0;

    void Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << TestName << " 失敗: " << message << std::endl;
            ++g_failures;
        }
    }

    bool IsGpuTestSkipForced()
    {
        char* forceSkip = nullptr;
        size_t forceSkipLength = 0;
        if (_dupenv_s(&forceSkip, &forceSkipLength, "NORVESLIB_FORCE_GPU_TEST_SKIP") != 0 || forceSkip == nullptr)
        {
            return false;
        }
        const bool bForceSkip = std::strcmp(forceSkip, "1") == 0;
        free(forceSkip);
        return bForceSkip;
    }

    int SkipGpuTest(const char* reason)
    {
        std::cout << TestName << " スキップ: " << reason << std::endl;
        return GpuTestSkipReturnCode;
    }

    class VulkanValidationErrorCapture
    {
    public:
        VulkanValidationErrorCapture() { RHI::Vulkan::BeginVulkanValidationErrorCaptureForTesting(); }
        ~VulkanValidationErrorCapture() { RHI::Vulkan::EndVulkanValidationErrorCaptureForTesting(); }
        uint32_t GetHitCount() const { return RHI::Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting(); }
    };

    // ========================================
    // 合成した画像と記録の表
    // ========================================

    // 記録の番号 1.. の材質の番号（添字 0 は空の記録の枠で使わない）
    const uint32_t RecordMaterials[] = {0, 0, 1, 2, 5, 1, 12, 3, 4, 6, 7, 3};
    constexpr uint32_t RecordCount = sizeof(RecordMaterials) / sizeof(RecordMaterials[0]) - 1;
    // 三角形が 2 つしかない記録（三角形の番号 5 は引けない）
    constexpr uint32_t ShortRecord = 11;
    // 表に無い記録の番号
    constexpr uint32_t MissingRecord = 99;

    VisibilityBuffer::RecordTable BuildRecordTable()
    {
        VisibilityBuffer::RecordTable table;
        for (uint32_t record = 1; record <= RecordCount; ++record)
        {
            VisibilityBuffer::DrawRecord drawRecord;
            drawRecord.Kind = static_cast<uint32_t>(VisibilityBuffer::RecordKind::ProceduralChunk);
            drawRecord.InstanceIndex = record;
            drawRecord.MaterialIndex = RecordMaterials[record];
            drawRecord.TriangleCount = record == ShortRecord ? 2u : 128u;
            const uint32_t number = table.Add(drawRecord);
            if (number != record)
            {
                std::cerr << TestName << " 記録の番号が想定と違う: " << number << std::endl;
                ++g_failures;
            }
        }
        return table;
    }

    uint32_t MakeId(uint32_t record, uint32_t triangle)
    {
        return (record << VisibilityBuffer::TRIANGLE_BITS) | triangle;
    }

    // タイル (tileX, tileY) の中の画素 (x, y) へ ID を置く（画像の外なら何もしない）
    void SetPixel(std::vector<uint32_t>& image, uint32_t tileX, uint32_t tileY, uint32_t x, uint32_t y, uint32_t id)
    {
        const uint32_t px = tileX * 8 + x;
        const uint32_t py = tileY * 8 + y;
        if (px < ImageWidth && py < ImageHeight)
        {
            image[py * ImageWidth + px] = id;
        }
    }

    // タイルの全画素を、記録の番号の並び（巡回）で埋める。三角形の番号は画素ごとに変える。記録の番号 0 は空の画素
    void FillTile(std::vector<uint32_t>& image, uint32_t tileX, uint32_t tileY, const std::vector<uint32_t>& records)
    {
        uint32_t cursor = 0;
        for (uint32_t y = 0; y < 8; ++y)
        {
            for (uint32_t x = 0; x < 8; ++x)
            {
                const uint32_t record = records[cursor % records.size()];
                SetPixel(image, tileX, tileY, x, y,
                         record == 0 ? VisibilityBuffer::EMPTY_ID : MakeId(record, (x + y * 8) % 4));
                ++cursor;
            }
        }
    }

    // 5x3 のタイル（番号 = tileY * 5 + tileX）。ケース A の想定は冒頭のコメントと ExpectedLists を参照
    std::vector<uint32_t> BuildImage()
    {
        std::vector<uint32_t> image(ImageWidth * ImageHeight, VisibilityBuffer::EMPTY_ID);
        // タイル 0: 材質 0・1・2 が混じる（記録 1・2・3）
        FillTile(image, 0, 0, {1, 2, 3});
        // タイル 1: 空
        // タイル 2: 材質 1 だけ。ただし別の記録（2 と 5）で出る
        FillTile(image, 2, 0, {2, 5});
        // タイル 3: 材質 0 が 1 画素だけ（タイルの隅）
        SetPixel(image, 3, 0, 7, 7, MakeId(1, 3));
        // タイル 4（右端の幅 4 の部分タイル）: 材質 5（記録 4）
        FillTile(image, 4, 0, {4});
        // タイル 5: 材質 2 と 5（記録 3・4）。空の画素も混ざる
        FillTile(image, 0, 1, {3, 0, 4, 0});
        // タイル 6: 材質 0..7 のすべて + 上限以上の材質 12（記録 1..10）
        FillTile(image, 1, 1, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
        // タイル 7: 引けない ID だけ（表に無い記録、三角形が記録の範囲外）。材質には数えない
        SetPixel(image, 2, 1, 0, 0, MakeId(MissingRecord, 3));
        SetPixel(image, 2, 1, 1, 0, MakeId(ShortRecord, 5));
        SetPixel(image, 2, 1, 2, 3, MakeId(MissingRecord, 0));
        // タイル 8: 材質 6 と 7（記録 9・10）
        FillTile(image, 3, 1, {9, 10});
        // タイル 9（部分タイル）: 材質 4（記録 8）
        FillTile(image, 4, 1, {8});
        // タイル 10（下端の高さ 4 の部分タイル）: 材質 0 と 1（記録 1・2）
        FillTile(image, 0, 2, {1, 2});
        // タイル 11: 空
        // タイル 12（部分タイル）: 材質 2（記録 3）
        FillTile(image, 2, 2, {3});
        // タイル 13: 空
        // タイル 14（右下の 4x4 の部分タイル）: 材質 3（記録 7）
        FillTile(image, 4, 2, {7});
        return image;
    }

    // 手で導いた材質ごとの一覧（ケース A。タイルの番号の昇順）
    const std::vector<std::vector<uint32_t>> ExpectedLists = {
        {0, 3, 6, 10},  // 材質 0
        {0, 2, 6, 10},  // 材質 1
        {0, 5, 6, 12},  // 材質 2
        {6, 14},        // 材質 3
        {6, 9},         // 材質 4
        {4, 5, 6},      // 材質 5
        {6, 8},         // 材質 6
        {6, 8},         // 材質 7
    };
    constexpr uint32_t ExpectedUnresolvedPixels = 3;

    // ========================================
    // CPU の参照（記録の表の引きだけを使い、シェーダーと独立にタイルごとの材質の集合を求める）
    // ========================================

    struct Reference
    {
        std::vector<std::vector<uint32_t>> Lists; // 材質ごとのタイルの番号（昇順）
        uint32_t OutOfRange = 0;
        uint32_t Unresolved = 0;
        uint32_t MaxMaterialPlusOne = 0;
    };

    Reference BuildReference(const std::vector<uint32_t>& image, const VisibilityBuffer::RecordTable& table, uint32_t maxMaterials)
    {
        Reference reference;
        reference.Lists.resize(maxMaterials);
        const uint32_t tilesX = (ImageWidth + 7) / 8;
        const uint32_t tilesY = (ImageHeight + 7) / 8;
        for (uint32_t tileY = 0; tileY < tilesY; ++tileY)
        {
            for (uint32_t tileX = 0; tileX < tilesX; ++tileX)
            {
                std::vector<uint32_t> materials;
                for (uint32_t y = tileY * 8; y < std::min(tileY * 8 + 8, ImageHeight); ++y)
                {
                    for (uint32_t x = tileX * 8; x < std::min(tileX * 8 + 8, ImageWidth); ++x)
                    {
                        const uint32_t id = image[y * ImageWidth + x];
                        if (VisibilityBuffer::IsEmpty(id))
                        {
                            continue;
                        }
                        const VisibilityBuffer::DrawRecord* record = nullptr;
                        uint32_t triangle = 0;
                        if (!table.TryResolve(id, record, triangle))
                        {
                            ++reference.Unresolved;
                            continue;
                        }
                        if (std::find(materials.begin(), materials.end(), record->MaterialIndex) == materials.end())
                        {
                            materials.push_back(record->MaterialIndex);
                        }
                    }
                }
                for (const uint32_t material : materials)
                {
                    if (material >= maxMaterials)
                    {
                        ++reference.OutOfRange;
                        continue;
                    }
                    reference.Lists[material].push_back(tileY * tilesX + tileX);
                    reference.MaxMaterialPlusOne = std::max(reference.MaxMaterialPlusOne, material + 1);
                }
            }
        }
        return reference;
    }

    // ========================================
    // GPU の実行と読み戻し
    // ========================================

    struct Outputs
    {
        std::vector<uint32_t> Args;
        std::vector<uint32_t> List;
        std::vector<uint32_t> Stats;
        bool bRecorded = false;
    };

    // 4 つの出力バッファを作り（末尾に見張りを置く）、分類を記録して読み戻す。
    // listCapacity は Layout の一覧の大きさ（タイルの番号の数）
    bool RunClassify(const DevicePtr& device,
                     MaterialTileClassify& classify,
                     const TexturePtr& idTexture,
                     const BufferPtr& recordTable,
                     uint64_t recordTableBytes,
                     uint32_t maxEntriesPerTile,
                     uint64_t frameIndex,
                     Outputs& outputs,
                     MaterialTiles::Layout& outLayout)
    {
        const MaterialTiles::Layout layout =
            MaterialTiles::ComputeLayout(ImageWidth, ImageHeight, TestMaxMaterials, maxEntriesPerTile);
        outLayout = layout;
        const ResourceUsage usage = ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
        const uint64_t argsBytes = layout.ArgsBytes() + GuardWords * sizeof(uint32_t);
        const uint64_t listBytes = layout.ListBytes() + GuardWords * sizeof(uint32_t);
        const uint64_t cursorsBytes = layout.CursorsBytes();
        const uint64_t statsBytes = MaterialTiles::STATS_BYTES;

        BufferPtr args = device->CreateBuffer(BufferDesc(argsBytes, usage, true, "MaterialTileTestArgs"));
        BufferPtr list = device->CreateBuffer(BufferDesc(listBytes, usage, true, "MaterialTileTestList"));
        BufferPtr cursors = device->CreateBuffer(BufferDesc(cursorsBytes, usage, true, "MaterialTileTestCursors"));
        BufferPtr stats = device->CreateBuffer(BufferDesc(statsBytes, usage, true, "MaterialTileTestStats"));
        CommandListPtr commandList = device->CreateCommandList();
        if (!args || !list || !cursors || !stats || !commandList)
        {
            std::cerr << TestName << " 出力のバッファかコマンドリストを作れませんでした" << std::endl;
            return false;
        }

        // 出力は見張りの値で埋める。引数・統計は分類が 0 から数え直し、一覧は書いた範囲だけが変わる
        for (const BufferPtr& buffer : {args, list, cursors, stats})
        {
            uint32_t* mapped = static_cast<uint32_t*>(buffer->Map(0u, buffer->GetSize()));
            if (mapped == nullptr)
            {
                std::cerr << TestName << " バッファをマップできませんでした" << std::endl;
                return false;
            }
            for (uint64_t word = 0; word < buffer->GetSize() / sizeof(uint32_t); ++word)
            {
                mapped[word] = GuardWord;
            }
            buffer->Unmap();
        }

        classify.BeginFrame(frameIndex);
        MaterialTileClassifyDispatch dispatch;
        dispatch.IdTexture = idTexture;
        dispatch.RecordTable = recordTable;
        dispatch.RecordTableBytes = recordTableBytes;
        dispatch.Args = args;
        dispatch.List = list;
        dispatch.Cursors = cursors;
        dispatch.Stats = stats;
        dispatch.Width = ImageWidth;
        dispatch.Height = ImageHeight;
        dispatch.Layout = layout;

        commandList->Begin();
        for (const BufferPtr& buffer : {args, list, cursors, stats})
        {
            commandList->BufferBarrier(buffer, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, buffer->GetSize());
        }
        outputs.bRecorded = classify.Record(commandList.get(), dispatch);
        for (const BufferPtr& buffer : {args, list, cursors, stats})
        {
            commandList->BufferBarrier(buffer, ResourceState::UnorderedAccess, ResourceState::HostRead, 0u, buffer->GetSize());
        }
        commandList->End();
        commandList->Submit(true);
        device->WaitIdle();

        auto readAll = [](const BufferPtr& buffer, std::vector<uint32_t>& out) -> bool
        {
            const uint32_t* mapped = static_cast<const uint32_t*>(buffer->Map(0u, buffer->GetSize()));
            if (mapped == nullptr)
            {
                return false;
            }
            out.assign(mapped, mapped + buffer->GetSize() / sizeof(uint32_t));
            buffer->Unmap();
            return true;
        };
        if (!readAll(args, outputs.Args) || !readAll(list, outputs.List) || !readAll(stats, outputs.Stats))
        {
            std::cerr << TestName << " 出力を読み戻せませんでした" << std::endl;
            return false;
        }
        return true;
    }

    uint32_t ArgWord(const Outputs& outputs, uint32_t material, uint32_t word)
    {
        return outputs.Args[material * 4u + word];
    }

    // 引数の外（末尾の見張り）が変わっていないこと
    bool GuardsIntact(const Outputs& outputs, const MaterialTiles::Layout& layout)
    {
        const size_t argsWords = layout.ArgsBytes() / sizeof(uint32_t);
        const size_t listWords = layout.ListBytes() / sizeof(uint32_t);
        for (uint32_t guard = 0; guard < GuardWords; ++guard)
        {
            if (outputs.Args[argsWords + guard] != GuardWord || outputs.List[listWords + guard] != GuardWord)
            {
                return false;
            }
        }
        return true;
    }

    // ケース A・C 共通: 引数・一覧・統計が参照と一致すること。expectedLists は材質ごとのタイルの番号（昇順）
    void CheckAgainstLists(const char* label,
                           const Outputs& outputs,
                           const MaterialTiles::Layout& layout,
                           const std::vector<std::vector<uint32_t>>& expectedLists,
                           uint32_t expectedOutOfRange,
                           uint32_t expectedUnresolved)
    {
        Expect(outputs.bRecorded, "分類を記録できなければならない");
        uint32_t expectedOffset = 0;
        uint32_t totalEntries = 0;
        uint32_t maxMaterialPlusOne = 0;
        for (uint32_t material = 0; material < layout.MaxMaterials; ++material)
        {
            const std::vector<uint32_t>& expected = expectedLists[material];
            const uint32_t count = static_cast<uint32_t>(expected.size());
            const bool bShapeOk = ArgWord(outputs, material, 0) == count && ArgWord(outputs, material, 1) == 1u &&
                                  ArgWord(outputs, material, 2) == 1u && ArgWord(outputs, material, 3) == expectedOffset;
            if (!bShapeOk)
            {
                std::cerr << TestName << " " << label << " 材質 " << material << " の引数が違う: GPU=("
                          << ArgWord(outputs, material, 0) << "," << ArgWord(outputs, material, 1) << ","
                          << ArgWord(outputs, material, 2) << "," << ArgWord(outputs, material, 3) << ") 期待=(" << count
                          << ",1,1," << expectedOffset << ")" << std::endl;
            }
            Expect(bShapeOk, "材質ごとの引数（タイルの数, 1, 1, 一覧の先頭位置）が期待と一致しなければならない");

            std::vector<uint32_t> actual;
            for (uint32_t index = 0; index < count && expectedOffset + index < layout.ListCapacity; ++index)
            {
                actual.push_back(outputs.List[expectedOffset + index]);
            }
            std::sort(actual.begin(), actual.end());
            const bool bListOk = actual == expected;
            if (!bListOk)
            {
                std::cerr << TestName << " " << label << " 材質 " << material << " のタイルの一覧が違う: GPU=[";
                for (const uint32_t tile : actual)
                {
                    std::cerr << tile << " ";
                }
                std::cerr << "]" << std::endl;
            }
            Expect(bListOk, "材質ごとのタイルの一覧（集合）が期待と一致しなければならない");

            expectedOffset += count;
            totalEntries += count;
            if (count != 0)
            {
                maxMaterialPlusOne = material + 1;
            }
        }
        Expect(outputs.Stats[0] == 0, "一覧に入りきらず落としたタイルが 0 のはず");
        Expect(outputs.Stats[1] == expectedOutOfRange, "上限以上の材質の（タイル, 材質）の数が期待と一致しなければならない");
        Expect(outputs.Stats[2] == expectedUnresolved, "記録から引けなかった画素の数が期待と一致しなければならない");
        Expect(outputs.Stats[3] == maxMaterialPlusOne, "見えた最大の材質の番号 + 1 が期待と一致しなければならない");
        Expect(outputs.Stats[4] == totalEntries, "一覧に書いた（タイル, 材質）の数が期待と一致しなければならない");
        Expect(GuardsIntact(outputs, layout), "引数・一覧の外（見張り）へ書いてはならない");
    }

    TexturePtr CreateIdTexture(const DevicePtr& device, const std::vector<uint32_t>& image, const char* name)
    {
        TextureDesc desc;
        desc.Width = ImageWidth;
        desc.Height = ImageHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.TextureFormat = Format::R32_UINT;
        desc.Dimension = TextureDimension::Texture2D;
        desc.Usage = ResourceUsage::ShaderResource;
        desc.DebugName = name;
        TexturePtr texture = device->CreateTexture(desc);
        if (!texture)
        {
            return nullptr;
        }
        texture->Update(image.data(), ImageWidth * sizeof(uint32_t), ImageWidth * ImageHeight * sizeof(uint32_t));
        return texture;
    }

    int RunTest()
    {
        if (IsGpuTestSkipForced())
        {
            return SkipGpuTest("NORVESLIB_FORCE_GPU_TEST_SKIP=1 が指定された");
        }

        VulkanValidationErrorCapture validationCapture;
        RHIDeviceDesc deviceDesc;
        deviceDesc.Api = GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = true;
        DevicePtr device = RHI::CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            return SkipGpuTest("Vulkanデバイスを利用できません");
        }

        ShaderManager shaderManager;
        String shaderRoot(NORVES_SOURCE_ROOT);
        shaderRoot += "/Assets/Shaders";
        if (!shaderManager.Initialize(device.get(), shaderRoot))
        {
            std::cerr << TestName << " ShaderManagerを初期化できませんでした" << std::endl;
            return 1;
        }

        {
            MaterialTileClassify classify;
            if (!classify.Initialize(device.get(), &shaderManager))
            {
                std::cerr << TestName << " 材質のタイル分類を初期化できませんでした" << std::endl;
                return 1;
            }

            const VisibilityBuffer::RecordTable table = BuildRecordTable();
            BufferPtr recordTable = device->CreateBuffer(
                BufferDesc(table.SizeInBytes(), ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead, true,
                           "MaterialTileTestRecords"));
            if (!recordTable)
            {
                std::cerr << TestName << " 記録の表のバッファを作れませんでした" << std::endl;
                return 1;
            }
            void* mappedTable = recordTable->Map(0u, table.SizeInBytes());
            if (mappedTable == nullptr)
            {
                std::cerr << TestName << " 記録の表をマップできませんでした" << std::endl;
                return 1;
            }
            std::memcpy(mappedTable, table.Data(), static_cast<size_t>(table.SizeInBytes()));
            recordTable->Unmap();

            const std::vector<uint32_t> image = BuildImage();
            TexturePtr idTexture = CreateIdTexture(device, image, "MaterialTileTestIds");
            if (!idTexture)
            {
                std::cerr << TestName << " ID のテクスチャを作れませんでした" << std::endl;
                return 1;
            }

            // ----- ケース A -----
            {
                Outputs outputs;
                MaterialTiles::Layout layout;
                if (!RunClassify(device, classify, idTexture, recordTable, table.SizeInBytes(),
                                 MaterialTiles::MAX_MATERIALS_PER_TILE, 0, outputs, layout))
                {
                    return 1;
                }
                // 手で導いた一覧と、記録の表だけを使う CPU の参照が一致している（想定そのものの確認）
                const Reference reference = BuildReference(image, table, TestMaxMaterials);
                Expect(reference.Lists == ExpectedLists, "CPU の参照が手で導いた一覧と一致しなければならない");
                Expect(reference.Unresolved == ExpectedUnresolvedPixels, "CPU の参照の引けない画素の数が想定と違う");
                Expect(reference.OutOfRange == 1, "上限以上の材質が出るタイルは 1 つのはず（タイル 6 の材質 12）");

                CheckAgainstLists("A", outputs, layout, reference.Lists, reference.OutOfRange, reference.Unresolved);
                // 空のタイル（1・11・13）と引けない ID だけのタイル（7）は、どの材質の一覧にも出ない
                for (uint32_t material = 0; material < TestMaxMaterials; ++material)
                {
                    const uint32_t offset = ArgWord(outputs, material, 3);
                    for (uint32_t index = 0; index < ArgWord(outputs, material, 0) && offset + index < layout.ListCapacity;
                         ++index)
                    {
                        const uint32_t tile = outputs.List[offset + index];
                        Expect(tile != 1 && tile != 7 && tile != 11 && tile != 13, "空のタイルはどの材質にも入ってはならない");
                    }
                }
                std::cout << TestName << " ケース A: タイル=" << layout.TileCount << " 一覧に書いた数=" << outputs.Stats[4]
                          << " 上限外=" << outputs.Stats[1] << " 引けない画素=" << outputs.Stats[2] << std::endl;
            }

            // ----- ケース B: 一覧が足りない -----
            {
                Outputs outputs;
                MaterialTiles::Layout layout;
                if (!RunClassify(device, classify, idTexture, recordTable, table.SizeInBytes(), 1, 1, outputs, layout))
                {
                    return 1;
                }
                Expect(layout.ListCapacity == 15, "1 タイル 1 つの見込みなら一覧の大きさは 15 のはず");
                Expect(outputs.bRecorded, "一覧が足りなくても分類は記録できなければならない");
                // 材質の番号の順に詰めて切り詰める: 材質 0〜2 は 4 つずつ、材質 3 は 2 つ、材質 4 は 1 つ、以降は 0
                const uint32_t expectedCounts[TestMaxMaterials] = {4, 4, 4, 2, 1, 0, 0, 0};
                const uint32_t expectedOffsets[TestMaxMaterials] = {0, 4, 8, 12, 14, 15, 15, 15};
                for (uint32_t material = 0; material < TestMaxMaterials; ++material)
                {
                    const bool bOk = ArgWord(outputs, material, 0) == expectedCounts[material] &&
                                     ArgWord(outputs, material, 1) == 1u && ArgWord(outputs, material, 2) == 1u &&
                                     ArgWord(outputs, material, 3) == expectedOffsets[material];
                    if (!bOk)
                    {
                        std::cerr << TestName << " ケース B 材質 " << material << " の引数が違う: GPU=("
                                  << ArgWord(outputs, material, 0) << "," << ArgWord(outputs, material, 3) << ")" << std::endl;
                    }
                    Expect(bOk, "一覧が足りないとき、引数は一覧に収まる数へ切り詰められなければならない");
                    // 切り詰めた分も、書いたタイルは本来の一覧の部分集合
                    for (uint32_t index = 0; index < expectedCounts[material]; ++index)
                    {
                        const uint32_t tile = outputs.List[expectedOffsets[material] + index];
                        const std::vector<uint32_t>& full = ExpectedLists[material];
                        Expect(std::find(full.begin(), full.end(), tile) != full.end(),
                               "切り詰めた一覧のタイルは、その材質に出るタイルでなければならない");
                    }
                }
                Expect(outputs.Stats[0] == 8, "落としたタイルの数は 8 のはず（材質 4 が 1、材質 5 が 3、材質 6・7 が 2 ずつ）");
                Expect(outputs.Stats[4] == 15, "一覧に書いた数は一覧の大きさのはず");
                Expect(GuardsIntact(outputs, layout), "一覧が足りなくても、一覧の外へ書いてはならない");
                std::cout << TestName << " ケース B: 一覧の大きさ=" << layout.ListCapacity << " 落とした数=" << outputs.Stats[0]
                          << std::endl;
            }

            // ----- ケース C: 全部が空 -----
            {
                const std::vector<uint32_t> emptyImage(ImageWidth * ImageHeight, VisibilityBuffer::EMPTY_ID);
                TexturePtr emptyTexture = CreateIdTexture(device, emptyImage, "MaterialTileTestEmpty");
                if (!emptyTexture)
                {
                    std::cerr << TestName << " 空の ID のテクスチャを作れませんでした" << std::endl;
                    return 1;
                }
                Outputs outputs;
                MaterialTiles::Layout layout;
                if (!RunClassify(device, classify, emptyTexture, recordTable, table.SizeInBytes(),
                                 MaterialTiles::MAX_MATERIALS_PER_TILE, 2, outputs, layout))
                {
                    return 1;
                }
                const std::vector<std::vector<uint32_t>> none(TestMaxMaterials);
                CheckAgainstLists("C", outputs, layout, none, 0, 0);
            }

            // ----- 範囲が合わない入力は記録しない -----
            {
                CommandListPtr commandList = device->CreateCommandList();
                MaterialTileClassifyDispatch bad;
                bad.IdTexture = idTexture;
                bad.RecordTable = recordTable;
                bad.RecordTableBytes = table.SizeInBytes();
                bad.Width = ImageWidth;
                bad.Height = ImageHeight;
                bad.Layout = MaterialTiles::ComputeLayout(ImageWidth, ImageHeight, TestMaxMaterials);
                classify.BeginFrame(3);
                commandList->Begin();
                Expect(!classify.Record(commandList.get(), bad), "出力のバッファが無い分類は記録してはならない");

                const ResourceUsage usage = ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
                bad.Args = device->CreateBuffer(BufferDesc(16, usage, true, "MaterialTileTestSmallArgs"));
                bad.List = device->CreateBuffer(BufferDesc(16, usage, true, "MaterialTileTestSmallList"));
                bad.Cursors = device->CreateBuffer(BufferDesc(16, usage, true, "MaterialTileTestSmallCursors"));
                bad.Stats = device->CreateBuffer(BufferDesc(MaterialTiles::STATS_BYTES, usage, true, "MaterialTileTestStats2"));
                Expect(!classify.Record(commandList.get(), bad), "Layout より小さいバッファへの分類は記録してはならない");
                commandList->End();
            }

            // ----- RecordClear: 引数と統計を 0 にする -----
            {
                const ResourceUsage usage = ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
                BufferPtr args = device->CreateBuffer(BufferDesc(64, usage, true, "MaterialTileTestClearArgs"));
                BufferPtr stats = device->CreateBuffer(
                    BufferDesc(MaterialTiles::STATS_BYTES, usage, true, "MaterialTileTestClearStats"));
                CommandListPtr commandList = device->CreateCommandList();
                if (!args || !stats || !commandList)
                {
                    std::cerr << TestName << " RecordClear の資源を作れませんでした" << std::endl;
                    return 1;
                }
                for (const BufferPtr& buffer : {args, stats})
                {
                    uint32_t* mapped = static_cast<uint32_t*>(buffer->Map(0u, buffer->GetSize()));
                    for (uint64_t word = 0; mapped && word < buffer->GetSize() / sizeof(uint32_t); ++word)
                    {
                        mapped[word] = GuardWord;
                    }
                    buffer->Unmap();
                }
                commandList->Begin();
                commandList->BufferBarrier(args, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, args->GetSize());
                commandList->BufferBarrier(stats, ResourceState::Undefined, ResourceState::UnorderedAccess, 0u, stats->GetSize());
                Expect(MaterialTileClassify::RecordClear(commandList.get(), args, stats), "引数と統計を 0 にできなければならない");
                Expect(!MaterialTileClassify::RecordClear(commandList.get(), nullptr, stats), "引数が無ければ 0 にできない");
                commandList->BufferBarrier(args, ResourceState::UnorderedAccess, ResourceState::HostRead, 0u, args->GetSize());
                commandList->BufferBarrier(stats, ResourceState::UnorderedAccess, ResourceState::HostRead, 0u, stats->GetSize());
                commandList->End();
                commandList->Submit(true);
                device->WaitIdle();
                for (const BufferPtr& buffer : {args, stats})
                {
                    const uint32_t* mapped = static_cast<const uint32_t*>(buffer->Map(0u, buffer->GetSize()));
                    bool bAllZero = mapped != nullptr;
                    for (uint64_t word = 0; mapped && word < buffer->GetSize() / sizeof(uint32_t); ++word)
                    {
                        bAllZero = bAllZero && mapped[word] == 0u;
                    }
                    buffer->Unmap();
                    Expect(bAllZero, "RecordClear の後、引数と統計はすべて 0 でなければならない");
                }
            }

            device->WaitIdle();
        }
        shaderManager.Shutdown();

        const uint32_t validationErrorCount = validationCapture.GetHitCount();
        std::cout << "VUID_COUNT=" << validationErrorCount << '\n';
        Expect(validationErrorCount == 0u, "Vulkan の検証エラーが出てはならない");

        std::cout << (g_failures == 0 ? "RESULT=PASS" : "RESULT=FAIL") << '\n';
        return g_failures == 0 ? 0 : 1;
    }
} // namespace

int main()
{
    try
    {
        return RunTest();
    }
    catch (const std::exception& exception)
    {
        std::cerr << TestName << "で例外が出ました: " << exception.what() << '\n';
        return 1;
    }
}
