#include "R8ExrSequenceValidation.h"

#include "RenderingValidation/RenderingPerceptualDiff.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>

#include <Windows.h>

#include "exr.h"

namespace NorvesLib::Test::RenderingValidation
{
    using Core::Container::VariableArray;

    namespace
    {
        constexpr char LutMagic[8] = {'N', 'L', 'U', 'T', '3', 'D', '0', '1'};
        constexpr uint32_t LutFormatRgba16F = 1u;
        constexpr size_t LutHeaderBytes = 32u;

        uint8_t EncodeSrgbByte(double linear)
        {
            const double clamped = std::clamp(linear, 0.0, 1.0);
            const double encoded = clamped <= 0.0031308 ? 12.92 * clamped
                                                        : 1.055 * std::pow(clamped, 1.0 / 2.4) - 0.055;
            return static_cast<uint8_t>(std::lround(std::clamp(encoded, 0.0, 1.0) * 255.0));
        }

        bool ReadWholeFile(const char* path, VariableArray<uint8_t>& outBytes)
        {
            FILE* file = nullptr;
            if (fopen_s(&file, path, "rb") != 0 || !file)
            {
                return false;
            }
            std::fseek(file, 0, SEEK_END);
            const long size = std::ftell(file);
            std::fseek(file, 0, SEEK_SET);
            bool bSuccess = size > 0;
            if (bSuccess)
            {
                outBytes.resize(static_cast<size_t>(size));
                bSuccess = std::fread(outBytes.data(), 1u, outBytes.size(), file) == outBytes.size();
            }
            std::fclose(file);
            return bSuccess;
        }

        // `..._frame<数字>.exr` のフレーム番号を読む。
        bool ParseFrameIndex(const char* fileName, uint32_t& outFrame)
        {
            const char* marker = nullptr;
            for (const char* search = std::strstr(fileName, "_frame"); search;
                 search = std::strstr(search + 1, "_frame"))
            {
                marker = search;
            }
            if (!marker)
            {
                return false;
            }
            const char* digits = marker + 6;
            uint64_t value = 0u;
            const char* cursor = digits;
            while (*cursor >= '0' && *cursor <= '9')
            {
                value = value * 10u + static_cast<uint64_t>(*cursor - '0');
                ++cursor;
            }
            if (cursor == digits || value > UINT32_MAX || std::strcmp(cursor, ".exr") != 0)
            {
                return false;
            }
            outFrame = static_cast<uint32_t>(value);
            return true;
        }

        struct FrameFile
        {
            uint32_t Frame = 0u;
            char Path[MAX_PATH] = {};
        };

        double Median(VariableArray<double> values)
        {
            if (values.empty())
            {
                return 0.0;
            }
            std::sort(values.begin(), values.end());
            const size_t middle = values.size() / 2u;
            return values.size() % 2u == 1u ? values[middle] : 0.5 * (values[middle - 1u] + values[middle]);
        }

        void PrintFrameList(const char* key, const VariableArray<uint32_t>& frames)
        {
            std::cout << key << "=" << frames.size();
            for (size_t index = 0u; index < frames.size() && index < 16u; ++index)
            {
                std::cout << (index == 0u ? " frames=" : ",") << frames[index];
            }
            std::cout << '\n';
        }
    } // namespace

    bool LoadAcesOutputLut(const char* path, AcesOutputLut& outLut)
    {
        VariableArray<uint8_t> bytes;
        if (!path || !ReadWholeFile(path, bytes) || bytes.size() < LutHeaderBytes ||
            std::memcmp(bytes.data(), LutMagic, sizeof(LutMagic)) != 0)
        {
            return false;
        }
        uint32_t size = 0u;
        uint32_t channels = 0u;
        uint32_t format = 0u;
        float shaperOffset = 0.0f;
        float shaperMax = 0.0f;
        std::memcpy(&size, bytes.data() + 8u, 4u);
        std::memcpy(&channels, bytes.data() + 12u, 4u);
        std::memcpy(&format, bytes.data() + 16u, 4u);
        std::memcpy(&shaperOffset, bytes.data() + 24u, 4u);
        std::memcpy(&shaperMax, bytes.data() + 28u, 4u);
        const size_t lattice = static_cast<size_t>(size) * size * size;
        if (size < 2u || channels != 4u || format != LutFormatRgba16F || !(shaperOffset > 0.0f) ||
            !(shaperMax > shaperOffset) || bytes.size() != LutHeaderBytes + lattice * channels * 2u)
        {
            return false;
        }
        outLut.Size = size;
        outLut.ShaperOffset = shaperOffset;
        outLut.ShaperMax = shaperMax;
        outLut.Values.resize(lattice * 3u);
        for (size_t point = 0u; point < lattice; ++point)
        {
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                uint16_t bits = 0u;
                std::memcpy(&bits, bytes.data() + LutHeaderBytes + (point * 4u + channel) * 2u, 2u);
                outLut.Values[point * 3u + channel] = DecodeIeee754Binary16(bits);
            }
        }
        return true;
    }

    void ApplyAcesOutputLut(const AcesOutputLut& lut, const float sceneLinear[3], float outDisplayLinear[3])
    {
        const double span = std::log2(static_cast<double>(lut.ShaperMax) / lut.ShaperOffset + 1.0);
        const double last = static_cast<double>(lut.Size - 1u);
        uint32_t base[3] = {};
        double fraction[3] = {};
        for (uint32_t axis = 0u; axis < 3u; ++axis)
        {
            const double value = std::isfinite(sceneLinear[axis])
                                     ? std::clamp(static_cast<double>(sceneLinear[axis]), 0.0,
                                                  static_cast<double>(lut.ShaperMax))
                                     : 0.0;
            const double shaped = std::clamp(std::log2(value / lut.ShaperOffset + 1.0) / span, 0.0, 1.0);
            const double position = shaped * last;
            base[axis] = std::min(static_cast<uint32_t>(position), lut.Size - 2u);
            fraction[axis] = position - base[axis];
        }
        const size_t size = lut.Size;
        for (uint32_t channel = 0u; channel < 3u; ++channel)
        {
            double result = 0.0;
            for (uint32_t corner = 0u; corner < 8u; ++corner)
            {
                const uint32_t red = base[0] + (corner & 1u);
                const uint32_t green = base[1] + ((corner >> 1u) & 1u);
                const uint32_t blue = base[2] + ((corner >> 2u) & 1u);
                const double weight = ((corner & 1u) ? fraction[0] : 1.0 - fraction[0]) *
                                      (((corner >> 1u) & 1u) ? fraction[1] : 1.0 - fraction[1]) *
                                      (((corner >> 2u) & 1u) ? fraction[2] : 1.0 - fraction[2]);
                result += weight * lut.Values[((blue * size + green) * size + red) * 3u + channel];
            }
            outDisplayLinear[channel] = static_cast<float>(result);
        }
    }

    Rgba8Image ToDisplayLdr(const AcesOutputLut& lut, const RgbaFloatImage& sceneLinear, float exposure)
    {
        Rgba8Image result;
        result.Width = sceneLinear.Width;
        result.Height = sceneLinear.Height;
        result.RowPitchBytes = sceneLinear.Width * 4u;
        const size_t pixelCount = static_cast<size_t>(sceneLinear.Width) * sceneLinear.Height;
        result.Pixels.resize(pixelCount * 4u);
        for (size_t pixel = 0u; pixel < pixelCount; ++pixel)
        {
            const float exposed[3] = {sceneLinear.Values[pixel * 4u] * exposure,
                                      sceneLinear.Values[pixel * 4u + 1u] * exposure,
                                      sceneLinear.Values[pixel * 4u + 2u] * exposure};
            float display[3] = {};
            ApplyAcesOutputLut(lut, exposed, display);
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                result.Pixels[pixel * 4u + channel] = EncodeSrgbByte(display[channel]);
            }
            result.Pixels[pixel * 4u + 3u] = 255u;
        }
        return result;
    }

    bool ReadExrRgba(const char* path, RgbaFloatImage& outImage)
    {
        exr_image image = {};
        if (!path || exr_load_from_file(path, nullptr, &image) != EXR_SUCCESS)
        {
            return false;
        }
        bool bSuccess = image.num_parts >= 1 && image.parts && !image.parts[0].is_deep;
        float* interleaved = nullptr;
        int width = 0;
        int height = 0;
        int channels = 0;
        if (bSuccess)
        {
            bSuccess = exr_part_to_rgba_float(nullptr, &image.parts[0], &interleaved, &width, &height,
                                              &channels) == EXR_SUCCESS &&
                       interleaved && width > 0 && height > 0 && channels > 0;
        }
        if (bSuccess)
        {
            // interleavedの成分は見出しのchannelの順（名前順）。
            int source[4] = {-1, -1, -1, -1};
            const exr_header& header = image.parts[0].header;
            for (int channel = 0; channel < header.num_channels && channel < channels; ++channel)
            {
                const char* name = header.channels[channel].name;
                const int target = std::strcmp(name, "R") == 0   ? 0
                                   : std::strcmp(name, "G") == 0 ? 1
                                   : std::strcmp(name, "B") == 0 ? 2
                                   : std::strcmp(name, "A") == 0 ? 3
                                                                 : -1;
                if (target >= 0)
                {
                    source[target] = channel;
                }
            }
            outImage.Width = static_cast<uint32_t>(width);
            outImage.Height = static_cast<uint32_t>(height);
            const size_t pixelCount = static_cast<size_t>(width) * height;
            outImage.Values.assign(pixelCount * 4u, 0.0f);
            for (size_t pixel = 0u; pixel < pixelCount; ++pixel)
            {
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    outImage.Values[pixel * 4u + channel] =
                        source[channel] >= 0 ? interleaved[pixel * channels + source[channel]]
                                             : (channel == 3u ? 1.0f : 0.0f);
                }
            }
        }
        std::free(interleaved);
        exr_image_free(&image);
        return bSuccess;
    }

    bool ExrSequenceReport::Passed() const
    {
        return ExpectedFrames > 0u && FoundFrames == ExpectedFrames && MissingFrames.empty() &&
               DuplicateFrames.empty() && UnreadableFrames.empty() && DimensionMismatchFrames.empty() &&
               NonFiniteFrames.empty() && PoppingFrames.empty();
    }

    bool ValidateExrSequence(const char* directory,
                             const char* sceneName,
                             uint32_t firstFrame,
                             uint32_t expectedFrameCount,
                             const AcesOutputLut& lut,
                             float exposure,
                             const ExrSequenceRules& rules,
                             ExrSequenceReport& outReport)
    {
        outReport = ExrSequenceReport{};
        outReport.FirstFrame = firstFrame;
        outReport.ExpectedFrames = expectedFrameCount;
        if (!directory || !sceneName || expectedFrameCount == 0u || lut.Size < 2u)
        {
            return false;
        }
        char pattern[MAX_PATH] = {};
        const int patternLength =
            std::snprintf(pattern, sizeof(pattern), "%s/%s_seed*_spp*_frame*.exr", directory, sceneName);
        if (patternLength < 0 || patternLength >= static_cast<int>(sizeof(pattern)))
        {
            return false;
        }
        const DWORD attributes = GetFileAttributesA(directory);
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u)
        {
            return false;
        }

        // 期待する範囲のフレームごとに、見つけたファイルを1つ割り当てる（同じ番号が2つあれば重複）。
        VariableArray<FrameFile> files(expectedFrameCount);
        VariableArray<uint8_t> bFound(expectedFrameCount, 0u);
        WIN32_FIND_DATAA findData = {};
        HANDLE find = FindFirstFileA(pattern, &findData);
        if (find != INVALID_HANDLE_VALUE)
        {
            do
            {
                uint32_t frame = 0u;
                if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u ||
                    !ParseFrameIndex(findData.cFileName, frame) || frame < firstFrame ||
                    frame - firstFrame >= expectedFrameCount)
                {
                    continue;
                }
                const uint32_t slot = frame - firstFrame;
                if (bFound[slot] != 0u)
                {
                    outReport.DuplicateFrames.push_back(frame);
                    continue;
                }
                bFound[slot] = 1u;
                files[slot].Frame = frame;
                std::snprintf(files[slot].Path, sizeof(files[slot].Path), "%s/%s", directory, findData.cFileName);
            } while (FindNextFileA(find, &findData));
            FindClose(find);
        }

        Rgba8Image previous;
        bool bHasPrevious = false;
        uint32_t previousFrame = 0u;
        for (uint32_t slot = 0u; slot < expectedFrameCount; ++slot)
        {
            const uint32_t frame = firstFrame + slot;
            if (bFound[slot] == 0u)
            {
                outReport.MissingFrames.push_back(frame);
                bHasPrevious = false;
                continue;
            }
            ++outReport.FoundFrames;
            RgbaFloatImage image;
            if (!ReadExrRgba(files[slot].Path, image))
            {
                outReport.UnreadableFrames.push_back(frame);
                bHasPrevious = false;
                continue;
            }
            // 最初に読めたフレームの寸法を連番の寸法とする。
            if (outReport.Width == 0u)
            {
                outReport.Width = image.Width;
                outReport.Height = image.Height;
            }
            if (image.Width != outReport.Width || image.Height != outReport.Height)
            {
                outReport.DimensionMismatchFrames.push_back(frame);
                bHasPrevious = false;
                continue;
            }
            ExrSequenceNonFinite nonFinite;
            nonFinite.Frame = frame;
            for (size_t index = 0u; index < image.Values.size(); ++index)
            {
                if (!std::isfinite(image.Values[index]))
                {
                    if (nonFinite.Count == 0u)
                    {
                        const size_t pixel = index / 4u;
                        nonFinite.X = static_cast<uint32_t>(pixel % image.Width);
                        nonFinite.Y = static_cast<uint32_t>(pixel / image.Width);
                        nonFinite.Channel = static_cast<uint32_t>(index % 4u);
                    }
                    ++nonFinite.Count;
                }
            }
            if (nonFinite.Count != 0u)
            {
                outReport.NonFiniteFrames.push_back(nonFinite);
            }
            Rgba8Image current = ToDisplayLdr(lut, image, exposure);
            if (bHasPrevious && previousFrame + 1u == frame)
            {
                PerceptualDifferenceMetrics metrics;
                if (CompareLdrFlip(previous, current, metrics) != PerceptualDiffStatus::Success)
                {
                    outReport.UnreadableFrames.push_back(frame);
                }
                else
                {
                    outReport.AdjacentFrames.push_back(frame);
                    outReport.AdjacentMeanFlip.push_back(metrics.MeanFlipError);
                }
            }
            previous = std::move(current);
            bHasPrevious = true;
            previousFrame = frame;
        }

        outReport.MedianFlip = Median(outReport.AdjacentMeanFlip);
        outReport.PoppingLimit = std::max(outReport.MedianFlip * rules.PoppingMedianScale, rules.PoppingFloor);
        for (size_t index = 0u; index < outReport.AdjacentMeanFlip.size(); ++index)
        {
            if (outReport.AdjacentMeanFlip[index] > outReport.PoppingLimit)
            {
                outReport.PoppingFrames.push_back(outReport.AdjacentFrames[index]);
            }
        }
        return true;
    }

    void PrintExrSequenceReport(const ExrSequenceReport& report)
    {
        std::cout << "r8_exr_sequence first=" << report.FirstFrame << " expected=" << report.ExpectedFrames
                  << " found=" << report.FoundFrames << " size=" << report.Width << "x" << report.Height << '\n';
        PrintFrameList("r8_exr_missing", report.MissingFrames);
        PrintFrameList("r8_exr_duplicate", report.DuplicateFrames);
        PrintFrameList("r8_exr_unreadable", report.UnreadableFrames);
        PrintFrameList("r8_exr_dimension_mismatch", report.DimensionMismatchFrames);
        std::cout << "r8_exr_nonfinite_frames=" << report.NonFiniteFrames.size() << '\n';
        for (const ExrSequenceNonFinite& nonFinite : report.NonFiniteFrames)
        {
            std::cout << "r8_exr_nonfinite frame=" << nonFinite.Frame << " count=" << nonFinite.Count
                      << " first=(" << nonFinite.X << "," << nonFinite.Y << ") channel=" << nonFinite.Channel
                      << '\n';
        }
        double maximum = 0.0;
        uint32_t maximumFrame = 0u;
        for (size_t index = 0u; index < report.AdjacentMeanFlip.size(); ++index)
        {
            if (report.AdjacentMeanFlip[index] > maximum)
            {
                maximum = report.AdjacentMeanFlip[index];
                maximumFrame = report.AdjacentFrames[index];
            }
        }
        std::cout << "r8_exr_adjacent_flip pairs=" << report.AdjacentMeanFlip.size()
                  << " median=" << report.MedianFlip << " limit=" << report.PoppingLimit << " max=" << maximum
                  << " max_frame=" << maximumFrame << '\n';
        PrintFrameList("r8_exr_popping", report.PoppingFrames);
        std::cout << "r8_exr_sequence_result=" << (report.Passed() ? "PASS" : "FAIL") << '\n';
    }
} // namespace NorvesLib::Test::RenderingValidation
