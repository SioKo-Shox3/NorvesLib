// PNG・NVTEX と、インポスターを復元するための JSON を書く。
#include "ImpostorBakerOutput.h"

#include "Container/StringView.h"
#include "Text/JsonWriter.h"
#include "TextureCooker.h"
#include <climits>
#include <exception>
#include <fstream>
#include <limits>
#include "stb_image_write.h"

namespace NorvesLib::Tools::ImpostorBaker
{
    namespace C = Core::Container;
    namespace
    {
        constexpr const char* AtlasNames[] = {"albedo", "normal", "orm", "depth"};
        constexpr const char* CookedNames[] = {"albedoNvtex", "normalNvtex", "ormNvtex", "depthNvtex"};

        std::filesystem::path AtlasFilename(const std::filesystem::path& name, uint32_t index, bool bCooked)
        {
            auto filename = name;
            filename += "_";
            filename += AtlasNames[index];
            filename += bCooked ? ".nvtex" : ".png";
            return filename;
        }

        struct PngWriteContext
        {
            C::VariableArray<uint8_t> Bytes;
            bool bFailed = false;
        };

        void AppendPngBytes(void* context, void* data, int size)
        {
            auto& output = *static_cast<PngWriteContext*>(context);
            if (output.bFailed || size <= 0)
            {
                return;
            }
            // C のコールバック境界へ例外を渡さない。
            try
            {
                const auto* begin = static_cast<const uint8_t*>(data);
                output.Bytes.insert(output.Bytes.end(), begin, begin + size);
            }
            catch (const std::exception&)
            {
                output.bFailed = true;
            }
        }

        void WriteVector(Core::JsonWriter& writer, const char* key, const Math::Vector3& value)
        {
            writer.BeginArray(key);
            writer.WriteNumberElement(value.x);
            writer.WriteNumberElement(value.y);
            writer.WriteNumberElement(value.z);
            writer.EndArray();
        }
    } // namespace

    C::AnsiString ImpostorBakerPathUtf8(const std::filesystem::path& path)
    {
        const auto text = path.u8string();
        return C::AnsiString(C::AnsiStringView(reinterpret_cast<const char*>(text.data()), text.size()));
    }

    bool EncodeImpostorBakerPng(const ImpostorBakerPixels& pixels, C::VariableArray<uint8_t>& png,
                               C::AnsiString& error)
    {
        const uint64_t rowBytes = static_cast<uint64_t>(pixels.Width) * pixels.Channels;
        if (!pixels.Width || !pixels.Height ||
            (pixels.Channels != 1 && pixels.Channels != 3 && pixels.Channels != 4) ||
            pixels.Width > INT_MAX || pixels.Height > INT_MAX || rowBytes > INT_MAX ||
            rowBytes * pixels.Height != pixels.Bytes.size())
        {
            error = "PNG にする画素の寸法・チャンネル数・バイト数が不正です";
            return false;
        }
        PngWriteContext output;
        const int result = stbi_write_png_to_func(AppendPngBytes, &output,
            static_cast<int>(pixels.Width), static_cast<int>(pixels.Height), static_cast<int>(pixels.Channels),
            pixels.Bytes.data(), static_cast<int>(rowBytes));
        if (!result || output.bFailed || output.Bytes.empty())
        {
            error = "PNG の符号化または書き出し用メモリの確保に失敗しました";
            return false;
        }
        png = std::move(output.Bytes);
        return true;
    }

    bool WriteImpostorBakerFile(const std::filesystem::path& path, C::Span<const uint8_t> bytes,
                               C::AnsiString& error)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file || bytes.size() > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
        {
            error = C::AnsiString("出力ファイルを開けないか、データが大きすぎます: ") + ImpostorBakerPathUtf8(path);
            return false;
        }
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file.close();
        if (!file)
        {
            error = C::AnsiString("出力ファイルを最後まで書けません: ") + ImpostorBakerPathUtf8(path);
            return false;
        }
        return true;
    }

    bool CookImpostorBakerTexture(C::Span<const uint8_t> png, uint32_t atlasIndex,
                                 const std::filesystem::path& path, C::AnsiString& error)
    {
        if (atlasIndex >= 4)
        {
            error = "NVTEX にするアトラスの番号が不正です";
            return false;
        }
        AssetCook::TextureSourceImage source;
        source.Bytes = png.data();
        source.Size = png.size();
        source.Name = ImpostorBakerPathUtf8(path);
        AssetCook::TextureUsageCookParams params;
        // オブジェクト空間法線の RGB は、ORM と同じ BC7 linear で保持する。
        params.Usage = atlasIndex == 0 ? AssetCook::TextureUsage::Albedo :
                       atlasIndex == 3 ? AssetCook::TextureUsage::Single : AssetCook::TextureUsage::Orm;
        AssetCook::TextureCookResult result;
        C::AnsiString reason;
        if (!AssetCook::CookTextureForUsage(source, {}, params, result, reason))
        {
            error = C::AnsiString("NVTEX を焼けません: ") + ImpostorBakerPathUtf8(path) + ": " + reason;
            return false;
        }
        return WriteImpostorBakerFile(path, {result.NvtexBytes.data(), result.NvtexBytes.size()}, error);
    }

    bool WriteImpostorBakerMetadata(const std::filesystem::path& path, const std::filesystem::path& source,
                                   const std::filesystem::path& name,
                                   const Core::Rendering::ImpostorBakeMetadata& metadata, bool bCooked,
                                   C::AnsiString& error)
    {
        Core::JsonWriter writer(true);
        writer.BeginObject();
        writer.WriteUInt64("version", 1);
        writer.WriteString("layout", metadata.Layout == Core::Rendering::ImpostorOctahedralLayout::HemiOctahedral ?
                           "hemi-octahedral" : "full-octahedral");
        writer.WriteUInt64("cellsX", metadata.AxisCellCountX);
        writer.WriteUInt64("cellsY", metadata.AxisCellCountY);
        writer.WriteUInt64("cellResolution", metadata.CellResolution);
        writer.WriteUInt64("atlasWidth", metadata.AtlasWidth);
        writer.WriteUInt64("atlasHeight", metadata.AtlasHeight);
        WriteVector(writer, "boundsCenter", metadata.BoundsCenter);
        writer.WriteNumber("boundsRadius", metadata.BoundsRadius);
        WriteVector(writer, "boundsMin", metadata.BoundsMin);
        WriteVector(writer, "boundsMax", metadata.BoundsMax);
        writer.WriteNumber("boardSize", 2.0 * metadata.BoundsRadius);
        WriteVector(writer, "pivot", metadata.BoundsCenter);
        writer.BeginObject("conventions");
        writer.WriteString("pole", "+Y");
        writer.WriteString("cellOrder", "cy * cellsX + cx");
        writer.WriteBool("rowZeroIsTop", true);
        writer.WriteBool("depthNearIsZero", true);
        writer.WriteString("normalSpace", "object");
        writer.WriteString("orm", "R=AO,G=roughness,B=metallic");
        writer.WriteString("coverage", "albedo.a");
        writer.EndObject();
        writer.BeginObject("files");
        for (uint32_t i = 0; i < 4; ++i)
        {
            writer.WriteString(AtlasNames[i], ImpostorBakerPathUtf8(AtlasFilename(name, i, false)).c_str());
            if (bCooked)
            {
                writer.WriteString(CookedNames[i], ImpostorBakerPathUtf8(AtlasFilename(name, i, true)).c_str());
            }
        }
        writer.EndObject();
        writer.WriteString("source", ImpostorBakerPathUtf8(source.filename()).c_str());
        writer.WriteUInt64("vertexCount", metadata.VertexCount);
        writer.WriteUInt64("triangleCount", metadata.IndexCount / 3);
        writer.EndObject();
        if (!writer.IsComplete())
        {
            error = "インポスターのメタデータを JSON にできません";
            return false;
        }
        const auto json = writer.ToString();
        return WriteImpostorBakerFile(path,
            {reinterpret_cast<const uint8_t*>(json.c_str()), json.size()}, error);
    }

    bool WriteImpostorBakerOutput(const std::filesystem::path& directory, const std::filesystem::path& name,
                                 const std::filesystem::path& source,
                                 const Core::Rendering::ImpostorBakeMetadata& metadata,
                                 const ImpostorBakerPixels (&pixels)[4], bool bNoCook,
                                 C::VariableArray<std::filesystem::path>& files, C::AnsiString& error)
    {
        std::error_code directoryError;
        std::filesystem::create_directories(directory, directoryError);
        if (directoryError)
        {
            error = C::AnsiString("出力ディレクトリを作れません: ") + ImpostorBakerPathUtf8(directory);
            return false;
        }
        files.clear();
        for (uint32_t i = 0; i < 4; ++i)
        {
            C::VariableArray<uint8_t> png;
            const auto pngPath = directory / AtlasFilename(name, i, false);
            if (!EncodeImpostorBakerPng(pixels[i], png, error) ||
                !WriteImpostorBakerFile(pngPath, {png.data(), png.size()}, error))
            {
                return false;
            }
            files.push_back(pngPath);
            if (!bNoCook)
            {
                const auto nvtexPath = directory / AtlasFilename(name, i, true);
                if (!CookImpostorBakerTexture({png.data(), png.size()}, i, nvtexPath, error))
                {
                    return false;
                }
                files.push_back(nvtexPath);
            }
        }
        auto jsonFilename = name;
        jsonFilename += ".impostor.json";
        const auto jsonPath = directory / jsonFilename;
        if (!WriteImpostorBakerMetadata(jsonPath, source, name, metadata, !bNoCook, error))
        {
            return false;
        }
        files.push_back(jsonPath);
        return true;
    }
} // namespace NorvesLib::Tools::ImpostorBaker
