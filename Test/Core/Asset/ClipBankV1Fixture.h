#pragma once
// 新v1試験の独立入力。既存fixture/goldenを書き換えない。
#include "M9LooseFixture.h"
#include "Tools/AssetCook/RigClipBankCook.h"
#include "Animation/ClipBankBinding.h"
#include "Resource/ImportSettingsFile.h"
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#define RIG_CHECK(x)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            std::fprintf(stderr, "Rig v1 %s:%d %s\n", __FILE__, __LINE__, #x);                                         \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
namespace NorvesLib::Tests::RigV1Fixture
{
    namespace C = Core::Container;
    namespace S = Core::Skeletal;
    using Bytes = C::VariableArray<uint8_t>;
    using Text = C::AnsiString;
    inline C::Span<const uint8_t> View(const Text& s)
    {
        return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
    }
    inline C::Span<const uint8_t> View(const Bytes& b)
    {
        return {b.data(), b.size()};
    }
    inline uint32_t U32(const Bytes& b, size_t o)
    {
        return uint32_t(b[o]) | (uint32_t(b[o + 1]) << 8) | (uint32_t(b[o + 2]) << 16) | (uint32_t(b[o + 3]) << 24);
    }
    inline uint64_t U64(const Bytes& b, size_t o)
    {
        return U32(b, o) | (uint64_t(U32(b, o + 4)) << 32);
    }
    inline void Put32(Bytes& b, size_t o, uint32_t v)
    {
        for (unsigned n = 0; n < 4; ++n)
        {
            b[o + n] = uint8_t(v >> (8 * n));
        }
    }
    inline void Put64(Bytes& b, size_t o, uint64_t v)
    {
        Put32(b, o, uint32_t(v));
        Put32(b, o + 4, uint32_t(v >> 32));
    }
    inline void Float(Bytes& b, size_t o, float v)
    {
        Put32(b, o, std::bit_cast<uint32_t>(v));
    }
    inline uint64_t Hash(C::Span<const uint8_t> bytes)
    {
        uint64_t h = 14695981039346656037ull;
        for (uint8_t c : bytes)
        {
            h = (h ^ c) * 1099511628211ull;
        }
        return h;
    }
    inline void Reseal(Bytes& b)
    {
        Put64(b, 48, Hash({b.data() + 256, b.size() - 256}));
    }
    inline size_t SectionOffset(const Bytes& b, size_t section)
    {
        return size_t(U64(b, 256 + section * 32 + 8));
    }
    inline Text Replace(Text text, const char* needle, const Text& value)
    {
        const C::AnsiStringView key(needle);
        const auto at = text.find(key);
        RIG_CHECK(at != Text::npos);
        return text.substr(0, at) + value + text.substr(at + key.size());
    }
    inline Bytes Buffer()
    {
        return AssetFixtures::BuildM9LooseBuffer<Bytes>(
            Float,
            [](Bytes& b, size_t o, uint16_t v)
            {
                b[o] = uint8_t(v);
                b[o + 1] = uint8_t(v >> 8);
            },
            [](Bytes& b, size_t o, float y)
            {
                for (size_t i = 0; i < 16; ++i)
                {
                    Float(b, o + i * 4, i % 5 == 0 ? 1.f : 0.f);
                }
                Float(b, o + 52, y);
            });
    }
    inline Bytes ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        RIG_CHECK(file);
        const auto size = file.tellg();
        RIG_CHECK(size >= 0);
        Bytes bytes(static_cast<size_t>(size));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        RIG_CHECK(file);
        return bytes;
    }
    inline void WriteBytes(const std::filesystem::path& path, const Bytes& bytes)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        RIG_CHECK(file);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file.close();
        RIG_CHECK(!file.fail());
    }
    inline Text BaseJson(const char* name = "ValidU8Float.gltf")
    {
        auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
        auto bytes = ReadBytes(root / "Assets/Models/M9Skinned" / name);
        Text result;
        for (uint8_t c : bytes)
        {
            if (c != '\r' && c != '\n' && c != ' ')
            {
                result += char(c);
            }
        }
        return result;
    }
    struct Fixture
    {
        std::filesystem::path OldDirectory, Root;
        Text Json = BaseJson();
        Bytes Binary = Buffer();
        Fixture()
        {
            OldDirectory = std::filesystem::current_path();
            char label[80];
            std::snprintf(label, sizeof(label), "norves-rig-v1-%llu",
                          static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
            Root = std::filesystem::temp_directory_path() / label;
            RIG_CHECK(std::filesystem::create_directory(Root));
            RIG_CHECK(std::filesystem::create_directory(Root / "RigV1Fixture"));
            std::filesystem::current_path(Root);
            WriteBytes("RigV1Fixture/fixture.bin", Binary);
        }
        ~Fixture()
        {
            std::filesystem::current_path(OldDirectory);
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }
        void SetBuffer(const Bytes& bytes)
        {
            WriteBytes("RigV1Fixture/fixture.bin", bytes);
        }
        S::RigAuthoringCpu Import(const Text& text, const char* label = "RigV1Fixture/rig.gltf",
                                  const Core::AssetImport::LoadedImportSettings* settings = nullptr)
        {
            S::RigAuthoringCpu out;
            S::RigV1Report report;
            const bool ok = S::DecodeRigAuthoringNativePath(View(text), label, out, report, {}, settings);
            if (!ok)
            {
                std::fprintf(stderr, "Import rig status=%u decode=%u\n", unsigned(report.Status),
                             unsigned(report.DecodeStatus));
            }
            RIG_CHECK(ok);
            return out;
        }
        S::ClipBankV1 Bank(const Text& text, Bytes* outBytes = nullptr)
        {
            Tools::AssetCook::RigClipBankCookResult cooked;
            S::RigV1Report report;
            RIG_CHECK(Tools::AssetCook::CookRigClipBankV1NativePath(View(text), "RigV1Fixture/rig.gltf",
                                                                    "nvskel.v1.clips", cooked, report));
            WriteBytes("bank.nvclip", cooked.Bytes);
            auto disk = ReadBytes("bank.nvclip");
            RIG_CHECK(disk == cooked.Bytes);
            S::ClipBankV1 bank;
            RIG_CHECK(S::ParseClipBankV1(View(disk), bank, report));
            if (outBytes)
            {
                *outBytes = std::move(disk);
            }
            return bank;
        }
    };
    inline Text RootTrs(const Text& json, const Text& fields)
    {
        return Replace(json, "\"name\":\"Root\",", Text("\"name\":\"Root\",") + fields);
    }
    inline Text ChildTrs(const Text& json, const Text& fields)
    {
        return Replace(json, "\"translation\":[0,1,0]", fields);
    }
    inline Text AllTranslation(const Text& json)
    {
        return Replace(json, "\"channels\":[",
                       Text("\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}},"));
    }
    inline Bytes SwappedBuffer(const Bytes& input)
    {
        auto result = input;
        for (size_t i = 96; i < 108; ++i)
        {
            result[i] = uint8_t(1 - result[i]);
        }
        for (size_t i = 0; i < 64; ++i)
        {
            result[224 + i] = input[288 + i];
            result[288 + i] = input[224 + i];
        }
        return result;
    }
    inline Math::Matrix4x4 MeshMatrix(const Core::SkinnedMeshResource& mesh)
    {
        const auto& m = mesh.GetMeshNodeGlobalTransform();
        return {m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]};
    }
} // namespace NorvesLib::Tests::RigV1Fixture
