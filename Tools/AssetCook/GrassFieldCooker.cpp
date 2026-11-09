#include "GrassFieldCooker.h"

#include "AssetCookOutput.h"
#include "NativeCookPath.h"
#include "TerrainSpec.h"
#include "Asset/CookedGrassFieldFormat.h"
#include "Asset/CookedSkeletalNameCodec.h"
#include "Text/JsonDocument.h"
#include "Vegetation/GrassSpecies.h"

#include <climits>
#include <cmath>
#include <exception>
#include <limits>
#include "stb_image.h"

namespace NorvesLib::Tools::AssetCook
{
    namespace
    {
        namespace Asset = Core::Asset;
        namespace Container = Core::Container;
        using Container::AnsiString;
        using Container::Span;
        using Container::VariableArray;
        using Core::JsonValue;

        struct Image
        {
            uint32_t Width = 0;
            VariableArray<uint8_t> Pixels;
        };

        struct Chunk
        {
            int32_t X = 0;
            int32_t Z = 0;
            VariableArray<uint8_t> A;
            VariableArray<uint8_t> B;
        };

        struct Input
        {
            float Size = 0;
            uint32_t AResolution = 0;
            uint32_t BResolution = 0;
            int32_t FirstX = 0;
            int32_t FirstZ = 0;
            uint32_t CountX = 0;
            uint32_t CountZ = 0;
            bool bPng = false;
            Image Images[5];
            VariableArray<AnsiString> Names;
            VariableArray<Chunk> Chunks;
            // 省略されたチャンクは -1。照合で密度ゼロの領域も検査する。
            VariableArray<int64_t> ChunkIndices;
            uint64_t SourceHash = Asset::CookedTextureFormatV0::Fnv1a64OffsetBasis;
        };

        bool Fail(AnsiString& error, const char* text)
        {
            error = text;
            return false;
        }

        bool Utf8(const Container::String& text, AnsiString& out, AnsiString& error)
        {
            const Span<const Container::String::value_type> units(text.data(), text.size());
            const auto measured = Asset::MeasureSkeletalNameEncoding(2, units);
            if (!measured.Succeeded())
            {
                return Fail(error, "文字列は NUL を含まない有効な Unicode で指定してください");
            }
            VariableArray<uint8_t> bytes(measured.ByteCount);
            if (!Asset::EncodeSkeletalWireName(2, units, {bytes.data(), bytes.size()}).Succeeded())
            {
                return Fail(error, "文字列を UTF-8 に変換できません");
            }
            out.clear();
            out.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return true;
        }

        bool Text(JsonValue value, AnsiString& out, AnsiString& error)
        {
            if (!value.IsString() || value.AsString().empty())
            {
                return Fail(error, "パス・種別名・source.type は空でない文字列で指定してください");
            }
            return Utf8(value.AsString(), out, error);
        }

        bool Path(JsonValue value, const std::filesystem::path& parent, std::filesystem::path& out, AnsiString& error)
        {
            AnsiString text;
            if (!Text(value, text, error))
            {
                return false;
            }
            const auto* first = reinterpret_cast<const char8_t*>(text.data());
            out = (parent / std::filesystem::path(first, first + text.size())).lexically_normal();
            return true;
        }

        bool Read(const std::filesystem::path& path, VariableArray<uint8_t>& bytes, AnsiString& error)
        {
            std::string detail;
            if (!Detail::ReadSkeletalBinaryFile(path, bytes, detail))
            {
                error = "入力ファイルを読み込めません: ";
                AnsiString name;
                if (Detail::EncodeCookPathUtf8(path, name))
                {
                    error += name;
                }
                return false;
            }
            return true;
        }

        void HashFile(uint64_t& hash, Span<const uint8_t> bytes)
        {
            // ファイル境界も含め、参照順に長さと全バイトを混ぜる。
            for (unsigned i = 0; i < 8; ++i)
            {
                hash ^= static_cast<uint8_t>(static_cast<uint64_t>(bytes.size()) >> (i * 8));
                hash *= Asset::CookedTextureFormatV0::Fnv1a64Prime;
            }
            for (uint8_t byte : bytes)
            {
                hash ^= byte;
                hash *= Asset::CookedTextureFormatV0::Fnv1a64Prime;
            }
        }

        bool Number(JsonValue value, double min, double max, double& out, AnsiString& error, bool bInteger = false)
        {
            out = value.AsNumber();
            if (!value.IsNumber() || !std::isfinite(out) || out < min || out > max ||
                (bInteger && std::floor(out) != out))
            {
                return Fail(error, "配置の数値が未指定・範囲外です。寸法は正、解像度は 1..1024、画素値は整数 0..255 で指定してください");
            }
            return true;
        }

        bool Pair(JsonValue value, double* out, bool bCounts, AnsiString& error)
        {
            if (!value.IsArray() || value.GetArraySize() != 2)
            {
                return Fail(error, "origin_m・chunks は [x, z] の2要素で指定してください");
            }
            for (size_t i = 0; i < 2; ++i)
            {
                if (!Number(value.GetArrayElement(i), bCounts ? 1.0 : -std::numeric_limits<double>::max(),
                            bCounts ? UINT32_MAX : std::numeric_limits<double>::max(), out[i], error, bCounts))
                {
                    return false;
                }
            }
            return true;
        }

        bool SetRange(Input& input, double x, double z, double nx, double nz, AnsiString& error)
        {
            const double rx = x / input.Size;
            const double rz = z / input.Size;
            const auto aligned = [](double value)
            {
                return std::isfinite(value) && std::abs(value - std::round(value)) <= 1.0e-6;
            };
            if (!aligned(rx) || !aligned(rz) || !aligned(nx) || !aligned(nz) || nx < 1 || nz < 1 ||
                nx > UINT32_MAX || nz > UINT32_MAX || rx < INT32_MIN || rz < INT32_MIN ||
                std::round(rx) + std::round(nx) - 1 > INT32_MAX ||
                std::round(rz) + std::round(nz) - 1 > INT32_MAX || nx * nz > UINT32_MAX)
            {
                return Fail(error, "場の原点と大きさはチャンク寸法の整数倍で、チャンク座標は int32 の範囲に収めてください");
            }
            input.FirstX = static_cast<int32_t>(std::round(rx));
            input.FirstZ = static_cast<int32_t>(std::round(rz));
            input.CountX = static_cast<uint32_t>(std::round(nx));
            input.CountZ = static_cast<uint32_t>(std::round(nz));
            input.ChunkIndices.resize(static_cast<size_t>(input.CountX) * input.CountZ);
            for (auto& index : input.ChunkIndices)
            {
                index = -1;
            }
            return true;
        }

        bool LoadPng(const std::filesystem::path& path, uint64_t width, uint64_t height, int channels,
                     Input& input, Image& image, AnsiString& error)
        {
            VariableArray<uint8_t> bytes;
            if (!Read(path, bytes, error))
            {
                return false;
            }
            HashFile(input.SourceHash, {bytes.data(), bytes.size()});
            constexpr uint8_t Signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
            int w = 0, h = 0, c = 0;
            if (bytes.size() < 26 || bytes.size() > INT_MAX || std::memcmp(bytes.data(), Signature, 8) != 0 ||
                bytes[24] != 8 || bytes[25] != (channels == 1 ? 0 : 2) || width > INT_MAX || height > INT_MAX ||
                !stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &c) ||
                w != static_cast<int>(width) || h != static_cast<int>(height) || c != channels)
            {
                return Fail(error, "PNG の形式または寸法が違います。A は8bitグレー、tint は8bit RGB で chunks×解像度に合わせてください");
            }
            stbi_uc* decoded = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &c, channels);
            Container::TUniquePtr<stbi_uc, decltype(&stbi_image_free)> owner(decoded, stbi_image_free);
            if (decoded == nullptr)
            {
                return Fail(error, "PNG の画素を読み込めません");
            }
            image.Width = static_cast<uint32_t>(w);
            image.Pixels.resize(static_cast<size_t>(w) * h * channels);
            std::memcpy(image.Pixels.data(), decoded, image.Pixels.size());
            return true;
        }

        void StoreChunk(Input& input, uint32_t x, uint32_t z, Chunk&& chunk)
        {
            bool bNonzero = false;
            for (size_t i = 0; i < chunk.A.size(); i += 4)
            {
                bNonzero |= chunk.A[i] != 0;
            }
            if (bNonzero)
            {
                chunk.X = static_cast<int32_t>(static_cast<int64_t>(input.FirstX) + x);
                chunk.Z = static_cast<int32_t>(static_cast<int64_t>(input.FirstZ) + z);
                input.ChunkIndices[static_cast<size_t>(z) * input.CountX + x] = static_cast<int64_t>(input.Chunks.size());
                input.Chunks.push_back(std::move(chunk));
            }
        }

        bool BuildPng(JsonValue source, const std::filesystem::path& parent, Input& input, AnsiString& error)
        {
            double origin[2], counts[2];
            if (!Pair(source.FindMember("origin_m"), origin, false, error) ||
                !Pair(source.FindMember("chunks"), counts, true, error) ||
                !SetRange(input, origin[0], origin[1], counts[0], counts[1], error))
            {
                return false;
            }
            input.bPng = true;
            constexpr const char* Keys[] = {"density", "height", "moisture", "species", "tint"};
            for (size_t i = 0; i < 5; ++i)
            {
                std::filesystem::path path;
                const uint32_t resolution = i == 4 ? input.BResolution : input.AResolution;
                if (!Path(source.FindMember(Keys[i]), parent, path, error) ||
                    !LoadPng(path, static_cast<uint64_t>(input.CountX) * resolution,
                             static_cast<uint64_t>(input.CountZ) * resolution, i == 4 ? 3 : 1, input, input.Images[i], error))
                {
                    return false;
                }
            }
            for (uint8_t id : input.Images[3].Pixels)
            {
                if (id >= input.Names.size())
                {
                    return Fail(error, "species PNG に種別表の範囲外の ID があります");
                }
            }
            for (uint32_t z = 0; z < input.CountZ; ++z)
            {
                for (uint32_t x = 0; x < input.CountX; ++x)
                {
                    Chunk chunk;
                    const size_t a = input.AResolution, b = input.BResolution;
                    chunk.A.resize(a * a * 4);
                    chunk.B.resize(b * b * 4);
                    for (size_t row = 0; row < a; ++row)
                    {
                        for (size_t col = 0; col < a; ++col)
                        {
                            const size_t src = (z * a + row) * input.Images[0].Width + x * a + col;
                            for (size_t channel = 0; channel < 4; ++channel)
                            {
                                chunk.A[(row * a + col) * 4 + channel] = input.Images[channel].Pixels[src];
                            }
                        }
                    }
                    for (size_t row = 0; row < b; ++row)
                    {
                        for (size_t col = 0; col < b; ++col)
                        {
                            const size_t src = ((z * b + row) * input.Images[4].Width + x * b + col) * 3;
                            for (size_t channel = 0; channel < 3; ++channel)
                            {
                                chunk.B[(row * b + col) * 4 + channel] = input.Images[4].Pixels[src + channel];
                            }
                            chunk.B[(row * b + col) * 4 + 3] = 0;
                        }
                    }
                    StoreChunk(input, x, z, std::move(chunk));
                }
            }
            return true;
        }

        const char* TerrainStatusName(Asset::CookedTerrainParseStatus status)
        {
            constexpr const char* Names[] = {
                "Success", "InvalidBlob", "HeaderTooSmall", "BadMagic",
                "UnsupportedVersion", "EndianMismatch", "HeaderSizeMismatch", "FileSizeMismatch",
                "ReservedFieldNonZero", "PayloadHashMismatch", "InvalidDimensions", "InvalidLodCount",
                "InvalidLayerCount", "RecordSizeMismatch", "TableLayoutMismatch", "IntegerOverflow",
                "InvalidLayerRecord", "DuplicateLayerFourCC", "TileIndexOutOfRange", "DuplicateTile",
                "InvalidTileBounds", "LayerRangeOutOfRange", "HeightSizeMismatch", "LayerHashMismatch",
                "EmbeddedTextureInvalid"
            };
            static_assert(static_cast<size_t>(Asset::CookedTerrainParseStatus::EmbeddedTextureInvalid) + 1 == sizeof(Names) / sizeof(Names[0]));
            const auto index = static_cast<size_t>(status);
            return index < sizeof(Names) / sizeof(Names[0]) ? Names[index] : "Unknown";
        }

        const char* TextureStatusName(Asset::CookedTextureParseStatus status)
        {
            constexpr const char* Names[] = {
                "Success", "InvalidBlob", "EmptyBlob", "HeaderTooSmall",
                "BadMagic", "UnsupportedVersion", "EndianMismatch", "HeaderSizeMismatch",
                "MipRecordSizeMismatch", "FileSizeMismatch", "ReservedFieldNonZero", "InvalidDimensions",
                "InvalidMipCount", "InvalidPayloadSize", "UnknownPixelFormat", "UnknownColorSpace",
                "InvalidColorSpaceForFormat", "IntegerOverflow", "MipTableSizeMismatch", "MipTableOutOfRange",
                "MipOffsetBeforePayload", "MipPackingMismatch", "MipDimensionsMismatch", "MipDataSizeMismatch",
                "PayloadHashMismatch", "TruncatedPayload", "InvalidTileShape", "InvalidFirstTailMip",
                "TileTableSizeMismatch", "TileTableOutOfRange", "TileRecordMismatch", "TailRangeMismatch",
                "MetadataTooSmall", "ReadFailed"
            };
            static_assert(static_cast<size_t>(Asset::CookedTextureParseStatus::ReadFailed) + 1 == sizeof(Names) / sizeof(Names[0]));
            const auto index = static_cast<size_t>(status);
            return index < sizeof(Names) / sizeof(Names[0]) ? Names[index] : "Unknown";
        }

        bool LoadTerrainLayers(const Asset::CookedTerrainData& terrain, Asset::AssetPackageFourCC fourCC,
                               Asset::CookedTexturePixelFormat format, VariableArray<Asset::CookedTextureData>& textures,
                               AnsiString& error)
        {
            const int32_t layer = terrain.FindLayer(fourCC);
            if (layer < 0)
            {
                return Fail(error, "地形に GRS0 または MSK0 の層がありません");
            }
            for (uint32_t i = 0; i < terrain.Tiles.size(); ++i)
            {
                auto parsed = Asset::ParseCookedTexture(terrain.GetLayerBlob(i, static_cast<uint32_t>(layer)));
                if (!parsed.Succeeded())
                {
                    error = "地形の層を解析できません: ";
                    error += TextureStatusName(parsed.Status);
                    return false;
                }
                if (parsed.Texture.PixelFormat != format || parsed.Texture.ColorSpace != Asset::CookedTextureColorSpace::Linear ||
                    parsed.Texture.Width != terrain.GetSamplesPerSide() || parsed.Texture.Height != terrain.GetSamplesPerSide() ||
                    parsed.Texture.LayerCount != 1)
                {
                    return Fail(error, "地形の GRS0 は r8 linear、MSK0 は rgba8 linear のセル角格子である必要があります");
                }
                textures.push_back(std::move(parsed.Texture));
            }
            return true;
        }

        uint8_t SampleLayer(const Asset::CookedTerrainData& terrain, const VariableArray<Asset::CookedTextureData>& textures,
                            double gx, double gz, size_t stride)
        {
            // NVTRN の標本はセル角。TileCells+1 の末尾の辺は隣の先頭と共有する。
            // 境界は +X/+Z 側のタイルを選び、外周だけ最後のタイルへ寄せる。
            const uint32_t tx = std::min(static_cast<uint32_t>(gx / terrain.TileCells), terrain.TileCountX - 1);
            const uint32_t tz = std::min(static_cast<uint32_t>(gz / terrain.TileCells), terrain.TileCountZ - 1);
            uint32_t tileIndex = 0;
            (void)terrain.FindTile(tx, tz, &tileIndex);
            const double x = std::clamp(gx - tx * terrain.TileCells, 0.0, static_cast<double>(terrain.TileCells));
            const double z = std::clamp(gz - tz * terrain.TileCells, 0.0, static_cast<double>(terrain.TileCells));
            const uint32_t x0 = static_cast<uint32_t>(std::floor(x)), z0 = static_cast<uint32_t>(std::floor(z));
            const uint32_t x1 = std::min(x0 + 1, terrain.TileCells), z1 = std::min(z0 + 1, terrain.TileCells);
            const auto pixels = textures[tileIndex].GetMipBytes(0);
            const size_t side = terrain.GetSamplesPerSide();
            const auto at = [&](uint32_t sx, uint32_t sz) { return pixels[(sz * side + sx) * stride]; };
            const double top = std::lerp(static_cast<double>(at(x0, z0)), static_cast<double>(at(x1, z0)), x - x0);
            const double bottom = std::lerp(static_cast<double>(at(x0, z1)), static_cast<double>(at(x1, z1)), x - x0);
            return static_cast<uint8_t>(std::clamp(std::lround(std::lerp(top, bottom, z - z0)), 0l, 255l));
        }

        bool BuildTerrain(JsonValue source, const std::filesystem::path& parent, Input& input, AnsiString& error)
        {
            std::filesystem::path path;
            AnsiString species;
            double height = 0, tint[3] = {};
            const auto tintValue = source.FindMember("tint");
            if (!Path(source.FindMember("terrain"), parent, path, error) ||
                !Text(source.FindMember("species"), species, error) ||
                !Number(source.FindMember("height_factor"), 0, 255, height, error, true))
            {
                return false;
            }
            if (!tintValue.IsArray() || tintValue.GetArraySize() != 3)
            {
                return Fail(error, "地形入力の tint は [r, g, b] の3要素で指定してください");
            }
            for (size_t i = 0; i < 3; ++i)
            {
                if (!Number(tintValue.GetArrayElement(i), 0, 255, tint[i], error, true))
                {
                    return false;
                }
            }
            size_t speciesId = 0;
            while (speciesId < input.Names.size() && input.Names[speciesId] != species)
            {
                ++speciesId;
            }
            if (speciesId == input.Names.size())
            {
                return Fail(error, "地形入力の species が種別表にありません");
            }
            VariableArray<uint8_t> bytes;
            if (path.extension() == ".nvtrn")
            {
                if (!Read(path, bytes, error))
                {
                    return false;
                }
                HashFile(input.SourceHash, {bytes.data(), bytes.size()});
            }
            else
            {
                TerrainSpecCookResult result;
                if (!CookTerrainSpecFile(path, result, error))
                {
                    return false;
                }
                for (const auto& file : result.InputFiles)
                {
                    VariableArray<uint8_t> dependency;
                    if (!Read(file, dependency, error))
                    {
                        return false;
                    }
                    HashFile(input.SourceHash, {dependency.data(), dependency.size()});
                }
                bytes = std::move(result.Build.NvtrnBytes);
            }
            auto parsed = Asset::ParseCookedTerrain(Asset::AssetBlob::CopyBytes({bytes.data(), bytes.size()}));
            if (!parsed.Succeeded())
            {
                error = "地形を解析できません: ";
                error += TerrainStatusName(parsed.Status);
                return false;
            }
            const auto& terrain = parsed.Terrain;
            if (!SetRange(input, terrain.OriginX, terrain.OriginZ,
                          static_cast<double>(terrain.CellCountX) * terrain.CellSizeM / input.Size,
                          static_cast<double>(terrain.CellCountZ) * terrain.CellSizeM / input.Size, error))
            {
                return false;
            }
            VariableArray<Asset::CookedTextureData> density, moisture;
            if (!LoadTerrainLayers(terrain, TerrainGrassLayerFourCC, Asset::CookedTexturePixelFormat::R8UNorm, density, error) ||
                !LoadTerrainLayers(terrain, TerrainMaskLayerFourCC, Asset::CookedTexturePixelFormat::RGBA8UNorm, moisture, error))
            {
                return false;
            }
            for (uint32_t z = 0; z < input.CountZ; ++z)
            {
                for (uint32_t x = 0; x < input.CountX; ++x)
                {
                    Chunk chunk;
                    const size_t a = input.AResolution, b = input.BResolution;
                    chunk.A.resize(a * a * 4);
                    chunk.B.resize(b * b * 4);
                    for (size_t row = 0; row < a; ++row)
                    {
                        for (size_t col = 0; col < a; ++col)
                        {
                            const double gx = (x + (col + 0.5) / a) * input.Size / terrain.CellSizeM;
                            const double gz = (z + (row + 0.5) / a) * input.Size / terrain.CellSizeM;
                            const size_t dst = (row * a + col) * 4;
                            chunk.A[dst] = SampleLayer(terrain, density, gx, gz, 1);
                            chunk.A[dst + 1] = static_cast<uint8_t>(height);
                            chunk.A[dst + 2] = SampleLayer(terrain, moisture, gx, gz, 4);
                            chunk.A[dst + 3] = static_cast<uint8_t>(speciesId);
                        }
                    }
                    for (size_t i = 0; i < b * b; ++i)
                    {
                        for (size_t channel = 0; channel < 3; ++channel)
                        {
                            chunk.B[i * 4 + channel] = static_cast<uint8_t>(tint[channel]);
                        }
                        chunk.B[i * 4 + 3] = 0;
                    }
                    StoreChunk(input, x, z, std::move(chunk));
                }
            }
            return true;
        }

        bool LoadInput(const std::filesystem::path& path, Input& input, AnsiString& error)
        {
            VariableArray<uint8_t> bytes;
            if (!Read(path, bytes, error))
            {
                return false;
            }
            HashFile(input.SourceHash, {bytes.data(), bytes.size()});
            Span<const uint8_t> text(bytes.data(), bytes.size());
            if (text.size() >= 3 && text[0] == 0xef && text[1] == 0xbb && text[2] == 0xbf)
            {
                text = {text.data() + 3, text.size() - 3};
            }
            Core::JsonDocument document;
            if (!Core::JsonDocument::TryParseUtf8(text, document) || !document.GetRoot().IsObject())
            {
                return Fail(error, "草の配置を JSON オブジェクトとして解析できません");
            }
            const auto root = document.GetRoot();
            double version = 0, size = 0, a = 0, b = 0;
            if (!Number(root.FindMember("version"), 0, 0, version, error, true) ||
                !Number(root.FindMember("chunk_size_m"), std::numeric_limits<float>::min(), std::numeric_limits<float>::max(), size, error) ||
                !Number(root.FindMember("layer_a_resolution"), 1, 1024, a, error, true) ||
                !Number(root.FindMember("layer_b_resolution"), 1, 1024, b, error, true))
            {
                return false;
            }
            input.Size = static_cast<float>(size);
            input.AResolution = static_cast<uint32_t>(a);
            input.BResolution = static_cast<uint32_t>(b);
            const auto parent = path.parent_path();
            std::filesystem::path speciesPath;
            if (!Path(root.FindMember("species_table"), parent, speciesPath, error) || !Read(speciesPath, bytes, error))
            {
                return false;
            }
            HashFile(input.SourceHash, {bytes.data(), bytes.size()});
            Core::Vegetation::GrassSpeciesTable table;
            Container::String speciesError;
            AnsiString speciesPathText;
            if (!Detail::EncodeCookPathUtf8(speciesPath, speciesPathText) ||
                !Core::Vegetation::TryLoadGrassSpeciesTable(speciesPathText.c_str(), table, &speciesError))
            {
                AnsiString detail;
                (void)Utf8(speciesError, detail, error);
                error = "草の種別表を読み込めません: ";
                error += detail;
                return false;
            }
            for (const auto& species : table.GetAll())
            {
                AnsiString name;
                if (!Utf8(species.Name, name, error) || name.empty() || name.size() > Asset::CookedGrassFieldFormatV0::MaxSpeciesNameLength)
                {
                    return Fail(error, "種別名は UTF-8 で1..63バイトに収めてください");
                }
                input.Names.push_back(std::move(name));
            }
            const auto source = root.FindMember("source");
            AnsiString type;
            if (!Text(source.FindMember("type"), type, error))
            {
                return false;
            }
            if (type == "png")
            {
                return BuildPng(source, parent, input, error);
            }
            if (type == "terrain")
            {
                return BuildTerrain(source, parent, input, error);
            }
            return Fail(error, "source.type は png または terrain で指定してください");
        }

        uint64_t CompareLayer(Span<const uint8_t> actual, Span<const uint8_t> expected)
        {
            uint64_t mismatches = 0;
            for (size_t i = 0; i < expected.size(); i += 4)
            {
                mismatches += std::memcmp(actual.data() + i, expected.data() + i, 4) != 0;
            }
            return mismatches;
        }

        uint64_t ComparePngChunk(const Input& input, uint32_t cx, uint32_t cz,
                                 Span<const uint8_t> a, Span<const uint8_t> b)
        {
            // 焼き側のチャンク配列や転送処理を使わず、元画像の画素と直接比較する。
            uint64_t mismatches = 0;
            for (uint32_t row = 0; row < input.AResolution; ++row)
            {
                for (uint32_t col = 0; col < input.AResolution; ++col)
                {
                    bool bDifferent = false;
                    for (size_t channel = 0; channel < 4; ++channel)
                    {
                        const auto& image = input.Images[channel];
                        const size_t source = static_cast<size_t>(cz * input.AResolution + row) * image.Width +
                                              cx * input.AResolution + col;
                        bDifferent |= a[(static_cast<size_t>(row) * input.AResolution + col) * 4 + channel] != image.Pixels[source];
                    }
                    mismatches += bDifferent;
                }
            }
            for (uint32_t row = 0; row < input.BResolution; ++row)
            {
                for (uint32_t col = 0; col < input.BResolution; ++col)
                {
                    const auto& image = input.Images[4];
                    const size_t source = (static_cast<size_t>(cz * input.BResolution + row) * image.Width +
                                           cx * input.BResolution + col) * 3;
                    const size_t dest = (static_cast<size_t>(row) * input.BResolution + col) * 4;
                    mismatches += std::memcmp(b.data() + dest, image.Pixels.data() + source, 3) != 0 || b[dest + 3] != 0;
                }
            }
            return mismatches;
        }

        bool Check(const std::filesystem::path& packagePath, const AnsiString& entryName,
                   const std::filesystem::path& sourcePath, AnsiString& error)
        {
            VariableArray<uint8_t> bytes;
            if (!Read(packagePath, bytes, error))
            {
                return false;
            }
            FileStream::Package package;
            if (!package.LoadFromMemory({bytes.data(), bytes.size()}) || package.GetFormat() != FileStream::PackageFormat::V1)
            {
                return Fail(error, "パッケージを解析できません: PackageParseFailed");
            }
            FileStream::PackageEntry entry;
            if (!package.FindEntry(entryName, Asset::CookedGrassFieldFormatV0::PackageEntryType, entry))
            {
                return Fail(error, "Gfd0 の entry がありません: EntryNotFound");
            }
            const auto blob = package.OpenEntry(entry);
            const auto parsed = Asset::ParseCookedGrassField(blob);
            if (!parsed.Succeeded())
            {
                error = "草の場を解析できません: ";
                error += Asset::GetCookedGrassFieldParseStatusName(parsed.Status);
                return false;
            }
            Input input;
            if (!LoadInput(sourcePath, input, error))
            {
                return false;
            }
            const auto& field = parsed.Field;
            bool bMetadata = field.ChunkSizeMeters == input.Size && field.LayerAResolution == input.AResolution &&
                             field.LayerBResolution == input.BResolution && field.SpeciesNames.size() == input.Names.size();
            if (field.SpeciesNames.size() == input.Names.size())
            {
                for (size_t i = 0; i < input.Names.size(); ++i)
                {
                    bMetadata &= field.SpeciesNames[i] == input.Names[i];
                }
            }
            const uint64_t pixelsPerChunk = static_cast<uint64_t>(input.AResolution) * input.AResolution +
                                            static_cast<uint64_t>(input.BResolution) * input.BResolution;
            VariableArray<int64_t> actualIndices(input.ChunkIndices.size());
            for (auto& index : actualIndices)
            {
                index = -1;
            }
            uint64_t mismatches = 0;
            for (size_t i = 0; i < field.Chunks.size(); ++i)
            {
                const auto& chunk = field.Chunks[i];
                const int64_t x = static_cast<int64_t>(chunk.X) - input.FirstX;
                const int64_t z = static_cast<int64_t>(chunk.Z) - input.FirstZ;
                if (x < 0 || z < 0 || x >= input.CountX || z >= input.CountZ)
                {
                    mismatches += static_cast<uint64_t>(field.LayerAResolution) * field.LayerAResolution +
                                  static_cast<uint64_t>(field.LayerBResolution) * field.LayerBResolution;
                }
                else
                {
                    actualIndices[static_cast<size_t>(z) * input.CountX + static_cast<size_t>(x)] = static_cast<int64_t>(i);
                }
            }
            for (uint32_t z = 0; z < input.CountZ; ++z)
            {
                for (uint32_t x = 0; x < input.CountX; ++x)
                {
                    const size_t cell = static_cast<size_t>(z) * input.CountX + x;
                    bool bExpected = input.ChunkIndices[cell] >= 0;
                    if (input.bPng)
                    {
                        // 省略の正当性も、チャンク生成の結果から独立して元の密度 PNG で確かめる。
                        bExpected = false;
                        for (uint32_t row = 0; row < input.AResolution; ++row)
                        {
                            for (uint32_t col = 0; col < input.AResolution; ++col)
                            {
                                const auto& image = input.Images[0];
                                bExpected |= image.Pixels[static_cast<size_t>(z * input.AResolution + row) * image.Width +
                                                          x * input.AResolution + col] != 0;
                            }
                        }
                    }
                    const int64_t actual = actualIndices[cell];
                    if (actual < 0 && !bExpected)
                    {
                        continue;
                    }
                    if (actual < 0 || !bExpected || field.LayerAResolution != input.AResolution || field.LayerBResolution != input.BResolution)
                    {
                        mismatches += pixelsPerChunk;
                        continue;
                    }
                    const auto a = field.GetLayerA(static_cast<uint32_t>(actual));
                    const auto b = field.GetLayerB(static_cast<uint32_t>(actual));
                    if (input.bPng)
                    {
                        mismatches += ComparePngChunk(input, x, z, a, b);
                    }
                    else
                    {
                        const auto& expected = input.Chunks[static_cast<size_t>(input.ChunkIndices[cell])];
                        mismatches += CompareLayer(a, {expected.A.data(), expected.A.size()});
                        mismatches += CompareLayer(b, {expected.B.data(), expected.B.size()});
                    }
                }
            }
            const bool bPassed = bMetadata && mismatches == 0;
            // 既存の単体 cook の cooked_hash は entry 全体の FNV-1a64。
            const uint64_t packageHash = Asset::ComputeAssetPackagePayloadHash(blob.GetData(), blob.GetSize());
            std::cout << "GRASS_FIELD_CHECK result=" << (bPassed ? "pass" : "fail") << " chunks=" << field.Chunks.size()
                      << " mismatched_pixels=" << mismatches << " payload_hash=" << Asset::FormatAssetHashHex(field.PayloadHash).c_str()
                      << " package_hash=" << Asset::FormatAssetHashHex(packageHash).c_str() << "\n";
            return bPassed;
        }
    } // namespace

    bool CookGrassFieldSpecFile(const std::filesystem::path& path, GrassFieldCookResult& out, AnsiString& error)
    {
        out = {};
        error.clear();
        try
        {
            Input input;
            if (!LoadInput(path, input, error))
            {
                return false;
            }
            Asset::CookedGrassFieldWriteDesc desc;
            desc.ChunkSizeMeters = input.Size;
            desc.LayerAResolution = input.AResolution;
            desc.LayerBResolution = input.BResolution;
            desc.SpeciesNames = input.Names;
            for (const auto& chunk : input.Chunks)
            {
                desc.Chunks.push_back({chunk.X, chunk.Z, {chunk.A.data(), chunk.A.size()}, {chunk.B.data(), chunk.B.size()}});
            }
            GrassFieldCookResult result;
            const auto status = Asset::WriteCookedGrassField(desc, result.Bytes);
            if (status != Asset::CookedGrassFieldWriteStatus::Success)
            {
                error = "草の場を書き出せません: CookedGrassFieldWriteStatus=";
                error += AnsiString(std::to_string(static_cast<unsigned>(status)).c_str());
                return false;
            }
            const auto parsed = Asset::ParseCookedGrassField(Asset::AssetBlob::CopyBytes({result.Bytes.data(), result.Bytes.size()}));
            if (!parsed.Succeeded())
            {
                error = "書き出した草の場を解析できません: ";
                error += Asset::GetCookedGrassFieldParseStatusName(parsed.Status);
                return false;
            }
            result.SourceHash = input.SourceHash;
            result.PayloadHash = parsed.Field.PayloadHash;
            result.ChunkCount = static_cast<uint32_t>(input.Chunks.size());
            result.SpeciesCount = static_cast<uint32_t>(input.Names.size());
            out = std::move(result);
            return true;
        }
        catch (const std::exception&)
        {
            return Fail(error, "草の場を構築できません。入力の寸法・パスと利用可能なメモリを確認してください");
        }
    }

    bool RunGrassFieldCheckCommand(int argc, char** argv, int& exitCode)
    {
        if (argc < 2 || std::string_view(argv[1]) != "--check-grass-field")
        {
            return false;
        }
        exitCode = 1;
        AnsiString error;
        try
        {
            std::filesystem::path package, source;
            AnsiString entry;
            for (int i = 2; i < argc; ++i)
            {
                const std::string_view option(argv[i]);
                if (i + 1 >= argc || (option != "--package" && option != "--entry" && option != "--source"))
                {
                    error = "照合には --package <pkg> --entry <名前> --source <配置.json> を指定してください";
                    break;
                }
                const char* value = argv[++i];
                if (option == "--package" && package.empty())
                {
                    package = value;
                }
                else if (option == "--source" && source.empty())
                {
                    source = value;
                }
                else if (option == "--entry" && entry.empty())
                {
                    entry = value;
                }
                else
                {
                    error = "照合の引数が重複しています";
                    break;
                }
            }
            if (error.empty() && (package.empty() || source.empty() || entry.empty()))
            {
                error = "照合には --package・--entry・--source が必要です";
            }
            if (error.empty() && Check(package, entry, source, error))
            {
                exitCode = 0;
            }
        }
        catch (const std::exception&)
        {
            error = "草の場を照合できません。入力の寸法・パスと利用可能なメモリを確認してください";
        }
        if (!error.empty())
        {
            std::cout << "GRASS_FIELD_CHECK result=fail chunks=0 mismatched_pixels=0 payload_hash=0000000000000000 package_hash=0000000000000000\n";
            std::cerr << "AssetCook error: " << error.c_str() << "\n";
        }
        return true;
    }
} // namespace NorvesLib::Tools::AssetCook
