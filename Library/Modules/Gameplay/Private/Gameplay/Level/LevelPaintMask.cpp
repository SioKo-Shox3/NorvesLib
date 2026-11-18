// 地形格子に沿う配置編集用マスク、円形ブラシ、PNG 入出力を実装する。
#include "Gameplay/Level/LevelPaintMask.h"

#include "Rendering/Terrain/TerrainHeightField.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <Windows.h>
#include "stb_image.h"
#include "stb_image_write.h"

namespace NorvesLib::Modules::Gameplay::Level
{
    namespace
    {
        using Core::Container::Span;
        using Core::Container::String;
        using Core::Container::VariableArray;
        using Core::Container::WideString;

        bool Fail(String* pOutError, const char* message)
        {
            if (pOutError != nullptr)
            {
                *pOutError = message;
            }
            return false;
        }

        bool GetSampleCount(const LevelPaintMaskLayout& layout, size_t& outCount)
        {
            if (!layout.IsValid() || static_cast<size_t>(layout.Columns) >
                std::numeric_limits<size_t>::max() / static_cast<size_t>(layout.Rows))
            {
                return false;
            }
            outCount = static_cast<size_t>(layout.Columns) * layout.Rows;
            return true;
        }

        bool IsBrushValid(const LevelPaintBrush& brush)
        {
            return std::isfinite(brush.Radius) && brush.Radius > 0.0f &&
                std::isfinite(brush.Strength) && std::isfinite(brush.Falloff);
        }

        double BrushWeight(const LevelPaintBrush& brush, double distance)
        {
            if (!IsBrushValid(brush) || !std::isfinite(distance) || distance > brush.Radius)
            {
                return 0.0;
            }
            const double strength = std::clamp(static_cast<double>(brush.Strength), 0.0, 1.0);
            const double falloff = std::clamp(static_cast<double>(brush.Falloff), 0.0, 1.0);
            if (falloff == 0.0)
            {
                return strength;
            }
            const double radius = brush.Radius;
            const double t = std::clamp(1.0 - (radius - distance) / (radius * falloff), 0.0, 1.0);
            return strength * (1.0 - t * t * (3.0 - 2.0 * t));
        }

        double SampleWorldCoordinate(double first, double spacing, uint32_t index)
        {
            return first + static_cast<double>(index) * spacing;
        }

        // 円の外接矩形を求めるため、各軸の単調な標本位置を二分探索する。
        uint32_t FindSampleBoundary(double first, double spacing, uint32_t count, double value, bool bUpper)
        {
            uint32_t begin = 0;
            uint32_t end = count;
            while (begin < end)
            {
                const uint32_t middle = begin + (end - begin) / 2;
                const double sample = SampleWorldCoordinate(first, spacing, middle);
                if (sample < value || (bUpper && sample == value))
                {
                    begin = middle + 1;
                }
                else
                {
                    end = middle;
                }
            }
            return begin;
        }

        bool DecodePath(const String& path, WideString& outPath)
        {
            if (path.empty() || path.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
                std::strlen(path.c_str()) != path.size())
            {
                return false;
            }
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(),
                                                  static_cast<int>(path.size()), nullptr, 0);
            if (count <= 0)
            {
                return false;
            }
            VariableArray<wchar_t> buffer(static_cast<size_t>(count));
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()),
                                    buffer.data(), count) != count)
            {
                return false;
            }
            outPath.clear();
            outPath.append(buffer.data(), buffer.size());
            return true;
        }

        bool EnsureDirectory(const WideString& path)
        {
            if (CreateDirectoryW(path.c_str(), nullptr))
            {
                return true;
            }
            if (GetLastError() != ERROR_ALREADY_EXISTS)
            {
                return false;
            }
            const DWORD attributes = GetFileAttributesW(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }

        bool EnsureParentDirectories(const WideString& path)
        {
            const DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
            if (required == 0)
            {
                return false;
            }
            VariableArray<wchar_t> buffer(static_cast<size_t>(required));
            const DWORD length = GetFullPathNameW(path.c_str(), required, buffer.data(), nullptr);
            if (length == 0 || length >= required)
            {
                return false;
            }
            WideString fullPath(buffer.data());
            for (size_t i = 0; i < fullPath.size(); ++i)
            {
                if (fullPath[i] == L'/')
                {
                    fullPath[i] = L'\\';
                }
            }

            size_t start = 3;
            if (fullPath.size() >= 2 && fullPath[0] == L'\\' && fullPath[1] == L'\\')
            {
                const bool bExtended = fullPath.size() >= 4 && fullPath[2] == L'?' && fullPath[3] == L'\\';
                const bool bExtendedUnc = bExtended && fullPath.size() >= 8 &&
                    (fullPath[4] == L'U' || fullPath[4] == L'u') &&
                    (fullPath[5] == L'N' || fullPath[5] == L'n') &&
                    (fullPath[6] == L'C' || fullPath[6] == L'c') && fullPath[7] == L'\\';
                if (bExtended && !bExtendedUnc && fullPath.size() >= 7 && fullPath[5] == L':')
                {
                    start = 7;
                }
                else
                {
                    // UNC のサーバー名と共有名は作成対象にしない。
                    start = bExtendedUnc ? 8 : (bExtended ? 4 : 2);
                    unsigned separators = bExtended && !bExtendedUnc ? 1 : 2;
                    while (start < fullPath.size() && separators > 0)
                    {
                        if (fullPath[start++] == L'\\')
                        {
                            --separators;
                        }
                    }
                }
            }
            for (size_t i = start; i < fullPath.size(); ++i)
            {
                if (fullPath[i] == L'\\' && i > start && !EnsureDirectory(fullPath.substr(0, i)))
                {
                    return false;
                }
            }
            return true;
        }

        struct FileHandle
        {
            HANDLE Value = INVALID_HANDLE_VALUE;

            ~FileHandle()
            {
                if (Value != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(Value);
                }
            }
        };

        struct TemporaryFile
        {
            const wchar_t* Path = nullptr;
            bool bCreated = false;

            ~TemporaryFile()
            {
                if (bCreated)
                {
                    DeleteFileW(Path);
                }
            }
        };

        struct PngWriteContext
        {
            HANDLE File = INVALID_HANDLE_VALUE;
            bool bSuccess = true;
        };

        void WritePngBytes(void* pContext, void* pData, int size)
        {
            auto& context = *static_cast<PngWriteContext*>(pContext);
            if (!context.bSuccess || size <= 0)
            {
                return;
            }
            DWORD written = 0;
            context.bSuccess = WriteFile(context.File, pData, static_cast<DWORD>(size), &written, nullptr) != 0 &&
                written == static_cast<DWORD>(size);
        }

        uint32_t ReadBigEndian32(const uint8_t* pBytes)
        {
            return (static_cast<uint32_t>(pBytes[0]) << 24) | (static_cast<uint32_t>(pBytes[1]) << 16) |
                (static_cast<uint32_t>(pBytes[2]) << 8) | static_cast<uint32_t>(pBytes[3]);
        }

        bool IsCompletePng(Span<const uint8_t> bytes)
        {
            constexpr uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
            if (bytes.size() < 33 || std::memcmp(bytes.data(), signature, sizeof(signature)) != 0)
            {
                return false;
            }
            size_t offset = sizeof(signature);
            bool bFirst = true;
            bool bHasData = false;
            while (bytes.size() - offset >= 12)
            {
                const uint32_t length = ReadBigEndian32(bytes.data() + offset);
                if (static_cast<size_t>(length) > bytes.size() - offset - 12)
                {
                    return false;
                }
                const uint8_t* pType = bytes.data() + offset + 4;
                if (bFirst && (std::memcmp(pType, "IHDR", 4) != 0 || length != 13))
                {
                    return false;
                }
                bFirst = false;
                // stb は CRC を検証しないため、各チャンクの破損をここで検出する。
                uint32_t crc = 0xffffffffu;
                for (size_t i = 0; i < static_cast<size_t>(length) + 4; ++i)
                {
                    crc ^= pType[i];
                    for (unsigned bit = 0; bit < 8; ++bit)
                    {
                        crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
                    }
                }
                if ((crc ^ 0xffffffffu) != ReadBigEndian32(pType + 4 + length))
                {
                    return false;
                }
                if (std::memcmp(pType, "IDAT", 4) == 0)
                {
                    bHasData = true;
                }
                if (std::memcmp(pType, "IEND", 4) == 0)
                {
                    return length == 0 && bHasData;
                }
                offset += static_cast<size_t>(length) + 12;
            }
            return false;
        }

        struct DecodedPixels
        {
            stbi_uc* Data = nullptr;

            ~DecodedPixels()
            {
                stbi_image_free(Data);
            }
        };
    } // namespace

    const char* GetLevelPaintLayerFileStem(ELevelPaintLayer layer)
    {
        switch (layer)
        {
        case ELevelPaintLayer::GrassDensity: return "grass_density";
        case ELevelPaintLayer::Dryness: return "dry_grass";
        default: return "";
        }
    }

    bool LevelPaintMaskLayout::IsValid() const
    {
        return Columns >= 2 && Rows >= 2 && std::isfinite(FirstX) && std::isfinite(FirstZ) &&
            std::isfinite(Spacing) && Spacing > 0.0;
    }

    LevelPaintMaskLayout MakeLevelPaintMaskLayout(const Core::Rendering::Terrain::TerrainGridLayout& grid, bool bFlipZ)
    {
        return {grid.Columns, grid.Rows, grid.FirstX, grid.FirstZ, grid.Spacing, bFlipZ};
    }

    float EvaluateLevelPaintBrushWeight(const LevelPaintBrush& brush, float distance)
    {
        return static_cast<float>(BrushWeight(brush, distance));
    }

    bool LevelPaintMask::Initialize(const LevelPaintMaskLayout& layout, uint8_t defaultValue)
    {
        size_t count = 0;
        if (!GetSampleCount(layout, count))
        {
            return false;
        }
        try
        {
            VariableArray<uint8_t> values(count, defaultValue);
            VariableArray<uint8_t> baseline(count, defaultValue);
            m_Values = std::move(values);
            m_Baseline = std::move(baseline);
            m_Layout = layout;
            m_bDirty = false;
            return true;
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        catch (const std::length_error&)
        {
            return false;
        }
    }

    void LevelPaintMask::Reset()
    {
        m_Layout = {};
        m_Values.clear();
        m_Baseline.clear();
        m_bDirty = false;
    }

    bool LevelPaintMask::IsValid() const
    {
        size_t count = 0;
        return GetSampleCount(m_Layout, count) && m_Values.size() == count && m_Baseline.size() == count;
    }

    const LevelPaintMaskLayout& LevelPaintMask::GetLayout() const
    {
        return m_Layout;
    }

    Span<const uint8_t> LevelPaintMask::GetValues() const
    {
        return {m_Values.data(), m_Values.size()};
    }

    Span<const uint8_t> LevelPaintMask::GetBaseline() const
    {
        return {m_Baseline.data(), m_Baseline.size()};
    }

    bool LevelPaintMask::SetBaseline(Span<const uint8_t> baseline, bool bResetValues)
    {
        if (!IsValid() || baseline.size() != m_Baseline.size() || baseline.data() == nullptr)
        {
            return false;
        }
        std::memmove(m_Baseline.data(), baseline.data(), baseline.size());
        if (bResetValues)
        {
            std::memmove(m_Values.data(), m_Baseline.data(), m_Baseline.size());
            m_bDirty = false;
        }
        return true;
    }

    bool LevelPaintMask::SetValues(Span<const uint8_t> values)
    {
        if (!IsValid() || values.size() != m_Values.size() || values.data() == nullptr)
        {
            return false;
        }
        std::memmove(m_Values.data(), values.data(), values.size());
        m_bDirty = false;
        return true;
    }

    void LevelPaintMask::GetSampleWorldXZ(uint32_t column, uint32_t row, double& outX, double& outZ) const
    {
        outX = SampleWorldCoordinate(m_Layout.FirstX, m_Layout.Spacing, column);
        outZ = SampleWorldCoordinate(m_Layout.FirstZ, m_Layout.Spacing, row);
    }

    bool LevelPaintMask::WorldToSample(double x, double z, double& outColumn, double& outRow) const
    {
        if (!IsValid())
        {
            return false;
        }
        const double deltaX = x - m_Layout.FirstX;
        const double deltaZ = z - m_Layout.FirstZ;
        outColumn = std::isfinite(deltaX) ? deltaX / m_Layout.Spacing :
            x / m_Layout.Spacing - m_Layout.FirstX / m_Layout.Spacing;
        outRow = std::isfinite(deltaZ) ? deltaZ / m_Layout.Spacing :
            z / m_Layout.Spacing - m_Layout.FirstZ / m_Layout.Spacing;
        return true;
    }

    uint32_t LevelPaintMask::ApplyBrush(double centerX, double centerZ, const LevelPaintBrush& brush, bool bErase)
    {
        if (!IsValid() || !std::isfinite(centerX) || !std::isfinite(centerZ) || !IsBrushValid(brush) ||
            brush.Strength <= 0.0f)
        {
            return 0;
        }
        const double radius = brush.Radius;
        const double infinity = std::numeric_limits<double>::infinity();
        const double minX = std::nextafter(centerX - radius, -infinity);
        const double maxX = std::nextafter(centerX + radius, infinity);
        const double minZ = std::nextafter(centerZ - radius, -infinity);
        const double maxZ = std::nextafter(centerZ + radius, infinity);
        const uint32_t firstColumn = FindSampleBoundary(m_Layout.FirstX, m_Layout.Spacing, m_Layout.Columns, minX, false);
        const uint32_t endColumn = FindSampleBoundary(m_Layout.FirstX, m_Layout.Spacing, m_Layout.Columns, maxX, true);
        const uint32_t firstRow = FindSampleBoundary(m_Layout.FirstZ, m_Layout.Spacing, m_Layout.Rows, minZ, false);
        const uint32_t endRow = FindSampleBoundary(m_Layout.FirstZ, m_Layout.Spacing, m_Layout.Rows, maxZ, true);
        uint64_t changed = 0;
        for (uint32_t row = firstRow; row < endRow; ++row)
        {
            const double z = SampleWorldCoordinate(m_Layout.FirstZ, m_Layout.Spacing, row);
            for (uint32_t column = firstColumn; column < endColumn; ++column)
            {
                const double x = SampleWorldCoordinate(m_Layout.FirstX, m_Layout.Spacing, column);
                const double weight = BrushWeight(brush, std::hypot(x - centerX, z - centerZ));
                if (weight <= 0.0)
                {
                    continue;
                }
                const size_t index = static_cast<size_t>(row) * m_Layout.Columns + column;
                const uint8_t oldValue = m_Values[index];
                const uint8_t target = bErase ? m_Baseline[index] : brush.Value;
                const uint8_t newValue = static_cast<uint8_t>(std::lround(
                    static_cast<double>(oldValue) + (static_cast<double>(target) - oldValue) * weight));
                if (newValue != oldValue)
                {
                    m_Values[index] = newValue;
                    ++changed;
                }
            }
        }
        if (changed != 0)
        {
            m_bDirty = true;
        }
        return static_cast<uint32_t>(std::min(changed, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
    }

    uint32_t LevelPaintMask::Paint(double centerX, double centerZ, const LevelPaintBrush& brush)
    {
        return ApplyBrush(centerX, centerZ, brush, false);
    }

    uint32_t LevelPaintMask::Erase(double centerX, double centerZ, const LevelPaintBrush& brush)
    {
        return ApplyBrush(centerX, centerZ, brush, true);
    }

    bool LevelPaintMask::IsDirty() const
    {
        return m_bDirty;
    }

    void LevelPaintMask::ClearDirty()
    {
        m_bDirty = false;
    }

    bool SaveLevelPaintMaskPng(const LevelPaintMask& mask, const String& filePath, String* pOutError)
    {
        if (pOutError != nullptr)
        {
            pOutError->clear();
        }
        if (!mask.IsValid())
        {
            return Fail(pOutError, "保存するマスクの配置または値が無効です。");
        }
        const LevelPaintMaskLayout& layout = mask.GetLayout();
        // stb の符号付き整数による行バッファとフィルター計算を超えないようにする。
        const uint64_t filteredSize = (static_cast<uint64_t>(layout.Columns) + 1) * layout.Rows;
        if (layout.Columns > static_cast<uint32_t>(std::numeric_limits<int>::max() / 128) ||
            layout.Rows > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
            filteredSize > static_cast<uint64_t>(std::numeric_limits<int>::max() / 2))
        {
            return Fail(pOutError, "マスクの寸法が PNG の書き出し上限を超えています。");
        }
        try
        {
            WideString path;
            if (!DecodePath(filePath, path))
            {
                return Fail(pOutError, "保存先のパスが空か、不正な文字を含んでいます。");
            }
            if (!EnsureParentDirectories(path))
            {
                return Fail(pOutError, "保存先のディレクトリを作成できませんでした。");
            }
            const Span<const uint8_t> values = mask.GetValues();
            VariableArray<uint8_t> flipped;
            const uint8_t* pPixels = values.data();
            if (layout.bFlipZ)
            {
                flipped.resize(values.size());
                for (uint32_t row = 0; row < layout.Rows; ++row)
                {
                    const size_t source = static_cast<size_t>(layout.Rows - 1 - row) * layout.Columns;
                    const size_t destination = static_cast<size_t>(row) * layout.Columns;
                    std::memcpy(flipped.data() + destination, values.data() + source, layout.Columns);
                }
                pPixels = flipped.data();
            }
            const WideString temporaryPath = path + L".tmp";
            TemporaryFile temporary{temporaryPath.c_str(), false};
            FileHandle file;
            file.Value = CreateFileW(temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file.Value == INVALID_HANDLE_VALUE)
            {
                return Fail(pOutError, "保存用の一時ファイルを作成できませんでした。");
            }
            temporary.bCreated = true;
            PngWriteContext context{file.Value, true};
            const int encoded = stbi_write_png_to_func(WritePngBytes, &context, static_cast<int>(layout.Columns),
                                                       static_cast<int>(layout.Rows), 1, pPixels,
                                                       static_cast<int>(layout.Columns));
            const bool bFlushed = FlushFileBuffers(file.Value) != 0;
            const bool bClosed = CloseHandle(file.Value) != 0;
            file.Value = INVALID_HANDLE_VALUE;
            if (encoded == 0 || !context.bSuccess || !bFlushed || !bClosed)
            {
                return Fail(pOutError, "マスク PNG の一時ファイルへの書き込みに失敗しました。");
            }
            if (!MoveFileExW(temporaryPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                return Fail(pOutError, "マスク PNG の保存先ファイルの置換に失敗しました。");
            }
            temporary.bCreated = false;
            return true;
        }
        catch (const std::bad_alloc&)
        {
            return Fail(pOutError, "マスク PNG の保存に必要なメモリを確保できませんでした。");
        }
        catch (const std::length_error&)
        {
            return Fail(pOutError, "マスク PNG の保存に必要な配列が大きすぎます。");
        }
    }

    bool LoadLevelPaintMaskPng(const String& filePath, const LevelPaintMaskLayout& layout,
                               VariableArray<uint8_t>& outValues, String* pOutError)
    {
        if (pOutError != nullptr)
        {
            pOutError->clear();
        }
        size_t count = 0;
        if (!GetSampleCount(layout, count))
        {
            return Fail(pOutError, "読み込み先のマスク配置が無効です。");
        }
        try
        {
            WideString path;
            if (!DecodePath(filePath, path))
            {
                return Fail(pOutError, "読み込み元のパスが空か、不正な文字を含んでいます。");
            }
            FileHandle file;
            file.Value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file.Value == INVALID_HANDLE_VALUE)
            {
                return Fail(pOutError, "マスク PNG を開けませんでした。");
            }
            LARGE_INTEGER fileSize{};
            if (!GetFileSizeEx(file.Value, &fileSize) || fileSize.QuadPart <= 0 ||
                fileSize.QuadPart > std::numeric_limits<int>::max())
            {
                return Fail(pOutError, "マスク PNG のサイズが無効か、読み込み上限を超えています。");
            }
            VariableArray<uint8_t> bytes(static_cast<size_t>(fileSize.QuadPart));
            DWORD read = 0;
            if (!ReadFile(file.Value, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) ||
                read != bytes.size())
            {
                return Fail(pOutError, "マスク PNG の読み込みに失敗しました。");
            }
            if (!IsCompletePng(Span<const uint8_t>(bytes)) || bytes[24] != 8)
            {
                return Fail(pOutError, "マスク PNG が破損しているか、8bit PNG ではありません。");
            }
            int width = 0;
            int height = 0;
            int channels = 0;
            const int byteCount = static_cast<int>(bytes.size());
            if (!stbi_info_from_memory(bytes.data(), byteCount, &width, &height, &channels) ||
                width <= 0 || height <= 0 || channels < 1 || channels > 4 ||
                static_cast<uint32_t>(width) != layout.Columns || static_cast<uint32_t>(height) != layout.Rows)
            {
                return Fail(pOutError, "マスク PNG の寸法またはチャンネル数が配置と合いません。");
            }
            DecodedPixels pixels;
            pixels.Data = stbi_load_from_memory(bytes.data(), byteCount, &width, &height, &channels, 0);
            if (pixels.Data == nullptr || channels < 1 || channels > 4 ||
                static_cast<uint32_t>(width) != layout.Columns || static_cast<uint32_t>(height) != layout.Rows)
            {
                return Fail(pOutError, "マスク PNG の画像データを復号できませんでした。");
            }
            VariableArray<uint8_t> values(count);
            for (uint32_t row = 0; row < layout.Rows; ++row)
            {
                const uint32_t sourceRow = layout.bFlipZ ? layout.Rows - 1 - row : row;
                for (uint32_t column = 0; column < layout.Columns; ++column)
                {
                    const size_t destination = static_cast<size_t>(row) * layout.Columns + column;
                    const size_t source = (static_cast<size_t>(sourceRow) * layout.Columns + column) *
                        static_cast<size_t>(channels);
                    values[destination] = pixels.Data[source];
                }
            }
            outValues = std::move(values);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            return Fail(pOutError, "マスク PNG の読み込みに必要なメモリを確保できませんでした。");
        }
        catch (const std::length_error&)
        {
            return Fail(pOutError, "マスク PNG の読み込みに必要な配列が大きすぎます。");
        }
    }
} // namespace NorvesLib::Modules::Gameplay::Level
