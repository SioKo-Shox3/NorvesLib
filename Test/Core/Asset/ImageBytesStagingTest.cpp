#include "Resource/ModelStaging.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <charconv>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core;
namespace Staging = NorvesLib::Core::Resource::ModelStaging;

namespace
{
    constexpr uint8_t Png[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,13,73,68,65,84,120,156,99,72,153,118,226,63,0,5,230,2,194,63,121,106,233,0,0,0,0,73,69,78,68,174,66,96,130};
    Container::String CorePath(const std::filesystem::path& path)
    {
#if defined(UNICODE)
        return Container::String(path.c_str());
#else
        return Container::String(path.generic_string().c_str());
#endif
    }
    struct Fixture
    {
        std::filesystem::path Root;
        Fixture()
        {
            char number[64] = {};
            const auto converted = std::to_chars(number, number + sizeof(number),
                std::chrono::steady_clock::now().time_since_epoch().count());
            assert(converted.ec == std::errc{});
            const Container::AnsiString name = Container::AnsiString("NorvesImageStaging-") +
                Container::AnsiString(Container::AnsiStringView(number, static_cast<size_t>(converted.ptr - number)));
            Root = std::filesystem::temp_directory_path() / std::filesystem::path(name.begin(), name.end());
            assert(std::filesystem::create_directory(Root));
            std::ofstream file(Root / "pixel.png", std::ios::binary);
            file.write(reinterpret_cast<const char*>(Png), sizeof(Png));
            file.flush();
            assert(file.good());
        }
        ~Fixture()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }
    };
    void Same(const Staging::StagedTextureData& a, const Staging::StagedTextureData& b)
    {
        assert(a.Width == b.Width && a.Height == b.Height && a.Format == b.Format);
        assert(a.PixelData.size() == b.PixelData.size() && !a.PixelData.empty());
        assert(std::memcmp(a.PixelData.data(), b.PixelData.data(), a.PixelData.size()) == 0);
        assert(a.HasLoosePixelData() && b.HasLoosePixelData() && !a.bHasPreparedTexture && !b.bHasPreparedTexture);
    }
} // namespace

int main()
{
    Fixture fixture;
    Staging::TextureReference reference;
    reference.ResolvedFallbackPath = CorePath(fixture.Root / "pixel.png");
    Staging::StagedTextureData fileTexture, bytesTexture;
    assert(Staging::StageStandardTexture(reference, "pixel", fileTexture, "test", 0));
    assert(Staging::StageStandardTextureBytes(Png, "pixel", bytesTexture, "test", 0));
    Same(fileTexture, bytesTexture);
    assert(bytesTexture.Width == 1 && bytesTexture.Height == 1 && bytesTexture.PixelData.size() == 4);
    assert(bytesTexture.PixelData[0] == 100 && bytesTexture.PixelData[1] == 150 && bytesTexture.PixelData[2] == 200 && bytesTexture.PixelData[3] == 255);
    assert(bytesTexture.Format == Rendering::TextureCreateInfo::Format::RGBA8_UNORM);
    assert(bytesTexture.PixelData.data() != Png);
    Staging::StagedTextureData fileAo, fileRoughness, fileMetal, ao, roughness, metal;
    assert(Staging::StageArmTextures(reference, "pixel", fileAo, fileRoughness, fileMetal, "test", 0));
    assert(Staging::StageArmTextureBytes(Png, "pixel", ao, roughness, metal, "test", 0));
    Same(fileAo, ao); Same(fileRoughness, roughness); Same(fileMetal, metal);
    assert(ao.PixelData.size() == 1 && ao.PixelData[0] == 100 && roughness.PixelData[0] == 150 && metal.PixelData[0] == 200);
    assert(ao.Format == Rendering::TextureCreateInfo::Format::R8_UNORM && roughness.Format == ao.Format && metal.Format == ao.Format);

    const uint8_t jpegPrefix[] = {255, 216, 255};
    const uint8_t wrong[] = {1, 2, 3};
    const Container::Span<const uint8_t> invalid[] = {{}, {nullptr, sizeof(Png)}, {Png, 8}, jpegPrefix, wrong,
        {Png, static_cast<size_t>(std::numeric_limits<int>::max()) + 1}};
    const auto standardBefore = bytesTexture.PixelData.data();
    const auto aoBefore = ao.PixelData.data();
    for (const auto bytes : invalid)
    {
        assert(!Staging::StageStandardTextureBytes(bytes, "invalid", bytesTexture, "test", 0));
        assert(bytesTexture.PixelData.data() == standardBefore);
        Same(fileTexture, bytesTexture);
        assert(!Staging::StageArmTextureBytes(bytes, "invalid", ao, roughness, metal, "test", 0));
        assert(ao.PixelData.data() == aoBefore);
        Same(fileAo, ao); Same(fileRoughness, roughness); Same(fileMetal, metal);
    }
    assert(!Staging::StageArmTextureBytes(Png, "alias", ao, ao, metal, "test", 0));
    Same(fileAo, ao); Same(fileMetal, metal);
    std::cout << "ImageBytesStagingTest PASS: file_bytes_owned_rgba_arm_failure_alias\n";
    return 0;
}
