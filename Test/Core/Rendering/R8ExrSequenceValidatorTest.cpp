// R8のEXR連番の検査の契約。合成の連番（背景の勾配の上を明るい円が一定の速さで動く）で、正常な連番は合格し、
// 欠番・非有限の画素（フレームと座標）・寸法の違い・差し込んだ1フレームの跳び（ポッピング）をそれぞれ検出すること。
// LUTのCPUの写像が0を0付近へ写し、灰色の段階で単調であること。
#include "R8ExrSequenceValidation.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>

#include <Windows.h>

#include "exr.h"

namespace
{
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr uint32_t Width = 64u;
    constexpr uint32_t Height = 48u;
    constexpr uint32_t FrameCount = 8u;
    constexpr float DiscSpeed = 1.5f;
    constexpr float DiscRadius = 6.0f;
    constexpr const char* SceneName = "synthetic";

    int g_Failures = 0;

    void Expect(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            ++g_Failures;
            std::cerr << "FAILED: " << message << '\n';
        }
    }

    bool Contains(const VariableArray<uint32_t>& values, uint32_t value)
    {
        for (uint32_t current : values)
        {
            if (current == value)
            {
                return true;
            }
        }
        return false;
    }

    // 縁を1画素で滑らかにした円（画素の被覆の近似）を背景の勾配へ重ねる。
    RgbaFloatImage MakeFrame(uint32_t width, uint32_t height, float discX)
    {
        RgbaFloatImage image;
        image.Width = width;
        image.Height = height;
        image.Values.assign(static_cast<size_t>(width) * height * 4u, 1.0f);
        const float discY = 0.5f * static_cast<float>(height);
        for (uint32_t y = 0u; y < height; ++y)
        {
            for (uint32_t x = 0u; x < width; ++x)
            {
                const float dx = static_cast<float>(x) + 0.5f - discX;
                const float dy = static_cast<float>(y) + 0.5f - discY;
                const float coverage =
                    std::fmin(std::fmax(DiscRadius + 0.5f - std::sqrt(dx * dx + dy * dy), 0.0f), 1.0f);
                const float background = 0.05f + 0.4f * static_cast<float>(x) / width;
                const size_t base = (static_cast<size_t>(y) * width + x) * 4u;
                image.Values[base] = background + coverage * (2.0f - background);
                image.Values[base + 1u] = 0.8f * background + coverage * (1.6f - 0.8f * background);
                image.Values[base + 2u] = 0.6f * background + coverage * (1.2f - 0.6f * background);
            }
        }
        return image;
    }

    // 製品の書き出し（WritePathTracingExrFrame）と同じB・G・Rのfloat、ZIP scanlineで書く。非有限の値も拒まない。
    bool WriteFrame(const char* directory, uint32_t frame, const RgbaFloatImage& image)
    {
        char path[MAX_PATH] = {};
        std::snprintf(path, sizeof(path), "%s/%s_seed%08x_spp%06u_frame%06u.exr", directory, SceneName,
                      0x1234u, 64u, frame);
        exr_channel channels[3] = {};
        std::memcpy(channels[0].name, "B", 2u);
        std::memcpy(channels[1].name, "G", 2u);
        std::memcpy(channels[2].name, "R", 2u);
        for (exr_channel& channel : channels)
        {
            channel.pixel_type = EXR_PIXEL_FLOAT;
            channel.x_sampling = 1;
            channel.y_sampling = 1;
        }
        exr_header header = {};
        header.part_type = EXR_PART_SCANLINE;
        header.compression = EXR_COMPRESSION_ZIP;
        header.line_order = EXR_LINEORDER_INCREASING_Y;
        header.data_window = {0, 0, static_cast<int32_t>(image.Width - 1u), static_cast<int32_t>(image.Height - 1u)};
        header.display_window = header.data_window;
        header.pixel_aspect_ratio = 1.0f;
        header.screen_window_width = 1.0f;
        header.num_channels = 3;
        header.channels = channels;
        exr_writer* writer = nullptr;
        if (exr_writer_create(nullptr, &writer) != EXR_SUCCESS)
        {
            return false;
        }
        constexpr uint32_t ScanlinesPerBlock = 16u;
        bool bSuccess = exr_writer_add_part(writer, &header, nullptr) == EXR_SUCCESS &&
                        exr_writer_begin_stream_file(writer, path, EXR_COMPRESSION_ZIP) == EXR_SUCCESS;
        VariableArray<float> blue(static_cast<size_t>(image.Width) * ScanlinesPerBlock);
        VariableArray<float> green(blue.size());
        VariableArray<float> red(blue.size());
        for (uint32_t y = 0u; y < image.Height && bSuccess; y += ScanlinesPerBlock)
        {
            const uint32_t rows = image.Height - y < ScanlinesPerBlock ? image.Height - y : ScanlinesPerBlock;
            for (uint32_t row = 0u; row < rows; ++row)
            {
                for (uint32_t x = 0u; x < image.Width; ++x)
                {
                    const size_t source = (static_cast<size_t>(y + row) * image.Width + x) * 4u;
                    const size_t destination = static_cast<size_t>(row) * image.Width + x;
                    blue[destination] = image.Values[source + 2u];
                    green[destination] = image.Values[source + 1u];
                    red[destination] = image.Values[source];
                }
            }
            const void* channelRows[3] = {blue.data(), green.data(), red.data()};
            bSuccess = exr_writer_write_scanline_block(writer, 0, static_cast<int32_t>(y), channelRows) == EXR_SUCCESS;
        }
        if (bSuccess)
        {
            bSuccess = exr_writer_end_stream(writer) == EXR_SUCCESS;
        }
        exr_writer_destroy(writer);
        return bSuccess;
    }

    // 検査ごとのdirectoryを作り、前回のEXRを消す。
    bool PrepareDirectory(const char* name, char (&outDirectory)[MAX_PATH])
    {
        const char* root = NORVES_BINARY_ROOT "/RenderingValidation/R8ExrSequenceValidatorTestRuns";
        CreateDirectoryA(NORVES_BINARY_ROOT "/RenderingValidation", nullptr);
        CreateDirectoryA(root, nullptr);
        std::snprintf(outDirectory, sizeof(outDirectory), "%s/%s", root, name);
        CreateDirectoryA(outDirectory, nullptr);
        char pattern[MAX_PATH] = {};
        std::snprintf(pattern, sizeof(pattern), "%s/*.exr", outDirectory);
        WIN32_FIND_DATAA findData = {};
        HANDLE find = FindFirstFileA(pattern, &findData);
        if (find != INVALID_HANDLE_VALUE)
        {
            do
            {
                char path[MAX_PATH] = {};
                std::snprintf(path, sizeof(path), "%s/%s", outDirectory, findData.cFileName);
                DeleteFileA(path);
            } while (FindNextFileA(find, &findData));
            FindClose(find);
        }
        const DWORD attributes = GetFileAttributesA(outDirectory);
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u;
    }

    enum class Defect
    {
        None,
        Missing,
        NonFinite,
        Dimension,
        Popping
    };

    constexpr uint32_t MissingFrame = 3u;
    constexpr uint32_t NonFiniteFrame = 5u;
    constexpr uint32_t NonFiniteX = 10u;
    constexpr uint32_t NonFiniteY = 7u;
    constexpr uint32_t DimensionFrame = 2u;
    constexpr uint32_t PoppingFrame = 4u;

    bool RunCase(const char* name, Defect defect, const AcesOutputLut& lut, ExrSequenceReport& outReport)
    {
        char directory[MAX_PATH] = {};
        if (!PrepareDirectory(name, directory))
        {
            std::cerr << "directoryを作れません: " << name << '\n';
            return false;
        }
        for (uint32_t frame = 0u; frame < FrameCount; ++frame)
        {
            if (defect == Defect::Missing && frame == MissingFrame)
            {
                continue;
            }
            float discX = 12.0f + DiscSpeed * static_cast<float>(frame);
            if (defect == Defect::Popping && frame == PoppingFrame)
            {
                discX += 24.0f;
            }
            const uint32_t height = defect == Defect::Dimension && frame == DimensionFrame ? Height - 8u : Height;
            RgbaFloatImage image = MakeFrame(Width, height, discX);
            if (defect == Defect::NonFinite && frame == NonFiniteFrame)
            {
                image.Values[(static_cast<size_t>(NonFiniteY) * Width + NonFiniteX) * 4u + 1u] =
                    std::numeric_limits<float>::quiet_NaN();
            }
            if (!WriteFrame(directory, frame, image))
            {
                std::cerr << "EXRを書けません: " << name << " frame=" << frame << '\n';
                return false;
            }
        }
        const ExrSequenceRules rules;
        if (!ValidateExrSequence(directory, SceneName, 0u, FrameCount, lut, 1.0f, rules, outReport))
        {
            std::cerr << "検査を実行できません: " << name << '\n';
            return false;
        }
        std::cout << "case=" << name << '\n';
        PrintExrSequenceReport(outReport);
        return true;
    }
} // namespace

int main()
{
    AcesOutputLut lut;
    if (!LoadAcesOutputLut(NORVES_SOURCE_ROOT "/Assets/ColorManagement/Aces20SdrRec709.lut3d", lut))
    {
        std::cerr << "LUTを読めません\n";
        return 1;
    }
    Expect(lut.Size == 65u, "LUTの格子は65");

    // 0は0付近へ写り、灰色の段階は単調に明るくなる。
    const float black[3] = {0.0f, 0.0f, 0.0f};
    float mapped[3] = {};
    ApplyAcesOutputLut(lut, black, mapped);
    Expect(std::fabs(mapped[0]) < 1.0e-3f && std::fabs(mapped[1]) < 1.0e-3f && std::fabs(mapped[2]) < 1.0e-3f,
           "0はdisplayの0付近へ写る");
    float previous = -1.0f;
    for (float grey = 0.001f; grey < 200.0f; grey *= 2.0f)
    {
        const float input[3] = {grey, grey, grey};
        ApplyAcesOutputLut(lut, input, mapped);
        Expect(mapped[1] > previous, "灰色の段階で単調に明るくなる");
        previous = mapped[1];
    }

    ExrSequenceReport report;
    if (RunCase("normal", Defect::None, lut, report))
    {
        Expect(report.Passed(), "正常な連番は合格する");
        Expect(report.Width == Width && report.Height == Height, "連番の寸法を読む");
        Expect(report.AdjacentMeanFlip.size() == FrameCount - 1u, "隣接の組をすべて比べる");
        Expect(report.MedianFlip > 0.0, "動く円で隣接フレームの差が出る");
    }
    else
    {
        Expect(false, "正常な連番を検査できる");
    }

    if (RunCase("missing", Defect::Missing, lut, report))
    {
        Expect(!report.Passed(), "欠番のある連番は失敗する");
        Expect(report.MissingFrames.size() == 1u && Contains(report.MissingFrames, MissingFrame),
               "欠番のフレーム番号を出す");
        Expect(report.PoppingFrames.empty(), "欠番の前後はポッピングとして比べない");
    }
    else
    {
        Expect(false, "欠番の連番を検査できる");
    }

    if (RunCase("nonfinite", Defect::NonFinite, lut, report))
    {
        Expect(!report.Passed(), "非有限の画素がある連番は失敗する");
        Expect(report.NonFiniteFrames.size() == 1u && report.NonFiniteFrames[0].Frame == NonFiniteFrame &&
                   report.NonFiniteFrames[0].X == NonFiniteX && report.NonFiniteFrames[0].Y == NonFiniteY &&
                   report.NonFiniteFrames[0].Channel == 1u && report.NonFiniteFrames[0].Count == 1u,
               "非有限の画素のフレームと座標を出す");
    }
    else
    {
        Expect(false, "非有限の画素の連番を検査できる");
    }

    if (RunCase("dimension", Defect::Dimension, lut, report))
    {
        Expect(!report.Passed(), "寸法の違うフレームがある連番は失敗する");
        Expect(report.DimensionMismatchFrames.size() == 1u && Contains(report.DimensionMismatchFrames, DimensionFrame),
               "寸法の違うフレーム番号を出す");
    }
    else
    {
        Expect(false, "寸法の違う連番を検査できる");
    }

    if (RunCase("popping", Defect::Popping, lut, report))
    {
        Expect(!report.Passed(), "1フレームの跳びがある連番は失敗する");
        Expect(Contains(report.PoppingFrames, PoppingFrame) && Contains(report.PoppingFrames, PoppingFrame + 1u),
               "跳んだフレームの前後の組をポッピングとして出す");
        Expect(report.PoppingFrames.size() == 2u, "跳びの無い組はポッピングにしない");
    }
    else
    {
        Expect(false, "跳びのある連番を検査できる");
    }

    if (g_Failures != 0)
    {
        std::cerr << "R8ExrSequenceValidatorTest failures=" << g_Failures << '\n';
        return 1;
    }
    std::cout << "R8ExrSequenceValidatorTest passed\n";
    return 0;
}
