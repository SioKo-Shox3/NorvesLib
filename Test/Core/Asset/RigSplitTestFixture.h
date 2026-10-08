#pragma once
// split cook/loader試験だけの実file fixture。B1/goldenには触らない。
#include "ClipBankV1Fixture.h"
#include "Tools/AssetCook/RigSplitCook.h"
#include "Resource/RigSplitAssetLoader.h"
namespace NorvesLib::Tests::RigSplitFixture
{
    namespace F = RigV1Fixture;
    namespace Cook = Tools::AssetCook;
    namespace C = Core::Container;
    namespace S = Core::Skeletal;
    using Bytes = F::Bytes;
    using ByteView = C::Span<const uint8_t>;
    inline void U32(Bytes& bytes, uint32_t value, bool big = false)
    {
        for (unsigned i = 0; i < 4; ++i)
        {
            bytes.push_back(static_cast<uint8_t>(value >> (8 * (big ? 3 - i : i))));
        }
    }
    inline uint32_t Crc(ByteView bytes)
    {
        uint32_t value = UINT32_MAX;
        for (uint8_t c : bytes)
        {
            value ^= c;
            for (unsigned bit = 0; bit < 8; ++bit)
            {
                value = (value >> 1) ^ ((value & 1) ? 0xedb88320u : 0u);
            }
        }
        return ~value;
    }
    inline void Chunk(Bytes& png, const char* type, ByteView data)
    {
        U32(png, static_cast<uint32_t>(data.size()), true);
        const auto start = png.size();
        png.insert(png.end(), type, type + 4);
        png.insert(png.end(), data.begin(), data.end());
        U32(png, Crc({png.data() + start, png.size() - start}), true);
    }
    inline Bytes Png(uint32_t width, uint32_t height, ByteView pixels)
    {
        RIG_CHECK(pixels.size() == static_cast<size_t>(width) * height * 4);
        Bytes result{137, 80, 78, 71, 13, 10, 26, 10}, header;
        U32(header, width, true);
        U32(header, height, true);
        for (uint8_t c : {8, 6, 0, 0, 0})
        {
            header.push_back(c);
        }
        Chunk(result, "IHDR", header);
        Bytes filtered;
        for (size_t y = 0; y < height; ++y)
        {
            filtered.push_back(0);
            const auto begin = pixels.data() + y * width * 4;
            filtered.insert(filtered.end(), begin, begin + width * 4);
        }
        RIG_CHECK(filtered.size() <= 65535);
        const auto length = static_cast<uint16_t>(filtered.size());
        Bytes zlib{0x78,
                   0x01,
                   1,
                   static_cast<uint8_t>(length),
                   static_cast<uint8_t>(length >> 8),
                   static_cast<uint8_t>(~length),
                   static_cast<uint8_t>((~length) >> 8)};
        zlib.insert(zlib.end(), filtered.begin(), filtered.end());
        uint32_t a = 1, b = 0;
        for (uint8_t c : filtered)
        {
            a = (a + c) % 65521;
            b = (b + a) % 65521;
        }
        U32(zlib, (b << 16) | a, true);
        Chunk(result, "IDAT", zlib);
        Chunk(result, "IEND", {});
        return result;
    }
    inline Cook::RigSplitCookRequest Request()
    {
        Cook::RigSplitCookRequest r;
        r.SourcePath = "RigV1Fixture/rig.gltf";
        r.SkeletonPath = "Models/Rig.nvskel";
        r.MeshPath = "Models/Mesh.nvskel";
        r.BankPath = "Animations/Wave.nvclip";
        return r;
    }
    inline Cook::RigSplitCookResult CookSource(const F::Text& json,
                                               const Cook::RigSplitCookRequest& request = Request())
    {
        Cook::RigSplitCookResult out;
        S::RigV1Report report;
        F::Text error;
        const bool ok = Cook::CookRigSplitV1NativePath(F::View(json), request, out, report, error);
        if (!ok)
        {
            std::fprintf(stderr, "Split cook status=%u error=%s\n", unsigned(report.Status), error.c_str());
        }
        RIG_CHECK(ok);
        return out;
    }
    inline void WriteEntry(const Cook::RigSplitCookEntry& entry)
    {
        const std::filesystem::path path(entry.Reference.CookedPackage.c_str());
        std::filesystem::create_directories(path.parent_path());
        F::WriteBytes(path, entry.Package);
    }
    inline void WritePackages(const Cook::RigSplitCookResult& cooked)
    {
        WriteEntry(cooked.Skeleton);
        WriteEntry(cooked.Mesh);
        WriteEntry(cooked.Bank);
    }
    inline Core::ResourceIO::RigSplitLoadPlan LoadPlan(const F::Fixture& f, const F::Text& json)
    {
        auto assets = C::MakeShared<Core::Asset::AssetSystem>(C::AnsiString(f.Root.generic_string().c_str()));
        RIG_CHECK(assets->LoadManifestFromJsonText(C::String(json.c_str())));
        Core::ResourceIO::RigSplitLoadPlan plan;
        plan.Assets = std::move(assets);
        const auto r = Request();
        plan.SkeletonPath = r.SkeletonPath;
        plan.MeshPath = r.MeshPath;
        plan.BankPaths.push_back(r.BankPath);
        return plan;
    }
    inline F::Text AddMaterial(const F::Text& base, const F::Text& material)
    {
        auto json = F::Replace(base, "\"mode\":4", "\"mode\":4,\"material\":0");
        return F::Replace(json, "\"skins\":", F::Text("\"materials\":[") + material + "],\"skins\":");
    }
    inline void Sidecar(const F::Text& text)
    {
        F::Bytes bytes;
        bytes.insert(bytes.end(), text.begin(), text.end());
        F::WriteBytes("RigV1Fixture/rig.gltf.import.json", bytes);
    }
} // namespace NorvesLib::Tests::RigSplitFixture
